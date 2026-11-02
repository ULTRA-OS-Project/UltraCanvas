// Apps/DemoApp/UltraCanvasDropDownExamples.cpp
// Comprehensive dropdown/combobox examples with icons and multi-selection
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  section, every text and status line sized to fit, buttons
//                  as wide as their text
// Version: 2.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    namespace {
        // A control and its caption side by side; the caption takes the rest
        // of the line and wraps, or moves under the control when narrow.
        std::shared_ptr<UltraCanvasContainer> AddDropdownLine(const std::shared_ptr<UltraCanvasContainer>& card,
                                                              const std::string& id) {
            auto row = DemoKit::AddRow(card, id);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            return row;
        }
    } // anonymous namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDropdownExamples() {
        auto mainContainer = DemoKit::CreatePage("DropdownExamples", "UltraCanvas Dropdown Component Examples",
                "Comprehensive dropdown examples showcasing icons, multi-selection, and various configurations");

        // ===== SECTION 1: BASIC DROPDOWNS =====
        auto row1 = DemoKit::AddRow(mainContainer, "DropdownRow1");
        auto card1 = DemoKit::AddCard(row1, "DropdownBasicCard", "Basic Dropdowns");

        // Simple Dropdown
        auto simpleDropdown = std::make_shared<UltraCanvasDropdown>("SimpleDropdown", 0, 0, 220, 30);
        simpleDropdown->AddItem("Option 1");
        simpleDropdown->AddItem("Option 2");
        simpleDropdown->AddItem("Option 3");
        simpleDropdown->AddItem("Very Long Option Text That Extends");
        simpleDropdown->SetSelectedIndex(0);

        auto simpleLine = AddDropdownLine(card1, "SimpleLine");
        DemoKit::AddFixed(simpleLine, simpleDropdown);
        DemoKit::Add(simpleLine, DemoKit::CreateText("SimpleLabel", "Simple dropdown with text items"), 1);

        // Dropdown with separators
        auto separatorDropdown = std::make_shared<UltraCanvasDropdown>("SeparatorDropdown", 0, 0, 220, 30);
        separatorDropdown->AddItem("File Operations");
        separatorDropdown->AddItem("Open", "open");
        separatorDropdown->AddItem("Save", "save");
        separatorDropdown->AddSeparator();
        separatorDropdown->AddItem("Edit Operations");
        separatorDropdown->AddItem("Copy", "copy");
        separatorDropdown->AddItem("Paste", "paste");
        separatorDropdown->SetSelectedIndex(1);

        auto separatorLine = AddDropdownLine(card1, "SeparatorLine");
        DemoKit::AddFixed(separatorLine, separatorDropdown);
        DemoKit::Add(separatorLine, DemoKit::CreateText("SeparatorLabel", "Dropdown with separators"), 1);

        // ===== SECTION 2: DROPDOWNS WITH ICONS =====
        auto card2 = DemoKit::AddCard(row1, "DropdownIconsCard", "Dropdowns with Icons");

        // File types dropdown with icons
        auto fileTypesDropdown = std::make_shared<UltraCanvasDropdown>("FileTypesDropdown", 0, 0, 220, 30);
        fileTypesDropdown->AddItem("Text Document", "txt", NormalizePath(GetResourcesDir() + "media/icons/document.png"));
        fileTypesDropdown->AddItem("PDF Document", "pdf", NormalizePath(GetResourcesDir() + "media/icons/pdf.png"));
        fileTypesDropdown->AddItem("Image File", "img", NormalizePath(GetResourcesDir() + "media/icons/image.png"));
        fileTypesDropdown->AddItem("Video File", "vid", NormalizePath(GetResourcesDir() + "media/icons/video.png"));
        fileTypesDropdown->SetSelectedIndex(0);

        auto fileTypesLine = AddDropdownLine(card2, "FileTypesLine");
        DemoKit::AddFixed(fileTypesLine, fileTypesDropdown);
        DemoKit::Add(fileTypesLine, DemoKit::CreateText("FileTypesLabel", "File types with document icons"), 1);

        // Actions dropdown with icons
        auto actionsDropdown = std::make_shared<UltraCanvasDropdown>("ActionsDropdown", 0, 0, 220, 30);
        actionsDropdown->AddItem("New File", "new", NormalizePath(GetResourcesDir() + "media/icons/new.png"));
        actionsDropdown->AddItem("Open File", "open", NormalizePath(GetResourcesDir() + "media/icons/folder.png"));
        actionsDropdown->AddItem("Save File", "save", NormalizePath(GetResourcesDir() + "media/icons/save.png"));
        actionsDropdown->AddItem("Print", "print", NormalizePath(GetResourcesDir() + "media/icons/print.png"));
        actionsDropdown->AddItem("Settings", "settings", NormalizePath(GetResourcesDir() + "media/icons/settings.png"));
        actionsDropdown->SetSelectedIndex(0);

        auto actionsLine = AddDropdownLine(card2, "ActionsLine");
        DemoKit::AddFixed(actionsLine, actionsDropdown);
        DemoKit::Add(actionsLine, DemoKit::CreateText("ActionsLabel", "Actions with icon indicators"), 1);

        // ===== SECTION 3: COUNTRY FLAGS DROPDOWN =====
        auto row2 = DemoKit::AddRow(mainContainer, "DropdownRow2");
        auto card3 = DemoKit::AddCard(row2, "DropdownCountryCard", "Country Selection with Flags");

        // Countries dropdown with flags
        auto countriesDropdown = std::make_shared<UltraCanvasDropdown>("CountriesDropdown", 0, 0, 280, 30);

        // Add countries with their flag icons (ISO 3166-1 alpha-2 codes)
        countriesDropdown->AddItem("United States", "US", NormalizePath(GetResourcesDir() + "media/flags/US.png"));
        countriesDropdown->AddItem("United Kingdom", "GB", NormalizePath(GetResourcesDir() + "media/flags/GB.png"));
        countriesDropdown->AddItem("Germany", "DE", NormalizePath(GetResourcesDir() + "media/flags/DE.png"));
        countriesDropdown->AddItem("France", "FR", NormalizePath(GetResourcesDir() + "media/flags/FR.png"));
        countriesDropdown->AddItem("Italy", "IT", NormalizePath(GetResourcesDir() + "media/flags/IT.png"));
        countriesDropdown->AddItem("Spain", "ES", NormalizePath(GetResourcesDir() + "media/flags/ES.png"));
        countriesDropdown->AddItem("Japan", "JP", NormalizePath(GetResourcesDir() + "media/flags/JP.png"));
        countriesDropdown->AddItem("China", "CN", NormalizePath(GetResourcesDir() + "media/flags/CN.png"));
        countriesDropdown->AddItem("India", "IN", NormalizePath(GetResourcesDir() + "media/flags/IN.png"));
        countriesDropdown->AddItem("Brazil", "BR", NormalizePath(GetResourcesDir() + "media/flags/BR.png"));
        countriesDropdown->AddItem("Canada", "CA", NormalizePath(GetResourcesDir() + "media/flags/CA.png"));
        countriesDropdown->AddItem("Australia", "AU", NormalizePath(GetResourcesDir() + "media/flags/AU.png"));
        countriesDropdown->AddItem("Mexico", "MX", NormalizePath(GetResourcesDir() + "media/flags/MX.png"));
        countriesDropdown->AddItem("Russia", "RU", NormalizePath(GetResourcesDir() + "media/flags/RU.png"));
        countriesDropdown->AddItem("South Korea", "KR", NormalizePath(GetResourcesDir() + "media/flags/KR.png"));
        countriesDropdown->AddItem("Netherlands", "NL", NormalizePath(GetResourcesDir() + "media/flags/NL.png"));
        countriesDropdown->AddItem("Sweden", "SE", NormalizePath(GetResourcesDir() + "media/flags/SE.png"));
        countriesDropdown->AddItem("Switzerland", "CH", NormalizePath(GetResourcesDir() + "media/flags/CH.png"));
        countriesDropdown->AddItem("Poland", "PL", NormalizePath(GetResourcesDir() + "media/flags/PL.png"));
        countriesDropdown->AddItem("Belgium", "BE", NormalizePath(GetResourcesDir() + "media/flags/BE.png"));

        countriesDropdown->SetSelectedIndex(0);

        // Country selection status label
        auto countryStatusLabel = DemoKit::CreateText("CountryStatus", "Selected: United States (US)");
        countryStatusLabel->SetTextColor(Color(0, 100, 0, 255));

        // Callback for country selection
        countriesDropdown->onSelectionChanged = [countryStatusLabel](int index, const DropdownItem& item) {
            debugOutput << "Country selected: " << item.text << " (" << item.value << ")" << std::endl;
            countryStatusLabel->SetText("Selected: " + item.text + " (" + item.value + ")");
        };

        auto countryLine = AddDropdownLine(card3, "CountryLine");
        DemoKit::AddFixed(countryLine, countriesDropdown);
        DemoKit::Add(countryLine, countryStatusLabel, 1);

        // ===== SECTION 4: MULTI-SELECTION DROPDOWNS =====
        auto card4 = DemoKit::AddCard(row2, "DropdownMultiCard", "Multi-Selection Dropdowns");

        // Programming languages multi-select
        auto languagesDropdown = std::make_shared<UltraCanvasDropdown>("LanguagesDropdown", 0, 0, 250, 30);
        languagesDropdown->SetMultiSelectEnabled(true);
        languagesDropdown->AddItem("C++", "cpp", NormalizePath(GetResourcesDir() + "media/icons/cpp.png"));
        languagesDropdown->AddItem("Python", "py", NormalizePath(GetResourcesDir() + "media/icons/python.png"));
        languagesDropdown->AddItem("JavaScript", "js", NormalizePath(GetResourcesDir() + "media/icons/javascript.png"));
        languagesDropdown->AddItem("Java", "java", NormalizePath(GetResourcesDir() + "media/icons/java.png"));
        languagesDropdown->AddItem("C#", "cs", NormalizePath(GetResourcesDir() + "media/icons/csharp.png"));
        languagesDropdown->AddItem("Go", "go", NormalizePath(GetResourcesDir() + "media/icons/go.png"));

        // Pre-select some items
        languagesDropdown->SetItemSelected(0, true); // C++
        languagesDropdown->SetItemSelected(1, true); // Python

        auto languagesStatus = DemoKit::CreateText("LanguagesStatus", "Selected: C++, Python");
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
            debugOutput << "Programming languages selection changed: " << selectedText << std::endl;
        };

        auto languagesLine = AddDropdownLine(card4, "LanguagesLine");
        DemoKit::AddFixed(languagesLine, languagesDropdown);
        DemoKit::Add(languagesLine, languagesStatus, 1);

        // Fruit multi-select (no icons)
        auto fruitsDropdown = std::make_shared<UltraCanvasDropdown>("FruitsDropdown", 0, 0, 250, 30);
        fruitsDropdown->SetMultiSelectEnabled(true);
        fruitsDropdown->AddItem("Apple", "apple");
        fruitsDropdown->AddItem("Banana", "banana");
        fruitsDropdown->AddItem("Orange", "orange");
        fruitsDropdown->AddItem("Grape", "grape");
        fruitsDropdown->AddItem("Strawberry", "strawberry");
        fruitsDropdown->AddItem("Mango", "mango");
        fruitsDropdown->AddItem("Pineapple", "pineapple");
        fruitsDropdown->AddItem("Watermelon", "watermelon");

        auto fruitsLine = AddDropdownLine(card4, "FruitsLine");
        DemoKit::AddFixed(fruitsLine, fruitsDropdown);
        DemoKit::Add(fruitsLine, DemoKit::CreateText("FruitsLabel", "Multi-select fruits (with checkboxes)"), 1);

        // Control buttons for fruits dropdown (width 0: as wide as the text)
        auto selectAllBtn = std::make_shared<UltraCanvasButton>("SelectAllFruits", 0, 0, 0, 30);
        selectAllBtn->SetText("Select All");
        selectAllBtn->SetOnClick([fruitsDropdown]() {
            fruitsDropdown->SelectAll();
            debugOutput << "Selected all fruits" << std::endl;
        });

        auto deselectAllBtn = std::make_shared<UltraCanvasButton>("DeselectAllFruits", 0, 0, 0, 30);
        deselectAllBtn->SetText("Clear All");
        deselectAllBtn->SetOnClick([fruitsDropdown]() {
            fruitsDropdown->DeselectAll();
            debugOutput << "Cleared all fruit selections" << std::endl;
        });

        auto getSelectedBtn = std::make_shared<UltraCanvasButton>("GetSelectedFruits", 0, 0, 0, 30);
        getSelectedBtn->SetText("Get Selected");
        getSelectedBtn->SetOnClick([fruitsDropdown]() {
            auto selected = fruitsDropdown->GetSelectedItems();
            debugOutput << "Currently selected fruits (" << selected.size() << "): ";
            for (const auto& item : selected) {
                debugOutput << item.text << " ";
            }
            debugOutput << std::endl;
        });

        auto fruitButtons = DemoKit::AddRow(card4, "FruitButtons", 10);
        DemoKit::Add(fruitButtons, selectAllBtn);
        DemoKit::Add(fruitButtons, deselectAllBtn);
        DemoKit::Add(fruitButtons, getSelectedBtn);

        // ===== SECTION 5: MULTI-SELECT COUNTRIES WITH FLAGS =====
        auto row3 = DemoKit::AddRow(mainContainer, "DropdownRow3");
        auto card5 = DemoKit::AddCard(row3, "DropdownTravelCard", "Multi-Select Countries (Travel Destinations)");

        // Travel destinations multi-select with flags
        auto travelDropdown = std::make_shared<UltraCanvasDropdown>("TravelDropdown", 0, 0, 280, 30);
        travelDropdown->SetMultiSelectEnabled(true);
        travelDropdown->AddItem("France", "FR", NormalizePath(GetResourcesDir() + "media/flags/FR.png"));
        travelDropdown->AddItem("Italy", "IT", NormalizePath(GetResourcesDir() + "media/flags/IT.png"));
        travelDropdown->AddItem("Spain", "ES", NormalizePath(GetResourcesDir() + "media/flags/ES.png"));
        travelDropdown->AddItem("Japan", "JP", NormalizePath(GetResourcesDir() + "media/flags/JP.png"));
        travelDropdown->AddItem("Thailand", "TH", NormalizePath(GetResourcesDir() + "media/flags/TH.png"));
        travelDropdown->AddItem("United States", "US", NormalizePath(GetResourcesDir() + "media/flags/US.png"));
        travelDropdown->AddItem("United Kingdom", "GB", NormalizePath(GetResourcesDir() + "media/flags/GB.png"));
        travelDropdown->AddItem("Australia", "AU", NormalizePath(GetResourcesDir() + "media/flags/AU.png"));
        travelDropdown->AddItem("Greece", "GR", NormalizePath(GetResourcesDir() + "media/flags/GR.png"));
        travelDropdown->AddItem("Switzerland", "CH", NormalizePath(GetResourcesDir() + "media/flags/CH.png"));

        auto travelStatus = DemoKit::CreateText("TravelStatus", "Select your travel destinations");
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
            debugOutput << "Travel destinations: ";
            for (const auto& item : items) {
                debugOutput << item.text << " (" << item.value << ") ";
            }
            debugOutput << std::endl;
        };

        auto travelLine = AddDropdownLine(card5, "TravelLine");
        DemoKit::AddFixed(travelLine, travelDropdown);
        DemoKit::Add(travelLine, travelStatus, 1);

        // ===== SECTION 6: INTERACTIVE DEMO =====
        auto card6 = DemoKit::AddCard(row3, "DropdownInteractiveCard", "Interactive Demo");

        // Create interactive demo with event logging
        auto interactiveDropdown = std::make_shared<UltraCanvasDropdown>("InteractiveDropdown", 0, 0, 250, 30);
        interactiveDropdown->AddItem("Monitor Events", "monitor");
        interactiveDropdown->AddItem("Selection Changed", "selection");
        interactiveDropdown->AddItem("Item Hovered", "hover");
        interactiveDropdown->AddItem("Dropdown Opened", "opened");
        interactiveDropdown->AddItem("Dropdown Closed", "closed");
        interactiveDropdown->SetSelectedIndex(0);

        auto eventLog = DemoKit::CreateText("EventLog", "Event log: No events yet");
        eventLog->SetFontSize(11);
        eventLog->SetBackgroundColor(Color(245, 245, 245, 255));
        eventLog->SetBorders(1.0f);
        eventLog->SetBordersColor(Color(180, 180, 180, 255));
        eventLog->SetPadding(5.0f);

        // Set up all callbacks
        interactiveDropdown->onSelectionChanged = [eventLog](int index, const DropdownItem& item) {
            std::string log = "Selection Changed: " + item.text + " (index: " + std::to_string(index) + ")";
            eventLog->SetText(log);
            debugOutput << log << std::endl;
        };

        interactiveDropdown->onItemHovered = [eventLog](int index, const DropdownItem& item) {
            std::string log = "Item Hovered: " + item.text + " (index: " + std::to_string(index) + ")";
            eventLog->SetText(log);
        };

        interactiveDropdown->onDropdownOpened = [eventLog]() {
            std::string log = "Event: Dropdown Opened";
            eventLog->SetText(log);
            debugOutput << log << std::endl;
        };

        interactiveDropdown->onDropdownClosed = [eventLog]() {
            std::string log = "Event: Dropdown Closed";
            eventLog->SetText(log);
            debugOutput << log << std::endl;
        };

        // The log under the dropdown, as wide as the card.
        DemoKit::AddFixed(card6, interactiveDropdown);
        DemoKit::Add(card6, eventLog);

        debugOutput << "Dropdown examples created successfully" << std::endl;

        return mainContainer;
    }

} // namespace UltraCanvas