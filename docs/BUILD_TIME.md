# Build time analysis: tool_editor (CMake configure + build)

Scope: why configuring and building the editor (`tool_editor`) is slow. Analysis only. No CMake file, commit or push was touched. The repo `build/` directory was not used.

## Setup and caveats

- Machine: Linux, **2 CPU cores**, GCC 13.3.0 (`c++`), CMake 3.28.3, Ninja 1.11.1. No ccache or sccache installed.
- Options: `-DTOOL_WITH_EDITOR=ON -DTOOL_WITH_SDL3=ON -DTOOL_WITH_SPRITEUI=ON -DTOOL_WITH_DILIGENT=OFF` (MiniAudio, QuickJS on by default). Release was used for the main build because the `editor` preset in `CMakePresets.json` sets `CMAKE_BUILD_TYPE=Release` and `build_editor.sh` uses that preset.
- Builds ran from a copy of the source tree in the scratchpad, not the repo. Reason: `tool_editor` writes its binary and copies `assets/` into `Projeto Final/` inside the source tree, so building the repo would modify it.
- Repo CMake inputs (47 `CMakeLists.txt` and `*.cmake` files) were hashed before and after the analysis. They did not change.
- `third_party/SpriteUi` is a **symlink to `/home/claude/spriteui`**, outside the repo, and that folder was being edited during the analysis. The first no-op measurement was therefore contaminated (CMake regenerated, and `main.cpp` and QuickJS were recompiled). It was discarded. All numbers below for builds after that point use a frozen copy of SpriteUI (173 files, tree hash `ac2de3fcd4dd`).
- The project's own build (`build/`, Makefiles, MSVC on Windows per `build_log.txt`) differs from these numbers. These are GCC on Linux with Ninja. Relative proportions should be similar, but absolute times on Windows/MSVC were not measured.
- Each timing is a single run unless noted. Configure time varied from 13 s to 43 s across identical fresh-directory runs, so treat small differences as noise.

## Measured numbers

| # | Step | Result |
|---|------|--------|
| 1 | Configure, 4 options, fresh dir, empty build type | **15.9 s** |
| 2 | Same configure again, same dir | **0.7 s** (no fetch, no rebuild) |
| 3 | Configure, fresh dir, Release (3 runs) | 43.0 s, 16.1 s, 13.1 s |
| 4 | Configure with SDL, editor and SpriteUI off (only core, quickjs, LightPrism, MiniAudio off) | **0.8 s** |
| 5 | Configure with `TOOL_WITH_DILIGENT=ON`, fresh dir | **57.2 s** configure; downloads **442 MB** (`DiligentCore`, git clone with submodules) into `_deps` |
| 6 | Same DILIGENT=ON configure again, same dir | **1.0 s**, no re-download |
| 7 | Clean build `tool_editor`, Release, `-j2`, 390 steps (first run, SpriteUI not yet frozen) | **157.0 s** |
| 8 | Clean build `tool_editor`, Release, `-j2`, frozen tree (repeat) | **150.9 s** |
| 9 | Clean build `tool_editor`, empty `CMAKE_BUILD_TYPE` (default), `-j2` | **45.6 s** |
| 10 | Clean build `tool_editor`, Release, `TOOL_SDL_CONSOLE_BUILD=ON`, `-j2` (timing only, drops X11/Wayland) | **123.0 s** (289 steps) |
| 11 | No-op `ninja tool_editor` | **0.015 s** (0 steps) |
| 12 | No-op `cmake --build` | **0.02 s** |
| 13 | Touch `apps/editor/image_io.cpp`, rebuild | **3.5 s** (compile 3.3 s, link 0.19 s) |
| 14 | Touch `apps/editor/main.cpp`, rebuild | **50.4 s** (compile 50.2 s, link 0.19 s) |
| 15 | POST_BUILD asset copy alone (57 files, 492 KB) | **0.014 s** |
| 16 | `main.cpp` single compile, flags `-O0` (empty build type) | **16.3 s** |
| 17 | `main.cpp` single compile, `-O1` | **27.6 s** |
| 18 | `main.cpp` single compile, `-O2` | **44.5 s** |
| 19 | `main.cpp` single compile, `-O3` (Release; GCC `-ftime-report` run) | **59.8 s** (57.9 s in the Ninja build) |

Build wall time (row 8, 150.9 s) against summed compile work (268 s across 390 steps, two cores): the ideal lower bound is about 134 s. The gap is a serial tail: `main.cpp` runs alone from about 99 s to 157 s while one core is idle.

## Dependency graph (Release, clean build, per-step times from ninja's log)

| Component | Source | Steps | Summed CPU time | Notes |
|-----------|--------|------:|----------------:|-------|
| SDL3 (static) | vendored `third_party/SDL3`, `add_subdirectory` with `EXCLUDE_FROM_ALL` | 337 | **96.2 s** | Builds **all** Linux backends (X11, Wayland, KMSDRM, offscreen, Vulkan, dummy, ...) plus SDL_uclibc (23 libm files), wayland protocol generation, a 1.5 s PCH. Built from source in every new build dir. |
| quickjs-ng (`qjs`) | vendored | 5 | **38.9 s** | `quickjs.c` alone is **35.3 s**. Its own CMake sets Release when `CMAKE_BUILD_TYPE` is empty, so it is `-O3` in both Release and default builds (measured flags). |
| imgui + imnodes | vendored | 8 | **26.1 s** | `imgui.cpp` 10.2 s, `imgui_widgets.cpp` 6.5 s, `imgui_draw.cpp` 3.7 s, `imnodes.cpp` 3.0 s |
| MiniAudio device | vendored header | 2 | **21.8 s** | Single TU, `miniaudio_device.cpp`. Header implementation compiled inside one .cpp file. |
| Editor app | `apps/editor` | 2 | **63.8 s** | `main.cpp` 57.9 s, `image_io.cpp` 5.9 s |
| SpriteUI | symlink to `/home/claude/spriteui` | 24 | 14.8 s | |
| LightPrism | vendored | 11 | 6.6 s | |
| Diligent | not built (OFF) | 0 | 0 | Clone + configure only (row 5). Full build not measured. |

Option check, third-party targets: no unnecessary option is on. SDL tests, examples, install and test library are OFF. SpriteUI tests, examples, benchmarks, fuzzers, docs and visualizer are OFF. LightPrism tests and soft renderer are OFF. QuickJS examples and CLI are OFF.

## Top 15 slowest translation units (clean Release build, from ninja's `.ninja_log`)

| Rank | Seconds | File |
|-----:|--------:|------|
| 1 | 57.85 | `apps/editor/main.cpp` (-O3) |
| 2 | 35.25 | `third_party/quickjs-ng/quickjs.c` (-O3, forced by its CMake) |
| 3 | 21.74 | `engine/.../audio/src/miniaudio_device.cpp` |
| 4 | 10.17 | `third_party/imgui/imgui.cpp` |
| 5 | 6.63 | SDL `src/dynapi/SDL_dynapi.c` |
| 6 | 6.45 | `third_party/imgui/imgui_widgets.cpp` |
| 7 | 6.18 | SDL `src/video/SDL_blit_auto.c` |
| 8 | 5.95 | `apps/editor/image_io.cpp` |
| 9 | 3.69 | `third_party/imgui/imgui_draw.cpp` |
| 10 | 3.08 | SDL `src/render/software/SDL_blendline.c` |
| 11 | 3.03 | `third_party/imnodes/imnodes.cpp` |
| 12 | 2.77 | SDL `src/render/software/SDL_blendfillrect.c` |
| 13 | 2.39 | SDL `src/gpu/vulkan/SDL_gpu_vulkan.c` |
| 14 | 2.33 | SpriteUI `src/Text/TruetypeAtlas.cpp` |
| 15 | 2.23 | SDL `src/render/SDL_render.c` |

Source: the first clean Release build (row 7). `main.cpp` and `quickjs.c` dominate. Everything else is small.

Why `main.cpp` is slow (GCC `-ftime-report`, row 19): total 59.8 s wall. Parsing 5.3 s (9%). Optimization and code generation 51.4 s (87%). Of that, callgraph function expansion (back end) 35.8 s and IPA passes 9.9 s. Preprocessed output is 236k lines and 7.5 MB across 7,318 included file markers. The cost is in the optimizer, not header parsing, so splitting headers would save little.

## Causes, ranked by measured cost

1. **Optimization level (`-O3` in Release).** Clean build drops from **150.9 s to 45.6 s** when `CMAKE_BUILD_TYPE` is left empty (row 9 vs 8, -105 s, about 70%). Per file: `main.cpp` 58 s to 16 s; `miniaudio_device.cpp` 21.7 s to 3.9 s; `imgui.cpp` 10.2 s to 1.9 s. QuickJS is `-O3` in both cases. Caveat: runtime speed of the unoptimized editor was not measured.
2. **`main.cpp` as a serial tail.** At `-O3` it takes about 58 s (rows 16 to 19). It runs alone at the end of the `-j2` build and sets the minimum wall time. It is also what a one-line edit to `main.cpp` costs (50 s, row 14).
3. **SDL3 built from source with all backends.** 337 of 390 steps and 96 s of CPU work (36% of total). This is paid again in every new build directory. Measured reduction: `TOOL_SDL_CONSOLE_BUILD=ON` gives 150.9 s to 123.0 s (row 10, -28 s, -101 steps), but it also removes the window system, so it cannot be used for the editor window. It is a sizing number only.
4. **QuickJS single TU.** `quickjs.c` takes 35 s (25 s in the empty-type build), forced to `-O3` by quickjs's own CMake default.
5. **Fresh-directory configure.** 13 to 16 s per fresh build dir. Almost all of it is SDL3 feature probing: with SDL, editor and SpriteUI off, configure takes 0.8 s (row 4). A second configure in the same dir takes 0.7 s (row 2). With `TOOL_WITH_DILIGENT=ON`, the fresh-dir configure adds a 442 MB clone (row 5).
6. **imgui, imnodes, MiniAudio.** Together about 48 s of CPU work (26 s and 22 s). Mostly fixed costs of vendored third-party code.

## Checked and not a cause

- **POST_BUILD asset copy** (`apps/editor/CMakeLists.txt` lines 31 to 34): 0.014 s. It runs only on relink.
- **No-op rebuild** (rows 11 and 12): 0.02 s. There is no rebuild on a clean tree.
- **Second configure** (row 2): 0.7 s. No re-fetch or re-build.
- **GLOB and GLOB_RECURSE:** none in the project's CMake files. The `file(GLOB ...)` calls are inside SDL's own CMake and run at configure time only. A no-op build takes 0.02 s, so the missing `CONFIGURE_DEPENDS` is not a cost.
- **Shader build step:** does not exist. No CMake file references `tools/compile_shaders.py` or `build_glsl.py`. The task's premise was incorrect.
- **Unity build:** off (`CMAKE_UNITY_BUILD` not set).
- **PCH:** none in the project's targets. SDL's PCH takes 1.5 s.
- **Linker:** `ld.bfd` (default). Link takes 0.19 s, so a faster linker would save nothing measurable. `lld` and `ld.gold` exist, `mold` does not.
- **ccache / sccache:** not installed. This is a missing speedup for repeated builds, not a cause. Not measured.
- **Core count:** the machine has 2 cores. This is a hardware limit here, not a CMake setting. The build did not use more than 2 jobs.
- **Asset copy size:** `assets/` is 492 KB (57 files). `Projeto Final/` is 10 MB because of the executable and previous copies, not because of the copy step.

## Recommended CMake changes (not applied)

Expected savings below are either measured or explicitly marked as estimated.

1. **`apps/editor/CMakeLists.txt`, after line 1:** add `set_source_files_properties(main.cpp PROPERTIES COMPILE_OPTIONS "-O1")`, guarded for GCC/Clang (MSVC uses `/O1`). Measured: `main.cpp` single compile 59.8 s to 27.6 s (-32 s). Estimated wall-time effect on this 2-core clean build: about -30 s, because `main.cpp` is the tail. Not measured end to end. Runtime impact of `-O1` on the editor loop was not measured, so check frame time before committing.
2. **`CMakePresets.json`, add a preset** (for example `editor-dev`, inheriting `editor`, with an empty or Debug `CMAKE_BUILD_TYPE`) for iteration. Measured: clean build 150.9 s to 45.6 s (empty type). Use only while iterating. The unoptimized editor is slower at runtime, which was not measured. Do not make it the default.
3. **`cmake/Dependencies.cmake`, line 149 to 151:** if Diligent is ever needed, place `DiligentCore` under `third_party/DiligentCore`. The code already checks for that path. This removes the 442 MB clone in every fresh build directory. The clone time is part of the 57 s in row 5, not separately measured.
4. **`cmake/Dependencies.cmake`, lines 44 to 50 (`TOOL_SDL_CONSOLE_BUILD`):** measured as a sizing number only (-28 s in one run). Not suitable for the editor window. Setting `SDL_X11` and `SDL_WAYLAND` off individually (not measured) might save part of that. Test before using.
5. **Process, not CMake:** reuse one build directory per configuration. Each fresh directory costs about 96 s of SDL build plus 13 to 16 s of configure, measured.
6. **`third_party/SpriteUi` (symlink to `/home/claude/spriteui`):** edits in that folder trigger CMake regeneration and recompile `main.cpp` (50 s, row 14). Pin SpriteUI as a FetchContent commit or vendor it, so the editor build depends only on repo files.

## Not measured

- Windows/MSVC build times (the project's real target per `build_log.txt`).
- Build with more than 2 jobs, or with the Make generator used by the repo's `build/` directory (measured with Ninja only).
- Runtime performance of `-O1` for `main.cpp`, and of the unoptimized build.
- Per-file `-O1` or `-O2` for `quickjs.c` or MiniAudio.
- Individual SDL backend switches (`SDL_X11`, `SDL_WAYLAND`, KMSDRM) separately from `TOOL_SDL_CONSOLE_BUILD`.
- Full DiligentCore build (only its clone and configure were measured).
- `-ftime-trace`: that is a Clang flag, and the compiler here is GCC. `-ftime-report` was used instead.
- ccache effect, lld effect on link time (link is 0.19 s, so likely negligible).
- Run-to-run variance: each build was measured once, except the configure runs (three Release fresh-dir runs, 13 to 43 s). Treat differences under about 10% as noise.
- Timings with the other agent's concurrent load removed. Some earlier runs overlapped with the other agent's edits in `/home/claude/spriteui`, which is why the frozen tree was used for the final numbers.

Raw data (ninja logs, GCC time report, configure logs) is in the scratchpad at `/tmp/claude-0/-home-claude/ba2f3c8c-792a-5f2e-ba9d-c8c8e659432a/scratchpad/cmake-analysis/`.
