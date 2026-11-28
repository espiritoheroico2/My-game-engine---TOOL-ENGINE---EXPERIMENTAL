// Teste de corretude da oclusao por GPU (HiZ), comparada com a oclusao por software (CPU). Vulkan offscreen.
// Uso: occlusion_test [vulkan]
//
// CENA: uma parede grande (oclusor) com objetos ATRAS (devem sumir), NA FRENTE, ao LADO, espiando pela BORDA e ACIMA (devem
// ficar), mais uma camera dentro de um objeto. Mais ~90 caixas aleatorias (sem encostar) que se escondem umas as outras.
//
// VERDADE (independente de GPU e CPU): um objeto e "realmente visivel" se algum ponto da sua superficie voltada para a
// camera (cantos, meio das arestas, centros das faces) enxerga o olho sem que o segmento olho->ponto cruze outra caixa.
// FALSO OCULTO = o modo marcou como escondido um objeto realmente visivel. A exigencia e ZERO falsos ocultos, em varias poses.
// Tambem confere a eficacia (a GPU esconde os objetos atras da parede) e compara com o buffer de oclusao da CPU.
#include "tool/render/gpu_renderer.hpp"
#include "tool/render/occlusion.hpp"
#include "tool/rhi/diligent_device.hpp"

#include "LightPrism/Backends/DiligentDevice.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace tool;

namespace {
int failures = 0;
// Registra uma verificacao; imprime e conta falhas.
void check(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "[ok]  " : "[FALHA]", what.c_str()); if (!ok) ++failures; }

constexpr u32 kW = 1280, kH = 720;

enum Cat { Wall, Behind, Front, Side, Edge, Above, Near, Random };
const char* kCatName[] = {"wall", "behind", "front", "side", "edge", "above", "near", "random"};

struct Obj {
  Vec3 pos;          // centro
  Vec3 scale;        // escala por eixo (a caixa vai de pos - 0.5*h*scale a pos + ...)
  int mesh;          // 0 = cubo 0.5, 1 = cubo 0.4 (segundo lote)
  Cat cat;
  Vec3 half() const { const f32 h = mesh == 0 ? 0.5f : 0.4f; return {h * scale.x, h * scale.y, h * scale.z}; }
  f32 radius() const { const f32 h = mesh == 0 ? 0.5f : 0.4f; return h * 1.7320508f * std::max({scale.x, scale.y, scale.z}); }
  Mat4 world() const { return Mat4::translation(pos) * Mat4::scale(scale); }
};

// Chave de posicao (arredondada) para achar o objeto no resultado da GPU.
long long key(const Vec3& p) {
  auto q = [](f32 v) { return static_cast<long long>(std::llround(v * 100.f)) + 100000; };
  return (q(p.x) * 200003LL + q(p.y)) * 200003LL + q(p.z);
}

// Segmento a->b cruza a caixa (lo, hi)? Teste de lajes (slabs) com 1 mm de folga para dentro (nao conta encostar).
bool segmentHitsBox(const Vec3& a, const Vec3& b, const Vec3& lo, const Vec3& hi) {
  f32 t0 = 0.f, t1 = 1.f;
  const f32 s[3] = {a.x, a.y, a.z}, d[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, l[3] = {lo.x + 1e-3f, lo.y + 1e-3f, lo.z + 1e-3f}, h[3] = {hi.x - 1e-3f, hi.y - 1e-3f, hi.z - 1e-3f};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-9f) { if (s[i] < l[i] || s[i] > h[i]) return false; continue; }
    f32 ta = (l[i] - s[i]) / d[i], tb = (h[i] - s[i]) / d[i];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  return true;
}

// Verdade: o objeto i e realmente visivel da camera eye? (ver o cabecalho). Sem nenhuma face voltada: dentro da caixa = visivel.
bool trulyVisible(const std::vector<Obj>& objs, size_t i, const Vec3& eye) {
  const Obj& o = objs[i];
  const Vec3 h = o.half();
  const f32 e[3] = {eye.x - o.pos.x, eye.y - o.pos.y, eye.z - o.pos.z}, hh[3] = {h.x, h.y, h.z};
  bool inside = true;
  for (int a = 0; a < 3; ++a) if (std::fabs(e[a]) > hh[a]) inside = false;
  if (inside) return true;
  for (int sx = -1; sx <= 1; ++sx) for (int sy = -1; sy <= 1; ++sy) for (int sz = -1; sz <= 1; ++sz) {
    if (!sx && !sy && !sz) continue;
    const int sg[3] = {sx, sy, sz};
    bool facing = false;
    for (int a = 0; a < 3; ++a) if (sg[a] != 0 && static_cast<f32>(sg[a]) * e[a] > hh[a]) facing = true;
    if (!facing) continue;
    const Vec3 p{o.pos.x + sx * h.x, o.pos.y + sy * h.y, o.pos.z + sz * h.z};
    bool blocked = false;
    for (size_t j = 0; j < objs.size() && !blocked; ++j) {
      if (j == i) continue;
      const Vec3 hj = objs[j].half();
      blocked = segmentHitsBox(eye, p, objs[j].pos - hj, objs[j].pos + hj);
    }
    if (!blocked) return true;
  }
  return false;
}

// Cena fixa + caixas aleatorias que nao encostam em nada.
std::vector<Obj> buildScene() {
  std::vector<Obj> v;
  v.push_back({{0, 3, 0}, {12, 6, 0.5f}, 0, Wall});   // oclusor grande: x [-6,6], y [0,6], z [-0.25,0.25]
  for (int z : {-5, -8, -11}) for (int y = 0; y < 4; ++y) for (int x = -3; x <= 2; ++x) v.push_back({{x + 0.5f, 1.5f + static_cast<f32>(y), static_cast<f32>(z)}, {1, 1, 1}, 0, Behind});
  for (int x = -2; x <= 2; ++x) for (f32 y : {1.5f, 3.f, 4.5f}) v.push_back({{static_cast<f32>(x), y, 6.f}, {1, 1, 1}, 1, Front});
  for (f32 sx : {-11.f, 11.f}) for (f32 y : {1.f, 3.f, 5.f}) for (f32 z : {-4.f, -8.f}) v.push_back({{sx, y, z}, {1, 1, 1}, 0, Side});
  for (f32 sx : {-7.7f, 7.7f}) for (f32 y : {1.f, 3.f, 5.f}) v.push_back({{sx, y, -4.f}, {1, 1, 1}, 0, Edge});   // o raio ao centro toca a borda da parede
  for (f32 x : {-3.f, 0.f, 3.f}) for (f32 z : {-5.f, -9.f}) v.push_back({{x, 9.f, z}, {1, 1, 1}, 1, Above});
  v.push_back({{0, 3, 14}, {1, 1, 1}, 0, Near});   // a camera da pose 0 esta dentro deste objeto
  // Caixas aleatorias (LCG fixo), sem sobrepor nada (margem de 0.3).
  u32 s = 12345u;
  auto rnd = [&]() { s = s * 1664525u + 1013904223u; return static_cast<f32>((s >> 8) & 0xFFFF) / 65535.f; };
  for (int tries = 0; tries < 4000 && v.size() < 1 + 72 + 15 + 12 + 6 + 6 + 1 + 90; ++tries) {
    Obj o{{-14.f + 28.f * rnd(), 0.5f + 8.f * rnd(), -14.f + 24.f * rnd()}, {0.6f + 1.4f * rnd(), 0.6f + 1.4f * rnd(), 0.6f + 1.4f * rnd()}, rnd() > 0.5f ? 1 : 0, Random};
    bool ok = true;
    for (const Obj& p : v) {
      const Vec3 a = o.half(), b = p.half();
      if (std::fabs(o.pos.x - p.pos.x) < a.x + b.x + 0.3f && std::fabs(o.pos.y - p.pos.y) < a.y + b.y + 0.3f && std::fabs(o.pos.z - p.pos.z) < a.z + b.z + 0.3f) { ok = false; break; }
    }
    if (ok) v.push_back(o);
  }
  return v;
}
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  rhi::DeviceInfo info;
  info.backend = rhi::Backend::Vulkan; info.width = kW; info.height = kH;
  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar Vulkan: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s compute=%d\n", rhi::toString(dev->backend()), dev->deviceName().c_str(), dev->supportsCompute() ? 1 : 0);

  GpuRenderer gpu;
  if (!gpu.init(*dev, rhi::createLightPrismDevice(*dev), std::string(TOOL_SOURCE_DIR) + "/assets/shaders", kW, kH)) { std::fprintf(stderr, "FALHA: init do renderizador\n"); return 1; }
  check(gpu.gpuOcclusionAvailable(), "oclusao GPU disponivel (shaders e pipelines criados)");
  if (!gpu.gpuOcclusionAvailable()) return 1;

  LightRig rig;
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.5f, -1.f, -0.4f}; sun.intensity = 2.f; sun.castShadow = false;
  const std::vector<Light> lights = {sun};
  const Mesh meshes[2] = {Mesh::cube(0.5f), Mesh::cube(0.4f)};
  const std::vector<Obj> objs = buildScene();
  std::printf("cena: %zu objetos\n", objs.size());
  std::map<int, int> perCat;
  for (const Obj& o : objs) ++perCat[o.cat];
  for (const auto& [c, n] : perCat) std::printf("  %-7s %d\n", kCatName[c], n);

  struct Pose { const char* name; Vec3 eye, target; };
  const Pose poses[] = {{"frente", {0, 3, 14}, {0, 3, 0}}, {"lado-direita", {9, 6, 12}, {0, 2, 0}},
                        {"lado-esquerda", {-6, 1.5f, 10}, {0, 3, -2}}, {"cima", {0, 14, 7}, {0, 0, -4}}, {"atras-baixo", {2, 0.8f, -16}, {0, 3, 0}}};

  // Um quadro da GPU com a oclusao ligada ou desligada; devolve as chaves das instancias que o desenho indireto vai desenhar.
  auto gpuFrame = [&](const Camera& cam, bool occ, std::set<long long>& drawn) -> bool {
    const Mat4 v = Mat4::lookAt(cam.eye, cam.target, cam.up), p = cam.projection(static_cast<f32>(kW), static_cast<f32>(kH));
    rig.build(lights, {0.03f, 0.03f, 0.04f}, cam.eye, v, p.m[0][0], p.m[1][1], cam.nearZ, cam.farZ);
    gpu.setGpuOcclusion(occ);
    dev->beginFrame();
    gpu.begin(cam, {0.02f, 0.025f, 0.035f});
    if (!gpu.setLighting(rig, v, p)) { std::fprintf(stderr, "FALHA: luz da GPU: %s\n", gpu.lastError().c_str()); return false; }
    for (const Obj& o : objs) gpu.draw(meshes[o.mesh], o.world(), {{0.8f, 0.4f, 0.2f}, 0.5f, 0.f});
    gpu.end();
    dev->endFrame();
    std::vector<std::array<f32, 3>> pos;
    if (!gpu.readGpuVisiblePositions(pos)) return false;
    drawn.clear();
    for (const auto& q : pos) drawn.insert(key({q[0], q[1], q[2]}));
    return true;
  };

  int totalFalseGpu = 0, totalFalseCpu = 0, totalGpuHidden = 0, totalCpuHidden = 0, totalLiteral = 0;
  for (const Pose& ps : poses) {
    Camera cam; cam.eye = ps.eye; cam.target = ps.target; cam.nearZ = 0.1f; cam.farZ = 200.f;
    std::set<long long> frustumOnly, withOcc;
    if (!gpuFrame(cam, false, frustumOnly) || !gpuFrame(cam, true, withOcc)) { check(false, std::string("quadro GPU falhou na pose ") + ps.name); continue; }
    check(gpu.gpuOcclusionUsedLastFrame(), std::string("pose ") + ps.name + ": o teste de oclusao rodou na GPU");

    // CPU: mesmo criterio do editor (oclusores >= 8 px, esfera -> caixa, hidden()).
    const Mat4 view = Mat4::lookAt(cam.eye, cam.target, cam.up), proj = cam.projection(static_cast<f32>(kW), static_cast<f32>(kH));
    render::OcclusionBuffer occBuf;
    occBuf.begin(kW, kH, proj * view);
    const f32 tanHalf = std::tan(0.5f * cam.fovY);
    for (const Obj& o : objs) {
      const Mesh& m = meshes[o.mesh];
      const Mat4 wm = o.world();
      const f32 r = o.radius();
      const f32 dist = std::sqrt((o.pos.x - cam.eye.x) * (o.pos.x - cam.eye.x) + (o.pos.y - cam.eye.y) * (o.pos.y - cam.eye.y) + (o.pos.z - cam.eye.z) * (o.pos.z - cam.eye.z)) - r;
      if (dist <= cam.nearZ || r * (0.5f * static_cast<f32>(kH)) / (tanHalf * dist) < 8.f) continue;
      for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        if (i + 5 < m.idx.size() && m.idx[i + 3] == m.idx[i] && m.idx[i + 4] == m.idx[i + 2]) {
          occBuf.addQuad(wm.transformPoint(m.pos[m.idx[i]]), wm.transformPoint(m.pos[m.idx[i + 1]]), wm.transformPoint(m.pos[m.idx[i + 2]]), wm.transformPoint(m.pos[m.idx[i + 5]]));
          i += 3; continue;
        }
        occBuf.addTriangle(wm.transformPoint(m.pos[m.idx[i]]), wm.transformPoint(m.pos[m.idx[i + 1]]), wm.transformPoint(m.pos[m.idx[i + 2]]));
      }
    }
    // O CPU testa sobre a lista ja filtrada por frustum (mesma lista que a GPU recebe no estagio 2).
    std::set<long long> cpuVisible;
    for (const Obj& o : objs) {
      if (!frustumOnly.count(key(o.pos))) continue;
      const f32 r = o.radius();
      Vec3 box[8]; int k = 0;
      for (f32 x : {-r, r}) for (f32 y : {-r, r}) for (f32 z : {-r, r}) box[k++] = o.pos + Vec3{x, y, z};
      if (!occBuf.hidden(box)) cpuVisible.insert(key(o.pos));
    }

    int falseGpu = 0, falseCpu = 0, gpuHidden = 0, cpuHidden = 0, literal = 0, behindN = 0, behindGpu = 0, behindCpu = 0;
    std::map<int, int> hiddenByCat;
    for (size_t i = 0; i < objs.size(); ++i) {
      const long long k = key(objs[i].pos);
      if (!frustumOnly.count(k)) continue;   // fora do frustum: nao entra na comparacao
      const bool truth = trulyVisible(objs, i, ps.eye);
      const bool gpuVis = withOcc.count(k) > 0, cpuVis = cpuVisible.count(k) > 0;
      if (!gpuVis) { ++gpuHidden; ++hiddenByCat[objs[i].cat]; }
      if (!cpuVis) ++cpuHidden;
      if (truth && !gpuVis) { ++falseGpu; std::printf("   FALSO OCULTO (GPU): %s em (%.2f, %.2f, %.2f)\n", kCatName[objs[i].cat], objs[i].pos.x, objs[i].pos.y, objs[i].pos.z); }
      if (truth && !cpuVis) ++falseCpu;
      if (cpuVis && !gpuVis) ++literal;
      if (objs[i].cat == Behind) { ++behindN; if (!gpuVis) ++behindGpu; if (!cpuVis) ++behindCpu; }
    }
    std::printf("pose %-14s frustum %zu | GPU escondeu %d, CPU escondeu %d | falsos ocultos GPU %d, CPU %d | CPU-visivel e GPU-oculto %d | atras da parede: GPU %d/%d, CPU %d/%d\n",
                ps.name, frustumOnly.size(), gpuHidden, cpuHidden, falseGpu, falseCpu, literal, behindGpu, behindN, behindCpu, behindN);
    totalFalseGpu += falseGpu; totalFalseCpu += falseCpu; totalGpuHidden += gpuHidden; totalCpuHidden += cpuHidden; totalLiteral += literal;
    check(falseGpu == 0, std::string("pose ") + ps.name + ": zero falsos ocultos na GPU");
    if (std::string(ps.name) == "frente") {   // pose de referencia: a categoria do objeto decide o esperado
      check(behindGpu == behindN && behindN > 0, "pose frente: a GPU esconde todos os objetos atras da parede");
      for (int c : {Front, Side, Edge, Above, Near}) check(hiddenByCat[c] == 0, std::string("pose frente: GPU nao esconde objetos da categoria ") + kCatName[c]);
      check(withOcc.count(key(objs[0].pos)) > 0, "pose frente: a propria parede continua visivel");
    }
  }
  std::printf("TOTAL: GPU escondeu %d, CPU escondeu %d; falsos ocultos GPU %d, CPU %d; CPU-visivel e GPU-oculto %d (todos conferidos contra a verdade)\n",
              totalGpuHidden, totalCpuHidden, totalFalseGpu, totalFalseCpu, totalLiteral);
  check(totalFalseGpu == 0, "zero falsos ocultos na GPU em todas as poses");
  check(totalGpuHidden > 0, "a GPU escondeu pelo menos um objeto (o teste nao e vazio)");
  return failures ? 1 : 0;
}
