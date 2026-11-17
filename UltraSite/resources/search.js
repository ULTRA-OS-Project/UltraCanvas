// UltraSite search module: the navbar's search box (and the search page's)
// searches the site's index for the page's language, fetched on the first
// focus. Results are a list under the box (arrow keys, Enter, Escape) or,
// on the search page, the page's own list. Without it the box submits to
// /search/, which lists every page. "/" puts the cursor into the box.
const forms = [...document.querySelectorAll('form[data-us~="search"]')];
const loads = new Map();
let shown = 0;

// The index is a page to an entry: [title, address, description,
// [[anchor, heading, text], ...]]. Searched as records {t, u, h, x}.
function records(pages) {
  const out = [];
  for (const [title, path, description, sections] of pages) {
    let text = description;
    for (const [anchor, heading, part] of sections) {
      if (!anchor) {
        text = text || part;
        continue;
      }
      out.push({ t: title, h: heading, u: path + '#' + anchor, x: part || '' });
    }
    out.push({ t: title, h: '', u: path, x: text || '' });
  }
  return out;
}

function load(url) {
  if (!loads.has(url)) {
    loads.set(url, fetch(url).then((response) => response.json()).then(records).catch(() => []));
  }
  return loads.get(url);
}

function find(index, query) {
  const words = query.toLowerCase().split(/\s+/).filter(Boolean);
  if (!words.length) return [];
  const hits = [];
  for (const record of index) {
    const title = record.t.toLowerCase();
    const heading = (record.h || '').toLowerCase();
    const text = (record.x || '').toLowerCase();
    let score = 0;
    let all = true;
    for (const word of words) {
      const points = (title.includes(word) ? 10 : 0) + (heading.includes(word) ? 6 : 0) + (text.includes(word) ? 1 : 0);
      if (!points) {
        all = false;
        break;
      }
      score += points + (title.startsWith(word) ? 4 : 0) + (heading.startsWith(word) ? 2 : 0);
    }
    if (all) hits.push([score + (record.h ? 0 : 8) + (title === query.toLowerCase().trim() ? 20 : 0), record]);
  }
  hits.sort((a, b) => b[0] - a[0]);
  // At most three hits from one page, so one long page cannot fill the list.
  const perPage = new Map();
  const out = [];
  for (const [, record] of hits) {
    const page = record.u.split('#')[0];
    const seen = perPage.get(page) || 0;
    if (seen >= 3) continue;
    perPage.set(page, seen + 1);
    out.push(record);
  }
  return out;
}

function excerpt(text, query) {
  if (!text) return '';
  const word = query.toLowerCase().split(/\s+/).filter(Boolean)[0] || '';
  const at = Math.max(0, text.toLowerCase().indexOf(word) - 40);
  return (at > 0 ? '…' : '') + text.slice(at, at + 140) + (text.length > at + 140 ? '…' : '');
}

function item(record, query, id) {
  const li = document.createElement('li');
  li.setAttribute('role', 'option');
  li.id = id;
  const link = document.createElement('a');
  link.href = record.u;
  // A section shows its heading first and its page under it.
  const title = document.createElement('strong');
  title.textContent = record.h || record.t;
  link.append(title);
  if (record.h) {
    const page = document.createElement('span');
    page.textContent = record.t;
    link.append(page);
  }
  const text = excerpt(record.x, query);
  if (text) {
    const small = document.createElement('small');
    small.textContent = text;
    link.append(small);
  }
  li.append(link);
  return li;
}

for (const form of forms) {
  const input = form.querySelector('input[type="search"]');
  if (!input) continue;
  const page = form.dataset.results ? document.getElementById(form.dataset.results) : null;
  const list = page ? null : document.createElement('ul');
  const listId = 'us-search-list-' + (++shown);
  let active = -1;
  let records = [];
  if (list) {
    list.className = 'us-search-results';
    list.id = listId;
    list.setAttribute('role', 'listbox');
    list.hidden = true;
    form.append(list);
    input.setAttribute('role', 'combobox');
    input.setAttribute('aria-controls', listId);
    input.setAttribute('aria-expanded', 'false');
    input.setAttribute('aria-autocomplete', 'list');
  }
  const close = () => {
    if (!list) return;
    list.hidden = true;
    input.setAttribute('aria-expanded', 'false');
    input.removeAttribute('aria-activedescendant');
    active = -1;
  };
  const mark = (index) => {
    const options = list.querySelectorAll('[role="option"]');
    options.forEach((option, i) => option.setAttribute('aria-selected', String(i === index)));
    active = index;
    if (options[index]) {
      input.setAttribute('aria-activedescendant', options[index].id);
      options[index].scrollIntoView({ block: 'nearest' });
    }
  };
  const show = async () => {
    const query = input.value.trim();
    const index = await load(form.dataset.index);
    if (query !== input.value.trim()) return;
    records = find(index, query).slice(0, page ? 100 : 8);
    const target = page || list;
    target.replaceChildren();
    if (page) {
      const noscript = document.querySelector('.us-search-noscript');
      const all = document.querySelector('.us-site-index');
      if (noscript) noscript.hidden = true;
      if (all) all.hidden = !!query;
    }
    if (!query) {
      close();
      return;
    }
    if (!records.length) {
      const nothing = document.createElement(page ? 'p' : 'li');
      nothing.className = 'us-search-nothing';
      nothing.textContent = (form.dataset.nothing || 'Nothing found for') + ' “' + query + '”';
      target.append(nothing);
    } else {
      const holder = page ? document.createElement('ul') : target;
      if (page) {
        holder.className = 'us-search-results us-search-results-page';
        target.append(holder);
      }
      records.forEach((record, i) => holder.append(item(record, query, listId + '-' + i)));
    }
    if (list) {
      list.hidden = false;
      input.setAttribute('aria-expanded', 'true');
      active = -1;
    }
  };
  input.addEventListener('focus', () => load(form.dataset.index), { once: true });
  input.addEventListener('input', show);
  input.addEventListener('keydown', (event) => {
    if (!list || list.hidden) return;
    const count = records.length;
    if (event.key === 'ArrowDown' && count) {
      mark((active + 1) % count);
      event.preventDefault();
    } else if (event.key === 'ArrowUp' && count) {
      mark((active - 1 + count) % count);
      event.preventDefault();
    } else if (event.key === 'Enter' && active >= 0) {
      location.href = records[active].u;
      event.preventDefault();
    } else if (event.key === 'Escape') {
      close();
    }
  });
  form.addEventListener('focusout', (event) => {
    if (!form.contains(event.relatedTarget)) close();
  });
  if (page) {
    const query = new URLSearchParams(location.search).get('q');
    if (query) {
      input.value = query;
      show();
    }
  }
}

document.addEventListener('keydown', (event) => {
  if (event.key !== '/' || event.ctrlKey || event.metaKey || event.altKey) return;
  const target = event.target;
  if (target.closest && target.closest('input, textarea, select, [contenteditable]')) return;
  const input = document.querySelector('form[data-us~="search"] input[type="search"]');
  if (input) {
    input.focus();
    event.preventDefault();
  }
});
