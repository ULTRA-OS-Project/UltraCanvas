- **DemoApp: `--component` starts without the About window, and
  `--no-about` leaves it out on any start.** The modal "UltraCanvas Demo -
  Information" window opened on every start, also over the page
  `--component <id>` had just selected, so a page could only be screenshotted
  after a script clicked it away. Naming a page now implies `--no-about`;
  `--help` lists both.
- **Docs: no page points at the removed layout managers any more.**
  `GettingStarted.md` still told readers (and the prompts it suggests) to lay
  out with `UltraCanvasBoxLayout`, `UltraCanvasGridLayout` and
  `UltraCanvasFlexLayout`, and `UltraCanvasImagePerformanceTest.md` included
  `UltraCanvasBoxLayout.h`; none of those headers exist. Both now name the
  CSSLayout engine (`layout.SetFlexRow()` / `SetFlexColumn()` / `SetGrid()`),
  which `UltraCanvasLayoutExamples.md` documents.
