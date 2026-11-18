#pragma once

#include "tool/core/json.hpp"

#include <algorithm>
#include <string>

namespace editor {

struct EngineSettings {
    std::string language = "en";
    float uiScale = 1.25f;
    int renderer = 0;
    int graphicsApi = 0;
    float renderScale = 0.5f;
    int fpsLimit = 4;

    tool::Json toJson() const {
        tool::Json j = tool::Json::object();
        j.set("language", language);
        j.set("scale", static_cast<double>(uiScale));
        j.set("renderer", renderer);
        j.set("api", graphicsApi);
        j.set("renderScale", static_cast<double>(renderScale));
        j.set("fpsLimit", fpsLimit);
        return j;
    }

    static EngineSettings fromJson(const tool::Json& j) {
        EngineSettings e;
        if (!j.isObject()) return e;
        if (j["language"].isString()) e.language = j["language"].asString();
        e.uiScale = std::clamp(static_cast<float>(j["scale"].asNumber(e.uiScale)), 0.7f, 2.5f);
        e.renderer = std::clamp(static_cast<int>(j["renderer"].asNumber(e.renderer)), 0, 4);
        e.graphicsApi = std::clamp(static_cast<int>(j["api"].asNumber(e.graphicsApi)), 0, 4);
        e.renderScale = std::clamp(static_cast<float>(j["renderScale"].asNumber(e.renderScale)), 0.25f, 2.0f);
        e.fpsLimit = std::clamp(static_cast<int>(j["fpsLimit"].asNumber(e.fpsLimit)), 0, 4);
        return e;
    }
};

} // namespace editor
