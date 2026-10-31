// ToolEngine Editor - Desfazer/Refazer (Ctrl+Z / Ctrl+Y ou Ctrl+Shift+Z).
//
// COMO FUNCIONA (simples de proposito)
//   Cada "estado" e a cena inteira serializada em JSON (o mesmo formato do arquivo .scene.json, so as entidades).
//   O editor chama `tick(interagindo)` todo quadro: quando o usuario TERMINA uma interacao (soltou o mouse, saiu de um campo
//   do Inspector) o estado atual e comparado com o ultimo guardado e, se mudou, vira um passo de historico.
//   Vantagens: cobre gizmo, Inspector, hierarquia, criar/duplicar/apagar sem codigo especifico para cada um, e e imune a
//   "esqueci de registrar o comando". Custo: 1 serializacao por interacao (a cena do editor e pequena) - nada por quadro parado.
//   Limites: 100 passos e ~16 MB no total (os mais antigos saem primeiro).
#pragma once
#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace editor {

struct UndoState { std::string data; int selected = -1; }; // selected = indice da entidade selecionada na ordem de serializacao

class UndoStack {
 public:
  static constexpr size_t kMaxSteps = 100;
  static constexpr size_t kMaxBytes = 16u << 20;

  // Define o estado atual SEM criar passo (ao abrir/criar cena, parar o Play). Limpa o historico.
  void reset(UndoState s) { undo_.clear(); redo_.clear(); cur_ = std::move(s); bytes_ = cur_.data.size(); }

  // Registra `now` como novo estado se for diferente do atual. Retorna true se criou um passo.
  bool commit(UndoState now) {
    if (now.data == cur_.data) { cur_.selected = now.selected; return false; } // so a selecao mudou: nao e passo
    bytes_ += now.data.size();
    undo_.push_back(std::move(cur_)); cur_ = std::move(now); redo_.clear();
    trim();
    return true;
  }
  // Troca o estado atual SEM criar passo: usado logo apos restaurar um estado, para que ler a cena de volta
  // (que pode serializar um pouco diferente, ex. caminho de script) nao seja confundido com uma edicao.
  void rebase(UndoState s) { bytes_ += s.data.size(); cur_ = std::move(s); }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  size_t undoCount() const { return undo_.size(); }
  size_t redoCount() const { return redo_.size(); }
  const UndoState& current() const { return cur_; }

  // Volta um passo; devolve o estado que deve ser carregado na cena (ou nullptr).
  const UndoState* undo() {
    if (undo_.empty()) return nullptr;
    redo_.push_back(std::move(cur_)); cur_ = std::move(undo_.back()); undo_.pop_back(); return &cur_;
  }
  const UndoState* redo() {
    if (redo_.empty()) return nullptr;
    undo_.push_back(std::move(cur_)); cur_ = std::move(redo_.back()); redo_.pop_back(); return &cur_;
  }

 private:
  void trim() {
    while (undo_.size() > kMaxSteps || (bytes_ > kMaxBytes && !undo_.empty())) {
      bytes_ -= std::min(bytes_, undo_.front().data.size()); undo_.pop_front();
    }
  }
  std::deque<UndoState> undo_, redo_;
  UndoState cur_;
  size_t bytes_ = 0;
};

} // namespace editor
