// Runs the scripted 3D sandbox session in the WebAssembly build and compares its digest with the native build's.
// Usage: node sandbox_parity.mjs bending_sandbox.wasm native_sandbox_digest.json
import { readFileSync } from "node:fs";

const [wasmPath, digestPath] = process.argv.slice(2);
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const sb = instance.exports;
sb.__wasm_call_ctors();
sb.sb_init();

const native = JSON.parse(readFileSync(digestPath, "utf8")).scripted_session;
const started = performance.now();
const wasm = sb.sb_run_scripted_session();
const elapsed = performance.now() - started;
const ok = wasm === native;
console.log(`  [${ok ? " OK " : "FAIL"}] scripted session digest: wasm ${wasm} vs native ${native} (${elapsed.toFixed(0)} ms in wasm)`);

// Smoke-test the exports the browser uses.
const player = new Float64Array(sb.memory.buffer, sb.sb_player(), 40);
const bodies = sb.sb_pack_bodies();
const volumes = sb.sb_pack_volumes();
const info = new Float64Array(sb.memory.buffer, sb.sb_terrain_info(), 8);
const smoke = bodies > 10 && volumes > 0 && info[0] === 241 && Number.isFinite(player[0]);
console.log(`  [${smoke ? " OK " : "FAIL"}] exports: ${bodies} bodies, ${volumes} volumes, ${info[0]}x${info[1]} terrain, player at (${player[0].toFixed(0)}, ${player[1].toFixed(0)})`);

const passed = ok && smoke;
console.log(`\n3D sandbox WebAssembly parity: ${passed ? "ALL PASSED" : "FAILED"}`);
process.exit(passed ? 0 : 1);
