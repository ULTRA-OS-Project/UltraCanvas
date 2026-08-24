// core/UltraCanvasSpellChecker.cpp
// Implementation of the cross-platform spell checking service
// Version: 1.0.1
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework

#include "UltraCanvasSpellChecker.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace UltraCanvas {

// ===================================================================
// TEXT UTILITIES
// ===================================================================
namespace SpellCheckText {

// A byte is treated as part of a word when it is an ASCII letter, an ASCII
// digit, or any UTF-8 continuation/lead byte (>= 0x80). Treating all non-ASCII
// bytes as word content keeps accented Latin, Greek and Cyrillic words intact
// without pulling in a full Unicode table.
static bool IsWordByte(unsigned char c) {
    return std::isalpha(c) != 0 || std::isdigit(c) != 0 || c >= 0x80;
}

// Apostrophes and hyphens only count as word content when letters sit on both
// sides: "don't" and "well-known" stay whole, but "'quoted'" loses its quotes.
static bool IsInnerJoiner(unsigned char c) {
    return c == '\'' || c == '-' || c == '_' || c == '.' || c == '@';
}

size_t CountCodepoints(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

size_t ByteOffsetToCharIndex(const std::string& text, size_t byteOffset) {
    size_t limit = std::min(byteOffset, text.size());
    size_t index = 0;
    for (size_t i = 0; i < limit; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xC0) != 0x80) ++index;
    }
    return index;
}

size_t CharIndexToByteOffset(const std::string& text, size_t charIndex) {
    size_t index = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xC0) != 0x80) {
            if (index == charIndex) return i;
            ++index;
        }
    }
    return text.size();
}

bool ContainsDigit(const std::string& word) {
    for (unsigned char c : word) {
        if (std::isdigit(c)) return true;
    }
    return false;
}

bool IsAllUpperCase(const std::string& word) {
    bool sawLetter = false;
    for (unsigned char c : word) {
        if (c >= 0x80) return false;           // Non-ASCII: do not guess
        if (std::isalpha(c)) {
            sawLetter = true;
            if (std::islower(c)) return false;
        }
    }
    return sawLetter;
}

bool IsMixedCase(const std::string& word) {
    // True for camelCase / PascalCase style identifiers: an upper-case letter
    // appearing after the first character. A normally capitalised word such as
    // "Berlin" is NOT mixed case and stays checkable.
    bool sawUpperAfterFirst = false;
    bool sawLower = false;
    for (size_t i = 0; i < word.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(word[i]);
        if (c >= 0x80) continue;
        if (std::islower(c)) sawLower = true;
        if (i > 0 && std::isupper(c)) sawUpperAfterFirst = true;
    }
    return sawUpperAfterFirst && sawLower;
}

bool LooksLikeUrlOrEmail(const std::string& word) {
    if (word.find('@') != std::string::npos) return true;
    if (word.find("://") != std::string::npos) return true;
    if (word.rfind("www.", 0) == 0) return true;
    // A dot with letters on both sides and no space: "example.com"
    size_t dot = word.find('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < word.size()) {
        if (std::isalpha(static_cast<unsigned char>(word[dot - 1])) &&
            std::isalpha(static_cast<unsigned char>(word[dot + 1]))) {
            return true;
        }
    }
    return false;
}

std::string ToLowerAscii(const std::string& word) {
    std::string out;
    out.reserve(word.size());
    for (unsigned char c : word) {
        out += (c < 0x80) ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    }
    return out;
}

std::vector<WordSpan> TokenizeWords(const std::string& text) {
    std::vector<WordSpan> spans;
    spans.reserve(text.size() / 6 + 8);

    size_t i = 0;
    size_t charIndex = 0;
    int lineIndex = 0;
    int columnIndex = 0;

    auto advance = [&](size_t byteCount) {
        for (size_t k = 0; k < byteCount && i < text.size(); ++k, ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if ((c & 0xC0) != 0x80) {
                ++charIndex;
                if (c == '\n') {
                    ++lineIndex;
                    columnIndex = 0;
                } else {
                    ++columnIndex;
                }
            }
        }
    };

    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (!IsWordByte(c)) {
            advance(1);
            continue;
        }

        WordSpan span;
        span.startByte = i;
        span.startChar = charIndex;
        span.lineIndex = lineIndex;
        span.columnIndex = columnIndex;

        size_t end = i;
        while (end < text.size()) {
            unsigned char cur = static_cast<unsigned char>(text[end]);
            if (IsWordByte(cur)) {
                ++end;
                continue;
            }
            if (IsInnerJoiner(cur) && end + 1 < text.size() &&
                IsWordByte(static_cast<unsigned char>(text[end + 1]))) {
                ++end;
                continue;
            }
            break;
        }

        span.byteLength = end - i;
        span.text = text.substr(i, span.byteLength);
        span.charLength = CountCodepoints(span.text);
        spans.push_back(std::move(span));

        advance(end - i);
    }

    return spans;
}

const SpellError* FindErrorAtByteOffset(const std::vector<SpellError>& errors,
                                        size_t byteOffset) {
    for (const SpellError& error : errors) {
        if (error.ContainsByte(byteOffset)) return &error;
    }
    return nullptr;
}

} // namespace SpellCheckText

// ===================================================================
// SQUIGGLE RENDERING
// ===================================================================
namespace SpellCheckRendering {

void DrawWavyUnderline(IRenderContext* ctx,
                       float x, float y, float width,
                       const Color& color,
                       float amplitude, float waveLength, float strokeWidth) {
    if (!ctx || width <= 0.0f) return;
    if (waveLength <= 0.5f) waveLength = 0.5f;

    // A zig-zag polyline reads as a squiggle at UI sizes and avoids any
    // ambiguity around the path-completion API. IRenderContext draws in
    // doubles, so the point list is built as Point2Dd rather than converted
    // element by element at the call.
    std::vector<Point2Dd> points;
    points.reserve(static_cast<size_t>(width / waveLength) + 3);

    float cursor = x;
    bool up = true;
    points.emplace_back(static_cast<double>(cursor), static_cast<double>(y));
    while (cursor < x + width) {
        cursor = std::min(cursor + waveLength, x + width);
        points.emplace_back(static_cast<double>(cursor),
                            static_cast<double>(up ? (y - amplitude) : (y + amplitude)));
        up = !up;
    }
    if (points.size() < 2) return;

    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(strokeWidth);
    ctx->DrawLinePath(points, false);
}

void DrawDottedUnderline(IRenderContext* ctx,
                         float x, float y, float width,
                         const Color& color, float strokeWidth) {
    if (!ctx || width <= 0.0f) return;

    const float dotLength = 1.5f;
    const float gapLength = 1.5f;

    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(strokeWidth);

    float cursor = x;
    while (cursor < x + width) {
        float segmentEnd = std::min(cursor + dotLength, x + width);
        ctx->DrawLine(Point2Dd(cursor, y), Point2Dd(segmentEnd, y));
        cursor = segmentEnd + gapLength;
    }
}

void DrawSpellErrorMark(IRenderContext* ctx,
                        const Rect2Df& wordRect,
                        const SpellCheckStyle& style,
                        SpellErrorKind kind) {
    if (!ctx || wordRect.width <= 0.0f) return;

    const Color color = style.ColorForKind(kind);
    const float baseY = wordRect.y + wordRect.height + style.verticalOffset;

    switch (style.markStyle) {
        case SpellErrorMarkStyle::WavyUnderline:
            DrawWavyUnderline(ctx, wordRect.x, baseY, wordRect.width,
                              color, style.waveAmplitude, style.waveLength,
                              style.strokeWidth);
            break;

        case SpellErrorMarkStyle::DottedUnderline:
            DrawDottedUnderline(ctx, wordRect.x, baseY, wordRect.width,
                                color, style.strokeWidth);
            break;

        case SpellErrorMarkStyle::SolidUnderline:
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(style.strokeWidth);
            ctx->DrawLine(Point2Dd(wordRect.x, baseY),
                          Point2Dd(wordRect.x + wordRect.width, baseY));
            break;

        case SpellErrorMarkStyle::Highlight:
            ctx->SetFillPaint(style.highlightColor);
            ctx->FillRectangle(wordRect);
            break;
    }
}

} // namespace SpellCheckRendering

// ===================================================================
// SERVICE - CONSTRUCTION AND LIFECYCLE
// ===================================================================

UltraCanvasSpellChecker& UltraCanvasSpellChecker::Instance() {
    static UltraCanvasSpellChecker instance;
    return instance;
}

UltraCanvasSpellChecker::UltraCanvasSpellChecker() {
    userDictionaryPath = ResolveDefaultUserDictionaryPath();
}

UltraCanvasSpellChecker::~UltraCanvasSpellChecker() {
    Shutdown();
}

bool UltraCanvasSpellChecker::Initialize() {
    if (initialized.load()) return true;

    std::unique_ptr<ISpellCheckBackend> selected = CreateNativeSpellCheckBackend();
    if (!selected || !selected->Initialize()) {
        selected = CreateHunspellSpellCheckBackend();
        if (selected && !selected->Initialize()) {
            selected.reset();
        }
    }
    if (!selected) return false;

    {
        std::lock_guard<std::mutex> lock(backendMutex);
        backend = std::move(selected);
    }

    // Cache the language list once - enumeration walks the filesystem on
    // Linux and is far too slow to run while a menu is being built.
    std::vector<SpellLanguageInfo> languages;
    {
        std::lock_guard<std::mutex> lock(backendMutex);
        if (backend) languages = backend->EnumerateLanguages();
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        cachedLanguages = std::move(languages);
    }

    LoadUserDictionary();

    const std::string preferred = DetectPreferredLanguage();
    if (!preferred.empty()) SetLanguage(preferred);

    initialized.store(true);
    StartWorker();
    return true;
}

void UltraCanvasSpellChecker::Shutdown() {
    StopWorker();

    if (initialized.load()) {
        SaveUserDictionary();
    }

    std::lock_guard<std::mutex> lock(backendMutex);
    if (backend) {
        backend->Shutdown();
        backend.reset();
    }
    initialized.store(false);
}

bool UltraCanvasSpellChecker::IsInitialized() const {
    return initialized.load();
}

void UltraCanvasSpellChecker::SetBackend(std::unique_ptr<ISpellCheckBackend> newBackend) {
    if (!newBackend) return;
    newBackend->Initialize();

    std::vector<SpellLanguageInfo> languages = newBackend->EnumerateLanguages();
    {
        std::lock_guard<std::mutex> lock(backendMutex);
        if (backend) backend->Shutdown();
        backend = std::move(newBackend);
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        cachedLanguages = std::move(languages);
    }
    initialized.store(true);
    StartWorker();
}

std::string UltraCanvasSpellChecker::GetBackendName() const {
    std::lock_guard<std::mutex> lock(backendMutex);
    return backend ? backend->GetBackendName() : std::string("(none)");
}

// ===================================================================
// SERVICE - LANGUAGE MANAGEMENT
// ===================================================================

std::vector<SpellLanguageInfo> UltraCanvasSpellChecker::GetAvailableLanguages() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return cachedLanguages;
}

SpellLanguageInfo UltraCanvasSpellChecker::GetLanguageInfo(const std::string& languageCode) const {
    std::lock_guard<std::mutex> lock(stateMutex);
    for (const SpellLanguageInfo& info : cachedLanguages) {
        if (info.code == languageCode) return info;
    }
    return SpellLanguageInfo(languageCode, languageCode, languageCode, false);
}

bool UltraCanvasSpellChecker::SetLanguage(const std::string& languageCode) {
    if (languageCode.empty()) return false;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(backendMutex);
        if (!backend) return false;
        if (backend->GetLanguage() == languageCode) return true;
        changed = backend->SetLanguage(languageCode);
    }
    if (!changed) return false;

    // Results produced with the previous dictionary are meaningless now.
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        completedResults.clear();
    }
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        pendingJobs.clear();
    }

    if (onSpellLanguageChange) onSpellLanguageChange(languageCode);
    return true;
}

std::string UltraCanvasSpellChecker::GetLanguage() const {
    std::lock_guard<std::mutex> lock(backendMutex);
    return backend ? backend->GetLanguage() : std::string();
}

std::string UltraCanvasSpellChecker::DetectPreferredLanguage() const {
    std::vector<SpellLanguageInfo> languages;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        languages = cachedLanguages;
    }
    if (languages.empty()) return std::string();

    auto normalise = [](std::string value) {
        size_t cut = value.find('.');            // "de_DE.UTF-8" -> "de_DE"
        if (cut != std::string::npos) value.erase(cut);
        cut = value.find('@');
        if (cut != std::string::npos) value.erase(cut);
        std::replace(value.begin(), value.end(), '-', '_');
        return value;
    };

    const char* environmentKeys[] = { "LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE" };
    for (const char* key : environmentKeys) {
        const char* raw = std::getenv(key);
        if (!raw || !*raw) continue;
        const std::string wanted = normalise(raw);
        if (wanted.empty() || wanted == "C" || wanted == "POSIX") continue;

        for (const SpellLanguageInfo& info : languages) {
            if (info.isAvailable && info.code == wanted) return info.code;
        }
        // Fall back to the same base language with a different region.
        const std::string base = wanted.substr(0, wanted.find('_'));
        for (const SpellLanguageInfo& info : languages) {
            if (info.isAvailable && info.code.rfind(base, 0) == 0) return info.code;
        }
    }

    for (const SpellLanguageInfo& info : languages) {
        if (info.isAvailable) return info.code;
    }
    return std::string();
}

// ===================================================================
// SERVICE - MODE AND STYLE
// ===================================================================

void UltraCanvasSpellChecker::SetMode(SpellCheckMode newMode) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (mode == newMode) return;
        mode = newMode;
    }
    if (newMode == SpellCheckMode::Disabled) {
        std::lock_guard<std::mutex> lock(jobMutex);
        pendingJobs.clear();
    }
    if (onSpellModeChange) onSpellModeChange(newMode);
}

SpellCheckMode UltraCanvasSpellChecker::GetMode() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return mode;
}

bool UltraCanvasSpellChecker::IsEnabled() const {
    return initialized.load() && GetMode() != SpellCheckMode::Disabled;
}

void UltraCanvasSpellChecker::SetStyle(const SpellCheckStyle& newStyle) {
    std::lock_guard<std::mutex> lock(stateMutex);
    style = newStyle;
}

SpellCheckStyle UltraCanvasSpellChecker::GetStyle() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return style;
}

// ===================================================================
// SERVICE - CHECKING
// ===================================================================

bool UltraCanvasSpellChecker::IsWordAcceptable(const std::string& word) const {
    const std::string lowered = SpellCheckText::ToLowerAscii(word);
    std::lock_guard<std::mutex> lock(dictionaryMutex);
    if (userDictionary.count(lowered) != 0) return true;
    if (ignoredWords.count(lowered) != 0) return true;
    return false;
}

bool UltraCanvasSpellChecker::IsCorrect(const std::string& word) const {
    if (word.empty()) return true;
    if (IsWordAcceptable(word)) return true;

    std::lock_guard<std::mutex> lock(backendMutex);
    if (!backend) return true;
    return backend->IsWordCorrect(word);
}

std::vector<std::string> UltraCanvasSpellChecker::GetSuggestions(const std::string& word,
                                                                int maxCount) const {
    if (word.empty()) return {};
    std::lock_guard<std::mutex> lock(backendMutex);
    if (!backend) return {};
    return backend->GetSuggestions(word, maxCount);
}

std::vector<SpellError> UltraCanvasSpellChecker::CheckText(const std::string& text,
                                                           const SpellCheckOptions& options) const {
    std::vector<SpellError> errors;
    if (text.empty()) return errors;
    if (!initialized.load()) return errors;

    const std::vector<SpellCheckText::WordSpan> spans = SpellCheckText::TokenizeWords(text);

    std::string previousLowered;
    int previousLine = -1;

    for (const SpellCheckText::WordSpan& span : spans) {
        const std::string& word = span.text;

        if (static_cast<int>(span.charLength) < options.minimumWordLength) {
            previousLowered.clear();
            continue;
        }
        if (options.shouldSkipRange && options.shouldSkipRange(span.startByte, span.byteLength)) {
            previousLowered.clear();
            continue;
        }
        if (options.skipUrlsAndEmails && SpellCheckText::LooksLikeUrlOrEmail(word)) {
            previousLowered.clear();
            continue;
        }
        if (options.skipWordsWithUnderscore && word.find('_') != std::string::npos) {
            previousLowered.clear();
            continue;
        }
        if (options.skipWordsWithDigits && SpellCheckText::ContainsDigit(word)) {
            previousLowered.clear();
            continue;
        }
        if (options.skipUpperCaseWords && SpellCheckText::IsAllUpperCase(word)) {
            previousLowered.clear();
            continue;
        }
        if (options.skipMixedCaseWords && SpellCheckText::IsMixedCase(word)) {
            previousLowered.clear();
            continue;
        }

        const std::string lowered = SpellCheckText::ToLowerAscii(word);

        // Repeated word detection runs before the dictionary lookup so that
        // "the the" is reported even though "the" is spelled correctly.
        if (options.detectRepeatedWords &&
            !previousLowered.empty() &&
            previousLowered == lowered &&
            previousLine == span.lineIndex) {

            SpellError repeated;
            repeated.startByte = span.startByte;
            repeated.byteLength = span.byteLength;
            repeated.startChar = span.startChar;
            repeated.charLength = span.charLength;
            repeated.lineIndex = span.lineIndex;
            repeated.columnIndex = span.columnIndex;
            repeated.word = word;
            repeated.kind = SpellErrorKind::RepeatedWord;
            errors.push_back(std::move(repeated));

            previousLowered = lowered;
            previousLine = span.lineIndex;
            continue;
        }

        previousLowered = lowered;
        previousLine = span.lineIndex;

        if (IsWordAcceptable(word)) continue;

        bool correct = true;
        {
            // Locked per word, not per document, so a long background check
            // never starves a synchronous lookup on the render thread.
            std::lock_guard<std::mutex> lock(backendMutex);
            if (!backend) return errors;
            correct = backend->IsWordCorrect(word);
        }
        if (correct) continue;

        SpellError error;
        error.startByte = span.startByte;
        error.byteLength = span.byteLength;
        error.startChar = span.startChar;
        error.charLength = span.charLength;
        error.lineIndex = span.lineIndex;
        error.columnIndex = span.columnIndex;
        error.word = word;
        error.kind = SpellErrorKind::Misspelled;

        if (options.fetchSuggestions) {
            std::lock_guard<std::mutex> lock(backendMutex);
            if (backend) error.suggestions = backend->GetSuggestions(word, options.maxSuggestions);
        }

        errors.push_back(std::move(error));
    }

    return errors;
}

// ===================================================================
// SERVICE - ASYNCHRONOUS CHECKING
// ===================================================================

uint64_t UltraCanvasSpellChecker::QueueCheckText(uint64_t contextId,
                                                 const std::string& text,
                                                 const SpellCheckOptions& options) {
    if (!IsEnabled()) return 0;

    SpellCheckJob job;
    job.contextId = contextId;
    const uint64_t jobId = nextJobId.fetch_add(1);
    job.jobId = jobId;
    job.text = text;
    job.options = options;

    {
        std::lock_guard<std::mutex> lock(jobMutex);
        // Coalesce: a queued but unstarted job for this element is obsolete.
        pendingJobs.erase(
            std::remove_if(pendingJobs.begin(), pendingJobs.end(),
                           [contextId](const SpellCheckJob& queued) {
                               return queued.contextId == contextId;
                           }),
            pendingJobs.end());

        latestJobPerContext[contextId] = job.jobId;
        pendingJobs.push_back(std::move(job));
    }
    jobSignal.notify_one();

    return jobId;
}

bool UltraCanvasSpellChecker::TryTakeResult(uint64_t contextId, SpellCheckResult& outResult) {
    std::lock_guard<std::mutex> lock(resultMutex);
    auto it = completedResults.find(contextId);
    if (it == completedResults.end()) return false;
    outResult = std::move(it->second);
    completedResults.erase(it);
    return true;
}

void UltraCanvasSpellChecker::SetContextNotifier(uint64_t contextId,
                                                 std::function<void()> notifier) {
    std::lock_guard<std::mutex> lock(resultMutex);
    if (notifier) {
        contextNotifiers[contextId] = std::move(notifier);
    } else {
        contextNotifiers.erase(contextId);
    }
}

void UltraCanvasSpellChecker::CancelContext(uint64_t contextId) {
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        pendingJobs.erase(
            std::remove_if(pendingJobs.begin(), pendingJobs.end(),
                           [contextId](const SpellCheckJob& queued) {
                               return queued.contextId == contextId;
                           }),
            pendingJobs.end());
        latestJobPerContext.erase(contextId);
    }
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        completedResults.erase(contextId);
        contextNotifiers.erase(contextId);
    }
}

bool UltraCanvasSpellChecker::HasPendingWork() const {
    std::lock_guard<std::mutex> lock(jobMutex);
    return !pendingJobs.empty();
}

void UltraCanvasSpellChecker::StartWorker() {
    if (workerRunning.exchange(true)) return;
    workerThread = std::thread(&UltraCanvasSpellChecker::WorkerLoop, this);
}

void UltraCanvasSpellChecker::StopWorker() {
    {
        // The flag must be cleared while holding jobMutex. Clearing it outside
        // lets the worker evaluate the wait predicate as false and then block
        // after the notify has already fired, so join() would never return.
        std::lock_guard<std::mutex> lock(jobMutex);
        if (!workerRunning.exchange(false)) return;
    }
    jobSignal.notify_all();
    if (workerThread.joinable()) workerThread.join();

    std::lock_guard<std::mutex> lock(jobMutex);
    pendingJobs.clear();
    latestJobPerContext.clear();
}

void UltraCanvasSpellChecker::WorkerLoop() {
    while (workerRunning.load()) {
        SpellCheckJob job;
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobSignal.wait(lock, [this] {
                return !workerRunning.load() || !pendingJobs.empty();
            });
            if (!workerRunning.load()) return;
            job = std::move(pendingJobs.front());
            pendingJobs.pop_front();
        }

        SpellCheckResult result;
        result.contextId = job.contextId;
        result.jobId = job.jobId;
        result.languageCode = GetLanguage();
        result.errors = CheckText(job.text, job.options);

        // Drop the result if a newer edit arrived while we were checking.
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            auto it = latestJobPerContext.find(job.contextId);
            if (it == latestJobPerContext.end() || it->second != job.jobId) continue;
        }
        std::function<void()> notifier;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            completedResults[job.contextId] = std::move(result);
            auto notifierIt = contextNotifiers.find(job.contextId);
            if (notifierIt != contextNotifiers.end()) notifier = notifierIt->second;
        }
        // Invoked outside the lock: the notifier marshals to the UI thread, and
        // holding resultMutex across that would let a drain on the render
        // thread block this one.
        if (notifier) notifier();
    }
}

// ===================================================================
// SERVICE - DICTIONARY MANAGEMENT
// ===================================================================

std::string UltraCanvasSpellChecker::ResolveDefaultUserDictionaryPath() const {
    std::string base;

#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA")) base = appData;
    if (base.empty()) {
        if (const char* profile = std::getenv("USERPROFILE")) base = profile;
    }
    const std::string separator = "\\";
    const std::string folder = "UltraCanvas";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) base = xdg;
    if (base.empty()) {
        if (const char* home = std::getenv("HOME")) {
            base = std::string(home) + "/.local/share";
        }
    }
    const std::string separator = "/";
    const std::string folder = "UltraCanvas";
#endif

    if (base.empty()) return "UltraCanvasUserDictionary.txt";
    return base + separator + folder + separator + "UserDictionary.txt";
}

void UltraCanvasSpellChecker::SetUserDictionaryPath(const std::string& filePath) {
    {
        std::lock_guard<std::mutex> lock(dictionaryMutex);
        userDictionaryPath = filePath;
    }
    LoadUserDictionary();
}

std::string UltraCanvasSpellChecker::GetUserDictionaryPath() const {
    std::lock_guard<std::mutex> lock(dictionaryMutex);
    return userDictionaryPath;
}

bool UltraCanvasSpellChecker::LoadUserDictionary() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(dictionaryMutex);
        path = userDictionaryPath;
    }
    if (path.empty()) return false;

    std::ifstream input(path);
    if (!input) return false;   // Absent file is not an error on first run

    std::unordered_set<std::string> loaded;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        loaded.insert(SpellCheckText::ToLowerAscii(line));
    }

    std::lock_guard<std::mutex> lock(dictionaryMutex);
    userDictionary = std::move(loaded);
    return true;
}

bool UltraCanvasSpellChecker::SaveUserDictionary() const {
    std::string path;
    std::vector<std::string> words;
    {
        std::lock_guard<std::mutex> lock(dictionaryMutex);
        path = userDictionaryPath;
        words.assign(userDictionary.begin(), userDictionary.end());
    }
    if (path.empty()) return false;

    std::sort(words.begin(), words.end());

    // The default path lives under a directory the framework owns and which
    // will not exist on first run; without this every added word is lost.
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::error_code directoryError;
        std::filesystem::create_directories(target.parent_path(), directoryError);
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;

    output << "# UltraCanvas user dictionary\n";
    for (const std::string& word : words) {
        output << word << "\n";
    }
    return output.good();
}

bool UltraCanvasSpellChecker::AddToUserDictionary(const std::string& word) {
    if (word.empty()) return false;

    // Give the backend first refusal so native user dictionaries stay in sync
    // with the rest of the desktop. A backend that reports true has persisted
    // the word itself (macOS, Windows, enchant), so storing a second copy here
    // would only make the two lists drift apart - see ISpellCheckBackend.h.
    bool storedByBackend = false;
    {
        std::lock_guard<std::mutex> lock(backendMutex);
        if (backend) storedByBackend = backend->AddWordToBackendDictionary(word);
    }
    if (!storedByBackend) {
        {
            std::lock_guard<std::mutex> lock(dictionaryMutex);
            userDictionary.insert(SpellCheckText::ToLowerAscii(word));
        }
        SaveUserDictionary();
    }

    if (onSpellDictionaryChange) onSpellDictionaryChange(word);
    return true;
}

bool UltraCanvasSpellChecker::RemoveFromUserDictionary(const std::string& word) {
    size_t removed = 0;
    {
        std::lock_guard<std::mutex> lock(dictionaryMutex);
        removed = userDictionary.erase(SpellCheckText::ToLowerAscii(word));
    }
    if (removed == 0) return false;

    SaveUserDictionary();
    if (onSpellDictionaryChange) onSpellDictionaryChange(word);
    return true;
}

std::vector<std::string> UltraCanvasSpellChecker::GetUserDictionary() const {
    std::lock_guard<std::mutex> lock(dictionaryMutex);
    std::vector<std::string> words(userDictionary.begin(), userDictionary.end());
    std::sort(words.begin(), words.end());
    return words;
}

void UltraCanvasSpellChecker::RequestRecheck() {
    // Dropping the session ignore list and any cached result makes the next
    // queued check re-evaluate from scratch; the callback is what tells open
    // documents to queue one.
    ClearIgnoredWords();
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        completedResults.clear();
    }
    if (onSpellDictionaryChange) onSpellDictionaryChange(std::string());
}

void UltraCanvasSpellChecker::IgnoreWord(const std::string& word) {
    if (word.empty()) return;
    std::lock_guard<std::mutex> lock(dictionaryMutex);
    ignoredWords.insert(SpellCheckText::ToLowerAscii(word));
}

void UltraCanvasSpellChecker::ClearIgnoredWords() {
    std::lock_guard<std::mutex> lock(dictionaryMutex);
    ignoredWords.clear();
}

bool UltraCanvasSpellChecker::IsWordIgnored(const std::string& word) const {
    std::lock_guard<std::mutex> lock(dictionaryMutex);
    return ignoredWords.count(SpellCheckText::ToLowerAscii(word)) != 0;
}

// ===================================================================
// SERVICE - HOST APPLICATION MENU INTEGRATION
// ===================================================================

namespace {
// Every language entry shares one radio group so UltraCanvasMenu enforces
// exclusivity itself; the value only has to be distinct from other groups in
// the same menu.
constexpr int kSpellLanguageRadioGroup = 9101;
} // namespace

std::vector<MenuItemData> UltraCanvasSpellChecker::BuildLanguageMenuItems(bool useNativeNames) {
    UltraCanvasSpellChecker& service = Instance();
    std::vector<MenuItemData> items;

    std::vector<SpellLanguageInfo> languages = service.GetAvailableLanguages();
    const std::string current = service.GetLanguage();

    // Alphabetical by the label the user will actually read.
    std::sort(languages.begin(), languages.end(),
              [useNativeNames](const SpellLanguageInfo& a, const SpellLanguageInfo& b) {
                  const std::string& left = useNativeNames ? a.nativeName : a.displayName;
                  const std::string& right = useNativeNames ? b.nativeName : b.displayName;
                  return left < right;
              });

    for (const SpellLanguageInfo& info : languages) {
        if (!info.isAvailable) continue;

        std::string label = useNativeNames ? info.nativeName : info.displayName;
        if (label.empty()) label = info.code;

        const std::string code = info.code;
        items.push_back(MenuItemData::Radio(label, kSpellLanguageRadioGroup, code == current,
            [code]() {
                UltraCanvasSpellChecker::Instance().SetLanguage(code);
            }));
    }

    return items;
}

std::vector<MenuItemData> UltraCanvasSpellChecker::BuildSpellCheckMenuItems(
    const SpellMenuOptions& options) {

    UltraCanvasSpellChecker& service = Instance();
    std::vector<MenuItemData> items;

    if (options.showEnableToggle) {
        items.push_back(MenuItemData::Checkbox(options.enableLabel, service.IsEnabled(),
            [](bool checked) {
                UltraCanvasSpellChecker::Instance().SetMode(
                    checked ? SpellCheckMode::AsYouType : SpellCheckMode::Disabled);
            }));
    }

    if (options.showLanguageList) {
        if (options.showEnableToggle) items.push_back(MenuItemData::Separator());

        std::vector<MenuItemData> languageItems =
            BuildLanguageMenuItems(options.useNativeLanguageNames);

        if (languageItems.empty()) {
            // A visible explanation beats an empty submenu the user cannot
            // interpret. The callback is intentionally inert.
            items.push_back(MenuItemData::Action(options.noDictionaryLabel, []() {}));
        } else {
            for (MenuItemData& item : languageItems) {
                items.push_back(std::move(item));
            }
        }
    }

    if (options.showDictionaryActions) {
        items.push_back(MenuItemData::Separator());
        items.push_back(MenuItemData::Action(options.recheckLabel, []() {
            UltraCanvasSpellChecker::Instance().RequestRecheck();
        }));
    }

    return items;
}

MenuItemData UltraCanvasSpellChecker::BuildSpellCheckMenu(const SpellMenuOptions& options) {
    // A lambda-provided submenu is rebuilt every time it opens, so the enable
    // check mark and the active-language radio always show live state without
    // the host application having to rebuild its menu bar.
    auto provider = [options]() { return BuildSpellCheckMenuItems(options); };

    if (!options.iconPath.empty()) {
        return MenuItemData::Submenu(options.menuLabel, options.iconPath, provider);
    }
    return MenuItemData::Submenu(options.menuLabel, provider);
}

std::vector<MenuItemData> UltraCanvasSpellChecker::BuildSuggestionMenuItems(
    const SpellError& error,
    std::function<void(const std::string&)> onApply,
    std::function<void()> onRecheck) {

    UltraCanvasSpellChecker& service = Instance();
    std::vector<MenuItemData> items;

    std::vector<std::string> suggestions = error.suggestions;
    if (suggestions.empty()) {
        suggestions = service.GetSuggestions(error.word, 8);
    }

    if (suggestions.empty()) {
        items.push_back(MenuItemData::Action("(no suggestions)", []() {}));
    } else {
        for (const std::string& suggestion : suggestions) {
            items.push_back(MenuItemData::Action(suggestion,
                [suggestion, onApply]() {
                    if (onApply) onApply(suggestion);
                }));
        }
    }

    items.push_back(MenuItemData::Separator());

    const std::string word = error.word;
    items.push_back(MenuItemData::Action("Add to Dictionary",
        [word, onRecheck]() {
            UltraCanvasSpellChecker::Instance().AddToUserDictionary(word);
            if (onRecheck) onRecheck();
        }));

    items.push_back(MenuItemData::Action("Ignore",
        [word, onRecheck]() {
            UltraCanvasSpellChecker::Instance().IgnoreWord(word);
            if (onRecheck) onRecheck();
        }));

    return items;
}

} // namespace UltraCanvas
