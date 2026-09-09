// include/UltraCanvasSpellChecker.h
// Cross-platform spell checking service with runtime language switching and menu integration
// Version: 1.0.1
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasMenu.h"
#include "ISpellCheckBackend.h"

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

// ===== SPELL CHECK ENUMS =====
// NOTE: no value is named "None" - X11 defines None as a macro (Rule 3).

enum class SpellCheckMode {
    Disabled,      // No checking at all
    AsYouType,     // Re-check after each edit, debounced, on the worker thread
    OnDemand       // Only when CheckNow() is called by the host application
};

enum class SpellErrorKind {
    Misspelled,        // Word not found in dictionary
    RepeatedWord,      // "the the"
    Capitalization     // Sentence not capitalised (reserved, backend dependent)
};

enum class SpellErrorMarkStyle {
    WavyUnderline,     // Classic red squiggle
    DottedUnderline,
    SolidUnderline,
    Highlight          // Tinted background behind the word
};

// ===== SPELL ERROR =====
// Offsets are given three ways so both TextInput (byte/char based) and
// TextArea (line/column based) can consume the same result without conversion.
struct SpellError {
    size_t startByte = 0;      // Byte offset into the checked text
    size_t byteLength = 0;     // Length in bytes
    size_t startChar = 0;      // UTF-8 codepoint offset into the checked text
    size_t charLength = 0;     // Length in codepoints
    int lineIndex = -1;        // 0-based line, -1 when text has no newlines
    int columnIndex = -1;      // 0-based codepoint column within that line
    std::string word;
    SpellErrorKind kind = SpellErrorKind::Misspelled;
    std::vector<std::string> suggestions;   // Empty unless fetchSuggestions was set

    bool ContainsByte(size_t byteOffset) const {
        return byteOffset >= startByte && byteOffset < startByte + byteLength;
    }
};

// ===== CHECK OPTIONS =====
struct SpellCheckOptions {
    bool skipWordsWithDigits = true;      // "abc123", "H2O"
    bool skipUpperCaseWords = true;       // "HTTP", "UTF"
    bool skipMixedCaseWords = true;       // "camelCase", "UltraCanvas"
    bool skipUrlsAndEmails = true;        // http://..., name@host
    bool skipWordsWithUnderscore = true;  // snake_case identifiers
    bool detectRepeatedWords = true;      // "the the"
    bool fetchSuggestions = false;        // Pre-fill suggestions (slow - use lazily instead)
    int maxSuggestions = 8;
    int minimumWordLength = 2;            // Single letters are never flagged

    // Optional veto hook. Return true to skip the byte range [start, start+len).
    // UltraTexter uses this to skip fenced code, inline code, links and math
    // without the spell module needing to know anything about markdown.
    //
    // THREADING: invoked on the spell worker thread, never on the render
    // thread. It must not touch element state that the render thread mutates -
    // capture an immutable snapshot when the options are built instead.
    std::function<bool(size_t /*startByte*/, size_t /*byteLength*/)> shouldSkipRange;
};

// ===== ASYNC RESULT =====
struct SpellCheckResult {
    uint64_t contextId = 0;      // Caller-supplied element identity
    uint64_t jobId = 0;          // Monotonic, lets callers discard stale results
    std::string languageCode;    // Language the check actually ran with
    std::vector<SpellError> errors;
};

// ===== VISUAL STYLE =====
struct SpellCheckStyle {
    SpellErrorMarkStyle markStyle = SpellErrorMarkStyle::WavyUnderline;
    Color markColor = Color(220, 38, 38, 255);       // Misspelling
    Color repeatedWordColor = Color(37, 99, 235, 255); // Repeated word
    Color highlightColor = Color(220, 38, 38, 40);   // Used by Highlight style
    float waveAmplitude = 1.6f;    // Peak height of the squiggle in px
    float waveLength = 4.0f;       // Horizontal px per half-cycle
    float strokeWidth = 1.3f;
    float verticalOffset = 1.0f;   // px below the text baseline box

    Color ColorForKind(SpellErrorKind kind) const {
        return (kind == SpellErrorKind::RepeatedWord) ? repeatedWordColor : markColor;
    }
};

// ===== MENU BUILD OPTIONS =====
struct SpellMenuOptions {
    std::string menuLabel = "Spell Check";
    std::string enableLabel = "Enable Spell Check";
    std::string addToDictionaryLabel = "Add to Dictionary...";
    std::string recheckLabel = "Recheck Document";
    std::string noDictionaryLabel = "(no dictionaries installed)";
    bool showEnableToggle = true;
    bool showLanguageList = true;
    bool showDictionaryActions = true;
    bool useNativeLanguageNames = true;   // "Deutsch" instead of "German"
    std::string iconPath;                 // Optional icon for the submenu
};

// ===== SPELL CHECKER SERVICE =====
// Singleton. Owns the backend, the user dictionary, the ignore list and one
// worker thread. All public methods are safe to call from the render thread.
class UltraCanvasSpellChecker {
public:
    static UltraCanvasSpellChecker& Instance();

    // ===== LIFECYCLE =====
    // Selects the native backend for the current platform and falls back to
    // Hunspell when that fails. Safe to call more than once.
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const;

    // Replaces the active backend. Ownership is transferred.
    void SetBackend(std::unique_ptr<ISpellCheckBackend> backend);
    std::string GetBackendName() const;

    // ===== LANGUAGE MANAGEMENT =====
    std::vector<SpellLanguageInfo> GetAvailableLanguages() const;
    bool SetLanguage(const std::string& languageCode);
    std::string GetLanguage() const;
    SpellLanguageInfo GetLanguageInfo(const std::string& languageCode) const;

    // Picks a sensible startup language from LANG / LC_ALL / user settings,
    // falling back to the first available dictionary.
    std::string DetectPreferredLanguage() const;

    // ===== MODE =====
    void SetMode(SpellCheckMode mode);
    SpellCheckMode GetMode() const;
    bool IsEnabled() const;

    // ===== SYNCHRONOUS CHECKING =====
    bool IsCorrect(const std::string& word) const;
    std::vector<std::string> GetSuggestions(const std::string& word, int maxCount = 8) const;
    std::vector<SpellError> CheckText(const std::string& text,
                                      const SpellCheckOptions& options = SpellCheckOptions()) const;

    // ===== ASYNCHRONOUS CHECKING =====
    // Queues text for the worker thread. A pending job for the same contextId
    // is replaced, so fast typing never builds a backlog. Returns the job id.
    uint64_t QueueCheckText(uint64_t contextId,
                            const std::string& text,
                            const SpellCheckOptions& options = SpellCheckOptions());

    // Called from the render thread, once per frame. Returns false when nothing
    // finished for that context. Mirrors the markdown prescan drain pattern.
    bool TryTakeResult(uint64_t contextId, SpellCheckResult& outResult);
    void CancelContext(uint64_t contextId);
    bool HasPendingWork() const;

    // Called once a result for `contextId` has been published, so a caller that
    // only drains while rendering knows a frame is now worth drawing. Without
    // it a check that finishes after the edit's repaint would sit undelivered
    // until something unrelated caused the element to redraw.
    //
    // THREADING: invoked on the worker thread. It must be cheap and thread
    // safe - marshal to the UI thread rather than touching element state, and
    // guard against the caller having been destroyed in the meantime.
    // Passing an empty function clears the registration; CancelContext also
    // clears it.
    void SetContextNotifier(uint64_t contextId, std::function<void()> notifier);

    // ===== DICTIONARY MANAGEMENT =====
    bool AddToUserDictionary(const std::string& word);
    bool RemoveFromUserDictionary(const std::string& word);
    std::vector<std::string> GetUserDictionary() const;
    void IgnoreWord(const std::string& word);          // Session only, not persisted
    void ClearIgnoredWords();

    // Drops the session ignore list and every cached result, then fires
    // onSpellDictionaryChange so open documents re-queue a check. This is what
    // the menu's "Recheck Document" entry calls.
    void RequestRecheck();
    bool IsWordIgnored(const std::string& word) const;

    void SetUserDictionaryPath(const std::string& filePath);
    std::string GetUserDictionaryPath() const;
    bool LoadUserDictionary();
    bool SaveUserDictionary() const;

    // ===== STYLE =====
    void SetStyle(const SpellCheckStyle& style);
    // Returned by value: the style is guarded by an internal mutex and may be
    // replaced by another thread while a render pass is reading it.
    SpellCheckStyle GetStyle() const;

    // ===== HOST APPLICATION MENU INTEGRATION =====
    // Returns a complete submenu the host can drop into its menu bar:
    //
    //   menuBar->AddItem(MenuItemData::Submenu("Tools", {
    //       UltraCanvasSpellChecker::BuildSpellCheckMenu(),
    //   }));
    //
    // The submenu is lambda-provided, so it rebuilds every time it opens and
    // the enable check mark and active-language radio always show live state.
    // The host does not need to rebuild its menu bar when the language changes.
    static MenuItemData BuildSpellCheckMenu(const SpellMenuOptions& options = SpellMenuOptions());

    // The same items without the enclosing submenu, for applications splicing
    // them into a menu of their own.
    static std::vector<MenuItemData> BuildSpellCheckMenuItems(
        const SpellMenuOptions& options = SpellMenuOptions());

    // Language entries only, for applications that build their own layout.
    // Emitted as radio items in one shared group, so the menu enforces
    // exclusivity and shows which dictionary is active.
    static std::vector<MenuItemData> BuildLanguageMenuItems(bool useNativeNames = true);

    // Suggestion items for a right-click context menu on a misspelled word.
    // onApply receives the replacement text chosen by the user.
    static std::vector<MenuItemData> BuildSuggestionMenuItems(
        const SpellError& error,
        std::function<void(const std::string&)> onApply,
        std::function<void()> onRecheck = nullptr);

    // ===== CALLBACKS =====
    // Base verb forms per Rule 6.
    std::function<void(const std::string&)> onSpellLanguageChange;  // new language code
    std::function<void(SpellCheckMode)> onSpellModeChange;
    std::function<void(const std::string&)> onSpellDictionaryChange; // word added/removed

    UltraCanvasSpellChecker(const UltraCanvasSpellChecker&) = delete;
    UltraCanvasSpellChecker& operator=(const UltraCanvasSpellChecker&) = delete;

private:
    UltraCanvasSpellChecker();
    ~UltraCanvasSpellChecker();

    struct SpellCheckJob {
        uint64_t contextId = 0;
        uint64_t jobId = 0;
        std::string text;
        SpellCheckOptions options;
    };

    void StartWorker();
    void StopWorker();
    void WorkerLoop();

    bool IsWordAcceptable(const std::string& word) const;
    std::string ResolveDefaultUserDictionaryPath() const;

    mutable std::mutex backendMutex;
    std::unique_ptr<ISpellCheckBackend> backend;

    mutable std::mutex dictionaryMutex;
    std::unordered_set<std::string> userDictionary;
    std::unordered_set<std::string> ignoredWords;
    std::string userDictionaryPath;

    mutable std::mutex stateMutex;
    SpellCheckMode mode = SpellCheckMode::AsYouType;
    SpellCheckStyle style;
    std::vector<SpellLanguageInfo> cachedLanguages;
    std::atomic<bool> initialized{false};

    mutable std::mutex jobMutex;
    std::condition_variable jobSignal;
    std::deque<SpellCheckJob> pendingJobs;
    std::unordered_map<uint64_t, uint64_t> latestJobPerContext;
    std::atomic<uint64_t> nextJobId{1};
    std::atomic<bool> workerRunning{false};
    std::thread workerThread;

    mutable std::mutex resultMutex;
    std::unordered_map<uint64_t, SpellCheckResult> completedResults;
    std::unordered_map<uint64_t, std::function<void()>> contextNotifiers;
};

// ===== TEXT UTILITIES =====
// Public because both text components and the host application need them.
namespace SpellCheckText {

    // Splits UTF-8 text into candidate words. Returned ranges carry byte,
    // codepoint and line/column positions.
    struct WordSpan {
        size_t startByte = 0;
        size_t byteLength = 0;
        size_t startChar = 0;
        size_t charLength = 0;
        int lineIndex = 0;
        int columnIndex = 0;
        std::string text;
    };

    std::vector<WordSpan> TokenizeWords(const std::string& text);

    // Byte offset -> codepoint index, and back. Used when mapping a mouse hit
    // on a rendered glyph run to a SpellError.
    size_t ByteOffsetToCharIndex(const std::string& text, size_t byteOffset);
    size_t CharIndexToByteOffset(const std::string& text, size_t charIndex);
    size_t CountCodepoints(const std::string& text);

    bool ContainsDigit(const std::string& word);
    bool IsAllUpperCase(const std::string& word);
    bool IsMixedCase(const std::string& word);
    bool LooksLikeUrlOrEmail(const std::string& word);
    std::string ToLowerAscii(const std::string& word);

    // Returns the error under a byte offset, or nullptr. For context menus.
    const SpellError* FindErrorAtByteOffset(const std::vector<SpellError>& errors,
                                            size_t byteOffset);

} // namespace SpellCheckText

// ===== SQUIGGLE RENDERING =====
// Pure IRenderContext drawing - no element knowledge, so any component that can
// produce a word rectangle can call these. Built on DrawLinePath(), which is
// unambiguous in Master_functions_V4_1 (Rule 2 compliance).
namespace SpellCheckRendering {

    // Draws the mark under one word. wordRect is the glyph run box in element
    // coordinates; the mark is drawn just below it.
    void DrawSpellErrorMark(IRenderContext* ctx,
                            const Rect2Df& wordRect,
                            const SpellCheckStyle& style,
                            SpellErrorKind kind = SpellErrorKind::Misspelled);

    // Lower-level: a wavy line from x to x+width at baseline y.
    void DrawWavyUnderline(IRenderContext* ctx,
                           float x, float y, float width,
                           const Color& color,
                           float amplitude, float waveLength, float strokeWidth);

    void DrawDottedUnderline(IRenderContext* ctx,
                             float x, float y, float width,
                             const Color& color, float strokeWidth);

} // namespace SpellCheckRendering

} // namespace UltraCanvas
