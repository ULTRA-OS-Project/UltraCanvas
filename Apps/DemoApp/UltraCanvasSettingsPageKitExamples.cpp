// Apps/DemoApp/UltraCanvasSettingsPageKitExamples.cpp
// Widgets > Settings window: the framework's settings page kit
// (UltraCanvasSettingsPageKit) shown live. A button opens a settings window
// built with the kit - the tree of pages on the left, the page on the right,
// the page's Restore defaults and Close at the foot - with three sample
// pages that use every control the kit makes: radios, a checkbox, a slider
// with its value, a colour box, a button row, and a scrolling list of
// checkboxes. The page beside it says what the kit is and where it is used.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasSlider.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {
    using namespace SettingsPageKit;

    // The sample application's settings: what the window edits
    struct SampleSettings {
        int theme = 0;                 // 0 light, 1 dark, 2 follow the system
        bool compact = false;
        int textSize = 12;             // 9..18
        int accent = 0;                // an index into kAccents
        std::vector<bool> formats = std::vector<bool>(12, true);
    };

    const Color kAccents[] = {
        Color(37, 99, 235, 255), Color(220, 38, 38, 255), Color(5, 150, 105, 255),
        Color(217, 119, 6, 255), Color(124, 58, 237, 255),
    };
    constexpr int kAccentCount = static_cast<int>(sizeof(kAccents) / sizeof(kAccents[0]));

    const char* kFormatNames[] = {"PNG", "JPEG", "WebP", "AVIF", "HEIC", "JPEG XL", "GIF", "TIFF",
                                  "BMP", "SVG", "PDF", "QOI"};

    constexpr const char* kPageLook = "look";
    constexpr const char* kPageTheme = "look/theme";
    constexpr const char* kPageText = "look/text";
    constexpr const char* kPageFiles = "files";
    constexpr const char* kPageFormats = "files/formats";
    constexpr const char* kPageStart = "start";

    // The open sample window: the kit's shell with the sample's controls
    struct SampleDialog : public UltraCanvasSettingsWindow {
        SampleSettings settings;
        std::shared_ptr<UltraCanvasLabel> status;       // the demo page's line: what changed last
        std::vector<std::pair<int, std::shared_ptr<UltraCanvasRadio>>> themeRadios;
        UltraCanvasRadioGroup themeGroup;
        std::shared_ptr<UltraCanvasCheckbox> compactBox;
        std::shared_ptr<UltraCanvasButton> accentBox;
        std::shared_ptr<UltraCanvasSlider> sizeSlider;
        std::shared_ptr<UltraCanvasLabel> sizeValue;
        std::vector<std::shared_ptr<UltraCanvasCheckbox>> formatBoxes;

        void Say(const std::string& what) {
            if (!status) return;
            status->SetText("Last change: " + what);
            status->RequestRedraw();
        }
    };

    std::shared_ptr<SampleDialog> g_sample;   // kept while its window is up

    std::shared_ptr<UltraCanvasContainer> BuildThemePage(SampleDialog* d) {
        PageParts parts = MakePage("spk-page-theme", "Theme", "How the sample application is drawn:");
        const char* names[] = {"Light", "Dark", "Follow the system"};
        d->themeRadios.clear();
        for (int i = 0; i < 3; ++i) {
            auto radio = MakeChoice("spk-theme-" + std::to_string(i), names[i], d->settings.theme == i);
            d->themeGroup.AddRadioButton(radio);
            d->themeRadios.emplace_back(i, radio);
            parts.body->AddChild(radio);
        }
        d->themeGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            for (const auto& [index, radio] : d->themeRadios) {
                if (radio != selected) continue;
                d->settings.theme = index;
                d->Say(std::string("theme = ") + (index == 0 ? "light" : index == 1 ? "dark" : "system"));
            }
        };
        d->compactBox = MakeCheckbox("spk-compact", "Compact rows", kTextWidth, d->settings.compact, [d](bool on) {
            d->settings.compact = on;
            d->Say(std::string("compact = ") + (on ? "on" : "off"));
        });
        d->compactBox->SetMargin(6, 0, 0, 0);
        parts.body->AddChild(d->compactBox);

        AddBodyCaption(parts, "spk-accent-caption", "The accent colour (click the box for the next one):");
        d->accentBox = MakeColorBox("spk-accent", kAccents[d->settings.accent], "The accent colour", [d]() {
            d->settings.accent = (d->settings.accent + 1) % kAccentCount;
            SetColorBoxColor(d->accentBox, kAccents[d->settings.accent]);
            d->Say("accent " + std::to_string(d->settings.accent + 1) + " of " + std::to_string(kAccentCount));
        });
        parts.body->AddChild(MakeLabelledRow("spk-accent-row", "Accent colour:", d->accentBox));

        d->resets[kPageTheme] = PageReset{"Restore default theme", 180, [d]() {
            d->settings.theme = 0;
            d->settings.compact = false;
            d->settings.accent = 0;
            for (const auto& [index, radio] : d->themeRadios)
                if (index == 0) radio->SetChecked(true);
            if (d->compactBox) d->compactBox->SetChecked(false);
            SetColorBoxColor(d->accentBox, kAccents[0]);
            d->Say("theme restored");
        }};
        AddNote(parts, "spk-theme-note1",
                "A radio group for one choice of several, a checkbox for a switch, a colour box for a colour: "
                "MakeChoice, MakeCheckbox and MakeColorBox, all in the window's one type scale.");
        AddNote(parts, "spk-theme-note2",
                "The notes block sits at the foot of every page, clear of the controls, so the eye finds the "
                "control where it found it on the last page.");
        return parts.page;
    }

    void UpdateSizeLabel(SampleDialog* d) {
        if (!d->sizeValue) return;
        d->sizeValue->SetText(std::to_string(d->settings.textSize) + " pt");
        d->sizeValue->RequestRedraw();
    }

    std::shared_ptr<UltraCanvasContainer> BuildTextPage(SampleDialog* d) {
        PageParts parts = MakePage("spk-page-text", "Text", "The size the sample application's text is drawn at:");
        d->sizeValue = MakeSliderValueLabel("spk-size-value");
        d->sizeSlider = MakeIntSlider("spk-size", 9, 18, d->settings.textSize, [d](int size) {
            d->settings.textSize = size;
            UpdateSizeLabel(d);
            d->Say("text size = " + std::to_string(size));
        });
        parts.body->AddChild(MakeSliderRow("spk-size-row", "Text size:", d->sizeSlider, d->sizeValue, 0));
        UpdateSizeLabel(d);

        auto row = MakeButtonRow("spk-size-buttons");
        row->AddChild(MakeButton("spk-size-smaller", "Smaller", 100, [d]() {
            if (d->settings.textSize > 9) d->sizeSlider->SetValue(static_cast<float>(--d->settings.textSize));
            UpdateSizeLabel(d);
            d->Say("text size = " + std::to_string(d->settings.textSize));
        }));
        row->AddChild(MakeButton("spk-size-larger", "Larger", 100, [d]() {
            if (d->settings.textSize < 18) d->sizeSlider->SetValue(static_cast<float>(++d->settings.textSize));
            UpdateSizeLabel(d);
            d->Say("text size = " + std::to_string(d->settings.textSize));
        }));
        parts.body->AddChild(row);

        d->resets[kPageText] = PageReset{"Restore default size", 170, [d]() {
            d->settings.textSize = 12;
            if (d->sizeSlider) d->sizeSlider->SetValue(12.0f);
            UpdateSizeLabel(d);
            d->Say("text size restored");
        }};
        AddNote(parts, "spk-text-note1",
                "MakeIntSlider reports while the handle moves and when it is let go, so what it sets follows the "
                "slider; MakeSliderRow puts the caption, the slider and its value label on one line.");
        AddNote(parts, "spk-text-note2",
                "The bottom bar's Restore button is the shown page's: labelled and wired for it (resets[pageId]), "
                "hidden on a page that has nothing to restore.");
        return parts.page;
    }

    std::shared_ptr<UltraCanvasContainer> BuildFormatsPage(SampleDialog* d) {
        PageParts parts = MakePage("spk-page-formats", "File formats",
                                   "Which picture types the sample application offers:", /*scrolls=*/true);
        d->formatBoxes.clear();
        for (int i = 0; i < static_cast<int>(d->settings.formats.size()); ++i) {
            auto box = MakeCheckbox("spk-format-" + std::to_string(i), kFormatNames[i], kTextWidth,
                                    d->settings.formats[static_cast<size_t>(i)], [d, i](bool on) {
                d->settings.formats[static_cast<size_t>(i)] = on;
                d->Say(std::string(kFormatNames[i]) + (on ? " on" : " off"));
            });
            d->formatBoxes.push_back(box);
            parts.body->AddChild(box);
        }
        auto row = MakeButtonRow("spk-format-buttons");
        auto setAll = [d](bool on) {
            for (size_t i = 0; i < d->settings.formats.size(); ++i) {
                d->settings.formats[i] = on;
                d->formatBoxes[i]->SetChecked(on);
            }
            d->Say(on ? "every format on" : "every format off");
        };
        row->AddChild(MakeButton("spk-format-all", "Everything on", 120, [setAll]() { setAll(true); }));
        row->AddChild(MakeButton("spk-format-none", "Everything off", 120, [setAll]() { setAll(false); }));
        parts.body->AddChild(row);
        d->resets[kPageFormats] = PageReset{"Restore defaults", 170, [setAll]() { setAll(true); }};
        AddNote(parts, "spk-formats-note1",
                "A page that scrolls (MakePage with scrolls = true) keeps its notes under the caption, before "
                "the list, so they are read before the checkboxes rather than found after them.");
        return parts.page;
    }

    std::shared_ptr<UltraCanvasContainer> BuildStartPage() {
        PageParts parts = MakePage("spk-page-start", "Settings", "Open a section on the left and choose the page to set:");
        AddNote(parts, "spk-start-note1", "Look - the theme, the accent colour and the text size of the sample application.");
        AddNote(parts, "spk-start-note2", "Files - which picture types it offers, on a page that scrolls.");
        AddNote(parts, "spk-start-note3", "Every change applies straight away; there is nothing to confirm.");
        return parts.page;
    }

    void OpenSampleWindow(UltraCanvasWindowBase* parent, const std::shared_ptr<UltraCanvasLabel>& status) {
        if (g_sample && g_sample->IsOpen()) {
            g_sample->status = status;
            g_sample->window->Show();
            g_sample->window->RaiseAndFocus();
            return;
        }
        auto d = std::make_shared<SampleDialog>();
        d->status = status;
        if (!d->Create(parent, "Sample application - Settings")) return;
        d->AddSection(kPageLook, "Look");
        d->AddTreeNode(kPageLook, kPageTheme, "Theme");
        d->AddTreeNode(kPageLook, kPageText, "Text");
        d->AddSection(kPageFiles, "Files");
        d->AddTreeNode(kPageFiles, kPageFormats, "File formats");
        d->AddPage(kPageTheme, BuildThemePage(d.get()));
        d->AddPage(kPageText, BuildTextPage(d.get()));
        d->AddPage(kPageFormats, BuildFormatsPage(d.get()));
        d->AddPage(kPageStart, BuildStartPage());
        d->onClosed = []() {
            if (g_sample) g_sample->Say("the window was closed");
        };
        g_sample = d;
        d->Finish(kPageStart);
    }
} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSettingsPageKitExamples() {
    auto page = DemoKit::CreatePage("SettingsPageKitExamples", Tr("Settings window"),
            Tr("The look of an application's settings window, once: UltraCanvasSettingsPageKit builds the window "
            "with its tree of pages, the page skeleton and the controls in one type scale. UltraFiler, UltraSnip "
            "and UOS-Settings are built on it."));

    auto row = DemoKit::AddRow(page, "SettingsPageKitRow");

    auto open = DemoKit::AddCard(row, "SettingsPageKitOpenCard", Tr("A sample settings window"),
            Tr("Three pages of a made-up application, built with the kit: the theme (radios, a checkbox, a colour "
            "box), the text size (a slider with its value and a button row) and the file formats (a scrolling "
            "list of checkboxes). Every change applies at once; each page has its Restore button."));
    auto button = CreateButton("SettingsPageKitOpen", 0, 0, 200, 34, "Open settings window");
    auto status = DemoKit::AddText(open, "SettingsPageKitStatus", Tr("Last change: none yet"));
    button->SetOnClick([this, status]() { OpenSampleWindow(mainWindow.get(), status); });
    // The button first, its status line under it
    open->RemoveChild(status);
    DemoKit::AddFixed(open, button);
    DemoKit::Add(open, status);

    auto what = DemoKit::AddCard(row, "SettingsPageKitWhatCard", Tr("What the kit gives"));
    DemoKit::Add(what, DemoKit::CreateBullets("SettingsPageKitWhat", {
        Tr("UltraCanvasSettingsWindow: the window, the tree of pages, the page area, Restore defaults and Close; "
        "Create, AddSection, AddTreeNode, AddPage, resets, Finish, ShowPage, SelectPage"),
        Tr("A page's skeleton: MakePage (title, caption, body, notes), AddNote, AddBodyCaption"),
        Tr("The controls in one type scale: MakeChoice, MakeCheckbox, MakeIntSlider with MakeSliderRow, "
        "MakeButton and MakeButtonRow, MakeChoiceRow, MakeLabelledRow, MakeColorBox"),
        Tr("SettingsWindowOptions: the size, the tree's width, a logo above the tree, a dialog or a main window"),
    }));

    auto where = DemoKit::AddCard(row, "SettingsPageKitWhereCard", Tr("Where it is used"));
    DemoKit::Add(where, DemoKit::CreateBullets("SettingsPageKitWhere", {
        Tr("UltraFiler - Settings: 22 pages, where the look was first drawn"),
        Tr("UltraSnip - Settings: the picture type, the recording, what happens after a snip"),
        Tr("UOS-Settings: the ULTRA OS settings, a main window with the logo above its tree"),
    }));
    return page;
}

} // namespace UltraCanvas
