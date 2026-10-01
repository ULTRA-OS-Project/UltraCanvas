- **Screen readers reach UltraCanvas applications on Linux and Windows.** The
  platform-neutral accessibility model (`UltraCanvasAccessibility.h`) now has
  platform bridges:
  - **AT-SPI on Linux** (`OS/Linux/UltraCanvasLinuxAccessibility`): the
    application registers on the accessibility bus and answers for its
    windows (frames) and elements - roles, names, states, extents, hit
    testing, focus, and for text the `Text` interface (text, caret,
    selection, characters, words, sentences, lines, attributes, character
    and range extents). Focus, window activation, caret and selection
    changes and text inserted or deleted are signalled. It connects when the
    desktop turns accessibility on (`org.a11y.Status`), also later, never
    without a session bus; `NO_AT_BRIDGE=1` keeps it off and
    `UC_ACCESSIBILITY_ALWAYS_ON=1` forces it on. GDBus calls are answered on
    the UI thread through an fd watch on the event loop.
  - **UI Automation on Windows** (`OS/MSWindows/UltraCanvasWindowsAccessibility`):
    `WM_GETOBJECT` returns a fragment root per window, every element is a
    fragment, and text elements offer the Text pattern with ranges by
    character, format run, word, line, paragraph and document, text
    attributes, heading style ids and spelling/comment/revision
    annotations; focus, text and selection events are raised.
    `UIAutomationCore.dll` is loaded at run time. Compile-checked; not yet
    run against Narrator, NVDA or JAWS.
  - Shared by both: `UltraCanvasAccessibilityBridge.h` (tree walk, element
    ids, screen geometry, hit testing, text diffing).
  - `IAccessibleText::IsReadOnly()`; `AccessibilityEventType::ElementDestroyed`,
    announced by every element's destructor;
    `UltraCanvasWindowBase::GetContentScreenOrigin()` - the screen position
    of a window's content, below the title bar on Windows.
  - `Tests/AtspiBridgeTest` drives the AT-SPI bridge with a libatspi client
    on a private accessibility bus (built when `atspi-2` headers are
    present; skips without at-spi2-core).
