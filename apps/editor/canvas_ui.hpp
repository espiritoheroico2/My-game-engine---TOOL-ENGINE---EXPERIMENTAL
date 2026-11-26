#pragma once

// API nativa do Canvas, em cima da SpriteUI: os elementos vivem numa SE::ElementTree (handles com geracao, sem
// ponteiro solto) sob uma raiz Panel que preenche a area do canvas. Cada elemento tem um id unico do jogo inteiro
// (ui_ids.hpp), uma ordem Z e uma regiao do canvas. Eventos seguem o modelo de C#: Event<Args...> guarda as funcoes
// inscritas e Trigger() chama todas, em ordem.
//
// Exemplo (C++, mesmo desenho que o script vai usar):
//   canvas_ui::Canvas hud;
//   const std::string save = hud.create(canvas_ui::Kind::Button, "Save Game", 0.05f, 0.08f, 5);
//   hud.find(save)->pressed.Add([](const std::string& sender) { /* SalvarJogo(sender) */ });
//   hud.frame(dt, pointerEvents, n, area);   // dispara "pressed" quando o botao e clicado
//
// Posicao: x e y sao fracoes da area do canvas (0 = esquerda/topo, 1 = direita/base). Tamanho em pixels logicos.
//
// Otimizacao:
//   - elementos em vetor plano, com indice por id (busca O(1), sem varrer a lista);
//   - a arvore so e arrumada quando algo mudou (dirty), nao a cada quadro;
//   - Trigger nao copia a lista de funcoes: remocao durante o disparo marca o slot e so compacta no fim;
//   - botao clicado vira um id numa lista curta; os eventos so disparam depois do laco de ponteiro, para o
//     callback poder criar ou apagar elementos sem invalidar o laco.
//
// Limites: sem JS ainda (a ligacao com QuickJS vem depois da aprovacao). Remover uma funcao exige o id da inscricao
// devolvido por Add(), porque std::function nao se compara com ==.

#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/Label.hpp"
#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "ui_ids.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor::canvas_ui {

namespace SE = ToolEngine::SpriteUi::Elements;
namespace SC = ToolEngine::SpriteUi;

enum class Kind : std::uint8_t { Text, Button };
enum class Border : std::uint8_t { Square, Round };

// Evento estilo C#: Add (+=) devolve a inscricao, Remove (-=) apaga pela inscricao, Trigger dispara todas.
template <class... Args>
class Event {
 public:
  using Fn = std::function<void(Args...)>;

  // Inscreve uma funcao. Devolve o id da inscricao (use em Remove).
  std::uint32_t Add(Fn fn) {
    subs_.push_back({next_, std::move(fn)});
    return next_++;
  }

  // Cancela a inscricao. Durante um Trigger, o slot so e marcado e sai no fim do disparo.
  bool Remove(std::uint32_t sub) {
    for (std::size_t i = 0; i < subs_.size(); ++i) {
      if (subs_[i].id != sub || !subs_[i].fn) continue;
      if (depth_ > 0) subs_[i].fn = nullptr;
      else subs_.erase(subs_.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
    return false;
  }

  // Chama todas as funcoes inscritas, na ordem. Inscricoes feitas durante o disparo so valem no proximo.
  // Recursao: a partir de kMaxDepth niveis o disparo para (evita laço infinito de evento que dispara evento).
  void Trigger(Args... args) {
    if (depth_ >= kMaxDepth) return;
    ++depth_;
    const std::size_t n = subs_.size();
    for (std::size_t i = 0; i < n && i < subs_.size(); ++i) {
      if (subs_[i].fn) subs_[i].fn(args...);
    }
    if (--depth_ == 0) {
      std::erase_if(subs_, [](const Sub& s) { return !s.fn; });
    }
  }

  [[nodiscard]] std::size_t Count() const noexcept {
    std::size_t c = 0;
    for (const Sub& s : subs_) c += s.fn ? 1 : 0;
    return c;
  }

  static constexpr int kMaxDepth = 8;

 private:
  struct Sub {
    std::uint32_t id = 0;
    Fn fn;
  };
  std::vector<Sub> subs_;
  std::uint32_t next_ = 1;
  int depth_ = 0;
};

// Um elemento do canvas: o id (unico no jogo), o handle da arvore, e os dados que o editor e o script leem.
struct Entry {
  std::string id;
  SC::UIHandle handle{};
  Kind kind = Kind::Text;
  int region = 0;
  int z = 0;
  Event<const std::string&> pressed;   // dispara com o id do elemento clicado (o "sender")
};

class Canvas {
 public:
  Canvas() {
    root_ = tree_.Create<SE::Panel>(SC::UIHandle{});
    if (auto* root = tree_.GetAs<SE::Panel>(root_)) {   // raiz preenche a area do canvas
      root->transform.anchorMin = {0.f, 0.f};
      root->transform.anchorMax = {1.f, 1.f};
      root->transform.offsetMin = {0.f, 0.f};
      root->transform.offsetMax = {0.f, 0.f};
    }
  }

  // Cria um elemento. Devolve o id unico ("e1", "e2", ...). Entra por cima de todos os outros.
  std::string create(Kind kind, const std::string& text, float x, float y, int region, Border border = Border::Round) {
    Entry e;
    e.id = ui_ids::next();
    e.kind = kind;
    e.region = region;
    e.z = topZ() + 1;
    if (kind == Kind::Button) {
      e.handle = tree_.Create<SE::Button>(root_);
      if (auto* b = tree_.GetAs<SE::Button>(e.handle)) b->cornerRadius = border == Border::Round ? 6.f : 0.f;
    } else {
      e.handle = tree_.Create<SE::Label>(root_);
      if (auto* l = tree_.GetAs<SE::Label>(e.handle)) l->text = text;
    }
    place(e.handle, x, y, kind == Kind::Button ? kButtonW : kTextW, kind == Kind::Button ? kButtonH : kTextH);
    std::string id = e.id;
    index_[id] = entries_.size();
    entries_.push_back(std::move(e));
    dirty_ = true;
    return id;
  }

  // Elemento pelo id, ou nullptr. O ponteiro vale ate o proximo create ou destroy.
  [[nodiscard]] Entry* find(const std::string& id) {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &entries_[it->second];
  }

  // Apaga o elemento e libera o handle. Id desconhecido: falso e nada muda.
  bool destroy(const std::string& id) {
    const auto it = index_.find(id);
    if (it == index_.end()) return false;
    const std::size_t i = it->second;
    tree_.Destroy(entries_[i].handle);
    index_.erase(it);
    if (i + 1 != entries_.size()) {   // troca com o ultimo: o vetor continua plano
      entries_[i] = std::move(entries_.back());
      index_[entries_[i].id] = i;
    }
    entries_.pop_back();
    dirty_ = true;
    return true;
  }

  // Poe o elemento por cima de todos (z maior). Id desconhecido: falso.
  bool bringToFront(const std::string& id) {
    Entry* e = find(id);
    if (!e) return false;
    e->z = topZ() + 1;
    tree_.BringToFront(e->handle);
    dirty_ = true;
    return true;
  }

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  // Quadro: arruma a arvore so se mudou algo, avanca o tempo dos botoes e manda o ponteiro. Entre botoes sobrepostos
  // so o de cima recebe o ponteiro. Os botoes clicados disparam "pressed" depois do laco.
  void frame(float frameSeconds, const SE::PointerEvent* events, std::size_t count, SC::Rect area) {
    if (dirty_) {
      tree_.Arrange(root_, area);
      dirty_ = false;
    }
    for (Entry& e : entries_) {
      if (e.kind != Kind::Button) continue;
      if (auto* b = tree_.GetAs<SE::Button>(e.handle)) b->Advance(frameSeconds);
    }
    std::vector<std::size_t> clicked;
    for (std::size_t k = 0; k < count; ++k) {
      const std::size_t top = topButtonAt(events[k].position);
      for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& e = entries_[i];
        if (e.kind != Kind::Button) continue;
        auto* b = tree_.GetAs<SE::Button>(e.handle);
        if (!b) continue;
        SE::PointerEvent ev = events[k];
        ev.inside = (i == top);
        if (b->OnPointer(ev).clicked) clicked.push_back(i);
      }
    }
    for (std::size_t i : clicked) {
      if (i >= entries_.size()) continue;   // um callback anterior pode ter apagado o elemento
      const std::string id = entries_[i].id;
      entries_[i].pressed.Trigger(id);
    }
  }

  static constexpr float kButtonW = 120.f, kButtonH = 40.f, kTextW = 160.f, kTextH = 24.f;

 private:
  int topZ() const noexcept {
    int top = -1;
    for (const Entry& e : entries_) top = std::max(top, e.z);
    return top;
  }

  // Posicao por fracao: ancora no ponto (x, y) da area da raiz, com tamanho fixo em pixels logicos.
  void place(SC::UIHandle h, float x, float y, float w, float hgt) {
    auto* el = tree_.Get(h);
    if (!el) return;
    el->transform.anchorMin = {x, y};
    el->transform.anchorMax = {x, y};
    el->transform.offsetMin = {0.f, 0.f};
    el->transform.offsetMax = {w, hgt};
  }

  // Indice do botao de cima (maior z) sob o ponto, ou o tamanho da lista se nenhum.
  std::size_t topButtonAt(SC::Vec2 p) {
    std::size_t best = entries_.size();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      const Entry& e = entries_[i];
      if (e.kind != Kind::Button) continue;
      const auto* b = tree_.GetAs<SE::Button>(e.handle);
      if (!b || !b->border.Contains(p)) continue;
      if (best == entries_.size() || e.z > entries_[best].z) best = i;
    }
    return best;
  }

  SE::ElementTree tree_;
  SC::UIHandle root_{};
  std::vector<Entry> entries_;
  std::unordered_map<std::string, std::size_t> index_;
  bool dirty_ = true;
};

}  // namespace editor::canvas_ui
