// Estado e pedidos do host da janela Assets: importacao, renomear, apagar, cenas e fila de importacao.
// O desenho e a janela SpriteUI: assets_spriteui.hpp. Regras puras: assets_model.hpp. Eventos: assets_sprite_panel.hpp.
#pragma once
#include "editor_state.hpp"
#include "extra_ui.hpp"
#include "i18n.hpp"
#include "script_complete.hpp"


#include <mutex>

namespace editor {

using editor::F;
using editor::T;
using editor::W;

// Arquivos pedidos para importar (botao Import / arrastar e soltar). O dialogo do SDL responde em outra thread.
inline std::mutex& importMutex() { static std::mutex m; return m; }
inline std::vector<std::string>& importQueue() { static std::vector<std::string> q; return q; }
inline void queueImport(const std::string& path) { std::lock_guard lk(importMutex()); importQueue().push_back(path); }

struct AssetsUi {
  std::string folder = "Assets";   // pasta atual, relativa ao projeto
  std::string selected;            // caminho relativo do item selecionado (a janela Assets em SpriteUI escreve aqui)
};
inline AssetsUi& ui() { static AssetsUi u; return u; }

// Revisao da pasta Assets: cada operacao de arquivo chama touchAssets(), e os caches recarregam.
inline unsigned& assetsRevision() { static unsigned r = 0; return r; }
inline void touchAssets() { ++assetsRevision(); }

inline const char* kindLabel(AssetKind k) {
  switch (k) {
    case AssetKind::Folder: return "Folder"; case AssetKind::Model: return "Model"; case AssetKind::Audio: return "Audio";
    case AssetKind::Scene: return "Scene"; case AssetKind::Image: return "Image"; case AssetKind::Text: return "Text"; default: return "File";
  }
}

// Troca "antigo" por "novo" (arquivo ou prefixo de pasta) em todo texto de cena guardado em disco e na cena aberta.
inline void updateReferences(Editor& ed, const std::string& oldRel, const std::string& newRel) {
  auto fix = [&](std::string& s) {
    if (s == oldRel) s = newRel;
    else if (s.size() > oldRel.size() && s.compare(0, oldRel.size(), oldRel) == 0 && s[oldRel.size()] == '/') s = newRel + s.substr(oldRel.size());
  };
  ed.scene.world().each<MeshRenderer>([&](tool::Entity, MeshRenderer& m) { std::string s = m.model; fix(s); ser::copyStr(m.model, sizeof m.model, s); });
  ed.scene.world().each<AudioSource>([&](tool::Entity, AudioSource& a) { std::string s = a.clip; fix(s); ser::copyStr(a.clip, sizeof a.clip, s); });
  for (auto& a : ed.sceneInfo.assets) fix(a);
  // outras cenas (texto): troca "<antigo>" e "<antigo>/..." entre aspas
  for (auto& name : ed.project.listScenes()) {
    if (name == ed.sceneInfo.name) continue;
    const fs::path p = ed.project.scenePath(name); std::string text;
    if (!readTextFile(p, text)) continue;
    auto replaceAll = [&](const std::string& from, const std::string& to) { size_t pos = 0; bool ch = false; while ((pos = text.find(from, pos)) != std::string::npos) { text.replace(pos, from.size(), to); pos += to.size(); ch = true; } return ch; };
    bool changed = replaceAll("\"" + oldRel + "\"", "\"" + newRel + "\"");
    changed = replaceAll("\"" + oldRel + "/", "\"" + newRel + "/") || changed;
    if (changed) writeTextFile(p, text);
  }
  ed.lib.clear();
}

inline bool insideAssets(const Editor& ed, const fs::path& p) {
  std::error_code ec;
  const fs::path rel = fs::weakly_canonical(p, ec).lexically_relative(fs::weakly_canonical(ed.project.assetsDir(), ec));
  return !rel.empty() && *rel.begin() != "..";
}

// Renomeia um arquivo/pasta dentro de Project/Assets e corrige todas as referencias. Retorna "" se deu certo, senao o erro.
inline std::string renameAsset(Editor& ed, const std::string& relOld, const std::string& newName) {
  if (!safeName(newName)) return "Invalid name";
  const fs::path oldp = ed.project.resolve(relOld);
  if (oldp.empty() || !insideAssets(ed, oldp)) return "Path outside the Assets folder";
  const fs::path newp = oldp.parent_path() / newName;
  if (oldp == newp) return "";
  std::error_code ec;
  if (fs::exists(newp, ec)) return "A file with that name already exists";
  fs::rename(oldp, newp, ec);
  if (ec) return "Rename failed: " + ec.message();
  touchAssets();
  const std::string nr = ed.project.relative(newp);
  updateReferences(ed, relOld, nr);
  ed.saveScene();
  ed.statusLine = "Renamed to " + nr;
  return "";
}

// Apaga um arquivo/pasta de Project/Assets (confinado a essa pasta). Retorna "" se deu certo, senao o erro.
inline std::string deleteAsset(Editor& ed, const std::string& rel) {
  const fs::path p = ed.project.resolve(rel);
  if (p.empty() || !insideAssets(ed, p) || fs::path(rel) == "Assets") return "Path outside the Assets folder";
  std::error_code ec;
  fs::remove_all(p, ec);
  touchAssets();
  ed.lib.clear();
  if (ec) return "Delete failed: " + ec.message();
  ed.statusLine = "Deleted " + rel;
  return "";
}

inline void instantiateAsset(Editor& ed, const std::string& rel, bool atTarget = true) {
  const AssetKind k = kindOf(rel, false);
  tool::Entity e;
  if (k == AssetKind::Model) e = ed.makeModel(rel);
  else if (k == AssetKind::Audio) { e = ed.makeAudio(rel); ed.assignAsset(rel); }
  else return;
  if (atTarget) if (auto* t = ed.scene.world().get<Transform>(e)) { t->pos[0] = ed.target.x; t->pos[1] = ed.target.y - 0.5f; t->pos[2] = ed.target.z; }
  ed.selected = e;
}

inline void processImports(Editor& ed) {
  std::vector<std::string> q;
  { std::lock_guard lk(importMutex()); q.swap(importQueue()); }
  if (!q.empty()) touchAssets();
  for (auto& src : q) {
    std::string err;
    const fs::path dst = ed.project.importFile(src, ed.project.root / ui().folder, &err);
    if (dst.empty()) { ed.statusLine = "Import failed: " + err; tool::logError(ed.statusLine); continue; }
    const std::string rel = ed.project.relative(dst);
    ed.statusLine = "Imported: " + rel; tool::logInfo(ed.statusLine);
    const AssetKind k = kindOf(dst, false);
    if (k == AssetKind::Model || k == AssetKind::Audio) ed.assignAsset(rel);
    ui().selected = rel;
  }
}


} // namespace editor
