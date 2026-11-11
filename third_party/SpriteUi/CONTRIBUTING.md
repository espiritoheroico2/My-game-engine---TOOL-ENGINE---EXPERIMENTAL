# Contributing

## Development setup

SpriteUI requires CMake 3.21+ and a C++20 compiler. The core has no mandatory third-party runtime dependencies.

Configure, build and run tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSPRITEUI_CHECK_HEADERS=ON
cmake --build build --parallel
cmake --build build --target spriteui_check_headers --parallel
ctest --test-dir build --output-on-failure
```

To run the optional CPU benchmarks:

```sh
cmake -S . -B build-bench -DSPRITEUI_BUILD_TESTS=OFF -DSPRITEUI_BUILD_BENCHMARKS=ON
cmake --build build-bench --target spriteui_benchmarks --parallel
./build-bench/spriteui_benchmarks
```

## Change expectations

- Keep the core independent of ECS and graphics API integrations.
- Follow `docs/CODING_STANDARDS.md` and the existing module/API patterns.
- Add or update focused tests for behavior changes and regressions.
- Keep public headers self-contained and include new public headers in `SpriteUi/SpriteUi.hpp`.
- Update the relevant documentation when behavior, limitations or build steps change.
- Do not commit generated build output, snapshots, credentials or other sensitive data.

The GitHub Actions workflow runs compiler, header, package-consumer and test checks. Benchmark timings are informational and are not pass/fail thresholds.
