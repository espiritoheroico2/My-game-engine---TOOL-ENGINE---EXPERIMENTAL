// ToolEngine - importador FBX binario (versoes 7000-7700, as usadas por Blender/Maya/3ds Max/Unity).
//
// COMO FUNCIONA O FORMATO
//  Arquivo = cabecalho "Kaydara FBX Binary  \0\x1a\0" + versao (u32) + arvore de nos. Cada no tem:
//    EndOffset, NumProperties, PropertyListLen (u32 ate a v7400, u64 a partir da v7500), NameLen, Name,
//    Properties (tipo de 1 char + valor; arrays 'f','d','l','i','b' podem estar comprimidos com zlib),
//    nos filhos e um registro nulo de fim.
//  A cena fica em "Objects" (Geometry, Model, Material...) e as relacoes em "Connections" (C: OO filho->pai):
//    Geometry --> Model (a malha pertence ao objeto), Material --> Model, Model --> Model (hierarquia).
//
// O QUE ESTE IMPORTADOR FAZ
//  1. Le a arvore de nos (arrays inflados com tool::assets::zlibDecompress, com limite de saida).
//  2. Para cada Model com Geometry: monta a malha, triangula poligonos em leque, expande atributos
//     "por vertice-de-poligono" (cada canto do poligono vira um vertice: sem soldar, mas sempre correto).
//  3. Transformacao local do Model = T * Rpre * R * inv(Rpost) * S (angulos em graus, ordem de Euler do
//     arquivo); mundo = pai * local; a matriz de mundo e assada nos vertices.
//  4. GlobalSettings: UnitScaleFactor (cm -> m) e UpAxis (Z-up -> Y-up) sao aplicados (opcional).
//  5. UV: FBX tem origem embaixo; invertemos V para a convencao da engine (origem em cima).
//
// LIMITES CONHECIDOS: FBX ASCII (exporte como binario), pivos/offsets de rotacao, animacao, skin, texturas,
// um unico material por objeto, poligonos concavos (a triangulacao em leque assume poligonos convexos).
// SEGURANCA: arquivo nao confiavel. Todos os tamanhos sao conferidos contra o restante do arquivo antes
// de alocar; arrays inflados tem tamanho esperado exato; profundidade e numero de nos sao limitados.
#pragma once
#include "tool/assets/inflate.hpp"
#include "tool/assets/model.hpp"

#include <cmath>
#include <cstring>
#include <map>

namespace tool::assets {

namespace fbx_detail {

struct Prop {
  char type = 0;
  i64 i = 0;
  f64 d = 0;
  std::string s;
  std::vector<f64> ad; // arrays 'f' e 'd'
  std::vector<i64> ai; // arrays 'i', 'l', 'b'
};

struct Node {
  std::string name;
  std::vector<Prop> props;
  std::vector<Node> kids;
  const Node* child(const char* n) const {
    for (auto& k : kids) if (k.name == n) return &k;
    return nullptr;
  }
};

// So guardamos arrays de nos que o importador usa (ou pequenos): curvas de animacao enormes sao puladas.
inline bool wantsArray(const std::string& n) {
  return n == "Vertices" || n == "PolygonVertexIndex" || n == "Normals" || n == "NormalsIndex" || n == "UV" || n == "UVIndex";
}

class Parser {
public:
  Parser(const std::vector<u8>& d, const ImportOptions& o) : d_(d), opt_(o) {}
  std::string error;
  size_t nodeCount = 0;
  u32 version = 0;

  bool parse(Node& root) {
    static const char kMagic[] = "Kaydara FBX Binary  ";
    if (d_.size() < 27 || std::memcmp(d_.data(), kMagic, 20) != 0) { error = "cabecalho FBX binario invalido"; return false; }
    std::memcpy(&version, d_.data() + 23, 4);
    if (version < 7000 || version > 7700) { error = "versao de FBX nao suportada: " + std::to_string(version) + " (suportadas: 7000-7700)"; return false; }
    size_t pos = 27;
    // Os nos de topo (FBXHeaderExtension, GlobalSettings, Objects, Connections...) sao filhos de uma raiz virtual.
    while (pos < d_.size()) {
      Node n;
      const int st = parseNode(pos, n, 0);
      if (st < 0) return false;
      if (st == 1) break; // registro nulo = fim da lista de nos de topo
      root.kids.push_back(std::move(n));
    }
    return true;
  }

private:
  const std::vector<u8>& d_;
  const ImportOptions& opt_;

  bool have(size_t pos, u64 n) const { return pos <= d_.size() && n <= d_.size() - pos; }
  u64 rdU(size_t pos, int bytes) const { u64 v = 0; std::memcpy(&v, d_.data() + pos, static_cast<size_t>(bytes)); return v; } // little-endian

  // Retorna 0 = no lido, 1 = registro nulo, -1 = erro.
  int parseNode(size_t& pos, Node& node, size_t depth) {
    if (depth > opt_.limits.maxDepth) { error = "arvore FBX profunda demais"; return -1; }
    const bool wide = version >= 7500; // a partir da 7500 os campos de cabecalho sao de 64 bits
    const size_t fw = wide ? 8 : 4, hs = 3 * fw + 1;
    if (!have(pos, hs)) { error = "FBX truncado (cabecalho de no)"; return -1; }
    const u64 endOff = rdU(pos, static_cast<int>(fw)), numProps = rdU(pos + fw, static_cast<int>(fw)), propLen = rdU(pos + 2 * fw, static_cast<int>(fw));
    const size_t nameLen = d_[pos + 3 * fw];
    if (endOff == 0 && numProps == 0 && propLen == 0 && nameLen == 0) { pos += hs; return 1; }
    if (++nodeCount > opt_.limits.maxNodes * 10) { error = "FBX com nos demais"; return -1; }
    if (endOff > d_.size() || endOff < pos + hs + nameLen || propLen > endOff - (pos + hs + nameLen)) { error = "FBX: tamanhos de no inconsistentes"; return -1; }
    node.name.assign(reinterpret_cast<const char*>(d_.data() + pos + hs), nameLen);
    pos += hs + nameLen;
    const size_t propsEnd = pos + static_cast<size_t>(propLen);
    if (numProps > propLen) { error = "FBX: numero de propriedades inconsistente"; return -1; } // cada propriedade ocupa >= 1 byte
    const bool keep = wantsArray(node.name);
    node.props.reserve(static_cast<size_t>(numProps));
    for (u64 k = 0; k < numProps; ++k) {
      Prop p;
      if (!parseProp(pos, propsEnd, p, keep)) return -1;
      node.props.push_back(std::move(p));
    }
    if (pos != propsEnd) { error = "FBX: lista de propriedades com tamanho errado"; return -1; }
    while (pos < endOff) { // filhos ate EndOffset
      Node c;
      const int st = parseNode(pos, c, depth + 1);
      if (st < 0) return -1;
      if (st == 0) node.kids.push_back(std::move(c));
      if (pos > endOff) { error = "FBX: no filho ultrapassa o no pai"; return -1; }
    }
    pos = static_cast<size_t>(endOff);
    return 0;
  }

  bool parseProp(size_t& pos, size_t limit, Prop& p, bool keepArrays) {
    if (pos >= limit) { error = "FBX: propriedade fora da lista"; return false; }
    p.type = static_cast<char>(d_[pos++]);
    auto need = [&](u64 n) { return pos <= limit && n <= limit - pos; };
    switch (p.type) {
      case 'Y': if (!need(2)) break; { int16_t v; std::memcpy(&v, &d_[pos], 2); p.i = v; p.d = v; } pos += 2; return true;
      case 'C': if (!need(1)) break; p.i = d_[pos]; p.d = static_cast<f64>(p.i); pos += 1; return true;
      case 'I': if (!need(4)) break; { int32_t v; std::memcpy(&v, &d_[pos], 4); p.i = v; p.d = v; } pos += 4; return true;
      case 'F': if (!need(4)) break; { f32 v; std::memcpy(&v, &d_[pos], 4); p.d = v; p.i = static_cast<i64>(v); } pos += 4; return true;
      case 'D': if (!need(8)) break; std::memcpy(&p.d, &d_[pos], 8); p.i = static_cast<i64>(p.d); pos += 8; return true;
      case 'L': if (!need(8)) break; { int64_t v; std::memcpy(&v, &d_[pos], 8); p.i = v; p.d = static_cast<f64>(v); } pos += 8; return true;
      case 'S': case 'R': {
        if (!need(4)) break;
        const u32 len = static_cast<u32>(rdU(pos, 4));
        pos += 4;
        if (!need(len)) break;
        p.s.assign(reinterpret_cast<const char*>(&d_[pos]), len);
        pos += len;
        return true;
      }
      case 'f': case 'd': case 'l': case 'i': case 'b': {
        if (!need(12)) break;
        const u64 count = rdU(pos, 4), enc = rdU(pos + 4, 4), clen = rdU(pos + 8, 4);
        pos += 12;
        const size_t es = (p.type == 'd' || p.type == 'l') ? 8 : (p.type == 'b' ? 1 : 4);
        const u64 raw = count * es; // count < 2^32 e es <= 8: nao estoura u64
        if (count > opt_.limits.maxVertices * 8ull) { error = "FBX: array grande demais"; return false; }
        const u64 stored = enc == 0 ? raw : clen;
        if ((enc != 0 && enc != 1) || !need(stored)) break;
        if (keepArrays || count <= 16) {
          const u8* src = &d_[pos];
          std::vector<u8> inflated;
          if (enc == 1) {
            std::string err;
            if (!zlibDecompress(src, static_cast<size_t>(clen), inflated, static_cast<size_t>(raw), &err) || inflated.size() != raw) { error = "FBX: array comprimido invalido (" + err + ")"; return false; }
            src = inflated.data();
          }
          for (u64 k = 0; k < count; ++k) {
            const u8* e = src + k * es;
            switch (p.type) {
              case 'f': { f32 v; std::memcpy(&v, e, 4); p.ad.push_back(v); break; }
              case 'd': { f64 v; std::memcpy(&v, e, 8); p.ad.push_back(v); break; }
              case 'i': { int32_t v; std::memcpy(&v, e, 4); p.ai.push_back(v); break; }
              case 'l': { int64_t v; std::memcpy(&v, e, 8); p.ai.push_back(v); break; }
              default: p.ai.push_back(*e); break;
            }
          }
        }
        pos += static_cast<size_t>(stored);
        return true;
      }
      default: error = std::string("FBX: tipo de propriedade desconhecido '") + p.type + "'"; return false;
    }
    if (error.empty()) error = "FBX truncado (propriedade)";
    return false;
  }
};

// "Cubo\0\x01Model" -> "Cubo"
inline std::string cleanName(const std::string& s) { return s.substr(0, s.find('\0')); }

// Valor numerico de uma propriedade "P" de Properties70: P: "Nome", "Tipo", "Label", "Flags", v1, v2, ...
inline const Node* findP(const Node* props70, const char* name) {
  if (!props70) return nullptr;
  for (auto& k : props70->kids)
    if (k.name == "P" && !k.props.empty() && k.props[0].s == name) return &k;
  return nullptr;
}
inline const Node* props70Of(const Node& n) { return n.child("Properties70"); }
inline Vec3 vec3P(const Node* props70, const char* name, Vec3 def) {
  const Node* p = findP(props70, name);
  if (!p || p->props.size() < 7) return def;
  return {static_cast<f32>(p->props[4].d), static_cast<f32>(p->props[5].d), static_cast<f32>(p->props[6].d)};
}
inline f64 numP(const Node* props70, const char* name, f64 def) {
  const Node* p = findP(props70, name);
  return (p && p->props.size() > 4) ? p->props[4].d : def;
}

constexpr f32 kDeg2Rad = 3.14159265f / 180.f;

// Rotacao de Euler do FBX. Para a ordem listada [a,b,c] o FBX aplica a, depois b, depois c: R = Rc * Rb * Ra.
inline Mat4 eulerMatrix(Vec3 deg, int order) {
  static const int perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 2, 0}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}}; // XYZ,XZY,YZX,YXZ,ZXY,ZYX
  const f32 ang[3] = {deg.x * kDeg2Rad, deg.y * kDeg2Rad, deg.z * kDeg2Rad};
  auto axis = [&](int a) { return a == 0 ? Mat4::rotationX(ang[0]) : a == 1 ? Mat4::rotationY(ang[1]) : Mat4::rotationZ(ang[2]); };
  const int* p = perm[order >= 0 && order < 6 ? order : 0];
  return axis(p[2]) * axis(p[1]) * axis(p[0]);
}
inline Mat4 transposeRot(const Mat4& r) { Mat4 t; for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) t.m[c][row] = r.m[row][c]; return t; } // inversa de uma rotacao

struct Model {
  i64 id = 0, parent = 0;
  std::string name;
  Mat4 local;
  std::vector<i64> geoms;
  i64 material = 0;
  int materialCount = 0;
};

} // namespace fbx_detail

inline ImportResult importFbx(const std::vector<u8>& bytes, const ImportOptions& opt = {}) {
  using namespace fbx_detail;
  if (bytes.size() >= 5 && std::memcmp(bytes.data(), "; FBX", 5) == 0)
    return failure("FBX em formato ASCII nao e suportado; exporte como FBX binario");
  Node root;
  Parser parser(bytes, opt);
  if (!parser.parse(root)) return failure(parser.error);
  ImportResult res;
  res.model.format = "FBX";
  ModelData& md = res.model;

  // ---- GlobalSettings: escala da unidade e eixo "up" ----
  Mat4 sceneRoot;
  if (const Node* gs = root.child("GlobalSettings")) {
    const Node* p70 = props70Of(*gs);
    if (opt.applyUnitScale && findP(p70, "UnitScaleFactor")) {
      const f32 f = static_cast<f32>(numP(p70, "UnitScaleFactor", 1.0)) / 100.f; // 1.0 = centimetro; queremos metros
      if (f > 0.f) sceneRoot = Mat4::scale({f, f, f}) * sceneRoot;
    }
    if (opt.applyUpAxis) {
      const int up = static_cast<int>(numP(p70, "UpAxis", 1)), sign = static_cast<int>(numP(p70, "UpAxisSign", 1));
      Mat4 r;
      if (up == 2) r = Mat4::rotationX((sign >= 0 ? -90.f : 90.f) * kDeg2Rad);      // Z-up -> Y-up: (x,y,z) -> (x,z,-y)
      else if (up == 0) r = Mat4::rotationZ((sign >= 0 ? 90.f : -90.f) * kDeg2Rad); // X-up -> Y-up
      else if (sign < 0) r = Mat4::rotationX(180.f * kDeg2Rad);
      sceneRoot = sceneRoot * r;
    }
  }

  // ---- Objects ----
  const Node* objects = root.child("Objects");
  const Node* conns = root.child("Connections");
  if (!objects) return failure("FBX sem no 'Objects'");
  std::map<i64, const Node*> geoNodes, matNodes;
  std::map<i64, Model> models;
  for (const Node& o : objects->kids) {
    if (o.props.empty()) continue;
    const i64 id = o.props[0].i;
    if (o.name == "Geometry" && o.props.size() >= 3 && o.props[2].s == "Mesh") geoNodes[id] = &o;
    else if (o.name == "Material") matNodes[id] = &o;
    else if (o.name == "Model") {
      if (models.size() >= opt.limits.maxNodes) return failure("FBX com modelos demais", code::kImportLimit);
      Model m;
      m.id = id;
      m.name = o.props.size() > 1 ? cleanName(o.props[1].s) : "model";
      const Node* p70 = props70Of(o);
      const int order = static_cast<int>(numP(p70, "RotationOrder", 0));
      // Transformacao local completa do FBX (sem pivos/offsets): T * Rpre * R * inv(Rpost) * S
      const Mat4 T = Mat4::translation(vec3P(p70, "Lcl Translation", {0, 0, 0}));
      const Mat4 S = Mat4::scale(vec3P(p70, "Lcl Scaling", {1, 1, 1}));
      const Mat4 R = eulerMatrix(vec3P(p70, "Lcl Rotation", {0, 0, 0}), order);
      const Mat4 Rpre = eulerMatrix(vec3P(p70, "PreRotation", {0, 0, 0}), 0);
      const Mat4 RpostInv = transposeRot(eulerMatrix(vec3P(p70, "PostRotation", {0, 0, 0}), 0));
      m.local = T * Rpre * R * RpostInv * S;
      models[id] = std::move(m);
    }
  }
  // ---- Connections: filho -> pai (OO = objeto-objeto) ----
  if (conns)
    for (const Node& c : conns->kids) {
      if (c.name != "C" || c.props.size() < 3 || c.props[0].s != "OO") continue;
      const i64 child = c.props[1].i, parent = c.props[2].i;
      auto pm = models.find(parent);
      if (geoNodes.count(child) && pm != models.end()) pm->second.geoms.push_back(child);
      else if (matNodes.count(child) && pm != models.end()) { if (pm->second.materialCount++ == 0) pm->second.material = child; }
      else if (models.count(child)) models[child].parent = parent; // pai 0 = raiz da cena
    }

  // Matriz de mundo de um modelo = pai * local, subindo ate a raiz (limite de profundidade contra ciclos).
  auto worldOf = [&](const Model& m, bool& ok) {
    Mat4 w = m.local;
    const Model* cur = &m;
    for (size_t depth = 0; cur->parent != 0; ++depth) {
      auto it = models.find(cur->parent);
      if (it == models.end()) break;
      if (depth > opt.limits.maxDepth) { ok = false; return w; }
      cur = &it->second;
      w = cur->local * w;
    }
    return w;
  };

  for (auto& [id, m] : models) {
    if (m.geoms.empty()) continue;
    bool ok = true;
    const Mat4 world = sceneRoot * worldOf(m, ok);
    if (!ok) return failure("FBX: hierarquia de modelos com ciclo");
    Material mat;
    mat.albedo = {0.8f, 0.8f, 0.8f}; mat.metallic = 0.f; mat.roughness = 0.6f;
    if (m.material != 0) {
      const Node* p70 = props70Of(*matNodes[m.material]);
      const Vec3 dc = vec3P(p70, "DiffuseColor", vec3P(p70, "Diffuse", {0.8f, 0.8f, 0.8f}));
      const f32 df = static_cast<f32>(numP(p70, "DiffuseFactor", 1.0));
      mat.albedo = dc * df;
    }
    if (m.materialCount > 1) md.warnings.push_back("'" + m.name + "': varios materiais por objeto nao suportados; usando o primeiro");
    for (i64 gid : m.geoms) {
      const Node& g = *geoNodes[gid];
      const Node *vn = g.child("Vertices"), *pn = g.child("PolygonVertexIndex");
      if (!vn || !pn || vn->props.empty() || pn->props.empty()) { md.warnings.push_back("'" + m.name + "': geometria sem vertices/poligonos"); continue; }
      const std::vector<f64>& V = vn->props[0].ad;
      const std::vector<i64>& P = pn->props[0].ai;
      const size_t nCtrl = V.size() / 3;
      if (md.vertexCount() + P.size() > opt.limits.maxVertices) return failure("modelo excede o limite de vertices", code::kImportLimit);

      // Camada de atributo (normais/UV): como mapear "canto do poligono" -> valor.
      struct Layer { std::string map, ref; const std::vector<f64>* data = nullptr; const std::vector<i64>* index = nullptr; };
      auto layer = [&](const char* nodeName, const char* dataName, const char* idxName) {
        Layer L;
        const Node* ln = g.child(nodeName);
        if (!ln) return L;
        if (auto* mp = ln->child("MappingInformationType"); mp && !mp->props.empty()) L.map = mp->props[0].s;
        if (auto* rf = ln->child("ReferenceInformationType"); rf && !rf->props.empty()) L.ref = rf->props[0].s;
        if (auto* dn = ln->child(dataName); dn && !dn->props.empty()) L.data = &dn->props[0].ad;
        if (auto* in = ln->child(idxName); in && !in->props.empty()) L.index = &in->props[0].ai;
        return L;
      };
      const Layer ln = layer("LayerElementNormal", "Normals", "NormalsIndex"), lu = layer("LayerElementUV", "UV", "UVIndex");
      // Indice de dado para (canto p, ponto de controle c, poligono q), conforme o mapeamento declarado.
      auto slot = [&](const Layer& L, size_t p, size_t c, size_t q, size_t& out) {
        if (!L.data) return false;
        size_t s = L.map == "ByPolygonVertex" ? p : (L.map == "ByVertice" || L.map == "ByVertex" || L.map == "ByControlPoint") ? c : L.map == "ByPolygon" ? q : L.map == "AllSame" ? 0 : static_cast<size_t>(-1);
        if (s == static_cast<size_t>(-1)) return false;
        if (L.ref == "IndexToDirect" || L.ref == "Index") {
          if (!L.index || s >= L.index->size() || (*L.index)[s] < 0) return false;
          s = static_cast<size_t>((*L.index)[s]);
        }
        out = s;
        return true;
      };

      Mesh mesh;
      const bool hasN = ln.data != nullptr, hasUv = lu.data != nullptr;
      mesh.pos.reserve(P.size());
      std::vector<u32> poly; // cantos do poligono corrente (indices de vertice expandido)
      bool polyOk = true;
      size_t q = 0, droppedPolys = 0;
      for (size_t p = 0; p < P.size(); ++p) {
        // Fim de poligono = indice negativo (o valor real e ~v = -v-1).
        const bool endOfPoly = P[p] < 0;
        const i64 ctrl64 = endOfPoly ? ~P[p] : P[p];
        const bool valid = ctrl64 >= 0 && static_cast<size_t>(ctrl64) < nCtrl;
        const size_t c = valid ? static_cast<size_t>(ctrl64) : 0;
        if (!valid) polyOk = false;
        // Um vertice "expandido" por canto, na ordem de PolygonVertexIndex (ByPolygonVertex mapeia direto).
        mesh.pos.push_back(valid ? Vec3{static_cast<f32>(V[3 * c]), static_cast<f32>(V[3 * c + 1]), static_cast<f32>(V[3 * c + 2])} : Vec3{});
        size_t s = 0;
        if (hasN) {
          Vec3 n{0, 1, 0};
          if (slot(ln, p, c, q, s) && 3 * s + 2 < ln.data->size()) n = {static_cast<f32>((*ln.data)[3 * s]), static_cast<f32>((*ln.data)[3 * s + 1]), static_cast<f32>((*ln.data)[3 * s + 2])};
          mesh.normal.push_back(normalize(n));
        }
        if (hasUv) {
          f32 u = 0, v = 0;
          if (slot(lu, p, c, q, s) && 2 * s + 1 < lu.data->size()) { u = static_cast<f32>((*lu.data)[2 * s]); v = 1.f - static_cast<f32>((*lu.data)[2 * s + 1]); } // V invertido
          mesh.uv.push_back(u); mesh.uv.push_back(v);
        }
        poly.push_back(static_cast<u32>(p));
        if (endOfPoly) {
          if (polyOk && poly.size() >= 3)
            for (size_t k = 1; k + 1 < poly.size(); ++k) mesh.idx.insert(mesh.idx.end(), {poly[0], poly[k], poly[k + 1]}); // leque
          else ++droppedPolys;
          poly.clear();
          polyOk = true;
          ++q;
        }
      }
      if (droppedPolys) md.warnings.push_back("'" + m.name + "': " + std::to_string(droppedPolys) + " poligono(s) invalido(s) ignorado(s)");
      if (!hasN) md.warnings.push_back("'" + m.name + "': sem normais no arquivo; calculadas por face");
      if (!finalizeMesh(mesh, md.warnings, m.name, /*smooth=*/false)) { md.warnings.push_back("'" + m.name + "': malha vazia ignorada"); continue; }
      bakeTransform(mesh, world);
      ModelPart part;
      part.name = m.name;
      part.mesh = std::move(mesh);
      part.material = mat;
      md.parts.push_back(std::move(part));
    }
  }
  if (md.parts.empty()) return failure("o arquivo FBX nao tem malhas");
  if (root.child("Takes") || objects->child("AnimationStack")) md.warnings.push_back("animacoes ignoradas (ainda nao suportadas)");
  if (objects->child("Texture")) md.warnings.push_back("texturas ignoradas (so cor difusa por enquanto)");
  for (auto& w : md.warnings) debug().report(code::kImportWarn, Severity::Warning, "FBX: " + w);
  res.ok = true;
  return res;
}

} // namespace tool::assets
