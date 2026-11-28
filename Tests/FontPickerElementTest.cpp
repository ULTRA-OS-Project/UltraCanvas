// Tests/FontPickerElementTest.cpp
// UltraCanvasFontPicker on a real window, read back from the composited
// pixels (Xvfb in CI): the open list's sections (the default font, the
// document's fonts with a missing one marked, the recent fonts, all fonts),
// every name drawn in its own font - the same letters in two fonts come out
// as different pixels, in the open list and in the closed box -, the styles
// list beside a family with Light and Medium, choosing a style (the family,
// the weight, the slant, the face's own family name), a family of Regular,
// Bold and the italics without one, typing to jump, the keys, and the recent
// fonts following a choice; Escape, routed through the application as a real
// key is (its press and its release), closes the styles list alone, then the
// list. The text bar's Font list (UltraCanvasTextFormatBar) is this picker:
// it lists the generic names, takes the document's and the recent fonts from
// its host, and a family or a style chosen there reaches onFormatChanged as
// the family with Bold and Italic. ULTRACANVAS_SCREENSHOT_DIR=<dir> writes the
// window as PPM files at each step. Skips without a display (fails with
// ULTRACANVAS_TEST_DISPLAY_REQUIRED).
// Version: 1.1.0 - Escape through the application: one level per press, the release closing nothing;
//                  the text bar's Font list
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasFontPicker.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTextFormatBar.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

constexpr int kWidth = 900, kHeight = 720;

bool WritePpm(const std::shared_ptr<UltraCanvasWindow>& window, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(kWidth) * 3);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
            row[static_cast<size_t>(x) * 3 + 0] = px.r;
            row[static_cast<size_t>(x) * 3 + 1] = px.g;
            row[static_cast<size_t>(x) * 3 + 2] = px.b;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(out);
}

// The pixels of an area, as a string to compare.
std::vector<uint32_t> Pixels(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& area) {
    std::vector<uint32_t> out;
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x) {
            Color px;
            window->GetPixelColor(x, y, px);
            out.push_back((static_cast<uint32_t>(px.r) << 16) | (static_cast<uint32_t>(px.g) << 8) | px.b);
        }
    return out;
}

// How many pixels of an area differ from its background (its first pixel).
int Ink(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& area) {
    Color background;
    window->GetPixelColor(area.x, area.y, background);
    int n = 0;
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) continue;
            if (std::abs(px.r - background.r) + std::abs(px.g - background.g) + std::abs(px.b - background.b) > 90) ++n;
        }
    return n;
}

UCEvent Key(UCKeys key, const std::string& text = std::string()) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.text = text;
    if (text.size() == 1) event.character = text[0];
    return event;
}

// A key as the platform layer hands it to the application, for `window`.
void Dispatch(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window, UCEventType type,
              UCKeys key) {
    UCEvent event;
    event.type = type;
    event.virtualKey = key;
    event.targetWindow = window;
    event.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(event);
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Font Picker Element Suite"            << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");
    UltraCanvasApplication app;
    if (!app.Initialize("FontPickerElementTest")) SKIP_ALL("application would not initialise");
    // The lists are read back from the window's surface.
    UltraCanvasWindowBase::SetPopupsInOwnWindows(false);
    WindowConfig cfg;
    cfg.title = "FontPickerElementTest";
    cfg.width = kWidth;
    cfg.height = kHeight;
    cfg.resizable = false;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto picker = CreateFontPicker("Fonts", 20, 20, 130, 26);
    picker->SetDefaultFontEntry("Default font");
    picker->SetDocumentFonts({"Ubuntu", "No Such Font 0815"});
    picker->SetRecentFonts({"Ubuntu Mono", "Ubuntu"});
    picker->SetCurrentFont("Ubuntu");
    std::vector<FontPickerChoice> chosen;
    std::vector<std::vector<std::string>> recentSaved;
    picker->onFontChosen = [&chosen](const FontPickerChoice& choice) { chosen.push_back(choice); };
    picker->onRecentFontsChanged = [&recentSaved](const std::vector<std::string>& list) { recentSaved.push_back(list); };
    window->AddChild(picker);
    std::vector<std::shared_ptr<UltraCanvasUIElement>> elements = {picker};
    for (int i = 0; i < 3; ++i) DisplayTest::Frame(window, elements);

    const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
    auto shoot = [&](const char* name) {
        if (!shotDir) return;
        const std::string path = std::string(shotDir) + "/" + name;
        std::cerr << (WritePpm(window, path) ? "   wrote " : "   could not write ") << path << std::endl;
    };
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) {
            if (picker->GetListView()) picker->GetListView()->RequestRedraw();
            if (picker->GetStylesListView()) picker->GetStylesListView()->RequestRedraw();
            DisplayTest::Frame(window, elements);
        }
    };

    // ===== THE CLOSED BOX: the current family in its own font =====
    const Rect2Di boxText(22, 22, 60, 22);   // the first letters of the name
    picker->SetCurrentFont("Ubuntu Mono");
    frames(2);
    const auto boxMono = Pixels(window, boxText);
    picker->SetCurrentFont("Ubuntu Condensed");
    frames(2);
    const auto boxCondensed = Pixels(window, boxText);
    TEST("the closed box draws its family in itself (Ubuntu Mono and Ubuntu Condensed differ in 'Ubuntu')",
         boxMono != boxCondensed && Ink(window, boxText) > 20);
    picker->SetCurrentFont("Ubuntu");

    // ===== THE SECTIONS =====
    picker->OpenList();
    frames(3);
    TEST("the list opens", picker->IsListOpen() && picker->GetListView() != nullptr);
    using Kind = UltraCanvasFontPicker::RowKind;
    using Section = UltraCanvasFontPicker::Section;
    auto kindOf = [&](int row) { const auto* r = picker->GetRow(row); return r ? r->kind : Kind::Caption; };
    auto textOf = [&](int row) { const auto* r = picker->GetRow(row); return r ? r->text : std::string(); };
    TEST("row 0 is the default font", kindOf(0) == Kind::DefaultFont && textOf(0) == "Default font");
    TEST("then the document's caption", kindOf(1) == Kind::Caption && picker->GetRow(1)->section == Section::Document);
    TEST("the document's installed font", kindOf(2) == Kind::Family && textOf(2) == "Ubuntu");
    TEST("the document's missing font is marked", kindOf(3) == Kind::Missing && textOf(3) == "No Such Font 0815");
    TEST("then the recent caption", kindOf(4) == Kind::Caption && picker->GetRow(4)->section == Section::Recent);
    TEST("a recent font the document names is not named twice",
         kindOf(5) == Kind::Family && textOf(5) == "Ubuntu Mono" && kindOf(6) == Kind::Caption);
    TEST("then 'All fonts'", picker->GetRow(6) && picker->GetRow(6)->section == Section::AllFonts);
    const int all = static_cast<int>(picker->GetFamilies().size());
    TEST("every installed family is in 'All fonts'", picker->GetRowCount() == 7 + all && all >= 6);
    TEST("the current font is highlighted where it is first", picker->GetHighlightedRow() == 2);
    TEST("a missing font is drawn in the interface's font", picker->RowFontFamily(3) == picker->GetStyle().fontFamily);
    TEST("a family's row is drawn in itself", picker->RowFontFamily(2) == "Ubuntu" && picker->RowFontFamily(5) == "Ubuntu Mono");
    shoot("font-picker-sections.ppm");

    // The pixels: the rows of "Ubuntu" and "Ubuntu Mono" share their first
    // six letters; drawn each in itself, those differ.
    UltraCanvasListView* list = picker->GetListView();
    const Point2Df listAt = list->GetPositionInWindow();
    auto rowArea = [&](int row, int x, int width) {
        const Rect2Di cell = list->GetCellRect(row, 0);
        return Rect2Di(static_cast<int>(listAt.x) + cell.x + x, static_cast<int>(listAt.y) + cell.y + 3, width,
                       cell.height - 6);
    };
    const Rect2Di ubuntuName = rowArea(2, 28, 44);
    const Rect2Di monoName = rowArea(5, 28, 44);
    TEST("the rows are drawn", Ink(window, ubuntuName) > 20 && Ink(window, monoName) > 20);
    TEST("'Ubuntu' in Ubuntu and in Ubuntu Mono differ", Pixels(window, ubuntuName) != Pixels(window, monoName));
    TEST("each family row has its format icon", Ink(window, rowArea(2, 6, 16)) > 10);
    TEST("the missing font's row has its text", Ink(window, rowArea(3, 28, 120)) > 10);

    // ===== THE STYLES LIST =====
    const int ubuntuAll = picker->FindRow("Ubuntu", Section::AllFonts);
    const int monoAll = picker->FindRow("Ubuntu Mono", Section::AllFonts);
    TEST("both are in 'All fonts'", ubuntuAll > 6 && monoAll > 6);
    const InstalledFontFamily* ubuntu = FindInstalledFontFamily(picker->GetFamilies(), "Ubuntu");
    const InstalledFontFamily* mono = FindInstalledFontFamily(picker->GetFamilies(), "Ubuntu Mono");
    TEST("Ubuntu (Thin to Bold) has a styles list", ubuntu && picker->HasStylesList(*ubuntu));
    TEST("Ubuntu Mono (Regular, Bold and the italics) has none", mono && !picker->HasStylesList(*mono));
    picker->SetStylesSubmenu(FontPickerStylesSubmenu::EveryMultiStyleFamily);
    TEST("... unless every family with styles gets one", mono && picker->HasStylesList(*mono));
    picker->SetStylesSubmenu(FontPickerStylesSubmenu::BeyondBoldItalic);
    picker->HighlightRow(ubuntuAll);
    frames(1);
    TEST("Right opens the styles", picker->OnEvent(Key(UCKeys::Right)) && picker->IsStylesOpen() &&
                                   picker->GetStylesRow() == ubuntuAll);
    frames(3);
    int light = -1, mediumItalic = -1, regular = -1;
    const auto& styles = picker->GetStylesShown();
    for (size_t i = 0; i < styles.size(); ++i) {
        if (styles[i].name == "Light") light = static_cast<int>(i);
        if (styles[i].name == "Medium Italic") mediumItalic = static_cast<int>(i);
        if (styles[i].name == "Regular") regular = static_cast<int>(i);
    }
    TEST("the styles list has Light, Regular and Medium Italic", light >= 0 && regular >= 0 && mediumItalic >= 0);
    UltraCanvasListView* stylesList = picker->GetStylesListView();
    TEST("it opens on the regular style",
         stylesList && stylesList->GetSelection() && stylesList->GetSelection()->GetCurrentRow() == regular);
    if (stylesList && light >= 0 && regular >= 0) {
        const Point2Df at = stylesList->GetPositionInWindow();
        TEST("it stands beside the list", at.x >= listAt.x + list->GetWidth() - 4);
        auto styleArea = [&](int row) {
            const Rect2Di cell = stylesList->GetCellRect(row, 0);
            return Rect2Di(static_cast<int>(at.x) + cell.x + 12, static_cast<int>(at.y) + cell.y + 3, 60, cell.height - 6);
        };
        TEST("each style is drawn", Ink(window, styleArea(light)) > 10 && Ink(window, styleArea(regular)) > 10);
        TEST("Light and Regular are drawn differently", Pixels(window, styleArea(light)) != Pixels(window, styleArea(regular)));
    }
    shoot("font-picker-styles.ppm");
    TEST("Left closes the styles", picker->OnEvent(Key(UCKeys::Left)) && !picker->IsStylesOpen() && picker->IsListOpen());
    picker->OpenStyles(ubuntuAll);
    TEST("choosing a style closes both lists", picker->ChooseStyle(light) && !picker->IsListOpen() && !picker->IsStylesOpen());
    TEST("... and gives the family, the weight and the slant",
         !chosen.empty() && chosen.back().family == "Ubuntu" && chosen.back().style == "Light" &&
         chosen.back().weight == 300 && chosen.back().slant == FontSlant::Normal && chosen.back().stretch == 100 &&
         chosen.back().fromStyles);
    // "Ubuntu Light" without bold is Medium to fontconfig: Light has no
    // name of its own a document could keep it by.
    TEST("... and no face family name the engine would not give back", !chosen.empty() && chosen.back().faceFamily.empty());
    TEST("the box shows the family chosen", picker->GetCurrentFamily() == "Ubuntu" && picker->GetCurrentStyle() == "Light");
    picker->OpenList();
    frames(1);
    picker->OpenStyles(picker->FindRow("Ubuntu", Section::AllFonts));
    TEST("the styles list opens on the current style", picker->GetStylesListView()->GetSelection()->GetCurrentRow() == light);
    picker->ChooseStyle(mediumItalic);
    TEST("Medium Italic: weight 500, italic",
         chosen.back().weight == 500 && chosen.back().slant == FontSlant::Italic && chosen.back().style == "Medium Italic");
    TEST("... kept by its own family name, without bold",
         chosen.back().faceFamily == "Ubuntu Light" && !chosen.back().faceFamilyBold);
    picker->OpenList();
    picker->OpenStyles(picker->FindRow("Ubuntu", Section::AllFonts));
    int thin = -1;
    for (size_t i = 0; i < picker->GetStylesShown().size(); ++i)
        if (picker->GetStylesShown()[i].name == "Thin") thin = static_cast<int>(i);
    picker->ChooseStyle(thin);
    TEST("Thin: lighter than Light, kept as \"Ubuntu Thin\"",
         chosen.back().weight < 300 && chosen.back().faceFamily == "Ubuntu Thin" && !chosen.back().faceFamilyBold);

    // ===== TYPING TO JUMP, AND THE KEYS =====
    picker->OpenList();
    frames(1);
    for (const char* letter : {"u", "b", "u", "n", "t", "u", " ", "m"}) picker->OnEvent(Key(UCKeys::Unknown, letter));
    frames(3);
    TEST("typing 'ubuntu m' jumps to Ubuntu Mono in 'All fonts'", picker->GetHighlightedRow() == monoAll);
    shoot("font-picker-typing.ppm");
    picker->OnEvent(Key(UCKeys::Home));
    TEST("Home goes to the first row", picker->GetHighlightedRow() == 0);
    picker->OnEvent(Key(UCKeys::Down));
    TEST("Down steps over a caption", picker->GetHighlightedRow() == 2);
    picker->OnEvent(Key(UCKeys::Up));
    TEST("Up steps over it back", picker->GetHighlightedRow() == 0);
    picker->HighlightRow(monoAll);
    const size_t before = chosen.size();
    picker->OnEvent(Key(UCKeys::Return));
    TEST("Return chooses the family", chosen.size() == before + 1 && chosen.back().family == "Ubuntu Mono" &&
                                      !chosen.back().fromStyles && !picker->IsListOpen());
    TEST("the recent fonts follow, newest first",
         !recentSaved.empty() && recentSaved.back().front() == "Ubuntu Mono" &&
         picker->GetRecentFonts().front() == "Ubuntu Mono" && picker->GetRecentFonts().size() == 2);
    picker->OpenList();
    picker->ChooseRow(0);
    TEST("the default-font row chooses \"\"", chosen.back().family.empty() && picker->GetCurrentFamily().empty());
    picker->OpenList();
    TEST("a caption is no choice", !picker->ChooseRow(1) && picker->IsListOpen());
    // Escape as the application gets it: a press and a release each. The
    // styles list goes first, the list stays - then the list.
    picker->OpenStyles(picker->FindRow("Ubuntu", Section::AllFonts));
    TEST("the styles list is open over the list", picker->IsStylesOpen() && picker->IsListOpen());
    Dispatch(app, window, UCEventType::KeyDown, UCKeys::Escape);
    Dispatch(app, window, UCEventType::KeyUp, UCKeys::Escape);
    TEST("one Escape (press and release) closes the styles list alone", !picker->IsStylesOpen() && picker->IsListOpen());
    Dispatch(app, window, UCEventType::KeyDown, UCKeys::Escape);
    Dispatch(app, window, UCEventType::KeyUp, UCKeys::Escape);
    TEST("the next Escape closes the list", !picker->IsListOpen());
    picker->SetMixed();
    TEST("a mixed selection: no value", picker->IsMixed() && picker->GetAccessibleValueText().empty());
    window->RemoveChild(picker);

    // ===== THE TEXT BAR'S FONT LIST =====
    auto bar = CreateTextFormatBar("Bar", 44.0f);
    bar->SetElementSize(Size2Df(kWidth, 44));
    struct Report { TextFormatAttribute attribute; TextFormat format; };
    std::vector<Report> reports;
    int opening = 0;
    std::vector<std::vector<std::string>> barRecent;
    // As a host does: it applies the attribute to its text, then shows the
    // text's format again - bold and italic as the text has them so far.
    TextFormat text;
    bar->onFormatChanged = [&](TextFormatAttribute attribute, const TextFormat& f) {
        reports.push_back({attribute, f});
        if (attribute == TextFormatAttribute::Font) text.fontFamily = f.fontFamily;
        if (attribute == TextFormatAttribute::Bold) text.bold = f.bold;
        if (attribute == TextFormatAttribute::Italic) text.italic = f.italic;
        bar->SetFormat(text);
    };
    bar->onFontListOpening = [&]() {
        ++opening;
        bar->SetDocumentFonts({"Ubuntu Mono", "No Such Font 0815"});
    };
    bar->onRecentFontsChanged = [&barRecent](const std::vector<std::string>& list) { barRecent.push_back(list); };
    bar->SetRecentFonts({"Ubuntu Condensed"});
    text.fontFamily = "Sans";
    bar->SetFormat(text);
    window->AddChild(bar);
    std::vector<std::shared_ptr<UltraCanvasUIElement>> barElements = {bar};
    for (int i = 0; i < 3; ++i) DisplayTest::Frame(window, barElements);
    UltraCanvasFontPicker* fontList = bar->GetFontPicker();
    TEST("the bar's Font list is a font picker", fontList != nullptr);
    if (fontList) {
        TEST("its closed box keeps the dropdown's width in the bar",
             fontList->GetWidth() >= 90 && fontList->GetWidth() <= 210 && fontList->GetHeight() <= 30);
        TEST("the generic names come first, then the installed families",
             bar->GetFontFamilies().size() > 3 && bar->GetFontFamilies()[0] == "Sans" &&
             fontList->FindRow("Ubuntu", Section::AllFonts) >= 0);
        TEST("Sans is no missing font in the box", fontList->IsCurrentInstalled() && fontList->GetCurrentFamily() == "Sans");
        fontList->OpenList();
        for (int i = 0; i < 3; ++i) {
            if (fontList->GetListView()) fontList->GetListView()->RequestRedraw();
            DisplayTest::Frame(window, barElements);
        }
        shoot("text-bar-font-list.ppm");
        TEST("opening the list asks the host for the document's fonts", opening == 1 && fontList->IsListOpen());
        const int missingRow = fontList->FindRow("No Such Font 0815", Section::Document);
        TEST("... shown under \"In this document\", the missing one marked",
             fontList->FindRow("Ubuntu Mono", Section::Document) >= 0 && fontList->GetRow(missingRow) &&
             fontList->GetRow(missingRow)->kind == UltraCanvasFontPicker::RowKind::Missing);
        TEST("... and the recent ones under \"Recent\"", fontList->FindRow("Ubuntu Condensed", Section::Recent) >= 0);
        TEST("the bar has the keyboard while its list is open", bar->HasKeyboardFocus());

        // A family: onFormatChanged(Font) with the family, bold and italic kept.
        reports.clear();
        TEST("choosing a family closes the list",
             fontList->ChooseRow(fontList->FindRow("Ubuntu Mono", Section::AllFonts)) && !fontList->IsListOpen());
        TEST("... and reports the family", reports.size() == 1 && reports[0].attribute == TextFormatAttribute::Font &&
                                           reports[0].format.fontFamily == "Ubuntu Mono" && !reports[0].format.bold &&
                                           !reports[0].format.italic && bar->GetFormat().fontFamily == "Ubuntu Mono");
        TEST("the recent fonts follow, for the host to keep",
             !barRecent.empty() && barRecent.back().size() == 2 && barRecent.back()[0] == "Ubuntu Mono" &&
             bar->GetRecentFonts().front() == "Ubuntu Mono");

        // A style: the family (its face's own name), then Bold and Italic as they changed.
        auto chooseStyle = [&](const std::string& name) {
            fontList->OpenList();
            if (!fontList->OpenStyles(fontList->FindRow("Ubuntu", Section::AllFonts))) return false;
            const auto& shown = fontList->GetStylesShown();
            for (size_t i = 0; i < shown.size(); ++i) {
                if (shown[i].name == name) {
                    reports.clear();
                    return fontList->ChooseStyle(static_cast<int>(i));
                }
            }
            fontList->CloseList();
            return false;
        };
        auto reported = [&reports](TextFormatAttribute attribute) {
            return std::any_of(reports.begin(), reports.end(), [attribute](const Report& r) { return r.attribute == attribute; });
        };
        TEST("Ubuntu's styles list opens from the bar", chooseStyle("Medium Italic"));
        TEST("Medium Italic: the family by its face's name, then Italic",
             reports.size() == 2 && reports[0].attribute == TextFormatAttribute::Font &&
             reports[0].format.fontFamily == "Ubuntu Light" && reports[1].attribute == TextFormatAttribute::Italic &&
             reports[1].format.italic && !reports[1].format.bold);
        TEST("Bold: the family, then Bold, and Italic off",
             chooseStyle("Bold") && !reports.empty() && reports[0].attribute == TextFormatAttribute::Font &&
             reports.back().format.bold && !reports.back().format.italic && reported(TextFormatAttribute::Bold) &&
             reported(TextFormatAttribute::Italic));
        if (!reports.empty()) std::cerr << "   (Bold reported as \"" << reports[0].format.fontFamily << "\")" << std::endl;
        TEST("the B and I buttons show it", bar->GetFormat().bold && !bar->GetFormat().italic);
        TEST("Bold Italic: the host's text ends bold and italic", chooseStyle("Bold Italic") && text.bold && text.italic &&
                                                                  bar->GetFormat().bold && bar->GetFormat().italic);
        TEST("Thin: kept as \"Ubuntu Thin\", not bold",
             chooseStyle("Thin") && !reports.empty() && reports[0].format.fontFamily == "Ubuntu Thin" &&
             !bar->GetFormat().bold);
    }

    std::cerr << "========================================" << std::endl;
    std::cerr << testCount - failCount << " passed, " << failCount << " failed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
