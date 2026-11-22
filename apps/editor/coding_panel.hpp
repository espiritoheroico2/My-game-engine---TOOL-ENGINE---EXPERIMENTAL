#pragma once

// Janela Coding: lista dos codigos-fonte do projeto (esquerda) e editor de texto (direita).
// A moldura e SpriteUI (framed::Chrome). O corpo e desenhado direto na lista de primitivas do ImGui
// (primeiro plano) dentro da area da moldura, com entrada de teclado e mouse proprias. Clicar num codigo salva o
// que estava aberto e abre o clicado. Ctrl+S salva. O botao do canto direito mostra/oculta a lista.

#include "framed_window.hpp"
#include "i18n.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace editor::coding {

namespace fs = std::filesystem;

class Panel {
 public:
  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : chrome_{text, sdl} {
    chrome_.setBodyFill(false);   // o corpo e desenhado aqui: a moldura nao pinta por cima
  }
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  [[nodiscard]] framed::Chrome& chrome() { return chrome_; }
  [[nodiscard]] const framed::Chrome& chrome() const { return chrome_; }

  [[nodiscard]] float fontSize() const { return codeFont_; }
  void setFontSize(float size) { codeFont_ = std::clamp(size, kMinCodeFont, kMaxCodeFont); }
  [[nodiscard]] bool wantsTextCursor(float x, float y) const {
    return editing_ && x >= editorRect_.x && x < editorRect_.x + editorRect_.w &&
           y >= editorRect_.y && y < editorRect_.y + editorRect_.h;
  }

  // Pasta do projeto: varre os codigos quando ela muda. Trocar de projeto salva o arquivo aberto antes.
  void setRoot(const fs::path& root) {
    if (scanned_ && root == root_) return;
    if (dirty_) save();
    root_ = root;
    rescan();
  }

  // Desenha o corpo (origem e tamanho da area da moldura) e trata a entrada enquanto o mouse e o teclado estao nele.
  void drawBody(ImVec2 o, ImVec2 s) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = ImGui::GetFont();
    const ImVec2 e{o.x + s.x, o.y + s.y};
    const ImVec2 mouse = io.MousePos;
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool wheelOk = io.MouseWheel != 0.f;

    dl->PushClipRect(o, e, true);
    dl->AddRectFilled(o, e, col(kBody));

    // Barra: Salvar, nome do arquivo e botao da lista no canto direito.
    const ImVec2 saveA{o.x + kPad, o.y + 4.f}, saveB{saveA.x + 52.f, saveA.y + kBar - 8.f};
    if (button(dl, font, saveA, saveB, T("Save"), mouse, click)) save();
    const std::string name = current_ >= 0 ? files_[static_cast<std::size_t>(current_)].rel + (dirty_ ? " *" : "")
                                           : std::string(T("No file open"));
    text(dl, font, ImVec2(saveB.x + kPad, o.y + 8.f), name.c_str(), col(kDim));

    // Controles de tamanho do codigo: ficam no topo, como na barra da janela, e nunca dependem da lista.
    const float zoomW = 28.f;
    const float zoomGap = 4.f;
    const ImVec2 plusA{e.x - kPad - zoomW, saveA.y}, plusB{e.x - kPad, saveB.y};
    const ImVec2 minusA{plusA.x - zoomGap - zoomW, saveA.y}, minusB{plusA.x - zoomGap, saveB.y};
    if (button(dl, font, minusA, minusB, "-", mouse, click)) {
      setFontSize(codeFont_ - 1.f);
    }
    if (button(dl, font, plusA, plusB, "+", mouse, click)) {
      setFontSize(codeFont_ + 1.f);
    }

    char zoomLabel[16];
    std::snprintf(zoomLabel, sizeof zoomLabel, "%.0f", codeFont_);
    const float zoomLabelW = font->CalcTextSizeA(kFont, FLT_MAX, 0.f, zoomLabel).x;
    text(dl, font, ImVec2(minusA.x - zoomLabelW - 8.f, o.y + 8.f), zoomLabel, col(kDim));

    const char* toggle = listShown_ ? T("Hide list") : T("Show list");
    const float tw = font->CalcTextSizeA(kFont, FLT_MAX, 0.f, toggle).x + 16.f;
    const ImVec2 tA{minusA.x - kPad - tw - 8.f, saveA.y}, tB{minusA.x - kPad - 8.f, saveB.y};
    if (button(dl, font, tA, tB, toggle, mouse, click)) listShown_ = !listShown_;
    dl->AddLine(ImVec2(o.x, o.y + kBar), ImVec2(e.x, o.y + kBar), col(kBorder));

    const float top = o.y + kBar + 1.f;
    float editorX = o.x;
    if (listShown_) {
      const ImVec2 la{o.x, top}, lb{o.x + kListW, e.y};
      if (wheelOk && inside(mouse, la, lb)) listScroll_ = std::max(0.f, listScroll_ - io.MouseWheel * kRow * 2.f);
      const float maxScroll = std::max(0.f, static_cast<float>(files_.size()) * kRow - (lb.y - la.y) + kRow);
      listScroll_ = std::min(listScroll_, maxScroll);
      dl->PushClipRect(la, lb, true);
      if (files_.empty()) text(dl, font, ImVec2(la.x + kPad, la.y + 6.f), T("No code files found in the project."), col(kDim));
      for (std::size_t i = 0; i < files_.size(); ++i) {
        const float y = top + static_cast<float>(i) * kRow - listScroll_;
        if (y + kRow < la.y || y > lb.y) continue;
        const ImVec2 ra{la.x, y}, rb{lb.x - 1.f, y + kRow};
        const bool selected = static_cast<int>(i) == current_;
        const bool hover = inside(mouse, ra, rb);
        if (selected) dl->AddRectFilled(ra, rb, col(kSelect));
        else if (hover) dl->AddRectFilled(ra, rb, col(kHover));
        text(dl, font, ImVec2(ra.x + kPad, y + 3.f), files_[i].rel.c_str(), col(selected ? kText : kDim));
        if (click && hover) open(i);
      }
      dl->PopClipRect();
      dl->AddLine(ImVec2(lb.x, top), ImVec2(lb.x, e.y), col(kBorder));
      editorX = lb.x + 1.f;
    }

    const ImVec2 ea{editorX + kPad, top + 4.f}, eb{e.x - kPad, e.y - kPad};
    if (current_ < 0) {
      text(dl, font, ea, T("Choose a code file in the list to edit it."), col(kDim));
    } else {
      drawEditor(dl, font, io, ea, eb, mouse, click);
    }

    dl->PopClipRect();
    dl->AddRect(o, e, col(kBorder));   // contorno da paleta (a moldura nao desenha o seu)
  }

 private:
  static constexpr float kFont = 12.f;
  static constexpr float kMinCodeFont = 12.f;
  static constexpr float kMaxCodeFont = 28.f;
  static constexpr float kCodeFontStep = 1.f;
  static constexpr float kPad = 8.f;
  static constexpr float kBar = 30.f;
  static constexpr float kRow = 18.f;
  static constexpr float kLine = 16.f;
  static constexpr float kListW = 240.f;
  static constexpr std::size_t kMaxFiles = 5000;

  enum Tone { kBody, kBorder, kText, kDim, kSelect, kHover, kButton, kButtonHover, kCaret };
  static ImU32 col(Tone t) {
    switch (t) {
      case kBody: return IM_COL32(14, 19, 28, 250);
      case kBorder: return IM_COL32(41, 56, 84, 255);
      case kText: return IM_COL32(233, 234, 238, 255);
      case kDim: return IM_COL32(150, 160, 178, 255);
      case kSelect: return IM_COL32(52, 68, 96, 255);
      case kHover: return IM_COL32(30, 40, 58, 255);
      case kButton: return IM_COL32(40, 70, 112, 255);
      case kButtonHover: return IM_COL32(56, 96, 150, 255);
      case kCaret: return IM_COL32(120, 190, 255, 255);
    }
    return IM_COL32(255, 255, 255, 255);
  }

  static bool inside(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y; }

  static void text(ImDrawList* dl, ImFont* font, ImVec2 p, const char* t, ImU32 c) {
    dl->AddText(font, kFont, p, c, t);
  }

  static void codeText(ImDrawList* dl, ImFont* font, float size, ImVec2 p, const char* t, ImU32 c) {
    dl->AddText(font, size, p, c, t);
  }

  static bool button(ImDrawList* dl, ImFont* font, ImVec2 a, ImVec2 b, const char* label, ImVec2 mouse, bool click) {
    const bool hover = inside(mouse, a, b);
    dl->AddRectFilled(a, b, col(hover ? kButtonHover : kButton), 3.f);
    const float w = font->CalcTextSizeA(kFont, FLT_MAX, 0.f, label).x;
    text(dl, font, ImVec2(a.x + (b.x - a.x - w) * 0.5f, a.y + (b.y - a.y - kFont) * 0.5f - 1.f), label, col(kText));
    return click && hover;
  }

  static float widthOf(ImFont* font, float size, const std::string& s, std::size_t n) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str(), s.c_str() + n).x;
  }

  // Coluna (em bytes) mais proxima da posicao x dentro da linha.
  static int columnAt(ImFont* font, float size, const std::string& s, float x) {
    int best = 0;
    float bestD = FLT_MAX;
    for (std::size_t c = 0; c <= s.size(); ++c) {
      const float d = std::abs(widthOf(font, size, s, c) - x);
      if (d < bestD) { bestD = d; best = static_cast<int>(c); }
    }
    return best;
  }

  static void appendUtf8(std::string& out, ImWchar c) {
    if (c < 0x80) { out.push_back(static_cast<char>(c)); return; }
    if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); return; }
    out.push_back(static_cast<char>(0xE0 | (c >> 12)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }

  void drawEditor(ImDrawList* dl, ImFont* font, ImGuiIO& io, ImVec2 ea, ImVec2 eb, ImVec2 mouse, bool click) {
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();

    const float lineHeight = codeFont_ * 1.35f;
    const int digits = std::max(2, static_cast<int>(std::to_string(std::max(1, static_cast<int>(lines_.size()))).size()));
    const float gutterW = std::max(36.f, static_cast<float>(digits) * codeFont_ * 0.68f + 10.f);
    const ImVec2 codeA{ea.x + gutterW, ea.y};
    const ImVec2 codeB{eb.x, eb.y};
    editorRect_ = {codeA.x, codeA.y, codeB.x - codeA.x, codeB.y - codeA.y};

    dl->AddRectFilled(ImVec2(ea.x, ea.y - 4.f), ImVec2(codeA.x - 4.f, eb.y), col(kBody));
    dl->AddLine(ImVec2(codeA.x - 5.f, ea.y - 4.f), ImVec2(codeA.x - 5.f, eb.y), col(kBorder));

    const int visible = std::max(1, static_cast<int>((eb.y - ea.y) / lineHeight));
    const int count = static_cast<int>(lines_.size());

    if (click) {
      if (inside(mouse, codeA, codeB)) {
        editing_ = true;
        caretLine_ = std::clamp(topLine_ + static_cast<int>((mouse.y - codeA.y) / lineHeight), 0, count - 1);
        caretCol_ = columnAt(font, codeFont_, lines_[static_cast<std::size_t>(caretLine_)], mouse.x - codeA.x);
        selectionAnchorLine_ = caretLine_;
        selectionAnchorCol_ = caretCol_;
        selecting_ = true;
      } else {
        editing_ = false;
        selecting_ = false;
      }
    }
    if (selecting_ && io.MouseDown[0] && inside(mouse, codeA, codeB)) {
      caretLine_ = std::clamp(topLine_ + static_cast<int>((mouse.y - codeA.y) / lineHeight), 0, count - 1);
      caretCol_ = columnAt(font, codeFont_, lines_[static_cast<std::size_t>(caretLine_)], mouse.x - codeA.x);
    } else if (selecting_ && !io.MouseDown[0]) {
      selecting_ = false;
    }
    if (io.MouseWheel != 0.f && inside(mouse, codeA, codeB)) {
      topLine_ = std::clamp(topLine_ - static_cast<int>(io.MouseWheel) * 3, 0, std::max(0, count - 1));
    }

    if (editing_ && !io.KeyCtrl) {
      for (ImWchar c : io.InputQueueCharacters) {
        if (c >= 32 && c != 127) insertText(utf8(c));
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Enter, true)) newline();
      if (ImGui::IsKeyPressed(ImGuiKey_Tab, true)) insertText("    ");
      if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) backspace();
      if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) deleteForward();
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) moveLeft();
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) moveRight();
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) moveVertical(-1, visible);
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) moveVertical(1, visible);
      if (ImGui::IsKeyPressed(ImGuiKey_Home, true)) caretCol_ = 0;
      if (ImGui::IsKeyPressed(ImGuiKey_End, true)) caretCol_ = static_cast<int>(lines_[static_cast<std::size_t>(caretLine_)].size());
      if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) moveVertical(-visible, visible);
      if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) moveVertical(visible, visible);
    }

    // Mantem o cursor visivel.
    if (caretLine_ < topLine_) topLine_ = caretLine_;
    if (caretLine_ >= topLine_ + visible) topLine_ = caretLine_ - visible + 1;

    for (int i = topLine_; i < std::min(count, topLine_ + visible + 1); ++i) {
      const float y = codeA.y + static_cast<float>(i - topLine_) * lineHeight;
      char number[24];
      std::snprintf(number, sizeof number, "%d", i + 1);
      const float numberW = font->CalcTextSizeA(codeFont_, FLT_MAX, 0.f, number).x;
      codeText(dl, font, codeFont_, ImVec2(codeA.x - gutterW + gutterW - numberW - 8.f, y), number, col(kDim));

      int startLine = selectionAnchorLine_, startCol = selectionAnchorCol_;
      int endLine = caretLine_, endCol = caretCol_;
      if (endLine < startLine || (endLine == startLine && endCol < startCol)) {
        std::swap(startLine, endLine); std::swap(startCol, endCol);
      }
      if (i >= startLine && i <= endLine && (startLine != endLine || startCol != endCol)) {
        const std::string& line = lines_[static_cast<std::size_t>(i)];
        const int beginCol = i == startLine ? startCol : 0;
        const int finishCol = i == endLine ? endCol : static_cast<int>(line.size());
        if (finishCol >= beginCol) {
          const float x0 = codeA.x + widthOf(font, codeFont_, line, static_cast<std::size_t>(beginCol));
          const float x1 = codeA.x + widthOf(font, codeFont_, line, static_cast<std::size_t>(finishCol));
          dl->AddRectFilled(ImVec2(x0, y), ImVec2(std::max(x0 + 1.f, x1), y + lineHeight), col(kSelect));
        }
      }
      codeText(dl, font, codeFont_, ImVec2(codeA.x, y), lines_[static_cast<std::size_t>(i)].c_str(), col(kText));
    }
    if (editing_) {
      const std::string& l = lines_[static_cast<std::size_t>(caretLine_)];
      const float x = codeA.x + widthOf(font, codeFont_, l, static_cast<std::size_t>(caretCol_));
      const float y = codeA.y + static_cast<float>(caretLine_ - topLine_) * lineHeight;
      if (caretLine_ >= topLine_ && caretLine_ < topLine_ + visible) dl->AddLine(ImVec2(x, y), ImVec2(x, y + lineHeight), col(kCaret));
    }
  }

  static std::string utf8(ImWchar c) {
    std::string s;
    appendUtf8(s, c);
    return s;
  }

  // Edicao: a linha do cursor e as operacoes sobre lines_.
  bool hasSelection() const {
    return selectionAnchorLine_ != caretLine_ || selectionAnchorCol_ != caretCol_;
  }
  void deleteSelection() {
    if (!hasSelection()) return;
    int startLine = selectionAnchorLine_, startCol = selectionAnchorCol_;
    int endLine = caretLine_, endCol = caretCol_;
    if (endLine < startLine || (endLine == startLine && endCol < startCol)) {
      std::swap(startLine, endLine); std::swap(startCol, endCol);
    }
    if (startLine == endLine) {
      lines_[static_cast<std::size_t>(startLine)].erase(static_cast<std::size_t>(startCol),
                                                        static_cast<std::size_t>(endCol - startCol));
    } else {
      std::string& first = lines_[static_cast<std::size_t>(startLine)];
      std::string& last = lines_[static_cast<std::size_t>(endLine)];
      first.erase(static_cast<std::size_t>(startCol));
      first += last.substr(static_cast<std::size_t>(endCol));
      lines_.erase(lines_.begin() + startLine + 1, lines_.begin() + endLine + 1);
    }
    caretLine_ = startLine;
    caretCol_ = startCol;
    selectionAnchorLine_ = caretLine_;
    selectionAnchorCol_ = caretCol_;
    dirty_ = true;
  }
  void insertText(const std::string& t) {
    if (hasSelection()) deleteSelection();
    std::string& l = lines_[static_cast<std::size_t>(caretLine_)];
    l.insert(static_cast<std::size_t>(caretCol_), t);
    caretCol_ += static_cast<int>(t.size());
    dirty_ = true;
  }
  void newline() {
    if (hasSelection()) deleteSelection();
    std::string& l = lines_[static_cast<std::size_t>(caretLine_)];
    std::string rest = l.substr(static_cast<std::size_t>(caretCol_));
    l.resize(static_cast<std::size_t>(caretCol_));
    lines_.insert(lines_.begin() + caretLine_ + 1, rest);
    ++caretLine_;
    caretCol_ = 0;
    dirty_ = true;
  }
  void backspace() {
    if (hasSelection()) { deleteSelection(); return; }
    if (caretCol_ > 0) {
      lines_[static_cast<std::size_t>(caretLine_)].erase(static_cast<std::size_t>(caretCol_ - 1), 1);
      --caretCol_;
    } else if (caretLine_ > 0) {
      const std::size_t prev = lines_[static_cast<std::size_t>(caretLine_ - 1)].size();
      lines_[static_cast<std::size_t>(caretLine_ - 1)] += lines_[static_cast<std::size_t>(caretLine_)];
      lines_.erase(lines_.begin() + caretLine_);
      --caretLine_;
      caretCol_ = static_cast<int>(prev);
    } else {
      return;
    }
    dirty_ = true;
  }
  void deleteForward() {
    if (hasSelection()) { deleteSelection(); return; }
    std::string& l = lines_[static_cast<std::size_t>(caretLine_)];
    if (static_cast<std::size_t>(caretCol_) < l.size()) {
      l.erase(static_cast<std::size_t>(caretCol_), 1);
    } else if (caretLine_ + 1 < static_cast<int>(lines_.size())) {
      l += lines_[static_cast<std::size_t>(caretLine_ + 1)];
      lines_.erase(lines_.begin() + caretLine_ + 1);
    } else {
      return;
    }
    dirty_ = true;
  }
  void moveLeft() {
    if (caretCol_ > 0) --caretCol_;
    else if (caretLine_ > 0) { --caretLine_; caretCol_ = static_cast<int>(lines_[static_cast<std::size_t>(caretLine_)].size()); }
  }
  void moveRight() {
    if (caretCol_ < static_cast<int>(lines_[static_cast<std::size_t>(caretLine_)].size())) ++caretCol_;
    else if (caretLine_ + 1 < static_cast<int>(lines_.size())) { ++caretLine_; caretCol_ = 0; }
  }
  void moveVertical(int delta, int visible) {
    (void)visible;
    caretLine_ = std::clamp(caretLine_ + delta, 0, static_cast<int>(lines_.size()) - 1);
    caretCol_ = std::min(caretCol_, static_cast<int>(lines_[static_cast<std::size_t>(caretLine_)].size()));
  }

  void rescan() {
    scanned_ = true;
    files_.clear();
    current_ = -1;
    lines_.clear();
    dirty_ = false;
    std::error_code ec;
    if (root_.empty() || !fs::is_directory(root_, ec)) return;
    fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      const fs::path p = it->path();
      std::error_code e2;
      if (it->is_directory(e2)) {
        if (skipDirectory(p.filename().string())) it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(e2) || !isCodeFile(p)) continue;
      files_.push_back({fs::relative(p, root_, e2).generic_string(), p});
      if (files_.size() >= kMaxFiles) break;
    }
    std::sort(files_.begin(), files_.end(), [](const Entry& a, const Entry& b) { return a.rel < b.rel; });
  }

  static bool skipDirectory(const std::string& name) {
    static const char* const kSkip[] = {".git", ".vs", ".vscode", "out", "build", "Build", "third_party", "node_modules"};
    if (!name.empty() && name[0] == '.') return true;
    return std::any_of(std::begin(kSkip), std::end(kSkip), [&](const char* s) { return name == s; });
  }

  static bool isCodeFile(const fs::path& p) {
    if (p.filename() == "CMakeLists.txt") return true;
    static const char* const kExt[] = {".cpp", ".cc", ".cxx", ".c", ".hpp", ".hh", ".hxx", ".h", ".inl", ".cmake",
                                       ".py", ".lua", ".glsl", ".hlsl", ".vert", ".frag", ".comp", ".sh", ".bat",
                                       ".ps1", ".js", ".ts", ".cs"};
    const std::string ext = p.extension().string();
    return std::any_of(std::begin(kExt), std::end(kExt), [&](const char* e) { return ext == e; });
  }

  // Abre um codigo: salva o atual (se mudou) e carrega o escolhido.
  void open(std::size_t index) {
    if (static_cast<int>(index) == current_) return;
    if (dirty_) save();
    std::ifstream f(files_[index].abs, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string raw = ss.str();
    crlf_ = raw.find("\r\n") != std::string::npos;
    lines_.clear();
    std::string cur;
    for (char c : raw) {
      if (c == '\r') continue;
      if (c == '\n') { lines_.push_back(cur); cur.clear(); continue; }
      cur.push_back(c);
    }
    lines_.push_back(cur);
    current_ = static_cast<int>(index);
    caretLine_ = caretCol_ = topLine_ = 0;
    dirty_ = false;
    editing_ = false;
  }

  void save() {
    if (current_ < 0) return;
    std::string out;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
      out += lines_[i];
      if (i + 1 < lines_.size()) out += crlf_ ? "\r\n" : "\n";
    }
    std::ofstream f(files_[static_cast<std::size_t>(current_)].abs, std::ios::binary | std::ios::trunc);
    if (!f) return;   // falha ao salvar: o texto continua na tela e o arquivo fica marcado
    f << out;
    dirty_ = false;
  }

  struct Entry {
    std::string rel;
    fs::path abs;
  };

  framed::Chrome chrome_;
  fs::path root_;
  bool scanned_ = false;
  std::vector<Entry> files_;
  int current_ = -1;
  std::vector<std::string> lines_;
  bool crlf_ = false;
  int caretLine_ = 0, caretCol_ = 0, topLine_ = 0;
  float listScroll_ = 0.f;
  bool dirty_ = false;
  bool editing_ = false;
  bool listShown_ = true;
  bool selecting_ = false;
  int selectionAnchorLine_ = 0, selectionAnchorCol_ = 0;
  float codeFont_ = kMinCodeFont;
  struct RectState { float x = 0.f, y = 0.f, w = 0.f, h = 0.f; } editorRect_{};
};

}  // namespace editor::coding
