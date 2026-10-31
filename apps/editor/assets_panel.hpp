// ToolEngine Editor - janelas "Assets" (navegador de arquivos do projeto) e "Scenes" (cenas separadas).
//
// ASSETS: mostra Project/Assets como pastas e arquivos, em LISTA ou ICONES. Permite renomear (atualizando as
//   referencias nas cenas), apagar, criar pasta/arquivo de texto, editar textos, importar (botao Import, arrastar
//   arquivos do sistema para a janela do editor) e ouvir sons. Duplo clique: pasta = entrar, modelo = colocar na
//   cena, som = ouvir, cena = abrir, texto = editar. Arrastar um modelo/som para a janela Scene o instancia la.
// SCENES: cada cena e um arquivo Project/Scenes/<nome>.scene.json com seus proprios objetos e sua lista de
//   assets "alocados". Criar/abrir/renomear/apagar cenas e gerenciar os assets de cada uma.
#pragma once
#include "editor_state.hpp"
#include "extra_ui.hpp"
#include "i18n.hpp"

#include "imgui.h"

#include <mutex>

namespace editor {

using editor::F;
using editor::T;
using editor::W;

// Arquivos pedidos para importar (botao Import / arrastar e soltar). O dialogo do SDL responde em outra thread.
inline std::mutex& importMutex() { static std::mutex m; return m; }
inline std::vector<std::string>& importQueue() { static std::vector<std::string> q; return q; }
inline void queueImport(const std::string& path) { std::lock_guard lk(importMutex()); importQueue().push_back(path); }

struct AssetsUi {
  std::string folder = "Assets";   // pasta atual, relativa ao projeto
  bool iconView = true;
  float iconSize = 84.f;
  char filter[48] = "";
  std::string selected;            // caminho relativo do item selecionado
  // dialogos
  std::string renameTarget; char renameBuf[100] = ""; bool openRename = false;
  std::string deleteTarget; bool openDelete = false;
  char newFolderBuf[100] = "New Folder"; bool openNewFolder = false;
  char newFileBuf[100] = "notes.txt"; bool openNewFile = false;
  // editor de texto
  bool textOpen = false; std::string textPath; std::vector<char> textBuf; std::string textStatus;
  // cenas
  char newSceneBuf[60] = "Level2"; bool openNewScene = false;
  std::string sceneRenameTarget; char sceneRenameBuf[60] = ""; bool openSceneRename = false;
  std::string sceneDeleteTarget; bool openSceneDelete = false;
};
inline AssetsUi& ui() { static AssetsUi u; return u; }

inline const char* kindLabel(AssetKind k) {
  switch (k) {
    case AssetKind::Folder: return "Folder"; case AssetKind::Model: return "Model"; case AssetKind::Audio: return "Audio";
    case AssetKind::Scene: return "Scene"; case AssetKind::Image: return "Image"; case AssetKind::Text: return "Text"; default: return "File";
  }
}

// Troca "antigo" por "novo" (arquivo ou prefixo de pasta) em todo texto de cena guardado em disco e na cena aberta.
inline void updateReferences(Editor& ed, const std::string& oldRel, const std::string& newRel) {
  auto fix = [&](std::string& s) {
    if (s == oldRel) s = newRel;
    else if (s.size() > oldRel.size() && s.compare(0, oldRel.size(), oldRel) == 0 && s[oldRel.size()] == '/') s = newRel + s.substr(oldRel.size());
  };
  ed.scene.world().each<MeshRenderer>([&](tool::Entity, MeshRenderer& m) { std::string s = m.model; fix(s); ser::copyStr(m.model, sizeof m.model, s); });
  ed.scene.world().each<AudioSource>([&](tool::Entity, AudioSource& a) { std::string s = a.clip; fix(s); ser::copyStr(a.clip, sizeof a.clip, s); });
  for (auto& a : ed.sceneInfo.assets) fix(a);
  // outras cenas (texto): troca "<antigo>" e "<antigo>/..." entre aspas
  for (auto& name : ed.project.listScenes()) {
    if (name == ed.sceneInfo.name) continue;
    const fs::path p = ed.project.scenePath(name); std::string text;
    if (!readTextFile(p, text)) continue;
    auto replaceAll = [&](const std::string& from, const std::string& to) { size_t pos = 0; bool ch = false; while ((pos = text.find(from, pos)) != std::string::npos) { text.replace(pos, from.size(), to); pos += to.size(); ch = true; } return ch; };
    bool changed = replaceAll("\"" + oldRel + "\"", "\"" + newRel + "\"");
    changed = replaceAll("\"" + oldRel + "/", "\"" + newRel + "/") || changed;
    if (changed) writeTextFile(p, text);
  }
  ed.lib.clear();
}

inline bool insideAssets(const Editor& ed, const fs::path& p) {
  std::error_code ec;
  const fs::path rel = fs::weakly_canonical(p, ec).lexically_relative(fs::weakly_canonical(ed.project.assetsDir(), ec));
  return !rel.empty() && *rel.begin() != "..";
}

// Renomeia um arquivo/pasta dentro de Project/Assets e corrige todas as referencias. Retorna "" se deu certo, senao o erro.
inline std::string renameAsset(Editor& ed, const std::string& relOld, const std::string& newName) {
  if (!safeName(newName)) return "Invalid name";
  const fs::path oldp = ed.project.resolve(relOld);
  if (oldp.empty() || !insideAssets(ed, oldp)) return "Path outside the Assets folder";
  const fs::path newp = oldp.parent_path() / newName;
  if (oldp == newp) return "";
  std::error_code ec;
  if (fs::exists(newp, ec)) return "A file with that name already exists";
  fs::rename(oldp, newp, ec);
  if (ec) return "Rename failed: " + ec.message();
  const std::string nr = ed.project.relative(newp);
  updateReferences(ed, relOld, nr);
  ed.saveScene();
  ed.statusLine = "Renamed to " + nr;
  return "";
}

// Apaga um arquivo/pasta de Project/Assets (confinado a essa pasta). Retorna "" se deu certo, senao o erro.
inline std::string deleteAsset(Editor& ed, const std::string& rel) {
  const fs::path p = ed.project.resolve(rel);
  if (p.empty() || !insideAssets(ed, p) || fs::path(rel) == "Assets") return "Path outside the Assets folder";
  std::error_code ec;
  fs::remove_all(p, ec);
  ed.lib.clear();
  if (ec) return "Delete failed: " + ec.message();
  ed.statusLine = "Deleted " + rel;
  return "";
}

inline void instantiateAsset(Editor& ed, const std::string& rel, bool atTarget = true) {
  const AssetKind k = kindOf(rel, false);
  tool::Entity e;
  if (k == AssetKind::Model) e = ed.makeModel(rel);
  else if (k == AssetKind::Audio) { e = ed.makeAudio(rel); ed.assignAsset(rel); }
  else return;
  if (atTarget) if (auto* t = ed.scene.world().get<Transform>(e)) { t->pos[0] = ed.target.x; t->pos[1] = ed.target.y - 0.5f; t->pos[2] = ed.target.z; }
  ed.selected = e;
}

inline void processImports(Editor& ed) {
  std::vector<std::string> q;
  { std::lock_guard lk(importMutex()); q.swap(importQueue()); }
  for (auto& src : q) {
    std::string err;
    const fs::path dst = ed.project.importFile(src, ed.project.root / ui().folder, &err);
    if (dst.empty()) { ed.statusLine = "Import failed: " + err; tool::logError(ed.statusLine); continue; }
    const std::string rel = ed.project.relative(dst);
    ed.statusLine = "Imported: " + rel; tool::logInfo(ed.statusLine);
    const AssetKind k = kindOf(dst, false);
    if (k == AssetKind::Model || k == AssetKind::Audio) ed.assignAsset(rel);
    ui().selected = rel;
  }
}

inline void drawTileIcon(ImDrawList* dl, ImVec2 p, float s, AssetKind k, const char* ext) {
  struct Style { ImU32 fill; const char* text; };
  Style st{IM_COL32(110, 110, 120, 255), ""};
  switch (k) {
    case AssetKind::Folder: st = {IM_COL32(222, 170, 60, 255), ""}; break;
    case AssetKind::Model: st = {IM_COL32(90, 150, 230, 255), "3D"}; break;
    case AssetKind::Audio: st = {IM_COL32(110, 200, 130, 255), ""}; break;
    case AssetKind::Scene: st = {IM_COL32(190, 110, 220, 255), "SCN"}; break;
    case AssetKind::Image: st = {IM_COL32(230, 120, 120, 255), "IMG"}; break;
    case AssetKind::Text: st = {IM_COL32(170, 170, 180, 255), "TXT"}; break;
    default: break;
  }
  const float m = s * 0.12f;
  const ImVec2 a{p.x + m, p.y + m}, b{p.x + s - m, p.y + s - m};
  if (k == AssetKind::Folder) {
    dl->AddRectFilled({a.x, a.y + s * 0.08f}, {a.x + s * 0.4f, a.y + s * 0.28f}, st.fill, 3.f);
    dl->AddRectFilled({a.x, a.y + s * 0.18f}, b, st.fill, 4.f);
  } else {
    dl->AddRectFilled(a, b, st.fill, 5.f);
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 90), 5.f);
  }
  const char* txt = k == AssetKind::Audio ? ext : st.text;
  if (txt && txt[0]) {
    std::string up = txt[0] == '.' ? std::string(txt + 1) : std::string(txt);
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const ImVec2 ts = ImGui::CalcTextSize(up.c_str());
    dl->AddText({p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f}, IM_COL32(20, 20, 25, 255), up.c_str());
  }
}

inline void openTextEditor(const fs::path& p) {
  AssetsUi& u = ui();
  std::string text;
  std::error_code ec;
  if (fs::file_size(p, ec) > 60000 || !readTextFile(p, text, 60000)) { u.textStatus = "File too large to edit here (60 KB limit)"; u.textOpen = true; u.textPath = p.string(); u.textBuf.assign(1, 0); return; }
  u.textBuf.assign(65536, 0);
  std::memcpy(u.textBuf.data(), text.data(), std::min(text.size(), u.textBuf.size() - 1));
  u.textPath = p.string(); u.textOpen = true; u.textStatus.clear();
}

inline void drawAssets(Editor& ed) {
  AssetsUi& u = ui();
  if (!ImGui::Begin(W("Assets", "assets").c_str(), &ed.showAssets)) { ImGui::End(); return; }
  helpIcon("start");
  ed.project.ensure();
  const fs::path base = ed.project.root / u.folder;
  std::error_code ec;
  if (!fs::exists(base, ec)) u.folder = "Assets";

  // --- barra superior
  if (ImGui::Button("<") && u.folder != "Assets") u.folder = fs::path(u.folder).parent_path().generic_string();
  ImGui::SameLine();
  { // caminho clicavel
    fs::path acc; int i = 0;
    for (auto& part : fs::path(u.folder)) {
      acc /= part;
      if (i++) { ImGui::SameLine(0, 2); ImGui::TextUnformatted("/"); ImGui::SameLine(0, 2); }
      if (ImGui::SmallButton((part.string() + "##crumb" + std::to_string(i)).c_str())) { u.folder = acc.generic_string(); break; }
      ImGui::SameLine(0, 2);
    }
    ImGui::NewLine();
  }
  if (ImGui::Button(T("Import..."))) {
    SDL_ShowOpenFileDialog([](void*, const char* const* files, int) { if (files) for (; *files; ++files) queueImport(*files); }, nullptr, nullptr, nullptr, 0, nullptr, true);
  }
  ImGui::SameLine(); if (ImGui::Button(T("New folder"))) { u.openNewFolder = true; std::snprintf(u.newFolderBuf, sizeof u.newFolderBuf, "New Folder"); }
  ImGui::SameLine(); if (ImGui::Button(T("New text file"))) { u.openNewFile = true; std::snprintf(u.newFileBuf, sizeof u.newFileBuf, "notes.txt"); }
  ImGui::SameLine(); if (ImGui::RadioButton(T("List"), !u.iconView)) u.iconView = false;
  ImGui::SameLine(); if (ImGui::RadioButton(T("Icons"), u.iconView)) u.iconView = true;
  ImGui::SetNextItemWidth(120); ImGui::InputTextWithHint("##assetfilter", T("search"), u.filter, sizeof u.filter);
  if (u.iconView) { ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("##iconsize", &u.iconSize, 48.f, 140.f, "%.0f"); }
  ImGui::TextDisabled("%s", T("Drop files from your computer onto the editor window to import them."));
  ImGui::Separator();

  // --- itens
  struct Item { fs::path path; bool dir; AssetKind kind; };
  std::vector<Item> items;
  std::string flt = u.filter; std::transform(flt.begin(), flt.end(), flt.begin(), ::tolower);
  for (auto& e : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec)) {
    const bool d = e.is_directory(ec);
    std::string n = e.path().filename().string(), l = n; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (!flt.empty() && l.find(flt) == std::string::npos) continue;
    items.push_back({e.path(), d, kindOf(e.path(), d)});
  }
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.dir != b.dir) return a.dir;
    std::string x = a.path.filename().string(), y = b.path.filename().string();
    std::transform(x.begin(), x.end(), x.begin(), ::tolower); std::transform(y.begin(), y.end(), y.begin(), ::tolower);
    return x < y;
  });

  ImGui::BeginChild("assetitems", ImVec2(0, 0), ImGuiChildFlags_None);
  const float tile = u.iconSize, pad = 8.f, lineH = ImGui::GetTextLineHeightWithSpacing();
  const int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (tile + pad)));
  int idx = 0;
  for (auto& it : items) {
    const std::string name = it.path.filename().string(), rel = ed.project.relative(it.path);
    ImGui::PushID(rel.c_str());
    bool clicked = false, dbl = false;
    if (u.iconView) {
      if (idx % cols != 0) ImGui::SameLine(0, pad);
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      clicked = ImGui::Selectable("##tile", u.selected == rel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(tile, tile + lineH * 1.3f));
      dbl = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      drawTileIcon(dl, pos, tile, it.kind, it.path.extension().string().c_str());
      dl->PushClipRect({pos.x, pos.y + tile}, {pos.x + tile, pos.y + tile + lineH * 1.3f}, true);
      const ImVec2 ts = ImGui::CalcTextSize(name.c_str());
      dl->AddText({pos.x + std::max(0.f, (tile - ts.x) * 0.5f), pos.y + tile + 2.f}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
      dl->PopClipRect();
    } else {
      const std::string label = std::string(it.dir ? "[+] " : "    ") + name;
      clicked = ImGui::Selectable(label.c_str(), u.selected == rel, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns);
      dbl = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
      ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.6f); ImGui::TextDisabled("%s", T(kindLabel(it.kind)));
      if (!it.dir) { ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.85f + ImGui::GetCursorPosX() * 0.f); std::error_code e2; ImGui::TextDisabled("%.1f KB", static_cast<double>(fs::file_size(it.path, e2)) / 1024.0); }
    }
    if (clicked) u.selected = rel;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
    if (!it.dir && ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("ASSET_PATH", rel.c_str(), rel.size() + 1); ImGui::Text("%s", name.c_str()); ImGui::EndDragDropSource(); }
    if (dbl) {
      switch (it.kind) {
        case AssetKind::Folder: u.folder = rel; break;
        case AssetKind::Model: case AssetKind::Audio: instantiateAsset(ed, rel); break;
        case AssetKind::Scene: { std::string n = name.substr(0, name.size() - 11); ed.openScene(n); } break;
        case AssetKind::Text: openTextEditor(it.path); break;
        default: break;
      }
    }
    if (ImGui::BeginPopupContextItem("ctx")) {
      u.selected = rel;
      if (it.kind == AssetKind::Model && ImGui::MenuItem(T("Add to scene"))) instantiateAsset(ed, rel);
      if (it.kind == AssetKind::Audio) {
        if (ImGui::MenuItem(T("Play preview"))) ed.previewSound(rel);
        if (ImGui::MenuItem(T("Add Audio Source to scene"))) instantiateAsset(ed, rel);
      }
      if ((it.kind == AssetKind::Model || it.kind == AssetKind::Audio) && ImGui::MenuItem(T("Assign to current scene"))) { ed.assignAsset(rel); ed.statusLine = "Assigned to scene: " + rel; }
      if (it.kind == AssetKind::Text && ImGui::MenuItem(T("Edit"))) openTextEditor(it.path);
      if (it.kind == AssetKind::Scene && ImGui::MenuItem(T("Open scene"))) ed.openScene(name.substr(0, name.size() - 11));
      if (it.kind == AssetKind::Folder && ImGui::MenuItem(T("Open"))) u.folder = rel;
      ImGui::Separator();
      if (ImGui::MenuItem(T("Rename"))) { u.renameTarget = rel; std::snprintf(u.renameBuf, sizeof u.renameBuf, "%s", name.c_str()); u.openRename = true; }
      if (ImGui::MenuItem(T("Delete"))) { u.deleteTarget = rel; u.openDelete = true; }
      ImGui::EndPopup();
    }
    ImGui::PopID();
    ++idx;
  }
  if (items.empty()) ImGui::TextDisabled("%s", T("This folder is empty."));
  if (ImGui::IsKeyPressed(ImGuiKey_F2) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !u.selected.empty()) { u.renameTarget = u.selected; std::snprintf(u.renameBuf, sizeof u.renameBuf, "%s", fs::path(u.selected).filename().string().c_str()); u.openRename = true; }
  if (ImGui::BeginPopupContextWindow("assetsbg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::MenuItem(T("New folder"))) u.openNewFolder = true;
    if (ImGui::MenuItem(T("New text file"))) u.openNewFile = true;
    if (ImGui::MenuItem(T("Import..."))) SDL_ShowOpenFileDialog([](void*, const char* const* files, int) { if (files) for (; *files; ++files) queueImport(*files); }, nullptr, nullptr, nullptr, 0, nullptr, true);
    ImGui::EndPopup();
  }
  ImGui::EndChild();

  // --- dialogos
  if (u.openRename) { ImGui::OpenPopup("Rename##dlg"); u.openRename = false; }
  if (ImGui::BeginPopupModal("Rename##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(T("New name:"));
    const bool enter = ImGui::InputText("##rn", u.renameBuf, sizeof u.renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    const bool ok = safeName(u.renameBuf);
    if (!ok) ImGui::TextColored({1, .5f, .4f, 1}, "%s", T("Invalid name (no / \\ : * ? \" < > |)."));
    if ((ImGui::Button(T("OK")) || enter) && ok) {
      const std::string err = renameAsset(ed, u.renameTarget, u.renameBuf);
      if (!err.empty()) ed.statusLine = err;
      else { const std::string nr = ed.project.relative(ed.project.resolve(u.renameTarget).parent_path() / u.renameBuf); if (u.selected == u.renameTarget) u.selected = nr; if (u.folder == u.renameTarget) u.folder = nr; }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (u.openDelete) { ImGui::OpenPopup("Delete##dlg"); u.openDelete = false; }
  if (ImGui::BeginPopupModal("Delete##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s", F("Delete '%s'? This cannot be undone.", u.deleteTarget.c_str()).c_str());
    if (ImGui::Button(T("Delete"))) {
      const std::string err = deleteAsset(ed, u.deleteTarget); if (!err.empty()) ed.statusLine = err;
      u.selected.clear(); ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (u.openNewFolder) { ImGui::OpenPopup("New folder##dlg"); u.openNewFolder = false; }
  if (ImGui::BeginPopupModal("New folder##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const bool enter = ImGui::InputText("##nf", u.newFolderBuf, sizeof u.newFolderBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button(T("OK")) || enter) { if (safeName(u.newFolderBuf)) fs::create_directories(base / u.newFolderBuf, ec); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (u.openNewFile) { ImGui::OpenPopup("New text file##dlg"); u.openNewFile = false; }
  if (ImGui::BeginPopupModal("New text file##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const bool enter = ImGui::InputText("##ntf", u.newFileBuf, sizeof u.newFileBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button(T("OK")) || enter) { if (safeName(u.newFileBuf) && !fs::exists(base / u.newFileBuf, ec)) writeTextFile(base / u.newFileBuf, ""); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::End();

  // --- editor de texto (janela flutuante)
  if (u.textOpen) {
    ImGui::SetNextWindowSize({560, 420}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin((std::string(T("Edit file")) + ": " + fs::path(u.textPath).filename().string() + "###textedit").c_str(), &u.textOpen)) {
      if (ImGui::Button(T("Save")) && u.textBuf.size() > 1) {
        const bool ok = writeTextFile(u.textPath, std::string(u.textBuf.data()));
        u.textStatus = ok ? "Saved." : "Failed to save.";
        ed.lib.clear();
      }
      ImGui::SameLine(); ImGui::TextDisabled("%s", T(u.textStatus.c_str()));
      if (u.textBuf.size() > 1) ImGui::InputTextMultiline("##textbody", u.textBuf.data(), u.textBuf.size(), ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_AllowTabInput);
    }
    ImGui::End();
  }
}

// --- janela de cenas ---------------------------------------------------------------------------------------
inline void drawScenes(Editor& ed) {
  AssetsUi& u = ui();
  if (!ImGui::Begin(W("Scenes", "scenes").c_str(), &ed.showScenes)) { ImGui::End(); return; }
  helpIcon("start");
  if (ed.sceneList.empty()) ed.refreshScenes();
  ImGui::Text("%s", F("Current scene: %s", ed.sceneInfo.name.c_str()).c_str());
  if (ImGui::Button(T("Save scene"))) ed.saveScene();
  ImGui::SameLine(); if (ImGui::Button(T("New scene..."))) { u.openNewScene = true; }
  ImGui::SameLine(); if (ImGui::Button(T("Reload list"))) ed.refreshScenes();
  ImGui::TextDisabled("%s", T("Switching scenes saves the current one first."));
  ImGui::SeparatorText(T("Scenes in project"));
  ImGui::BeginChild("scenelist", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.5f), ImGuiChildFlags_Borders);
  for (auto& n : ed.sceneList) {
    ImGui::PushID(n.c_str());
    const bool cur = n == ed.sceneInfo.name;
    if (ImGui::Selectable((n + (cur ? "  *" : "")).c_str(), cur, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !cur) { ed.openScene(n); ImGui::PopID(); break; }
    if (ImGui::BeginPopupContextItem("sctx")) {
      if (!cur && ImGui::MenuItem(T("Open"))) { ed.openScene(n); ImGui::EndPopup(); ImGui::PopID(); break; }
      if (ImGui::MenuItem(T("Rename"))) { u.sceneRenameTarget = n; std::snprintf(u.sceneRenameBuf, sizeof u.sceneRenameBuf, "%s", n.c_str()); u.openSceneRename = true; }
      if (!cur && ImGui::MenuItem(T("Delete"))) { u.sceneDeleteTarget = n; u.openSceneDelete = true; }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::SeparatorText(T("Assets assigned to this scene"));
  if (!u.selected.empty() && (kindOf(u.selected, false) == AssetKind::Model || kindOf(u.selected, false) == AssetKind::Audio)) {
    if (ImGui::Button(F("Assign '%s'", fs::path(u.selected).filename().string().c_str()).c_str())) ed.assignAsset(u.selected);
  } else ImGui::TextDisabled("%s", T("Select a model or sound in Assets to assign it here."));
  int remove = -1;
  for (size_t i = 0; i < ed.sceneInfo.assets.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
    ImGui::SameLine(); ImGui::TextUnformatted(ed.sceneInfo.assets[i].c_str());
    const std::string& a = ed.sceneInfo.assets[i]; std::error_code ec;
    if (!fs::exists(ed.project.resolve(a), ec)) { ImGui::SameLine(); ImGui::TextColored({1, .5f, .4f, 1}, "%s", T("(missing)")); }
    ImGui::PopID();
  }
  if (remove >= 0) ed.sceneInfo.assets.erase(ed.sceneInfo.assets.begin() + remove);
  if (ed.sceneInfo.assets.empty()) ImGui::TextDisabled("%s", T("(none)"));

  if (u.openNewScene) { ImGui::OpenPopup("New scene##dlg"); u.openNewScene = false; }
  if (ImGui::BeginPopupModal("New scene##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const bool enter = ImGui::InputText("##ns", u.newSceneBuf, sizeof u.newSceneBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button(T("Create")) || enter) { if (ed.newScene(u.newSceneBuf)) ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    if (!ed.statusLine.empty()) ImGui::TextDisabled("%s", ed.statusLine.c_str());
    ImGui::EndPopup();
  }
  if (u.openSceneRename) { ImGui::OpenPopup("Rename scene##dlg"); u.openSceneRename = false; }
  if (ImGui::BeginPopupModal("Rename scene##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const bool enter = ImGui::InputText("##rs", u.sceneRenameBuf, sizeof u.sceneRenameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if ((ImGui::Button(T("OK")) || enter) && safeName(u.sceneRenameBuf)) {
      std::error_code ec; const bool cur = u.sceneRenameTarget == ed.sceneInfo.name;
      if (cur) ed.saveScene();
      if (!fs::exists(ed.project.scenePath(u.sceneRenameBuf), ec)) { fs::rename(ed.project.scenePath(u.sceneRenameTarget), ed.project.scenePath(u.sceneRenameBuf), ec); if (!ec && cur) ed.sceneInfo.name = u.sceneRenameBuf; }
      ed.refreshScenes(); ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (u.openSceneDelete) { ImGui::OpenPopup("Delete scene##dlg"); u.openSceneDelete = false; }
  if (ImGui::BeginPopupModal("Delete scene##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s", F("Delete scene '%s'? This cannot be undone.", u.sceneDeleteTarget.c_str()).c_str());
    if (ImGui::Button(T("Delete"))) { std::error_code ec; fs::remove(ed.project.scenePath(u.sceneDeleteTarget), ec); ed.refreshScenes(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(); if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::End();
}

} // namespace editor
