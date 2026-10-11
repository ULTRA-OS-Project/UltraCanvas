# UltraVideo

A video editor for ULTRA OS and the other UltraCanvas desktops, on the
VideoFX module (FFmpeg behind VideoFX's own API) and the framework's
`UltraCanvasVideoTimeline` element.

The application is not written yet. What this directory holds is its design:

- `screens/` - the screens as SVG (`*-light.svg`), one Markdown
  specification per component, and `BrandBook.md` with the tokens, type,
  spacing and interaction rules they share.
  `screens/components/` holds the design's neutral timeline model as
  TypeScript types (`index.d.ts`), the reference renderer that draws it
  (`bundle.js`, a visual specification, not production code) and its
  stylesheet (`bundle.css`).
- `Icons/` - the interface icon set the screens use (16 px grid, 1.5 px
  stroke, `currentColor`), as single-path SVG files.
- `UltraVideoTokens.h` - the design tokens as C++: both palettes, spacing,
  radii, geometry and text styles, generated from the design's `tokens.json`.

The proposal that maps the design onto the framework, the engine and the
application, with the element specification, the engine changes and the
roadmap, is
[`Docs/Research/UltraVideoDesignProposal.md`](../../Docs/Research/UltraVideoDesignProposal.md).
The engine is documented in [`Docs/Modules/VideoFX/README.md`](../../Docs/Modules/VideoFX/README.md).
