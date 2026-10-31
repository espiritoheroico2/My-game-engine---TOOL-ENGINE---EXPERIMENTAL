# Build guide

Full step-by-step instructions: **`HOW_TO_BUILD.txt`** (English) / **`COMO_COMPILAR.txt`** (Português), in the project root.

## Shortest path
| System | Do this |
|---|---|
| Windows (any recent Visual Studio + "Desktop development with C++") | double-click **`build_editor.bat`** |
| Linux / macOS | `./build_editor.sh` |

Output: `Projeto Final/ToolEditor(.exe)`. The log is saved in `build_log.txt`. No Git, Ninja or internet needed: SDL3, ImGui, imnodes and
miniaudio are vendored in `third_party/`. The CMake generator is the platform default (newest installed Visual Studio on Windows).

## Presets
| Preset | Purpose |
|---|---|
| `editor` | the editor only, CPU renderer (default for users) |
| `editor-gpu` | editor + Vulkan/OpenGL through DiligentCore (needs `third_party/DiligentCore`, see HOW_TO_BUILD.txt §5) |
| `dev` / `test` / `release` / `asan` | engine core + unit tests + sandbox, no window/GPU (fast, for development) |
| `beta` / `full` / `ci-full` | everything on (GPU tests included) |

Commands: `cmake --preset <name> && cmake --build --preset <name>`; tests: `ctest --preset dev`.
CMake 4.x is supported (`CMAKE_POLICY_VERSION_MINIMUM=3.5` is set).

## Tests
- Core: `cmake --preset dev && cmake --build --preset dev && ctest --preset dev`
- Editor UI (i18n/theme): `ctest --preset editor`
- GPU (`beta`): `gpu_test_vulkan`, `gpu_test_opengl` need a Vulkan / OpenGL 4.3 driver.

## Direct3D 12
OFF by default (`-DTOOL_WITH_D3D12=ON`), untested.

## Troubleshooting
See section 6 of `HOW_TO_BUILD.txt`.

## GPU backends (Vulkan / OpenGL)
Preset `editor-gpu` builds DiligentCore (vendored clone in `third_party/DiligentCore`). First build takes ~12 minutes. Without it the editor runs on the CPU renderer. The editor switches API at runtime in Settings > Graphics.
