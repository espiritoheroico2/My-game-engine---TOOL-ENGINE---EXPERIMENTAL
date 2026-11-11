#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
if command -v ninja >/dev/null 2>&1; then
    cmake --preset default
    cmake --build --preset default
    ctest --preset default
else
    cmake -S . -B build -DSPRITEUI_BUILD_EXAMPLES=ON -DSPRITEUI_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure
fi
