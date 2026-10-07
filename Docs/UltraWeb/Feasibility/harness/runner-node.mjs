// node runner: node [--jitless] runner-node.mjs <mode> <iter> <engine> <env.js> <app.js...>
import fs from 'node:fs';
import vm from 'node:vm';
const [, , mode, iter, engine, envFile, ...appFiles] = process.argv;
// A browser has neither; leaving them would hand React's scheduler a
// different code path than the other engines take.
delete globalThis.setImmediate; delete globalThis.MessageChannel;
// Node's fetch family are lazy properties that load its HTTP stack (which
// needs WebAssembly, missing under --jitless) as soon as the environment
// looks at them. QuickJS has none of them either.
for (const k of ['fetch', 'FormData', 'Headers', 'Request', 'Response', 'MessageEvent', 'WebSocket', 'EventSource']) delete globalThis[k];
const st = setTimeout, ct = clearTimeout;
globalThis.__host = {
  mode, iterations: +iter, engine,
  setTimeout: (f, ms) => st(f, ms || 0), clearTimeout: (id) => ct(id),
  now: () => performance.now(), print: (s) => console.log(s),
};
vm.runInThisContext(fs.readFileSync(envFile, 'utf8'), { filename: envFile });
const t = performance.now();
for (const f of appFiles) vm.runInThisContext(fs.readFileSync(f, 'utf8'), { filename: f });
globalThis.__host.bundleEvalMs = +(performance.now() - t).toFixed(2);
globalThis.__runDriver();
