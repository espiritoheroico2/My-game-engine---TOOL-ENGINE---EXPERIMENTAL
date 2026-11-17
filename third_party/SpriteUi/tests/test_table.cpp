// Tabela (Table.hpp): larguras de coluna, cabecalho, linha sob o ponto, rolagem e selecao. Sem janela.
#include "SpriteUi/Widgets/Table.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Widgets;

int run = 0;
int failed = 0;

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        ++run;                                                                                               \
        if (!(expr)) {                                                                                       \
            ++failed;                                                                                        \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);                           \
        }                                                                                                    \
    } while (0)

bool Near(float a, float b, float eps = 1.0e-3f) {
    return std::fabs(a - b) <= eps;
}

TableColumn Fixed(float width) {
    TableColumn c;
    c.sizing = ColumnSizing::Fixed;
    c.width = width;
    return c;
}

TableColumn Proportional(float weight, float minWidth = 24.0f) {
    TableColumn c;
    c.sizing = ColumnSizing::Proportional;
    c.weight = weight;
    c.minWidth = minWidth;
    return c;
}

void TestFixedKeepsWidthAndProportionalSharesRest() {
    const auto widths = ResolveColumnWidths({Fixed(100.0f), Proportional(1.0f), Proportional(3.0f)}, 500.0f);
    CHECK(widths.size() == 3);
    CHECK(Near(widths[0], 100.0f));
    CHECK(Near(widths[1], 100.0f)); // 400 de sobra, dividido 1:3
    CHECK(Near(widths[2], 300.0f));
}

void TestMinWidthWinsOverTightSpace() {
    const auto widths = ResolveColumnWidths({Fixed(200.0f), Proportional(1.0f, 40.0f)}, 100.0f);
    CHECK(Near(widths[0], 200.0f)); // a fixa nao encolhe
    CHECK(Near(widths[1], 40.0f));  // a proporcional nao cai abaixo do minimo
}

void TestZeroAndNonFiniteWidthsAreSafe() {
    const auto none = ResolveColumnWidths({Proportional(1.0f, 10.0f)}, 0.0f);
    CHECK(Near(none[0], 10.0f));
    const auto nan = ResolveColumnWidths({Proportional(2.0f, 10.0f)}, std::nanf(""));
    CHECK(Near(nan[0], 10.0f));
    CHECK(ResolveColumnWidths({}, 300.0f).empty());
}

void TestColumnXAccumulatesWidths() {
    TableControl table;
    table.SetColumns({Fixed(50.0f), Fixed(70.0f), Proportional(1.0f)});
    table.SetSize(300.0f, 200.0f);
    CHECK(Near(table.ColumnX(0), 0.0f));
    CHECK(Near(table.ColumnX(1), 50.0f));
    CHECK(Near(table.ColumnX(2), 120.0f));
    CHECK(Near(table.ColumnX(9), 300.0f)); // indice fora: soma tudo, sem estourar
}

void TestRowsStartBelowHeaderAndHitTestSkipsIt() {
    TableControl table;
    table.SetColumns({Fixed(100.0f)});
    table.SetHeaderHeight(30.0f);
    table.SetRowHeight(20.0f);
    table.SetRowCount(10);
    table.SetSize(100.0f, 130.0f);

    CHECK(Near(table.RowTop(0), 30.0f));
    CHECK(Near(table.RowTop(2), 70.0f));
    CHECK(table.RowAt({10.0f, 10.0f}) == NoControlIndex);  // cabecalho
    CHECK(table.RowAt({10.0f, 30.0f}) == 0);
    CHECK(table.RowAt({10.0f, 49.0f}) == 0);
    CHECK(table.RowAt({10.0f, 50.0f}) == 1);
    CHECK(table.RowAt({150.0f, 40.0f}) == NoControlIndex); // fora da largura
    CHECK(table.RowAt({10.0f, 500.0f}) == NoControlIndex); // abaixo da ultima linha
}

void TestVirtualRangeCoversOnlyVisibleRows() {
    TableControl table;
    table.SetColumns({Fixed(100.0f)});
    table.SetHeaderHeight(20.0f);
    table.SetRowHeight(10.0f);
    table.SetRowCount(100000);
    table.SetSize(100.0f, 120.0f); // 100 de area para linhas: 10 visiveis
    const VirtualListRange range = table.VisibleRows();
    CHECK(range.firstIndex == 0);
    CHECK(range.count <= 20); // visiveis mais overscan, nunca a lista inteira
    CHECK(range.lastIndex < 100000);
}

void TestScrollClampsToContent() {
    TableControl table;
    table.SetColumns({Fixed(100.0f)});
    table.SetHeaderHeight(20.0f);
    table.SetRowHeight(10.0f);
    table.SetRowCount(20);      // conteudo: 200
    table.SetSize(100.0f, 120.0f); // area de linhas: 100 -> maximo 100
    CHECK(table.ScrollBy(60.0f));
    CHECK(Near(table.rows.scrollOffsetY, 60.0f));
    CHECK(table.ScrollBy(500.0f));
    CHECK(Near(table.rows.scrollOffsetY, 100.0f));
    CHECK(!table.ScrollBy(10.0f)); // ja no fim
    CHECK(!table.ScrollBy(NAN));
}

void TestSelectionKeysAndScrollFollow() {
    TableControl table;
    table.SetColumns({Fixed(100.0f)});
    table.SetHeaderHeight(20.0f);
    table.SetRowHeight(10.0f);
    table.SetRowCount(20);
    table.SetSize(100.0f, 50.0f); // 30 de area para linhas: 3 linhas

    CHECK(table.Key(ListKey::Down));
    CHECK(table.rows.SelectedIndex() == 0);
    CHECK(table.SelectRow(7));
    CHECK(table.rows.SelectedIndex() == 7);
    // A linha 7 tem de estar visivel: a rolagem sobe para que ela caiba na area.
    CHECK(table.rows.scrollOffsetY + table.rows.viewportHeight >= 80.0f);
    CHECK(table.rows.scrollOffsetY <= 70.0f);
}

void TestRowCountChangeClearsStaleSelection() {
    TableControl table;
    table.SetColumns({Fixed(100.0f)});
    table.SetRowCount(10);
    table.SetSize(100.0f, 200.0f);
    CHECK(table.SelectRow(8));
    table.SetRowCount(5); // a linha 8 deixou de existir
    CHECK(table.rows.SelectedIndex() == NoControlIndex);
}

} // namespace

int main() {
    TestFixedKeepsWidthAndProportionalSharesRest();
    TestMinWidthWinsOverTightSpace();
    TestZeroAndNonFiniteWidthsAreSafe();
    TestColumnXAccumulatesWidths();
    TestRowsStartBelowHeaderAndHitTestSkipsIt();
    TestVirtualRangeCoversOnlyVisibleRows();
    TestScrollClampsToContent();
    TestSelectionKeysAndScrollFollow();
    TestRowCountChangeClearsStaleSelection();

    std::printf("table: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
