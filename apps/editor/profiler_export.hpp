#pragma once

#include "profiler_model.hpp"
#include "tool/render/render_stats.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace editor::profiler_export {

inline constexpr int kSampleCount = 5;
inline constexpr double kSampleIntervalSeconds = 1.0;

class CaptureSchedule {
 public:
    void start(double nowSeconds) {
        sampleCount_ = 0;
        nextSampleSeconds_ = nowSeconds + kSampleIntervalSeconds;
        active_ = true;
    }

    bool due(double nowSeconds) const { return active_ && nowSeconds >= nextSampleSeconds_; }

    bool record(double nowSeconds) {
        if (!due(nowSeconds)) return false;
        ++sampleCount_;
        if (sampleCount_ == kSampleCount) {
            active_ = false;
            return true;
        }
        nextSampleSeconds_ = nowSeconds + kSampleIntervalSeconds;
        return false;
    }

    int sampleCount() const { return sampleCount_; }

 private:
    int sampleCount_ = 0;
    double nextSampleSeconds_ = 0.0;
    bool active_ = false;
};

struct Snapshot {
    double elapsedSeconds = 0.0;
    int rowCount = 0;
    bool truncated = false;
    std::array<profiler_model::Row, profiler_model::kMaxRows> rows{};
    std::array<std::array<float, profiler_model::kGraphSamples>, profiler_model::kGraphCount> graphs{};
    tool::render::FrameRenderStats renderStats;
};

inline Snapshot Capture(const profiler_model::ProfilerModel& model, double elapsedSeconds,
                        const tool::render::FrameRenderStats& renderStats) {
    Snapshot snapshot;
    snapshot.elapsedSeconds = elapsedSeconds;
    snapshot.rowCount = model.rowCount();
    snapshot.truncated = model.truncated();
    snapshot.renderStats = renderStats;
    for (int i = 0; i < snapshot.rowCount; ++i) snapshot.rows[static_cast<std::size_t>(i)] = model.row(i);
    for (int graph = 0; graph < profiler_model::kGraphCount; ++graph) {
        const float* values = model.graph(graph);
        for (int i = 0; i < profiler_model::kGraphSamples; ++i)
            snapshot.graphs[static_cast<std::size_t>(graph)][static_cast<std::size_t>(i)] = values[i];
    }
    return snapshot;
}

inline Snapshot Capture(const profiler_model::ProfilerModel& model, double elapsedSeconds) {
    return Capture(model, elapsedSeconds, {});
}

inline std::string EscapeMarkdown(const char* text) {
    std::string escaped;
    if (!text) return escaped;
    for (const char* p = text; *p; ++p) {
        if (*p == '\\' || *p == '|') escaped.push_back('\\');
        if (*p == '\r') continue;
        if (*p == '\n') {
            escaped += "<br>";
            continue;
        }
        escaped.push_back(*p);
    }
    return escaped;
}

inline std::string ToMarkdown(const std::vector<Snapshot>& snapshots) {
    std::string out;
    out.reserve(snapshots.size() * 32768);
    out += "# Profiler data export\n\n";
    out += "- Samples: " + std::to_string(snapshots.size()) + "\n";
    out += "- Sampling interval: 1 second\n";
    out += "- Data source: all rows and graph histories displayed by the Profiler\n\n";

    for (std::size_t sampleIndex = 0; sampleIndex < snapshots.size(); ++sampleIndex) {
        const Snapshot& sample = snapshots[sampleIndex];
        char heading[96];
        std::snprintf(heading, sizeof heading, "## Sample %zu (%.2f s)\n\n", sampleIndex + 1, sample.elapsedSeconds);
        out += heading;
        for (int rowIndex = 0; rowIndex < sample.rowCount; ++rowIndex) {
            const profiler_model::Row& row = sample.rows[static_cast<std::size_t>(rowIndex)];
            switch (row.kind) {
                case profiler_model::RowKind::Heading:
                    out += "### ";
                    out += EscapeMarkdown(row.text);
                    out += "\n\n";
                    break;
                case profiler_model::RowKind::Text:
                    out += "- ";
                    out += EscapeMarkdown(row.text);
                    out += "\n";
                    break;
                case profiler_model::RowKind::Cells:
                    out += "| Metric | Value |\n|---|---|\n";
                    for (int cellIndex = 0; cellIndex < row.cellCount; ++cellIndex) {
                        const profiler_model::Cell& cell = row.cells[cellIndex];
                        out += "| ";
                        out += EscapeMarkdown(cell.label[0] ? cell.label : "Value");
                        out += " | ";
                        out += EscapeMarkdown(cell.value);
                        out += " |\n";
                    }
                    out += "\n";
                    break;
                case profiler_model::RowKind::Bar:
                    out += "- **";
                    out += EscapeMarkdown(row.text);
                    out += "**: ";
                    if (row.cellCount > 0) out += EscapeMarkdown(row.cells[0].value);
                    out += " (";
                    char fraction[24];
                    std::snprintf(fraction, sizeof fraction, "%.1f%%", std::clamp(row.fraction, 0.f, 1.f) * 100.f);
                    out += fraction;
                    out += ")\n";
                    break;
                case profiler_model::RowKind::Graph:
                    out += "**Graph:** ";
                    out += EscapeMarkdown(row.text);
                    if (row.cellCount > 0) {
                        out += " (";
                        out += EscapeMarkdown(row.cells[0].label);
                        out += " ";
                        out += EscapeMarkdown(row.cells[0].value);
                        out += ")";
                    }
                    out += "\n\n";
                    break;
                case profiler_model::RowKind::Gap:
                    break;
            }
        }

        out += "\n### Graph histories\n\n";
        out += "| Frame | Frame time (ms) | CPU work (ms) |\n|---:|---:|---:|\n";
        for (int i = 0; i < profiler_model::kGraphSamples; ++i) {
            char values[80];
            const float frame = sample.graphs[0][static_cast<std::size_t>(i)];
            const float work = sample.graphs[1][static_cast<std::size_t>(i)];
            std::snprintf(values, sizeof values, "| %d | %.4f | %.4f |\n", i + 1,
                          std::isfinite(frame) ? frame : 0.f, std::isfinite(work) ? work : 0.f);
            out += values;
        }
        if (sample.truncated) out += "\n> Profiler row list was truncated.\n";
        out += "\n";
    }
    return out;
}

inline std::string ToCsv(const std::vector<Snapshot>& snapshots) {
    std::string out = "sample_seconds,frame,view,pass,draw_calls,instances,triangles,cpu_ms,gpu_ms,backend_draws,backend_triangles,backend_uploads,backend_pipeline_switches,point_faces_reused,point_faces_rendered,point_faces_first_use,point_faces_target_reset,point_faces_light_changed,point_faces_casters_changed,point_face_render_failures\n";
    for (const Snapshot& sample : snapshots) {
        for (std::size_t viewIndex = 0; viewIndex < tool::render::kRenderViewCount; ++viewIndex) {
            const auto& view = sample.renderStats.views[viewIndex];
            if (!view.valid) continue;
            const char* viewName = tool::render::renderViewName(static_cast<tool::render::RenderViewId>(viewIndex));
            for (std::size_t passIndex = 0; passIndex < tool::render::kRenderPassCount; ++passIndex) {
                const auto& pass = view.passes[passIndex];
                char row[512];
                if (passIndex == static_cast<std::size_t>(tool::render::RenderPassId::PointShadow)) {
                    const auto& cache = view.shadowCache;
                    std::snprintf(row, sizeof row, "%.3f,%llu,%s,%s,%llu,%llu,%llu,%.6f,,,,,,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                                  sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                                  viewName, tool::render::renderPassName(static_cast<tool::render::RenderPassId>(passIndex)),
                                  static_cast<unsigned long long>(pass.drawCalls),
                                  static_cast<unsigned long long>(pass.instances),
                                  static_cast<unsigned long long>(pass.triangles), pass.cpuMs,
                                  static_cast<unsigned long long>(cache.pointFacesReused),
                                  static_cast<unsigned long long>(cache.pointFacesRendered),
                                  static_cast<unsigned long long>(cache.pointFacesFirstUse),
                                  static_cast<unsigned long long>(cache.pointFacesTargetReset),
                                  static_cast<unsigned long long>(cache.pointFacesLightChanged),
                                  static_cast<unsigned long long>(cache.pointFacesCastersChanged),
                                  static_cast<unsigned long long>(cache.pointFaceRenderFailures));
                } else {
                    std::snprintf(row, sizeof row, "%.3f,%llu,%s,%s,%llu,%llu,%llu,%.6f,,,,,,,,,,,,\n",
                                  sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                                  viewName, tool::render::renderPassName(static_cast<tool::render::RenderPassId>(passIndex)),
                                  static_cast<unsigned long long>(pass.drawCalls),
                                  static_cast<unsigned long long>(pass.instances),
                                  static_cast<unsigned long long>(pass.triangles), pass.cpuMs);
                }
                out += row;
            }
            char backendRow[256];
            std::snprintf(backendRow, sizeof backendRow, "%.3f,%llu,%s,Backend total,,,,,,%llu,%llu,%llu,%llu,,,,,,,\n",
                          sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                          viewName, static_cast<unsigned long long>(view.backendDrawCalls),
                          static_cast<unsigned long long>(view.backendTriangles),
                          static_cast<unsigned long long>(view.backendBufferUploads),
                          static_cast<unsigned long long>(view.backendPipelineSwitches));
            out += backendRow;
        }
        if (sample.renderStats.views[0].valid || sample.renderStats.views[1].valid) {
            for (std::size_t passIndex = 0; passIndex < tool::render::kRenderPassCount; ++passIndex) {
                const auto pass = sample.renderStats.total(static_cast<tool::render::RenderPassId>(passIndex));
                char row[256];
                if (passIndex == static_cast<std::size_t>(tool::render::RenderPassId::PointShadow)) {
                    const auto cache = sample.renderStats.shadowCacheTotal();
                    std::snprintf(row, sizeof row, "%.3f,%llu,Total,%s,%llu,%llu,%llu,%.6f,,,,,,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                                  sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                                  tool::render::renderPassName(static_cast<tool::render::RenderPassId>(passIndex)),
                                  static_cast<unsigned long long>(pass.drawCalls),
                                  static_cast<unsigned long long>(pass.instances),
                                  static_cast<unsigned long long>(pass.triangles), pass.cpuMs,
                                  static_cast<unsigned long long>(cache.pointFacesReused),
                                  static_cast<unsigned long long>(cache.pointFacesRendered),
                                  static_cast<unsigned long long>(cache.pointFacesFirstUse),
                                  static_cast<unsigned long long>(cache.pointFacesTargetReset),
                                  static_cast<unsigned long long>(cache.pointFacesLightChanged),
                                  static_cast<unsigned long long>(cache.pointFacesCastersChanged),
                                  static_cast<unsigned long long>(cache.pointFaceRenderFailures));
                } else {
                    std::snprintf(row, sizeof row, "%.3f,%llu,Total,%s,%llu,%llu,%llu,%.6f,,,,,,,,,,,,\n",
                                  sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                                  tool::render::renderPassName(static_cast<tool::render::RenderPassId>(passIndex)),
                                  static_cast<unsigned long long>(pass.drawCalls),
                                  static_cast<unsigned long long>(pass.instances),
                                  static_cast<unsigned long long>(pass.triangles), pass.cpuMs);
                }
                out += row;
            }
            const auto total = sample.renderStats.total();
            char row[256];
            std::snprintf(row, sizeof row, "%.3f,%llu,Total,All passes,%llu,%llu,%llu,%.6f,,,,,,,,,,,,\n",
                          sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                          static_cast<unsigned long long>(total.drawCalls),
                          static_cast<unsigned long long>(total.instances),
                          static_cast<unsigned long long>(total.triangles), total.cpuMs);
            out += row;
            const auto backendTotal = sample.renderStats.backendTotal();
            char backendRow[256];
            std::snprintf(backendRow, sizeof backendRow, "%.3f,%llu,Total,Backend total,,,,,,%llu,%llu,%llu,%llu,,,,,,,\n",
                          sample.elapsedSeconds, static_cast<unsigned long long>(sample.renderStats.frameId),
                          static_cast<unsigned long long>(backendTotal.backendDrawCalls),
                          static_cast<unsigned long long>(backendTotal.backendTriangles),
                          static_cast<unsigned long long>(backendTotal.backendBufferUploads),
                          static_cast<unsigned long long>(backendTotal.backendPipelineSwitches));
            out += backendRow;
        }
    }
    return out;
}

} // namespace editor::profiler_export
