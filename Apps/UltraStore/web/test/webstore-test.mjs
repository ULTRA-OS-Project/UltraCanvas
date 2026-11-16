#!/usr/bin/env node
// Apps/UltraStore/web/test/webstore-test.mjs
// The UltraStore web store end to end in Chromium: the fixture site
// (test/fixture, published by the admin backend) with the build's test
// module, loader and runtime laid over it (stage-store.mjs), served as a
// host would serve it - one folder, nothing rewritten on the way out.
//
//   0. Without a browser: the store's module (site/'s, the one a store is
//      published with) holds the store's keys and not the fixture's, the
//      test module holds both, and the store's module is within its budget.
//   1. The runtime from the site: Discover paints, the catalog verifies
//      (the fixture's key built into the test module), an app's page and a
//      category open by address, the back button returns, and the budgets
//      hold (the first screen's requests and pictures).
//   2. A tampered catalog is refused: the page says so and shows nothing
//      from it.
//   2b. The store's module in the test module's place: it runs, and refuses
//      the fixture's catalog - a key it was not built with signed it.
//   3. The UltraCanvas extension installed: the runtime comes from it and
//      the site sends only the pages, the loader, the module and the
//      catalog.
//
//   node webstore-test.mjs <build dir of Apps/UltraStore/web> <build dir of UltraCanvas/OS/WASM/runtime>
//
// Environment:
//   UWS_STORE       the store to serve, as stage-store.mjs left it: CI stages
//                   and uploads it first, so a run's artifact is the store
//                   that was tested. A folder that holds no store yet is
//                   staged into from the build; with none named, a temporary
//                   folder is, and removed afterwards
//   UWS_OUT         screenshots and logs (default: uws-test-out)
//   UWS_CHROME      a Chrome / Chromium binary (default: Playwright's Chromium)
//   UWS_PLAYWRIGHT  the playwright package when not resolvable from here
//   UWS_TIMEOUT     ms to wait for the app to start (default 120000)
//
// Version: 0.2.0 - serves one staged store (stage-store.mjs, UWS_STORE), not the fixture with the build's files mapped in by the server; the store's module apart from the test module: its keys, its budget, its refusal of the fixture's catalog
// Author: UltraCanvas Framework / ULTRA OS
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { stageStore } from './stage-store.mjs';

const [webBuild, runtimeBuild] = process.argv.slice(2);
if (!webBuild || !runtimeBuild) {
    console.error('usage: node webstore-test.mjs <web store build dir> <runtime build dir>');
    process.exit(2);
}
const here = path.dirname(fileURLToPath(import.meta.url));
const extension = path.resolve(runtimeBuild, 'ultracanvas-extension');
// The store's module: site/'s, which trusts the store's keys alone. The
// staged store carries the test module under the same name.
const storeModule = path.resolve(webBuild, 'site', 'ultrastore-web.wasm');
const out = process.env.UWS_OUT || 'uws-test-out';
const startTimeout = Number(process.env.UWS_TIMEOUT || 120000);
fs.mkdirSync(out, { recursive: true });
const { chromium } = await import(process.env.UWS_PLAYWRIGHT || 'playwright');

// ===== The store: the fixture with the build's files in place of the stand-ins =====
// Staged as publish lays it: the runtime under runtime/<its hash>/, the
// pages naming that folder and the built version, so the extension (the same
// build) is accepted.
let store = process.env.UWS_STORE ? path.resolve(process.env.UWS_STORE) : '';
let staged = false;
if (store && fs.existsSync(path.join(store, 'index.html'))) {
    console.log(`     serving the staged store ${store}`);
} else {
    staged = !store;
    if (!store) store = fs.mkdtempSync(path.join(os.tmpdir(), 'uws-store-'));
    const made = stageStore(webBuild, store);
    console.log(`     staged ${made.files.length} files in ${store}, the runtime under ${made.runtimeFolder}`);
}

// ===== A static server over it =====
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.wasm': 'application/wasm',
                '.data': 'application/octet-stream', '.json': 'application/json', '.sig': 'text/plain',
                '.png': 'image/png', '.webp': 'image/webp', '.avif': 'image/avif' };
let served = [];
let tamper = false;
let serveStoreModule = false;
function resolve(urlPath) {
    let file = path.join(store, decodeURIComponent(urlPath).replace(/^\/+/, ''));
    if (fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, 'index.html');
    return file;
}
const server = http.createServer((req, res) => {
    const file = resolve(new URL(req.url, 'http://x').pathname);
    if (!file.startsWith(store + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
        res.writeHead(404);
        res.end();
        return;
    }
    let body = fs.readFileSync(serveStoreModule && file === path.join(store, 'ultrastore-web.wasm') ? storeModule : file);
    if (tamper && file.endsWith('catalog.json')) body = Buffer.from(body.toString('utf8').replace('"sequence"', '"sequence_"').replace(/"price": "29.90"/, '"price": "0.90"'));
    served.push({ path: path.relative(store, file), bytes: body.length });
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
const kib = (bytes) => (bytes / 1024).toFixed(1) + ' KiB';
const total = (list) => list.reduce((sum, r) => sum + r.bytes, 0);

function watch(page, name) {
    const lines = [];
    page.on('console', (m) => lines.push(m.text()));
    page.on('pageerror', (e) => lines.push(`[pageerror] ${e.message}`));
    return {
        lines,
        async waitFor(text, ms = 20000) {
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

const launch = { headless: true };
if (process.env.UWS_CHROME) launch.executablePath = process.env.UWS_CHROME;

// ===== 0. What needs no browser: the keys in each module, the budget =====
{
    const module = fs.readFileSync(storeModule);
    const testModule = fs.readFileSync(path.join(store, 'ultrastore-web.wasm'));
    // The keys are string literals in the module (UltraStoreBuiltin.h), so
    // its bytes show which it was built with. The test module is the
    // control: were the fixture's key not readable in it, its absence from
    // the store's module would say nothing.
    const hexKey = (file) => (fs.readFileSync(file, 'utf8').split(/\r?\n/).find((line) => /^[0-9a-fA-F]{64}$/.test(line.trim())) || '').trim().toLowerCase();
    const fixtureKey = hexKey(path.join(here, 'fixture', 'trusted-key.hex'));
    const keysDir = path.resolve(here, '..', '..', 'keys');
    const storeKeys = fs.readdirSync(keysDir).filter((name) => name.endsWith('.pub')).sort().map((name) => hexKey(path.join(keysDir, name)));
    check(fixtureKey.length === 64 && storeKeys.length > 0 && storeKeys.every((key) => key.length === 64),
        `the fixture's key and the store's ${storeKeys.length} key(s) (Apps/UltraStore/keys/*.pub) read as 64 hex digits`);
    check(storeKeys.every((key) => module.includes(key)), 'the store\'s module holds the store\'s keys');
    check(testModule.includes(fixtureKey) && storeKeys.every((key) => testModule.includes(key)), 'the test module holds the fixture\'s key and the store\'s');
    check(!module.includes(fixtureKey), 'the store\'s module does not hold the fixture\'s key');
    check(!module.equals(testModule), 'the staged store carries the test module, not the store\'s');

    const brotli = (bytes) => zlib.brotliCompressSync(bytes, { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 11 } }).length;
    const size = brotli(module);
    console.log(`     ultrastore-web.wasm: ${kib(module.length)} raw, ${kib(size)} Brotli (the test module: ${kib(testModule.length)} raw, ${kib(brotli(testModule))} Brotli)`);
    check(size <= 150 * 1024, `the store's module is within its budget (${kib(size)} Brotli of 150 KiB)`);
    const shadow = fs.readFileSync(path.join(store, 'index.html'), 'utf8');
    check(/<h1|<h2/.test(shadow) && shadow.includes('ultrastore://app/'), 'the HTML shadow carries the store\'s texts and the deep links');
}

// ===== 1. The runtime from the site =====
{
    const browser = await chromium.launch(launch);
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const log = watch(page, '1-site');
    served = [];
    await page.goto(base);
    const s = await waitState(page);
    check(s.state === 'running', `site: the module runs (${s.status})`);
    check(s.source === 'site', `site: the runtime came from the site (${s.source})`);
    check(await log.waitFor('[ultrastore-web] catalog accepted', startTimeout), 'site: the catalog verified and was accepted');
    check(await log.waitFor('[ultrastore-web] page discover'), 'site: Discover is shown');
    await page.waitForTimeout(1500);
    await page.screenshot({ path: path.join(out, '1-discover.png') });
    const firstScreen = served.filter((r) => !r.path.startsWith('runtime') && !r.path.endsWith('.wasm') && !r.path.endsWith('ultracanvas-loader.js'));
    const pictures = firstScreen.filter((r) => /\.(png|webp|avif)$/.test(r.path));
    console.log(`     the first screen asked for ${firstScreen.length} files besides the runtime and the module, ${kib(total(pictures))} of pictures`);
    check(total(pictures) <= 300 * 1024, `site: Discover's pictures are within 300 KiB (${kib(total(pictures))})`);
    check(firstScreen.filter((r) => r.path.endsWith('catalog.json')).length === 1, 'site: one catalog.json');
    // The variants are named after the original's stem (UltraVideo-64.webp).
    check(served.some((r) => /^media\/.*-(64|128|256)\.webp$/.test(r.path)), 'site: the icons came as the catalog\'s WebP variants');

    // An app's page by its address, a category, and back.
    await page.goto(base + 'app/org.ultraos.ultravideo/');
    check(await log.waitFor('[ultrastore-web] page app/org.ultraos.ultravideo', startTimeout), 'site: the app page opens at its address');
    await page.waitForTimeout(1500);
    await page.screenshot({ path: path.join(out, '1-app.png') });
    const appPictures = served.filter((r) => /shot-.*\.(png|webp|avif)$/.test(r.path));
    check(appPictures.length > 0 && total(appPictures) <= 500 * 1024, `site: the app page's screenshots are within 500 KiB (${kib(total(appPictures))})`);
    await page.goto(base + 'category/video/');
    check(await log.waitFor('[ultrastore-web] page category/video', startTimeout), 'site: a category opens at its address');
    await page.waitForTimeout(800);
    await page.screenshot({ path: path.join(out, '1-category.png') });
    await page.goto(base + '?page=categories');
    check(await log.waitFor('[ultrastore-web] page categories', startTimeout), 'site: ?page= names a page');
    const sawBefore = log.lines.filter((l) => l.includes('page category/video')).length;
    await page.goBack();
    await page.waitForTimeout(1500);
    check(log.lines.filter((l) => l.includes('page category/video')).length > sawBefore, 'site: the back button returns to the category');
    log.done();
    await browser.close();
    console.log(`     the site sent ${kib(total(served))}:`);
    for (const r of served.slice(0, 40)) console.log(`       ${r.path.padEnd(60)} ${kib(r.bytes)}`);
}

// ===== 1b. A German visitor: the catalogues come from the site =====
{
    const browser = await chromium.launch(launch);
    const context = await browser.newContext({ locale: 'de-DE', viewport: { width: 1280, height: 900 } });
    const page = await context.newPage();
    const log = watch(page, '1b-german');
    served = [];
    await page.goto(base);
    const s = await waitState(page);
    check(s.state === 'running', `german: the module runs (${s.status})`);
    check(await log.waitFor('[ultrastore-web] locale de loaded', startTimeout), 'german: the German catalogues were fetched and loaded');
    check(await log.waitFor('[ultrastore-web] catalog accepted', startTimeout), 'german: the catalog is accepted');
    check(served.some((r) => r.path === 'locale/de/UltraStore.json'), 'german: the site sent the store\'s German set');
    await page.waitForTimeout(1200);
    await page.screenshot({ path: path.join(out, '1b-german.png') });
    log.done();
    await browser.close();
}

// ===== 2. A tampered catalog =====
{
    tamper = true;
    const browser = await chromium.launch(launch);
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const log = watch(page, '2-tampered');
    await page.goto(base);
    const s = await waitState(page);
    check(s.state === 'running', `tampered: the module runs (${s.status})`);
    check(await log.waitFor('[ultrastore-web] catalog refused', startTimeout), 'tampered: the catalog is refused');
    check(!log.lines.some((l) => l.includes('catalog accepted')), 'tampered: nothing from it is accepted');
    await page.waitForTimeout(800);
    await page.screenshot({ path: path.join(out, '2-tampered.png') });
    log.done();
    await browser.close();
    tamper = false;
}

// ===== 2b. The store's module: the fixture's key is not one of its own =====
// A new browser, so nothing an accepted catalog announced is remembered: the
// module has the keys it was built with and no others.
{
    serveStoreModule = true;
    const browser = await chromium.launch(launch);
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const log = watch(page, '2b-store-module');
    await page.goto(base);
    const s = await waitState(page);
    check(s.state === 'running', `store's module: it runs (${s.status})`);
    check(await log.waitFor('[ultrastore-web] catalog refused', startTimeout), 'store\'s module: the fixture\'s catalog is refused');
    check(!log.lines.some((l) => l.includes('catalog accepted')), 'store\'s module: nothing from it is accepted');
    await page.waitForTimeout(800);
    await page.screenshot({ path: path.join(out, '2b-store-module.png') });
    log.done();
    await browser.close();
    serveStoreModule = false;
}

// ===== 3. The extension installed =====
if (fs.existsSync(path.join(extension, 'manifest.json'))) {
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'uws-profile-'));
    const context = await chromium.launchPersistentContext(profile, {
        headless: true,
        channel: 'chromium',
        executablePath: process.env.UWS_CHROME,
        args: [`--disable-extensions-except=${extension}`, `--load-extension=${extension}`],
    });
    const page = await context.newPage();
    await page.setViewportSize({ width: 1280, height: 900 });
    const log = watch(page, '3-extension');
    served = [];
    await page.goto(base + 'app/org.ultraos.ultrapaint/');
    const s = await waitState(page);
    check(s.state === 'running', `extension: the module runs (${s.status})`);
    check(s.source === 'extension', `extension: the runtime came from the extension (${s.source})`);
    check(await log.waitFor('[ultrastore-web] page app/org.ultraos.ultrapaint', startTimeout), 'extension: the app page is shown');
    check(!served.some((r) => r.path.startsWith('runtime')), 'extension: the site sent no runtime');
    await page.waitForTimeout(1000);
    await page.screenshot({ path: path.join(out, '3-extension.png') });
    log.done();
    await context.close();
    console.log(`     with the extension the site sent ${kib(total(served))}`);
} else {
    console.log(`     (no extension at ${extension}: the extension path is not tested)`);
}

server.close();
if (staged) fs.rmSync(store, { recursive: true, force: true });
console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
