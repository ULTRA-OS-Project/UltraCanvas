// Engine-independent benchmark driver: clicks the app's buttons through
// DOM events, flushes the framework, checks the resulting DOM, and times
// each operation. Results go to host.print as one JSON line.
const host = globalThis.__host;
const { document, window } = globalThis.__spike;
const ITER = host.iterations || 5;

function click(el) {
  if (!el) throw new Error('click target missing');
  el.dispatchEvent(new window.Event('click', { bubbles: true, cancelable: true }));
}
const rows = () => document.querySelectorAll('tbody tr');
const assert = (c, m) => { if (!c) throw new Error('check failed: ' + m); };
const label = (tr) => tr.querySelector('td:nth-child(2) a').textContent;
const idOf = (tr) => tr.querySelector('td').textContent;
const byId = (id) => () => document.getElementById(id);
let swapIds = null;

// [name, prepare (untimed, returns the click target), check (untimed)]
const ops = [
  ['create1k', byId('run'), () => assert(rows().length === 1000, '1000 rows')],
  ['replace1k', byId('run'), () => assert(rows().length === 1000, '1000 rows after replace')],
  ['update10th', byId('update'), () => assert(label(rows()[0]).endsWith(' !!!') && !label(rows()[1]).endsWith(' !!!'), 'every 10th updated')],
  ['select', () => rows()[1].querySelector('td:nth-child(2) a'), () => assert(rows()[1].className === 'danger', 'row 2 selected')],
  ['swap', () => { const r = rows(); swapIds = [idOf(r[1]), idOf(r[998])]; return document.getElementById('swaprows'); },
           () => { const r = rows(); assert(idOf(r[1]) === swapIds[1] && idOf(r[998]) === swapIds[0], 'rows swapped'); }],
  ['remove', () => rows()[4].querySelector('a.remove'), () => assert(rows().length === 999, '999 rows')],
  ['clear1k', byId('clear'), () => assert(rows().length === 0, 'cleared')],
  ['create10k', byId('runlots'), () => assert(rows().length === 10000, '10000 rows')],
  ['clear10k', byId('clear'), () => assert(rows().length === 0, 'cleared 10k')],
  ['append1k', () => { click(document.getElementById('run')); globalThis.__app.flush(); return document.getElementById('add'); },
               () => assert(rows().length === 2000, '2000 rows')],
  ['clear2k', byId('clear'), () => assert(rows().length === 0, 'cleared 2k')],
];

function median(a) { const s = [...a].sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; }

export async function run() {
  const out = { app: globalThis.__app.name, engine: host.engine, mode: globalThis.__spike.mode, iterations: ITER, bundleEvalMs: host.bundleEvalMs, ok: true, ops: {}, dom: {} };
  try {
    const container = document.getElementById('root');
    let t = host.now();
    await globalThis.__app.mount(container);
    out.mountMs = +(host.now() - t).toFixed(2);
    const times = {}, dom = {};
    for (let i = 0; i < ITER; i++) {
      for (const [name, prepare, check] of ops) {
        const target = prepare();
        globalThis.__spike.resetStats();
        t = host.now();
        click(target);
        globalThis.__app.flush();
        const dt = host.now() - t;
        (dom[name] ||= []).push(globalThis.__spike.stats());
        check();
        (times[name] ||= []).push(dt);
      }
    }
    for (const k in times) out.ops[k] = +median(times[k]).toFixed(2);
    if (globalThis.__spike.mode === 'surface') {
      for (const k in dom) out.dom[k] = { domMs: +median(dom[k].map((d) => d.domTime)).toFixed(2), calls: median(dom[k].map((d) => d.domCalls)) };
      out.surface = globalThis.__spike.surface();
      out.probes = globalThis.__spike.probes();
    }
  } catch (e) {
    out.ok = false; out.error = String(e && e.stack || e);
  }
  host.print(JSON.stringify(out));
}
