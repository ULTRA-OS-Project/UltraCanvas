- **DemoApp: sharp toolbar icons on the Vector Editing and WYSIWYG pages.**
  - *Vector Editing:* Undo and Redo used Texter's icons, drawn as thin
    filled outlines, beside ArtCreator's 2 px line icons. They now use the
    framework's shared line icons (`media/icons/menu/undo.svg`, `redo.svg`),
    the ones ArtCreator's own toolbar uses: 2 px strokes on whole pixels, like
    every other button on the page.
  - *WYSIWYG:* the page imitates Texter and keeps Texter's icons, but five of
    them were exports with fractional coordinates and hairline strokes, so at
    24 px the list bars fell between two pixel rows and the italic *i*, the
    numbers, the `{#}` and the `<>` were smeared. `md-italic`, `md-code`,
    `md-list-unordered`, `md-list-ordered` and `code2` in
    `media/icons/texter/` are redrawn on the 24 px grid in the weight of
    Texter's other icons - black, 1 px lines on whole pixels. Texter's
    markdown side bar uses four of them and is sharp too (Texter 1.65).
