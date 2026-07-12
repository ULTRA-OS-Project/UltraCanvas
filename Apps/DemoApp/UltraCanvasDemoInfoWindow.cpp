// UltraCanvasDemoInfoWindow.cpp
// Implementation of info window shown at application startup
// Version: 1.0.3 - event.targetWindow read via weak_ptr lock()
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextArea.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== INFO WINDOW IMPLEMENTATION =====

    // Open a popup window showing the changelog file in an editable, scrollable
    // text area. Mirrors the gauge example's "Code" button popup: the window is
    // kept alive by a static handle (one popup at a time) and closes on Escape.
    static void ShowChangelogPopup(const std::string& filePath) {
        static std::shared_ptr<UltraCanvasWindow> changelogWindow;

        std::ifstream file(filePath);
        std::string text;
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            text = buffer.str();
        } else {
            debugOutput << "Failed to open changelog: " << filePath << std::endl;
            text = "Could not load changelog file:\n" + filePath;
        }

        WindowConfig wc;
        wc.title = "UltraCanvas - Changelog";
        wc.width = 760;
        wc.height = 560;
        wc.resizable = true;
        wc.type = WindowType::Standard;
        changelogWindow = CreateWindow(wc);
        if (!changelogWindow || !changelogWindow->IsCreated()) return;

        auto editor = std::make_shared<UltraCanvasTextArea>("ChangelogViewer");
        editor->layout.display = CSSLayout::DisplayType::Block;
        editor->size.width = CSSLayout::Dimension::Vw(100);
        editor->size.height = CSSLayout::Dimension::Vh(100);
        editor->SetText(text);
        editor->SetShowLineNumbers(false);
        editor->SetFontSize(12);

        changelogWindow->SetEventCallback([](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                if (auto tw = event.targetWindow.lock()) static_cast<UltraCanvasWindow*>(tw.get())->Close();
                return true;
            }
            return false;
        });
        changelogWindow->AddChild(editor);
        changelogWindow->Show();
    }

    InfoWindow::InfoWindow() : UltraCanvasWindow() {
    }

    InfoWindow::~InfoWindow() {
        // Cleanup
    }

    bool InfoWindow::Initialize() {
        debugOutput << "Initializing Info Window..." << std::endl;

        // Configure the info window
        WindowConfig config;
        config.title = "UltraCanvas Demo - Information";
        config.width = 630;
        config.height = 625;
        config.resizable = false;
        config.type = WindowType::Dialog;
        config.modal = true;
        //config.centerOnScreen = true;

        Create(config);
        if (!_created) {
            debugOutput << "Failed to create info window" << std::endl;
            return false;
        }

        // Create window content
        CreateInfoContent();

        return true;
    }

    void InfoWindow::CreateInfoContent() {
        // Create title label
        layout.SetFlexColumn();

        // Example code icon and label
        auto exampleAppIcon = CreateImageElement("AppIcon", 130, 130);
        exampleAppIcon->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/UltraCanvas.png"));
        exampleAppIcon->SetFitMode(ImageFitMode::Contain);
        exampleAppIcon->SetMargin(12,0,6,0);
        // align-self: center centers the icon on the cross (horizontal) axis of the
        // flex column. justify-self is grid-only and would wipe this FlexItem.
        exampleAppIcon->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        AddChild(exampleAppIcon);

        titleLabel = std::make_shared<UltraCanvasLabel>("InfoTitle");
        titleLabel->SetText("UltraCanvas Demo");
        titleLabel->SetFontSize(16);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetTextColor(Color(0, 60, 120, 255));
        titleLabel->SetMargin(10);
        titleLabel->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        AddChild(titleLabel);

        // Create divider line 1
        auto divider = std::make_shared<UltraCanvasUIElement>("Divider", 600, 2);
        divider->SetBackgroundColor(Color(200, 200, 200, 255));
        divider->SetMargin(0,10,0,10);
        divider->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(divider);

        infoLabel1 = std::make_shared<UltraCanvasLabel>("InfoText1");
        infoLabel1->SetText("UltraCanvas is a comprehensive, open source, cross-plattform,\n"
                            "multi-programming-language GUI for programmers.\n"
                            "UltraCanvas will be the main GUI for ULTRA OS.\n"
                            "UltraCanvas will be available for both desktop as also mobile platforms.");
        infoLabel1->SetFontSize(11);
        infoLabel1->SetAlignment(TextAlignment::Center);
        infoLabel1->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1->SetMargin(10,20,10,20);
        AddChild(infoLabel1);

        // Create divider line 2
        auto divider2 = std::make_shared<UltraCanvasUIElement>("Divider", 600, 2);
        divider2->SetBackgroundColor(Color(200, 200, 200, 255));
        divider2->SetMargin(0,10,0,10);
        divider2->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(divider2);


        infoLabel1_4 = CreateLabel("On the right side of the title of each UC element you can find these icons:");
        infoLabel1_4->SetFontSize(12);
        infoLabel1_4->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_4->SetMargin(12,20,5,20);
        AddChild(infoLabel1_4);

        // Create icon descriptions with actual icons
        int iconSize = 24;

        // Programmers guide icon and label
        auto doccontainer = CreateContainer("doccont1");
        doccontainer->layout.SetFlexRow();
        doccontainer->SetMargin(10,20,10,20);
        programmersGuideIcon = CreateImageElement("DocIcon", iconSize, iconSize);
        programmersGuideIcon->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/text.png"));
        programmersGuideIcon->SetFitMode(ImageFitMode::Contain);
        doccontainer->AddChild(programmersGuideIcon);

        infoLabel2 = CreateLabel("DocText", "a) Programmer's Guide");
        infoLabel2->SetFontSize(12);
        infoLabel2->SetFontWeight(FontWeight::Bold);
        infoLabel2->SetAlignment(TextAlignment::Left);
        infoLabel2->SetTextColor(Color(60, 60, 60, 255));
        infoLabel2->SetMargin(3,0,0,10);
        infoLabel2->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        doccontainer->AddChild(infoLabel2);
        AddChild(doccontainer);

        // Example code icon and label
        auto codeContainer = CreateContainer("codecont1");
        codeContainer->layout.SetFlexRow();
        codeContainer->SetMargin(0,20,10,20);

        exampleCodeIcon = CreateImageElement("CodeIcon", iconSize, iconSize);
        exampleCodeIcon->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/c-plus-plus-icon.png"));
        exampleCodeIcon->SetFitMode(ImageFitMode::Contain);
        codeContainer->AddChild(exampleCodeIcon);

        infoLabel3 = CreateLabel("CodeText", 0, 22, "b) Example Code");
        infoLabel3->SetFontSize(12);
        infoLabel3->SetFontWeight(FontWeight::Bold);
        infoLabel3->SetAlignment(TextAlignment::Left);
        infoLabel3->SetTextColor(Color(60, 60, 60, 255));
        infoLabel3->SetMargin(3,0,0,10);
        infoLabel2->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        codeContainer->AddChild(infoLabel3);
        AddChild(codeContainer);

        // Create additional info
        auto additionalInfo = std::make_shared<UltraCanvasLabel>("AdditionalInfo");
        additionalInfo->SetText("These icons provide quick access to documentation and source code.");
        additionalInfo->SetFontSize(10);
        additionalInfo->SetAlignment(TextAlignment::Center);
        additionalInfo->SetTextColor(Color(100, 100, 100, 255));
        additionalInfo->SetWrap(TextWrap::WrapWord);
        additionalInfo->SetMargin(10,20);
        AddChild(additionalInfo);

        // link ultraos.eu
        auto openUltraosCallback = []() {
            OpenURL("https://www.ultraos.eu");
        };
        infoLabel1_1 = std::make_shared<UltraCanvasLabel>();
        infoLabel1_1->SetText("URL <span color=\"blue\">www.ultraos.eu</span>");
        infoLabel1_1->SetFontSize(10);
        infoLabel1_1->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_1->SetTextIsMarkup(true);
        infoLabel1_1->SetMargin(2,20);
        infoLabel1_1->onClick = openUltraosCallback;
        AddChild(infoLabel1_1);

        // link ULTRA OS Github
        infoLabel1_2 = std::make_shared<UltraCanvasLabel>();
        infoLabel1_2->SetText("Github: <span color=\"blue\">github.com/ULTRA-OS-Project/UltraCanvas</span>");
        infoLabel1_2->SetFontSize(10);
        infoLabel1_2->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_2->SetMargin(2,20);
        infoLabel1_2->SetTextIsMarkup(true);
        auto openGitHubCallback = []() {
            OpenURL("https://github.com/ULTRA-OS-Project/UltraCanvas");
        };
        infoLabel1_2->onClick = openGitHubCallback;
        AddChild(infoLabel1_2);

        // link Changelog
        infoLabel1_3 = std::make_shared<UltraCanvasLabel>();
        infoLabel1_3->SetText("Changelog: <span color=\"blue\">view what's new in this version</span>");
        infoLabel1_3->SetFontSize(10);
        infoLabel1_3->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_3->SetMargin(2,20);
        infoLabel1_3->SetTextIsMarkup(true);
        auto openChangelogCallback = []() {
            // Show the changelog (shipped under the resources dir as
            // Docs/UltraCanvas/CHANGELOG.md) in a text area popup, the same way
            // the gauge example's "Code" button shows generated source. OpenURL
            // can't render a local markdown file, so a text window is used.
            ShowChangelogPopup(NormalizePath(GetResourcesDir() + "Docs/UltraCanvas/CHANGELOG.md"));
        };
        infoLabel1_3->onClick = openChangelogCallback;
        AddChild(infoLabel1_3);

        // Create OK button
        okButton = std::make_shared<UltraCanvasButton>("OkButton", 100, 35);
        okButton->SetText("OK");
        okButton->SetStyle(ButtonStyles::SuccessStyle());
        okButton->SetCornerRadius(4);
        okButton->SetMargin(10);
        okButton->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        // Set button click handler
        okButton->SetOnClick([this]() {
            OnOkButtonClick();
        });

        AddChild(okButton);

        auto verLabel = CreateLabel("VerText", std::string("UltraCanvas version ")+versionString);
        verLabel->SetFontSize(10);
        verLabel->SetAlignment(TextAlignment::Center);
        verLabel->SetTextColor(Color(60, 60, 60, 255));
        verLabel->SetMargin(3);
        //verLabel->SetBorders(1);
        AddChild(verLabel);
    }

    void InfoWindow::SetOkCallback(std::function<void()> callback) {
        onOkCallback = callback;
    }

    void InfoWindow::OnOkButtonClick() {
        debugOutput << "OK button clicked - closing info window" << std::endl;

        // Call the callback if set
        if (onOkCallback) {
            onOkCallback();
        }

        // Close the window
        //Close();
    }

// ===== DEMO APPLICATION INFO WINDOW METHODS =====

    void UltraCanvasDemoApplication::ShowInfoWindow() {
        if (infoWindowShown) {
            return; // Already shown
        }

        debugOutput << "Showing application info window..." << std::endl;

        // Create the info window
        infoWindow = std::make_shared<InfoWindow>();

        if (!infoWindow->Initialize()) {
            debugOutput << "Failed to initialize info window" << std::endl;
            return;
        }

        // Set callback to handle OK button
        infoWindow->SetOkCallback([this]() {
            CloseInfoWindow();
        });

        // Show the window as modal
        infoWindow->Show();

        infoWindowShown = true;
    }

    void UltraCanvasDemoApplication::CloseInfoWindow() {
        debugOutput << "Closing info window..." << std::endl;

        if (!infoWindow || !infoWindow->Close()) {
            return;
        }

        infoWindow.reset();

        // Focus back to main window
        if (mainWindow) {
            mainWindow->SetFocus();
        }
    }

} // namespace UltraCanvas