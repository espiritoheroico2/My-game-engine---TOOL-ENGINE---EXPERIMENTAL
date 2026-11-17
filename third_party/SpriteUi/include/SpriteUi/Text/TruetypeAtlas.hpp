#pragma once

// Atlas de glifos com stb_truetype (opcional: target SpriteUi::TextStb, ativado por SPRITEUI_WITH_TEXT_STB).
//
// Limites deste primeiro passo, de proposito:
//  - Sem shaping complexo: um glifo por codepoint, sem kerning, ligaduras ou marcas combinantes.
//  - Atlas de rasterizacao (alfa 8 bits), nao SDF/MSDF. Renderizar muito menor ou maior que
//    rasterPixelSize fica serrilhado. SDF fica para a proxima etapa.
//  - Um atlas, uma pagina. Sem atlas dinamico.
//
// Convencoes iguais as do DebugShaper: xAdvance e planeBounds em unidades de fonte com
// metrics.unitsPerEm = 1000; planeBounds = [x0, y0, x1, y1] com y para cima a partir da linha de base;
// uvRect = [u0, v0, u1, v1] normalizado no atlas.

#include "SpriteUi/Text/FontMetrics.hpp"
#include "SpriteUi/Text/FontRenderMetadata.hpp"
#include "SpriteUi/Text/GlyphInstance.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi::Text {

// Limites duros de BuildTruetypeAtlas. Acima deles a configuracao e rejeitada antes de qualquer alocacao.
// Exemplo de jogo: um atlas de 512 px para HUD nunca precisa de em de 1024 px; esse pedido e erro de digitacao.
inline constexpr std::uint32_t kTruetypeMaxGlyphCountLimit = 65536;   // Teto absoluto de codepoints por atlas.
inline constexpr float kTruetypeMaxRasterPixelSize = 512.0f;          // Tamanho maximo do em rasterizado.
inline constexpr std::uint32_t kTruetypeMaxPadding = 64;              // Borda vazia maxima entre glifos.

struct TruetypeAtlasConfig {
    float rasterPixelSize = 48.0f;                    // Tamanho em pixels do em usado na rasterizacao (1..512).
    std::uint32_t atlasWidth = 512;
    std::uint32_t atlasHeight = 512;
    std::uint32_t firstCodepoint = 0x20;              // ASCII imprimivel por padrao.
    std::uint32_t lastCodepoint = 0x7E;
    std::uint32_t padding = 1;                        // Pixels vazios entre glifos (0..64).
    // Quantos codepoints o pedido pode conter (lastCodepoint - firstCodepoint + 1). Impede que um intervalo
    // acidentalmente amplo, como 0..10FFFF, gere milhoes de iteracoes. Padrao 4096; teto 65536.
    std::uint32_t maxGlyphCount = 4096;
    // Expoente aplicado a cobertura (alfa = cobertura^coverageGamma). 1.0 nao muda nada. Use 0.5 para texto
    // claro sobre fundo escuro quando o blend e feito em sRGB (SDL): sem isso o traco fica fino.
    float coverageGamma = 1.0f;
};

struct TruetypeGlyph {
    std::uint32_t glyphId = 0;
    float advance = 0.0f;                             // Em unidades de fonte (unitsPerEm = 1000).
    float planeBounds[4]{};
    float uvRect[4]{};
};

struct TruetypeAtlas {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;                 // Alfa 8 bits, width x height, linha por linha.
    FontMetrics metrics;                              // unitsPerEm = 1000; ascent/descent/lineGap nessa escala.
    std::unordered_map<std::uint32_t, TruetypeGlyph> glyphs;   // Chave: codepoint Unicode.
    std::uint32_t fallbackCodepoint = '?';            // Usado para codepoints fora do atlas.
    // Formato do atlas. BuildTruetypeAtlas sempre grava BitmapCoverage: flag FlagBitmapText, nunca MSDF.
    FontRenderMetadata renderMetadata;

    [[nodiscard]] bool IsValid() const noexcept;
};

// Origem da fonte, informada pelo host. Trusted = assets empacotados do jogo. Untrusted = arquivo escolhido pelo
// usuario, mod ou rede. O padrao e Untrusted, o caminho mais restrito.
enum class FontOrigin : std::uint8_t { Trusted, Untrusted };

// Contramedida configuravel contra fontes malformadas. O stb_truetype nao valida os limites de todas as leituras;
// esta politica limita o tamanho do arquivo e, se pedido, recusa fontes nao confiaveis.
// Exemplo de jogo: a fonte do HUD vem dos assets do jogo (Trusted). Um mod que traz sua propria fonte
// (Untrusted) e recusado quando allowUntrustedFonts = false.
//
// Limitacao declarada: a validacao confere cabecalho, tamanho e a tabela de tabelas (offset + length dentro do
// arquivo, sem overflow). Nao confere o conteudo de cada tabela. Um parser completo (FreeType, em processo isolado
// quando o modelo de ameaca exigir) fica para a fase 2.
struct FontLoadPolicy {
    std::size_t maxFontFileBytes = 32u * 1024u * 1024u;   // 32 MiB. Arquivo maior e recusado antes de qualquer leitura.
    bool allowUntrustedFonts = true;                       // Padrao preserva o uso atual. Com false, so Trusted carrega.
};

// Monta o atlas a partir dos bytes de um TTF/OTF. Retorna false se a fonte for invalida, a configuracao
// for absurda, a politica recusar a fonte ou os glifos nao couberem no atlas (nesse caso out fica vazio).
[[nodiscard]] bool BuildTruetypeAtlas(
    std::span<const std::uint8_t> fontBytes,
    const TruetypeAtlasConfig& config,
    TruetypeAtlas& out,
    const FontLoadPolicy& policy = {},
    FontOrigin origin = FontOrigin::Untrusted);

// Converte UTF-8 em um GlyphInstance por codepoint, na ordem do texto. clusterByteOffset e o offset
// do byte inicial do codepoint. Bytes UTF-8 invalidos e codepoints fora do atlas viram fallbackCodepoint.
// Tab avanca 4 espacos; controles (como '\n') ocupam um glifo sem area e sem avanco, como no DebugShaper.
[[nodiscard]] std::vector<GlyphInstance> ShapeTruetype(
    std::string_view utf8,
    const TruetypeAtlas& atlas);

} // namespace ToolEngine::SpriteUi::Text
