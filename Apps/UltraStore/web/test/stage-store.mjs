#!/usr/bin/env node
// Apps/UltraStore/web/test/stage-store.mjs
// Stages a whole web store in one folder: the fixture (test/fixture, a store
// published by the admin backend without the module, the loader and the
// runtime) with a build laid over it the way the admin backend's publish
// lays a site/ (Apps/UltraStoreAdmin/core/WebSite.cpp) - the module, the
// loader, the mark and the catalogues beside catalog.json, the runtime under
// runtime/<first 12 hex digits of its .wasm's SHA-256>/, and every page
// naming that folder and the build's runtime version. What it leaves is what
// a static host serves: the browser test (webstore-test.mjs) opens it, and
// wasm-build.yml uploads it as preview/ in the ultrastore-web-<variant>
// artifact, so the compiled store of a run can be downloaded and looked at.
//
//   node stage-store.mjs <build dir of Apps/UltraStore/web> <folder to make>
//
// The module is not site/'s. The fixture's catalog is signed with the
// fixture's key, which the store's module does not trust and must not: the
// one laid here is the build's test module, ultrastore-web-test.wasm
// (-DULTRASTORE_WEB_TEST_KEYS, Apps/UltraStore/web/CMakeLists.txt), under
// the name the pages ask for. Everything else is site/'s. So this is a test
// store, never a release; the folder to publish a store with is site/.
// The folder to make must not exist yet, or be empty.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const moduleFile = 'ultrastore-web.wasm';
const testModuleFile = 'ultrastore-web-test.wasm';
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

// Lays the build (<webBuild>/site, with <webBuild>/ultrastore-web-test.wasm
// as its module) over the fixture into storeDir and returns what it made:
// { runtimeFolder, runtimeVersion, pages, files }. Throws, naming the file,
// when the build lacks one a store cannot run without, so a store that would
// not start is never staged - or uploaded.
export function stageStore(webBuild, storeDir, fixture = path.join(here, 'fixture')) {
    const site = path.resolve(webBuild, 'site');
    const testModule = path.resolve(webBuild, testModuleFile);
    const filled = (file) => fs.existsSync(file) && fs.statSync(file).size > 0;
    const need = [moduleFile, loaderFile, 'site.json', ...runtimeFiles.map((name) => path.join('runtime', name))];
    const missing = need.filter((name) => !filled(path.join(site, name)));
    if (missing.length) throw new Error(`${site} lacks ${missing.join(', ')}: build the target ultrastore-web-site first`);
    if (!filled(testModule)) {
        throw new Error(`${testModule} is not built: the fixture's catalog is signed with the fixture's key, which only the test module trusts - `
            + `configure with -DULTRASTORE_WEB_TEST_KEYS=$(cat Apps/UltraStore/web/test/fixture/trusted-key.hex) and build ultrastore-web-test`);
    }
    if (fs.existsSync(storeDir) && fs.readdirSync(storeDir).length) throw new Error(`${storeDir} is not empty: name a new folder, or remove it first`);
    const runtimeVersion = JSON.parse(fs.readFileSync(path.join(site, 'site.json'), 'utf8')).runtime_version || '';
    if (!runtimeVersion) throw new Error(`${path.join(site, 'site.json')} names no runtime_version`);

    // The fixture: the catalog and its signature, the pages, the pictures, the packages.
    fs.mkdirSync(storeDir, { recursive: true });
    copyTree(fixture, storeDir, notPublished);

    // The build: the test module under the module's name, then the loader,
    // the mark and the catalogues of site/.
    fs.copyFileSync(testModule, path.join(storeDir, moduleFile));
    for (const name of [loaderFile, markFile]) {
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
        [moduleFile, 'the web store itself, a WebAssembly side module (the test build)'],
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

Not a release. The catalog is the fixture's, and ${moduleFile} here is the
build's test module (${testModuleFile}), which trusts the fixture's signing
key beside the store's own. A real store is published by the admin backend
with web.site_dir naming a build's site/ folder, whose module trusts the
store's keys alone (Apps/UltraStore/web/README.md).
`);
    return { runtimeFolder, runtimeVersion, pages, files: filesUnder(storeDir) };
}

// Run as a program (not imported by the test). Both sides as real paths: Node
// names this file by its real path, the command line may reach it through a link.
if (process.argv[1] && fs.existsSync(process.argv[1]) && fs.realpathSync(process.argv[1]) === fs.realpathSync(fileURLToPath(import.meta.url))) {
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
