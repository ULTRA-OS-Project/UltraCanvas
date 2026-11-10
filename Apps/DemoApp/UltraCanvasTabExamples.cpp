// Apps/DemoApp/UltraCanvasTabExamples.cpp
// Comprehensive demonstration of all tabbed container features
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  section, the tabbed containers as wide as the card, every
//                  text sized to fit, tab pages scroll when their text is taller
// Version: 1.1.0 - the five Pill colourways
// Version: 1.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasCheckbox.h"
#include <sstream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== HELPER: CREATE SAMPLE TAB CONTENT =====
    std::shared_ptr<UltraCanvasContainer> CreateSampleTabContent(
            const std::string& id, const std::string& text, const Color& bgColor, int w = 550, int h = 180) {

        auto content = std::make_shared<UltraCanvasContainer>(id, 0, 0, w, h);
        content->SetBackgroundColor(bgColor);
        content->SetPadding(15);
        content->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        // A text taller than the tab (a longer translation) scrolls.
        ContainerStyle style;
        style.autoShowScrollbars = true;
        style.autoShowHorizontalScrollbar = false;
        content->SetContainerStyle(style);

        // The first child: the Pill examples recolour it.
        auto label = DemoKit::CreateText(id + "_label", text);
        label->SetTextColor(Colors::Black);
        DemoKit::Add(content, label);

        return content;
    }

// ===== MAIN COMPREHENSIVE TAB DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTabExamples() {
        auto mainContainer = DemoKit::CreatePage("ComprehensiveTabDemo",
                Tr("UltraCanvas Tabbed Container - Comprehensive Demo"),
                Tr("Showcasing all tab styles, features, and configurations"));

        // One example: its bold caption over the tabbed container, which
        // keeps its height and takes the card's width.
        auto addExample = [](const std::shared_ptr<UltraCanvasContainer>& card,
                             const std::string& labelId, const std::string& text) {
            auto group = DemoKit::CreateColumn(labelId + "Group", 4);
            DemoKit::Add(card, group);
            auto label = DemoKit::AddText(group, labelId, text);
            label->SetFontSize(11);
            label->SetFontWeight(FontWeight::Bold);
            return group;
        };

        // ========================================
        // SECTION 1: TAB STYLES (Classic, Modern, Flat, Rounded)
        // ========================================
        auto card1 = DemoKit::AddCard(mainContainer, "Section1", Tr("1. TAB VISUAL STYLES"));
        card1->layout.SetFlexGap(14);

        // --- Classic Style ---
        auto classicGroup = addExample(card1, "ClassicLabel", Tr("Classic Style (Windows 7 look with 3D borders)"));

        auto classicTabs = std::make_shared<UltraCanvasTabbedContainer>("ClassicTabs", 0, 0, 800, 220);
        classicTabs->SetTabStyle(TabStyle::Classic);
        classicTabs->SetTabHeight(30);
//        classicTabs->SetCornerRadius(4.0f);
//        classicTabs->SetTabElevation(2.0f);

        classicTabs->AddTab(Tr("Home"), CreateSampleTabContent("Classic1",
                                                           Tr("Classic Style Features:\n\n"
                                                           "• 3D raised effect for active tabs\n"
                                                           "• Subtle shadows for depth\n"
                                                           "• Border connects active tab to content area\n"
                                                           "• Inactive tabs appear slightly recessed\n"
                                                           "• Professional Windows 7/10 aesthetic"),
                                                           Color(250, 250, 250)));

        classicTabs->AddTab(Tr("Documents"), CreateSampleTabContent("Classic2",
                                                                Tr("This tab demonstrates how multiple tabs work together.\n\n"
                                                                "Click between tabs to see the 3D effect and smooth transitions."),
                                                                Color(245, 250, 255)));

        classicTabs->AddTab(Tr("Settings"), CreateSampleTabContent("Classic3",
                                                               Tr("Settings content goes here.\n\n"
                                                               "Each tab can contain any UltraCanvas UI elements."),
                                                               Color(250, 245, 255)));

        classicTabs->SetActiveTab(0);
        classicTabs->SetCloseMode(TabCloseMode::NoClose);
        DemoKit::AddStretched(classicGroup, classicTabs);

        // --- Modern Style ---
        auto modernGroup = addExample(card1, "ModernLabel", Tr("Modern Style (Material Design with bottom indicator)"));

        auto modernTabs = std::make_shared<UltraCanvasTabbedContainer>("ModernTabs", 0, 0, 800, 220);
        modernTabs->SetTabStyle(TabStyle::Modern);
        modernTabs->SetTabHeight(32);
//        modernTabs->SetActiveTabIndicatorHeight(3.0f);
//        modernTabs->SetActiveTabIndicatorColor(Color(33, 150, 243)); // Material Blue

        modernTabs->AddTab(Tr("Files and Folders"), CreateSampleTabContent("Modern1",
                                                                       Tr("Modern Style Features:\n\n"
                                                                       "• Flat design with no borders\n"
                                                                       "• Bold bottom indicator line (3px) under active tab\n"
                                                                       "• Subtle background color change on hover\n"
                                                                       "• Clean, minimal Material Design appearance\n"
                                                                       "• Popular in Google and Android apps"),
                                                                       Color(250, 250, 250)));

        modernTabs->AddTab(Tr("User Profiles"), CreateSampleTabContent("Modern2",
                                                                   Tr("User profiles and management features.\n\n"
                                                                   "Notice the blue indicator line that follows the active tab."),
                                                                   Color(245, 250, 255)));

        modernTabs->AddTab(Tr("Contacts"), CreateSampleTabContent("Modern3",
                                                              Tr("Contact management interface.\n\n"
                                                              "Modern style is ideal for web applications and mobile-inspired UIs."),
                                                              Color(250, 245, 255)));

        modernTabs->AddTab(Tr("Applications"), CreateSampleTabContent("Modern4",
                                                                  Tr("Application settings and configuration."),
                                                                  Color(255, 250, 245)));

        modernTabs->SetActiveTab(0);
        modernTabs->SetCloseMode(TabCloseMode::NoClose);
        DemoKit::AddStretched(modernGroup, modernTabs);

        // --- Flat Style ---
        auto flatGroup = addExample(card1, "FlatLabel", Tr("Flat Style (Minimal text-only design)"));

        auto flatTabs = std::make_shared<UltraCanvasTabbedContainer>("FlatTabs", 0, 0, 800, 220);
        flatTabs->SetTabStyle(TabStyle::Flat);
        flatTabs->SetTabHeight(28);

        flatTabs->AddTab(Tr("General"), CreateSampleTabContent("Flat1",
                                                           Tr("Flat Style Features:\n\n"
                                                           "• Completely flat with no borders\n"
                                                           "• Only active tab shows different background\n"
                                                           "• Maximum minimalism\n"
                                                           "• Very clean and unobtrusive\n"
                                                           "• Perfect for simple, elegant interfaces"),
                                                           Color(250, 250, 250)));

        flatTabs->AddTab(Tr("Advanced"), CreateSampleTabContent("Flat2",
                                                            Tr("Advanced configuration options.\n\n"
                                                            "Flat style keeps the focus on content, not chrome."),
                                                            Color(245, 250, 255)));

        flatTabs->AddTab(Tr("About"), CreateSampleTabContent("Flat3",
                                                         Tr("About this application.\n\n"
                                                         "Minimal UI, maximum content."),
                                                         Color(250, 245, 255)));

        flatTabs->SetActiveTab(0);
        flatTabs->SetCloseMode(TabCloseMode::NoClose);
        DemoKit::AddStretched(flatGroup, flatTabs);

        // --- Rounded Style ---
        auto roundedGroup = addExample(card1, "RoundedLabel", Tr("Rounded Style (Chrome/Firefox browser-style)"));

        auto roundedTabs = std::make_shared<UltraCanvasTabbedContainer>("RoundedTabs", 0, 0, 800, 220);
        roundedTabs->SetTabStyle(TabStyle::Rounded);
        roundedTabs->SetTabHeight(30);
        roundedTabs->SetTabCornerRadius(8.0f);
        roundedTabs->SetTabElevation(1.0f);

        roundedTabs->AddTab(Tr("Page 1"), CreateSampleTabContent("Rounded1",
                                                             Tr("Rounded Style Features:\n\n"
                                                             "• Rounded top-left and top-right corners\n"
                                                             "• Active tab connects smoothly to content area\n"
                                                             "• Optional shadow for depth\n"
                                                             "• Soft, friendly appearance\n"
                                                             "• Used in Chrome, Edge, Firefox browsers"),
                                                             Color(240, 240, 255)));

        roundedTabs->AddTab(Tr("Page 2"), CreateSampleTabContent("Rounded2",
                                                             Tr("Another page in the rounded style.\n\n"
                                                             "Notice the smooth rounded corners on the tabs."),
                                                             Color(240, 240, 255)));

        roundedTabs->AddTab(Tr("Page 3"), CreateSampleTabContent("Rounded3",
                                                             Tr("Third page content.\n\n"
                                                             "This style is very popular in modern applications."),
                                                             Color(240, 240, 255)));

        roundedTabs->SetActiveTab(0);
        DemoKit::AddStretched(roundedGroup, roundedTabs);

        // --- Pill Style: five colourways ---
        // The same style throughout; only colours and insets differ. They
        // match the five in Docs/UltraCanvas/UltraCanvasTabExamples.md and
        // Tests/TabPillStyleScreenshotTest.cpp.
        struct PillColourway {
            std::string name;
            Color bar;
            Color activeFill, activeBorder, activeText, activeClose;
            Color inactiveFill, inactiveText;
            Color hoverFill;
            Color closeButton;
            Color content, contentDivider;
            float chipRadius;      // 0 = full capsule
            bool newTabButton;
        };
        const PillColourway pillColourways[] = {
            {Tr("Pill Style 1 - outlined pill on a tinted bar (unselected tabs are text only)"),
             Color(229, 234, 241),
             Colors::White, Color(96, 146, 224), Color(30, 37, 46), Colors::Transparent,
             Colors::Transparent, Color(84, 96, 112),
             Color(255, 255, 255, 140),
             Color(84, 96, 112), Colors::White, Color(205, 212, 222), 0.0f, true},
            {Tr("Pill Style 2 - tinted pill with a teal outline"),
             Color(250, 251, 252),
             Color(228, 244, 240), Color(34, 150, 130), Color(20, 60, 55), Colors::Transparent,
             Colors::Transparent, Color(90, 96, 104),
             Color(238, 240, 243),
             Color(110, 116, 125), Colors::White, Color(226, 229, 234), 0.0f, false},
            {Tr("Pill Style 3 - solid accent pill, grey pills for the others"),
             Color(245, 246, 248),
             Color(41, 112, 196), Colors::Transparent, Colors::White, Color(225, 236, 250),
             Color(226, 229, 234), Color(60, 64, 72),
             Color(212, 216, 224),
             Color(110, 116, 125), Colors::White, Color(226, 229, 234), 0.0f, false},
            {Tr("Pill Style 4 - neutral chips (corner radius 6)"),
             Color(252, 252, 253),
             Colors::White, Color(205, 205, 214), Color(30, 37, 46), Colors::Transparent,
             Color(240, 241, 244), Color(90, 90, 100),
             Color(232, 233, 237),
             Color(120, 120, 130), Colors::White, Color(228, 228, 232), 6.0f, false},
            {Tr("Pill Style 5 - dark bar"),
             Color(30, 30, 36),
             Color(46, 46, 58), Color(120, 170, 240), Color(240, 240, 245), Colors::Transparent,
             Colors::Transparent, Color(170, 170, 185),
             Color(255, 255, 255, 24),
             Color(170, 170, 185), Color(24, 24, 30), Color(60, 60, 72), 0.0f, false},
        };

        int pillIndex = 0;
        for (const PillColourway& cw : pillColourways) {
            const std::string suffix = std::to_string(++pillIndex);
            const bool first = (pillIndex == 1);

            auto pillGroup = addExample(card1, "PillLabel" + suffix, cw.name);

            // The first colourway gets a full page; the others only need
            // their tab bar and a line of content.
            const int height = first ? 220 : 110;
            auto pillTabs = std::make_shared<UltraCanvasTabbedContainer>("PillTabs" + suffix, 0, 0, 800, height);
            pillTabs->SetTabStyle(TabStyle::Pill);
            pillTabs->SetTabHeight(36);
            pillTabs->SetPillInset(2, 4);
            pillTabs->SetPillCornerRadius(cw.chipRadius);
            pillTabs->SetCloseMode(TabCloseMode::Closable);
            pillTabs->SetTabMaxWidth(220);
            pillTabs->fontSize = 12;
            pillTabs->SetTabBarColor(cw.bar);
            pillTabs->SetActiveTabBackgroundColor(cw.activeFill);
            pillTabs->SetActiveTabBorderColor(cw.activeBorder);
            pillTabs->SetActiveTabTextColor(cw.activeText);
            pillTabs->SetActiveTabCloseButtonColor(cw.activeClose);
            pillTabs->SetInactiveTabBackgroundColor(cw.inactiveFill);
            pillTabs->SetInactiveTabTextColor(cw.inactiveText);
            pillTabs->SetHoveredTabBackgroundColor(cw.hoverFill);
            pillTabs->SetCloseButtonColor(cw.closeButton);
            pillTabs->SetCloseButtonHoverColor(cw.activeText);
            pillTabs->contentAreaColor = cw.content;
            pillTabs->tabContentBorderColor = cw.contentDivider;
            pillTabs->SetShowNewTabButton(cw.newTabButton);
            pillTabs->SetNewTabButtonShape(NewTabButtonShape::Circle);
            pillTabs->newTabButtonColor = Colors::Transparent;
            pillTabs->newTabButtonHoverColor = cw.hoverFill;
            pillTabs->newTabButtonIconColor = cw.inactiveText;

            const Color pageText = (cw.content.r < 128) ? Color(220, 220, 228) : Colors::Black;
            auto page = [&](const std::string& id, const std::string& text) {
                auto content = CreateSampleTabContent(id, text, cw.content, 550, first ? 180 : 70);
                if (auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(content->GetChildren().front()))
                    label->SetTextColor(pageText);
                return content;
            };
            pillTabs->AddTab(Tr("Inbox"), page("Pill" + suffix + "a", first ?
                                                         Tr("Pill Style Features:\n\n"
                                                         "• Each tab is a capsule inset in the tab bar\n"
                                                         "• The open tab is filled and outlined in its own colours\n"
                                                         "• Unselected tabs are plain text, or quiet pills, until hovered\n"
                                                         "• Colours and insets give the five colourways on this page\n"
                                                         "• The page keeps a hairline, not a frame") :
                                                         TrF("Colourway {0}: the same TabStyle::Pill, other colours.", suffix)));
            pillTabs->AddTab(Tr("UltraMail: Add/Delete account"), page("Pill" + suffix + "b",
                                                         Tr("A long title is truncated inside its capsule.")));
            pillTabs->AddTab(Tr("Drafts"), page("Pill" + suffix + "c",
                                                         Tr("Hover a tab to see its capsule fade in.")));
            pillTabs->SetTabIcon(0, NormalizePath(GetResourcesDir() + "media/icons/home-icon.png"));
            pillTabs->SetTabIcon(1, NormalizePath(GetResourcesDir() + "media/icons/settings.png"));
            pillTabs->SetTabIcon(2, NormalizePath(GetResourcesDir() + "media/icons/document.png"));
            pillTabs->SetActiveTab(first ? 0 : 1);
            DemoKit::AddStretched(pillGroup, pillTabs);
        }

        // ========================================
        // SECTION 2: TAB FEATURES (Icons, Badges, Close Buttons)
        // ========================================
        auto card2 = DemoKit::AddCard(mainContainer, "Section2", Tr("2. TAB FEATURES (Icons, Badges, Close Buttons)"));
        card2->layout.SetFlexGap(14);

        // --- Tabs with Icons and Badges ---
        auto featuresGroup = addExample(card2, "FeaturesLabel", Tr("Tabs with Icons, Notification Badges, and Close Buttons"));

        auto featureTabs = std::make_shared<UltraCanvasTabbedContainer>("FeatureTabs", 0, 0, 800, 220);
        featureTabs->SetTabStyle(TabStyle::Rounded);
        featureTabs->SetTabHeight(32);
        featureTabs->SetCloseMode(TabCloseMode::Closable);
        featureTabs->SetIconSize(16);

        // Add tabs with icons
        int tabIndex1 = featureTabs->AddTab(Tr("Messages"), CreateSampleTabContent("Feature1",
                                                                               Tr("Tab Features Demonstrated:\n\n"
                                                                               "• Tab icons (16x16 images next to title)\n"
                                                                               "• Notification badges (red circles with counts)\n"
                                                                               "• Close buttons (× on each tab)\n"
                                                                               "• Hover effects on all interactive elements\n"
                                                                               "• Visual feedback for user actions"),
                                                                               Color(250, 250, 250)));
        featureTabs->SetTabIcon(tabIndex1, NormalizePath(GetResourcesDir() + "media/icons/envelope-icon.png"));
        featureTabs->SetTabBadge(tabIndex1, "5", true);  // 5 unread messages

        int tabIndex2 = featureTabs->AddTab(Tr("Notifications"), CreateSampleTabContent("Feature2",
                                                                                    Tr("Notification center content.\n\n"
                                                                                    "Badge shows 12 new notifications."),
                                                                                    Color(245, 250, 255)));
        featureTabs->SetTabIcon(tabIndex2, NormalizePath(GetResourcesDir() + "media/icons/bell-icon.png"));
        featureTabs->SetTabBadge(tabIndex2, "999+", true);  // 12 notifications

        int tabIndex3 = featureTabs->AddTab(Tr("Settings"), CreateSampleTabContent("Feature3",
                                                                               Tr("Settings and preferences.\n\n"
                                                                               "This tab has an icon but no badge."),
                                                                               Color(250, 245, 255)));
        featureTabs->SetTabIcon(tabIndex3, NormalizePath(GetResourcesDir() + "media/icons/settings.png"));

        featureTabs->SetActiveTab(0);
        DemoKit::AddStretched(featuresGroup, featureTabs);

        // ========================================
        // SECTION 3: TAB OVERFLOW & SCROLLING
        // ========================================
        auto card3 = DemoKit::AddCard(mainContainer, "Section3", Tr("3. TAB OVERFLOW HANDLING (Dropdown & Search)"));
        card3->layout.SetFlexGap(14);

        // --- Many Tabs with Overflow Dropdown ---
        auto overflowGroup = addExample(card3, "OverflowLabel", Tr("20 Tabs with Overflow Dropdown + Search (when scrollbar appears)"));

        auto overflowTabs = std::make_shared<UltraCanvasTabbedContainer>("OverflowTabs", 0, 0, 800, 220);
        overflowTabs->SetTabStyle(TabStyle::Modern);
        overflowTabs->SetTabHeight(30);
        overflowTabs->SetOverflowDropdownPosition(OverflowDropdownPosition::Left);
        overflowTabs->SetDropdownSearchEnabled(true);  // Search appears when dropdown needs scrollbar
        overflowTabs->SetOverflowDropdownWidth(28);

        // Add 20 tabs to trigger overflow
        for (int i = 1; i <= 20; i++) {
            const std::string title = TrF("Tab {0}", i);
            const std::string contentText = TrF("Content for Tab {0}\n\n"
                                                "This demonstrates the overflow dropdown feature.\n"
                                                "Click the dropdown arrow at the end of the tab bar to see all tabs.\n"
                                                "Search field appears when dropdown has scrollbar.", i);

            Color tabColor = Color(
                    240 + (i % 3) * 5,
                    245 + (i % 5) * 2,
                    250 - (i % 4) * 3
            );

            overflowTabs->AddTab(title,
                                 CreateSampleTabContent("Overflow" + std::to_string(i),
                                                        contentText, tabColor));
        }

        overflowTabs->SetActiveTab(0);
        DemoKit::AddStretched(overflowGroup, overflowTabs);

        // ========================================
        // SECTION 4: NEW TAB BUTTON - STYLE A (Fixed Tab)
        // ========================================
        auto card4 = DemoKit::AddCard(mainContainer, "Section4", Tr("4. NEW TAB BUTTON - STYLE A (Browser-Style Fixed Tab)"));
        card4->layout.SetFlexGap(14);

        auto newTabAGroup = addExample(card4, "NewTabALabel", Tr("Click the '+' tab to create new tabs (Browser behavior)"));

        auto newTabATabs = std::make_shared<UltraCanvasTabbedContainer>("NewTabATabs", 0, 0, 800, 220);
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
        newTabATabs->onNewTabRequest = [newTabATabs = newTabATabs.get()]() {
            static int tabCounter = 1;

            const std::string title = TrF("New Tab {0}", tabCounter);
            const std::string contentText = TrF("This is a dynamically created tab!\n\n"
                                                "Tab #{0} created at runtime.\n"
                                                "Click the '+' tab to create more tabs.\n"
                                                "Use the '×' button to close tabs.", tabCounter);

            auto content = CreateSampleTabContent(
                    "DynamicTab" + std::to_string(tabCounter),
                    
                    contentText,
                    Color(240 + (tabCounter % 10), 250, 240 + (tabCounter % 15))
            );

            int newIndex = newTabATabs->AddTab(title, content);
            newTabATabs->SetActiveTab(newIndex);

            tabCounter++;
        };

        // Add initial tabs
        newTabATabs->AddTab(Tr("Welcome"), CreateSampleTabContent("NewTabA1",
                                                              Tr("Style A - Fixed Tab Button Features:\n\n"
                                                              "• Mini tab that looks like regular tabs\n"
                                                              "• Fixed width (32px default)\n"
                                                              "• Inherits visual styling from tab style\n"
                                                              "• Always visible, doesn't scroll\n"
                                                              "• Used in Chrome, Edge, Firefox browsers"),
                                                              Color(250, 250, 250)));

        newTabATabs->AddTab(Tr("Tab 2"), CreateSampleTabContent("NewTabA2",
                                                            Tr("Click the '+' tab to create new tabs!\n\n"
                                                            "Each new tab can be closed individually."),
                                                            Color(245, 250, 255)));

        newTabATabs->SetActiveTab(0);
        DemoKit::AddStretched(newTabAGroup, newTabATabs);

        // ========================================
        // SECTION 5: NEW TAB BUTTON - STYLE B (Icon Button)
        // ========================================
        auto card5 = DemoKit::AddCard(mainContainer, "Section5", Tr("5. NEW TAB BUTTON - STYLE B (VS Code-Style Icon Button)"));
        card5->layout.SetFlexGap(14);

        auto newTabBGroup = addExample(card5, "NewTabBLabel", Tr("Click the '+' icon button to create new tabs (Code editor behavior)"));

        auto newTabBTabs = std::make_shared<UltraCanvasTabbedContainer>("NewTabBTabs", 0, 0, 800, 220);
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
        newTabBTabs->onNewTabRequest = [newTabBTabs = newTabBTabs.get()]() {
            static int tabCounter = 1;

            std::ostringstream titleStream;
            titleStream << "File " << tabCounter << ".txt";

            const std::string contentText = TrF("Code Editor Tab #{0}\n\n"
                                                "This mimics VS Code tab behavior.\n"
                                                "Compact icon button for creating tabs.\n"
                                                "Minimal UI, maximum workspace.", tabCounter);

            auto content = CreateSampleTabContent(
                    "CodeTab" + std::to_string(tabCounter),
                    
                    contentText,
                    Color(248, 248, 248)
            );

            int newIndex = newTabBTabs->AddTab(titleStream.str(), content);
            newTabBTabs->SetActiveTab(newIndex);

            tabCounter++;
        };

        // Add initial tabs
        newTabBTabs->AddTab("main.cpp", CreateSampleTabContent("NewTabB1",
                                                               Tr("Style B - Icon Button Features:\n\n"
                                                               "• Simple icon button (24x24 default)\n"
                                                               "• Separate from tab visual style\n"
                                                               "• More compact and minimal\n"
                                                               "• Centered in tab bar\n"
                                                               "• Used in VS Code, Sublime Text, code editors"),
                                                               Color(248, 248, 248)));

        newTabBTabs->AddTab("utils.h", CreateSampleTabContent("NewTabB2",
                                                              Tr("Click the small '+' button to add files.\n\n"
                                                              "Perfect for code editor interfaces."),
                                                              Color(250, 250, 250)));

        newTabBTabs->SetActiveTab(0);
        newTabBTabs->SetCloseMode(TabCloseMode::Closable);
        DemoKit::AddStretched(newTabBGroup, newTabBTabs);

        // ========================================
        // SECTION 6: TAB POSITIONS (Top, Bottom, Left, Right)
        // ========================================
        auto card6 = DemoKit::AddCard(mainContainer, "Section6", Tr("6. TAB POSITIONS (Top, Bottom, Left, Right)"));
        card6->layout.SetFlexGap(14);

        // Bottom and left side by side (under each other when narrow), right below
        auto positionContainer = DemoKit::CreateColumn("PositionContainer", 12);
        DemoKit::Add(card6, positionContainer);
        auto positionRow = DemoKit::CreateRow("PositionRow", 30);
        DemoKit::Add(positionContainer, positionRow);

        // Bottom Position Tabs
        auto bottomTabs = std::make_shared<UltraCanvasTabbedContainer>("BottomTabs", 0, 0, 370, 190);
        bottomTabs->SetTabPosition(TabPosition::Bottom);
        bottomTabs->SetTabStyle(TabStyle::Classic);
        bottomTabs->AddTab(Tr("Bottom 1"), CreateSampleTabContent("Bottom1",
                                                              Tr("Tabs at bottom\n(Terminal/Console style)"), Color(250, 250, 250), 340, 150));
        bottomTabs->AddTab(Tr("Bottom 2"), CreateSampleTabContent("Bottom2",
                                                              Tr("Useful for tool windows"), Color(245, 250, 255), 340, 150));
        bottomTabs->SetActiveTab(0);
        DemoKit::AddFixed(positionRow, bottomTabs);

        // Left Position Tabs
        auto leftTabs = std::make_shared<UltraCanvasTabbedContainer>("LeftTabs", 0, 0, 370, 190);
        leftTabs->SetTabPosition(TabPosition::Left);
        leftTabs->SetTabStyle(TabStyle::Flat);
        leftTabs->AddTab(Tr("Left 1"), CreateSampleTabContent("Left1",
                                                          Tr("Vertical tabs on left side"), Color(250, 250, 250), 240, 150));
        leftTabs->AddTab(Tr("Left Panel 2"), CreateSampleTabContent("Left2",
                                                          Tr("Sidebar navigation style"), Color(245, 250, 255), 240, 150));
        leftTabs->SetActiveTab(0);
        DemoKit::AddFixed(positionRow, leftTabs);

        // Right Position Tabs
        auto rightTabs = std::make_shared<UltraCanvasTabbedContainer>("RightTabs", 0, 0, 780, 200);
        rightTabs->SetTabPosition(TabPosition::Right);
        rightTabs->SetTabStyle(TabStyle::Modern);
        rightTabs->AddTab(Tr("Right 1"), CreateSampleTabContent("Right1",
                                                                  Tr("Tabs on right side - useful for property panels and inspector windows"), Color(250, 250, 250)));
        rightTabs->AddTab(Tr("Right Panel 2"), CreateSampleTabContent("Right2",
                                                                  Tr("Common in design tools and IDEs for tool palettes"), Color(245, 250, 255)));
        rightTabs->SetActiveTab(0);
        DemoKit::AddStretched(positionContainer, rightTabs);

        // ========================================
        // SECTION 7: ADVANCED FEATURES
        // ========================================
        /*
        auto card7 = DemoKit::AddCard(mainContainer, "Section7", Tr("7. ADVANCED FEATURES (Drag & Drop, Callbacks, Custom Colors)"));
        card7->layout.SetFlexGap(14);

        auto advancedGroup = addExample(card7, "AdvancedLabel", Tr("Custom styled tabs with per-tab colors and event callbacks"));

        auto advancedTabs = std::make_shared<UltraCanvasTabbedContainer>("AdvancedTabs", 0, 0, 800, 220);
        advancedTabs->SetTabStyle(TabStyle::Rounded);
        advancedTabs->SetTabHeight(32);
        advancedTabs->SetShowTabSeparators(true);

        // Add tabs with custom colors
        int redTabIndex = advancedTabs->AddTab(Tr("Error Log"), CreateSampleTabContent("Advanced1",
                                                                                   Tr("Custom Tab Features:\n\n"
                                                                                   "• Per-tab background colors\n"
                                                                                   "• Per-tab text colors\n"
                                                                                   "• Tab separators (vertical lines)\n"
                                                                                   "• Event callbacks (onTabChange, onTabClose)\n"
                                                                                   "• Drag & drop reordering (try it!)"),
                                                                                   Color(255, 240, 240)));
        advancedTabs->SetTabBackgroundColor(redTabIndex, Color(255, 200, 200));
        advancedTabs->SetTabTextColor(redTabIndex, Color(150, 0, 0));

        int greenTabIndex = advancedTabs->AddTab(Tr("Success"), CreateSampleTabContent("Advanced2",
                                                                                   Tr("This tab has custom green coloring.\n\n"
                                                                                   "Perfect for status indicators!"),
                                                                                   Color(240, 255, 240)));
        advancedTabs->SetTabBackgroundColor(greenTabIndex, Color(200, 255, 200));
        advancedTabs->SetTabTextColor(greenTabIndex, Color(0, 120, 0));

        int blueTabIndex = advancedTabs->AddTab(Tr("Information"), CreateSampleTabContent("Advanced3",
                                                                                      Tr("Blue themed tab.\n\n"
                                                                                      "Each tab can have unique styling."),
                                                                                      Color(240, 240, 255)));
        advancedTabs->SetTabBackgroundColor(blueTabIndex, Color(200, 220, 255));
        advancedTabs->SetTabTextColor(blueTabIndex, Color(0, 60, 150));
        advancedTabs->SetCloseMode(TabCloseMode::Closable);

        // Set up callbacks
        advancedTabs->onTabChange = [](int oldIndex, int newIndex) {
            debugOutput << "Tab changed from " << oldIndex << " to " << newIndex << std::endl;
        };

        advancedTabs->SetActiveTab(0);
        DemoKit::AddStretched(advancedGroup, advancedTabs);
        */
        // ========================================
        // FOOTER
        // ========================================
        auto footer = DemoKit::AddCaption(mainContainer, "Footer",
                Tr("UltraCanvas Tabbed Container Demo\n"
                "All features demonstrated above are production-ready and fully functional."));
        footer->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);

        return mainContainer;
    }

} // namespace UltraCanvas