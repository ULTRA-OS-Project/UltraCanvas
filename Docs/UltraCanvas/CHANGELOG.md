#### 2026-05-18 *0.1.37*
- Arc diagram improvement
- Change layout of "Bitmap elements" screen in the Demo app
- Use TextArea instead of Markdown element in the "Text Document/Markdown" screen in the Demo app

#### 2026-05-17 *0.1.36*
- Change the Breadcrumbs element demo
- Make more Docs for different controls 
- Added Slideshow example in Widgets/Slideshow section of Demo app
- Fix for Pie Chart labels

#### 2026-05-15 *0.1.35*
- Remove JXL from Image Performance test as JXL format does not supported by currently used libvips
- Fix buttons size to fit text
- Fix bug when element is deleted but capturing the mouse or focused then app may crash
- Implemented Breadcrumbs element demo

#### 2026-05-15 *0.1.34*
- Use TextArea to show Markdown info in the Modules section
- Add more modules description to Modules section
- Implement Breadcrumb demo
- 
#### 2026-05-14 *0.1.33*
- Implemented new Image performance demo
- Fix problem with AltGr+key in Windows
- Fix ordered list content offset in MD-mode in TextArea
- Implemented Pie Chart element
- Implemented Adjacency Diagram element
- Implemented Arc Diagram element
- Implemented Breadcrumb element

#### 2026-05-12 *0.1.32*
- Implemented UltraCanvasFileLoader
- Implemented OS Recent files support (add opened files to OS Recent files list)
- Fix wrong calculation of mouse coordinates and bounds in the some diagrams 

#### 2026-05-10 *0.1.31*
- Fixed font rendering, now Windows and Linux will rendered using same included DejaVue fonts

#### 2026-05-09 *0.1.30*
- Implemented more different diagrams
- Implemented JitterChart
- Attempt to fix menu crash on MacOS

#### 2026-05-06 *0.1.29*
- Refactor Checkbox code, split to Checkbox/Redio/Switch
- Implemented more visual styles for Switch
- Attempt to fix crash on MacOS

#### 2026-05-06 *0.1.28*
- Implement support tooltips for menu itmes
- Implement maxWidth option for menu and ellipsize mode for menu items

#### 2026-05-04 *0.1.27*
- Attempt to implement MacOS HiDPI support

#### 2026-04-30 *0.1.26*
- Fix cursor position in Markdown mode in TextArea

#### 2026-04-28 *0.1.25*
- Major rework in the UltraCanvas rendering, implemented optimized rendering.
  Now popups/tooltips rendered in own surfaces (does not need to repaint main content after show/hide)
  Implemented partial rendering using dirty rectangles (for any content) 

#### 2026-04-23 *0.1.24*
- Fix tooltips rendering

#### 2026-04-23 *0.1.23*
- Some color fixes for Dark mode

#### 2026-04-20 *0.1.21*
- Refactored elements rendering/events coordinate system to use element-based coordinates where 0,0 is
  top-left element's corner instead of container-based where 0,0 was container top-left corner.
  Set clipping to element's bounds and save/restore state in container's rendering loop instead rely on element Render().
  Don't render invisible elements (hidden by scroll position)

#### 2026-04-20 *0.1.20*
- Shard very long lines to speed up TextArea on big files.
  Lines longer than 4000 codepoints are split at a break char
  (space/tab/punct) or force-split at 12000 during SetText.
  Known problems: 
    if line has no break chars (very rare case) it will splitted at 12000 char boundary,
    attempt to glue that lines (backspace or delete) will cause reshard (resplit) again
    and it will splitted at same boundary, visually it will looks like delete/backspace did not work 
- Show current line marker (red box) for cursor position
- Calculate and show real logical line numbers for split lines. 

#### 2026-04-17 *0.1.19*
- UpdateGeometry for visible childs in container only

#### 2026-04-16 *0.1.18*
- Revert back to the Sans font for Windows insetad of detecting default font. It detected the "Segoe UI" and this shit font can't be vertically centerted without special patches especially for that font, it always shifted down a little (even in browsers)
- Fixed bug with high CPU usage and slow cursor movement on the big files with very long lines.

#### 2026-04-16 *0.1.17*
- Add on-the-fly submenu regeneration to UltraCanvasMenu
- Fix Windows high CPU usage when app is idle.
- Fix main-row digit/punctuation key mappings on Linux and macOS
- Fix vertical text position issues in Windows (was shifted down a little)

#### 2026-04-16 *0.1.16*
- Full rework of display and rendering the text. Use Pango layout to format text. Allow to use variable height lines

#### 2026-04-06 *0.1.15*
- Add platform-native system font detection, replace hardcoded "Sans" defaults

