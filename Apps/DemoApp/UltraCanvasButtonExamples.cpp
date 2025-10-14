// Apps/DemoApp/UltraCanvasButtonDemo.cpp
// Comprehensive button component demonstration
// Version: 2.0.0
// Last Modified: 2025-01-11
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iostream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateButtonExamples() {
            auto mainContainer = std::make_shared<UltraCanvasContainer>("ButtonExamples", 100, 0, 0, 1000, 800);

            // ===== PAGE TITLE =====
            auto title = std::make_shared<UltraCanvasLabel>("ButtonTitle", 101, 20, 10, 500, 35);
            title->SetText("UltraCanvas Button Component Showcase");
            title->SetFontSize(18);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(50, 50, 150, 255));
            mainContainer->AddChild(title);

            auto subtitle = std::make_shared<UltraCanvasLabel>("ButtonSubtitle", 102, 20, 45, 800, 25);
            subtitle->SetText("Demonstrating all button styles, states, and the new split button feature");
            subtitle->SetFontSize(12);
            subtitle->SetTextColor(Color(100, 100, 100, 255));
            mainContainer->AddChild(subtitle);

            // Status label for button feedback
            auto statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 103, 600, 10, 380, 60);
            statusLabel->SetText("Click any button to see feedback here");
            statusLabel->SetFontSize(11);
            statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
            statusLabel->SetBorderWidth(1.0f);
            statusLabel->SetPadding(8.0f);
            mainContainer->AddChild(statusLabel);

            int yOffset = 90;

            // ========================================
            // SECTION 1: BASIC BUTTON STYLES
            // ========================================
            auto section1Label = std::make_shared<UltraCanvasLabel>("Section1", 110, 20, yOffset, 300, 25);
            section1Label->SetText("Basic Button Styles");
            section1Label->SetFontWeight(FontWeight::Bold);
            section1Label->SetTextColor(Color(0, 100, 200, 255));
            mainContainer->AddChild(section1Label);
            yOffset += 35;

            // Standard Button
            auto standardBtn = CreateButton("StandardButton", 111, 20, yOffset, 120, 35, "Standard");
            standardBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Standard button clicked\nDefault style with hover and press effects");
            };
            mainContainer->AddChild(standardBtn);

            // Primary Style Button
            auto primaryBtn = CreateButton("PrimaryButton", 112, 150, yOffset, 120, 35, "Primary");
            primaryBtn->SetStyle(ButtonStyles::PrimaryStyle());
            primaryBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Primary button clicked\nUsing PrimaryStyle() - blue theme");
            };
            mainContainer->AddChild(primaryBtn);

            // Secondary Style Button
            auto secondaryBtn = CreateButton("SecondaryButton", 113, 280, yOffset, 120, 35, "Secondary");
            secondaryBtn->SetStyle(ButtonStyles::SecondaryStyle());
            secondaryBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Secondary button clicked\nUsing SecondaryStyle() - bordered");
            };
            mainContainer->AddChild(secondaryBtn);

            // Danger Style Button
            auto dangerBtn = CreateButton("DangerButton", 114, 410, yOffset, 120, 35, "Danger");
            dangerBtn->SetStyle(ButtonStyles::DangerStyle());
            dangerBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Danger button clicked\nUsing DangerStyle() - red theme");
            };
            mainContainer->AddChild(dangerBtn);

            // Success Style Button
            auto successBtn = CreateButton("SuccessButton", 115, 540, yOffset, 120, 35, "Success");
            successBtn->SetStyle(ButtonStyles::SuccessStyle());
            successBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Success button clicked\nUsing SuccessStyle() - green theme");
            };
            mainContainer->AddChild(successBtn);

            yOffset += 50;

            // ========================================
            // SECTION 2: BUTTON STATES
            // ========================================
            auto section2Label = std::make_shared<UltraCanvasLabel>("Section2", 120, 20, yOffset, 300, 25);
            section2Label->SetText("Button States");
            section2Label->SetFontWeight(FontWeight::Bold);
            section2Label->SetTextColor(Color(0, 100, 200, 255));
            mainContainer->AddChild(section2Label);
            yOffset += 35;

            // Normal State
            auto normalBtn = CreateButton("NormalBtn", 121, 20, yOffset, 120, 35, "Normal");
            normalBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Normal state button\nEnabled and ready for interaction");
            };
            mainContainer->AddChild(normalBtn);

            // Disabled State
            auto disabledBtn = CreateButton("DisabledBtn", 122, 150, yOffset, 120, 35, "Disabled");
            disabledBtn->SetEnabled(false);
            mainContainer->AddChild(disabledBtn);

            // Focused State (simulated)
            auto focusedBtn = CreateButton("FocusedBtn", 123, 280, yOffset, 120, 35, "Focused");
            focusedBtn->SetFocus();
            focusedBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Focused button clicked\nShows focus ring when selected");
            };
            mainContainer->AddChild(focusedBtn);

            yOffset += 50;

            // ========================================
            // SECTION 3: ICONS & TEXT
            // ========================================
            auto section3Label = std::make_shared<UltraCanvasLabel>("Section3", 130, 20, yOffset, 300, 25);
            section3Label->SetText("Icons & Text");
            section3Label->SetFontWeight(FontWeight::Bold);
            section3Label->SetTextColor(Color(0, 100, 200, 255));
            mainContainer->AddChild(section3Label);
            yOffset += 35;

            // Text Only
            auto textOnlyBtn = CreateButton("TextOnly", 131, 20, yOffset, 100, 35, "Text Only");
            textOnlyBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Text-only button clicked");
            };
            mainContainer->AddChild(textOnlyBtn);

            // Icon Left
            auto iconLeftBtn = CreateButton("IconLeft", 132, 130, yOffset, 120, 35, "Save");
            iconLeftBtn->SetIcon("assets/icons/save.png");
            iconLeftBtn->SetIconPosition(IconPosition::Left);
            iconLeftBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Save button clicked\nIcon positioned on the left");
            };
            mainContainer->AddChild(iconLeftBtn);

            // Icon Right
            auto iconRightBtn = CreateButton("IconRight", 133, 260, yOffset, 120, 35, "Next");
            iconRightBtn->SetIcon("assets/icons/arrow-right.png");
            iconRightBtn->SetIconPosition(IconPosition::Right);
            iconRightBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Next button clicked\nIcon positioned on the right");
            };
            mainContainer->AddChild(iconRightBtn);

            // Icon Only
            auto iconOnlyBtn = CreateButton("IconOnly", 134, 390, yOffset, 40, 35, "");
            iconOnlyBtn->SetIcon("assets/icons/settings.png");
            iconOnlyBtn->SetStyle(ButtonStyles::IconOnlyStyle());
            iconOnlyBtn->SetTooltip("Settings");
            iconOnlyBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Settings button clicked\nIcon-only button with tooltip");
            };
            mainContainer->AddChild(iconOnlyBtn);

            // Icon Top
            auto iconTopBtn = CreateButton("IconTop", 135, 440, yOffset, 80, 62, "Upload");
            iconTopBtn->SetIcon("assets/icons/upload.png");
            iconTopBtn->SetIconPosition(IconPosition::Top);
            iconTopBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Upload button clicked\nIcon positioned above text");
            };
            mainContainer->AddChild(iconTopBtn);

            // Icon Bottom
            auto iconBottomBtn = CreateButton("IconBottom", 136, 530, yOffset, 80, 62, "Download");
            iconBottomBtn->SetIcon("assets/icons/download.png");
            iconBottomBtn->SetIconPosition(IconPosition::Bottom);
            iconBottomBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Download button clicked\nIcon positioned below text");
            };
            mainContainer->AddChild(iconBottomBtn);

            auto iconRightBtn2 = CreateButton("IconRight", 133, 630, yOffset, 250, 35, "Continue with UltraCanvas");
            iconRightBtn2->SetIcon("assets/images/UltraCanvas-logo.png");
            iconRightBtn2->SetIconPosition(IconPosition::Left);
            iconRightBtn2->SetColors(Colors::White, Color(240, 240, 240, 255), Colors::Gray, Colors::LightGray);
            iconRightBtn2->onClick = [statusLabel]() {
                statusLabel->SetText("Continue with UltraCanvas button clicked\nIcon positioned on the right");
            };
            mainContainer->AddChild(iconRightBtn2);

            yOffset += 65;

            // ========================================
            // SECTION 4: SPLIT BUTTONS (NEW FEATURE)
            // ========================================
            auto section4Label = std::make_shared<UltraCanvasLabel>("Section4", 140, 20, yOffset, 400, 25);
            section4Label->SetText("Split Buttons - New Feature!");
            section4Label->SetFontWeight(FontWeight::Bold);
            section4Label->SetTextColor(Color(200, 50, 50, 255));
            mainContainer->AddChild(section4Label);
            yOffset += 35;

            // Basic Split Button
            auto splitBtn = CreateButton("SplitButton", 141, 20, yOffset, 130, 35, "Action");
            splitBtn->EnableSplitButton(true);
            splitBtn->SetSplitButtonSecondaryText("▼");
            splitBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Split button primary clicked\nMain action executed");
            };
            splitBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Split button secondary clicked\nShow dropdown menu here");
            };
            mainContainer->AddChild(splitBtn);

            // Badge Style (Sponsors)
            auto sponsorBtn = CreateButton("SponsorButton", 142, 160, yOffset, 140, 35, "sponsors");
            sponsorBtn->SetStyle(ButtonStyles::BadgeButtonStyle());
            sponsorBtn->SetSplitButtonSecondaryText("31");
            sponsorBtn->SetSplitButtonColors(
                    Colors::ButtonFace, Colors::TextDefault,
                    Color(52, 152, 219, 255), Colors::White
            );
            sponsorBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Sponsors button clicked\nNavigate to sponsors page");
            };
            sponsorBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Sponsor count clicked\nShow sponsor list (31 sponsors)");
            };
            mainContainer->AddChild(sponsorBtn);

            // Badge Style (Patreon)
            auto patreonBtn = CreateButton("PatreonButton", 143, 310, yOffset, 140, 35, "Patreon");
            patreonBtn->EnableSplitButton(true);
            patreonBtn->SetIcon("assets/icons/patreon.png");
            patreonBtn->SetIconPosition(IconPosition::Left);
            patreonBtn->SetIconSize(20,20);
            patreonBtn->SetIconSpacing(7);
            patreonBtn->SetSplitButtonSecondaryText("122");
            patreonBtn->SetSplitButtonColors(
                    Colors::ButtonFace, Colors::TextDefault,
                    Color(249, 104, 84, 255), Colors::White
            );
            patreonBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Patreon button clicked\nOpen Patreon page");
            };
            patreonBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Patron count clicked\nShow patron list (122 patrons)");
            };
            mainContainer->AddChild(patreonBtn);

            // Badge Style (Liberapay)
            auto liberapayBtn = CreateButton("LiberapayButton", 144, 460, yOffset, 140, 35, "liberapay");
            liberapayBtn->EnableSplitButton(true);
            liberapayBtn->SetSplitButtonSecondaryText("5");
            liberapayBtn->SetSplitButtonColors(
                    Colors::ButtonFace, Colors::TextDefault,
                    Color(255, 193, 7, 255), Colors::Black
            );
            liberapayBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Liberapay button clicked\nOpen Liberapay page");
            };
            liberapayBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Liberapay count clicked\nShow supporters (5)");
            };
            mainContainer->AddChild(liberapayBtn);

            // PayPal Style
            auto paypalBtn = CreateButton("PaypalButton", 145, 610, yOffset, 140, 35, "Paypal");
            paypalBtn->EnableSplitButton(true);
            paypalBtn->SetIcon("assets/icons/paypal.png");
            paypalBtn->SetIconPosition(IconPosition::Left);
            paypalBtn->SetIconSize(20,20);
            paypalBtn->SetIconSpacing(7);
            paypalBtn->SetSplitButtonSecondaryText("297");
            paypalBtn->SetSplitButtonColors(
                    Colors::ButtonFace, Colors::TextDefault,
                    Color(0, 112, 186, 255), Colors::White
            );
            paypalBtn->onClick = [statusLabel]() {
                statusLabel->SetText("PayPal button clicked\nOpen PayPal donation page");
            };
            paypalBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("PayPal count clicked\nShow donors (297)");
            };
            mainContainer->AddChild(paypalBtn);

            yOffset += 50;

            // Counter Style Split Buttons
            auto counterBtn = CreateButton("CounterButton", 146, 20, yOffset, 130, 35, "Likes");
            counterBtn->SetStyle(ButtonStyles::CounterButtonStyle());
            counterBtn->SetSplitButtonSecondaryText("1.2k");
            counterBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Like button clicked\nAdded your like!");
            };
            counterBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Like count clicked\nShow who liked (1,200 users)");
            };
            mainContainer->AddChild(counterBtn);

            // Vertical Split Button
            auto vertSplitBtn = CreateButton("VertSplitButton", 147, 160, yOffset, 100, 50, "File");
            vertSplitBtn->EnableSplitButton(true);
            vertSplitBtn->SetSplitButtonOrientation(false); // Vertical
            vertSplitBtn->SetSplitButtonSecondaryText("Menu ▼");
            vertSplitBtn->onClick = [statusLabel]() {
                statusLabel->SetText("File action clicked\nOpen file dialog");
            };
            vertSplitBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("File menu clicked\nShow file options menu");
            };
            mainContainer->AddChild(vertSplitBtn);

            yOffset += 65;

            // ========================================
            // SECTION 5: CUSTOM STYLING
            // ========================================
            auto section5Label = std::make_shared<UltraCanvasLabel>("Section5", 150, 20, yOffset, 300, 25);
            section5Label->SetText("Custom Styling");
            section5Label->SetFontWeight(FontWeight::Bold);
            section5Label->SetTextColor(Color(0, 100, 200, 255));
            mainContainer->AddChild(section5Label);
            yOffset += 35;

            // Flat Button
            auto flatBtn = CreateButton("FlatButton", 151, 20, yOffset, 100, 35, "Flat");
            flatBtn->SetStyle(ButtonStyles::FlatStyle());
            flatBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Flat button clicked\nNo background, minimal style");
            };
            mainContainer->AddChild(flatBtn);

            // Rounded Button
            auto roundedBtn = CreateButton("RoundedButton", 152, 130, yOffset, 100, 35, "Rounded");
            roundedBtn->SetCornerRadius(18.0f);
            roundedBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Rounded button clicked\nCustom corner radius applied");
            };
            mainContainer->AddChild(roundedBtn);

            // Custom Colors
            auto customColorBtn = CreateButton("CustomColor", 153, 240, yOffset, 120, 35, "Custom");
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
            mainContainer->AddChild(customColorBtn);

            // Shadow Button
            auto shadowBtn = CreateButton("ShadowButton", 154, 370, yOffset, 120, 35, "Shadow");
            shadowBtn->SetShadow(true, Color(0, 0, 0, 128), Point2Di(3, 3));
            shadowBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Shadow button clicked\nCustom shadow effect applied");
            };
            mainContainer->AddChild(shadowBtn);

            // Large Button
            auto largeBtn = CreateButton("LargeButton", 155, 500, yOffset, 150, 50, "Large");
            largeBtn->SetFont("Arial", 16, FontWeight::Bold);
            largeBtn->SetPadding(20, 20, 10, 10);
            largeBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Large button clicked\nCustom size and padding");
            };
            mainContainer->AddChild(largeBtn);

            yOffset += 65;

            // ========================================
            // SECTION 6: INTERACTIVE EXAMPLES
            // ========================================
            auto section6Label = std::make_shared<UltraCanvasLabel>("Section6", 160, 20, yOffset, 300, 25);
            section6Label->SetText("Interactive Examples");
            section6Label->SetFontWeight(FontWeight::Bold);
            section6Label->SetTextColor(Color(0, 100, 200, 255));
            mainContainer->AddChild(section6Label);
            yOffset += 35;

            // Toggle Example
            auto toggleBtn = CreateButton("ToggleButton", 161, 20, yOffset, 120, 35, "Toggle: OFF");
            bool* toggleState = new bool(false);
            toggleBtn->onClick = [toggleBtn, toggleState, statusLabel]() {
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
            mainContainer->AddChild(toggleBtn);

            // Counter Example
            auto counterExampleBtn = CreateButton("CounterExample", 162, 150, yOffset, 140, 35, "Clicks: 0");
            int* clickCount = new int(0);
            counterExampleBtn->EnableSplitButton(true);
            counterExampleBtn->SetSplitButtonRatio(0);
            counterExampleBtn->SetSplitButtonSecondaryText("Reset");
            counterExampleBtn->onClick = [counterExampleBtn, clickCount, statusLabel]() {
                (*clickCount)++;
                std::ostringstream text;
                text << "Clicks: " << *clickCount;
                counterExampleBtn->SetText(text.str());

                std::ostringstream status;
                status << "Button clicked " << *clickCount << " times\nSecondary button resets counter";
                statusLabel->SetText(status.str());
            };
            counterExampleBtn->onSecondaryClick = [counterExampleBtn, clickCount, statusLabel]() {
                *clickCount = 0;
                counterExampleBtn->SetText("Clicks: 0");
                statusLabel->SetText("Counter reset to 0");
            };
            mainContainer->AddChild(counterExampleBtn);

            // Multi-Action Button
            auto multiBtn = CreateButton("MultiAction", 163, 300, yOffset, 170, 35, "Save");
            multiBtn->EnableSplitButton(true);
            multiBtn->SetSplitButtonRatio(0);
            multiBtn->SetSplitButtonSecondaryText("Options");
            multiBtn->SetIcon("assets/icons/save.png");
            multiBtn->onClick = [statusLabel]() {
                statusLabel->SetText("Quick save executed\nFile saved with default settings");
            };
            multiBtn->onSecondaryClick = [statusLabel]() {
                statusLabel->SetText("Save options clicked\nWould show: Save As, Save Copy, Export...");
            };
            mainContainer->AddChild(multiBtn);

            yOffset += 50;

            // ========================================
            // SECTION 7: BUTTON BUILDER EXAMPLE
            // ========================================
            auto section7Label = std::make_shared<UltraCanvasLabel>("Section7", 170, 20, yOffset, 400, 25);
            section7Label->SetText("Button Builder Pattern");
            section7Label->SetFontWeight(FontWeight::Bold);
            section7Label->SetTextColor(Color(0, 100, 200, 255));
            mainContainer->AddChild(section7Label);
            yOffset += 35;

            // Create button using builder pattern
            auto builderBtn = ButtonBuilder("BuilderButton", 171)
                    .SetPosition(20, yOffset)
                    .SetSize(230, 40)
                    .SetText("Built with Builder")
                    .SetIcon("assets/icons/build.png")
                    .SetStyle(ButtonStyles::PrimaryStyle())
                    .EnableSplitButton(true)
                    .SetSplitSecondaryText("→")
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
            mainContainer->AddChild(builderBtn);

            // Another builder example - complex configuration
            auto complexBuilderBtn = ButtonBuilder("ComplexBuilder", 172)
                    .SetPosition(260, yOffset)
                    .SetSize(180, 40)
                    .SetText("Complex")
                    .EnableSplitButton(true)
                    .SetSplitSecondaryText("99+")
                    .SetSplitRatio(0)
                    .SetSplitColors(
                            Color(75, 0, 130, 255), Colors::White,      // Indigo primary
                            Color(255, 69, 0, 255), Colors::White       // OrangeRed secondary
                    )
                    .SetFont("Arial", 14, FontWeight::Bold)
                    .SetShadow(true)
                    .OnClick([statusLabel]() {
                        statusLabel->SetText("Complex builder button clicked\nPrimary action with custom styling");
                    })
                    .OnSecondaryClick([statusLabel]() {
                        statusLabel->SetText("Badge clicked\n99+ notifications");
                    })
                    .Build();
            mainContainer->AddChild(complexBuilderBtn);

            return mainContainer;
    }

} // namespace UltraCanvas