#pragma once

// Cache de atlas de fonte, indexado por FontAtlasKey (conteudo da fonte + estilo + formato + resolucao).
//
// O que o cache resolve: evita reconstruir um atlas a cada quadro, evita enviar a mesma pagina duas vezes e
// limita a memoria total por maxBytes e maxEntries.
//
// O que ele NAO faz: nao toca na GPU. A textura do atlas e do host. Quando uma entrada sai do cache, o callback
// onEvict avisa o host, que deve enfileirar a liberacao da textura atras da fence do ultimo quadro que a usou.
// Liberar antes disso deixa um quadro em voo apontando para memoria destruida.
//
// Regra de uso: uma entrada em uso nunca sai. "Em uso" e qualquer AtlasPtr devolvido que ainda esteja vivo: o
// cache guarda uma referencia, entao use_count() > 1 significa que alguem a esta usando. Por isso o cache pode
// ficar acima do orcamento enquanto tudo estiver em uso, e volta ao orcamento quando a referencia some e Trim()
// e chamado. Exemplo de jogo: o atlas do HUD fica preso enquanto o quadro atual o desenha; ao trocar de cena,
// ele sai no proximo Trim() depois do ultimo quadro.
//
// Uso de uma thread: o cache nao tem sincronizacao. O host chama a partir da thread que monta o quadro.

#include "SpriteUi/Text/FontRenderMetadata.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

namespace ToolEngine::SpriteUi::Text {

using FontFileHash = std::uint64_t;   // Hash do conteudo do arquivo (HashFontBytes), nao do caminho.

// Estilo tipografico do atlas. Italico e obliquo ficam de fora ate existir fonte real para cada um.
enum class FontStyle : std::uint8_t { Regular, Italic };

// Identidade de um atlas. Duas chaves iguais produzem o mesmo atlas; qualquer campo diferente exige outro atlas.
struct FontAtlasKey {
    FontFileHash file = 0;                                   // Conteudo da fonte.
    std::uint32_t faceIndex = 0;                             // Indice dentro de uma colecao TTC.
    std::uint16_t weight = 400;                              // Peso (instancia de fonte variavel ou arquivo).
    FontStyle style = FontStyle::Regular;
    TextRenderMode mode = TextRenderMode::BitmapCoverage;    // Formato do atlas.
    std::uint16_t rasterBucket = 0;                          // Bucket de ChooseBitmapRasterPixelSize (0 = n/a).
    std::uint16_t pxRange = 0;                               // Faixa de distancia (SDF/MSDF). Zero no bitmap.
    std::uint32_t charsetId = 0;                             // Identificador do conjunto de codepoints.

    [[nodiscard]] bool operator==(const FontAtlasKey& other) const noexcept = default;
};

struct FontAtlasKeyHash {
    [[nodiscard]] std::size_t operator()(const FontAtlasKey& key) const noexcept;
};

// Hash FNV-1a de 64 bits do conteudo da fonte. Exemplo: dois mods com a mesma fonte, em caminhos diferentes,
// geram a mesma chave e compartilham o atlas.
[[nodiscard]] FontFileHash HashFontBytes(std::span<const std::uint8_t> bytes) noexcept;

class FontAtlasCache {
public:
    using AtlasPtr = std::shared_ptr<const TruetypeAtlas>;

    struct Config {
        std::size_t maxBytes = 64u * 1024u * 1024u;   // Teto somado de memoria CPU dos atlas guardados.
        std::size_t maxEntries = 64;                  // Teto de quantidade de atlas guardados.
        // Chamado quando uma entrada sai do cache (inclusive em Clear). O host usa para liberar a textura GPU
        // atras da fence. Pode ser vazio se o host nao tiver GPU associada.
        std::function<void(const FontAtlasKey&)> onEvict;
    };

    FontAtlasCache() = default;
    explicit FontAtlasCache(Config config);

    // Devolve o atlas guardado para a chave, ou nullptr. Nao cria entrada.
    [[nodiscard]] AtlasPtr Find(const FontAtlasKey& key);

    // Devolve o atlas guardado; se faltar, chama build (que preenche o atlas e devolve false em falha) e guarda o
    // resultado. Falha devolve nullptr e nao deixa entrada parcial. Atlas construido aqui sai com use_count > 1
    // ate o chamador soltar o ponteiro, entao o Trim automatico nunca o derruba na hora.
    [[nodiscard]] AtlasPtr GetOrBuild(const FontAtlasKey& key, const std::function<bool(TruetypeAtlas&)>& build);

    // Remove, do mais antigo para o mais novo, entradas sem uso externo ate ficar dentro do orcamento. Entradas em
    // uso nunca saem, mesmo acima do orcamento.
    void Trim();

    // Remove todas as entradas sem uso externo. As em uso ficam, e voltam a ser candidatas ao Trim depois.
    void Clear();

    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t Bytes() const noexcept { return bytes_; }
    [[nodiscard]] const Config& GetConfig() const noexcept { return config_; }

private:
    struct Entry {
        AtlasPtr atlas;
        std::size_t bytes = 0;
        std::uint64_t lastUse = 0;   // Relogio logico: maior = usado mais recentemente.
    };

    [[nodiscard]] static std::size_t EstimateBytes(const TruetypeAtlas& atlas) noexcept;
    void Remove(std::unordered_map<FontAtlasKey, Entry, FontAtlasKeyHash>::iterator it);
    void EvictToBudget();

    Config config_;
    std::unordered_map<FontAtlasKey, Entry, FontAtlasKeyHash> entries_;
    std::size_t bytes_ = 0;
    std::uint64_t clock_ = 0;
};

} // namespace ToolEngine::SpriteUi::Text
