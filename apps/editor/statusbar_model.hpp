#pragma once

// Modelo da barra de status do editor: decide os textos (API, FPS, cena, entidades, erros, linha de status e idioma).
// Nao usa SDL, ImGui nem SpriteUI, e roda no ctest (tests/test_statusbar_model.cpp).
//
// Regras:
//  - Os textos so mudam no maximo 4 vezes por segundo (kRefreshSec). Entre dois refreshes o modelo nao muda.
//  - refresh() devolve true so quando algum texto mudou: a janela remonta o conteudo apenas nesse caso.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace editor::statusbar {

inline constexpr double kRefreshSec = 0.25;

using Translate = const char* (*)(const char*);

struct Info {
  std::string api;
  double frameMs = 0.0;
  std::string scene;
  std::size_t entities = 0;
  std::size_t errors = 0;
  std::size_t warnings = 0;
  std::string statusLine;
  std::string language;
};

enum class Tone : std::uint8_t { Normal, Dim, Error };

struct Segment {
  std::string text;
  Tone tone = Tone::Normal;

  bool operator==(const Segment& other) const { return text == other.text && tone == other.tone; }
};

class StatusBarModel {
 public:
  // Recalcula os segmentos se ja passou o intervalo de refresh. Retorna true quando algum texto mudou.
  bool refresh(double nowSec, const Info& info, Translate tr) {
    if (refreshed_ && nowSec < nextRefreshSec_) return false;
    refreshed_ = true;
    nextRefreshSec_ = nowSec + kRefreshSec;
    std::vector<Segment> next = build(info, tr);
    const std::string nextLanguage = info.language;
    if (next == segments_ && nextLanguage == language_) return false;
    segments_ = std::move(next);
    language_ = nextLanguage;
    return true;
  }

  [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }
  [[nodiscard]] const std::string& language() const { return language_; }

 private:
  static std::string format(const char* pattern, const std::string& text) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, pattern, text.c_str());
    return buffer;
  }

  static std::vector<Segment> build(const Info& info, Translate tr) {
    std::vector<Segment> out;
    out.push_back({format(tr("API: %s"), info.api), Tone::Normal});
    char fps[32];
    std::snprintf(fps, sizeof fps, "%.0f FPS", info.frameMs > 0.0 ? 1000.0 / info.frameMs : 0.0);
    out.push_back({fps, Tone::Normal});
    out.push_back({format(tr("scene: %s"), info.scene), Tone::Normal});
    char entities[64];
    std::snprintf(entities, sizeof entities, tr("entities: %zu"), info.entities);
    out.push_back({entities, Tone::Normal});
    char counts[96];
    std::snprintf(counts, sizeof counts, tr("errors: %zu  warnings: %zu"), info.errors, info.warnings);
    out.push_back({counts, info.errors > 0 ? Tone::Error : Tone::Dim});
    if (!info.statusLine.empty()) out.push_back({info.statusLine, Tone::Dim});
    return out;
  }

  std::vector<Segment> segments_;
  std::string language_;
  double nextRefreshSec_ = 0.0;
  bool refreshed_ = false;
};

}  // namespace editor::statusbar
