// Plugins/Documents/Word/UltraCanvasSymbolFonts.cpp
// Maps text set in the Windows symbol fonts (Symbol, Wingdings 1-3, Webdings)
// to the Unicode characters it depicts. A document stores such a character as
// the font's 8-bit code (or as U+F000 + code); drawn in any other font it is a
// wrong letter or a private-use box - a letterhead's telephone icon became
// "(" or an empty square wherever Webdings was not installed.
// Version: 1.0.0
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "UltraCanvasWordFormatInternal.h"

#include <cstdint>

namespace UltraCanvas {
namespace WordFormatInternal {

namespace {

#include "UltraCanvasSymbolFontTable.inc"

// Several of the pictographs Unicode 7 added for Wingdings/Webdings exist in
// very few fonts, so they would trade a wrong letter for a missing-glyph box.
// These have a long-established equivalent every system font set draws.
uint32_t PreferCommonGlyph(uint32_t codepoint) {
    switch (codepoint) {
        case 0x1F57F:           // BLACK TOUCHTONE TELEPHONE
        case 0x1F57E:           // WHITE TOUCHTONE TELEPHONE
        case 0x1F580:           // TELEPHONE ON TOP OF MODEM
            return 0x260E;      // BLACK TELEPHONE
        case 0x1F581:           // CLAMSHELL MOBILE PHONE
            return 0x1F4F1;     // MOBILE PHONE
        case 0x1F582:           // BACK OF ENVELOPE
        case 0x1F583:           // STAMPED ENVELOPE
        case 0x1F586:           // PEN OVER STAMPED ENVELOPE
            return 0x2709;      // ENVELOPE
        default:
            return codepoint;
    }
}

const uint32_t* TableFor(const std::string& family) {
    const std::string name = ToLower(family);
    if (name == "symbol") return kSymbolToUnicode;
    if (name == "wingdings") return kWingdingsToUnicode;
    if (name == "wingdings 2") return kWingdings2ToUnicode;
    if (name == "wingdings 3") return kWingdings3ToUnicode;
    if (name == "webdings") return kWebdingsToUnicode;
    return nullptr;
}

// Decodes one UTF-8 sequence at text[i]; advances i. Invalid bytes decode as
// themselves so nothing is lost.
uint32_t NextCodepoint(const std::string& text, size_t& i) {
    return DecodeUtf8(text, i);
}

} // namespace

uint32_t DecodeUtf8(const std::string& text, size_t& i) {
    const auto byte = [&](size_t at) { return static_cast<uint8_t>(text[at]); };
    uint8_t lead = byte(i);
    size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3
                  : (lead >> 3) == 0x1E ? 4 : 1;
    if (i + length > text.size()) length = 1;
    uint32_t cp = length == 1 ? lead
                : length == 2 ? (lead & 0x1Fu)
                : length == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
    for (size_t k = 1; k < length; ++k) cp = (cp << 6) | (byte(i + k) & 0x3Fu);
    i += length;
    return cp;
}

namespace {

void MapRuns(std::vector<RichTextRun>& runs) {
    for (RichTextRun& run : runs) {
        if (run.IsInlineImage() || !TableFor(run.fontFamily)) continue;
        std::string mapped;
        for (size_t i = 0; i < run.text.size();) {
            uint32_t cp = NextCodepoint(run.text, i);
            uint32_t unicode = SymbolFontCharToUnicode(run.fontFamily, cp);
            AppendUtf8(mapped, unicode ? unicode : cp);
        }
        run.text = std::move(mapped);
        // The text is ordinary Unicode now; asking for the symbol font would
        // put the wrong glyphs back wherever that font is installed.
        run.fontFamily.clear();
    }
}

} // namespace

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

uint32_t SymbolFontCharToUnicode(const std::string& fontFamily, uint32_t codepoint) {
    const uint32_t* table = TableFor(fontFamily);
    if (!table) return 0;
    // Word and LibreOffice store symbol-font characters in the private-use
    // block U+F020..U+F0FF (U+F000 + font code) as often as the bare code.
    if (codepoint >= 0xF020 && codepoint <= 0xF0FF) codepoint -= 0xF000;
    if (codepoint < 0x20 || codepoint > 0xFF) return 0;
    if (codepoint == 0x20) return 0x20;
    return PreferCommonGlyph(table[codepoint - 0x20]);
}

void MapSymbolFontRuns(UCRichDocument& document) {
    auto mapBlocks = [](std::vector<RichDocBlock>& blocks) {
        for (RichDocBlock& block : blocks) {
            MapRuns(block.runs);
            for (RichTableRow& row : block.tableRows) {
                for (RichTableCell& cell : row.cells) MapRuns(cell.runs);
            }
            // Text of its own never draws in a symbol font either.
            if (TableFor(block.paragraphFontFamily)) block.paragraphFontFamily.clear();
        }
    };
    mapBlocks(document.blocks);
    for (RichPageFurniture* furniture : {&document.pageFurniture, &document.firstPageFurniture}) {
        mapBlocks(furniture->header);
        mapBlocks(furniture->footer);
    }
}

} // namespace WordFormatInternal
} // namespace UltraCanvas
