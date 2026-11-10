// ToolEngine - SoftRenderer: rasterizador em CPU (alpha). Desenha a cena 3D iluminada sem GPU:
// serve de viewport do editor, de renderizador de referencia/teste e de fallback sem backend grafico.
// Iluminacao pela Light Prism (lighting.hpp): BRDF PBR, luzes pontuais/spot/sol, cascatas de sombra (CSM) e luz
// ambiente em SH9. O tonemap e ACES + sRGB, igual ao caminho GPU.
#pragma once
#include "tool/render/lighting.hpp"
#include "tool/render/texture.hpp"
#include "tool/core/parallel.hpp"

#include "LightPrism/Reference/Brdf.hpp"
#include "LightPrism/Reference/Tonemap.hpp"
#include "LightPrism/Rt/Bvh.hpp"
#include "LightPrism/Rt/RtEffects.hpp"
#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/Shadow/Csm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace tool {

// Malha triangulada: listas paralelas de posicao/normal (uma entrada por vertice) + indices (3 por triangulo,
// sentido anti-horario = face frontal, sistema de coordenadas de mao direita com Y para cima).
// `uv` e opcional (2 floats por vertice, origem no canto superior esquerdo, como no glTF); vazio = sem UV.
// Importadores (engine/assets) preenchem esta mesma estrutura, entao SoftRenderer e GpuRenderer desenham
// formas geradas por codigo e modelos importados pelo mesmo caminho.
struct Mesh {
  std::vector<Vec3> pos, normal;
  std::vector<u32> idx;
  std::vector<f32> uv;
  // Versao da malha: quem gera a malha de novo (ex.: chunk de blocos editado) sobe este numero. A GPU compara-o
  // para reenviar os buffers mesmo quando o tamanho e o endereco dos dados nao mudaram.
  u32 revision = 0;

  static Mesh plane(f32 half = 5.f) {
    Mesh m;
    m.pos = {{-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half}};
    m.normal = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    m.idx = {0, 2, 1, 0, 3, 2};
    m.uv = {0, 0, 1, 0, 1, 1, 0, 1};
    return m;
  }
  static Mesh cube(f32 h = 0.5f) {
    Mesh m;
    struct Face { Vec3 n, u, v; };
    const Face faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
                           {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
    for (const Face& f : faces) {
      const u32 b = static_cast<u32>(m.pos.size());
      for (auto [su, sv] : {std::pair{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}) { m.pos.push_back((f.n + f.u * su + f.v * sv) * h); m.normal.push_back(f.n); m.uv.push_back((su + 1.f) * 0.5f); m.uv.push_back((sv + 1.f) * 0.5f); }
      for (u32 i : {0u, 1u, 2u, 0u, 2u, 3u}) m.idx.push_back(b + i);
    }
    return m;
  }
  static Mesh sphere(f32 r = 0.5f, int seg = 24, int rings = 12) {
    Mesh m;
    for (int y = 0; y <= rings; ++y)
      for (int x = 0; x <= seg; ++x) {
        const f32 th = 3.14159265f * static_cast<f32>(y) / static_cast<f32>(rings), ph = 2.f * 3.14159265f * static_cast<f32>(x) / static_cast<f32>(seg);
        const Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        m.pos.push_back(n * r); m.normal.push_back(n); m.uv.push_back(static_cast<f32>(x) / static_cast<f32>(seg)); m.uv.push_back(static_cast<f32>(y) / static_cast<f32>(rings));
      }
    for (int y = 0; y < rings; ++y)
      for (int x = 0; x < seg; ++x) {
        const u32 a = static_cast<u32>(y * (seg + 1) + x), b = a + 1, c = a + static_cast<u32>(seg + 1), d = c + 1;
        for (u32 i : {a, b, c, b, d, c}) m.idx.push_back(i);
      }
    return m;
  }

  // ---- Formas extras (cilindro, cone, capsula, roda, prisma triangular e hexagonal) -------------------------------
  // Todas sao gravadas com a face de fora virada para fora: addTri() inverte a ordem de um triangulo se ele apontar
  // para dentro da normal do vertice. Assim as formas nao dependem de uma convencao de enrolamento.
  u32 addVtx(Vec3 p, Vec3 n) {
    pos.push_back(p); normal.push_back(n); uv.push_back(0.f); uv.push_back(0.f);
    return static_cast<u32>(pos.size() - 1);
  }
  void addTri(u32 a, u32 b, u32 c) {
    const Vec3 pa = pos[a], pb = pos[b], pc = pos[c], n = normal[a];
    const f32 ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z, vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
    const f32 gx = uy * vz - uz * vy, gy = uz * vx - ux * vz, gz = ux * vy - uy * vx;
    if (gx * n.x + gy * n.y + gz * n.z < 0.f) std::swap(b, c);
    idx.push_back(a); idx.push_back(b); idx.push_back(c);
  }
  // Revolucao de um perfil (r, y) em torno do eixo Y. smooth = normais suaves nos pontos do perfil (capsula);
  // senao cada trecho e plano (cilindro, cone). Perfil de baixo para cima, por fora.
  static Mesh lathe(const std::vector<std::pair<f32, f32>>& prof, int seg, bool smooth) {
    Mesh m;
    const size_t n = prof.size();
    if (n < 2) return m;
    std::vector<std::pair<f32, f32>> sn(n - 1, {1.f, 0.f}); // normal de cada trecho no plano (r, y): (dy, -dr) aponta para fora
    for (size_t j = 0; j + 1 < n; ++j) {
      const f32 dr = prof[j + 1].first - prof[j].first, dy = prof[j + 1].second - prof[j].second;
      const f32 len = std::sqrt(dr * dr + dy * dy);
      if (len > 1e-6f) sn[j] = {dy / len, -dr / len};
    }
    // normal usada por um vertice do trecho j: no inicio (endPt=false) ou no fim (endPt=true) do trecho.
    auto nrm = [&](size_t j, bool endPt) {
      if (!smooth) return sn[j];
      std::pair<f32, f32> s = sn[j];
      const bool has = endPt ? (j + 2 < n) : (j > 0);
      if (has) { const auto o = sn[endPt ? j + 1 : j - 1]; s.first += o.first; s.second += o.second; }
      const f32 l = std::sqrt(s.first * s.first + s.second * s.second);
      if (l > 1e-6f) s = {s.first / l, s.second / l};
      return s;
    };
    for (size_t j = 0; j + 1 < n; ++j) {
      const f32 dr = prof[j + 1].first - prof[j].first, dy = prof[j + 1].second - prof[j].second;
      if (std::sqrt(dr * dr + dy * dy) < 1e-6f) continue; // trecho de tamanho zero: nada a desenhar
      const auto a = nrm(j, false), b = nrm(j, true);
      std::vector<u32> r0(static_cast<size_t>(seg) + 1), r1(static_cast<size_t>(seg) + 1);
      for (int k = 0; k <= seg; ++k) {
        const f32 ph = 2.f * 3.14159265f * static_cast<f32>(k) / static_cast<f32>(seg), c = std::cos(ph), s = std::sin(ph);
        r0[static_cast<size_t>(k)] = m.addVtx(Vec3{prof[j].first * c, prof[j].second, prof[j].first * s}, Vec3{a.first * c, a.second, a.first * s});
        r1[static_cast<size_t>(k)] = m.addVtx(Vec3{prof[j + 1].first * c, prof[j + 1].second, prof[j + 1].first * s}, Vec3{b.first * c, b.second, b.first * s});
      }
      for (int k = 0; k < seg; ++k) {
        const size_t i = static_cast<size_t>(k);
        m.addTri(r0[i], r0[i + 1], r1[i]);
        m.addTri(r0[i + 1], r1[i + 1], r1[i]);
      }
    }
    return m;
  }
  // Cilindro de raio r e altura h, centrado na origem (base em -h/2, topo em +h/2).
  static Mesh cylinder(f32 r = 0.5f, f32 h = 1.f, int seg = 32) {
    const f32 hh = h * 0.5f;
    return lathe({{0.f, -hh}, {r, -hh}, {r, hh}, {0.f, hh}}, seg, false);
  }
  // Cone: base de raio r em -h/2, ponta em +h/2.
  static Mesh cone(f32 r = 0.5f, f32 h = 1.f, int seg = 32) {
    const f32 hh = h * 0.5f;
    return lathe({{0.f, -hh}, {r, -hh}, {0.f, hh}}, seg, false);
  }
  // Capsula: cilindro com semiesferas nas pontas; altura total h (inclui as semiesferas).
  static Mesh capsule(f32 r = 0.35f, f32 h = 1.f, int seg = 24, int caps = 8) {
    const f32 PI = 3.14159265f, L = std::max(0.f, h - 2.f * r) * 0.5f;
    std::vector<std::pair<f32, f32>> prof;
    for (int i = 0; i <= caps; ++i) { const f32 t = -PI * 0.5f + PI * 0.5f * static_cast<f32>(i) / static_cast<f32>(caps); prof.push_back({r * std::cos(t), -L + r * std::sin(t)}); }
    for (int i = 0; i <= caps; ++i) { const f32 t = PI * 0.5f * static_cast<f32>(i) / static_cast<f32>(caps); prof.push_back({r * std::cos(t), L + r * std::sin(t)}); }
    return lathe(prof, seg, true);
  }
  // Roda: aro (toroide) no plano XY, com o eixo no Z. Diametro externo = 2 * (R + r).
  static Mesh wheel(f32 R = 0.4f, f32 r = 0.1f, int seg = 32, int tseg = 12) {
    Mesh m;
    const f32 PI = 3.14159265f;
    for (int i = 0; i <= seg; ++i)
      for (int j = 0; j <= tseg; ++j) {
        const f32 u = 2.f * PI * static_cast<f32>(i) / static_cast<f32>(seg), v = 2.f * PI * static_cast<f32>(j) / static_cast<f32>(tseg);
        const f32 cu = std::cos(u), su = std::sin(u), cv = std::cos(v), sv = std::sin(v);
        m.addVtx(Vec3{(R + r * cv) * cu, (R + r * cv) * su, r * sv}, Vec3{cv * cu, cv * su, sv});
      }
    for (int i = 0; i < seg; ++i)
      for (int j = 0; j < tseg; ++j) {
        const u32 a = static_cast<u32>(i * (tseg + 1) + j), b = a + static_cast<u32>(tseg + 1);
        m.addTri(a, a + 1, b);
        m.addTri(a + 1, b + 1, b);
      }
    return m;
  }
  // Prisma regular de `sides` lados (3 = triangulo, 6 = hexagono), raio circunscrito r, altura h, centrado na origem.
  static Mesh prism(int sides, f32 r = 0.5f, f32 h = 0.5f) {
    Mesh m;
    const f32 PI = 3.14159265f, hh = h * 0.5f;
    for (int k = 0; k < sides; ++k) {
      const f32 a0 = 2.f * PI * static_cast<f32>(k) / static_cast<f32>(sides), a1 = 2.f * PI * static_cast<f32>(k + 1) / static_cast<f32>(sides), am = (a0 + a1) * 0.5f;
      const Vec3 n{std::cos(am), 0.f, std::sin(am)};
      const u32 b0 = m.addVtx(Vec3{r * std::cos(a0), -hh, r * std::sin(a0)}, n), b1 = m.addVtx(Vec3{r * std::cos(a1), -hh, r * std::sin(a1)}, n);
      const u32 t0 = m.addVtx(Vec3{r * std::cos(a0), hh, r * std::sin(a0)}, n), t1 = m.addVtx(Vec3{r * std::cos(a1), hh, r * std::sin(a1)}, n);
      m.addTri(b0, b1, t0);
      m.addTri(b1, t1, t0);
    }
    for (const f32 s : {-1.f, 1.f}) { // tampas: leque a partir do centro
      const Vec3 n{0.f, s, 0.f};
      const u32 c = m.addVtx(Vec3{0.f, s * hh, 0.f}, n);
      std::vector<u32> ring(static_cast<size_t>(sides));
      for (int k = 0; k < sides; ++k) {
        const f32 a = 2.f * PI * static_cast<f32>(k) / static_cast<f32>(sides);
        ring[static_cast<size_t>(k)] = m.addVtx(Vec3{r * std::cos(a), s * hh, r * std::sin(a)}, n);
      }
      for (int k = 0; k < sides; ++k) m.addTri(c, ring[static_cast<size_t>(k)], ring[static_cast<size_t>((k + 1) % sides)]);
    }
    return m;
  }
};

// castShadow / receiveShadow: por objeto, independentes (ver lighting.hpp).
// emissive: radiancia linear emitida pelo objeto (cor x intensidade); nao e iluminada, so soma na cor (e o bloom usa o brilho).
struct Material { Vec3 albedo{0.8f, 0.8f, 0.8f}; f32 roughness = 0.5f; f32 metallic = 0.f; bool castShadow = true, receiveShadow = true; const Texture* albedoMap = nullptr; Vec3 emissive{0.f, 0.f, 0.f}; f32 opacity = 1.f; /* <1: passe transparente (mistura) */
  Vec3 fogColor{0.6f, 0.65f, 0.7f}; f32 fogStart = 5.f, fogEnd = 50.f, fogAmount = 0.f; /* neblina por distancia; 0 = sem */ };

struct Camera {
  Vec3 eye{0, 2, 6}, target{0, 0.5f, 0}, up{0, 1, 0};
  f32 fovY = 0.9f, nearZ = 0.1f, farZ = 100.f;
  bool ortho = false;            // 2D: projecao ortografica; orthoHeight e a altura visivel em unidades do mundo
  f32 orthoHeight = 10.f;
  // Matriz de projecao da camera para uma imagem w x h (perspectiva ou ortografica).
  Mat4 projection(f32 w, f32 h) const {
    const f32 aspect = w / h;
    if (!ortho) return Mat4::perspective(fovY, aspect, nearZ, farZ);
    const f32 hh = orthoHeight * 0.5f, hw = hh * aspect;
    return Mat4::ortho(-hw, hw, -hh, hh, nearZ, farZ);
  }
};

class SoftRenderer {
public:
  SoftRenderer(u32 w = 320, u32 h = 180) { resize(w, h); }

  void resize(u32 w, u32 h) {
    w_ = std::max(1u, w); h_ = std::max(1u, h);
    const size_t n = static_cast<size_t>(w_) * h_;
    color_.assign(n * 4, 0); depth_.assign(n, 1.f);
    tdepth_.assign(n, 1.f); tCovered_.assign(n, 0); tDirect_.assign(n, Vec3{}); tAmb_.assign(n, Vec3{}); tAlpha_.assign(n, 0.f);
    direct_.assign(n, Vec3{}); amb_.assign(n, Vec3{}); emit_.assign(n, Vec3{}); covered_.assign(n, 0); glow_.assign(n * 3, 0.f);
    depthV_.assign(n, 0.f); nrmV_.assign(n, LightPrism::Vec3{}); ao_.assign(n, 1.f);
    posW_.assign(n, LightPrism::Vec3{}); nrmW_.assign(n, LightPrism::Vec3{});
  }
  u32 width() const { return w_; }
  u32 height() const { return h_; }
  const u8* pixels() const { flush(); return color_.data(); } // RGBA8 (conclui o desenho pendente)
  // Mesmo buffer de pixels, mas gravavel: usado pelo pos-processamento (postfx.hpp) que altera a imagem ja pronta.
  u8* pixelsMutable() { flush(); return color_.data(); }
  // Brilho linear por pixel (RGB float, w x h x 3): so a emissao dos materiais com emissao ligada. Base do bloom.
  const f32* glowLinear() const { flush(); return glow_.data(); }
  u64 trianglesDrawn() const { return tris_; }
  u64 pixelsShaded() const { flush(); return shaded_; }

  // clearAlpha 0 marca o fundo como "sem geometria" (o editor preenche o ceu depois, pelo canal alfa).
  void begin(const Camera& cam, const Vec3& clearColor = {0.08f, 0.09f, 0.11f}, f32 clearAlpha = 1.f) {
    cam_ = cam;
    view_ = Mat4::lookAt(cam.eye, cam.target, cam.up);
    viewProj_ = cam.projection(static_cast<f32>(w_), static_cast<f32>(h_)) * view_;
    std::fill(depth_.begin(), depth_.end(), 1.f);
    std::fill(covered_.begin(), covered_.end(), u8{0});
    if (glowDirty_) { std::fill(glow_.begin(), glow_.end(), 0.f); glowDirty_ = false; } // so zera se o quadro anterior teve emissao
    std::fill(depthV_.begin(), depthV_.end(), 0.f); // 0 = sem geometria (entrada do SSAO)
    proj00_ = cam.projection(static_cast<f32>(w_), static_cast<f32>(h_)).m[0][0];
    proj11_ = cam.projection(static_cast<f32>(w_), static_cast<f32>(h_)).m[1][1];
    // Fundo: a cor de limpeza e sRGB direto (como o alvo sRGB da GPU), sem a curva ACES usada nos objetos.
    auto toSrgb = [](f32 c) { c = std::clamp(c, 0.f, 1.f); return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; };
    color_[0] = toByte(toSrgb(clearColor.x)); color_[1] = toByte(toSrgb(clearColor.y)); color_[2] = toByte(toSrgb(clearColor.z));
    color_[3] = clearAlpha < 0.5f ? 0 : 255;
    for (size_t i = 1; i < static_cast<size_t>(w_) * h_; ++i) std::memcpy(&color_[i * 4], &color_[0], 4); // cor de fundo: calcula 1 vez e copia
    pending_.clear(); pendingT_.clear(); mats_.clear();
    std::fill(tdepth_.begin(), tdepth_.end(), 1.f); std::fill(tCovered_.begin(), tCovered_.end(), u8{0});
    shadowOn_ = false; shadowSkip_ = false; for (auto& t : shadowTris_) t.clear(); // sombras: o chamador liga de novo com setLighting() a cada quadro
    pointOn_ = false; for (auto& t : faceTris_) t.clear();
    tris_ = shaded_ = 0;
  }

  // Iluminacao do quadro (LightFrame, vindo de LightRig::build). Chamar depois de begin() e ANTES dos draw().
  // reuse = true: nada que projeta sombra nem a luz mudou ("sleeper"): mantem os mapas do quadro anterior e nao
  // rasteriza os casters. Sem cascatas (csm.cascadeCount == 0) nao ha passe de sombra e o custo e zero.
  // Ray tracing (CPU): com `bvh` (triangulos do mundo), a oclusao ambiente vem de raios reais no lugar do SSAO.
  // O BVH precisa viver ate pixels(). nullptr = volta ao SSAO. Defina antes de ler os pixels de cada quadro.
  void setRayTracedAo(const LightPrism::Rt::Bvh* bvh, const LightPrism::Rt::RtAoConfig& cfg) { rtBvh_ = bvh; rtAo_ = cfg; }

  // pointReuse = true: luzes pontuais/spot e casters iguais ao quadro anterior (a camera nao entra aqui): os mapas de
  // face sao mantidos e nada e rasterizado de novo. O chamador garante a igualdade (assinatura).
  void setLighting(const LightFrame& f, bool reuse = false, bool pointReuse = false) {
    lf_ = f;
    const bool on = lf_.shadows();
    const u32 count = on ? std::min<u32>(lf_.csm.gpu.cascadeCount, LightPrism::kMaxCsmCascades) : 0u;
    const int res = on ? std::clamp(static_cast<int>(lf_.csm.gpu.shadowMapSize), 128, 4096) : shadowRes_;
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    const bool sameLayout = on && res == shadowRes_ && count == shadowCount_ && shadowMap_[0].size() == n;
    shadowOn_ = on; shadowCount_ = count; shadowRes_ = res;
    shadowSkip_ = on && reuse && shadowValid_ && sameLayout;
    for (u32 k = 0; k < count; ++k) {
      cascadeVp_[k] = toTool(lf_.csm.gpu.cascades[k].viewProj);
      if (!shadowSkip_) shadowMap_[k].assign(n, 1.f);
      shadowTris_[k].clear();
    }
    if (on && !shadowSkip_) shadowValid_ = false;
    // Sombras de luzes pontuais/spot: um mapa por face (slot = luz * 6 + face), sempre refeito a cada quadro.
    pointOn_ = !lf_.shadowLights.empty();
    pointRes_ = std::clamp(static_cast<int>(lf_.pointShadowResolution), 64, 2048);
    if (!pointOn_) pointValid_ = false;
    const bool sameFaces = pointRes_ == pointResBuilt_ && lf_.shadowLights.size() == pointLightsBuilt_;
    pointSkip_ = pointOn_ && pointReuse && pointValid_ && sameFaces;
    if (pointOn_ && !pointSkip_) {
      const size_t pn = static_cast<size_t>(pointRes_) * static_cast<size_t>(pointRes_);
      pointValid_ = false;
      for (size_t i = 0; i < lf_.shadowLights.size(); ++i)
        for (u32 f = 0; f < lf_.shadowLights[i].faces; ++f) {
          const size_t slot = i * kMaxPointShadowFaces + f;
          faceMap_[slot].assign(pn, 1e30f); // profundidade em metros: "infinito" = nada na frente (1.0 seria 1 m)
          faceTris_[slot].clear();
        }
    }
    pointResBuilt_ = pointRes_; pointLightsBuilt_ = lf_.shadowLights.size();
  }
  // Sombras de luzes pontuais/spot foram reaproveitadas neste quadro (nenhuma face rasterizada de novo).
  bool pointShadowPassSkipped() const { return pointSkip_; }
  // Luzes pontuais/spot com sombra neste quadro (ShadowLight) e quantas faces tem cada uma.
  size_t pointShadowLights() const { return lf_.shadowLights.size(); }
  bool shadowPassSkipped() const { return shadowSkip_; }
  bool shadowsActive() const { return shadowOn_; }
  u64 shadowTriangles() const { u64 n = 0; for (const auto& t : shadowTris_) n += t.size(); return n; }

  // draw() so PREPARA os triangulos (transforma vertices, descarta fora da tela/costas); o desenho dos pixels
  // acontece em flush(), dividido em faixas de linhas processadas por varias threads. Cada faixa percorre os
  // triangulos na ordem em que foram enviados, entao o resultado e identico ao de uma unica thread.
  void draw(const Mesh& m, const Mat4& world, const Material& mat) {
    struct V { f32 x, y, z, w; Vec3 wp, n; f32 u, v; };
    std::vector<V> v(m.pos.size());
    for (size_t i = 0; i < m.pos.size(); ++i) {
      const Vec3 wp = world.transformPoint(m.pos[i]);
      const Vec4 c = viewProj_.mul({wp.x, wp.y, wp.z, 1});
      const Vec4 nw = world.mul({m.normal[i].x, m.normal[i].y, m.normal[i].z, 0});
      const bool hasUv = m.uv.size() == m.pos.size() * 2;
      v[i] = {c.x, c.y, c.z, c.w, wp, normalize({nw.x, nw.y, nw.z}), hasUv ? m.uv[2 * i] : 0.f, hasUv ? m.uv[2 * i + 1] : 0.f};
    }
    const u32 matIdx = static_cast<u32>(mats_.size());
    mats_.push_back(mat);
    if (shadowOn_ && !shadowSkip_ && mat.castShadow && mat.opacity >= 1.f) { // transparentes nao projetam sombra (como na GPU) // triangulos vistos do sol, uma lista por cascata (sem culling: sombra vale para as duas faces)
      const f32 R = static_cast<f32>(shadowRes_);
      for (u32 k = 0; k < shadowCount_; ++k) {
        std::vector<Vec3> ls(m.pos.size()); // x,y em texels, z em 0..1 (convencao da LightPrism: y cresce para baixo)
        for (size_t i = 0; i < m.pos.size(); ++i) {
          const Vec3 wp = world.transformPoint(m.pos[i]); const Vec4 c = cascadeVp_[k].mul({wp.x, wp.y, wp.z, 1});
          ls[i] = {(c.x * 0.5f + 0.5f) * R, (0.5f - c.y * 0.5f) * R, c.z};
        }
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
          ShadowTri st; const Vec3* q[3] = {&ls[m.idx[t]], &ls[m.idx[t + 1]], &ls[m.idx[t + 2]]};
          for (int j = 0; j < 3; ++j) { st.x[j] = q[j]->x; st.y[j] = q[j]->y; st.z[j] = q[j]->z; }
          st.area = (st.x[1] - st.x[0]) * (st.y[2] - st.y[0]) - (st.y[1] - st.y[0]) * (st.x[2] - st.x[0]);
          if (std::abs(st.area) < 1e-6f) continue;
          st.x0 = std::max(0, static_cast<int>(std::floor(std::min({st.x[0], st.x[1], st.x[2]})))); st.x1 = std::min(shadowRes_ - 1, static_cast<int>(std::ceil(std::max({st.x[0], st.x[1], st.x[2]}))));
          st.y0 = std::max(0, static_cast<int>(std::floor(std::min({st.y[0], st.y[1], st.y[2]})))); st.y1 = std::min(shadowRes_ - 1, static_cast<int>(std::ceil(std::max({st.y[0], st.y[1], st.y[2]}))));
          if (st.x0 > st.x1 || st.y0 > st.y1) continue;
          shadowTris_[k].push_back(st);
        }
      }
    }
    if (pointOn_ && !pointSkip_ && mat.castShadow && mat.opacity >= 1.f) { // luzes pontuais/spot: cada face de cada luz com sombra recebe os triangulos
      for (size_t i = 0; i < lf_.shadowLights.size(); ++i)
        for (u32 f = 0; f < lf_.shadowLights[i].faces; ++f) addFaceCaster(m, world, lf_.shadowLights[i], f, i * kMaxPointShadowFaces + f);
    }
    // Recorte no plano near (Sutherland-Hodgman em clip space): triangulos que cruzam o near sao cortados, nao descartados.
    // Antes um triangulo com um vertice atras da camera sumia inteiro (ex.: o chao com a camera dentro do seu alcance).
    std::vector<V> tris;
    tris.reserve(m.idx.size());
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
      V in[3] = {v[m.idx[t]], v[m.idx[t + 1]], v[m.idx[t + 2]]};
      if (cam_.ortho) { tris.push_back(in[0]); tris.push_back(in[1]); tris.push_back(in[2]); continue; }
      auto mix = [](const V& p, const V& q, f32 k) {
        return V{p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k, p.z + (q.z - p.z) * k, p.w + (q.w - p.w) * k,
                 p.wp + (q.wp - p.wp) * k, p.n + (q.n - p.n) * k, p.u + (q.u - p.u) * k, p.v + (q.v - p.v) * k};
      };
      V poly[4]; int np = 0;
      for (int i = 0; i < 3; ++i) {
        const V& p = in[i]; const V& q = in[(i + 1) % 3];
        const f32 dp = p.w - cam_.nearZ, dq = q.w - cam_.nearZ;
        if (dp >= 0.f) poly[np++] = p;
        if ((dp >= 0.f) != (dq >= 0.f)) poly[np++] = mix(p, q, dp / (dp - dq));
      }
      for (int k = 1; k + 1 < np; ++k) { tris.push_back(poly[0]); tris.push_back(poly[k]); tris.push_back(poly[k + 1]); }
    }
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
      const V& a = tris[t]; const V& b = tris[t + 1]; const V& c = tris[t + 2];
      auto sx = [&](const V& p) { return (p.x / p.w * 0.5f + 0.5f) * static_cast<f32>(w_); };
      auto sy = [&](const V& p) { return (1.f - (p.y / p.w * 0.5f + 0.5f)) * static_cast<f32>(h_); };
      Tri r;
      r.x[0] = sx(a); r.y[0] = sy(a); r.x[1] = sx(b); r.y[1] = sy(b); r.x[2] = sx(c); r.y[2] = sy(c);
      r.area = (r.x[1] - r.x[0]) * (r.y[2] - r.y[0]) - (r.y[1] - r.y[0]) * (r.x[2] - r.x[0]);
      if (r.area >= 0) continue; // culling de faces traseiras (frente = anti-horario no espaco do mundo)
      r.x0 = std::max(0, static_cast<int>(std::floor(std::min({r.x[0], r.x[1], r.x[2]})))); r.x1 = std::min(static_cast<int>(w_) - 1, static_cast<int>(std::ceil(std::max({r.x[0], r.x[1], r.x[2]}))));
      r.y0 = std::max(0, static_cast<int>(std::floor(std::min({r.y[0], r.y[1], r.y[2]})))); r.y1 = std::min(static_cast<int>(h_) - 1, static_cast<int>(std::ceil(std::max({r.y[0], r.y[1], r.y[2]}))));
      if (r.x0 > r.x1 || r.y0 > r.y1) continue; // fora da tela
      ++tris_;
      const V* vs[3] = {&a, &b, &c};
      for (int k = 0; k < 3; ++k) { r.iw[k] = 1.f / vs[k]->w; r.zw[k] = vs[k]->z / vs[k]->w; r.wp[k] = vs[k]->wp; r.n[k] = vs[k]->n; r.uv[k][0] = vs[k]->u; r.uv[k][1] = vs[k]->v; }
      r.mat = matIdx;
      (mat.opacity < 1.f ? pendingT_ : pending_).push_back(r);
    }
  }

  // Rasteriza os triangulos pendentes (varias threads, cada uma com uma faixa de linhas).
  void flush() const {
    if (pending_.empty() && pendingT_.empty()) return;
    flushShadow();
    const int H = static_cast<int>(h_);
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const int nT = std::clamp(static_cast<int>(std::min(hw, 8u)), 1, std::max(1, H / 24));
    std::vector<u64> shadedPer(static_cast<size_t>(nT), 0);
    auto work = [&](int band) {
      const int y0 = H * band / nT, y1 = H * (band + 1) / nT - 1;
      u64 count = 0;
      for (const Tri& t : pending_) {
        if (t.y1 < y0 || t.y0 > y1) continue;
        rasterize(t, std::max(t.y0, y0), std::min(t.y1, y1), count);
      }
      shadedPer[static_cast<size_t>(band)] = count;
    };
    if (nT == 1) work(0);
    else tool::parallelFor(static_cast<size_t>(nT), 1, [&](size_t b, size_t e) { for (size_t band = b; band < e; ++band) work(static_cast<int>(band)); });
    for (u64 c : shadedPer) shaded_ += c;
    resolve();
    pending_.clear();
    if (!pendingT_.empty()) { // transparentes: rasterizados depois do opaco, com profundidade do opaco, e misturados por cima
      auto workT = [&](int band) {
        const int y0 = H * band / nT, y1 = H * (band + 1) / nT - 1;
        u64 count = 0;
        for (const Tri& t : pendingT_) {
          if (t.y1 < y0 || t.y0 > y1) continue;
          rasterize(t, std::max(t.y0, y0), std::min(t.y1, y1), count, true);
        }
      };
      if (nT == 1) workT(0);
      else tool::parallelFor(static_cast<size_t>(nT), 1, [&](size_t b, size_t e) { for (size_t band = b; band < e; ++band) workT(static_cast<int>(band)); });
      resolveTransparent();
      pendingT_.clear();
    }
  }

  static f32 srgbDecode(f32 v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
  static f32 srgbEncode(f32 v) { v = std::clamp(v, 0.f, 1.f); return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f; }
  // Mistura as camadas transparentes sobre a cor final do opaco (no espaco sRGB, como o resto do tonemap: aproximacao).
  void resolveTransparent() const {
    parallelRange(static_cast<size_t>(w_) * h_, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        if (!tCovered_[i]) continue;
        // Mistura em linear (como a GPU, que blenda no formato sRGB): decodifica, mistura e codifica de volta.
        const LightPrism::Vec3 o = LightPrism::Reference::TonemapToSRGB(LightRig::lp(tDirect_[i] + tAmb_[i]));
        const f32 a = std::clamp(tAlpha_[i], 0.f, 1.f);
        u8* px = &color_[i * 4];
        const f32 src[3] = {o.x, o.y, o.z};
        for (int c = 0; c < 3; ++c) px[c] = toByte(srgbEncode(srgbDecode(px[c] / 255.f) * (1.f - a) + srgbDecode(src[c]) * a));
      }
    });
  }

  // Projeta os triangulos de um objeto na face f de uma luz pontual/spot (perspectiva). Triangulos com algum vertice
  // atras do near da face sao descartados (sombra incompleta so nesse caso raro, perto da luz).
  void addFaceCaster(const Mesh& m, const Mat4& world, const ShadowLight& sl, u32 f, size_t slot) {
    const f32 R = static_cast<f32>(pointRes_);
    std::vector<Vec4> c(m.pos.size());
    for (size_t i = 0; i < m.pos.size(); ++i) {
      const Vec3 wp = world.transformPoint(m.pos[i]);
      c[i] = sl.vp[f].mul({wp.x, wp.y, wp.z, 1});
    }
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
      const Vec4* q[3] = {&c[m.idx[t]], &c[m.idx[t + 1]], &c[m.idx[t + 2]]};
      if (q[0]->w <= sl.nearZ || q[1]->w <= sl.nearZ || q[2]->w <= sl.nearZ) continue;
      FaceTri ft;
      for (int j = 0; j < 3; ++j) {
        ft.x[j] = (q[j]->x / q[j]->w * 0.5f + 0.5f) * R;
        ft.y[j] = (0.5f - q[j]->y / q[j]->w * 0.5f) * R;
        ft.iw[j] = 1.f / q[j]->w; // 1/w: interpolado na tela para obter a profundidade correta
      }
      ft.area = (ft.x[1] - ft.x[0]) * (ft.y[2] - ft.y[0]) - (ft.y[1] - ft.y[0]) * (ft.x[2] - ft.x[0]);
      if (std::abs(ft.area) < 1e-6f) continue;
      ft.x0 = std::max(0, static_cast<int>(std::floor(std::min({ft.x[0], ft.x[1], ft.x[2]}))));
      ft.x1 = std::min(pointRes_ - 1, static_cast<int>(std::ceil(std::max({ft.x[0], ft.x[1], ft.x[2]}))));
      ft.y0 = std::max(0, static_cast<int>(std::floor(std::min({ft.y[0], ft.y[1], ft.y[2]}))));
      ft.y1 = std::min(pointRes_ - 1, static_cast<int>(std::ceil(std::max({ft.y[0], ft.y[1], ft.y[2]}))));
      if (ft.x0 > ft.x1 || ft.y0 > ft.y1) continue;
      faceTris_[slot].push_back(ft);
    }
  }

  // Rasteriza as faces de luzes pontuais/spot. Profundidade = distancia ao longo do eixo da face (w do clip), com
  // interpolacao correta em perspectiva (1/w linear na tela).
  void flushFaces() const {
    const int R = pointRes_;
    if (pointSkip_) { for (auto& t : faceTris_) t.clear(); pointValid_ = pointOn_; return; }
    for (size_t slot = 0; slot < kMaxPointShadowLights * kMaxPointShadowFaces; ++slot) {
      if (faceTris_[slot].empty()) continue;
      std::vector<f32>& map = faceMap_[slot];
      for (const FaceTri& t : faceTris_[slot]) {
        const f32 inv = 1.f / t.area;
        for (int y = t.y0; y <= t.y1; ++y)
          for (int x = t.x0; x <= t.x1; ++x) {
            const f32 px = static_cast<f32>(x) + 0.5f, py = static_cast<f32>(y) + 0.5f;
            const f32 w0 = ((t.x[1] - px) * (t.y[2] - py) - (t.y[1] - py) * (t.x[2] - px)) * inv;
            const f32 w1 = ((t.x[2] - px) * (t.y[0] - py) - (t.y[2] - py) * (t.x[0] - px)) * inv;
            const f32 w2 = 1.f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            const f32 iw = w0 * t.iw[0] + w1 * t.iw[1] + w2 * t.iw[2];
            if (iw <= 0.f) continue;
            const f32 depth = 1.f / iw;
            f32& d = map[static_cast<size_t>(y) * static_cast<size_t>(R) + static_cast<size_t>(x)];
            if (depth < d) d = depth;
          }
      }
      faceTris_[slot].clear();
    }
    pointValid_ = pointOn_;
  }

  // Visibilidade (0..1) de um ponto pela face de uma luz pontual/spot: 3x3 PCF sobre o mapa da face. Viés em distancia
  // (ao longo da normal, contra acne) e constante (contra a propria superficie). Fora da face = iluminado.
  f32 faceVisibility(const ShadowLight& sl, size_t slotBase, Vec3 p, Vec3 n) const {
    const Vec3 d = p - sl.position;
    u32 f = 0;
    if (sl.type == LightType::Point) {
      const f32 ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
      if (ax >= ay && ax >= az) f = d.x > 0.f ? 0u : 1u;
      else if (ay >= az) f = d.y > 0.f ? 2u : 3u;
      else f = d.z > 0.f ? 4u : 5u;
    }
    if (f >= sl.faces) return 1.f;
    const f32 dist = length(d);
    const Vec3 pb = p + n * (0.01f + 0.002f * dist);
    const Vec4 c = sl.vp[f].mul({pb.x, pb.y, pb.z, 1});
    if (c.w <= sl.nearZ) return 1.f;
    const f32 nx = c.x / c.w, ny = c.y / c.w;
    if (std::abs(nx) > 1.f || std::abs(ny) > 1.f) return 1.f;
    const int R = pointRes_;
    const int cx = static_cast<int>(std::lround((nx * 0.5f + 0.5f) * static_cast<f32>(R) - 0.5f));
    const int cy = static_cast<int>(std::lround((0.5f - ny * 0.5f) * static_cast<f32>(R) - 0.5f));
    const f32 depth = c.w, bias = 0.02f + 0.0015f * depth;
    const std::vector<f32>& map = faceMap_[slotBase + f];
    int lit = 0, tot = 0;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int x = std::clamp(cx + dx, 0, R - 1), y = std::clamp(cy + dy, 0, R - 1);
        ++tot;
        if (map[static_cast<size_t>(y) * static_cast<size_t>(R) + static_cast<size_t>(x)] >= depth - bias) ++lit;
      }
    return static_cast<f32>(lit) / static_cast<f32>(tot);
  }

  // Visibilidade de uma luz pontual/spot do quadro, achada pela posicao (a LightPrism guarda a mesma posicao).
  f32 pointShadowFor(LightType type, const LightPrism::Vec3& lpos, Vec3 p, Vec3 n) const {
    if (!pointOn_) return 1.f;
    for (size_t i = 0; i < lf_.shadowLights.size(); ++i) {
      const ShadowLight& sl = lf_.shadowLights[i];
      if (sl.type != type) continue;
      const Vec3 d{sl.position.x - lpos.x, sl.position.y - lpos.y, sl.position.z - lpos.z};
      if (dot(d, d) > 1e-8f) continue;
      return faceVisibility(sl, i * kMaxPointShadowFaces, p, n);
    }
    return 1.f;
  }

  // Passe de profundidade do sol: uma profundidade por cascata. Cada thread preenche uma faixa de linhas (sem disputa).
  void flushShadow() const {
    flushFaces();
    if (shadowSkip_) { for (auto& t : shadowTris_) t.clear(); return; }
    if (!shadowOn_) { for (auto& t : shadowTris_) t.clear(); shadowValid_ = false; return; }
    const int R = shadowRes_;
    auto raster = [R](const std::vector<ShadowTri>& tris, std::vector<f32>& map) {
      const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
      const int nT = std::clamp(static_cast<int>(std::min(hw, 8u)), 1, std::max(1, R / 64));
      auto work = [&](int band) {
        const int y0 = R * band / nT, y1 = R * (band + 1) / nT - 1;
        for (const ShadowTri& t : tris) {
          if (t.y1 < y0 || t.y0 > y1) continue;
          const f32 inv = 1.f / t.area;
          for (int y = std::max(t.y0, y0); y <= std::min(t.y1, y1); ++y)
            for (int x = t.x0; x <= t.x1; ++x) {
              const f32 px = static_cast<f32>(x) + 0.5f, py = static_cast<f32>(y) + 0.5f;
              const f32 w0 = ((t.x[1] - px) * (t.y[2] - py) - (t.y[1] - py) * (t.x[2] - px)) * inv, w1 = ((t.x[2] - px) * (t.y[0] - py) - (t.y[2] - py) * (t.x[0] - px)) * inv, w2 = 1.f - w0 - w1;
              if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue; // pequena tolerancia: evita furos entre triangulos vizinhos
              const f32 z = w0 * t.z[0] + w1 * t.z[1] + w2 * t.z[2];
              f32& d = map[static_cast<size_t>(y) * static_cast<size_t>(R) + static_cast<size_t>(x)];
              if (z >= 0.f && z < d) d = z;
            }
        }
      };
      if (nT == 1) work(0);
      else tool::parallelFor(static_cast<size_t>(nT), 1, [&](size_t b, size_t e) { for (size_t band = b; band < e; ++band) work(static_cast<int>(band)); });
    };
    for (u32 k = 0; k < shadowCount_; ++k) {
      if (!shadowTris_[k].empty()) raster(shadowTris_[k], shadowMap_[k]);
      shadowTris_[k].clear();
    }
    shadowValid_ = true;
  }

  const Mat4& viewProj() const { return viewProj_; }

  // Projeta um ponto do mundo para pixels (para picking/gizmos). Retorna false se estiver atras da camera.
  bool project(Vec3 p, f32& sx, f32& sy) const {
    const Vec4 c = viewProj_.mul({p.x, p.y, p.z, 1});
    if (c.w <= 0) return false;
    sx = (c.x / c.w * 0.5f + 0.5f) * static_cast<f32>(w_); sy = (1.f - (c.y / c.w * 0.5f + 0.5f)) * static_cast<f32>(h_);
    return true;
  }

private:
  struct Tri { f32 x[3], y[3], zw[3], iw[3]; f32 area; Vec3 wp[3], n[3]; f32 uv[3][2]; int x0, x1, y0, y1; u32 mat; };
  struct ShadowTri { f32 x[3], y[3], z[3]; f32 area; int x0, x1, y0, y1; };
  struct FaceTri { f32 x[3], y[3], iw[3]; f32 area; int x0, x1, y0, y1; };

  void rasterize(const Tri& t, int ya, int yb, u64& count, bool transparent = false) const {
    const Material& mat = mats_[t.mat];
    const f32 ax = t.x[0], ay = t.y[0], bx = t.x[1], by = t.y[1], cx = t.x[2], cy = t.y[2];
    for (int y = ya; y <= yb; ++y)
      for (int x = t.x0; x <= t.x1; ++x) {
        const f32 px = static_cast<f32>(x) + 0.5f, py = static_cast<f32>(y) + 0.5f;
        f32 w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / t.area, w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / t.area, w2 = 1.f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const f32 z = w0 * t.zw[0] + w1 * t.zw[1] + w2 * t.zw[2];
        const size_t di = static_cast<size_t>(y) * w_ + static_cast<size_t>(x);
        if (transparent) { if (z < 0 || z >= depth_[di] || z >= tdepth_[di]) continue; tdepth_[di] = z; } // nao escreve no opaco
        else { if (z < 0 || z >= depth_[di]) continue; depth_[di] = z; }
        // interpolacao com correcao de perspectiva
        const f32 p0 = w0 * t.iw[0], p1 = w1 * t.iw[1], p2 = w2 * t.iw[2], inv = 1.f / (p0 + p1 + p2);
        const Vec3 wp = (t.wp[0] * p0 + t.wp[1] * p1 + t.wp[2] * p2) * inv;
        const Vec3 n = normalize((t.n[0] * p0 + t.n[1] * p1 + t.n[2] * p2) * inv);
        if (mat.albedoMap && mat.albedoMap->valid()) { // textura: multiplica a cor do material pelo texel (mais proximo)
          const f32 uu = (t.uv[0][0] * p0 + t.uv[1][0] * p1 + t.uv[2][0] * p2) * inv, vv = (t.uv[0][1] * p0 + t.uv[1][1] * p1 + t.uv[2][1] * p2) * inv;
          u8 tx[4]; mat.albedoMap->texel(uu, vv, tx);
          Material mm = mat; mm.albedo = {mat.albedo.x * tx[0] / 255.f, mat.albedo.y * tx[1] / 255.f, mat.albedo.z * tx[2] / 255.f};
          emitPixel(transparent, di, shade(wp, n, mm), wp, n, mat.opacity);
        } else emitPixel(transparent, di, shade(wp, n, mat), wp, n, mat.opacity);
        ++count;
      }
  }

  // Sombreamento: BRDF PBR da LightPrism (Reference::EvaluateBRDF) para cada luz do quadro, sombra do sol por cascatas
  // (Reference::CsmVisibility) e luz ambiente como irradiancia SH9 (Ibl::EvalSh). O tonemap fica em put().
  struct Lit { Vec3 direct, amb; Vec3 emit; }; // emit: emissao do material (so ela alimenta o brilho/bloom)
  Lit shade(Vec3 p, Vec3 n, const Material& m) const {
    namespace R = LightPrism::Reference;
    const Vec3 V = normalize(cam_.eye - p);
    R::SurfaceData s;
    s.N = LightRig::lp(n); s.V = LightRig::lp(V); s.albedo = LightRig::lp(m.albedo); s.metallic = m.metallic; s.roughness = m.roughness;
    const LightPrism::Vec3 P = LightRig::lp(p);
    // Luz ambiente = IBL da LightPrism, igual ao pbr.hlsl: difuso (kD) + especular split-sum (LUT da BRDF).
    // Ambiente uniforme: a radiancia refletida e a irradiancia SH de R. Multiplicada pelo SSAO depois.
    const LightPrism::Vec3 Nl = s.N, Vl = s.V, alb = s.albedo;
    const float rawNoV = Nl.x * Vl.x + Nl.y * Vl.y + Nl.z * Vl.z;
    const float NoV = std::max(rawNoV, 1e-4f);
    const float rough = std::max(m.roughness, 0.045f);   // LP_MIN_ROUGHNESS (PBR.hlsli)
    const LightPrism::Vec3 F0{0.04f + (alb.x - 0.04f) * m.metallic, 0.04f + (alb.y - 0.04f) * m.metallic, 0.04f + (alb.z - 0.04f) * m.metallic};
    float A = 0.f, B = 0.f;
    LightPrism::Ibl::SampleBrdfLut(sharedBrdfLut(), NoV, rough, A, B);
    const LightPrism::Vec3 Fss{F0.x * A + B, F0.y * A + B, F0.z * A + B};
    const LightPrism::Vec3 kD{(1.f - std::min(Fss.x, 1.f)) * (1.f - m.metallic), (1.f - std::min(Fss.y, 1.f)) * (1.f - m.metallic), (1.f - std::min(Fss.z, 1.f)) * (1.f - m.metallic)};
    const float dirF = LightPrism::Ibl::DominantDirFactor(rough);
    const LightPrism::Vec3 Rm{Nl.x * 2.f * rawNoV - Vl.x, Nl.y * 2.f * rawNoV - Vl.y, Nl.z * 2.f * rawNoV - Vl.z};
    LightPrism::Vec3 Rd{Nl.x + (Rm.x - Nl.x) * dirF, Nl.y + (Rm.y - Nl.y) * dirF, Nl.z + (Rm.z - Nl.z) * dirF};
    const float rl = std::sqrt(Rd.x * Rd.x + Rd.y * Rd.y + Rd.z * Rd.z);
    if (rl > 1e-12f) Rd = {Rd.x / rl, Rd.y / rl, Rd.z / rl};
    const LightPrism::Vec3 dif = LightPrism::Ibl::EvalSh(lf_.ambient, Nl);
    const LightPrism::Vec3 spc = LightPrism::Ibl::EvalSh(lf_.ambient, Rd);
    LightPrism::Vec3 amb{kD.x * alb.x * dif.x + Fss.x * spc.x, kD.y * alb.y * dif.y + Fss.y * spc.y, kD.z * alb.z * dif.z + Fss.z * spc.z};
    LightPrism::Vec3 col{0.f, 0.f, 0.f};                                                                       // luz direta
    // Luzes pontuais/spot: a BRDF e a janela da LightPrism; a sombra (quando a luz projeta) multiplica a contribuicao.
    for (const auto& l : lf_.points) col = col + R::EvaluatePointLight(l, s, P) * (m.receiveShadow ? pointShadowFor(LightType::Point, l.position, p, n) : 1.f);
    for (const auto& l : lf_.spots) col = col + R::EvaluateSpotLight(l, s, P) * (m.receiveShadow ? pointShadowFor(LightType::Spot, l.position, p, n) : 1.f);
    if (lf_.hasSun) {
      f32 vis = 1.f;
      if (shadowOn_ && m.receiveShadow) {
        LightPrism::CsmDepthMaps maps; maps.size = static_cast<uint32_t>(shadowRes_);
        for (u32 k = 0; k < shadowCount_; ++k) maps.data[k] = shadowMap_[k].data();
        const f32 viewDepth = -LightRig::lp(view_.transformPoint(p)).z;
        vis = R::CsmVisibility(lf_.csm.gpu, maps, P, s.N, viewDepth);
      }
      col = col + R::EvaluateDirectionalLight(lf_.sun, s) * vis;
    }
    col = col + LightPrism::Vec3{m.emissive.x, m.emissive.y, m.emissive.z};   // emissao do proprio objeto (sem sombra nem AO)
    if (m.fogAmount > 0.f) { // neblina: (1-f)*(direta + ambiente*AO) + f*cor; a parte do ambiente e escalada aqui (o AO vem depois)
      const f32 d = length(p - cam_.eye);
      const f32 f = std::clamp((d - m.fogStart) / std::max(m.fogEnd - m.fogStart, 0.001f), 0.f, 1.f) * m.fogAmount;
      const LightPrism::Vec3 fc{m.fogColor.x, m.fogColor.y, m.fogColor.z};
      col = col * (1.f - f) + fc * f; amb = amb * (1.f - f);
    }
    return {{col.x, col.y, col.z}, {amb.x, amb.y, amb.z}, m.emissive};
  }
  // Guarda o pixel vencedor: luz direta, luz ambiente, distancia de view e normal de view (para o SSAO).
  // Pixel de um triangulo: opaco vai para o buffer principal; transparente vai para a camada de mistura.
  void emitPixel(bool transparent, size_t i, Lit c, Vec3 wp, Vec3 n, f32 alpha) const {
    if (!transparent) { store(i, c, wp, n); return; }
    tDirect_[i] = c.direct; tAmb_[i] = c.amb; tAlpha_[i] = alpha; tCovered_[i] = 1;
  }
  void store(size_t i, Lit c, Vec3 wp, Vec3 n) const {
    direct_[i] = c.direct; amb_[i] = c.amb; emit_[i] = c.emit; covered_[i] = 1; glowDirty_ = true;
    posW_[i] = {wp.x, wp.y, wp.z}; nrmW_[i] = {n.x, n.y, n.z};
    const Vec4 vp = view_.mul({wp.x, wp.y, wp.z, 1});
    depthV_[i] = -vp.z;
    const Vec4 nv = view_.mul({n.x, n.y, n.z, 0});
    nrmV_[i] = {nv.x, nv.y, nv.z};
  }
  // Depois de rasterizar: SSAO da Light Prism (ComputeSsao) sobre a cena inteira e cor final = direta + ambiente * AO.
  // Roda fn(begin, end) sobre [0, n) em faixas, em varias threads (ate 8). Cada faixa escreve so os seus indices.
  // Usa o pool persistente (tool::parallelFor): nao cria threads a cada quadro.
  template <class F> static void parallelRange(size_t n, F&& fn) { tool::parallelFor(n, 4096, std::forward<F>(fn)); }

  void resolve() const {
    const size_t n = static_cast<size_t>(w_) * h_;
    if (rtBvh_) {
      // Oclusao ambiente por raios: cada pixel coberto lanca raios a partir do ponto da superficie (BVH da LightPrism).
      // A funcao e pura (so le o BVH) e o raio depende so do indice do pixel: o resultado nao muda com as threads.
      parallelRange(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i)
          ao_[i] = covered_[i] ? LightPrism::Rt::RtAmbientOcclusion(*rtBvh_, posW_[i], nrmW_[i], rtAo_, static_cast<uint32_t>(i)) : 1.f;
      });
    } else if (lf_.ssaoOn) {
      LightPrism::Ssao::SsaoInput in;
      in.width = w_; in.height = h_; in.viewDepth = depthV_.data(); in.viewNormal = nrmV_.data();
      in.proj00 = proj00_; in.proj11 = proj11_;
      LightPrism::Ssao::ComputeSsao(lf_.ssao, in, ao_);
    } else ao_.assign(n, 1.f);
    parallelRange(n, [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        const size_t g = i * 3;
        if (!covered_[i]) { glow_[g] = glow_[g + 1] = glow_[g + 2] = 0.f; continue; }
        glow_[g] = emit_[i].x; glow_[g + 1] = emit_[i].y; glow_[g + 2] = emit_[i].z;
        put(i, direct_[i] + amb_[i] * ao_[i]);
      }
    });
  }
  // Tonemap ACES + sRGB exato (Reference::TonemapToSRGB, o mesmo do HLSL). A exposicao ja esta nas intensidades.
  void put(size_t i, Vec3 c) const {
    const LightPrism::Vec3 o = LightPrism::Reference::TonemapToSRGB(LightRig::lp(c));
    u8* px = &color_[i * 4];
    px[0] = toByte(o.x); px[1] = toByte(o.y); px[2] = toByte(o.z); px[3] = 255;
  }
  static u8 toByte(f32 v) { return static_cast<u8>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }
  static Mat4 toTool(const LightPrism::Mat4& m) { Mat4 r; for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) r.m[c][k] = m.m[k][c]; return r; } // linha -> coluna (ver LightRig::toLp)

  u32 w_ = 1, h_ = 1;
  mutable std::vector<u8> color_;
  mutable std::vector<f32> depth_;
  mutable std::vector<Tri> pending_, pendingT_;  // pendingT_: triangulos transparentes (passe depois do opaco)
  mutable std::vector<f32> tdepth_, tAlpha_;
  mutable std::vector<Vec3> tDirect_, tAmb_;
  mutable std::vector<u8> tCovered_;
  std::vector<Material> mats_;
  Camera cam_; Mat4 view_, viewProj_;
  LightFrame lf_;
  u64 tris_ = 0;
  mutable u64 shaded_ = 0;
  // sombras do sol: uma profundidade (e uma lista de triangulos) por cascata da LightPrism
  bool shadowOn_ = false, shadowSkip_ = false; mutable bool shadowValid_ = false; int shadowRes_ = 1024; u32 shadowCount_ = 0;
  Mat4 cascadeVp_[LightPrism::kMaxCsmCascades];
  mutable std::vector<f32> shadowMap_[LightPrism::kMaxCsmCascades];
  mutable std::vector<ShadowTri> shadowTris_[LightPrism::kMaxCsmCascades];
  // Buffers do pixel vencedor (ver store/resolve): luz direta e ambiente em linear, profundidade e normal de view.
  mutable std::vector<Vec3> direct_, amb_, emit_;
  mutable std::vector<f32> glow_;                  // emissao do pixel vencedor, RGB linear (ver glowLinear)
  mutable bool glowDirty_ = false;
  mutable std::vector<u8> covered_;
  mutable std::vector<f32> depthV_, ao_;
  mutable std::vector<LightPrism::Vec3> posW_, nrmW_; // posicao e normal de mundo do pixel vencedor (para o RT)
  const LightPrism::Rt::Bvh* rtBvh_ = nullptr; LightPrism::Rt::RtAoConfig rtAo_{};
  mutable std::vector<LightPrism::Vec3> nrmV_;
  f32 proj00_ = 1.f, proj11_ = 1.f;
  // sombras de luzes pontuais/spot: slot = luz * 6 + face (ver setLighting)
  bool pointOn_ = false; int pointRes_ = 512;
  bool pointSkip_ = false;                 // este quadro reaproveita os mapas de face
  mutable bool pointValid_ = false;        // os mapas de face ja foram rasterizados para a configuracao atual
  int pointResBuilt_ = 0; size_t pointLightsBuilt_ = 0;
  mutable std::vector<f32> faceMap_[kMaxPointShadowLights * kMaxPointShadowFaces];
  mutable std::vector<FaceTri> faceTris_[kMaxPointShadowLights * kMaxPointShadowFaces];
};

} // namespace tool
