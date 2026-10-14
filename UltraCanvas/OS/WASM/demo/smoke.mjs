#!/usr/bin/env node
// Smoke test for the WebAssembly pages: serve a build directory with serve.py,
// open each page in a headless Chromium, wait for the backend's first frame
// and fail on a page that never presents one or whose console carries an
// error - a page whose builder threw ("Error creating example for ..."), an
// uncaught JavaScript error, an Emscripten abort. The screenshots and
// console logs go to SMOKE_OUT for a look afterwards.
//
//   node smoke.mjs <dir> <page[?query]>...
//
//   node smoke.mjs build-wasm UltraCanvasDemo.html \
//       'UltraCanvasDemo.html?component=jpegimages' wasm-demo.html
//
// Environment:
//   SMOKE_OUT         directory for <page>.png and <page>.log (default: smoke-out)
//   SMOKE_CHROME      a Chrome / Chromium binary to run
//   SMOKE_CHANNEL     a Playwright channel instead ("chrome" uses the machine's
//                     Google Chrome, which the GitHub runners carry)
//                     neither: Playwright's own Chromium, if installed
//   SMOKE_PLAYWRIGHT  path of the playwright package when it is not resolvable
//                     from this file (an npm install elsewhere)
//   SMOKE_PORT        serve.py's port (default 8765)
//   SMOKE_TIMEOUT     ms to wait for the first frame (default 120000)
//   SMOKE_SETTLE      ms to keep collecting after the first frame (default 3000)
//
// The pages need the cross-origin isolation headers serve.py sends, so this
// script always serves the directory itself rather than taking a URL.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const [dir, ...specs] = process.argv.slice(2);
if (!dir || specs.length === 0) {
    console.error('usage: node smoke.mjs <dir> <page[?query]>...');
    process.exit(2);
}
const out = process.env.SMOKE_OUT || 'smoke-out';
const port = Number(process.env.SMOKE_PORT || 8765);
const firstFrameTimeout = Number(process.env.SMOKE_TIMEOUT || 120000);
const settle = Number(process.env.SMOKE_SETTLE || 3000);
fs.mkdirSync(out, { recursive: true });

const { chromium } = await import(process.env.SMOKE_PLAYWRIGHT || 'playwright');

const here = path.dirname(fileURLToPath(import.meta.url));
const server = spawn('python3', [path.join(here, 'serve.py'), String(port)],
    { cwd: dir, stdio: 'ignore' });
const base = `http://127.0.0.1:${port}/`;
for (let i = 0; ; ++i) {
    try { await fetch(base); break; } catch (e) {
        if (i === 50) { console.error('serve.py did not come up on port ' + port); server.kill(); process.exit(2); }
        await new Promise(r => setTimeout(r, 100));
    }
}

const launch = {};
if (process.env.SMOKE_CHROME) launch.executablePath = process.env.SMOKE_CHROME;
else if (process.env.SMOKE_CHANNEL) launch.channel = process.env.SMOKE_CHANNEL;
const browser = await chromium.launch(launch);
console.log(`smoke: ${browser.version()} on ${base}, ${specs.length} page(s)`);

// What fails a page. "Failed to load image" and the like are the framework
// reporting a sample it cannot read, which a page shows and survives.
const failing = /Error creating example|Uncaught|Aborted\(|RuntimeError|unreachable executed|memory access out of bounds|exception thrown and not caught/;
const firstFrame = /first frame presented (\d+) ms/;

let failures = 0;
for (const spec of specs) {
    const name = spec.replace(/\.html/, '').replace(/[^A-Za-z0-9_-]+/g, '_');
    const ctx = await browser.newContext({ viewport: { width: 1400, height: 900 } });
    const page = await ctx.newPage();
    const lines = [];
    let presented = null;
    let resolveFrame;
    const framePromise = new Promise(r => { resolveFrame = r; });
    // A failing line ends the wait at once: a page that aborted during
    // start-up never presents a frame, and the timeout would only add
    // minutes per page to the job.
    page.on('console', m => {
        const text = m.text();
        lines.push(`[${m.type()}] ${text}`);
        const hit = firstFrame.exec(text);
        if (hit && presented === null) { presented = Number(hit[1]); resolveFrame(true); }
        if (failing.test(text)) resolveFrame(false);
    });
    page.on('pageerror', e => { lines.push(`[pageerror] ${e.message}`); resolveFrame(false); });
    page.on('crash', () => { lines.push('[pageerror] the page crashed'); resolveFrame(false); });

    const problems = [];
    try {
        await page.goto(base + spec, { waitUntil: 'load', timeout: firstFrameTimeout });
        const framed = await Promise.race([framePromise, new Promise(r => setTimeout(() => r(false), firstFrameTimeout))]);
        if (!framed && presented === null) problems.push(`no first frame within ${firstFrameTimeout} ms`);
        await page.waitForTimeout(settle);
        await page.screenshot({ path: path.join(out, `${name}.png`) });
    } catch (e) {
        problems.push(e.message.split('\n')[0]);
    }
    fs.writeFileSync(path.join(out, `${name}.log`), lines.join('\n') + '\n');
    const bad = lines.filter(l => failing.test(l));
    if (bad.length) problems.push(...bad.slice(0, 10));
    await ctx.close();

    if (problems.length) {
        failures++;
        console.log(`FAIL ${spec}`);
        for (const p of problems) console.log(`     ${p}`);
        // The end of the console, so the job log says how far the page got
        // without a trip to the artifact.
        const tail = lines.slice(-20);
        if (tail.length) {
            console.log(`     last ${tail.length} of ${lines.length} console lines:`);
            for (const l of tail) console.log(`       ${l.slice(0, 300)}`);
        }
    } else {
        console.log(`ok   ${spec}  first frame ${presented} ms, ${lines.length} console lines`);
    }
}

await browser.close();
server.kill();
if (failures) {
    console.log(`smoke: ${failures} of ${specs.length} page(s) failed; screenshots and logs in ${out}/`);
    process.exit(1);
}
console.log(`smoke: all ${specs.length} page(s) presented a frame with a clean console`);
