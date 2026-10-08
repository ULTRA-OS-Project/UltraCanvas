- **Docs: `GettingStarted.md` and `UltraCanvasImagePerformanceTest.md` pass
  the doc checker.** The Image Performance Test page now matches the demo
  source:
  - the `UltraCanvasImageElement` outline carries its real constructors and
    `LoadFromFile(path, forceLoad)`;
  - the Include BMP checkbox reacts through `onStateChanged`, not
    `onCheckedChanged`;
  - its two `std::filesystem` calls convert with `PathFromUtf8` /
    `PathToUtf8`, as the UTF-8 path rule asks, where they used `path(str)`
    and `.string()`;
  - new sections show the page's private helpers (`NowMs`,
    `DefaultOptionsFor`), its grouped bar chart and the `PanelState` every
    lambda shares.

  Every snippet on the page now compiles against the headers with all its
  names typed.
- **`scripts/check_doc_examples.py` 1.1.0.**
  - A snippet's `#if` / `#ifdef` / `#else` / `#endif` and `#define` lines
    stay where they are, so Windows-only code (`WinMain` under
    `#ifdef _WIN32`) is left out on Linux instead of being compiled.
  - A `doc-check` comment can `#define` a macro the build supplies
    (`MYAPP_VERSION`).
  - Prose may name a function that the doc's own headers or its `doc-check`
    comment declare.
  - `Name (` with a space is no longer read as a call, so
    `Code needs to be PRed (N lines)` is not flagged.
  - A name whose type sits in another block is no longer given up when
    clang suggests that type on one of its two generated lines but not on
    the other.
