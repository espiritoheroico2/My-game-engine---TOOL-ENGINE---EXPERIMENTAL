// occlusion.hpp - Oclusao por software, conservadora: pula objetos que estao INTEIROS atras de superficies grandes.
//
// COMO FUNCIONA (por quadro)
//  1. begin(): cria uma grade de profundidade grossa (1/8 da resolucao, minimo 16 x 9).
//  2. addTriangle(): cada oclusor (triangulo no mundo) so conta nas celulas que ele cobre INTEIRAS. Para cada
//     celula guardamos a profundidade MAIS DISTANTE desse oclusor naquela celula (a profundidade e interpolada
//     em 1/z, que e afim na tela, entao o maximo sai nos cantos da celula). Varios oclusores: vale o mais proximo.
//  3. hidden(): a caixa envolvente do objeto (8 cantos no mundo) so e escondida se, em TODAS as celulas que ela
//     cobre na tela, existe um oclusor cuja parte mais distante ainda esta mais perto que o ponto mais proximo
//     da caixa. Se qualquer canto estiver atras do plano near, ou a caixa estiver fora da tela, nao esconde.
//
// POR QUE E SEGURO: uma celula so conta se o oclusor a cobre por inteiro, e a comparacao usa o ponto mais
// proximo da caixa. Logo, o objeto escondido nunca apareceria em nenhum pixel. Na duvida, desenha.
// Limite: so para camera em perspectiva; oclusor pequeno (menor que uma celula) nao esconde nada.
#pragma once

#include "tool/math/math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace tool::render {

class OcclusionBuffer {
public:
  // Prepara a grade para uma imagem w x h. viewProj = projecao * vista da camera (mesma do renderizador).
  void begin(u32 w, u32 h, const Mat4& viewProj) {
    gw_ = std::max<u32>(16, w / 8);
    gh_ = std::max<u32>(9, h / 8);
    vp_ = viewProj;
    zfar_.assign(static_cast<size_t>(gw_) * gh_, kInf);
    active_ = true;
  }
  bool active() const { return active_; }

  // Oclusor: triangulo em coordenadas do mundo. Ignorado se algum vertice estiver atras do plano near.
  void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    if (!active_) return;
    Vec4 ca, cb, cc;
    if (!project(a, ca) || !project(b, cb) || !project(c, cc)) return;
    float ax, ay, bx, by, cx, cy;
    toGrid(ca, ax, ay); toGrid(cb, bx, by); toGrid(cc, cx, cy);
    const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::fabs(area) < 1e-9f) return;                       // triangulo de aresta: nao cobre celula nenhuma
    const float ia = 1.f / ca.w, ib = 1.f / cb.w, ic = 1.f / cc.w;
    const int x0 = clampi(static_cast<int>(std::floor(std::min({ax, bx, cx}))), 0, static_cast<int>(gw_));
    const int x1 = clampi(static_cast<int>(std::ceil(std::max({ax, bx, cx}))), 0, static_cast<int>(gw_));
    const int y0 = clampi(static_cast<int>(std::floor(std::min({ay, by, cy}))), 0, static_cast<int>(gh_));
    const int y1 = clampi(static_cast<int>(std::ceil(std::max({ay, by, cy}))), 0, static_cast<int>(gh_));
    for (int ty = y0; ty < y1; ++ty)
      for (int tx = x0; tx < x1; ++tx) {
        float minInv = std::numeric_limits<float>::max();
        bool full = true;
        for (int k = 0; k < 4 && full; ++k) {
          const float px = static_cast<float>(tx + (k & 1)), py = static_cast<float>(ty + (k >> 1));
          const float wa = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;   // peso de A no ponto
          const float wb = ((px - ax) * (cy - ay) - (py - ay) * (cx - ax)) / area;   // peso de B
          const float wc = 1.f - wa - wb;                                            // peso de C
          if (wa < -kEps || wb < -kEps || wc < -kEps) { full = false; break; }
          minInv = std::min(minInv, wa * ia + wb * ib + wc * ic);
        }
        if (!full) continue;
        const float zFarthest = 1.f / minInv;                  // profundidade mais distante do oclusor na celula
        float& cell = zfar_[static_cast<size_t>(ty) * gw_ + static_cast<size_t>(tx)];
        cell = std::min(cell, zFarthest);
      }
  }

  // Quad: dois triangulos de uma mesma face (a,b,c) + (a,c,d). Se for plano e convexo na tela, a celula conta quando
  // os 4 cantos dela estao DENTRO do quad, mesmo que a diagonal passe por ela. A profundidade vem do plano: 1/w e
  // afim na tela, entao a extrapolacao de (a,b,c) fora do triangulo e exata. Se nao for plano/convexo, usa os triangulos.
  void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    if (!active_) return;
    const Vec3 n = cross3(sub3(b, a), sub3(c, a));
    const float nl = std::sqrt(dot3(n, n));
    if (nl < 1e-12f || std::fabs(dot3(n, sub3(d, a))) > 1e-4f * nl * std::max(1.f, std::sqrt(dot3(sub3(d, a), sub3(d, a))))) {
      addTriangle(a, b, c); addTriangle(a, c, d); return;   // nao e plano
    }
    Vec4 p[4]; const Vec3 v[4] = {a, b, c, d};
    float gx[4], gy[4];
    for (int i = 0; i < 4; ++i) { if (!project(v[i], p[i])) return; toGrid(p[i], gx[i], gy[i]); }
    // convexidade: todas as curvas com o mesmo sinal
    float sgn = 0.f;
    for (int i = 0; i < 4; ++i) {
      const int j = (i + 1) % 4, k = (i + 2) % 4;
      const float cr = (gx[j] - gx[i]) * (gy[k] - gy[j]) - (gy[j] - gy[i]) * (gx[k] - gx[j]);
      if (std::fabs(cr) < 1e-9f) { addTriangle(a, b, c); addTriangle(a, c, d); return; }   // quad degenerado
      const float s = cr > 0 ? 1.f : -1.f;
      if (sgn == 0.f) sgn = s; else if (s != sgn) { addTriangle(a, b, c); addTriangle(a, c, d); return; }  // nao convexo
    }
    // pesos do plano: usa o triangulo (a,b,c) sem clampear (extrapolacao afim)
    const float area = (gx[1] - gx[0]) * (gy[2] - gy[0]) - (gy[1] - gy[0]) * (gx[2] - gx[0]);
    if (std::fabs(area) < 1e-9f) { addTriangle(a, c, d); return; }
    const float ia = 1.f / p[0].w, ib = 1.f / p[1].w, ic = 1.f / p[2].w;
    auto inside = [&](float px, float py) {
      for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const float cr = (gx[j] - gx[i]) * (py - gy[i]) - (gy[j] - gy[i]) * (px - gx[i]);
        if (sgn > 0 ? cr < -kEps : cr > kEps) return false;
      }
      return true;
    };
    const int x0 = clampi(static_cast<int>(std::floor(std::min({gx[0], gx[1], gx[2], gx[3]}))), 0, static_cast<int>(gw_));
    const int x1 = clampi(static_cast<int>(std::ceil(std::max({gx[0], gx[1], gx[2], gx[3]}))), 0, static_cast<int>(gw_));
    const int y0 = clampi(static_cast<int>(std::floor(std::min({gy[0], gy[1], gy[2], gy[3]}))), 0, static_cast<int>(gh_));
    const int y1 = clampi(static_cast<int>(std::ceil(std::max({gy[0], gy[1], gy[2], gy[3]}))), 0, static_cast<int>(gh_));
    for (int ty = y0; ty < y1; ++ty)
      for (int tx = x0; tx < x1; ++tx) {
        float minInv = std::numeric_limits<float>::max();
        bool full = true;
        for (int k = 0; k < 4 && full; ++k) {
          const float px = static_cast<float>(tx + (k & 1)), py = static_cast<float>(ty + (k >> 1));
          if (!inside(px, py)) { full = false; break; }
          const float wa = ((gx[1] - px) * (gy[2] - py) - (gy[1] - py) * (gx[2] - px)) / area;
          const float wb = ((px - gx[0]) * (gy[2] - gy[0]) - (py - gy[0]) * (gx[2] - gx[0])) / area;
          minInv = std::min(minInv, wa * ia + wb * ib + (1.f - wa - wb) * ic);
        }
        if (!full || minInv <= 0.f) continue;
        float& cell = zfar_[static_cast<size_t>(ty) * gw_ + static_cast<size_t>(tx)];
        cell = std::min(cell, 1.f / minInv);
      }
  }

  // Caixa envolvente em 8 cantos (coordenadas do mundo): esta escondida atras dos oclusores?
  bool hidden(const Vec3 corners[8]) const {
    if (!active_) return false;
    float minW = std::numeric_limits<float>::max();
    float gx0 = std::numeric_limits<float>::max(), gy0 = gx0, gx1 = -gx0, gy1 = -gx0;
    for (int i = 0; i < 8; ++i) {
      Vec4 c;
      if (!project(corners[i], c)) return false;               // atras do near: nao da para afirmar nada
      minW = std::min(minW, c.w);
      float x, y; toGrid(c, x, y);
      gx0 = std::min(gx0, x); gx1 = std::max(gx1, x);
      gy0 = std::min(gy0, y); gy1 = std::max(gy1, y);
    }
    const int x0 = clampi(static_cast<int>(std::floor(gx0)), 0, static_cast<int>(gw_));
    const int x1 = clampi(static_cast<int>(std::ceil(gx1)), 0, static_cast<int>(gw_));
    const int y0 = clampi(static_cast<int>(std::floor(gy0)), 0, static_cast<int>(gh_));
    const int y1 = clampi(static_cast<int>(std::ceil(gy1)), 0, static_cast<int>(gh_));
    if (x1 <= x0 || y1 <= y0) return false;                    // fora da tela: o recorte ja cuida disso
    for (int ty = y0; ty < y1; ++ty)
      for (int tx = x0; tx < x1; ++tx)
        if (!(zfar_[static_cast<size_t>(ty) * gw_ + static_cast<size_t>(tx)] < minW)) return false;
    return true;
  }

private:
  static constexpr float kInf = std::numeric_limits<float>::max();
  static constexpr float kNear = 1e-4f;  // w minimo aceito (a frente da camera)
  static constexpr float kEps = 1e-5f;   // tolerancia de borda na cobertura

  static Vec3 sub3(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  static Vec3 cross3(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
  static float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

  bool project(const Vec3& p, Vec4& out) const {
    out = vp_.mul({p.x, p.y, p.z, 1.f});
    return out.w > kNear;
  }
  // Coordenadas de grade: x para a direita, y para baixo (so importa que sejam as mesmas em todo lugar).
  void toGrid(const Vec4& c, float& x, float& y) const {
    x = (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(gw_);
    y = (0.5f - c.y / c.w * 0.5f) * static_cast<float>(gh_);
  }

  Mat4 vp_;
  u32 gw_ = 0, gh_ = 0;
  bool active_ = false;
  std::vector<float> zfar_;  // por celula: profundidade mais distante do oclusor mais proximo que cobre a celula
};

} // namespace tool::render
