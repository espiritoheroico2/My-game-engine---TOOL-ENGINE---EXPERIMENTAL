#pragma once

// Tela de inicio (criar ou abrir projeto), parte SEM desenho: estado dos campos, foco e acoes.
// Substitui o estado de project_dialog.hpp (ProjectDialog) para a versao SpriteUI. Nao usa SDL nem ImGui,
// assim pode ser testada sem janela (apps/editor/tests/test_start_screen_model.cpp).
//
// Regras:
//  - Cada campo e um SpriteUi::Widgets::TextEditState: o texto so muda por tecla, colagem ou corte.
//    Desenhar nunca altera o texto (regra R3 do ui_contract.hpp, "campo nao perde o texto").
//  - Foco: um campo por vez. Tab troca o foco entre campos. Enter cria (campos de criar) ou abre (campo de abrir).
//  - Copiar e cortar devolvem o texto em clipboardOut; o host grava na area de transferencia do sistema.
#include "SpriteUi/Widgets/TextEdit.hpp"
#include "project_settings.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace editor::start {

namespace Edit = ToolEngine::SpriteUi::Widgets;

enum class Field : std::size_t { Name = 0, Author = 1, Location = 2, OpenPath = 3, Count = 4 };
inline constexpr std::size_t kFieldCount = static_cast<std::size_t>(Field::Count);

enum class Key { Left, Right, Home, End, Backspace, Delete, Enter, Tab, Escape };

class Model {
 public:
  // lastProject: ultimo projeto aberto (vazio se nenhum). location: pasta sugerida para criar.
  Model(std::string lastProject, std::string location)
      : lastProject_{std::move(lastProject)} {
    fields_[idx(Field::Name)] = Edit::TextEditState{"Meu Jogo"};
    fields_[idx(Field::Location)] = Edit::TextEditState{std::move(location)};
    focus_ = Field::Name;
  }

  // Estado para desenho e testes.
  [[nodiscard]] const Edit::TextEditState& field(Field f) const { return fields_[idx(f)]; }
  [[nodiscard]] bool hasFocus(Field f) const { return focused_ && focus_ == f; }
  [[nodiscard]] bool focused() const { return focused_; }
  [[nodiscard]] Field focusField() const { return focus_; }
  [[nodiscard]] const std::string& error() const { return error_; }          // chave em ingles (use T())
  [[nodiscard]] const std::string& lastProject() const { return lastProject_; }
  [[nodiscard]] bool done() const { return done_; }
  [[nodiscard]] const fs::path& result() const { return result_; }
  [[nodiscard]] const std::string& clipboardOut() const { return clipboardOut_; }

  // Foco. Clicar num campo ou ir por Tab chama Focus.
  void Focus(Field f) {
    focus_ = f;
    focused_ = true;
  }
  void Blur() { focused_ = false; }

  // Texto. Cada um e uma operacao explicita (grava desfazer antes de mudar).
  void InsertText(std::string_view utf8) {
    if (!focused_) return;
    current().InsertText(utf8);
  }
  void Paste(std::string_view utf8) { InsertText(utf8); }
  void SelectAll() {
    if (focused_) current().SelectAll();
  }
  void Copy() {
    if (focused_) clipboardOut_ = current().Copy();
  }
  void Cut() {
    if (!focused_) return;
    clipboardOut_ = current().Cut();
  }
  void Undo() {
    if (focused_) (void)current().Undo();
  }
  // Ponteiro: move o caret para um offset de byte (vindo do layout). Shift estende a selecao.
  void SetCaret(std::size_t byteOffset, bool extend) {
    if (!focused_) return;
    current().MoveCaretTo(byteOffset, extend);
  }

  void Press(editor::start::Key key, bool shift) {
    switch (key) {
      case editor::start::Key::Left: if (focused_) current().MoveLeft(shift); break;
      case editor::start::Key::Right: if (focused_) current().MoveRight(shift); break;
      case editor::start::Key::Home: if (focused_) current().MoveHome(shift); break;
      case editor::start::Key::End: if (focused_) current().MoveEnd(shift); break;
      case editor::start::Key::Backspace: if (focused_) (void)current().Backspace(); break;
      case editor::start::Key::Delete: if (focused_) (void)current().Delete(); break;
      case editor::start::Key::Tab: NextField(shift ? -1 : 1); break;
      case editor::start::Key::Enter: Enter(); break;
      case editor::start::Key::Escape: Blur(); break;
    }
  }

  // Acoes. Retornam true quando o projeto foi criado ou aberto (done() passa a valer).
  bool CreateProject(const std::string& defaultLocation) {
    std::string err;
    std::string loc = field(Field::Location).Text();
    if (loc.empty()) loc = defaultLocation;
    const fs::path root = createProjectFolder(field(Field::Name).Text(), field(Field::Author).Text(), loc, err);
    if (root.empty()) {
      error_ = err;
      return false;
    }
    Finish(root);
    return true;
  }

  bool OpenPath() { return OpenFolder(field(Field::OpenPath).Text()); }

  bool OpenLast() {
    if (lastProject_.empty()) return false;
    return OpenFolder(lastProject_);
  }

  // Troca o texto de um campo (ex.: pasta escolhida no dialogo nativo). Limpa desfazer.
  void SetText(Field f, std::string text) { fields_[idx(f)] = Edit::TextEditState{std::move(text)}; }

  // Limpa o erro quando o usuario volta a mexer nos campos.
  void ClearError() { error_.clear(); }

 private:
  static constexpr std::size_t idx(Field f) { return static_cast<std::size_t>(f); }
  Edit::TextEditState& current() { return fields_[idx(focus_)]; }

  void NextField(int step) {
    const int n = static_cast<int>(kFieldCount);
    int i = static_cast<int>(idx(focus_));
    i = (i + step % n + n) % n;
    Focus(static_cast<Field>(i));
  }

  void Enter() {
    if (!focused_) return;
    if (focus_ == Field::OpenPath) {
      OpenPath();
    } else if (focus_ == Field::Location || focus_ == Field::Name || focus_ == Field::Author) {
      CreateProject(defaultLocation_);
    }
  }

  bool OpenFolder(const std::string& path) {
    std::string err;
    const fs::path root = openProjectFolder(path, err);
    if (root.empty()) {
      error_ = err;
      return false;
    }
    Finish(root);
    return true;
  }

  void Finish(const fs::path& root) {
    result_ = root;
    done_ = true;
    error_.clear();
  }

 public:
  // Pasta usada quando o campo de local esta vazio (o host define com defaultProjectsFolder).
  void SetDefaultLocation(std::string path) { defaultLocation_ = std::move(path); }

 private:
  std::array<Edit::TextEditState, kFieldCount> fields_{};
  Field focus_ = Field::Name;
  bool focused_ = true;
  std::string error_;
  std::string lastProject_;
  std::string clipboardOut_;
  std::string defaultLocation_;
  bool done_ = false;
  fs::path result_;
};

} // namespace editor::start
