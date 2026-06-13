#include "UltraCanvasDemo.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasFileLoader.h"
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
            { "None",     QRGradientType::None,     Colors::Black,    Colors::Black },
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

        struct FormatChoice {
            const char* label;
            QRImageFormat format;
            const char* extension;
        };
        const FormatChoice kFormats[] = {
            { "PNG  (.png)",  QRImageFormat::PNG,  "png"  },
            { "JPEG (.jpg)",  QRImageFormat::JPEG, "jpg"  },
            { "QOI  (.qoi)",  QRImageFormat::QOI,  "qoi"  },
            { "WebP (.webp)", QRImageFormat::WebP, "webp" },
            { "AVIF (.avif)", QRImageFormat::AVIF, "avif" },
        };

        FileDialogOptions ImageOpenFilters(const std::string& title) {
            FileDialogOptions opts;
            opts.SetTitle(title)
                .AddFilter("Images", { "png", "jpg", "jpeg", "webp", "qoi", "avif", "bmp", "tiff", "gif" })
                .AddFilter("All files", "*");
            return opts;
        }

        std::shared_ptr<UltraCanvasUIElement> BuildEncodePage() {
            auto page = std::make_shared<UltraCanvasContainer>("QREncodePage", 0, 0, 1100, 720);

            auto title = std::make_shared<UltraCanvasLabel>("QREncodeTitle", 20, 10, 1000, 24);
            title->SetText("Encode — generate a QR code with custom style, logo, and color");
            title->SetFontSize(14);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(50, 50, 150, 255));
            page->AddChild(title);

            auto baseName = [](const std::string& p) -> std::string {
                auto pos = p.find_last_of("/\\");
                return pos == std::string::npos ? p : p.substr(pos + 1);
            };

            auto qr = std::make_shared<UltraCanvasQRCode>("qr-encode", 30, 50, 420, 420);
            qr->SetContent("https://ultraos.eu");
            std::string logoPath = NormalizePath(GetResourcesDir() + "media/images/UOS_logo_white.png");
            auto logoImg = UCImage::Get(logoPath);
            qr->SetLogo(logoImg, 0.25f);
            qr->SetLogoStyle(QRLogoStyle::CenterRounded);
            qr->SetGradient(QRGradientType::Diagonal, Color(0,102,204), Color(102,204,255));
            page->AddChild(qr);
            auto* qrPtr = qr.get();

            auto contentLbl = std::make_shared<UltraCanvasLabel>("ContentLbl", 30, 490, 200, 22);
            contentLbl->SetText("Content:");
            contentLbl->SetFontSize(12);
            contentLbl->SetFontWeight(FontWeight::Bold);
            page->AddChild(contentLbl);

            auto contentInput = std::make_shared<UltraCanvasTextInput>("ContentInput", 30, 513, 420, 32);
            contentInput->SetText("https://ultraos.eu");
            page->AddChild(contentInput);

            auto status = std::make_shared<UltraCanvasLabel>("EncStatus", 30, 552, 420, 22);
            status->SetText("");
            status->SetFontSize(11);
            status->SetTextColor(Color(120, 120, 120, 255));
            page->AddChild(status);
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
                    ss << "\u2014";
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

            const int ctrlX = 480;
            int       ctrlY = 50;
            const int colW  = 600;

            auto header = [&](const std::string& text) {
                auto lbl = std::make_shared<UltraCanvasLabel>(
                    "h_" + text, ctrlX, ctrlY, colW, 22);
                lbl->SetText(text);
                lbl->SetFontSize(12);
                lbl->SetFontWeight(FontWeight::Bold);
                page->AddChild(lbl);
                ctrlY += 26;
            };

            // Builds a compact row of colour swatches positioned to the right of
            // a (narrowed) style dropdown. The selected swatch gets a bold
            // outline; `apply` performs the actual colour change on the QR code.
            auto buildSwatchRow = [page, ctrlX](
                    int yRow, const std::string& idPrefix,
                    std::function<void(const Color&)> apply,
                    std::shared_ptr<std::function<void(int)>> setActive) {
                auto buttons = std::make_shared<std::vector<UltraCanvasButton*>>();
                int x = ctrlX + 160;
                for (int i = 0; i < static_cast<int>(std::size(kSwatches)); ++i) {
                    const auto& s = kSwatches[i];
                    auto b = std::make_shared<UltraCanvasButton>(
                        idPrefix + s.name, x, yRow, 28, 26, "");
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
                    page->AddChild(b);
                    x += 30;
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

                int x = ctrlX;
                for (const auto& p : kPresets) {
                    auto b = std::make_shared<UltraCanvasButton>(
                        std::string("preset_") + p.label, x, ctrlY, 88, 28, p.label);
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
                    page->AddChild(b);
                    x += 92;
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

                ctrlY += 36;
            }

            header("Error correction");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("ecl_dd", ctrlX, ctrlY, 220, 28);
                for (const auto& c : kECLs) dd->AddItem(c.label);
                dd->SetSelectedIndex(1, false);
                dd->onSelectionChanged = [qrPtr, refresh](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kECLs))) {
                        qrPtr->SetErrorCorrection(kECLs[idx].ecl);
                        refresh();
                    }
                };
                page->AddChild(dd);
                ctrlY += 36;
            }

            // Point style + the colour used for the data modules. The dropdown
            // is narrowed so the point-colour swatches fit on the same row.
            header("Point style");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("pt_dd", ctrlX, ctrlY, 150, 28);
                for (const auto& c : kPointStyles) dd->AddItem(c.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kPointStyles))) {
                        qrPtr->SetPointStyle(kPointStyles[idx].style);
                    }
                };
                page->AddChild(dd);

                buildSwatchRow(ctrlY + 1, "ptcol_",
                    [qrPtr, gradResetToNone](const Color& c) {
                        // A solid module colour overrides any gradient; keep the
                        // gradient dropdown in sync by snapping it back to "None".
                        qrPtr->ClearGradient();
                        qrPtr->SetForegroundColor(c);
                        if (*gradResetToNone) (*gradResetToNone)();
                    },
                    setActivePoint);
                ctrlY += 36;
            }

            // Finder style + a dedicated colour for the three finder patterns.
            header("Finder style");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("fnd_dd", ctrlX, ctrlY, 150, 28);
                for (const auto& c : kFinderStyles) dd->AddItem(c.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kFinderStyles))) {
                        qrPtr->SetFinderStyle(kFinderStyles[idx].style);
                    }
                };
                page->AddChild(dd);

                buildSwatchRow(ctrlY + 1, "fndcol_",
                    [qrPtr](const Color& c) { qrPtr->SetFinderColor(c); },
                    setActiveFinder);
                ctrlY += 36;
            }

            header("Foreground gradient");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("grad_dd", ctrlX, ctrlY, 220, 28);
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
                    if (g.type == QRGradientType::None) {
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
                page->AddChild(dd);
                ctrlY += 36;
            }

            header("Logo");
            {
                const int rowY = ctrlY;

                // --- Row 1: upload / remove / placement style --------------
                auto uploadBtn = std::make_shared<UltraCanvasButton>(
                    "logo_upload", ctrlX, rowY, 140, 30, "Upload logo\u2026");
                uploadBtn->SetFontSize(11);

                auto clearBtn = std::make_shared<UltraCanvasButton>(
                    "logo_clear", ctrlX + 148, rowY, 100, 30, "Remove logo");
                clearBtn->SetFontSize(11);

                auto styleDD = std::make_shared<UltraCanvasDropdown>(
                    "logo_style", ctrlX + 256, rowY, 170, 30);
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

                page->AddChild(uploadBtn);
                page->AddChild(clearBtn);
                page->AddChild(styleDD);

                // --- Row 2: thumbnail, file name, Zoom and Margin ----------
                const int row2 = rowY + 38;

                // Dark backdrop so a (white) logo is visible in the thumbnail.
                auto thumbBg = std::make_shared<UltraCanvasContainer>(
                    "logo_thumb_bg", ctrlX, row2, 50, 50);
                thumbBg->SetBackgroundColor(Color(70, 70, 70, 255));
                thumbBg->SetBorders(1.0f, Color(120, 120, 120, 255));
                page->AddChild(thumbBg);

                auto thumb = std::make_shared<UltraCanvasImageElement>(
                    "logo_thumb", ctrlX + 1, row2 + 1, 48, 48);
                thumb->SetFitMode(ImageFitMode::Contain);
                if (logoImg && logoImg->IsValid()) thumb->LoadFromImage(logoImg);
                page->AddChild(thumb);

                auto nameLbl = std::make_shared<UltraCanvasLabel>(
                    "logo_name", ctrlX + 58, row2, 420, 16);
                nameLbl->SetText(baseName(logoPath));
                nameLbl->SetFontSize(11);
                page->AddChild(nameLbl);

                auto zoomLbl = std::make_shared<UltraCanvasLabel>(
                    "logo_zoom_lbl", ctrlX + 58, row2 + 26, 88, 16);
                zoomLbl->SetText("Zoom 25%");
                zoomLbl->SetFontSize(11);
                page->AddChild(zoomLbl);

                auto zoomSlider = std::make_shared<UltraCanvasSlider>(
                    "logo_zoom", ctrlX + 148, row2 + 26, 120, 20);
                zoomSlider->SetRange(5, 40);   // percent of the QR width
                zoomSlider->SetStep(1);
                zoomSlider->SetValue(25);      // matches the initial logoScale 0.25
                page->AddChild(zoomSlider);

                auto marginLbl = std::make_shared<UltraCanvasLabel>(
                    "logo_margin_lbl", ctrlX + 278, row2 + 26, 90, 16);
                marginLbl->SetText("Margin 4px");
                marginLbl->SetFontSize(11);
                page->AddChild(marginLbl);

                auto marginSlider = std::make_shared<UltraCanvasSlider>(
                    "logo_margin", ctrlX + 372, row2 + 26, 120, 20);
                marginSlider->SetRange(0, 20);
                marginSlider->SetStep(1);
                marginSlider->SetValue(4);     // matches the default logoBorderWidth
                page->AddChild(marginSlider);

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
                    auto opts = ImageOpenFilters("Choose a logo");
                    std::string path = UltraCanvasNativeDialogs::OpenFile(opts);
                    if (path.empty()) return;
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

                clearBtn->SetOnClick([qrPtr, statusPtr, thumbPtr, nameLblPtr]() {
                    qrPtr->ClearLogo();
                    thumbPtr->LoadFromImage(std::make_shared<UCImage>());
                    nameLblPtr->SetText("(no logo)");
                    statusPtr->SetText("Logo removed");
                });

                ctrlY = row2 + 56;
            }

            header("Save as image");
            {
                auto formatDD = std::make_shared<UltraCanvasDropdown>(
                    "save_fmt", ctrlX, ctrlY, 180, 30);
                for (const auto& f : kFormats) formatDD->AddItem(f.label);
                formatDD->SetSelectedIndex(0, false);
                page->AddChild(formatDD);

                auto saveBtn = std::make_shared<UltraCanvasButton>(
                    "save_btn", ctrlX + 190, ctrlY, 110, 30, "Save\u2026");
                saveBtn->SetFontSize(11);
                auto* fmtPtr = formatDD.get();
                saveBtn->SetOnClick([qrPtr, fmtPtr, statusPtr, activeSlug]() {
                    int idx = fmtPtr->GetSelectedIndex();
                    if (idx < 0 || idx >= static_cast<int>(std::size(kFormats))) return;
                    const auto& f = kFormats[idx];
                    FileDialogOptions opts;
                    opts.SetTitle("Save QR code")
                        .SetDefaultFileName(std::string("ultracanvas_") + *activeSlug + "." + f.extension)
                        .AddFilter(f.label, f.extension);
                    std::string path = UltraCanvasNativeDialogs::SaveFile(opts);
                    if (path.empty()) return;
                    std::string err;
                    bool ok = qrPtr->ExportToImage(path, f.format, 12, &err);
                    statusPtr->SetText(ok ? ("Saved: " + path) : ("Save failed: " + err));
                });
                page->AddChild(saveBtn);

                auto svgBtn = std::make_shared<UltraCanvasButton>(
                    "save_svg", ctrlX + 310, ctrlY, 110, 30, "Save SVG\u2026");
                svgBtn->SetFontSize(11);
                svgBtn->SetOnClick([qrPtr, statusPtr, activeSlug]() {
                    FileDialogOptions opts;
                    opts.SetTitle("Save QR code (SVG)")
                        .SetDefaultFileName(std::string("ultracanvas_") + *activeSlug + ".svg")
                        .AddFilter("SVG", "svg");
                    std::string path = UltraCanvasNativeDialogs::SaveFile(opts);
                    if (path.empty()) return;
                    bool ok = qrPtr->ExportToSVG(path, 10);
                    statusPtr->SetText(ok ? ("Saved: " + path) : "SVG export failed");
                });
                page->AddChild(svgBtn);
                ctrlY += 40;
            }

            refresh();
            return page;
        }

        std::shared_ptr<UltraCanvasUIElement> BuildDecodePage() {
            auto page = std::make_shared<UltraCanvasContainer>("QRDecodePage", 0, 0, 1100, 720);

            auto title = std::make_shared<UltraCanvasLabel>("DecTitle", 20, 10, 1000, 24);
            title->SetText("Decode — open an image and scan it for QR codes");
            title->SetFontSize(14);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(50, 50, 150, 255));
            page->AddChild(title);

            auto hint = std::make_shared<UltraCanvasLabel>("DecHint", 20, 40, 1000, 22);
            if (QRCodeUtils::IsDecoderAvailable()) {
                hint->SetText("Click \"Open image\u2026\" to choose a photo or screenshot containing one or more QR codes.");
            } else {
                hint->SetText("Decoder support is not built in — install libzbar-dev and rebuild to enable scanning.");
            }
            hint->SetFontSize(11);
            hint->SetTextColor(Color(110, 110, 110, 255));
            page->AddChild(hint);

            auto openBtn = std::make_shared<UltraCanvasButton>(
                "DecOpen", 20, 72, 160, 32, "Open image\u2026");
            openBtn->SetFontSize(12);
            // Use SetColors (not SetBackgroundColor) so the button's normal/
            // hover/pressed style colors are set \u2014 otherwise it keeps the
            // default light ButtonFace and white text looks disabled.
            openBtn->SetColors(Color(40, 167, 69), Color(34, 142, 59),
                               Color(28, 117, 49), Color(40, 167, 69));
            openBtn->SetTextColors(Colors::White, Colors::White,
                                   Colors::White, Colors::White);
            page->AddChild(openBtn);

            auto preview = std::make_shared<UltraCanvasImageElement>(
                "DecPreview", 20, 116, 480, 444);
            preview->SetFitMode(ImageFitMode::Contain);
            page->AddChild(preview);
            auto* previewPtr = preview.get();

            auto resultsTitle = std::make_shared<UltraCanvasLabel>(
                "DecResultsTitle", 520, 100, 560, 22);
            resultsTitle->SetText("Scan result");
            resultsTitle->SetFontSize(13);
            resultsTitle->SetFontWeight(FontWeight::Bold);
            page->AddChild(resultsTitle);

            auto resultsBody = std::make_shared<UltraCanvasLabel>(
                "DecResultsBody", 520, 130, 560, 450);
            resultsBody->SetText("(no image scanned yet)");
            resultsBody->SetFontSize(11);
            resultsBody->SetTextColor(Color(70, 70, 70, 255));
            resultsBody->SetWrap(TextWrap::WrapWordChar);
            page->AddChild(resultsBody);
            auto* resultsPtr = resultsBody.get();

            auto status = std::make_shared<UltraCanvasLabel>(
                "DecStatus", 196, 77, 884, 22);
            status->SetText("");
            status->SetFontSize(11);
            status->SetTextColor(Color(120, 120, 120, 255));
            page->AddChild(status);
            auto* statusPtr = status.get();

            openBtn->SetOnClick([previewPtr, resultsPtr, statusPtr]() {
                auto opts = ImageOpenFilters("Open image to scan");
                std::string path = UltraCanvasNativeDialogs::OpenFile(opts);
                if (path.empty()) return;

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

            return page;
        }

    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateQRCodeExamples() {
        auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
            "QRCodeTabs", 0, 0, 1100, 760);
        tabs->AddTab("Encode", BuildEncodePage());
        tabs->AddTab("Decode", BuildDecodePage());
        tabs->SetActiveTab(0);
        return tabs;
    }

} // namespace UltraCanvas
