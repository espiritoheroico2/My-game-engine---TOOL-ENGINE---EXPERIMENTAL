#pragma once
// Configuracoes da ENGINE e do USUARIO guardadas por projeto: o bloco "engine" do Project/project.json.
// Pura: so tool::Json (sem SDL, sem ImGui), para o teste rodar sem janela. Ao ler, cada valor e limitado a faixa
// valida; projeto antigo (sem o bloco) recebe os padroes. Quem aplica os valores na janela e o Editor.
#include "tool/core/json.hpp"

#include <algorithm>
#include <string>

namespace editor {

struct EngineSettings {
  std::string language = "en";      // idioma da interface (codigo: en, pt, ...)
  float uiScale = 1.25f;            // escala da interface, 1.0 a 2.0 (padrao 125 por cento)
  std::string uiRenderer = "auto";  // driver de render da SpriteUI: "auto", "opengl" ou "software"
  int graphicsApi = 0;              // API da Cena e do Jogo: 0 Software, 1 Vulkan, 2 OpenGL, 3 D3D12, 4 D3D11
  float renderScale = 1.f;          // escala de render da cena, 0.25 a 2.0
  int fpsMode = 4;                  // limite de quadros, 0 a 4 (ver settings_model)

  bool operator==(const EngineSettings& o) const = default;

  [[nodiscard]] tool::Json toJson() const {
    tool::Json j = tool::Json::object();
    j.set("language", language);
    j.set("uiScale", static_cast<double>(uiScale));
    j.set("uiRenderer", uiRenderer);
    j.set("graphicsApi", graphicsApi);
    j.set("renderScale", static_cast<double>(renderScale));
    j.set("fpsMode", fpsMode);
    return j;
  }

  // Le o bloco; qualquer campo ausente ou fora da faixa vira o padrao (ou o valor mais proximo permitido).
  [[nodiscard]] static EngineSettings fromJson(const tool::Json& j) {
    EngineSettings s;
    if (!j.isObject()) return s;
    const std::string lang = j["language"].asString().substr(0, 16);
    if (!lang.empty()) s.language = lang;
    s.uiScale = static_cast<float>(std::clamp(j["uiScale"].asNumber(s.uiScale), 1.0, 2.0));
    const std::string renderer = j["uiRenderer"].asString();
    s.uiRenderer = (renderer == "opengl" || renderer == "software") ? renderer : "auto";
    s.graphicsApi = static_cast<int>(std::clamp(j["graphicsApi"].asNumber(s.graphicsApi), 0.0, 4.0));
    s.renderScale = static_cast<float>(std::clamp(j["renderScale"].asNumber(s.renderScale), 0.25, 2.0));
    s.fpsMode = static_cast<int>(std::clamp(j["fpsMode"].asNumber(s.fpsMode), 0.0, 4.0));
    return s;
  }
};

}  // namespace editor
