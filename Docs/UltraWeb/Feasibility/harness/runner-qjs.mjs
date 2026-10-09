// qjs runner (native and WASI builds): qjs runner-qjs.mjs <mode> <iter> <engine> <env.js> <app.js...>
import * as std from 'qjs:std';
import * as os from 'qjs:os';
const [, mode, iter, engine, envFile, ...appFiles] = scriptArgs;
globalThis.__host = {
  mode, iterations: +iter, engine,
  setTimeout: (f, ms) => os.setTimeout(f, ms || 0), clearTimeout: (id) => os.clearTimeout(id),
  now: () => performance.now(), print: (s) => print(s),
};
std.loadScript(envFile);
const t = performance.now();
for (const f of appFiles) std.loadScript(f);
globalThis.__host.bundleEvalMs = +(performance.now() - t).toFixed(2);
globalThis.__runDriver();
