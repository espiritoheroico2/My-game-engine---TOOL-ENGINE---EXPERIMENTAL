#pragma once

// Arvore de nos como modelo puro (sem desenho, sem janela, sem entrada de plataforma). O chamador traduz
// cliques e teclas em chamadas destes metodos e desenha as linhas visiveis de VisibleRows().
//
// Regras:
//  - Um no e raiz (parent = NoControlIndex) ou filho de outro no. Um no sem filhos e folha: nao expande.
//  - Linhas visiveis: os nos cujos ancestrais estao todos expandidos, em ordem de profundidade. A profundidade
//    de cada linha diz quanto recuar o rotulo. A lista so e refeita quando a estrutura ou a expansao muda.
//  - Teclado: Cima/Baixo andam entre as linhas visiveis (sem volta). Direita expande um no recolhido ou entra
//    no primeiro filho de um no expandido. Esquerda recolhe um no expandido ou sobe para o pai. Enter alterna
//    a expansao de um no com filhos.
//  - Recolher um no esconde os descendentes: se a selecao estava dentro dele, a selecao sobe para o no recolhido.
//  - Controle desabilitado ignora teclas e cliques (retornam false). SetExpanded e chamada de programa.

#include "SpriteUi/Widgets/Controls.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

enum class TreeKey : std::uint8_t { Up, Down, Left, Right, Enter };

struct TreeRow {
    std::size_t node = NoControlIndex;
    std::size_t depth = 0; // Raiz = 0. O desenho recua o rotulo em depth * largura de indentacao.
};

class TreeControl {
public:
    // Adiciona um no. parent = NoControlIndex cria uma raiz. Retorna o indice do no novo.
    // Um parent invalido vira raiz, para que nenhum no fique fora da arvore.
    std::size_t AddNode(std::string label, std::size_t parent = NoControlIndex);

    [[nodiscard]] std::size_t NodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] const std::string& Label(std::size_t node) const noexcept { return nodes_[node].label; }
    [[nodiscard]] std::size_t Parent(std::size_t node) const noexcept { return nodes_[node].parent; }
    [[nodiscard]] bool HasChildren(std::size_t node) const noexcept { return !nodes_[node].children.empty(); }
    [[nodiscard]] bool IsExpanded(std::size_t node) const noexcept { return nodes_[node].expanded; }

    // Linhas visiveis na ordem de desenho (cache: nao e recalculada a cada quadro).
    [[nodiscard]] const std::vector<TreeRow>& VisibleRows() const noexcept { return rows_; }

    // Quantas vezes a lista inteira de linhas foi refeita. Diagnostico para testes de desempenho.
    [[nodiscard]] std::size_t RebuildCount() const noexcept { return rebuildCount_; }

    // Expande ou recolhe um no (chamada de programa; ignora disabled). Folhas nao mudam. Retorna true se mudou.
    bool SetExpanded(std::size_t node, bool expanded);

    // Clique no alternador: alterna a expansao. Ignora disabled. Retorna true se mudou.
    [[nodiscard]] bool Toggle(std::size_t node);

    // Seleciona um no visivel. No escondido ou invalido nao muda nada. Retorna true se a selecao mudou.
    [[nodiscard]] bool Select(std::size_t node);

    // Teclado sobre a selecao. Sem selecao, Cima/Baixo levam a primeira/ultima linha. Retorna true se mudou.
    [[nodiscard]] bool Key(TreeKey key);

    [[nodiscard]] std::size_t Selected() const noexcept { return selected_; }

    bool disabled = false;

    [[nodiscard]] ControlState State() const noexcept {
        return disabled ? ControlState::Disabled : ControlState::Normal;
    }

private:
    struct Node {
        std::string label;
        std::size_t parent = NoControlIndex;
        std::vector<std::size_t> children;
        bool expanded = false;
    };

    void RebuildRows();
    void AppendSubtree(std::size_t node, std::size_t depth, std::vector<TreeRow>& out) const;
    void InsertDescendantRows(std::size_t node);
    void EraseDescendantRows(std::size_t node);
    [[nodiscard]] std::size_t RowOf(std::size_t node) const noexcept;
    [[nodiscard]] bool MoveSelection(int delta);
    [[nodiscard]] bool KeyRight();
    [[nodiscard]] bool KeyLeft();

    std::vector<Node> nodes_;
    std::vector<TreeRow> rows_;
    std::vector<std::size_t> roots_;
    std::size_t selected_ = NoControlIndex;
    std::size_t rebuildCount_ = 0;
};

} // namespace ToolEngine::SpriteUi::Widgets
