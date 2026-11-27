- **TabbedContainer: `SetIconAfterTitle`** puts a tab's icon at the tab's right
  end, after its title (left of a badge or the close button), instead of
  leading the title.
- **DemoApp OpenGL showcase: the tabs read as tabs.** The Modern style with an
  accent underline under the open tab, and a coloured thumbnail of each tab's
  content (the torus knot, the fire shader, the Zarch terrain) at the right of
  its title.
- **DemoApp: every page's tabs in that one look.** `DemoKit::StyleTabs` (the
  Modern style, a 3 px accent underline under the open tab) replaces the mix
  of Rounded, Classic, Modern and default tab bars on the 53 pages with tabs;
  the Tabs page keeps its tour of every style.
- **DemoApp OpenGL showcase: a Z-up model stands upright in the 3D Models
  tab.** The tab now rotates a mesh by the file's own up axis, as the
  framework's viewers do, so the COLLADA aircraft no longer lies on its side.
