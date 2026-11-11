# Changelog

Notable changes to SpriteUI are documented here. This project has not published a 0.6.0 release yet.

## Unreleased — 0.6.0

### Added

- C++20 layout engine with flex wrapping, content alignment and row baseline alignment.
- Text line breaking for a documented subset of Unicode opportunities, source-aware layout, caret rectangles and insertion hit testing.
- Nine-Slice calculation and rounded-panel SDF CPU reference.
- CTest smoke test that installs the library and builds a separate `find_package(SpriteUI)` consumer.
- Optional CPU microbenchmarks for layout and render extraction.
- GitHub Actions matrix for MSVC, GCC and Clang, plus a GCC ASan/UBSan job.
- Optional SDL3 (3.2+) mouse/focus input adapter, kept separate from graphics backends.
- SDL3 keyboard/gamepad navigation commands, with passing synthetic event tests and installed-target consumer coverage.
- SDL3 UTF-8 text input and IME composition forwarding, including candidate-area placement and text-input activation controls.
- SDL3 UTF-8 clipboard read/write and availability helpers, with embedded-NUL rejection.
- SDL3 RAII window wrapper with checked creation, move-only ownership, and basic title/size/position/visibility controls.
- Release-readiness, architecture status and consumer documentation.
- Documented the ToolEngine migration goal: SpriteUI remains ImGui-independent, with a staged editor replacement and ImGui removal only after regression validation.
- Clarified the ToolEngine integration boundary: reuse its ECS, SDL event loop and custom RHI; recorded current input/render API gaps rather than adding parallel Flecs, windowing or Diligent paths.
- Added an optional SDL3 interactive visualizer using the CPU reference renderer, with palette/toggle/slider interaction, PNG export and a headless snapshot mode; it is explicitly not a GPU preview.
- Added an editable UTF-8 text field to the visualizer with cursor navigation, codepoint deletion, and Enter readback to the application console.
- Added Xmind Regular as the visualizer's primary font, TTF/OTF selection via `--font`, and bitmap-coverage rendering alongside the existing MSDF path.
- Added named visualizer text styles with consistent sizes and bottom margins, and adjusted stacked headings/cards to preserve readable vertical spacing.
- Added clickable visualizer tabs, a synthetic two-colour selected-tab gradient, floating action buttons, and an opacity fade driven by `ToonMotionSystem`.
- Added atlas-region `UIIcon` metadata and icon-item construction without coupling the core to SVG/image decoding or a host renderer.
- Added per-item opacity during render extraction, preserving premultiplied alpha and the existing 80-byte GPU instance ABI.
- Added `docs/TOOLENGINE_INTEGRATION_GUIDE.md` with host ownership boundaries, RHI/input/ECS integration phases, asset import guidance, and MVP acceptance criteria.
- Added stateless validated keyframe timeline sampling; host-owned playback and ECS application remain explicit integration points.
- Added host-clock-driven timeline playback controls for play, pause, stop, seek, and looping.
- Added an optional Clang libFuzzer target for JSON settings validation/round trips, with ASan/UBSan and a CI corpus run.
- Added host-agnostic multi-pointer frame routing with isolated capture/hover state, deterministic cancellation for omitted pointers, and duplicate-frame/input validation.
- Added SDL3 finger down/motion/up/cancel translation into logical pointer snapshots, including synthesized touch-mouse filtering and simultaneous-contact tests.
- Added the `fotos/LOGO.png` image to the visualizer's featured-ad card through an optional Windows Imaging Component PNG loader; headless tests assert the image pixels render.
- Added a public API reference entry page and an optional Doxygen generation target (`SPRITEUI_BUILD_DOCS`).

### Fixed

- Corrected MSDF screen-space coverage by converting the atlas `pxRange` from texels using the texture dimensions before applying UV derivatives.
- Centralized focus-candidate validation for spatial and semantic navigation without changing routing behavior.

### Still out of scope

- GPU backend and native ToolEngine ECS/window/input integration.
- HarfBuzz/FreeType font shaping and a production MSDF atlas.
- Light Prism integration, platform validation beyond the documented local checks, and GPU parity testing.
