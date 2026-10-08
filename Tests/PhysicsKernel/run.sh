#!/usr/bin/env bash
# Compiles and runs the engine-independent physics kernel tests with the system C++ compiler.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
OUT="${HERE}/build"
mkdir -p "${OUT}"

"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Werror \
	-I "${ROOT}/Plugins/Bending/Source/Bending/Public" \
	"${HERE}/ThermoKernelTests.cpp" -o "${OUT}/ThermoKernelTests"

"${OUT}/ThermoKernelTests"
