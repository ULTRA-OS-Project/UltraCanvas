// Apps/DemoApp/UltraCanvasSlideshowExamples.cpp
// Demonstration of UltraCanvasSlideshow: indicator styles, info-panel layouts,
// indicator edges and transition styles.
// Version: 1.1.0
// Last Modified: 2026-06-09
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasSlideshow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <vector>

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
            { "Bars (Subaru)",  SlideshowIndicatorShape::Bars,        30.0f, 3.0f,  6.0f, 0.0f },
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
        auto root = std::make_shared<UltraCanvasContainer>("SlideshowExamples", 0, 0, 1000, 860);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("SlideshowTitle", 20, 10, 800, 30);
        title->SetText("UltraCanvas Slideshow — Subaru-style diashow with selectable indicators");
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
        auto show = CreateSlideshow("solterra-slideshow", 20, 80, 920, 480);

        SlideshowConfig cfg = MakeBaseConfig();

        // Build slide list from images that ship in media/. We pair each image
        // with descriptive German text echoing the Subaru Solterra page so the
        // visual matches the reference screenshot closely.
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
                        "SUBARU Care App",
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

        // Indicator-style picker row
        auto pickerTitle = std::make_shared<UltraCanvasLabel>("IndicatorPickerTitle", 20, 605, 200, 22);
        pickerTitle->SetText("Indicator style:");
        pickerTitle->SetFontSize(12);
        pickerTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(pickerTitle);

        int btnX = 20;
        int btnY = 630;
        int btnH = 30;
        for (const auto& choice : kStyles) {
            int btnW = 130;
            auto button = std::make_shared<UltraCanvasButton>(
                    std::string("indicator_btn_") + choice.label, btnX, btnY, btnW, btnH, choice.label);
            button->SetFontSize(11);
            button->SetCornerRadius(4.0f);
            auto styleCopy = choice;
            button->SetOnClick([showPtr, styleCopy]() {
                SlideshowIndicatorStyle is = showPtr->GetIndicatorStyle();
                is.shape        = styleCopy.shape;
                is.itemWidth    = styleCopy.itemWidth;
                is.itemHeight   = styleCopy.itemHeight;
                is.spacing      = styleCopy.spacing;
                is.cornerRadius = styleCopy.cornerRadius;
                showPtr->SetIndicatorStyle(is);
            });
            root->AddChild(button);
            btnX += btnW + 8;
        }

        // Playback controls row
        int ctrlY = 675;
        auto prevBtn = std::make_shared<UltraCanvasButton>("slide_prev", 20, ctrlY, 80, 28, "‹ Prev");
        prevBtn->SetFontSize(11);
        prevBtn->SetOnClick([showPtr]() { showPtr->Previous(); });
        root->AddChild(prevBtn);

        auto nextBtn = std::make_shared<UltraCanvasButton>("slide_next", 108, ctrlY, 80, 28, "Next ›");
        nextBtn->SetFontSize(11);
        nextBtn->SetOnClick([showPtr]() { showPtr->Next(); });
        root->AddChild(nextBtn);

        auto pauseBtn = std::make_shared<UltraCanvasButton>("slide_playpause", 196, ctrlY, 90, 28, "Pause");
        pauseBtn->SetFontSize(11);
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
        root->AddChild(pauseBtn);

        // Fade-style picker
        struct FadeChoice { const char* label; SlideshowFadeStyle style; };
        const FadeChoice fades[] = {
            { "CrossFade", SlideshowFadeStyle::CrossFade },
            { "FadeOut/In", SlideshowFadeStyle::FadeOutIn },
            { "Slide H",   SlideshowFadeStyle::SlideHorizontal },
            { "Slide V",   SlideshowFadeStyle::SlideVertical },
            { "Zoom",      SlideshowFadeStyle::ZoomFade },
            { "Instant",   SlideshowFadeStyle::NoFade },
        };
        int fbX = 310;
        auto fadeTitle = std::make_shared<UltraCanvasLabel>("FadeTitle", fbX, ctrlY + 4, 80, 22);
        fadeTitle->SetText("Fade:");
        fadeTitle->SetFontSize(11);
        root->AddChild(fadeTitle);
        fbX += 50;
        for (const auto& f : fades) {
            auto fb = std::make_shared<UltraCanvasButton>(
                    std::string("fade_") + f.label, fbX, ctrlY, 95, 28, f.label);
            fb->SetFontSize(11);
            auto styleVal = f.style;
            fb->SetOnClick([showPtr, styleVal]() {
                SlideshowConfig c = showPtr->GetConfig();
                c.fadeStyle = styleVal;
                showPtr->SetConfig(c);
                showPtr->Play();
            });
            root->AddChild(fb);
            fbX += 100;
        }

        // ===== Info-panel layout picker (split / overlay positions) =====
        struct LayoutChoice { const char* label; SlideshowInfoLayout layout; };
        const LayoutChoice layouts[] = {
            { "Split R",      SlideshowInfoLayout::SplitRight },
            { "Split L",      SlideshowInfoLayout::SplitLeft },
            { "Split Top",    SlideshowInfoLayout::SplitTop },
            { "Split Bot",    SlideshowInfoLayout::SplitBottom },
            { "Ovl Left",     SlideshowInfoLayout::OverlayLeft },
            { "Ovl Right",    SlideshowInfoLayout::OverlayRight },
            { "Ovl Top",      SlideshowInfoLayout::OverlayTop },
            { "Ovl Bottom",   SlideshowInfoLayout::OverlayBottom },
            { "Ovl TL",       SlideshowInfoLayout::OverlayTopLeft },
            { "Ovl TR",       SlideshowInfoLayout::OverlayTopRight },
            { "Ovl BL",       SlideshowInfoLayout::OverlayBottomLeft },
            { "Ovl BR",       SlideshowInfoLayout::OverlayBottomRight },
            { "Ovl Center",   SlideshowInfoLayout::OverlayCenter },
            { "Ovl Full",     SlideshowInfoLayout::OverlayFull },
            { "No Panel",     SlideshowInfoLayout::Hidden },
        };

        int layoutTitleY = 712;
        auto layoutTitle = std::make_shared<UltraCanvasLabel>("LayoutTitle", 20, layoutTitleY, 300, 22);
        layoutTitle->SetText("Info-panel layout (split = own region, overlay = on the image):");
        layoutTitle->SetFontSize(12);
        layoutTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(layoutTitle);

        int lx = 20, ly = 738;
        const int lBtnW = 92, lBtnH = 26, lGap = 5;
        for (const auto& lc : layouts) {
            if (lx + lBtnW > 980) { lx = 20; ly += lBtnH + lGap; }
            auto lb = std::make_shared<UltraCanvasButton>(
                    std::string("layout_") + lc.label, lx, ly, lBtnW, lBtnH, lc.label);
            lb->SetFontSize(10);
            auto layoutVal = lc.layout;
            lb->SetOnClick([showPtr, layoutVal]() {
                SlideshowConfig c = showPtr->GetConfig();
                c.infoLayout = layoutVal;
                // Pick readable text colors for the chosen layout: light text on
                // the translucent overlay scrim, dark text on the opaque split panel.
                if (SlideshowLayoutIsOverlay(layoutVal)) {
                    c.titleColor = Color(255, 255, 255, 255);
                    c.bodyColor  = Color(235, 235, 235, 255);
                } else {
                    c.titleColor = Color(20, 20, 22, 255);
                    c.bodyColor  = Color(70, 70, 70, 255);
                }
                showPtr->SetConfig(c);
                showPtr->Play();
            });
            root->AddChild(lb);
            lx += lBtnW + lGap;
        }

        // ===== Indicator edge picker (which side the strip hugs) =====
        struct EdgeChoice { const char* label; SlideshowIndicatorEdge edge; };
        const EdgeChoice edges[] = {
            { "Bottom", SlideshowIndicatorEdge::Bottom },
            { "Top",    SlideshowIndicatorEdge::Top },
            { "Left",   SlideshowIndicatorEdge::Left },
            { "Right",  SlideshowIndicatorEdge::Right },
        };
        int edgeY = ly + lBtnH + 14;
        auto edgeTitle = std::make_shared<UltraCanvasLabel>("EdgeTitle", 20, edgeY + 4, 130, 22);
        edgeTitle->SetText("Indicator edge:");
        edgeTitle->SetFontSize(12);
        edgeTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(edgeTitle);

        int ex = 150;
        for (const auto& ec : edges) {
            auto eb = std::make_shared<UltraCanvasButton>(
                    std::string("edge_") + ec.label, ex, edgeY, 80, 26, ec.label);
            eb->SetFontSize(11);
            auto edgeVal = ec.edge;
            eb->SetOnClick([showPtr, edgeVal]() {
                SlideshowIndicatorStyle is = showPtr->GetIndicatorStyle();
                is.edge = edgeVal;
                showPtr->SetIndicatorStyle(is);
            });
            root->AddChild(eb);
            ex += 88;
        }

        return root;
    }

} // namespace UltraCanvas
