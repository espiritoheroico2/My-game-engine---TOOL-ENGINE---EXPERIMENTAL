#include "profiler_export.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>

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

void TestRenderPassStatsExportSeparatesViewsAndAggregates() {
      editor::profiler_model::ProfilerModel model;
      editor::FrameProfiler profiler;
      editor::profiler_model::Extras extras;
      model.refresh(0.0, profiler, profiler.sys.get(), extras, [](const char* s) { return s; });

      tool::render::FrameRenderStats stats;
      stats.beginFrame();
      auto& scene = stats.views[static_cast<std::size_t>(tool::render::RenderViewId::Scene)];
      scene.valid = true;
      scene.backendDrawCalls = 3;
      scene.backendTriangles = 12;
      scene.backendBufferUploads = 4;
      scene.backendPipelineSwitches = 2;
      scene.shadowCache.pointFacesReused = 2;
      scene.shadowCache.pointFacesRendered = 1;
      scene.shadowCache.pointFacesFirstUse = 1;
      scene.shadowCache.pointFacesTargetReset = 1;
      scene.shadowCache.pointFacesLightChanged = 2;
      scene.shadowCache.pointFacesCastersChanged = 3;
      scene.shadowCache.pointFaceRenderFailures = 1;
      auto& opaque = scene.passes[static_cast<std::size_t>(tool::render::RenderPassId::Opaque)];
      opaque.recordDraw(6, 2);
      opaque.cpuMs = 0.25;
      auto& game = stats.views[static_cast<std::size_t>(tool::render::RenderViewId::Game)];
      game.valid = true;
      game.backendDrawCalls = 5;
      game.backendTriangles = 18;
      game.backendBufferUploads = 7;
      game.backendPipelineSwitches = 3;
      game.shadowCache.pointFacesReused = 4;
      game.shadowCache.pointFacesRendered = 2;
      game.shadowCache.pointFacesCastersChanged = 1;
      game.passes[static_cast<std::size_t>(tool::render::RenderPassId::Opaque)].recordDraw(3, 1);

      const auto snapshot = editor::profiler_export::Capture(model, 1.0, stats);
      const std::string csv = editor::profiler_export::ToCsv({snapshot});
      CHECK(stats.frameId == 1);
      CHECK(stats.total().drawCalls == 2);
      CHECK(stats.total().instances == 3);
      CHECK(stats.shadowCacheTotal().pointFacesReused == 6);
      CHECK(csv.find("sample_seconds,frame,view,pass,draw_calls,instances,triangles,cpu_ms,gpu_ms,backend_draws,backend_triangles,backend_uploads,backend_pipeline_switches,point_faces_reused,point_faces_rendered,point_faces_first_use,point_faces_target_reset,point_faces_light_changed,point_faces_casters_changed,point_face_render_failures") == 0);
      CHECK(csv.find(",Scene,Opaque,1,2,4,0.250000,") != std::string::npos);
      CHECK(csv.find(",Game,Opaque,1,1,1,0.000000,") != std::string::npos);
      CHECK(csv.find(",Total,Opaque,2,3,5,0.250000,") != std::string::npos);
      CHECK(csv.find(",Scene,Point/Spot shadow,0,0,0,0.000000,,,,,,2,1,1,1,2,3,1") != std::string::npos);
      CHECK(csv.find(",Total,Point/Spot shadow,0,0,0,0.000000,,,,,,6,3,1,1,2,4,1") != std::string::npos);
      CHECK(csv.find(",Total,All passes,2,3,5,0.250000,") != std::string::npos);
      CHECK(csv.find(",Scene,Backend total,,,,,,3,12,4,2") != std::string::npos);
      CHECK(csv.find(",Total,Backend total,,,,,,8,30,11,5") != std::string::npos);
      std::istringstream lines(csv);
      std::string line;
      while (std::getline(lines, line)) {
          int columns = 1;
          for (char ch : line) if (ch == ',') ++columns;
          CHECK(columns == 20);
      }
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
    TestRenderPassStatsExportSeparatesViewsAndAggregates();
    TestCaptureScheduleRecordsFiveSamplesOneSecondApart();
    std::printf("Editor.ProfilerExport: %d checks, %d failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
