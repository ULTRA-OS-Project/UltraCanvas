// Tests/ClipboardHistoryTest.cpp
// The clipboard history the ULTRA OS desktop shows: what it records, and in
// which order.
//
// Two defects this pins down. A password copied from UltraPassword was
// recorded by the desktop's clipboard monitor and listed in its clipboard menu
// until a hundred newer copies pushed it out; the 30-second clear emptied the
// clipboard, not the history. Content marked secret - by this framework's
// ClipboardHint::Secret or by another program's marker - is now never
// recorded. And the menu walked the history from the back, believing the
// newest entry was last, so once there were more than fifteen it showed the
// oldest; the history is newest first, and that is checked here.
//
// Runs headless: a fake backend stands in for the platform clipboard, playing
// both this process and "another program owns the clipboard".
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasClipboard.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

// What the clipboard holds, as every backend would see it, shared with the
// test so it can play another program copying.
struct FakeClipboardState {
    std::string text;
    bool markedSecret = false;
    bool changed = false;
    int plainSets = 0;
    int secretSets = 0;
};

class FakeClipboardBackend : public UltraCanvasClipboardBackend {
public:
    explicit FakeClipboardBackend(std::shared_ptr<FakeClipboardState> state) : state(std::move(state)) {}

    bool Initialize() override { return true; }
    void Shutdown() override {}

    bool GetClipboardText(std::string& text) override { text = state->text; return true; }
    bool SetClipboardText(const std::string& text) override {
        state->text = text;
        state->markedSecret = false;
        state->changed = true;
        state->plainSets++;
        return true;
    }
    bool SetClipboardSecretText(const std::string& text) override {
        state->text = text;
        state->markedSecret = true;
        state->changed = true;
        state->secretSets++;
        return true;
    }
    bool IsClipboardMarkedSecret() override { return state->markedSecret; }

    bool GetClipboardImage(std::vector<uint8_t>&, std::string&) override { return false; }
    bool SetClipboardImage(const std::vector<uint8_t>&, const std::string&) override { return false; }
    bool GetClipboardFiles(std::vector<std::string>&) override { return false; }
    bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }

    bool HasClipboardChanged() override { return state->changed; }
    void ResetChangeState() override { state->changed = false; }

    std::vector<std::string> GetAvailableFormats() override { return {"text/plain"}; }
    bool IsFormatAvailable(const std::string& format) override { return format == "text/plain"; }

private:
    std::shared_ptr<FakeClipboardState> state;
};

// Another program puts text on the clipboard.
void OtherProgramCopies(FakeClipboardState& state, const std::string& text, bool secret) {
    state.text = text;
    state.markedSecret = secret;
    state.changed = true;
}

// The monitor looks at most every 500 ms; wait that out, then let it look.
void LetMonitorLook(UltraCanvasClipboard& clipboard) {
    std::this_thread::sleep_for(std::chrono::milliseconds(520));
    clipboard.Update();
}

bool HistoryContains(const UltraCanvasClipboard& clipboard, const std::string& text) {
    for (const auto& entry : clipboard.GetEntries()) {
        if (entry.content == text) return true;
    }
    return false;
}

std::string Text(const ClipboardData& entry) { return entry.content; }

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Clipboard History Suite"               << std::endl;
    std::cerr << "========================================" << std::endl;

    std::cerr << "\n--- The history is newest first ---" << std::endl;
    {
        auto state = std::make_shared<FakeClipboardState>();
        UltraCanvasClipboard clipboard;
        TEST("initialises with a backend", clipboard.InitializeWithBackend(std::make_unique<FakeClipboardBackend>(state)));
        clipboard.ClearHistory();
        clipboard.AddEntry(ClipboardData(ClipboardDataType::Text, "first"));
        clipboard.AddEntry(ClipboardData(ClipboardDataType::Text, "second"));
        clipboard.AddEntry(ClipboardData(ClipboardDataType::Text, "third"));
        const auto& entries = clipboard.GetEntries();
        TEST("three entries", entries.size() == 3);
        TEST("the newest is at index 0", entries.size() == 3 && Text(entries[0]) == "third");
        TEST("the oldest is last", entries.size() == 3 && Text(entries[2]) == "first");
        clipboard.AddEntry(ClipboardData(ClipboardDataType::Text, "first"));
        TEST("copying an entry again moves it to the front, once",
             entries.size() == 3 && Text(entries[0]) == "first" && Text(entries[1]) == "third");
    }

    std::cerr << "\n--- The monitor records copies, but not secrets ---" << std::endl;
    {
        auto state = std::make_shared<FakeClipboardState>();
        UltraCanvasClipboard clipboard;
        clipboard.InitializeWithBackend(std::make_unique<FakeClipboardBackend>(state));
        clipboard.ClearHistory();
        clipboard.StartMonitoring();

        OtherProgramCopies(*state, "Meeting at 14:00", false);
        LetMonitorLook(clipboard);
        TEST("an ordinary copy is recorded", HistoryContains(clipboard, "Meeting at 14:00"));

        OtherProgramCopies(*state, "correct horse battery staple", true);
        LetMonitorLook(clipboard);
        TEST("a copy marked secret by another program is not recorded",
             !HistoryContains(clipboard, "correct horse battery staple"));
        TEST("and the history did not grow", clipboard.GetEntryCount() == 1);

        LetMonitorLook(clipboard);
        TEST("nor on a later look, while it stays on the clipboard",
             !HistoryContains(clipboard, "correct horse battery staple"));

        OtherProgramCopies(*state, "", false);     // UltraPassword's clear after 30 s
        LetMonitorLook(clipboard);
        OtherProgramCopies(*state, "Room 3.12", false);
        LetMonitorLook(clipboard);
        TEST("recording carries on after the secret", HistoryContains(clipboard, "Room 3.12"));
        TEST("newest first", !clipboard.GetEntries().empty() && Text(clipboard.GetEntries()[0]) == "Room 3.12");
    }

    std::cerr << "\n--- ClipboardHint::Secret ---" << std::endl;
    {
        auto state = std::make_shared<FakeClipboardState>();
        UltraCanvasClipboard clipboard;
        clipboard.InitializeWithBackend(std::make_unique<FakeClipboardBackend>(state));
        clipboard.ClearHistory();
        clipboard.StartMonitoring();

        TEST("a secret copy succeeds", clipboard.SetText("s3cr3t-key", ClipboardHint::Secret));
        TEST("it goes out through the backend's secret path, with the marker",
             state->secretSets == 1 && state->plainSets == 0 && state->markedSecret);
        TEST("the text is on the clipboard", state->text == "s3cr3t-key");
        state->changed = true;                     // as a backend that notices its own copy would
        LetMonitorLook(clipboard);
        TEST("this process's own monitor does not record it", !HistoryContains(clipboard, "s3cr3t-key"));

        TEST("a normal copy takes the plain path", clipboard.SetText("hello", ClipboardHint::Normal));
        TEST("without the marker", state->plainSets == 1 && !state->markedSecret);
    }

    std::cerr << "\n--- A secret already on the clipboard at start-up ---" << std::endl;
    {
        auto state = std::make_shared<FakeClipboardState>();
        state->text = "hunter2";
        state->markedSecret = true;
        state->changed = true;
        UltraCanvasClipboard clipboard;
        clipboard.InitializeWithBackend(std::make_unique<FakeClipboardBackend>(state));
        TEST("is not recorded when the clipboard starts", !HistoryContains(clipboard, "hunter2"));
        clipboard.StartMonitoring();
        LetMonitorLook(clipboard);
        TEST("nor by the first look of the monitor", !HistoryContains(clipboard, "hunter2"));

        auto plainState = std::make_shared<FakeClipboardState>();
        plainState->text = "an address";
        UltraCanvasClipboard plain;
        plain.InitializeWithBackend(std::make_unique<FakeClipboardBackend>(plainState));
        TEST("ordinary text on the clipboard at start-up is recorded, as before", HistoryContains(plain, "an address"));
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
