#!/usr/bin/env bash
# Builds and runs every simulation test: kernel math + thermodynamics, reaction scenarios (native),
# then the WebAssembly build and a parity check that it reproduces the native results exactly.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="${ROOT}/Plugins/Bending/Source/Bending"
DEMO="${ROOT}/Tools/SimDemo"
OUT="${ROOT}/Tests/Sim/build"
CXX="${CXX:-clang++}"
mkdir -p "${OUT}"

FLAGS=(-std=c++20 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wshadow -I "${SIM}/Public" -I "${DEMO}" -I "${ROOT}/Tests/Sim")
KERNEL=("${SIM}"/Private/Sim/*.cpp)

echo "== Building native tests with ${CXX}"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${ROOT}/Tests/Sim/ThermoTests.cpp" -o "${OUT}/thermo_tests"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${DEMO}/BendingSimDemo.cpp" "${ROOT}/Tests/Sim/ScenarioTests.cpp" -o "${OUT}/scenario_tests"

echo "== Thermodynamics & kernel math"
"${OUT}/thermo_tests"
echo "== Reaction scenarios"
"${OUT}/scenario_tests" "${OUT}/native_digest.json"

echo "== WebAssembly build + parity"
"${DEMO}/build_wasm.sh" "${OUT}"
node "${ROOT}/Tests/Sim/wasm_parity.mjs" "${OUT}/bending_sim.wasm" "${OUT}/native_digest.json"
