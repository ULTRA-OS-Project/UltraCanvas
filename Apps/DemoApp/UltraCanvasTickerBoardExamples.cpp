// Apps/DemoApp/UltraCanvasTickerBoardExamples.cpp
// Ticker board demonstration: a market ticker in a font, an LED dot-matrix
// sign, live quotes on a dot grid, and a headline crawl - with the controls
// that pause, speed up and re-face a board while it rolls.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  board, the boards as wide as the page, buttons sized to their
//                  text, the status line under the controls
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasTickerBoard.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== SAMPLE DATA =====

    namespace {
        struct SampleQuote {
            const char* symbol;
            double price;
            double change;
            const char* name;
        };

        // The opening prices of the demo's imaginary exchange.
        const SampleQuote kSampleQuotes[] = {
            {"UCNV", 189.30,  1.25, "UltraCanvas Framework"},
            {"VFX",   42.18, -0.64, "VideoFX Studios"},
            {"NETM",  77.05,  0.00, "NetMonitor Systems"},
            {"FILR",  12.90,  0.37, "Filer Logistics"},
            {"MAIL",  58.44, -2.10, "UltraMail Holdings"},
            {"PAINT", 24.76,  0.52, "Paint & Pixel"},
            {"VAULT", 310.00, 4.80, "Vault Security"},
            {"CLEAN",  9.15, -0.08, "Cleaner Utilities"},
        };

        // Running prices for the live board: one random walk per quote,
        // shared by the timer that steps them and the button that resets them.
        struct LivePrices {
            std::vector<double> price;
            std::vector<double> change;
            std::minstd_rand random{20261013u};
        };

        TickerItem SampleQuoteItem(const SampleQuote& q, double price, double change) {
            TickerItem item = MakeTickerQuote(q.symbol, price, change);
            item.tooltip = std::string(q.name) + "\nLast " + item.value + "  " + item.change;
            return item;
        }

        void FillQuotes(UltraCanvasTickerBoard* board) {
            std::vector<TickerItem> items;
            for (const SampleQuote& q : kSampleQuotes) items.push_back(SampleQuoteItem(q, q.price, q.change));
            board->SetItems(items);
        }

        // A control button: as wide as its text, at least the width it had.
        std::shared_ptr<UltraCanvasButton> ControlButton(const std::string& id, float minWidth,
                                                         const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(minWidth);
            button->boxConstraints = limits;
            return button;
        }
    }

// ===== MAIN TICKER BOARD EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTickerBoardExamples() {
        auto container = DemoKit::CreatePage("TickerBoardContainer",
            "Ticker Board Examples - Crawling Quotes, LED Signs and Headline Crawls",
            "A strip of items - label, value, change - crawling across a band without end. The Text face sets it in a "
            "font; the DotMatrix face lights it on a grid of 5x7 LED dots that the text steps across one column at a time. "
            "Every board pauses while the pointer rests on it, shows an item's tooltip, and reports a click on an item."
        );

        // === STATUS LINE (what was clicked) ===
        // Added under the controls below.
        auto statusLabel = DemoKit::CreateText("TbStatusLabel",
            "Click an item on any board to see it here. Hover a quote for its company name.");
        statusLabel->SetTextColor(Color(70, 70, 90, 255));
        auto reportClick = [status = statusLabel.get()](const char* boardName, const TickerItem& item) {
            std::string text = std::string(boardName) + ": " + item.label;
            if (!item.value.empty())  text += " " + item.value;
            if (!item.change.empty()) text += " " + item.change;
            status->SetText(text);
        };

        // ===== EXAMPLE 1: TEXT FACE - MARKET TICKER =====
        auto quotesCard = DemoKit::AddCard(container, "TbQuotesCaption",
            "Text face - market quotes with trend colours and arrows (hover pauses, click reports)");

        auto quotesBoard = CreateTickerBoard("TbQuotesBoard", 0, 0, 1160, 44, TickerBoardFace::Text);
        FillQuotes(quotesBoard.get());
        quotesBoard->onItemClick = [board = quotesBoard.get(), reportClick](size_t index) {
            reportClick("Quotes", board->GetItems()[index]);
        };
        DemoKit::AddStretched(quotesCard, quotesBoard);

        // ===== EXAMPLE 2: DOT MATRIX FACE - AMBER LED SIGN =====
        auto signCard = DemoKit::AddCard(container, "TbSignCaption",
            "DotMatrix face - an amber LED sign: the message steps across a fixed grid of dots");

        auto signBoard = CreateTickerBoard("TbSignBoard", 0, 0, 1160, 64, TickerBoardFace::DotMatrix);
        {
            TickerBoardStyle s = signBoard->GetStyle();
            s.backgroundColor = Color(12, 10, 8, 255);
            s.labelColor = Color(255, 170, 0, 255);
            s.separatorColor = Color(255, 170, 0, 160);
            s.offDotColor = Color(255, 170, 0, 22);
            s.pixelsPerSecond = 110.0f;
            s.cornerRadius = 6.0f;
            signBoard->SetStyle(s);
        }
        signBoard->SetItems({
            TickerItem("WELCOME TO THE ULTRACANVAS DEMO"),
            TickerItem("Next departure: Platform 4 at 12:45"),
            TickerItem("Zürich 18°C - clear skies"),
            TickerItem("Tickets from €9.90 / £8.50"),
        });
        signBoard->onItemClick = [board = signBoard.get(), reportClick](size_t index) {
            reportClick("LED sign", board->GetItems()[index]);
        };
        DemoKit::AddStretched(signCard, signBoard);

        // ===== EXAMPLE 3: DOT MATRIX QUOTES, UPDATED LIVE =====
        auto liveCard = DemoKit::AddCard(container, "TbLiveCaption",
            "DotMatrix face - quotes on a green/red dot grid, re-priced every 1.5 s in place (UpdateItem keeps the position)");

        auto liveBoard = CreateTickerBoard("TbLiveBoard", 0, 0, 1160, 56, TickerBoardFace::DotMatrix);
        {
            TickerBoardStyle s = liveBoard->GetStyle();
            s.backgroundColor = Color(6, 8, 10, 255);
            s.labelColor = Color(120, 200, 255, 255);
            s.valueColor = Color(240, 240, 240, 255);
            s.offDotColor = Color(255, 255, 255, 12);
            s.pixelsPerSecond = 80.0f;
            liveBoard->SetStyle(s);
        }
        FillQuotes(liveBoard.get());
        liveBoard->onItemClick = [board = liveBoard.get(), reportClick](size_t index) {
            reportClick("Live quotes", board->GetItems()[index]);
        };
        DemoKit::AddStretched(liveCard, liveBoard);

        auto live = std::make_shared<LivePrices>();
        for (const SampleQuote& q : kSampleQuotes) {
            live->price.push_back(q.price);
            live->change.push_back(q.change);
        }
        // The timer outlives nothing: it stops itself once the page is gone.
        std::weak_ptr<UltraCanvasContainer> rootWeak = container;
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            app->StartTimer(1500, true, [live, rootWeak, board = liveBoard.get()](TimerId id) {
                if (rootWeak.expired()) {
                    if (auto* a = UltraCanvasApplication::GetInstance()) a->StopTimer(id);
                    return;
                }
                std::uniform_int_distribution<size_t> pick(0, live->price.size() - 1);
                std::uniform_real_distribution<double> step(-0.9, 0.9);
                // Two quotes move per tick; the rest hold their last change.
                for (int n = 0; n < 2; ++n) {
                    const size_t i = pick(live->random);
                    const double delta = step(live->random) * live->price[i] / 100.0;
                    live->price[i] += delta;
                    live->change[i] += delta;
                    board->UpdateItem(i, SampleQuoteItem(kSampleQuotes[i], live->price[i], live->change[i]));
                }
            });
        }

        // ===== EXAMPLE 4: TEXT FACE - HEADLINE CRAWL, RIGHTWARD, LIGHT =====
        auto newsCard = DemoKit::AddCard(container, "TbNewsCaption",
            "Text face - a headline crawl: light band, larger font, rightward, no pause on hover");

        auto newsBoard = CreateTickerBoard("TbNewsBoard", 0, 0, 1160, 40, TickerBoardFace::Text);
        {
            TickerBoardStyle s = newsBoard->GetStyle();
            s.direction = TickerDirection::Rightward;
            s.pauseOnHover = false;
            s.backgroundColor = Color(245, 246, 250, 255);
            s.labelColor = Color(30, 34, 44, 255);
            s.valueColor = Color(90, 96, 110, 255);
            s.separatorColor = Color(200, 60, 60, 255);
            s.separator = "\xE2\x97\x86";   // a diamond
            s.fontSize = 16.0f;
            s.cornerRadius = 8.0f;
            s.pixelsPerSecond = 70.0f;
            newsBoard->SetStyle(s);
        }
        newsBoard->SetItems({
            TickerItem("BREAKING", "Shared core library ships on every platform"),
            TickerItem("DEMO", "Diagrams section gains a ticker board"),
            TickerItem("WEATHER", "Clear skies over the build farm, light CI winds"),
            TickerItem("SPORT", "UltraFiler wins the 1 000 000-file sprint"),
        });
        newsBoard->onItemClick = [board = newsBoard.get(), reportClick](size_t index) {
            reportClick("Headlines", board->GetItems()[index]);
        };
        DemoKit::AddStretched(newsCard, newsBoard);

        // ===== CONTROLS =====
        auto controlsCard = DemoKit::AddCard(container, "TbControlsCaption",
            "Controls - act on the market ticker at the top");

        auto controlsRow = DemoKit::AddRow(controlsCard, "TbControlsRow", 10);

        auto pauseButton = ControlButton("TbPauseButton", 130, "Pause");
        pauseButton->SetOnClick([board = quotesBoard.get(), button = pauseButton.get()]() {
            if (board->IsRunning()) { board->Stop();  button->SetText("Resume"); }
            else                    { board->Start(); button->SetText("Pause"); }
        });
        DemoKit::Add(controlsRow, pauseButton);

        auto slowerButton = ControlButton("TbSlowerButton", 110, "Slower");
        slowerButton->SetOnClick([board = quotesBoard.get()]() {
            board->SetSpeed(std::max(10.0f, board->GetStyle().pixelsPerSecond - 20.0f));
        });
        DemoKit::Add(controlsRow, slowerButton);

        auto fasterButton = ControlButton("TbFasterButton", 110, "Faster");
        fasterButton->SetOnClick([board = quotesBoard.get()]() {
            board->SetSpeed(std::min(400.0f, board->GetStyle().pixelsPerSecond + 20.0f));
        });
        DemoKit::Add(controlsRow, fasterButton);

        auto directionButton = ControlButton("TbDirectionButton", 150, "Reverse direction");
        directionButton->SetOnClick([board = quotesBoard.get()]() {
            board->SetDirection(board->GetStyle().direction == TickerDirection::Leftward
                                ? TickerDirection::Rightward : TickerDirection::Leftward);
        });
        DemoKit::Add(controlsRow, directionButton);

        auto faceButton = ControlButton("TbFaceButton", 170, "Switch to DotMatrix");
        faceButton->SetOnClick([board = quotesBoard.get(), button = faceButton.get()]() {
            const bool toDots = board->GetStyle().face == TickerBoardFace::Text;
            board->SetFace(toDots ? TickerBoardFace::DotMatrix : TickerBoardFace::Text);
            button->SetText(toDots ? "Switch to Text" : "Switch to DotMatrix");
        });
        DemoKit::Add(controlsRow, faceButton);

        auto arrowsButton = ControlButton("TbArrowsButton", 130, "Hide arrows");
        arrowsButton->SetOnClick([board = quotesBoard.get(), button = arrowsButton.get()]() {
            TickerBoardStyle s = board->GetStyle();
            s.showArrows = !s.showArrows;
            board->SetStyle(s);
            button->SetText(s.showArrows ? "Hide arrows" : "Show arrows");
        });
        DemoKit::Add(controlsRow, arrowsButton);

        auto resetButton = ControlButton("TbResetButton", 150, "Reset live prices");
        resetButton->SetOnClick([live, board = liveBoard.get()]() {
            for (size_t i = 0; i < live->price.size(); ++i) {
                live->price[i] = kSampleQuotes[i].price;
                live->change[i] = kSampleQuotes[i].change;
            }
            FillQuotes(board);
        });
        DemoKit::Add(controlsRow, resetButton);

        DemoKit::Add(controlsCard, statusLabel);

        // === HOW IT IS USED ===
        auto usageCard = DemoKit::AddCard(container, "TbUsageCard");
        auto usageLabel = DemoKit::AddText(usageCard, "TbUsageLabel",
            "auto board = CreateTickerBoard(\"Prices\", 0, 0, 800, 44, TickerBoardFace::Text);\n"
            "board->AddItem(MakeTickerQuote(\"UCNV\", 189.30, 1.25));   // value \"189.30\", change \"+1.25 (+0.66%)\", trend Up\n"
            "board->AddItem(TickerItem(\"Platform 4\", \"12:45\"));      // a label and a value, no change\n"
            "board->SetText(\"WELCOME\");                              // or one crawling message\n"
            "board->onItemClick = [](size_t index) { /* open the quote */ };\n"
            "board->UpdateItem(0, MakeTickerQuote(\"UCNV\", 190.05, 2.00)); // re-price in place while it rolls\n"
            "\n"
            "Style: face (Text / DotMatrix), direction, pixelsPerSecond, pauseOnHover, the band and trend colours,\n"
            "fontFamily / fontSize / weights and gaps for Text, dotPitch / dotFill / offDotColor and column gaps for DotMatrix."
        );
        usageLabel->SetFont("Monospace", 11);
        usageLabel->SetTextColor(Color(50, 50, 70, 255));
        usageLabel->SetBackgroundColor(Color(248, 248, 252, 255));
        usageLabel->SetPadding(8);

        return container;
    }

} // namespace UltraCanvas
