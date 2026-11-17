// UltraSite core module: on every page of an UltraSite site, deferred.
// It only adds behaviour to HTML that already works without it:
// - prefetch: the page behind a same-origin link is fetched while the
//   pointer rests on it, on touch, or on keyboard focus, so the click
//   finds it in the cache. Browsers that run the page's speculation rules
//   (Chromium) prerender on their own; this covers the others.
// - theme: the toggle button (hidden without script) switches between the
//   dark and the light look and remembers the choice; the inline script in
//   <head> applies a remembered choice before the first paint.
// On a site with the swap navigation (<html data-us-nav="swap">) the swap
// module prefetches instead, into the cache it swaps pages from, and the
// toggle starts again on every page it brings in (the us:page event).
// Rule for every UltraSite module: a // comment stands on a line of its
// own, never after code (the build strips whole comment lines only).
const doc = document;
const root = doc.documentElement;
const seen = new Set([location.href.split('#')[0]]);
const connection = navigator.connection;
const saveData = !!(connection && (connection.saveData || /(^|-)2g$/.test(connection.effectiveType || '')));
const prerenders = typeof HTMLScriptElement.supports === 'function' && HTMLScriptElement.supports('speculationrules');
const swaps = root.dataset.usNav === 'swap';

// A page of the site: same origin, a path ending in "/" or ".html", not a
// download and not marked data-no-prefetch.
function isPage(link) {
  if (link.origin !== location.origin || link.hasAttribute('download') || link.hasAttribute('data-no-prefetch')) return false;
  if (link.target && link.target !== '_self') return false;
  const path = link.pathname;
  if (path.includes('/assets/')) return false;
  return path.endsWith('/') || path.endsWith('.html');
}

function prefetch(link) {
  if (saveData || prerenders || swaps || !isPage(link)) return;
  const url = link.href.split('#')[0];
  if (seen.has(url)) return;
  seen.add(url);
  const hint = doc.createElement('link');
  hint.rel = 'prefetch';
  hint.href = url;
  doc.head.appendChild(hint);
}

function onIntent(event) {
  const target = event.target;
  const link = target && target.closest ? target.closest('a[href]') : null;
  if (link) prefetch(link);
}

doc.addEventListener('pointerover', onIntent, { passive: true });
doc.addEventListener('touchstart', onIntent, { passive: true });
doc.addEventListener('focusin', onIntent);

const scheme = matchMedia('(prefers-color-scheme: dark)');
const current = () => root.dataset.theme || (scheme.matches ? 'dark' : 'light');
let toggle = null;
const show = () => {
  if (toggle) toggle.setAttribute('aria-pressed', String(current() === 'dark'));
};
scheme.addEventListener('change', show);

function startToggle() {
  toggle = doc.querySelector('[data-us-theme-toggle]');
  if (!toggle) return;
  toggle.hidden = false;
  show();
  toggle.addEventListener('click', () => {
    const next = current() === 'dark' ? 'light' : 'dark';
    root.dataset.theme = next;
    show();
    try {
      localStorage.setItem('us-theme', next);
    } catch (error) {
      // Storage refused (private mode): the choice lasts for this page.
    }
  });
}

startToggle();
doc.addEventListener('us:page', startToggle);
