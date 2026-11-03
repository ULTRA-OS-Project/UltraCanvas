# UltraCanvasTextFormatBar

## What it is

The bar a program shows over text the user sets on a canvas, a page or a video
frame: **Font** (every installed family, each shown in itself), **Size**,
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
lies and applies the bar to it. A character attribute (Font to Strikethrough)
goes to the selected words through `TextFormatCharDelta`; alignment and the
spacings belong to the whole text. `TextFormatAtCaret` gives the bar the format
at the editor's caret, and `onControlDone` hands the keyboard back to the text
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

Every family `GetInstalledFontFamilies()` reports, after the generic "Sans",
"Serif" and "Monospace", each drawn in itself (`DropdownItem::fontFamily`). The
dropdown prepares those previews lazily: once the bar is on screen, in small
idle slices, the rows the list opens on first - see *A font list* in
[UltraCanvasDropDownExamples.md](UltraCanvasDropDownExamples.md). A format
naming a font this computer lacks shows it at the top of the list, marked as
not installed. `RefreshFonts()` reads the installed fonts again.

## Look

The bar takes its ground, ink and controls from a theme
(`ApplyTheme(UltraCanvasTheme)`; the current theme when it is built): in a dark
program it is dark, and its icons (`media/icons/textformat/`) take the ink. A
host may still set its own ground with `SetBackgroundColor`.
