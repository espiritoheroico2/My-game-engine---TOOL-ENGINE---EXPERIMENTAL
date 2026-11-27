#pragma once

// Modelo dos elementos de UI do Canvas na Hierarchy: as linhas filhas do Canvas, a selecao por indice e as
// operacoes sobre a lista de elementos (adicionar, renomear, duplicar, mover, apagar). Sem janela e sem SDL.
//
// As linhas dos elementos sao linhas de ACESSO: nao sao objetos da cena (nao contam em "entities" e nao aparecem
// na Scene). Cada uma tem um id de linha com o bit 63 ligado, que leva o canvas e o indice do elemento.
//
// Exemplo: o Canvas "HUD" tem [Text "Score", Button "Play"]. Apagar o Text (indice 0) deixa o Button no indice 0; se o
// Button estava selecionado (indice 1), a selecao passa para o indice 0. Apagar o proprio selecionado limpa a selecao.
//
// O nome do elemento e o campo name (separado do text, que e o que o Text e o Button mostram). Quando o name esta
// vazio, a linha mostra o nome do tipo.
//
// Roda no ctest como Editor.CanvasElems.

#include "components.hpp"
#include "hierarchy_model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace editor::canvas_elems {

namespace hm = editor::hierarchy_model;

static_assert(hm::kElemKindCount == static_cast<int>(UiKind::Count), "lista de nomes de tipos fora de sincronia com UiKind");

// Selecao de elemento: indice na lista do canvas, ou -1 (nenhum).
inline constexpr int kNone = -1;

// Mascara da parte do id do canvas guardada no id de linha (indice e 16 bits da geracao).
inline constexpr std::uint64_t kCanvasMask = 0x0000FFFFFFFFFFFFull;

// Nome do tipo (Text, Button...). Tipo fora da faixa vira "Text".
[[nodiscard]] inline const char* KindName(int kind) {
  return hm::kElemKindNames[(kind >= 0 && kind < hm::kElemKindCount) ? kind : 0];
}

// Nome mostrado do elemento: o name ou, se vazio, o nome do tipo.
[[nodiscard]] inline std::string DisplayName(const UiElem& e) {
  std::string n(e.name);
  return n.empty() ? std::string(KindName(e.kind)) : n;
}

// Rotulo da linha: "nome  (Tipo)". Quando o nome e o proprio nome do tipo, so o tipo aparece uma vez.
[[nodiscard]] inline std::string RowLabel(const UiElem& e) {
  const std::string name = DisplayName(e);
  const std::string kind = KindName(e.kind);
  return name == kind ? kind : name + "  (" + kind + ")";
}

// Id de linha do elemento: bit 63 + indice (bits 48..55) + canvas (bits 0..47 da chave do objeto).
[[nodiscard]] inline hm::Id RowId(hm::Id canvasKey, int index) {
  return hm::kElemBit | (static_cast<hm::Id>(index & 0xFF) << 48) | (canvasKey & kCanvasMask);
}
// Indice do elemento num id de linha.
[[nodiscard]] inline int IndexOfRow(hm::Id id) { return static_cast<int>((id >> 48) & 0xFF); }
// Parte do canvas (48 bits) num id de linha; compare com (chave do objeto & kCanvasMask).
[[nodiscard]] inline hm::Id CanvasPartOfRow(hm::Id id) { return id & kCanvasMask; }

// Acrescenta as linhas dos elementos do canvas (pai = canvasKey) a lista de itens da Hierarchy.
inline void AppendRows(std::vector<hm::Item>& items, hm::Id canvasKey, const CanvasComp& c) {
  for (int i = 0; i < c.count && i < kMaxUi; ++i) {
    hm::Item item;
    item.id = RowId(canvasKey, i);
    item.parent = canvasKey;
    item.label = RowLabel(c.items[i]);
    item.kind = hm::Kind::Element;
    item.editText = c.items[i].name;
    items.push_back(std::move(item));
  }
}

// Indice valido na lista do canvas?
[[nodiscard]] inline bool ValidIndex(const CanvasComp& c, int i) { return i >= 0 && i < c.count && i < kMaxUi; }

// Novo elemento do tipo dado, no fim da lista: ids e ordem Z novos e os valores iniciais do tipo (initUiKind).
// Posicao e cor ficam nos padroes de UiElem. Devolve o indice, ou kNone se a lista esta cheia ou o tipo e invalido.
// A selecao passa para o novo elemento.
inline int Add(CanvasComp& c, int kind, int& selected) {
  if (c.count >= kMaxUi || kind < 0 || kind >= static_cast<int>(UiKind::Count)) return kNone;
  UiElem& e = c.items[c.count];
  e = UiElem{};
  stampNew(e, c);   // usa c.count para o Z: o elemento ainda nao conta, entao fica acima dos existentes
  initUiKind(e, kind);
  if (kind == static_cast<int>(UiKind::Text)) std::snprintf(e.text, sizeof e.text, "%s", "Text");
  else if (kind == static_cast<int>(UiKind::Button)) {
    std::snprintf(e.text, sizeof e.text, "%s", "Button");
    e.w = 0.16f; e.h = 0.06f; e.radius = 6.f; e.color[0] = 0.9f; e.color[1] = 0.8f; e.color[2] = 0.3f;
  } else if (kind == static_cast<int>(UiKind::Panel)) {
    e.radius = 4.f; e.color[0] = e.color[1] = e.color[2] = 0.1f; e.color[3] = 0.6f; e.text[0] = 0;
  } else if (kind == static_cast<int>(UiKind::Bar)) {
    e.w = 0.3f; e.h = 0.03f; e.color[1] = 0.8f; e.color[2] = 0.2f; e.text[0] = 0;
  }
  selected = c.count;
  return c.count++;
}

// Renomeia (grava o name, nao o text). Nome vazio ou indice invalido: nada muda e devolve false.
inline bool Rename(CanvasComp& c, int i, const std::string& name) {
  if (!ValidIndex(c, i) || name.empty()) return false;
  std::snprintf(c.items[i].name, sizeof c.items[i].name, "%s", name.c_str());
  return true;
}

// Duplica o elemento i: a copia entra logo depois, com id novo e ordem Z acima de todos. A selecao vai para a copia.
// Devolve o indice da copia, ou kNone (indice invalido ou lista cheia).
inline int Duplicate(CanvasComp& c, int i, int& selected) {
  if (!ValidIndex(c, i) || c.count >= kMaxUi) return kNone;
  for (int k = c.count; k > i + 1; --k) c.items[k] = c.items[k - 1];
  c.items[i + 1] = c.items[i];
  ++c.count;
  stampNew(c.items[i + 1], c);
  selected = i + 1;
  return i + 1;
}

// Move o elemento uma posicao (dir = -1 sobe, +1 desce). So troca a ordem da lista (o Z nao muda). A selecao
// acompanha: o elemento movido e o vizinho trocado. Devolve false se nao deu (fim da lista ou indice invalido).
inline bool Move(CanvasComp& c, int i, int dir, int& selected) {
  const int j = i + dir;
  if ((dir != -1 && dir != 1) || !ValidIndex(c, i) || !ValidIndex(c, j)) return false;
  std::swap(c.items[i], c.items[j]);
  if (selected == i) selected = j;
  else if (selected == j) selected = i;
  return true;
}

// Apaga o elemento i e desloca os seguintes. Selecao: o apagado limpa; um indice maior desce uma posicao.
inline bool Delete(CanvasComp& c, int i, int& selected) {
  if (!ValidIndex(c, i)) return false;
  for (int k = i; k + 1 < c.count; ++k) c.items[k] = c.items[k + 1];
  --c.count;
  c.items[c.count] = UiElem{};
  if (selected == i) selected = kNone;
  else if (selected > i) --selected;
  return true;
}

// ---- Lista de entradas do elemento (Menu, List, List container, rotulos do Inventory) ----

// Acrescenta uma entrada "Item N" no fim. Falso se a lista esta cheia.
inline bool AddEntry(UiElem& e) {
  if (e.entryCount < 0 || e.entryCount >= kMaxUiEntries) return false;
  std::snprintf(e.entries[e.entryCount], kUiEntryLen, "Item %d", e.entryCount + 1);
  ++e.entryCount;
  return true;
}

// Tira a entrada i e desloca as seguintes. Falso se o indice e invalido.
inline bool RemoveEntry(UiElem& e, int i) {
  if (i < 0 || i >= e.entryCount || e.entryCount > kMaxUiEntries) return false;
  for (int k = i; k + 1 < e.entryCount; ++k) std::snprintf(e.entries[k], kUiEntryLen, "%s", e.entries[k + 1]);
  --e.entryCount;
  e.entries[e.entryCount][0] = 0;
  return true;
}

// Troca a entrada i com a vizinha (dir = -1 sobe, +1 desce). Falso no fim da lista ou com indice invalido.
inline bool MoveEntry(UiElem& e, int i, int dir) {
  const int j = i + dir;
  if ((dir != -1 && dir != 1) || i < 0 || j < 0 || i >= e.entryCount || j >= e.entryCount) return false;
  char tmp[kUiEntryLen];
  std::snprintf(tmp, kUiEntryLen, "%s", e.entries[i]);
  std::snprintf(e.entries[i], kUiEntryLen, "%s", e.entries[j]);
  std::snprintf(e.entries[j], kUiEntryLen, "%s", tmp);
  return true;
}

}  // namespace editor::canvas_elems
