#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/tests/build"
mkdir -p "$BUILD"

g++ -std=c++17 -Wall -Wextra -Werror \
  -I"$ROOT/main/include" \
  "$ROOT/tests/calendar_model_test.cpp" \
  "$ROOT/main/src/calendar_model.cpp" \
  -o "$BUILD/calendar_model_test"

"$BUILD/calendar_model_test"
