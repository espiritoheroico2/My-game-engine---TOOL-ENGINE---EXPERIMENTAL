// ToolEngine Editor - resolucao segura de caminhos relativos dentro da pasta do projeto (F-12).
//
// Header SEM dependencias do editor (sem ImGui, render ou audio), para que os testes possam
// valida-lo diretamente. Project::resolve() em project.hpp delega para resolveInside().
//
// Defesa em duas camadas:
//   1. lexica: rejeita vazio, caminho absoluto, raiz de unidade e ".." (como antes);
//   2. em disco: canonicaliza a raiz e percorre o caminho componente a componente. Cada link
//      simbolico encontrado (inclusive pendente, cujo alvo nao existe) tem o alvo lido e
//      canonicalizado; o destino final precisa ficar dentro da raiz canonicalizada.
//
// Limitacao declarada: TOCTOU. A verificacao e o uso do caminho sao momentos diferentes; um
// processo externo pode trocar um diretorio por um link entre os dois. Para ameaca concorrente,
// seria preciso abrir com APIs que nao seguem links (openat com O_NOFOLLOW, por exemplo).
// Em Windows, junctions e diferencas de maiusculas/minusculas nao sao tratadas aqui.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace editor::projpath {

namespace fs = std::filesystem;

// Checagem lexica, sem tocar no disco. Devolve o caminho normalizado e relativo, ou vazio se for rejeitado.
inline fs::path lexicalRelative(const std::string& rel) {
  if (rel.empty()) return {};
  const fs::path p = fs::path(rel).lexically_normal();
  if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) return {};
  for (const auto& part : p) {
    if (part == "..") return {};
  }
  return p;
}

// Verdadeiro se `root` e prefixo de `candidate`, comparando componente a componente.
// Ambos devem ser canonicos (absolutos, sem links) para o resultado ter sentido.
inline bool isWithin(const fs::path& root, const fs::path& candidate) {
  auto r = root.begin();
  auto c = candidate.begin();
  for (; r != root.end() && c != candidate.end(); ++r, ++c) {
    if (*r != *c) return false;
  }
  return r == root.end();
}

// Percorre `rel` (ja lexicamente normalizado e sem "..") a partir da raiz canonica, resolvendo links.
// Devolve o caminho real final, ou nullopt se algum link sair da raiz ou ocorrer erro de sistema.
inline std::optional<fs::path> walkInside(const fs::path& canonRoot, const fs::path& lexRel) {
  fs::path cur = canonRoot;
  std::error_code ec;
  for (const auto& part : lexRel) {
    if (part == ".") continue;
    cur /= part;
    // Componente inexistente nao e erro aqui (arquivo novo a ser criado): so falha de verdade interrompe.
    const fs::file_status st = fs::symlink_status(cur, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return std::nullopt;
    if (!fs::is_symlink(st)) continue;

    // Link simbolico (existente ou pendente): le o alvo, torna-o absoluto em relacao ao diretorio do link
    // e canonicaliza. weakly_canonical aceita alvo inexistente, o que cobre links pendentes.
    fs::path target = fs::read_symlink(cur, ec);
    if (ec) return std::nullopt;
    if (target.is_relative()) target = cur.parent_path() / target;
    target = fs::weakly_canonical(target, ec);
    if (ec || !isWithin(canonRoot, target)) return std::nullopt;
    cur = target; // as proximas partes sao anexadas a partir do alvo real
  }
  return cur;
}

// Resolve `rel` dentro de `root`. Devolve root / rel (forma lexica, como antes, para manter as
// comparacoes de caminho do editor), ou vazio se o destino real escapar de `root`.
inline fs::path resolveInside(const fs::path& root, const std::string& rel) {
  const fs::path lex = lexicalRelative(rel);
  if (lex.empty()) return {};

  std::error_code ec;
  // weakly_canonical aceita raiz que ainda nao existe (ex.: Project/ antes de ensure()).
  const fs::path canonRoot = fs::weakly_canonical(root, ec);
  if (ec) return {};

  const auto real = walkInside(canonRoot, lex);
  if (!real) return {};

  // Checagem final e redundante de proposito: o destino canonico precisa estar dentro da raiz.
  const fs::path finalReal = fs::weakly_canonical(*real, ec);
  if (ec || !isWithin(canonRoot, finalReal)) return {};

  return root / lex;
}

} // namespace editor::projpath
