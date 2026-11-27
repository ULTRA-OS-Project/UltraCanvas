# UltraCanvasTextFormatBar

## What it is

The bar a program shows over text the user sets on a canvas, a page or a video
frame: **Font** (an [UltraCanvasFontPicker](UltraCanvasFontPicker.md): the
fonts the text uses, the ones chosen last and every installed family, each
shown in itself, with the styles beyond bold and italic beside a family that
has them), **Size**,
**Bold**, **Italic**, **Underline**, **Strikethrough**, **Align** left / centre
/ right, **Line spacing** (% of the size) and **Letter spacing**, in one row.
ArtCreator, UltraPaint and UltraVideo show it while their text tool is active.

It edits a `TextFormat` and reports every change with the attribute that
changed. It never touches the text itself: what a change goes to - the selected
words of the text being typed, the whole text, the selected objects - is the
host's to decide.

## A host typing on a canvas

The usual host - a window class, whose members the callbacks reach through
`this` - lays an `UltraCanvasRichTextEdit` over its canvas where the text
lies and applies the bar to it. A character attribute (Font to Strikethrough,
and the letter spacing) goes to the selected words through
`TextFormatCharDelta`; alignment and the line spacing belong to the whole text.
`TextFormatAtCaret` gives the bar the format at the editor's caret (a word's own
letter spacing too), and `onControlDone` hands the keyboard back to the text
once a control of the bar is finished with it.

<!-- doc-check: std::shared_ptr<UltraCanvasRichTextEdit> editor; std::shared_ptr<UltraCanvasContainer> window; TextFormat textBase; void ApplyToWholeText(TextFormatAttribute attribute, const TextFormat& format); -->
```cpp
auto bar = CreateTextFormatBar("text-bar");
bar->SetSizeUnit(" px", 1, 2000, 0, 1);            // a bitmap editor's sizes
window->AddChild(bar);

bar->onFormatChanged = [this](TextFormatAttribute attribute, const TextFormat& format) {
    if (IsCharacterAttribute(attribute) && editor->HasSelection()) {
        // The selected words take it (0.75 pt a pixel).
        editor->ApplyCharFormat(TextFormatCharDelta(attribute, format, 0.75f));
    } else {
        ApplyToWholeText(attribute, format);       // the host's own
    }
};
bar->onControlDone = [this]() { editor->SetFocus(true); };

// The bar follows the caret.
editor->onSelectionChanged = [this, bar = bar.get()]() {
    bar->SetFormat(TextFormatAtCaret(editor->GetFormatState(), textBase, 0.75f));
};
```

## The keyboard

A size typed into the bar's field, or a font looked up in its list by its first
letters, is typed into the bar: a host's own shortcuts must leave those keys
alone. A shortcut that ran instead - Ctrl+A as Select All - finished the text
being typed first, and the size then went to the whole text rather than the
selected words. `HasKeyboardFocus()` says when the keys are the bar's:

<!-- doc-check: std::shared_ptr<UltraCanvasTextFormatBar> bar; std::shared_ptr<UltraCanvasRichTextEdit> editor; bool RunShortcut(const UCEvent& e); -->
```cpp
auto shortcuts = [&](const UCEvent& e) -> bool {
    if (bar->IsVisible() && bar->HasKeyboardFocus()) {
        if (e.virtualKey == UCKeys::Escape) {   // back to the text
            editor->SetFocus(true);
            return true;
        }
        return false;                           // the field's or the list's
    }
    return RunShortcut(e);                      // the host's own
};
```

## What a host cannot draw

A host whose renderer cannot draw an attribute hides its control, so the bar
never offers what the result would not show:

<!-- doc-check: std::shared_ptr<UltraCanvasTextFormatBar> bar; -->
```cpp
bar->SetAttributeVisible(TextFormatAttribute::LetterSpacing, false);
bar->SetAttributeVisible(TextFormatAttribute::Underline, false);
```

The rule between two groups goes with them when a whole group is hidden.

## Units

Sizes are in the host's unit: `SetSizeUnit(suffix, minimum, maximum, decimals,
step)` - points in a drawing, pixels in a bitmap. Letter spacing takes the same
unit unless `SetLetterSpacingUnit` gives it its own. Line spacing is always a
percentage of the size from one baseline to the next (120 % is the common
default).

## The Font list

The Font list is an [UltraCanvasFontPicker](UltraCanvasFontPicker.md)
(`GetFontPicker()`), the closed box showing the text's family in itself. Open,
it is wider than the box and has three sections:

- **In this document** - the families the host's document uses, which it
  hands over in `onFontListOpening` with `SetDocumentFonts`.
  `AddTextFormatFonts(families, base, words)` gathers them, one text after
  another: a text's own family and the ones its words set. A family this
  computer lacks is marked so, in grey italics; the text's own family is
  there too when it is not installed.
- **Recent** - the fonts chosen last, newest first. The host reads them from
  its settings once (`SetRecentFonts`) and saves them in
  `onRecentFontsChanged`, so each program keeps its own.
- **All fonts** - the generic "Sans", "Serif" and "Monospace", then every
  installed family A to Z (`GetInstalledFontCatalogue()`), each drawn in
  itself with a small icon for its format. The pictures are made in idle
  slices once the list opens.

Typing with the list open jumps to the family whose name starts with what was
typed. The keyboard focus stays with the text being typed meanwhile, so the
selected words stay selected (`HasKeyboardFocus()` is true while the list is
open).

A family with styles bold and italic cannot reach - Light, SemiBold,
Condensed, Black - has an arrow to its styles. The bar has a Bold and an
Italic button but no weight of its own, so a style chosen there is turned
into what a text with bold and italic keeps: the face's own family name where
the text engine reaches the face by it ("Ubuntu Light", "Ubuntu Thin", with
Bold on where that name's bold is the face), else the family with Bold for a
weight of 600 or more; Italic from the style's slant. It reaches
`onFormatChanged` as a `Font` change followed by `Bold` and `Italic` where
those changed, one call each. A family of Regular, Bold, Italic and Bold
Italic alone has no arrow: the B and I buttons are its styles.

<!-- doc-check: std::shared_ptr<UltraCanvasTextFormatBar> bar; std::vector<std::shared_ptr<const UCRichDocument>> texts; std::vector<std::string> savedRecentFonts; void SaveRecentFonts(const std::vector<std::string>& fonts); -->
```cpp
bar->onFontListOpening = [&]() {
    std::vector<std::string> fonts;
    for (const auto& words : texts) AddTextFormatFonts(fonts, "Sans", words.get());
    bar->SetDocumentFonts(fonts);
};
bar->SetRecentFonts(savedRecentFonts);              // from the program's settings
bar->onRecentFontsChanged = [](const std::vector<std::string>& fonts) { SaveRecentFonts(fonts); };
```

`RefreshFonts()` reads the installed fonts again, and the bar does that itself
when the fonts the text engine knows change (`GetFontSetGeneration()`): a font
file registered, or on Windows the system fonts arriving after a start with a
cold font cache - the list made with the window held the bundled fonts alone
until then. `GetFontFamilies()` names what "All fonts" lists.

## Narrow places

The controls take about 860 pixels; the Font list's closed box 210 of them. In
a narrower place the box gives up width first, down to 90 pixels, so every
other control stays in reach; the list it opens stays as wide as a name needs.

## Look

The bar takes its ground, ink and controls from a theme
(`ApplyTheme(UltraCanvasTheme)`; the current theme when it is built): in a dark
program it is dark, and its icons (`media/icons/textformat/`) take the ink. A
host may still set its own ground with `SetBackgroundColor`.
