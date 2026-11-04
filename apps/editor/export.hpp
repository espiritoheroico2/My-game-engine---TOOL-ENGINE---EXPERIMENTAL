// ToolEngine Editor - EXPORTAR o jogo para uma pasta pronta para rodar (Windows x64 e Linux x64).
//
// O que sai na pasta <Produto>-win64 (ou -linux64):
//   <Produto>.exe (ou <Produto>)   copia do proprio editor; com --player abre o jogo sem a interface de edicao
//   *.dll                          bibliotecas que estiverem ao lado do editor (SDL3 etc.)
//   assets/                        shaders, idiomas e temas que a engine carrega do disco
//   Data/Project/                  cenas, objetos, scripts e mapa de botoes em texto (JSON): pode ser editado
//   export.json                    dados do "Editor info" + tamanho do pacote (e o marcador desta pasta)
//   Play.bat / play.sh             atalho para abrir o jogo
//
// SEGURANCA: a pasta de saida so e apagada se tiver export.json (nao apaga pastas do usuario por engano).
#pragma once
#include "project.hpp"
#include "project_settings.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

namespace editor {
namespace xp {

struct Result { bool ok = false; fs::path folder; std::string err; uintmax_t bytes = 0; size_t files = 0; };

// Nome seguro para arquivo e pasta: letras, numeros, espaco, "-" e "_" (o resto sai). Nunca vazio.
inline std::string baseName(const std::string& product) {
  std::string out;
  for (unsigned char c : product) if ((c < 128 && std::isalnum(c)) || c == '-' || c == '_' || c == ' ') out += static_cast<char>(c);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() > 40) out.resize(40);
  return out.empty() ? std::string("Game") : out;
}

inline bool skipEntry(const fs::path& p) {
  const std::string n = p.filename().string();
  return n.empty() || n[0] == '.' || (n.size() > 4 && n.substr(n.size() - 4) == ".bak");
}

// Copia uma arvore inteira. Conta bytes e arquivos. Ignora entradas ocultas e *.bak.
inline bool copyTree(const fs::path& from, const fs::path& to, std::string& err, uintmax_t& bytes, size_t& files) {
  std::error_code ec;
  if (!fs::exists(from, ec)) return true; // pasta ausente = nada a copiar
  fs::create_directories(to, ec);
  for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
    if (skipEntry(it->path())) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
    const fs::path dst = to / fs::relative(it->path(), from, ec);
    if (it->is_directory(ec)) { fs::create_directories(dst, ec); continue; }
    if (!it->is_regular_file(ec)) continue;
    fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "Could not copy " + it->path().filename().string(); return false; }
    bytes += fs::file_size(dst, ec); ++files;
  }
  if (ec) { err = "Could not read " + from.filename().string(); return false; }
  return true;
}

// Exporta. `editorDir` = pasta onde estao o executavel do editor, assets/ e as DLLs. `outRoot` = pasta de saida.
// Falha (sem criar nada) se as informacoes do "Editor info" estiverem incompletas ou a plataforma nao existir aqui.
inline Result exportGame(const Project& proj, const ProjectSettings& s, int platform, const fs::path& editorDir,
                         const std::string& editorExe, const fs::path& outRoot) {
  Result r;
  if (platform < 0 || platform >= static_cast<int>(kExportPlatforms.size()) || !platformAvailable(platform)) { r.err = "This platform is not available in this build."; return r; }
  if (const auto miss = s.missingInfo(); !miss.empty()) {
    r.err = "Fill in the Editor info first:";
    for (auto& m : miss) r.err += " " + m + ";";
    return r;
  }
  const bool win = platform == 0;
  const std::string base = baseName(s.projectName);
  const std::string suffix = win ? "-win64" : "-linux64";
  const fs::path folder = outRoot / (base + suffix);
  r.folder = folder;
  std::error_code ec;
  if (fs::exists(folder, ec)) {
    if (!fs::exists(folder / "export.json", ec)) { r.err = "The output folder exists and is not an export: " + folder.string(); return r; }
    fs::remove_all(folder, ec);
    if (ec) { r.err = "Could not replace the previous export."; return r; }
  }
  fs::create_directories(folder, ec);
  if (ec) { r.err = "Could not create the output folder."; return r; }

  const fs::path src = editorDir / editorExe;
  if (!fs::exists(src, ec)) { r.err = "Editor executable not found: " + src.string(); return r; }
  const std::string exeName = win ? base + ".exe" : base;
  fs::copy_file(src, folder / exeName, fs::copy_options::overwrite_existing, ec);
  if (ec) { r.err = "Could not copy the executable."; return r; }
  if (!win) fs::permissions(folder / exeName, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, ec);
  if (win) { // DLLs ao lado do editor (SDL3 e o que mais o build tiver copiado)
    for (fs::directory_iterator it(editorDir, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_regular_file(ec) && it->path().extension() == ".dll") { fs::copy_file(it->path(), folder / it->path().filename(), fs::copy_options::overwrite_existing, ec); }
  }
  if (!copyTree(editorDir / "assets", folder / "assets", r.err, r.bytes, r.files)) return r;
  if (!copyTree(proj.root, folder / "Data" / "Project", r.err, r.bytes, r.files)) return r;

  // Atalho para abrir o jogo: o proprio executavel recebe --player e entra no modo de jogo.
  if (win) writeTextFile(folder / "Play.bat", "@echo off\r\ncd /d \"%~dp0\"\r\nstart \"\" \"" + exeName + "\" --player\r\n");
  else { writeTextFile(folder / "play.sh", "#!/bin/sh\ncd \"$(dirname \"$0\")\" || exit 1\nexec \"./" + exeName + "\" --player\n");
         fs::permissions(folder / "play.sh", fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, ec); }

  // export.json: dados do Editor info (o que o Windows mostra nas propriedades do arquivo fica aqui tambem).
  const std::time_t now = std::time(nullptr);
  std::tm tmNow{};
#if defined(_WIN32)
  localtime_s(&tmNow, &now);
#else
  localtime_r(&now, &tmNow);
#endif
  char stamp[32]; std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &tmNow);
  tool::Json j = tool::Json::object();
  j.set("product", s.projectName); j.set("creator", s.author); j.set("studio", s.info.studio);
  j.set("country", s.info.country); j.set("city", s.info.city); j.set("version", s.info.version);
  j.set("description", s.info.description); j.set("copyright", s.copyrightText(tmNow.tm_year + 1900));
  j.set("website", s.info.website); j.set("platform", std::string(kExportPlatforms[static_cast<size_t>(platform)]));
  j.set("executable", exeName); j.set("exportedAt", std::string(stamp));
  j.set("files", static_cast<double>(r.files)); j.set("bytes", static_cast<double>(r.bytes));
  if (!writeTextFile(folder / "export.json", j.dump())) { r.err = "Could not write export.json."; return r; }
  r.ok = true;
  return r;
}

} // namespace xp
} // namespace editor
