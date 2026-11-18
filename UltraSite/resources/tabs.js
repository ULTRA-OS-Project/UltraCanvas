// UltraSite tabs module: a [data-us~="tabs"] box of <section class="us-tab"
// data-label="..."> panels becomes a tab list. Without it the panels stand
// one under the other, each under its label. The choice is remembered for
// every box with the same labels.
let count = 0;
for (const box of document.querySelectorAll('[data-us~="tabs"]')) {
  const panels = [...box.children].filter((child) => child.classList.contains('us-tab'));
  if (panels.length < 2) continue;
  const key = 'us-tabs:' + panels.map((panel) => panel.dataset.label).join('|');
  const list = document.createElement('div');
  list.className = 'us-tablist';
  list.setAttribute('role', 'tablist');
  const tabs = [];
  const select = (index, remember) => {
    tabs.forEach((tab, i) => {
      tab.setAttribute('aria-selected', String(i === index));
      tab.tabIndex = i === index ? 0 : -1;
      panels[i].hidden = i !== index;
    });
    if (!remember) return;
    try {
      localStorage.setItem(key, panels[index].dataset.label);
    } catch (error) {
      // Storage refused: the choice lasts for this page.
    }
  };
  panels.forEach((panel, index) => {
    const id = 'us-tab-' + (++count);
    const tab = document.createElement('button');
    tab.type = 'button';
    tab.className = 'us-tab-button';
    tab.id = id;
    tab.setAttribute('role', 'tab');
    tab.setAttribute('aria-controls', id + '-panel');
    tab.textContent = panel.dataset.label;
    panel.id = id + '-panel';
    panel.setAttribute('role', 'tabpanel');
    panel.setAttribute('aria-labelledby', id);
    panel.tabIndex = 0;
    const label = panel.querySelector(':scope > .us-tab-label');
    if (label) label.hidden = true;
    tab.addEventListener('click', () => select(index, true));
    tab.addEventListener('keydown', (event) => {
      const step = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
      if (!step) return;
      const next = (index + step + panels.length) % panels.length;
      select(next, true);
      tabs[next].focus();
      event.preventDefault();
    });
    tabs.push(tab);
    list.appendChild(tab);
  });
  box.insertBefore(list, box.firstChild);
  let start = 0;
  try {
    const saved = localStorage.getItem(key);
    const found = panels.findIndex((panel) => panel.dataset.label === saved);
    if (found >= 0) start = found;
  } catch (error) {
    // No storage: the first tab.
  }
  select(start, false);
}
