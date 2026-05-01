// UltraCanvasDemoInfoWindow.cpp
// Implementation of info window shown at application startup
// Version: 1.0.1
// Last Modified: 2026-05-01
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBoxLayout.h"
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== INFO WINDOW IMPLEMENTATION =====

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
        auto layout = CreateVBoxLayout(this);

        // Example code icon and label
        auto exampleAppIcon = CreateImageElement("AppIcon", 130, 130);
        exampleAppIcon->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/UltraCanvas.png"));
        exampleAppIcon->SetFitMode(ImageFitMode::Contain);
        exampleAppIcon->SetMargin(12,0,6,0);
        layout->AddUIElement(exampleAppIcon)->SetCrossAlignment(LayoutAlignment::Center)->SetMainAlignment(LayoutAlignment::Center);

        titleLabel = std::make_shared<UltraCanvasLabel>("InfoTitle");
        titleLabel->SetText("UltraCanvas Demo");
        titleLabel->SetFontSize(16);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetAutoResize(true);
        titleLabel->SetTextColor(Color(0, 60, 120, 255));
        titleLabel->SetMargin(10);
        layout->AddUIElement(titleLabel)->SetCrossAlignment(LayoutAlignment::Center);

        // Create divider line 1
        auto divider = std::make_shared<UltraCanvasUIElement>("Divider", 1001, 10, 50, 600, 2);
        divider->SetBackgroundColor(Color(200, 200, 200, 255));
        divider->SetMargin(0,10,0,10);
        layout->AddUIElement(divider)->SetWidthMode(SizeMode::Fill);

        infoLabel1 = std::make_shared<UltraCanvasLabel>("InfoText1");
        infoLabel1->SetText("UltraCanvas is a comprehensive, open source, cross-plattform,\n"
                            "multi-programming-language GUI for programmers.\n"
                            "UltraCanvas will be the main GUI for ULTRA OS.\n"
                            "UltraCanvas will be available for both desktop as also mobile platforms.");
        infoLabel1->SetFontSize(11);
        infoLabel1->SetAlignment(TextAlignment::Center);
        infoLabel1->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1->SetAutoResize(true);
        infoLabel1->SetMargin(10,20,10,20);
        layout->AddUIElement(infoLabel1)->SetMainAlignment(LayoutAlignment::Center)->SetCrossAlignment(LayoutAlignment::Center);

        // Create divider line 2
        auto divider2 = std::make_shared<UltraCanvasUIElement>("Divider", 1001, 10, 50, 600, 2);
        divider2->SetBackgroundColor(Color(200, 200, 200, 255));
        divider2->SetMargin(0,10,0,10);
        layout->AddUIElement(divider2)->SetWidthMode(SizeMode::Fill);


        infoLabel1_4 = CreateLabel("On the right side of the title of each UC element you can find these icons:");
        infoLabel1_4->SetFontSize(12);
        infoLabel1_4->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_4->SetAutoResize(true);
        infoLabel1_4->SetMargin(12,20,5,20);
        layout->AddUIElement(infoLabel1_4);

        // Create icon descriptions with actual icons
        int iconSize = 24;

        // Programmers guide icon and label
        auto doccontainer = CreateContainer("doccont1", 0, 0, 0, 0, 25);
        auto docContainerLayout = CreateHBoxLayout(doccontainer.get());
        doccontainer->SetMargin(10,20,10,20);
        layout->AddUIElement(doccontainer)->SetWidthMode(SizeMode::Fill);
        programmersGuideIcon = CreateImageElement("DocIcon", iconSize, iconSize);
        programmersGuideIcon->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/text.png"));
        programmersGuideIcon->SetFitMode(ImageFitMode::Contain);
        docContainerLayout->AddUIElement(programmersGuideIcon);

        infoLabel2 = CreateLabel("DocText", 0, 21, "a) Programmer's Guide");
        infoLabel2->SetFontSize(12);
        infoLabel2->SetFontWeight(FontWeight::Bold);
        infoLabel2->SetAlignment(TextAlignment::Left);
        infoLabel2->SetTextColor(Color(60, 60, 60, 255));
        infoLabel2->SetMargin(3,0,0,10);
        docContainerLayout->AddUIElement(infoLabel2, 1)->SetCrossAlignment(LayoutAlignment::Center);

        // Example code icon and label
        auto codeContainer = CreateContainer("codecont1", 0, 0, 0, 0, 25);
        auto codeContainerLayout = CreateHBoxLayout(codeContainer.get());
        codeContainer->SetMargin(0,20,10,20);
        layout->AddUIElement(codeContainer)->SetWidthMode(SizeMode::Fill);

        exampleCodeIcon = CreateImageElement("CodeIcon", iconSize, iconSize);
        exampleCodeIcon->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/c-plus-plus-icon.png"));
        exampleCodeIcon->SetFitMode(ImageFitMode::Contain);
        codeContainerLayout->AddUIElement(exampleCodeIcon);

        infoLabel3 = CreateLabel("CodeText", 0, 22, "b) Example Code");
        infoLabel3->SetFontSize(12);
        infoLabel3->SetFontWeight(FontWeight::Bold);
        infoLabel3->SetAlignment(TextAlignment::Left);
        infoLabel3->SetTextColor(Color(60, 60, 60, 255));
        infoLabel3->SetMargin(3,0,0,10);
        codeContainerLayout->AddUIElement(infoLabel3, 1)->SetCrossAlignment(LayoutAlignment::Center);;

        // Create additional info
        auto additionalInfo = std::make_shared<UltraCanvasLabel>("AdditionalInfo", 1007, 50, 310, 500, 40);
        additionalInfo->SetText("These icons provide quick access to documentation and source code.");
        additionalInfo->SetFontSize(10);
        additionalInfo->SetAlignment(TextAlignment::Center);
        additionalInfo->SetTextColor(Color(100, 100, 100, 255));
        additionalInfo->SetWrap(TextWrap::WrapWord);
        additionalInfo->SetMargin(10,20);
        layout->AddUIElement(additionalInfo)->SetWidthMode(SizeMode::Fill);

        // link ultraos.eu
        auto openUltraosCallback = []() {
            OpenURL("https://www.ultraos.eu");
        };
        infoLabel1_1 = std::make_shared<UltraCanvasLabel>();
        infoLabel1_1->SetText("URL <span color=\"blue\">www.ultraos.eu</span>");
        infoLabel1_1->SetFontSize(10);
        infoLabel1_1->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_1->SetAutoResize(true);
        infoLabel1_1->SetTextIsMarkup(true);
        infoLabel1_1->SetMargin(2,20);
        infoLabel1_1->onClick = openUltraosCallback;
        layout->AddUIElement(infoLabel1_1);

        // link ULTRA OS Github
        infoLabel1_2 = std::make_shared<UltraCanvasLabel>();
        infoLabel1_2->SetText("Github: <span color=\"blue\">github.com/ULTRA-OS-Project/UltraCanvas</span>");
        infoLabel1_2->SetFontSize(10);
        infoLabel1_2->SetTextColor(Color(60, 60, 60, 255));
        infoLabel1_2->SetAutoResize(true);
        infoLabel1_2->SetMargin(2,20);
        infoLabel1_2->SetTextIsMarkup(true);
        auto openGitHubCallback = []() {
            OpenURL("https://github.com/ULTRA-OS-Project/UltraCanvas");
        };
        infoLabel1_2->onClick = openGitHubCallback;
        layout->AddUIElement(infoLabel1_2);

        // Create OK button
        okButton = std::make_shared<UltraCanvasButton>("OkButton", 1008, 250, 370, 100, 35);
        okButton->SetText("OK");
        okButton->SetStyle(ButtonStyles::SuccessStyle());
        okButton->SetCornerRadius(4);
        okButton->SetMargin(10);

        // Set button click handler
        okButton->SetOnClick([this]() {
            OnOkButtonClick();
        });

        layout->AddUIElement(okButton)->SetCrossAlignment(LayoutAlignment::Center);

        auto verLabel = CreateLabel("VerText", 0, 22, std::string("UltraCanvas version ")+versionString);
        verLabel->SetFontSize(10);
        verLabel->SetAlignment(TextAlignment::Center);
        verLabel->SetTextColor(Color(60, 60, 60, 255));
        verLabel->SetMargin(3);
        //verLabel->SetBorders(1);
        verLabel->SetAutoResize(true);
        layout->AddUIElement(verLabel)->SetMainAlignment(LayoutAlignment::Center)->SetCrossAlignment(LayoutAlignment::Center);;
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