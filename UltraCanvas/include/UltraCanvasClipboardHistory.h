// include/UltraCanvasClipboardHistory.h
// The clipboard history of ULTRA OS: every copy, in every format the
// framework's clipboard can read, kept on disk so it outlives the program that
// showed it. UltraDesktop records into it (and shows it in its quick panel);
// the UltraClipboard application shows, searches and edits the same history.
//
//   UltraCanvasClipboardHistory history;
//   if (history.Open()) {                               // the per-user default place
//       ClipboardSnapshot copy;
//       if (CaptureClipboard(*GetClipboard(), copy)) history.Record(copy);
//       for (const auto& entry : history.List()) { ... entry.title ... }
//   }
//
// Where it lives (Docs/Research/UltraClipboardDesignProposal.md §5.5):
//   <data>/ultraos/clipboard/history.db      SQLite (UltraDatabase, WAL): entries, formats, settings
//   <data>/ultraos/clipboard/blobs/ab/….bin  one file per payload, named by a keyed hash, encrypted
//   <data>/ultraos/clipboard/thumbs/….png    96 × 96 thumbnails of images
//   <config>/ultraos/clipboard.key           the 256-bit key, owner-only, kept apart from the data
// <data> is $XDG_DATA_HOME (~/.local/share), %LOCALAPPDATA% on Windows and
// ~/Library/Application Support on macOS; <config> is the folder the
// desktop's own settings use.
//
// Titles, texts and payloads are encrypted with XChaCha20-Poly1305 when
// UltraCrypt has libsodium. That protects a copy of the data folder and a
// backup of it; it does not protect against a program running as the same
// user, which can read the clipboard itself. Thumbnails are not encrypted.
//
// Two processes share one history: the desktop records and the application
// edits. Every write raises a generation number that the other side reads
// with GetGeneration() and reloads on. Exactly one process records at a
// time, the holder of the recorder lease (AcquireRecorder).
//
// Not thread-safe: use one instance from one thread.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasClipboard;

// ===== WHAT A COPY IS =====
enum class ClipboardEntryKind : int {
    Text = 0,
    Code = 1,       // text that reads as source code
    RichText = 2,   // HTML with its plain text
    Link = 3,       // one absolute URL
    Colour = 4,     // one colour literal: #3B82F6, rgb(…), hsl(…)
    Image = 5,
    Files = 6
};

// "Text", "Code", "Formatted text", "Link", "Colour", "Image", "Files".
std::string ClipboardEntryKindName(ClipboardEntryKind kind);

// The formats a snapshot carries. Images keep the MIME type the platform gave
// (image/png, image/jpeg, image/tiff, image/bmp - on Windows a bare CF_DIB).
namespace ClipboardMime {
    inline constexpr const char* Text  = "text/plain;charset=utf-8";
    inline constexpr const char* Html  = "text/html";
    // "copy" or "cut", then one path per line.
    inline constexpr const char* Files = "application/x-ultracanvas-files";
}

struct ClipboardFormat {
    std::string mime;
    std::vector<uint8_t> data;
};

// One copy, as read from the clipboard.
struct ClipboardSnapshot {
    std::vector<ClipboardFormat> formats;
    std::string sourceApplication;   // a program's name, "" when unknown

    const ClipboardFormat* Find(const std::string& mime) const;
    const ClipboardFormat* FindImage() const;
    std::string GetText() const;     // the plain text, "" when none
    void SetText(const std::string& text);
    bool IsEmpty() const;
};

// One entry of the history, as listed. The content itself is read with
// ReadFormats / ReadText.
struct ClipboardHistoryEntry {
    int64_t id = 0;
    ClipboardEntryKind kind = ClipboardEntryKind::Text;
    std::string title;               // first line · file names · URL · "Image W × H"
    std::string preview;             // the first 4 KB of the text (searchable)
    std::string sourceApplication;
    int64_t copiedAt = 0;            // Unix ms, when first copied
    int64_t lastUsedAt = 0;          // Unix ms, when last copied or chosen again
    int useCount = 0;
    bool pinned = false;
    uint64_t sizeBytes = 0;          // all formats together
    int width = 0, height = 0;       // images
    int fileCount = 0;               // files
    bool cut = false;                // files that were cut, not copied
    int lineCount = 0;               // text kinds
    std::string thumbnailPath;       // images: a 96 × 96 PNG; "" for kinds drawn by the UI
};

struct ClipboardHistoryQuery {
    std::string text;                        // matched case- and accent-insensitively
    std::vector<ClipboardEntryKind> kinds;   // empty = all
    bool pinnedOnly = false;
    size_t limit = 0;                        // 0 = all
};

// Shared by every process that opens the history (kept in the database).
struct ClipboardHistoryPolicy {
    int maxEntries = 500;                        // unpinned
    int maxAgeDays = 30;                         // unpinned, by last use
    uint64_t maxTotalBytes = 512ull << 20;
    uint64_t maxEntryBytes = 64ull << 20;
    bool recordingPaused = false;
    bool imageThumbnails = true;
    std::vector<std::string> excludedApplications;

    static ClipboardHistoryPolicy Defaults();
};

struct ClipboardHistoryStats {
    size_t entries = 0;
    size_t pinned = 0;
    uint64_t bytes = 0;
    bool encrypted = false;
};

enum class ClipboardRecordResult {
    Recorded,      // a new entry
    MovedToTop,    // already in the history: it is the newest again
    Empty,         // nothing the history keeps
    Paused,
    Excluded,      // its source application is excluded
    TooLarge,      // over maxEntryBytes
    Failed
};

class UltraCanvasClipboardHistory {
public:
    UltraCanvasClipboardHistory();
    ~UltraCanvasClipboardHistory();
    UltraCanvasClipboardHistory(const UltraCanvasClipboardHistory&) = delete;
    UltraCanvasClipboardHistory& operator=(const UltraCanvasClipboardHistory&) = delete;

    // The per-user places (see the top of this file).
    static std::string DefaultDirectory();
    static std::string DefaultKeyPath();
    // False when this build has no database (no libsqlite3): the history is
    // then unavailable and Open always fails.
    static bool IsAvailable();

    // Creates what is missing. A key that no longer opens the history (lost,
    // replaced) leaves it unreadable: it is cleared and started again.
    bool Open(const std::string& directory = DefaultDirectory(),
              const std::string& keyPath = DefaultKeyPath());
    void Close();
    bool IsOpen() const;
    const std::string& GetLastError() const;
    const std::string& GetDirectory() const;

    // ===== RECORDING =====
    ClipboardRecordResult Record(const ClipboardSnapshot& snapshot, int64_t* entryId = nullptr);

    // ===== READING =====
    // Pinned first, then the most recently used first.
    std::vector<ClipboardHistoryEntry> List(const ClipboardHistoryQuery& query = {});
    std::optional<ClipboardHistoryEntry> Get(int64_t id);
    bool ReadFormats(int64_t id, std::vector<ClipboardFormat>& formats);
    std::string ReadText(int64_t id);
    ClipboardHistoryStats GetStats();

    // ===== ACTING =====
    // The entry was put back on the clipboard: it becomes the newest.
    bool MarkUsed(int64_t id);
    bool SetPinned(int64_t id, bool pinned);
    // Hidden at once; gone for good after kUndoSeconds unless Restore()d.
    bool Remove(int64_t id);
    bool Restore(int64_t id);
    // An edited copy. keepOriginal: a new entry, the original stays;
    // otherwise the entry's content is replaced. Returns the entry with the
    // new content, 0 on failure.
    int64_t Replace(int64_t id, const ClipboardSnapshot& content, bool keepOriginal);
    // Removes the unpinned entries, and the pinned ones too when asked.
    size_t Clear(bool includePinned);
    // Applies the policy's limits and finishes removals; Record runs it too.
    void Prune();

    static constexpr int kUndoSeconds = 8;

    // ===== SETTINGS =====
    ClipboardHistoryPolicy GetPolicy();
    bool SetPolicy(const ClipboardHistoryPolicy& policy);

    // ===== SHARING WITH ANOTHER PROCESS =====
    // Rises with every change, from any process.
    uint64_t GetGeneration();
    // The recorder lease: true while `holder` may record. A holder keeps it by
    // calling this at least every few seconds; a higher priority takes it
    // over (the desktop from the application), and a holder silent for
    // kLeaseSeconds loses it.
    bool AcquireRecorder(const std::string& holder, int priority);
    void ReleaseRecorder(const std::string& holder);
    static constexpr int kLeaseSeconds = 6;

private:
    ClipboardRecordResult RecordContent(const ClipboardSnapshot& snapshot, int64_t* entryId, bool applyPolicy);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ===== THE LIVE CLIPBOARD =====
// Reads what is on the clipboard: files (with the cut flag), else an image,
// else text with its HTML. False when there is nothing of those.
bool CaptureClipboard(UltraCanvasClipboard& clipboard, ClipboardSnapshot& snapshot);
// Puts formats back: files, else an image, else HTML with its text, else text.
bool RestoreToClipboard(UltraCanvasClipboard& clipboard, const std::vector<ClipboardFormat>& formats);

// Records the clipboard into a history, from a UI timer. Holds the recorder
// lease while it can; a copy marked secret by its source is never recorded.
// When the program that made the last copy quits and takes the clipboard with
// it (X11), the recorder puts that copy back on the clipboard.
class UltraCanvasClipboardRecorder {
public:
    // role: names the holder ("desktop", "ultraclipboard"); the process id is
    // added. The desktop records with a higher priority than an application.
    void Attach(UltraCanvasClipboardHistory* history, UltraCanvasClipboard* clipboard,
                const std::string& role, int priority);
    void Detach();
    // Call every 200-500 ms on the UI thread.
    void Tick();
    bool IsRecording() const { return recording; }

    // Asked for the source of a new copy (the desktop answers with the active
    // window's application); optional.
    std::function<std::string()> sourceProvider;
    // A copy was recorded (or moved to the top).
    std::function<void(int64_t entryId, ClipboardRecordResult result)> onRecorded;

private:
    UltraCanvasClipboardHistory* history = nullptr;
    UltraCanvasClipboard* clipboard = nullptr;
    std::string holder;
    int priority = 0;
    bool recording = false;
    bool capturedInitial = false;
    int64_t lastLeaseCheck = 0;
    int64_t onClipboard = 0;   // the entry that is on the clipboard now, 0 when unknown
};

// Case- and accent-insensitive form of a UTF-8 string for searching: Latin,
// Greek and Cyrillic letters lowered, Latin accents dropped, ß as "ss".
std::string FoldForClipboardSearch(const std::string& text);

// The edit dialog's text tools. Case changes cover Latin, Greek and Cyrillic.
enum class ClipboardTextEdit {
    Trim,        // spaces at the ends of lines, empty lines at the ends of the text
    JoinLines,   // one line, the lines joined by single spaces
    Upper,
    Lower,
    Title,       // Every Word Capitalised
    Sentence     // The first letter of each sentence capitalised, the rest lower
};
std::string EditClipboardText(const std::string& text, ClipboardTextEdit edit);

// An image entry as a file another program opens (UltraClipboard hands images
// to UltraPaint this way): its bytes - a Windows CF_DIB gains the BMP file
// header it lacks - and the extension that names its format ("png", "jpg").
std::vector<uint8_t> ClipboardImageFile(const ClipboardFormat& image, std::string& extension);

} // namespace UltraCanvas
