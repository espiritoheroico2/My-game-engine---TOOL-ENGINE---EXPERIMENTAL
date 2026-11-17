#pragma once

// Tabela como modelo puro: colunas (largura fixa ou proporcional), cabecalho e linhas em lista virtual. Nao
// desenha e nao guarda o conteudo das celulas: o chamador so informa a quantidade de linhas e pede o texto das
// que estao em VisibleRows(). Assim uma tabela com 100 mil linhas custa o mesmo que uma com 20.
//
// Regras:
//  - Colunas fixas mantem a largura pedida. As proporcionais dividem o que sobra, na razao dos pesos.
//  - Nenhuma coluna fica abaixo do seu minWidth, mesmo que a tabela seja estreita demais.
//  - Linhas reusam ListViewControl: selecao, teclado (setas, Home/End) e rolagem minima para a selecao caber.
//  - Coordenadas locais: origem no canto superior esquerdo da tabela, o cabecalho ocupa o topo.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/Controls.hpp"
#include "SpriteUi/Widgets/VirtualList.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

enum class ColumnSizing : std::uint8_t { Fixed, Proportional };

struct TableColumn {
    std::string title;
    ColumnSizing sizing = ColumnSizing::Fixed;
    float width = 100.0f;  // Usado pelas colunas fixas.
    float weight = 1.0f;   // Usado pelas colunas proporcionais.
    float minWidth = 24.0f;
};

// Largura de cada coluna para a largura total dada. Ver a regra no topo do arquivo.
[[nodiscard]] std::vector<float> ResolveColumnWidths(const std::vector<TableColumn>& columns, float totalWidth);

class TableControl {
public:
    void SetColumns(std::vector<TableColumn> columns);
    [[nodiscard]] const std::vector<TableColumn>& Columns() const noexcept { return columns_; }

    // Largura e altura da tabela inteira (cabecalho incluido). Recalcula colunas e a area de linhas.
    void SetSize(float width, float height);
    void SetHeaderHeight(float height);
    void SetRowCount(std::size_t count) noexcept { rows.SetItemCount(count); }
    void SetRowHeight(float height) noexcept;

    [[nodiscard]] float HeaderHeight() const noexcept { return headerHeight_; }
    [[nodiscard]] const std::vector<float>& ColumnWidths() const noexcept { return widths_; }
    [[nodiscard]] float ColumnX(std::size_t column) const noexcept;

    // Faixa de linhas que precisam ser desenhadas (com overscan).
    [[nodiscard]] VirtualListRange VisibleRows() const noexcept { return rows.VisibleRange(); }

    // Topo de uma linha em coordenadas locais da tabela (ja com a rolagem aplicada).
    [[nodiscard]] float RowTop(std::size_t row) const noexcept;

    // Linha sob o ponto local, ou NoControlIndex se o ponto cai no cabecalho ou fora da tabela.
    [[nodiscard]] std::size_t RowAt(Vec2 local) const noexcept;

    // Rolagem vertical pelo delta (roda do mouse). Retorna true se a rolagem mudou.
    [[nodiscard]] bool ScrollBy(float deltaY) noexcept;

    [[nodiscard]] bool SelectRow(std::size_t row) noexcept { return rows.Select(row); }
    [[nodiscard]] bool Key(ListKey key) noexcept { return rows.Key(key); }

    ListViewControl rows;

private:
    void UpdateWidths() noexcept;

    std::vector<TableColumn> columns_;
    std::vector<float> widths_;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float headerHeight_ = 28.0f;
};

} // namespace ToolEngine::SpriteUi::Widgets
