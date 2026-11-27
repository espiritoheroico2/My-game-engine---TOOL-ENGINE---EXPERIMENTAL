// Newtoon - narrowphase convexa: casco x casco, casco x caixa e casco x esfera pelo SAT (Teorema do Eixo Separador).
//
// MODELO: toda forma convexa (casco ou caixa) vira uma "vista" no mundo: vertices e planos das faces ja transformados
// (posicao + rotacao + escala). A caixa usa a mesma topologia (cubo unitario) do casco, entao UM unico caminho de codigo
// cobre casco x caixa (o chao e uma caixa estatica grande) e casco x casco.
//
// SAT: eixos das faces dos dois corpos + produtos vetoriais de arestas (so os pares que formam uma face da soma de
// Minkowski: teste do arco de Gauss, Gregorius GDC 2013). O eixo de MENOR penetracao decide:
//   - eixo de face: recorte (Sutherland-Hodgman) da face incidente contra as laterais da face de referencia ->
//     poligono de contato (ate 8 pontos; se houver mais, escolhe os 8 mais espalhados, deterministico);
//   - eixo de aresta x aresta: um ponto (meio do segmento mais curto entre as arestas).
// Sem alocacao: tudo em arrays de tamanho fixo na pilha. Normal sempre aponta de A para B, profundidade > 0.
#pragma once
#include "newtoon_hull.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Newtoon {

// Ponto de contato gerado pela narrowphase convexa.
struct ConvexContact { Vec3 p, n; float depth = 0.f; };
// Maximo de pontos por par (igual ao kMaxManifoldPoints do mundo; o warm start guarda ate 8).
constexpr int kConvexMaxContacts = 8;

// Forma convexa no espaco do mundo. `topo` fornece faces/arestas (indices); v/fn/fd sao os dados ja transformados.
struct ConvexView {
  const ConvexHull* topo = nullptr;
  int nv = 0, nf = 0;
  Vec3 v[kMaxHullVerts];
  Vec3 fn[kMaxHullFaces];
  float fd[kMaxHullFaces];
  Vec3 centroid;
};

namespace convex_detail {

// Cubo unitario (vertices +-1) com topologia pronta; uma instancia estatica compartilhada por todas as caixas.
inline const ConvexHull& UnitBoxTopology() {
  static const ConvexHull box = [] {
    ConvexHull h;
    for (int s = 0; s < 8; ++s) h.points.push_back({(s & 1) ? 1.f : -1.f, (s & 2) ? 1.f : -1.f, (s & 4) ? 1.f : -1.f});
    h.bounds = {{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    hull_detail::BuildTopology(h);
    return h;
  }();
  return box;
}

// Numero de lados do prisma que aproxima o cilindro na narrowphase (16 lados = 32 vertices, 18 faces, 48 arestas).
constexpr int kCylinderSides = 16;

// Cilindro unitario (raio 1, meia altura 1, eixo Y) como prisma de kCylinderSides lados, com topologia pronta.
// Os vertices ficam NO circulo (poligono inscrito): a colisao fica no maximo ~2% menor que o cilindro analitico
// (cos(pi/16) = 0.98). Escalando por (raio, meiaAltura, raio) obtem-se qualquer cilindro. Compartilhado por todos.
inline const ConvexHull& UnitCylinderTopology() {
  static const ConvexHull cyl = [] {
    ConvexHull h;
    for (int i = 0; i < kCylinderSides; ++i) {
      const float a = 6.28318531f * static_cast<float>(i) / static_cast<float>(kCylinderSides);
      h.points.push_back({std::cos(a), -1.f, std::sin(a)});
      h.points.push_back({std::cos(a), 1.f, std::sin(a)});
    }
    h.bounds = {{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    hull_detail::BuildTopology(h);
    return h;
  }();
  return cyl;
}

}  // namespace convex_detail

// Monta a vista no mundo: v = c + R*(p * s). Planos: n' = normalize(n / s), d' = d / |n / s| (valido para escala
// nao uniforme positiva), depois levados ao mundo por R e c.
inline void BuildConvexView(const ConvexHull& topo, const Mat3& R, const Vec3& c, const Vec3& scale, ConvexView& out) {
  out.topo = &topo;
  out.nv = static_cast<int>(std::min<size_t>(topo.points.size(), kMaxHullVerts));
  out.nf = static_cast<int>(std::min<size_t>(topo.faces.size(), kMaxHullFaces));
  const Vec3 s{std::max(scale.x, 1e-4f), std::max(scale.y, 1e-4f), std::max(scale.z, 1e-4f)};
  const bool unit = s.x == 1.f && s.y == 1.f && s.z == 1.f;
  Vec3 cen{0, 0, 0};
  for (int i = 0; i < out.nv; ++i) {
    const Vec3& p = topo.points[static_cast<size_t>(i)];
    out.v[i] = c + R * Vec3{p.x * s.x, p.y * s.y, p.z * s.z};
    cen += out.v[i];
  }
  out.centroid = out.nv > 0 ? cen * (1.f / static_cast<float>(out.nv)) : c;
  for (int f = 0; f < out.nf; ++f) {
    const HullFace& hf = topo.faces[static_cast<size_t>(f)];
    if (unit) { out.fn[f] = R * hf.n; out.fd[f] = hf.d + Dot(out.fn[f], c); continue; }   // escala 1: sem divisao nem raiz
    const Vec3 nl{hf.n.x / s.x, hf.n.y / s.y, hf.n.z / s.z};
    const float L = std::max(nl.Magnitude(), 1e-12f);
    out.fn[f] = R * (nl * (1.f / L));
    out.fd[f] = hf.d / L + Dot(out.fn[f], c);
  }
}

// Vista de uma caixa orientada (centro c, rotacao R, meias-medidas e).
inline void BuildBoxView(const Mat3& R, const Vec3& c, const Vec3& e, ConvexView& out) {
  BuildConvexView(convex_detail::UnitBoxTopology(), R, c, e, out);
}

// Vista de um cilindro de eixo Y local (raio r, meia altura hh) como prisma convexo: mesmo SAT do casco e da caixa.
inline void BuildCylinderView(const Mat3& R, const Vec3& c, float r, float hh, ConvexView& out) {
  BuildConvexView(convex_detail::UnitCylinderTopology(), R, c, Vec3{r, hh, r}, out);
}

namespace convex_detail {

// Teste do arco de Gauss: as arestas (a1,a2 = normais das faces de A; b1,b2 = de B) formam uma face da soma de
// Minkowski? Arcos A-B e C-D (C=-b1, D=-b2) na esfera unitaria devem se cruzar.
inline bool IsMinkowskiFace(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
  const Vec3 bxa = Cross(b, a), dxc = Cross(d, c);
  const float cba = Dot(c, bxa), dba = Dot(d, bxa), adc = Dot(a, dxc), bdc = Dot(b, dxc);
  return cba * dba < 0.f && adc * bdc < 0.f && cba * bdc > 0.f;
}

// Reduz n pontos para m escolhendo os mais espalhados: o mais profundo primeiro, depois repetidamente o que maximiza
// a distancia minima aos ja escolhidos (amostragem do ponto mais distante). Empates: menor indice -> deterministico.
inline int ReduceContacts(ConvexContact* pts, int n, int m) {
  if (n <= m) return n;
  ConvexContact chosen[kConvexMaxContacts];
  bool used[64] = {};
  int cnt = 0, first = 0;
  for (int i = 1; i < n; ++i) if (pts[i].depth > pts[first].depth) first = i;
  chosen[cnt++] = pts[first]; used[first] = true;
  while (cnt < m) {
    int bi = -1; float bd = -1.f;
    for (int i = 0; i < n; ++i) {
      if (used[i]) continue;
      float md = 1e30f;
      for (int k = 0; k < cnt; ++k) md = std::min(md, (pts[i].p - chosen[k].p).SqrMagnitude());
      if (md > bd) { bd = md; bi = i; }
    }
    if (bi < 0) break;
    chosen[cnt++] = pts[bi]; used[bi] = true;
  }
  for (int i = 0; i < cnt; ++i) pts[i] = chosen[i];
  return cnt;
}

}  // namespace convex_detail

// Colisao convexa x convexa. Devolve o numero de contatos (0 = separados), em out[0..kConvexMaxContacts).
// Normal de A para B; depth > 0.
inline int ConvexCollide(const ConvexView& A, const ConvexView& B, ConvexContact* out) {
  using namespace convex_detail;
  // 1) Faces de A e de B: separacao = menor projecao do outro corpo menos o plano. > 0 => existe eixo separador.
  float sepA = -1e30f, sepB = -1e30f; int fA = -1, fB = -1;
  for (int f = 0; f < A.nf; ++f) {
    float mn = 1e30f; for (int j = 0; j < B.nv; ++j) mn = std::min(mn, Dot(A.fn[f], B.v[j]));
    const float s = mn - A.fd[f];
    if (s > 0.f) return 0;
    if (s > sepA) { sepA = s; fA = f; }
  }
  for (int f = 0; f < B.nf; ++f) {
    float mn = 1e30f; for (int j = 0; j < A.nv; ++j) mn = std::min(mn, Dot(B.fn[f], A.v[j]));
    const float s = mn - B.fd[f];
    if (s > 0.f) return 0;
    if (s > sepB) { sepB = s; fB = f; }
  }
  if (fA < 0 || fB < 0) return 0;

  // 2) Arestas x arestas, so os pares que formam face de Minkowski.
  float sepE = -1e30f; Vec3 axisE{0, 1, 0}; int eA = -1, eB = -1;
  const auto& edgesA = A.topo->edges; const auto& edgesB = B.topo->edges;
  // Pre-calculo por aresta de B do plano do arco de Gauss (d x c com c=-n1, d=-n2): feito UMA vez, nao a cada par.
  Vec3 dxcB[kMaxHullEdges];
  for (size_t ib = 0; ib < edgesB.size(); ++ib) dxcB[ib] = Cross(-B.fn[edgesB[ib].f1], -B.fn[edgesB[ib].f0]);
  for (size_t ia = 0; ia < edgesA.size(); ++ia) {
    const HullEdge& ea = edgesA[ia];
    const Vec3 dA = A.v[ea.b] - A.v[ea.a];
    const float lenA2 = dA.SqrMagnitude();
    const Vec3 &a1 = A.fn[ea.f0], &a2 = A.fn[ea.f1];
    const Vec3 bxa = Cross(a2, a1);
    for (size_t ib = 0; ib < edgesB.size(); ++ib) {
      const HullEdge& eb = edgesB[ib];
      // Teste do arco de Gauss (IsMinkowskiFace) com saida antecipada: a maioria dos pares falha nos 2 primeiros produtos.
      const Vec3 c = -B.fn[eb.f0], d = -B.fn[eb.f1];
      const float cba = Dot(c, bxa), dba = Dot(d, bxa);
      if (!(cba * dba < 0.f)) continue;
      const Vec3& dxc = dxcB[ib];
      const float adc = Dot(a1, dxc), bdc = Dot(a2, dxc);
      if (!(adc * bdc < 0.f && cba * bdc > 0.f)) continue;
      const Vec3 dB = B.v[eb.b] - B.v[eb.a];
      Vec3 ax = Cross(dA, dB);
      const float l2 = ax.SqrMagnitude();
      if (l2 < 1e-8f * lenA2 * dB.SqrMagnitude() || l2 < 1e-20f) continue;   // arestas quase paralelas
      ax = ax * (1.f / std::sqrt(l2));
      if (Dot(ax, A.v[ea.a] - A.centroid) < 0.f) ax = -ax;                   // aponta para fora de A
      const float s = Dot(ax, B.v[eb.a] - A.v[ea.a]);
      if (s > 0.f) return 0;
      if (s > sepE) { sepE = s; axisE = ax; eA = static_cast<int>(ia); eB = static_cast<int>(ib); }
    }
  }

  const float faceBest = std::max(sepA, sepB);
  // Aresta so vence com folga (igual ao caixa-caixa): evita alternar entre face e aresta em repouso.
  if (eA >= 0 && (-sepE) * 1.05f + 1e-4f < (-faceBest)) {
    const HullEdge& ea = edgesA[static_cast<size_t>(eA)]; const HullEdge& eb = edgesB[static_cast<size_t>(eB)];
    const Vec3 pa = A.v[ea.a], da = A.v[ea.b] - pa, pb = B.v[eb.a], db = B.v[eb.b] - pb, r = pa - pb;
    const float a = Dot(da, da), e = Dot(db, db), b = Dot(da, db), c = Dot(da, r), f = Dot(db, r), den = a * e - b * b;
    float s = 0.5f, t = 0.5f;
    if (std::fabs(den) > 1e-12f) { s = std::clamp((b * f - c * e) / den, 0.f, 1.f); t = std::clamp((a * f - b * c) / den, 0.f, 1.f); }
    out[0] = {((pa + da * s) + (pb + db * t)) * 0.5f, axisE, std::max(-sepE, 1e-4f)};
    return 1;
  }

  // 3) Contato de face: recorte da face incidente contra a face de referencia.
  const bool refIsA = sepA >= sepB - 5e-4f;            // empate: A e a referencia (deterministico)
  const ConvexView& R = refIsA ? A : B; const ConvexView& I = refIsA ? B : A;
  const int fr = refIsA ? fA : fB;
  const Vec3 nR = R.fn[fr];
  const HullFace& refFace = R.topo->faces[static_cast<size_t>(fr)];
  int fi = 0; float md = 1e30f;                         // face incidente: a mais oposta a nR
  for (int f = 0; f < I.nf; ++f) { const float d = Dot(I.fn[f], nR); if (d < md) { md = d; fi = f; } }
  const HullFace& incFace = I.topo->faces[static_cast<size_t>(fi)];

  constexpr int kBuf = 64;
  Vec3 bufA[kBuf], bufB[kBuf]; Vec3* cur = bufA; Vec3* nxt = bufB; int nc = 0;
  for (int k = 0; k < incFace.count && nc < kBuf; ++k) cur[nc++] = I.v[I.topo->faceVerts[incFace.first + k]];
  for (int i = 0; i < refFace.count && nc > 0; ++i) {
    const Vec3 a0 = R.v[R.topo->faceVerts[refFace.first + i]];
    const Vec3 a1 = R.v[R.topo->faceVerts[refFace.first + (i + 1) % refFace.count]];
    Vec3 sn = Cross(a1 - a0, nR);                      // lateral que aponta para fora do poligono de referencia
    const float sl = sn.Magnitude(); if (sl < 1e-12f) continue; sn = sn * (1.f / sl);
    int nn = 0;
    for (int k = 0; k < nc; ++k) {
      const Vec3 &p = cur[k], &q = cur[(k + 1) % nc];
      const float dp = Dot(sn, p - a0), dq = Dot(sn, q - a0);
      const bool pin = dp <= 0.f, qin = dq <= 0.f;
      if (pin && nn < kBuf) nxt[nn++] = p;
      if (pin != qin) {
        // Intersecao so se cair estritamente dentro da aresta: com vertice exatamente sobre o plano (faces alinhadas)
        // o ponto ja foi emitido e repetir geraria contatos duplicados.
        const float t = dp / (dp - dq);
        if (t > 1e-5f && t < 1.f - 1e-5f && nn < kBuf) nxt[nn++] = p + (q - p) * t;
      }
    }
    std::swap(cur, nxt); nc = nn;
  }

  const Vec3 nContact = refIsA ? nR : -nR;             // sempre de A para B
  ConvexContact tmp[kBuf]; int cnt = 0;
  for (int k = 0; k < nc; ++k) {
    const float depth = R.fd[fr] - Dot(nR, cur[k]);    // abaixo do plano de referencia = penetrando
    if (depth > 0.f) tmp[cnt++] = {cur[k], nContact, depth};
  }
  if (cnt == 0) {                                      // numerico: fica o vertice incidente mais fundo
    int best = 0; float bd = 1e30f;
    for (int j = 0; j < I.nv; ++j) { const float d = Dot(nR, I.v[j]); if (d < bd) { bd = d; best = j; } }
    tmp[cnt++] = {I.v[best], nContact, std::max(-faceBest, 1e-4f)};
  }
  cnt = ReduceContacts(tmp, cnt, kConvexMaxContacts);
  for (int k = 0; k < cnt; ++k) out[k] = tmp[k];
  return cnt;
}

// Esfera (centro cs, raio r) contra forma convexa X. Normal da ESFERA para X (como o SphereBox do mundo).
inline bool SphereConvexCollide(const Vec3& cs, float r, const ConvexView& X, ConvexContact& out) {
  float best = -1e30f; int bf = -1;
  for (int f = 0; f < X.nf; ++f) {
    const float s = Dot(X.fn[f], cs) - X.fd[f];
    if (s >= r) return false;                          // eixo separador de face
    if (s > best) { best = s; bf = f; }
  }
  if (bf < 0) return false;
  const Vec3 nf = X.fn[bf];
  const Vec3 q = cs - nf * best;                       // projecao do centro no plano da melhor face
  bool inside = best < 0.f;                            // centro dentro do solido: face mais rasa
  if (!inside) {                                       // fora: a projecao cai dentro do poligono da face?
    const HullFace& hf = X.topo->faces[static_cast<size_t>(bf)];
    inside = true;
    for (int i = 0; i < hf.count && inside; ++i) {
      const Vec3 a0 = X.v[X.topo->faceVerts[hf.first + i]], a1 = X.v[X.topo->faceVerts[hf.first + (i + 1) % hf.count]];
      if (Dot(Cross(a1 - a0, nf), q - a0) > 0.f) inside = false;
    }
  }
  if (inside) { out = {q, -nf, r - best}; return true; }
  // Fora e sobre aresta/vertice: ponto mais proximo entre todas as arestas.
  float bd2 = 1e30f; Vec3 bq = q;
  for (const HullEdge& e : X.topo->edges) {
    const Vec3 a0 = X.v[e.a], ab = X.v[e.b] - a0;
    const float l2 = std::max(ab.SqrMagnitude(), 1e-20f);
    const float t = std::clamp(Dot(cs - a0, ab) / l2, 0.f, 1.f);
    const Vec3 pq = a0 + ab * t; const float d2 = (cs - pq).SqrMagnitude();
    if (d2 < bd2) { bd2 = d2; bq = pq; }
  }
  const float dist = std::sqrt(bd2);
  if (dist >= r) return false;
  const Vec3 nXS = dist > 1e-6f ? (cs - bq) * (1.f / dist) : nf;   // de X para a esfera
  out = {bq, -nXS, r - dist};
  return true;
}

// Raio contra o casco no espaco do corpo (planos escalados). Devolve true com t e normal no MUNDO.
inline bool RayConvex(const ConvexHull& hull, const Mat3& R, const Vec3& c, const Vec3& scale, const Vec3& o, const Vec3& d, float maxT, float& tOut, Vec3& nOut, bool* startsInside = nullptr) {
  const Mat3 Rt = R.Transposed(); const Vec3 lo = Rt * (o - c), ld = Rt * d;
  const Vec3 s{std::max(scale.x, 1e-4f), std::max(scale.y, 1e-4f), std::max(scale.z, 1e-4f)};
  float t0 = 0.f, t1 = maxT; int face = -1; Vec3 nEnter{0, 1, 0};
  for (size_t f = 0; f < hull.faces.size(); ++f) {
    const HullFace& hf = hull.faces[f];
    const Vec3 nl{hf.n.x / s.x, hf.n.y / s.y, hf.n.z / s.z};
    const float L = std::max(nl.Magnitude(), 1e-12f);
    const Vec3 n = nl * (1.f / L); const float dd = hf.d / L;
    const float dist = Dot(n, lo) - dd, den = Dot(n, ld);
    if (std::fabs(den) < 1e-9f) { if (dist > 0.f) return false; continue; }
    const float t = -dist / den;
    if (den < 0.f) { if (t > t0) { t0 = t; face = static_cast<int>(f); nEnter = n; } }
    else t1 = std::min(t1, t);
    if (t0 > t1) return false;
  }
  tOut = t0; nOut = face >= 0 ? R * nEnter : Vec3{0, 1, 0};
  if (startsInside) *startsInside = face < 0;          // nenhum plano de entrada: o raio nasce dentro do casco
  return true;
}

// Raio contra o cilindro analitico (eixo Y local, raio r, meia altura hh) no mundo: centro c, rotacao R.
// Interseccao raio x (laje |y| <= hh) x (disco x^2+z^2 <= r^2). Devolve t e a normal no MUNDO; se o raio nasce
// dentro, t = 0, normal = -d e *startsInside = true (mesma convencao da caixa).
inline bool RayCylinder(const Mat3& R, const Vec3& c, float r, float hh, const Vec3& o, const Vec3& d, float maxT, float& tOut, Vec3& nOut, bool* startsInside = nullptr) {
  const Mat3 Rt = R.Transposed(); const Vec3 lo = Rt * (o - c), ld = Rt * d;
  float t0 = 0.f, t1 = maxT; Vec3 nl{0, 0, 0}; bool entered = false;
  // Laje em Y (tampas).
  if (std::fabs(ld.y) < 1e-9f) { if (std::fabs(lo.y) > hh) return false; }
  else {
    float a = (-hh - lo.y) / ld.y, b = (hh - lo.y) / ld.y; float sg = -1.f;
    if (a > b) { std::swap(a, b); sg = 1.f; }
    if (a > t0) { t0 = a; nl = {0.f, sg, 0.f}; entered = true; }
    t1 = std::min(t1, b); if (t0 > t1) return false;
  }
  // Lateral: |(lo + t ld)_xz|^2 = r^2 e uma quadratica em t.
  const float qa = ld.x * ld.x + ld.z * ld.z, qb = lo.x * ld.x + lo.z * ld.z, qc = lo.x * lo.x + lo.z * lo.z - r * r;
  if (qa < 1e-12f) { if (qc > 0.f) return false; }     // raio paralelo ao eixo: so conta se estiver dentro do disco
  else {
    const float disc = qb * qb - qa * qc; if (disc < 0.f) return false;
    const float sq = std::sqrt(disc), a = (-qb - sq) / qa, b = (-qb + sq) / qa;
    if (a > t0) { t0 = a; const Vec3 hp = lo + ld * a; const float L = std::max(std::sqrt(hp.x * hp.x + hp.z * hp.z), 1e-12f); nl = {hp.x / L, 0.f, hp.z / L}; entered = true; }
    t1 = std::min(t1, b); if (t0 > t1) return false;
  }
  tOut = t0;
  nOut = entered ? R * nl : -d;
  if (startsInside) *startsInside = !entered;
  return true;
}

}  // namespace Newtoon
