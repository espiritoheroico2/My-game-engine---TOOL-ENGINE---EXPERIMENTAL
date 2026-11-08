// ToolEngine - importacao de modelos 3D: tipos comuns e utilitarios.
//
// VISAO GERAL DO PIPELINE DE IMPORTACAO
//   arquivo (.glb/.gltf/.fbx) --> parser do formato --> ModelData (lista de ModelPart) --> editor/jogo
//
//  * Cada ModelPart = uma malha (tool::Mesh) + um material (cor/metalico/rugosidade) + nome + no dono.
//  * A HIERARQUIA do arquivo e preservada (ModelData::nodes): cada no tem o pai, a transformacao local e as
//    partes que desenha. As malhas ficam no espaco local do proprio no (nao sao "assadas" pela matriz de mundo).
//    Um no cuja matriz nao e TRS puro (espelhada ou com cisalhamento) e assado no normalizeNodes(): a
//    transformacao vai para as malhas dele e para a transformacao dos filhos, mantendo a aparencia.
//  * Uma malha de um arquivo pode ter varias partes com materiais diferentes (uma por primitive/material).
//  * Convencoes de saida (iguais as do resto da engine): mao direita, Y para cima, metros, triangulos
//    anti-horarios, UV com origem no canto superior esquerdo.
//  * Entrada NAO CONFIAVEL: todo importador valida limites (ImportLimits), indices e tamanhos, e devolve
//    erro (nunca crasha) em arquivo malformado. Existe fuzzer para isso em fuzz/.
//  * Nao suportado ainda (avisos em ModelData::warnings): texturas, animacao, skin/ossos, cameras/luzes
//    do arquivo, acessores esparsos, FBX em formato ASCII.
#pragma once
#include "tool/debug/debug.hpp"
#include "tool/math/math.hpp"
#include "tool/render/soft_renderer.hpp" // Mesh, Material

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace tool::assets {

namespace code {
inline constexpr const char* kImportFail = "TOOL-E240";  // arquivo invalido / formato nao suportado
inline constexpr const char* kImportLimit = "TOOL-E241"; // arquivo excede os limites de seguranca
inline constexpr const char* kImportWarn = "TOOL-W240";  // importou, mas ignorou algo
} // namespace code

// Limites de seguranca contra arquivos maliciosos ou corrompidos ("bombas" de memoria).
struct ImportLimits {
  size_t maxFileBytes = 256u << 20;   // tamanho do arquivo
  size_t maxVertices = 8u << 20;      // vertices totais do modelo
  size_t maxNodes = 100000;           // nos de cena / modelos
  size_t maxDepth = 64;               // profundidade da hierarquia (evita recursao infinita)
};

struct ImportOptions {
  ImportLimits limits;
  // FBX: aplicar UnitScaleFactor (cm -> m) e eixo "up" do arquivo (Z-up -> Y-up). Desligue para receber
  // os numeros crus do arquivo.
  bool applyUnitScale = true;
  bool applyUpAxis = true;
};

struct ModelPart {
  std::string name;
  Mesh mesh;             // no espaco local do no dono
  Material material;
  int node = 0;          // indice em ModelData::nodes
};

// No da hierarquia do arquivo. O pai sempre tem indice menor que o filho (ordem de visita).
struct ModelNode {
  std::string name;
  int parent = -1;       // -1 = raiz
  Mat4 local;            // transformacao relativa ao pai (identidade por padrao)
  std::vector<int> parts; // indices em ModelData::parts desenhados por este no
};

struct ModelData {
  std::string format;                 // "glTF", "GLB", "FBX"
  std::vector<ModelPart> parts;
  std::vector<ModelNode> nodes;       // hierarquia; vazia = modelo sem nos (so partes, no espaco do mundo)
  std::vector<std::string> warnings;  // recursos ignorados (texturas, animacao...)

  // Matriz de mundo de cada no (pai * local, na ordem da lista).
  std::vector<Mat4> worldMatrices() const {
    std::vector<Mat4> w(nodes.size());
    for (size_t n = 0; n < nodes.size(); ++n) {
      const int p = nodes[n].parent;
      w[n] = (p >= 0 && static_cast<size_t>(p) < n) ? w[static_cast<size_t>(p)] * nodes[n].local : nodes[n].local;
    }
    return w;
  }

  size_t vertexCount() const { size_t n = 0; for (auto& p : parts) n += p.mesh.pos.size(); return n; }
  size_t triangleCount() const { size_t n = 0; for (auto& p : parts) n += p.mesh.idx.size() / 3; return n; }
  // Caixa envolvente de todas as partes, em coordenadas de mundo (para enquadrar a camera / escalar).
  bool bounds(Vec3& mn, Vec3& mx) const {
    bool any = false;
    const std::vector<Mat4> world = worldMatrices();
    for (auto& p : parts) {
      const Mat4 w = nodes.empty() ? Mat4{} : world[static_cast<size_t>(p.node)];
      for (auto& lv : p.mesh.pos) {
        const Vec3 v = nodes.empty() ? lv : w.transformPoint(lv);
        if (!any) { mn = mx = v; any = true; continue; }
        mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
        mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
      }
    }
    return any;
  }
};

struct ImportResult {
  bool ok = false;
  std::string error; // preenchido quando ok == false
  ModelData model;
};

inline ImportResult failure(std::string msg, const char* diagCode = code::kImportFail) {
  debug().report(diagCode, Severity::Error, "importacao: " + msg);
  ImportResult r;
  r.error = std::move(msg);
  return r;
}

// Le o arquivo inteiro respeitando o limite de tamanho (fail-closed).
inline bool readFileBytes(const std::string& path, size_t maxBytes, std::vector<u8>& out, std::string& err) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) { err = "nao foi possivel abrir " + path; return false; }
  const std::streamoff sz = f.tellg();
  if (sz < 0 || static_cast<size_t>(sz) > maxBytes) { err = "arquivo maior que o limite de " + std::to_string(maxBytes >> 20) + " MB"; return false; }
  out.resize(static_cast<size_t>(sz));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(out.data()), sz);
  if (!f) { err = "falha ao ler " + path; return false; }
  return true;
}

// ---- utilitarios de malha ------------------------------------------------------------------------------

// Normais suaves: soma das normais de face ponderadas pela area (o produto vetorial ja carrega a area).
inline void computeSmoothNormals(Mesh& m) {
  m.normal.assign(m.pos.size(), Vec3{});
  for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
    const u32 a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
    const Vec3 n = cross(m.pos[b] - m.pos[a], m.pos[c] - m.pos[a]);
    for (u32 v : {a, b, c}) m.normal[v] = m.normal[v] + n;
  }
  for (auto& n : m.normal) { n = normalize(n); if (length(n) < 0.5f) n = {0, 1, 0}; } // vertice sem triangulos
}

// Normal "plana" por triangulo: usada quando o arquivo nao traz normais e os vertices nao sao compartilhados.
inline void computeFlatNormals(Mesh& m) {
  m.normal.assign(m.pos.size(), Vec3{0, 1, 0});
  for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
    const Vec3 n = normalize(cross(m.pos[m.idx[i + 1]] - m.pos[m.idx[i]], m.pos[m.idx[i + 2]] - m.pos[m.idx[i]]));
    for (size_t k = 0; k < 3; ++k) m.normal[m.idx[i + k]] = n;
  }
}

// Determinante da parte 3x3 (sinal < 0 = espelhamento: inverte o sentido dos triangulos).
inline f32 det3(const Mat4& a) {
  const auto& m = a.m;
  return m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) - m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) + m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
}

// "Assa" a matriz de mundo na malha: posicoes pela matriz, normais pela inversa-transposta (correta mesmo
// com escala nao uniforme) e, se a matriz espelha (det < 0), troca a ordem dos triangulos para manter
// a face frontal anti-horaria.
inline void bakeTransform(Mesh& mesh, const Mat4& w) {
  const f32 d = det3(w);
  // Matriz cofatora C = det * inversa-transposta da parte 3x3. A(r,c) = w.m[c][r] (a matriz e coluna-maior).
  // Na forma ciclica (indices (r+1)%3, (r+2)%3) o sinal alternado do cofator ja esta embutido.
  auto A = [&](int r, int c) { return w.m[c][r]; };
  f32 C[3][3];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      const int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (c + 1) % 3, c2 = (c + 2) % 3;
      C[r][c] = A(r1, c1) * A(r2, c2) - A(r1, c2) * A(r2, c1);
    }
  // Com det < 0 (espelhamento) o cofator aponta "para dentro"; multiplicar pelo sinal corrige.
  const f32 sgn = d < 0 ? -1.f : 1.f;
  for (auto& p : mesh.pos) p = w.transformPoint(p);
  for (auto& n : mesh.normal)
    n = normalize(Vec3{C[0][0] * n.x + C[0][1] * n.y + C[0][2] * n.z, C[1][0] * n.x + C[1][1] * n.y + C[1][2] * n.z, C[2][0] * n.x + C[2][1] * n.y + C[2][2] * n.z} * sgn);
  if (d < 0)
    for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) std::swap(mesh.idx[i + 1], mesh.idx[i + 2]);
}

// Um no e "TRS puro" quando a parte 3x3 tem colunas perpendiculares (sem cisalhamento) e det > 0 (sem espelho).
// Esses nos cabem em Transform (posicao/rotacao/escala) do editor e mantem o sentido dos triangulos.
inline bool isPlainTRS(const Mat4& m) {
  Vec3 c[3];
  for (int i = 0; i < 3; ++i) c[i] = {m.m[i][0], m.m[i][1], m.m[i][2]};
  for (int i = 0; i < 3; ++i) if (length(c[i]) < 1e-8f) return false;
  for (int i = 0; i < 3; ++i)
    for (int j = i + 1; j < 3; ++j)
      if (std::fabs(dot(c[i], c[j])) > 1e-4f * length(c[i]) * length(c[j])) return false;
  return det3(m) > 0.f;
}

// Garante que todo no seja TRS puro: um no fora disso e assado nas malhas dele e os filhos recebem a matriz.
inline void normalizeNodes(ModelData& md) {
  for (size_t n = 0; n < md.nodes.size(); ++n) {
    if (isPlainTRS(md.nodes[n].local)) continue;
    const Mat4 M = md.nodes[n].local;
    for (int p : md.nodes[n].parts) bakeTransform(md.parts[static_cast<size_t>(p)].mesh, M);
    for (size_t c = n + 1; c < md.nodes.size(); ++c)
      if (md.nodes[c].parent == static_cast<int>(n)) md.nodes[c].local = M * md.nodes[c].local;
    md.nodes[n].local = Mat4{};
  }
}

// Confere a malha ao final da importacao: normais/UV com o tamanho certo, indices dentro do intervalo.
// Triangulos com indice invalido sao removidos (aviso) em vez de derrubar a engine.
inline bool finalizeMesh(Mesh& m, std::vector<std::string>& warnings, const std::string& name, bool smooth = true) {
  if (m.pos.empty() || m.idx.size() < 3) return false;
  if (m.idx.size() % 3 != 0) m.idx.resize(m.idx.size() - m.idx.size() % 3);
  std::vector<u32> ok;
  ok.reserve(m.idx.size());
  size_t dropped = 0;
  for (size_t i = 0; i < m.idx.size(); i += 3) {
    if (m.idx[i] < m.pos.size() && m.idx[i + 1] < m.pos.size() && m.idx[i + 2] < m.pos.size()) ok.insert(ok.end(), m.idx.begin() + static_cast<std::ptrdiff_t>(i), m.idx.begin() + static_cast<std::ptrdiff_t>(i) + 3);
    else ++dropped;
  }
  if (dropped) warnings.push_back("'" + name + "': " + std::to_string(dropped) + " triangulo(s) com indice invalido removido(s)");
  m.idx.swap(ok);
  if (m.idx.empty()) return false;
  if (m.normal.size() != m.pos.size()) { if (smooth) computeSmoothNormals(m); else computeFlatNormals(m); }
  if (m.uv.size() != m.pos.size() * 2) m.uv.clear();
  return true;
}

} // namespace tool::assets
