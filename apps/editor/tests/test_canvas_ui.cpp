// Testes sem janela da API nativa do Canvas (canvas_ui.hpp): ids, ordem Z, eventos e clique. Roda no ctest como Editor.CanvasUi.
#include "canvas_ui.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace cu = editor::canvas_ui;
  namespace SE = ToolEngine::SpriteUi::Elements;
  using SC_Rect = ToolEngine::SpriteUi::Rect;
  using SC_Vec2 = ToolEngine::SpriteUi::Vec2;
  const SC_Rect area{{0.f, 0.f}, {200.f, 200.f}};

  // Ids unicos e no formato "e...".
  {
    cu::Canvas c;
    const std::string a = c.create(cu::Kind::Text, "A", 0.1f, 0.1f, 1);
    const std::string b = c.create(cu::Kind::Button, "B", 0.5f, 0.5f, 2);
    const std::string d = c.create(cu::Kind::Text, "C", 0.9f, 0.9f, 3);
    Check(a != b && b != d && a != d, "ids dos elementos sao distintos");
    Check(!a.empty() && a[0] == 'e', "id no formato e...");
    Check(c.find(b) && c.find(b)->region == 2, "regiao gravada no elemento");
    Check(c.find(b)->z > c.find(a)->z, "elemento novo entra por cima (z maior)");

    // Remover um do meio nao perde os outros (troca com o ultimo).
    Check(c.destroy(b), "destroy de id existente");
    Check(c.find(b) == nullptr, "id apagado nao existe mais");
    Check(c.find(a) && c.find(d) && c.size() == 2, "os outros elementos continuam achaveis");
    Check(!c.destroy(b), "destroy de id ja apagado: falso");
    const std::string e2 = c.create(cu::Kind::Text, "D", 0.2f, 0.2f, 1);
    Check(e2 != a && e2 != d && e2 != b, "id apagado nao e reaproveitado");

    Check(c.bringToFront(a) && c.find(a)->z > c.find(d)->z, "bringToFront poe o elemento por cima");
    Check(!c.bringToFront("nao-existe"), "bringToFront de id desconhecido: falso");
  }

  // Evento estilo C#: Add, Trigger chama todos, Remove tira um.
  {
    cu::Event<int> ev;
    std::vector<int> calls;
    const std::uint32_t s1 = ev.Add([&calls](int v) { calls.push_back(v * 10); });
    ev.Add([&calls](int v) { calls.push_back(v * 100); });
    ev.Trigger(1);
    Check(calls.size() == 2 && calls[0] == 10 && calls[1] == 100, "Trigger chama todas as funcoes, na ordem");
    calls.clear();
    Check(ev.Remove(s1), "Remove pela inscricao");
    ev.Trigger(2);
    Check(calls.size() == 1 && calls[0] == 200, "depois do Remove so a outra roda");
    Check(!ev.Remove(s1), "Remove da mesma inscricao de novo: falso");
    Check(ev.Count() == 1, "contagem de inscricoes");
  }

  // Remover durante o disparo nao quebra o laco; a funcao removida nao roda de novo.
  {
    cu::Event<> ev;
    int a = 0, b = 0;
    const std::uint32_t idB = ev.Add([&] { ++b; });
    ev.Add([&] { ++a; ev.Remove(idB); });   // remove a outra durante o disparo
    ev.Trigger();   // b roda (1), a remove b
    ev.Trigger();   // b nao roda mais
    Check(a == 2 && b == 1, "remover durante o disparo: a funcao roda uma vez antes de sair e nao roda mais");
  }

  // Recursao: evento que dispara ele mesmo para no limite.
  {
    cu::Event<> ev;
    int depth = 0;
    ev.Add([&] { ++depth; ev.Trigger(); });
    ev.Trigger();
    Check(depth == cu::Event<>::kMaxDepth, "recursao para no limite de profundidade");
  }

  // Clique: Down e Up dentro do botao disparam Pressed uma vez, com o id como sender.
  {
    cu::Canvas c;
    const std::string btn = c.create(cu::Kind::Button, "Save", 0.5f, 0.5f, 5);   // caixa em (100,100)-(220,140)
    int fired = 0;
    std::string sender;
    c.find(btn)->pressed.Add([&](const std::string& s) { ++fired; sender = s; });
    SE::PointerEvent ev[2];
    ev[0].phase = SE::PointerPhase::Down; ev[0].position = SC_Vec2{110.f, 110.f}; ev[0].inside = true;
    ev[1].phase = SE::PointerPhase::Up;   ev[1].position = SC_Vec2{110.f, 110.f}; ev[1].inside = true;
    c.frame(0.016f, ev, 2, area);
    Check(fired == 1 && sender == btn, "clique no botao dispara Pressed uma vez, com o id");
    c.frame(0.016f, nullptr, 0, area);
    Check(fired == 1, "sem novo clique, Pressed nao dispara de novo");
  }

  // Botoes sobrepostos: so o de cima recebe o clique.
  {
    cu::Canvas c;
    const std::string bottom = c.create(cu::Kind::Button, "Baixo", 0.5f, 0.5f, 1);
    const std::string top = c.create(cu::Kind::Button, "Cima", 0.5f, 0.5f, 1);   // criado depois: por cima
    int bottomFired = 0, topFired = 0;
    c.find(bottom)->pressed.Add([&](const std::string&) { ++bottomFired; });
    c.find(top)->pressed.Add([&](const std::string&) { ++topFired; });
    SE::PointerEvent ev[2];
    ev[0].phase = SE::PointerPhase::Down; ev[0].position = SC_Vec2{110.f, 110.f}; ev[0].inside = true;
    ev[1].phase = SE::PointerPhase::Up;   ev[1].position = SC_Vec2{110.f, 110.f}; ev[1].inside = true;
    c.frame(0.016f, ev, 2, area);
    Check(topFired == 1 && bottomFired == 0, "botao sobreposto: so o de cima dispara");
  }

  std::printf("%d falha(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
