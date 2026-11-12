#!/usr/bin/env node
// OS/WASM/runtime/test/runtime-test.mjs
// End-to-end test of the UltraCanvas runtime in Chromium
// (Docs/UltraCanvas/UltraCanvasWasmRuntime.md): the sample C++ app
// hello-app.wasm, a side module, on the sample site, three ways.
//
//   1. No extension, the site with the runtime (index.html): the runtime
//      comes from the site; the app's button, checkbox and its own element
//      answer real clicks.
//   2. No extension, the site without the runtime (extension-only.html):
//      the page says the extension is needed.
//   3. The extension installed: both pages take the runtime from it, the
//      app answers clicks, and the site sends only the page, the loader and
//      the app.
//
//   node runtime-test.mjs <build dir of UltraCanvas/OS/WASM/runtime>
//
// The app prints a line for each event ("[hello-app] clicked 1"), and its
// controls sit at fixed positions, so the test clicks there and waits for
// the line.
//
// Environment:
//   UCR_OUT         screenshots and logs (default: ucr-test-out)
//   UCR_CHROME      a Chrome / Chromium binary (default: Playwright's Chromium)
//   UCR_PLAYWRIGHT  the playwright package when not resolvable from here
//   UCR_TIMEOUT     ms to wait for the app to start (default 120000)
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';

const buildDir = process.argv[2];
if (!buildDir) {
    console.error('usage: node runtime-test.mjs <build dir>');
    process.exit(2);
}
const site = path.resolve(buildDir, 'site');
const extension = path.resolve(buildDir, 'ultracanvas-extension');
const out = process.env.UCR_OUT || 'ucr-test-out';
const startTimeout = Number(process.env.UCR_TIMEOUT || 120000);
fs.mkdirSync(out, { recursive: true });
const { chromium } = await import(process.env.UCR_PLAYWRIGHT || 'playwright');

// ===== A static server that counts what it sends =====
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
                '.data': 'application/octet-stream' };
let served = [];
const server = http.createServer((req, res) => {
    const file = path.join(site, decodeURIComponent(new URL(req.url, 'http://x').pathname));
    if (!file.startsWith(site) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
        res.writeHead(404);
        res.end();
        return;
    }
    const body = fs.readFileSync(file);
    served.push({ path: path.relative(site, file), bytes: body.length });
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
    res.end(body);
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}/`;

let failures = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
    if (!ok) ++failures;
};
const failing = /Uncaught|Aborted\(|RuntimeError|unreachable executed|memory access out of bounds|undefined symbol/;

function watch(page, name) {
    const lines = [];
    page.on('console', (m) => lines.push(m.text()));
    page.on('pageerror', (e) => lines.push(`[pageerror] ${e.message}`));
    return {
        lines,
        async waitFor(text, ms = 15000) {
            const until = Date.now() + ms;
            while (Date.now() < until) {
                if (lines.some((l) => l.includes(text))) return true;
                await page.waitForTimeout(100);
            }
            return false;
        },
        done() {
            fs.writeFileSync(path.join(out, name + '.log'), lines.join('\n') + '\n');
            const bad = lines.filter((l) => failing.test(l));
            check(bad.length === 0, `${name}: no errors in the console` + (bad.length ? ` (${bad[0]})` : ''));
        },
    };
}

async function waitState(page) {
    await page.waitForFunction(() => document.documentElement.dataset.ultracanvasState, null, { timeout: startTimeout });
    return page.evaluate(() => ({
        state: document.documentElement.dataset.ultracanvasState,
        source: document.documentElement.dataset.ultracanvasRuntimeSource,
        status: (document.getElementById('ultracanvas-status') || {}).textContent,
    }));
}

// A click at the app's own window coordinates.
async function clickAt(page, x, y) {
    const canvas = await page.evaluate(() => {
        const r = document.querySelector('canvas').getBoundingClientRect();
        return { x: r.left, y: r.top };
    });
    await page.mouse.click(canvas.x + x, canvas.y + y);
}

// The sample app's controls (examples/hello-app.cpp).
async function exercise(page, log, label) {
    check(await log.waitFor('[hello-app] started', startTimeout), `${label}: the app's main() ran`);
    check(log.lines.some((l) => l.includes('[hello-app] caught an exception thrown in the runtime')),
          `${label}: an exception thrown in the runtime was caught in the app`);
    await page.waitForTimeout(500);
    await clickAt(page, 120, 110);
    check(await log.waitFor('[hello-app] clicked 1'), `${label}: the button's onClick reached the app`);
    await clickAt(page, 30, 164);
    check(await log.waitFor('[hello-app] ticked 1'), `${label}: the checkbox's onStateChanged reached the app`);
    await clickAt(page, 60, 270);
    check(await log.waitFor('[hello-app] dot green'), `${label}: the app's own element drew and took the click`);
}

const kib = (bytes) => (bytes / 1024).toFixed(1) + ' KiB';
const total = (list) => list.reduce((sum, r) => sum + r.bytes, 0);
const launch = { headless: true };
if (process.env.UCR_CHROME) launch.executablePath = process.env.UCR_CHROME;

// ===== 1. No extension, the runtime from the site =====
{
    const browser = await chromium.launch(launch);
    const page = await browser.newPage({ viewport: { width: 900, height: 600 } });
    const log = watch(page, '1-site');
    served = [];
    await page.goto(base + 'index.html');
    const s = await waitState(page);
    check(s.state === 'running', `site: the app runs (${s.status})`);
    check(s.source === 'site', `site: the runtime came from the site (${s.source})`);
    await exercise(page, log, 'site');
    await page.screenshot({ path: path.join(out, '1-site.png') });
    log.done();
    await browser.close();
    console.log(`     the site sent ${kib(total(served))}:`);
    for (const r of served) console.log(`       ${r.path.padEnd(34)} ${kib(r.bytes)}`);
}

// ===== 2. No extension, no runtime on the site =====
{
    const browser = await chromium.launch(launch);
    const page = await browser.newPage();
    const log = watch(page, '2-no-extension');
    await page.goto(base + 'extension-only.html');
    const s = await waitState(page);
    check(s.state === 'failed' && /extension/.test(s.status), `no extension: the page asks for it (${s.status})`);
    log.done();
    await browser.close();
}

// ===== 3. The extension installed =====
{
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'ucr-profile-'));
    // The full Chromium, not Playwright's headless shell, which has no extensions.
    const context = await chromium.launchPersistentContext(profile, {
        ...launch,
        ...(launch.executablePath ? {} : { channel: 'chromium' }),
        viewport: { width: 900, height: 600 },
        args: [`--disable-extensions-except=${extension}`, `--load-extension=${extension}`],
    });
    for (const pageName of ['extension-only.html', 'index.html']) {
        const page = await context.newPage();
        const name = '3-extension-' + pageName.replace('.html', '');
        const log = watch(page, name);
        served = [];
        await page.goto(base + pageName);
        const s = await waitState(page);
        check(s.state === 'running', `extension, ${pageName}: the app runs (${s.status})`);
        check(s.source === 'extension', `extension, ${pageName}: the runtime came from the extension (${s.source})`);
        await exercise(page, log, `extension, ${pageName}`);
        const visit = served.slice();
        check(!visit.some((r) => r.path.startsWith('runtime')), `extension, ${pageName}: the site sent no runtime`);
        console.log(`     the site sent ${kib(total(visit))}: ${visit.map((r) => r.path).join(', ')}`);
        await page.screenshot({ path: path.join(out, name + '.png') });
        log.done();
        await page.close();
    }
    await context.close();
    fs.rmSync(profile, { recursive: true, force: true });
}

server.close();
console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
