#!/usr/bin/env bash
# Builds and runs every simulation test: kernel math + thermodynamics, reaction scenarios, terrain, water whip, the
# sparring partner's brain and the 3D sandbox (native), then both WebAssembly builds and parity checks that they reproduce the native results exactly.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="${ROOT}/Plugins/Bending/Source/Bending"
DEMO="${ROOT}/Tools/SimDemo"
SANDBOX="${ROOT}/Tools/Sandbox3D"
OUT="${ROOT}/Tests/Sim/build"
CXX="${CXX:-clang++}"
mkdir -p "${OUT}"

FLAGS=(-std=c++20 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wshadow -I "${SIM}/Public" -I "${DEMO}" -I "${ROOT}/Tests/Sim")
KERNEL=("${SIM}"/Private/Sim/*.cpp)

echo "== Building native tests with ${CXX}"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${ROOT}/Tests/Sim/ThermoTests.cpp" -o "${OUT}/thermo_tests"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${DEMO}/BendingSimDemo.cpp" "${ROOT}/Tests/Sim/ScenarioTests.cpp" -o "${OUT}/scenario_tests"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${ROOT}/Tests/Sim/TerrainTests.cpp" -o "${OUT}/terrain_tests"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${ROOT}/Tests/Sim/WhipTests.cpp" -o "${OUT}/whip_tests"
"${CXX}" "${FLAGS[@]}" "${KERNEL[@]}" "${ROOT}/Tests/Sim/SparringTests.cpp" -o "${OUT}/sparring_tests"
"${CXX}" "${FLAGS[@]}" -I "${SANDBOX}" "${KERNEL[@]}" "${SANDBOX}/BendingSandbox3D.cpp" "${ROOT}/Tests/Sim/SandboxTests.cpp" -o "${OUT}/sandbox_tests"

echo "== Thermodynamics & kernel math"
"${OUT}/thermo_tests"
echo "== Reaction scenarios"
"${OUT}/scenario_tests" "${OUT}/native_digest.json"
echo "== Terrain & earthbending"
"${OUT}/terrain_tests"
echo "== Water whip"
"${OUT}/whip_tests"
echo "== Sparring partner"
"${OUT}/sparring_tests"
echo "== 3D sandbox"
"${OUT}/sandbox_tests" "${OUT}/native_sandbox_digest.json"

echo "== WebAssembly builds + parity"
"${DEMO}/build_wasm.sh" "${OUT}"
node "${ROOT}/Tests/Sim/wasm_parity.mjs" "${OUT}/bending_sim.wasm" "${OUT}/native_digest.json"
"${SANDBOX}/build_wasm.sh" "${OUT}"
node "${ROOT}/Tests/Sim/sandbox_parity.mjs" "${OUT}/bending_sandbox.wasm" "${OUT}/native_sandbox_digest.json"
