// Material como asset (material_asset.hpp): ida e volta JSON, clamp, nome, shader, arquivos. Sem janela.
// Roda no ctest como Editor.MaterialAsset. Usa uma pasta temporaria do sistema, apagada no fim.
#include "material_asset.hpp"

#include <cstdio>

namespace {
int run = 0, failed = 0;
void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) { ++failed; std::fprintf(stderr, "[FAIL] linha %d: %s\n", line, expr); }
}
#define CHECK(e) Check((e), #e, __LINE__)
}

int main() {
  namespace ma = editor::material_asset;
  namespace fs = std::filesystem;
  const fs::path tmp = fs::temp_directory_path() / "toolengine_material_asset_test";
  std::error_code ec; fs::remove_all(tmp, ec);
  const std::string dir = tmp.string();
  std::string err;

  // Nome sanitizado.
  CHECK(ma::sanitizeName("a/b:c*") == "a_b_c_");
  CHECK(ma::sanitizeName("  ..  ") == "material");
  CHECK(ma::sanitizeName("").empty() == false);
  CHECK(ma::sanitizeName(std::string(300, 'x')).size() == 100);
  CHECK(fs::path(ma::folder(dir)).filename() == "Materials");
  CHECK(fs::path(ma::pathFor(dir, "Ouro/1")).filename() == "Ouro_1.json");

  // Clamp.
  ma::MaterialAsset w;
  w.roughness = 5.f; w.metallic = -2.f; w.albedo[0] = 3.f; w.albedo[3] = -1.f; w.emissiveStrength = -4.f; w.shader = "xyz";
  const auto c = ma::clamped(w);
  CHECK(c.roughness == 1.f && c.metallic == 0.f && c.albedo[0] == 1.f && c.albedo[3] == 0.f);
  CHECK(c.emissiveStrength == 0.f && c.shader == "pbr");

  // Ida e volta.
  ma::MaterialAsset m;
  m.name = "Ouro"; m.albedo[0] = 0.9f; m.albedo[1] = 0.7f; m.albedo[2] = 0.2f; m.albedo[3] = 0.5f;
  m.roughness = 0.3f; m.metallic = 1.f; m.emissive[1] = 0.25f; m.emissiveStrength = 2.5f;
  m.texture = "Textures/ouro.png"; m.blend = ma::Blend::Additive; m.shader = "unlit"; m.doubleSided = true;
  ma::MaterialAsset r;
  CHECK(ma::fromJson(ma::toJson(m), r, err) && err.empty());
  CHECK(r.name == "Ouro" && r.albedo[0] == 0.9f && r.albedo[3] == 0.5f && r.roughness == 0.3f && r.metallic == 1.f);
  CHECK(r.emissive[1] == 0.25f && r.emissiveStrength == 2.5f && r.texture == "Textures/ouro.png");
  CHECK(r.blend == ma::Blend::Additive && r.shader == "unlit" && r.doubleSided);

  // Chave ausente = padrao; shader desconhecido vira pbr com aviso; JSON invalido = false.
  CHECK(ma::fromJson("{}", r, err) && r.roughness == 0.5f && r.shader == "pbr" && r.blend == ma::Blend::Opaque && !r.doubleSided);
  CHECK(ma::fromJson("{\"shader\":\"magic\",\"roughness\":9}", r, err) && r.shader == "pbr" && !err.empty() && r.roughness == 1.f);
  CHECK(!ma::fromJson("nao e json", r, err) && !err.empty());
  CHECK(!ma::fromJson("[1,2]", r, err));

  // Campos avancados: padroes (material antigo), ida e volta e clamp.
  CHECK(ma::fromJson("{}", r, err) && r.normalMap.empty() && r.normalStrength == 1.f && r.uvTiling[0] == 1.f && r.uvTiling[1] == 1.f &&
        r.uvOffset[0] == 0.f && r.uvOffset[1] == 0.f && r.alphaCutoff == 0.f);
  ma::MaterialAsset adv;
  adv.name = "Tijolo"; adv.normalMap = "Textures/tijolo_n.png"; adv.normalStrength = 1.5f;
  adv.uvTiling[0] = 4.f; adv.uvTiling[1] = 2.f; adv.uvOffset[0] = 0.25f; adv.uvOffset[1] = -0.5f; adv.alphaCutoff = 0.4f;
  ma::MaterialAsset advR;
  CHECK(ma::fromJson(ma::toJson(adv), advR, err));
  CHECK(advR.normalMap == "Textures/tijolo_n.png" && advR.normalStrength == 1.5f && advR.uvTiling[0] == 4.f && advR.uvTiling[1] == 2.f);
  CHECK(advR.uvOffset[0] == 0.25f && advR.uvOffset[1] == -0.5f && advR.alphaCutoff == 0.4f);
  CHECK(ma::fromJson("{\"uvTiling\":[0,-3],\"normalStrength\":9,\"alphaCutoff\":2}", r, err) && r.uvTiling[0] == 1.f && r.uvTiling[1] == 1.f &&
        r.normalStrength == 2.f && r.alphaCutoff == 1.f);                    // tiling <= 0 vira 1; forca 0..2; cutoff 0..1
  CHECK(ma::fromJson("{\"normalStrength\":-1,\"alphaCutoff\":-1}", r, err) && r.normalStrength == 0.f && r.alphaCutoff == 0.f);
  ma::MaterialAsset bad; bad.uvTiling[0] = -5.f; bad.uvTiling[1] = 0.f; bad.alphaCutoff = 7.f;
  const auto bc = ma::clamped(bad);
  CHECK(bc.uvTiling[0] > 0.f && bc.uvTiling[1] > 0.f && bc.alphaCutoff == 1.f);

  // Arquivos: pasta ausente, save/load/list/remove.
  CHECK(ma::listNames(dir).empty());
  CHECK(ma::save(dir, m, err));
  ma::MaterialAsset b; b.name = "Azul";
  CHECK(ma::save(dir, b, err));
  const auto names = ma::listNames(dir);
  CHECK(names.size() == 2 && names[0] == "Azul" && names[1] == "Ouro");
  ma::MaterialAsset l;
  CHECK(ma::load(ma::pathFor(dir, "Ouro"), l, err) && l.shader == "unlit" && l.metallic == 1.f);
  CHECK(!ma::load(ma::pathFor(dir, "Nao existe"), l, err) && !err.empty());
  CHECK(!ma::remove((tmp / "outro.txt").string(), err));                       // nao e .json
  CHECK(!ma::remove((tmp / "fora.json").string(), err));                       // fora de Materials
  CHECK(ma::remove(ma::pathFor(dir, "Ouro"), err));
  CHECK(ma::listNames(dir).size() == 1);

  fs::remove_all(tmp, ec);
  std::printf("%d verificacoes, %d falhas\n", run, failed);
  return failed == 0 ? 0 : 1;
}
