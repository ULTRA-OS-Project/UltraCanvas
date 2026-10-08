- **A mouse press the element under the pointer does not take climbs to its
  parents.** The window handed `MouseDown`, `MouseUp` and `MouseDoubleClick`
  to the innermost interactive element under the pointer and, when that
  element did not take it, straight to the window - never to the elements
  around it, although the wheel, drags, touches and keys already climbed. So
  a container that acts on a click (a clickable card, a row, a tile) never
  heard of a click on the label, icon or inner row inside it, and answered
  only on its padding: UltraMail's account tiles switched the account from 1
  of 14 spots. A press now goes to the element's parent, then the
  grandparent, each with the pointer in its own local space, until one
  returns `true`. The climb:
  - stops below the window, which still gets a press nobody took, once, at
    the end - so a press in a popup (a child of the window) never reaches
    what lies under it;
  - stops at an element that left the tree, or was destroyed, while the
    press was being handled (a click that closed its row): the chain is
    taken before the first element runs, and `CleanupElementReferences`
    clears an element destroyed meanwhile, also during a modal dialog's
    nested event loop;
  - changes nothing for a press an element takes (returns `true`), which is
    what buttons, inputs, lists, sliders and menus do with their own clicks.
  Two elements were made ready for it, because a press they left would now
  have reached a parent that acts on it:
  - **`UltraCanvasTextInput` takes a double-click.** A fast second click
    arrives as `MouseDoubleClick`, which the input dropped; in the
    spreadsheet's cell editor it would have climbed to the sheet, which reads
    a double-click on a cell as "start editing" and rebuilt the editor from
    the stored text - losing what had been typed. It now selects the word
    under the pointer (all of a password field) and is a second press on the
    clear and reveal buttons, and a release over the field is the field's.
  - **`UltraCanvasFilerWidget` leaves alone a press on its own elements.** A
    right-click on the filter's "clear" button (which has no menu) would have
    reached the view as a right-click on the folder: it took the keyboard
    from the search field, committed an open rename and opened the folder
    menu, which then swallowed the next click.
  Gained by the change without code of their own: UltraMail's address-book
  sidebar answers a click on a group's name, and an
  UltraDesktop sticky note can be dragged by its grip (the grip is a label,
  and the board was written expecting its press to climb - it never did).
  Rules for element authors - return `true` for a press you acted on, and a
  container now sees the presses its children did not take - are in
  `UltraCanvasCoordinateSystemGuide.md`, *Which element gets a press*. Tests:
  `MouseClickBubblingTest` (a window under Xvfb; it skips without a display):
  7 of its 21 checks fail on the old code, and 3 Filer checks fail with the
  climb but without the Filer's guard.
