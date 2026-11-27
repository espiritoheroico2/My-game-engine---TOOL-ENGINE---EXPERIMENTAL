// Regras puras de usabilidade (usability_hints.hpp): estado vazio da Hierarchy e do Inspector, botoes bloqueados,
// motivo de acoes bloqueadas em Scenes e Export, e confirmacao em dois cliques. Sem janela.
// Roda como Editor.UsabilityHints.
#include "usability_hints.hpp"

#include <cstdio>
#include <cstring>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace U = editor::usability;

  // 1. Hierarchy: lista vazia orienta; com linhas nao diz nada.
  Check(U::HierarchyEmptyHint(0) != nullptr, "Hierarchy vazia mostra orientacao");
  Check(U::HierarchyEmptyHint(0) && std::strstr(U::HierarchyEmptyHint(0), "+ Create"), "orientacao cita o botao + Create");
  Check(U::HierarchyEmptyHint(1) == nullptr, "Hierarchy com objeto nao mostra orientacao");

  // 2. Barra: Create sempre age; Duplicate e Delete so com selecao.
  const auto noSel = U::HierarchyToolbarState(false);
  Check(noSel.create && !noSel.duplicate && !noSel.remove, "sem selecao: so Create");
  const auto sel = U::HierarchyToolbarState(true);
  Check(sel.create && sel.duplicate && sel.remove, "com selecao: todos agem");

  // 3. Inspector sem selecao: diz onde clicar.
  Check(std::strstr(U::InspectorEmptyText(), "Hierarchy") != nullptr, "Inspector vazio cita a Hierarchy");

  // 4. Scenes: motivo por acao.
  using A = U::SceneAct;
  Check(U::SceneActionBlockedReason(A::Open, "", "Main") != nullptr, "sem selecao: Open bloqueado com motivo");
  Check(U::SceneActionBlockedReason(A::Rename, "", "Main") != nullptr, "sem selecao: Rename bloqueado com motivo");
  Check(U::SceneActionBlockedReason(A::Open, "Main", "Main") != nullptr, "abrir a cena atual e bloqueado");
  Check(U::SceneActionBlockedReason(A::Delete, "Main", "Main") != nullptr, "apagar a cena atual e bloqueado");
  Check(U::SceneActionBlockedReason(A::Rename, "Main", "Main") == nullptr, "renomear a cena atual e permitido");
  Check(U::SceneActionBlockedReason(A::Open, "Alpha", "Main") == nullptr, "abrir outra cena e permitido");
  Check(U::SceneActionBlockedReason(A::Delete, "Alpha", "Main") == nullptr, "apagar outra cena e permitido");
  Check(U::ScenesEmptyHint(0) != nullptr && U::ScenesEmptyHint(2) == nullptr, "dica de lista de cenas vazia so sem cenas");

  // 5. Export: motivo do botao apagado.
  Check(U::ExportBlockedReason(false, true) == nullptr, "export liberado");
  Check(U::ExportBlockedReason(false, false) != nullptr, "plataforma indisponivel explica");
  Check(U::ExportBlockedReason(true, true) != nullptr, "info faltando explica");

  // 6. Confirmacao em dois cliques.
  U::ConfirmArm arm;
  Check(!arm.Armed(5), "comeca desarmado");
  Check(!arm.Click(5) && arm.Armed(5), "primeiro clique arma e nao executa");
  Check(arm.Click(5) && !arm.Armed(5), "segundo clique no mesmo botao executa e desarma");
  Check(!arm.Click(5), "depois de executar, o proximo clique arma de novo");
  arm.Touch(7);
  Check(!arm.Armed(5), "clicar em outro controle desarma");
  Check(!arm.Click(5), "apos desarmar, o clique volta a armar (nao executa)");
  Check(!arm.Click(6) && !arm.Click(5), "armar um botao e clicar noutro nao executa nenhum");
  arm.Click(8);
  arm.Cancel();
  Check(!arm.Click(8), "Cancel desarma");
  arm.Touch(9);
  Check(!arm.Armed(8), "Touch no outro botao tira a armacao");
  Check(!arm.Armed(U::ConfirmArm::kNone), "kNone nunca fica armado");

  std::printf("%s\n", gFailures == 0 ? "TUDO OK" : "HA FALHAS");
  return gFailures == 0 ? 0 : 1;
}
