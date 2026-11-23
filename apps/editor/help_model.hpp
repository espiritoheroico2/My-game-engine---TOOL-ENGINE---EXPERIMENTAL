#pragma once

// Modelo da janela Help (SpriteUI): topico aberto, busca, idioma e blocos do texto. Nao usa SDL, ImGui nem desenho,
// e roda no ctest (tests/test_help_model.cpp). Os textos vem de help_content.hpp.
//
// Regras:
//  - A busca olha titulo e texto do topico, sem diferenciar maiusculas, no idioma da interface.
//  - O topico aberto continua aberto mesmo quando a busca o tira da lista.

#include "help_content.hpp"
#include "SpriteUi/Widgets/TextField.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::help {

namespace Edit = ToolEngine::SpriteUi::Widgets;

enum class BlockKind : std::uint8_t { Heading, Paragraph, Bullet, Code };

struct Block {
  BlockKind kind = BlockKind::Paragraph;
  std::string text;   // Codigo: as linhas do bloco separadas por \n.
};

enum class SearchKey : std::uint8_t { Left, Right, Home, End, Backspace, Delete };

inline std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string::size_type start = 0;
  while (true) {
    const std::string::size_type end = text.find('\n', start);
    lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) return lines;
    start = end + 1;
  }
}

inline std::string JoinLines(const std::vector<std::string>& lines) {
  std::string joined;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) joined += '\n';
    joined += lines[i];
  }
  return joined;
}

// Quebra o texto de um topico em blocos, com a mesma marcacao do Help antigo: "# titulo", "- item",
// bloco ``` ... ``` (codigo) e o resto como paragrafo. Bloco de codigo sem fechamento e descartado.
inline std::vector<Block> ParseBody(const std::string& body) {
  std::vector<Block> blocks;
  std::vector<std::string> code;
  bool inCode = false;
  for (const std::string& line : SplitLines(body)) {
    if (line.rfind("```", 0) == 0) {
      if (!inCode) {
        code.clear();
      } else {
        blocks.push_back({BlockKind::Code, JoinLines(code)});
      }
      inCode = !inCode;
      continue;
    }
    if (inCode) {
      code.push_back(line);
    } else if (line.rfind("# ", 0) == 0) {
      blocks.push_back({BlockKind::Heading, line.substr(2)});
    } else if (line.rfind("- ", 0) == 0) {
      blocks.push_back({BlockKind::Bullet, line.substr(2)});
    } else if (!line.empty()) {
      blocks.push_back({BlockKind::Paragraph, line});
    }
  }
  return blocks;
}

inline std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

inline const char* TitleOf(const HelpTopic& topic, bool portuguese) { return portuguese ? topic.titlePt : topic.titleEn; }
inline const char* TitleOf(const HelpTopic& topic, const std::string& language) {
  if (language.rfind("es", 0) == 0 && topic.titleEs) return topic.titleEs;
  if (language.rfind("pt", 0) == 0) return topic.titlePt;
  return topic.titleEn;
}

class HelpModel {
 public:
  void SetPortuguese(bool portuguese) { SetLanguage(portuguese ? "pt-BR" : "en"); }
  void SetLanguage(std::string language) { language_ = std::move(language); }
  [[nodiscard]] bool Portuguese() const { return language_.rfind("pt", 0) == 0; }
  [[nodiscard]] const std::string& Language() const { return language_; }

  // Abre o topico de um id (icone "?" ou F1). Id desconhecido: fica o topico atual.
  void Open(const std::string& id) {
    const auto& topics = helpTopics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
      if (id == topics[i].id) selected_ = i;
    }
  }
  void SelectTopic(std::size_t index) {
    if (index < helpTopics().size()) selected_ = index;
  }

  [[nodiscard]] std::size_t SelectedIndex() const { return selected_; }
  [[nodiscard]] const HelpTopic& Current() const { return helpTopics()[selected_]; }
  [[nodiscard]] const char* CurrentTitle() const { return TitleOf(Current(), language_); }
  [[nodiscard]] std::vector<Block> CurrentBlocks() const {
    return ParseBody(BodyOf(Current()));
  }

  // Indices (em helpTopics()) dos topicos que passam na busca, na ordem da lista.
  [[nodiscard]] std::vector<std::size_t> VisibleTopics() const {
    const std::string query = Lower(search_.Edit().Text());
    std::vector<std::size_t> visible;
    const auto& topics = helpTopics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
      if (query.empty() || Matches(topics[i], query)) visible.push_back(i);
    }
    return visible;
  }

  // Campo de busca (desenho e clique usam o TextField da biblioteca).
  [[nodiscard]] Edit::TextField& Search() { return search_; }
  [[nodiscard]] const std::string& SearchText() const { return search_.Edit().Text(); }
  [[nodiscard]] bool SearchFocused() const { return search_.Focused(); }
  void SetSearchFocus(bool focused) { search_.SetFocus(focused); }

  // Texto digitado e atalhos de edicao. Sem foco na busca, nada muda.
  void InsertSearchText(std::string_view utf8) {
    if (search_.Focused()) search_.Edit().InsertText(utf8);
  }
  void SelectAllSearch() {
    if (search_.Focused()) search_.Edit().SelectAll();
  }
  void PressSearch(SearchKey key, bool shift) {
    if (!search_.Focused()) return;
    Edit::TextEditState& edit = search_.Edit();
    switch (key) {
      case SearchKey::Left: edit.MoveLeft(shift); break;
      case SearchKey::Right: edit.MoveRight(shift); break;
      case SearchKey::Home: edit.MoveHome(shift); break;
      case SearchKey::End: edit.MoveEnd(shift); break;
      case SearchKey::Backspace: (void)edit.Backspace(); break;
      case SearchKey::Delete: (void)edit.Delete(); break;
    }
  }

 private:
  // Busca no titulo e no texto do idioma da interface.
  [[nodiscard]] bool Matches(const HelpTopic& topic, const std::string& query) const {
    const std::string title = Lower(TitleOf(topic, language_));
    const std::string body = Lower(BodyOf(topic));
    return title.find(query) != std::string::npos || body.find(query) != std::string::npos;
  }

  const char* BodyOf(const HelpTopic& topic) const {
    if (language_.rfind("es", 0) == 0 && topic.bodyEs) return topic.bodyEs;
    if (language_.rfind("pt", 0) == 0) return topic.bodyPt;
    return topic.bodyEn;
  }

  Edit::TextField search_;
  std::size_t selected_ = 0;
  std::string language_ = "en";
};

}  // namespace editor::help
