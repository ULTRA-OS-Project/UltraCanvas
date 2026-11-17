// UltraSite copy module: a copy button on every code block that asks for
// one (<pre data-us="copy">). Without it the code is selectable text. It
// starts again on every page the swap navigation brings in (us:page).
function start() {
  if (!navigator.clipboard) return;
  for (const pre of document.querySelectorAll('pre[data-us~="copy"]')) {
    if (pre.parentElement && pre.parentElement.classList.contains('us-code-frame')) continue;
    const label = pre.dataset.copyLabel || 'Copy';
    const done = pre.dataset.copiedLabel || label;
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'us-copy';
    button.textContent = label;
    button.addEventListener('click', async () => {
      const code = pre.querySelector('code') || pre;
      try {
        await navigator.clipboard.writeText(code.textContent);
        button.textContent = done;
        setTimeout(() => { button.textContent = label; }, 1500);
      } catch (error) {
        // The browser refused the clipboard: nothing to say.
      }
    });
    const frame = document.createElement('div');
    frame.className = 'us-code-frame';
    pre.before(frame);
    frame.append(pre, button);
  }
}

start();
document.addEventListener('us:page', start);
