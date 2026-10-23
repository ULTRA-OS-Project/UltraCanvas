// Apps/DemoApp/UltraCanvasQRCodeExamples.cpp
// QR code demonstration: an Encode tab (content presets, error correction,
// point / finder styles and colours, gradients, a logo, saving as an image)
// and a Decode tab that scans an opened image.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the tabs in a card
//                  that fills the area, each tab a flex layout that scrolls when
//                  taller, every label and button sized to its text
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <vector>
#include <functional>
#include <cctype>

namespace UltraCanvas {

    namespace {

        struct PointStyleChoice { const char* label; QRPointStyle style; };
        const PointStyleChoice kPointStyles[] = {
            { "Square",          QRPointStyle::Square },
            { "Rounded Square",  QRPointStyle::RoundedSquare },
            { "Circle",          QRPointStyle::Circle },
            { "Diamond",         QRPointStyle::Diamond },
            { "Hexagon",         QRPointStyle::Hexagon },
            { "Vertical Bars",   QRPointStyle::VerticalBars },
            { "Horizontal Bars", QRPointStyle::HorizontalBars },
            { "Dots",            QRPointStyle::Dots },
        };

        struct FinderStyleChoice { const char* label; QRFinderStyle style; };
        const FinderStyleChoice kFinderStyles[] = {
            { "Standard",       QRFinderStyle::Standard },
            { "Rounded",        QRFinderStyle::Rounded },
            { "Circle",         QRFinderStyle::Circle },
            { "Rounded Circle", QRFinderStyle::RoundedCircle },
            { "Diamond",        QRFinderStyle::Diamond },
        };

        struct ECLChoice { const char* label; QRErrorCorrection ecl; };
        const ECLChoice kECLs[] = {
            { "Low (7%)",      QRErrorCorrection::Low },
            { "Medium (15%)",  QRErrorCorrection::Medium },
            { "Quartile (25%)",QRErrorCorrection::Quartile },
            { "High (30%)",    QRErrorCorrection::High },
        };

        struct GradientChoice {
            const char* label; QRGradientType type; Color start; Color end;
        };
        const GradientChoice kGradients[] = {
            { "None",     QRGradientType::NoGradient,     Colors::Black,    Colors::Black },
            { "Linear",   QRGradientType::Linear,   Color(0,102,204), Color(102,204,255) },
            { "Radial",   QRGradientType::Radial,   Color(139,0,139), Color(255,0,128) },
            { "Diagonal", QRGradientType::Diagonal, Color(204,0,0),   Color(255,140,0) },
        };

        // Shared colour palette used by the inline point / finder colour
        // selectors that sit next to the Point style and Finder style dropdowns.
        struct SwatchChoice { const char* name; Color color; };
        const SwatchChoice kSwatches[] = {
            { "Black",   Colors::Black },
            { "Indigo",  Color(0, 0, 139) },
            { "Teal",    Color(0, 128, 128) },
            { "Crimson", Color(139, 0, 0) },
            { "Purple",  Color(139, 0, 139) },
            { "Amber",   Color(255, 140, 0) },
        };

        struct PresetChoice { const char* label; std::string (*build)(); };
        const PresetChoice kPresets[] = {
            { "URL",   []() { return QRCodeUtils::CreateURLContent("https://ultraos.eu"); } },
            { "Email", []() { return QRCodeUtils::CreateEmailContent("info@ultraos.org", "Hello", "Sent from UltraCanvas demo"); } },
            { "SMS",   []() { return QRCodeUtils::CreateSMSContent("+15551234567", "Hi from UltraCanvas"); } },
            { "WiFi",  []() { return QRCodeUtils::CreateWiFiContent("UltraOS-Net", "supersecret", "WPA"); } },
            { "vCard", []() { return QRCodeUtils::CreateVCardContent("Jane Doe", "+15551234567", "jane@example.com"); } },
            { "Geo",   []() { return QRCodeUtils::CreateGeoContent(37.7749, -122.4194); } },
        };

        // Every picture type this build reads - FileLoader's list
        FileDialogOptions ImageOpenFilters(const std::string& title) {
            FileDialogOptions opts;
            opts.SetTitle(title);
            UltraCanvasFileLoader::AddLoadTypes(opts, "Images", {MediaFormatCategory::Bitmap});
            opts.AddFilter("All files", "*");
            return opts;
        }

        // A button that is at least as large as it was built, and grows with
        // its text.
        void SetMinSize(UltraCanvasUIElement& element, float width, float height) {
            CSSLayout::BoxConstraints limits = element.boxConstraints.value_or(CSSLayout::BoxConstraints{});
            limits.minWidth  = CSSLayout::Dimension::Px(width);
            limits.minHeight = CSSLayout::Dimension::Px(height);
            element.boxConstraints = limits;
        }

        // A tab: a column the tab sizes exactly; content taller or wider than
        // that scrolls.
        std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id) {
            auto page = std::make_shared<UltraCanvasContainer>(id);
            page->SetPadding(12, 20, 12, 20);
            page->layout.SetFlexColumn().SetFlexGap(8)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            ContainerStyle style;
            style.autoShowScrollbars = true;
            page->SetContainerStyle(style);
            return page;
        }

        // A row whose items sit centred on one line (a label beside its control).
        std::shared_ptr<UltraCanvasContainer> AddControlRow(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                            const std::string& id, float gap = 8) {
            auto row = DemoKit::AddRow(parent, id, gap);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            return row;
        }

        // The tab's title: the old 14 px blue bold heading.
        void AddTabTitle(const std::shared_ptr<UltraCanvasContainer>& page,
                         const std::string& id, const std::string& text) {
            auto title = DemoKit::AddHeading(page, id, text);
            title->SetFontSize(14);
            title->SetTextColor(Color(50, 50, 150, 255));
        }

        std::shared_ptr<UltraCanvasUIElement> BuildEncodePage() {
            auto page = MakeTabPage("QREncodePage");

            AddTabTitle(page, "QREncodeTitle", "Encode — generate a QR code with custom style, logo, and color");

            auto baseName = [](const std::string& p) -> std::string {
                auto pos = p.find_last_of("/\\");
                return pos == std::string::npos ? p : p.substr(pos + 1);
            };

            // The code with its content field on the left, the controls beside
            // it (under it when the tab is narrow).
            auto columns = DemoKit::CreateRow("QREncodeColumns", 30);
            columns->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
            DemoKit::Add(page, columns);

            auto codeColumn = DemoKit::CreateColumn("QREncodeCodeColumn", 6);
            DemoKit::Add(columns, codeColumn);

            auto qr = std::make_shared<UltraCanvasQRCode>("qr-encode", 0, 0, 420, 420);
            qr->SetContent("https://ultraos.eu");
            std::string logoPath = NormalizePath(GetResourcesDir() + "media/images/UOS_logo_white.png");
            auto logoImg = UCImage::Get(logoPath);
            qr->SetLogo(logoImg, 0.25f);
            qr->SetLogoStyle(QRLogoStyle::CenterRounded);
            qr->SetGradient(QRGradientType::Diagonal, Color(0,102,204), Color(102,204,255));
            DemoKit::AddFixed(codeColumn, qr);
            auto* qrPtr = qr.get();

            auto contentLbl = DemoKit::AddText(codeColumn, "ContentLbl", "Content:");
            contentLbl->SetFontWeight(FontWeight::Bold);
            contentLbl->SetMargin(12, 0, 0, 0);

            auto contentInput = std::make_shared<UltraCanvasTextInput>("ContentInput", 0, 0, 420, 32);
            contentInput->SetText("https://ultraos.eu");
            DemoKit::AddStretched(codeColumn, contentInput);

            auto status = DemoKit::CreateText("EncStatus", "");
            status->SetFontSize(11);
            status->SetTextColor(Color(120, 120, 120, 255));
            DemoKit::Add(codeColumn, status);
            auto* statusPtr = status.get();

            // Tracks which content preset is currently active so the matching
            // button can be highlighted and the export file name can reflect
            // the selected content type (e.g. "ultracanvas_url.png").
            auto activeSlug      = std::make_shared<std::string>("url");
            auto setActivePreset = std::make_shared<std::function<void(int)>>();

            // Handles wired up later by the inline colour swatch rows and the
            // gradient dropdown so the three colour controls stay consistent:
            //  - setActivePoint / setActiveFinder highlight the chosen swatch
            //  - gradResetToNone snaps the gradient dropdown back to "None"
            //    when a solid point colour is picked.
            auto setActivePoint  = std::make_shared<std::function<void(int)>>();
            auto setActiveFinder = std::make_shared<std::function<void(int)>>();
            auto gradResetToNone = std::make_shared<std::function<void()>>();

            auto refresh = [qrPtr, statusPtr]() {
                std::ostringstream ss;
                if (qrPtr->IsValid()) {
                    ss << "Version " << qrPtr->GetVersion()
                       << "  •  " << qrPtr->GetModuleCount() << "x"
                       << qrPtr->GetModuleCount() << " modules";
                } else if (!qrPtr->GetErrorMessage().empty()) {
                    ss << "Error: " << qrPtr->GetErrorMessage();
                } else {
                    ss << "—";
                }
                statusPtr->SetText(ss.str());
            };

            contentInput->onTextChanged = [qrPtr, refresh, setActivePreset, activeSlug](const std::string& s) {
                qrPtr->SetContent(s);
                // Manual editing no longer matches a preset, so drop the
                // highlight and fall back to a generic export name.
                if (*setActivePreset) (*setActivePreset)(-1);
                *activeSlug = "qr";
                refresh();
            };

            // The controls take the rest of the width, at least 400 px.
            auto controls = DemoKit::CreateColumn("QREncodeControls", 6);
            DemoKit::Add(columns, controls, 1);
            controls->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(400));

            auto header = [&](const std::string& text) {
                auto lbl = DemoKit::AddText(controls, "h_" + text, text);
                lbl->SetFontWeight(FontWeight::Bold);
                lbl->SetMargin(4, 0, 0, 0);
            };

            // Builds a compact row of colour swatches to the right of a
            // (narrowed) style dropdown. The selected swatch gets a bold
            // outline; `apply` performs the actual colour change on the QR code.
            auto buildSwatchRow = [](
                    const std::shared_ptr<UltraCanvasContainer>& row, const std::string& idPrefix,
                    std::function<void(const Color&)> apply,
                    std::shared_ptr<std::function<void(int)>> setActive) {
                auto swatches = DemoKit::CreateRow(idPrefix + "swatches", 2);
                swatches->SetMargin(0, 0, 0, 2);
                DemoKit::Add(row, swatches);
                auto buttons = std::make_shared<std::vector<UltraCanvasButton*>>();
                for (int i = 0; i < static_cast<int>(std::size(kSwatches)); ++i) {
                    const auto& s = kSwatches[i];
                    auto b = std::make_shared<UltraCanvasButton>(
                        idPrefix + s.name, 0, 0, 28, 26, "");
                    b->SetColors(s.color, s.color, s.color, s.color);
                    b->SetBorder(1.0f, Color(120, 120, 120, 255));
                    b->SetTooltip(s.name);
                    buttons->push_back(b.get());

                    Color col = s.color;
                    int   idx = i;
                    b->SetOnClick([apply, col, setActive, idx]() {
                        apply(col);
                        if (*setActive) (*setActive)(idx);
                    });
                    DemoKit::AddFixed(swatches, b);
                }
                *setActive = [buttons](int active) {
                    for (int j = 0; j < static_cast<int>(buttons->size()); ++j) {
                        if (j == active)
                            (*buttons)[j]->SetBorder(3.0f, Colors::Black);
                        else
                            (*buttons)[j]->SetBorder(1.0f, Color(120, 120, 120, 255));
                    }
                };
            };

            header("Content presets");
            {
                // Highlight colours for the active preset vs. the idle ones.
                const Color kPresetIdle (225, 225, 225, 255);
                const Color kPresetIdleH(210, 210, 210, 255);
                const Color kPresetSel  ( 40, 110, 215, 255);
                const Color kPresetSelH ( 30,  95, 190, 255);

                auto buttons = std::make_shared<std::vector<UltraCanvasButton*>>();
                auto presetRow = DemoKit::AddRow(controls, "QRPresetRow", 4);

                for (const auto& p : kPresets) {
                    // At least the old 88 x 28, wider when the label needs it.
                    auto b = std::make_shared<UltraCanvasButton>(
                        std::string("preset_") + p.label, p.label);
                    SetMinSize(*b, 88, 28);
                    b->SetFontSize(11);
                    b->SetColors(kPresetIdle, kPresetIdleH);
                    b->SetTextColors(Colors::TextDefault);

                    int idx = static_cast<int>(buttons->size());
                    buttons->push_back(b.get());

                    // Lowercase the label for use as an export file-name slug.
                    std::string slug;
                    for (char c : std::string(p.label))
                        slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    auto build = p.build;
                    auto* inputPtr = contentInput.get();
                    b->SetOnClick([qrPtr, inputPtr, build, refresh,
                                   setActivePreset, activeSlug, slug, idx]() {
                        std::string s = build();
                        inputPtr->SetText(s);
                        qrPtr->SetContent(s);
                        if (*setActivePreset) (*setActivePreset)(idx);
                        *activeSlug = slug;
                        refresh();
                    });
                    DemoKit::Add(presetRow, b);
                }

                *setActivePreset = [buttons, kPresetIdle, kPresetIdleH,
                                    kPresetSel, kPresetSelH](int active) {
                    for (int i = 0; i < static_cast<int>(buttons->size()); ++i) {
                        auto* btn = (*buttons)[i];
                        if (i == active) {
                            btn->SetColors(kPresetSel, kPresetSelH, kPresetSel, kPresetSel);
                            btn->SetTextColors(Colors::White);
                        } else {
                            btn->SetColors(kPresetIdle, kPresetIdleH);
                            btn->SetTextColors(Colors::TextDefault);
                        }
                    }
                };

                // The default content is a URL, so highlight that preset.
                (*setActivePreset)(0);
            }

            header("Error correction");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("ecl_dd", 0, 0, 220, 28);
                for (const auto& c : kECLs) dd->AddItem(c.label);
                dd->SetSelectedIndex(1, false);
                dd->onSelectionChanged = [qrPtr, refresh](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kECLs))) {
                        qrPtr->SetErrorCorrection(kECLs[idx].ecl);
                        refresh();
                    }
                };
                DemoKit::AddFixed(controls, dd);
            }

            // Point style + the colour used for the data modules. The dropdown
            // is narrowed so the point-colour swatches fit on the same row.
            header("Point style");
            {
                auto row = AddControlRow(controls, "QRPointRow");
                auto dd = std::make_shared<UltraCanvasDropdown>("pt_dd", 0, 0, 150, 28);
                for (const auto& c : kPointStyles) dd->AddItem(c.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kPointStyles))) {
                        qrPtr->SetPointStyle(kPointStyles[idx].style);
                    }
                };
                DemoKit::AddFixed(row, dd);

                buildSwatchRow(row, "ptcol_",
                    [qrPtr, gradResetToNone](const Color& c) {
                        // A solid module colour overrides any gradient; keep the
                        // gradient dropdown in sync by snapping it back to "None".
                        qrPtr->ClearGradient();
                        qrPtr->SetForegroundColor(c);
                        if (*gradResetToNone) (*gradResetToNone)();
                    },
                    setActivePoint);
            }

            // Finder style + a dedicated colour for the three finder patterns.
            header("Finder style");
            {
                auto row = AddControlRow(controls, "QRFinderRow");
                auto dd = std::make_shared<UltraCanvasDropdown>("fnd_dd", 0, 0, 150, 28);
                for (const auto& c : kFinderStyles) dd->AddItem(c.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kFinderStyles))) {
                        qrPtr->SetFinderStyle(kFinderStyles[idx].style);
                    }
                };
                DemoKit::AddFixed(row, dd);

                buildSwatchRow(row, "fndcol_",
                    [qrPtr](const Color& c) { qrPtr->SetFinderColor(c); },
                    setActiveFinder);
            }

            header("Foreground gradient");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("grad_dd", 0, 0, 220, 28);
                for (const auto& g : kGradients) dd->AddItem(g.label);
                auto* gradDd = dd.get();
                *gradResetToNone = [gradDd]() { gradDd->SetSelectedIndex(0, false); };
                // The QR code is created with a Diagonal gradient above, so the
                // dropdown must start on that entry to reflect the actual state.
                int gradDefault = 0;
                for (int i = 0; i < static_cast<int>(std::size(kGradients)); ++i)
                    if (kGradients[i].type == QRGradientType::Diagonal) { gradDefault = i; break; }
                dd->SetSelectedIndex(gradDefault, false);
                dd->onSelectionChanged = [qrPtr, setActivePoint](int idx, const DropdownItem&) {
                    if (idx < 0 || idx >= static_cast<int>(std::size(kGradients))) return;
                    const auto& g = kGradients[idx];
                    if (g.type == QRGradientType::NoGradient) {
                        // Revert to a solid black module colour and reflect that
                        // in the point-colour swatches (Black is the first one).
                        qrPtr->ClearGradient();
                        qrPtr->SetForegroundColor(Colors::Black);
                        if (*setActivePoint) (*setActivePoint)(0);
                    } else {
                        // A gradient overrides the solid point colour.
                        qrPtr->SetGradient(g.type, g.start, g.end);
                        if (*setActivePoint) (*setActivePoint)(-1);
                    }
                };
                DemoKit::AddFixed(controls, dd);
            }

            header("Logo");
            {
                // --- Row 1: upload / remove / placement style --------------
                auto row1 = AddControlRow(controls, "QRLogoRow1");

                auto uploadBtn = std::make_shared<UltraCanvasButton>("logo_upload", "Upload logo");
                SetMinSize(*uploadBtn, 140, 30);
                uploadBtn->SetFontSize(11);

                auto clearBtn = std::make_shared<UltraCanvasButton>("logo_clear", "Remove logo");
                SetMinSize(*clearBtn, 100, 30);
                clearBtn->SetFontSize(11);

                auto styleDD = std::make_shared<UltraCanvasDropdown>(
                    "logo_style", 0, 0, 170, 30);
                styleDD->AddItem("Center");
                styleDD->AddItem("Center + Border");
                styleDD->AddItem("Center Rounded");
                styleDD->AddItem("Center Circle");
                // The logo is created with the Center Rounded style above.
                styleDD->SetSelectedIndex(2, false);

                auto styleFromIndex = [](int idx) {
                    switch (idx) {
                        case 0:  return QRLogoStyle::Center;
                        case 1:  return QRLogoStyle::CenterWithBorder;
                        case 2:  return QRLogoStyle::CenterRounded;
                        case 3:  return QRLogoStyle::CenterCircle;
                        default: return QRLogoStyle::CenterWithBorder;
                    }
                };
                styleDD->onSelectionChanged = [qrPtr, styleFromIndex](int idx, const DropdownItem&) {
                    qrPtr->SetLogoStyle(styleFromIndex(idx));
                };

                DemoKit::Add(row1, uploadBtn);
                DemoKit::Add(row1, clearBtn);
                DemoKit::AddFixed(row1, styleDD);

                // --- Row 2: thumbnail, file name, Zoom and Margin ----------
                auto row2 = DemoKit::AddRow(controls, "QRLogoRow2", 8);

                // Dark backdrop so a (white) logo is visible in the thumbnail.
                auto thumbBg = std::make_shared<UltraCanvasContainer>(
                    "logo_thumb_bg", 0, 0, 50, 50);
                thumbBg->SetBackgroundColor(Color(70, 70, 70, 255));
                thumbBg->SetBorders(1.0f, Color(120, 120, 120, 255));
                thumbBg->layout.SetFlexColumn();
                DemoKit::AddFixed(row2, thumbBg);

                auto thumb = std::make_shared<UltraCanvasImageElement>(
                    "logo_thumb", 0, 0, 48, 48);
                thumb->SetFitMode(ImageFitMode::Contain);
                if (logoImg && logoImg->IsValid()) thumb->LoadFromImage(logoImg);
                DemoKit::AddFixed(thumbBg, thumb);

                auto logoInfo = DemoKit::CreateColumn("QRLogoInfo", 6);
                DemoKit::Add(row2, logoInfo, 1);

                auto nameLbl = DemoKit::CreateText("logo_name", baseName(logoPath));
                nameLbl->SetFontSize(11);
                DemoKit::Add(logoInfo, nameLbl);

                auto sliders = AddControlRow(logoInfo, "QRLogoSliders");

                // The readouts keep their old width as a minimum so the slider
                // beside them does not shift as the number changes.
                auto zoomLbl = DemoKit::CreateText("logo_zoom_lbl", "Zoom 25%");
                zoomLbl->SetFontSize(11);
                zoomLbl->boxConstraints = CSSLayout::BoxConstraints{};
                zoomLbl->boxConstraints->minWidth = CSSLayout::Dimension::Px(80);
                DemoKit::Add(sliders, zoomLbl);

                auto zoomSlider = std::make_shared<UltraCanvasSlider>(
                    "logo_zoom", 0, 0, 120, 20);
                zoomSlider->SetRange(5, 40);   // percent of the QR width
                zoomSlider->SetStep(1);
                zoomSlider->SetValue(25);      // matches the initial logoScale 0.25
                DemoKit::AddFixed(sliders, zoomSlider);

                auto marginLbl = DemoKit::CreateText("logo_margin_lbl", "Margin 4px");
                marginLbl->SetFontSize(11);
                marginLbl->boxConstraints = CSSLayout::BoxConstraints{};
                marginLbl->boxConstraints->minWidth = CSSLayout::Dimension::Px(80);
                marginLbl->SetMargin(0, 0, 0, 8);
                DemoKit::Add(sliders, marginLbl);

                auto marginSlider = std::make_shared<UltraCanvasSlider>(
                    "logo_margin", 0, 0, 120, 20);
                marginSlider->SetRange(0, 20);
                marginSlider->SetStep(1);
                marginSlider->SetValue(4);     // matches the default logoBorderWidth
                DemoKit::AddFixed(sliders, marginSlider);

                // --- Wire up behaviour now that every element exists -------
                auto* thumbPtr     = thumb.get();
                auto* nameLblPtr   = nameLbl.get();
                auto* zoomLblPtr   = zoomLbl.get();
                auto* marginLblPtr = marginLbl.get();
                auto* zoomPtr      = zoomSlider.get();
                auto* marginPtr    = marginSlider.get();
                auto* stylePtr     = styleDD.get();

                zoomSlider->onValueChanged = [qrPtr, zoomLblPtr](float v) {
                    auto cfg = qrPtr->GetStyleConfig();
                    cfg.logoScale = v / 100.0f;
                    qrPtr->SetStyleConfig(cfg);
                    zoomLblPtr->SetText("Zoom " + std::to_string(static_cast<int>(v)) + "%");
                };

                marginSlider->onValueChanged = [qrPtr, marginLblPtr](float v) {
                    auto cfg = qrPtr->GetStyleConfig();
                    cfg.logoBorderWidth = v;
                    qrPtr->SetStyleConfig(cfg);
                    marginLblPtr->SetText("Margin " + std::to_string(static_cast<int>(v)) + "px");
                };

                uploadBtn->SetOnClick([qrPtr, statusPtr, thumbPtr, nameLblPtr,
                                       zoomPtr, marginPtr, stylePtr,
                                       styleFromIndex, baseName]() {
                    UltraCanvasFileLoader::OpenFileDialog(ImageOpenFilters("Choose a logo"),
                        [qrPtr, statusPtr, thumbPtr, nameLblPtr, zoomPtr, marginPtr, stylePtr,
                         styleFromIndex, baseName](DialogResult result, const std::string& path) {
                        if (result != DialogResult::OK || path.empty()) return;
                        auto img = UCImage::Get(path);
                        if (!img || !img->IsValid()) {
                            statusPtr->SetText("Failed to load logo: " + path);
                            return;
                        }
                        auto cfg = qrPtr->GetStyleConfig();
                        cfg.logoImage       = img;
                        cfg.logoScale       = zoomPtr->GetValue() / 100.0f;
                        cfg.logoBorderWidth = marginPtr->GetValue();
                        cfg.logoStyle       = styleFromIndex(stylePtr->GetSelectedIndex());
                        qrPtr->SetStyleConfig(cfg);
                        thumbPtr->LoadFromImage(img);
                        nameLblPtr->SetText(baseName(path));
                        statusPtr->SetText("Logo loaded: " + path);
                    });
                });

                clearBtn->SetOnClick([qrPtr, statusPtr, thumbPtr, nameLblPtr]() {
                    qrPtr->ClearLogo();
                    thumbPtr->LoadFromImage(std::make_shared<UCImage>());
                    nameLblPtr->SetText("(no logo)");
                    statusPtr->SetText("Logo removed");
                });
            }

            header("Save as image");
            {
                // Every picture type this build writes (FileLoader's list), and
                // SVG through the code's own vector writer
                auto saveRow = DemoKit::AddRow(controls, "QRSaveRow");
                auto saveBtn = std::make_shared<UltraCanvasButton>("save_btn", "Save");
                SetMinSize(*saveBtn, 110, 30);
                saveBtn->SetFontSize(11);
                saveBtn->SetOnClick([qrPtr, statusPtr, activeSlug]() {
                    FileDialogOptions opts;
                    opts.SetTitle("Save QR code")
                        .SetDefaultFileName(std::string("ultracanvas_") + *activeSlug + ".png");
                    UltraCanvasFileLoader::AddImageSaveTypes(opts);
                    opts.AddFilter("SVG image", "svg");
                    UltraCanvasFileLoader::SaveFileDialog(opts,
                        [qrPtr, statusPtr](DialogResult result, const std::string& path) {
                        if (result != DialogResult::OK || path.empty()) return;
                        std::string ext = PathToUtf8(PathFromUtf8(path).extension());
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        std::string err;
                        const bool ok = ext == ".svg" ? qrPtr->ExportToSVG(path, 10)
                                                      : qrPtr->ExportToImage(path, 12, &err);
                        statusPtr->SetText(ok ? ("Saved: " + path)
                                              : ("Save failed: " + (err.empty() ? std::string("SVG export failed") : err)));
                    });
                });
                DemoKit::Add(saveRow, saveBtn);
            }

            refresh();
            return page;
        }

        std::shared_ptr<UltraCanvasUIElement> BuildDecodePage() {
            auto page = MakeTabPage("QRDecodePage");

            AddTabTitle(page, "DecTitle", "Decode — open an image and scan it for QR codes");

            auto hint = DemoKit::CreateText("DecHint", "");
            if (QRCodeUtils::IsDecoderAvailable()) {
                hint->SetText("Click \"Open image\" to choose a photo or screenshot containing one or more QR codes.");
            } else {
                hint->SetText("Decoder support is not built in — install libzbar-dev and rebuild to enable scanning.");
            }
            hint->SetFontSize(11);
            hint->SetTextColor(Color(110, 110, 110, 255));
            DemoKit::Add(page, hint);

            // The button and, beside it, the status line (which takes the
            // rest of the width and wraps a long path).
            auto openRow = AddControlRow(page, "DecOpenRow", 16);

            auto openBtn = std::make_shared<UltraCanvasButton>("DecOpen", "Open image");
            SetMinSize(*openBtn, 160, 32);
            openBtn->SetFontSize(12);
            // Use SetColors (not SetBackgroundColor) so the button's normal/
            // hover/pressed style colors are set — otherwise it keeps the
            // default light ButtonFace and white text looks disabled.
            openBtn->SetColors(Color(40, 167, 69), Color(34, 142, 59),
                               Color(28, 117, 49), Color(40, 167, 69));
            openBtn->SetTextColors(Colors::White, Colors::White,
                                   Colors::White, Colors::White);
            DemoKit::Add(openRow, openBtn);

            auto status = DemoKit::CreateText("DecStatus", "");
            status->SetFontSize(11);
            status->SetTextColor(Color(120, 120, 120, 255));
            status->SetWrap(TextWrap::WrapWordChar);
            DemoKit::Add(openRow, status, 1);
            status->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(200));
            auto* statusPtr = status.get();

            // The preview and the results beside it (under it when narrow).
            auto columns = DemoKit::CreateRow("DecColumns", 20);
            columns->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
            DemoKit::Add(page, columns);

            auto preview = std::make_shared<UltraCanvasImageElement>(
                "DecPreview", 0, 0, 480, 444);
            preview->SetFitMode(ImageFitMode::Contain);
            DemoKit::AddFixed(columns, preview);
            auto* previewPtr = preview.get();

            auto results = DemoKit::CreateColumn("DecResults", 8);
            DemoKit::Add(columns, results, 1);
            results->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(300));

            DemoKit::AddHeading(results, "DecResultsTitle", "Scan result");

            auto resultsBody = DemoKit::CreateText("DecResultsBody", "(no image scanned yet)");
            resultsBody->SetFontSize(11);
            resultsBody->SetTextColor(Color(70, 70, 70, 255));
            resultsBody->SetWrap(TextWrap::WrapWordChar);
            DemoKit::Add(results, resultsBody);
            auto* resultsPtr = resultsBody.get();

            openBtn->SetOnClick([previewPtr, resultsPtr, statusPtr]() {
                UltraCanvasFileLoader::OpenFileDialog(ImageOpenFilters("Open image to scan"),
                    [previewPtr, resultsPtr, statusPtr](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) return;

                    previewPtr->LoadFromFile(path);

                    if (!QRCodeUtils::IsDecoderAvailable()) {
                        resultsPtr->SetText("Decoder not built in.\nInstall libzbar and rebuild.");
                        statusPtr->SetText("Loaded image: " + path);
                        return;
                    }

                    std::string err;
                    auto results = QRCodeUtils::ScanQRCodeFile(path, &err);
                    if (results.empty()) {
                        if (err.empty()) {
                            resultsPtr->SetText("No QR codes detected in this image.");
                        } else {
                            resultsPtr->SetText("Scan error:\n" + err);
                        }
                    } else {
                        std::ostringstream ss;
                        ss << "Detected " << results.size()
                           << (results.size() == 1 ? " code:" : " codes:") << "\n\n";
                        int i = 1;
                        for (const auto& r : results) {
                            ss << "#" << i++ << "  [" << r.type << "]\n";
                            ss << r.data << "\n\n";
                        }
                        resultsPtr->SetText(ss.str());
                    }
                    statusPtr->SetText("Scanned: " + path);
                });
            });

            return page;
        }

    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateQRCodeExamples() {
        auto page = DemoKit::CreatePage("QRCodeExamples", "QR Code",
                "Encode a QR code with custom style, logo and colours, and save it as an image; "
                "decode the QR codes in a photo or screenshot.");
        // The tabs take the area's height; a tab taller than that scrolls.
        page->SetFillsArea(true);

        auto card = DemoKit::CreateCard("QRCodeCard");
        card->SetPadding(4);
        DemoKit::Add(page, card, 1);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
            "QRCodeTabs", 0, 0, 0, 0);
        tabs->AddTab("Encode", BuildEncodePage());
        tabs->AddTab("Decode", BuildDecodePage());
        tabs->SetActiveTab(0);
        DemoKit::Add(card, tabs, 1);
        tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        return page;
    }

} // namespace UltraCanvas
