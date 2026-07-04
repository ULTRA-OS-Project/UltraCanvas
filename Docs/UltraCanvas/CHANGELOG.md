#### 2026-06-26 *0.2.32*
- Implemented HiDPI and scaling for all platforms
- Merge "LaTeX document renderer for UltraCanvas"
- Merge "OCR and vectorize solution for UltraCanvas"
- Merge "UltraCanvas heatmap demo presets"
- Merge "Media viewer widget for UltraCanvas"
- Merge "UltraCanvas Gauges demo fixes"

#### 2026-06-26 *0.2.31*
- Merge "UltraCanvas popup changelog link"
- Merge "UltraCanvas Gauge demo layout"
- Merge "UltraCanvas Slider demo layout"

#### 2026-06-26 *0.2.30*
- Demo: moved the **Waveform Chart** example out of *Audio Elements* and into the *Charts* category, where it belongs alongside the other data visualizations (a waveform is an amplitude-over-time plot, not a structural diagram).
- Waveform: added a **Display range** control to the waveform demo and a backing `SetVisibleWindowSeconds()` API on `UltraCanvasWaveformElement`. The view can now show the whole track ("All audio") or a trailing window that scrolls with the playhead — "Last 10 seconds" or "Last 60 seconds". Rendering, click-to-seek and the playhead all map to the visible window.
- Merge "UltraCanvas Album demo optimizations"
- Merge "UltraCanvas Gauge layout overlap"

#### 2026-06-24 *0.2.29*
- Merge "Waveform chart UltraCanvas integration"
- Merge "UltraCanvas Heatmap demo optimization"
- Merge "UltraCanvas treeview auto-scroll"
- Merge "Module infos MD diagram/image viewer"
- Changelog link on startup info page
- Merge "UltraCanvas demo Modules info pages"

#### 2026-06-24 *0.2.28*
- Fixed crash in the UltraCanvasDependenciesExamples demo screen
- In the UltraCanvasListView change model pointer to shared_ptr instead of raw pointer, prevent possible crashes

#### 2026-06-23 *0.2.27*
- Fixes Video Player issues (freeze video/audio mainly in Linux)
- Fix Video Player layout issues, wrong aligned text, use icons instead of manual drawing

#### 2026-06-23 *0.2.26*
- Docs: audited every implemented demo element for a wired-up Programmer's Guide / example doc and filled all the gaps. Added five new guides — `UltraCanvasScrollbarExamples.md`, `UltraCanvasSlideshowExamples.md`, `UltraCanvasQRCodeExamples.md`, `UltraCanvasSpreadsheetExamples.md` and `UltraCanvasXARExamples.md` — each documenting the real public API (no invented symbols) with runnable examples drawn from the matching demo source.
- Demo: wired the **C++ source** and **documentation** header icons for the elements that were missing them — Scrollbars, Spreadsheet, Slideshow, QR code and XAR now point at their example `.cpp` and new `.md`; Video and Audio now point at their existing `UltraCanvasVideoExamples.cpp`/`UltraCanvasVideo.md` and `UltraCanvasAudioExamples.cpp`/`UltraCanvasAudio.md`; and Album now links its existing `UltraCanvasAlbumExamples.md`.

#### 2026-06-23 *0.2.25*
- Gauges: added a Programmer's Guide (`Docs/UltraCanvas/UltraCanvasGaugeExamples.md`) covering the full mode-driven API — all 17 `GaugeMode`s, the round-gauge (CircularRing) style system, decorations (ranges/thresholds/external pointers/sub-dial), live clock & stopwatch controls, and a runnable code example per gauge family.
- Demo: the Gauges demo page now shows the **C++ source** and **documentation** header icons (wired its `demoSource`/`demoDoc` to `UltraCanvasGaugeExamples.cpp` and the new guide), matching every other element, plus the four tab variants (Round Gauges, Progress & Linear, Specialized, Analog) in the tree.

- Fix Windows video playback: the player loaded a file and played audio but showed no picture, and the play/pause/stop transport misbehaved (multiple clicks to stop, no resume after pause). Media Foundation's sample-grabber video sink rejected the session's rate control (`MF_E_UNSUPPORTED_RATE`) and never delivered a single frame, which also corrupted the session and scrambled the transport state. `MFDecodeSession` now decodes video through an `IMFSourceReader` (RGB32, advanced video processing) paced to the audio clock, with an audio-only Media Session providing sound + the master clock (video-only files fall back to a wall clock). Also force opaque alpha on MF RGB32 frames — the unused "X" byte was 0, which rendered fully transparent in the premultiplied Cairo `ARGB32` pixmap. Linux/GStreamer playback is unchanged.
- Video: added a cross-platform thumbnail / poster-frame API (`UltraCanvasVideoThumbnail.h`). `CaptureVideoThumbnail()` returns a single decoded frame, `CaptureVideoThumbnailPixmap()` a ready-to-draw `UCPixmap`, and `SaveVideoThumbnail()` writes a file (encoder chosen from the extension: `.qoi` → QOI, otherwise PNG — both need no libvips), e.g. for `UltraCanvasAlbum` `thumbnailPath`. Each takes a `VideoThumbnailRequest` (target time — or an automatic position — plus optional aspect-preserving `maxWidth`/`maxHeight`).
- Video: new opt-in backend capability `IVideoBackend::GrabThumbnail()`. The GStreamer (Linux) backend implements it as a throwaway `uridecodebin` pipeline that prerolls to PAUSED, seeks accurately and pulls one preroll sample (no audio, no full playback). Backends without it (null / Media Foundation / AVFoundation) use a generic decode-session fallback, so thumbnails work wherever decoding does.

#### 2026-06-23 *0.2.24*
- Merge "Color picker widget for UltraCanvas" and fix layout errors.
- Fix toolbar button width (make it auto)
- Fixed incremental search in TextArea (stop advance on each typed matched character)
- Refactor Audio element. Use composite widget instead manual draw. Use SVG icons for play/pause/etc.. buttons
  
#### 2026-06-21 *0.2.23*
- `UltraCanvasGLSurface` now resizes its render target / framebuffer to follow the element's actual bounds on every render, however the bounds were changed. Previously the framebuffer size (`surfaceWidth_`/`surfaceHeight_`) was only updated from the `SetBounds` override, so a layout-driven resize — flex/grid stretch, a parent resize, `SetElementSize`, a window resize — left the GL content stuck at its old size (it wrote `finalBounds` without routing through `SetBounds`). `Render()` now syncs the framebuffer size from `GetLocalBounds()` and forces a content re-render that pass, so GL surfaces resize correctly under any layout path (this is what made the Shaders-tab "maximize" need an explicit `SetBounds`; flexible/maximized GL surfaces now grow on their own).

#### 2026-06-21 *0.2.22*
- Fix the "maximize canvas" control in the OpenGL 3D Showcase "Shaders" tab. Three issues: (1) the toggle button was pinned to the tab content-area's right edge instead of the canvas's top-right corner, so once maximized it sat far from the (un-grown) canvas; (2) it showed a "⛶" glyph the demo font lacks (rendered as "…"); (3) clicking it did not enlarge the canvas. The canvas was being resized via `SetElementSize`, which only sets a CSS dimension — but the tab content is absolutely positioned (no layout pass re-applies it) and, crucially, `UltraCanvasGLSurface` only grows its GL framebuffer from its own `SetBounds` override. The maximize now resizes the surface with `SetBounds` (so the framebuffer actually grows to fill the tab), pins the button to the surface's current top-right corner in both states, and renders the new `media/icons/maximise.svg` icon (drawn as a white mask on the dark translucent button) instead of the missing glyph.

#### 2026-06-21 *0.2.21*
- Fix the per-effect parameter-slider captions ("Brightness", "Splat radius (px)", etc.) being partially obscured in the OpenGL 3D Showcase "Shaders" tab control panel. The sliders use an always-on `Number` value display, which `UltraCanvasSlider` draws just above the track — and since the slider pins its track to the bottom `handleSize` (16px) of its bounds, a short (22px) slider drew the value text ~10px above its own top edge, on top of the caption placed above it. The four per-effect slider groups (Ball Surface, Pulse, Fragments, Circles) now use 36px-tall sliders (handle + a value-text row + margin) and re-spaced rows, so each value number sits inside its slider below the caption with no overlap.

#### 2026-06-21 *0.2.20*
- Curated the OpenGL 3D Showcase "Shaders" tab effect list. Removed four effects together with their GLSL/source, formula headers, per-effect uniforms, state fields and parameter-slider groups: "Warp Starfield", "Rössler Attractor", "Mandala (12-wave)" and "ULTRA OS Logo". Reordered the remaining list so the three Twigl one-liners "Horizon", "Protostar2" and "Plasma Orb" appear first. The info text and `Docs/UltraCanvas/UltraCanvasGLSurfaceExamples.md` were updated to match; effects with live sliders are now Ball Surface, Pulse, Fragments and Circles.

#### 2026-06-21 *0.2.19*
- Fix the OpenGL 3D Showcase demo showing horizontal and vertical scrollbars inside every tab even when the window had room. The tabbed container was `980×690` with a 34px tab bar, so each tab's content area was only `980×656` — smaller than the tab contents, whose control panel reaches x≈986 and whose Shaders source viewer reaches y≈680. Since each tab's root is resized to the content-area bounds (`autoShowScrollbars` defaults on), the small overflow forced scrollbars. The showcase container (`1024×800`) and tabbed container (`1004×726`, content area `1004×692`) were enlarged so the content area clears every tab's children with margin; the three tab roots were updated to match. No scrollbars now appear in any tab.

#### 2026-06-20 *0.2.18*
- Breadcrumb: added a `Parallelogram` item style (`BreadcrumbItemStyle::Parallelogram` + `BreadcrumbStyle::Parallelogram()` preset) — interlocking slanted/skewed segments (outer edges of the first/last segment stay vertical) sharing the `arrowSize` skew depth with the Arrow style.
- Breadcrumb: added an optional **level indicator** — a leading numbered badge per item. `BreadcrumbStyle::showLevelIndicator` enables it; `levelIndicatorBackground` selects the badge background (`Round`, `Rectangle`, or `NoBackground`); `levelIndicatorBorder` outlines it; plus `levelIndicatorSize`/`levelIndicatorColor`/`levelIndicatorTextColor`/`levelIndicatorBorderColor`/`levelIndicatorBorderWidth`. The new `BreadcrumbStyle::Steps()` preset combines Arrow segments with round numbered badges (dark "wizard step" strip). Works with any item style.
- Demo: added "15. Numbered steps", "16. Parallelogram", and "17. Level indicators" (round / rectangle / none / bordered) rows to the breadcrumb demo page.

#### 2026-06-20 *0.2.17*
- Breadcrumb: added an `Arrow` item style (`BreadcrumbItemStyle::Arrow` + `BreadcrumbStyle::Arrow()` preset) — interlocking right-pointing arrow/chevron "steps". Each segment grows a pointed tip past its right edge that nests into the next segment's matching left notch (the first segment is flat-left, the last one's tip trails off), with the current step highlighted. New `BreadcrumbStyle::arrowSize` controls the tip/notch depth. Added a "14. Arrow steps" row to the breadcrumb demo page.

#### 2026-06-20 *0.2.16*
- Fix the Breadcrumb demo: item text was not vertically centered within the strip/pills. The element hand-rolled its vertical centering as `centerY - (int)textHeight / 2`, truncating the half-height to an integer (drifting the glyphs ~1px off the center line shared by the separators and icons) and, more importantly, never routing through the text layout's automatic centering — so it never compensated for the font's top line-leading. On fonts that split external leading above the baseline (e.g. Segoe UI on Windows) this pushed the visible text several pixels low. Breadcrumb item/overflow text now centers via the text layout's `VerticalAlignment::Middle` over the slot height (full sub-pixel precision), and `UCTextLayout::GetLayoutVerticalOffset` re-enables the top-leading compensation for Middle alignment, computed in Pango units to keep the sub-pixel offset (a previous int-pixel version had regressed Segoe UI 12pt). The compensation is a no-op where `baseline == ascent` (DejaVu/FreeSans on Linux), so other platforms are unaffected.

#### 2026-06-19 *0.2.15*
- Fixes Gstreamer build in Linux
- Fixes Gstreamer (video player) and native dialogs crash in Linux
- Merged "Add Radar Chart element"
- Fix Radar Chart rendering and animation
- Show ULTRA OS overview page when "ULTRA OS modules" tree node is selected
- Merge "Add rounded corner label examples and fix resource paths"
- Merge "Add digital clock, segmented ring, centre content, and faded colours to gauges"

#### 2026-06-19 *0.2.15*
- Added intro description for different Modules

#### 2026-06-19 *0.2.14*
- Merge "PDF Text demo page layout" fix
- Merge heatmap implementation
- Merge "Shader graphics for demo" (more shader examples)
- Merge "UltraCanvas module demo content"

#### 2026-06-18 *0.2.13*
- Fix the Gauge element's linear/progress "rounded bar" rendering at low values. `RenderLinearBar` drew the value fill with a corner radius of half the bar's thickness regardless of the fill length. `FillRoundedRectangle` does not clamp the radius, so once the fill became shorter than the bar's thickness (roughly under 10% on a wide bar) the four corner arcs overlapped and collapsed the fill into a perfect circle. The radius is now clamped to half of the fill's smaller dimension, so short fills render as a proper pill that stays inside the track.

#### 2026-06-17 *0.2.12*
- Fix two bugs in the OpenGL "Zarch" 3D demo:
- The application could not be closed while an animated (Continuous) GL surface was on screen, and the debug log kept spinning. `UltraCanvasGLSurface::Render` re-posted a full-window `Redraw` event every frame; it now invalidates only the surface's own region and stops re-arming once the window is closing/closed/hidden, so sibling widgets no longer repaint at the animation frame rate and the event loop can shut down.
- The release build crashed when opening the Zarch tab (before any 3D was drawn) while the debug build did not. The terrain/tree hash (`Hash2`) multiplied signed `int`s past `INT_MAX`, which is undefined behaviour that an optimised (`-O3`) build is free to miscompile; it now uses well-defined unsigned arithmetic. The Models/Shaders tabs do not use this hash, which is why only Zarch was affected.
- Merge "Groupbox border alignment bug" fix
- Merge "DatePicker demo layout bug" fix
- Merge "Spreadsheet save button with format options" fix
- Merge "Texter unsaved tab indicator bug" fix
- Merge "UltraCanvas audio layout improvements"

#### 2026-06-17 *0.2.11*
- Spreadsheet demo: added a "Save…" button to the toolbar that offers every format the engine can write (OpenDocument `.ods`, `.csv`, `.tsv`). Choosing a CSV/TSV name opens a new "Text Export" options dialog (character set, field separator, text delimiter, quoting policy, line ending, optional BOM) with a live text preview; `.ods` saves directly.
- Spreadsheet engine: added `SaveCSVWithOptions`/`ExportCSVToString` plus a `CSVExportOptions` struct and a `CSVEncodeFromUtf8` charset encoder (UTF-8/UTF-16/Latin-1/Windows-1252, optional BOM). Fixed `.tsv` saving to actually use a tab separator.
- Merge "PDF support", implemented PDF demo (viewer in the demo app)
- Merge "Datepicker multi-month display"
- Merge "UltraCanvas album widget improvements"

#### 2026-06-15 *0.2.10*
- Fix the spreadsheet component, editing, keys navigation, cell size changing, scrolling, loading files

#### 2026-06-14 *0.2.9*
- OpenGL surface support enabled on Windows: implemented the WGL context manager (hidden helper window + legacy-context bootstrap to load `wglCreateContextAttribsARB`, then a requested core/compatibility context). Modern GL entry points are resolved via GLEW (`mingw-w64-x86_64-glew`), since `opengl32.dll` only exports OpenGL 1.1. `ULTRACANVAS_ENABLE_GL` now defaults ON for Windows when GLEW is found.
- OpenGL surface support enabled on macOS: completed the CGL context manager (honors the requested GL version/profile and color/depth/stencil config, real extension querying via `glGetStringi`) and let `ULTRACANVAS_ENABLE_GL` default ON for macOS as well as Linux.
- Merged with the "UltraCanvas date picker widget" code
- Merged with the "UltraCanvas Album widget" code
- Merged with the "UltraCanvas Demo Slideshow layout" code
- Merged with the "UltraCanvas QR code demo page" code

#### 2026-06-12 *0.2.8*
- Slideshow demo: reworked the options panel into a labelled-row grid (label column on the left, wrapping option buttons on the right) grouped Controls / Indicator / Indicator edge / Fade style / Panel layout / Image / Letterbox fill. Each group now behaves like a radio with the active choice highlighted, the panel-layout split/overlay/off positions are grouped under sub-labels, and crop focus dims unless the Cover fit is selected
- Slideshow demo: retitled the page to "UltraCanvas Slideshow widget" and removed brand-specific references throughout the slideshow widget and its demo
- Slideshow demo: framed the live widget with a captioned "Slideshow widget" box and added a "Slideshow widget options" heading above the controls, so it's clear which part is the actual widget and which are programmer-facing options
- Slideshow: the widget now takes keyboard focus on click and supports manual navigation with the arrow keys — Left/Down go to the previous slide, Right/Up to the next (numpad arrows too)

#### 2026-06-09 *0.2.7*
- Merged and fixed the "# Spreadsheet support for UltraCanvas"

#### 2026-06-09 *0.2.6*
- Merged and fixed the "OpenGL 3D showcase demo"

#### 2026-06-09 *0.2.5*
- Slideshow: added comprehensive info-panel layouts via `SlideshowInfoLayout` — split on any of the four sides (`SplitLeft/Right/Top/Bottom`), edge overlays on the image (`OverlayLeft/Right/Top/Bottom`), corner overlays (`OverlayTopLeft/TopRight/BottomLeft/BottomRight`), `OverlayCenter`, `OverlayFull`, and `Hidden`
- Slideshow: indicators can now hug any edge via `SlideshowIndicatorEdge` (Top/Bottom rows, Left/Right stacked columns)
- Slideshow: added `SlideVertical` and `ZoomFade` transition styles
- Slideshow: configurable image fitting for mismatched images — `imageFit` (Cover auto-crop, Contain, Fill, ScaleDown, NoScale), an `imageFocus` focal point that picks which part survives a crop, and `gapFill` for letterboxed images (background color, dedicated letterbox color, or a zoomed image backdrop)
- Slideshow demo: added pickers for info-panel layout, indicator edge, image fit, crop focus and letterbox fill, plus the new transitions

#### 2026-06-07 *0.2.4*
- Fix QRCode examples page (fix wrong character and change the default QR Code generation)

#### 2026-06-07 *0.2.3*
- QR code decoder fixed (installed missing lib and configured github build)
- Merged branch "UltraCanvas QR demo field visibility bug"
- Merged branch "Barcode widget for UltraCanvas" and fix barcode widget and its demo

#### 2026-06-04 *0.2.2*
- Added Gauges, Compositor diagram, QR code
- Fixed Tabbed container and inner tabs layout
- Implemented SortChildNodes method and autoSortChildren property in the TreeView
- Fix label layout resize bug
- Make Arc diagram and Architectural Adjacency Diagram use Grid layout instead of fixed elements positions

#### 2026-06-03 *0.2.1*
- Major update. Implemented CSS Flex/Grid/Absolute layout support.
 
#### 2026-05-20 *0.1.39*
- Show cursor and allow selection in the TextArea in read-only mode
- Autodetext syntax highlighting rules by filename with auto fallbask to extension
- Fix possible bug in menu and tooltips rendering (wrong color)

#### 2026-05-19 *0.1.38*
- Implemented the SplitPane element
- Fix bug when scrollbar of outer container overlap with inner container's elements or scrollbar (mouse events incorrectly goes to inner container instead of outer container's scrollbar)
- Implemented the Barcode widget with 1D symbology encoders: Code 39 / 39 Extended / 93 / 128 (A/B/C/auto), GS1-128, EAN-13/8, UPC-A/E, ISBN-13, ITF, ITF-14 (with bearer bars), Standard 2 of 5, Codabar, MSI Plessey (4 check-digit modes), Pharmacode. From-scratch C++20 encoders, no external dependencies; live editor + gallery in Tools → Bar code

#### 2026-05-18 *0.1.37*
- Arc diagram improvement
- Change layout of "Bitmap elements" screen in the Demo app
- Use TextArea instead of Markdown element in the "Text Document/Markdown" screen in the Demo app
- Replace DejaVu default embedded font by Ubuntu font

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

