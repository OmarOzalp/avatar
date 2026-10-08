#!/usr/bin/env bash
# Builds the bending kernel + 3D sandbox into a freestanding WebAssembly module (no Emscripten, no libc).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
SIM="${ROOT}/Plugins/Bending/Source/Bending"
OUT="${1:-${HERE}/build}"
mkdir -p "${OUT}"

clang++ --target=wasm32 -std=c++20 -O2 \
	-nostdlib -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-math-errno -ffp-contract=off \
	-mbulk-memory -Wall -Wextra -Werror \
	-I "${SIM}/Public" -I "${HERE}" \
	"${SIM}"/Private/Sim/*.cpp "${HERE}/BendingSandbox3D.cpp" "${HERE}/BendingSandboxWasm.cpp" \
	-Wl,--no-entry -Wl,--export=__wasm_call_ctors -Wl,--export-memory -Wl,-z,stack-size=2097152 \
	-o "${OUT}/bending_sandbox.wasm"

echo "${OUT}/bending_sandbox.wasm ($(wc -c < "${OUT}/bending_sandbox.wasm") bytes)"
