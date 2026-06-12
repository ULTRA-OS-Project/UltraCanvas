// Apps/DemoApp/UltraCanvasSlideshowExamples.cpp
// Demonstration of UltraCanvasSlideshow: indicator styles, info-panel layouts,
// indicator edges and transition styles. The option buttons are laid out in a
// labelled-row grid (label column on the left, wrapping option buttons on the
// right) grouped Controls / Indicator / Fade / Panel layout / Image / Letterbox,
// with the active choice highlighted like a radio group.
// Version: 1.2.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasSlideshow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSeparator.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <utility>

namespace UltraCanvas {

    namespace {
        struct StyleChoice {
            const char* label;
            SlideshowIndicatorShape shape;
            float itemWidth;
            float itemHeight;
            float spacing;
            float cornerRadius;
        };

        // Tuned defaults for each indicator style so the demo button switches
        // produce a visually distinct result without further tweaking.
        const StyleChoice kStyles[] = {
            { "Bars",           SlideshowIndicatorShape::Bars,        30.0f, 3.0f,  6.0f, 0.0f },
            { "Dots",           SlideshowIndicatorShape::Dots,         9.0f, 9.0f, 10.0f, 0.0f },
            { "Progress Bar",   SlideshowIndicatorShape::ProgressBar, 30.0f, 3.0f,  0.0f, 1.5f },
            { "Story Bars",     SlideshowIndicatorShape::StoryBars,   30.0f, 3.0f,  4.0f, 1.5f },
            { "Counter",        SlideshowIndicatorShape::Counter,     30.0f, 3.0f,  6.0f, 0.0f },
            { "Thumbnails",     SlideshowIndicatorShape::Thumbnails,  60.0f, 40.0f, 8.0f, 0.0f },
            { "Labels",         SlideshowIndicatorShape::Labels,      80.0f, 3.0f,  4.0f, 0.0f },
            { "Hidden",         SlideshowIndicatorShape::Hidden,      30.0f, 3.0f,  6.0f, 0.0f },
        };

        SlideshowConfig MakeBaseConfig() {
            SlideshowConfig cfg;
            cfg.slideDuration   = std::chrono::milliseconds(5000);
            cfg.fadeDuration    = std::chrono::milliseconds(700);
            cfg.fadeStyle       = SlideshowFadeStyle::CrossFade;
            cfg.textMode        = SlideshowTextMode::PerSlide;
            cfg.imagePanelRatio = 0.62f;
            cfg.autoPlay        = true;
            cfg.pauseOnHover    = true;
            cfg.loop            = true;
            cfg.backgroundColor = Color(20, 20, 22, 255);
            cfg.infoPanelColor  = Color(248, 248, 248, 255);
            cfg.titleColor      = Color(20, 20, 22, 255);
            cfg.bodyColor       = Color(70, 70, 70, 255);
            cfg.titleFontSize   = 22.0f;
            cfg.bodyFontSize    = 13.0f;
            cfg.infoPadding     = 24;
            cfg.indicators.shape          = SlideshowIndicatorShape::Bars;
            cfg.indicators.itemWidth      = 30.0f;
            cfg.indicators.itemHeight     = 3.0f;
            cfg.indicators.spacing        = 6.0f;
            cfg.indicators.inactiveColor  = Color(200, 200, 200, 200);
            cfg.indicators.activeColor    = Color(0, 122, 224, 255);
            cfg.indicators.hoverColor     = Color(140, 140, 140, 255);
            cfg.indicators.marginFromEdge = 18;
            return cfg;
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSlideshowExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("SlideshowExamples", 0, 0, 1000, 1070);

        root->SetPadding(0,5,5,0);
        // Title
        auto title = std::make_shared<UltraCanvasLabel>("SlideshowTitle", 20, 10, 800, 30);
        title->SetText("UltraCanvas Slideshow widget");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("SlideshowSubtitle", 20, 42, 900, 22);
        subtitle->SetText("Image cross-fades on a timer with an info panel beside or over it. "
                          "Pick the indicator style, info-panel layout, indicator edge and transition "
                          "with the buttons below; hover the slideshow to pause.");
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(110, 110, 110, 255));
        root->AddChild(subtitle);

        // Slideshow itself
        auto show = CreateSlideshow("ultracanvas-slideshow", 20, 80, 920, 480);

        SlideshowConfig cfg = MakeBaseConfig();

        // Build slide list from images that ship in media/. We pair each image
        // with descriptive German text about a generic electric vehicle so the
        // info panel has realistic, varied content to lay out.
        const std::string mediaRoot = NormalizePath(GetResourcesDir() + "media/images/");

        show->AddSlide({mediaRoot + "landscape.jpg",
                        "Komfort neu definiert",
                        "Der Innenraum verbindet nahtlos Komfort und stilvolles Design "
                        "mit durchdachten Funktionen, die jeden Moment besonders machen. "
                        "Die außergewöhnliche Ruhe, die charakteristisch für "
                        "Elektrofahrzeuge ist, sorgt für ein einzigartiges Fahrerlebnis."});

        show->AddSlide({mediaRoot + "portrait.jpg",
                        "Design, das auffällt",
                        "Markantes Design trifft auf Aerodynamik. Jede Linie ist mit "
                        "Bedacht gezeichnet — für einen unverwechselbaren Auftritt "
                        "und maximale Effizienz auf langen Strecken."});

        show->AddSlide({mediaRoot + "sample_photo.jpg",
                        "Performance",
                        "Souveräne Beschleunigung und präzise Lenkung machen jede "
                        "Fahrt zum Erlebnis. Das symmetrische Allradantrieb-System "
                        "sorgt zuverlässig für Traktion auf jedem Untergrund."});

        show->AddSlide({mediaRoot + "sample_hq.jpg",
                        "Sicherheit",
                        "Modernste Assistenzsysteme überwachen das Umfeld in Echtzeit. "
                        "Adaptive Geschwindigkeitsregelung, Spurhalteassistent und "
                        "Notbremsassistent arbeiten Hand in Hand."});

        show->AddSlide({mediaRoot + "screenshot.png",
                        "Care App",
                        "Bleiben Sie immer verbunden mit Ihrem Fahrzeug — Status, "
                        "Ladestand und Servicetermine direkt auf Ihrem Smartphone."});

        show->SetConfig(cfg);
        show->Play();
        root->AddChild(show);

        // Status label — updated when a slide changes
        auto statusLabel = std::make_shared<UltraCanvasLabel>("SlideStatus", 20, 575, 920, 22);
        statusLabel->SetText("Slide 1 / 5");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(120, 120, 120, 255));
        root->AddChild(statusLabel);

        auto* showPtr = show.get();
        auto* statusPtr = statusLabel.get();
        show->onSlideChanged = [showPtr, statusPtr](size_t idx) {
            std::ostringstream oss;
            oss << "Slide " << (idx + 1) << " / " << showPtr->GetSlideCount()
                << " — " << showPtr->GetSlides()[idx].infoTitle;
            statusPtr->SetText(oss.str());
        };

        // ====================================================================
        // Options panel — a labelled-row grid mirroring the reference layout:
        // a fixed label column on the left and a wrapping row of option buttons
        // on the right. Each group behaves like a radio: clicking a button
        // highlights it (gold border + darker fill) and clears its siblings.
        // ====================================================================

        // ---- Geometry ----
        const int kLabelX    = 20;            // main-row label column
        const int kContentX  = 150;           // button area / sub-label column
        const int kContentR  = 980;           // buttons may not cross this edge
        const int kSubLabelW = 70;            // sub-row label column width
        const int kSubBtnX   = kContentX + kSubLabelW + 10;  // sub-row buttons
        const int kBtnH      = 26;
        const int kBtnGap    = 6;
        const int kRowGap    = 12;

        // ---- Palette (hex values taken from the reference mockup) ----
        struct BtnVisual { Color baseBg, selBg, text, border; };
        const BtnVisual indV { Color(251,233,205,255), Color(246,214,158,255),
                               Color(90,74,42,255),    Color(0,0,0,77) };
        const BtnVisual fadeV{ Color(237,253,248,255), Color(191,245,229,255),
                               Color(15,110,86,255),   Color(0,0,0,77) };
        const BtnVisual ctrlV{ Color(255,255,255,255), Color(236,236,236,255),
                               Color(27,27,26,255),    Color(0,0,0,77) };
        const BtnVisual offV { Color(226,75,74,255),   Color(226,75,74,255),
                               Color(255,255,255,255), Color(163,45,45,255) };
        const Color kSelBorder(201,143,46,255);          // #C98F2E
        const Color kLabelColor(27,27,26,255);
        const Color kSubLabelColor(107,107,102,255);

        // Rough content-width estimate so each button hugs its label.
        auto estW = [](const std::string& s) {
            return std::max(46, (int)std::lround(s.size() * 7.0 + 24));
        };

        using BtnGroup = std::shared_ptr<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>;

        // Paint a button in its normal or selected state.
        auto applyVisual = [kSelBorder](UltraCanvasButton* b, const BtnVisual& v, bool sel) {
            b->SetColors(sel ? v.selBg : v.baseBg, v.selBg);
            b->SetTextColors(v.text);
            b->SetBorder(sel ? 2.0f : 1.0f, sel ? kSelBorder : v.border);
        };

        // A non-radio control button (Previous / Pause / Next).
        auto addCtrl = [&](const std::string& id, int& x, int y,
                           const std::string& label, std::function<void()> cb) {
            int w = estW(label);
            auto b = std::make_shared<UltraCanvasButton>(id, (float)x, (float)y,
                                                         (float)w, (float)kBtnH, label);
            b->SetFontSize(11);
            b->SetCornerRadius(6.0f);
            applyVisual(b.get(), ctrlV, false);
            if (cb) b->SetOnClick(cb);
            root->AddChild(b);
            x += w + kBtnGap;
            return b;
        };

        // One radio button registered in `group`; clicking selects it and
        // restyles every sibling, then runs the supplied action.
        auto addRadio = [&](BtnGroup group, int x, int y, const std::string& label,
                            const BtnVisual& v, bool selected, std::function<void()> cb) {
            auto btn = std::make_shared<UltraCanvasButton>(
                    "opt_" + label + "_" + std::to_string(x) + "_" + std::to_string(y),
                    (float)x, (float)y, (float)estW(label), (float)kBtnH, label);
            btn->SetFontSize(11);
            btn->SetCornerRadius(6.0f);
            applyVisual(btn.get(), v, selected);
            auto* raw = btn.get();
            group->push_back({raw, v});
            btn->SetOnClick([group, raw, cb, applyVisual]() {
                for (auto& pr : *group) applyVisual(pr.first, pr.second, pr.first == raw);
                if (cb) cb();
            });
            root->AddChild(btn);
        };

        // One option in a flowed (wrapping) radio row. A `brk` entry forces the
        // next item onto a new line.
        struct RadioSpec {
            std::string label;
            BtnVisual vis;
            bool selected = false;
            bool brk = false;
            std::function<void()> cb;
        };
        auto flowRadio = [&](BtnGroup group, int startX, int& y,
                             const std::vector<RadioSpec>& specs) {
            int x = startX;
            for (const auto& s : specs) {
                if (s.brk) { x = startX; y += kBtnH + kBtnGap; continue; }
                int w = estW(s.label);
                if (x != startX && x + w > kContentR) { x = startX; y += kBtnH + kBtnGap; }
                addRadio(group, x, y, s.label, s.vis, s.selected, s.cb);
                x += w + kBtnGap;
            }
        };

        // A row / sub-row caption.
        auto addLabel = [&](const std::string& text, int x, int y, int w,
                            float fs, bool bold, Color col) {
            auto lbl = std::make_shared<UltraCanvasLabel>(
                    "lbl_" + text + "_" + std::to_string(y),
                    (float)x, (float)y, (float)w, 22.0f);
            lbl->SetText(text);
            lbl->SetFontSize(fs);
            if (bold) lbl->SetFontWeight(FontWeight::Bold);
            lbl->SetTextColor(col);
            root->AddChild(lbl);
        };

        // A horizontal divider between sections.
        auto addDivider = [&](int& y, bool tight) {
            auto sep = std::make_shared<UltraCanvasSeparator>(
                    false, 1, kContentR - kLabelX, Color(0, 0, 0, 38));
            sep->SetElementAbsolutePosition(Point2Df((float)kLabelX, (float)y));
            root->AddChild(sep);
            y += tight ? 8 : 12;
        };

        int y = 600;

        // ----- Controls -----
        addLabel("Controls", kLabelX, y + 4, 120, 12.0f, false, kLabelColor);
        {
            int cx = kContentX;
            addCtrl("slide_prev", cx, y, "‹ Previous",
                    [showPtr]() { showPtr->Previous(); });
            auto pauseBtn = addCtrl("slide_playpause", cx, y, "Pause", nullptr);
            addCtrl("slide_next", cx, y, "Next ›",
                    [showPtr]() { showPtr->Next(); });
            auto* pauseBtnPtr = pauseBtn.get();
            pauseBtn->SetOnClick([showPtr, pauseBtnPtr]() {
                if (showPtr->IsPlaying()) {
                    showPtr->Pause();
                    pauseBtnPtr->SetText("Play");
                } else {
                    showPtr->Play();
                    pauseBtnPtr->SetText("Pause");
                }
            });
        }
        y += kBtnH + kRowGap;
        addDivider(y, false);

        // ----- Indicator style -----
        addLabel("Indicator", kLabelX, y + 4, 120, 12.0f, false, kLabelColor);
        {
            auto indGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();
            std::vector<RadioSpec> specs;
            const size_t styleCount = sizeof(kStyles) / sizeof(kStyles[0]);
            for (size_t i = 0; i < styleCount; ++i) {
                const auto& choice = kStyles[i];
                bool isOff = (choice.shape == SlideshowIndicatorShape::Hidden);
                std::string disp = isOff ? "OFF" : (i == 0 ? "Bars" : choice.label);
                auto styleCopy = choice;
                specs.push_back({ disp, isOff ? offV : indV,
                    choice.shape == SlideshowIndicatorShape::Bars, false,
                    [showPtr, styleCopy]() {
                        SlideshowIndicatorStyle is = showPtr->GetIndicatorStyle();
                        is.shape        = styleCopy.shape;
                        is.itemWidth    = styleCopy.itemWidth;
                        is.itemHeight   = styleCopy.itemHeight;
                        is.spacing      = styleCopy.spacing;
                        is.cornerRadius = styleCopy.cornerRadius;
                        showPtr->SetIndicatorStyle(is);
                    } });
            }
            flowRadio(indGroup, kContentX, y, specs);
        }
        y += kBtnH + kRowGap;

        // ----- Indicator edge -----
        addLabel("Indicator edge", kLabelX, y + 4, 130, 12.0f, false, kLabelColor);
        {
            struct EdgeChoice { const char* label; SlideshowIndicatorEdge edge; };
            const EdgeChoice edges[] = {
                { "Bottom", SlideshowIndicatorEdge::Bottom },
                { "Top",    SlideshowIndicatorEdge::Top },
                { "Left",   SlideshowIndicatorEdge::Left },
                { "Right",  SlideshowIndicatorEdge::Right },
            };
            auto edgeGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();
            std::vector<RadioSpec> specs;
            for (const auto& ec : edges) {
                auto edgeVal = ec.edge;
                specs.push_back({ ec.label, indV,
                    ec.edge == SlideshowIndicatorEdge::Bottom, false,
                    [showPtr, edgeVal]() {
                        SlideshowIndicatorStyle is = showPtr->GetIndicatorStyle();
                        is.edge = edgeVal;
                        showPtr->SetIndicatorStyle(is);
                    } });
            }
            flowRadio(edgeGroup, kContentX, y, specs);
        }
        y += kBtnH + kRowGap;
        addDivider(y, true);

        // ----- Fade style -----
        addLabel("Fade style", kLabelX, y + 4, 110, 12.0f, false, kLabelColor);
        {
            struct FadeChoice { const char* label; SlideshowFadeStyle style; };
            const FadeChoice fades[] = {
                { "CrossFade",  SlideshowFadeStyle::CrossFade },
                { "FadeOut/In", SlideshowFadeStyle::FadeOutIn },
                { "Slide H",    SlideshowFadeStyle::SlideHorizontal },
                { "Slide V",    SlideshowFadeStyle::SlideVertical },
                { "Zoom",       SlideshowFadeStyle::ZoomFade },
                { "Instant",    SlideshowFadeStyle::NoFade },
            };
            auto fadeGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();
            std::vector<RadioSpec> specs;
            for (const auto& f : fades) {
                auto styleVal = f.style;
                specs.push_back({ f.label, fadeV,
                    f.style == SlideshowFadeStyle::CrossFade, false,
                    [showPtr, styleVal]() {
                        SlideshowConfig c = showPtr->GetConfig();
                        c.fadeStyle = styleVal;
                        showPtr->SetConfig(c);
                        showPtr->Play();
                    } });
            }
            flowRadio(fadeGroup, kContentX, y, specs);
        }
        y += kBtnH + kRowGap;
        addDivider(y, false);

        // ----- Panel layout (Split / Overlay positions, plus OFF) -----
        // A single radio group spread across three sub-rows. Split layouts give
        // the panel its own region; overlay layouts float it over the image.
        addLabel("Panel layout", kLabelX, y + 4, 120, 12.0f, false, kLabelColor);
        {
            auto panelCb = [showPtr](SlideshowInfoLayout layoutVal) {
                return [showPtr, layoutVal]() {
                    SlideshowConfig c = showPtr->GetConfig();
                    c.infoLayout = layoutVal;
                    // Light text reads on the overlay scrim; dark text on the
                    // opaque split panel.
                    if (SlideshowLayoutIsOverlay(layoutVal)) {
                        c.titleColor = Color(255, 255, 255, 255);
                        c.bodyColor  = Color(235, 235, 235, 255);
                    } else {
                        c.titleColor = Color(20, 20, 22, 255);
                        c.bodyColor  = Color(70, 70, 70, 255);
                    }
                    showPtr->SetConfig(c);
                    showPtr->Play();
                };
            };
            auto panelGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();

            // Split sub-row (SplitRight is the config default).
            addLabel("Split", kContentX, y + 4, kSubLabelW, 11.0f, false, kSubLabelColor);
            std::vector<RadioSpec> splitSpecs = {
                { "Left",   indV, false, false, panelCb(SlideshowInfoLayout::SplitLeft) },
                { "Right",  indV, true,  false, panelCb(SlideshowInfoLayout::SplitRight) },
                { "Top",    indV, false, false, panelCb(SlideshowInfoLayout::SplitTop) },
                { "Bottom", indV, false, false, panelCb(SlideshowInfoLayout::SplitBottom) },
            };
            flowRadio(panelGroup, kSubBtnX, y, splitSpecs);
            y += kBtnH + kBtnGap;

            // Overlay sub-row (edges, then corners / center / full).
            addLabel("Overlay", kContentX, y + 4, kSubLabelW, 11.0f, false, kSubLabelColor);
            std::vector<RadioSpec> overlaySpecs = {
                { "Left",         indV, false, false, panelCb(SlideshowInfoLayout::OverlayLeft) },
                { "Right",        indV, false, false, panelCb(SlideshowInfoLayout::OverlayRight) },
                { "Top",          indV, false, false, panelCb(SlideshowInfoLayout::OverlayTop) },
                { "Bottom",       indV, false, false, panelCb(SlideshowInfoLayout::OverlayBottom) },
                { "",             indV, false, true,  nullptr },
                { "Top left",     indV, false, false, panelCb(SlideshowInfoLayout::OverlayTopLeft) },
                { "Top right",    indV, false, false, panelCb(SlideshowInfoLayout::OverlayTopRight) },
                { "Bottom left",  indV, false, false, panelCb(SlideshowInfoLayout::OverlayBottomLeft) },
                { "Bottom right", indV, false, false, panelCb(SlideshowInfoLayout::OverlayBottomRight) },
                { "Center",       indV, false, false, panelCb(SlideshowInfoLayout::OverlayCenter) },
                { "Full",         indV, false, false, panelCb(SlideshowInfoLayout::OverlayFull) },
            };
            flowRadio(panelGroup, kSubBtnX, y, overlaySpecs);
            y += kBtnH + kBtnGap;

            // OFF sub-row (no panel).
            addLabel(" ", kContentX, y + 4, kSubLabelW, 11.0f, false, kSubLabelColor);
            std::vector<RadioSpec> offSpecs = {
                { "OFF", offV, false, false, panelCb(SlideshowInfoLayout::Hidden) },
            };
            flowRadio(panelGroup, kSubBtnX, y, offSpecs);
        }
        y += kBtnH + kRowGap;
        addDivider(y, true);

        // ----- Image (Fit + Crop focus) -----
        addLabel("Image", kLabelX, y + 4, 120, 12.0f, false, kLabelColor);
        {
            auto fitGroup   = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();
            auto focusGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();

            // Crop focus only applies to a Cover crop — dim it otherwise.
            auto setFocusEnabled = [focusGroup](bool en) {
                for (auto& pr : *focusGroup) pr.first->SetDisabled(!en);
            };

            // Fit sub-row (Cover is the config default).
            addLabel("Fit", kContentX, y + 4, kSubLabelW, 11.0f, false, kSubLabelColor);
            struct FitChoice { const char* label; ImageFitMode fit; };
            const FitChoice fits[] = {
                { "Cover (crop)",   ImageFitMode::Cover },
                { "Contain",        ImageFitMode::Contain },
                { "Fill (stretch)", ImageFitMode::Fill },
                { "Scale Down",     ImageFitMode::ScaleDown },
            };
            std::vector<RadioSpec> fitSpecs;
            for (const auto& fc : fits) {
                auto fitVal = fc.fit;
                fitSpecs.push_back({ fc.label, indV,
                    fc.fit == ImageFitMode::Cover, false,
                    [showPtr, setFocusEnabled, fitVal]() {
                        SlideshowConfig c = showPtr->GetConfig();
                        c.imageFit = fitVal;
                        showPtr->SetConfig(c);
                        showPtr->Play();
                        setFocusEnabled(fitVal == ImageFitMode::Cover);
                    } });
            }
            flowRadio(fitGroup, kSubBtnX, y, fitSpecs);
            y += kBtnH + kBtnGap;

            // Crop focus sub-row (center is the config default).
            addLabel("Crop focus", kContentX, y + 4, kSubLabelW + 10, 11.0f, false, kSubLabelColor);
            struct FocusChoice { const char* label; float x; float y; };
            const FocusChoice focuses[] = {
                { "Focus TL",     0.0f, 0.0f },
                { "Focus Center", 0.5f, 0.5f },
                { "Focus BR",     1.0f, 1.0f },
            };
            std::vector<RadioSpec> focSpecs;
            for (const auto& foc : focuses) {
                float vx = foc.x, vy = foc.y;
                focSpecs.push_back({ foc.label, indV,
                    (foc.x == 0.5f && foc.y == 0.5f), false,
                    [showPtr, vx, vy]() {
                        SlideshowConfig c = showPtr->GetConfig();
                        c.imageFocus = Point2Df(vx, vy);
                        showPtr->SetConfig(c);
                        showPtr->Play();
                    } });
            }
            flowRadio(focusGroup, kSubBtnX, y, focSpecs);
        }
        y += kBtnH + kRowGap;
        addDivider(y, true);

        // ----- Letterbox fill -----
        addLabel("Letterbox fill", kLabelX, y + 4, 130, 12.0f, false, kLabelColor);
        {
            struct GapChoice { const char* label; SlideshowGapFill gap; };
            const GapChoice gaps[] = {
                { "Gap: Bg",    SlideshowGapFill::BackgroundColor },
                { "Gap: Black", SlideshowGapFill::LetterboxColor },
                { "Gap: Image", SlideshowGapFill::BlurredImage },
            };
            auto gapGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();
            std::vector<RadioSpec> specs;
            for (const auto& gc : gaps) {
                auto gapVal = gc.gap;
                specs.push_back({ gc.label, indV,
                    gc.gap == SlideshowGapFill::BackgroundColor, false,
                    [showPtr, gapVal]() {
                        SlideshowConfig c = showPtr->GetConfig();
                        c.gapFill = gapVal;
                        showPtr->SetConfig(c);
                        showPtr->Play();
                    } });
            }
            flowRadio(gapGroup, kContentX, y, specs);
        }
        y += kBtnH + kRowGap;

        return root;
    }

} // namespace UltraCanvas
