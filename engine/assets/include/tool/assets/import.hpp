// ToolEngine - ponto de entrada unico para importar modelos: detecta o formato pelo CONTEUDO (magic bytes),
// nao so pela extensao, e delega para o parser certo.
//   importModelFile("caixa.glb") -> ImportResult
#pragma once
#include "tool/assets/fbx.hpp"
#include "tool/assets/gltf.hpp"

#include <algorithm>
#include <filesystem>

namespace tool::assets {

// Extensoes que o editor aceita (usado no arrastar-e-soltar).
inline bool isModelExtension(const std::string& path) {
  std::string e = std::filesystem::path(path).extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e == ".glb" || e == ".gltf" || e == ".fbx";
}

inline ImportResult importModelMemory(const std::vector<u8>& bytes, const std::string& baseDir, const ImportOptions& opt = {}) {
  if (bytes.size() >= 4 && std::memcmp(bytes.data(), "glTF", 4) == 0) return importGltf(bytes, baseDir, opt);
  if (bytes.size() >= 20 && std::memcmp(bytes.data(), "Kaydara FBX Binary  ", 20) == 0) return importFbx(bytes, opt);
  if (bytes.size() >= 5 && std::memcmp(bytes.data(), "; FBX", 5) == 0) return importFbx(bytes, opt); // devolve a mensagem de "ASCII nao suportado"
  for (u8 c : bytes) { // .gltf e JSON: o primeiro caractere nao-branco deve ser '{'
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (c == '{') return importGltf(bytes, baseDir, opt);
    break;
  }
  return failure("formato de modelo nao reconhecido (suportados: .glb, .gltf, .fbx binario)");
}

inline ImportResult importModelFile(const std::string& path, const ImportOptions& opt = {}) {
  std::vector<u8> bytes;
  std::string err;
  if (!readFileBytes(path, opt.limits.maxFileBytes, bytes, err)) return failure(err);
  ImportResult r = importModelMemory(bytes, std::filesystem::path(path).parent_path().string(), opt);
  if (r.ok) logInfo("modelo importado: " + path + " [" + r.model.format + "] " + std::to_string(r.model.parts.size()) + " parte(s), " + std::to_string(r.model.triangleCount()) + " triangulos");
  return r;
}

} // namespace tool::assets
