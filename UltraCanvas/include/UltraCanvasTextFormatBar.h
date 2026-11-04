// include/UltraCanvasTextFormatBar.h
// UltraCanvasTextFormatBar - the bar a program shows over text it lets the
// user set: Font (every installed family, each shown in itself), Size, Bold,
// Italic, Underline, Strikethrough, Align left / centre / right, Line spacing
// and Letter spacing, in one row. ArtCreator, UltraPaint and UltraVideo show
// it while their text tool is active, over the text typed on the canvas or
// the frame (an UltraCanvasRichTextEdit lying on it).
//
// The bar edits a TextFormat and reports each change with the attribute that
// changed; it never touches the text itself. What a change goes to is the
// host's: the selected words of the text being typed, the whole text, the
// selected objects. TextFormatCharDelta turns a character attribute into the
// editor's RichCharFormatDelta, and TextFormatAtCaret reads the editor's
// format at its caret back into a TextFormat, so a host with a rich text
// editor needs a few lines for both directions.
//
// A host whose renderer cannot draw an attribute hides it
// (SetAttributeVisible), so the bar never offers what the result would not
// show. Sizes are in the host's unit (SetSizeUnit: pt in a drawing, px in a
// bitmap); letter spacing is in the same unit unless it is given its own.
//
// Version: 1.1.0 - letter spacing is a character attribute: the selected words take it;
//                  HasKeyboardFocus: a host's shortcuts leave the keys to a control of the bar;
//                  the Font list is made again when the fonts change (the system fonts
//                  arriving after the start - on Windows the list held the bundled fonts alone)
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasRichDocumentEditor.h"   // RichCharFormatDelta, RichCharFormatState
#include "UltraCanvasTheme.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasButton;
class UltraCanvasDropdown;
class UltraCanvasImageElement;
class UltraCanvasSegmentedControl;
class UltraCanvasSeparator;
class UltraCanvasSpinner;

enum class TextFormatAlign { Left = 0, Centre = 1, Right = 2 };

enum class TextFormatAttribute {
    Font, Size, Bold, Italic, Underline, Strikethrough, Align, LineSpacing, LetterSpacing
};

// Font to Strikethrough and the letter spacing are the character attributes:
// a part of a text can have its own. Alignment and the line spacing belong
// to the whole text (to its paragraphs).
inline bool IsCharacterAttribute(TextFormatAttribute attribute) {
    return attribute == TextFormatAttribute::Font || attribute == TextFormatAttribute::Size ||
           attribute == TextFormatAttribute::Bold || attribute == TextFormatAttribute::Italic ||
           attribute == TextFormatAttribute::Underline || attribute == TextFormatAttribute::Strikethrough ||
           attribute == TextFormatAttribute::LetterSpacing;
}

struct TextFormat {
    std::string fontFamily = "Sans";
    float size = 24.0f;                  // in the bar's size unit
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    TextFormatAlign align = TextFormatAlign::Left;
    float lineSpacing = 120.0f;          // % of the size from one baseline to the next
    float letterSpacing = 0.0f;          // added between two letters, in the letter spacing unit
};

// The change a character attribute of `format` makes in an
// UltraCanvasRichTextEdit (ApplyCharFormat): `pointsPerUnit` turns the bar's
// size and letter spacing units into the editor's points (0.75 for pixels, 1
// for points). An empty delta for alignment and the line spacing, which are
// the whole text's.
RichCharFormatDelta TextFormatCharDelta(TextFormatAttribute attribute, const TextFormat& format,
                                        float pointsPerUnit);

// What the bar shows for the caret (or the selected words) of an editor:
// `base` - the text's own format - with what the runs there set of their
// own (RichTextEdit::GetFormatState). A mixed attribute shows the base's.
TextFormat TextFormatAtCaret(const RichCharFormatState& state, const TextFormat& base, float pointsPerUnit);

class UltraCanvasTextFormatBar : public UltraCanvasContainer {
public:
    explicit UltraCanvasTextFormatBar(const std::string& identifier, float height = 50.0f);
    ~UltraCanvasTextFormatBar() override;

    // Shows a format; no onFormatChanged.
    void SetFormat(const TextFormat& format);
    const TextFormat& GetFormat() const { return format; }

    // Leaves an attribute's control out (a renderer that cannot draw it).
    void SetAttributeVisible(TextFormatAttribute attribute, bool visible);
    bool IsAttributeVisible(TextFormatAttribute attribute) const;

    // The size field's unit, range, decimals and arrow step; letter spacing
    // takes the same unit unless SetLetterSpacingUnit gives it its own.
    void SetSizeUnit(const std::string& suffix, double minimum, double maximum, int decimals, double step);
    void SetLetterSpacingUnit(const std::string& suffix, double minimum, double maximum, int decimals, double step);

    // The Font list again from the fonts installed now (one installed
    // meanwhile joins it). Each family is shown in itself; the list prepares
    // those previews in idle slices once the bar is on screen. The bar does
    // this itself when the fonts the text engine knows change
    // (GetFontSetGeneration) - the system fonts that arrive after the start
    // on Windows, a font file registered.
    void RefreshFonts();

    // Laid out again: the window does this to every element after the fonts
    // changed, and the bar then makes its Font list again.
    void InvalidateLayout() override;
    const std::vector<std::string>& GetFontFamilies() const { return families; }

    // The bar's ground, ink and controls from a theme (a dark program's bar
    // is dark; the icons take the ink).
    void ApplyTheme(const UltraCanvasTheme& theme) override;

    // A control of the bar has the keyboard: a size or a spacing being
    // typed, a font looked up by its first letters. A host's own shortcuts
    // leave the keys to it then - a shortcut that finished the text being
    // typed would send what is typed next to the whole text instead of the
    // selected words.
    bool HasKeyboardFocus() const;

    // An attribute was changed by the user; `format` is the whole new format.
    std::function<void(TextFormatAttribute attribute, const TextFormat& format)> onFormatChanged;
    // A control of the bar has finished with the keyboard (a choice made, a
    // list closed, a field left): a host typing a text gives it back to the
    // text. Called after the event, as a list reports its closing from
    // inside the event that closed it.
    std::function<void()> onControlDone;

private:
    void Build(float height);
    void Changed(TextFormatAttribute attribute);
    void ControlDone();
    void ShowFormat();
    void FillFontList();
    std::shared_ptr<UltraCanvasUIElement> ControlOf(TextFormatAttribute attribute) const;

    TextFormat format;
    std::vector<std::string> families;
    std::string missingFamily;           // shown at the top of the list: not installed
    bool showing = false;                // ShowFormat is setting the controls
    unsigned long fontGeneration = 0;    // GetFontSetGeneration when the list was made
    bool fontRefreshQueued = false;
    bool letterUnitOwn = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    std::shared_ptr<UltraCanvasDropdown> fontBox;
    std::shared_ptr<UltraCanvasSpinner> sizeBox, lineSpacingBox, letterSpacingBox;
    std::shared_ptr<UltraCanvasButton> boldButton, italicButton, underlineButton, strikeButton;
    std::shared_ptr<UltraCanvasSegmentedControl> alignSwitch;
    std::shared_ptr<UltraCanvasImageElement> lineSpacingIcon, letterSpacingIcon;
    std::vector<std::shared_ptr<UltraCanvasSeparator>> rules;
};

inline std::shared_ptr<UltraCanvasTextFormatBar> CreateTextFormatBar(const std::string& identifier,
                                                                     float height = 50.0f) {
    return std::make_shared<UltraCanvasTextFormatBar>(identifier, height);
}

} // namespace UltraCanvas
