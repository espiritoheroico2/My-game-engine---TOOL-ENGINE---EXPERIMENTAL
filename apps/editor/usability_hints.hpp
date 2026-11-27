#pragma once

// Regras puras de usabilidade das janelas do editor: textos de orientacao (estado vazio), motivo de um botao estar
// bloqueado e confirmacao em dois cliques para acoes que apagam dados. Sem SDL e sem SpriteUI; roda no ctest como
// Editor.UsabilityHints (tests/test_usability_hints.cpp).
//
// Os textos devolvidos sao as chaves em ingles: quem desenha passa por T(...) (assets/lang). Devolver nullptr
// significa "nada a dizer" (a acao e permitida ou ja ha conteudo).
//
// Exemplo: Hierarchy sem objetos -> HierarchyEmptyHint(0) pede para usar "+ Create"; Scenes com a cena atual
// selecionada -> SceneActionBlockedReason(SceneAct::Delete, "Main", "Main") explica que ela esta aberta.

#include <cstddef>
#include <string_view>

namespace editor::usability {

// ---------- Hierarchy ----------

// Orientacao da lista vazia (sem nenhum objeto na cena).
[[nodiscard]] inline const char* HierarchyEmptyHint(std::size_t rowCount) noexcept {
  return rowCount == 0 ? "The scene is empty. Click \"+ Create\" above to add an object." : nullptr;
}

// Quais botoes da barra do topo agem agora. Duplicar e Apagar precisam de um objeto selecionado.
struct HierarchyToolbar {
  bool create = true;
  bool duplicate = false;
  bool remove = false;
};

[[nodiscard]] inline HierarchyToolbar HierarchyToolbarState(bool hasSelection) noexcept {
  return {true, hasSelection, hasSelection};
}

// ---------- Inspector ----------

// Texto do Inspector sem objeto selecionado: diz o que fazer, em vez de so constatar o vazio.
[[nodiscard]] inline const char* InspectorEmptyText() noexcept {
  return "No object selected. Click an object in the Hierarchy or in the Scene view to edit it here.";
}

// ---------- Scenes ----------

enum class SceneAct : unsigned char { Open, Rename, Delete };

// Motivo de a acao sobre a cena selecionada estar bloqueada; nullptr quando ela e permitida.
[[nodiscard]] inline const char* SceneActionBlockedReason(SceneAct act, std::string_view selected,
                                                          std::string_view current) noexcept {
  if (selected.empty()) return "Select a scene in the list first.";
  const bool isCurrent = selected == current;
  if (act == SceneAct::Open && isCurrent) return "This scene is already open.";
  if (act == SceneAct::Delete && isCurrent) return "Cannot delete the open scene. Open another scene first.";
  return nullptr;
}

// Lista de cenas sem nenhuma: orienta a criar.
[[nodiscard]] inline const char* ScenesEmptyHint(std::size_t sceneCount) noexcept {
  return sceneCount == 0 ? "No scenes yet. Click \"New scene...\" to create one." : nullptr;
}

// ---------- Export ----------

// Por que o botao "Export game" esta bloqueado; nullptr quando pode exportar. Os campos do Editor info faltando
// ja tem aviso proprio na janela, entao aqui so o caso da plataforma sem suporte (antes o botao ficava apagado
// sem explicacao).
[[nodiscard]] inline const char* ExportBlockedReason(bool missingInfo, bool platformAvailable) noexcept {
  if (!platformAvailable) return "This build cannot export this platform. Pick the one that matches your own system.";
  if (missingInfo) return "Fill in the required Editor info fields first.";
  return nullptr;
}

// ---------- Confirmacao em dois cliques ----------

// Acao destrutiva sem janela de confirmacao: o primeiro clique "arma" o botao (o texto muda para pedir confirmacao);
// o segundo clique no MESMO botao executa. Clicar em qualquer outro lugar desarma.
class ConfirmArm {
 public:
  static constexpr int kNone = -1;

  // Chamado a cada clique de botao com a identidade dele. true = confirmado (executar). false = armou agora.
  [[nodiscard]] bool Click(int key) noexcept {
    if (key != kNone && key == armed_) {
      armed_ = kNone;
      return true;
    }
    armed_ = key;
    return false;
  }

  // Clique em outro controle (ou fora de qualquer botao): desarma se nao for o botao armado.
  void Touch(int key) noexcept {
    if (key != armed_) armed_ = kNone;
  }

  void Cancel() noexcept { armed_ = kNone; }
  [[nodiscard]] bool Armed(int key) const noexcept { return key != kNone && key == armed_; }

 private:
  int armed_ = kNone;
};

}  // namespace editor::usability
