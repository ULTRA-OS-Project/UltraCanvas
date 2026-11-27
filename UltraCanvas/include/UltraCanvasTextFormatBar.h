// include/UltraCanvasTextFormatBar.h
// UltraCanvasTextFormatBar - the bar a program shows over text it lets the
// user set: Font (an UltraCanvasFontPicker: every installed family, each
// shown in itself with its format's icon, the fonts the text uses and the
// ones chosen last above them, and the styles beyond bold and italic -
// Light, Condensed, Black - beside a family that has them), Size, Bold,
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
// Version: 1.2.0 - the Font list is an UltraCanvasFontPicker: "In this document" (SetDocumentFonts,
//                  onFontListOpening), "Recent" (SetRecentFonts, onRecentFontsChanged: the host
//                  keeps them in its settings), all fonts with their format icons, and the styles
//                  list beside a family with styles B and I cannot reach - a style chosen sets
//                  the family (the face's own name where it has one) and Bold and Italic;
//                  AddTextFormatFonts gathers the families a text names
// Version: 1.1.0 - letter spacing is a character attribute: the selected words take it;
//                  HasKeyboardFocus: a host's shortcuts leave the keys to a control of the bar;
//                  the Font list is made again when the fonts change (the system fonts
//                  arriving after the start - on Windows the list held the bundled fonts alone)
// Version: 1.0.0
// Last Modified: 2026-11-27
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
class UltraCanvasFontPicker;
struct FontPickerChoice;
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

// The families a text names - `base`, its own, and those the runs of
// `words` (may be null) set - added to `families` where they are not yet,
// in the order met: what a host hands SetDocumentFonts, one text after
// another.
void AddTextFormatFonts(std::vector<std::string>& families, const std::string& base, const UCRichDocument* words);

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
    // meanwhile joins it): the generic names (Sans, Serif, Monospace) first,
    // then every installed family A to Z. Each is shown in itself; the list
    // prepares those pictures in idle slices once it opens. The bar does
    // this itself when the fonts the text engine knows change
    // (GetFontSetGeneration) - the system fonts that arrive after the start
    // on Windows, a font file registered.
    void RefreshFonts();

    // The Font list's "In this document": the families the host's document
    // uses (AddTextFormatFonts gathers them), in its order. The text's own
    // family is there too when it is not installed. Set it in
    // onFontListOpening, so it is current each time the list opens.
    void SetDocumentFonts(const std::vector<std::string>& families);
    const std::vector<std::string>& GetDocumentFonts() const { return documentFonts; }
    // The Font list's "Recent": the fonts chosen last, newest first - the
    // host reads them from its settings at the start and saves them in
    // onRecentFontsChanged.
    void SetRecentFonts(const std::vector<std::string>& families);
    const std::vector<std::string>& GetRecentFonts() const;
    // The Font list itself, for a host that words or styles it otherwise
    // and for a test.
    UltraCanvasFontPicker* GetFontPicker() const { return fontBox.get(); }

    // Laid out again: the window does this to every element after the fonts
    // changed, and the bar then makes its Font list again.
    void InvalidateLayout() override;
    const std::vector<std::string>& GetFontFamilies() const { return families; }

    // The bar's ground, ink and controls from a theme (a dark program's bar
    // is dark; the icons take the ink).
    void ApplyTheme(const UltraCanvasTheme& theme) override;

    // A control of the bar has the keyboard: a size or a spacing being
    // typed, the Font list open (a font looked up by its first letters,
    // though the keyboard focus stays with the text). A host's own shortcuts
    // leave the keys to it then - a shortcut that finished the text being
    // typed would send what is typed next to the whole text instead of the
    // selected words.
    bool HasKeyboardFocus() const;

    // An attribute was changed by the user; `format` is the whole new format.
    // A style chosen in the Font list's styles (Light Italic) is a Font
    // change followed by Bold and Italic where those changed, one call each.
    std::function<void(TextFormatAttribute attribute, const TextFormat& format)> onFormatChanged;
    // Just before the Font list opens: the host hands it the document's
    // fonts (SetDocumentFonts).
    std::function<void()> onFontListOpening;
    // A font was chosen, so the recent ones changed: the host saves them.
    std::function<void(const std::vector<std::string>& families)> onRecentFontsChanged;
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
    void FontChosen(const FontPickerChoice& choice);
    void PassDocumentFonts();
    std::shared_ptr<UltraCanvasUIElement> ControlOf(TextFormatAttribute attribute) const;

    TextFormat format;
    std::vector<std::string> families;
    std::vector<std::string> documentFonts;
    bool showing = false;                // ShowFormat is setting the controls
    unsigned long fontGeneration = 0;    // GetFontSetGeneration when the list was made
    bool fontRefreshQueued = false;
    bool letterUnitOwn = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    std::shared_ptr<UltraCanvasFontPicker> fontBox;
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
