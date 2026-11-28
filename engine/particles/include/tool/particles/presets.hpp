// ToolEngine - Particulas: efeitos prontos (fumaca, fogo, fluido) como texto JSON no formato de effect.hpp.
// Ficam embutidos para funcionar sem arquivos soltos; o editor grava uma copia em <projeto>/Effects na primeira vez.
#pragma once
#include <string_view>

namespace tool::particles {

// Nomes dos presets (sem extensao): o arquivo do projeto e Effects/<nome>.json.
inline constexpr const char* kPresetNames[] = {"smoke", "fire", "fluid"};
inline constexpr int kPresetCount = 3;

// Fumaca estilo "dispersao" (cidade): nasce em uma caixa larga, sobe por empuxo, e a turbulencia espalha e mistura.
// As particulas crescem e somem devagar. Alfa ordenado, com fade suave nas superficies.
inline constexpr const char* kPresetSmoke = R"JSON({
  "name": "smoke", "shader": "alpha", "emissive": 0, "soft": 0.8,
  "emitter": { "rate": 140, "shape": "box", "box": [1.5, 0.15, 1.5], "seed": 11, "loop": true },
  "init": { "life": [4.0, 6.5], "speed": [0.4, 1.1], "direction": [0, 1, 0], "coneAngle": 25, "velocity": "cone",
            "size": [0.7, 1.1], "color": [0.62, 0.62, 0.65, 1], "temperature": 1, "rotation": [0, 360] },
  "modules": [
    { "type": "buoyancy", "value": 1.3, "cooling": 0.35 },
    { "type": "turbulence", "amplitude": 1.6, "frequency": 0.35, "scroll": 0.25, "seed": 5, "every": 1 },
    { "type": "drag", "value": 0.45 },
    { "type": "sizeOverLife", "from": 1.0, "to": 3.4 },
    { "type": "colorOverLife", "from": [0.62, 0.62, 0.65, 1], "to": [0.28, 0.28, 0.31, 1] },
    { "type": "alphaOverLife", "from": 0.75, "to": 0.0 }
  ]
})JSON";

// Fogo: base em disco (cone), sobe rapido por empuxo e esfria de amarelo para vermelho; usa o brilho (bloom).
inline constexpr const char* kPresetFire = R"JSON({
  "name": "fire", "shader": "bloom", "emissive": 3.0, "soft": 0.3,
  "emitter": { "rate": 240, "shape": "cone", "radius": 0.3, "seed": 21, "loop": true },
  "init": { "life": [0.6, 1.2], "speed": [1.4, 2.8], "direction": [0, 1, 0], "coneAngle": 12, "velocity": "cone",
            "size": [0.55, 0.85], "color": [1.0, 0.85, 0.35, 1], "temperature": 1, "rotation": [0, 360] },
  "modules": [
    { "type": "buoyancy", "value": 4.0, "cooling": 1.2 },
    { "type": "turbulence", "amplitude": 2.6, "frequency": 1.2, "scroll": 1.0, "seed": 3, "every": 1 },
    { "type": "sizeOverLife", "from": 1.0, "to": 0.1 },
    { "type": "colorOverLife", "from": [1.0, 0.85, 0.35, 1], "to": [0.9, 0.1, 0.02, 1] },
    { "type": "alphaOverLife", "from": 1.0, "to": 0.0 }
  ]
})JSON";

// Fluido simples (fonte de agua): jato em cone, gravidade e arraste leve; gotas azuis translucidas.
inline constexpr const char* kPresetFluid = R"JSON({
  "name": "fluid", "shader": "alpha", "emissive": 0, "soft": 0.2,
  "emitter": { "rate": 600, "shape": "disc", "radius": 0.1, "seed": 31, "loop": true },
  "init": { "life": [1.6, 2.2], "speed": [5.0, 7.0], "direction": [0, 1, 0], "coneAngle": 12, "velocity": "cone",
            "size": [0.12, 0.2], "color": [0.25, 0.55, 0.95, 1], "temperature": 0, "rotation": [0, 360] },
  "modules": [
    { "type": "gravity", "value": [0, -9.8, 0] },
    { "type": "drag", "value": 0.15 },
    { "type": "colorOverLife", "from": [0.25, 0.55, 0.95, 1], "to": [0.6, 0.8, 1.0, 1] },
    { "type": "alphaOverLife", "from": 0.9, "to": 0.25 }
  ]
})JSON";

// Texto JSON do preset pelo nome ("smoke", "fire", "fluid"), ou nullptr se nao existir.
inline const char* presetJson(std::string_view name) {
  if (name == "smoke") return kPresetSmoke;
  if (name == "fire") return kPresetFire;
  if (name == "fluid") return kPresetFluid;
  return nullptr;
}

} // namespace tool::particles
