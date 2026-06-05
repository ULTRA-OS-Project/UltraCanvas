#include "UltraCanvasDemo.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasFileLoader.h"
#include <array>
#include <sstream>

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

        struct PresetChoice { const char* label; std::string (*build)(); };
        const PresetChoice kPresets[] = {
            { "URL",   []() { return QRCodeUtils::CreateURLContent("https://ultracanvas.dev"); } },
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

            auto qr = std::make_shared<UltraCanvasQRCode>("qr-encode", 30, 50, 420, 420);
            qr->SetContent("https://ultracanvas.dev");
            page->AddChild(qr);
            auto* qrPtr = qr.get();

            auto contentLbl = std::make_shared<UltraCanvasLabel>("ContentLbl", 30, 480, 200, 22);
            contentLbl->SetText("Content:");
            contentLbl->SetFontSize(12);
            contentLbl->SetFontWeight(FontWeight::Bold);
            page->AddChild(contentLbl);

            auto contentInput = std::make_shared<UltraCanvasTextInput>("ContentInput", 30, 504, 420, 32);
            contentInput->SetText("https://ultracanvas.dev");
            page->AddChild(contentInput);

            auto status = std::make_shared<UltraCanvasLabel>("EncStatus", 30, 542, 420, 22);
            status->SetText("");
            status->SetFontSize(11);
            status->SetTextColor(Color(120, 120, 120, 255));
            page->AddChild(status);
            auto* statusPtr = status.get();

            auto refresh = [qrPtr, statusPtr]() {
                std::ostringstream ss;
                if (qrPtr->IsValid()) {
                    ss << "Version " << qrPtr->GetVersion()
                       << "  •  " << qrPtr->GetModuleCount() << "\xd7"
                       << qrPtr->GetModuleCount() << " modules";
                } else if (!qrPtr->GetErrorMessage().empty()) {
                    ss << "Error: " << qrPtr->GetErrorMessage();
                } else {
                    ss << "\u2014";
                }
                statusPtr->SetText(ss.str());
            };

            contentInput->onTextChanged = [qrPtr, refresh](const std::string& s) {
                qrPtr->SetContent(s);
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

            header("Content presets");
            {
                int x = ctrlX;
                for (const auto& p : kPresets) {
                    auto b = std::make_shared<UltraCanvasButton>(
                        std::string("preset_") + p.label, x, ctrlY, 88, 28, p.label);
                    b->SetFontSize(11);
                    auto build = p.build;
                    auto* inputPtr = contentInput.get();
                    b->SetOnClick([qrPtr, inputPtr, build, refresh]() {
                        std::string s = build();
                        inputPtr->SetText(s);
                        qrPtr->SetContent(s);
                        refresh();
                    });
                    page->AddChild(b);
                    x += 92;
                }
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

            header("Point style");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("pt_dd", ctrlX, ctrlY, 220, 28);
                for (const auto& c : kPointStyles) dd->AddItem(c.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kPointStyles))) {
                        qrPtr->SetPointStyle(kPointStyles[idx].style);
                    }
                };
                page->AddChild(dd);
                ctrlY += 36;
            }

            header("Finder style");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("fnd_dd", ctrlX, ctrlY, 220, 28);
                for (const auto& c : kFinderStyles) dd->AddItem(c.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx >= 0 && idx < static_cast<int>(std::size(kFinderStyles))) {
                        qrPtr->SetFinderStyle(kFinderStyles[idx].style);
                    }
                };
                page->AddChild(dd);
                ctrlY += 36;
            }

            header("Foreground gradient");
            {
                auto dd = std::make_shared<UltraCanvasDropdown>("grad_dd", ctrlX, ctrlY, 220, 28);
                for (const auto& g : kGradients) dd->AddItem(g.label);
                dd->SetSelectedIndex(0, false);
                dd->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    if (idx < 0 || idx >= static_cast<int>(std::size(kGradients))) return;
                    const auto& g = kGradients[idx];
                    if (g.type == QRGradientType::None) {
                        qrPtr->ClearGradient();
                        qrPtr->SetForegroundColor(Colors::Black);
                    } else {
                        qrPtr->SetGradient(g.type, g.start, g.end);
                    }
                };
                page->AddChild(dd);
                ctrlY += 36;
            }

            header("Foreground color");
            {
                struct Swatch { const char* name; Color color; };
                const Swatch swatches[] = {
                    { "Black",  Colors::Black },
                    { "Indigo", Color(0, 0, 139) },
                    { "Teal",   Color(0, 128, 128) },
                    { "Crimson",Color(139, 0, 0) },
                    { "Purple", Color(139, 0, 139) },
                    { "Amber",  Color(255, 140, 0) },
                };
                int x = ctrlX;
                for (const auto& s : swatches) {
                    auto b = std::make_shared<UltraCanvasButton>(
                        std::string("sw_") + s.name, x, ctrlY, 60, 28, s.name);
                    b->SetFontSize(10);
                    b->SetColors(s.color);
                    b->SetTextColors(Colors::White);
                    auto col = s.color;
                    b->SetOnClick([qrPtr, col]() {
                        qrPtr->ClearGradient();
                        qrPtr->SetForegroundColor(col);
                    });
                    page->AddChild(b);
                    x += 64;
                }
                ctrlY += 36;
            }

            header("Logo");
            {
                auto uploadBtn = std::make_shared<UltraCanvasButton>(
                    "logo_upload", ctrlX, ctrlY, 150, 30, "Upload logo\u2026");
                uploadBtn->SetFontSize(11);
                uploadBtn->SetOnClick([qrPtr, statusPtr]() {
                    auto opts = ImageOpenFilters("Choose a logo");
                    std::string path = UltraCanvasNativeDialogs::OpenFile(opts);
                    if (path.empty()) return;
                    auto img = UCImage::Get(path);
                    if (!img || !img->IsValid()) {
                        statusPtr->SetText("Failed to load logo: " + path);
                        return;
                    }
                    qrPtr->SetErrorCorrection(QRErrorCorrection::High);
                    qrPtr->SetLogo(img, 0.22f);
                    qrPtr->SetLogoStyle(QRLogoStyle::CenterWithBorder);
                    statusPtr->SetText("Logo loaded: " + path);
                });
                page->AddChild(uploadBtn);

                auto clearBtn = std::make_shared<UltraCanvasButton>(
                    "logo_clear", ctrlX + 160, ctrlY, 110, 30, "Remove logo");
                clearBtn->SetFontSize(11);
                clearBtn->SetOnClick([qrPtr, statusPtr]() {
                    qrPtr->ClearLogo();
                    statusPtr->SetText("Logo removed");
                });
                page->AddChild(clearBtn);

                auto styleDD = std::make_shared<UltraCanvasDropdown>(
                    "logo_style", ctrlX + 280, ctrlY, 180, 30);
                styleDD->AddItem("Center");
                styleDD->AddItem("Center + Border");
                styleDD->AddItem("Center Rounded");
                styleDD->AddItem("Center Circle");
                styleDD->SetSelectedIndex(1, false);
                styleDD->onSelectionChanged = [qrPtr](int idx, const DropdownItem&) {
                    QRLogoStyle s = QRLogoStyle::CenterWithBorder;
                    switch (idx) {
                        case 0: s = QRLogoStyle::Center; break;
                        case 1: s = QRLogoStyle::CenterWithBorder; break;
                        case 2: s = QRLogoStyle::CenterRounded; break;
                        case 3: s = QRLogoStyle::CenterCircle; break;
                    }
                    qrPtr->SetLogoStyle(s);
                };
                page->AddChild(styleDD);
                ctrlY += 40;
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
                saveBtn->SetOnClick([qrPtr, fmtPtr, statusPtr]() {
                    int idx = fmtPtr->GetSelectedIndex();
                    if (idx < 0 || idx >= static_cast<int>(std::size(kFormats))) return;
                    const auto& f = kFormats[idx];
                    FileDialogOptions opts;
                    opts.SetTitle("Save QR code")
                        .SetDefaultFileName(std::string("ultracanvas_qr.") + f.extension)
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
                svgBtn->SetOnClick([qrPtr, statusPtr]() {
                    FileDialogOptions opts;
                    opts.SetTitle("Save QR code (SVG)")
                        .SetDefaultFileName("ultracanvas_qr.svg")
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
                "DecResultsTitle", 520, 80, 560, 22);
            resultsTitle->SetText("Scan result");
            resultsTitle->SetFontSize(13);
            resultsTitle->SetFontWeight(FontWeight::Bold);
            page->AddChild(resultsTitle);

            auto resultsBody = std::make_shared<UltraCanvasLabel>(
                "DecResultsBody", 520, 110, 560, 450);
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
