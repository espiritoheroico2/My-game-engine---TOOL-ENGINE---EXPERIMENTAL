#include "project_engine_settings.hpp"

#include <cstdio>

int main() {
    editor::EngineSettings original;
    original.language = "pt";
    original.uiScale = 1.25f;
    original.renderer = 2;
    original.graphicsApi = 3;
    original.renderScale = 1.5f;
    original.fpsLimit = 2;

    const auto j = original.toJson();
    const auto restored = editor::EngineSettings::fromJson(j);
    const bool roundTrip =
        restored.language == original.language &&
        restored.uiScale == original.uiScale &&
        restored.renderer == original.renderer &&
        restored.graphicsApi == original.graphicsApi &&
        restored.renderScale == original.renderScale &&
        restored.fpsLimit == original.fpsLimit;

    const auto invalid = tool::Json::parse(
        R"({"scale":9,"renderer":99,"api":-1,"renderScale":0.01,"fpsLimit":99})");
    const auto clamped = editor::EngineSettings::fromJson(*invalid);
    const bool limits =
        clamped.uiScale == 2.5f &&
        clamped.renderer == 4 &&
        clamped.graphicsApi == 0 &&
        clamped.renderScale == 0.25f &&
        clamped.fpsLimit == 4;

    std::printf("project engine settings: roundTrip=%d limits=%d\n", roundTrip, limits);
    return roundTrip && limits ? 0 : 1;
}
