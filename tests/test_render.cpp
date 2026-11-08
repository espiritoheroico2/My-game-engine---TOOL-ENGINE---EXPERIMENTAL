#include "test.hpp"
#include "tool/render/lighting.hpp"
#include "tool/render/occlusion.hpp"
#include "LightPrism/Rt/Bvh.hpp"
#include "LightPrism/Rt/RtEffects.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/bloom.hpp"
#include "tool/rhi/null_device.hpp"

using namespace tool;
using namespace tool::rhi;

namespace { struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } }; }

TEST(math_mat_mul_and_translation) {
  Mat4 t = Mat4::translation({1, 2, 3});
  Vec3 p = t.transformPoint({1, 1, 1});
  CHECK(p.x == 2 && p.y == 3 && p.z == 4);
  Mat4 s = Mat4::scale({2, 2, 2}) * t; // escala depois de transladar
  Vec3 q = s.transformPoint({0, 0, 0});
  CHECK(q.x == 2 && q.y == 4 && q.z == 6);
}

TEST(math_perspective_depth_range_0_1) {
  Mat4 p = Mat4::perspective(1.0f, 1.f, 0.1f, 100.f);
  Vec3 nearP = p.transformPoint({0, 0, -0.1f}), farP = p.transformPoint({0, 0, -100.f});
  CHECK(std::abs(nearP.z) < 1e-4f && std::abs(farP.z - 1.f) < 1e-3f);
}

TEST(math_frustum_culling) {
  Mat4 vp = Mat4::perspective(1.0f, 1.f, 0.1f, 100.f) * Mat4::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  Frustum f = Frustum::fromViewProj(vp);
  CHECK(f.intersectsSphere({0, 0, -10}, 1.f));
  CHECK(!f.intersectsSphere({0, 0, 10}, 1.f));      // atras
  CHECK(!f.intersectsSphere({0, 0, -200}, 1.f));    // alem do far
  CHECK(!f.intersectsSphere({500, 0, -10}, 1.f));   // muito a direita
  AABB in; in.grow({-1, -1, -11}); in.grow({1, 1, -9});
  AABB out; out.grow({100, 0, -10}); out.grow({101, 1, -9});
  CHECK(f.intersectsAABB(in) && !f.intersectsAABB(out));
}

TEST(lightrig_maps_point_and_spot_lights_into_lightprism) {
  LightRig rig;
  Light p; p.type = LightType::Point; p.position = {0, 2, 0}; p.range = 8.f; p.intensity = 4.f;
  Light sp; sp.type = LightType::Spot; sp.position = {0, 3, 0}; sp.direction = {0, -1, 0}; sp.range = 10.f; sp.intensity = 6.f;
  const Mat4 v = Mat4::lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
  rig.build({p, sp}, {0, 0, 0}, {0, 2, 5}, v, 1.f, 1.f, 0.1f, 100.f);
  CHECK(rig.frame().points.size() == 1 && rig.frame().spots.size() == 1);
  CHECK(!rig.frame().hasSun && !rig.frame().shadows() && rig.frame().droppedLights == 0);
}

TEST(lightrig_budget_drops_the_lights_beyond_the_limit) {
  LightRig rig;
  LightingSettings s; s.maxPointLights = 2; rig.configure(s);
  std::vector<Light> ls(5); for (Light& l : ls) { l.type = LightType::Point; l.range = 5.f; }
  const Mat4 v = Mat4::lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
  rig.build(ls, {0, 0, 0}, {0, 2, 5}, v, 1.f, 1.f, 0.1f, 100.f);
  CHECK(rig.frame().points.size() == 2 && rig.frame().droppedLights == 3);
}

TEST(lightrig_sun_casts_cascades_only_when_enabled) {
  const Mat4 v = Mat4::lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.4f, -1.f, -0.3f}; sun.intensity = 3.f; sun.castShadow = true;
  LightRig rig;
  rig.build({sun}, {0, 0, 0}, {0, 2, 5}, v, 1.f, 1.f, 0.1f, 100.f);
  CHECK(rig.frame().hasSun && rig.frame().shadows());
  LightingSettings off; off.shadows = false; rig.configure(off);
  rig.build({sun}, {0, 0, 0}, {0, 2, 5}, v, 1.f, 1.f, 0.1f, 100.f);
  CHECK(rig.frame().hasSun && !rig.frame().shadows());   // sol continua, sem sombra
}

TEST(lightrig_presets_and_custom_values) {
  LightingSettings lo, hi; lo.tier = 0; hi.tier = 3;
  CHECK(makeQuality(hi).shadowMapResolution >= makeQuality(lo).shadowMapResolution);
  LightingSettings cu; cu.custom = true; cu.csmCascades = 2; cu.shadowMapResolution = 1024; cu.pcfTaps = 9;
  const LightPrism::QualitySettings q = makeQuality(cu);
  CHECK(q.csmCascades == 2 && q.shadowMapResolution == 1024 && q.pcfTaps == 9);
  LightingSettings mid; mid.tier = 2;
  cu.custom = false;   // sem custom, o preset manda (os valores de custom sao ignorados)
  CHECK(makeQuality(cu).shadowMapResolution == makeQuality(mid).shadowMapResolution);
}

TEST(rhi_backend_priority_and_fallback) {
  CHECK(chooseBackend(Platform::Windows, {Backend::Vulkan, Backend::D3D12}) == Backend::D3D12);
  CHECK(chooseBackend(Platform::Windows, {Backend::Vulkan}) == Backend::Vulkan);
  CHECK(chooseBackend(Platform::Web, {Backend::GLES}) == Backend::GLES);
  CHECK(chooseBackend(Platform::Linux, {}) == Backend::Null);
  Quiet q;
  CHECK(chooseBackend(Platform::Linux, {Backend::Vulkan}, Backend::D3D12) == Backend::Vulkan); // forcado indisponivel
  CHECK(debug().hasCode("TOOL-W120"));
}

TEST(rhi_null_device_draw_and_validation) {
  Quiet q;
  auto dev = createNullDevice();
  float verts[9] = {}; unsigned idx[3] = {0, 1, 2};
  auto vb = dev->createBuffer({sizeof verts, BufferUsage::Vertex, false}, verts);
  auto ib = dev->createBuffer({sizeof idx, BufferUsage::Index, false}, idx);
  ShaderDesc sd; sd.bytecode = {1, 2, 3};
  auto vs = dev->createShader(sd), ps = dev->createShader(sd);
  PipelineDesc pdesc; pdesc.vs = vs; pdesc.ps = ps; auto pipe = dev->createPipeline(pdesc);
  auto rt = dev->createTexture({64, 64, 1, Format::RGBA8, true}, nullptr);
  auto cl = dev->beginCommands();
  cl->begin(rt, kNullHandle, 0, 0, 0, 1);
  cl->setPipeline(pipe); cl->setVertexBuffer(vb); cl->setIndexBuffer(ib);
  cl->drawIndexed(3, 2);
  cl->end();
  dev->submit(*cl); dev->present();
  CHECK(dev->frameStats().draws == 1 && dev->frameStats().triangles == 2);
  CHECK(debug().count(Severity::Error) == 0);
  // erros de uso detectados
  auto cl2 = dev->beginCommands();
  cl2->begin(rt, kNullHandle, 0, 0, 0, 1);
  cl2->drawIndexed(3, 1); // sem pipeline
  cl2->end();
  CHECK(debug().hasCode("TOOL-E135"));
  dev->destroy(vb);
  dev->updateBuffer(vb, verts, 4); // handle obsoleto
  CHECK(debug().hasCode(diag::kStaleHandle));
  CHECK(dev->createBuffer({0, BufferUsage::Vertex, false}, nullptr).isNull());
  CHECK(dev->createPipeline(PipelineDesc{}).isNull());
}

#include "tool/render/soft_renderer.hpp"

namespace {
// Monta o quadro da Light Prism para a camera e as luzes dadas (o editor faz o mesmo a cada quadro).
void lightFrame(LightRig& rig, const std::vector<Light>& ls, Vec3 amb, const Camera& cam, u32 w, u32 h) {
  const Mat4 v = Mat4::lookAt(cam.eye, cam.target, cam.up), p = cam.projection(static_cast<f32>(w), static_cast<f32>(h));
  rig.build(ls, amb, cam.eye, v, p.m[0][0], p.m[1][1], cam.nearZ, cam.farZ);
}
}

namespace {
bool px(const SoftRenderer& r, int x, int y, u8& R, u8& G, u8& B) { if (x < 0 || y < 0 || x >= static_cast<int>(r.width()) || y >= static_cast<int>(r.height())) return false; const u8* p = r.pixels() + (static_cast<size_t>(y) * r.width() + static_cast<size_t>(x)) * 4; R = p[0]; G = p[1]; B = p[2]; return true; }
}

TEST(soft_renderer_draws_lit_cube_in_center) {
  SoftRenderer r(160, 90);
  Camera cam; cam.eye = {0, 0, 4}; cam.target = {0, 0, 0};
  r.begin(cam);
  Light l; l.type = LightType::Point; l.position = {2, 2, 3}; l.range = 20.f; l.intensity = 30.f;
  LightRig rig; lightFrame(rig, {l}, {}, cam, 160, 90); r.setLighting(rig.frame());
  r.draw(Mesh::cube(0.5f), Mat4(), Material{{0.9f, 0.2f, 0.2f}, 0.6f, 0.f});
  u8 R = 0, G = 0, B = 0;
  CHECK(px(r, 80, 45, R, G, B) && R > G + 30);      // cubo vermelho iluminado no centro
  CHECK(px(r, 2, 2, R, G, B) && R < 100 && G < 100 && B < 100);  // fundo escuro
  CHECK(r.trianglesDrawn() > 0 && r.pixelsShaded() > 100);
}

TEST(soft_renderer_depth_test_front_occludes_back) {
  SoftRenderer r(100, 100);
  Camera cam; cam.eye = {0, 0, 6}; cam.target = {0, 0, 0}; r.begin(cam);
  Light sun; sun.type = LightType::Directional; sun.direction = {0, 0, -1}; sun.intensity = 3.f;
  LightRig rig; lightFrame(rig, {sun}, {}, cam, 100, 100); r.setLighting(rig.frame());
  r.draw(Mesh::cube(1.f), Mat4::translation({0, 0, -2}), Material{{0, 0, 1}, 0.8f, 0.f});   // atras, azul
  r.draw(Mesh::cube(0.5f), Mat4(), Material{{1, 0, 0}, 0.8f, 0.f});                         // na frente, vermelho
  u8 R = 0, G = 0, B = 0; px(r, 50, 50, R, G, B);
  CHECK(R > B);                                                // vermelho na frente
  const Mesh& c = Mesh::cube(); (void)c;
}

TEST(soft_renderer_backface_culling_and_offscreen) {
  SoftRenderer r(64, 64);
  Camera cam; cam.eye = {0, 0, 4}; cam.target = {0, 0, 0}; r.begin(cam);
  r.draw(Mesh::cube(0.5f), Mat4::translation({0, 0, 20}), Material{});   // atras da camera
  CHECK(r.trianglesDrawn() == 0);
  r.draw(Mesh::cube(0.5f), Mat4::translation({100, 0, 0}), Material{});  // fora da tela
  CHECK(r.pixelsShaded() == 0);
  r.begin(cam); r.draw(Mesh::cube(0.5f), Mat4(), Material{});
  CHECK(r.trianglesDrawn() == 2);                                         // de frente so a face +Z (2 triangulos)
}

TEST(soft_renderer_project_point) {
  SoftRenderer r(200, 100); Camera cam; cam.eye = {0, 0, 5}; cam.target = {0, 0, 0}; r.begin(cam);
  f32 x, y; CHECK(r.project({0, 0, 0}, x, y) && std::abs(x - 100) < 0.5f && std::abs(y - 50) < 0.5f);
  CHECK(!r.project({0, 0, 10}, x, y));
}

#include "tool/render/gpu_renderer.hpp"

#ifndef TOOL_SOURCE_DIR
#define TOOL_SOURCE_DIR "."
#endif

TEST(gpu_renderer_needs_a_lightprism_device) {
  Quiet q; auto dev = rhi::createNullDevice(); GpuRenderer gr;
  CHECK(!gr.init(*dev, nullptr, std::string(TOOL_SOURCE_DIR) + "/assets/shaders", 320, 180) && debug().hasCode("TOOL-E142"));
}

TEST(hlsl_include_resolution) {
  std::string src; CHECK(loadHlsl(std::string(TOOL_SOURCE_DIR) + "/assets/shaders", "pbr.hlsl", src));
  CHECK(src.find("#include") == std::string::npos && src.find("EvaluateClusteredLights") != std::string::npos);
}

// ---------- v0.4: sombras em tempo real e pos-processamento ----------
namespace {
// Cena: sol + cubo flutuando sobre um plano. Soma o brilho do quadro para comparar com/sem sombras.
double shadowSceneLuma(bool shadows, bool casterCasts, bool groundReceives) {
  const Mesh ground = Mesh::plane(5.f), cube = Mesh::cube(1.2f);
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.4f, -1.f, -0.3f}; sun.intensity = 3.f; sun.castShadow = true;
  Camera cam; cam.eye = {0, 9, 0.01f}; cam.target = {0, 0, 0};
  SoftRenderer r(160, 160);
  r.begin(cam, {0.02f, 0.02f, 0.03f});
  LightingSettings st; st.shadows = shadows; st.shadowMapResolution = 512;
  LightRig rig; rig.configure(st); lightFrame(rig, {sun}, {}, cam, 160, 160); r.setLighting(rig.frame());
  Material g; g.receiveShadow = groundReceives; g.castShadow = false;
  Material c; c.castShadow = casterCasts; c.receiveShadow = false;
  r.draw(ground, Mat4(), g); r.draw(cube, Mat4::translation({0, 3.0f, 0}), c);
  double sum = 0; for (size_t i = 0; i < 160u * 160u * 4u; i += 4) sum += r.pixels()[i] + r.pixels()[i + 1] + r.pixels()[i + 2];
  return sum;
}
}

TEST(shadows_darken_receivers_only_when_enabled_and_cast) {
  const double on = shadowSceneLuma(true, true, true), off = shadowSceneLuma(false, true, true);
  CHECK(on < off * 0.998);                                   // sombra escurece o chao
  CHECK(std::abs(shadowSceneLuma(true, false, true) - off) < off * 1e-3);   // objeto sem castShadow nao projeta
  CHECK(std::abs(shadowSceneLuma(true, true, false) - off) < off * 1e-3);   // chao sem receiveShadow nao recebe
}

TEST(postfx_identity_is_free_and_exposure_brightens) {
  std::vector<u8> img(32 * 32 * 4, 100), ref = img;
  PostFx fx; applyPostFx(img.data(), 32, 32, fx);   // desligado: nao toca
  CHECK(img == ref);
  fx.enabled = true; fx.exposure = 1.f; CHECK(fx.active());
  applyPostFx(img.data(), 32, 32, fx);
  CHECK(img[0] > 190 && img[0] < 210);               // +1 EV dobra (100 -> ~200)
  PostFx g; g.enabled = true; g.exposure = 1.f; PostFx l; l.enabled = true; l.exposure = 0.5f; l.additive = true;
  CHECK(std::abs(combinePostFx(g, l).exposure - 1.5f) < 1e-5f);   // local soma ao global
  l.additive = false; CHECK(std::abs(combinePostFx(g, l).exposure - 0.5f) < 1e-5f);   // local substitui
}

TEST(postfx_vignette_darkens_corners_not_center) {
  std::vector<u8> img(64 * 64 * 4, 200);
  PostFx fx; fx.enabled = true; fx.vignette = 1.f; fx.vignetteSize = 0.3f; applyPostFx(img.data(), 64, 64, fx);
  CHECK(img[(32 * 64 + 32) * 4] > 190 && img[0] < 120);
}

TEST(parallel_for_covers_every_index_once_and_postfx_matches_reference) {
  std::vector<int> hits(10007, 0);
  for (int rep = 0; rep < 50; ++rep) tool::parallelFor(hits.size(), 64, [&](size_t b, size_t e) { for (size_t i = b; i < e; ++i) ++hits[i]; });
  bool ok = true; for (int h : hits) ok &= (h == 50); CHECK(ok);
  // referencia em float do caminho exposicao+contraste+saturacao+tint (sem vinheta/nitidez)
  const int w = 37, h = 23; std::vector<tool::u8> img(static_cast<size_t>(w * h * 4)); for (size_t i = 0; i < img.size(); ++i) img[i] = static_cast<tool::u8>((i * 53) & 255);
  std::vector<tool::u8> ref = img;
  tool::PostFx fx; fx.enabled = true; fx.exposure = 0.4f; fx.contrast = 1.2f; fx.saturation = 1.5f; fx.tintStrength = 0.3f; fx.tint[0] = 1.f; fx.tint[1] = 0.8f; fx.tint[2] = 0.6f;
  tool::applyPostFx(img.data(), w, h, fx);
  int worst = 0;
  for (size_t i = 0; i < ref.size(); i += 4) {
    float c[3]; for (int k = 0; k < 3; ++k) { float v = static_cast<float>(ref[i + static_cast<size_t>(k)]) / 255.f * std::exp2(fx.exposure); v = (v - 0.5f) * fx.contrast + 0.5f; c[k] = std::clamp(v, 0.f, 1.f) * 255.f; }
    const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    for (int k = 0; k < 3; ++k) { float v = l + (c[k] - l) * fx.saturation; v *= 1.f - fx.tintStrength + fx.tint[k] * fx.tintStrength; v = std::clamp(v, 0.f, 255.f); worst = std::max(worst, std::abs(static_cast<int>(v + 0.5f) - static_cast<int>(img[i + static_cast<size_t>(k)]))); }
  }
  CHECK(worst <= 2);
}

TEST(shadow_map_is_reused_when_nothing_changed_and_rebuilt_when_it_does) {
  const Mesh ground = Mesh::plane(5.f), cube = Mesh::cube(1.2f);
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.4f, -1.f, -0.3f}; sun.intensity = 3.f; sun.castShadow = true;
  Camera cam; cam.eye = {0, 9, 0.01f}; cam.target = {0, 0, 0};
  SoftRenderer r(96, 96); LightingSettings st; st.shadowMapResolution = 256;
  LightRig rig; rig.configure(st);
  Material g; g.castShadow = false; Material c; c.receiveShadow = false;
  auto frame = [&](bool reuse, float cubeX) {
    r.begin(cam, {0.02f, 0.02f, 0.03f}); lightFrame(rig, {sun}, {}, cam, 96, 96); r.setLighting(rig.frame(), reuse);
    r.draw(ground, Mat4(), g); r.draw(cube, Mat4::translation({cubeX, 3.f, 0}), c);
    return std::vector<u8>(r.pixels(), r.pixels() + 96 * 96 * 4);
  };
  const auto a = frame(false, 0.f);
  const auto b = frame(true, 0.f);
  CHECK(a == b); CHECK(r.shadowPassSkipped() && r.shadowTriangles() == 0);   // reaproveitou: nenhum triangulo de sombra rasterizado
  const auto d = frame(false, 2.f);
  CHECK(d != a);                                                               // objeto mexeu e o chamador pediu rebuild: sombra mudou
}

// --- sombras de luzes pontuais (cubo de 6 faces) e spot (1 face), feitas pela engine (a LightPrism so tem CSM do sol)
namespace {
// Luma (R+G+B) do chao logo abaixo do cubo, com uma luz pontual acima. blockerCasts = o cubo projeta sombra.
double pointShadowLuma(bool lightCasts, bool blockerCasts, u32 res = 512) {
  SoftRenderer r(160, 160);
  Camera cam; cam.eye = {0, 4, 4}; cam.target = {0, 0, 0};
  LightingSettings st; st.pointShadowLights = 2; st.pointShadowResolution = res;
  LightRig rig; rig.configure(st);
  Light l; l.type = LightType::Point; l.position = {0, 2.5f, 0}; l.range = 20.f; l.intensity = 30.f; l.castShadow = lightCasts;
  r.begin(cam, {0, 0, 0});
  lightFrame(rig, {l}, {0.2f, 0.2f, 0.2f}, cam, 160, 160); r.setLighting(rig.frame()); // ambiente: a sombra fica mensuravel
  Material blocker{{0.8f, 0.8f, 0.8f}, 0.8f, 0.f}; blocker.castShadow = blockerCasts;
  Material ground{{0.8f, 0.8f, 0.8f}, 0.8f, 0.f}; ground.castShadow = false; ground.receiveShadow = true;
  r.draw(Mesh::cube(0.4f), Mat4::translation({0, 1.f, 0}), blocker);
  r.draw(Mesh::plane(5.f), Mat4(), ground);
  f32 sx = 0, sy = 0; u8 R = 0, G = 0, B = 0;
  const bool okp = r.project({0, 0, 0}, sx, sy);
  if (!okp || !px(r, static_cast<int>(sx), static_cast<int>(sy), R, G, B)) return -1.0;
  return R + G + B;
}
}

TEST(point_light_shadow_darkens_ground_behind_blocker) {
  const double on = pointShadowLuma(true, true), off = pointShadowLuma(false, true), noCast = pointShadowLuma(true, false);
  CHECK(on > 0 && off > 0);
  CHECK(on < off * 0.6);                                  // o cubo na frente da luz pontual sombreia o chao
  CHECK(std::abs(noCast - off) < off * 1e-3 + 1.0);       // objeto sem castShadow nao projeta sombra
  CHECK(std::abs(pointShadowLuma(true, true, 1024) - on) < on * 0.15); // mais resolucao nao muda o quadro muito
}

TEST(point_and_spot_shadow_plan_respects_budget_and_faces) {
  Camera cam; cam.eye = {0, 0, 0}; cam.target = {0, 0, -1};
  Light near; near.type = LightType::Point; near.position = {0, 0, -2}; near.castShadow = true;
  Light far; far.type = LightType::Point; far.position = {0, 0, -9}; far.castShadow = true;
  Light spot; spot.type = LightType::Spot; spot.position = {0, 0, -5}; spot.direction = {0, 0, -1}; spot.outerAngle = 0.5f; spot.castShadow = true;
  LightingSettings st; st.pointShadowLights = 1; LightRig rig; rig.configure(st);
  lightFrame(rig, {far, near, spot}, {}, cam, 64, 64);
  CHECK(rig.frame().shadowLights.size() == 1);                     // orcamento 1: a mais proxima da camera
  CHECK(rig.frame().shadowLights[0].faces == 6 && rig.frame().shadowLights[0].position.z == -2.f);

  st.pointShadowLights = 3; rig.configure(st);
  lightFrame(rig, {far, near, spot}, {}, cam, 64, 64);
  CHECK(rig.frame().shadowLights.size() == 3);
  // ordem por distancia a camera: near (2 m, ponto, 6 faces), spot (5 m, 1 face), far (9 m, ponto, 6 faces)
  CHECK(rig.frame().shadowLights[1].type == LightType::Spot && rig.frame().shadowLights[1].faces == 1);
  CHECK(rig.frame().shadowLights[2].type == LightType::Point && rig.frame().shadowLights[2].faces == 6);

  st.pointShadowLights = 0; rig.configure(st);
  lightFrame(rig, {far, near, spot}, {}, cam, 64, 64);
  CHECK(rig.frame().shadowLights.empty());                         // orcamento 0 desliga
}

// --- upscaling e antialiasing por software (engine/render/include/tool/render/upscale.hpp)
#include "tool/render/upscale.hpp"

TEST(upscale_flat_image_stays_flat_for_every_filter) {
  // uma imagem lisa continua lisa em todos os filtros (pesos normalizados: soma 1)
  std::vector<uint8_t> src(8 * 6 * 4, 117);
  for (auto f : {tool::UpscaleFilter::Bilinear, tool::UpscaleFilter::Bicubic, tool::UpscaleFilter::Lanczos2}) {
    std::vector<uint8_t> dst(23 * 17 * 4, 0);
    tool::upscaleRgba(src.data(), 8, 6, dst.data(), 23, 17, f);
    bool flat = true; for (uint8_t v : dst) if (v != 117) { flat = false; break; }
    CHECK(flat);
  }
}

TEST(upscale_bilinear_linear_ramp_is_exact) {
  // gradiente horizontal 0..(w-1)*32: o bilinear reproduz a reta (sem erro de borda no meio)
  const int sw = 4, sh = 1, dw = 8, dh = 1;
  std::vector<uint8_t> src(sw * sh * 4, 0);
  for (int x = 0; x < sw; ++x) for (int c = 0; c < 4; ++c) src[static_cast<size_t>(x) * 4 + c] = static_cast<uint8_t>(x * 64);
  std::vector<uint8_t> dst(dw * dh * 4, 0);
  tool::upscaleRgba(src.data(), sw, sh, dst.data(), dw, dh, tool::UpscaleFilter::Bilinear);
  // destino 0: s = -0.25 -> clamp -> 0. destino 7: s = 3.25 -> clamp no ultimo pixel -> 192
  CHECK(dst[0] == 0);
  CHECK(std::abs(static_cast<int>(dst[7 * 4]) - 192) <= 1);
  // destino 3 (s = 0.875*... ) deve ficar monotonico crescente
  for (int x = 1; x < dw; ++x) CHECK(dst[static_cast<size_t>(x) * 4] >= dst[static_cast<size_t>(x - 1) * 4]);
}

TEST(upscale_lanczos_and_bicubic_keep_edges_monotonic_bounds) {
  // degrau preto->branco: os filtros de 4 taps nao passam de 0..255 (clamp) e continuam crescendo
  const int sw = 6, sh = 1, dw = 18;
  std::vector<uint8_t> src(sw * 4, 0);
  for (int x = 3; x < sw; ++x) for (int c = 0; c < 4; ++c) src[static_cast<size_t>(x) * 4 + c] = 255;
  for (auto f : {tool::UpscaleFilter::Bicubic, tool::UpscaleFilter::Lanczos2}) {
    std::vector<uint8_t> dst(dw * 4, 0);
    tool::upscaleRgba(src.data(), sw, 1, dst.data(), dw, 1, f);
    CHECK(dst[0] == 0 && dst[(dw - 1) * 4] == 255);
    for (int x = 1; x < dw; ++x) CHECK(dst[static_cast<size_t>(x) * 4] + 2 >= dst[static_cast<size_t>(x - 1) * 4]);
  }
}

TEST(fxaa_flat_image_unchanged_and_edge_is_softened) {
  const int w = 16, h = 8;
  std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4, 40);
  std::vector<uint8_t> scratch;
  tool::fxaaRgba(img.data(), w, h, scratch);
  bool same = true; for (uint8_t v : img) if (v != 40) { same = false; break; }
  CHECK(same);
  // borda DIAGONAL em escada (preto abaixo da diagonal x<y, branco acima): o FXAA deve criar tons intermediarios.
  // (uma borda vertical perfeitamente reta nao tem serrilhado; o FXAA corretamente nao a altera)
  std::vector<uint8_t> e(static_cast<size_t>(w) * h * 4, 0);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { const size_t i = (static_cast<size_t>(y) * w + x) * 4; const uint8_t v = x < y ? 0 : 255; e[i] = e[i + 1] = e[i + 2] = v; e[i + 3] = 255; }
  tool::fxaaRgba(e.data(), w, h, scratch);
  int mid = 0; for (int y = 1; y < h - 1; ++y) for (int x = 1; x < w - 1; ++x) { const int v = e[(static_cast<size_t>(y) * w + x) * 4]; if (v > 0 && v < 255) ++mid; }
  CHECK(mid > 0);
  CHECK(e[(static_cast<size_t>(3) * w + 0) * 4] == 0 && e[(static_cast<size_t>(3) * w + 15) * 4] == 255);   // longe da borda nada muda
}

// --- texturas no albedo (texture.hpp) ---------------------------------------------------------------------------
TEST(texture_texel_samples_nearest_and_repeats_outside_0_1) {
  Texture t; t.w = 2; t.h = 1; t.id = 1; t.rgba = {255, 0, 0, 255, 0, 0, 255, 255};   // esquerda vermelha, direita azul
  u8 o[4];
  t.texel(0.25f, 0.5f, o); CHECK(o[0] == 255 && o[2] == 0);
  t.texel(0.75f, 0.5f, o); CHECK(o[0] == 0 && o[2] == 255);
  t.texel(1.25f, 0.5f, o); CHECK(o[0] == 255);     // u = 1.25 repete u = 0.25
  t.texel(-0.25f, 0.5f, o); CHECK(o[2] == 255);    // u = -0.25 repete u = 0.75
}

TEST(texture_soft_renderer_plane_takes_the_texel_colours_by_uv) {
  Texture t; t.w = 2; t.h = 1; t.id = 2; t.rgba = {255, 0, 0, 255, 0, 0, 255, 255};
  SoftRenderer r(64, 64);
  Camera cam; cam.eye = {0, 5, 0}; cam.target = {0, 0, 0}; cam.up = {0, 0, -1};   // de cima: x cresce para a direita da tela
  r.begin(cam, {0, 0, 0});
  { LightRig rig; lightFrame(rig, {}, {1, 1, 1}, cam, 64, 64); r.setLighting(rig.frame()); }
  Material mat{{1, 1, 1}, 0.9f, 0.f, false, false}; mat.albedoMap = &t;
  r.draw(Mesh::plane(1.f), Mat4{}, mat);
  u8 R = 0, G = 0, B = 0;
  px(r, 24, 32, R, G, B); CHECK(R > B);   // lado esquerdo: u perto de 0 -> vermelho
  px(r, 40, 32, R, G, B); CHECK(B > R);   // lado direito: u perto de 1 -> azul
}

// ---- oclusao (tool/render/occlusion.hpp) ----------------------------------------------------------------
namespace {
// 8 cantos da caixa envolvente dos vertices de uma malha colocada no mundo por `m`.
void worldCorners(const Mesh& mesh, const Mat4& m, Vec3 out[8]) {
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (const Vec3& v : mesh.pos) {
    const Vec3 p = m.transformPoint(v);
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  int i = 0;
  for (f32 x : {lo.x, hi.x}) for (f32 y : {lo.y, hi.y}) for (f32 z : {lo.z, hi.z}) out[i++] = {x, y, z};
}
void boxCorners(Vec3 c, f32 h, Vec3 out[8]) {
  int i = 0;
  for (f32 x : {-h, h}) for (f32 y : {-h, h}) for (f32 z : {-h, h}) out[i++] = c + Vec3{x, y, z};
}
Mat4 viewProj(const Camera& cam, int w, int h) { return cam.projection(static_cast<f32>(w), static_cast<f32>(h)) * Mat4::lookAt(cam.eye, cam.target, cam.up); }
}

TEST(occlusion_buffer_hides_box_behind_wall) {
  Camera cam; cam.eye = {0, 0, 0}; cam.target = {0, 0, -1};
  render::OcclusionBuffer ob; ob.begin(320, 180, viewProj(cam, 320, 180));
  ob.addTriangle({-1, -1, -5}, {1, -1, -5}, {1, 1, -5});       // parede 2 x 2 em z = -5
  ob.addTriangle({-1, -1, -5}, {1, 1, -5}, {-1, 1, -5});
  Vec3 c[8];
  boxCorners({1.f, -0.8f, -10}, 0.5f, c); CHECK(ob.hidden(c));   // pequena, atras da parede e longe da diagonal: escondida
  // Com dois triangulos, a diagonal entre eles passa pelo centro e nenhuma celula cortada e coberta inteira: conservador.
  boxCorners({0, 0, -10}, 0.5f, c); CHECK(!ob.hidden(c));
  boxCorners({0, 0, -10}, 3.f, c);  CHECK(!ob.hidden(c));       // maior que a parede na tela: aparece pelas bordas
  boxCorners({0, 0, -2}, 0.5f, c);  CHECK(!ob.hidden(c));       // na frente da parede
  boxCorners({0, 0, -4}, 2.f, c);   CHECK(!ob.hidden(c));       // atravessa a parede: um canto esta na frente
  boxCorners({0, 0, 15}, 0.5f, c);  CHECK(!ob.hidden(c));       // atras da camera
  boxCorners({40, 0, -10}, 0.5f, c); CHECK(!ob.hidden(c));      // fora da tela: quem decide e o recorte
}

TEST(occlusion_culling_does_not_change_the_image) {
  // Mesma cena renderizada duas vezes: uma desenha tudo, a outra pula o que a oclusao esconde. Os pixels devem bater.
  Camera cam; cam.eye = {0, 0, 0}; cam.target = {0, 0, -1};
  const int W = 160, H = 90;
  Light sun; sun.type = LightType::Directional; sun.direction = {0, 0, -1}; sun.intensity = 3.f;
  const Mesh cube = Mesh::cube(1.f);
  const Mat4 wallM = Mat4::translation({0, 0, -5}) * Mat4::scale({2.f, 2.f, 0.1f});
  const Mat4 hiddenM = Mat4::translation({2.f, 0.f, -10}) * Mat4::scale({0.25f, 0.25f, 0.25f}); // longe da diagonal da parede (ocupa quase a tela)
  const Mat4 visibleM = Mat4::translation({1.2f, 0.3f, -4.f}) * Mat4::scale({0.6f, 0.6f, 0.6f});
  auto render = [&](bool cull, bool& hiddenDecision) {
    SoftRenderer r(W, H); r.begin(cam);
    LightRig rig; lightFrame(rig, {sun}, {}, cam, W, H); r.setLighting(rig.frame());
    render::OcclusionBuffer ob;
    hiddenDecision = false;
    if (cull) {
      ob.begin(W, H, viewProj(cam, W, H));
      // mesmo caminho do editor: pares de triangulos de uma face viram quad
      for (size_t i = 0; i + 2 < cube.idx.size(); i += 3) {
        const auto P = [&](size_t k) { return wallM.transformPoint(cube.pos[cube.idx[k]]); };
        if (i + 5 < cube.idx.size() && cube.idx[i + 3] == cube.idx[i] && cube.idx[i + 4] == cube.idx[i + 2]) { ob.addQuad(P(i), P(i + 1), P(i + 2), P(i + 5)); i += 3; continue; }
        ob.addTriangle(P(i), P(i + 1), P(i + 2));
      }
      Vec3 c[8]; worldCorners(cube, hiddenM, c); hiddenDecision = ob.hidden(c);
    }
    r.draw(cube, wallM, Material{{0.8f, 0.2f, 0.2f}, 0.8f, 0.f});
    if (!hiddenDecision) r.draw(cube, hiddenM, Material{{0.2f, 0.9f, 0.2f}, 0.8f, 0.f});
    r.draw(cube, visibleM, Material{{0.2f, 0.2f, 0.9f}, 0.8f, 0.f});
    return std::vector<u8>(r.pixels(), r.pixels() + static_cast<size_t>(W) * H * 4);
  };
  bool dummy = false, decided = false;
  const std::vector<u8> all = render(false, dummy);
  const std::vector<u8> culled = render(true, decided);
  CHECK(decided);                       // a caixa verde esta de fato escondida (o teste nao e trivial)
  CHECK(all == culled);                 // e pular o que esta escondido nao muda nenhum pixel
}

TEST(rt_ambient_occlusion_darkens_plane_next_to_a_wall) {
  // Camera do alto; parede entre x=0.3 e 0.4 (y 0..1). Pixel do chao perto da parede escurece; longe nao.
  Camera cam; cam.eye = {0, 4, 0}; cam.target = {0, 0, 0}; cam.up = {0, 0, -1};
  const Mesh wall = Mesh::cube(1.f);
  const Mat4 wallM = Mat4::translation({0.35f, 0.5f, 0.f}) * Mat4::scale({0.05f, 0.5f, 0.5f});
  std::vector<LightPrism::Rt::Triangle> tris;
  for (size_t i = 0; i + 2 < wall.idx.size(); i += 3) {
    const Vec3 a = wallM.transformPoint(wall.pos[wall.idx[i]]), b = wallM.transformPoint(wall.pos[wall.idx[i + 1]]), c = wallM.transformPoint(wall.pos[wall.idx[i + 2]]);
    tris.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}});
  }
  LightPrism::Rt::Bvh bvh; bvh.Build(tris);
  LightPrism::Rt::RtAoConfig cfg; cfg.rays = 32; cfg.radius = 0.6f;
  auto brightness = [&](bool rt, int col) {
    SoftRenderer r(64, 64); r.begin(cam, {0, 0, 0}, 1.f);
    LightRig rig; lightFrame(rig, {}, {0.3f, 0.3f, 0.3f}, cam, 64, 64); r.setLighting(rig.frame());
    r.draw(Mesh::plane(1.f), Mat4{}, Material{{1, 1, 1}, 0.9f, 0.f, false, false});
    if (rt) r.setRayTracedAo(&bvh, cfg);
    u8 R = 0, G = 0, B = 0; px(r, col, 32, R, G, B);
    return static_cast<int>(R);
  };
  const int near = brightness(true, 36);   // ~0.06 m da parede: muitos raios batem nela
  const int far = brightness(true, 19);    // ~1.1 m da parede: fora do raio de 0.6 m
  CHECK(far > 0);
  CHECK(near < far * 0.9f);             // medido: 141 perto contra 177 longe
}

TEST(occlusion_quad_covers_cells_crossed_by_the_diagonal) {
  // O mesmo quadrado do teste anterior, agora como quad plano: a caixa no centro (cortada pela diagonal) e escondida.
  Camera cam; cam.eye = {0, 0, 0}; cam.target = {0, 0, -1};
  render::OcclusionBuffer ob; ob.begin(320, 180, viewProj(cam, 320, 180));
  ob.addQuad({-1, -1, -5}, {1, -1, -5}, {1, 1, -5}, {-1, 1, -5});
  Vec3 c[8];
  boxCorners({0, 0, -10}, 0.5f, c); CHECK(ob.hidden(c));
  boxCorners({0, 0, -2}, 0.5f, c);  CHECK(!ob.hidden(c));      // na frente do quad
  // Quad nao plano: cai nos dois triangulos (a caixa do centro nao e escondida, por seguranca).
  render::OcclusionBuffer bent; bent.begin(320, 180, viewProj(cam, 320, 180));
  bent.addQuad({-1, -1, -5}, {1, -1, -5}, {1, 1, -4}, {-1, 1, -5});
  boxCorners({0, 0, -10}, 0.5f, c); CHECK(!bent.hidden(c));
}

TEST(point_shadow_maps_reused_when_only_the_camera_moves_give_the_same_image) {
  // Luz pontual com sombra e uma caixa que projeta sombra num chao. So a camera muda entre os quadros: reaproveitar
  // os mapas de face da luz (que nao dependem da camera) tem de dar exatamente a mesma imagem que renderizar do zero.
  Light pl; pl.type = LightType::Point; pl.position = {0, 2, 0}; pl.range = 10; pl.intensity = 5; pl.castShadow = true;
  Camera c1; c1.eye = {0, 1, 3}; c1.target = {0, 0.5f, -2}; c1.up = {0, 1, 0};
  Camera c2; c2.eye = {1.5f, 1.2f, 3}; c2.target = {0, 0.5f, -2}; c2.up = {0, 1, 0};
  const Mat4 boxM = Mat4::translation({0, 0.5f, -2});
  const Mesh box = Mesh::cube(0.5f);
  const Material boxMat{{0.8f, 0.4f, 0.2f}, 0.9f, 0.f, true, true};
  const Material floorMat{{0.7f, 0.7f, 0.7f}, 0.9f, 0.f, false, true};
  auto draw = [&](SoftRenderer& r) { r.draw(Mesh::plane(3.f), Mat4{}, floorMat); r.draw(box, boxM, boxMat); };

  SoftRenderer shared(96, 72);
  LightRig rigA; lightFrame(rigA, {pl}, {0.1f, 0.1f, 0.1f}, c1, 96, 72);
  shared.begin(c1, {0, 0, 0}, 1.f); shared.setLighting(rigA.frame(), false, false); draw(shared); (void)shared.pixels();

  LightRig rigB; lightFrame(rigB, {pl}, {0.1f, 0.1f, 0.1f}, c2, 96, 72);
  shared.begin(c2, {0, 0, 0}, 1.f); shared.setLighting(rigB.frame(), false, true); draw(shared);
  CHECK(shared.pointShadowPassSkipped());
  const std::vector<u8> reused(shared.pixels(), shared.pixels() + 96 * 72 * 4);

  SoftRenderer fresh(96, 72);
  fresh.begin(c2, {0, 0, 0}, 1.f); fresh.setLighting(rigB.frame(), false, false); draw(fresh);
  const std::vector<u8> full(fresh.pixels(), fresh.pixels() + 96 * 72 * 4);
  CHECK(reused == full);
}

TEST(light_rig_keeps_point_lights_registered_and_follows_changes) {
  // A lista de luzes e registrada so quando muda: parada, o quadro sai igual; movida, a nova posicao entra; removida, some.
  Camera cam; cam.eye = {0, 1, 3}; cam.target = {0, 0, 0}; cam.up = {0, 1, 0};
  Light pl; pl.type = LightType::Point; pl.position = {0, 2, 0}; pl.range = 6; pl.intensity = 3;
  LightRig rig;
  lightFrame(rig, {pl}, {}, cam, 64, 64);
  CHECK(rig.frame().points.size() == 1);
  const auto p1 = rig.frame().points.empty() ? LightPrism::PointLightGPU{} : rig.frame().points[0];
  lightFrame(rig, {pl}, {}, cam, 64, 64);                        // parada: mesmo resultado
  CHECK(rig.frame().points.size() == 1);
  CHECK(!rig.frame().points.empty() && rig.frame().points[0].position.x == p1.position.x && rig.frame().points[0].position.y == p1.position.y);
  Light moved = pl; moved.position = {3, 2, 0};
  lightFrame(rig, {moved}, {}, cam, 64, 64);                     // movida: a nova posicao entra
  CHECK(rig.frame().points.size() == 1 && std::fabs(rig.frame().points[0].position.x - 3.f) < 1e-4f);
  lightFrame(rig, {}, {}, cam, 64, 64);                          // removida: nao sobra nada
  CHECK(rig.frame().points.empty());
}


// Bloom (CPU): um pixel branco no centro espalha luz para os vizinhos, sem mudar o alfa nem o canto distante.
TEST(bloom_cpu_spreads_a_bright_pixel_and_keeps_alpha) {
  const unsigned W = 32, H = 32;
  std::vector<std::uint8_t> px(W * H * 4, 0);
  for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
  px[(16 * W + 16) * 4 + 0] = px[(16 * W + 16) * 4 + 1] = px[(16 * W + 16) * 4 + 2] = 255;
  tool::applyBloomRgba8(px.data(), W, H, 0.5f, 1.0f);
  auto red = [&](unsigned x, unsigned y) { return px[(y * W + x) * 4]; };
  CHECK(red(20, 16) > 0);     // vizinho proximo recebe halo
  CHECK(red(0, 0) == 0);      // canto longe continua preto
  bool alphaKept = true;
  for (std::size_t i = 3; i < px.size(); i += 4) alphaKept = alphaKept && px[i] == 255;
  CHECK(alphaKept);
}

// Imagem escura (abaixo do limiar) nao muda com o bloom ligado.
TEST(bloom_cpu_leaves_dark_image_unchanged) {
  const unsigned W = 16, H = 16;
  std::vector<std::uint8_t> px(W * H * 4, 60);
  const auto before = px;
  tool::applyBloomRgba8(px.data(), W, H, 0.75f, 0.8f);
  CHECK(px == before);
}
