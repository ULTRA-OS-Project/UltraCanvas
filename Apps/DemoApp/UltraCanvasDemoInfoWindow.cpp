// UltraCanvasDemoInfoWindow.cpp
// Implementation of info window shown at application startup
// Version: 1.0.0
// Last Modified: 2025-01-14
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include <iostream>

namespace UltraCanvas {

// ===== INFO WINDOW IMPLEMENTATION =====

    InfoWindow::InfoWindow() : UltraCanvasWindow() {
    }

    InfoWindow::~InfoWindow() {
        // Cleanup
    }

    bool InfoWindow::Initialize() {
        std::cout << "Initializing Info Window..." << std::endl;

        // Configure the info window
        WindowConfig config;
        config.title = "UltraCanvas Demo - Information";
        config.width = 600;
        config.height = 350;
        config.resizable = false;
        config.type = WindowType::Dialog;
        config.modal = true;
        //config.centerOnScreen = true;

        if (!Create(config)) {
            std::cerr << "Failed to create info window" << std::endl;
            return false;
        }

        // Create window content
        CreateInfoContent();

        return true;
    }

    void InfoWindow::CreateInfoContent() {
        // Create title label
        titleLabel = std::make_shared<UltraCanvasLabel>("InfoTitle", 1000, 50, 30, 500, 30);
        titleLabel->SetText("Welcome to UltraCanvas Demo Application");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetTextColor(Color(0, 60, 120, 255));
        AddChild(titleLabel);

        // Create divider line
        auto divider = std::make_shared<UltraCanvasContainer>("Divider", 1001, 50, 70, 500, 2);
        ContainerStyle dividerStyle;
        dividerStyle.backgroundColor = Color(200, 200, 200, 255);
        dividerStyle.borderWidth = 0;
        divider->SetContainerStyle(dividerStyle);
        AddChild(divider);

        // Create info text
        infoLabel1 = std::make_shared<UltraCanvasLabel>("InfoText1", 1002, 10, 100, 600, 25);
        infoLabel1->SetText("On the right side of the title of each UC element you can find these icons:");
        infoLabel1->SetFontSize(14);
        infoLabel1->SetAlignment(TextAlignment::Center);
        infoLabel1->SetTextColor(Color(60, 60, 60, 255));
        AddChild(infoLabel1);

        // Create icon descriptions with actual icons
        int iconY = 140;
        int iconSize = 24;
        int textOffset = 35;

        // Programmers guide icon and label
        programmersGuideIcon = std::make_shared<UltraCanvasImageElement>("DocIcon", 1003, 20, iconY, iconSize, iconSize);
        programmersGuideIcon->LoadFromFile("assets/icons/component.png");
        programmersGuideIcon->SetScaleMode(ImageScaleMode::Uniform);
        AddChild(programmersGuideIcon);

        infoLabel2 = std::make_shared<UltraCanvasLabel>("DocText", 1004, 20 + textOffset, iconY, 400, 25);
        infoLabel2->SetText("a) Programmer's Guide");
        infoLabel2->SetFontSize(14);
        infoLabel2->SetAlignment(TextAlignment::Left);
        infoLabel2->SetTextColor(Color(60, 60, 60, 255));
        AddChild(infoLabel2);

        // Example code icon and label
        iconY += 40;
        exampleCodeIcon = std::make_shared<UltraCanvasImageElement>("CodeIcon", 1005, 20, iconY, iconSize, iconSize);
        exampleCodeIcon->LoadFromFile("assets/icons/c-plus-plus-icon.png");
        exampleCodeIcon->SetScaleMode(ImageScaleMode::Uniform);
        AddChild(exampleCodeIcon);

        infoLabel3 = std::make_shared<UltraCanvasLabel>("CodeText", 1006, 20 + textOffset, iconY, 400, 25);
        infoLabel3->SetText("b) Example Code");
        infoLabel3->SetFontSize(14);
        infoLabel3->SetAlignment(TextAlignment::Left);
        infoLabel3->SetTextColor(Color(60, 60, 60, 255));
        AddChild(infoLabel3);

        // Create additional info
        auto additionalInfo = std::make_shared<UltraCanvasLabel>("AdditionalInfo", 1007, 50, 230, 500, 40);
        additionalInfo->SetText("Click on any item in the left panel to see its demonstration.\n"
                                "These icons provide quick access to documentation and source code.");
        additionalInfo->SetFontSize(12);
        additionalInfo->SetAlignment(TextAlignment::Center);
        additionalInfo->SetTextColor(Color(100, 100, 100, 255));
        additionalInfo->SetWordWrap(true);
        AddChild(additionalInfo);

        // Create OK button
        okButton = std::make_shared<UltraCanvasButton>("OkButton", 1008, 250, 290, 100, 35);
        okButton->SetText("OK");
        okButton->SetStyle(ButtonStyles::SuccessStyle());
//        okButton->SetColor(Color(0, 120, 200, 255));
//        okButton->SetTextColor(Color(255, 255, 255, 255));
//        okButton->SetHoverBackgroundColor(Color(0, 140, 220, 255));
//        okButton->SetPressedBackgroundColor(Color(0, 100, 180, 255));
        okButton->SetCornerRadius(4);

        // Set button click handler
        okButton->SetOnClick([this]() {
            OnOkButtonClick();
        });

        AddChild(okButton);
    }

    void InfoWindow::SetOkCallback(std::function<void()> callback) {
        onOkCallback = callback;
    }

    void InfoWindow::OnOkButtonClick() {
        std::cout << "OK button clicked - closing info window" << std::endl;

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

        std::cout << "Showing application info window..." << std::endl;

        // Create the info window
        infoWindow = std::make_shared<InfoWindow>();

        if (!infoWindow->Initialize()) {
            std::cerr << "Failed to initialize info window" << std::endl;
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
        std::cout << "Closing info window..." << std::endl;

        if (infoWindow) {
            infoWindow.reset();
        }

        // Focus back to main window
        if (mainWindow) {
            mainWindow->SetFocus();
        }
    }

} // namespace UltraCanvas