// Teste do modelo dos elementos de UI do Canvas na Hierarchy: linhas filhas (quantidade, rotulos, id de linha e indice),
// renomear, duplicar, mover, apagar (com o indice selecionado depois de cada uma), valores iniciais ao adicionar e a
// lista de entradas.
#include "canvas_elems_model.hpp"
#include "hierarchy_model.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;

// Confere uma condicao e conta a falha.
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FALHOU: %s\n", what); ++g_fail; }
}

// Nome do elemento i (campo name, gravado pelo Rename).
static std::string nm(const editor::CanvasComp& c, int i) { return c.items[i].name; }

int main() {
  using namespace editor;
  namespace ce = editor::canvas_elems;
  namespace hm = editor::hierarchy_model;

  // --- linhas filhas: quantidade, rotulo, tipo, pai e mapeamento id <-> indice
  CanvasComp c;
  int sel = ce::kNone;
  check(ce::Add(c, static_cast<int>(UiKind::Text), sel) == 0 && sel == 0, "primeiro Add devolve 0 e seleciona");
  check(ce::Add(c, static_cast<int>(UiKind::Panel), sel) == 1 && sel == 1, "segundo Add devolve 1");
  check(ce::Add(c, static_cast<int>(UiKind::Button), sel) == 2 && sel == 2, "terceiro Add devolve 2");
  const hm::Id canvasKey = (hm::Id{3} << 32) | 7u;
  std::vector<hm::Item> items;
  ce::AppendRows(items, canvasKey, c);
  check(items.size() == 3, "tres linhas filhas");
  check(items[0].kind == hm::Kind::Element && items[0].parent == canvasKey, "linha e de acesso e filha do canvas");
  check(hm::IsElemId(items[0].id) && !hm::IsElemId(canvasKey), "bit de linha so nas linhas de elemento");
  check(items[0].label == "Text", "rotulo do Text: so o tipo (nome = tipo)");
  check(items[1].label == "Panel", "rotulo do Panel sem texto: nome do tipo");
  check(items[2].label == "Button", "rotulo do Button");
  for (int i = 0; i < 3; ++i) {
    check(ce::IndexOfRow(items[static_cast<size_t>(i)].id) == i, "indice sai do id de linha");
    check(ce::CanvasPartOfRow(items[static_cast<size_t>(i)].id) == (canvasKey & ce::kCanvasMask), "canvas sai do id de linha");
  }
  check(items[0].id != items[1].id, "ids de linha distintos");
  check(ce::Rename(c, 0, "Score"), "renomear");
  items.clear();
  ce::AppendRows(items, canvasKey, c);
  check(items[0].label == "Score  (Text)", "rotulo com nome e tipo");
  check(!ce::Rename(c, 0, ""), "nome vazio recusado");
  check(!ce::Rename(c, 9, "x"), "indice invalido recusado");
  check(nm(c, 0) == "Score", "nome fica como estava");
  hm::HierarchyModel model;
  std::vector<hm::Item> all;
  hm::Item cv; cv.id = canvasKey; cv.label = "HUD"; cv.kind = hm::Kind::Canvas;
  all.push_back(cv);
  ce::AppendRows(all, canvasKey, c);
  model.Sync(all);
  check(model.RowCount() == 4, "modelo: canvas + 3 linhas");
  check(model.DepthOfRow(1) == 1 && model.IdOfRow(1) == ce::RowId(canvasKey, 0), "linha do elemento 0 e filha do canvas");
  check(!model.CanReparent(ce::RowId(canvasKey, 0), canvasKey), "linha de elemento nao vira filha de objeto");
  check(!model.CanReparent(canvasKey, ce::RowId(canvasKey, 0)), "objeto nao vira filho de linha de elemento");
  (void)model.Select(ce::RowId(canvasKey, 2));
  check(model.Selected() == ce::RowId(canvasKey, 2) && ce::IndexOfRow(model.Selected()) == 2, "selecao da linha mapeia no indice 2");

  // --- valores iniciais: initUiKind aplicado, id unico e Z crescente
  CanvasComp d;
  int s2 = ce::kNone;
  for (int k = 0; k < static_cast<int>(UiKind::Count); ++k) {
    check(ce::Add(d, k, s2) == k, "Add de cada tipo");
    UiElem ref;
    initUiKind(ref, k);
    const UiElem& e = d.items[k];
    check(e.kind == k, "tipo gravado");
    if (k == static_cast<int>(UiKind::Image) || k == static_cast<int>(UiKind::Window) || k == static_cast<int>(UiKind::Menu) ||
        k == static_cast<int>(UiKind::List) || k == static_cast<int>(UiKind::Inventory) || k == static_cast<int>(UiKind::ListContainer)) {
      check(e.w == ref.w && e.h == ref.h && e.radius == ref.radius, "tamanho e raio do initUiKind");
      check(e.entryCount == ref.entryCount, "entradas do initUiKind");
    }
    check(e.id[0] != 0, "id gerado");
    check(e.z == k, "Z acima dos anteriores");
  }
  check(d.items[static_cast<int>(UiKind::Menu)].entryCount == 3, "Menu nasce com 3 entradas");
  check(d.items[static_cast<int>(UiKind::ListContainer)].entryCount == 8, "List container nasce com 8 linhas");
  check(d.items[static_cast<int>(UiKind::Inventory)].rows == 3 && d.items[static_cast<int>(UiKind::Inventory)].cols == 4, "Inventory 3x4");
  check(std::strcmp(d.items[static_cast<int>(UiKind::Window)].text, "Window") == 0, "Window com titulo");
  check(std::strcmp(d.items[0].id, d.items[1].id) != 0, "ids diferentes");
  check(ce::Add(d, -1, s2) == ce::kNone && ce::Add(d, 99, s2) == ce::kNone, "tipo invalido recusado");
  while (d.count < kMaxUi) (void)ce::Add(d, 0, s2);
  check(ce::Add(d, 0, s2) == ce::kNone, "lista cheia recusa");
  check(ce::RowLabel(d.items[static_cast<int>(UiKind::Menu)]) == "Menu", "Menu sem texto: so o tipo");

  // --- duplicar
  CanvasComp e;
  int s = ce::kNone;
  (void)ce::Add(e, 0, s); (void)ce::Add(e, 2, s); (void)ce::Add(e, 3, s);
  ce::Rename(e, 0, "A"); ce::Rename(e, 1, "B"); ce::Rename(e, 2, "C");
  s = 1;
  check(ce::Duplicate(e, 1, s) == 2 && e.count == 4, "duplicar B entra logo depois");
  check(nm(e, 0) == "A" && nm(e, 1) == "B" && nm(e, 2) == "B" && nm(e, 3) == "C", "ordem depois de duplicar");
  check(s == 2, "selecao vai para a copia");
  check(std::strcmp(e.items[1].id, e.items[2].id) != 0, "copia tem id novo");
  check(e.items[2].z > e.items[3].z, "copia fica por cima (Z maior)");
  check(ce::Duplicate(e, 7, s) == ce::kNone, "duplicar indice invalido");

  // --- mover
  CanvasComp m;
  int ms = ce::kNone;
  (void)ce::Add(m, 0, ms); (void)ce::Add(m, 0, ms); (void)ce::Add(m, 0, ms);
  ce::Rename(m, 0, "A"); ce::Rename(m, 1, "B"); ce::Rename(m, 2, "C");
  ms = 1;
  check(ce::Move(m, 1, -1, ms) && nm(m, 0) == "B" && nm(m, 1) == "A" && ms == 0, "subir B: selecao segue o B");
  check(!ce::Move(m, 0, -1, ms) && ms == 0, "subir o primeiro nao faz nada");
  ms = 0;
  check(ce::Move(m, 1, 1, ms) && nm(m, 1) == "C" && nm(m, 2) == "A" && ms == 0, "descer A (nao selecionado): selecao fica");
  ms = 1;
  check(ce::Move(m, 0, 1, ms) && nm(m, 0) == "C" && nm(m, 1) == "B" && ms == 0, "vizinho trocado leva a selecao junto");
  ms = 2;
  check(!ce::Move(m, 2, 1, ms) && ms == 2, "descer o ultimo nao faz nada");

  // --- apagar: indice selecionado depois
  CanvasComp x;
  int xs = ce::kNone;
  for (int i = 0; i < 4; ++i) (void)ce::Add(x, 0, xs);
  ce::Rename(x, 0, "A"); ce::Rename(x, 1, "B"); ce::Rename(x, 2, "C"); ce::Rename(x, 3, "D");
  xs = 3;
  check(ce::Delete(x, 1, xs) && x.count == 3 && xs == 2 && nm(x, 2) == "D", "apagar antes do selecionado: indice desce");
  xs = 0;
  check(ce::Delete(x, 2, xs) && x.count == 2 && xs == 0, "apagar depois do selecionado: indice igual");
  xs = 1;
  check(ce::Delete(x, 1, xs) && x.count == 1 && xs == ce::kNone, "apagar o selecionado limpa a selecao");
  xs = ce::kNone;
  check(ce::Delete(x, 0, xs) && x.count == 0 && xs == ce::kNone, "apagar o ultimo deixa a lista vazia");
  check(!ce::Delete(x, 0, xs), "apagar em lista vazia");

  // --- entradas
  UiElem list;
  initUiKind(list, static_cast<int>(UiKind::List));
  check(list.entryCount == 3, "List nasce com 3 entradas");
  check(ce::AddEntry(list) && list.entryCount == 4 && std::strcmp(list.entries[3], "Item 4") == 0, "adicionar entrada");
  check(ce::MoveEntry(list, 0, 1) && std::strcmp(list.entries[0], "Item 2") == 0 && std::strcmp(list.entries[1], "Item 1") == 0, "mover entrada");
  check(!ce::MoveEntry(list, 0, -1) && !ce::MoveEntry(list, 3, 1), "mover alem das pontas");
  check(ce::RemoveEntry(list, 0) && list.entryCount == 3 && std::strcmp(list.entries[0], "Item 1") == 0, "remover entrada");
  while (list.entryCount < kMaxUiEntries) (void)ce::AddEntry(list);
  check(!ce::AddEntry(list), "lista de entradas cheia");

  // --- salvar: a lista alterada vai para o JSON (rename e delete aparecem)
  CanvasComp sv;
  int ss = ce::kNone;
  (void)ce::Add(sv, 0, ss); (void)ce::Add(sv, 2, ss);
  ce::Rename(sv, 0, "Primeiro"); ce::Rename(sv, 1, "Segundo");
  ce::Delete(sv, 0, ss);
  const tool::Json js = itemsJson(sv);
  check(js.items().size() == 1 && js.items()[0]["name"].asString() == "Segundo", "JSON reflete apagar e renomear");

  // --- nome separado do texto: rename grava name e deixa text; salvar e carregar mantem name; arquivo antigo sem name
  CanvasComp nt;
  int ns = ce::kNone;
  (void)ce::Add(nt, 0, ns);
  std::snprintf(nt.items[0].text, sizeof nt.items[0].text, "%s", "Ola");
  check(ce::Rename(nt, 0, "Titulo"), "rename do Text");
  check(std::strcmp(nt.items[0].name, "Titulo") == 0 && std::strcmp(nt.items[0].text, "Ola") == 0, "rename grava name e deixa text");
  (void)ce::Add(nt, 1, ns);
  check(std::strcmp(nt.items[1].name, "") == 0, "elemento novo nasce sem nome");
  const tool::Json saved = itemsJson(nt);
  CanvasComp back;
  readItems(saved, back);
  check(back.count == 2 && std::strcmp(back.items[0].name, "Titulo") == 0 && std::strcmp(back.items[0].text, "Ola") == 0,
        "salvar e carregar mantem name e text");
  check(std::strcmp(back.items[1].name, "") == 0, "Panel sem nome continua sem nome depois de carregar");
  tool::Json oldArr = tool::Json::array();
  tool::Json oldItem = tool::Json::object();
  oldItem.set("kind", 0); oldItem.set("text", std::string("Antigo"));
  oldArr.push(oldItem);
  CanvasComp oldc;
  readItems(oldArr, oldc);
  check(oldc.count == 1 && std::strcmp(oldc.items[0].name, "") == 0, "arquivo antigo sem name carrega com nome vazio");
  check(ce::DisplayName(oldc.items[0]) == "Text", "sem nome: mostra o nome do tipo");

  if (g_fail == 0) std::printf("OK\n");
  return g_fail == 0 ? 0 : 1;
}
