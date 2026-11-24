#pragma once
// Estado das janelas de ajuda e de teste (sem ImGui): qual topico do Help esta aberto e a janela Test map.
// Quem desenha: help_panel.hpp e test_map_panel.hpp (SpriteUI). Quem abre: o "?" de cada moldura, F1 e a linha de comando.
#include "i18n.hpp"

#include <string>

namespace editor {

struct HelpState { bool open = false; std::string topic = "start"; std::string pending; };
inline HelpState& helpState() { static HelpState h; return h; }
inline void openHelp(const std::string& topic) { auto& h = helpState(); h.open = true; h.pending = topic; }
inline std::string helpLanguage() { return I18n::get().code(); }

// Janela "Test map of chunks": gera um mapa gigante de chunks de terreno para testar a Draw distance e a oclusao.
struct TestMapState { bool open = false; int side = 32; };
inline TestMapState& testMapState() { static TestMapState s; return s; }
inline void openTestMap() { testMapState().open = true; }

}  // namespace editor
