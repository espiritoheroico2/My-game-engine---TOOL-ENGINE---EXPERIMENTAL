#pragma once

// Linhas genericas das secoes de Configuracoes em SpriteUI (Lighting, Physics, Game, Optimizing, Language, Tags e
// Project). O main.cpp monta as linhas a partir do Editor (rotulo, valor, faixa e opcoes); o painel as desenha e,
// quando o usuario mexe, devolve um pedido Field::Form com o id da linha. O main.cpp grava o valor no Editor.
// Sem SDL: os tipos ficam fora do painel para poderem ser usados nos testes.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::settings_form {

// Tipo de linha. Heading e Gap so organizam; Note e so texto; os demais recebem interacao.
enum class RowKind : std::uint8_t {
    Heading,   // subtitulo da secao (texto em negrito)
    Note,      // texto explicativo (quebra de linha automatica)
    Slider,    // valor entre lo e hi (isInt: passo de 1). Mostra `note` ao lado, se houver
    Check,     // liga/desliga (on); o painel o desenha como TOGGLE (trilho + bolinha), nao como caixa de selecao
    Dropdown,  // combo: options, escolhida = index
    Radio,     // lista de opcoes em radio: options, escolhida = index
    Button,    // botao com `label`; cada clique gera um pedido
    Text,      // campo de texto (text); o pedido sai ao confirmar (Enter ou perder o foco)
    Gap        // espaco vertical
};

struct Row {
    RowKind kind = RowKind::Note;
    int id = 0;                        // identifica o campo para o main.cpp (ver FormId em main.cpp)
    std::string label;                 // rotulo a esquerda (Heading, Note e Button usam como texto)
    std::string note;                  // texto ao lado do controle (ex.: "3 px"), ou ajuda abaixo
    float value = 0.f;                 // Slider
    float lo = 0.f;                    // Slider: minimo
    float hi = 1.f;                    // Slider: maximo
    bool isInt = false;                // Slider: valores inteiros
    bool on = false;                   // Check
    int index = 0;                     // Dropdown, Radio: opcao escolhida
    std::vector<std::string> options;  // Dropdown, Radio
    std::string text;                  // Text: conteudo atual
    bool enabled = true;               // false = mostrado apagado e sem interacao
    std::string tip;                   // dica (tooltip) em ingles (chave de traducao); vazio = usa DefaultTip(id)
};

using Rows = std::vector<Row>;

// Dica padrao de cada linha, pelo id (ver FormId em main.cpp). Texto em ingles: o painel traduz com T(...).
// Devolve nullptr se o id nao tem dica. Uma linha pode sobrescrever com Row::tip.
[[nodiscard]] inline const char* DefaultTip(int id) noexcept {
    if (id >= 110 && id <= 115) return "Image shown on this side of the skybox. (none) uses the solid color.";
    if (id >= 210 && id <= 216) return "Sets the world gravity to this ready-made value.";
    if (id >= 1000 && id < 2000) return "Removes this tag link.";
    if (id >= 2000 && id < 3000) return "Renames this tag.";
    switch (id) {
        // Presets de configuracao (topo de Settings)
        case 50: return "Opens a file browser to load a configuration file (.json) and apply it to the current scene.";
        case 51: return "Name of the configuration file to save or delete. It is saved in the project's Configs folder.";
        case 52: return "Saves the current render and physics settings to a file with this name.";
        case 53: return "Deletes the configuration file with this name. Click twice to confirm.";
        // Game
        case 100: return "Shows or hides the reference grid in the Scene window.";
        case 101: return "Shows or hides the reference grid in the Game window.";
        case 102: return "Shows or hides the icons of cameras, lights and sounds in the Scene window.";
        case 103: return "How the screen is divided when two cameras have the 'Principal' tag.";
        case 104: return "Each camera fills its whole half of the screen, cropping the sides instead of leaving black bars.";
        case 105: return "Draws a sky made of 6 images around the scene.";
        case 120: case 121: case 122: case 123: return "Color of the sides of the skybox that have no image.";
        // Physics
        case 200: return "Turns the world physics of this scene on or off.";
        case 201: case 202: case 203: return "Gravity acceleration on this axis, in meters per second squared.";
        case 220: return "How many times the solver refines contacts each step. More is more stable but slower.";
        case 221: return "How many physics steps run per second. More is more precise but costs more CPU.";
        case 222: return "Draws the collision shapes in the Scene window.";
        case 223: return "Only bodies within this distance of the camera are simulated. 0 simulates everything.";
        case 224: return "Softens contacts so stacks of objects stay stable.";
        case 225: return "Reuses the previous step's contact forces so resting objects settle faster.";
        case 226: return "Groups of touching bodies go to sleep together when they stop moving, saving CPU.";
        case 227: return "Smooths the drawn motion between physics steps, useful on high refresh rate screens.";
        case 228: return "How stiff contacts are. Higher values make objects feel harder and less squishy.";
        case 229: return "How fast contact bounce dies out. Higher values absorb more energy.";
        // Optimizing
        case 300: return "Limits how many frames per second the editor draws. VSync follows the monitor.";
        case 301: return "How many mesh chunks stay in memory. A larger cache loads faster but uses more memory.";
        case 302: return "Scales the whole editor interface.";
        case 303: return "Draws only when something changes, which saves CPU and battery.";
        case 304: return "Replaces far away meshes with simpler versions to gain speed.";
        case 305: return "Meshes farther than this distance, in meters, are simplified.";
        case 306: return "How many faces are removed from simplified meshes.";
        case 307: return "Enlarges the image on the GPU with AMD FidelityFX Super Resolution 1. The render size comes from the FSR quality mode. Needs Vulkan or Direct3D.";
        case 308: return "Strength of the sharpening pass that runs after FSR 1. 0 is off, 1 is the strongest.";
        case 309: return "Chooses AMD FSR 4. It needs a compatible GPU on Vulkan. The choice is saved, but it is applied only in the next step (motion vectors).";
        case 310: return "Frame generation (FSR 3.1) creates extra frames. It is not integrated yet.";
        case 311: return "How much FSR enlarges the image. A higher factor renders fewer pixels (faster) and enlarges more. It sets the render scale while FSR is on.";
        case 312: return "Render size as a fraction of the output size (0.5 to 1.0) for the Custom FSR mode.";
        // Language
        case 400: return "Chooses the language of the editor interface.";
        case 401: return "Reads the language files from disk again, to see edits without restarting.";
        case 402: return "Saves a file with every phrase of the editor, ready to translate.";
        // Tags
        case 500: return "Built-in tag that the alias will point to.";
        case 501: return "Your own name for the selected built-in tag.";
        case 502: return "Links the alias to the selected tag.";
        case 503: return "New name for the tag being renamed.";
        case 504: return "Cancels the rename.";
        // Project
        case 600: return "Command used to open script files in your code editor.";
        // Lighting
        case 700: return "Skips objects that are outside the camera view.";
        case 701: return "Skips objects hidden behind large objects.";
        case 702: return "Objects farther than this are not drawn. 0 uses each camera's Far value.";
        case 703: case 704: case 705: case 713: return "Color of the sun light.";
        case 706: return "Brightness of the sun light.";
        case 707: return "Compass direction the sun comes from, in degrees.";
        case 708: return "Height of the sun above the horizon, in degrees.";
        case 709: case 710: case 711: return "Color of the ambient light that reaches everything.";
        case 712: return "Makes the sun cast shadows.";
        case 720: return "Quality preset for the lighting. Custom lets you set every value.";
        case 721: return "Enables manual control of the lighting values below.";
        case 722: return "Number of shadow cascades. More cascades give sharper shadows near the camera.";
        case 723: return "Resolution of each shadow map. Higher is sharper and slower.";
        case 724: return "Softness of the shadow edges.";
        case 725: return "Maximum lights that affect each screen cluster.";
        case 726: return "Size of the light index list used by the clustered lighting.";
        case 727: return "Lets point lights compute their reach automatically.";
        case 730: return "Maximum number of point lights drawn at once.";
        case 731: return "Maximum number of spot lights drawn at once.";
        case 740: return "Turns shadows on or off.";
        case 741: return "How far from the camera shadows are drawn.";
        case 742: return "Balances cascade sizes: higher values give more detail far away.";
        case 743: return "Distance over which one cascade fades into the next.";
        case 744: return "Extra distance behind the view that still casts shadows.";
        case 745: return "Pushes shadows slightly to avoid dark stripes on surfaces. Too much detaches shadows.";
        case 746: return "Pushes shadows along the surface normal to avoid acne on slopes.";
        case 747: return "Snaps the shadow map to pixels to stop shimmering while the camera moves.";
        case 750: return "Maximum number of point lights that cast shadows.";
        case 751: return "Resolution of the shadow maps of point lights.";
        case 760: return "Darkens creases and corners where light would be blocked (screen space ambient occlusion).";
        case 761: return "Number of samples per pixel. More is smoother and slower.";
        case 762: return "Reach of the occlusion effect, in meters.";
        case 763: return "Reduces false darkening on flat surfaces.";
        case 764: return "Strength of the occlusion darkening.";
        case 770: return "Uses ray tracing for lighting effects.";
        case 771: return "Computes ambient occlusion with rays on the CPU.";
        case 772: return "Rays per pixel for ray-traced occlusion. More is smoother and slower.";
        case 773: return "Reach of the ray-traced occlusion, in meters.";
        case 780: return "Makes bright areas glow.";
        case 781: return "Strength of the glow.";
        case 790: return "Amount of ambient light.";
        case 791: return "Exposure applied before the color is calculated.";
        case 800: return "Turns the post-processing color adjustments on or off.";
        case 801: return "Overall brightness of the final image.";
        case 802: return "Difference between dark and bright areas.";
        case 803: return "Color intensity. 0 is black and white.";
        case 804: case 805: case 806: return "Color tint applied over the final image.";
        case 807: return "How strong the tint is.";
        case 808: return "Darkens the corners of the screen.";
        case 809: return "How far the darkening reaches into the screen.";
        case 810: return "Makes the image look crisper.";
        case 811: return "Restores every post-processing value to its default.";
        default: return nullptr;
    }
}

// Valor de um deslizante de linha: NaN vira o minimo; com isInt, arredonda para inteiro; depois prende em [lo, hi].
// Se hi for menor que lo, devolve lo. Regra pura, sem SDL, para o teste cobrir.
[[nodiscard]] inline float ClampSliderValue(float value, float lo, float hi, bool isInt) noexcept {
    if (std::isnan(value)) value = lo;
    if (hi < lo) return lo;
    if (isInt) value = std::round(value);
    return std::clamp(value, lo, hi);
}

} // namespace editor::settings_form
