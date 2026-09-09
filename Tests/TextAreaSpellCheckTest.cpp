// TextAreaSpellCheckTest.cpp
// Runtime test for UltraCanvasTextArea spell check and character-range geometry
// Version: 1.0.0
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// Unlike SpellCheckerTest (which is framework-independent), this one needs a
// real window and render context: the whole point is that the mapping from a
// document byte offset to an on-screen rectangle goes through the live line
// layouts, scroll offsets and gutter width.
//
// Runs headless under Xvfb. Skips - rather than fails - when no display or no
// dictionary is available, so it stays usable on a bare CI machine.

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasSpellChecker.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;
static int skipCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl;  \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;      \
        return 0;                                                             \
    } while (0)

namespace {

// Drives frames until `done` holds, or the budget runs out. Results are drained
// during Render, so the frames have to actually happen - and the predicate is
// evaluated only after at least one frame, since the errors still on hand at
// entry describe the previous text.
// In a real application the element is repainted because the spell service
// posts a redraw through UltraCanvasApplication::PostToUIThread, which the
// event loop drains. This harness drives frames by hand and never runs that
// loop, so it marks the element dirty itself - what is under test here is the
// spell pipeline, not the dirty-rect plumbing.
bool PumpUntil(const std::shared_ptr<UltraCanvasWindow>& window,
               const std::shared_ptr<UltraCanvasTextArea>& area,
               const std::function<bool()>& done,
               int maxFrames = 300) {
    for (int frame = 0; frame < maxFrames; ++frame) {
        area->RequestRedraw();
        window->UpdateAndRender();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (done()) return true;
    }
    return done();
}

bool PumpUntilSpellErrors(const std::shared_ptr<UltraCanvasWindow>& window,
                          const std::shared_ptr<UltraCanvasTextArea>& area,
                          int maxFrames = 300) {
    return PumpUntil(window, area, [&area]() { return !area->GetSpellErrors().empty(); },
                     maxFrames);
}

const SpellError* FindError(const std::vector<SpellError>& errors,
                            const std::string& word) {
    for (const SpellError& error : errors) {
        if (error.word == word) return &error;
    }
    return nullptr;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Spell Check Runtime Suite"   << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaSpellCheckTest")) SKIP_ALL("application would not initialise");

    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();
    if (!service.Initialize()) SKIP_ALL("spell service would not initialise");
    std::cerr << "   backend: " << service.GetBackendName() << std::endl;

    bool haveDictionary = false;
    for (const SpellLanguageInfo& info : service.GetAvailableLanguages()) {
        if (info.isAvailable && info.code.rfind("en", 0) == 0 &&
            service.SetLanguage(info.code)) {
            std::cerr << "   language: " << info.code << std::endl;
            haveDictionary = true;
            break;
        }
    }
    if (!haveDictionary) SKIP_ALL("no English dictionary installed");
    service.SetMode(SpellCheckMode::AsYouType);

    WindowConfig cfg;
    cfg.title = "TextAreaSpellCheckTest";
    cfg.width = 800;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");

    // UpdateAndRender() is a no-op on an unmapped window, and every check here
    // depends on frames actually being drawn.
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("SpellArea", 0, 0, 780, 560);
    window->AddChild(area);

    // ===== ENABLE / DISABLE =====
    std::cerr << "\n--- Enable and disable ---" << std::endl;
    TEST("Spell check starts disabled", !area->IsSpellCheckEnabled());
    area->SetSpellCheckEnabled(true);
    TEST("SetSpellCheckEnabled(true) sticks", area->IsSpellCheckEnabled());

    // ===== ERRORS APPEAR =====
    std::cerr << "\n--- Errors reach the element ---" << std::endl;
    const std::string text = "This sentance has a typo.\nSo does thiss line.";
    area->SetText(text);

    const bool gotErrors = PumpUntilSpellErrors(window, area);
    TEST("A queued check reaches the element while rendering", gotErrors);

    const std::vector<SpellError>& errors = area->GetSpellErrors();
    const SpellError* first = FindError(errors, "sentance");
    const SpellError* second = FindError(errors, "thiss");
    TEST("Misspelling on line 1 is reported", first != nullptr);
    TEST("Misspelling on line 2 is reported", second != nullptr);
    TEST("Byte offsets index the document text",
         first && text.substr(first->startByte, first->byteLength) == "sentance");
    TEST("Second line offsets index the document text",
         second && text.substr(second->startByte, second->byteLength) == "thiss");
    TEST("Correctly spelled words are not reported", FindError(errors, "This") == nullptr);

    // ===== CHARACTER RANGE GEOMETRY =====
    std::cerr << "\n--- GetCharacterRangeBounds ---" << std::endl;
    if (first && second) {
        const std::vector<Rect2Df> firstBounds =
            area->GetCharacterRangeBounds(first->startByte, first->byteLength);
        TEST("A visible word maps to at least one rectangle", !firstBounds.empty());

        if (!firstBounds.empty()) {
            const Rect2Df& box = firstBounds.front();
            TEST("Rectangle has positive width", box.width > 0.0f);
            TEST("Rectangle has positive height", box.height > 0.0f);
            TEST("Rectangle sits inside the element horizontally",
                 box.x >= 0.0f && box.x + box.width <= 800.0f);
            TEST("Rectangle sits inside the element vertically",
                 box.y >= 0.0f && box.y + box.height <= 600.0f);
        }

        // Word two is on the following line, so it must be lower on screen.
        const std::vector<Rect2Df> secondBounds =
            area->GetCharacterRangeBounds(second->startByte, second->byteLength);
        TEST("Word on the second line also maps", !secondBounds.empty());
        if (!firstBounds.empty() && !secondBounds.empty()) {
            TEST("A later line maps lower on screen",
                 secondBounds.front().y > firstBounds.front().y);
        }

        // A longer range must be wider than a shorter one starting at the same place.
        const std::vector<Rect2Df> shortRange =
            area->GetCharacterRangeBounds(first->startByte, 2);
        const std::vector<Rect2Df> longRange =
            area->GetCharacterRangeBounds(first->startByte, first->byteLength);
        TEST("A longer range yields a wider rectangle",
             !shortRange.empty() && !longRange.empty() &&
             longRange.front().width > shortRange.front().width);

        TEST("An empty range yields nothing",
             area->GetCharacterRangeBounds(first->startByte, 0).empty());
        TEST("An out-of-range offset yields nothing",
             area->GetCharacterRangeBounds(text.size() + 5000, 4).empty());
    }

    // ===== SUGGESTIONS =====
    std::cerr << "\n--- Suggestions ---" << std::endl;
    if (first) {
        const std::vector<std::string> suggestions =
            service.GetSuggestions(first->word, 8);
        TEST("The flagged word has suggestions", !suggestions.empty());
    }

    // ===== APPLYING A CORRECTION =====
    std::cerr << "\n--- ApplySpellSuggestion ---" << std::endl;
    if (first) {
        const SpellError target = *first;   // errors are rebuilt by the edit
        const bool applied = area->ApplySpellSuggestion(target, "sentence");
        TEST("Applying a suggestion succeeds", applied);
        TEST("The document text is corrected",
             area->GetText().find("This sentence has a typo.") != std::string::npos);
        TEST("The old spelling is gone",
             area->GetText().find("sentance") == std::string::npos);

        // The remaining misspelling must survive, with offsets valid against
        // the NEW text - proving results are not stale after an edit.
        area->RunSpellCheck();
        const bool recheck = PumpUntil(window, area, [&area]() {
            return FindError(area->GetSpellErrors(), "thiss") != nullptr;
        });
        TEST("A re-check after the edit produces results", recheck);
        const std::string updated = area->GetText();
        const SpellError* still = FindError(area->GetSpellErrors(), "thiss");
        TEST("The untouched misspelling is still reported", still != nullptr);
        TEST("Its offsets are valid against the corrected text",
             still && still->startByte + still->byteLength <= updated.size() &&
             updated.substr(still->startByte, still->byteLength) == "thiss");
        TEST("The corrected word is no longer flagged",
             FindError(area->GetSpellErrors(), "sentance") == nullptr);
    }

    // ===== ADD TO DICTIONARY =====
    std::cerr << "\n--- User dictionary suppresses a flag ---" << std::endl;
    {
        area->SetText("Zorblax is a name.");
        const bool flagged = PumpUntil(window, area, [&area]() {
            return FindError(area->GetSpellErrors(), "Zorblax") != nullptr;
        });
        TEST("An unknown proper noun is flagged", flagged);

        service.IgnoreWord("Zorblax");
        area->RunSpellCheck();
        const bool cleared = PumpUntil(window, area, [&area]() {
            return FindError(area->GetSpellErrors(), "Zorblax") == nullptr;
        });
        TEST("Ignoring the word clears the flag", cleared);
        service.ClearIgnoredWords();
    }

    // ===== DISABLING CLEARS STATE =====
    std::cerr << "\n--- Disabling ---" << std::endl;
    {
        area->SetText("Another sentance with a typo.");
        PumpUntilSpellErrors(window, area);
        area->SetSpellCheckEnabled(false);
        TEST("Disabling clears the reported errors", area->GetSpellErrors().empty());
        window->UpdateAndRender();
        TEST("Rendering while disabled reports nothing", area->GetSpellErrors().empty());
    }

    // ===== READ-ONLY GUARD =====
    std::cerr << "\n--- ReplaceTextRange guards ---" << std::endl;
    {
        area->SetText("hello world");
        area->SetReadOnly(true);
        TEST("A read-only area refuses a range replacement",
             !area->ReplaceTextRange(0, 5, "howdy"));
        area->SetReadOnly(false);
        TEST("A writable area accepts one", area->ReplaceTextRange(0, 5, "howdy"));
        TEST("The replacement landed", area->GetText() == "howdy world");

        // A zero-length range is an insertion at that offset, not at the caret.
        area->SetText("hello world");
        area->SetCursorPosition({0, 0});
        TEST("A zero-length range inserts", area->ReplaceTextRange(5, 0, " there"));
        TEST("The insertion landed at the offset, not the caret",
             area->GetText() == "hello there world");

        // Deleting with no replacement.
        area->SetText("hello there world");
        TEST("An empty replacement deletes", area->ReplaceTextRange(5, 6, ""));
        TEST("The deletion landed", area->GetText() == "hello world");

        // A range spanning the newline joins both lines.
        area->SetText("first\nsecond");
        TEST("A range across a line break is replaceable",
             area->ReplaceTextRange(5, 1, " "));
        TEST("The lines were joined", area->GetText() == "first second");
    }

    service.Shutdown();

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "  " << (testCount - failCount) << "/" << testCount << " passed";
    if (skipCount > 0) std::cerr << ", " << skipCount << " skipped";
    std::cerr << std::endl;
    std::cerr << "========================================" << std::endl;

    return failCount == 0 ? 0 : 1;
}
