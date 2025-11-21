// Apps/DemoApp/UltraCanvasComprehensiveTabDemo.cpp
// Comprehensive demonstration of all tabbed container features
// Version: 1.0.0
// Last Modified: 2025-11-17
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasCheckbox.h"
#include <sstream>

namespace UltraCanvas {

// ===== HELPER: CREATE SAMPLE TAB CONTENT =====
    std::shared_ptr<UltraCanvasContainer> CreateSampleTabContent(
            const std::string& id, int uid, const std::string& text, const Color& bgColor, int w = 550, int h = 180) {

        auto content = std::make_shared<UltraCanvasContainer>(id, uid, 0, 0, w, h);
        content->SetBackgroundColor(bgColor);
        content->SetPadding(15);

        auto label = std::make_shared<UltraCanvasLabel>(id + "_label", uid + 1, 0, 0, w - 30, h - 30);
        label->SetText(text);
        label->SetFontSize(12);
        //label->SetAlignment(TextAlignment::TopLeft);
        label->SetTextColor(Colors::Black);
        content->AddChild(label);

        return content;
    }

// ===== MAIN COMPREHENSIVE TAB DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTabExamples() {
        // Main container with scrolling
        auto mainContainer = std::make_shared<UltraCanvasContainer>("ComprehensiveTabDemo", 7000, 0, 0, 1020, 3000);
        //mainContainer->EnableVerticalScrolling(true);
//        mainContainer->SetBackgroundColor(Color(245, 245, 245));

        int yOffset = 10;

        // ========================================
        // TITLE
        // ========================================
        auto mainTitle = std::make_shared<UltraCanvasLabel>("MainTitle", 7001, 20, yOffset, 960, 35);
        mainTitle->SetText("UltraCanvas Tabbed Container - Comprehensive Demo");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetTextColor(Color(40, 40, 40));
        mainContainer->AddChild(mainTitle);
        yOffset += 45;

        auto subtitle = std::make_shared<UltraCanvasLabel>("Subtitle", 7002, 20, yOffset, 960, 25);
        subtitle->SetText("Showcasing all tab styles, features, and configurations");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(subtitle);
        yOffset += 40;

        // ========================================
        // SECTION 1: TAB STYLES (Classic, Modern, Flat, Rounded)
        // ========================================
        auto section1Title = std::make_shared<UltraCanvasLabel>("Section1Title", 7010, 20, yOffset, 960, 30);
        section1Title->SetText("1. TAB VISUAL STYLES");
        section1Title->SetFontSize(14);
        section1Title->SetFontWeight(FontWeight::Bold);
        section1Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section1Title);
        yOffset += 35;

        // --- Classic Style ---
        auto classicLabel = std::make_shared<UltraCanvasLabel>("ClassicLabel", 7011, 40, yOffset, 400, 20);
        classicLabel->SetText("Classic Style (Windows 7 look with 3D borders)");
        classicLabel->SetFontSize(11);
        classicLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(classicLabel);
        yOffset += 25;

        auto classicTabs = std::make_shared<UltraCanvasTabbedContainer>("ClassicTabs", 7012, 40, yOffset, 800, 220);
        classicTabs->SetTabStyle(TabStyle::Classic);
        classicTabs->SetTabHeight(30);
//        classicTabs->SetCornerRadius(4.0f);
//        classicTabs->SetTabElevation(2.0f);

        classicTabs->AddTab("Home", CreateSampleTabContent("Classic1", 7013,
                                                           "Classic Style Features:\n\n"
                                                           "• 3D raised effect for active tabs\n"
                                                           "• Subtle shadows for depth\n"
                                                           "• Border connects active tab to content area\n"
                                                           "• Inactive tabs appear slightly recessed\n"
                                                           "• Professional Windows 7/10 aesthetic",
                                                           Color(250, 250, 250)));

        classicTabs->AddTab("Documents", CreateSampleTabContent("Classic2", 7015,
                                                                "This tab demonstrates how multiple tabs work together.\n\n"
                                                                "Click between tabs to see the 3D effect and smooth transitions.",
                                                                Color(245, 250, 255)));

        classicTabs->AddTab("Settings", CreateSampleTabContent("Classic3", 7017,
                                                               "Settings content goes here.\n\n"
                                                               "Each tab can contain any UltraCanvas UI elements.",
                                                               Color(250, 245, 255)));

        classicTabs->SetActiveTab(0);
        classicTabs->SetCloseMode(TabCloseMode::NoClose);
        mainContainer->AddChild(classicTabs);
        yOffset += 230;

        // --- Modern Style ---
        auto modernLabel = std::make_shared<UltraCanvasLabel>("ModernLabel", 7020, 40, yOffset, 400, 20);
        modernLabel->SetText("Modern Style (Material Design with bottom indicator)");
        modernLabel->SetFontSize(11);
        modernLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(modernLabel);
        yOffset += 25;

        auto modernTabs = std::make_shared<UltraCanvasTabbedContainer>("ModernTabs", 7021, 40, yOffset, 800, 220);
        modernTabs->SetTabStyle(TabStyle::Modern);
        modernTabs->SetTabHeight(32);
//        modernTabs->SetActiveTabIndicatorHeight(3.0f);
//        modernTabs->SetActiveTabIndicatorColor(Color(33, 150, 243)); // Material Blue

        modernTabs->AddTab("Files and Folders", CreateSampleTabContent("Modern1", 7022,
                                                                       "Modern Style Features:\n\n"
                                                                       "• Flat design with no borders\n"
                                                                       "• Bold bottom indicator line (3px) under active tab\n"
                                                                       "• Subtle background color change on hover\n"
                                                                       "• Clean, minimal Material Design appearance\n"
                                                                       "• Popular in Google and Android apps",
                                                                       Color(250, 250, 250)));

        modernTabs->AddTab("User Profiles", CreateSampleTabContent("Modern2", 7024,
                                                                   "User profiles and management features.\n\n"
                                                                   "Notice the blue indicator line that follows the active tab.",
                                                                   Color(245, 250, 255)));

        modernTabs->AddTab("Contacts", CreateSampleTabContent("Modern3", 7026,
                                                              "Contact management interface.\n\n"
                                                              "Modern style is ideal for web applications and mobile-inspired UIs.",
                                                              Color(250, 245, 255)));

        modernTabs->AddTab("Applications", CreateSampleTabContent("Modern4", 7028,
                                                                  "Application settings and configuration.",
                                                                  Color(255, 250, 245)));

        modernTabs->SetActiveTab(0);
        modernTabs->SetCloseMode(TabCloseMode::NoClose);
        mainContainer->AddChild(modernTabs);
        yOffset += 230;

        // --- Flat Style ---
        auto flatLabel = std::make_shared<UltraCanvasLabel>("FlatLabel", 7030, 40, yOffset, 400, 20);
        flatLabel->SetText("Flat Style (Minimal text-only design)");
        flatLabel->SetFontSize(11);
        flatLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(flatLabel);
        yOffset += 25;

        auto flatTabs = std::make_shared<UltraCanvasTabbedContainer>("FlatTabs", 7031, 40, yOffset, 800, 220);
        flatTabs->SetTabStyle(TabStyle::Flat);
        flatTabs->SetTabHeight(28);

        flatTabs->AddTab("General", CreateSampleTabContent("Flat1", 7032,
                                                           "Flat Style Features:\n\n"
                                                           "• Completely flat with no borders\n"
                                                           "• Only active tab shows different background\n"
                                                           "• Maximum minimalism\n"
                                                           "• Very clean and unobtrusive\n"
                                                           "• Perfect for simple, elegant interfaces",
                                                           Color(250, 250, 250)));

        flatTabs->AddTab("Advanced", CreateSampleTabContent("Flat2", 7034,
                                                            "Advanced configuration options.\n\n"
                                                            "Flat style keeps the focus on content, not chrome.",
                                                            Color(245, 250, 255)));

        flatTabs->AddTab("About", CreateSampleTabContent("Flat3", 7036,
                                                         "About this application.\n\n"
                                                         "Minimal UI, maximum content.",
                                                         Color(250, 245, 255)));

        flatTabs->SetActiveTab(0);
        flatTabs->SetCloseMode(TabCloseMode::NoClose);
        mainContainer->AddChild(flatTabs);
        yOffset += 230;

        // --- Rounded Style ---
        auto roundedLabel = std::make_shared<UltraCanvasLabel>("RoundedLabel", 7040, 40, yOffset, 400, 20);
        roundedLabel->SetText("Rounded Style (Chrome/Firefox browser-style)");
        roundedLabel->SetFontSize(11);
        roundedLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(roundedLabel);
        yOffset += 25;

        auto roundedTabs = std::make_shared<UltraCanvasTabbedContainer>("RoundedTabs", 7041, 40, yOffset, 800, 220);
        roundedTabs->SetTabStyle(TabStyle::Rounded);
        roundedTabs->SetTabHeight(30);
        roundedTabs->SetTabCornerRadius(8.0f);
        roundedTabs->SetTabElevation(1.0f);

        roundedTabs->AddTab("Page 1", CreateSampleTabContent("Rounded1", 7042,
                                                             "Rounded Style Features:\n\n"
                                                             "• Rounded top-left and top-right corners\n"
                                                             "• Active tab connects smoothly to content area\n"
                                                             "• Optional shadow for depth\n"
                                                             "• Soft, friendly appearance\n"
                                                             "• Used in Chrome, Edge, Firefox browsers",
                                                             Color(240, 240, 255)));

        roundedTabs->AddTab("Page 2", CreateSampleTabContent("Rounded2", 7044,
                                                             "Another page in the rounded style.\n\n"
                                                             "Notice the smooth rounded corners on the tabs.",
                                                             Color(240, 240, 255)));

        roundedTabs->AddTab("Page 3", CreateSampleTabContent("Rounded3", 7046,
                                                             "Third page content.\n\n"
                                                             "This style is very popular in modern applications.",
                                                             Color(240, 240, 255)));

        roundedTabs->SetActiveTab(0);
        mainContainer->AddChild(roundedTabs);
        yOffset += 230;

        // ========================================
        // SECTION 2: TAB FEATURES (Icons, Badges, Close Buttons)
        // ========================================
        auto section2Title = std::make_shared<UltraCanvasLabel>("Section2Title", 7050, 20, yOffset, 960, 30);
        section2Title->SetText("2. TAB FEATURES (Icons, Badges, Close Buttons)");
        section2Title->SetFontSize(14);
        section2Title->SetFontWeight(FontWeight::Bold);
        section2Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section2Title);
        yOffset += 35;

        // --- Tabs with Icons and Badges ---
        auto featuresLabel = std::make_shared<UltraCanvasLabel>("FeaturesLabel", 7051, 40, yOffset, 600, 20);
        featuresLabel->SetText("Tabs with Icons, Notification Badges, and Close Buttons");
        featuresLabel->SetFontSize(11);
        featuresLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(featuresLabel);
        yOffset += 25;

        auto featureTabs = std::make_shared<UltraCanvasTabbedContainer>("FeatureTabs", 7052, 40, yOffset, 800, 220);
        featureTabs->SetTabStyle(TabStyle::Rounded);
        featureTabs->SetTabHeight(32);
        featureTabs->SetCloseMode(TabCloseMode::Closable);
        featureTabs->SetIconSize(16);

        // Add tabs with icons
        int tabIndex1 = featureTabs->AddTab("Messages", CreateSampleTabContent("Feature1", 7053,
                                                                               "Tab Features Demonstrated:\n\n"
                                                                               "✓ Tab icons (16x16 images next to title)\n"
                                                                               "✓ Notification badges (red circles with counts)\n"
                                                                               "✓ Close buttons (× on each tab)\n"
                                                                               "✓ Hover effects on all interactive elements\n"
                                                                               "✓ Visual feedback for user actions",
                                                                               Color(250, 250, 250)));
        featureTabs->SetTabIcon(tabIndex1, "assets/icons/envelope-icon.png");
        featureTabs->SetTabBadge(tabIndex1, "5", true);  // 5 unread messages

        int tabIndex2 = featureTabs->AddTab("Notifications", CreateSampleTabContent("Feature2", 7055,
                                                                                    "Notification center content.\n\n"
                                                                                    "Badge shows 12 new notifications.",
                                                                                    Color(245, 250, 255)));
        featureTabs->SetTabIcon(tabIndex2, "assets/icons/bell-icon.png");
        featureTabs->SetTabBadge(tabIndex2, "999+", true);  // 12 notifications

        int tabIndex3 = featureTabs->AddTab("Settings", CreateSampleTabContent("Feature3", 7057,
                                                                               "Settings and preferences.\n\n"
                                                                               "This tab has an icon but no badge.",
                                                                               Color(250, 245, 255)));
        featureTabs->SetTabIcon(tabIndex3, "assets/icons/settings.png");

        featureTabs->SetActiveTab(0);
        mainContainer->AddChild(featureTabs);
        yOffset += 230;

        // ========================================
        // SECTION 3: TAB OVERFLOW & SCROLLING
        // ========================================
        auto section3Title = std::make_shared<UltraCanvasLabel>("Section3Title", 7060, 20, yOffset, 960, 30);
        section3Title->SetText("3. TAB OVERFLOW HANDLING (Dropdown & Search)");
        section3Title->SetFontSize(14);
        section3Title->SetFontWeight(FontWeight::Bold);
        section3Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section3Title);
        yOffset += 35;

        // --- Many Tabs with Overflow Dropdown ---
        auto overflowLabel = std::make_shared<UltraCanvasLabel>("OverflowLabel", 7061, 40, yOffset, 700, 20);
        overflowLabel->SetText("20 Tabs with Overflow Dropdown + Search (when scrollbar appears)");
        overflowLabel->SetFontSize(11);
        overflowLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(overflowLabel);
        yOffset += 25;

        auto overflowTabs = std::make_shared<UltraCanvasTabbedContainer>("OverflowTabs", 7062, 40, yOffset, 800, 220);
        overflowTabs->SetTabStyle(TabStyle::Modern);
        overflowTabs->SetTabHeight(30);
        overflowTabs->SetOverflowDropdownPosition(OverflowDropdownPosition::Left);
        overflowTabs->SetDropdownSearchEnabled(true);  // Search appears when dropdown needs scrollbar
        overflowTabs->SetOverflowDropdownWidth(28);

        // Add 20 tabs to trigger overflow
        for (int i = 1; i <= 20; i++) {
            std::ostringstream titleStream;
            titleStream << "Tab " << i;

            std::ostringstream contentStream;
            contentStream << "Content for Tab " << i << "\n\n"
                          << "This demonstrates the overflow dropdown feature.\n"
                          << "Click the dropdown button (≡) to see all tabs.\n"
                          << "Search field appears when dropdown has scrollbar.";

            Color tabColor = Color(
                    240 + (i % 3) * 5,
                    245 + (i % 5) * 2,
                    250 - (i % 4) * 3
            );

            overflowTabs->AddTab(titleStream.str(),
                                 CreateSampleTabContent("Overflow" + std::to_string(i), 7063 + i,
                                                        contentStream.str(), tabColor));
        }

        overflowTabs->SetActiveTab(0);
        mainContainer->AddChild(overflowTabs);
        yOffset += 230;

        // ========================================
        // SECTION 4: NEW TAB BUTTON - STYLE A (Fixed Tab)
        // ========================================
        auto section4Title = std::make_shared<UltraCanvasLabel>("Section4Title", 7090, 20, yOffset, 960, 30);
        section4Title->SetText("4. NEW TAB BUTTON - STYLE A (Browser-Style Fixed Tab)");
        section4Title->SetFontSize(14);
        section4Title->SetFontWeight(FontWeight::Bold);
        section4Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section4Title);
        yOffset += 35;

        auto newTabALabel = std::make_shared<UltraCanvasLabel>("NewTabALabel", 7091, 40, yOffset, 700, 20);
        newTabALabel->SetText("Click the '+' tab to create new tabs (Browser behavior)");
        newTabALabel->SetFontSize(11);
        newTabALabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(newTabALabel);
        yOffset += 25;

        auto newTabATabs = std::make_shared<UltraCanvasTabbedContainer>("NewTabATabs", 7092, 40, yOffset, 800, 220);
        newTabATabs->SetTabStyle(TabStyle::Rounded);
        newTabATabs->SetTabHeight(30);
        newTabATabs->SetTabCornerRadius(8.0f);
        newTabATabs->SetCloseMode(TabCloseMode::Closable);

        // Enable Style A - Fixed Tab
        newTabATabs->SetShowNewTabButton(true);
        newTabATabs->SetNewTabButtonWidth(32);
        newTabATabs->SetNewButtonColor(Colors::Transparent);
//        newTabATabs->SetNewTabButtonIcon("+");
        //newTabATabs->UseDefaultNewTabColors(true);  // Inherit tab colors

        // Set up new tab callback
        newTabATabs->onNewTabRequest = [newTabATabs]() {
            static int tabCounter = 1;

            std::ostringstream titleStream;
            titleStream << "New Tab " << tabCounter;

            std::ostringstream contentStream;
            contentStream << "This is a dynamically created tab!\n\n"
                          << "Tab #" << tabCounter << " created at runtime.\n"
                          << "Click the '+' tab to create more tabs.\n"
                          << "Use the '×' button to close tabs.";

            auto content = CreateSampleTabContent(
                    "DynamicTab" + std::to_string(tabCounter),
                    8000 + tabCounter,
                    contentStream.str(),
                    Color(240 + (tabCounter % 10), 250, 240 + (tabCounter % 15))
            );

            int newIndex = newTabATabs->AddTab(titleStream.str(), content);
            newTabATabs->SetActiveTab(newIndex);

            tabCounter++;
        };

        // Add initial tabs
        newTabATabs->AddTab("Welcome", CreateSampleTabContent("NewTabA1", 7093,
                                                              "Style A - Fixed Tab Button Features:\n\n"
                                                              "• Mini tab that looks like regular tabs\n"
                                                              "• Fixed width (32px default)\n"
                                                              "• Inherits visual styling from tab style\n"
                                                              "• Always visible, doesn't scroll\n"
                                                              "• Used in Chrome, Edge, Firefox browsers",
                                                              Color(250, 250, 250)));

        newTabATabs->AddTab("Tab 2", CreateSampleTabContent("NewTabA2", 7095,
                                                            "Click the '+' tab to create new tabs!\n\n"
                                                            "Each new tab can be closed individually.",
                                                            Color(245, 250, 255)));

        newTabATabs->SetActiveTab(0);
        mainContainer->AddChild(newTabATabs);
        yOffset += 230;

        // ========================================
        // SECTION 5: NEW TAB BUTTON - STYLE B (Icon Button)
        // ========================================
        auto section5Title = std::make_shared<UltraCanvasLabel>("Section5Title", 7100, 20, yOffset, 960, 30);
        section5Title->SetText("5. NEW TAB BUTTON - STYLE B (VS Code-Style Icon Button)");
        section5Title->SetFontSize(14);
        section5Title->SetFontWeight(FontWeight::Bold);
        section5Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section5Title);
        yOffset += 35;

        auto newTabBLabel = std::make_shared<UltraCanvasLabel>("NewTabBLabel", 7101, 40, yOffset, 700, 20);
        newTabBLabel->SetText("Click the '+' icon button to create new tabs (Code editor behavior)");
        newTabBLabel->SetFontSize(11);
        newTabBLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(newTabBLabel);
        yOffset += 25;

        auto newTabBTabs = std::make_shared<UltraCanvasTabbedContainer>("NewTabBTabs", 7102, 40, yOffset, 800, 220);
        newTabBTabs->SetTabStyle(TabStyle::Flat);
        newTabBTabs->SetTabHeight(28);
        newTabBTabs->SetCloseMode(TabCloseMode::Closable);

        // Enable Style B - Icon Button
        newTabBTabs->SetShowNewTabButton(true);
//        newTabBTabs->SetNewTabButtonSize(24,24);
//        newTabBTabs->SetNewTabButtonIcon("+");
//        newTabBTabs->SetNewTabIconSize(16.0f);
//        newTabBTabs->SetNewTabIconColors(
//                Color(100, 100, 100),  // Normal
//                Color(50, 50, 50)      // Hover
//        );
//        newTabBTabs->SetNewTabIconBackgroundColors(
//                Colors::Transparent,           // Normal
//                Color(220, 220, 220, 100)     // Hover
//        );

        // Set up new tab callback
        newTabBTabs->onNewTabRequest = [newTabBTabs]() {
            static int tabCounter = 1;

            std::ostringstream titleStream;
            titleStream << "File " << tabCounter << ".txt";

            std::ostringstream contentStream;
            contentStream << "Code Editor Tab #" << tabCounter << "\n\n"
                          << "This mimics VS Code tab behavior.\n"
                          << "Compact icon button for creating tabs.\n"
                          << "Minimal UI, maximum workspace.";

            auto content = CreateSampleTabContent(
                    "CodeTab" + std::to_string(tabCounter),
                    9000 + tabCounter,
                    contentStream.str(),
                    Color(248, 248, 248)
            );

            int newIndex = newTabBTabs->AddTab(titleStream.str(), content);
            newTabBTabs->SetActiveTab(newIndex);

            tabCounter++;
        };

        // Add initial tabs
        newTabBTabs->AddTab("main.cpp", CreateSampleTabContent("NewTabB1", 7103,
                                                               "Style B - Icon Button Features:\n\n"
                                                               "• Simple icon button (24x24 default)\n"
                                                               "• Separate from tab visual style\n"
                                                               "• More compact and minimal\n"
                                                               "• Centered in tab bar\n"
                                                               "• Used in VS Code, Sublime Text, code editors",
                                                               Color(248, 248, 248)));

        newTabBTabs->AddTab("utils.h", CreateSampleTabContent("NewTabB2", 7105,
                                                              "Click the small '+' button to add files.\n\n"
                                                              "Perfect for code editor interfaces.",
                                                              Color(250, 250, 250)));

        newTabBTabs->SetActiveTab(0);
        newTabBTabs->SetCloseMode(TabCloseMode::Closable);
        mainContainer->AddChild(newTabBTabs);
        yOffset += 230;

        // ========================================
        // SECTION 6: TAB POSITIONS (Top, Bottom, Left, Right)
        // ========================================
        auto section6Title = std::make_shared<UltraCanvasLabel>("Section6Title", 7110, 20, yOffset, 960, 30);
        section6Title->SetText("6. TAB POSITIONS (Top, Bottom, Left, Right)");
        section6Title->SetFontSize(14);
        section6Title->SetFontWeight(FontWeight::Bold);
        section6Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section6Title);
        yOffset += 35;

        // Create a container for side-by-side position demos
        auto positionContainer = std::make_shared<UltraCanvasContainer>("PositionContainer", 7111, 40, yOffset, 800, 480);
        positionContainer->SetBackgroundColor(Colors::Transparent);

        // Bottom Position Tabs
        auto bottomTabs = std::make_shared<UltraCanvasTabbedContainer>("BottomTabs", 7112, 10, 10, 370, 190);
        bottomTabs->SetTabPosition(TabPosition::Bottom);
        bottomTabs->SetTabStyle(TabStyle::Classic);
        bottomTabs->AddTab("Bottom 1", CreateSampleTabContent("Bottom1", 7113,
                                                              "Tabs at bottom\n(Terminal/Console style)", Color(250, 250, 250), 340, 150));
        bottomTabs->AddTab("Bottom 2", CreateSampleTabContent("Bottom2", 7115,
                                                              "Useful for tool windows", Color(245, 250, 255), 340, 150));
        bottomTabs->SetActiveTab(0);
        positionContainer->AddChild(bottomTabs);

        // Left Position Tabs
        auto leftTabs = std::make_shared<UltraCanvasTabbedContainer>("LeftTabs", 7116, 410, 10, 370, 190);
        leftTabs->SetTabPosition(TabPosition::Left);
        leftTabs->SetTabStyle(TabStyle::Flat);
        leftTabs->AddTab("Left 1", CreateSampleTabContent("Left1", 7117,
                                                          "Vertical tabs on left side", Color(250, 250, 250), 240, 150));
        leftTabs->AddTab("Left Panel 2", CreateSampleTabContent("Left2", 7119,
                                                          "Sidebar navigation style", Color(245, 250, 255), 240, 150));
        leftTabs->SetActiveTab(0);
        positionContainer->AddChild(leftTabs);

        // Right Position Tabs
        auto rightTabs = std::make_shared<UltraCanvasTabbedContainer>("RightTabs", 7120, 10, 260, 780, 200);
        rightTabs->SetTabPosition(TabPosition::Right);
        rightTabs->SetTabStyle(TabStyle::Modern);
        rightTabs->AddTab("Right 1", CreateSampleTabContent("Right1", 7121,
                                                                  "Tabs on right side - useful for property panels and inspector windows", Color(250, 250, 250)));
        rightTabs->AddTab("Right Panel 2", CreateSampleTabContent("Right2", 7123,
                                                                  "Common in design tools and IDEs for tool palettes", Color(245, 250, 255)));
        rightTabs->SetActiveTab(0);
        positionContainer->AddChild(rightTabs);

        mainContainer->AddChild(positionContainer);
        yOffset += 510;

        // ========================================
        // SECTION 7: ADVANCED FEATURES
        // ========================================
        /*
        auto section7Title = std::make_shared<UltraCanvasLabel>("Section7Title", 7130, 20, yOffset, 960, 30);
        section7Title->SetText("7. ADVANCED FEATURES (Drag & Drop, Callbacks, Custom Colors)");
        section7Title->SetFontSize(14);
        section7Title->SetFontWeight(FontWeight::Bold);
        section7Title->SetTextColor(Color(0, 100, 200));
        mainContainer->AddChild(section7Title);
        yOffset += 35;

        auto advancedLabel = std::make_shared<UltraCanvasLabel>("AdvancedLabel", 7131, 40, yOffset, 700, 20);
        advancedLabel->SetText("Custom styled tabs with per-tab colors and event callbacks");
        advancedLabel->SetFontSize(11);
        advancedLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(advancedLabel);
        yOffset += 25;

        auto advancedTabs = std::make_shared<UltraCanvasTabbedContainer>("AdvancedTabs", 7132, 40, yOffset, 800, 220);
        advancedTabs->SetTabStyle(TabStyle::Rounded);
        advancedTabs->SetTabHeight(32);
        advancedTabs->SetShowTabSeparators(true);

        // Add tabs with custom colors
        int redTabIndex = advancedTabs->AddTab("Error Log", CreateSampleTabContent("Advanced1", 7133,
                                                                                   "Custom Tab Features:\n\n"
                                                                                   "• Per-tab background colors\n"
                                                                                   "• Per-tab text colors\n"
                                                                                   "• Tab separators (vertical lines)\n"
                                                                                   "• Event callbacks (onTabChange, onTabClose)\n"
                                                                                   "• Drag & drop reordering (try it!)",
                                                                                   Color(255, 240, 240)));
        advancedTabs->SetTabBackgroundColor(redTabIndex, Color(255, 200, 200));
        advancedTabs->SetTabTextColor(redTabIndex, Color(150, 0, 0));

        int greenTabIndex = advancedTabs->AddTab("Success", CreateSampleTabContent("Advanced2", 7135,
                                                                                   "This tab has custom green coloring.\n\n"
                                                                                   "Perfect for status indicators!",
                                                                                   Color(240, 255, 240)));
        advancedTabs->SetTabBackgroundColor(greenTabIndex, Color(200, 255, 200));
        advancedTabs->SetTabTextColor(greenTabIndex, Color(0, 120, 0));

        int blueTabIndex = advancedTabs->AddTab("Information", CreateSampleTabContent("Advanced3", 7137,
                                                                                      "Blue themed tab.\n\n"
                                                                                      "Each tab can have unique styling.",
                                                                                      Color(240, 240, 255)));
        advancedTabs->SetTabBackgroundColor(blueTabIndex, Color(200, 220, 255));
        advancedTabs->SetTabTextColor(blueTabIndex, Color(0, 60, 150));
        advancedTabs->SetCloseMode(TabCloseMode::Closable);

        // Set up callbacks
        advancedTabs->onTabChange = [](int oldIndex, int newIndex) {
            std::cout << "Tab changed from " << oldIndex << " to " << newIndex << std::endl;
        };

        advancedTabs->SetActiveTab(0);
        mainContainer->AddChild(advancedTabs);
        yOffset += 230;
        */
        // ========================================
        // FOOTER
        // ========================================
        auto footer = std::make_shared<UltraCanvasLabel>("Footer", 7140, 20, yOffset, 960, 40);
        footer->SetText("UltraCanvas Tabbed Container Demo\n"
                        "All features demonstrated above are production-ready and fully functional.");
        footer->SetFontSize(10);
        footer->SetTextColor(Color(120, 120, 120));
        footer->SetAlignment(TextAlignment::Center);
        mainContainer->AddChild(footer);
        yOffset += 50;

        // Set main container content height for scrolling
//        mainContainer->SetHeight(yOffset);

        return mainContainer;
    }

} // namespace UltraCanvas