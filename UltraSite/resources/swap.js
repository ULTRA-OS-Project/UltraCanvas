// UltraSite swap module: the swap navigation, on a site that chose it
// (SiteConfig::navigation, <html data-us-nav="swap">). A click on a link to
// another page of the site fetches that page and puts its <body> and its
// <head> lines in place of this page's, instead of loading it: the scripts
// already running stay, and every module starts again on the new page (the
// us:page event). Pages are fetched while the pointer rests on a link, so a
// click usually finds the page in hand.
// Browsers that prerender through the speculation rules (Chromium) load
// pages as before: a prerendered page is shown at once, faster than a swap.
// After a swap the new page's <h1> has the focus and the new title is
// announced to screen readers; the address, Back and Forward, and the
// scroll position of every visited page are kept.
// Anything it cannot swap is loaded the ordinary way: an error, a page that
// is not HTML, another stylesheet or policy, or a page that loads a module
// not written to start again (a script without data-us-swap).
const doc = document;
const root = doc.documentElement;
const prerenders = typeof HTMLScriptElement.supports === 'function' && HTMLScriptElement.supports('speculationrules');
const connection = navigator.connection;
const saveData = !!(connection && (connection.saveData || /(^|-)2g$/.test(connection.effectiveType || '')));
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
// Fetched pages, by path and query, kept for five minutes.
const pages = new Map();
const keep = 5 * 60 * 1000;
let shown = '';
let turn = 0;

function key(url) {
  const address = new URL(url, location.href);
  return address.pathname + address.search;
}

// A page of the site, as core.js sees one.
function isPage(link) {
  if (link.origin !== location.origin || link.hasAttribute('download') || link.hasAttribute('data-no-prefetch')) return false;
  if (link.target && link.target !== '_self') return false;
  const path = link.pathname;
  if (path.includes('/assets/')) return false;
  return path.endsWith('/') || path.endsWith('.html');
}

function fetchPage(url) {
  const name = key(url);
  const held = pages.get(name);
  if (held && Date.now() - held.time < keep) return held.page;
  if (pages.size >= 30) pages.delete(pages.keys().next().value);
  const page = fetch(name, { credentials: 'same-origin' }).then(async (response) => {
    const type = response.headers.get('content-type') || '';
    if (!response.ok || !type.includes('text/html')) throw new Error('not a page');
    return { url: response.url, html: await response.text() };
  });
  pages.set(name, { time: Date.now(), page });
  page.catch(() => pages.delete(name));
  return page;
}

function onIntent(event) {
  const target = event.target;
  const link = target && target.closest ? target.closest('a[href]') : null;
  if (!saveData && link && isPage(link) && key(link.href) !== shown) fetchPage(link.href).catch(() => {});
}

const modules = (page) => [...page.querySelectorAll('script[type="module"][src]')];
const sheets = (page) => [...page.querySelectorAll('link[rel="stylesheet"]')].map((link) => link.getAttribute('href')).join(' ');
const policy = (page) => {
  const meta = page.querySelector('meta[http-equiv="Content-Security-Policy" i]');
  return meta ? meta.getAttribute('content') : '';
};

// Whether the next page can replace this one without a load.
function swappable(next) {
  if (next.documentElement.dataset.usNav !== 'swap' || !next.body) return false;
  if (sheets(next) !== sheets(doc) || policy(next) !== policy(doc)) return false;
  return modules(doc).concat(modules(next)).every((script) => script.hasAttribute('data-us-swap'));
}

// Head lines that stay as the first page set them: what runs, styles and
// sets the document up. Every other line - the title, the description, the
// canonical address, the social card, structured data - is the page's own.
const fixed = (node) =>
  node.matches('script:not([type="application/ld+json"]), link[rel="stylesheet"], meta[charset], meta[http-equiv], meta[name="viewport"]');

function render(next) {
  const own = new Map();
  for (const node of doc.head.children) {
    if (!fixed(node)) own.set(node.outerHTML, node);
  }
  const loaded = new Set(modules(doc).map((script) => script.getAttribute('src')));
  for (const node of [...next.head.children]) {
    if (fixed(node)) continue;
    if (own.has(node.outerHTML)) own.delete(node.outerHTML);
    else doc.head.append(doc.adoptNode(node));
  }
  for (const node of own.values()) node.remove();
  for (const name of ['lang', 'dir']) {
    const value = next.documentElement.getAttribute(name);
    if (value === null) root.removeAttribute(name);
    else root.setAttribute(name, value);
  }
  doc.body.replaceWith(doc.adoptNode(next.body));
  // The modules already here start again; the new page's others load and
  // start on their own.
  doc.dispatchEvent(new CustomEvent('us:page'));
  for (const script of modules(next)) {
    const src = script.getAttribute('src');
    if (loaded.has(src)) continue;
    const added = doc.createElement('script');
    added.type = 'module';
    added.src = src;
    added.setAttribute('data-us-swap', '');
    doc.head.append(added);
  }
}

function target(hash) {
  if (!hash) return null;
  const id = decodeURIComponent(hash.slice(1));
  return doc.getElementById(id) || doc.getElementsByName(id)[0] || null;
}

function place(hash, top) {
  const anchor = typeof top === 'number' ? null : target(hash);
  if (anchor) anchor.scrollIntoView();
  else scrollTo(0, top || 0);
}

// The focus to the new page's heading, and its title said aloud.
function arrive() {
  const heading = doc.querySelector('main h1') || doc.querySelector('h1') || doc.querySelector('main');
  if (heading) {
    if (!heading.hasAttribute('tabindex')) heading.setAttribute('tabindex', '-1');
    heading.focus({ preventScroll: true });
  }
  const live = doc.createElement('div');
  live.className = 'us-visually-hidden';
  live.setAttribute('aria-live', 'polite');
  doc.body.append(live);
  setTimeout(() => { live.textContent = doc.title; }, 100);
}

// To `url`: pushed as a new entry, or (`top` given) the entry Back or
// Forward reached, scrolled to where it was left.
async function go(url, top) {
  const ticket = ++turn;
  const push = typeof top !== 'number';
  let page;
  try {
    page = await fetchPage(url);
  } catch (error) {
    if (ticket === turn) push ? location.assign(url) : location.reload();
    return;
  }
  if (ticket !== turn) return;
  const next = new DOMParser().parseFromString(page.html, 'text/html');
  if (!swappable(next)) {
    push ? location.assign(url) : location.reload();
    return;
  }
  const address = new URL(page.url);
  address.hash = new URL(url, location.href).hash;
  if (push) {
    history.replaceState({ us: true, top: scrollY }, '');
    history.pushState({ us: true, top: 0 }, '', address.href);
  } else if (address.href !== location.href) {
    history.replaceState(history.state, '', address.href);
  }
  shown = key(address.href);
  const swap = () => {
    render(next);
    place(address.hash, push ? undefined : top);
    arrive();
  };
  if (doc.startViewTransition && !reducedMotion.matches) doc.startViewTransition(swap);
  else swap();
}

function onClick(event) {
  if (event.defaultPrevented || event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
  const link = event.target && event.target.closest ? event.target.closest('a[href]') : null;
  if (!link || !isPage(link)) return;
  // A link within this page: the browser scrolls to it.
  if (key(link.href) === shown && link.hash) return;
  event.preventDefault();
  go(link.href);
}

function onPop(event) {
  if (key(location.href) === shown) {
    // Back or Forward within this page, between its anchors.
    place(location.hash, location.hash ? undefined : (event.state && event.state.top) || 0);
    return;
  }
  go(location.href, (event.state && event.state.us && event.state.top) || 0);
}

if (root.dataset.usNav === 'swap' && !prerenders && window.DOMParser && history.pushState) {
  shown = key(location.href);
  history.scrollRestoration = 'manual';
  // Back to this page from another site, or a reload: where it was left.
  const entry = performance.getEntriesByType ? performance.getEntriesByType('navigation')[0] : null;
  const state = history.state;
  if (entry && entry.type !== 'navigate' && state && state.us && state.top && !location.hash) scrollTo(0, state.top);
  history.replaceState({ us: true, top: scrollY }, '');
  addEventListener('pagehide', () => history.replaceState({ us: true, top: scrollY }, ''));
  doc.addEventListener('click', onClick);
  doc.addEventListener('pointerover', onIntent, { passive: true });
  doc.addEventListener('touchstart', onIntent, { passive: true });
  doc.addEventListener('focusin', onIntent);
  doc.addEventListener('us:navigate', (event) => {
    event.preventDefault();
    go(event.detail);
  });
  addEventListener('popstate', onPop);
}
