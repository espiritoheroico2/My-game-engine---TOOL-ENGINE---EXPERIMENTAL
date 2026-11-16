// Presets do visual em JSON (apps/editor/ui_presets.hpp): ida e volta, limites, leitura tolerante e arquivos.
#include "ui_presets.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

using editor::presets::UiPreset;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
    ++run;
    if (!ok) {
        ++failed;
        std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
    }
}
#define CHECK(expr) Check((expr), #expr)

void TestRoundTripKeepsEveryField() {
    current = "TestRoundTripKeepsEveryField";
    UiPreset p;
    p.name = "Escuro grande";
    p.fontSize = 20.0f;
    p.fieldHeight = 40.0f;
    p.paddingX = 12.0f;
    p.caretWidth = 2.0f;
    p.background = {0.1f, 0.1f, 0.1f, 1.0f};
    p.selection = {0.2f, 0.4f, 0.8f, 0.5f};
    const auto back = editor::presets::FromJson(editor::presets::ToJson(p));
    CHECK(back.has_value());
    CHECK(back && *back == p);
}

void TestOutOfRangeValuesAreClamped() {
    current = "TestOutOfRangeValuesAreClamped";
    const auto j = tool::Json::parse(
        R"({"fontSize": 999, "fieldHeight": 1, "paddingX": -5, "caretWidth": 50, "text": [2, -1, 0.5, 9]})");
    CHECK(j.has_value());
    const auto p = editor::presets::FromJson(*j);
    CHECK(p.has_value());
    CHECK(p && p->fontSize == 48.0f);
    CHECK(p && p->fieldHeight == 20.0f);
    CHECK(p && p->paddingX == 0.0f);
    CHECK(p && p->caretWidth == 4.0f);
    CHECK(p && p->text[0] == 1.0f && p->text[1] == 0.0f && p->text[3] == 1.0f);
}

void TestMissingOrWrongKeysUseDefaults() {
    current = "TestMissingOrWrongKeysUseDefaults";
    const UiPreset d;
    const auto j = tool::Json::parse(R"({"fontSize": "grande", "background": "vermelho", "name": ""})");
    const auto p = editor::presets::FromJson(*j);
    CHECK(p.has_value());
    CHECK(p && p->fontSize == d.fontSize);
    CHECK(p && p->background == d.background);
    CHECK(p && p->name == d.name);
    CHECK(p && p->fieldHeight == d.fieldHeight);
}

void TestUnknownKeysAreIgnored() {
    current = "TestUnknownKeysAreIgnored";
    const auto j = tool::Json::parse(R"({"name": "Novo", "presetVersion": 9, "futuro": {"x": 1}})");
    const auto p = editor::presets::FromJson(*j);
    CHECK(p.has_value());
    CHECK(p && p->name == "Novo");
}

void TestNonObjectIsRejected() {
    current = "TestNonObjectIsRejected";
    CHECK(!editor::presets::FromJson(*tool::Json::parse("[1, 2]")).has_value());
    CHECK(!editor::presets::FromJson(*tool::Json::parse("\"texto\"")).has_value());
    CHECK(!tool::Json::parse("{ quebrado").has_value());
}

void TestFileStemIsSafe() {
    current = "TestFileStemIsSafe";
    CHECK(editor::presets::FileStemFor("Meu preset") == "Meu preset");
    CHECK(editor::presets::FileStemFor("../../etc") == "______etc");
    CHECK(editor::presets::FileStemFor("   ") == "preset");
    CHECK(editor::presets::FileStemFor("a/b\\c") == "a_b_c");
}

void TestSaveLoadAndListOnDisk() {
    current = "TestSaveLoadAndListOnDisk";
    const auto dir = std::filesystem::temp_directory_path() / "spriteui_presets_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    UiPreset a;
    a.name = "Alpha";
    a.fontSize = 14.0f;
    UiPreset b;
    b.name = "Beta";
    b.fieldHeight = 44.0f;
    CHECK(editor::presets::SavePreset(dir, a));
    CHECK(editor::presets::SavePreset(dir, b));

    const auto loaded = editor::presets::LoadPreset(dir / "Alpha.json");
    CHECK(loaded.has_value());
    CHECK(loaded && *loaded == a);

    // Um arquivo quebrado na pasta e ignorado sem derrubar a lista.
    {
        std::FILE* f = std::fopen((dir / "lixo.json").string().c_str(), "wb");
        if (f) { std::fputs("{ nao e json", f); std::fclose(f); }
    }
    const auto all = editor::presets::ListPresets(dir);
    CHECK(all.size() == 2);
    CHECK(all.size() == 2 && all[0].name == "Alpha");
    CHECK(all.size() == 2 && all[1].name == "Beta");

    CHECK(!editor::presets::LoadPreset(dir / "nao_existe.json").has_value());
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main() {
    TestRoundTripKeepsEveryField();
    TestOutOfRangeValuesAreClamped();
    TestMissingOrWrongKeysUseDefaults();
    TestUnknownKeysAreIgnored();
    TestNonObjectIsRejected();
    TestFileStemIsSafe();
    TestSaveLoadAndListOnDisk();
    std::printf("ui presets: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
