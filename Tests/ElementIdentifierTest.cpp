// Tests/ElementIdentifierTest.cpp
// One string is an element's identifier.
//
// UltraCanvasLabel's one-string constructor took the label's TEXT until
// 2026-11, where UltraCanvasContainer's - and 38 of the 42 elements' - takes
// the identifier. It read like the others and meant the opposite: eleven
// callers in the repository had handed it a name (the dialog's severity icon,
// Texter's status bar, the HTML builder, LabelBuilder) and one a text. This
// pins the rule as it is now - UltraCanvasLabel(id), CreateLabel(id) and
// LabelBuilder(id) name the label and leave its text empty, the text goes
// second; the checkbox, the radio button and the switch, which were built
// like the label, the same - and the line the framework writes to debugOutput
// when a one-string element is handed words, which no constructor can refuse
// and which scripts/check_label_identifiers.py only sees in a literal.
//
// No window and no display: elements are constructed and asked what they are.
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSwitch.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

// How many lines of the debug log say that an identifier reads as text.
int Warnings(const std::filesystem::path& log) {
    std::ifstream in(log);
    int count = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.find("one string is the element's identifier") != std::string::npos) ++count;
    }
    return count;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Element Identifier Suite"               << std::endl;
    std::cerr << "========================================" << std::endl;

    std::cerr << "\n--- A label built from one string ---" << std::endl;
    {
        UltraCanvasLabel label("CountLabel");
        TEST("The string is its identifier", label.GetIdentifier() == "CountLabel");
        TEST("and it has no text", label.GetText().empty());
        label.SetText("3 of 12");
        TEST("until it is given one", label.GetText() == "3 of 12" && label.GetIdentifier() == "CountLabel");

        auto shared = std::make_shared<UltraCanvasLabel>("StatusBar");
        TEST("The same through make_shared", shared->GetIdentifier() == "StatusBar" && shared->GetText().empty());

        // As the container beside it, which is what the label is read like.
        UltraCanvasContainer container("ContentSection");
        TEST("as a container's one string is", container.GetIdentifier() == "ContentSection");
    }

    std::cerr << "\n--- The text goes second ---" << std::endl;
    {
        UltraCanvasLabel named("InfoTitle", "About");
        TEST("An identifier and a text", named.GetIdentifier() == "InfoTitle" && named.GetText() == "About");
        UltraCanvasLabel anonymous("", "Select a repository");
        TEST("A text and no name", anonymous.GetIdentifier().empty() && anonymous.GetText() == "Select a repository");
        UltraCanvasLabel sized("Sized", 120, 20, "Ready");
        TEST("With a size", sized.GetIdentifier() == "Sized" && sized.GetText() == "Ready");
        UltraCanvasLabel empty;
        TEST("No argument: neither", empty.GetIdentifier().empty() && empty.GetText().empty());
    }

    std::cerr << "\n--- The factory and the builder ---" << std::endl;
    {
        auto label = CreateLabel("greeting");
        TEST("CreateLabel(one string) names the label", label->GetIdentifier() == "greeting" && label->GetText().empty());
        auto both = CreateLabel("greeting", "Hello");
        TEST("CreateLabel(identifier, text)", both->GetIdentifier() == "greeting" && both->GetText() == "Hello");
        // The builder handed its identifier to the one-string factory, and so
        // built a label that was called nothing and said "greeting".
        auto built = LabelBuilder("greeting").Build();
        TEST("LabelBuilder(identifier) names the label", built->GetIdentifier() == "greeting");
        TEST("and gives it no text", built->GetText().empty());
        auto said = LabelBuilder("greeting").SetText("Hello").Build();
        TEST("SetText is where its text comes from", said->GetIdentifier() == "greeting" && said->GetText() == "Hello");
    }

    std::cerr << "\n--- The checkbox, the radio button and the switch ---" << std::endl;
    {
        UltraCanvasCheckbox box("WrapLines");
        TEST("A checkbox's one string is its identifier", box.GetIdentifier() == "WrapLines" && box.GetText().empty());
        UltraCanvasCheckbox labelled("WrapLines", "Wrap long lines");
        TEST("and its text goes second", labelled.GetIdentifier() == "WrapLines" && labelled.GetText() == "Wrap long lines");

        UltraCanvasRadio radio("SizeLarge");
        TEST("A radio button's one string is its identifier", radio.GetIdentifier() == "SizeLarge" && radio.GetText().empty());
        UltraCanvasRadio large("SizeLarge", "Large");
        TEST("and its text goes second", large.GetIdentifier() == "SizeLarge" && large.GetText() == "Large");

        UltraCanvasSwitch toggle("DarkMode");
        TEST("A switch's one string is its identifier", toggle.GetIdentifier() == "DarkMode" && toggle.GetText().empty());
        UltraCanvasSwitch dark("DarkMode", "Dark mode");
        TEST("and its text goes second", dark.GetIdentifier() == "DarkMode" && dark.GetText() == "Dark mode");

        UltraCanvasCheckbox none;
        TEST("No argument: neither", none.GetIdentifier().empty() && none.GetText().empty());
    }

    std::cerr << "\n--- Words handed to a one-string element ---" << std::endl;
    {
        const std::filesystem::path log =
            std::filesystem::temp_directory_path() / "ultracanvas-element-identifier-test.log";
        std::error_code ec;
        std::filesystem::remove(log, ec);
        SetDebugOutputFile(PathToUtf8(log));
        SetDebugOutputEnabled(true);

        UltraCanvasLabel named("CountLabel");
        UltraCanvasLabel dotted("uai-hf-status");
        UltraCanvasLabel anonymous("", "Two words");
        debugOutput << std::flush;
        TEST("A name says nothing", Warnings(log) == 0);

        // label-identifier-ok: words on purpose, to see them reported
        UltraCanvasLabel words("Select a repository");
        debugOutput << std::flush;
        TEST("Words are reported on debugOutput", Warnings(log) == 1);
        TEST("and are the identifier all the same", words.GetIdentifier() == "Select a repository" && words.GetText().empty());

        // label-identifier-ok: words on purpose, through the factory
        auto made = CreateLabel("Two words");
        debugOutput << std::flush;
        TEST("Through the factory too", Warnings(log) == 2);

        // label-identifier-ok: words on purpose, to a checkbox
        UltraCanvasCheckbox box("Wrap long lines");
        debugOutput << std::flush;
        TEST("And for a checkbox", Warnings(log) == 3);

        // A list built in a loop must not write ten thousand lines.
        for (int i = 0; i < 100; ++i) UltraCanvasLabel row("Row " + std::to_string(i));
        debugOutput << std::flush;
        TEST("Twenty times at most", Warnings(log) == 20);

        SetDebugOutputEnabled(false);
        SetDebugOutputFile("");
        std::filesystem::remove(log, ec);
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
