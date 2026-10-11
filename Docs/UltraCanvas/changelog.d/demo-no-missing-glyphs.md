- **The DemoApp shows no box of hex digits on Windows.** Its labels and
  buttons wrote characters the bundled Ubuntu UI font does not have - "✓",
  "✗", "⚠", arrows, "◀ ▶ ▲ ▼ ▾ ⋮" and emoji - in 117 places over 27 pages,
  and with the bundled fonts alone each drew as a box. Now:
  - the status bar reads "Status: Fully Implemented" without a mark, and the
    Label page's status pills say "Success", "Warning", "Error";
  - the "✓ feature" lists of the CDR, XAR, EPS, DWG, STL and Tabs pages are
    "• feature" lists;
  - buttons carry flat icons of `media/icons/menu/` in their text colour
    (`SetFlatButtonIcon` in `UltraCanvasDemo.h`): Open, Save, Vectorize, Reset,
    Run OCR, Full Size, Export, Prev / Next, the PDF search arrows, "Format
    Cells", the sidebar toolbar and the Dependencies page's link menu;
  - the Domain Dashboard draws its ticks, crosses and three-dot menus;
  - arrows in sentences are words ("Raster to SVG").
  `UltraCanvasDemo.h` says so for the next example: a label or a button holds
  only characters the bundled UI font has.
- **Eleven more icons in `media/icons/menu/`**: chevron-left, chevron-right,
  chevron-up, chevron-down, reset, wand, zoom-in, users, chart, globe and code.
