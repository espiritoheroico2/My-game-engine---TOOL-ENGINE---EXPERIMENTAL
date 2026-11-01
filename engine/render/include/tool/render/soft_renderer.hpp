// ToolEngine - SoftRenderer: rasterizador em CPU (alpha). Desenha a cena 3D iluminada sem GPU:
// serve de viewport do editor, de renderizador de referencia/teste e de fallback sem backend grafico.
// Usa o mesmo modelo de luz de lighting.hpp (atenuacao fisica, spot, direcional).
#pragma once
#include "tool/render/lighting.hpp"
#include "tool/render/texture.hpp"
#include "tool/render/shadow.hpp"

#include <algorithm>
#include <cstring>
#include <thread>
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
};

// castShadow / receiveShadow: por objeto, independentes (ver shadow.hpp).
struct Material { Vec3 albedo{0.8f, 0.8f, 0.8f}; f32 roughness = 0.5f; f32 metallic = 0.f; bool castShadow = true, receiveShadow = true; const Texture* albedoMap = nullptr; };

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

  void resize(u32 w, u32 h) { w_ = std::max(1u, w); h_ = std::max(1u, h); color_.assign(static_cast<size_t>(w_) * h_ * 4, 0); depth_.assign(static_cast<size_t>(w_) * h_, 1.f); }
  u32 width() const { return w_; }
  u32 height() const { return h_; }
  const u8* pixels() const { flush(); return color_.data(); } // RGBA8 (conclui o desenho pendente)
  // Mesmo buffer de pixels, mas gravavel: usado pelo pos-processamento (postfx.hpp) que altera a imagem ja pronta.
  u8* pixelsMutable() { flush(); return color_.data(); }
  u64 trianglesDrawn() const { return tris_; }
  u64 pixelsShaded() const { flush(); return shaded_; }

  // clearAlpha 0 marca o fundo como "sem geometria" (o editor preenche o ceu depois, pelo canal alfa).
  void begin(const Camera& cam, const Vec3& clearColor = {0.08f, 0.09f, 0.11f}, f32 clearAlpha = 1.f) {
    cam_ = cam;
    view_ = Mat4::lookAt(cam.eye, cam.target, cam.up);
    viewProj_ = cam.projection(static_cast<f32>(w_), static_cast<f32>(h_)) * view_;
    std::fill(depth_.begin(), depth_.end(), 1.f);
    put(0, clearColor);
    color_[3] = clearAlpha < 0.5f ? 0 : 255;
    for (size_t i = 1; i < static_cast<size_t>(w_) * h_; ++i) std::memcpy(&color_[i * 4], &color_[0], 4); // cor de fundo: calcula 1 vez e copia
    pending_.clear(); mats_.clear();
    shadowOn_ = false; shadowSkip_ = false; shadowTris_.clear(); // sombras: o chamador liga de novo com setShadow() a cada quadro
    tris_ = shaded_ = 0;
  }

  void setLights(const std::vector<Light>& l, Vec3 ambient = {0.03f, 0.03f, 0.04f}) { lights_ = l; ambient_ = ambient; }

  // Liga as sombras do sol (a luz com castShadow = true). Chamar depois de begin() e ANTES dos draw().
  // `fr` vem de makeSunShadow(); com enabled=false (ou fr invalido) nada e gerado e o custo e zero.
  // reuse = true: nada que projeta sombra nem a luz mudou ("sleeper"): mantem o mapa do quadro anterior e nao rasteriza os casters.
  void setShadow(const ShadowSettings& st, const ShadowFrustum& fr, bool reuse = false) {
    shadowOn_ = st.enabled && fr.valid; shadowSet_ = st; shadowFr_ = fr;
    const int res = std::clamp(st.resolution, 128, 4096);
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    shadowSkip_ = shadowOn_ && reuse && shadowValid_ && res == shadowRes_ && shadowMap_.size() == n;
    shadowRes_ = res;
    if (shadowOn_ && !shadowSkip_) { shadowMap_.assign(n, 1.f); shadowValid_ = false; }
    shadowTris_.clear();
  }
  bool shadowPassSkipped() const { return shadowSkip_; }
  u64 shadowTriangles() const { return shadowTris_.size(); }

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
    if (shadowOn_ && !shadowSkip_ && mat.castShadow) { // triangulos vistos do sol (sem culling: sombra vale para as duas faces)
      const f32 R = static_cast<f32>(shadowRes_);
      std::vector<Vec3> ls(m.pos.size()); // x,y em texels, z em 0..1
      for (size_t i = 0; i < m.pos.size(); ++i) {
        const Vec3 wp = world.transformPoint(m.pos[i]); const Vec4 c = shadowFr_.viewProj.mul({wp.x, wp.y, wp.z, 1});
        ls[i] = {(c.x * 0.5f + 0.5f) * R, (1.f - (c.y * 0.5f + 0.5f)) * R, c.z};
      }
      for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        ShadowTri st; const Vec3* q[3] = {&ls[m.idx[t]], &ls[m.idx[t + 1]], &ls[m.idx[t + 2]]};
        for (int k = 0; k < 3; ++k) { st.x[k] = q[k]->x; st.y[k] = q[k]->y; st.z[k] = q[k]->z; }
        st.area = (st.x[1] - st.x[0]) * (st.y[2] - st.y[0]) - (st.y[1] - st.y[0]) * (st.x[2] - st.x[0]);
        if (std::abs(st.area) < 1e-6f) continue;
        st.x0 = std::max(0, static_cast<int>(std::floor(std::min({st.x[0], st.x[1], st.x[2]})))); st.x1 = std::min(shadowRes_ - 1, static_cast<int>(std::ceil(std::max({st.x[0], st.x[1], st.x[2]}))));
        st.y0 = std::max(0, static_cast<int>(std::floor(std::min({st.y[0], st.y[1], st.y[2]})))); st.y1 = std::min(shadowRes_ - 1, static_cast<int>(std::ceil(std::max({st.y[0], st.y[1], st.y[2]}))));
        if (st.x0 > st.x1 || st.y0 > st.y1) continue;
        shadowTris_.push_back(st);
      }
    }
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
      const V& a = v[m.idx[t]]; const V& b = v[m.idx[t + 1]]; const V& c = v[m.idx[t + 2]];
      if (!cam_.ortho && (a.w <= cam_.nearZ || b.w <= cam_.nearZ || c.w <= cam_.nearZ)) continue; // sem recorte no plano near (alpha)
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
      pending_.push_back(r);
    }
  }

  // Rasteriza os triangulos pendentes (varias threads, cada uma com uma faixa de linhas).
  void flush() const {
    if (pending_.empty()) return;
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
    else {
      std::vector<std::thread> th;
      for (int i = 1; i < nT; ++i) th.emplace_back(work, i);
      work(0);
      for (auto& t : th) t.join();
    }
    for (u64 c : shadedPer) shaded_ += c;
    pending_.clear();
  }

  // Passe de profundidade do sol: cada thread preenche uma faixa de linhas do mapa (sem disputa de escrita).
  void flushShadow() const {
    if (shadowSkip_) { shadowTris_.clear(); return; }
    if (!shadowOn_ || shadowTris_.empty()) { shadowTris_.clear(); shadowValid_ = shadowOn_; return; }
    const int R = shadowRes_;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const int nT = std::clamp(static_cast<int>(std::min(hw, 8u)), 1, std::max(1, R / 64));
    auto work = [&](int band) {
      const int y0 = R * band / nT, y1 = R * (band + 1) / nT - 1;
      for (const ShadowTri& t : shadowTris_) {
        if (t.y1 < y0 || t.y0 > y1) continue;
        const f32 inv = 1.f / t.area;
        for (int y = std::max(t.y0, y0); y <= std::min(t.y1, y1); ++y)
          for (int x = t.x0; x <= t.x1; ++x) {
            const f32 px = static_cast<f32>(x) + 0.5f, py = static_cast<f32>(y) + 0.5f;
            const f32 w0 = ((t.x[1] - px) * (t.y[2] - py) - (t.y[1] - py) * (t.x[2] - px)) * inv, w1 = ((t.x[2] - px) * (t.y[0] - py) - (t.y[2] - py) * (t.x[0] - px)) * inv, w2 = 1.f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue; // pequena tolerancia: evita furos entre triangulos vizinhos
            const f32 z = w0 * t.z[0] + w1 * t.z[1] + w2 * t.z[2];
            f32& d = shadowMap_[static_cast<size_t>(y) * static_cast<size_t>(R) + static_cast<size_t>(x)];
            if (z >= 0.f && z < d) d = z;
          }
      }
    };
    if (nT == 1) work(0);
    else { std::vector<std::thread> th; for (int i = 1; i < nT; ++i) th.emplace_back(work, i); work(0); for (auto& t : th) t.join(); }
    shadowTris_.clear(); shadowValid_ = true;
  }

  // Fracao de luz (0 = sombra total, 1 = iluminado) que chega ao ponto p com normal n. PCF (media de vizinhos).
  f32 sunVisibility(Vec3 p, Vec3 n, f32 ndl) const {
    const f32 R = static_cast<f32>(shadowRes_);
    p = p + n * (shadowSet_.normalBias * shadowFr_.texelWorld);
    const Vec4 c = shadowFr_.viewProj.mul({p.x, p.y, p.z, 1});
    const f32 fx = (c.x * 0.5f + 0.5f) * R, fy = (1.f - (c.y * 0.5f + 0.5f)) * R;
    if (c.z > 1.f || c.z < 0.f || fx < 0.f || fy < 0.f || fx >= R || fy >= R) return 1.f; // fora do mapa: sem sombra
    const f32 bias = shadowSet_.bias * (1.f + 2.f * (1.f - ndl)); // mais bias em superficies de raspao
    const int k = std::clamp(shadowSet_.pcf, 0, 2), ix = static_cast<int>(fx), iy = static_cast<int>(fy), res = shadowRes_;
    int lit = 0, total = 0;
    for (int dy = -k; dy <= k; ++dy)
      for (int dx = -k; dx <= k; ++dx) {
        const int sx = std::clamp(ix + dx, 0, res - 1), sy = std::clamp(iy + dy, 0, res - 1);
        ++total; if (shadowMap_[static_cast<size_t>(sy) * static_cast<size_t>(res) + static_cast<size_t>(sx)] >= c.z - bias) ++lit;
      }
    return static_cast<f32>(lit) / static_cast<f32>(total);
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

  void rasterize(const Tri& t, int ya, int yb, u64& count) const {
    const Material& mat = mats_[t.mat];
    const f32 ax = t.x[0], ay = t.y[0], bx = t.x[1], by = t.y[1], cx = t.x[2], cy = t.y[2];
    for (int y = ya; y <= yb; ++y)
      for (int x = t.x0; x <= t.x1; ++x) {
        const f32 px = static_cast<f32>(x) + 0.5f, py = static_cast<f32>(y) + 0.5f;
        f32 w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / t.area, w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / t.area, w2 = 1.f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const f32 z = w0 * t.zw[0] + w1 * t.zw[1] + w2 * t.zw[2];
        const size_t di = static_cast<size_t>(y) * w_ + static_cast<size_t>(x);
        if (z < 0 || z >= depth_[di]) continue;
        depth_[di] = z;
        // interpolacao com correcao de perspectiva
        const f32 p0 = w0 * t.iw[0], p1 = w1 * t.iw[1], p2 = w2 * t.iw[2], inv = 1.f / (p0 + p1 + p2);
        const Vec3 wp = (t.wp[0] * p0 + t.wp[1] * p1 + t.wp[2] * p2) * inv;
        const Vec3 n = normalize((t.n[0] * p0 + t.n[1] * p1 + t.n[2] * p2) * inv);
        if (mat.albedoMap && mat.albedoMap->valid()) { // textura: multiplica a cor do material pelo texel (mais proximo)
          const f32 uu = (t.uv[0][0] * p0 + t.uv[1][0] * p1 + t.uv[2][0] * p2) * inv, vv = (t.uv[0][1] * p0 + t.uv[1][1] * p1 + t.uv[2][1] * p2) * inv;
          u8 tx[4]; mat.albedoMap->texel(uu, vv, tx);
          Material mm = mat; mm.albedo = {mat.albedo.x * tx[0] / 255.f, mat.albedo.y * tx[1] / 255.f, mat.albedo.z * tx[2] / 255.f};
          put(di, shade(wp, n, mm));
        } else put(di, shade(wp, n, mat));
        ++count;
      }
  }

  Vec3 shade(Vec3 p, Vec3 n, const Material& m) const {
    const Vec3 V = normalize(cam_.eye - p);
    Vec3 col{ambient_.x * m.albedo.x, ambient_.y * m.albedo.y, ambient_.z * m.albedo.z};
    const f32 shin = 2.f / std::max(m.roughness * m.roughness, 0.02f) - 2.f;
    for (const Light& L : lights_) {
      Vec3 toL; f32 att = 1.f;
      if (L.type == LightType::Directional) toL = normalize(L.direction * -1.f);
      else {
        const Vec3 d = L.position - p; const f32 dist = length(d); toL = d * (1.f / std::max(dist, 1e-4f));
        att = attenuation(dist, L.range);
        if (L.type == LightType::Spot) att *= spotFactor(L.direction, toL * -1.f, std::cos(L.innerAngle), std::cos(L.outerAngle));
      }
      const f32 ndl = std::max(0.f, dot(n, toL));
      if (ndl <= 0 || att <= 0) continue;
      if (L.castShadow && shadowOn_ && m.receiveShadow) att *= 1.f - shadowSet_.strength * (1.f - sunVisibility(p, n, ndl));
      const Vec3 H = normalize(toL + V);
      const f32 spec = std::pow(std::max(0.f, dot(n, H)), shin) * (1.f - m.roughness * 0.7f);
      const f32 k = L.intensity * att * ndl;
      const f32 diff = 1.f - m.metallic;
      col = col + Vec3{L.color.x * k * (m.albedo.x * diff + spec * (0.04f + m.metallic * m.albedo.x)), L.color.y * k * (m.albedo.y * diff + spec * (0.04f + m.metallic * m.albedo.y)),
                       L.color.z * k * (m.albedo.z * diff + spec * (0.04f + m.metallic * m.albedo.z))};
    }
    return col;
  }
  // Reinhard + gama 2.2 por tabela (a versao com pow() por pixel era o gargalo da CPU).
  static const u8* tonemapLut() {
    static const std::vector<u8> lut = [] {
      std::vector<u8> t(4097);
      for (size_t i = 0; i < t.size(); ++i) t[i] = static_cast<u8>(std::clamp(std::pow(static_cast<f32>(i) / 4096.f, 1.f / 2.2f), 0.f, 1.f) * 255.f + 0.5f);
      return t;
    }();
    return lut.data();
  }
  void put(size_t i, Vec3 c) const {
    const u8* lut = tonemapLut();
    auto tm = [&](f32 v) { v = std::max(v, 0.f); v = v / (1.f + v); return lut[static_cast<int>(v * 4096.f)]; };
    u8* o = const_cast<u8*>(&color_[i * 4]);
    o[0] = tm(c.x); o[1] = tm(c.y); o[2] = tm(c.z); o[3] = 255;
  }

  u32 w_ = 1, h_ = 1;
  mutable std::vector<u8> color_;
  mutable std::vector<f32> depth_;
  mutable std::vector<Tri> pending_;
  std::vector<Material> mats_;
  Camera cam_; Mat4 view_, viewProj_;
  std::vector<Light> lights_; Vec3 ambient_;
  u64 tris_ = 0;
  mutable u64 shaded_ = 0;
  // sombras do sol
  bool shadowOn_ = false, shadowSkip_ = false; mutable bool shadowValid_ = false; ShadowSettings shadowSet_; ShadowFrustum shadowFr_; int shadowRes_ = 1024;
  mutable std::vector<f32> shadowMap_;
  mutable std::vector<ShadowTri> shadowTris_;
};

} // namespace tool
