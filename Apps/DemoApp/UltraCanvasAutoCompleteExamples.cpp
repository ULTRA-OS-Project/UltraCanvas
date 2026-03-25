// Apps/DemoApp/UltraCanvasAutoCompleteExamples.cpp
// Auto-complete input field examples
// Version: 1.0.0
// Last Modified: 2026-03-22
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasAutoComplete.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// Helper function to create section separator
    std::shared_ptr<UltraCanvasContainer> CreateAutoCompleteSeparator(long id, long x, long y, long width) {
        auto separator = std::make_shared<UltraCanvasContainer>("ACSep" + std::to_string(id), id, x, y, width, 2);
        separator->SetBackgroundColor(Color(200, 200, 200, 255));
        return separator;
    }

// Helper function to create section title
    std::shared_ptr<UltraCanvasLabel> CreateAutoCompleteSectionTitle(long id, long x, long y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("ACSecTitle" + std::to_string(id), id, x, y, 400, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAutoCompleteExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("AutoCompleteExamples", 800, 0, 0, 1020, 1400);
        mainContainer->SetBackgroundColor(Colors::White);
        mainContainer->SetPadding(0, 0, 10, 0);

        long currentY = 10;

        // ===== MAIN TITLE =====
        auto mainTitle = std::make_shared<UltraCanvasLabel>("ACMainTitle", 801, 20, currentY, 900, 30);
        mainTitle->SetText("UltraCanvas AutoComplete Component Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);

        currentY += 40;

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("ACDesc", 802, 20, currentY, 960, 40);
        description->SetText("AutoComplete examples showcasing static items, dynamic providers, scrollable lists, and event handling");
        description->SetWordWrap(true);
        description->SetTextColor(Color(80, 80, 80, 255));
        mainContainer->AddChild(description);

        currentY += 30;

        // ===== SECTION 1: BASIC AUTOCOMPLETE =====
        mainContainer->AddChild(CreateAutoCompleteSectionTitle(810, 20, currentY, "Basic AutoComplete"));
        currentY += 35;

        // Fruit autocomplete
        auto fruitAC = CreateAutoComplete("FruitAC", 811, 30, currentY, 250);
        fruitAC->SetPlaceholder("Type a fruit name...");
        fruitAC->AddItem("Apple", "apple");
        fruitAC->AddItem("Apricot", "apricot");
        fruitAC->AddItem("Banana", "banana");
        fruitAC->AddItem("Blueberry", "blueberry");
        fruitAC->AddItem("Cherry", "cherry");
        fruitAC->AddItem("Grape", "grape");
        fruitAC->AddItem("Lemon", "lemon");
        fruitAC->AddItem("Mango", "mango");
        fruitAC->AddItem("Orange", "orange");
        fruitAC->AddItem("Peach", "peach");
        fruitAC->AddItem("Pear", "pear");
        fruitAC->AddItem("Pineapple", "pineapple");
        fruitAC->AddItem("Strawberry", "strawberry");
        fruitAC->AddItem("Watermelon", "watermelon");
        fruitAC->SetMinCharsToTrigger(0);

        auto fruitStatusLabel = std::make_shared<UltraCanvasLabel>("FruitACStatus", 812, 300, currentY + 5, 400, 20);
        fruitStatusLabel->SetText("Type to search fruits...");
        fruitStatusLabel->SetFontSize(12);
        fruitStatusLabel->SetTextColor(Color(0, 100, 0, 255));

        fruitAC->onItemSelected = [fruitStatusLabel](int index, const AutoCompleteItem& item) {
            fruitStatusLabel->SetText("Selected: " + item.text + " (value: " + item.value + ")");
            debugOutput << "Fruit selected: " << item.text << " (" << item.value << ")" << std::endl;
        };

        auto fruitLabel = std::make_shared<UltraCanvasLabel>("FruitACLabel", 813, 300, currentY + 25, 400, 20);
        fruitLabel->SetText("Static list of 14 fruits with substring matching");
        fruitLabel->SetFontSize(11);
        fruitLabel->SetTextColor(Color(100, 100, 100, 255));

        mainContainer->AddChild(fruitAC);
        mainContainer->AddChild(fruitStatusLabel);
        mainContainer->AddChild(fruitLabel);

        currentY += 55;
        mainContainer->AddChild(CreateAutoCompleteSeparator(814, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 2: COUNTRY AUTOCOMPLETE =====
        mainContainer->AddChild(CreateAutoCompleteSectionTitle(820, 20, currentY, "Country Selection"));
        currentY += 35;

        auto countryAC = CreateAutoComplete("CountryAC", 821, 30, currentY, 280);
        countryAC->SetPlaceholder("Search countries...");
        countryAC->AddItem("Argentina", "AR");
        countryAC->AddItem("Australia", "AU");
        countryAC->AddItem("Austria", "AT");
        countryAC->AddItem("Belgium", "BE");
        countryAC->AddItem("Brazil", "BR");
        countryAC->AddItem("Canada", "CA");
        countryAC->AddItem("China", "CN");
        countryAC->AddItem("Denmark", "DK");
        countryAC->AddItem("Finland", "FI");
        countryAC->AddItem("France", "FR");
        countryAC->AddItem("Germany", "DE");
        countryAC->AddItem("India", "IN");
        countryAC->AddItem("Italy", "IT");
        countryAC->AddItem("Japan", "JP");
        countryAC->AddItem("Mexico", "MX");
        countryAC->AddItem("Netherlands", "NL");
        countryAC->AddItem("Norway", "NO");
        countryAC->AddItem("Poland", "PL");
        countryAC->AddItem("Russia", "RU");
        countryAC->AddItem("South Korea", "KR");
        countryAC->AddItem("Spain", "ES");
        countryAC->AddItem("Sweden", "SE");
        countryAC->AddItem("Switzerland", "CH");
        countryAC->AddItem("United Kingdom", "GB");
        countryAC->AddItem("United States", "US");

        auto countryStatusLabel = std::make_shared<UltraCanvasLabel>("CountryACStatus", 822, 330, currentY + 5, 400, 20);
        countryStatusLabel->SetText("Type to search countries...");
        countryStatusLabel->SetFontSize(12);
        countryStatusLabel->SetTextColor(Color(0, 100, 0, 255));

        countryAC->onItemSelected = [countryStatusLabel](int index, const AutoCompleteItem& item) {
            countryStatusLabel->SetText("Selected: " + item.text + " (code: " + item.value + ")");
            debugOutput << "Country selected: " << item.text << " (" << item.value << ")" << std::endl;
        };

        mainContainer->AddChild(countryAC);
        mainContainer->AddChild(countryStatusLabel);

        currentY += 55;
        mainContainer->AddChild(CreateAutoCompleteSeparator(823, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 3: DYNAMIC PROVIDER =====
        mainContainer->AddChild(CreateAutoCompleteSectionTitle(830, 20, currentY, "Dynamic Suggestion Provider"));
        currentY += 35;

        auto dynamicAC = CreateAutoComplete("DynamicAC", 831, 30, currentY, 280);
        dynamicAC->SetPlaceholder("Search programming topics...");

        auto dynamicStatusLabel = std::make_shared<UltraCanvasLabel>("DynamicACStatus", 832, 330, currentY + 5, 500, 40);
        dynamicStatusLabel->SetText("Uses onRequestSuggestions callback to generate results dynamically");
        dynamicStatusLabel->SetWordWrap(true);
        dynamicStatusLabel->SetFontSize(12);
        dynamicStatusLabel->SetTextColor(Color(0, 100, 0, 255));

        // Dynamic provider: returns suggestions based on query
        dynamicAC->onRequestSuggestions = [](const std::string& query) -> std::vector<AutoCompleteItem> {
            // Full list of programming languages and frameworks
            static const std::vector<std::pair<std::string, std::string>> allTopics = {
                {"C++", "cpp"}, {"C#", "csharp"}, {"C", "c"},
                {"CSS", "css"}, {"Cairo Graphics", "cairo"},
                {"Java", "java"}, {"JavaScript", "javascript"},
                {"Python", "python"}, {"PyTorch", "pytorch"},
                {"PHP", "php"},
                {"Ruby", "ruby"}, {"Rust", "rust"},
                {"React", "react"}, {"React Native", "react-native"},
                {"Go", "go"}, {"Golang", "golang"},
                {"Swift", "swift"}, {"SwiftUI", "swiftui"},
                {"Kotlin", "kotlin"},
                {"TypeScript", "typescript"},
                {"Vue.js", "vuejs"}, {"Angular", "angular"},
                {"Node.js", "nodejs"}, {"Next.js", "nextjs"},
                {"Django", "django"}, {"Flask", "flask"},
                {"Spring Boot", "spring-boot"},
                {"Docker", "docker"}, {"Kubernetes", "kubernetes"},
                {"PostgreSQL", "postgresql"}, {"MySQL", "mysql"},
                {"MongoDB", "mongodb"}, {"Redis", "redis"},
                {"GraphQL", "graphql"}, {"REST API", "rest-api"},
                {"Git", "git"}, {"GitHub", "github"},
                {"Linux", "linux"}, {"Lua", "lua"},
            };

            if (query.empty()) return {};

            std::vector<AutoCompleteItem> results;
            std::string lowerQuery;
            for (char c : query) lowerQuery += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            for (const auto& [text, value] : allTopics) {
                std::string lowerText;
                for (char c : text) lowerText += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lowerText.find(lowerQuery) != std::string::npos) {
                    results.emplace_back(text, value);
                }
            }
            return results;
        };

        dynamicAC->onItemSelected = [dynamicStatusLabel](int index, const AutoCompleteItem& item) {
            dynamicStatusLabel->SetText("Selected: " + item.text + " (key: " + item.value + ")");
            debugOutput << "Dynamic item selected: " << item.text << std::endl;
        };

        mainContainer->AddChild(dynamicAC);
        mainContainer->AddChild(dynamicStatusLabel);

        currentY += 65;
        mainContainer->AddChild(CreateAutoCompleteSeparator(833, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 4: LARGE LIST WITH SCROLLBAR =====
        mainContainer->AddChild(CreateAutoCompleteSectionTitle(840, 20, currentY, "Large List (US States - 50 items)"));
        currentY += 35;

        auto statesAC = CreateAutoComplete("StatesAC", 841, 30, currentY, 280);
        statesAC->SetPlaceholder("Search US states...");

        // All 50 US states
        statesAC->AddItem("Alabama", "AL");
        statesAC->AddItem("Alaska", "AK");
        statesAC->AddItem("Arizona", "AZ");
        statesAC->AddItem("Arkansas", "AR");
        statesAC->AddItem("California", "CA");
        statesAC->AddItem("Colorado", "CO");
        statesAC->AddItem("Connecticut", "CT");
        statesAC->AddItem("Delaware", "DE");
        statesAC->AddItem("Florida", "FL");
        statesAC->AddItem("Georgia", "GA");
        statesAC->AddItem("Hawaii", "HI");
        statesAC->AddItem("Idaho", "ID");
        statesAC->AddItem("Illinois", "IL");
        statesAC->AddItem("Indiana", "IN");
        statesAC->AddItem("Iowa", "IA");
        statesAC->AddItem("Kansas", "KS");
        statesAC->AddItem("Kentucky", "KY");
        statesAC->AddItem("Louisiana", "LA");
        statesAC->AddItem("Maine", "ME");
        statesAC->AddItem("Maryland", "MD");
        statesAC->AddItem("Massachusetts", "MA");
        statesAC->AddItem("Michigan", "MI");
        statesAC->AddItem("Minnesota", "MN");
        statesAC->AddItem("Mississippi", "MS");
        statesAC->AddItem("Missouri", "MO");
        statesAC->AddItem("Montana", "MT");
        statesAC->AddItem("Nebraska", "NE");
        statesAC->AddItem("Nevada", "NV");
        statesAC->AddItem("New Hampshire", "NH");
        statesAC->AddItem("New Jersey", "NJ");
        statesAC->AddItem("New Mexico", "NM");
        statesAC->AddItem("New York", "NY");
        statesAC->AddItem("North Carolina", "NC");
        statesAC->AddItem("North Dakota", "ND");
        statesAC->AddItem("Ohio", "OH");
        statesAC->AddItem("Oklahoma", "OK");
        statesAC->AddItem("Oregon", "OR");
        statesAC->AddItem("Pennsylvania", "PA");
        statesAC->AddItem("Rhode Island", "RI");
        statesAC->AddItem("South Carolina", "SC");
        statesAC->AddItem("South Dakota", "SD");
        statesAC->AddItem("Tennessee", "TN");
        statesAC->AddItem("Texas", "TX");
        statesAC->AddItem("Utah", "UT");
        statesAC->AddItem("Vermont", "VT");
        statesAC->AddItem("Virginia", "VA");
        statesAC->AddItem("Washington", "WA");
        statesAC->AddItem("West Virginia", "WV");
        statesAC->AddItem("Wisconsin", "WI");
        statesAC->AddItem("Wyoming", "WY");

        auto statesStatusLabel = std::make_shared<UltraCanvasLabel>("StatesACStatus", 842, 330, currentY + 5, 400, 20);
        statesStatusLabel->SetText("50 items - scrollbar appears automatically");
        statesStatusLabel->SetFontSize(12);
        statesStatusLabel->SetTextColor(Color(0, 100, 0, 255));

        statesAC->onItemSelected = [statesStatusLabel](int index, const AutoCompleteItem& item) {
            statesStatusLabel->SetText("Selected: " + item.text + " (" + item.value + ")");
            debugOutput << "State selected: " << item.text << " (" << item.value << ")" << std::endl;
        };

        mainContainer->AddChild(statesAC);
        mainContainer->AddChild(statesStatusLabel);

        currentY += 55;
        mainContainer->AddChild(CreateAutoCompleteSeparator(843, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 5: INTERACTIVE DEMO =====
        mainContainer->AddChild(CreateAutoCompleteSectionTitle(850, 20, currentY, "Interactive Event Demo"));
        currentY += 35;

        auto interactiveAC = CreateAutoComplete("InteractiveAC", 851, 30, currentY, 250);
        interactiveAC->SetPlaceholder("Type anything...");
        interactiveAC->AddItem("Alpha", "a");
        interactiveAC->AddItem("Beta", "b");
        interactiveAC->AddItem("Gamma", "g");
        interactiveAC->AddItem("Delta", "d");
        interactiveAC->AddItem("Epsilon", "e");
        interactiveAC->AddItem("Zeta", "z");
        interactiveAC->AddItem("Eta", "h");
        interactiveAC->AddItem("Theta", "t");

        auto eventLog = std::make_shared<UltraCanvasLabel>("ACEventLog", 852, 300, currentY, 500, 80);
        eventLog->SetText("Event log: No events yet");
        eventLog->SetWordWrap(true);
        eventLog->SetFontSize(11);
        eventLog->SetBackgroundColor(Color(245, 245, 245, 255));
        eventLog->SetBorders(1.0f);
        eventLog->SetBordersColor(Color(180, 180, 180, 255));
        eventLog->SetPadding(5.0f);

        interactiveAC->onTextChanged = [eventLog](const std::string& text) {
            eventLog->SetText("onTextChanged: \"" + text + "\"");
        };

        interactiveAC->onItemSelected = [eventLog](int index, const AutoCompleteItem& item) {
            std::string log = "onItemSelected: " + item.text + " (value: " + item.value + ", index: " + std::to_string(index) + ")";
            eventLog->SetText(log);
            debugOutput << log << std::endl;
        };

        interactiveAC->onPopupOpened = [eventLog]() {
            eventLog->SetText("onPopupOpened");
            debugOutput << "AutoComplete popup opened" << std::endl;
        };

        interactiveAC->onPopupClosed = [eventLog]() {
            eventLog->SetText("onPopupClosed");
            debugOutput << "AutoComplete popup closed" << std::endl;
        };

        mainContainer->AddChild(interactiveAC);
        mainContainer->AddChild(eventLog);

        currentY += 95;

        debugOutput << "AutoComplete examples created successfully with " << (currentY + 60) << " pixels height" << std::endl;

        return mainContainer;
    }

} // namespace UltraCanvas
