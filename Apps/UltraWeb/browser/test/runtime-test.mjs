#!/usr/bin/env node
// Apps/UltraWeb/browser/test/runtime-test.mjs
// End-to-end test of the UltraWeb runtime in Chromium
// (Docs/UltraWeb/UltraWebInTheBrowser.md): the sample app hello.wasm on the
// sample site, three ways.
//
//   1. No extension, site with the runtime (index.html): the runtime comes
//      from the site. Clicks, the checkbox, the timer, the fetch, and the
//      stored count after a reload all work.
//   2. No extension, site without the runtime (extension-only.html): the
//      page says the extension is needed.
//   3. The extension installed: both pages take the runtime from it, and the
//      site serves only the page, the loader and the app.
//
//   node runtime-test.mjs <build dir of Apps/UltraWeb/browser>
//
// Environment:
//   UWR_OUT         screenshots and logs (default: uwr-test-out)
//   UWR_CHROME      a Chrome / Chromium binary (default: Playwright's Chromium)
//   UWR_PLAYWRIGHT  the playwright package when not resolvable from here
//   UWR_TIMEOUT     ms to wait for an app to start (default 120000)
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
const extension = path.resolve(buildDir, 'ultraweb-extension');
const out = process.env.UWR_OUT || 'uwr-test-out';
const startTimeout = Number(process.env.UWR_TIMEOUT || 120000);
fs.mkdirSync(out, { recursive: true });
const { chromium } = await import(process.env.UWR_PLAYWRIGHT || 'playwright');

// ===== A static server that counts what it sends =====
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
                '.data': 'application/octet-stream', '.txt': 'text/plain' };
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
const failing = /Uncaught|Aborted\(|RuntimeError|unreachable executed|memory access out of bounds/;

function watchConsole(page, name) {
    const lines = [];
    page.on('console', (m) => lines.push(`[${m.type()}] ${m.text()}`));
    page.on('pageerror', (e) => lines.push(`[pageerror] ${e.message}`));
    return () => {
        fs.writeFileSync(path.join(out, name + '.log'), lines.join('\n') + '\n');
        const bad = lines.filter((l) => failing.test(l));
        check(bad.length === 0, `${name}: no errors in the console` + (bad.length ? ` (${bad[0]})` : ''));
    };
}

async function waitState(page, name) {
    await page.waitForFunction(() => document.documentElement.dataset.ultrawebState, null, { timeout: startTimeout });
    return page.evaluate(() => ({
        state: document.documentElement.dataset.ultrawebState,
        source: document.documentElement.dataset.ultrawebRuntimeSource,
        status: (document.getElementById('ultraweb-status') || {}).textContent,
    }));
}

// Where the app's element showing `text` is on the page, or null.
async function locate(page, text) {
    return page.evaluate((text) => {
        const out = Module._malloc(16);
        const name = Module.stringToNewUTF8(text);
        const found = Module._uw_runtime_locate(name, out);
        const box = Array.from(new Float32Array(Module.HEAPU8.buffer, out, 4));
        Module._free(out);
        Module._free(name);
        if (!found) return null;
        const canvas = document.querySelector('canvas').getBoundingClientRect();
        return { x: canvas.left + box[0], y: canvas.top + box[1], width: box[2], height: box[3] };
    }, text);
}

async function waitText(page, text, ms = 15000) {
    const until = Date.now() + ms;
    while (Date.now() < until) {
        if (await locate(page, text)) return true;
        await page.waitForTimeout(100);
    }
    return false;
}

async function click(page, text) {
    const box = await locate(page, text);
    if (!box) return false;
    await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    return true;
}

const kib = (bytes) => (bytes / 1024).toFixed(1) + ' KiB';
const total = (list) => list.reduce((sum, r) => sum + r.bytes, 0);
const launch = { headless: true };
if (process.env.UWR_CHROME) launch.executablePath = process.env.UWR_CHROME;

// ===== 1. No extension, the runtime from the site =====
{
    const browser = await chromium.launch(launch);
    const page = await browser.newPage({ viewport: { width: 1000, height: 700 } });
    const done = watchConsole(page, '1-site');
    served = [];
    await page.goto(base + 'index.html');
    const s = await waitState(page);
    check(s.state === 'running', `site: the app runs (${s.status})`);
    check(s.source === 'site', `site: the runtime came from the site (${s.source})`);
    const count = await page.evaluate(() => Module._uw_runtime_element_count());
    check(count > 10, `site: the app built ${count} elements`);
    check(await waitText(page, 'Clicked 0 times (kept across visits)'), 'site: the count starts at 0');
    check(await click(page, 'Count'), 'site: the Count button is there');
    check(await waitText(page, 'Clicked 1 time (kept across visits)'), 'site: a click reaches the app');
    check(await click(page, 'Greet'), 'site: the Greet button is there');
    check(await waitText(page, 'Hello, world.'), 'site: Greet answers');
    check(await click(page, 'Shout'), 'site: the Shout checkbox is there');
    check(await waitText(page, 'Hello, world!!!'), 'site: the checkbox toggles');
    check(await waitText(page, 'hello.txt says: Hello from the site, fetched by the app through uc_fetch.'),
          'site: uc_fetch reads hello.txt');
    check(await waitText(page, 'Running for 2 s', 10000), 'site: the timer ticks');
    const firstVisit = served.slice();
    check(firstVisit.some((r) => r.path.startsWith('runtime')), 'site: the site sent the runtime');
    await page.screenshot({ path: path.join(out, '1-site.png') });
    // Storage reaches IndexedDB every 2 s; a reload then finds the count.
    await page.waitForTimeout(3000);
    await page.reload();
    const again = await waitState(page);
    check(again.state === 'running', 'site: the app runs again after a reload');
    check(await waitText(page, 'Clicked 1 time (kept across visits)'), 'site: uc_storage kept the count across the reload');
    done();
    await browser.close();
    console.log(`     site sent ${kib(total(firstVisit))} on the first visit:`);
    for (const r of firstVisit) console.log(`       ${r.path.padEnd(32)} ${kib(r.bytes)}`);
}

// ===== 2. No extension, no runtime on the site =====
{
    const browser = await chromium.launch(launch);
    const page = await browser.newPage();
    const done = watchConsole(page, '2-no-extension');
    await page.goto(base + 'extension-only.html');
    const s = await waitState(page);
    check(s.state === 'failed' && /extension/.test(s.status), `no extension: the page asks for it (${s.status})`);
    done();
    await browser.close();
}

// ===== 3. The extension installed =====
{
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'uwr-profile-'));
    const context = await chromium.launchPersistentContext(profile, {
        ...launch,
        viewport: { width: 1000, height: 700 },
        args: [`--disable-extensions-except=${extension}`, `--load-extension=${extension}`],
    });
    for (const pageName of ['extension-only.html', 'index.html']) {
        const page = await context.newPage();
        const done = watchConsole(page, '3-extension-' + pageName.replace('.html', ''));
        served = [];
        await page.goto(base + pageName);
        const s = await waitState(page);
        check(s.state === 'running', `extension, ${pageName}: the app runs (${s.status})`);
        check(s.source === 'extension', `extension, ${pageName}: the runtime came from the extension (${s.source})`);
        check(await click(page, 'Count'), `extension, ${pageName}: the Count button is there`);
        check(await waitText(page, 'Clicked 1 time (kept across visits)') || await waitText(page, 'Clicked 2 times (kept across visits)'),
              `extension, ${pageName}: a click reaches the app`);
        check(await waitText(page, 'hello.txt says: Hello from the site, fetched by the app through uc_fetch.'),
              `extension, ${pageName}: uc_fetch reads hello.txt`);
        await page.waitForTimeout(500);
        const visit = served.slice();
        check(!visit.some((r) => r.path.startsWith('runtime')), `extension, ${pageName}: the site sent no runtime`);
        console.log(`     site sent ${kib(total(visit))}: ${visit.map((r) => r.path).join(', ')}`);
        await page.screenshot({ path: path.join(out, '3-extension-' + pageName.replace('.html', '') + '.png') });
        done();
        await page.close();
    }
    await context.close();
    fs.rmSync(profile, { recursive: true, force: true });
}

server.close();
console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
