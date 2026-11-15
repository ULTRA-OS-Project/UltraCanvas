- **The Apps window keeps your place in the tiles as well.** The tree view
  stopped jumping back to the top in 0.4.19; the tiles still did, because the
  whole scrolling area was built again on every keystroke in the filter box,
  every new list of applications and every application moved. The area is
  kept now and only the tiles inside it are made again, so the view stays
  where you left it. Switching to the tree and back, or a filter that matches
  nothing, builds the page afresh as before.
- **The Apps window can be tested.** It read the machine's own `.desktop`
  files and made its own window, so nothing could drive it: `Hooks`
  gains `applications`, a list handed to the window in place of the scan
  (nothing is read from disk or kept between openings), `ShowApps` is public
  beside it, and `Window()` lets a caller reach the page's elements by their
  identifiers. `UltraDesktopAppStarterTest` uses all three to hold both views
  to keeping the chosen category, the opened categories and the scroll
  position across a rebuild; CI runs it on a virtual display.
