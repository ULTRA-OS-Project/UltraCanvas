// UltraSite toc module: the doc layout's table of contents marks the
// heading being read - the last one scrolled past the top of the window.
// Without it the table of contents is plain links. It starts again on
// every page the swap navigation brings in (the us:page event).
let page = null;

function start() {
  if (page) page.abort();
  page = new AbortController();
  const toc = document.querySelector('[data-us~="toc"]');
  if (!toc) return;
  const links = new Map();
  for (const link of toc.querySelectorAll('a[href^="#"]')) {
    links.set(decodeURIComponent(link.hash.slice(1)), link);
  }
  const headings = [...links.keys()].map((id) => document.getElementById(id)).filter(Boolean);
  let current = null;
  let queued = false;
  const mark = () => {
    queued = false;
    let found = headings[0] || null;
    for (const heading of headings) {
      if (heading.getBoundingClientRect().top <= 120) found = heading;
      else break;
    }
    if (found === current) return;
    if (current) links.get(current.id).removeAttribute('aria-current');
    current = found;
    if (current) links.get(current.id).setAttribute('aria-current', 'true');
  };
  const queue = () => {
    if (queued) return;
    queued = true;
    requestAnimationFrame(mark);
  };
  addEventListener('scroll', queue, { passive: true, signal: page.signal });
  addEventListener('resize', queue, { passive: true, signal: page.signal });
  mark();
}

start();
document.addEventListener('us:page', start);
