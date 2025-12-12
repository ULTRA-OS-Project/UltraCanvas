// Apps/DemoApp/UltraCanvasDropDownExamples.cpp
// Comprehensive dropdown/combobox examples with icons and multi-selection
// Version: 2.0.0
// Last Modified: 2025-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <iostream>

namespace UltraCanvas {

// Helper function to create section separator
    std::shared_ptr<UltraCanvasContainer> CreateDropdownSeparator(long id, long x, long y, long width) {
        auto separator = std::make_shared<UltraCanvasContainer>("DropdownSep" + std::to_string(id), id, x, y, width, 2);
        separator->SetBackgroundColor(Color(200, 200, 200, 255));
        return separator;
    }

// Helper function to create section title
    std::shared_ptr<UltraCanvasLabel> CreateDropdownSectionTitle(long id, long x, long y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("DropdownSecTitle" + std::to_string(id), id, x, y, 400, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDropdownExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("DropdownExamples", 300, 0, 0, 1020, 1000);
        mainContainer->SetBackgroundColor(Colors::White);

        long currentY = 10;

        // ===== MAIN TITLE =====
        auto mainTitle = std::make_shared<UltraCanvasLabel>("DropdownMainTitle", 301, 20, currentY, 900, 30);
        mainTitle->SetText("UltraCanvas Dropdown Component Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);

        currentY += 40;

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("DropdownDesc", 302, 20, currentY, 960, 40);
        description->SetText("Comprehensive dropdown examples showcasing icons, multi-selection, and various configurations");
        description->SetWordWrap(true);
        description->SetTextColor(Color(80, 80, 80, 255));
        mainContainer->AddChild(description);

        currentY += 30;

        // ===== SECTION 1: BASIC DROPDOWNS =====
        mainContainer->AddChild(CreateDropdownSectionTitle(310, 20, currentY, "Basic Dropdowns"));
        currentY += 35;

        // Simple Dropdown
        auto simpleDropdown = std::make_shared<UltraCanvasDropdown>("SimpleDropdown", 311, 30, currentY, 220, 30);
        simpleDropdown->AddItem("Option 1");
        simpleDropdown->AddItem("Option 2");
        simpleDropdown->AddItem("Option 3");
        simpleDropdown->AddItem("Very Long Option Text That Extends");
        simpleDropdown->SetSelectedIndex(0);

        auto simpleLabel = std::make_shared<UltraCanvasLabel>("SimpleLabel", 312, 270, currentY + 5, 300, 20);
        simpleLabel->SetText("Simple dropdown with text items");
        simpleLabel->SetFontSize(12);

        mainContainer->AddChild(simpleDropdown);
        mainContainer->AddChild(simpleLabel);

        currentY += 45;

        // Dropdown with separators
        auto separatorDropdown = std::make_shared<UltraCanvasDropdown>("SeparatorDropdown", 313, 30, currentY, 220, 30);
        separatorDropdown->AddItem("File Operations");
        separatorDropdown->AddItem("Open", "open");
        separatorDropdown->AddItem("Save", "save");
        separatorDropdown->AddSeparator();
        separatorDropdown->AddItem("Edit Operations");
        separatorDropdown->AddItem("Copy", "copy");
        separatorDropdown->AddItem("Paste", "paste");
        separatorDropdown->SetSelectedIndex(1);

        auto separatorLabel = std::make_shared<UltraCanvasLabel>("SeparatorLabel", 314, 270, currentY + 5, 300, 20);
        separatorLabel->SetText("Dropdown with separators");
        separatorLabel->SetFontSize(12);

        mainContainer->AddChild(separatorDropdown);
        mainContainer->AddChild(separatorLabel);

        currentY += 45;
        mainContainer->AddChild(CreateDropdownSeparator(315, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 2: DROPDOWNS WITH ICONS =====
        mainContainer->AddChild(CreateDropdownSectionTitle(320, 20, currentY, "Dropdowns with Icons"));
        currentY += 35;

        // File types dropdown with icons
        auto fileTypesDropdown = std::make_shared<UltraCanvasDropdown>("FileTypesDropdown", 321, 30, currentY, 220, 30);
        fileTypesDropdown->AddItem("Text Document", "txt", "./assets/icons/document.png");
        fileTypesDropdown->AddItem("PDF Document", "pdf", "./assets/icons/pdf.png");
        fileTypesDropdown->AddItem("Image File", "img", "./assets/icons/image.png");
        fileTypesDropdown->AddItem("Video File", "vid", "./assets/icons/video.png");
        fileTypesDropdown->SetSelectedIndex(0);

        auto fileTypesLabel = std::make_shared<UltraCanvasLabel>("FileTypesLabel", 322, 270, currentY + 5, 250, 20);
        fileTypesLabel->SetText("File types with document icons");
        fileTypesLabel->SetFontSize(12);

        mainContainer->AddChild(fileTypesDropdown);
        mainContainer->AddChild(fileTypesLabel);

        currentY += 45;

        // Actions dropdown with icons
        auto actionsDropdown = std::make_shared<UltraCanvasDropdown>("ActionsDropdown", 323, 30, currentY, 220, 30);
        actionsDropdown->AddItem("New File", "new", "./assets/icons/new.png");
        actionsDropdown->AddItem("Open File", "open", "./assets/icons/folder.png");
        actionsDropdown->AddItem("Save File", "save", "./assets/icons/save.png");
        actionsDropdown->AddItem("Print", "print", "./assets/icons/print.png");
        actionsDropdown->AddItem("Settings", "settings", "./assets/icons/settings.png");
        actionsDropdown->SetSelectedIndex(0);

        auto actionsLabel = std::make_shared<UltraCanvasLabel>("ActionsLabel", 324, 270, currentY + 5, 250, 20);
        actionsLabel->SetText("Actions with icon indicators");
        actionsLabel->SetFontSize(12);

        mainContainer->AddChild(actionsDropdown);
        mainContainer->AddChild(actionsLabel);

        currentY += 45;
        mainContainer->AddChild(CreateDropdownSeparator(325, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 3: COUNTRY FLAGS DROPDOWN =====
        mainContainer->AddChild(CreateDropdownSectionTitle(330, 20, currentY, "Country Selection with Flags"));
        currentY += 35;

        // Countries dropdown with flags
        auto countriesDropdown = std::make_shared<UltraCanvasDropdown>("CountriesDropdown", 331, 30, currentY, 280, 30);

        // Add countries with their flag icons (ISO 3166-1 alpha-2 codes)
        countriesDropdown->AddItem("United States", "US", "./assets/flags/US.png");
        countriesDropdown->AddItem("United Kingdom", "GB", "./assets/flags/GB.png");
        countriesDropdown->AddItem("Germany", "DE", "./assets/flags/DE.png");
        countriesDropdown->AddItem("France", "FR", "./assets/flags/FR.png");
        countriesDropdown->AddItem("Italy", "IT", "./assets/flags/IT.png");
        countriesDropdown->AddItem("Spain", "ES", "./assets/flags/ES.png");
        countriesDropdown->AddItem("Japan", "JP", "./assets/flags/JP.png");
        countriesDropdown->AddItem("China", "CN", "./assets/flags/CN.png");
        countriesDropdown->AddItem("India", "IN", "./assets/flags/IN.png");
        countriesDropdown->AddItem("Brazil", "BR", "./assets/flags/BR.png");
        countriesDropdown->AddItem("Canada", "CA", "./assets/flags/CA.png");
        countriesDropdown->AddItem("Australia", "AU", "./assets/flags/AU.png");
        countriesDropdown->AddItem("Mexico", "MX", "./assets/flags/MX.png");
        countriesDropdown->AddItem("Russia", "RU", "./assets/flags/RU.png");
        countriesDropdown->AddItem("South Korea", "KR", "./assets/flags/KR.png");
        countriesDropdown->AddItem("Netherlands", "NL", "./assets/flags/NL.png");
        countriesDropdown->AddItem("Sweden", "SE", "./assets/flags/SE.png");
        countriesDropdown->AddItem("Switzerland", "CH", "./assets/flags/CH.png");
        countriesDropdown->AddItem("Poland", "PL", "./assets/flags/PL.png");
        countriesDropdown->AddItem("Belgium", "BE", "./assets/flags/BE.png");

        countriesDropdown->SetSelectedIndex(0);

        // Country selection status label
        auto countryStatusLabel = std::make_shared<UltraCanvasLabel>("CountryStatus", 332, 330, currentY + 5, 400, 20);
        countryStatusLabel->SetText("Selected: United States (US)");
        countryStatusLabel->SetFontSize(12);
        countryStatusLabel->SetTextColor(Color(0, 100, 0, 255));

        // Callback for country selection
        countriesDropdown->onSelectionChanged = [countryStatusLabel](int index, const DropdownItem& item) {
            std::cout << "Country selected: " << item.text << " (" << item.value << ")" << std::endl;
            countryStatusLabel->SetText("Selected: " + item.text + " (" + item.value + ")");
        };

        mainContainer->AddChild(countriesDropdown);
        mainContainer->AddChild(countryStatusLabel);

        currentY += 45;
        mainContainer->AddChild(CreateDropdownSeparator(337, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 4: MULTI-SELECTION DROPDOWNS =====
        mainContainer->AddChild(CreateDropdownSectionTitle(340, 20, currentY, "Multi-Selection Dropdowns"));
        currentY += 35;

        // Programming languages multi-select
        auto languagesDropdown = std::make_shared<UltraCanvasDropdown>("LanguagesDropdown", 341, 30, currentY, 250, 30);
        languagesDropdown->SetMultiSelectEnabled(true);
        languagesDropdown->AddItem("C++", "cpp", "./assets/icons/cpp.png");
        languagesDropdown->AddItem("Python", "py", "./assets/icons/python.png");
        languagesDropdown->AddItem("JavaScript", "js", "./assets/icons/javascript.png");
        languagesDropdown->AddItem("Java", "java", "./assets/icons/java.png");
        languagesDropdown->AddItem("C#", "cs", "./assets/icons/csharp.png");
        languagesDropdown->AddItem("Go", "go", "./assets/icons/go.png");

        // Pre-select some items
        languagesDropdown->SetItemSelected(0, true); // C++
        languagesDropdown->SetItemSelected(1, true); // Python

        auto languagesStatus = std::make_shared<UltraCanvasLabel>("LanguagesStatus", 342, 300, currentY + 5, 400, 60);
        languagesStatus->SetText("Selected: C++, Python");
        languagesStatus->SetWordWrap(true);
        languagesStatus->SetFontSize(12);
        languagesStatus->SetTextColor(Color(0, 100, 0, 255));

        // Callback for multi-selection
        languagesDropdown->onSelectedItemsChanged = [languagesStatus](const std::vector<DropdownItem>& items) {
            std::string selectedText = "Selected: ";
            if (items.empty()) {
                selectedText += "None";
            } else {
                for (size_t i = 0; i < items.size(); ++i) {
                    selectedText += items[i].text;
                    if (i < items.size() - 1) selectedText += ", ";
                }
            }
            languagesStatus->SetText(selectedText);
            std::cout << "Programming languages selection changed: " << selectedText << std::endl;
        };

        mainContainer->AddChild(languagesDropdown);
        mainContainer->AddChild(languagesStatus);

        currentY += 75;

        // Fruit multi-select (no icons)
        auto fruitsDropdown = std::make_shared<UltraCanvasDropdown>("FruitsDropdown", 343, 30, currentY, 250, 30);
        fruitsDropdown->SetMultiSelectEnabled(true);
        fruitsDropdown->AddItem("Apple", "apple");
        fruitsDropdown->AddItem("Banana", "banana");
        fruitsDropdown->AddItem("Orange", "orange");
        fruitsDropdown->AddItem("Grape", "grape");
        fruitsDropdown->AddItem("Strawberry", "strawberry");
        fruitsDropdown->AddItem("Mango", "mango");
        fruitsDropdown->AddItem("Pineapple", "pineapple");
        fruitsDropdown->AddItem("Watermelon", "watermelon");

        auto fruitsLabel = std::make_shared<UltraCanvasLabel>("FruitsLabel", 344, 300, currentY + 5, 400, 20);
        fruitsLabel->SetText("Multi-select fruits (with checkboxes)");
        fruitsLabel->SetFontSize(12);

        mainContainer->AddChild(fruitsDropdown);
        mainContainer->AddChild(fruitsLabel);

        currentY += 45;

        // Control buttons for fruits dropdown
        auto selectAllBtn = std::make_shared<UltraCanvasButton>("SelectAllFruits", 345, 30, currentY, 100, 30);
        selectAllBtn->SetText("Select All");
        selectAllBtn->SetOnClick([fruitsDropdown]() {
            fruitsDropdown->SelectAll();
            std::cout << "Selected all fruits" << std::endl;
        });

        auto deselectAllBtn = std::make_shared<UltraCanvasButton>("DeselectAllFruits", 346, 140, currentY, 100, 30);
        deselectAllBtn->SetText("Clear All");
        deselectAllBtn->SetOnClick([fruitsDropdown]() {
            fruitsDropdown->DeselectAll();
            std::cout << "Cleared all fruit selections" << std::endl;
        });

        auto getSelectedBtn = std::make_shared<UltraCanvasButton>("GetSelectedFruits", 347, 250, currentY, 120, 30);
        getSelectedBtn->SetText("Get Selected");
        getSelectedBtn->SetOnClick([fruitsDropdown]() {
            auto selected = fruitsDropdown->GetSelectedItems();
            std::cout << "Currently selected fruits (" << selected.size() << "): ";
            for (const auto& item : selected) {
                std::cout << item.text << " ";
            }
            std::cout << std::endl;
        });

        mainContainer->AddChild(selectAllBtn);
        mainContainer->AddChild(deselectAllBtn);
        mainContainer->AddChild(getSelectedBtn);

        currentY += 45;
        mainContainer->AddChild(CreateDropdownSeparator(348, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 5: MULTI-SELECT COUNTRIES WITH FLAGS =====
        mainContainer->AddChild(CreateDropdownSectionTitle(350, 20, currentY, "Multi-Select Countries (Travel Destinations)"));
        currentY += 35;

        // Travel destinations multi-select with flags
        auto travelDropdown = std::make_shared<UltraCanvasDropdown>("TravelDropdown", 351, 30, currentY, 280, 30);
        travelDropdown->SetMultiSelectEnabled(true);
        travelDropdown->AddItem("France", "FR", "./assets/flags/FR.png");
        travelDropdown->AddItem("Italy", "IT", "./assets/flags/IT.png");
        travelDropdown->AddItem("Spain", "ES", "./assets/flags/ES.png");
        travelDropdown->AddItem("Japan", "JP", "./assets/flags/JP.png");
        travelDropdown->AddItem("Thailand", "TH", "./assets/flags/TH.png");
        travelDropdown->AddItem("United States", "US", "./assets/flags/US.png");
        travelDropdown->AddItem("United Kingdom", "GB", "./assets/flags/GB.png");
        travelDropdown->AddItem("Australia", "AU", "./assets/flags/AU.png");
        travelDropdown->AddItem("Greece", "GR", "./assets/flags/GR.png");
        travelDropdown->AddItem("Switzerland", "CH", "./assets/flags/CH.png");

        auto travelStatus = std::make_shared<UltraCanvasLabel>("TravelStatus", 352, 330, currentY + 5, 500, 60);
        travelStatus->SetText("Select your travel destinations");
        travelStatus->SetWordWrap(true);
        travelStatus->SetFontSize(12);
        travelStatus->SetBackgroundColor(Color(250, 250, 250, 255));
        travelStatus->SetBorders(1, Color(200, 200, 200, 255));
        travelStatus->SetPadding(5);

        // Callback for travel selection
        travelDropdown->onMultiSelectionChanged = [travelStatus](const std::vector<int>& indices) {
            std::string statusText;
            if (indices.empty()) {
                statusText = "No destinations selected yet";
            } else {
                statusText = "Selected " + std::to_string(indices.size()) + " destination";
                if (indices.size() > 1) statusText += "s";
            }
            travelStatus->SetText(statusText);
        };

        travelDropdown->onSelectedItemsChanged = [](const std::vector<DropdownItem>& items) {
            std::cout << "Travel destinations: ";
            for (const auto& item : items) {
                std::cout << item.text << " (" << item.value << ") ";
            }
            std::cout << std::endl;
        };

        mainContainer->AddChild(travelDropdown);
        mainContainer->AddChild(travelStatus);

        currentY += 75;
        mainContainer->AddChild(CreateDropdownSeparator(353, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 6: INTERACTIVE DEMO =====
        mainContainer->AddChild(CreateDropdownSectionTitle(370, 20, currentY, "Interactive Demo"));
        currentY += 35;

        // Create interactive demo with event logging
        auto interactiveDropdown = std::make_shared<UltraCanvasDropdown>("InteractiveDropdown", 371, 30, currentY, 250, 30);
        interactiveDropdown->AddItem("Monitor Events", "monitor");
        interactiveDropdown->AddItem("Selection Changed", "selection");
        interactiveDropdown->AddItem("Item Hovered", "hover");
        interactiveDropdown->AddItem("Dropdown Opened", "opened");
        interactiveDropdown->AddItem("Dropdown Closed", "closed");
        interactiveDropdown->SetSelectedIndex(0);

        auto eventLog = std::make_shared<UltraCanvasLabel>("EventLog", 372, 300, currentY, 500, 80);
        eventLog->SetText("Event log: No events yet");
        eventLog->SetWordWrap(true);
        eventLog->SetFontSize(11);
        eventLog->SetBackgroundColor(Color(245, 245, 245, 255));
        eventLog->SetBorders(1.0f);
        eventLog->SetBordersColor(Color(180, 180, 180, 255));
        eventLog->SetPadding(5.0f);

        // Set up all callbacks
        interactiveDropdown->onSelectionChanged = [eventLog](int index, const DropdownItem& item) {
            std::string log = "Selection Changed: " + item.text + " (index: " + std::to_string(index) + ")";
            eventLog->SetText(log);
            std::cout << log << std::endl;
        };

        interactiveDropdown->onItemHovered = [eventLog](int index, const DropdownItem& item) {
            std::string log = "Item Hovered: " + item.text + " (index: " + std::to_string(index) + ")";
            eventLog->SetText(log);
        };

        interactiveDropdown->onDropdownOpened = [eventLog]() {
            std::string log = "Event: Dropdown Opened";
            eventLog->SetText(log);
            std::cout << log << std::endl;
        };

        interactiveDropdown->onDropdownClosed = [eventLog]() {
            std::string log = "Event: Dropdown Closed";
            eventLog->SetText(log);
            std::cout << log << std::endl;
        };

        mainContainer->AddChild(interactiveDropdown);
        mainContainer->AddChild(eventLog);

        currentY += 95;

        std::cout << "Dropdown examples created successfully with " << (currentY + 60) << " pixels height" << std::endl;

        return mainContainer;
    }

} // namespace UltraCanvas