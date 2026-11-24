#pragma once

// Roteiro de entrada para teste (--script arquivo). Cada linha diz um evento do mouse no quadro dado:
//   <quadro> down <x> <y>     aperta o botao esquerdo
//   <quadro> move <x> <y>     move o ponteiro (com o botao apertado, se houve down sem up)
//   <quadro> up <x> <y>      solta o botao esquerdo
// Linhas vazias e as que comecam com '#' sao ignoradas. Os eventos entram na fila do SDL no inicio do quadro,
// como se o usuario os tivesse feito. Serve para reproduzir cliques, arrastes e travamentos sem tocar no mouse.
#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace editor::input_script {

class Player {
 public:
  // Le o roteiro. False se o arquivo nao abre. Linhas invalidas sao ignoradas.
  bool load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream ss(line);
      Step s{};
      std::string kind;
      if (!(ss >> s.frame >> kind >> s.x >> s.y)) continue;
      if (kind == "down") s.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      else if (kind == "up") s.type = SDL_EVENT_MOUSE_BUTTON_UP;
      else if (kind == "move") s.type = SDL_EVENT_MOUSE_MOTION;
      else continue;
      steps_.push_back(s);
    }
    return true;
  }

  // Empurra para a fila os eventos devidos ate este quadro (inclusive).
  void pump(int frame, Uint32 windowId) {
    while (next_ < steps_.size() && steps_[next_].frame <= frame) {
      const Step& s = steps_[next_++];
      SDL_Event e{};
      if (s.type == SDL_EVENT_MOUSE_MOTION) {
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = windowId;
        e.motion.x = s.x;
        e.motion.y = s.y;
        e.motion.state = held_ ? SDL_BUTTON_LMASK : 0;
      } else {
        held_ = s.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.type = s.type;
        e.button.windowID = windowId;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = held_;
        e.button.clicks = 1;
        e.button.x = s.x;
        e.button.y = s.y;
      }
      SDL_PushEvent(&e);
    }
  }

  [[nodiscard]] bool done() const { return next_ >= steps_.size(); }
  [[nodiscard]] std::size_t size() const { return steps_.size(); }

 private:
  struct Step {
    int frame = 0;
    SDL_EventType type = SDL_EVENT_MOUSE_MOTION;
    float x = 0.f, y = 0.f;
  };
  std::vector<Step> steps_;
  std::size_t next_ = 0;
  bool held_ = false;
};

}  // namespace editor::input_script
