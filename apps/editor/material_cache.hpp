// ToolEngine Editor - cache dos materiais do projeto (nome -> MaterialAsset) e a regra que aplica o material ao desenho.
// O disco e lido UMA vez por material; depois so um poll por segundo confere se o arquivo mudou (nunca por objeto/quadro).
#pragma once
#include "components.hpp"
#include "material_asset.hpp"
#include "tool/render/soft_renderer.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

namespace editor {

class MaterialCache {
public:
  // Procura o material `name` do projeto `root`. nullptr se nao existe (aviso no log so na primeira vez).
  const material_asset::MaterialAsset* find(const std::string& root, const std::string& name) {
    if (name.empty()) return nullptr;
    if (root != root_) { entries_.clear(); root_ = root; }   // outro projeto: descarta tudo
    auto it = entries_.find(name);
    if (it == entries_.end()) { it = entries_.emplace(name, Entry{}).first; reload(it->first, it->second); }
    return it->second.ok ? &it->second.asset : nullptr;
  }

  // Chamar uma vez por quadro: no maximo 1x por segundo confere a data dos arquivos ja carregados e recarrega o que mudou.
  void poll(const std::string& root) {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastPoll_ < std::chrono::seconds(1)) return;
    lastPoll_ = now;
    if (root != root_) return;
    for (auto& [name, e] : entries_) {
      std::error_code ec;
      const fs::path p = material_asset::pathFor(root_, name);
      const bool exists = fs::exists(p, ec);
      const auto stamp = exists ? fs::last_write_time(p, ec) : fs::file_time_type{};
      if (exists != e.ok || (exists && stamp != e.stamp)) reload(name, e);   // apareceu, sumiu ou foi editado
    }
  }

  // Esquece tudo (o Inspector salvou ou criou um material): a proxima consulta le o arquivo de novo.
  void invalidate() { entries_.clear(); }

private:
  struct Entry { material_asset::MaterialAsset asset; bool ok = false; fs::file_time_type stamp{}; bool warned = false; };

  void reload(const std::string& name, Entry& e) {
    const std::string path = material_asset::pathFor(root_, name);
    std::string err;
    material_asset::MaterialAsset m;
    if (material_asset::load(path, m, err)) {
      e.asset = std::move(m); e.ok = true; e.warned = false;
      std::error_code ec; e.stamp = fs::last_write_time(path, ec);
      if (!err.empty()) tool::logWarn("Material '" + name + "': " + err);
    } else {
      e.ok = false; e.stamp = {};
      if (!e.warned) { e.warned = true; tool::logWarn("Material '" + name + "' not found; using the object's own values."); }   // uma vez so
    }
  }

  std::unordered_map<std::string, Entry> entries_;
  std::string root_;
  std::chrono::steady_clock::time_point lastPoll_{};
};

// Aplica o material `a` sobre o tool::Material `m` ja montado com os campos locais do MeshRenderer.
// Regras de mistura (o renderer so tem opaco e transparente por alfa):
//   Opaque   -> opaco (opacity = 1).
//   Alpha    -> opacity = albedo[3]; abaixo de 1 vai para o passe transparente.
//   Additive -> tratado como Alpha (o renderer nao tem mistura aditiva; o Inspector avisa).
//   shader "unlit" -> tratado como "pbr" (nao ha pipeline sem luz; o Inspector avisa).
// Emissao do material soma a emissao local (que ja esta em m.emissive, 0 se "Enable emission" estiver desligado).
// A textura nao entra aqui: use textureOf().
inline void applyMaterialAsset(tool::Material& m, const material_asset::MaterialAsset& a) {
  m.albedo = {a.albedo[0], a.albedo[1], a.albedo[2]};
  m.roughness = a.roughness;
  m.metallic = a.metallic;
  m.emissive = m.emissive + tool::Vec3{a.emissive[0], a.emissive[1], a.emissive[2]} * a.emissiveStrength;
  m.opacity = a.blend == material_asset::Blend::Opaque ? 1.f : a.albedo[3];
  // Campos avancados: tiling/offset de UV, forca do normal map, dupla face e alpha cutoff (so em Alpha; Opaque/Additive = 0).
  // A textura do normal map (ponteiro) e resolvida por quem tem a biblioteca de texturas (normalMapOf()).
  m.uvTiling[0] = a.uvTiling[0]; m.uvTiling[1] = a.uvTiling[1];
  m.uvOffset[0] = a.uvOffset[0]; m.uvOffset[1] = a.uvOffset[1];
  m.normalStrength = a.normalStrength;
  m.doubleSided = a.doubleSided;
  m.alphaCutoff = a.blend == material_asset::Blend::Alpha ? a.alphaCutoff : 0.f;
}

// Caminho do normal map do material (vazio = sem). So o material tem normal map; o MeshRenderer nao.
inline const char* normalMapOf(const material_asset::MaterialAsset* a) { return a ? a->normalMap.c_str() : ""; }

// Textura (caminho relativo ao projeto) a usar: a do material, ou a do MeshRenderer se o material nao tem. Pode ser vazia.
inline const char* textureOf(const MeshRenderer& mr, const material_asset::MaterialAsset* a) {
  return (a && !a->texture.empty()) ? a->texture.c_str() : mr.texture;
}

} // namespace editor
