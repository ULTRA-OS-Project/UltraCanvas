// Parse cost of a large bundle on QuickJS, and what a bytecode cache saves:
//   qjs parse-qjs.mjs <file.js>
// Prints one JSON line: compile time from source, size of the bytecode,
// and the time to load that bytecode back instead of compiling again.
import * as std from 'qjs:std';
import * as bjson from 'qjs:bjson';

const file = scriptArgs[1];
const src = std.loadFile(file);
const best = (f, n = 3) => { let m = Infinity; for (let i = 0; i < n; i++) { const t = performance.now(); f(); m = Math.min(m, performance.now() - t); } return m; };

let fn;
const compileMs = best(() => { fn = std.evalScript(src, { compile_only: true }); });
const bc = bjson.write(fn, bjson.WRITE_OBJ_BYTECODE | bjson.WRITE_OBJ_STRIP_DEBUG);
const loadMs = best(() => bjson.read(bc, 0, bc.byteLength, bjson.READ_OBJ_BYTECODE));
print(JSON.stringify({
    file: file.split('/').pop(), sourceBytes: src.length, bytecodeBytes: bc.byteLength,
    compileMs: +compileMs.toFixed(1), compileMBps: +(src.length / 1048576 / (compileMs / 1000)).toFixed(1),
    bytecodeLoadMs: +loadMs.toFixed(1),
}));
