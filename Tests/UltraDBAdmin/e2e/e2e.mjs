#!/usr/bin/env node
// Tests/UltraDBAdmin/e2e/e2e.mjs
// Drives UltraDBAdmin's page in a headless Chromium for run_e2e.sh: sign in
// through the relay, open a table, run a query that never ends. The page is
// one canvas, so it is driven as a person would - clicks at the places its
// fixed layout puts things in a 1400 x 900 viewport, keys typed - and what it
// did is checked afterwards in the relay's audit log; here the page itself is
// checked for what a browser alone can see: its frames, its console, the
// ticket gone from its address. Screenshots of every step go to <out>.
//
//   node e2e.mjs <page url> <out folder>
//
// Environment: E2E_CHROME (a Chrome binary), E2E_CHANNEL (a Playwright
// channel, "chrome" on the GitHub runners), E2E_PLAYWRIGHT (the playwright
// package's path when it is not resolvable from here).
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
import fs from 'node:fs';

const [url, out] = process.argv.slice(2);
if (!url || !out) {
    console.error('usage: node e2e.mjs <page url> <out folder>');
    process.exit(2);
}
fs.mkdirSync(out, { recursive: true });
const playwright = await import(process.env.E2E_PLAYWRIGHT || 'playwright');
// A path to the CommonJS package comes back as its default export.
const chromium = playwright.chromium || playwright.default.chromium;

// Where the page puts things (1400 x 900, the fonts it carries). A change to
// the layout moves them: the screenshots of a failed run show where to.
const SIGN_IN = [680, 368];          // the relay dialog's Sign in
const SHOP_EXPANDER = [29, 117];     // the tree's only connection
const ORDERS = [116, 169];           // its schema main's only table
const SQL_TAB = [571, 123];
const EDITOR = [800, 250];
const RUN = [352, 168];

const launch = {};
if (process.env.E2E_CHROME) launch.executablePath = process.env.E2E_CHROME;
else if (process.env.E2E_CHANNEL) launch.channel = process.env.E2E_CHANNEL;
const browser = await chromium.launch(launch);
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
const lines = [];
const problems = [];
let frames = 0;
page.on('console', m => {
    const text = m.text();
    lines.push(`[${m.type()}] ${text}`);
    if (text.includes('first frame presented')) ++frames;
    if (/abort|Aborted|RuntimeError/.test(text)) problems.push(text);
});
page.on('pageerror', e => { lines.push(`[pageerror] ${e.message}`); problems.push(e.message); });

async function untilFrame(count, what) {
    for (let i = 0; i < 600 && frames < count; ++i) await page.waitForTimeout(200);
    if (frames < count) throw new Error(`${what}: no frame`);
    await page.waitForTimeout(2500);
}
let step = 0;
async function shot(name) {
    await page.screenshot({ path: `${out}/${String(++step).padStart(2, '0')}-${name}.png` });
}
async function click(at, wait = 1500) {
    await page.mouse.click(at[0], at[1]);
    await page.waitForTimeout(wait);
}

let failed = false;
try {
    await page.goto(url);
    await untilFrame(1, 'the page');
    await shot('relay-dialog');

    // Sign in: the page leaves for the relay, the provider and back.
    // The page comes back as a new document, which presents a first frame again.
    await click(SIGN_IN, 0);
    await untilFrame(2, 'the page after signing in');
    await page.waitForTimeout(3000);
    if (page.url().includes('relay_ticket')) problems.push(`the ticket stayed in the address: ${page.url()}`);
    await shot('signed-in');

    // A table through the relay.
    await click(SHOP_EXPANDER, 3000);
    await click(ORDERS, 4000);
    await shot('orders');

    // A query that never ends: the relay stops it after 2 s.
    await click(SQL_TAB, 2000);
    await click(EDITOR, 500);
    await page.keyboard.type('WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c) SELECT count(*) FROM c',
                             { delay: 5 });
    await page.keyboard.press('Escape');
    await click(RUN, 7000);
    await shot('stopped');
} catch (e) {
    failed = true;
    problems.push(String(e));
    await shot('failed').catch(() => {});
}
fs.writeFileSync(`${out}/console.log`, lines.join('\n') + '\n');
await browser.close();
for (const p of problems) console.log(`FAIL  ${p}`);
if (!failed && problems.length === 0) console.log('ok    the page signed in, browsed and ran its query with a clean console');
process.exit(failed || problems.length ? 1 : 0);
