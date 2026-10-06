// Tests/ClipboardHistoryStoreTest.cpp
// The persistent clipboard history (UltraCanvasClipboardHistory): what a copy
// becomes, that it is encrypted on disk, searching, pinning, removing and
// undoing, the limits, two processes on one history, the recorder lease, and
// the recorder itself against a fake clipboard - the password marker, and
// putting the last copy back when its program quits.
//
// Headless: a temporary folder, a fake clipboard backend, no window.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

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

// A 4 x 3 red PNG.
const std::vector<uint8_t> kPng = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x08, 0x06, 0x00, 0x00, 0x00, 0xb4, 0xf4, 0xae, 0xc6, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xb8, 0xa3, 0xa1, 0xf1, 0x1f, 0x19, 0x33, 0x10, 0x14, 0x00,
    0x00, 0x95, 0x41, 0x1a, 0x05, 0x70, 0x0b, 0xcc, 0x23, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82};
// 4 x 3 of #3B82F6 at 16 bits a sample, as ImageMagick and scanners write
// them: its thumbnail came out white once.
const std::vector<uint8_t> kPng16 = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x10, 0x02, 0x00, 0x00, 0x00, 0x6b, 0x06, 0xe5, 0xd2, 0x00, 0x00, 0x00,
    0x17, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xb4, 0xb6, 0x6e, 0x6a, 0xfa, 0xf6, 0x8d, 0x01, 0x03, 0x30,
    0x31, 0xe0, 0x00, 0x38, 0x25, 0x00, 0xf1, 0x3d, 0x03, 0x6c, 0x70, 0xc7, 0x5d, 0xc5, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

ClipboardSnapshot TextCopy(const std::string& text, const std::string& source = "") {
    ClipboardSnapshot snapshot;
    snapshot.SetText(text);
    snapshot.sourceApplication = source;
    return snapshot;
}

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

bool FileContains(const fs::path& path, const std::string& needle) {
    std::FILE* file = OpenFileUtf8(PathToUtf8(path), "rb");
    if (!file) return false;
    std::string content;
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) content.append(buffer, n);
    std::fclose(file);
    return content.find(needle) != std::string::npos;
}

bool AnyFileContains(const fs::path& folder, const std::string& needle) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, ec); it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file() && FileContains(it->path(), needle)) return true;
    }
    return false;
}

int CountFiles(const fs::path& folder) {
    int count = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, ec); it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file()) ++count;
    }
    return count;
}

const ClipboardHistoryEntry* FindTitle(const std::vector<ClipboardHistoryEntry>& entries, const std::string& title) {
    for (const auto& entry : entries) {
        if (entry.title == title) return &entry;
    }
    return nullptr;
}

// ===== A FAKE CLIPBOARD =====
struct FakeState {
    std::string text, html;
    std::vector<uint8_t> image;
    std::string imageFormat;
    std::vector<std::string> files;
    bool cut = false;
    bool secret = false;
    bool owned = true;
    bool changed = false;
    void Clear() { text.clear(); html.clear(); image.clear(); files.clear(); cut = false; secret = false; }
};

class FakeBackend : public UltraCanvasClipboardBackend {
public:
    explicit FakeBackend(std::shared_ptr<FakeState> s) : state(std::move(s)) {}
    bool Initialize() override { return true; }
    void Shutdown() override {}
    bool GetClipboardText(std::string& text) override {
        if (!state->owned || state->text.empty()) return false;
        text = state->text;
        return true;
    }
    bool SetClipboardText(const std::string& text) override {
        state->Clear();
        state->text = text;
        state->owned = true;
        state->changed = true;
        return true;
    }
    bool SetClipboardHtml(const std::string& html, const std::string& plain) override {
        SetClipboardText(plain);
        state->html = html;
        return true;
    }
    bool GetClipboardHtml(std::string& html) override {
        if (!state->owned || state->html.empty()) return false;
        html = state->html;
        return true;
    }
    bool GetClipboardImage(std::vector<uint8_t>& data, std::string& format) override {
        if (!state->owned || state->image.empty()) return false;
        data = state->image;
        format = state->imageFormat;
        return true;
    }
    bool SetClipboardImage(const std::vector<uint8_t>& data, const std::string& format) override {
        state->Clear();
        state->image = data;
        state->imageFormat = format;
        state->owned = true;
        state->changed = true;
        return true;
    }
    bool GetClipboardFiles(std::vector<std::string>& paths) override {
        bool cut = false;
        return GetClipboardFiles(paths, cut);
    }
    bool SetClipboardFiles(const std::vector<std::string>& paths) override { return SetClipboardFiles(paths, false); }
    bool GetClipboardFiles(std::vector<std::string>& paths, bool& cut) override {
        if (!state->owned || state->files.empty()) return false;
        paths = state->files;
        cut = state->cut;
        return true;
    }
    bool SetClipboardFiles(const std::vector<std::string>& paths, bool cut) override {
        state->Clear();
        state->files = paths;
        state->cut = cut;
        state->owned = true;
        state->changed = true;
        return true;
    }
    bool IsClipboardMarkedSecret() override { return state->owned && state->secret; }
    bool HasClipboardOwner() override { return state->owned; }
    bool HasClipboardChanged() override { return state->changed; }
    void ResetChangeState() override { state->changed = false; }
    std::vector<std::string> GetAvailableFormats() override { return {}; }
    bool IsFormatAvailable(const std::string&) override { return false; }

private:
    std::shared_ptr<FakeState> state;
};

} // namespace

int main(int, char** argv) {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Clipboard History Store Suite"         << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!UltraCanvasClipboardHistory::IsAvailable()) {
        std::cerr << "SKIP: this build has no database" << std::endl;
        return 0;
    }
    UCImage::InitializeImageSubsysterm(argv[0]);

    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / PathFromUtf8("ultraclipboard-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path dir = root / "history";
    const fs::path keyPath = root / "config" / "clipboard.key";

    std::cerr << "\n--- What a copy becomes ---" << std::endl;
    UltraCanvasClipboardHistory history;
    TEST("opens in a new folder", history.Open(PathToUtf8(dir), PathToUtf8(keyPath)));
    TEST("the key is written", fs::exists(keyPath, ec) && fs::file_size(keyPath, ec) == 32);
    TEST("and kept apart from the data", !fs::exists(dir / "clipboard.key", ec));
    const bool encrypted = history.GetStats().encrypted;
    std::cerr << "      (encrypted: " << (encrypted ? "yes" : "no - no libsodium in this build") << ")" << std::endl;

    int64_t id = 0;
    TEST("text is recorded", history.Record(TextCopy("  Meeting at 14:00\nin room 3.12", "UltraMail"), &id) ==
                                 ClipboardRecordResult::Recorded && id > 0);
    TEST("the same text again moves it to the top",
         history.Record(TextCopy("  Meeting at 14:00\nin room 3.12")) == ClipboardRecordResult::MovedToTop);
    auto entries = history.List();
    TEST("one entry, not two", entries.size() == 1);
    TEST("titled by its first line", !entries.empty() && entries[0].title == "Meeting at 14:00");
    TEST("of kind Text, two lines, from UltraMail",
         !entries.empty() && entries[0].kind == ClipboardEntryKind::Text && entries[0].lineCount == 2 &&
         entries[0].sourceApplication == "UltraMail");
    TEST("its text reads back exactly", history.ReadText(id) == "  Meeting at 14:00\nin room 3.12");

    history.Record(TextCopy("https://github.com/ULTRA-OS-Project/UltraCanvas"));
    history.Record(TextCopy("#3B82F6"));
    history.Record(TextCopy("auto history = CreateListView(\"history\", 0, 0, 640, 480);"));
    ClipboardSnapshot rich = TextCopy("Q4 planning");
    rich.formats.push_back({ClipboardMime::Html, Bytes("<h1>Q4 planning</h1>")});
    history.Record(rich);
    ClipboardSnapshot files;
    files.formats.push_back({ClipboardMime::Files, Bytes("cut\n/home/me/Invoice.pdf\n/home/me/Contract.odt\n/home/me/a.txt")});
    history.Record(files);
    ClipboardSnapshot image;
    image.formats.push_back({"image/png", kPng});
    int64_t imageId = 0;
    history.Record(image, &imageId);

    entries = history.List();
    auto kindOf = [&](const std::string& title) {
        const ClipboardHistoryEntry* e = FindTitle(entries, title);
        return e ? static_cast<int>(e->kind) : -1;
    };
    TEST("a URL is a Link", kindOf("https://github.com/ULTRA-OS-Project/UltraCanvas") == static_cast<int>(ClipboardEntryKind::Link));
    TEST("a colour literal is a Colour", kindOf("#3B82F6") == static_cast<int>(ClipboardEntryKind::Colour));
    TEST("a statement is Code", kindOf("auto history = CreateListView(\"history\", 0, 0, 640, 480);") ==
                                static_cast<int>(ClipboardEntryKind::Code));
    TEST("HTML with text is Formatted text", kindOf("Q4 planning") == static_cast<int>(ClipboardEntryKind::RichText));
    const ClipboardHistoryEntry* fileEntry = FindTitle(entries, "Invoice.pdf, Contract.odt and 1 more");
    TEST("files are titled by their names", fileEntry != nullptr);
    TEST("three of them, cut", fileEntry && fileEntry->fileCount == 3 && fileEntry->cut);
    const auto imageEntry = history.Get(imageId);
    TEST("an image is measured", imageEntry && imageEntry->kind == ClipboardEntryKind::Image &&
                                 imageEntry->width == 4 && imageEntry->height == 3);
    TEST("and gets a thumbnail", imageEntry && !imageEntry->thumbnailPath.empty() &&
                                 fs::exists(PathFromUtf8(imageEntry->thumbnailPath), ec));
    std::vector<ClipboardFormat> formats;
    TEST("its bytes read back unchanged", history.ReadFormats(imageId, formats) && formats.size() == 1 &&
                                          formats[0].mime == "image/png" && formats[0].data == kPng);
    TEST("the newest is first", !entries.empty() && entries[0].kind == ClipboardEntryKind::Image);

    ClipboardSnapshot deepImage;
    deepImage.formats.push_back({"image/png", kPng16});
    int64_t deepId = 0;
    history.Record(deepImage, &deepId);
    const auto deepEntry = history.Get(deepId);
    uint32_t thumbPixel = 0;
    if (deepEntry && !deepEntry->thumbnailPath.empty()) {
        if (auto thumb = UCImage::Get(deepEntry->thumbnailPath)) {
            if (auto pixmap = thumb->GetPixmap(thumb->GetWidth(), thumb->GetHeight(), ImageFitMode::Contain, 1.0f)) {
                thumbPixel = pixmap->GetPixelData()[0];
            }
        }
    }
    auto near = [](uint32_t value, uint32_t want) { return value + 3 >= want && value <= want + 3; };
    TEST("a 16-bit image's thumbnail keeps its colour", near((thumbPixel >> 16) & 0xFF, 0x3B) &&
                                                         near((thumbPixel >> 8) & 0xFF, 0x82) &&
                                                         near(thumbPixel & 0xFF, 0xF6));
    history.Remove(deepId);
    history.Prune();

    std::cerr << "\n--- On disk ---" << std::endl;
    if (encrypted) {
        TEST("no title or text in the database file", !AnyFileContains(dir, "Meeting at 14:00") &&
                                                        !AnyFileContains(dir, "room 3.12"));
        TEST("nor any file name", !AnyFileContains(dir, "Invoice.pdf"));
    } else {
        std::cerr << "SKIP: encryption checks (no libsodium)" << std::endl;
    }

    std::cerr << "\n--- Search ---" << std::endl;
    history.Record(TextCopy("Müller, Straße 12, Łódź"));
    auto found = [&](const std::string& query) {
        ClipboardHistoryQuery q;
        q.text = query;
        return history.List(q).size();
    };
    TEST("case and accents do not matter", found("muller strasse lodz") == 1 && found("MÜLLER") == 1);
    TEST("every word must match", found("muller invoice") == 0);
    TEST("file names are searched", found("contract") == 1);
    TEST("the source application is searched", found("ultramail") == 1);
    TEST("kind: filters", found("kind:image") == 1 && found("kind:link") == 1);
    TEST("from: filters", found("from:ultramail meeting") == 1 && found("from:ultramail invoice") == 0);
    ClipboardHistoryQuery onlyText;
    onlyText.kinds = {ClipboardEntryKind::Text};
    TEST("kinds filter", history.List(onlyText).size() == 2);

    std::cerr << "\n--- Pin, remove, undo, edit ---" << std::endl;
    TEST("pin", history.SetPinned(id, true));
    entries = history.List();
    TEST("a pinned entry is listed first", !entries.empty() && entries[0].id == id && entries[0].pinned);
    ClipboardHistoryQuery pinnedOnly;
    pinnedOnly.pinnedOnly = true;
    TEST("and alone under pinned", history.List(pinnedOnly).size() == 1);
    const size_t before = history.List().size();
    TEST("remove hides it", history.Remove(imageId) && history.List().size() == before - 1 && !history.Get(imageId));
    TEST("undo brings it back", history.Restore(imageId) && history.List().size() == before);
    const int64_t edited = history.Replace(id, TextCopy("Meeting at 15:00"), true);
    TEST("an edit kept beside the original", edited > 0 && edited != id && history.List().size() == before + 1);
    const int64_t replaced = history.Replace(edited, TextCopy("Meeting at 16:00"), false);
    TEST("an edit in place replaces it", replaced > 0 && !history.Get(edited) && history.List().size() == before + 1);
    TEST("and its text is the new one", history.ReadText(replaced) == "Meeting at 16:00");

    std::cerr << "\n--- Policy ---" << std::endl;
    ClipboardHistoryPolicy policy = history.GetPolicy();
    TEST("password managers are excluded by default",
         std::find(policy.excludedApplications.begin(), policy.excludedApplications.end(), "KeePassXC") !=
                 policy.excludedApplications.end());
    TEST("a copy from an excluded application is not kept",
         history.Record(TextCopy("hunter2", "keepassxc")) == ClipboardRecordResult::Excluded);
    policy.recordingPaused = true;
    history.SetPolicy(policy);
    TEST("paused: nothing is kept", history.Record(TextCopy("while paused")) == ClipboardRecordResult::Paused);
    policy.recordingPaused = false;
    policy.maxEntries = 10;
    history.SetPolicy(policy);
    for (int i = 0; i < 15; ++i) history.Record(TextCopy("note " + std::to_string(i)));
    const auto stats = history.GetStats();
    TEST("at most ten unpinned entries are kept", stats.entries == 11 && stats.pinned == 1);
    entries = history.List();
    TEST("the oldest went first", !FindTitle(entries, "note 4") && FindTitle(entries, "note 5"));
    TEST("the pinned one stays", history.Get(id).has_value());

    std::cerr << "\n--- Two processes, one history ---" << std::endl;
    UltraCanvasClipboardHistory other;
    TEST("a second instance opens it", other.Open(PathToUtf8(dir), PathToUtf8(keyPath)));
    const uint64_t generation = other.GetGeneration();
    history.Record(TextCopy("from the first"));
    TEST("sees the generation rise", other.GetGeneration() > generation);
    ClipboardHistoryQuery q;
    q.text = "from the first";
    TEST("and reads the new entry", other.List(q).size() == 1);
    TEST("the recorder lease goes to the first asker", other.AcquireRecorder("app:1", 0));
    TEST("is renewed by its holder", other.AcquireRecorder("app:1", 0));
    TEST("a higher priority takes it over", history.AcquireRecorder("desktop:1", 10));
    TEST("the lower one no longer has it", !other.AcquireRecorder("app:1", 0));
    history.ReleaseRecorder("desktop:1");
    TEST("released, it can be taken again", other.AcquireRecorder("app:1", 0));
    other.Close();

    std::cerr << "\n--- Clearing ---" << std::endl;
    const int filesBefore = CountFiles(dir / "blobs");
    const size_t cleared = history.Clear(false);
    TEST("clear removes the unpinned entries", cleared > 0 && history.GetStats().entries == 1);
    TEST("and their stored payloads", CountFiles(dir / "blobs") < filesBefore && CountFiles(dir / "blobs") >= 1);
    TEST("the pinned one is left", history.Get(id).has_value());
    history.Close();

    std::cerr << "\n--- Another key ---" << std::endl;
    if (encrypted) {
        UltraCanvasClipboardHistory stranger;
        TEST("a history under another key opens", stranger.Open(PathToUtf8(dir), PathToUtf8(root / "other.key")));
        TEST("but starts again, empty", stranger.GetStats().entries == 0 && stranger.List().empty());
        stranger.Close();
    } else {
        std::cerr << "SKIP: (no libsodium)" << std::endl;
    }

    std::cerr << "\n--- The recorder ---" << std::endl;
    {
        UltraCanvasClipboardHistory recorded;
        recorded.Open(PathToUtf8(root / "recorded"), PathToUtf8(keyPath));
        auto state = std::make_shared<FakeState>();
        UltraCanvasClipboard clipboard;
        clipboard.InitializeWithBackend(std::make_unique<FakeBackend>(state));
        UltraCanvasClipboardRecorder recorder;
        int recordedCount = 0;
        recorder.onRecorded = [&recordedCount](int64_t, ClipboardRecordResult) { ++recordedCount; };
        recorder.sourceProvider = []() { return std::string("UltraTexter"); };
        recorder.Attach(&recorded, &clipboard, "test", 5);

        state->text = "first copy";
        recorder.Tick();
        TEST("records what is on the clipboard when it starts", recorder.IsRecording() && recordedCount == 1);
        TEST("with the source it was given", !recorded.List().empty() &&
                                             recorded.List()[0].sourceApplication == "UltraTexter");

        state->Clear();
        state->files = {"/tmp/a.png", "/tmp/b.png"};
        state->changed = true;
        recorder.Tick();
        TEST("records a file copy", recorded.List().size() == 2 &&
                                    recorded.List()[0].kind == ClipboardEntryKind::Files);

        state->Clear();
        state->image = kPng;
        state->imageFormat = "image/png";
        state->changed = true;
        recorder.Tick();
        TEST("records an image copy", recorded.List().size() == 3 &&
                                      recorded.List()[0].kind == ClipboardEntryKind::Image);

        state->Clear();
        state->text = "plain words";
        state->image = Bytes("plain words");   // an owner that answers every request with its text
        state->imageFormat = "image/png";
        state->html = "plain words";
        state->changed = true;
        recorder.Tick();
        TEST("text is not taken for an image, nor for HTML", recorded.List().size() == 4 &&
                                                             recorded.List()[0].kind == ClipboardEntryKind::Text);
        recorded.Remove(recorded.List()[0].id);

        state->Clear();
        state->text = "correct horse battery staple";
        state->secret = true;
        state->changed = true;
        recorder.Tick();
        TEST("never records a copy marked secret", recorded.List().size() == 3);

        state->Clear();
        state->text = "keep me";
        state->changed = true;
        recorder.Tick();
        TEST("records the next ordinary copy", recorded.List().size() == 4);
        state->owned = false;   // the program that copied it quits
        state->Clear();
        state->changed = true;
        recorder.Tick();
        TEST("puts the copy back when its program quits", state->owned && state->text == "keep me");

        state->Clear();
        state->text = "hunter2";
        state->secret = true;
        state->changed = true;
        recorder.Tick();
        state->owned = false;
        state->Clear();
        state->changed = true;
        recorder.Tick();
        TEST("but never puts back anything after a secret", !state->owned && state->text.empty());

        std::vector<ClipboardFormat> stored;
        const auto list = recorded.List();
        TEST("restore puts files back with their cut flag",
             recorded.ReadFormats(list[2].id, stored) && RestoreToClipboard(clipboard, stored) &&
             state->files.size() == 2 && !state->cut);
        recorder.Detach();
        recorded.Close();
    }

    std::cerr << "\n--- Search folding ---" << std::endl;
    TEST("ASCII lowered", FoldForClipboardSearch("HeLLo") == "hello");
    TEST("Latin accents dropped", FoldForClipboardSearch("Éàçüñ") == "eacun");
    TEST("ß and æ spelled out", FoldForClipboardSearch("Straße Æble") == "strasse aeble");
    TEST("Cyrillic lowered", FoldForClipboardSearch("ПРИВЕТ") == "привет");
    TEST("Greek lowered", FoldForClipboardSearch("ΑΒΓ") == "αβγ");
    TEST("CJK untouched", FoldForClipboardSearch("日本語") == "日本語");

    std::cerr << "\n--- Text tools ---" << std::endl;
    TEST("upper case, accents and all", EditClipboardText("straße éte żółw", ClipboardTextEdit::Upper) == "STRAßE ÉTE ŻÓŁW");
    TEST("lower case, Greek and Cyrillic", EditClipboardText("ΑΘΗΝΑ МОСКВА", ClipboardTextEdit::Lower) == "αθηνα москва");
    TEST("title case", EditClipboardText("the QUICK brown fox's tail", ClipboardTextEdit::Title) == "The Quick Brown Fox's Tail");
    TEST("a dash separates words", EditClipboardText("east\xE2\x80\x94west", ClipboardTextEdit::Title) == "East\xE2\x80\x94West");
    TEST("sentence case", EditClipboardText("HELLO THERE. HOW ARE YOU?\nfine", ClipboardTextEdit::Sentence) ==
                          "Hello there. How are you?\nFine");
    TEST("trim", EditClipboardText("\n\n  one  \ntwo\t\n\n", ClipboardTextEdit::Trim) == "  one\ntwo");
    TEST("join lines", EditClipboardText("one\n  two\n\nthree ", ClipboardTextEdit::JoinLines) == "one two three");

    fs::remove_all(root, ec);
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
