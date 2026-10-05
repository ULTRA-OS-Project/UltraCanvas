- **DemoApp: the Ultra Message page shows each sample message once.** Two
  causes. `SelectDemoItem` (used by `--component`) selected the tree node,
  which already fires `onNodeSelected`, and then called `OnTreeNodeSelected`
  itself, so every page opened that way was built twice and the first copy
  thrown away. The Ultra Message page seeded the bus both times. Separately,
  the page's broker and journal outlive the page, so each later visit seeded
  the samples again. The node is now selected silently and displayed once, and
  the page seeds only a feed that comes up empty. Building the page twice also
  destroyed the first Message Centre while its seeded messages were queued,
  the crash fixed in UltraMessage itself in #672.
- **DemoApp: the Ultra Message page's subtitle is no longer clipped.** It is
  three lines at the usual window width and had a fixed 40 px height; its
  height now follows its lines.
