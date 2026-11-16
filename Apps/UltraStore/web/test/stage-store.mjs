#!/usr/bin/env node
// Apps/UltraStore/web/test/stage-store.mjs
// Stages a whole web store in one folder: the fixture (test/fixture, a store
// published by the admin backend without the module, the loader and the
// runtime) with a build's site/ laid over it the way the admin backend's
// publish lays it (Apps/UltraStoreAdmin/core/WebSite.cpp) - the module, the
// loader, the mark and the catalogues beside catalog.json, the runtime under
// runtime/<first 12 hex digits of its .wasm's SHA-256>/, and every page
// naming that folder and the build's runtime version. What it leaves is what
// a static host serves: the browser test (webstore-test.mjs) opens it, and
// wasm-build.yml uploads it as the ultrastore-web-<variant> artifact, so the
// compiled store of a run can be downloaded and looked at.
//
//   node stage-store.mjs <build dir of Apps/UltraStore/web> <folder to make>
//
// The folder must not exist yet, or be empty. A test store, never a release:
// its catalog is the fixture's, and the module CI builds for it also trusts
// the fixture's key (ULTRASTORE_WEB_TEST_KEYS).
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const moduleFile = 'ultrastore-web.wasm';
const loaderFile = 'ultracanvas-loader.js';
const markFile = 'ultrastore.png';
const runtimeFiles = ['ultracanvas-runtime.js', 'ultracanvas-runtime.wasm', 'ultracanvas-runtime.data'];
// In the fixture for the build (the key the test module is built to trust),
// not a file a store publishes.
const notPublished = ['trusted-key.hex'];

// Every file under a folder, as paths relative to it, sorted.
function filesUnder(folder, relative = '') {
    const found = [];
    for (const entry of fs.readdirSync(path.join(folder, relative), { withFileTypes: true })) {
        const name = path.join(relative, entry.name);
        if (entry.isDirectory()) found.push(...filesUnder(folder, name));
        else if (entry.isFile()) found.push(name);
    }
    return found.sort();
}

function copyTree(from, to, skip = []) {
    for (const name of filesUnder(from)) {
        if (skip.includes(name)) continue;
        fs.mkdirSync(path.dirname(path.join(to, name)), { recursive: true });
        fs.copyFileSync(path.join(from, name), path.join(to, name));
    }
}

// Lays <webBuild>/site over the fixture into storeDir and returns what it
// made: { runtimeFolder, runtimeVersion, pages, files }. Throws, naming the
// file, when the build's site/ lacks one a store cannot run without, so a
// store that would not start is never staged - or uploaded.
export function stageStore(webBuild, storeDir, fixture = path.join(here, 'fixture')) {
    const site = path.resolve(webBuild, 'site');
    const need = [moduleFile, loaderFile, 'site.json', ...runtimeFiles.map((name) => path.join('runtime', name))];
    const missing = need.filter((name) => !fs.existsSync(path.join(site, name)) || fs.statSync(path.join(site, name)).size === 0);
    if (missing.length) throw new Error(`${site} lacks ${missing.join(', ')}: build the target ultrastore-web-site first`);
    if (fs.existsSync(storeDir) && fs.readdirSync(storeDir).length) throw new Error(`${storeDir} is not empty: name a new folder, or remove it first`);
    const runtimeVersion = JSON.parse(fs.readFileSync(path.join(site, 'site.json'), 'utf8')).runtime_version || '';
    if (!runtimeVersion) throw new Error(`${path.join(site, 'site.json')} names no runtime_version`);

    // The fixture: the catalog and its signature, the pages, the pictures, the packages.
    fs.mkdirSync(storeDir, { recursive: true });
    copyTree(fixture, storeDir, notPublished);

    // The build: the module, the loader, the mark, the catalogues.
    for (const name of [moduleFile, loaderFile, markFile]) {
        if (fs.existsSync(path.join(site, name))) fs.copyFileSync(path.join(site, name), path.join(storeDir, name));
    }
    if (fs.existsSync(path.join(site, 'locale'))) copyTree(path.join(site, 'locale'), path.join(storeDir, 'locale'));

    // The runtime, under its hash, as publish names it.
    const hash = crypto.createHash('sha256').update(fs.readFileSync(path.join(site, 'runtime', 'ultracanvas-runtime.wasm'))).digest('hex');
    const runtimeFolder = `runtime/${hash.slice(0, 12)}/`;
    fs.mkdirSync(path.join(storeDir, runtimeFolder), { recursive: true });
    for (const name of runtimeFiles) fs.copyFileSync(path.join(site, 'runtime', name), path.join(storeDir, runtimeFolder, name));

    // The pages name the fixture's stand-in runtime and its version: give
    // them this build's. A page whose loader names neither is a fixture this
    // script no longer understands, not one to pass over.
    let pages = 0;
    for (const name of filesUnder(storeDir).filter((file) => file.endsWith('.html'))) {
        const file = path.join(storeDir, name);
        const before = fs.readFileSync(file, 'utf8');
        if (!before.includes(loaderFile)) continue;
        let folders = 0;
        let versions = 0;
        const after = before
            .replace(/(data-runtime=")((?:\.\.\/)*)runtime\/[^"/]*\/"/g, (all, attribute, root) => { ++folders; return `${attribute}${root}${runtimeFolder}"`; })
            .replace(/data-runtime-version="[^"]*"/g, () => { ++versions; return `data-runtime-version="${runtimeVersion}"`; });
        if (folders !== 1 || versions !== 1) throw new Error(`${name}: its loader should name one data-runtime folder and one data-runtime-version, found ${folders} and ${versions}`);
        fs.writeFileSync(file, after);
        ++pages;
    }
    if (!pages) throw new Error(`${fixture} has no page with the loader (${loaderFile})`);

    const holds = [
        [moduleFile, 'the web store itself, a WebAssembly side module'],
        [loaderFile, 'the pages\' loader'],
        [runtimeFolder, 'the UltraCanvas runtime the module runs on (.js, .wasm, .data)'],
        ['locale/<language>/', 'the translation catalogues'],
        ['catalog.json, catalog.json.sig', 'the signed catalog'],
        ['index.html, categories/, ...', 'the pages, a folder per address (category/<id>/, app/<id>/)'],
        ['media/, packages/, restricted/', 'the pictures and the fixture\'s stand-in packages'],
    ].map(([what, is]) => `  ${what.padEnd(32)}${is}`).join('\n');
    const commit = process.env.GITHUB_SHA ? `, commit ${process.env.GITHUB_SHA}` : '';
    fs.writeFileSync(path.join(storeDir, 'README.txt'), `The UltraStore web store, as a static host serves it
====================================================

A test store: the browser test's fixture (Apps/UltraStore/web/test/fixture, a
small store published by the admin backend) with one build's files laid over
it the way the admin backend's publish lays them. UltraCanvas ${runtimeVersion}${commit}.

${holds}

To look at it, serve this folder and open it in a browser. Any static host
does, with no special headers:

  python3 -m http.server --bind 127.0.0.1 8080

then http://127.0.0.1:8080/ (Discover), http://127.0.0.1:8080/categories/ or
http://127.0.0.1:8080/app/org.ultraos.ultravideo/. Opened from the disk
(file://) a page stays its HTML shadow: a browser fetches no module from
there.

Not a release. The catalog is the fixture's, and the module of a CI build
trusts the fixture's signing key beside the store's own
(-DULTRASTORE_WEB_TEST_KEYS). A real store is published by the admin backend
with web.site_dir naming a build's site/ folder (Apps/UltraStore/web/README.md).
`);
    return { runtimeFolder, runtimeVersion, pages, files: filesUnder(storeDir) };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const [webBuild, storeDir] = process.argv.slice(2);
    if (!webBuild || !storeDir) {
        console.error('usage: node stage-store.mjs <web store build dir> <folder to make>');
        process.exit(2);
    }
    try {
        const staged = stageStore(webBuild, storeDir);
        const bytes = staged.files.reduce((sum, name) => sum + fs.statSync(path.join(storeDir, name)).size, 0);
        console.log(`${storeDir}: ${staged.files.length} files, ${(bytes / 1048576).toFixed(1)} MiB, ${staged.pages} pages on ${staged.runtimeFolder} (UltraCanvas ${staged.runtimeVersion})`);
    } catch (e) {
        console.error(`stage-store: ${e.message}`);
        process.exit(1);
    }
}
