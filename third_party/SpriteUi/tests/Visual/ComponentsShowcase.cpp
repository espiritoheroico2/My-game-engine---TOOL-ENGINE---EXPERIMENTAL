// Demonstracao visual dos componentes novos. Cada cena usa os modelos reais (progresso, separador, texto,
// arrasto, tabela, arvore, rolagem, menus, dica, modal e cor) e as primitivas de linha e retangulo
// arredondado. A cena e desenhada pelo renderizador de software do harness, que consome os mesmos itens que a
// GPU.
//
// Uso: spriteui_components_showcase <pasta de saida>. Grava um PNG por cena e confere alguns pixels; se algum
// pixel nao bater, o programa termina com erro. Texto aparece como caixas de glifo (o harness nao tem fonte).

#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Widgets/ColorPicker.hpp"
#include "SpriteUi/Widgets/DragDrop.hpp"
#include "SpriteUi/Widgets/Menu.hpp"
#include "SpriteUi/Widgets/Modal.hpp"
#include "SpriteUi/Widgets/ProgressBar.hpp"
#include "SpriteUi/Widgets/ScrollBar.hpp"
#include "SpriteUi/Widgets/Separator.hpp"
#include "SpriteUi/Widgets/Style.hpp"
#include "SpriteUi/Widgets/Table.hpp"
#include "SpriteUi/Widgets/TextArea.hpp"
#include "SpriteUi/Widgets/Tooltip.hpp"
#include "SpriteUi/Widgets/TreeView.hpp"
#include "SyntheticTextLayout.hpp"
#include "Visual/VisualHarness.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace SpriteUiTests::Visual;
namespace SU = ToolEngine::SpriteUi;
namespace R = ToolEngine::SpriteUi::Rendering;
namespace W = ToolEngine::SpriteUi::Widgets;
namespace Txt = ToolEngine::SpriteUi::Text;

constexpr int kWidth = 640;
constexpr int kHeight = 360;

// Cores em sRGB reto (como o harness pede).
constexpr SU::Vec4 kBackground{0.075f, 0.085f, 0.105f, 1.0f};
constexpr SU::Vec4 kPanel{0.13f, 0.16f, 0.22f, 1.0f};
constexpr SU::Vec4 kTrack{0.22f, 0.25f, 0.33f, 1.0f};
constexpr SU::Vec4 kAccent{0.25f, 0.6f, 0.95f, 1.0f};
constexpr SU::Vec4 kHighlight{0.3f, 0.75f, 0.45f, 1.0f};
constexpr SU::Vec4 kSelection{0.15f, 0.3f, 0.55f, 1.0f};
constexpr SU::Vec4 kText{0.92f, 0.92f, 0.94f, 1.0f};
constexpr SU::Vec4 kDim{0.45f, 0.5f, 0.6f, 1.0f};
constexpr SU::Vec4 kGrid{0.16f, 0.19f, 0.25f, 1.0f};
constexpr SU::Vec4 kRed{0.9f, 0.3f, 0.3f, 1.0f};
constexpr SU::Vec4 kGreen{0.3f, 0.85f, 0.4f, 1.0f};
constexpr SU::Vec4 kStripe{0.1f, 0.12f, 0.17f, 1.0f};

// Tamanho de fonte sintetico e espacamento que da linhas de 14 px (ver SyntheticTextLayout.hpp).
constexpr float kLabelSize = 14.0f;
constexpr float kLabelSpacing = 50.0f;
constexpr float kTableWidth = 400.0f;

int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "[FAIL] pixel: %s\n", what);
    }
}

// Monta a cena empilhando itens na ordem de desenho. Cada item recebe um paintOrder crescente.
class Canvas {
  public:
    Canvas() : scene_(kBackground) {}

    SU::UIHandle NewOwner() noexcept { return SU::UIHandle{++owners_, 1}; }

    std::uint32_t Clip(SU::Rect r) {
        return scene_.AddClip({r.position.x, r.position.y, r.Max().x, r.Max().y});
    }

    void Fill(SU::Rect r, SU::Vec4 srgb, std::uint32_t clip = R::NoClipIndex) {
        Push(R::MakeSolidRect(NewOwner(), r, R::SrgbStraightToLinearPremultiplied(srgb), clip, 0.0f,
                              paint_++));
    }

    void Rounded(SU::Rect r, float radius, SU::Vec4 srgb) {
        const auto item =
            R::MakeRoundedRect(NewOwner(), r, radius, R::SrgbStraightToLinearPremultiplied(srgb),
                               R::NoClipIndex, 0.0f, paint_++);
        if (item) Push(*item);
    }

    void Line(SU::Vec2 from, SU::Vec2 to, float thickness, SU::Vec4 srgb,
              std::uint32_t clip = R::NoClipIndex) {
        const auto item = R::MakeLine(NewOwner(), from, to, thickness,
                                      R::SrgbStraightToLinearPremultiplied(srgb), clip, 0.0f, paint_++);
        if (item) Push(*item);
    }

    void Label(const std::string& text, SU::Vec2 origin, SU::Vec4 srgb, std::uint32_t clip = R::NoClipIndex) {
        const Txt::TextLayoutResult layout =
            SyntheticText::Layout(text, kLabelSize, 0.0f, Txt::TextWrap::None, kLabelSpacing);
        SU::UILayoutRect box;
        box.resolvedPosition = origin;
        box.resolvedSize = layout.size;
        auto glyphs = Txt::GenerateTextRenderItems(layout, box, R::SrgbStraightToLinearPremultiplied(srgb),
                                                   R::WhiteTextureSlice, clip, 0.0f, paint_, NewOwner());
        paint_ += glyphs.size();
        for (auto& glyph : glyphs)
            Push(std::move(glyph));
    }

    // Itens de um widget que desenha com clipes locais: troca os indices pelos clipes desta cena.
    void Adopt(std::vector<R::UIRenderItem> items, const std::vector<R::ClipRectGPU>& localClips) {
        for (R::UIRenderItem& item : items) {
            if (item.clipIndex != R::NoClipIndex) item.clipIndex = scene_.AddClip(localClips[item.clipIndex]);
            item.paintOrder = paint_++;
            Push(std::move(item));
        }
    }

    [[nodiscard]] Image Render() const { return scene_.Render(kWidth, kHeight); }

  private:
    void Push(R::UIRenderItem item) { scene_.Items().push_back(std::move(item)); }

    Scene scene_;
    std::uint64_t paint_ = 0;
    std::uint32_t owners_ = 100; // Acima dos handles de teste, para nao colidir com o harness.
};

// ---------------------------------------------------------------- cena 1: progresso, separador e grade
Image ProgressAndGrid() {
    Canvas c;
    for (float x = 0.0f; x <= kWidth; x += 40.0f)
        c.Line({x, 0.0f}, {x, kHeight}, 1.0f, kGrid);
    for (float y = 0.0f; y <= kHeight; y += 40.0f)
        c.Line({0.0f, y}, {kWidth, y}, 1.0f, kGrid);

    // Gizmo de eixos: setas feitas de linhas girando no ponto de origem.
    const SU::Vec2 origin{40.0f, 330.0f};
    c.Line(origin, {120.0f, 330.0f}, 3.0f, kRed);
    c.Line({120.0f, 330.0f}, {112.0f, 324.0f}, 3.0f, kRed);
    c.Line({120.0f, 330.0f}, {112.0f, 336.0f}, 3.0f, kRed);
    c.Line(origin, {40.0f, 250.0f}, 3.0f, kGreen);
    c.Line({40.0f, 250.0f}, {34.0f, 258.0f}, 3.0f, kGreen);
    c.Line({40.0f, 250.0f}, {46.0f, 258.0f}, 3.0f, kGreen);

    c.Rounded({{40.0f, 40.0f}, {300.0f, 220.0f}}, 14.0f, kPanel);
    c.Label("Progresso", {64.0f, 52.0f}, kText);

    // Determinado 30% e 75%; indeterminado com fase 0.4 recortado ao trilho.
    const SU::Rect track30{{64.0f, 84.0f}, {252.0f, 14.0f}};
    const SU::Rect track75{{64.0f, 124.0f}, {252.0f, 14.0f}};
    const SU::Rect trackBusy{{64.0f, 164.0f}, {252.0f, 14.0f}};
    W::ProgressBarControl determinate30;
    (void)determinate30.SetRange(0.0f, 100.0f);
    (void)determinate30.SetValue(30.0f);
    W::ProgressBarControl determinate75;
    (void)determinate75.SetRange(0.0f, 100.0f);
    (void)determinate75.SetValue(75.0f);
    W::ProgressBarControl busy;
    busy.indeterminate = true;

    c.Fill(track30, kTrack);
    c.Fill(determinate30.FillRect(track30), kAccent);
    c.Fill(track75, kTrack);
    c.Fill(determinate75.FillRect(track75), kAccent);
    c.Fill(trackBusy, kTrack);
    c.Fill(busy.IndeterminateRect(trackBusy, 0.4f), kHighlight, c.Clip(trackBusy));

    // Separador horizontal dentro do painel.
    W::SeparatorControl horizontal;
    c.Fill(horizontal.LineRect({{64.0f, 192.0f}, {252.0f, 20.0f}}), kDim);

    // Painel da direita com separador vertical entre duas colunas.
    c.Rounded({{352.0f, 40.0f}, {248.0f, 220.0f}}, 14.0f, kPanel);
    W::SeparatorControl vertical;
    vertical.axis = W::SeparatorAxis::Vertical;
    c.Fill(vertical.LineRect({{466.0f, 60.0f}, {20.0f, 180.0f}}), kDim);
    c.Label("Esquerda", {372.0f, 64.0f}, kText);
    c.Label("Direita", {490.0f, 64.0f}, kText);

    Image image = c.Render();
    Expect(PixelIs(image, 100, 91, kAccent), "barra de 30 por cento preenchida no trecho inicial");
    Expect(PixelIs(image, 215, 91, kTrack), "barra de 30 por cento vazia depois do preenchimento");
    Expect(PixelIs(image, 120, 131, kAccent), "barra de 75 por cento preenchida");
    Expect(PixelIs(image, 160, 171, kHighlight, 3), "trecho indeterminado visivel no trilho");
    Expect(PixelIs(image, 40, 330, kRed, 3) || PixelIs(image, 60, 330, kRed, 3), "seta do eixo x desenhada");
    return image;
}

// ---------------------------------------------------------------- cena 2: area de texto e arrasto
Image TextAndDrag() {
    Canvas c;
    c.Rounded({{30.0f, 30.0f}, {330.0f, 300.0f}}, 14.0f, kPanel);

    // Area de texto com foco, tudo selecionado: a selecao sai uma faixa por linha.
    const SU::Rect box{{48.0f, 48.0f}, {294.0f, 150.0f}};
    const W::TextAreaStyle style;
    W::TextArea area(std::string(
        "Primeira linha\nSegunda linha, que quebra automaticamente na largura da caixa\nTerceira"));
    area.SetFocus(true);
    area.SelectAll();
    const float width = W::TextArea::ContentWidth(box.size, style);
    area.SetLayout(SyntheticText::Layout(area.Text(), kLabelSize, width, Txt::TextWrap::Word, kLabelSpacing),
                   width);

    std::vector<R::ClipRectGPU> clips;
    W::TextFieldRenderTarget target;
    target.owner = c.NewOwner();
    c.Adopt(area.Build(box, style, target, clips), clips);

    // Arrasto com carga: o arquivo sai da origem, passa do limiar e para sobre a pasta que aceita arquivos.
    const SU::Rect folder{{400.0f, 40.0f}, {200.0f, 110.0f}};
    const SU::Rect imageSlot{{400.0f, 190.0f}, {200.0f, 70.0f}};
    const SU::UIHandle folderOwner = c.NewOwner();
    const SU::UIHandle slotOwner = c.NewOwner();
    const std::vector<W::DropTarget> targets{
        W::DropTarget{folderOwner, folder, "file"},
        W::DropTarget{slotOwner, imageSlot, "image"},
    };

    W::DragDropControl drag;
    (void)drag.PointerDown(c.NewOwner(), {90.0f, 300.0f}, W::DragPayload{"file", "relatorio.txt"});
    (void)drag.PointerMove({130.0f, 280.0f}, targets);
    (void)drag.PointerMove({470.0f, 100.0f}, targets);
    const bool hoverFolder = drag.HoveredTarget() && *drag.HoveredTarget() == folderOwner;

    c.Rounded(folder, 12.0f, hoverFolder ? kHighlight : kPanel);
    c.Label("Pasta (aceita arquivo)", {416.0f, 52.0f}, kText);
    c.Rounded(imageSlot, 12.0f, kPanel);
    c.Label("Imagens (so imagem)", {416.0f, 202.0f}, kDim);

    // Carga sendo arrastada, presa ao ponteiro.
    const SU::Vec2 at = drag.Pointer();
    c.Rounded({{at.x - 60.0f, at.y - 12.0f}, {120.0f, 24.0f}}, 8.0f, kAccent);
    c.Label(drag.Payload().data, {at.x - 52.0f, at.y - 6.0f}, kText);

    Image image = c.Render();
    Expect(PixelIs(image, 560, 60, kHighlight), "pasta fica destacada quando aceita a carga sob o ponteiro");
    Expect(PixelIs(image, 560, 220, kPanel), "slot de imagem nao destaca carga de arquivo");
    Expect(!PixelIs(image, 200, 90, style.focusBackground, 3), "selecao da area de texto desenhada");
    return image;
}

// ---------------------------------------------------------------- cena 3: tabela, arvore e rolagem
Image TableTreeScroll() {
    Canvas c;

    // Tabela de 12 linhas com 3 colunas, rolada por 3 linhas e com a quarta linha selecionada.
    W::TableControl table;
    table.SetColumns({
        W::TableColumn{"Nome", W::ColumnSizing::Fixed, 150.0f, 1.0f, 24.0f},
        W::TableColumn{"Tipo", W::ColumnSizing::Proportional, 100.0f, 1.0f, 24.0f},
        W::TableColumn{"Tamanho", W::ColumnSizing::Fixed, 90.0f, 1.0f, 24.0f},
    });
    const float tableHeight = 230.0f;
    table.SetSize(kTableWidth, tableHeight);
    table.SetHeaderHeight(28.0f);
    table.SetRowHeight(24.0f);
    table.SetRowCount(12);
    (void)table.SelectRow(3);
    (void)table.ScrollBy(72.0f);

    const SU::Vec2 tableOrigin{40.0f, 40.0f};
    c.Fill({tableOrigin, {kTableWidth, table.HeaderHeight()}}, kTrack);
    const std::vector<std::string> titles{"Nome", "Tipo", "Tamanho"};
    for (std::size_t col = 0; col < titles.size(); ++col) {
        c.Label(titles[col], {tableOrigin.x + table.ColumnX(col) + 8.0f, tableOrigin.y + 6.0f}, kText);
    }

    const SU::Rect body{{tableOrigin.x, tableOrigin.y + table.HeaderHeight()},
                        {kTableWidth, tableHeight - table.HeaderHeight()}};
    const std::uint32_t bodyClip = c.Clip(body);
    const W::VirtualListRange visible = table.VisibleRows();
    for (std::size_t row = visible.firstIndex; row <= visible.lastIndex && row < 12; ++row) {
        const float top = tableOrigin.y + table.RowTop(row);
        const SU::Vec4 fill =
            row == table.rows.SelectedIndex() ? kSelection : (row % 2 == 0 ? kPanel : kStripe);
        c.Fill({{tableOrigin.x, top}, {kTableWidth, table.rows.items.GetItemHeight()}}, fill, bodyClip);
        const std::string name = "arquivo_" + std::to_string(row) + ".dat";
        c.Label(name, {tableOrigin.x + table.ColumnX(0) + 8.0f, top + 5.0f}, kText, bodyClip);
        c.Label(row % 2 == 0 ? "imagem" : "texto", {tableOrigin.x + table.ColumnX(1) + 8.0f, top + 5.0f},
                kDim, bodyClip);
        c.Label(std::to_string(row * 12) + " KB", {tableOrigin.x + table.ColumnX(2) + 8.0f, top + 5.0f}, kDim,
                bodyClip);
    }

    // Barra de rolagem da tabela: o polegar acompanha o deslocamento de table.rows.
    W::UIScrollViewComponent view;
    view.contentSize = {kTableWidth, 12.0f * table.rows.items.GetItemHeight()};
    view.viewportSize = {kTableWidth, body.size.y};
    view.scrollOffset = {0.0f, table.rows.scrollOffsetY};
    W::ScrollBarControl bar;
    const SU::Rect track{{444.0f, body.position.y}, {10.0f, body.size.y}};
    bar.SetTrack(track);
    c.Fill(track, kTrack);
    c.Rounded(bar.ThumbRect(view), 5.0f, kAccent);

    // Arvore ao lado: a raiz e a pasta de assets estao abertas e a pasta de assets esta selecionada.
    W::TreeControl tree;
    const std::size_t root = tree.AddNode("Projeto");
    (void)tree.AddNode("Cena", root);
    const std::size_t assets = tree.AddNode("Assets", root);
    (void)tree.AddNode("Personagem.png", assets);
    (void)tree.AddNode("Terreno.png", assets);
    (void)tree.SetExpanded(root, true);
    (void)tree.SetExpanded(assets, true);
    (void)tree.Select(assets);

    c.Rounded({{470.0f, 40.0f}, {154.0f, 230.0f}}, 10.0f, kPanel);
    const std::vector<W::TreeRow> rows = tree.VisibleRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float y = 48.0f + static_cast<float>(i) * 20.0f;
        const float x = 480.0f + static_cast<float>(rows[i].depth) * 12.0f;
        if (rows[i].node == tree.Selected()) c.Fill({{474.0f, y - 2.0f}, {146.0f, 18.0f}}, kSelection);
        if (tree.HasChildren(rows[i].node)) {
            c.Fill({{x, y + 4.0f}, {8.0f, 8.0f}}, tree.IsExpanded(rows[i].node) ? kAccent : kDim);
        }
        c.Label(tree.Label(rows[i].node), {x + 14.0f, y}, kText);
    }

    Image image = c.Render();
    const float selectedTop = tableOrigin.y + table.RowTop(3);
    Expect(PixelIs(image, 300, static_cast<int>(selectedTop) + 10, kSelection, 4) ||
               PixelIs(image, 300, static_cast<int>(selectedTop) + 3, kSelection, 4),
           "linha selecionada da tabela destacada");
    Expect(PixelIs(image, 449, 150, kAccent, 4) || PixelIs(image, 449, 120, kAccent, 4) ||
               PixelIs(image, 449, 180, kAccent, 4),
           "polegar da barra de rolagem visivel");
    return image;
}

// ---------------------------------------------------------------- cena 4: menus, submenu e dica
W::MenuItem Action(std::string label, std::string shortcut = {}, bool disabled = false) {
    return W::MenuItem{std::move(label), std::move(shortcut), W::MenuItemKind::Action, disabled, {}};
}

W::MenuItem Submenu(std::string label, std::vector<W::MenuItem> children) {
    return W::MenuItem{std::move(label), {}, W::MenuItemKind::Submenu, false, std::move(children)};
}

W::MenuItem Separator() {
    return W::MenuItem{{}, {}, W::MenuItemKind::Separator, false, {}};
}

constexpr float kMenuRowHeight = 26.0f;
constexpr float kMenuSeparatorHeight = 8.0f;
constexpr float kMenuWidth = 200.0f;

// Altura de cada linha de um menu: separadores sao mais baixos que os itens.
float RowTop(const std::vector<W::MenuItem>& items, std::size_t index) {
    float y = 0.0f;
    for (std::size_t i = 0; i < index && i < items.size(); ++i) {
        y += items[i].kind == W::MenuItemKind::Separator ? kMenuSeparatorHeight : kMenuRowHeight;
    }
    return y;
}

void DrawMenuLevel(Canvas& c, const W::MenuBarControl& bar, std::size_t depth, SU::Vec2 origin) {
    const std::vector<W::MenuItem>& items = bar.Dropdown().Items(depth);
    const std::size_t highlighted = bar.Dropdown().Highlighted(depth);
    float height = 0.0f;
    for (const W::MenuItem& item : items) {
        height += item.kind == W::MenuItemKind::Separator ? kMenuSeparatorHeight : kMenuRowHeight;
    }
    c.Rounded({origin, {kMenuWidth, height + 8.0f}}, 6.0f, kPanel);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float y = origin.y + 4.0f + RowTop(items, i);
        const W::MenuItem& item = items[i];
        if (item.kind == W::MenuItemKind::Separator) {
            c.Fill({{origin.x + 8.0f, y + 3.0f}, {kMenuWidth - 16.0f, 2.0f}}, kDim);
            continue;
        }
        if (i == highlighted) c.Fill({{origin.x + 4.0f, y}, {kMenuWidth - 8.0f, kMenuRowHeight}}, kSelection);
        const SU::Vec4 color = item.disabled ? kDim : kText;
        c.Label(item.label, {origin.x + 12.0f, y + 6.0f}, color);
        if (!item.shortcut.empty()) {
            const float shortcutWidth = static_cast<float>(item.shortcut.size()) * 8.0f;
            c.Label(item.shortcut, {origin.x + kMenuWidth - 12.0f - shortcutWidth, y + 6.0f}, kDim);
        }
        if (item.kind == W::MenuItemKind::Submenu)
            c.Label(">", {origin.x + kMenuWidth - 18.0f, y + 6.0f}, kText);
    }
}

Image MenuAndTooltip() {
    Canvas c;

    W::MenuBarControl bar;
    bar.SetMenus({
        W::MenuBarEntry{"Arquivo",
                        {Action("Novo", "Ctrl+N"), Action("Abrir", "Ctrl+O"), Separator(),
                         Submenu("Exportar", {Action("PNG"), Action("JPG")}), Separator(),
                         Action("Sair", {}, true)},
                        false},
        W::MenuBarEntry{"Editar", {Action("Desfazer", "Ctrl+Z"), Action("Refazer", "Ctrl+Y")}, false},
        W::MenuBarEntry{"Ajuda", {Action("Sobre")}, false},
    });
    (void)bar.OpenMenu(0);
    (void)bar.HoverItem(3); // "Exportar", um submenu
    (void)bar.Key(W::MenuKey::Right);

    // Cabecalho da barra de menus, com o menu aberto destacado.
    c.Fill({{0.0f, 0.0f}, {kWidth, 30.0f}}, kTrack);
    for (std::size_t i = 0; i < bar.Menus().size(); ++i) {
        const float x = 8.0f + static_cast<float>(i) * 90.0f;
        if (i == bar.ActiveMenu()) c.Fill({{x - 4.0f, 2.0f}, {84.0f, 26.0f}}, kSelection);
        c.Label(bar.Menus()[i].label, {x, 8.0f}, kText);
    }

    if (bar.IsOpen()) {
        const float dropdownY = 30.0f;
        DrawMenuLevel(c, bar, 0, {8.0f, dropdownY});
        if (bar.Dropdown().Depth() > 1) {
            const std::size_t parent = bar.Dropdown().OpenSubmenu(0);
            const float submenuY = dropdownY + 4.0f + RowTop(bar.Dropdown().Items(0), parent) - 4.0f;
            DrawMenuLevel(c, bar, 1, {8.0f + kMenuWidth + 4.0f, submenuY});
        }
    }

    // Dica de ferramenta: aparece depois do atraso sobre o alvo, na posicao colocada dentro da tela.
    W::TooltipConfig config;
    W::TooltipControl tip(config);
    tip.Enter(1, {440.0f, 280.0f}, 0.0f);
    (void)tip.Tick(1.0f);
    const SU::Vec2 tipSize{170.0f, 40.0f};
    const SU::Rect tipRect =
        W::PlaceTooltip(tip.Anchor(), tipSize, SU::Rect{{0.0f, 0.0f}, {kWidth, kHeight}}, config.offset);
    if (tip.Visible(1.0f)) {
        c.Rounded(tipRect, 6.0f, kAccent);
        c.Label("Salvar o arquivo atual", {tipRect.position.x + 10.0f, tipRect.position.y + 13.0f},
                kBackground);
    }

    Image image = c.Render();
    Expect(PixelIs(image, 40, 8, kSelection, 4) || PixelIs(image, 40, 16, kSelection, 4),
           "menu da barra destacado no cabecalho");
    Expect(PixelIs(image, 30, 104, kSelection, 4), "linha em destaque no menu aberto");
    Expect(PixelIs(image, static_cast<int>(tipRect.position.x) + 4, static_cast<int>(tipRect.position.y) + 4,
                   kAccent, 4),
           "dica aparece sobre a tela depois do atraso");
    return image;
}

// ---------------------------------------------------------------- cena 5: modal e seletor de cor
Image ModalAndColor() {
    Canvas c;
    c.Rounded({{30.0f, 30.0f}, {580.0f, 300.0f}}, 14.0f, kPanel);
    c.Label("Conteudo atras do dialogo", {60.0f, 56.0f}, kText);

    // Dialogo modal aberto com dois botoes; o foco comeca no segundo (padrao).
    W::ModalControl modal;
    const SU::Rect dialog{{170.0f, 60.0f}, {300.0f, 240.0f}};
    modal.Open(dialog, 2, 1, 77);

    // Fundo escurecido: uma camada translucida sobre a cena inteira.
    c.Fill({{0.0f, 0.0f}, {kWidth, kHeight}}, {0.0f, 0.0f, 0.0f, 0.55f});
    c.Rounded(modal.Dialog(), 12.0f, kPanel);
    c.Label("Cor do traco", {dialog.position.x + 20.0f, dialog.position.y + 16.0f}, kText);

    // Seletor de cor: quatro campos de 0 a 255 e a amostra com o valor original ao lado.
    W::ColorPickerControl picker(SU::Vec4{0.9f, 0.4f, 0.2f, 1.0f});
    (void)picker.SetChannel(W::ColorChannel::Red, 60);
    (void)picker.SetChannel(W::ColorChannel::Alpha, 180);
    const std::vector<std::pair<W::ColorChannel, const char*>> channels{
        {W::ColorChannel::Red, "R"},
        {W::ColorChannel::Green, "G"},
        {W::ColorChannel::Blue, "B"},
        {W::ColorChannel::Alpha, "A"},
    };
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const float x = dialog.position.x + 20.0f + static_cast<float>(i) * 66.0f;
        c.Label(channels[i].second, {x + 4.0f, dialog.position.y + 52.0f}, kDim);
        c.Rounded({{x, dialog.position.y + 70.0f}, {56.0f, 30.0f}}, 6.0f, kBackground);
        c.Label(picker.ChannelText(channels[i].first), {x + 8.0f, dialog.position.y + 78.0f}, kText);
    }
    const SU::Vec4 sample = picker.Sample();
    c.Fill({{dialog.position.x + 20.0f, dialog.position.y + 120.0f}, {130.0f, 40.0f}}, sample);
    const SU::Vec4 original = picker.Original();
    c.Fill({{dialog.position.x + 150.0f, dialog.position.y + 120.0f}, {130.0f, 40.0f}}, original);

    // Botoes com anel de foco no botao que tem o foco do modal.
    const std::vector<std::string> buttons{"Cancelar", "OK"};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const SU::Rect button{
            {dialog.position.x + 20.0f + static_cast<float>(i) * 140.0f, dialog.position.y + 186.0f},
            {120.0f, 32.0f}};
        if (i == modal.Focus())
            c.Rounded({{button.position.x - 3.0f, button.position.y - 3.0f},
                       {button.size.x + 6.0f, button.size.y + 6.0f}},
                      8.0f, kAccent);
        c.Rounded(button, 6.0f, kTrack);
        c.Label(buttons[i], {button.position.x + 14.0f, button.position.y + 9.0f}, kText);
    }

    Image image = c.Render();
    Expect(image.Srgb8(20, 20)[0] < 40, "fundo fora do dialogo escurecido pela camada");
    Expect(PixelIs(image, 250, 292, kPanel, 4), "dialogo modal desenhado sobre o fundo");
    return image;
}

bool WritePng(const Image& image, const std::filesystem::path& path) {
    const auto bytes = EncodePng(image);
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path outDir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("components");
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    const std::vector<std::pair<const char*, Image (*)()>> scenes{
        {"components-1-progresso-grade.png", &ProgressAndGrid},
        {"components-2-texto-arrasto.png", &TextAndDrag},
        {"components-3-tabela-arvore-rolagem.png", &TableTreeScroll},
        {"components-4-menu-dica.png", &MenuAndTooltip},
        {"components-5-modal-cor.png", &ModalAndColor},
    };
    for (const auto& [name, build] : scenes) {
        const Image image = build();
        const std::filesystem::path path = outDir / name;
        if (!WritePng(image, path)) {
            ++failures;
            std::fprintf(stderr, "[FAIL] nao gravou %s\n", path.string().c_str());
            continue;
        }
        std::printf("%s\n", path.string().c_str());
    }
    std::printf("components showcase: %zu cenas, %d falhas de pixel\n", scenes.size(), failures);
    return failures == 0 ? 0 : 1;
}
