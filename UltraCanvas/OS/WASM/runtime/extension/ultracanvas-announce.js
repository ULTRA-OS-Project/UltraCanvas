// OS/WASM/runtime/extension/ultracanvas-announce.js
// The extension's whole content script. It runs before the page's own
// scripts and only says where the installed UltraCanvas runtime is, with its
// version; the page's loader (ultracanvas-loader.js) loads it from there and
// starts its app. The extension never fetches or runs a site's code itself.
document.documentElement.dataset.ultracanvasRuntime = chrome.runtime.getURL("runtime/");
document.documentElement.dataset.ultracanvasRuntimeVersion = chrome.runtime.getManifest().version;
