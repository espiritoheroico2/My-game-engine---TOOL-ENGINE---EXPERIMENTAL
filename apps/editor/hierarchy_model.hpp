#pragma once

// Modelo da janela Hierarchy: a arvore de objetos da cena, a selecao e a expansao, sem janela, sem SDL e sem
// ImGui. A janela (hierarchy_panel.hpp) so traduz clique, tecla e arraste em chamadas daqui.
//
// A arvore e a TreeControl da SpriteUI. A selecao e guardada pelo id do objeto (nao pela posicao na lista), entao
// um objeto criado ou apagado no meio da lista nao faz a selecao pular para outro.
//
// Exemplo: o Player com a Arma como filho. Recolher o Player esconde a Arma. Se a Arma estava selecionada, a
// selecao sobe para o Player, e o resultado diz que a selecao mudou.
//
// Roda no ctest como Editor.HierarchyModel.

#include "SpriteUi/Widgets/TreeView.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::hierarchy_model {

namespace Wd = ToolEngine::SpriteUi::Widgets;

// Id estavel do objeto (o editor usa indice e geracao). 0 = nenhum.
using Id = std::uint64_t;
inline constexpr Id NoId = 0;

// Tipo do objeto: so muda o icone e a cor. Object e o objeto vazio.
enum class Kind : std::uint8_t { Object, Camera, Light, Audio, Particles, Ui };

struct Item {
    Id id = NoId;
    Id parent = NoId;   // NoId = raiz. Pai fora da lista vira raiz.
    std::string label;
    Kind kind = Kind::Object;
    bool hidden = false;
    bool locked = false;
};

// Resultado de uma acao sobre a arvore. activate = objeto a focar (duplo clique ou Enter numa folha).
struct Outcome {
    bool selectionChanged = false;
    Id activate = NoId;
};

// Faixa de linhas desenhadas: primeira linha e quantidade. So essas linhas entram no quadro.
struct RowRange {
    std::size_t first = 0;
    std::size_t count = 0;
};

// Linhas que cabem na faixa de rolagem: scrollY em pixels, linhas de rowHeight px, janela de viewHeight px.
[[nodiscard]] inline RowRange VisibleRange(std::size_t rowCount, float scrollY, float rowHeight, float viewHeight) noexcept {
    if (rowCount == 0 || rowHeight <= 0.f || viewHeight <= 0.f) return {};
    const float top = scrollY < 0.f ? 0.f : scrollY;
    const std::size_t first = static_cast<std::size_t>(top / rowHeight);
    if (first >= rowCount) return {};
    const std::size_t lines = static_cast<std::size_t>(viewHeight / rowHeight) + 2;   // uma linha parcial de cada lado
    return {first, std::min(lines, rowCount - first)};
}

class HierarchyModel {
 public:
    // Recebe a lista de objetos do quadro. Refaz a arvore so se a estrutura ou um rotulo mudou; senao so guarda os
    // dados (icone, ocultar, travar). Retorna true se refez a arvore.
    bool Sync(std::vector<Item> items) {
        const std::size_t signature = structureSignature(items);
        const bool changed = !built_ || signature != signature_;
        // O indice (id -> posicao) so muda se a sequencia de ids mudar. Comparar os ids e barato e nao aloca;
        // refazer o indice a cada quadro (clear + N insercoes) custava N alocacoes por quadro com o painel aberto.
        bool sameIds = !changed && items.size() == items_.size();
        for (std::size_t i = 0; sameIds && i < items.size(); ++i) sameIds = items[i].id == items_[i].id;
        items_ = std::move(items);
        if (!sameIds) {
            index_.clear();
            for (std::size_t i = 0; i < items_.size(); ++i) index_[items_[i].id] = i;
        }
        if (changed) {
            signature_ = signature;
            built_ = true;
            rebuild();
        }
        return changed;
    }

    [[nodiscard]] const std::vector<Wd::TreeRow>& Rows() const noexcept { return tree_.VisibleRows(); }
    [[nodiscard]] std::size_t RowCount() const noexcept { return tree_.VisibleRows().size(); }
    [[nodiscard]] Id IdOfRow(std::size_t row) const noexcept { return idOfNode(tree_.VisibleRows()[row].node); }
    [[nodiscard]] std::size_t DepthOfRow(std::size_t row) const noexcept { return tree_.VisibleRows()[row].depth; }
    [[nodiscard]] bool RowHasChildren(std::size_t row) const noexcept { return tree_.HasChildren(tree_.VisibleRows()[row].node); }
    [[nodiscard]] bool RowExpanded(std::size_t row) const noexcept { return tree_.IsExpanded(tree_.VisibleRows()[row].node); }

    // Objeto da lista pelo id (nullptr se nao existe). Os dados vem da ultima chamada de Sync.
    [[nodiscard]] const Item* Find(Id id) const {
        const auto it = index_.find(id);
        return it == index_.end() ? nullptr : &items_[it->second];
    }

    // Id do objeto selecionado, ou NoId. Selecao escondida (dentro de um no recolhido) nao aparece.
    [[nodiscard]] Id Selected() const noexcept { return idOfNode(tree_.Selected()); }

    // Seleciona um objeto visivel. Id escondido ou inexistente nao muda nada.
    Outcome Select(Id id) {
        const Id before = Selected();
        const auto it = nodeOf_.find(id);
        if (it != nodeOf_.end()) (void)tree_.Select(it->second);
        return afterTreeChange(before);
    }

    // Alternador de um no (clique na seta). Recolher pode mover a selecao para o no recolhido.
    Outcome Toggle(Id id) {
        const auto it = nodeOf_.find(id);
        if (it == nodeOf_.end()) return {};
        const Id before = Selected();
        if (!tree_.Toggle(it->second)) return {};
        return afterTreeChange(before);
    }

    // Teclas de seta e Enter. Enter numa folha pede para focar o objeto (activate). Enter num no com filhos alterna.
    Outcome Key(Wd::TreeKey key) {
        if (key == Wd::TreeKey::Enter && isLeafSelected()) return Outcome{false, Selected()};
        const Id before = Selected();
        if (!tree_.Key(key)) return {};
        return afterTreeChange(before);
    }

    // Reparentar: child vira filho de parent. Recusa o proprio objeto e qualquer ancestral da arvore (ciclo).
    [[nodiscard]] bool CanReparent(Id child, Id parent) const {
        if (child == NoId || parent == NoId || child == parent) return false;
        if (!index_.count(child) || !index_.count(parent)) return false;
        for (Id up = parent; up != NoId;) {
            if (up == child) return false;
            const Item* item = Find(up);
            up = item ? item->parent : NoId;
        }
        return true;
    }

 private:
    // Assinatura da estrutura: ids, pais e rotulos. Muda quando a arvore precisa ser refeita.
    static std::size_t structureSignature(const std::vector<Item>& items) {
        std::size_t h = items.size();
        std::hash<std::string> strHash;
        for (const Item& item : items) {
            h = h * 1000003u ^ std::hash<Id>{}(item.id);
            h = h * 1000003u ^ std::hash<Id>{}(item.parent);
            h = h * 1000003u ^ strHash(item.label);
        }
        return h;
    }

    [[nodiscard]] Id idOfNode(std::size_t node) const noexcept {
        return node < idOfNode_.size() ? idOfNode_[node] : NoId;
    }

    [[nodiscard]] bool isLeafSelected() const {
        const auto it = nodeOf_.find(Selected());
        return it != nodeOf_.end() && !tree_.HasChildren(it->second);
    }

    // Monta a arvore na ordem dos pais antes dos filhos (pilha, sem recursao: uma cadeia de 10 mil nos nao estoura).
    // Itens presos num ciclo viram raiz, para nenhum objeto sumir da janela.
    void rebuild() {
        tree_ = Wd::TreeControl{};
        nodeOf_.clear();
        idOfNode_.clear();
        std::unordered_map<Id, std::vector<std::size_t>> kids;
        std::vector<std::size_t> roots;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const Id parent = items_[i].parent;
            if (parent != NoId && index_.count(parent) && parent != items_[i].id) kids[parent].push_back(i);
            else roots.push_back(i);
        }
        std::vector<bool> placed(items_.size(), false);
        for (std::size_t pass = 0; pass < 2; ++pass) {
            const std::vector<std::size_t> starts = pass == 0 ? roots : unplacedIndices(placed);
            for (std::size_t start : starts) {
                if (placed[start]) continue;
                placed[start] = true;
                std::vector<std::pair<std::size_t, std::size_t>> stack{{start, Wd::NoControlIndex}};
                while (!stack.empty()) {
                    const auto [item, parentNode] = stack.back();
                    stack.pop_back();
                    const std::size_t node = tree_.AddNode(items_[item].label, parentNode);
                    nodeOf_[items_[item].id] = node;
                    idOfNode_.push_back(items_[item].id);
                    const auto it = kids.find(items_[item].id);
                    if (it == kids.end()) continue;
                    for (auto c = it->second.rbegin(); c != it->second.rend(); ++c) {
                        if (placed[*c]) continue;
                        placed[*c] = true;
                        stack.push_back({*c, node});
                    }
                }
            }
        }
        for (std::size_t node = 0; node < tree_.NodeCount(); ++node) {
            if (!tree_.HasChildren(node)) continue;
            tree_.SetExpanded(node, !collapsed_.count(idOfNode(node)));
        }
        if (selectedId_ != NoId && nodeOf_.count(selectedId_)) (void)tree_.Select(nodeOf_[selectedId_]);
    }

    [[nodiscard]] std::vector<std::size_t> unplacedIndices(const std::vector<bool>& placed) const {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < placed.size(); ++i) if (!placed[i]) out.push_back(i);
        return out;
    }

    // Depois de uma acao na arvore: guarda a expansao (para a proxima reconstrucao) e diz o que mudou.
    Outcome afterTreeChange(Id before) {
        for (std::size_t node = 0; node < tree_.NodeCount(); ++node) {
            if (!tree_.HasChildren(node)) continue;
            const Id id = idOfNode(node);
            if (tree_.IsExpanded(node)) collapsed_.erase(id);
            else collapsed_.insert(id);
        }
        Outcome out;
        selectedId_ = Selected();
        out.selectionChanged = selectedId_ != before;
        return out;
    }

    std::vector<Item> items_;
    std::unordered_map<Id, std::size_t> index_;   // id -> posicao em items_
    std::size_t signature_ = 0;
    bool built_ = false;
    Wd::TreeControl tree_;
    std::unordered_map<Id, std::size_t> nodeOf_;  // id -> no da arvore
    std::vector<Id> idOfNode_;                    // no da arvore -> id
    std::unordered_set<Id> collapsed_;            // nos recolhidos (os demais ficam abertos)
    Id selectedId_ = NoId;
};

}  // namespace editor::hierarchy_model
