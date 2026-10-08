// Runs every scenario inside the WebAssembly build and checks it reproduces the native end state exactly.
import { readFileSync } from 'node:fs';

const [wasmPath, digestPath] = process.argv.slice(2);
const native = JSON.parse(readFileSync(digestPath, 'utf8'));
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const bs = instance.exports;
bs.__wasm_call_ctors?.();

const FRAME = 1 / 60;
const STRIDE = 20;
let worst = 0;
let failures = 0;

for (const [id, expected] of Object.entries(native)) {
  bs.bs_load(Number(id));
  for (let f = 0; f < expected.frames; f++) bs.bs_advance(FRAME);
  const count = bs.bs_pack_volumes();
  const buf = new Float64Array(bs.memory.buffer, bs.bs_volume_buffer(), count * STRIDE);
  const volumes = [];
  for (let i = 0; i < count; i++) {
    const o = i * STRIDE;
    volumes.push([buf[o + 2], buf[o + 3], buf[o + 4], buf[o + 6], buf[o + 8], buf[o + 9]]);
  }
  const compare = (a, b) => {
    const d = Math.abs(a - b) / Math.max(1, Math.abs(b));
    worst = Math.max(worst, d);
    return d <= 1e-12;
  };
  let ok = volumes.length === expected.volumes.length
    && compare(bs.bs_bender_thermal_work(), expected.thermal)
    && compare(bs.bs_bender_kinetic_work(), expected.kinetic);
  for (let i = 0; ok && i < volumes.length; i++) {
    for (let k = 0; k < 6; k++) ok = compare(volumes[i][k], expected.volumes[i][k]) && ok;
  }
  failures += ok ? 0 : 1;
  console.log(`  [${ok ? ' OK ' : 'FAIL'}] scenario ${id}: ${volumes.length} volumes after ${expected.frames} frames match native`);
}
console.log(`\nWebAssembly parity: worst relative difference ${worst.toExponential(2)}, ${failures === 0 ? 'ALL PASSED' : failures + ' FAILED'}`);
process.exit(failures === 0 ? 0 : 1);
