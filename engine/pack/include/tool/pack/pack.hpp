// ToolEngine - empacotador/exportador: formato .tpak, orcamento de tamanho e relatorio.
//   [header][tabela de entradas][dados]  - ordem deterministica (build reproduzivel), SHA-256 por arquivo
//   e SHA-256 do conjunto. O leitor valida limites e hashes antes de entregar qualquer byte (fail-closed).
// Compressao (zstd) e assinatura (Ed25519) entram quando as bibliotecas forem adicionadas ao projeto.
#pragma once
#include "tool/core/sha256.hpp"
#include "tool/debug/debug.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tool::pack {

namespace code {
inline constexpr const char* kBadPack = "TOOL-E230";
inline constexpr const char* kBudget = "TOOL-E231";
inline constexpr const char* kHashMismatch = "TOOL-E232";
} // namespace code

constexpr u32 kMagic = 0x4B415054; // "TPAK"
constexpr u32 kVersion = 1;
constexpr size_t kMaxEntries = 1u << 20;
constexpr size_t kMaxPath = 256;

struct Entry { std::string path; u64 offset = 0, size = 0; Sha256Digest hash{}; };

class Builder {
public:
  // Caminhos normalizados e unicos; "../" e caminhos absolutos sao proibidos (path traversal).
  bool add(const std::string& path, std::vector<u8> data) {
    if (path.empty() || path.size() > kMaxPath || path[0] == '/' || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
      debug().report(code::kBadPack, Severity::Error, "caminho invalido no pacote: " + path); return false;
    }
    if (files_.count(path)) { debug().report(code::kBadPack, Severity::Error, "caminho duplicado: " + path); return false; }
    files_[path] = std::move(data);
    return true;
  }

  struct Report { size_t files = 0; u64 totalBytes = 0; std::vector<std::pair<std::string, u64>> largest; bool overBudget = false; };

  // Gera o pacote. Se `budgetBytes` > 0 e for excedido, falha e o relatorio aponta os maiores arquivos.
  bool build(std::vector<u8>& out, Report& rep, u64 budgetBytes = 0) const {
    rep = {};
    std::vector<std::pair<std::string, u64>> sizes;
    for (auto& [p, d] : files_) { rep.totalBytes += d.size(); sizes.push_back({p, d.size()}); }
    rep.files = files_.size();
    std::sort(sizes.begin(), sizes.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    sizes.resize(std::min<size_t>(sizes.size(), 5));
    rep.largest = sizes;
    if (budgetBytes && rep.totalBytes > budgetBytes) {
      rep.overBudget = true;
      debug().report(code::kBudget, Severity::Error, "pacote de " + std::to_string(rep.totalBytes) + " bytes excede o orcamento de " + std::to_string(budgetBytes) +
                                                    "; maior: " + (sizes.empty() ? "-" : sizes[0].first));
      return false;
    }
    std::vector<u8> table;
    auto w32 = [](std::vector<u8>& v, u32 x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>(x >> (8 * i))); };
    auto w64 = [&](std::vector<u8>& v, u64 x) { w32(v, static_cast<u32>(x)); w32(v, static_cast<u32>(x >> 32)); };
    u64 offset = 0;
    std::vector<const std::vector<u8>*> order;
    for (auto& [p, d] : files_) { // std::map: ordem alfabetica => deterministico
      w32(table, static_cast<u32>(p.size())); table.insert(table.end(), p.begin(), p.end());
      w64(table, offset); w64(table, d.size());
      const auto h = sha256(d.data(), d.size()); table.insert(table.end(), h.begin(), h.end());
      offset += d.size(); order.push_back(&d);
    }
    out.clear();
    w32(out, kMagic); w32(out, kVersion); w32(out, static_cast<u32>(files_.size())); w64(out, table.size());
    const auto th = sha256(table.data(), table.size());
    out.insert(out.end(), th.begin(), th.end()); // hash da tabela (cobre todos os hashes de arquivo)
    out.insert(out.end(), table.begin(), table.end());
    for (auto* d : order) out.insert(out.end(), d->begin(), d->end());
    return true;
  }

private:
  std::map<std::string, std::vector<u8>> files_;
};

class Reader {
public:
  // Valida cabecalho, hash da tabela, limites de cada entrada e sobreposicao. Retorna nullopt se algo estiver errado.
  static std::optional<Reader> open(const u8* data, size_t size) {
    Reader r; r.d_ = data; r.n_ = size;
    constexpr size_t kHeader = 4 + 4 + 4 + 8 + 32;
    if (size < kHeader) return fail("pacote menor que o cabecalho");
    size_t p = 0;
    auto u32_ = [&]() { u32 v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<u32>(data[p + static_cast<size_t>(i)]) << (8 * i); p += 4; return v; };
    auto u64_ = [&]() { u64 lo = u32_(), hi = u32_(); return lo | (hi << 32); };
    if (u32_() != kMagic) return fail("magic invalido");
    if (u32_() != kVersion) return fail("versao nao suportada");
    const u32 count = u32_(); const u64 tableSize = u64_();
    if (count > kMaxEntries || tableSize > size - kHeader) return fail("tabela fora dos limites");
    Sha256Digest th; std::memcpy(th.data(), data + p, 32); p += 32;
    const u8* tab = data + p;
    if (sha256(tab, static_cast<size_t>(tableSize)) != th) { debug().report(code::kHashMismatch, Severity::Error, "hash da tabela nao confere"); return std::nullopt; }
    r.dataStart_ = kHeader + static_cast<size_t>(tableSize);
    const u64 dataSize = size - r.dataStart_;
    size_t q = 0; const size_t tn = static_cast<size_t>(tableSize);
    u64 expectOffset = 0;
    for (u32 i = 0; i < count; ++i) {
      if (tn - q < 4) return fail("entrada truncada");
      u32 pl = 0; for (int k = 0; k < 4; ++k) pl |= static_cast<u32>(tab[q + static_cast<size_t>(k)]) << (8 * k); q += 4;
      if (pl == 0 || pl > kMaxPath || tn - q < static_cast<size_t>(pl) + 16 + 32) return fail("entrada invalida");
      Entry e; e.path.assign(reinterpret_cast<const char*>(tab + q), pl); q += pl;
      auto rd64 = [&]() { u64 v = 0; for (int k = 0; k < 8; ++k) v |= static_cast<u64>(tab[q + static_cast<size_t>(k)]) << (8 * k); q += 8; return v; };
      e.offset = rd64(); e.size = rd64();
      std::memcpy(e.hash.data(), tab + q, 32); q += 32;
      if (e.offset != expectOffset || e.size > dataSize || e.offset > dataSize - e.size) return fail("offset/tamanho fora dos limites");
      expectOffset += e.size;
      if (r.index_.count(e.path)) return fail("caminho duplicado");
      r.index_[e.path] = r.entries_.size(); r.entries_.push_back(e);
    }
    if (q != tn || expectOffset != dataSize) return fail("tamanho da tabela/dados inconsistente");
    return r;
  }

  const std::vector<Entry>& entries() const { return entries_; }
  bool has(const std::string& path) const { return index_.count(path); }

  // Le um arquivo conferindo o SHA-256; devolve nullopt se estiver adulterado.
  std::optional<std::vector<u8>> read(const std::string& path) const {
    auto it = index_.find(path);
    if (it == index_.end()) return std::nullopt;
    const Entry& e = entries_[it->second];
    const u8* p = d_ + dataStart_ + e.offset;
    if (sha256(p, static_cast<size_t>(e.size)) != e.hash) { debug().report(code::kHashMismatch, Severity::Error, "arquivo adulterado: " + path); return std::nullopt; }
    return std::vector<u8>(p, p + e.size);
  }

private:
  static std::nullopt_t fail(const char* m) { debug().report(code::kBadPack, Severity::Error, std::string("pacote invalido: ") + m); return std::nullopt; }
  const u8* d_ = nullptr; size_t n_ = 0, dataStart_ = 0;
  std::vector<Entry> entries_;
  std::map<std::string, size_t> index_;
};

} // namespace tool::pack
