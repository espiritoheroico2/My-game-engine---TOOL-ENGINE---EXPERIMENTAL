// ToolEngine - importador glTF 2.0 (.gltf com .bin/base64 e .glb binario).
//
// COMO FUNCIONA
//  1. Container: .glb = cabecalho "glTF" + chunk JSON + chunk BIN; .gltf = JSON puro (buffers por URI).
//  2. O JSON descreve: buffers -> bufferViews -> accessors (tipo/contagem/stride) -> meshes/primitives ->
//     nodes (hierarquia + transformacao) -> scenes. Lemos so o necessario para desenhar.
//  3. Cada no com malha vira 1+ ModelPart (1 por primitive). A matriz de mundo (pai * local) e assada nos
//     vertices (bakeTransform), entao o resultado independe da hierarquia do arquivo.
//  4. Material: pbrMetallicRoughness (baseColorFactor, metallicFactor, roughnessFactor). Texturas ainda nao.
//
// SEGURANCA: URIs externas so podem apontar para DENTRO da pasta do modelo (sem "..", sem caminho absoluto);
// todo acessor e conferido contra o tamanho real do buffer; limites de vertices/nos/profundidade.
#pragma once
#include "tool/assets/model.hpp"
#include "tool/core/json.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>

namespace tool::assets {

// ---- base64 (usado por buffers "data:application/octet-stream;base64,...") --------------------------------
inline bool base64Decode(const std::string& in, std::vector<u8>& out, size_t maxOut) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  out.clear();
  u32 acc = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    const int v = val(c);
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<u32>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out.size() >= maxOut) return false;
      out.push_back(static_cast<u8>((acc >> bits) & 0xFF));
    }
  }
  return true;
}

namespace gltf_detail {

struct Doc {
  tool::Json json;
  std::vector<std::vector<u8>> buffers;
  std::string baseDir;
  const ImportOptions* opt = nullptr;
  ModelData* model = nullptr;
  std::string error;
  size_t nodesVisited = 0;
};

inline size_t compSize(int ctype) {
  switch (ctype) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
  }
}
inline int typeComps(const std::string& t) {
  if (t == "SCALAR") return 1;
  if (t == "VEC2") return 2;
  if (t == "VEC3") return 3;
  if (t == "VEC4") return 4;
  if (t == "MAT4") return 16;
  return 0;
}

// Converte um componente (com ou sem normalizacao) para double.
inline double readComp(const u8* p, int ctype, bool normalized) {
  switch (ctype) {
    case 5120: { i32 v = static_cast<int8_t>(*p); return normalized ? std::max(v / 127.0, -1.0) : v; }
    case 5121: return normalized ? *p / 255.0 : *p;
    case 5122: { int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.0, -1.0) : v; }
    case 5123: { u16 v; std::memcpy(&v, p, 2); return normalized ? v / 65535.0 : v; }
    case 5125: { u32 v; std::memcpy(&v, p, 4); return v; }
    default: { f32 v; std::memcpy(&v, p, 4); return v; }
  }
}

inline bool indexOk(const tool::Json& j, size_t count, size_t& out) {
  if (!j.isNumber()) return false;
  const double d = j.asNumber(-1);
  if (d < 0 || d >= static_cast<double>(count) || d != std::floor(d)) return false;
  out = static_cast<size_t>(d);
  return true;
}

// Le um accessor inteiro como vetor de T (float ou u32), com todos os limites conferidos.
// `comps` = numero de componentes esperado (0 = qualquer); `count` recebe o numero de elementos.
template <class T>
bool readAccessor(Doc& d, size_t accIdx, int comps, std::vector<T>& out, size_t& count) {
  const tool::Json& acc = d.json["accessors"].items().at(accIdx);
  const int ctype = static_cast<int>(acc["componentType"].asNumber());
  const int nc = typeComps(acc["type"].asString());
  const size_t cs = compSize(ctype);
  if (!cs || !nc) { d.error = "accessor com tipo invalido"; return false; }
  if (comps && nc != comps) { d.error = "accessor com numero de componentes inesperado"; return false; }
  if (acc.has("sparse")) { d.error = "accessors esparsos (sparse) nao sao suportados"; return false; }
  const double cnt = acc["count"].asNumber(-1);
  if (cnt < 0 || cnt > static_cast<double>(d.opt->limits.maxVertices)) { d.error = "accessor com contagem acima do limite"; return false; }
  count = static_cast<size_t>(cnt);
  const bool norm = acc["normalized"].asBool();
  const size_t elem = cs * static_cast<size_t>(nc);
  out.assign(count * static_cast<size_t>(nc), T{});
  if (!acc.has("bufferView")) return true; // sem bufferView = zeros, pela especificacao
  size_t bvIdx;
  if (!indexOk(acc["bufferView"], d.json["bufferViews"].items().size(), bvIdx)) { d.error = "bufferView invalido"; return false; }
  const tool::Json& bv = d.json["bufferViews"].items()[bvIdx];
  size_t bufIdx;
  if (!indexOk(bv["buffer"], d.buffers.size(), bufIdx)) { d.error = "buffer invalido"; return false; }
  const std::vector<u8>& buf = d.buffers[bufIdx];
  const double bvOff = bv["byteOffset"].asNumber(0), bvLen = bv["byteLength"].asNumber(-1), accOff = acc["byteOffset"].asNumber(0);
  const double stride = bv.has("byteStride") ? bv["byteStride"].asNumber() : static_cast<double>(elem);
  if (bvOff < 0 || bvLen < 0 || accOff < 0 || stride < static_cast<double>(elem) || bvOff + bvLen > static_cast<double>(buf.size())) { d.error = "bufferView fora do buffer"; return false; }
  // O ultimo elemento termina em accOff + stride*(count-1) + elem; precisa caber na view.
  if (count > 0 && accOff + stride * static_cast<double>(count - 1) + static_cast<double>(elem) > bvLen) { d.error = "accessor ultrapassa o bufferView"; return false; }
  const u8* base = buf.data() + static_cast<size_t>(bvOff) + static_cast<size_t>(accOff);
  for (size_t i = 0; i < count; ++i) {
    const u8* e = base + static_cast<size_t>(stride) * i;
    for (int c = 0; c < nc; ++c) out[i * static_cast<size_t>(nc) + static_cast<size_t>(c)] = static_cast<T>(readComp(e + cs * static_cast<size_t>(c), ctype, norm));
  }
  return true;
}

// Matriz local do no: "matrix" (16 floats, coluna-maior como a nossa Mat4) ou T * R(quaternion) * S.
inline Mat4 nodeMatrix(const tool::Json& n) {
  Mat4 r;
  const auto& mm = n["matrix"].items();
  if (mm.size() == 16) {
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) r.m[c][row] = static_cast<f32>(mm[static_cast<size_t>(c * 4 + row)].asNumber());
    return r;
  }
  auto vec = [&](const char* k, size_t want, std::vector<f32>& o, std::vector<f32> def) {
    const auto& a = n[k].items();
    o = def;
    if (a.size() == want) for (size_t i = 0; i < want; ++i) o[i] = static_cast<f32>(a[i].asNumber());
  };
  std::vector<f32> t, q, s;
  vec("translation", 3, t, {0, 0, 0}); vec("rotation", 4, q, {0, 0, 0, 1}); vec("scale", 3, s, {1, 1, 1});
  f32 len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (len < 1e-8f) { q = {0, 0, 0, 1}; len = 1; }
  const f32 x = q[0] / len, y = q[1] / len, z = q[2] / len, w = q[3] / len;
  Mat4 R; // quaternion (x,y,z,w) -> matriz de rotacao, m[coluna][linha]
  R.m[0][0] = 1 - 2 * (y * y + z * z); R.m[0][1] = 2 * (x * y + w * z);     R.m[0][2] = 2 * (x * z - w * y);
  R.m[1][0] = 2 * (x * y - w * z);     R.m[1][1] = 1 - 2 * (x * x + z * z); R.m[1][2] = 2 * (y * z + w * x);
  R.m[2][0] = 2 * (x * z + w * y);     R.m[2][1] = 2 * (y * z - w * x);     R.m[2][2] = 1 - 2 * (x * x + y * y);
  return Mat4::translation({t[0], t[1], t[2]}) * R * Mat4::scale({s[0], s[1], s[2]});
}

inline Material materialOf(const Doc& d, const tool::Json& prim) {
  Material m; // padrao da glTF: branco, metalico 1, rugosidade 1
  m.albedo = {1, 1, 1}; m.metallic = 1.f; m.roughness = 1.f;
  size_t mi;
  if (!indexOk(prim["material"], d.json["materials"].items().size(), mi)) { m.metallic = 0.f; return m; } // sem material: fosco neutro
  const tool::Json& pbr = d.json["materials"].items()[mi]["pbrMetallicRoughness"];
  const auto& bc = pbr["baseColorFactor"].items();
  if (bc.size() >= 3) m.albedo = {static_cast<f32>(bc[0].asNumber(1)), static_cast<f32>(bc[1].asNumber(1)), static_cast<f32>(bc[2].asNumber(1))};
  m.metallic = static_cast<f32>(std::clamp(pbr["metallicFactor"].asNumber(1), 0.0, 1.0));
  m.roughness = static_cast<f32>(std::clamp(pbr["roughnessFactor"].asNumber(1), 0.04, 1.0));
  return m;
}

// Converte uma primitive em malha. Modos: 4 triangulos, 5 tira, 6 leque (0..3 pontos/linhas sao ignorados).
inline bool primitiveToPart(Doc& d, const tool::Json& prim, const Mat4& world, const std::string& name) {
  const int mode = static_cast<int>(prim["mode"].asNumber(4));
  if (mode < 4 || mode > 6) { d.model->warnings.push_back("'" + name + "': primitive com modo " + std::to_string(mode) + " (nao e triangulo) ignorada"); return true; }
  const tool::Json& attrs = prim["attributes"];
  size_t acc;
  if (!indexOk(attrs["POSITION"], d.json["accessors"].items().size(), acc)) { d.error = "primitive sem POSITION valido"; return false; }
  Mesh m;
  std::vector<f32> v;
  size_t n = 0;
  if (!readAccessor<f32>(d, acc, 3, v, n)) return false;
  if (d.model->vertexCount() + n > d.opt->limits.maxVertices) { d.error = "modelo excede o limite de vertices"; return false; }
  m.pos.resize(n);
  for (size_t i = 0; i < n; ++i) m.pos[i] = {v[3 * i], v[3 * i + 1], v[3 * i + 2]};
  if (indexOk(attrs["NORMAL"], d.json["accessors"].items().size(), acc)) {
    std::vector<f32> nv; size_t nn = 0;
    if (!readAccessor<f32>(d, acc, 3, nv, nn)) return false;
    if (nn == n) { m.normal.resize(n); for (size_t i = 0; i < n; ++i) m.normal[i] = normalize({nv[3 * i], nv[3 * i + 1], nv[3 * i + 2]}); }
  }
  if (indexOk(attrs["TEXCOORD_0"], d.json["accessors"].items().size(), acc)) {
    std::vector<f32> uv; size_t nn = 0;
    if (!readAccessor<f32>(d, acc, 2, uv, nn)) return false;
    if (nn == n) m.uv = std::move(uv);
  }
  // Indices: sem "indices" a malha e "nao indexada" (0,1,2,3,...).
  std::vector<u32> raw;
  if (indexOk(prim["indices"], d.json["accessors"].items().size(), acc)) {
    size_t c = 0;
    if (!readAccessor<u32>(d, acc, 1, raw, c)) return false;
  } else {
    raw.resize(n);
    for (size_t i = 0; i < n; ++i) raw[i] = static_cast<u32>(i);
  }
  if (mode == 4) m.idx = std::move(raw);
  else if (mode == 5) { // tira: cada vertice novo forma um triangulo; a paridade alterna o sentido
    for (size_t i = 2; i < raw.size(); ++i) {
      if (i % 2 == 0) m.idx.insert(m.idx.end(), {raw[i - 2], raw[i - 1], raw[i]});
      else m.idx.insert(m.idx.end(), {raw[i - 1], raw[i - 2], raw[i]});
    }
  } else { // leque: todos os triangulos partem do primeiro vertice
    for (size_t i = 2; i < raw.size(); ++i) m.idx.insert(m.idx.end(), {raw[0], raw[i - 1], raw[i]});
  }
  const bool hadNormals = m.normal.size() == m.pos.size();
  if (!finalizeMesh(m, d.model->warnings, name, /*smooth=*/true)) { d.model->warnings.push_back("'" + name + "': malha vazia ignorada"); return true; }
  if (!hadNormals) d.model->warnings.push_back("'" + name + "': sem normais no arquivo; calculadas automaticamente");
  bakeTransform(m, world);
  ModelPart part;
  part.name = name;
  part.mesh = std::move(m);
  part.material = materialOf(d, prim);
  d.model->parts.push_back(std::move(part));
  return true;
}

// Percorre a hierarquia acumulando a matriz de mundo. `depth` evita ciclos (um no que e filho de si mesmo).
inline bool visitNode(Doc& d, size_t idx, const Mat4& parent, size_t depth) {
  if (depth > d.opt->limits.maxDepth) { d.error = "hierarquia de nos profunda demais (ciclo?)"; return false; }
  if (++d.nodesVisited > d.opt->limits.maxNodes) { d.error = "nos demais"; return false; }
  const tool::Json& node = d.json["nodes"].items().at(idx);
  const Mat4 world = parent * nodeMatrix(node);
  size_t mi;
  if (indexOk(node["mesh"], d.json["meshes"].items().size(), mi)) {
    const tool::Json& mesh = d.json["meshes"].items()[mi];
    const std::string base = !node["name"].asString().empty() ? node["name"].asString() : !mesh["name"].asString().empty() ? mesh["name"].asString() : "mesh" + std::to_string(mi);
    const auto& prims = mesh["primitives"].items();
    for (size_t p = 0; p < prims.size(); ++p)
      if (!primitiveToPart(d, prims[p], world, prims.size() > 1 ? base + "." + std::to_string(p) : base)) return false;
  }
  for (const tool::Json& c : node["children"].items()) {
    size_t ci;
    if (!indexOk(c, d.json["nodes"].items().size(), ci)) { d.error = "filho de no invalido"; return false; }
    if (!visitNode(d, ci, world, depth + 1)) return false;
  }
  return true;
}

inline bool loadBuffers(Doc& d) {
  const auto& bufs = d.json["buffers"].items();
  for (size_t i = 0; i < bufs.size(); ++i) {
    const std::string uri = bufs[i]["uri"].asString();
    if (uri.empty()) { if (i >= d.buffers.size() || d.buffers[i].empty()) { d.error = "buffer sem uri e sem chunk BIN"; return false; } continue; } // GLB: ja carregado
    if (d.buffers.size() <= i) d.buffers.resize(i + 1);
    if (uri.rfind("data:", 0) == 0) { // dados embutidos em base64
      const size_t comma = uri.find(',');
      if (comma == std::string::npos || !base64Decode(uri.substr(comma + 1), d.buffers[i], d.opt->limits.maxFileBytes)) { d.error = "buffer base64 invalido"; return false; }
    } else { // arquivo externo: so dentro da pasta do modelo
      std::filesystem::path rel(uri);
      if (rel.is_absolute() || uri.find("..") != std::string::npos || uri.find(':') != std::string::npos) { d.error = "uri de buffer fora da pasta do modelo recusada: " + uri; return false; }
      std::string err;
      if (!readFileBytes((std::filesystem::path(d.baseDir) / rel).string(), d.opt->limits.maxFileBytes, d.buffers[i], err)) { d.error = err; return false; }
    }
    const double want = bufs[i]["byteLength"].asNumber(-1);
    if (want < 0 || static_cast<double>(d.buffers[i].size()) < want) { d.error = "buffer menor que o byteLength declarado"; return false; }
  }
  if (d.buffers.size() < bufs.size()) d.buffers.resize(bufs.size());
  return true;
}

} // namespace gltf_detail

// `bytes` = conteudo do .gltf/.glb; `baseDir` = pasta do arquivo (para .bin externos).
inline ImportResult importGltf(const std::vector<u8>& bytes, const std::string& baseDir, const ImportOptions& opt = {}) {
  using namespace gltf_detail;
  Doc d;
  d.opt = &opt;
  d.baseDir = baseDir;
  ImportResult res;
  d.model = &res.model;
  std::string jsonText;
  if (bytes.size() >= 12 && std::memcmp(bytes.data(), "glTF", 4) == 0) { // ---- GLB ----
    res.model.format = "GLB";
    auto rd32 = [&](size_t o) { u32 v; std::memcpy(&v, bytes.data() + o, 4); return v; }; // little-endian (x86/ARM)
    if (rd32(4) != 2) return failure("versao de GLB nao suportada (so 2)");
    if (rd32(8) > bytes.size()) return failure("GLB truncado");
    size_t pos = 12;
    const size_t end = std::min<size_t>(rd32(8), bytes.size());
    bool haveJson = false;
    while (pos + 8 <= end) { // chunks: [tamanho][tipo][dados] (tamanho multiplo de 4)
      const u32 len = rd32(pos), type = rd32(pos + 4);
      pos += 8;
      if (len > end - pos) return failure("chunk de GLB maior que o arquivo");
      if (type == 0x4E4F534A && !haveJson) { jsonText.assign(reinterpret_cast<const char*>(bytes.data() + pos), len); haveJson = true; }
      else if (type == 0x004E4942 && d.buffers.empty()) d.buffers.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.begin() + static_cast<std::ptrdiff_t>(pos + len));
      pos += len;
    }
    if (!haveJson) return failure("GLB sem chunk JSON");
  } else { // ---- .gltf (JSON) ----
    res.model.format = "glTF";
    jsonText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  auto j = tool::Json::parse(jsonText);
  if (!j || !j->isObject()) return failure("JSON do glTF invalido");
  d.json = std::move(*j);
  if (d.json["asset"]["version"].asString().rfind("2", 0) != 0) return failure("glTF: so a versao 2.x e suportada");
  const auto& exts = d.json["extensionsRequired"].items();
  for (const auto& e : exts)
    if (e.asString() == "KHR_draco_mesh_compression" || e.asString() == "EXT_meshopt_compression") return failure("glTF exige a extensao " + e.asString() + " (compressao de malha), ainda nao suportada");
  if (!loadBuffers(d)) return failure(d.error);

  // Cena: a "scene" padrao; senao, os nos que ninguem referencia como filho.
  std::vector<size_t> roots;
  const auto& scenes = d.json["scenes"].items();
  size_t si = 0;
  if (!scenes.empty() && (indexOk(d.json["scene"], scenes.size(), si) || (si = 0, true)))
    for (const auto& n : scenes[si]["nodes"].items()) { size_t ni; if (indexOk(n, d.json["nodes"].items().size(), ni)) roots.push_back(ni); }
  if (roots.empty()) {
    const size_t total = d.json["nodes"].items().size();
    std::vector<bool> isChild(total, false);
    for (const auto& n : d.json["nodes"].items()) for (const auto& c : n["children"].items()) { size_t ci; if (indexOk(c, total, ci)) isChild[ci] = true; }
    for (size_t i = 0; i < total; ++i) if (!isChild[i]) roots.push_back(i);
  }
  for (size_t r : roots) if (!visitNode(d, r, Mat4{}, 0)) return failure(d.error);
  if (res.model.parts.empty()) return failure("o arquivo nao tem malhas de triangulos");

  if (!d.json["animations"].items().empty()) res.model.warnings.push_back("animacoes ignoradas (ainda nao suportadas)");
  if (!d.json["skins"].items().empty()) res.model.warnings.push_back("skins/ossos ignorados");
  if (!d.json["textures"].items().empty()) res.model.warnings.push_back("texturas ignoradas (so cor/metalico/rugosidade por enquanto)");
  for (auto& w : res.model.warnings) debug().report(code::kImportWarn, Severity::Warning, "glTF: " + w);
  res.ok = true;
  return res;
}

} // namespace tool::assets
