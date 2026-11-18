#include "profiler_export.hpp"

#include <cstdio>
#include <cstring>

namespace {

int checks = 0;
int failed = 0;

void Check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failed;
        std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expression);
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

void TestCaptureCopiesAllRowsAndGraphHistory() {
    editor::profiler_model::ProfilerModel model;
    editor::FrameProfiler profiler;
    editor::profiler_model::Extras extras;
    profiler.frameMs.fill(12.5f);
    profiler.workMs.fill(8.25f);
    profiler.filled = 1;
    model.refresh(0.0, profiler, profiler.sys.get(), extras, [](const char* s) { return s; });

    const auto snapshot = editor::profiler_export::Capture(model, 1.0);
    CHECK(snapshot.elapsedSeconds == 1.0);
    CHECK(snapshot.rowCount == model.rowCount());
    CHECK(snapshot.rows[0].kind == model.row(0).kind);
    CHECK(snapshot.graphs[0].back() == 12.5f);
    CHECK(snapshot.graphs[1].back() == 8.25f);
}

void TestMarkdownContainsMetricsAndGraphs() {
    editor::profiler_model::ProfilerModel model;
    editor::FrameProfiler profiler;
    editor::profiler_model::Extras extras;
    profiler.frameMs[0] = 16.0f;
    profiler.workMs[0] = 10.0f;
    profiler.filled = 1;
    model.refresh(0.0, profiler, profiler.sys.get(), extras, [](const char* s) { return s; });

    const std::string markdown = editor::profiler_export::ToMarkdown(
        {editor::profiler_export::Capture(model, 0.0)});
    CHECK(markdown.find("# Profiler data export") != std::string::npos);
    CHECK(markdown.find("## Sample 1") != std::string::npos);
    CHECK(markdown.find("Frame time (ms)") != std::string::npos);
    CHECK(markdown.find("16.0000") != std::string::npos);
    CHECK(markdown.find("Time per zone") != std::string::npos);
}

void TestMarkdownEscapesTableSeparators() {
    CHECK(editor::profiler_export::EscapeMarkdown("API | Vulkan") == "API \\| Vulkan");
    CHECK(editor::profiler_export::EscapeMarkdown("line1\nline2") == "line1<br>line2");
}

void TestCaptureScheduleRecordsFiveSamplesOneSecondApart() {
    editor::profiler_export::CaptureSchedule schedule;
    schedule.start(10.0);
    CHECK(!schedule.due(10.99));
    for (int i = 1; i <= editor::profiler_export::kSampleCount; ++i) {
        const double time = 10.0 + i;
        CHECK(schedule.due(time));
        CHECK(schedule.record(time) == (i == editor::profiler_export::kSampleCount));
        CHECK(schedule.sampleCount() == i);
        CHECK(!schedule.due(time));
    }
}

} // namespace

int main() {
    TestCaptureCopiesAllRowsAndGraphHistory();
    TestMarkdownContainsMetricsAndGraphs();
    TestMarkdownEscapesTableSeparators();
    TestCaptureScheduleRecordsFiveSamplesOneSecondApart();
    std::printf("Editor.ProfilerExport: %d checks, %d failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
