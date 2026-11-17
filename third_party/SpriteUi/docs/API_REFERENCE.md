# SpriteUI API reference

This page is the entry point for the public C++ API. For generated class and member pages, configure
the optional Doxygen target with `SPRITEUI_BUILD_DOCS=ON`.

## Public modules

| Module | Public headers | Responsibility |
|---|---|---|
| Core | `SpriteUi/Core/*.hpp` | Handles, math, transforms, invalidation bits |
| Input | `SpriteUi/Input/*.hpp` | Single/multi-pointer routing, semantic navigation, hit targets |
| Layout | `SpriteUi/Layout/*.hpp` | Flex layout, entity tree, scene-to-render bridge |
| Widgets | `SpriteUi/Widgets/*.hpp` | Logical controls, virtual lists, docking, graphs, popups |
| Animation | `SpriteUi/Animation/ToonMotion.hpp` | Runtime tweens and keyframe timeline sampling/playback |
| Binding | `SpriteUi/Binding/ValueBinding.hpp` | Typed value-change detection |
| Text | `SpriteUi/Text/*.hpp` | Shaping contract, line breaking, layout, caret, text cache |
| Rendering | `SpriteUi/Rendering/*.hpp` | `UIRenderItem`, 80-byte GPU ABI, clips, batches, icons |
| Platform | `SpriteUi/Platform/*.hpp` | Optional SDL3 host adapter and standalone window wrapper |
| Config | `SpriteUi/Config/UIConfig.hpp` | Validated JSON settings persistence |

Include `<SpriteUi/SpriteUi.hpp>` for the core API. It is C++20 and does not require SDL3, ImGui,
an ECS, or a graphics API. Consumers that need the SDL3 adapter link the separate
`SpriteUi::SDL3Input` target.

## Frame lifecycle

1. The host updates logical UI state and solves layout.
2. The host maps its entities and input into `HitTarget`, `UIRenderItem`, and
   `UIInputSnapshot`; the core does not own the host's ECS or event pump.
3. `Input::UIEventRouter::Process` routes a single pointer, or
   `Input::UIMultiPointerEventRouter::ProcessFrame` routes stable per-pointer snapshots and cancels
   pointers omitted from the next frame.
4. `Rendering::RenderExtractor::Build` converts visible render items and clip rectangles into a
   stable, paint-ordered `Rendering::FramePacket`.
5. Validate with `Rendering::IsFramePacketValid`, then submit through a host-owned
   `Rendering::IRenderBackend`.

The CPU `NullRenderBackend` validates packets but does not draw. The SDL3 visualizer's software
renderer is a test/reference renderer, not a production GPU backend.

Multi-pointer routing is host-platform agnostic. It expects one snapshot per currently active
pointer each frame, all sharing that frame ID. Omitted pointers are cancelled and removed; duplicate
pointer IDs reject the frame. Hover/capture/focus state is tracked per pointer, so the host must
define which pointer is allowed to own keyboard focus. The SDL3 adapter's `BuildPointerSnapshots`
translates mouse and active finger contacts, assigns per-contact pointer IDs, and marks cancelled
fingers without manufacturing clicks. Applications that keep using the legacy `BuildSnapshot`
continue to receive mouse-only input.

## Rendering and alpha

- `UIRenderItem::tintPremultipliedLinear` uses linear premultiplied RGBA.
- `UIRenderItem::opacity` is a finite scalar clamped to `[0, 1]` during extraction; it scales tint
  RGB and alpha together without changing `SpriteInstanceGPU`.
- The instance ABI is 80 bytes. Update its HLSL contract and compile-time layout assertions together
  if it ever changes.
- Draw batch order preserves visual paint order; non-adjacent materials must not be reordered across
  alpha-blended items.
- `Rendering::UIIcon` references an already-loaded texture slice and normalized UV rectangle.
  Decoding, rasterizing, atlas packing, GPU upload, and resource lifetime belong to the host asset
  pipeline.

## Animation

`Animation::ToonMotionSystem` advances runtime tweens and reports samples/completions. The host
applies samples to ECS/components. `Animation::ToonTimeline` stores validated tracks and samples values at a requested time.
`Animation::ToonTimelinePlayer` adds host-driven play, pause, stop, seek, and optional looping;
call `Update(deltaTime, timeline, samples)` and apply the returned values. The host still owns the
clock source, editor controls, persistence, and ECS writes.

## Build and tests

```powershell
cmake -S . -B build -DSPRITEUI_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

For Clang builds, an optional libFuzzer target exercises JSON settings validation and
serialization round trips:

```sh
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DSPRITEUI_BUILD_FUZZERS=ON
cmake --build build-fuzz --target spriteui_fuzz_config
./build-fuzz/spriteui_fuzz_config tests/fuzz/corpus/config -max_total_time=60
```

Generate HTML API pages when Doxygen is installed:

```powershell
cmake -S . -B build-docs -DSPRITEUI_BUILD_DOCS=ON
cmake --build build-docs --target spriteui_docs
```

See [`TOOLENGINE_INTEGRATION_GUIDE.md`](TOOLENGINE_INTEGRATION_GUIDE.md) for the engine-specific
integration sequence and unresolved host contracts.
