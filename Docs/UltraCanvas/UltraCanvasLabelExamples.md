# UltraCanvasLabel Documentation

## Overview

**UltraCanvasLabel** shows text: a caption beside a control, a heading, a
status line, a paragraph that wraps, text with links or with images flowing
in it. It measures its own text, so in a flex or grid layout it is as big as
its text unless it is given a size.

**Header:** `include/UltraCanvasLabel.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **Sizes itself to its text** in a layout (`MeasureOwnContent`,
  `ComputeIntrinsicSizes`); with word wrap on, its height follows the width
  the layout gives it.
- **Styling:** font, colour, alignment, wrap mode, line height, a text
  shadow (`LabelStyle`), plus everything every element has: background,
  borders and rounded corners, padding, margin.
- **Pango markup** (`SetTextIsMarkup`) and **text links** (`SetTextLinks`).
- **Inline images** flowing in the text like a browser's `<img>`.
- **Selectable text** (`SetSelectable`): drag to select, double-click a word,
  triple-click all of it, Ctrl+C to copy - and with an
  `UltraCanvasTextSelection` one selection across many labels, as in a
  browser.
- **Events:** click, hover, link activated and hovered, text changed.
- **Disabled look:** `disabledTextColor` while the label `IsDisabled()`.

## Constructors

```cpp
UltraCanvasLabel(const std::string& identifier, float x, float y, float w, float h,
                 const std::string& labelText = "");
UltraCanvasLabel(const std::string& identifier, float w, float h,
                 const std::string& labelText = "");
UltraCanvasLabel(const std::string& identifier, const std::string& labelText);
explicit UltraCanvasLabel(const std::string& labelText = "");
```

- A width or height greater than 0 is a **set size**: the label keeps it, and
  a container's stretch does not change it (see `Docs/CSSLayout.md`,
  "Stretching is a design decision").
- Pass no size (or -1) and the label is as big as its text, or as wide as a
  stretching container makes it.
- A non-zero `x` / `y` places the label at that position inside its parent
  instead of in the parent's flow (legacy absolute placement); in a flex or
  grid container leave them at 0, or use a constructor without them.

## LabelStyle

```cpp
struct LabelStyle {
    FontStyle fontStyle;                                   // family, size, weight
    Color textColor = Colors::Black;
    Color disabledTextColor = Color(178, 178, 184, 255);  // while IsDisabled()

    TextAlignment horizontalAlign = TextAlignment::Left;
    VerticalAlignment verticalAlign = VerticalAlignment::Middle;
    TextWrap wrap = TextWrap::WrapNone;    // WrapNone, WrapWord, WrapChar, WrapWordChar
    float lineHeightPx = 0.f;              // CSS line-height; 0 = the font's own

    bool hasShadow = false;                // the text drawn once more, offset, under it
    Color shadowColor = Color(0, 0, 0, 128);
    Point2Di shadowOffset = Point2Di(1, 1);
};
```

Background, borders, corner radius, padding and margin are not part of the
label's style: they belong to every element (`SetBackgroundColor`,
`SetBorders`, `SetBorderRadius`, `SetPadding`, `SetMargin`).

### Predefined styles

- `LabelStyle::DefaultStyle()`: the defaults above.
- `LabelStyle::HeaderStyle()`: 18 pt bold.
- `LabelStyle::SubHeaderStyle()`, `LabelStyle::CaptionStyle()`,
  `LabelStyle::StatusStyle()`: a sub-heading, a small muted caption, status
  bar text.

## Methods

### Text

```cpp
void SetText(const std::string& newText);   // re-measures, redraws, fires onTextChanged
const std::string& GetText() const;
void SetTextIsMarkup(bool markup);          // the text is Pango markup (<b>, <span color=...>)
```

To clear a label, `SetText("")`; to append, `SetText(label->GetText() + more)`.

### Style and font

```cpp
void SetStyle(const LabelStyle& newStyle);
const LabelStyle& GetStyle() const;
void SetFont(const std::string& fontFamily, float fontSize = 12.0f,
             FontWeight weight = FontWeight::Normal);
void SetFontSize(float fontSize);
void SetFontWeight(FontWeight weight);
void SetTextColor(const Color& color);
void SetAlignment(TextAlignment horizontal,
                  VerticalAlignment vertical = VerticalAlignment::Middle);
void SetWrap(TextWrap wrap);
```

Every setter that changes the text's size (font, wrap, text, markup)
re-measures the label and redraws it; a colour only redraws it. A shadow is
switched on through the style:

```cpp
LabelStyle style = label->GetStyle();
style.hasShadow = true;
style.shadowOffset = Point2Di(2, 2);
label->SetStyle(style);
```

### From UltraCanvasUIElement

```cpp
void SetBackgroundColor(const Color& color);
void SetBorders(float width, const Color& color = Colors::Black, float borderRadius = 0.0f,
                const UCDashPattern& dash = UCDashPattern());
void SetBordersColor(const Color& color);
void SetBorderRadius(float radius);              // rounded fill without a border
void SetPadding(float all);                      // also (vertical, horizontal)
void SetPadding(float top, float right, float bottom, float left);
void SetMargin(float all);                       // also (vertical, horizontal), (top, right, bottom, left)
```

### Text links

```cpp
struct LabelTextLink {
    int startByte = 0;
    int endByte = 0;      // exclusive
    std::string href;
};
void SetTextLinks(std::vector<LabelTextLink> links);
const std::vector<LabelTextLink>& GetTextLinks() const;
void SetShowLinkTooltips(bool show);
int LinkIndexAtPoint(const Point2Di& localPoint);
```

Byte offsets refer to the text as laid out: markup parsed, entities decoded.
A click on a link fires `onLinkActivated` with its `href`.

### Inline Images

An image can flow in a label's text the way a browser flows an inline
`<img>`. Put a U+FFFC placeholder (`"\xEF\xBF\xBC"`, 3 bytes) in the text
where the image goes, and give the label the image, its display size and the
placeholder's byte offset in the *rendered* text. In markup mode, that is the
text after the markup is removed.

```cpp
const std::string text = "Rated \xEF\xBF\xBC out of five";
label->SetText(text);

LabelInlineImage star;
star.byteOffset = 6;                    // where the placeholder starts
star.width = 48;  star.height = 18;     // display size in px
star.image = UCImageRaster::Load("star.png");
label->SetInlineImages({ star });
```

- The label reserves the image's box on the placeholder
  (`TextAttributeFactory::CreateShape`) and draws the image there. A line
  holding an image grows to fit it.
- `align` (`LabelInlineImageAlign`) says where the image sits against the
  text of its line, like CSS `vertical-align`:
  - `Baseline` (the default): it stands on the baseline.
  - `Middle`: centred on the x-height.
  - `Top`: its top at the top of the text.
  - `Bottom`: its bottom at the bottom of the text, just under the baseline.

  The text's ascent and descent are measured from the label's own font.
- An image wider than the label's line is scaled down to fit, keeping its
  aspect ratio.
- `fit` (`ImageFitMode`, default `Fill`) and `position` (`ImagePosition`,
  default centred) say how the picture fills its `width` x `height` box, like
  CSS `object-fit` / `object-position`: `Fill` stretches it to the box;
  `Contain`, `Cover`, `NoScale` and `ScaleDown` keep its shape and place it by
  `position`, clipped to the box.
- `frame` (`LabelInlineImageFrame`) is the CSS box around the picture:
  margins, padding, four border sides (`borderTop` ... `borderLeft`, each a
  `LabelInlineImageBorder`: width, colour, dash; `SetBorders(w, colour)` for
  one all round), `borderRadius` and `background`. The line reserves the whole margin box; the background fills
  the border box, the border is drawn inside it, and the picture is clipped to
  the rounded corners. The default frame is empty: the picture alone.
- `InlineImageRect(i)` returns where image `i`'s picture is drawn (its content
  box), and `InlineImageBoxRect(i)` its border box, in label-local
  coordinates.
- A placeholder inside a `SetTextLinks` range is part of that link, so
  clicking the image activates it.

The HTML reader (`HTMLElementBuilder`) uses this for an `<img>` inside running
text. Images in a block that has no text of its own go on lines of their own,
placed by `text-align`: side by side on one wrapping line while only
whitespace separates them, a space of their font apart where the HTML has
whitespace, standing on the line's bottom unless `vertical-align` puts them at
its top or middle; `display:block` or `<br>` starts a new line.

### Selectable text

A label's text can be selected and copied the way text on a web page can.
It is off by default - a caption beside a control should not take the press
meant for the control.

```cpp
void SetSelectable(bool selectable);   // a selection of its own
bool IsSelectable() const;
void SetTextSelection(std::shared_ptr<UltraCanvasTextSelection> selection);   // a shared one
const std::shared_ptr<UltraCanvasTextSelection>& GetTextSelection() const;

void SetSelectedRange(int startByte, int endByte);   // an end past the text = its end
void ClearSelectedRange();
bool HasSelectedRange() const;
int GetSelectionStart() const;
int GetSelectionEnd() const;
std::string GetSelectedText();     // as a reader copies it
std::string GetRenderedText();     // markup parsed, entities decoded
int TextIndexAtPoint(const Point2Df& localPoint);   // nearest text position, -1 before the first layout
std::pair<int, int> WordRangeAt(int byteIndex);     // [first, second)
```

On a selectable label:

- **A drag selects**, a **double-click** takes the word under the pointer (a
  dot or apostrophe between letters stays in it: `example.com`, `don't`), a
  **triple-click** the whole text, and **Shift+click** extends the selection.
- **Ctrl+C** (or Ctrl+Insert, or Cmd+C) copies, **Ctrl+A** selects all. The
  pressed label takes the keyboard focus for them; only that one label of a
  selection accepts the focus, so a page of paragraphs is one stop in the Tab
  order, not one per paragraph.
- The highlight is drawn under the text in `LabelStyle::selectionColor`.
- `GetSelectedText()` leaves out the U+FFFC placeholders of inline images and
  soft hyphens, and copies a no-break space as a plain space.
- A **text link** opens when the button is released on it without having
  dragged, rather than on the press - otherwise a drag that starts on a link
  could never select it. A label with `onClick` stays a button: a press on it
  clicks, it does not select.
- The pointer is the text I-beam over the label (the hand over a link).
- Byte offsets are those of the rendered text, as for links and inline
  images. `SetText` and `SetTextIsMarkup` clear the selection.

```cpp
auto address = CreateLabel("address", "anna.berg@example.com");
address->SetSelectable(true);   // the reader can copy the address
```

#### One selection across many labels

`UltraCanvasTextSelection` (`UltraCanvasTextSelection.h`) is a selection that
several labels share, in reading order. A drag that starts in one label runs
on through the next ones, and a copy joins their parts: a line break between
labels above one another, a tab between labels side by side (the cells of a
table row). Create it with `std::make_shared` - every label keeps it alive,
while it holds the labels weakly and a label leaves it as it is destroyed.

```cpp
auto selection = std::make_shared<UltraCanvasTextSelection>();
selection->AddLabel(*subject);          // the order added is the reading order
selection->AddLabel(*sender);
selection->AddLabelsIn(*articleRoot);   // every label below, depth first

selection->onContextMenu = [](const UCEvent& event) {
    // A right-click on one of the labels: show Copy / Select All.
};
bool any = selection->HasSelection();
std::string text = selection->GetSelectedText();
selection->CopyToClipboard();
selection->SelectAll();
selection->ClearSelection();
```

- With the pointer between labels, the selection runs to the nearest text: a
  label beside the pointer on its line, otherwise the end of the last label
  above it. Dragged past the top or bottom of the scroll view the labels sit
  in, that view scrolls, faster the further the pointer is past its edge.
- A right-click inside the selection keeps it (for the menu's Copy); outside
  it, it clears it first. `onContextMenu` is then called - a right-click on
  a selectable label never opens a link. `onSelectionChanged` is raised
  whenever what is selected changes.
- `HTML::BuildOptions::selectableText` gives every label of a tree built from
  HTML one such selection (`HTML::BuildResult::textSelection`); UltraMail
  selects a mail's body that way.

## Event Callbacks

```cpp
std::function<void()> onClick;                           // a label with one shows the hand cursor
std::function<void()> onHoverEnter;
std::function<void()> onHoverLeave;
std::function<void(const std::string&)> onTextChanged;    // after SetText changed the text
// Text links (SetTextLinks): the clicked link's href, and the hovered one's
// as the pointer moves onto it ("" as it leaves the link).
std::function<void(const std::string&)> onLinkActivated;
std::function<void(const std::string&)> onLinkHovered;
```

In a selectable label (`SetSelectable`) `onLinkActivated` fires when the
button is released on the link it was pressed on, without a drag that
selected text in between.

`SetShowLinkTooltips(true)` also shows the hovered link's href in a tooltip
beside the pointer, following it along the link and hidden again as the
pointer leaves the link. It is off by
default, for an app that shows the address in its status line instead
(`HTML::BuildOptions::linkTooltips` sets it on the labels the HTML reader
builds).

### Example Event Handling

```cpp
label->onClick = []() {
    std::cerr << "Label clicked!" << std::endl;
};

label->onTextChanged = [](const std::string& newText) {
    std::cerr << "Text changed to: " << newText << std::endl;
};
```

## Factory Functions

```cpp
std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& identifier,
                                              float x, float y, float w, float h,
                                              const std::string& text = "");
std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& identifier,
                                              float w, float h, const std::string& text = "");
std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& identifier,
                                              const std::string& text);   // sized by its text
std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& text);
```

A label that sizes itself to its text is simply one created without a size:
`CreateLabel("status", "Ready")`. A heading or a status line is a label with
the predefined style: `label->SetStyle(LabelStyle::HeaderStyle())`.

## Builder

```cpp
auto label = LabelBuilder("greeting")
    .SetText("Hello World")
    .SetFont("Sans", 14.0f)
    .SetTextColor(Colors::Blue)
    .SetBackgroundColor(Color(240, 240, 240))
    .SetAlignment(TextAlignment::Center)
    .SetPadding(10.0f)
    .OnClick([]() { std::cerr << "Clicked!" << std::endl; })
    .Build();
```

`LabelBuilder` also has `SetStyle(const LabelStyle&)`.

## Usage Examples

### Basic label

```cpp
auto basicLabel = CreateLabel("basic", "Simple Text");
container->AddChild(basicLabel);
```

### Header

```cpp
auto header = CreateLabel("header", "Application Title");
header->SetStyle(LabelStyle::HeaderStyle());
header->SetTextColor(Color(0, 100, 200));
container->AddChild(header);
```

### Status indicators

```cpp
auto MakeStatus = [](const std::string& id, const std::string& text, Color fill, Color ink) {
    auto label = CreateLabel(id, text);
    label->SetBackgroundColor(fill);
    label->SetTextColor(ink);
    label->SetAlignment(TextAlignment::Center);
    label->SetPadding(4.0f, 10.0f);
    return label;
};
row->AddChild(MakeStatus("success", "\u2713 Success", Color(200, 255, 200), Color(0, 150, 0)));
row->AddChild(MakeStatus("warning", "\u26A0 Warning", Color(255, 255, 200), Color(200, 150, 0)));
row->AddChild(MakeStatus("error", "\u2717 Error", Color(255, 200, 200), Color(200, 0, 0)));
```

### Text that wraps

```cpp
auto paragraph = CreateLabel("paragraph",
    "A label with word wrap wraps at the width its container gives it, "
    "and its height follows.");
paragraph->SetWrap(TextWrap::WrapWord);
paragraph->SetBackgroundColor(Color(245, 245, 245));
paragraph->SetBorders(1.0f, Color(200, 200, 200));
paragraph->SetPadding(10.0f);
column->AddChild(paragraph);   // in a column it is as wide as its text, up to the column's width
```

### Rounded Corner Labels

Rounded corners are produced by passing a **border radius** as the third
argument to `SetBorders(width, color, radius)`. The radius is applied to the
background fill as well as the border stroke, so labels can be rounded **with**
a visible border or **without** one (by making the border color transparent).

For a rounded box with **no border at all** - no border width, so no space
reserved for one - call `SetBorderRadius(radius)` (on any element): the
background is filled with that corner radius.

```cpp
auto chip = std::make_shared<UltraCanvasLabel>("chip", 120, 28);
chip->SetText("Borderless");
chip->SetBackgroundColor(Color(20, 20, 19));
chip->SetTextColor(Colors::White);
chip->SetBorderRadius(10.0f);   // rounded fill, border widths stay 0
```

```cpp
// Rounded WITH a visible border (filled).
auto roundedFilled = std::make_shared<UltraCanvasLabel>(
    "roundedFilled", 200, 32);
roundedFilled->SetText("Rounded + Border");
roundedFilled->SetBackgroundColor(Color(225, 240, 255));
roundedFilled->SetTextColor(Color(0, 90, 170));
roundedFilled->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
roundedFilled->SetBorders(1.5f, Color(0, 120, 215), 10.0f); // width, color, radius

// Outline-only pill: transparent background, visible border, large radius.
auto roundedOutline = std::make_shared<UltraCanvasLabel>(
    "roundedOutline", 200, 32);
roundedOutline->SetText("Outlined Pill");
roundedOutline->SetTextColor(Color(120, 60, 160));
roundedOutline->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
roundedOutline->SetBorders(2.0f, Color(150, 90, 200), 16.0f); // radius >= h/2 -> pill

// Rounded WITHOUT a visible border: use a transparent border color so the
// rounded fill is kept but no stroke is drawn.
auto pillSuccess = std::make_shared<UltraCanvasLabel>(
    "pillSuccess", 150, 28);
pillSuccess->SetText("✓ Success");
pillSuccess->SetBackgroundColor(Color(76, 175, 80));
pillSuccess->SetTextColor(Colors::White);
pillSuccess->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
pillSuccess->SetBorders(1.0f, Colors::Transparent, 14.0f);

// Rounded multi-line card with a soft fill and no visible border.
auto roundedCard = std::make_shared<UltraCanvasLabel>(
    "roundedCard", 450, 90);
roundedCard->SetText("This rounded card has no visible border —\n"
                     "just a soft filled background with rounded corners.");
roundedCard->SetWrap(TextWrap::WrapWord);
roundedCard->SetBackgroundColor(Color(232, 244, 253));
roundedCard->SetBorders(1.0f, Colors::Transparent, 12.0f);
roundedCard->SetPadding(12.0f);
```

> **Tip:** A radius equal to or greater than half the label height produces a
> fully rounded "pill" shape. The renderer automatically clamps the radius so
> corners never overlap.

### A label that follows its text

```cpp
auto counter = CreateLabel("counter", "Click me");   // no size: it grows with its text
counter->onClick = [label = counter.get()]() {
    static int clicks = 0;
    label->SetText("Clicked " + std::to_string(++clicks) + " times");
};
```

### Label with a shadow

```cpp
auto shadowLabel = CreateLabel("shadow", "Text with Shadow");
LabelStyle style = shadowLabel->GetStyle();
style.fontStyle.fontSize = 18;
style.textColor = Colors::White;
style.hasShadow = true;
style.shadowOffset = Point2Di(2, 2);
shadowLabel->SetStyle(style);
shadowLabel->SetBackgroundColor(Color(100, 100, 100));
```

## Rendering

1. The background, borders and rounded corners are drawn by the element.
2. The text layout (`ITextLayout`) is built from the text, the style and the
   content width, and cached until one of them changes.
3. With `hasShadow`, the text is drawn once at `shadowOffset` in
   `shadowColor`, then in its own colour; inline images are drawn on their
   placeholders.

### Where the text layout comes from

The label caches an `ITextLayout` and builds it from a render context. At paint
time that is **the context passed to `Render()`**, and only failing that the one
reachable through the element's window. The distinction matters for a label
that is drawn without a window behind it — into an offscreen surface from
`CreateRenderContext(size, nullptr)`, the way the QR code plugin exports a PNG —
where there is no window to ask and the caller's context is the only one there
is.

Sizing happens earlier, outside any paint: `ComputeIntrinsicSizes()` has no
context to be handed and falls back to the window's, so a label measured before
it is attached simply reports zero and is measured again once it can be. A
label whose layout could not be built draws its background, border and focus
ring and skips its words; it does not fail the paint.

## Notes

- **Layout:** in a flex or grid container a label without a size measures
  its text; with `WrapWord` its height is the wrapped text's at the width it
  gets. Give it a width only when it really should have that width.
- **Thread safety:** UI thread only, like every element.
- **See also:** [UltraCanvasLabelPlacement](UltraCanvasLabelPlacement.md),
  [CSS layout](../CSSLayout.md),
  [UltraCanvasTextInput](UltraCanvasTextInputExamples.md),
  [UltraCanvasButton](UltraCanvasButtonExamples.md).
