#!/usr/bin/env bash
# Build and run the editor (Linux/macOS).  Usage: ./build_editor.sh [--gpu] [--no-run]
# No internet, git or Ninja needed: all third-party sources are in third_party/.
# --gpu needs third_party/DiligentCore (not shipped; see HOW_TO_BUILD.txt).
set -uo pipefail
cd "$(dirname "$0")"
PRESET=editor; RUN=1
for a in "$@"; do case "$a" in --gpu) PRESET=editor-gpu;; --no-run) RUN=0;; esac; done
command -v cmake >/dev/null || { echo "ERROR: cmake not found. Install CMake 3.25 or newer."; exit 1; }
{ cmake --preset "$PRESET" && cmake --build --preset "$PRESET"; } 2>&1 | tee build_log.txt
if [ "${PIPESTATUS[0]}" -ne 0 ]; then echo "BUILD FAILED. Send build_log.txt to the developer."; exit 1; fi
echo; echo "Done: Projeto Final/ToolEditor"
[ "$RUN" = 1 ] && exec "Projeto Final/ToolEditor"
