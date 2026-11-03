// Apps/DemoApp/UltraCanvasSlideshowExamples.cpp
// Demonstration of UltraCanvasSlideshow: indicator styles, info-panel layouts,
// indicator edges and transition styles. The option buttons are laid out in a
// labelled-row grid (label column on the left, wrapping option buttons on the
// right) grouped Controls / Indicator / Fade / Panel layout / Image / Letterbox,
// with the active choice highlighted like a radio group. The live widget is
// in its own card to separate it from the programmer-facing options.
// Version: 1.4.0 - the shared page look (UltraCanvasDemoPageKit): the widget and
//                  the options in cards, the options grid flex rows whose
//                  buttons are as wide as their text and wrap
// Version: 1.3.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSlideshow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSeparator.h"
#include <sstream>
#include <vector>
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
        auto root = DemoKit::CreatePage("SlideshowExamples", "UltraCanvas Slideshow widget",
                "The card below holds the live widget; the controls beneath it are "
                "demo-only options. Click the widget, then use the arrow keys to change "
                "slides (Left/Down = previous, Right/Up = next).");

        // Slideshow itself
        auto show = CreateSlideshow("ultracanvas-slideshow", 0, 0, 920, 480);

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

        // ---- The live widget in its own card, so the page makes clear which
        //      part is the actual widget and which are the programmer-facing
        //      options below. It is as wide as the card. ----
        auto widgetCard = DemoKit::AddCard(root, "SlideshowWidgetFrame", "Slideshow widget");
        DemoKit::AddStretched(widgetCard, show);

        // Status label — updated when a slide changes
        auto statusLabel = DemoKit::AddText(widgetCard, "SlideStatus", "Slide 1 / 5");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(120, 120, 120, 255));

        auto* showPtr = show.get();
        auto* statusPtr = statusLabel.get();
        show->onSlideChanged = [showPtr, statusPtr](size_t idx) {
            std::ostringstream oss;
            oss << "Slide " << (idx + 1) << " / " << showPtr->GetSlideCount()
                << " — " << showPtr->GetSlides()[idx].infoTitle;
            statusPtr->SetText(oss.str());
        };

        // ---- Options card: everything in it is programmer-facing. ----
        auto optionsCard = DemoKit::AddCard(root, "SlideshowOptionsHeading", "Slideshow widget options");
        auto optionsGrid = DemoKit::CreateColumn("SlideshowOptionsGrid", 12);
        DemoKit::Add(optionsCard, optionsGrid);

        // ====================================================================
        // Options panel — a labelled-row grid mirroring the reference layout:
        // a label column on the left and a wrapping row of option buttons
        // on the right. Each group behaves like a radio: clicking a button
        // highlights it (gold border + darker fill) and clears its siblings.
        // ====================================================================

        // ---- Geometry ----
        const float kLabelW    = 120;         // main-row label column (at least)
        const float kSubLabelW = 70;          // sub-row label column (at least)
        const float kBtnH      = 26;
        const float kBtnMinW   = 46;          // the narrowest option button
        const float kBtnGap    = 6;

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

        // Ids are numbered in creation order: the buttons have no position
        // of their own any more.
        int idCounter = 0;

        using BtnGroup = std::shared_ptr<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>;

        // Paint a button in its normal or selected state.
        auto applyVisual = [kSelBorder](UltraCanvasButton* b, const BtnVisual& v, bool sel) {
            b->SetColors(sel ? v.selBg : v.baseBg, v.selBg);
            b->SetTextColors(v.text);
            b->SetBorder(sel ? 2.0f : 1.0f, sel ? kSelBorder : v.border);
        };

        // A button as wide as its text, at least kBtnMinW.
        auto makeButton = [&](const std::string& id, const std::string& label) {
            auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, kBtnH, label);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(kBtnMinW);
            b->boxConstraints = limits;
            b->SetFontSize(11);
            b->SetCornerRadius(6.0f);
            return b;
        };

        // A non-radio control button (Previous / Pause / Next).
        auto addCtrl = [&](const std::shared_ptr<UltraCanvasContainer>& parent, const std::string& id,
                           const std::string& label, std::function<void()> cb) {
            auto b = makeButton(id, label);
            applyVisual(b.get(), ctrlV, false);
            if (cb) b->SetOnClick(cb);
            DemoKit::Add(parent, b);
            return b;
        };

        // One radio button registered in `group`; clicking selects it and
        // restyles every sibling, then runs the supplied action.
        auto addRadio = [&](BtnGroup group, const std::shared_ptr<UltraCanvasContainer>& parent,
                            const std::string& label, const BtnVisual& v, bool selected,
                            std::function<void()> cb) {
            auto btn = makeButton("opt_" + label + "_" + std::to_string(++idCounter), label);
            applyVisual(btn.get(), v, selected);
            auto* raw = btn.get();
            group->push_back({raw, v});
            btn->SetOnClick([group, raw, cb, applyVisual]() {
                for (auto& pr : *group) applyVisual(pr.first, pr.second, pr.first == raw);
                if (cb) cb();
            });
            DemoKit::Add(parent, btn);
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
        auto flowRadio = [&](BtnGroup group, const std::shared_ptr<UltraCanvasContainer>& content,
                             const std::vector<RadioSpec>& specs) {
            auto line = DemoKit::CreateRow("opt_line_" + std::to_string(++idCounter), kBtnGap);
            DemoKit::Add(content, line);
            for (const auto& s : specs) {
                if (s.brk) {
                    line = DemoKit::CreateRow("opt_line_" + std::to_string(++idCounter), kBtnGap);
                    DemoKit::Add(content, line);
                    continue;
                }
                addRadio(group, line, s.label, s.vis, s.selected, s.cb);
            }
        };

        // A labelled line of the grid: the caption (at least `labelW` wide, as
        // wide as its text when longer), then a column taking the rest of the
        // width, which is returned for the line's buttons or sub-lines.
        auto addGridRow = [&](const std::shared_ptr<UltraCanvasContainer>& parent, const std::string& text,
                              float labelW, float fs, Color col) {
            int n = ++idCounter;
            auto line = DemoKit::CreateRow("grid_" + std::to_string(n), 10, false);
            DemoKit::Add(parent, line);
            auto lbl = DemoKit::CreateText("lbl_" + text + "_" + std::to_string(n), text);
            lbl->SetFontSize(fs);
            lbl->SetTextColor(col);
            lbl->SetPadding(4, 0, 0, 0);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(labelW);
            lbl->boxConstraints = limits;
            DemoKit::Add(line, lbl);
            auto content = DemoKit::CreateColumn("grid_content_" + std::to_string(n), kBtnGap);
            DemoKit::Add(line, content, 1);
            content->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
            return content;
        };

        // A horizontal divider between sections.
        auto addDivider = [&]() {
            DemoKit::AddStretched(optionsGrid, std::make_shared<UltraCanvasSeparator>(
                    false, 1, 960, Color(0, 0, 0, 38)));
        };

        // ----- Controls -----
        auto controlsContent = addGridRow(optionsGrid, "Controls", kLabelW, 12.0f, kLabelColor);
        {
            auto ctrlLine = DemoKit::CreateRow("slide_controls", kBtnGap);
            DemoKit::Add(controlsContent, ctrlLine);
            addCtrl(ctrlLine, "slide_prev", "‹ Previous",
                    [showPtr]() { showPtr->Previous(); });
            auto pauseBtn = addCtrl(ctrlLine, "slide_playpause", "Pause", nullptr);
            addCtrl(ctrlLine, "slide_next", "Next ›",
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
        addDivider();

        // ----- Indicator style -----
        auto indicatorContent = addGridRow(optionsGrid, "Indicator", kLabelW, 12.0f, kLabelColor);
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
            flowRadio(indGroup, indicatorContent, specs);
        }

        // ----- Indicator edge -----
        auto edgeContent = addGridRow(optionsGrid, "Indicator edge", kLabelW, 12.0f, kLabelColor);
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
            flowRadio(edgeGroup, edgeContent, specs);
        }
        addDivider();

        // ----- Fade style -----
        auto fadeContent = addGridRow(optionsGrid, "Fade style", kLabelW, 12.0f, kLabelColor);
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
            flowRadio(fadeGroup, fadeContent, specs);
        }
        addDivider();

        // ----- Panel layout (Split / Overlay positions, plus OFF) -----
        // A single radio group spread across three sub-rows. Split layouts give
        // the panel its own region; overlay layouts float it over the image.
        auto panelContent = addGridRow(optionsGrid, "Panel layout", kLabelW, 12.0f, kLabelColor);
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
            auto splitLine = addGridRow(panelContent, "Split", kSubLabelW, 11.0f, kSubLabelColor);
            std::vector<RadioSpec> splitSpecs = {
                { "Left",   indV, false, false, panelCb(SlideshowInfoLayout::SplitLeft) },
                { "Right",  indV, true,  false, panelCb(SlideshowInfoLayout::SplitRight) },
                { "Top",    indV, false, false, panelCb(SlideshowInfoLayout::SplitTop) },
                { "Bottom", indV, false, false, panelCb(SlideshowInfoLayout::SplitBottom) },
            };
            flowRadio(panelGroup, splitLine, splitSpecs);

            // Overlay sub-row (edges, then corners / center / full).
            auto overlayLine = addGridRow(panelContent, "Overlay", kSubLabelW, 11.0f, kSubLabelColor);
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
            flowRadio(panelGroup, overlayLine, overlaySpecs);

            // OFF sub-row (no panel).
            auto offLine = addGridRow(panelContent, " ", kSubLabelW, 11.0f, kSubLabelColor);
            std::vector<RadioSpec> offSpecs = {
                { "OFF", offV, false, false, panelCb(SlideshowInfoLayout::Hidden) },
            };
            flowRadio(panelGroup, offLine, offSpecs);
        }
        addDivider();

        // ----- Image (Fit + Crop focus) -----
        auto imageContent = addGridRow(optionsGrid, "Image", kLabelW, 12.0f, kLabelColor);
        {
            auto fitGroup   = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();
            auto focusGroup = std::make_shared<std::vector<std::pair<UltraCanvasButton*, BtnVisual>>>();

            // Crop focus only applies to a Cover crop — dim it otherwise.
            auto setFocusEnabled = [focusGroup](bool en) {
                for (auto& pr : *focusGroup) pr.first->SetDisabled(!en);
            };

            // Fit sub-row (Cover is the config default).
            auto fitLine = addGridRow(imageContent, "Fit", kSubLabelW, 11.0f, kSubLabelColor);
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
            flowRadio(fitGroup, fitLine, fitSpecs);

            // Crop focus sub-row (center is the config default).
            auto focusLine = addGridRow(imageContent, "Crop focus", kSubLabelW, 11.0f, kSubLabelColor);
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
            flowRadio(focusGroup, focusLine, focSpecs);
        }
        addDivider();

        // ----- Letterbox fill -----
        auto letterboxContent = addGridRow(optionsGrid, "Letterbox fill", kLabelW, 12.0f, kLabelColor);
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
            flowRadio(gapGroup, letterboxContent, specs);
        }

        return root;
    }

} // namespace UltraCanvas
