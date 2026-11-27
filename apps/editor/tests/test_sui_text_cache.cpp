// Cache de layout de texto do Canvas (sui_panel.hpp): o caminho com cache tem de dar exatamente o texto cortado e as
// larguras do calculo direto (laco linear antigo), invalidar ao mudar a escala/atlas e respeitar o limite de entradas.
// Inclui um microbenchmark simples (nao e o quadro do editor). Fonte vem de EDITOR_FONT_PATH (CMake).
#include "sui_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

using editor::sui::Canvas;
using editor::sui::TextAtlasSet;

// Referencia: o laco linear antigo de Canvas::fit, sobre o layout sem cache.
std::string RefFit(const Canvas& c, const std::string& s, float size, float maxWidth) {
  const float full = c.layoutUncached(s, size).size.x;
  if (full <= maxWidth) return s;
  std::size_t cut = std::min(s.size(), static_cast<std::size_t>(std::floor(static_cast<float>(s.size()) * maxWidth / std::max(1.f, full))));
  while (cut > 0) {
    while (cut > 0 && cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    if (cut == 0) break;   // o laco antigo dava a volta em size_t aqui (travava); o resultado pretendido e "..."
    const std::string candidate = s.substr(0, cut) + "...";
    if (c.layoutUncached(candidate, size).size.x <= maxWidth) return candidate;
    --cut;
  }
  return "...";
}

std::vector<std::string> Samples() {
  return {
      "",
      "A",
      "Hierarchy",
      "Inspector",
      "Transform",
      "Posicao do objeto na cena",
      "A r\xC3\xA1pida raposa marrom pulou sobre o c\xC3\xA3o pregui\xC3\xA7oso",
      "Configura\xC3\xA7\xC3\xB5" "es de renderiza\xC3\xA7\xC3\xA3o e \xC3\xA1udio avan\xC3\xA7" "ado",
      "\xC3\xA7\xC3\xA3\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xA7\xC3\xA3\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA",
      "assets/textures/ambiente/floresta/arvore_grande_com_nome_muito_longo_demais_01.png",
      std::string(400, 'W'),
      std::string(300, 'i'),
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua",
  };
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  TextAtlasSet atlas;
  atlas.load(EDITOR_FONT_PATH, 1.f);
  Canvas c(atlas);

  const float sizes[] = {12.f, 14.f, 16.f};
  const auto samples = Samples();
  std::size_t compared = 0;
  bool allSame = true;
  for (int pass = 0; pass < 2; ++pass) {   // 1a passada: misses; 2a: hits. Ambas iguais a referencia.
    for (float size : sizes) {
      for (const std::string& s : samples) {
        const float full = c.layoutUncached(s, size).size.x;
        const float widths[] = {0.f, 0.5f, 1.f, 5.f, 10.f, 20.f, 33.3f, 50.f, 80.f, 120.f, 150.f, 200.f, 333.f, full * 0.5f, full * 0.9f,
                                full - 0.01f, full, full + 0.01f, full * 2.f + 5.f};
        for (float mw : widths) {
          const auto f = c.fitted(s, size, mw);
          const std::string ref = RefFit(c, s, size, mw);
          const float refW = c.layoutUncached(ref, size).size.x;
          ++compared;
          if (f.text != ref || f.width != refW) {
            allSame = false;
            std::printf("  diferente: size=%g mw=%g len=%zu got[%s] ref[%s] w=%g/%g\n", size, mw, s.size(), f.text.substr(0, 30).c_str(),
                        ref.substr(0, 30).c_str(), f.width, refW);
          }
        }
      }
    }
  }
  std::printf("  %zu comparacoes\n", compared);
  Check(allSame, "texto cortado e largura do cache == laco linear antigo (ASCII, acentos, longo, vazio, justo, estreito)");

  // Nunca corta no meio de um caractere UTF-8: o resultado e UTF-8 valido.
  {
    bool valid = true;
    const std::string acc = "\xC3\xA7\xC3\xA3\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xA7\xC3\xA3\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA";
    for (float mw = 1.f; mw < 140.f; mw += 1.7f) {
      const std::string t = c.fitted(acc, 14.f, mw).text;
      for (std::size_t i = 0; i < t.size();) {
        const unsigned char u = static_cast<unsigned char>(t[i]);
        const std::size_t n = u < 0x80 ? 1 : (u >> 5) == 6 ? 2 : (u >> 4) == 14 ? 3 : (u >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > t.size()) {
          valid = false;
          break;
        }
        for (std::size_t j = 1; j < n; ++j)
          if ((static_cast<unsigned char>(t[i + j]) & 0xC0) != 0x80) valid = false;
        i += n;
      }
    }
    Check(valid, "corte nunca parte uma sequencia UTF-8");
  }

  // layout() com cache == sem cache; addText devolve a largura do texto cortado.
  {
    const auto a = c.layout("Posicao do objeto", 14.f);
    const auto b = c.layoutUncached("Posicao do objeto", 14.f);
    Check(a.size.x == b.size.x && a.size.y == b.size.y && a.quads.size() == b.quads.size() && a.lineHeight == b.lineHeight,
          "layout() em cache == layoutUncached");
    const float w = c.addText(0, 0, "Posicao do objeto na cena atual do editor", 14.f, {}, 100.f);
    Check(w == c.fitted("Posicao do objeto na cena atual do editor", 14.f, 100.f).width && w <= 100.f, "addText devolve a largura cortada (<= maxWidth)");
    Check(c.addText(0, 0, "", 14.f, {}, 100.f) == 0.f && c.addText(0, 0, "x", 14.f, {}, 0.f) == 0.f, "texto vazio / largura 0 nao desenha");
  }

  // Contadores: repetir a mesma chamada so acerta.
  {
    c.clearTextCache();
    c.resetCacheStats();
    c.fitted("Hierarchy panel title", 14.f, 60.f);
    const auto s1 = c.cacheStats();
    c.fitted("Hierarchy panel title", 14.f, 60.f);
    const auto s2 = c.cacheStats();
    Check(s1.misses > 0 && s2.misses == s1.misses && s2.hits == s1.hits + 1, "segunda chamada igual e so hit (sem novo layout)");
  }

  // Altura de linha: uma entrada por tamanho; alternar nao recalcula.
  {
    c.clearTextCache();
    c.resetCacheStats();
    const float a1 = c.lineHeight(12.f), b1 = c.lineHeight(16.f);
    const auto s1 = c.cacheStats();
    float a2 = 0, b2 = 0;
    for (int i = 0; i < 10; ++i) {
      a2 = c.lineHeight(12.f);
      b2 = c.lineHeight(16.f);
    }
    const auto s2 = c.cacheStats();
    Check(a1 == a2 && b1 == b2 && a1 != b1, "lineHeight estavel por tamanho");
    Check(s2.misses == s1.misses && s2.hits == s1.hits, "alternar tamanhos nao toca no layout (1 entrada por tamanho)");
    Check(a1 == c.layoutUncached("Ag", 12.f).lineHeight, "lineHeight == layout direto de \"Ag\"");
  }

  // Escala da fonte invalida o cache e muda as medidas.
  {
    const std::string s = "Posicao do objeto na cena atual";
    const auto before = c.fitted(s, 14.f, 120.f);
    const float lh1 = c.lineHeight(14.f);
    Check(c.cacheStats().entries > 0, "cache tem entradas antes de mudar a escala");
    c.setFontScale(1.5f);
    Check(c.cacheStats().entries == 0, "setFontScale(novo valor) esvazia o cache");
    const auto after = c.fitted(s, 14.f, 120.f);
    Check(after.text == RefFit(c, s, 14.f, 120.f) && after.width == c.layoutUncached(after.text, 14.f).size.x,
          "apos mudar a escala o resultado == referencia na nova escala");
    Check(after.text != before.text || after.width != before.width, "a escala muda o resultado");
    Check(c.lineHeight(14.f) != lh1 && c.lineHeight(14.f) == c.layoutUncached("Ag", 14.f).lineHeight, "lineHeight recalculado na nova escala");
    c.fitted(s, 14.f, 120.f);
    const auto n = c.cacheStats().entries;
    c.setFontScale(1.5f);   // mesmo valor: nao esvazia
    Check(c.cacheStats().entries == n, "setFontScale com o mesmo valor mantem o cache");
    c.setFontScale(1.f);
  }

  // Atlas recarregado (outra escala de tela): clear() detecta e descarta.
  {
    Canvas c2(atlas);
    c2.clear();
    const std::string s = "Posicao do objeto na cena atual";
    c2.fitted(s, 14.f, 120.f);
    c2.clear();   // grava a impressao digital do atlas de 14 px
    const auto before = c2.fitted(s, 14.f, 90.f);
    atlas.load(EDITOR_FONT_PATH, 2.f);
    c2.clear();
    Check(c2.cacheStats().entries == 0, "atlas recarregado em outra escala: clear() descarta o cache");
    const auto after = c2.fitted(s, 14.f, 90.f);
    Check(after.text == RefFit(c2, s, 14.f, 90.f) && after.width == before.width, "resultado igual a referencia apos recarregar (largura logica nao depende da escala do atlas)");
    atlas.load(EDITOR_FONT_PATH, 1.f);
    c2.clear();
  }

  // Limite de entradas.
  {
    Canvas c3(atlas);
    c3.resetCacheStats();
    for (int i = 0; i < 6000; ++i) c3.fitted("rotulo " + std::to_string(i), 14.f, 500.f);
    const auto st = c3.cacheStats();
    Check(st.entries <= 4096 && st.evictions >= 1, "cache limitado: esvazia ao encher (<= 4096 entradas)");
    Check(c3.fitted("rotulo 5999", 14.f, 500.f).text == "rotulo 5999", "continua correto apos o esvaziamento");
  }

  // addParagraph em cache e sensivel a largura.
  {
    Canvas c4(atlas);
    const std::string p = "Um paragrafo longo que precisa quebrar em varias linhas por palavras dentro da largura.";
    const float h1 = c4.addParagraph(0, 0, p, 14.f, {}, 120.f);
    const float h2 = c4.addParagraph(0, 0, p, 14.f, {}, 120.f);
    const float h3 = c4.addParagraph(0, 0, p, 14.f, {}, 200.f);
    Check(h1 == h2 && h3 < h1 && h1 > c4.lineHeight(14.f) * 1.5f, "addParagraph em cache e sensivel a largura");
  }

  // Microbenchmark: 500 rotulos tipicos x 200 repeticoes, largura 150. Nao e o quadro do editor.
  {
    std::vector<std::string> labels;
    for (int i = 0; i < 500; ++i) {
      switch (i % 4) {
        case 0: labels.push_back("Objeto " + std::to_string(i)); break;
        case 1: labels.push_back("Posicao do objeto numero " + std::to_string(i)); break;
        case 2: labels.push_back("assets/textures/ambiente/floresta/arquivo_" + std::to_string(i) + ".png"); break;
        default: labels.push_back("Configura\xC3\xA7\xC3\xA3o avan\xC3\xA7" "ada " + std::to_string(i)); break;
      }
    }
    Canvas c5(atlas);
    using clock = std::chrono::steady_clock;
    volatile float sink = 0.f;
    const int reps = 200;
    const auto t0 = clock::now();
    for (int r = 0; r < reps; ++r)
      for (const std::string& l : labels) {
        const auto lay = c5.layoutUncached(RefFit(c5, l, 14.f, 150.f), 14.f);   // caminho antigo: fit() + layout()
        sink = sink + lay.size.x;
      }
    const auto t1 = clock::now();
    for (int r = 0; r < reps; ++r) {
      c5.clear();
      for (const std::string& l : labels) sink = sink + c5.addText(0, 0, l, 14.f, {}, 150.f);
    }
    const auto t2 = clock::now();
    const double msOld = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double msNew = std::chrono::duration<double, std::milli>(t2 - t1).count();
    const auto st = c5.cacheStats();
    std::printf("  microbench (500 rotulos x %d): sem cache %.1f ms, com cache %.1f ms, razao %.1fx (hits %llu, misses %llu)\n", reps, msOld, msNew,
                msOld / std::max(0.001, msNew), static_cast<unsigned long long>(st.hits), static_cast<unsigned long long>(st.misses));
    Check(msNew < msOld, "microbench: com cache mais rapido");
  }

  std::printf("%s (%d falhas)\n", gFailures == 0 ? "TODOS OK" : "COM FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
