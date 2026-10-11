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
UltraVideoMapping.{h,cpp}   BuildTimelineModel / ApplyChange: the document <-> the element
UltraVideoHistory.{h,cpp}   undo and redo, the clean mark
UltraVideoPreview.{h,cpp}   the engine's worker thread: frames for the viewer, thumbnails,
                            peaks
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
  `../../fonts/fonts.css`, that is `screens/tokens/` and `screens/fonts/`,
  which are not in the tree yet, so they show no colours and the fallback
  font until those two files are added.
- The interface icon set (16 px grid, 1.5 px stroke, `currentColor`) lives
  in `media/icons/ultravideo/`, where the window loads it from; the app
  icon in `media/appicon/UltraVideo.{svg,png}`.
- `UltraVideoTokens.h` - the design tokens as C++: both palettes, spacing,
  radii, geometry and text styles, generated from the design's `tokens.json`.

The proposal that maps the design onto the framework, the engine and the
application, with the element specification, the engine changes and the
roadmap, is
[`Docs/Research/UltraVideoDesignProposal.md`](../../Docs/Research/UltraVideoDesignProposal.md).
The engine is documented in [`Docs/Modules/VideoFX/README.md`](../../Docs/Modules/VideoFX/README.md).
