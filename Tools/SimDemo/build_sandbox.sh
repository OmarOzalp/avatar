#!/usr/bin/env bash
# Builds the interactive Bending Physics Lab page: the WebAssembly kernel embedded into the HTML template.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-${HERE}/build}"
"${HERE}/build_wasm.sh" "${OUT}" > /dev/null

python3 - "${HERE}/sandbox/BendingLab.template.html" "${OUT}/bending_sim.wasm" "${OUT}/BendingLab.html" <<'PY'
import base64
import sys

template, wasm, out = sys.argv[1:4]
with open(template, encoding="utf-8") as f:
    html = f.read()
with open(wasm, "rb") as f:
    payload = base64.b64encode(f.read()).decode("ascii")
with open(out, "w", encoding="utf-8") as f:
    f.write(html.replace("__WASM_BASE64__", payload))
PY

echo "${OUT}/BendingLab.html ($(wc -c < "${OUT}/BendingLab.html") bytes)"
