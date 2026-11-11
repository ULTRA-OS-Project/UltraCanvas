// Apps/UltraWeb/browser/extension/ultraweb-announce.js
// The extension's whole content script. It runs before the page's own
// scripts and only says where the installed runtime is; the page's loader
// (ultraweb-loader.js) loads it from there into the page and runs the app.
// The extension never fetches or runs a site's code itself.
document.documentElement.dataset.ultrawebRuntime = chrome.runtime.getURL("runtime/");
