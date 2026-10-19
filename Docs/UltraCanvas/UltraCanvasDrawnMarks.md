# UltraCanvasDrawnMarks

`#include "UltraCanvasDrawnMarks.h"` — small marks drawn as shapes for
elements that paint themselves: a tick, a cross, a column header's sort
triangle and the list bullets "•", "◦", "▪".

## Why draw them

The bundled UI font is Ubuntu (`media/fonts`). It has "•", "–", "—", "…",
"‹", "›", "×" and "·", but no "✓", "✗", "✕", "◦", "▪", "▲", "▼", arrows or
emoji. Those characters come from whatever fallback font the system has,
and where it has none — a stock Windows install, or the bundled fonts
alone — each one shows as a box of hex digits. A drawn mark looks the
same everywhere and takes the colour it is given.

For a button or a menu item, use a flat SVG icon of `media/icons/menu/`
drawn as a mask instead (`SetUseIconAsMask(true)` on a button,
`MenuStyle::tintIcons` on a menu). In a sentence, use a word ("to",
"Back") or a character the font has.

## Functions

```cpp
inline void DrawTickMark(IRenderContext* ctx, const Point2Dd& centre, double size, const Color& color);
inline void DrawCrossMark(IRenderContext* ctx, const Point2Dd& centre, double size, const Color& color);
inline void DrawSortTriangle(IRenderContext* ctx, const Point2Dd& centre, double size, bool up,
                             const Color& color);
inline bool IsDrawnBullet(const std::string& bullet);
inline bool DrawBulletMark(IRenderContext* ctx, const std::string& bullet, const Point2Dd& topLeft,
                           const FontStyle& font, const Color& color);
```

* `DrawTickMark`, `DrawCrossMark` — a stroked tick or diagonal cross
  filling most of a `size` x `size` box centred on `centre`, with round
  caps; the stroke grows with the size (at least 1.5 px).
* `DrawSortTriangle` — a filled triangle `size` wide, pointing up when
  `up` is true, down otherwise.
* `DrawBulletMark` — draws "•" (a disc), "◦" (a ring) or "▪" (a square)
  where `DrawText(bullet, topLeft)` in `font` would put the character:
  centred on the font's capitals (`GetCapCentreOffset`) and as large as
  Ubuntu's "•". It returns false and draws nothing for any other string,
  which the caller then draws as text.
* `IsDrawnBullet` — true for the three bullets `DrawBulletMark` draws.
  Each takes the room of a "•", so measure "•" to place text after one.

Each function saves and restores the context's state.

## Example

A row of a self-rendered checklist: a tick or a cross, then the text, both
centred on the row.

```cpp
#include "UltraCanvasDrawnMarks.h"

void DrawChecklistRow(IRenderContext* ctx, const Rect2Dd& row, const std::string& text, bool done) {
    FontStyle font;
    font.fontSize = 12;
    const Point2Dd markCentre(row.x + 8, row.y + row.height / 2);
    if (done) {
        DrawTickMark(ctx, markCentre, 14, Color(40, 167, 69));
    } else {
        DrawCrossMark(ctx, markCentre, 14, Color(220, 53, 69));
    }
    ctx->SetFontStyle(font);
    ctx->SetTextPaint(Color(60, 60, 60));
    ctx->DrawText(text, Point2Dd(row.x + 22, ctx->TextTopCentredOnCaps(row, font)));
}
```

A list item whose bullet is one of the standard three, or any string:

```cpp
#include "UltraCanvasDrawnMarks.h"

void DrawListItem(IRenderContext* ctx, const std::string& bullet, const Point2Dd& lineTop,
                  const FontStyle& font, const std::string& text) {
    if (!DrawBulletMark(ctx, bullet, lineTop, font, Color(80, 80, 80))) {
        ctx->SetFontStyle(font);
        ctx->SetTextPaint(Color(80, 80, 80));
        ctx->DrawText(bullet, lineTop);
    }
    ctx->SetFontStyle(font);
    const double bulletWidth = ctx->GetTextLineWidth(IsDrawnBullet(bullet) ? std::string("\xE2\x80\xA2") : bullet);
    ctx->DrawText(text, Point2Dd(lineTop.x + bulletWidth + 8, lineTop.y));
}
```

## Where the framework uses them

* `UltraCanvasPasswordRuleLegend` — the met and unmet icons ("✓", "✗", and
  "✔", "✘", "✕", "×", "○", "●" when a config sets them).
* `UltraCanvasTextArea` (Markdown lists) and `UltraCanvasRichTextEdit` —
  the nested list bullets.
* `UltraCanvasFilerWidget` — the details header's sort direction.
* `UltraCanvasNewDocumentDialog` — the close cross and the selected item's
  tick.
