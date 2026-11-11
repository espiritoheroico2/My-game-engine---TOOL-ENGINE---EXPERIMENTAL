# Thread-safety notes

This document describes the current core API. It is not a promise that future backends or adapters are thread-safe.

## General rule

Unless a type explicitly documents synchronization, treat each mutable SpriteUI object as **not thread-safe**. Do not concurrently mutate or read/write the same instance from multiple threads without external synchronization.

## Practical guidance

- Use separate instances of stateful classes (for example `LayoutTree`, `UIEventRouter`, `UINavigationRouter`, `ToonMotionPlayer`, `UIConfigManager` targets, widgets and dock nodes) per thread, or guard shared instances with a mutex.
- Functions that take caller-owned inputs and return new values (for example `RenderExtractor::Build`, `LayoutText` and `BuildClipHierarchy`) do not share internal mutable state in the current implementation. Concurrent calls are safe only when the input ranges and referenced objects are not being mutated concurrently.
- `NullRenderBackend::Submit` mutates its statistics and must not be called concurrently on the same backend instance without synchronization.
- The SDL3 adapter must be created and used on SDL's main thread. Its `SDL_Window` must outlive the adapter, and SDL event polling must be coordinated by the application event loop.
- A `FramePacket` may be read concurrently only after construction is complete and while no thread mutates it.
- The test harness uses global counters and is single-threaded.

## Future integrations

An ECS adapter, window/input adapter and GPU backend must document their own thread-affinity and synchronization requirements. GPU resource lifetime, command submission and fence synchronization belong to the concrete backend; the CPU core does not establish those guarantees.
