// Tests/RecentFilesTest.cpp
// Unit tests for UltraTexter's recent-files store (TextEditorConfigFile).
// Covers the "Save As drops the original file from Recent Files" report:
// recent_files.txt is shared by every UltraTexter window and process, so a
// mutation must merge with what is on disk instead of overwriting it with the
// writing window's start-up snapshot.
// Framework-independent: builds against the header only, no display needed.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasTextEditorConfig.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using UltraCanvas::TextEditorConfigFile;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    ++checks; \
    auto va = (a); auto vb = (b); \
    if (!(va == vb)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    } \
} while (0)

namespace {

constexpr int kMaxRecent = 30;   // TextEditorConfig::maxRecentFiles default

std::filesystem::path g_sandbox;

// Point the config manager at a private directory so the test never touches
// the developer's real ~/.config/UltraTexter.
void SetUpSandbox() {
    g_sandbox = std::filesystem::temp_directory_path() / "UltraTexter-RecentFilesTest";
    std::error_code ec;
    std::filesystem::remove_all(g_sandbox, ec);
    std::filesystem::create_directories(g_sandbox / "config", ec);
    std::filesystem::create_directories(g_sandbox / "docs", ec);
    std::filesystem::create_directories(g_sandbox / "backup", ec);

#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("APPDATA", (g_sandbox / "config").string().c_str());
#else
    setenv("XDG_CONFIG_HOME", (g_sandbox / "config").string().c_str(), 1);
    setenv("HOME", (g_sandbox / "config").string().c_str(), 1);   // macOS path
#endif
}

void ResetRecentFiles() {
    TextEditorConfigFile cfg;
    cfg.ClearRecentFiles();
}

// Create a real file: paths are normalized against the filesystem, so entries
// must exist for symlink/".." resolution to be exercised.
std::string MakeFile(const std::string& relative) {
    std::filesystem::path p = g_sandbox / relative;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << "content" << std::endl;
    return p.string();
}

bool Contains(const std::vector<std::string>& files, const std::string& path) {
    std::string normalized = TextEditorConfigFile::NormalizeRecentPath(path);
    return std::find(files.begin(), files.end(), normalized) != files.end();
}

int IndexOf(const std::vector<std::string>& files, const std::string& path) {
    std::string normalized = TextEditorConfigFile::NormalizeRecentPath(path);
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i] == normalized) return static_cast<int>(i);
    }
    return -1;
}

// The reported scenario, single window: open original.txt, then Save As to
// copy.txt. Both must be listed, original second.
void TestSaveAsKeepsOriginal() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    std::string original = MakeFile("docs/original.txt");
    std::string copy     = MakeFile("docs/copy.txt");

    cfg.AddRecentFile(original, kMaxRecent);              // file opened
    auto files = cfg.AddRecentFile(copy, kMaxRecent);     // Save As

    CHECK_EQ(files.size(), size_t(2));
    CHECK_EQ(IndexOf(files, copy), 0);
    CHECK_EQ(IndexOf(files, original), 1);

    // And the same after a restart (fresh read of recent_files.txt).
    TextEditorConfigFile reopened;
    CHECK(Contains(reopened.LoadRecentFiles(), original));
}

// The actual defect: a second window (or a second UltraTexter process, which is
// what opening a file from the desktop file manager creates) writes the list
// from the snapshot it read at start-up. Without merging, its Save As wipes the
// file the other window opened in the meantime.
void TestSaveAsInOtherWindowKeepsOriginal() {
    ResetRecentFiles();

    TextEditorConfigFile windowA;   // long-running window, started first
    windowA.LoadRecentFiles();      // snapshot: empty

    TextEditorConfigFile windowB;   // window that opens the original file
    std::string original = MakeFile("docs/report.md");
    windowB.AddRecentFile(original, kMaxRecent);

    // Window A never saw report.md; its Save As must not drop it.
    std::string saved = MakeFile("docs/notes.md");
    auto files = windowA.AddRecentFile(saved, kMaxRecent);

    CHECK(Contains(files, original));
    CHECK(Contains(files, saved));
    CHECK(Contains(TextEditorConfigFile().LoadRecentFiles(), original));
}

// Re-adding an entry moves it to the front instead of duplicating it.
void TestReAddMovesToFront() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    std::string a = MakeFile("docs/a.txt");
    std::string b = MakeFile("docs/b.txt");

    cfg.AddRecentFile(a, kMaxRecent);
    cfg.AddRecentFile(b, kMaxRecent);
    auto files = cfg.AddRecentFile(a, kMaxRecent);

    CHECK_EQ(files.size(), size_t(2));
    CHECK_EQ(IndexOf(files, a), 0);
    CHECK_EQ(IndexOf(files, b), 1);
}

// The same file reached through different spellings — a relative command-line
// argument, a resource path containing "..", a trailing "./" — is one entry.
void TestPathsAreNormalized() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    std::string canonical = MakeFile("docs/manual.txt");
    std::string viaDotDot = (g_sandbox / "docs" / ".." / "docs" / "manual.txt").string();
    std::string viaDot    = (g_sandbox / "docs" / "." / "manual.txt").string();

    cfg.AddRecentFile(canonical, kMaxRecent);
    cfg.AddRecentFile(viaDotDot, kMaxRecent);
    auto files = cfg.AddRecentFile(viaDot, kMaxRecent);

    CHECK_EQ(files.size(), size_t(1));
    CHECK(Contains(files, canonical));
}

// A relative path must be stored absolute, otherwise the entry stops resolving
// as soon as the working directory changes and gets pruned as "missing".
void TestRelativePathStoredAbsolute() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    MakeFile("docs/relative.txt");
    std::filesystem::path previousCwd = std::filesystem::current_path();
    std::filesystem::current_path(g_sandbox / "docs");

    auto files = cfg.AddRecentFile("relative.txt", kMaxRecent);
    std::filesystem::current_path(previousCwd);

    CHECK_EQ(files.size(), size_t(1));
    CHECK(std::filesystem::path(files[0]).is_absolute());
    CHECK(std::filesystem::exists(files[0]));
}

void TestTrimToMaximum() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    std::string newest;
    for (int i = 0; i < kMaxRecent + 5; ++i) {
        newest = MakeFile("docs/bulk/file" + std::to_string(i) + ".txt");
        cfg.AddRecentFile(newest, kMaxRecent);
    }

    auto files = cfg.LoadRecentFiles();
    CHECK_EQ(files.size(), size_t(kMaxRecent));
    CHECK_EQ(IndexOf(files, newest), 0);
    CHECK(!Contains(files, (g_sandbox / "docs/bulk/file0.txt").string()));
}

void TestRemoveAndClear() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    std::string a = MakeFile("docs/keep.txt");
    std::string b = MakeFile("docs/drop.txt");
    cfg.AddRecentFile(a, kMaxRecent);
    cfg.AddRecentFile(b, kMaxRecent);

    auto files = cfg.RemoveRecentFile(b);
    CHECK_EQ(files.size(), size_t(1));
    CHECK(Contains(files, a));

    // Removing an entry must not disturb entries added by another window.
    TextEditorConfigFile other;
    std::string c = MakeFile("docs/other-window.txt");
    other.AddRecentFile(c, kMaxRecent);
    files = cfg.RemoveRecentFile(b);           // already gone
    CHECK(Contains(files, c));

    cfg.ClearRecentFiles();
    CHECK(TextEditorConfigFile().LoadRecentFiles().empty());
}

// An empty path is ignored rather than written as a blank entry.
void TestEmptyPathIgnored() {
    ResetRecentFiles();
    TextEditorConfigFile cfg;

    std::string a = MakeFile("docs/only.txt");
    cfg.AddRecentFile(a, kMaxRecent);
    auto files = cfg.AddRecentFile("", kMaxRecent);

    CHECK_EQ(files.size(), size_t(1));
    CHECK(Contains(files, a));
}

} // namespace

int main() {
    SetUpSandbox();

    TestSaveAsKeepsOriginal();
    TestSaveAsInOtherWindowKeepsOriginal();
    TestReAddMovesToFront();
    TestPathsAreNormalized();
    TestRelativePathStoredAbsolute();
    TestTrimToMaximum();
    TestRemoveAndClear();
    TestEmptyPathIgnored();

    std::error_code ec;
    std::filesystem::remove_all(g_sandbox, ec);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
