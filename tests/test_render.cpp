#include "test.hpp"
#include "tool/render/lighting.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/shadow.hpp"
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

TEST(light_attenuation_window) {
  CHECK(attenuation(10.f, 10.f) == 0.f);
  CHECK(attenuation(1.f, 10.f) > attenuation(2.f, 10.f));
  CHECK(attenuation(9.9f, 10.f) < 1e-3f); // chega a zero suavemente
}

TEST(light_spot_cone) {
  const f32 inner = std::cos(0.3f), outer = std::cos(0.6f);
  CHECK(spotFactor({0, -1, 0}, {0, -1, 0}, inner, outer) == 1.f);
  CHECK(spotFactor({0, -1, 0}, {1, 0, 0}, inner, outer) == 0.f);
  const f32 mid = spotFactor({0, -1, 0}, {std::sin(0.45f), -std::cos(0.45f), 0}, inner, outer);
  CHECK(mid > 0.f && mid < 1.f);
}

TEST(light_gpu_layout_and_conversion) {
  Light l; l.type = LightType::Spot; l.position = {1, 2, 3}; l.direction = {0, -2, 0}; l.range = 7.f;
  GpuLight g = toGpu(l, 3);
  CHECK(g.type == 2 && g.shadowIndex == 3 && g.range == 7.f && g.direction[1] == -1.f);
  CHECK(g.spotCos < g.spotInnerCos);
}

TEST(shadow_cascade_splits_monotonic) {
  auto s = cascadeSplits(0.1f, 200.f, 4);
  CHECK(s.size() == 5 && s.front() == 0.1f && s.back() == 200.f);
  for (size_t i = 1; i < s.size(); ++i) CHECK(s[i] > s[i - 1]);
  CHECK(s[1] < 200.f / 4); // cascata proxima e curta
}

TEST(cluster_slice_log_distribution) {
  CHECK(clusterSlice(0.05f, 0.1f, 100.f, 24) == 0);
  CHECK(clusterSlice(100.f, 0.1f, 100.f, 24) == 23);
  CHECK(clusterSlice(1.f, 0.1f, 100.f, 24) < clusterSlice(10.f, 0.1f, 100.f, 24));
}

TEST(cluster_assignment_matches_bruteforce_sphere_test) {
  ClusterGrid g{8, 4, 12};
  Mat4 view = Mat4::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  std::vector<Light> ls(3);
  ls[0].position = {0, 0, -10}; ls[0].range = 2.f;
  ls[1].position = {0, 0, 10};  ls[1].range = 2.f;   // atras da camera: em nenhum cluster
  ls[2].type = LightType::Directional;                // fora dos clusters
  auto a = assignLights(g, view, 1.0f, 16.f / 9.f, 0.1f, 100.f, ls);
  size_t withLight0 = 0;
  for (u32 c = 0; c < g.count(); ++c)
    for (u32 k = 0; k < a.counts[c]; ++k) {
      u32 li = a.indices[a.offsets[c] + k];
      CHECK(li == 0);
      ++withLight0;
    }
  CHECK(withLight0 > 0);
  // o cluster central na profundidade da luz deve conte-la
  u32 iz = clusterSlice(10.f, 0.1f, 100.f, g.z);
  u32 center = (iz * g.y + 2) * g.x + 4;
  bool found = false;
  for (u32 k = 0; k < a.counts[center]; ++k) found |= a.indices[a.offsets[center] + k] == 0;
  CHECK(found);
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
bool px(const SoftRenderer& r, int x, int y, u8& R, u8& G, u8& B) { if (x < 0 || y < 0 || x >= static_cast<int>(r.width()) || y >= static_cast<int>(r.height())) return false; const u8* p = r.pixels() + (static_cast<size_t>(y) * r.width() + static_cast<size_t>(x)) * 4; R = p[0]; G = p[1]; B = p[2]; return true; }
}

TEST(soft_renderer_draws_lit_cube_in_center) {
  SoftRenderer r(160, 90);
  Camera cam; cam.eye = {0, 0, 4}; cam.target = {0, 0, 0};
  r.begin(cam);
  Light l; l.type = LightType::Point; l.position = {2, 2, 3}; l.range = 20.f; l.intensity = 30.f;
  r.setLights({l});
  r.draw(Mesh::cube(0.5f), Mat4(), Material{{0.9f, 0.2f, 0.2f}, 0.6f, 0.f});
  u8 R = 0, G = 0, B = 0;
  CHECK(px(r, 80, 45, R, G, B) && R > G + 30);      // cubo vermelho iluminado no centro
  CHECK(px(r, 2, 2, R, G, B) && R < 100 && G < 100 && B < 100);  // fundo escuro
  CHECK(r.trianglesDrawn() > 0 && r.pixelsShaded() > 100);
}

TEST(soft_renderer_depth_test_front_occludes_back) {
  SoftRenderer r(100, 100);
  Camera cam; cam.eye = {0, 0, 6}; cam.target = {0, 0, 0}; r.begin(cam);
  Light sun; sun.type = LightType::Directional; sun.direction = {0, 0, -1}; sun.intensity = 3.f; r.setLights({sun});
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

TEST(gpu_renderer_on_null_device_draws_and_caches_meshes) {
  Quiet q; auto dev = rhi::createNullDevice();
  GpuRenderer gr; CHECK(gr.init(*dev, std::string(TOOL_SOURCE_DIR) + "/assets/shaders", 320, 180));
  Mesh cube = Mesh::cube(), sph = Mesh::sphere();
  Light l; l.type = LightType::Point; l.position = {2, 3, 2}; l.intensity = 10;
  for (int f = 0; f < 3; ++f) {
    gr.begin(Camera{}, {l});
    gr.draw(cube, Mat4(), Material{}); gr.draw(sph, Mat4::translation({1, 0, 0}), Material{});
    gr.end(); dev->present();
  }
  CHECK(gr.cachedMeshes() == 2 && gr.draws() == 2);
  CHECK(dev->frameStats().draws == 2 && dev->frameStats().triangles == 12 + static_cast<u32>(sph.idx.size() / 3));
  CHECK(debug().count(Severity::Error) == 0);
  gr.resize(640, 360); CHECK(gr.width() == 640);
}

TEST(gpu_renderer_reports_missing_shaders) {
  Quiet q; auto dev = rhi::createNullDevice(); GpuRenderer gr;
  CHECK(!gr.init(*dev, "/nao/existe", 64, 64) && debug().hasCode("TOOL-E140"));
}

TEST(hlsl_include_resolution) {
  std::string src; CHECK(loadHlsl(std::string(TOOL_SOURCE_DIR) + "/assets/shaders", "pbr.hlsl", src));
  CHECK(src.find("DistributionGGX") != std::string::npos && src.find("#include") == std::string::npos);
}

// ---------- v0.4: sombras em tempo real e pos-processamento ----------
namespace {
// Cena: sol + cubo flutuando sobre um plano. Soma o brilho do quadro para comparar com/sem sombras.
double shadowSceneLuma(bool shadows, bool casterCasts, bool groundReceives) {
  const Mesh ground = Mesh::plane(5.f), cube = Mesh::cube(1.2f);
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.4f, -1.f, -0.3f}; sun.intensity = 3.f; sun.castShadow = true;
  Camera cam; cam.eye = {0, 9, 0.01f}; cam.target = {0, 0, 0};
  SoftRenderer r(160, 160);
  r.begin(cam, {0.02f, 0.02f, 0.03f}); r.setLights({sun});
  ShadowSettings st; st.enabled = shadows; st.resolution = 512;
  r.setShadow(st, makeSunShadow(sun.direction, {0, 0, 0}, 8.f, st.resolution));
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

TEST(shadow_frustum_is_valid_and_texel_snapped) {
  CHECK(!makeSunShadow({0, 0, 0}, {0, 0, 0}, 10.f, 1024).valid);          // direcao nula
  const auto a = makeSunShadow({-0.3f, -1.f, 0.2f}, {0.001f, 0, 0}, 10.f, 1024), b = makeSunShadow({-0.3f, -1.f, 0.2f}, {0.002f, 0, 0}, 10.f, 1024);
  CHECK(a.valid && std::abs(a.texelWorld - 20.f / 1024.f) < 1e-6f);
  for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) CHECK(std::abs(a.viewProj.m[c][r] - b.viewProj.m[c][r]) < 1e-4f); // pequenos movimentos nao mexem a matriz
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
  SoftRenderer r(96, 96); ShadowSettings st; st.enabled = true; st.resolution = 256;
  const ShadowFrustum fr = makeSunShadow(sun.direction, {0, 0, 0}, 8.f, st.resolution);
  Material g; g.castShadow = false; Material c; c.receiveShadow = false;
  auto frame = [&](bool reuse, float cubeX) {
    r.begin(cam, {0.02f, 0.02f, 0.03f}); r.setLights({sun}); r.setShadow(st, fr, reuse);
    r.draw(ground, Mat4(), g); r.draw(cube, Mat4::translation({cubeX, 3.f, 0}), c);
    return std::vector<u8>(r.pixels(), r.pixels() + 96 * 96 * 4);
  };
  const auto a = frame(false, 0.f);
  const auto b = frame(true, 0.f);
  CHECK(a == b); CHECK(r.shadowPassSkipped() && r.shadowTriangles() == 0);   // reaproveitou: nenhum triangulo de sombra rasterizado
  const auto d = frame(false, 2.f);
  CHECK(d != a);                                                               // objeto mexeu e o chamador pediu rebuild: sombra mudou
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
  r.setLights({}, {1, 1, 1});
  Material mat{{1, 1, 1}, 0.9f, 0.f, false, false}; mat.albedoMap = &t;
  r.draw(Mesh::plane(1.f), Mat4{}, mat);
  u8 R = 0, G = 0, B = 0;
  px(r, 24, 32, R, G, B); CHECK(R > B);   // lado esquerdo: u perto de 0 -> vermelho
  px(r, 40, 32, R, G, B); CHECK(B > R);   // lado direito: u perto de 1 -> azul
}
