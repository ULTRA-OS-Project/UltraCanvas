// Apps/DemoApp/UltraCanvasButtonExamples.cpp
// Comprehensive button component demonstration
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  section, buttons in wrapping rows, the status line sized to its text
// Version: 2.0.2 - the split buttons' arrows and the File menu: icons instead of glyphs
// Version: 2.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iostream>
#include <memory>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateButtonExamples() {
            auto page = DemoKit::CreatePage("ButtonExamples", "UltraCanvas Button Component Showcase",
                    "Demonstrating all button styles, states, and the new split button feature");

            // Status label for button feedback. At least two lines tall, as
            // most messages are, so a click does not move the buttons below.
            auto statusLabel = DemoKit::AddText(page, "StatusLabel", "Click any button to see feedback here");
            statusLabel->SetFontSize(11);
            statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
            statusLabel->SetBorders(1.0f);
            statusLabel->SetPadding(8.0f);
            statusLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            CSSLayout::BoxConstraints statusLimits;
            statusLimits.minHeight = CSSLayout::Dimension::Px(48);
            statusLabel->boxConstraints = statusLimits;

            // ========================================
            // SECTION 1: BASIC BUTTON STYLES
            // ========================================
            auto section1 = DemoKit::AddCard(page, "Section1", "Basic Button Styles");
            auto row1 = DemoKit::AddRow(section1, "Section1Row", 10);

            // Standard Button
            auto standardBtn = CreateButton("StandardButton", 0, 0, 120, 35, "Standard");
            standardBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Standard button clicked\nDefault style with hover and press effects");
            };
            DemoKit::AddFixed(row1, standardBtn);

            // Primary Style Button
            auto primaryBtn = CreateButton("PrimaryButton", 0, 0, 120, 35, "Primary");
            primaryBtn->SetStyle(ButtonStyles::PrimaryStyle());
            primaryBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Primary button clicked\nUsing PrimaryStyle() - blue theme");
            };
            DemoKit::AddFixed(row1, primaryBtn);

            // Secondary Style Button
            auto secondaryBtn = CreateButton("SecondaryButton", 0, 0, 120, 35, "Secondary");
            secondaryBtn->SetStyle(ButtonStyles::SecondaryStyle());
            secondaryBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Secondary button clicked\nUsing SecondaryStyle() - bordered");
            };
            DemoKit::AddFixed(row1, secondaryBtn);

            // Danger Style Button
            auto dangerBtn = CreateButton("DangerButton", 0, 0, 120, 35, "Danger");
            dangerBtn->SetStyle(ButtonStyles::DangerStyle());
            dangerBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Danger button clicked\nUsing DangerStyle() - red theme");
            };
            DemoKit::AddFixed(row1, dangerBtn);

            // Success Style Button
            auto successBtn = CreateButton("SuccessButton", 0, 0, 120, 35, "Success");
            successBtn->SetStyle(ButtonStyles::SuccessStyle());
            successBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Success button clicked\nUsing SuccessStyle() - green theme");
            };
            DemoKit::AddFixed(row1, successBtn);

            // ========================================
            // SECTION 2: BUTTON STATES
            // ========================================
            auto section2 = DemoKit::AddCard(page, "Section2", "Button States");
            auto row2 = DemoKit::AddRow(section2, "Section2Row", 10);

            // Normal State
            auto normalBtn = CreateButton("NormalBtn", 0, 0, 120, 35, "Normal");
            normalBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Normal state button\nEnabled and ready for interaction");
            };
            DemoKit::AddFixed(row2, normalBtn);

            // Disabled State
            auto disabledBtn = CreateButton("DisabledBtn", 0, 0, 120, 35, "Disabled");
            disabledBtn->SetDisabled(true);
            DemoKit::AddFixed(row2, disabledBtn);

            // Focused State (simulated)
            auto focusedBtn = CreateButton("FocusedBtn", 0, 0, 120, 35, "Focused");
            focusedBtn->SetFocus(true);
            focusedBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Focused button clicked\nShows focus ring when selected");
            };
            DemoKit::AddFixed(row2, focusedBtn);

            // ========================================
            // SECTION 3: ICONS & TEXT
            // ========================================
            auto section3 = DemoKit::AddCard(page, "Section3", "Icons & Text");
            auto row3 = DemoKit::AddRow(section3, "Section3Row", 10);

            // Text Only
            auto textOnlyBtn = CreateButton("TextOnly", 0, 0, 100, 35, "Text Only");
            textOnlyBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Text-only button clicked");
            };
            DemoKit::AddFixed(row3, textOnlyBtn);

            // Icon Left
            auto iconLeftBtn = CreateButton("IconLeft", 0, 0, 120, 35, "Save");
            iconLeftBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/save.png"));
            iconLeftBtn->SetIconPosition(ButtonIconPosition::Left);
            iconLeftBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Save button clicked\nIcon positioned on the left");
            };
            DemoKit::AddFixed(row3, iconLeftBtn);

            // Icon Right
            auto iconRightBtn = CreateButton("IconRight", 0, 0, 120, 35, "Next");
            iconRightBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/arrow-chevron-right.png"));
            iconRightBtn->SetIconPosition(ButtonIconPosition::Right);
            iconRightBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Next button clicked\nIcon positioned on the right");
            };
            DemoKit::AddFixed(row3, iconRightBtn);

            // Icon Only
//            auto iconOnlyBtn = CreateButton("IconOnly", 0, 0, 40, 35, "");
//            iconOnlyBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/settings.png"));
//            iconOnlyBtn->SetStyle(ButtonStyles::IconOnlyStyle());
////            iconOnlyBtn->SetTooltip("Settings");
//            iconOnlyBtn->onClick = [statusLabel]() {
//                statusLabel->SetText("Settings button clicked\nIcon-only button with tooltip");
//            };
//            DemoKit::AddFixed(row3, iconOnlyBtn);

            // Icon Top
            auto iconTopBtn = CreateButton("IconTop", 0, 0, 80, 62, "Upload");
            iconTopBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/upload.png"));
            iconTopBtn->SetIconPosition(ButtonIconPosition::Top);
            iconTopBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Upload button clicked\nIcon positioned above text");
            };
            DemoKit::AddFixed(row3, iconTopBtn);

            // Icon Bottom
            auto iconBottomBtn = CreateButton("IconBottom", 0, 0, 100, 62, "Download");
            iconBottomBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/download_34px.png"));
            iconBottomBtn->SetIconPosition(ButtonIconPosition::Bottom);
            iconBottomBtn->SetIconSize(41,34);
            iconBottomBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Download button clicked\nIcon positioned below text");
            };
            DemoKit::AddFixed(row3, iconBottomBtn);

            auto iconRightBtn2 = CreateButton("IconRight", 0, 0, 260, 35, "Continue with UltraCanvas");
            iconRightBtn2->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/ultracanvas_20px.png"));
            iconRightBtn2->SetIconSize(20, 20);
            iconRightBtn2->SetIconPosition(ButtonIconPosition::Left);
            iconRightBtn2->SetColors(Colors::White, Color(240, 240, 240, 255), Colors::Gray, Colors::LightGray);
            iconRightBtn2->onClick = [statusLabel]() {
                statusLabel->SetText("Continue with UltraCanvas button clicked\nIcon positioned on the right");
            };
            DemoKit::AddFixed(row3, iconRightBtn2);

            // ========================================
            // SECTION 4: SPLIT BUTTONS (NEW FEATURE)
            // ========================================
            auto section4 = DemoKit::AddCard(page, "Section4", "Split Buttons - New Feature!");
            auto row4 = DemoKit::AddRow(section4, "Section4Row", 10);

            // Basic Split Button
            auto splitBtn = CreateButton("SplitButton", 0, 0, 130, 35, "Action");
            splitBtn->SetSplitEnabled(true);
            // The arrow is the framework's dropdown icon, not a "▼" glyph,
            // which the bundled UI font does not have.
            splitBtn->SetSplitSecondaryIcon(NormalizePath(GetResourcesDir() + "media/icons/dropdown.svg"));
            splitBtn->SetSplitSecondaryIconSize(12, 8);
            splitBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Split button primary clicked\nMain action executed");
            };
            splitBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Split button secondary clicked\nShow dropdown menu here");
            };
            DemoKit::AddFixed(row4, splitBtn);

            // Badge Style (Sponsors)
            auto sponsorBtn = CreateButton("SponsorButton", 0, 0, 140, 35, "Sponsors");
            sponsorBtn->SetStyle(ButtonStyles::BadgeButtonStyle());
            sponsorBtn->SetSplitSecondaryText("31");
            sponsorBtn->SetSplitColors(
                    Color(52, 152, 219, 255), Colors::White, Colors::LightGray, Colors::Gray
            );
            sponsorBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Sponsors button clicked\nNavigate to sponsors page");
            };
            sponsorBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Sponsor count clicked\nShow sponsor list (31 sponsors)");
            };
            DemoKit::AddFixed(row4, sponsorBtn);

            // Badge Style (Patreon)
            auto patreonBtn = CreateButton("PatreonButton", 0, 0, 140, 35, "Patreon");
            patreonBtn->SetSplitEnabled(true);
            patreonBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/patreon.png"));
            patreonBtn->SetIconPosition(ButtonIconPosition::Left);
            patreonBtn->SetIconSize(20,20);
            patreonBtn->SetIconSpacing(7);
            patreonBtn->SetSplitSecondaryText("122");
            patreonBtn->SetSplitColors(
                    Color(249, 104, 84, 255), Colors::White, Colors::LightGray, Colors::Gray
            );
            patreonBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Patreon button clicked\nOpen Patreon page");
            };
            patreonBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Patron count clicked\nShow patron list (122 patrons)");
            };
            DemoKit::AddFixed(row4, patreonBtn);

            // Badge Style (Liberapay)
            auto liberapayBtn = CreateButton("LiberapayButton", 0, 0, 140, 35, "Liberapay");
            liberapayBtn->SetSplitEnabled(true);
            liberapayBtn->SetSplitSecondaryText("5");
            liberapayBtn->SetSplitColors(
                    Color(255, 193, 7, 255), Colors::Black, Colors::LightGray, Colors::Gray
            );
            liberapayBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Liberapay button clicked\nOpen Liberapay page");
            };
            liberapayBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Liberapay count clicked\nShow supporters (5)");
            };
            DemoKit::AddFixed(row4, liberapayBtn);

            // PayPal Style
            auto paypalBtn = CreateButton("PaypalButton", 0, 0, 140, 35, "Paypal");
            paypalBtn->SetSplitEnabled(true);
            paypalBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/paypal.png"));
            paypalBtn->SetIconPosition(ButtonIconPosition::Left);
            paypalBtn->SetIconSize(20,20);
            paypalBtn->SetIconSpacing(7);
            paypalBtn->SetSplitSecondaryText("297");
            paypalBtn->SetSplitColors(
                    Color(0, 112, 186, 255), Colors::White, Colors::LightGray, Colors::Gray
            );
            paypalBtn->onClick = [statusLabel]() {
                statusLabel->SetText("PayPal button clicked\nOpen PayPal donation page");
            };
            paypalBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("PayPal count clicked\nShow donors (297)");
            };
            DemoKit::AddFixed(row4, paypalBtn);

            // Counter Style Split Buttons, with the vertical split button on
            // a line of their own
            auto row4b = DemoKit::AddRow(section4, "Section4Row2", 10);

            auto counterBtn = CreateButton("CounterButton", 0, 0, 130, 35, "Likes");
            counterBtn->SetStyle(ButtonStyles::CounterButtonStyle());
            counterBtn->SetSplitSecondaryText("1.2k");
            counterBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Like button clicked\nAdded your like!");
            };
            counterBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Like count clicked\nShow who liked (1,200 users)");
            };
            DemoKit::AddFixed(row4b, counterBtn);

        auto vertSplitMenu = std::make_shared<UltraCanvasMenu>("vertSplitMenu", 0, 0, 200, 0);
        vertSplitMenu->SetMenuType(MenuType::PopupMenu);

        // Flat icons of media/icons/menu/, not emoji in the labels: an emoji
        // draws only where a colour emoji font is installed.
        auto menuIcon = [](const char* name) {
            return NormalizePath(GetResourcesDir() + "media/icons/menu/" + name + ".svg");
        };
        vertSplitMenu->AddItem(MenuItemData::Action("New file", menuIcon("new"), []() {
        }));

        vertSplitMenu->AddItem(MenuItemData::ActionWithShortcut("Open...", "Ctrl+O", menuIcon("folder"), []() {
              debugOutput << "Open file" << std::endl;
        }));

        vertSplitMenu->AddItem(MenuItemData::ActionWithShortcut("Save", "Ctrl+S", menuIcon("save"), []() {
              debugOutput << "Save file" << std::endl;
        }));
        vertSplitMenu->AddItem(MenuItemData::ActionWithShortcut("Save As...", "Ctrl+Shift+S", menuIcon("save"), []() {
              debugOutput << "Save as" << std::endl;
          }));
        vertSplitMenu->AddItem(MenuItemData::ActionWithShortcut("Exit", "Alt+F4", menuIcon("exit"), []() {
            debugOutput << "Exit application" << std::endl;
        }));

        // Vertical Split Button
            auto vertSplitBtn = CreateButton("VertSplitButton", 0, 0, 100, 50, "File");
            vertSplitBtn->SetSplitEnabled(true);
            vertSplitBtn->SetSplitHorizontal(false); // Vertical
            vertSplitBtn->SetSplitRatio(0.6);
            // The lower part is the dropdown arrow alone: the icon, not a "▼"
            // glyph the bundled UI font does not have.
            vertSplitBtn->SetSplitSecondaryIcon(NormalizePath(GetResourcesDir() + "media/icons/dropdown.svg"));
            vertSplitBtn->SetSplitSecondaryIconSize(12, 8);
            vertSplitBtn->onClick = [statusLabel]() {
                statusLabel->SetText("File action clicked\nOpen file dialog");
            };
            vertSplitBtn->onSecondaryClick = [page = page.get(), vertSplitMenu]() {
                auto ev = UltraCanvasApplication::GetInstance()->GetCurrentEvent();
                page->GetWindow()->AddChild(vertSplitMenu);
                vertSplitMenu->OpenMenu(Point2Di(ev.pointerWindow.x, ev.pointerWindow.y), *page->GetWindow(), PopupElementSettings());
            };
            DemoKit::AddFixed(row4b, vertSplitBtn);

            // ========================================
            // SECTION 5: CUSTOM STYLING
            // ========================================
            auto section5 = DemoKit::AddCard(page, "Section5", "Custom Styling");
            auto row5 = DemoKit::AddRow(section5, "Section5Row", 10);

            // Flat Button
            auto flatBtn = CreateButton("FlatButton", 0, 0, 100, 35, "Flat");
            flatBtn->SetStyle(ButtonStyles::FlatStyle());
            flatBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Flat button clicked\nNo background, minimal style");
            };
            DemoKit::AddFixed(row5, flatBtn);

            // Rounded Button
            auto roundedBtn = CreateButton("RoundedButton", 0, 0, 100, 35, "Rounded");
            roundedBtn->SetCornerRadius(18.0f);
            roundedBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Rounded button clicked\nCustom corner radius applied");
            };
            DemoKit::AddFixed(row5, roundedBtn);

            // Custom Colors
            auto customColorBtn = CreateButton("CustomColor", 0, 0, 120, 35, "Custom");
            customColorBtn->SetColors(
                    Color(255, 215, 0, 255),   // Gold normal
                    Color(255, 200, 0, 255),   // Gold hover
                    Color(218, 165, 32, 255),  // Darker gold pressed
                    Color(200, 200, 200, 255)  // Gray disabled
            );
            customColorBtn->SetTextColors(
                    Color(50, 50, 50, 255),    // Dark text
                    Color(0, 0, 0, 255),       // Black on hover
                    Color(0, 0, 0, 255),       // Black pressed
                    Color(150, 150, 150, 255)  // Gray disabled
            );
            customColorBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Custom colored button clicked\nGold theme with dark text");
            };
            DemoKit::AddFixed(row5, customColorBtn);

            // Shadow Button
            auto shadowBtn = CreateButton("ShadowButton", 0, 0, 120, 38, "Shadow");
            shadowBtn->SetShadow(true, Color(0, 0, 0, 128), Point2Di(3, 3));
            shadowBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Shadow button clicked\nCustom shadow effect applied");
            };
            DemoKit::AddFixed(row5, shadowBtn);

            // Large Button
            auto largeBtn = CreateButton("LargeButton", 0, 0, 150, 50, "Large");
            largeBtn->SetFont("Sans", 16, FontWeight::Bold);
            largeBtn->SetPadding(10, 20, 10, 20);
            largeBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Large button clicked\nCustom size and padding");
            };
            DemoKit::AddFixed(row5, largeBtn);

            // ========================================
            // SECTION 6: INTERACTIVE EXAMPLES
            // ========================================
            auto section6 = DemoKit::AddCard(page, "Section6", "Interactive Examples");
            auto row6 = DemoKit::AddRow(section6, "Section6Row", 10);

            // Toggle Example
            auto toggleBtn = CreateButton("ToggleButton", 0, 0, 120, 35, "Toggle: OFF");
            auto toggleState = std::make_shared<bool>(false);
            toggleBtn->onClick = [toggleBtn = toggleBtn.get(), toggleState, statusLabel]() {
                *toggleState = !*toggleState;
                toggleBtn->SetText(*toggleState ? "Toggle: ON" : "Toggle: OFF");
                toggleBtn->SetColors(
                        *toggleState ? Color(100, 200, 100, 255) : Colors::ButtonFace,
                        *toggleState ? Color(80, 180, 80, 255) : Colors::SelectionHover,
                        *toggleState ? Color(60, 160, 60, 255) : Color(204, 228, 247, 255),
                        Colors::LightGray
                );
                statusLabel->SetText(*toggleState ?
                                     "Toggle switched ON\nState persists until clicked again" :
                                     "Toggle switched OFF\nClick to enable");
            };
            DemoKit::AddFixed(row6, toggleBtn);

            // Counter Example
            auto counterExampleBtn = CreateButton("CounterExample", 0, 0, 140, 35, "Clicks: 0");
            auto clickCount = std::make_shared<int>(0);
            counterExampleBtn->SetSplitEnabled(true);
            counterExampleBtn->SetSplitRatio(0);
            counterExampleBtn->SetSplitSecondaryText("Reset");
            counterExampleBtn->onClick = [counterExampleBtn = counterExampleBtn.get(), clickCount, statusLabel]() {
                (*clickCount)++;
                std::ostringstream text;
                text << "Clicks: " << *clickCount;
                counterExampleBtn->SetText(text.str());

                std::ostringstream status;
                status << "Button clicked " << *clickCount << " times\nSecondary button resets counter";
                statusLabel->SetText(status.str());
            };
            counterExampleBtn->onSecondaryClick = [counterExampleBtn = counterExampleBtn.get(), clickCount, statusLabel]() {
                *clickCount = 0;
                counterExampleBtn->SetText("Clicks: 0");
                statusLabel->SetText("Counter reset to 0");
            };
            DemoKit::AddFixed(row6, counterExampleBtn);

            // Multi-Action Button
            auto multiBtn = CreateButton("MultiAction", 0, 0, 170, 35, "Save");
            multiBtn->SetSplitEnabled(true);
            multiBtn->SetSplitRatio(0);
//            multiBtn->SetSplitSecondaryText("");
            multiBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/save.png"));
            multiBtn->SetSplitSecondaryIcon(NormalizePath(GetResourcesDir() + "media/icons/settings.png"));
            multiBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Quick save executed\nFile saved with default settings");
            };
            multiBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Save options clicked\nWould show: Save As, Save Copy, Export...");
            };
            DemoKit::AddFixed(row6, multiBtn);

            // ========================================
            // SECTION 7: BUTTON BUILDER EXAMPLE
            // ========================================
            auto section7 = DemoKit::AddCard(page, "Section7", "Button Builder Pattern");
            auto row7 = DemoKit::AddRow(section7, "Section7Row", 10);

            // Create button using builder pattern
            auto builderBtn = ButtonBuilder("BuilderButton")
                    .SetStyle(ButtonStyles::PrimaryStyle())
                    .SetSize(240, 40)
                    .SetText("Built with Builder")
                    .SetIcon(NormalizePath(GetResourcesDir() + "media/icons/build.png"))
                    .SetIconSpacing(6)
                    .SetSplitEnabled(true)
                    .SetSplitSecondaryIcon(NormalizePath(GetResourcesDir() + "media/icons/arrow-chevron-right.png"))
                    .SetSplitIconSize(24,24)
//                    .SetSplitSecondaryText("→")
                    .SetSplitRatio(0.8)
                    .SetCornerRadius(8.0f)
                    .SetTooltip("This button was created using the Builder pattern")
                    .OnClick([statusLabel]() {
                        statusLabel->SetText("Builder pattern button clicked\nCreated with fluent interface");
                    })
                    .OnSecondaryClick([statusLabel]() {
                        statusLabel->SetText("Builder button arrow clicked\nWould navigate forward");
                    })
                    .Build();
            DemoKit::AddFixed(row7, builderBtn);

            // Another builder example - complex configuration
            auto complexBuilderBtn = ButtonBuilder("ComplexBuilder")
                    .SetSize(180, 40)
                    .SetText("Complex")
                    .SetSplitEnabled(true)
                    .SetSplitSecondaryText("99+")
                    .SetSplitRatio(0)
                    .SetSplitColors(
                            Color(75, 0, 130, 255), Colors::White,      // Indigo primary
                            Color(255, 69, 0, 255), Colors::White       // OrangeRed secondary
                    )
                    .SetFont("Sans", 14, FontWeight::Bold)
                    .SetShadow(true)
                    .OnClick([statusLabel]() {
                        statusLabel->SetText("Complex builder button clicked\nPrimary action with custom styling");
                    })
                    .OnSecondaryClick([statusLabel]() {
                        statusLabel->SetText("Badge clicked\n99+ notifications");
                    })
                    .Build();
            DemoKit::AddFixed(row7, complexBuilderBtn);

            return page;
    }

} // namespace UltraCanvas