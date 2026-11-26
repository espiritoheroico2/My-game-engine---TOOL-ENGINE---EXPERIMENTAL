#pragma once

// Teclas de edicao de um campo de texto da biblioteca (SpriteUI Widgets/TextField): Ctrl+A, Ctrl+C, Ctrl+V,
// Backspace, Delete, setas, Home e End. Inspector, Configuracoes e o seletor de cor usam esta mesma tabela, para
// o campo se comportar igual em todo lugar. Enter, Esc e as demais teclas ficam com quem chama.
//
// Exemplo: Ctrl+A seleciona o texto do campo; Backspace apaga a selecao. Devolve false para uma tecla que nao edita.

#include "SpriteUi/Widgets/TextField.hpp"

#include <SDL3/SDL.h>

namespace editor::text_keys {

inline bool ApplyEditKey(ToolEngine::SpriteUi::Widgets::TextField& field, SDL_Keycode key, SDL_Keymod mod) {
  const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
  const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
  if (ctrl && key == SDLK_A) {
    field.Edit().SelectAll();
  } else if (ctrl && key == SDLK_C) {
    SDL_SetClipboardText(field.Edit().Copy().c_str());
  } else if (ctrl && key == SDLK_V) {
    if (char* clip = SDL_GetClipboardText()) {
      field.Edit().Paste(clip);
      SDL_free(clip);
    }
  } else if (key == SDLK_BACKSPACE) {
    (void)field.Edit().Backspace();
  } else if (key == SDLK_DELETE) {
    (void)field.Edit().Delete();
  } else if (key == SDLK_LEFT) {
    field.Edit().MoveLeft(shift);
  } else if (key == SDLK_RIGHT) {
    field.Edit().MoveRight(shift);
  } else if (key == SDLK_HOME) {
    field.Edit().MoveHome(shift);
  } else if (key == SDLK_END) {
    field.Edit().MoveEnd(shift);
  } else {
    return false;
  }
  return true;
}

}  // namespace editor::text_keys
