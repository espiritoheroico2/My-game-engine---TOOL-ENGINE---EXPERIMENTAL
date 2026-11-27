#pragma once

// Dicas (tooltips) do painel de Assets: tabela "id do controle -> explicacao curta em ingles".
// Mesmo padrao do inspector_tips.hpp: o texto em ingles e a chave de traducao; o pacote de idioma
// (assets/lang/*.json) traz a versao em pt-BR e es com a mesma frase como chave.

#include <string_view>
#include <unordered_map>

namespace editor::assets_tips {

// Devolve a dica (em ingles) do controle `id`, ou nullptr se nao houver. O ponteiro vale para sempre (literal).
inline const char* Lookup(std::string_view id) {
    static const std::unordered_map<std::string_view, const char*> table = {
        // Moldura
        {"help", "Opens the Help page about the Assets window."},
        // Navegacao
        {"up", "Goes up to the parent folder."},
        {"crumb", "Path of the current folder. Click a name to jump to that folder."},
        // Ferramentas
        {"import", "Copies files from your computer into the current folder. You can also drop them onto the editor window."},
        {"newFolder", "Creates a new folder inside the current folder. Type a name and confirm."},
        {"newFile", "Creates a new empty text file inside the current folder. Type a name and confirm."},
        {"viewList", "Shows the files as a list with type and size."},
        {"viewIcons", "Shows the files as big icons. Materials show a preview picture."},
        {"search", "Type to filter the files of this folder by name."},
        // Itens da grade
        {"itemFolder", "Folder: click to select, double-click to open it."},
        {"itemModel", "3D model: click to select, double-click to add it to the scene."},
        {"itemAudio", "Audio file: click to select, double-click to add it to the scene."},
        {"itemScene", "Scene file: click to select, double-click to open this scene."},
        {"itemImage", "Image: click to select. Use it as a texture in the Inspector."},
        {"itemText", "Text file: click to select, double-click to edit it here."},
        {"itemMaterial", "Material: click to select; the preview is generated from the values in its JSON file. Double-click to edit it."},
        {"itemFile", "File: click to select."},
        {"scrollbar", "Scroll the list. You can also use the mouse wheel."},
        {"textArea", "Text editor. Typing adds text at the end of the file; Save writes it to disk."},
        // Barra de acoes
        {"open", "Opens the selected folder, scene or text file."},
        {"save", "Saves the text you typed into the file on disk."},
        {"add", "Adds the selected model or sound to the current scene as a new object."},
        {"assign", "Assigns the selected model or sound to the object selected in the scene."},
        {"rename", "Renames the selected file or folder."},
        {"closeText", "Closes the text editor without leaving the Assets window."},
        {"delete", "Deletes the selected file or folder. It asks for confirmation first."},
        // Campo de nome e confirmacao
        {"nameField", "Type the name here, then confirm with Enter or the button on the right."},
        {"confirm", "Confirms the action in progress (create, rename or delete)."},
        {"cancel", "Cancels the action in progress and changes nothing."},
        {"status", "Messages and usage hints for this window."},
    };
    const auto it = table.find(id);
    return it == table.end() ? nullptr : it->second;
}

}  // namespace editor::assets_tips
