- **Presentations: slide masters, slide layouts and transitions are read
  and written, in .pptx and .odp.** The readers had flattened a deck's
  masters and layouts into each slide's shapes (which drawing needs, and
  still gets) and kept no transitions. The writers always wrote one fixed
  master and layout. As a result, a slide editor had no layouts to offer
  for a new slide, and a saved deck lost its design and its transitions.
  - **PowerPoint reader:** reads every master and layout, in the file's
    order, into `PptxPresentation::masters` / `layouts`: name, layout type,
    master, background, and the placeholders resolved the way a slide's
    are, keeping their prompt text ("Click to edit Master title style").
    Sets each slide's `layoutIndex`. Reads `p:transition` (also PowerPoint
    2010's inside `mc:AlternateContent`) into `PptxSlide::transition`:
    - the kind, with a kind the model does not know read as Fade
    - the length, from `p14:dur`, else the speed
    - `advClick` and `advTm`
  - **PowerPoint writer:** a deck that has layouts is written with its own
    masters and layouts:
    - their names, types and backgrounds
    - their placeholders, with their look as a list style and their prompt
      text
    - the date and the slide number as fields
    - a blank layout for a master that has none
    - unique ids from 2^31, and a theme for each master

    Each slide links to its layout, and its placeholders name that
    layout's. A deck with no layouts still gets the standard master. A
    transition is written as `p:transition`. A length that no speed
    names goes in `p14:dur`, inside `mc:AlternateContent` with a fallback,
    as PowerPoint writes it.
  - **OpenDocument reader:**
    - **masters:** from the master pages: name, background, and the
      placeholder frames. Impress leaves these empty on its masters, so an
      empty one gets Impress's prompt text.
    - **layouts:** one for each presentation page layout, on the master
      that its slides use. Each layout is named and typed from its
      placeholders unless the file names it. A deck with no page layouts
      gets one layout per master page.
    - **transitions:** from the drawing-page style (`smil:type`, the speed,
      automatic advance with `presentation:duration`) and the page's
      `anim:transitionFilter`, which holds the exact length.
  - **OpenDocument writer:**
    - **masters:** written as master pages, with `<master>-title` /
      `-subtitle` / `-outline1..9` presentation styles. These styles are
      also the parents of the slides' frames.
    - **layouts:** written as presentation page layouts that carry the
      layout's name.
    - **slides:** each slide goes on its layout's master.
    - **transitions:** the style properties, plus the
      `anim:transitionFilter` that LibreOffice reads the length from.
  - **Empty placeholders:** both readers now keep a slide's empty
    placeholder. Before, they dropped it, so an editor had nothing to type
    into. The placeholder keeps its kind, its place, and one empty
    paragraph:
    - .pptx: the run's size comes from `a:endParaRPr`, else from the
      layout. A `<p:ph idx="N"/>` that names no type reads as `"body"`.
    - .odp: an empty title, subtitle or outline frame, and a
      `presentation:placeholder` frame, whose text is the prompt. The
      prompt text is not kept.
  - `PptxSlideFromLayout` gives a layout placeholder that has no place of
    its own the place of the master's placeholder of the same kind.
  - Tested by `PresentationLayoutsRoundTripTest`, on a deck with one master,
    two layouts and two transitions (Fade over 2 s that moves on after 5 s,
    and Push), saved as .pptx, .odp and .fodp and read back. The test also
    covers:
    - the package's parts, content types and ids
    - a second save of the deck that was read back
    - the standard set for a deck with no layouts
    - `PptxSlideFromLayout` on a deck that was read back
    - the LibreOffice-made `pptx-features` fixtures
    - a slide with an empty content placeholder, which is read back in its
      box from both formats
    - LibreOffice converting our .pptx to .odp and our .odp to .pptx, with
      layouts and transitions kept. This part is skipped when `soffice` is
      not installed.
