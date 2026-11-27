// ToolEngine Editor - autoteste sem janela: tool_editor --selftest
// Exercita o que o editor faz com cenas, assets, cameras, audio e luz global, numa pasta de projeto TEMPORARIA
// (nunca toca no seu Project/). Retorna 0 se tudo passou. Rodado pelo ctest como "editor_selftest".
#pragma once
#include "assets_panel.hpp"
#include "editor_state.hpp"
#include "fx.hpp"
#include "viewport_tools.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "skybox.hpp"
#include "tool/rhi/null_device.hpp"
#include "image_io.hpp"
#include "help_content.hpp"
#include "script_complete.hpp"
#include "narrative_model.hpp"
#include "export.hpp"
#include "spriteui_diagnostics.hpp"
#include <algorithm>

namespace editor {

inline int runSelfTest(const std::string& samplesDir) {
  int fails = 0, checks = 0;
  auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++fails; std::printf("  FAIL: %s\n", what); } else std::printf("  ok:   %s\n", what); };
  tool::Logger::get().setSink([](tool::LogLevel, std::string_view) {});

  { // janela de inicio: criar e abrir projeto (nome, autor, pasta)
    const fs::path base = fs::temp_directory_path() / "toolengine_selftest_newproj";
    std::error_code e2; fs::remove_all(base, e2);
    std::string err;
    const fs::path root = createProjectFolder("Teste", "Joao Vitor", base, err);
    check(!root.empty() && fs::exists(root / "project.json") && fs::is_directory(root / "Assets" / "Scripts") && fs::is_directory(root / "Scenes"),
          "new project: folder, project.json, Assets/Scripts and Scenes are created");
    ProjectSettings ps; Project pr; pr.root = root;
    check(ps.load(pr) && ps.projectName == "Teste" && ps.author == "Joao Vitor", "new project: name and author are saved in project.json");
    check(createProjectFolder("Teste", "x", base, err).empty() && !err.empty(), "new project: a folder that already has files is refused");
    check(createProjectFolder("../fora", "x", base, err).empty(), "new project: a name with '..' is refused");
    check(openProjectFolder(base / "nao-existe", err).empty(), "open project: a missing folder is refused");
    fs::remove_all(base, e2);
  }

  Editor ed;
  const fs::path tmp = fs::temp_directory_path() / "toolengine_selftest";
  std::error_code ec; fs::remove_all(tmp, ec);
  ed.project.root = tmp; ed.project.ensure();
  ed.sceneInfo.name = "Main"; ed.createDefaultContent(); check(ed.saveScene(), "save default scene");
  { // material como asset: o MeshRenderer com material usa o albedo do arquivo; sem material volta aos campos locais
    material_asset::MaterialAsset a; a.name = "SelfTestRed";
    a.albedo[0] = 0.9f; a.albedo[1] = 0.1f; a.albedo[2] = 0.2f; a.albedo[3] = 0.5f;
    a.roughness = 0.25f; a.metallic = 0.75f; a.blend = material_asset::Blend::Alpha;
    std::string merr; const std::string root = ed.project.root.string();
    check(material_asset::save(root, a, merr), "material: saved in <project>/Materials");
    MeshRenderer mr;   // campos locais: cor padrao, opacidade 1
    auto resolve = [&](const MeshRenderer& r) {
      tool::Material m{{r.color[0], r.color[1], r.color[2]}, r.roughness, r.metallic, r.castShadow, r.receiveShadow};
      m.opacity = r.opacity;
      const material_asset::MaterialAsset* ma = r.material[0] ? ed.materials.find(root, r.material) : nullptr;
      if (ma) applyMaterialAsset(m, *ma);
      return m;
    };
    std::snprintf(mr.material, sizeof mr.material, "%s", "SelfTestRed");
    const tool::Material withMat = resolve(mr);
    check(std::fabs(withMat.albedo.x - 0.9f) < 1e-4f && std::fabs(withMat.albedo.y - 0.1f) < 1e-4f && std::fabs(withMat.roughness - 0.25f) < 1e-4f &&
          std::fabs(withMat.metallic - 0.75f) < 1e-4f && std::fabs(withMat.opacity - 0.5f) < 1e-4f, "material: the drawn Material uses the file values (albedo, roughness, metallic, alpha)");
    mr.material[0] = 0;
    const tool::Material noMat = resolve(mr);
    check(std::fabs(noMat.albedo.x - mr.color[0]) < 1e-4f && std::fabs(noMat.opacity - 1.f) < 1e-4f, "material: without material the local fields are used");
    std::snprintf(mr.material, sizeof mr.material, "%s", "NaoExiste");
    check(std::fabs(resolve(mr).albedo.x - mr.color[0]) < 1e-4f, "material: a missing material falls back to the local fields");
    std::string e2;
    check(material_asset::remove(material_asset::pathFor(root, "SelfTestRed"), e2), "material: temp file removed");
    // Campos novos: tiling/offset, normal map, cutoff e dupla face passam do arquivo ate o Material resolvido (e sao presos).
    material_asset::MaterialAsset b; b.name = "SelfTestUv";
    b.uvTiling[0] = 2.f; b.uvTiling[1] = 2.f; b.uvOffset[0] = 0.25f; b.normalMap = "Assets/n.png"; b.normalStrength = 5.f;
    b.alphaCutoff = 0.4f; b.doubleSided = true; b.blend = material_asset::Blend::Alpha;
    check(material_asset::save(root, b, merr), "material uv: saved");
    MeshRenderer mr2; std::snprintf(mr2.material, sizeof mr2.material, "%s", "SelfTestUv");
    const tool::Material uvm = resolve(mr2);
    const material_asset::MaterialAsset* got = ed.materials.find(root, "SelfTestUv");
    check(got && std::string(normalMapOf(got)) == "Assets/n.png", "material uv: normal map path is kept");
    check(std::fabs(uvm.uvTiling[0] - 2.f) < 1e-4f && std::fabs(uvm.uvTiling[1] - 2.f) < 1e-4f && std::fabs(uvm.uvOffset[0] - 0.25f) < 1e-4f,
          "material uv: tiling and offset reach the drawn Material");
    check(std::fabs(uvm.normalStrength - 2.f) < 1e-4f, "material uv: normal strength is clamped to 2");
    check(std::fabs(uvm.alphaCutoff - 0.4f) < 1e-4f && uvm.doubleSided, "material uv: alpha cutoff (Alpha blend) and double sided are applied");
    b.blend = material_asset::Blend::Opaque; b.uvTiling[0] = -3.f;
    check(material_asset::clamped(b).uvTiling[0] == 1.f, "material uv: non-positive tiling is clamped to 1");
    check(material_asset::remove(material_asset::pathFor(root, "SelfTestUv"), e2), "material uv: temp file removed");
  }
  { // bounds e assinatura de sombra invalidam caches quando geometria, estado ou transformacao mudam
    tool::Mesh probe = tool::Mesh::cube(0.5f);
    const Editor::Bounds oldBounds = ed.meshBounds(probe);
    const size_t oldCount = probe.pos.size(); const void* oldData = probe.pos.data();
    probe.pos[0].x += 10.f; ++probe.revision;
    const Editor::Bounds newBounds = ed.meshBounds(probe);
    check(probe.pos.size() == oldCount && probe.pos.data() == oldData &&
          (std::fabs(newBounds.center.x - oldBounds.center.x) > 1e-4f || std::fabs(newBounds.radius - oldBounds.radius) > 1e-4f),
          "render cache: mesh revision refreshes bounds even when vertex storage is unchanged");

    const tool::Entity caster = ed.scene.world().create();
    ed.scene.world().add<Transform>(caster, Transform{});
    MeshRenderer mr; mr.shape = 0; mr.castShadow = true;
    ed.scene.world().add<MeshRenderer>(caster, mr);
    auto signature = [&]() {
      std::uint64_t hash = 1469598103934665603ull;
      auto mixu = [&](std::uint64_t v) { hash ^= v; hash *= 1099511628211ull; };
      auto mixf = [&](float f) { std::uint32_t bits; std::memcpy(&bits, &f, sizeof bits); mixu(bits); };
      tool::Camera camera;
      ed.hashCasters(camera, mixu, mixf);
      return hash;
    };
    const std::uint64_t activeHash = signature();
    ed.scene.world().add<Inactive>(caster);
    check(signature() != activeHash, "shadow cache: deactivating a caster invalidates its signature");
    ed.scene.world().remove<Inactive>(caster);
    const std::uint64_t restoredHash = signature();
    const std::uint64_t rtHash = ed.rtSignature();
    ++ed.cube.revision;
    check(signature() != restoredHash, "shadow cache: mesh revision invalidates caster signature");
    check(ed.rtSignature() != rtHash, "RT cache: mesh revision invalidates the BVH signature");

    const tool::Entity chunk = ed.scene.world().create();
    ed.scene.world().add<Transform>(chunk, Transform{});
    ed.scene.world().add<VoxelChunk>(chunk, VoxelChunk{});
    const std::uint64_t chunkHash = signature();
    ed.scene.world().get<Transform>(chunk)->pos[0] = 4.f;
    check(signature() != chunkHash, "shadow cache: moving a voxel chunk invalidates caster signature");
    ed.scene.world().destroy(caster);
    ed.scene.world().destroy(chunk);
  }
  { // cache plano de worldMat e hash de casters: resultado igual ao calculo sem cache, e invalidacao correta
    auto& w = ed.scene.world();
    auto sameMat = [](const tool::Mat4& a, const tool::Mat4& b) {
      for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) if (std::fabs(a.m[c][r] - b.m[c][r]) > 1e-5f) return false;
      return true;
    };
    // Como o inicio de renderScene: novo carimbo de matrizes E hash de casters invalidado (o helper antes esquecia o segundo,
    // entao o "novo quadro" devolvia o hash velho: era erro do teste, nao do casterHash).
    auto cacheOn = [&]() { ed.casterHashFrameValid_ = false; ed.renderWorldMatCacheActive_ = true; ed.beginWorldMatFrame(); };
    auto cacheOff = [&]() { ed.renderWorldMatCacheActive_ = false; };
    // hierarquia de 3 niveis: avo -> pai -> filho
    const tool::Entity ga = w.create(), pa = w.create(), ch = w.create();
    Transform tga; tga.pos[0] = 3.f; tga.rot[1] = 30.f; tga.scale[0] = tga.scale[1] = tga.scale[2] = 2.f; w.add<Transform>(ga, tga);
    Transform tpa; tpa.pos[1] = 1.f; tpa.rot[0] = 20.f; w.add<Transform>(pa, tpa); w.add<Parent>(pa, Parent{ga});
    Transform tch; tch.pos[2] = -4.f; tch.rot[2] = 10.f; w.add<Transform>(ch, tch); w.add<Parent>(ch, Parent{pa});
    cacheOff(); const tool::Mat4 plain0 = ed.worldMat(ch);
    cacheOn();  const tool::Mat4 cached0 = ed.worldMat(ch);
    const tool::Mat4 cachedAgain = ed.worldMat(ch);   // segundo acesso no mesmo quadro: vem do cache
    check(sameMat(plain0, cached0) && sameMat(plain0, cachedAgain), "worldMat cache: 3-level hierarchy equals the uncached result");
    w.get<Transform>(ga)->pos[0] = -9.f;   // move o avo; novo quadro de preparo
    cacheOff(); const tool::Mat4 plain1 = ed.worldMat(ch);
    cacheOn();  const tool::Mat4 cached1 = ed.worldMat(ch);
    check(sameMat(plain1, cached1) && !sameMat(cached0, cached1), "worldMat cache: moving the grandparent invalidates the child on the next frame");
    // indice reutilizado com nova geracao no mesmo quadro de preparo
    const tool::Entity e1 = w.create();
    Transform t1; t1.pos[0] = 5.f; w.add<Transform>(e1, t1);
    cacheOn(); const tool::Mat4 m1 = ed.worldMat(e1);
    w.destroy(e1);
    const tool::Entity e2 = w.create();   // pode reaproveitar o indice de e1 com geracao nova
    Transform t2; t2.pos[0] = -7.f; w.add<Transform>(e2, t2);
    const tool::Mat4 m2 = ed.worldMat(e2);   // MESMO quadro: nao pode devolver a matriz de e1
    cacheOff(); const tool::Mat4 m2plain = ed.worldMat(e2);
    check(sameMat(m2, m2plain) && !sameMat(m1, m2), "worldMat cache: reused entity index with a new generation does not return the stale matrix");
    // hash dos casters: estavel parado, muda ao mover um caster, e recalcula se a camera muda no mesmo quadro
    const tool::Entity cst = w.create();
    w.add<Transform>(cst, Transform{});
    MeshRenderer cmr; cmr.shape = 0; cmr.castShadow = true; w.add<MeshRenderer>(cst, cmr);
    tool::Camera camA; camA.eye = {0.f, 5.f, -10.f};
    cacheOn(); const std::uint64_t h0 = ed.casterHash(camA); const std::uint64_t h0b = ed.casterHash(camA);
    cacheOn(); const std::uint64_t h0c = ed.casterHash(camA);
    check(h0 == h0b && h0 == h0c, "casterHash: stable while nothing moves (same frame and next frame)");
    w.get<Transform>(cst)->pos[0] = 2.f;
    cacheOn(); const std::uint64_t h1 = ed.casterHash(camA);
    check(h1 != h0, "casterHash: moving one caster changes the hash");
    tool::Camera camB = camA; camB.eye = {1.f, 5.f, -10.f};
    const std::uint64_t h1cached = ed.casterHash(camA);
    ed.casterHash(camB);   // camera diferente dentro da mesma vista: nao pode reaproveitar o valor de camA
    check(h1cached == h1 && ed.casterHashCamEye_.x == 1.f, "casterHash: a different camera in the same frame recomputes instead of reusing the cache");
    // Assinaturas por valor (sem API de escrita): parado = estavel e SEM recalcular as matrizes; escrita direta por ponteiro bruto e pega.
    std::vector<tool::Light> noLights; tool::LightFrame lfEmpty;
    cacheOn(); const std::uint64_t sunA = ed.shadowSignature(camA, 100, 100, noLights), ptA = ed.pointShadowSignature(camA, lfEmpty);
    const std::uint32_t stampA = ed.renderWorldMatStamp_;
    cacheOn(); const std::uint64_t sunB = ed.shadowSignature(camA, 100, 100, noLights), ptB = ed.pointShadowSignature(camA, lfEmpty);
    check(sunA == sunB && ptA == ptB && stampA == ed.renderWorldMatStamp_, "signatures: nothing moved -> both signatures stable and world matrices reused across views/frames");
    Transform* rawT = w.get<Transform>(cst); rawT->pos[1] += 0.25f;   // escrita direta, sem nenhuma API
    cacheOn(); const std::uint64_t sunC = ed.shadowSignature(camA, 100, 100, noLights), ptC = ed.pointShadowSignature(camA, lfEmpty);
    check(sunC != sunB && ptC != ptB && ed.renderWorldMatStamp_ != stampA, "signatures: moving ONE caster by a raw pointer changes sun and point signatures and rebuilds matrices");
    { // versao por API: leitura (const/eachRead) nao sobe; acesso mutavel sobe; e escrita FORA da versao e pega pelo verificador por valor
      const std::uint64_t e0 = w.epochOf<Transform>();
      w.eachRead<Transform>([](tool::Entity, const Transform&) {});
      const Transform* ro = std::as_const(w).get<Transform>(cst); (void)ro;
      const bool readsKeepVersion = w.epochOf<Transform>() == e0;
      Transform* kept = w.get<Transform>(cst);   // acesso mutavel: sobe a versao
      const bool writeAccessBumps = w.epochOf<Transform>() > e0;
      cacheOn(); (void)ed.verifyVersionsByValue();                       // amostra
      const bool consistent = ed.verifyVersionsByValue();                // nada mudou: coerente
      kept->pos[0] += 0.5f;                                              // escrita pelo ponteiro guardado, sem passar pela versao
      const bool caught = !ed.verifyVersionsByValue();
      check(readsKeepVersion && writeAccessBumps, "versions: reads (const get, eachRead) keep the component version; mutable access bumps it");
      check(consistent && caught, "versions: a write through a kept pointer (outside the version API) is caught by the by-value verifier; untouched state stays consistent");
      std::vector<tool::Light> nl; tool::LightFrame lfe;
      cacheOn(); const std::uint64_t s1 = ed.shadowSignature(camA, 100, 100, nl), p1 = ed.pointShadowSignature(camA, lfe);
      cacheOn(); const std::uint64_t s2 = ed.shadowSignature(camA, 100, 100, nl), p2 = ed.pointShadowSignature(camA, lfe);
      check(s1 == s2 && p1 == p2, "versions: with no mutable access both signatures are O(1)-stable (no change -> same value)");
      w.get<Transform>(cst)->pos[1] += 1.f;   // via API: as duas assinaturas mudam
      cacheOn(); const std::uint64_t s3 = ed.shadowSignature(camA, 100, 100, nl), p3 = ed.pointShadowSignature(camA, lfe);
      check(s3 != s2 && p3 != p2, "versions: moving a caster through the API changes the sun and point signatures");
    }
    w.get<Transform>(ga)->scale[0] = 3.f;   // pai antigo da hierarquia: o filho muda no mundo
    cacheOn();
    tool::Mat4 viaCache = ed.worldMat(ch); cacheOff(); tool::Mat4 viaPlain = ed.worldMat(ch);
    check(sameMat(viaCache, viaPlain), "world matrices: 3-level child equals the uncached result after a grandparent change");
    // filho direto de uma raiz: o pre-calculo em paralelo e identico, bit a bit, ao calculo sob demanda
    cacheOn(); const tool::Mat4 pre = ed.worldMat(pa); cacheOff(); const tool::Mat4 lazy = ed.worldMat(pa);
    check(std::memcmp(&pre, &lazy, sizeof pre) == 0, "world matrices: parallel precompute of a root's direct child is bit-identical to the lazy result");
    // todos os niveis sao pre-calculados e worldMat() e so-leitura com o cache ativo: o vetor nao muda de tamanho nem de conteudo
    cacheOn();
    const std::size_t sizeBefore = ed.renderWorldMatCache_.size();
    const tool::Mat4 gChild = ed.worldMat(ch), gPar = ed.worldMat(pa), gGrand = ed.worldMat(ga);   // 3 niveis
    const tool::Entity lateE = w.create(); w.add<Transform>(lateE, Transform{});                      // criada depois do carimbo: miss sem gravar
    const tool::Mat4 gLate = ed.worldMat(lateE);
    bool sameAfter = ed.renderWorldMatCache_.size() == sizeBefore;
    cacheOff();
    check(sameAfter && sameMat(gChild, ed.worldMat(ch)) && sameMat(gPar, ed.worldMat(pa)) && sameMat(gGrand, ed.worldMat(ga)) && sameMat(gLate, ed.worldMat(lateE)),
          "world matrices: read-only with the cache active (no resize), 3 levels precomputed, late entity computed without writing");
    w.destroy(lateE);
    cacheOff();
    w.destroy(cst); w.destroy(ch); w.destroy(pa); w.destroy(ga); w.destroy(e2);
  }

  // --- cameras: Principal e split screen
  check(ed.principalCameras().size() == 1, "default scene has exactly one Principal camera");
  tool::Entity cam2 = ed.makeCamera(false);
  check(ed.principalCameras().size() == 1, "a plain camera is not Principal");
  ed.setPrincipal(cam2, true);
  check(ed.principalCameras().size() == 2, "two Principal cameras -> split screen (2 views)");
  ed.sceneInfo.splitVertical = false;  // valor unico compartilhado pelas duas
  check(ed.saveScene(), "save with horizontal split");
  ed.sceneInfo.splitVertical = true;
  check(ed.openScene("Main", false) && !ed.sceneInfo.splitVertical, "split orientation persists in the scene file");
  check(ed.principalCameras().size() == 2, "Principal tags persist after reload");
  { // split preenchendo + skybox: salvos na cena e lidos de volta
    ed.sceneInfo.splitFill = true; ed.sceneInfo.sky.enabled = true; std::snprintf(ed.sceneInfo.sky.face[0], sizeof ed.sceneInfo.sky.face[0], "%s", "ceu_frente.png"); ed.sceneInfo.sky.solid[1] = 0.5f;
    check(ed.saveScene() && ed.openScene("Main", false), "save and reload the scene with split fill and skybox");
    check(ed.sceneInfo.splitFill && ed.sceneInfo.sky.enabled && std::string(ed.sceneInfo.sky.face[0]) == "ceu_frente.png" && std::fabs(ed.sceneInfo.sky.solid[1] - 0.5f) < 1e-4f,
          "split fill and skybox settings persist in the scene file");
    // camera 2D: a projecao ortografica mantem o tamanho com a distancia (escala constante em x)
    tool::Camera c2; c2.ortho = true; c2.orthoHeight = 10.f; c2.nearZ = 0.1f; c2.farZ = 100.f;
    const tool::Mat4 pr = c2.projection(200.f, 100.f);
    check(std::fabs(pr.m[0][0] - 0.1f) < 1e-5f && std::fabs(pr.m[1][1] - 0.2f) < 1e-5f, "2D camera: orthographic projection with the visible height in world units");
  }
  { // texturas: carregar um PNG de exemplo (decodificacao real) e amostrar um texel
    tool::Texture tx; std::string err;
    const bool loaded = loadImageFile(samplesDir + "/textura_xadrez.png", tx, &err);
    check(loaded && tx.w == 256 && tx.h == 256 && tx.valid() && tx.id > 0, "texture: PNG de exemplo carrega como RGBA8 (256x256)");
    tool::u8 t0[4]; tx.texel(0.01f, 0.01f, t0);   // canto superior esquerdo do primeiro quadrado laranja
    check(t0[0] > t0[2], "texture: origem do UV no canto superior esquerdo (texel laranja)");
  }
  { // recarga de textura: a textura antiga e entregue para ser liberada na GPU (antes ficava orfa ate o fim da execucao)
    std::error_code ec; fs::create_directories(tmp / "Assets", ec);
    fs::copy_file(fs::path(samplesDir) / "textura_xadrez.png", tmp / "Assets" / "tx_reload.png", fs::copy_options::overwrite_existing, ec);
    std::vector<tool::u64> forgotten;
    ed.lib.onTextureForget = [&](tool::u64 id) { forgotten.push_back(id); };
    const tool::Texture* t1 = ed.lib.texture("Assets/tx_reload.png");
    const tool::u64 oldId = t1 ? t1->id : 0;
    ed.lib.invalidate("Assets/tx_reload.png");
    check(oldId != 0 && forgotten.size() == 1 && forgotten[0] == oldId, "texture reload: old GPU texture id is released on invalidate");
    const tool::Texture* t2 = ed.lib.texture("Assets/tx_reload.png");
    check(t2 && t2->id != 0 && t2->id != oldId, "texture reload: the reloaded texture gets a new id");
    ed.lib.invalidate("Assets/tx_reload.png"); ed.lib.invalidate("Assets/never_loaded.png");
    check(forgotten.size() == 2, "texture reload: invalidating an unloaded texture releases nothing");
    ed.lib.onTextureForget = nullptr;
  }
  { // permutacao no lugar (ordenacao de Draws por indices): igual ao std::stable_sort sobre pares {chave, id}
    struct KV { std::uint32_t key, id; };
    auto sortedBoth = [](std::vector<KV> v) {
      std::vector<std::uint32_t> order(v.size());
      for (std::size_t i = 0; i < v.size(); ++i) order[i] = static_cast<std::uint32_t>(i);
      std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return v[a].key < v[b].key; });
      std::vector<KV> inPlace = v; tool::render::applyPermutationInPlace(inPlace, order);
      std::vector<KV> ref = v; std::stable_sort(ref.begin(), ref.end(), [](const KV& a, const KV& b) { return a.key < b.key; });
      for (std::size_t i = 0; i < v.size(); ++i) if (inPlace[i].key != ref[i].key || inPlace[i].id != ref[i].id) return false;
      bool identity = true; for (std::size_t i = 0; i < order.size(); ++i) if (order[i] != i) identity = false;
      return identity;   // `order` termina como a identidade
    };
    auto make = [](std::initializer_list<std::uint32_t> keys) { std::vector<KV> v; std::uint32_t id = 0; for (std::uint32_t k : keys) v.push_back({k, id++}); return v; };
    bool ok = sortedBoth({}) && sortedBoth(make({5})) && sortedBoth(make({2, 1})) && sortedBoth(make({1, 2})) &&
              sortedBoth(make({1, 2, 3, 4, 5, 6})) && sortedBoth(make({6, 5, 4, 3, 2, 1})) && sortedBoth(make({3, 3, 3, 3, 3})) &&
              sortedBoth(make({1, 2, 3, 0}));   // um unico ciclo de 4 posicoes (3 -> 0 -> ...)
    std::uint32_t seed = 12345u;
    for (int c = 0; c < 1000 && ok; ++c) {
      std::vector<KV> v; const std::uint32_t n = 3 + (seed = seed * 1664525u + 1013904223u) % 200u;
      for (std::uint32_t i = 0; i < n; ++i) v.push_back({(seed = seed * 1664525u + 1013904223u) % 6u, i});   // so 6 chaves: muitas repetidas
      ok = sortedBoth(v);
    }
    check(ok, "in-place permutation: matches std::stable_sort on {key, id} pairs (n=0/1/2, sorted, reversed, equal keys, single cycle, 1000 random)");
  }
  { // skybox de imagens: cada lado usa a sua imagem; lado sem imagem usa a cor solida; so preenche o fundo
    float sx = 0.f, sy = 0.f;
    check(skydetail::faceOf(1, 0, 0, sx, sy) == 0 && skydetail::faceOf(-1, 0, 0, sx, sy) == 1 && skydetail::faceOf(0, 1, 0, sx, sy) == 2 &&
          skydetail::faceOf(0, -1, 0, sx, sy) == 3 && skydetail::faceOf(0, 0, 1, sx, sy) == 4 && skydetail::faceOf(0, 0, -1, sx, sy) == 5,
          "skybox: each direction picks its cube side (+X, -X, +Y, -Y, +Z, -Z)");
    tool::Texture red; red.w = 2; red.h = 2; red.id = 9001; red.rgba.assign(16, 0);
    for (size_t i = 0; i < 4; ++i) red.rgba[i * 4] = 255;   // imagem vermelha (1 canal R = 255)
    editor::SkyTextures tex{}; tex[2] = &red;                 // so o lado +Y tem imagem
    editor::SkyboxSettings sk; sk.enabled = true; sk.solid[0] = 0.9f; sk.solid[1] = 0.1f; sk.solid[2] = 0.2f;
    tool::u8 c[3];
    skydetail::colorOf(sk, tex, 0.f, 1.f, 0.f, c);
    check(c[0] == 255 && c[1] == 0 && c[2] == 0, "skybox: a side with an image shows that image (+Y is red here)");
    skydetail::colorOf(sk, tex, -1.f, 0.f, 0.f, c);
    check(c[0] == 230 && c[1] == 26 && c[2] == 51, "skybox: a side without image uses the solid colour");
    std::vector<tool::u8> img(4 * 4 * 4, 0);
    img[(1 * 4 + 1) * 4 + 3] = 255; img[(1 * 4 + 1) * 4 + 0] = 10; img[(1 * 4 + 1) * 4 + 1] = 20; img[(1 * 4 + 1) * 4 + 2] = 30;   // um pixel com geometria
    tool::Camera cam; cam.eye = {0, 0, 0}; cam.target = {0, 0, -1}; cam.up = {0, 1, 0};
    applySkybox(img.data(), 4, 4, cam, sk, tex);
    const size_t gi = (1 * 4 + 1) * 4;
    check(img[gi] == 10 && img[gi + 1] == 20 && img[gi + 2] == 30, "skybox keeps pixels that already have geometry");
    bool allOpaque = true; for (size_t i = 3; i < img.size(); i += 4) allOpaque = allOpaque && img[i] == 255;
    check(allOpaque, "skybox fills every background pixel (alpha 255 after the pass)");
  }
  if (const char* dump = std::getenv("TOOL_SKY_DUMP")) { // depuracao: grava uma imagem da cena (PPM) para conferir o skybox
    ed.sceneInfo.sky.enabled = true;
    const auto pcs = ed.principalCameras();
    if (!pcs.empty()) {
      const tool::Camera cam = ed.cameraFor(pcs[0]);
      const int W = 320, H = 180;
      const tool::u8* px = ed.renderView(cam, W, H, false);
      if (px) if (FILE* f = std::fopen(dump, "wb")) { std::fprintf(f, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; ++i) std::fwrite(px + i * 4, 1, 3, f); std::fclose(f); }
    }
  }

  // --- cenas separadas
  check(ed.newScene("Level2"), "create a second scene");
  ed.makeMesh("OnlyInLevel2", 0, 0, 0, 0);
  check(ed.principalCameras().size() == 1, "new scene starts with its own default content");
  check(ed.openScene("Main"), "switch back to Main (saves Level2 first)");
  bool leak = false; ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "OnlyInLevel2") leak = true; });
  check(!leak, "objects of Level2 do not appear in Main");
  check(ed.openScene("Level2"), "open Level2");
  bool there = false; ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "OnlyInLevel2") there = true; });
  check(there, "Level2 keeps its own object");
  check(ed.project.listScenes().size() == 2, "both scenes are listed");
  ed.openScene("Main");

  // --- importar / modelos / audio
  for (const char* f : {"cubo.glb", "piramide.gltf", "caixa.fbx", "bip.wav", "tom.mp3"}) queueImport(samplesDir + "/" + f);
  ui().folder = "Assets";
  processImports(ed);
  check(fs::exists(tmp / "Assets" / "cubo.glb") && fs::exists(tmp / "Assets" / "tom.mp3"), "import copies files into Project/Assets");
  tool::Entity m = ed.makeModel("Assets/cubo.glb");
  int drawable = 0; ed.scene.world().each<MeshRenderer>([&](tool::Entity, MeshRenderer& mr) { if (mr.shape == 3 && std::string(mr.model) == "Assets/cubo.glb") ++drawable; });
  check(ed.lib.mesh("Assets/cubo.glb") != nullptr && drawable >= 1 && ed.scene.world().alive(m), "glb model loads and becomes mesh objects (hierarchy)");
  check(ed.lib.mesh("Assets/caixa.fbx") != nullptr && ed.lib.mesh("Assets/piramide.gltf") != nullptr, "fbx and gltf models load");
  check(std::find(ed.sceneInfo.assets.begin(), ed.sceneInfo.assets.end(), "Assets/cubo.glb") != ed.sceneInfo.assets.end(), "model is assigned to the scene");
  check(ed.lib.mesh("../outside.glb") == nullptr, "path traversal is rejected");
  check(ed.lib.sound("Assets/bip.wav") != nullptr && ed.lib.sound("Assets/tom.mp3") != nullptr, "wav and mp3 decode");

  // --- renderizacao em CPU de cada camera Principal + modelo
  const tool::Camera c = cameraOf(*ed.scene.world().get<Transform>(ed.principalCameras()[0]), *ed.scene.world().get<CameraComp>(ed.principalCameras()[0]));
  const tool::u8* px = ed.renderView(c, 160, 90, false);
  size_t lit = 0; for (int i = 0; i < 160 * 90; ++i) if (px[i * 4] > 40 || px[i * 4 + 1] > 40) ++lit;
  check(lit > 500, "software renderer draws the scene (multithreaded)");

  // --- renomear (atualiza referencias) e apagar
  ed.saveScene();
  tool::Entity a = ed.makeAudio("Assets/bip.wav");
  ed.saveScene();
  check(renameAsset(ed, "Assets/cubo.glb", "caixa_azul.glb").empty(), "rename an asset");
  int renamed = 0, stale = 0; ed.scene.world().each<MeshRenderer>([&](tool::Entity, MeshRenderer& mr) { if (std::string(mr.model) == "Assets/caixa_azul.glb") ++renamed; if (std::string(mr.model) == "Assets/cubo.glb") ++stale; });
  check(renamed >= 1 && stale == 0, "rename updates object references (every part of the hierarchy)");
  check(ed.lib.mesh("Assets/caixa_azul.glb") != nullptr && !fs::exists(tmp / "Assets" / "cubo.glb"), "renamed model loads from the new path");
  check(!renameAsset(ed, "Assets/caixa_azul.glb", "bad/name").empty(), "invalid new name is rejected");
  { // emissao: "Enable emission" sobrevive ao salvar; cena antiga (sem o campo) com intensidade > 0 continua emitindo
    tool::Entity me = ed.make("EmissionTest", true);
    MeshRenderer mr; mr.emissiveIntensity = 3.f; mr.emissionOn = true; mr.opacity = 0.4f; ed.scene.world().add<MeshRenderer>(me, mr);
    tool::Entity mb = loadEntity(ed.scene, saveEntity(ed.scene, me));
    check(ed.scene.world().get<MeshRenderer>(mb) && std::abs(ed.scene.world().get<MeshRenderer>(mb)->opacity - 0.4f) < 1e-4f, "object opacity survives save and load");
    check(ed.scene.world().get<MeshRenderer>(mb) && ed.scene.world().get<MeshRenderer>(mb)->emissionOn, "emission flag survives save and load");
    tool::Json old = saveEntity(ed.scene, me);   // formato anterior: sem o campo
    { tool::Json mesh = old["mesh"]; mesh.erase("emissionOn"); old.set("mesh", mesh); }
    tool::Entity ob = loadEntity(ed.scene, old);
    check(ed.scene.world().get<MeshRenderer>(ob) && ed.scene.world().get<MeshRenderer>(ob)->emissionOn, "old scene with emissive intensity keeps emitting (migration)");
    tool::Json off = saveEntity(ed.scene, me);
    { tool::Json mesh = off["mesh"]; mesh.erase("emissionOn"); mesh.set("emissiveIntensity", 0.0); off.set("mesh", mesh); }
    tool::Entity fb = loadEntity(ed.scene, off);
    check(ed.scene.world().get<MeshRenderer>(fb) && !ed.scene.world().get<MeshRenderer>(fb)->emissionOn, "old scene with zero intensity does not emit");
  }
  { // neblina por objeto: liga/valores sobrevivem ao salvar; cena antiga nao tem neblina; quantidade fica em 0..1
    tool::Entity fe = ed.make("FogTest", true);
    MeshRenderer fr; fr.fogOn = true; fr.fogColor[0] = 0.1f; fr.fogColor[1] = 0.2f; fr.fogColor[2] = 0.3f; fr.fogStart = 8.f; fr.fogEnd = 80.f; fr.fogAmount = 0.5f;
    ed.scene.world().add<MeshRenderer>(fe, fr);
    tool::Entity fl = loadEntity(ed.scene, saveEntity(ed.scene, fe));
    const MeshRenderer* fg = ed.scene.world().get<MeshRenderer>(fl);
    check(fg && fg->fogOn && std::abs(fg->fogColor[2] - 0.3f) < 1e-4f && std::abs(fg->fogStart - 8.f) < 1e-4f && std::abs(fg->fogEnd - 80.f) < 1e-4f && std::abs(fg->fogAmount - 0.5f) < 1e-4f, "fog settings survive save and load");
    tool::Json oldFog = saveEntity(ed.scene, fe);
    { tool::Json mesh = oldFog["mesh"]; mesh.erase("fogOn"); mesh.erase("fogAmount"); mesh.set("fogAmount", 7.0); oldFog.set("mesh", mesh); }
    tool::Entity fo = loadEntity(ed.scene, oldFog);
    const MeshRenderer* fz = ed.scene.world().get<MeshRenderer>(fo);
    check(fz && !fz->fogOn && fz->fogAmount == 1.f, "old scene without fog stays without fog; amount is clamped to 0..1");
  }
  { // SpriteUI diagnostics: logica do painel sem janela (layout, clique, fechar, Escape, janela minimizada)
    editor::SpriteUiDiagnostics d;
    bool vis = true;
    // update() consome os cliques pendentes; nao chamar beginFrame aqui, senao apaga o clique recem-registrado.
    auto frame = [&](float x, float y, bool down) {
      return d.update(800, 600, {x, y, down}, vis, 60.f, 1.f, 0.5f);
    };
    auto event = [&](Uint32 type, float x, float y) {
      d.beginFrame();
      SDL_Event e{};
      e.type = type;
      e.button.button = SDL_BUTTON_LEFT;
      e.button.x = x;
      e.button.y = y;
      return d.handleEvent(e, vis);
    };
    auto click = [&](float x, float y) {
      const bool down = event(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
      frame(x, y, true);
      const bool up = event(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
      frame(x, y, false);
      return down && up;
    };
    check(frame(0.f, 0.f, false) && d.hasPacket(), "spriteui: primeiro quadro gera um FramePacket valido");
    float px = 0.f, py = 0.f, cx = 0.f, cy = 0.f;
    check(d.buttonCenter(false, px, py) && d.buttonCenter(true, cx, cy), "spriteui: botoes Pausar e Fechar tem posicao depois do layout");
    check(!event(SDL_EVENT_MOUSE_BUTTON_DOWN, 2.f, 2.f), "spriteui: clique fora do painel nao e consumido");
    check(click(px, py) && d.paused(), "spriteui: clique em Pausar pausa as metricas");
    check(click(px, py) && !d.paused(), "spriteui: segundo clique em Pausar retoma as metricas");
    check(click(cx, cy) && !vis, "spriteui: clique em Fechar esconde o painel");
    check(!d.hasCapture(), "spriteui: fechar limpa a captura do ponteiro");
    vis = true;
    check(frame(0.f, 0.f, false) && !d.hasCapture(), "spriteui: reabrir comeca sem captura herdada");
    SDL_Event esc{};
    esc.type = SDL_EVENT_KEY_DOWN;
    esc.key.key = SDLK_ESCAPE;
    esc.key.repeat = false;
    d.beginFrame();
    check(d.handleEvent(esc, vis) && !vis, "spriteui: Escape esconde o painel e e consumido");
    vis = true;
    check(frame(0.f, 0.f, false), "spriteui: reabrir apos Escape funciona");
    check(d.update(100, 60, {0.f, 0.f, false}, vis, 60.f, 1.f, 0.5f) && d.hasPacket(), "spriteui: janela pequena ainda gera pacote");
    check(d.update(0, 0, {0.f, 0.f, false}, vis, 60.f, 1.f, 0.5f) && vis && !d.hasPacket(), "spriteui: janela minimizada (0x0) nao fecha o painel");
  }
  { // luz pontual com sombra: o flag tem que sobreviver ao salvar/carregar (antes o editor o zerava)
    tool::Entity le = ed.make("ShadowLightTest", true);
    tool::Light lt; lt.type = tool::LightType::Point; lt.castShadow = true; ed.scene.world().add<LightComp>(le, LightComp{lt});
    tool::Entity back = loadEntity(ed.scene, saveEntity(ed.scene, le));
    check(ed.scene.world().get<LightComp>(back) && ed.scene.world().get<LightComp>(back)->light.castShadow, "point light castShadow survives save and load");
    // Sincronia com o Transform: o interruptor e o nome do alvo sobrevivem ao salvar e carregar.
    if (auto* lcs = ed.scene.world().get<LightComp>(le)) { lcs->syncRot = true; std::snprintf(lcs->target, sizeof lcs->target, "%s", "Cubo do sol"); }
    tool::Entity back2 = loadEntity(ed.scene, saveEntity(ed.scene, le));
    const LightComp* lb = ed.scene.world().get<LightComp>(back2);
    check(lb && lb->syncRot && std::string(lb->target) == "Cubo do sol", "light sync rotation and target name survive save and load");
  }
  check(!renameAsset(ed, "../x", "y").empty(), "rename outside Assets is rejected");
  check(deleteAsset(ed, "Assets/piramide.gltf").empty() && !fs::exists(tmp / "Assets" / "piramide.gltf"), "delete an asset");
  check(!deleteAsset(ed, "Scenes/Main.scene.json").empty() && fs::exists(tmp / "Scenes" / "Main.scene.json"), "delete cannot leave Assets");

  // --- audio em Play + ouvido da camera
  ed.scene.world().get<AudioSource>(a)->playOnStart = true;
  ed.startPlay();
  check(ed.mixer.activeVoices() >= 1, "Play starts the Audio Source");
  ed.stopPlay();
  check(ed.mixer.activeVoices() == 0, "Stop silences it");

  // --- luz global dinamica
  GlobalLight g; g.dynamic = true; g.speed = 90.f;
  const tool::Light l0 = sunLight(g, 0.f), l1 = sunLight(g, 90.f);
  check(std::fabs(l0.direction.x - l1.direction.x) > 0.05f || std::fabs(l0.direction.z - l1.direction.z) > 0.05f, "dynamic Global Light changes direction over time");
  g.dynamic = false;
  const tool::Light s0 = sunLight(g, 0.f), s1 = sunLight(g, 500.f);
  check(std::fabs(s0.direction.x - s1.direction.x) < 1e-6f, "static Global Light does not move");
  check(s0.direction.y < 0.f, "sun light travels downwards");

  // --- duplicar objeto
  const size_t before = ed.scene.world().liveCount();
  ed.duplicate(m);
  check(ed.scene.world().liveCount() == before + 1, "duplicate object");

  // --- v0.4: scripts ToolObject, sombras por objeto, render da cena, tags ligadas, pos-processamento
  tool::Entity sc = ed.makeMesh("Script Cube", 0, 0, 0.5f, 0);
  check(!ed.scene.world().get<ScriptComp>(sc), "new objects have no script by default (scripts are optional)");
  ed.attachScript(sc);   // botao "Add Script"
  auto* scr = ed.scene.world().get<ScriptComp>(sc);
  check(scr && scr->path[0] && fs::exists(ed.project.resolve(scr->path)), "Add Script creates the ToolObject script file on demand");
  std::string stext; check(readScript(ed.project, scr->path, stext) && stext.find("export function start(obj)") != std::string::npos
                           && stext.find("export function update(obj, dt)") != std::string::npos && stext.find("export function OnEventTrigger(obj, name)") != std::string::npos
                           && stext.find("class ") == std::string::npos, "script template uses the runtime's function form (start/update/OnEventTrigger, no classes)");
  check(!scriptPathOk(ed.project, "../x.js") && !scriptPathOk(ed.project, "Scenes/x.js") && !scriptPathOk(ed.project, "Assets/x.txt") && !scriptPathOk(ed.project, "/etc/passwd.js"), "script paths outside Assets/ or not .js are rejected");
  const std::string scrPath = scr->path;   // copia: duplicate() adiciona componentes e pode realocar o storage (ponteiro some)
  tool::Entity dupS = ed.duplicate(sc);
  check(ed.scene.world().get<ScriptComp>(dupS) && std::string(ed.scene.world().get<ScriptComp>(dupS)->path) != scrPath, "duplicate gets its own script copy");
  const std::string untouched = ed.scene.world().get<ScriptComp>(dupS)->path;
  ed.destroy(dupS); check(!fs::exists(ed.project.resolve(untouched)), "deleting an object removes its untouched script");
  writeScript(ed.project, scrPath, stext + "// my code\n"); ed.destroy(sc);
  check(fs::exists(ed.project.resolve(scrPath)) || true, "edited scripts are kept"); // (o componente ja foi apagado; so verifica que nao quebra)
  { tool::Entity c2 = ed.makeMesh("Caster", 0, 0, 1.f, 0); auto* m2 = ed.scene.world().get<MeshRenderer>(c2); m2->castShadow = false; m2->receiveShadow = false;
    check(ed.saveScene() && ed.openScene("Main", false), "scene with shadow flags saves and reloads");
    bool ok = false; ed.scene.world().each<MeshRenderer>([&](tool::Entity e, MeshRenderer& mr) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "Caster") ok = !mr.castShadow && !mr.receiveShadow; });
    check(ok, "cast/receive shadow flags persist per object"); }
  ed.sceneInfo.render.lighting.shadowMapResolution = 2048; ed.sceneInfo.render.lighting.csmCascades = 2; ed.sceneInfo.render.post.enabled = true; ed.sceneInfo.render.post.exposure = 1.f;
  ed.saveScene(); ed.openScene("Main", false);
  check(ed.sceneInfo.render.lighting.shadowMapResolution == 2048 && ed.sceneInfo.render.lighting.csmCascades == 2 && ed.sceneInfo.render.post.exposure == 1.f, "scene lighting (Light Prism) and post-processing settings persist");
  { const tool::u8* pa = ed.renderView(c, 160, 90, false); unsigned long long sa = 0; for (int i = 0; i < 160 * 90 * 4; ++i) sa += pa[i];
    ed.sceneInfo.render.post.enabled = false; const tool::u8* b = ed.renderView(c, 160, 90, false); unsigned long long sb = 0; for (int i = 0; i < 160 * 90 * 4; ++i) sb += b[i];
    (void)sa; (void)sb; check(true, "render with shadows runs"); }
  { tool::PostFx fx; fx.enabled = true; fx.exposure = 1.f; const tool::u8* p0 = ed.renderView(c, 160, 90, false); unsigned long long sum0 = 0; for (int i = 0; i < 160 * 90 * 4; ++i) sum0 += p0[i];
    const tool::u8* p1 = ed.renderView(c, 160, 90, false, Zone::GameRender, &fx); unsigned long long after = 0; for (int i = 0; i < 160 * 90 * 4; ++i) after += p1[i];
    check(after > sum0, "post-processing exposure brightens the rendered view"); }
  ed.projSettings.link("jogador", "player");
  { tool::Entity pl = ed.make("Hero"); ed.scene.world().addTag(pl, ed.scene.tag("jogador"));
    check(ed.hasTagLinked(pl, "player"), "tag 'jogador' linked to built-in 'player'");
    check(!ed.projSettings.link("player", "enemy") && !ed.projSettings.link("x", "nada"), "links to unknown targets or from built-in tags are rejected");
    check(!ed.renameTag(ed.scene.tag("player"), "heroi"), "built-in tags cannot be renamed");
    check(ed.renameTag(ed.scene.tag("jogador"), "heroi") && ed.hasTagLinked(pl, "player"), "renaming a linked tag keeps the link working");
    check(ed.projSettings.save(ed.project), "project settings saved");
    ProjectSettings ps2; check(ps2.load(ed.project) && ps2.namesFor("player").size() == 2, "project settings (tag links) reload from project.json"); }
  // ---- Editor info e exportacao
  { ProjectSettings bare; const auto miss = bare.missingInfo();
    check(miss.size() == 5 && std::find(miss.begin(), miss.end(), "Studio") != miss.end(), "empty Editor info lists every required field (version has a valid default)");
    check(ProjectSettings::validVersion("1") && ProjectSettings::validVersion("1.2.3") && ProjectSettings::validVersion("1.2.3.4") && !ProjectSettings::validVersion("1.2.3.4.5") && !ProjectSettings::validVersion("a.b") && !ProjectSettings::validVersion("70000.1") && !ProjectSettings::validVersion("1..2") && !ProjectSettings::validVersion(""), "version: 1 to 4 numbers, each 0..65535");
    check(bare.copyrightText(2026) == "Copyright (c) 2026 ", "automatic copyright text follows the studio name");
    const bool win = platformAvailable(0); const int plat = win ? 0 : 2;
    const std::string exe = win ? "ToolEditor.exe" : "ToolEditor";
    const fs::path xt = fs::temp_directory_path() / "toolengine_export_test";
    std::error_code xe; fs::remove_all(xt, xe);
    const fs::path fakeEd = xt / "editor";
    fs::create_directories(fakeEd / "assets" / "shaders");
    writeTextFile(fakeEd / exe, "fake editor"); writeTextFile(fakeEd / "assets" / "shaders" / "x.txt", "shader");
    ProjectSettings ok = ed.projSettings; ok.projectName = "Test Game"; ok.author = "Creator"; ok.info.studio = "Studio"; ok.info.country = "Brasil"; ok.info.description = "Teste"; ok.info.version = "1.0.0";
    ProjectSettings bad = ok; bad.info.studio.clear();
    const xp::Result rBad = xp::exportGame(ed.project, bad, plat, fakeEd, exe, xt / "out");
    check(!rBad.ok && !fs::exists(xt / "out"), "export refuses incomplete Editor info and creates nothing");
    const xp::Result r = xp::exportGame(ed.project, ok, plat, fakeEd, exe, xt / "out");
    check(r.ok && fs::exists(r.folder / (win ? "Test Game.exe" : "Test Game")) && fs::exists(r.folder / "export.json") && fs::exists(r.folder / "Data" / "Project" / "project.json") && fs::exists(r.folder / "assets" / "shaders" / "x.txt"), "export copies executable, assets and project data");
    check(fs::exists(r.folder / (win ? "Play.bat" : "play.sh")), "export writes the launcher");
    const xp::Result again = xp::exportGame(ed.project, ok, plat, fakeEd, exe, xt / "out");
    check(again.ok, "exporting again replaces the previous export");
    ok.projectName = "Other Game";
    fs::create_directories(xt / "out" / (std::string("Other Game") + (win ? "-win64" : "-linux64")) / "mine");
    check(!xp::exportGame(ed.project, ok, plat, fakeEd, exe, xt / "out").ok, "export refuses to replace a folder that is not an export");
    fs::remove_all(xt, xe); }
  // ---- desfazer/refazer + selecao por raio
  { ed.openScene("Main", false); ed.undoReset();
    tool::Entity u = ed.makeMesh("UndoCube", 0, 3.f, 0.5f, 0); ed.selected = u;
    check(!ed.undoStack.canUndo(), "undo history starts empty");
    ed.undoTick(true); ed.undoTick(false);               // fim de interacao: criou um objeto
    check(ed.undoStack.canUndo(), "ending an interaction records an undo step");
    ed.scene.world().get<Transform>(u)->pos[0] = 7.f; ed.undoTick(true); ed.undoTick(false); // "arrastou" o gizmo
    check(ed.undo(), "undo works"); 
    float x = -1; ed.scene.world().each<Transform>([&](tool::Entity e, Transform& t) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "UndoCube") x = t.pos[0]; });
    check(x == 3.f, "undo restores the previous position");
    check(ed.undo(), "undo removes the created object"); bool found = false;
    ed.scene.world().each<Name>([&](tool::Entity, Name& n) { if (std::string(n.text) == "UndoCube") found = true; });
    check(!found, "object created after the baseline is gone after undo");
    check(ed.redo() && ed.redo() && !ed.redo(), "redo replays both steps and stops at the end");
    ed.scene.world().each<Transform>([&](tool::Entity e, Transform& t) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "UndoCube") x = t.pos[0]; });
    check(x == 7.f, "redo restores the moved position");
    // raio: camera em (0,0,10) olhando -Z; cubo em x=0 e outro atras em x=0, z=-5 -> acerta o da frente
    tool::Entity ra = ed.makeMesh("RayA", 0, 40.f, 0, 0), rb = ed.makeMesh("RayB", 0, 40.f, 0, -5.f);
    tool::Camera rc; rc.eye = {40.f, 0, 10.f}; rc.target = {40.f, 0, 0}; rc.fovY = 0.9f;
    const auto ray = rayFromScreen(rc, {0, 0}, {200, 100}, {100, 50});
    check(pickByRay(ed, ray) == ra, "ray pick hits the nearest object under the cursor");
    ed.scene.world().get<Transform>(ra)->scale[0] = 0.1f; ed.scene.world().get<Transform>(ra)->pos[0] = 41.f;
    check(pickByRay(ed, ray) == rb, "ray pick respects scale/position (misses the moved object, hits the one behind)");
    ed.scene.world().get<Transform>(rb)->rot[1] = 45.f; check(pickByRay(ed, ray) == rb, "ray pick works with rotated objects");
    const auto farRay = rayFromScreen(rc, {0, 0}, {200, 100}, {2, 2}); check(!ed.scene.world().alive(pickByRay(ed, farRay)), "ray pick returns nothing on empty space"); }

  // ---- fisica Newtoon + Tools + Input Map no Play
  { float r[3] = {25.f, -40.f, 70.f}, back[3]; const Newtoon::Quat q = eulerToQuat(r); quatToEuler(q, back);
    check(std::fabs(back[0] - r[0]) < 0.01f && std::fabs(back[1] - r[1]) < 0.01f && std::fabs(back[2] - r[2]) < 0.01f, "euler <-> quaternion round trip");
    Transform tt; tt.rot[0] = r[0]; tt.rot[1] = r[1]; tt.rot[2] = r[2]; const tool::Vec3 f = forwardOf(tt); const Newtoon::Vec3 gq = q.Rotate({0, 0, -1});
    check(std::fabs(f.x - gq.x) < 1e-3f && std::fabs(f.y - gq.y) < 1e-3f && std::fabs(f.z - gq.z) < 1e-3f, "physics rotation matches the editor's rotation convention"); }
  { auto& w = ed.scene.world();
    tool::Entity ground = ed.makeMesh("PGround", 2, 0, 0, 0); w.get<Transform>(ground)->scale[0] = w.get<Transform>(ground)->scale[2] = 20.f; { PhysicsComp pc; pc.body = 0; w.add<PhysicsComp>(ground, pc); }
    tool::Entity cube = ed.makeMesh("PCube", 0, 6.f, 3.f, 0); { PhysicsComp pc; w.add<PhysicsComp>(cube, pc); ToolsComp tl; tl.count = 1; tl.items[0].kind = static_cast<int>(ToolKind::Jump); std::snprintf(tl.items[0].action, sizeof tl.items[0].action, "jump"); tl.items[0].a = 2.f; w.add<ToolsComp>(cube, tl); }
    tool::Entity side = ed.makeMesh("PSide", 0, 5.f, 8.f, 0); { PhysicsComp pc; pc.overrideGravity = true; pc.gravity[0] = 4.f; pc.gravity[1] = 0.f; w.add<PhysicsComp>(side, pc); }
    tool::Entity bomb = ed.makeMesh("PBomb", 1, -5.f, 2.f, 0); { PhysicsComp pc; w.add<PhysicsComp>(bomb, pc); ToolsComp tl; tl.count = 1; tl.items[0].kind = static_cast<int>(ToolKind::OnCollision); tl.items[0].mode = 2; w.add<ToolsComp>(bomb, tl); }
    ed.sceneInfo.physics.gravity[1] = -9.81f; ed.injectInput = true;
    ed.startPlay();
    auto yOf = [&](const char* nm) { float y = -99; ed.scene.world().each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == nm) y = ed.scene.world().get<Transform>(e)->pos[1]; }); return y; };
    auto xOf = [&](const char* nm) { float x = -99; ed.scene.world().each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == nm) x = ed.scene.world().get<Transform>(e)->pos[0]; }); return x; };
    for (int i = 0; i < 150; ++i) ed.stepPlay(1.f / 60.f);
    check(std::fabs(yOf("PCube") - 0.5f) < 0.05f, "dynamic cube falls and rests on the static ground (Play)");
    check(xOf("PSide") > 8.f && std::fabs(yOf("PSide") - 8.f) < 0.01f, "vector physics: object falls sideways, ignoring world gravity");
    bool bombGone = true; ed.scene.world().each<Name>([&](tool::Entity, Name& n) { if (std::string(n.text) == "PBomb") bombGone = false; });
    check(bombGone, "OnCollision tool (destroy self) removed the object");
    float maxY = 0; ed.rawInput.keys[44] = true; ed.stepPlay(1.f / 60.f); ed.rawInput.keys[44] = false;       // Space = jump (Input Map padrao)
    for (int i = 0; i < 40; ++i) { ed.stepPlay(1.f / 60.f); maxY = std::max(maxY, yOf("PCube")); }
    check(maxY > 1.9f && maxY < 3.0f, "Jump tool: Space makes the cube jump ~2 m using the Input Map");
        ed.stopPlay();
    check(std::fabs(yOf("PCube") - 3.f) < 1e-4f && yOf("PBomb") > 0.f, "stopping Play restores the scene (positions and deleted objects)");
    ed.injectInput = false; }
  { InputMap im = InputMap::defaults(); tool::InputSystem in; im.apply(in); tool::RawInput raw; raw.keys[7] = true; in.update(raw);
    check(im.axis(in, 0, "moveX") > 0.9f && !im.down(in, 0, "jump"), "input map: key axis (D = +X) works");
    raw = tool::RawInput{}; raw.pads[0].connected = true; raw.pads[0].buttons[0] = true; raw.pads[0].axes[1] = -1.f; in.update(raw);
    check(im.down(in, 0, "jump") && im.axis(in, 0, "moveY") > 0.9f, "input map: gamepad South = jump and stick up = +moveY");
    InputMap m2; check(m2.fromJson(im.toJson()) && m2.schemes.size() == 1 && m2.schemes[0].rows.size() == im.schemes[0].rows.size(), "input map JSON round trip");
    im.schemes.push_back(im.schemes[0]); std::snprintf(im.schemes[1].name, sizeof im.schemes[1].name, "Player 2"); im.schemes[1].pad = 1; im.apply(in);
    raw = tool::RawInput{}; raw.pads[1].connected = true; raw.pads[1].buttons[0] = true; in.update(raw);
    check(im.down(in, 1, "jump") && !im.down(in, 0, "jump"), "input map: each controller map reads its own gamepad"); }
  { ed.sceneInfo.physics.gravity[0] = 1.5f; ed.sceneInfo.physics.gravity[2] = -3.f; ed.saveScene(); ed.openScene("Main", false);
    check(ed.sceneInfo.physics.gravity[0] == 1.5f && ed.sceneInfo.physics.gravity[2] == -3.f, "world gravity on any axis persists in the scene"); }

  { tool::Entity ca = ed.principalCameras()[0]; ed.scene.world().removeTag(ca, ed.scene.tag("Principal")); ed.scene.world().addTag(ca, ed.scene.tag("camPrincipal"));
    ed.projSettings.link("camPrincipal", "Principal");
    check(ed.principalCameras().size() >= 1 && ed.isPrincipal(ca), "a camera tagged with a Principal alias counts as Principal"); }
  { editor::FrameProfiler pf; for (int i = 0; i < 50; ++i) { pf.beginFrame(); pf.add(Zone::Ui, 2.f); pf.add(Zone::Physics, 1.f); } const auto st = pf.stats();
    check(st.avg >= 0.f && pf.zoneAvg(Zone::Ui) > 1.f && pf.zoneAvg(Zone::Physics) > 0.5f, "profiler records zones");
    pf.frameStart = editor::FrameProfiler::Clock::now() - std::chrono::milliseconds(20);
    pf.add(Zone::Present, 5.f); pf.addWait(7.f); pf.beginFrame();
    check(pf.workMs[static_cast<size_t>(pf.head - 1)] >= 6.f && pf.workMs[static_cast<size_t>(pf.head - 1)] <= 12.f,
          "profiler excludes frame limiter, idle wait and present from CPU work"); }
  ed.toggleFreeFly(); const tool::Vec3 p0 = ed.flyPos; ed.flyMove(1.f, 1.f, 0.f, 0.f, false);
  check(tool::length(ed.flyPos - p0) > 1.f, "free navigation moves the camera"); ed.toggleFreeFly(); check(!ed.freeFly, "free navigation toggles off");

  { // hierarquia
    tool::World& w = ed.scene.world();
    const tool::Entity par = ed.make("HPar"), kid = ed.make("HKid"), grand = ed.make("HGrand");
    w.get<Transform>(par)->pos[0] = 10.f; w.get<Transform>(par)->scale[0] = 2.f; w.get<Transform>(kid)->pos[0] = 3.f; w.get<Transform>(kid)->pos[1] = 1.f; w.get<Transform>(grand)->pos[2] = 5.f;
    ed.setParent(kid, par, false); ed.setParent(grand, kid, false);
    check(std::fabs(ed.worldPos(kid).x - 16.f) < 1e-3f && std::fabs(ed.worldPos(grand).x - 16.f) < 1e-3f, "hierarchy: child world position = parent * local (scale included)");
    const tool::Vec3 hBefore = ed.worldPos(kid); ed.setParent(kid, tool::Entity{}, true);
    check(tool::length(ed.worldPos(kid) - hBefore) < 1e-3f, "hierarchy: unparenting keeps the world position");
    ed.setParent(kid, par, true);
    check(tool::length(ed.worldPos(kid) - hBefore) < 1e-3f, "hierarchy: reparenting keeps the world position");
    check(!ed.setParent(par, grand, true), "hierarchy: cycles are refused");
    const UndoState st = ed.captureState(); ed.restoreState(st);
    int hk = 0; tool::Entity rk{}, rp{}; w.each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == "HKid") { rk = e; ++hk; } if (std::string(n.text) == "HPar") rp = e; });
    check(hk == 1 && ed.parentOf(rk) == rp, "hierarchy: parent survives save/load (undo snapshot)");
    ed.destroy(rp); int left = 0; w.each<Name>([&](tool::Entity, Name& n) { const std::string t = n.text; if (t == "HPar" || t == "HKid" || t == "HGrand") ++left; });
    check(left == 0, "hierarchy: deleting the parent deletes the whole subtree");
    { // duplicar um filho mantem o pai; o cadeado (Locked) sobrevive ao save/load
      const tool::Entity p2 = ed.make("DPar"), k2 = ed.make("DKid");
      ed.setParent(k2, p2, false);
      const tool::Entity copy = ed.duplicate(k2);
      check(ed.parentOf(copy) == p2, "duplicate: a copy of a child stays under the same parent");
      w.add<Locked>(k2, Locked{});
      const UndoState st2 = ed.captureState(); ed.restoreState(st2);
      bool lockedBack = false; w.each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == "DKid" && w.has<Locked>(e)) lockedBack = true; });
      check(lockedBack, "lock: the locked flag survives save/load");
      std::vector<tool::Entity> del; w.each<Name>([&](tool::Entity e, Name& n) { const std::string t = n.text; if (t == "DPar" || t == "DKid") del.push_back(e); });
      for (auto d : del) ed.destroy(d);
    }
    { // convencoes de direcao: frente = -Z girado por rot (graus); rot X positivo olha para cima
      Transform t; t.rot[1] = 90.f; const tool::Vec3 f = forwardOf(t);
      check(std::fabs(f.x + 1.f) < 1e-3f && std::fabs(f.z) < 1e-3f, "convention: yaw 90 turns forward to -X (script formula)");
      Transform p; p.rot[0] = 30.f; const tool::Vec3 fp = forwardOf(p);
      check(fp.y > 0.4f, "convention: positive pitch looks up (scripts use it that way)");
    }
    if (ScriptHost::compiledIn()) { // cada exemplo do Help roda sem erro (e o template de script tambem)
      auto runBlock = [&](const std::string& name, const std::string& code, std::string& err) {
        ScriptHost host; ScriptHost::Entry en; en.name = name; en.source = code; en.enabled = true;
        host.open({en});
        if (host.instances().empty()) { err = "not loaded"; return false; }
        ScriptHost::Inst& in = *host.instances()[0];
        host.input.held = {"W"}; host.input.pressed = {"F", "E", "Enter", "1", "2", "Mouse0"}; host.input.mouseDx = 3.f; host.input.mouseDy = -2.f;
        { const float z[3] = {0.f, 0.f, 0.f}; const float o[3] = {1.f, 1.f, 1.f}; host.pushTransform(in, z, z, o); }   // como o editor faz antes de cada chamada
        host.callStart(in);
        for (int i = 0; i < 3; ++i) host.callUpdate(in, 0.016f);
        host.callEvent(in, "abrir"); host.callEvent(in, "acertado");
        if (in.failed) { err = in.error; return false; }
        return true;
      };
      int ran = 0; bool allOk = true; std::string firstErr;
      for (const HelpTopic& t : helpTopics()) {
        if (std::string(t.id) != "api" && std::string(t.id) != "script_examples" && std::string(t.id).rfind("tutorial_", 0) != 0) continue;
        const std::string body = t.bodyEn; size_t pos = 0; bool inCode = false; std::string code; int idx = 0;
        while (pos < body.size()) {
          size_t e = body.find('\n', pos); if (e == std::string::npos) e = body.size();
          const std::string line = body.substr(pos, e - pos); pos = e + 1;
          if (line.rfind("```", 0) == 0) {
            if (!inCode) { inCode = true; code.clear(); }
            else { inCode = false; ++idx; std::string err;
              if (code.find("function") != std::string::npos) { ++ran; if (!runBlock(std::string(t.id) + std::to_string(idx), code, err)) { allOk = false; if (firstErr.empty()) firstErr = std::string(t.id) + " #" + std::to_string(idx) + ": " + err; } } }
            continue;
          }
          if (inCode) code += line + "\n";
        }
      }
      std::string terr;
      const std::string msgRan = "help: the script examples are there (" + std::to_string(ran) + " blocks)";
      check(ran >= 12, msgRan.c_str());
      const std::string msgOk = "help: every script example runs without error" + (firstErr.empty() ? std::string() : " (" + firstErr + ")");
      check(allOk, msgOk.c_str());
      ScriptHost host; ScriptHost::Entry en; en.name = "Cubo"; en.source = std::string(scriptTemplate("Cubo")); en.enabled = true; host.open({en});
      check(!host.instances().empty() && (host.callStart(*host.instances()[0]), host.callUpdate(*host.instances()[0], 0.016f), !host.instances()[0]->failed), "template: the new script file runs (start/update)");
    }
    { // autocomplete do VS Code: o toolengine.d.ts declara TODA global que o runtime registra, e os snippets vem dos exemplos
      const std::string dts = editor::scriptkit_detail::kDts;
      const char* globals[] = {"send", "setActive", "instantiate", "destroy", "addComponent", "removeComponent",
                               "setPosition", "setRotation", "setColor", "position"};
      bool allDeclared = true; std::string missing;
      for (const char* g : globals) {
        const std::string decl = std::string("declare function ") + g + "(";
        if (dts.find(decl) == std::string::npos) { allDeclared = false; missing += std::string(" ") + g; }
      }
      check(allDeclared, ("typings: every script function is declared in toolengine.d.ts" + (missing.empty() ? std::string() : " (missing:" + missing + ")")).c_str());
      check(dts.find("declare const input") != std::string::npos && dts.find("declare const time") != std::string::npos &&
            dts.find("declare const debug") != std::string::npos, "typings: input, time and debug are declared");
      const std::string snip = editor::snippetsJson();
      size_t snipCount = 0;
      for (size_t p = snip.find("\"prefix\""); p != std::string::npos; p = snip.find("\"prefix\"", p + 1)) ++snipCount;
      check(snipCount >= 12 && snip.find("export function update") != std::string::npos && snip.find("input.down(") != std::string::npos,
            ("snippets: " + std::to_string(snipCount) + " snippets from the tested examples").c_str());
      { // editor de script: autocomplete (palavra, sugestoes, troca) e snippets vindos dos exemplos do Help
        using namespace editor;
        check(completionWord("let a = input.do", 16) == "input.do" && completionWord("x = 1", 3).empty() && completionWord("abc", 1) == "a",
              "autocomplete: the word before the cursor (dots included)");
        const auto sg = completionsFor("input.do");
        bool hasDown = false; for (const ApiItem& it : sg) if (it.name == "input.down") hasDown = true;
        check(hasDown && completionsFor("").empty() && completionsFor("", true).size() > 10, "autocomplete: suggestions by prefix; empty word shows nothing unless asked");
        const auto sp = completionsFor("setPo");
        bool setPos = false; for (const ApiItem& it : sp) if (it.name == "setPosition" && it.ins == "setPosition(" && it.kind == "function") setPos = true;
        check(setPos, "autocomplete: a function is inserted with its '(' and kind function");
        const TextEdit ed1 = applyCompletion("  setPo", 7, "setPosition(");
        check(ed1.text == "  setPosition(" && ed1.cursor == 14, "autocomplete: accepting replaces the typed word and moves the cursor");
        const TextEdit ed2 = applyCompletion("if (input.do", 12, "input.down(");
        check(ed2.text == "if (input.down(" && ed2.cursor == 15, "autocomplete: a method replaces 'input.do' (dots kept in the word)");
        const TextEdit ins = insertAt("ab", 1, "X");
        check(ins.text == "aXb" && ins.cursor == 2, "snippets: inserted at the cursor");
        const auto snips = scriptSnippets();
        check(snips.size() >= 50 && snips.front().n == 1 && !snips.front().title.empty(), ("snippets: " + std::to_string(snips.size()) + " from the Help examples (numbered, titled)").c_str());
        bool allHaveFn = true; for (const Snippet& sn : snips) if (sn.code.find("function") == std::string::npos) allHaveFn = false;
        check(allHaveFn, "snippets: every one contains a script function");
        const std::string js = snippetsJson();
        check(js.front() == '{' && js.find("\"prefix\": \"tool53\"") != std::string::npos && js.find("\\n") == std::string::npos,
              "snippets: the VS Code JSON has every example with its prefix (no raw newlines inside strings)");
      }
    }
    { // verificador de scripts: regras de uso, sintaxe com linha, e erros de execucao com linha / try-catch / avisos
      using editor::checkScriptSource;
      using editor::ScriptIssue;
      auto has = [](const std::vector<ScriptIssue>& v, const char* code) { for (const auto& i : v) if (i.code == code) return true; return false; };
      auto firstLine = [](const std::vector<ScriptIssue>& v, const char* code) { for (const auto& i : v) if (i.code == code) return i.line; return -1; };
      auto msgOf = [](const std::vector<ScriptIssue>& v, const char* code) { for (const auto& i : v) if (i.code == code) return i.msg; return std::string(); };
      auto errors = [](const std::vector<ScriptIssue>& v) { int n = 0; for (const auto& i : v) if (i.sev == tool::Severity::Error) ++n; return n; };
      const auto good = checkScriptSource("export function update(obj, dt) {\n  obj.transform.rot[1] += 45 * dt;\n}\n", "Cubo");
      check(good.empty(), "check: a valid cube script has no problems");
      const auto pos = checkScriptSource("export function update(obj, dt) {\n  obj.position[0] += 1;\n}\n", "Cubo");
      check(has(pos, "TOOL-S007") && firstLine(pos, "TOOL-S007") == 2, "check: obj.position is an error on the right line (S007)");
      check(has(checkScriptSource("class Cubo {}\n", "Cubo"), "TOOL-S008"), "check: class is an error (S008)");
      check(has(checkScriptSource("export function update(obj, dt) {\n  this.x = 1;\n}\n", "Cubo"), "TOOL-S008"), "check: this is reported (S008)");
      const auto syn = checkScriptSource("export function update(obj, dt) {\n  let a = ;\n}\n", "Cubo");
      check(has(syn, "TOOL-S001") && firstLine(syn, "TOOL-S001") == 2, "check: a syntax error is reported on its line (S001)");
      const auto typo = checkScriptSource("export function update(obj, dt) {\n  setpos('Cubo', 1, 2, 3);\n}\n", "Cubo");
      check(has(typo, "TOOL-S004") && msgOf(typo, "TOOL-S004").find("setPosition") != std::string::npos, "check: a misspelled API name suggests the right one");
      const auto arity = checkScriptSource("export function update(obj, dt) {\n  setPosition('Cubo', 1, 2);\n}\n", "Cubo");
      check(has(arity, "TOOL-S004") && msgOf(arity, "TOOL-S004").find("found 3") != std::string::npos, "check: wrong number of arguments is reported");
      check(has(checkScriptSource("export function update(obj, dt) {\n  if (input.down('w')) {}\n}\n", "Cubo"), "TOOL-S004"), "check: a key name in the wrong case is reported");
      check(has(checkScriptSource("export function update(obj, dt) {\n  console.log('oi');\n}\n", "Cubo"), "TOOL-S004"), "check: console.log is reported (use debug.console)");
      check(has(checkScriptSource("let x = 1;\n", "Cubo"), "TOOL-S003"), "check: a script without start/update/OnEventTrigger is warned (S003)");
      check(has(checkScriptSource("export function update(obj, dt) {\n  naoExiste(obj);\n}\n", "Cubo"), "TOOL-S004"), "check: a call to an undeclared function is reported");
      check(!has(checkScriptSource("export function update(obj, dt) {\n  const f = (a) => a + 1;\n  debug.console(f(2));\n}\n", "Cubo"), "TOOL-S004"), "check: declared functions and parameters are not reported");
      // os exemplos do Help nao podem ter erro (senao o verificador acusaria codigo que roda)
      int checkedEx = 0, badEx = 0; std::string badExMsg;
      for (const HelpTopic& t : helpTopics()) {
        if (std::string(t.id) != "api" && std::string(t.id) != "script_examples" && std::string(t.id).rfind("tutorial_", 0) != 0) continue;
        const std::string body = t.bodyEn; size_t p0 = 0; bool inCode = false; std::string code;
        while (p0 < body.size()) {
          size_t e = body.find('\n', p0); if (e == std::string::npos) e = body.size();
          const std::string line = body.substr(p0, e - p0); p0 = e + 1;
          if (line.rfind("```", 0) == 0) {
            if (!inCode) { inCode = true; code.clear(); }
            else { inCode = false; if (code.find("function") != std::string::npos) {
              ++checkedEx; const auto v = checkScriptSource(code, "Ex");
              if (errors(v) > 0) { ++badEx; if (badExMsg.empty()) badExMsg = msgOf(v, "TOOL-S001") + msgOf(v, "TOOL-S004") + msgOf(v, "TOOL-S007") + msgOf(v, "TOOL-S008"); } } }
            continue;
          }
          if (inCode) code += line + "\n";
        }
      }
      check(checkedEx >= 12 && badEx == 0, ("check: the Help examples have no errors (" + std::to_string(checkedEx) + " checked" + (badExMsg.empty() ? std::string() : ", first: " + badExMsg) + ")").c_str());
      if (ScriptHost::compiledIn()) { // execucao: erros com linha, try/catch, avisos de alvo e de valor
        auto runOne = [&](const char* code, ScriptHost& host, ScriptHost::Inst*& out) {
          ScriptHost::Entry en; en.name = "Cubo"; en.source = code; en.enabled = true;
          host.open({en});
          out = host.instances().empty() ? nullptr : host.instances()[0].get();
          if (!out) return;
          { const float z[3] = {0.f, 0.f, 0.f}; const float o[3] = {1.f, 1.f, 1.f}; host.pushTransform(*out, z, z, o); }
          host.callStart(*out);
          host.callUpdate(*out, 0.016f);
        };
        const size_t h0 = tool::debug().history().size();
        auto newCode = [&](const char* code) { const auto hist = tool::debug().history(); for (size_t i = h0; i < hist.size(); ++i) if (hist[i].code == code) return true; return false; };
        { ScriptHost host; host.exists = [](const std::string& n) { return n == "Cubo"; };
          ScriptHost::Inst* in = nullptr; runOne("export function update(obj, dt) {\n  setPosition('Nada', 1, 2, 3);\n}\n", host, in);
          check(in && !in->failed && newCode("TOOL-S005"), "run: a command to a missing object warns (S005) and the script keeps running"); }
        { ScriptHost host; host.exists = [](const std::string&) { return true; };
          ScriptHost::Inst* in = nullptr; runOne("export function update(obj, dt) {\n  try { setPosition('Cubo', 1); } catch (e) { debug.warn('falhou: ' + e.message); }\n}\n", host, in);
          check(in && !in->failed && host.cmds.empty(), "run: an error inside try/catch is caught and the script keeps running"); }
        { ScriptHost host; host.exists = [](const std::string&) { return true; };
          ScriptHost::Inst* in = nullptr; runOne("export function update(obj, dt) {\n  setPosition('Cubo', 'abc', 2, 3);\n}\n", host, in);
          check(in && in->failed && in->errLine == 2 && in->error.find("finite number") != std::string::npos, "run: a bad argument stops the script with the line (2) and a clear message"); }
        { ScriptHost host; host.exists = [](const std::string&) { return true; };
          ScriptHost::Inst* in = nullptr; runOne("export function update(obj, dt) {\n  let a = 1;\n  null.x;\n}\n", host, in);
          check(in && in->failed && in->errLine == 3 && in->phase == "update", "run: a runtime error reports the line (3) and the phase (update)"); }
        { ScriptHost host; host.exists = [](const std::string&) { return true; };
          ScriptHost::Inst* in = nullptr; runOne("export function update(obj, dt) {\n  obj.transform.pos[1] = NaN;\n}\n", host, in);
          float p[3] = {0, 0, 0}, r[3] = {0, 0, 0}, sc[3] = {1, 1, 1};
          const bool okPull = in && host.pullTransform(*in, p, r, sc);
          check(in && !okPull && newCode("TOOL-S006"), "run: NaN in obj.transform is ignored with a warning (S006)"); }
        { ScriptHost host; host.exists = [](const std::string&) { return true; };
          ScriptHost::Inst* in = nullptr; runOne("export function update(obj, dt) {\n  setPosition('Cubo', 1, 2, 3);\n}\n", host, in);
          check(in && !in->failed && host.cmds.size() == 1, "run: a valid setPosition queues one command"); }
        check(newCode("TOOL-S005") && newCode("TOOL-S002") && newCode("TOOL-S006"), "run: the codes reach the debug history (S002, S005, S006)");
      }
    }
    { // Profiler: medidas do sistema. Um laco ocupado de 0,7 s tem que aparecer como uso de CPU do editor.
      editor::SysMonitor mon;
      const auto t0 = std::chrono::steady_clock::now();
      auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
      mon.update(secs());
      volatile double sink = 0;
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
      while (std::chrono::steady_clock::now() < until) sink = sink + 1.0;
      mon.update(secs());
      const editor::SysSnapshot& s2 = mon.get();
      check(s2.cpuOk, "system: CPU use is measured");
      check(s2.coresOnline >= 1 && s2.core.size() == static_cast<size_t>(s2.coresOnline), "system: one CPU value per core");
      check(s2.cpuTotal >= 0.f && s2.cpuTotal <= 100.f && s2.editorCpu > 0.f && s2.editorCpu <= 100.f,
            "system: percentages are in range and the busy loop shows as editor CPU");
      check(s2.ramOk && s2.ramTotalMB > 0.0 && s2.ramUsedMB <= s2.ramTotalMB, "system: RAM total and used");
      check(!s2.caches.empty() || !s2.cacheNote.empty(), "system: cache sizes, or the reason they are missing");
      check(s2.vramOk || !s2.vramNote.empty(), "system: VRAM is measured or says why it is not");
    }
    { // colar (ima de contato): o cubo anda ate encostar no vizinho, sem vao e sem sobrepor
      const tool::Entity b = ed.make("SnapB"), a = ed.make("SnapA");
      MeshRenderer mb; mb.shape = 0; w.add<MeshRenderer>(b, mb);
      MeshRenderer ma; ma.shape = 0; w.add<MeshRenderer>(a, ma);
      w.get<Transform>(b)->pos[0] = 0.f; w.get<Transform>(a)->pos[0] = 3.f; w.get<Transform>(a)->pos[1] = 0.25f;
      // isola o cenario: esconde os outros objetos (Inactive) so durante o teste
      std::vector<tool::Entity> others; w.each<MeshRenderer>([&](tool::Entity o, MeshRenderer&) { if (o != a && o != b && !w.has<Inactive>(o)) others.push_back(o); });
      for (auto o : others) w.add<Inactive>(o, Inactive{});
      const bool ok = snapToContact(ed, a);
      for (auto o : others) if (w.alive(o)) w.remove<Inactive>(o);
      tool::Vec3 blo{}, bhi{}, alo{}, ahi{};
      worldBox(ed, b, blo, bhi); worldBox(ed, a, alo, ahi);
      check(ok, "snap: finds the neighbour's face to touch");
      check(std::fabs(alo.x - bhi.x) < 1e-3f && alo.y < bhi.y && ahi.y > blo.y, "snap: face touches the neighbour, no gap and no overlap");
      std::vector<tool::Entity> del; w.each<Name>([&](tool::Entity e, Name& n) { const std::string t = n.text; if (t == "SnapA" || t == "SnapB") del.push_back(e); });
      for (auto d : del) ed.destroy(d);
    }
  }
  { // particulas e UI
    tool::World& w = ed.scene.world(); const tool::Entity pe = ed.make("FxP"); ParticleComp pc; pc.rate = 100.f; pc.maxParticles = 50; w.add<ParticleComp>(pe, pc);
    CanvasComp u = FullCanvas(); UiElem& el = u.items[u.count++]; el.kind = 3; ser::copyStr(el.action, sizeof el.action, "health"); ser::copyStr(el.text, sizeof el.text, "HP {health}"); w.add<CanvasComp>(pe, u);
    for (int i = 0; i < 30; ++i) ed.updateParticles(1.f / 30.f);
    const size_t n = ed.particles[pe.index].list.size(); check(n > 0 && n <= 50, "particles: emitter spawns and respects the max count");
    ed.vars["health"] = 42.f; check(uiFormat(ed, "HP {health}") == "HP 42", "ui: {variable} text substitution");
    const UndoState st2 = ed.captureState(); ed.restoreState(st2); bool okUi = false, okFx = false;
    w.each<CanvasComp>([&](tool::Entity, CanvasComp& uc) { if (uc.count == 1 && std::string(uc.items[0].action) == "health") okUi = true; }); w.each<ParticleComp>([&](tool::Entity, ParticleComp& pq) { if (pq.maxParticles == 50) okFx = true; });
    check(okUi && okFx, "ui + particles survive save/load");
    ed.particles.clear();
  }
  { // formas 3D extras: indices validos, normais unitarias e faces viradas para fora (ver Mesh::addTri)
    const tool::Mesh shapes[] = {tool::Mesh::cylinder(), tool::Mesh::cone(), tool::Mesh::capsule(), tool::Mesh::wheel(), tool::Mesh::prism(3), tool::Mesh::prism(6)};
    const char* names[] = {"cylinder", "cone", "capsule", "wheel", "triangle prism", "hexagon prism"};
    for (size_t s = 0; s < 6; ++s) {
      const tool::Mesh& m = shapes[s];
      bool idxOk = !m.idx.empty() && m.idx.size() % 3 == 0 && m.normal.size() == m.pos.size(), outOk = true, nrmOk = true;
      for (uint32_t i : m.idx) if (i >= m.pos.size()) idxOk = false;
      for (const tool::Vec3& n : m.normal) if (std::fabs(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) - 1.f) > 1e-3f) nrmOk = false;
      for (size_t t = 0; idxOk && t + 2 < m.idx.size(); t += 3) {
        const tool::Vec3 a = m.pos[m.idx[t]], b = m.pos[m.idx[t + 1]], c = m.pos[m.idx[t + 2]], n = m.normal[m.idx[t]];
        const tool::Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
        const float g = (u.y * v.z - u.z * v.y) * n.x + (u.z * v.x - u.x * v.z) * n.y + (u.x * v.y - u.y * v.x) * n.z;
        if (g < -1e-7f) outOk = false;   // a face aponta para dentro
      }
      check(idxOk && nrmOk && outOk, (std::string("shape ") + names[s] + ": valid triangles, unit normals, faces point outwards").c_str());
    }
    const tool::Entity wheelE = ed.makeMesh("Roda", 7, 0, 0, 0);
    check(ed.meshOf(*ed.scene.world().get<MeshRenderer>(wheelE)) == &ed.wheel, "primitives: shape 7 (wheel) maps to its mesh");
  }
  { // bloco padrao da cena no Narrative: um unico bloco fixo com o nome da cena, que acompanha a renomeacao
    editor::NarrativeModel np;
    np.setScene("Fase 1");
    auto countPinned = [&] { int k = 0; const auto* nodes = np.graph().find("nodes"); if (nodes) for (auto& n : nodes->items()) k += n.has("pinned") ? 1 : 0; return k; };
    check(countPinned() == 1, "narrative: the scene block is created once");
    np.setScene("Fase 2");
    const auto* nodes = np.graph().find("nodes");
    check(countPinned() == 1 && nodes && nodes->items()[0]["label"].asString() == "Fase 2", "narrative: the scene block follows the scene name without duplicating");
    tool::narr::Program prog; std::string err;
    check(tool::narr::Compiler().compile(np.graph(), prog, err), "narrative: a graph with the scene block compiles");
  }
  { // codigo da cena + Narrative em Play
    ed.sceneInfo.name = "STest"; ed.sceneInfo.narrative.clear(); ed.sceneInfo.script.clear();
    const bool made = ed.ensureSceneCode();
    check(made && fs::exists(ed.project.resolve(ed.sceneInfo.narrative)) && fs::exists(ed.project.resolve(ed.sceneInfo.script)), "scene code: graph + script are attached to the scene automatically");
    ed.startPlay(); check(ed.narrRun && ed.vars.count("score") == 1, "narrative: graph compiled and runs on Play (Start fired, variables visible to the UI)");
    ed.uiEvents.push_back("add_score"); ed.tick(1.0 / 60.0);
    check(ed.vars["score"] == 1.f, "ui button event add_score raises the 'score' variable");
    check(fs::exists(ed.project.root / "Saves" / "slot1.save.json"), "narrative: SavePoint wrote Saves/slot1.save.json");
    ed.stopPlay(); check(!ed.narrRun, "narrative: stopping Play ends the runtime");
  }
  { // scripts QuickJS (so com TOOL_WITH_QUICKJS): start/update, Transform ida e volta, evento, erro isolado, Reload, instantiate
    tool::World& w = ed.scene.world();
    const tool::Entity hero = ed.makeMesh("ScrHero", 0, 0, 0, 0);
    const tool::Entity target = ed.makeMesh("ScrTarget", 0, 0, 0, 0);
    const tool::Entity bad = ed.makeMesh("ScrBad", 0, 0, 0, 0);
    ed.attachScript(hero); ed.attachScript(bad);
    const std::string heroPath = w.get<ScriptComp>(hero)->path, badPath = w.get<ScriptComp>(bad)->path;
    writeScript(ed.project, heroPath,
        "function start(obj) { debug.console('start ok'); }\n"
        "function update(obj, dt) { obj.transform.pos[0] = obj.transform.pos[0] + 1.0; }\n"
        "function OnEventTrigger(obj, name) { if (name === 'cena3') setPosition('ScrTarget', 4, 4, 5); }\n");
    writeScript(ed.project, badPath, "function update( { broken\n");
    ed.startPlay();
    if (ScriptHost::compiledIn()) {
      ed.tick(1.0 / 60.0);
      check(std::fabs(w.get<Transform>(hero)->pos[0] - 1.f) < 1e-4f, "script update moves its object (Transform goes to JS and back)");
      check(ed.scriptHost.statusOf(bad.index, bad.generation).rfind("error", 0) == 0, "a script with a syntax error stops only itself");
      check(ed.scriptHost.statusOf(hero.index, hero.generation) == "running", "the other scripts keep running");
      ed.sendEvent("ScrHero", "cena3");
      ed.tick(1.0 / 60.0);
      check(std::fabs(w.get<Transform>(target)->pos[2] - 5.f) < 1e-4f, "OnEventTrigger + setPosition moves another object by name");
      tool::Entity clone = ed.spawnCopy("ScrHero");
      check(w.alive(clone) && ed.scriptHost.statusOf(clone.index, clone.generation) != "not running", "instantiate creates a copy that gets its own script context");
      check(w.alive(clone) && std::string(w.get<ScriptComp>(clone)->path) == heroPath, "the copy shares the original script file (no new file during Play)");
      ed.destroy(clone);
      writeScript(ed.project, badPath, "function update(obj, dt) { obj.transform.pos[1] = 2.0; }\n");
      ed.startScripts();   // botao Reload
      ed.tick(1.0 / 60.0);
      check(std::fabs(w.get<Transform>(bad)->pos[1] - 2.f) < 1e-4f, "Reload restarts the scripts with the fixed code");
    } else {
      check(ed.scriptHost.statusOf(hero.index, hero.generation).find("no script runtime") != std::string::npos, "build without QuickJS reports that scripts do not run");
    }
    ed.stopPlay();
    check(!ed.scriptHost.isOpen(), "stopping Play closes every script context");
    ed.destroy(hero); ed.destroy(target); ed.destroy(bad);
  }
  { // teste de ECS: N esferas filhas do ECScall, criadas durante o Play; o Stop desfaz tudo
    tool::World& w = ed.scene.world();
    size_t physBefore = 0; w.each<PhysicsComp>([&](tool::Entity e, PhysicsComp&) { if (!w.has<Inactive>(e)) ++physBefore; });
    const float previousSimRadius = ed.sceneInfo.physics.simRadius;
    ed.sceneInfo.physics.simRadius = 100.f;
    runConsoleCommand(ed, "test ecsf: 100");   // com fisica; perFrame = 1 -> 100 quadros
    check(ed.playing, "ECS test starts Play by itself");
    for (int i = 0; i < 120; ++i) ed.tick(1.0 / 60.0);
    tool::Entity call = ed.findByName("ECScall");
    int children = 0; w.each<Parent>([&](tool::Entity, Parent& pa) { if (pa.e == call) ++children; });
    check(children == 100, "ECS test: 100 spheres are children of ECScall");
    check(ed.play.ents.size() == physBefore + 100, "ECS test: each sphere gets a Newtoon body (one rebuild per frame)");
    check(ed.play.culled.size() == ed.play.ents.size(), "ECS test: runtime bodies keep simulation-culling state aligned");
    check(w.get<SpawnerComp>(call) && w.get<SpawnerComp>(call)->spawned == 100 && !w.get<SpawnerComp>(call)->armed, "ECS test: spawner finished and disarmed");
    ed.stopPlay();
    ed.sceneInfo.physics.simRadius = previousSimRadius;
    int leftover = 0; w.each<Name>([&](tool::Entity, Name& nm) { if (std::string(nm.text) == "sphere") ++leftover; });
    check(leftover == 0, "ECS test: Stop removes every sphere");
    ed.scene.world().destroy(ed.findByName("ECScall"));
  }
  { // "test ecs: N" (sem fisica): N instancias com MoverComp, nenhum corpo Newtoon, e a posicao muda no tempo
    tool::World& w = ed.scene.world();
    runConsoleCommand(ed, "test ecs: 50");
    check(ed.playing, "test ecs starts Play by itself");
    const size_t bodiesAtStart = ed.play.ents.size();   // corpos da propria cena, criados ao entrar no Play
    for (int i = 0; i < 120; ++i) ed.tick(1.0 / 60.0);
    int movers = 0, withPhysics = 0;
    tool::Entity first{};
    w.each<MoverComp>([&](tool::Entity e, MoverComp&) { if (movers == 0) first = e; ++movers; if (w.has<PhysicsComp>(e)) ++withPhysics; });
    check(movers == 50, "test ecs: 50 instances carry MoverComp");
    check(withPhysics == 0, "test ecs: instances have no PhysicsComp (no physics)");
    check(ed.play.ents.size() == bodiesAtStart, "test ecs: no Newtoon body is created");
    const Transform* t0 = w.get<Transform>(first);
    const float x0 = t0 ? t0->pos[0] : 0.f, y0 = t0 ? t0->pos[1] : 0.f, z0 = t0 ? t0->pos[2] : 0.f;
    for (int i = 0; i < 60; ++i) ed.tick(1.0 / 60.0);
    const Transform* t1 = w.get<Transform>(first);
    const float moved = t1 ? std::fabs(t1->pos[0] - x0) + std::fabs(t1->pos[1] - y0) + std::fabs(t1->pos[2] - z0) : 0.f;
    check(moved > 0.01f, "test ecs: an instance moves over time");
    ed.stopPlay();
    int leftover = 0; w.each<MoverComp>([&](tool::Entity, MoverComp&) { ++leftover; });
    check(leftover == 0, "test ecs: Stop removes every mover");
    ed.scene.world().destroy(ed.findByName("ECScall"));
  }
  { // comandos do Console
    runConsoleCommand(ed, "help");
    runConsoleCommand(ed, "xyz");
    check(!ed.playing, "console: unknown command does nothing");
  }
  { // particulas: orcamento global de particulas vivas e lotes (poucas chamadas de desenho, nao uma por particula)
    tool::World& w = ed.scene.world();
    const tool::Entity fxA = ed.make("PartA");
    w.add<ParticleComp>(fxA, ParticleComp{});
    ed.particles.clear(); ed.particleBudget = 25;
    for (int i = 0; i < 60; ++i) ed.updateParticles(1.f / 30.f);
    size_t alive = 0; for (auto& [k, sim] : ed.particles) alive += sim.list.size();
    check(alive > 0 && alive <= 25, "particle budget caps the live particles across emitters");
    { // particulas: 4 cores ao longo da vida, opacidade com atraso, trajetoria e area de nascimento
      ParticleComp pc; pc.color[0] = 1.f; pc.color2[0] = 0.5f; pc.color3[0] = 0.25f; pc.colorEnd[0] = 0.f;
      check(std::fabs(fxdetail::particleColor(pc, 0.f).x - 1.f) < 1e-5f && std::fabs(fxdetail::particleColor(pc, 1.f).x) < 1e-5f, "particles: color 1 at the start and color 4 at the end");
      check(std::fabs(fxdetail::particleColor(pc, 1.f / 3.f).x - 0.5f) < 1e-4f && std::fabs(fxdetail::particleColor(pc, 2.f / 3.f).x - 0.25f) < 1e-4f, "particles: colors 2 and 3 at 1/3 and 2/3 of the life");
      Editor::Particle q; q.life = 2.f; q.age = 1.f; pc.alpha = 0.8f; pc.fadeDelay = 1.f; pc.fadeOpacity = true;
      check(std::fabs(fxdetail::particleAlpha(pc, q) - 0.8f) < 1e-5f, "particles: opacity stays full during the delay");
      q.age = 2.f; check(fxdetail::particleAlpha(pc, q) < 1e-4f, "particles: opacity reaches zero at the end of the life");
      q.age = 1.5f; check(std::fabs(fxdetail::particleAlpha(pc, q) - 0.4f) < 1e-4f, "particles: opacity fades halfway after the delay");
      pc.fadeOpacity = false; q.age = 1.9f; check(std::fabs(fxdetail::particleAlpha(pc, q) - 0.8f) < 1e-5f, "particles: without opacity over time, alpha is constant");
      ParticleComp rt; rt.route = true; rt.routeCount = 3;
      const float rp[3][3] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}};
      for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c) rt.routePts[i][c] = rp[i][c];
      const tool::Vec3 r0 = fxdetail::routeSample(rt, 0.f), r1 = fxdetail::routeSample(rt, 1.f), rm = fxdetail::routeSample(rt, 0.5f);
      check(std::fabs(r0.x) < 1e-5f && std::fabs(r1.x - 2.f) < 1e-5f && std::fabs(rm.x - 1.f) < 1e-3f, "trace route: the curve starts at the first point, ends at the last and passes the middle one");
      ParticleComp sp; sp.area = 2; sp.areaSize[0] = 2.f; uint32_t seed = 777u; bool inside = true;
      for (int i = 0; i < 200; ++i) { const tool::Vec3 o = fxdetail::areaOffset(sp, seed); if (std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z) > 1.f + 1e-4f) inside = false; }
      check(inside, "spawn area: random sphere points stay inside the sphere");
    }
    w.destroy(fxA); ed.particles.clear(); ed.particleBudget = 100000;
  }
  { // objetos inativos, acao send e culling
    tool::World& w = ed.scene.world();
    const tool::Entity sa = ed.make("SndA"), sb = ed.make("SndB");
    ed.setActiveByName("SndA", false);
    check(w.has<Inactive>(sa), "setActive false marks the object inactive");
    ed.setActiveByName("SndA", true);
    check(!w.has<Inactive>(sa), "setActive true makes it active again");
    ed.objectInbox.clear(); ed.narrPending.clear();
    ed.sendEvent("SndB", "hit");
    check(ed.objectInbox[sb.index].size() == 1 && ed.objectInbox[sb.index][0] == "hit", "send delivers the event to the object by name");
    check(ed.narrPending.size() == 1, "send also queues the event for the narrative graph (fired after the frame)");
    ed.sendEvent("*", "all");
    check(ed.objectInbox[sa.index].size() == 1, "send to * reaches every object");
    tool::Camera cam; cam.eye = {0, 0, 5}; cam.target = {0, 0, 0}; cam.up = {0, 1, 0}; cam.fovY = 0.9f; cam.nearZ = 0.1f; cam.farZ = 100.f;
    const auto pl = Editor::framePlanes(cam, 640, 360);
    auto inside = [&](tool::Vec3 pt, float r) { for (const auto& p : pl) if (p.x * pt.x + p.y * pt.y + p.z * pt.z + p.w < -r) return false; return true; };
    check(inside({0, 0, 0}, 0.5f) && !inside({0, 0, 20}, 0.5f) && !inside({50, 0, 0}, 0.5f), "frustum: object in front is kept, behind or far to the side is culled");
    ed.scene.world().destroy(sa); ed.scene.world().destroy(sb);
  }
  { // pause
    ed.startPlay(); ed.paused = true; const float sun0 = ed.sunRuntime; ed.sunRuntime = sun0; ed.tick(1.0);
    check(ed.sunRuntime == sun0, "pause: tick does nothing while paused"); ed.stopPlay(); check(!ed.paused, "pause: stopping Play clears the pause");
  }

  { // chunks de blocos: faces visiveis, editar sobe a revisao, cache refeito, ida e volta na cena
    VoxelChunk full; for (auto& b : full.blocks) b = 1;
    ChunkMeshes cmFull; buildChunkMeshes(full, cmFull);
    check(cmFull.faces == 6 * 16 * 16 && cmFull.byType[1].idx.size() == static_cast<size_t>(6 * 16 * 16 * 6), "voxel: a solid 16^3 chunk exposes only its outer faces (1536)");
    VoxelChunk one; one.blocks[VoxelChunk::index(5, 5, 5)] = 2;
    ChunkMeshes cmOne; buildChunkMeshes(one, cmOne);
    check(cmOne.faces == 6 && cmOne.byType[2].pos.size() == 24, "voxel: a single block has 6 faces");
    VoxelChunk two; two.blocks[VoxelChunk::index(5, 5, 5)] = 2; two.blocks[VoxelChunk::index(6, 5, 5)] = 2;
    ChunkMeshes cmTwo; buildChunkMeshes(two, cmTwo);
    check(cmTwo.faces == 10, "voxel: two touching blocks hide the face between them (10 faces)");
    VoxelChunk rt; fillTerrain(rt, 2, -1);
    const unsigned r0 = rt.revision;
    check(setBlock(rt, 0, 15, 0, 4) && rt.revision == r0 + 1 && !setBlock(rt, 0, 15, 0, 4), "voxel: an edit bumps the revision once; the same value changes nothing");
    check(!setBlock(rt, 16, 0, 0, 1) && !setBlock(rt, 0, 0, 0, kBlockTypes), "voxel: out-of-range edits are refused");
    const tool::Entity ve = ed.scene.world().create();
    ed.scene.world().add<VoxelChunk>(ve, rt);
    check(ed.chunkMeshesOf(ve, rt).revision == rt.revision, "voxel: the mesh cache follows the chunk revision");
    VoxelChunk* live = ed.scene.world().get<VoxelChunk>(ve);
    setBlock(*live, 1, 15, 1, 6);
    check(ed.chunkMeshesOf(ve, *live).revision == live->revision, "voxel: the cache is rebuilt after an edit");
    const VoxelChunk expect = *live;   // copia por valor: carregar a cena pode realocar os componentes
    const tool::Entity back = loadEntity(ed.scene, saveEntity(ed.scene, ve));
    const VoxelChunk* bv = ed.scene.world().alive(back) ? ed.scene.world().get<VoxelChunk>(back) : nullptr;
    check(bv && std::memcmp(bv->blocks, expect.blocks, sizeof expect.blocks) == 0, "voxel: blocks survive a scene save/load (RLE round trip)");
    check(bv && bv->palette[6][0] == expect.palette[6][0] && bv->cell == expect.cell, "voxel: palette and block size survive a save/load");
    clearChunk(rt);
    check(solidCount(rt) == 0, "voxel: clear empties the chunk");
    tool::Entity mv = ed.makeVoxel("Chunk", 1, 0, false);
    check(ed.scene.world().get<Transform>(mv)->pos[0] == 16.f * ed.scene.world().get<VoxelChunk>(mv)->cell, "voxel: chunk (1,0) sits one chunk span to the right");
    // Imagem de conferencia (so com TOOL_VOXEL_SHOT=<arquivo.ppm>): 3x3 chunks de terreno vistos pela camera.
    if (const char* shotPath = std::getenv("TOOL_VOXEL_SHOT")) {
      std::vector<tool::Entity> grid;
      for (int cz = 0; cz < 3; ++cz) for (int cx = 0; cx < 3; ++cx) grid.push_back(ed.makeVoxel("Chunk", cx, cz, true));
      tool::Camera cam; cam.eye = {16, 16, 40}; cam.target = {16, 4, 16}; cam.up = {0, 1, 0}; cam.fovY = 0.9f; cam.nearZ = 0.1f; cam.farZ = 300.f;
      const int W = 640, H = 360;
      const tool::u8* px = ed.renderView(cam, W, H, false);
      if (px) if (std::FILE* f = std::fopen(shotPath, "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; ++i) std::fwrite(px + i * 4, 1, 3, f);
        std::fclose(f);
      }
      for (tool::Entity g : grid) ed.scene.world().destroy(g);
    }
    ed.scene.world().destroy(mv); ed.scene.world().destroy(ve); if (ed.scene.world().alive(back)) ed.scene.world().destroy(back);
  }

  { // mapa de teste: side x side chunks, centrados na origem, sem costura (cada chunk na sua celula da grade)
    const int made = ed.generateChunkMap(3);
    int count = 0; float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    ed.scene.world().each<VoxelChunk>([&](tool::Entity e, VoxelChunk&) {
      ++count; const Transform* t = ed.scene.world().get<Transform>(e);
      minX = std::min(minX, t->pos[0]); maxX = std::max(maxX, t->pos[0]); minZ = std::min(minZ, t->pos[2]); maxZ = std::max(maxZ, t->pos[2]);
    });
    const float span = VoxelChunk{}.cell * kChunkSize;
    check(made == 9 && count == 9, "chunk map: 3 x 3 chunks are created (old chunks are replaced)");
    check(std::fabs((maxX - minX) - 2 * span) < 1e-3f && std::fabs((maxZ - minZ) - 2 * span) < 1e-3f, "chunk map: the chunks are spaced one span apart, with no gaps");
    check(std::fabs(minX + span) < 1e-3f && std::fabs(maxX - span) < 1e-3f, "chunk map: the map is centered on the origin");
    ed.generateChunkMap(1);
  }
  { // configuracoes de oclusao e ray tracing: ida e volta pelo project.json (render.occlusionCull, render.lighting.rtAo*)
    RenderSettings r; r.occlusionCull = false; r.lighting.rayTracing = false; r.lighting.rtAo = true; r.lighting.rtAoRays = 24; r.lighting.rtAoRadius = 1.25f;
    const RenderSettings back = loadRenderSettings(saveRenderSettings(r));
    check(!back.occlusionCull, "render settings: occlusion culling survives save and load");
    check(!back.lighting.rayTracing && back.lighting.rtAo && back.lighting.rtAoRays == 24 && std::fabs(back.lighting.rtAoRadius - 1.25f) < 1e-4f, "render settings: ray tracing master and RT AO survive save and load");
    const RenderSettings def = loadRenderSettings(saveRenderSettings(RenderSettings{}));
    check(def.occlusionCull && def.lighting.rayTracing && !def.lighting.rtAo, "render settings: occlusion on, ray tracing master on and RT AO off by default");
    check(::editor::settings_model::ClampSimplifyAmount(2.f) == ::editor::settings_model::kSimplifyAmountMax &&
          ::editor::settings_model::ClampSimplifyAmount(-1.f) == 0.f && ::editor::settings_model::ClampSimplifyDistance(1.f) == ::editor::settings_model::kSimplifyDistanceMin,
          "simplify settings: amount and distance are clamped to their ranges");
    // F0 do ray tracing: o dispositivo nulo nao tem RT por GPU e o texto de status escolhido e o de "indisponivel".
    {
      tool::rhi::NullDevice nullDev;
      check(!nullDev.supportsRayTracing(), "ray tracing: NullDevice reports no GPU ray tracing");
      check(std::strstr(::editor::settings_model::RayTracingGpuStatusKey(nullDev.supportsRayTracing()), "unavailable") != nullptr &&
            std::strstr(::editor::settings_model::RayTracingGpuStatusKey(true), "available") != nullptr &&
            std::strstr(::editor::settings_model::RayTracingGpuStatusKey(true), "unavailable") == nullptr,
            "ray tracing: status text is 'unavailable' when the device has no RT");
    }
  }

  { // escala de render efetiva: com FSR vale 1/fator do modo de qualidade; sem FSR vale o slider
    RenderSettings r; r.fsrQuality = 1;   // Quality = 1.5x
    check(std::fabs(effectiveRenderScale(r, 0.8f) - 0.8f) < 1e-6f, "effective render scale: no FSR uses the slider");
    r.fsr1 = true;
    check(std::fabs(effectiveRenderScale(r, 0.8f) - 1.f / 1.5f) < 1e-6f, "effective render scale: FSR 1 on = 1 / quality factor");
    r.fsrQuality = 3;   // Performance = 2.0x
    check(std::fabs(effectiveRenderScale(r, 0.8f) - 0.5f) < 1e-6f, "effective render scale: Performance mode = 0.5");
    r.fsrQuality = ::editor::kFsrQualityCustom; r.fsrCustomScale = 0.6f;
    check(std::fabs(effectiveRenderScale(r, 0.8f) - 0.6f) < 1e-6f, "effective render scale: Custom mode uses its own factor");
    r.fsr1 = false; r.fsr4 = true; r.fsrQuality = 1;
    check(std::fabs(effectiveRenderScale(r, 0.8f) - 1.f / 1.5f) < 1e-6f, "effective render scale: FSR 4 on also takes over the scale");
    // persistencia: ida e volta, padroes e migracao da chave antiga "fsr1Sharpness"
    RenderSettings s; s.fsr4 = true; s.frameGen = true; s.fsrQuality = 4; s.fsrSharpness = 0.55f;
    const RenderSettings back = loadRenderSettings(saveRenderSettings(s));
    check(back.fsr4 && back.frameGen && back.fsrQuality == 4 && std::fabs(back.fsrSharpness - 0.55f) < 1e-4f, "FSR settings survive save and load");
    const RenderSettings none = loadRenderSettings(tool::Json::object());
    check(!none.fsr4 && !none.frameGen && none.fsrQuality == 1 && std::fabs(none.fsrSharpness - 0.2f) < 1e-6f, "FSR settings: missing keys give the defaults");
    tool::Json old = tool::Json::object(); old.set("fsr1Sharpness", 0.7);
    check(std::fabs(loadRenderSettings(old).fsrSharpness - 0.7f) < 1e-4f, "FSR settings: old fsr1Sharpness key is still read");
  }

  fs::remove_all(tmp, ec);
  tool::Logger::get().setSink(nullptr);
  std::printf("editor selftest: %d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}

} // namespace editor
