# Implementation status

## SpriteUI CPU benchmark

Reference run for `third_party/SpriteUi/benchmarks/spriteui_frame_benchmark.cpp`:

- Configuration: Release, MSVC 19.51.36260.0, Windows.
- Scene: 20 windows, 200 buttons, and TrueType labels.
- Run length: 1,000 measured frames after 20 warm-up frames.
- Target: `spriteui_frame_benchmark`; configure with `TOOL_BUILD_TESTS=ON`.

| Phase | Average |
| --- | ---: |
| Layout | 0.4352 ms/frame |
| Text | 0.1277 ms/frame |
| Extraction | 0.2998 ms/frame |
| Assembly | 0.3806 ms/frame |
| Input | 0.0035 ms/frame |
| Total wall time | 1.2953 ms/frame (772.0 frames/s) |

This is a current-code reference, not a before/after comparison or an Editor frame-time measurement. The benchmark
uses `TextLayoutCache`; integrating the cache into active Editor panels and recording a comparable before/after run
remain pending. Results are hardware-dependent.
