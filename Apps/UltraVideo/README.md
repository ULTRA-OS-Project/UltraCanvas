# UltraVideo

A video editor for ULTRA OS and the other UltraCanvas desktops, on the
VideoFX module (FFmpeg behind VideoFX's own API) and the framework's
`UltraCanvasVideoTimeline` and `UltraCanvasTimecodeField` elements. How to
use it: [`Docs/UltraVideo/README.md`](../../Docs/UltraVideo/README.md);
what changed: [`Docs/UltraVideo/CHANGELOG.md`](../../Docs/UltraVideo/CHANGELOG.md).

## The code

```
main.cpp                    bootstrap: the project or the media named on the command line
UltraVideoWindow.{h,cpp}    the editor window: app bar, panes, media bin, timeline dock,
                            playback, the timeline's change loop, files, export
UltraVideoDocument.{h,cpp}  the document (the VideoFX project, clip ids, the bin, markers,
                            the view) and the .ultravideo file
UltraVideoFormats.{h,cpp}   the files it takes and makes: the engine's registered with the
                            codec registry, the dialogs offered FileLoader's lists
UltraVideoMapping.{h,cpp}   BuildTimelineModel / ApplyChange: the document <-> the element
UltraVideoHistory.{h,cpp}   undo and redo, the clean mark
UltraVideoPreview.{h,cpp}   the engine's worker thread: frames for the viewer, thumbnails,
                            peaks
UltraVideoProxies.{h,cpp}   proxies of the bin's videos, made on a thread of their own;
                            the viewer and the draft play the timeline through them
UltraVideoViewer.{h,cpp}    the frame surface, Fit, the transport
UltraVideoInspector.{h,cpp} the Clip, Audio and Project tabs
UltraVideoDialogs.{h,cpp}   the export dialog, the unsaved-changes question
UltraVideoStory.{h,cpp}     the story wizard: three steps in a modal, PlanStory and
                            BuildStoryDocument in UltraVideoMapping make the project
UltraVideoTheme.h           the tokens applied to elements: buttons, the timeline's and the
                            timecode field's styles
UltraVideoTokens.h          the design tokens as C++ (generated; do not edit)
UltraVideo.desktop          the freedesktop entry (projects and media)
CMakeLists.txt
```

The window owns the document, its history and the worker. Every edit goes
through `Edit()`: a snapshot, the change, a history entry, and the document
shown again everywhere (the timeline's model from `BuildTimelineModel`, the
preview planned again, the inspector, the app bar). The timeline element
never applies an edit itself: it reports one, `ApplyChange` writes it into
the document, and `SetModel` shows the result.

The test: `Tests/UltraVideoDocumentTest.cpp` (headless, the engine needed).

## The design

- `screens/` - the screens as SVG (`*-light.svg`), one Markdown
  specification per component, and `BrandBook.md` with the tokens, type,
  spacing and interaction rules they share.
  `screens/components/` holds the design's neutral timeline model as
  TypeScript types (`index.d.ts`), the reference renderer that draws it
  (`bundle.js`, a visual specification, not production code) and its
  stylesheet (`bundle.css`).
  `screens/components/pages/` holds the live pages: one per component,
  `Cover.html`, and `index.html` framing them all with a theme toggle. Open
  `index.html` in a browser. The pages link `../../tokens/tokens.css` and
  `../../fonts/fonts.css`: `screens/tokens/tokens.css` is generated from
  `screens/tokens/tokens.json` (the design's tokens, the source of truth
  for both the pages and `UltraVideoTokens.h`) by
  `scripts/generate_ultravideo_tokens.py`, and `screens/fonts/fonts.css`
  loads IBM Plex from Google Fonts with the framework's Ubuntu fonts from
  `media/fonts/` as the offline fallback.
- The interface icon set (16 px grid, 1.5 px stroke, `currentColor`) lives
  in `media/icons/ultravideo/`, where the window loads it from; the app
  icon in `media/appicon/UltraVideo.{svg,png}`.
- `UltraVideoTokens.h` - the design tokens as C++: both palettes, spacing,
  radii, geometry and text styles, generated from
  `screens/tokens/tokens.json` by `scripts/generate_ultravideo_tokens.py`
  (`--check` says whether it and `tokens.css` are current). Edit the JSON,
  run the script; never the outputs.

The proposal that maps the design onto the framework, the engine and the
application, with the element specification, the engine changes and the
roadmap, is
[`Docs/Research/UltraVideoDesignProposal.md`](../../Docs/Research/UltraVideoDesignProposal.md).
The engine is documented in [`Docs/Modules/VideoFX/README.md`](../../Docs/Modules/VideoFX/README.md).
