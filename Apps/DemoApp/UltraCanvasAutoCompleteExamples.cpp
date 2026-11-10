// Apps/DemoApp/UltraCanvasAutoCompleteExamples.cpp
// Auto-complete input field examples
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, status texts beside the fields sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasAutoComplete.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAutoCompleteExamples() {
        auto page = DemoKit::CreatePage("AutoCompleteExamples", Tr("UltraCanvas AutoComplete Component Examples"),
                Tr("AutoComplete examples showcasing static items, dynamic providers, scrollable lists, and event handling"));

        // A field keeps its width; its status texts sit in a column beside
        // it and move under it when the card is too narrow for both.
        auto fieldRow = [](const std::shared_ptr<UltraCanvasContainer>& card, const std::string& id,
                           const std::shared_ptr<UltraCanvasUIElement>& field) {
            auto row = DemoKit::AddRow(card, id + "Row");
            DemoKit::AddFixed(row, field);
            auto column = DemoKit::CreateColumn(id + "Info", 4);
            DemoKit::Add(row, column, 1);
            return column;
        };
        // A status line keeps its green: it reports what was picked.
        auto addStatus = [](const std::shared_ptr<UltraCanvasContainer>& column, const std::string& id,
                            const std::string& text) {
            auto label = DemoKit::AddText(column, id, text);
            label->SetTextColor(Color(0, 100, 0, 255));
            return label;
        };

        auto topRow = DemoKit::AddRow(page, "ACTopRow");

        // ===== SECTION 1: BASIC AUTOCOMPLETE =====
        auto card1 = DemoKit::AddCard(topRow, "ACCardBasic", Tr("Basic AutoComplete"));

        // Fruit autocomplete
        auto fruitAC = CreateAutoComplete("FruitAC", 0, 0, 250);
        fruitAC->SetPlaceholder(Tr("Type a fruit name..."));
        fruitAC->AddItem(Tr("Apple"), "apple");
        fruitAC->AddItem(Tr("Apricot"), "apricot");
        fruitAC->AddItem(Tr("Banana"), "banana");
        fruitAC->AddItem(Tr("Blueberry"), "blueberry");
        fruitAC->AddItem(Tr("Cherry"), "cherry");
        fruitAC->AddItem(Tr("Grape"), "grape");
        fruitAC->AddItem(Tr("Lemon"), "lemon");
        fruitAC->AddItem(Tr("Mango"), "mango");
        fruitAC->AddItem(Tr("Orange"), "orange");
        fruitAC->AddItem(Tr("Peach"), "peach");
        fruitAC->AddItem(Tr("Pear"), "pear");
        fruitAC->AddItem(Tr("Pineapple"), "pineapple");
        fruitAC->AddItem(Tr("Strawberry"), "strawberry");
        fruitAC->AddItem(Tr("Watermelon"), "watermelon");
        fruitAC->SetMinCharsToTrigger(0);

        auto fruitInfo = fieldRow(card1, "FruitAC", fruitAC);
        auto fruitStatusLabel = addStatus(fruitInfo, "FruitACStatus", Tr("Type to search fruits..."));

        fruitAC->onItemSelected = [fruitStatusLabel](int index, const AutoCompleteItem& item) {
            fruitStatusLabel->SetText(TrF("Selected: {0} (value: {1})", item.text, item.value));
            debugOutput << "Fruit selected: " << item.text << " (" << item.value << ")" << std::endl;
        };

        DemoKit::AddCaption(fruitInfo, "FruitACLabel", Tr("Static list of 14 fruits with substring matching"));

        // ===== SECTION 2: COUNTRY AUTOCOMPLETE =====
        auto card2 = DemoKit::AddCard(topRow, "ACCardCountry", Tr("Country Selection"));

        auto countryAC = CreateAutoComplete("CountryAC", 0, 0, 280);
        countryAC->SetPlaceholder(Tr("Search countries..."));
        countryAC->AddItem(Tr("Argentina"), "AR");
        countryAC->AddItem(Tr("Australia"), "AU");
        countryAC->AddItem(Tr("Austria"), "AT");
        countryAC->AddItem(Tr("Belgium"), "BE");
        countryAC->AddItem(Tr("Brazil"), "BR");
        countryAC->AddItem(Tr("Canada"), "CA");
        countryAC->AddItem(Tr("China"), "CN");
        countryAC->AddItem(Tr("Denmark"), "DK");
        countryAC->AddItem(Tr("Finland"), "FI");
        countryAC->AddItem(Tr("France"), "FR");
        countryAC->AddItem(Tr("Germany"), "DE");
        countryAC->AddItem(Tr("India"), "IN");
        countryAC->AddItem(Tr("Italy"), "IT");
        countryAC->AddItem(Tr("Japan"), "JP");
        countryAC->AddItem(Tr("Mexico"), "MX");
        countryAC->AddItem(Tr("Netherlands"), "NL");
        countryAC->AddItem(Tr("Norway"), "NO");
        countryAC->AddItem(Tr("Poland"), "PL");
        countryAC->AddItem(Tr("Russia"), "RU");
        countryAC->AddItem(Tr("South Korea"), "KR");
        countryAC->AddItem(Tr("Spain"), "ES");
        countryAC->AddItem(Tr("Sweden"), "SE");
        countryAC->AddItem(Tr("Switzerland"), "CH");
        countryAC->AddItem(Tr("United Kingdom"), "GB");
        countryAC->AddItem(Tr("United States"), "US");

        auto countryInfo = fieldRow(card2, "CountryAC", countryAC);
        auto countryStatusLabel = addStatus(countryInfo, "CountryACStatus", Tr("Type to search countries..."));

        countryAC->onItemSelected = [countryStatusLabel](int index, const AutoCompleteItem& item) {
            countryStatusLabel->SetText(TrF("Selected: {0} (code: {1})", item.text, item.value));
            debugOutput << "Country selected: " << item.text << " (" << item.value << ")" << std::endl;
        };

        auto middleRow = DemoKit::AddRow(page, "ACMiddleRow");

        // ===== SECTION 3: DYNAMIC PROVIDER =====
        auto card3 = DemoKit::AddCard(middleRow, "ACCardDynamic", Tr("Dynamic Suggestion Provider"));

        auto dynamicAC = CreateAutoComplete("DynamicAC", 0, 0, 280);
        dynamicAC->SetPlaceholder(Tr("Search programming topics..."));

        auto dynamicInfo = fieldRow(card3, "DynamicAC", dynamicAC);
        auto dynamicStatusLabel = addStatus(dynamicInfo, "DynamicACStatus",
                Tr("Uses onRequestSuggestions callback to generate results dynamically"));

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
            dynamicStatusLabel->SetText(TrF("Selected: {0} (key: {1})", item.text, item.value));
            debugOutput << "Dynamic item selected: " << item.text << std::endl;
        };

        // ===== SECTION 4: LARGE LIST WITH SCROLLBAR =====
        auto card4 = DemoKit::AddCard(middleRow, "ACCardStates", Tr("Large List (US States - 50 items)"));

        auto statesAC = CreateAutoComplete("StatesAC", 0, 0, 280);
        statesAC->SetPlaceholder(Tr("Search US states..."));

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

        auto statesInfo = fieldRow(card4, "StatesAC", statesAC);
        auto statesStatusLabel = addStatus(statesInfo, "StatesACStatus", Tr("50 items - scrollbar appears automatically"));

        statesAC->onItemSelected = [statesStatusLabel](int index, const AutoCompleteItem& item) {
            statesStatusLabel->SetText(TrF("Selected: {0} ({1})", item.text, item.value));
            debugOutput << "State selected: " << item.text << " (" << item.value << ")" << std::endl;
        };

        // ===== SECTION 5: INTERACTIVE DEMO =====
        auto card5 = DemoKit::AddCard(page, "ACCardInteractive", Tr("Interactive Event Demo"));

        auto interactiveAC = CreateAutoComplete("InteractiveAC", 0, 0, 250);
        interactiveAC->SetPlaceholder(Tr("Type anything..."));
        interactiveAC->AddItem("Alpha", "a");
        interactiveAC->AddItem("Beta", "b");
        interactiveAC->AddItem("Gamma", "g");
        interactiveAC->AddItem("Delta", "d");
        interactiveAC->AddItem("Epsilon", "e");
        interactiveAC->AddItem("Zeta", "z");
        interactiveAC->AddItem("Eta", "h");
        interactiveAC->AddItem("Theta", "t");

        auto eventInfo = fieldRow(card5, "InteractiveAC", interactiveAC);
        auto eventLog = DemoKit::AddText(eventInfo, "ACEventLog", Tr("Event log: No events yet"));
        eventLog->SetFontSize(11);
        eventLog->SetBackgroundColor(Color(245, 245, 245, 255));
        eventLog->SetBorders(1.0f);
        eventLog->SetBordersColor(Color(180, 180, 180, 255));
        eventLog->SetPadding(5.0f);

        interactiveAC->onTextChanged = [eventLog](const std::string& text) {
            eventLog->SetText("onTextChanged: \"" + text + "\"");
        };

        interactiveAC->onItemSelected = [eventLog](int index, const AutoCompleteItem& item) {
            std::string log = TrF("onItemSelected: {0} (value: {1}, index: {2})", item.text, item.value, index);
            eventLog->SetText(log);
            debugOutput << log << std::endl;
        };

        interactiveAC->onPopupOpened = [eventLog]() {
            eventLog->SetText("onPopupOpened");
            debugOutput << "AutoComplete popup opened" << std::endl;
        };

        interactiveAC->onAutocompletePopupClosed = [eventLog]() {
            eventLog->SetText("onPopupClosed");
            debugOutput << "AutoComplete popup closed" << std::endl;
        };

        debugOutput << "AutoComplete examples created successfully" << std::endl;

        return page;
    }

} // namespace UltraCanvas
