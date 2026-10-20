// Tests/UltraFIBU/UltraFIBUStartTests.cpp
// The start window's page without a window or a display: the hint it was
// given, the files opened last as buttons, a gone file said and dropped,
// an existing one handed over, the setup form refusing until a company and
// a first of a month are in, and the page's height. Built where the UI
// library is (unlike the engine suite), so it runs on the Linux CI build
// and on a developer's machine, with no display.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUStart.h"

#include "UltraFIBUEinrichtung.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraFIBU;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

template <typename T>
static std::vector<std::shared_ptr<T>> ChildrenOf(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& page) {
    std::vector<std::shared_ptr<T>> found;
    for (const auto& child : page->GetChildren()) {
        if (auto typed = std::dynamic_pointer_cast<T>(child)) found.push_back(typed);
    }
    return found;
}

static std::shared_ptr<UltraCanvas::UltraCanvasButton> ButtonWith(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& page,
                                                                  const std::string& part) {
    for (const auto& button : ChildrenOf<UltraCanvas::UltraCanvasButton>(page)) {
        if (button->GetText().find(part) != std::string::npos) return button;
    }
    return nullptr;
}

static std::shared_ptr<UltraCanvas::UltraCanvasLabel> StatusOf(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& page) {
    for (const auto& label : ChildrenOf<UltraCanvas::UltraCanvasLabel>(page)) {
        if (label->GetIdentifier() == "start.status") return label;
    }
    return nullptr;
}

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("ultrafibu-start-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);

    // A bookkeeping, set up the way the form would: the recent list's good entry.
    EinrichtungsDaten daten;
    daten.firma = "Erika Muster e.K.";
    daten.gjBeginn = Date(2026, 1, 1);
    const std::string buch = UltraCanvas::PathToUtf8(dir / "buch.db");
    const EinrichtungsBericht bericht = RichteBuchhaltungEin(buch, daten);
    Check(bericht.ok, "a bookkeeping is set up for the list: " + bericht.fehler);
    const std::string weg = UltraCanvas::PathToUtf8(dir / "weg.db");

    std::printf("The page\n");
    StartFenster start;
    std::vector<std::string> uebergeben, vergessen;
    start.onDateiGewaehlt = [&uebergeben](const std::string& pfad) { uebergeben.push_back(pfad); return std::string(); };
    start.onDateiVergessen = [&vergessen](const std::string& pfad) { vergessen.push_back(pfad); };
    const std::string hinweis = "Es ist noch keine Buchhaltung geöffnet.";
    auto seite = start.SeiteBauen(hinweis, "", {buch, weg});
    Check(seite != nullptr && seite->Width() == 560, "the page is built, 560 wide");
    Check(seite != nullptr && seite->PreferredHeight() > 400 && seite->PreferredHeight() < 800,
          "and about a window tall: " + std::to_string(seite ? seite->PreferredHeight() : 0));
    bool titel = false, hint = false, zuletzt = false, neu = false;
    for (const auto& label : ChildrenOf<UltraCanvas::UltraCanvasLabel>(seite)) {
        if (label->GetText() == "UltraFIBU - Buchhaltung") titel = true;
        if (label->GetText() == hinweis) hint = true;
        if (label->GetText() == "Zuletzt geöffnet") zuletzt = true;
        if (label->GetText() == "Neue Buchhaltung anlegen") neu = true;
    }
    Check(titel && hint && zuletzt && neu, "the title, the hint and both headings are on it");
    Check(ButtonWith(seite, "Bestehende Buchhaltung öffnen") != nullptr, "and the open button");

    std::printf("The files opened last\n");
    auto gut = ButtonWith(seite, "buch.db");
    auto fort = ButtonWith(seite, "weg.db");
    Check(gut != nullptr && fort != nullptr && gut->GetTooltip() == buch && fort->GetTooltip() == weg,
          "both entries are buttons, the whole path as tooltip");
    if (gut && gut->onClick) gut->onClick();
    Check(uebergeben == std::vector<std::string>{buch} && vergessen.empty(), "the one that exists is handed over");
    if (fort && fort->onClick) fort->onClick();
    Check(uebergeben.size() == 1 && vergessen == std::vector<std::string>{weg}, "the one that is gone is dropped");
    auto status = StatusOf(seite);
    Check(status != nullptr && status->GetText().find("gibt es nicht mehr") != std::string::npos, "and said so");

    std::printf("The setup form\n");
    Check(start.Grund() == "Der Firmenname fehlt." &&
              start.AnlegenKnopf() != nullptr && start.AnlegenKnopf()->IsDisabled(),
          "without a company the button is greyed and the reason says so: " + start.Grund());
    Check(start.Firma() != nullptr && start.Beginn() != nullptr, "the fields are reachable");
    if (start.Firma()) {
        start.Firma()->SetText("Erika Muster e.K.");
        if (start.Firma()->onTextChanged) start.Firma()->onTextChanged(start.Firma()->GetText());
    }
    Check(start.Grund().find("Geschäftsjahr") != std::string::npos && start.AnlegenKnopf()->IsDisabled(),
          "with a company the date is what is missing: " + start.Grund());
    if (start.Beginn()) {
        start.Beginn()->SetSelectedDate(UltraCanvas::UCDate(2026, 1, 1), false);
        if (start.Beginn()->onDateChanged) start.Beginn()->onDateChanged(start.Beginn()->GetSelectedDate());
    }
    Check(start.Grund().empty() && !start.AnlegenKnopf()->IsDisabled(), "with a first of a month the form can be submitted");
    Check(start.Daten().firma == "Erika Muster e.K." && start.Daten().gjBeginn == Date(2026, 1, 1) && start.Daten().skr == "SKR03",
          "and the data is what was typed, SKR03 by default");

    std::printf("A page with nothing opened yet\n");
    StartFenster leer;
    auto leereSeite = leer.SeiteBauen("Die Datei \"x.db\" gibt es nicht.", "x.db");
    Check(leereSeite != nullptr && ButtonWith(leereSeite, "x.db") == nullptr &&
              leereSeite->PreferredHeight() < seite->PreferredHeight(),
          "no recent list, and the page is shorter for it");

    fs::remove_all(dir, ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
