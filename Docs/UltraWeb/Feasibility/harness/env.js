// Browser-like environment for the spike: linkedom supplies a JS DOM, the
// host (qjs / node / quickjs-in-wasm runner) supplies timers and a clock via
// globalThis.__host. In "surface" mode every DOM call made by framework code
// (not by linkedom itself) is recorded, and the time spent inside the DOM is
// accounted separately so framework time and DOM time can be told apart.
import * as linkedom from 'linkedom';

const host = globalThis.__host;
const mode = host.mode || 'timing';
const surface = new Map();
const rec = (name) => surface.set(name, (surface.get(name) || 0) + 1);
let depth = 0, domTime = 0, domCalls = 0;
const now = host.now;

const { window, document } = linkedom.parseHTML(
  '<!doctype html><html><head><title>spike</title></head><body><div id="root"></div></body></html>');

function wrapFn(name, fn) {
  // dispatchEvent runs the listeners, which are framework code (with zone.js,
  // Angular's whole change detection): record it, but time only the DOM
  // calls those listeners make, not the dispatch around them.
  const timed = !name.endsWith('.dispatchEvent()');
  return function (...args) {
    if (depth === 0 && !timed) {
      rec(name); domCalls++;
      depth++;
      try { return fn.apply(this, args); } finally { depth--; }
    }
    if (depth === 0) {
      rec(name); domCalls++;
      depth++; const t = now();
      try { return fn.apply(this, args); } finally { domTime += now() - t; depth--; }
    }
    depth++;
    try { return fn.apply(this, args); } finally { depth--; }
  };
}
// A listener runs framework code again: reset the depth while it runs.
const listenerWrappers = new WeakMap();
function wrapListener(l) {
  if (!l) return l;
  let w = listenerWrappers.get(l);
  if (!w) {
    w = function (ev) {
      const saved = depth; depth = 0;
      try { return typeof l === 'function' ? l.call(this, ev) : l.handleEvent(ev); }
      finally { depth = saved; }
    };
    listenerWrappers.set(l, w);
  }
  return w;
}

const instrumented = new Set();
// Stop at the engine's own prototypes: NodeList extends Array, and the
// engine's built-ins are not part of the DOM surface being measured.
// Plain values only: reading Node's lazy getters (fetch, WebSocket) would
// load its HTTP stack, which needs WebAssembly and fails under --jitless.
for (const k of Object.getOwnPropertyNames(globalThis)) {
  const v = Object.getOwnPropertyDescriptor(globalThis, k).value;
  if (typeof v === 'function' && v.prototype) instrumented.add(v.prototype);
}
function instrumentProto(proto) {
  while (proto && proto !== Object.prototype && !instrumented.has(proto)) {
    instrumented.add(proto);
    const raw = (proto.constructor && proto.constructor.name) || '?';
    const cls = ({ DOMEventTarget: 'EventTarget', GlobalEvent: 'Event' })[raw] || raw.replace(/\d+$/, '');
    for (const key of Object.getOwnPropertyNames(proto)) {
      if (key === 'constructor') continue;
      const d = Object.getOwnPropertyDescriptor(proto, key);
      if (!d.configurable) continue;
      const nm = cls + '.' + key;
      if (typeof d.value === 'function') {
        let fn = d.value;
        if (key === 'addEventListener' || key === 'removeEventListener') {
          const orig = fn;
          fn = function (type, l, o) { return orig.call(this, type, wrapListener(l), o); };
        }
        d.value = wrapFn(nm + '()', fn);
      } else {
        if (d.get) d.get = wrapFn(nm, d.get);
        if (d.set) d.set = wrapFn(nm + ' =', d.set);
      }
      Object.defineProperty(proto, key, d);
    }
    proto = Object.getPrototypeOf(proto);
  }
}

// Constructors the DOM exposes (HTMLElement, Event, Node ...).
const domClasses = {};
for (const [k, v] of Object.entries(linkedom)) {
  if (typeof v === 'function' && v.prototype && /^[A-Z]/.test(k)) domClasses[k] = v;
}
for (const k of Object.getOwnPropertyNames(window)) {
  try {
    // linkedom's window falls through to globalThis; keep only DOM classes,
    // never the engine's own built-ins (Map, Promise ...) or its getters.
    const g = Object.getOwnPropertyDescriptor(globalThis, k);
    if (g && g.get) continue;
    const v = window[k];
    if (typeof v === 'function' && v.prototype && /^[A-Z]/.test(k) && !(g && g.value === v)) domClasses[k] = v;
  } catch (e) {}
}
for (const k of Object.keys(domClasses)) {
  const d = Object.getOwnPropertyDescriptor(globalThis, k);
  if (d && d.value === domClasses[k]) delete domClasses[k];
}

// Host-provided globals a browser has. Timers come from the runner.
let rafId = 0;
const hostGlobals = {
  window: null, document, self: null,
  navigator: { userAgent: 'UltraWeb-spike', language: 'en-US', languages: ['en-US'], platform: 'Linux', onLine: true, hardwareConcurrency: 1, cookieEnabled: false },
  location: { href: 'https://spike.local/', origin: 'https://spike.local', protocol: 'https:', host: 'spike.local', hostname: 'spike.local', port: '', pathname: '/', search: '', hash: '' },
  setTimeout: host.setTimeout, clearTimeout: host.clearTimeout,
  setInterval: (fn, ms, ...a) => { const o = { id: 0 }; const tick = () => { o.id = host.setTimeout(tick, ms); fn(...a); }; o.id = host.setTimeout(tick, ms); return o; },
  clearInterval: (o) => o && host.clearTimeout(o.id),
  requestAnimationFrame: (cb) => host.setTimeout(() => cb(now()), 16),
  cancelAnimationFrame: (id) => host.clearTimeout(id),
  getComputedStyle: (el) => el.style,
  matchMedia: (q) => ({ matches: false, media: q, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} }),
};
if (mode === 'surface') {
  for (const c of Object.values(domClasses)) instrumentProto(c.prototype);
  instrumentProto(Object.getPrototypeOf(document));
}

const winProxy = mode === 'surface' ? new Proxy(window, {
  get(t, k, r) { if (typeof k === 'string') rec('window.' + k); const v = k in hostGlobals ? hostGlobals[k] : Reflect.get(t, k); return v; },
}) : window;
hostGlobals.window = winProxy; hostGlobals.self = winProxy;

function defineGlobal(k, v) {
  if (mode === 'surface') {
    Object.defineProperty(globalThis, k, { configurable: true, get() { rec('global ' + k); return v; }, set(nv) { v = nv; } });
  } else {
    Object.defineProperty(globalThis, k, { configurable: true, writable: true, value: v });
  }
}
for (const [k, v] of Object.entries(domClasses)) defineGlobal(k, v);
for (const [k, v] of Object.entries(hostGlobals)) defineGlobal(k, v);
for (const [k, v] of Object.entries(hostGlobals)) if (!(k in window)) try { window[k] = v; } catch (e) {}

// Missing-global probes ("typeof setImmediate") reach the global object's
// prototype; a proxy there records the names frameworks look for.
const probes = new Set();
if (mode === 'surface') {
  try {
    const base = Object.getPrototypeOf(globalThis);
    Object.setPrototypeOf(globalThis, new Proxy(base, {
      has(t, k) { if (typeof k === 'string' && !(k in t)) probes.add(k); return k in t; },
      get(t, k, r) { return Reflect.get(t, k, r); },
    }));
  } catch (e) { probes.add('(probe proxy unsupported: ' + e.message + ')'); }
}

globalThis.__spike = {
  window, document, mode,
  stats() { return { domTime, domCalls }; },
  resetStats() { domTime = 0; domCalls = 0; },
  surface() { return [...surface.entries()].sort((a, b) => b[1] - a[1]); },
  probes() { return [...probes].sort(); },
};
