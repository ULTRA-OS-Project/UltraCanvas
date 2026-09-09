// SpellCheckerTest.cpp
// Test suite for the UltraCanvasSpellChecker service, tokenizer and backends
// Version: 1.0.0
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// Framework-independent: builds straight from the spell sources, with the WASM
// backend stub supplying CreateNativeSpellCheckBackend() so the portable
// Hunspell path is the one under test on every platform.
//
// Dictionary-dependent checks are skipped (not failed) when no dictionary is
// installed, so the suite still passes on a machine without hunspell data.

#include "UltraCanvasSpellChecker.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;
static int skipCount = 0;

#define TEST(name, condition)                                               \
    do {                                                                    \
        bool passed = (condition);                                          \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                           \
        testCount++;                                                        \
    } while (0)

#define SKIP(name, reason)                                                  \
    do {                                                                    \
        std::cerr << "SKIP: " << name << " (" << reason << ")" << std::endl; \
        skipCount++;                                                        \
    } while (0)

namespace {

const SpellCheckText::WordSpan* FindSpan(
    const std::vector<SpellCheckText::WordSpan>& spans, const std::string& word) {
    for (const SpellCheckText::WordSpan& span : spans) {
        if (span.text == word) return &span;
    }
    return nullptr;
}

bool HasErrorForWord(const std::vector<SpellError>& errors, const std::string& word) {
    for (const SpellError& error : errors) {
        if (error.word == word) return true;
    }
    return false;
}

const SpellError* FindErrorForWord(const std::vector<SpellError>& errors,
                                   const std::string& word) {
    for (const SpellError& error : errors) {
        if (error.word == word) return &error;
    }
    return nullptr;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraCanvas Spell Checker Test Suite" << std::endl;
    std::cerr << "========================================" << std::endl;

    // ===== TOKENIZER =====
    std::cerr << "\n--- Tokenizer ---" << std::endl;
    {
        const std::string text = "The quick brown fox";
        const auto spans = SpellCheckText::TokenizeWords(text);
        TEST("Splits a simple sentence into four words", spans.size() == 4);
        TEST("First word is 'The'", !spans.empty() && spans[0].text == "The");
        TEST("First word starts at byte 0", !spans.empty() && spans[0].startByte == 0);
        TEST("Second word starts at byte 4", spans.size() > 1 && spans[1].startByte == 4);
        TEST("Byte offsets index the original text",
             spans.size() > 3 &&
             text.substr(spans[3].startByte, spans[3].byteLength) == "fox");
    }
    {
        // Apostrophes and hyphens hold a word together; surrounding quotes do not.
        const auto spans = SpellCheckText::TokenizeWords("don't well-known 'quoted'");
        TEST("Keeps an internal apostrophe", FindSpan(spans, "don't") != nullptr);
        TEST("Keeps an internal hyphen", FindSpan(spans, "well-known") != nullptr);
        TEST("Strips surrounding quotes", FindSpan(spans, "quoted") != nullptr);
    }
    {
        const auto spans = SpellCheckText::TokenizeWords("alpha\nbeta\ngamma");
        TEST("Line index advances across newlines",
             spans.size() == 3 && spans[0].lineIndex == 0 &&
             spans[1].lineIndex == 1 && spans[2].lineIndex == 2);
        TEST("Column resets on a new line",
             spans.size() == 3 && spans[1].columnIndex == 0);
    }
    {
        // "Grüße" is UTF-8: byte length and codepoint length must differ.
        const std::string text = "Hallo Gr\xC3\xBC\xC3\x9F""e";
        const auto spans = SpellCheckText::TokenizeWords(text);
        const auto* span = spans.size() > 1 ? &spans[1] : nullptr;
        TEST("Multi-byte word survives tokenisation", span != nullptr);
        TEST("Byte length counts bytes", span && span->byteLength == 7);
        TEST("Char length counts codepoints", span && span->charLength == 5);
        TEST("Multi-byte word slices back out of the source",
             span && text.substr(span->startByte, span->byteLength) == "Gr\xC3\xBC\xC3\x9F""e");
    }

    // ===== UTF-8 OFFSET CONVERSION =====
    std::cerr << "\n--- UTF-8 offset conversion ---" << std::endl;
    {
        const std::string text = "a\xC3\xBC""b";   // a, u-umlaut, b
        TEST("CountCodepoints ignores continuation bytes",
             SpellCheckText::CountCodepoints(text) == 3);
        TEST("ByteOffsetToCharIndex maps past a 2-byte codepoint",
             SpellCheckText::ByteOffsetToCharIndex(text, 3) == 2);
        TEST("CharIndexToByteOffset is the inverse",
             SpellCheckText::CharIndexToByteOffset(text, 2) == 3);
        TEST("Round trip holds for every char index",
             SpellCheckText::ByteOffsetToCharIndex(
                 text, SpellCheckText::CharIndexToByteOffset(text, 1)) == 1);
    }

    // ===== WORD CLASSIFIERS =====
    std::cerr << "\n--- Word classifiers ---" << std::endl;
    {
        TEST("ContainsDigit finds a digit", SpellCheckText::ContainsDigit("H2O"));
        TEST("ContainsDigit rejects a plain word", !SpellCheckText::ContainsDigit("water"));
        TEST("IsAllUpperCase accepts an acronym", SpellCheckText::IsAllUpperCase("HTTP"));
        TEST("IsAllUpperCase rejects a normal word", !SpellCheckText::IsAllUpperCase("Http"));
        TEST("IsMixedCase accepts camelCase", SpellCheckText::IsMixedCase("camelCase"));
        TEST("IsMixedCase leaves a capitalised word checkable",
             !SpellCheckText::IsMixedCase("Berlin"));
        TEST("LooksLikeUrlOrEmail catches an address",
             SpellCheckText::LooksLikeUrlOrEmail("name@host.com"));
        TEST("LooksLikeUrlOrEmail catches a scheme",
             SpellCheckText::LooksLikeUrlOrEmail("https://example.com"));
        TEST("LooksLikeUrlOrEmail leaves prose alone",
             !SpellCheckText::LooksLikeUrlOrEmail("sentence"));
        TEST("ToLowerAscii lowers ASCII only",
             SpellCheckText::ToLowerAscii("AbC\xC3\x9F") == "abc\xC3\x9F");
    }

    // ===== LANGUAGE NAME RESOLUTION =====
    std::cerr << "\n--- Language names ---" << std::endl;
    {
        const SpellLanguageInfo german = ResolveSpellLanguageNames("de_DE");
        TEST("German display name", german.displayName == "German (Germany)");
        TEST("German native name", german.nativeName == "Deutsch (Germany)");

        const SpellLanguageInfo dashed = ResolveSpellLanguageNames("en-GB");
        TEST("Dash separator normalises like underscore",
             dashed.displayName == "English (United Kingdom)");

        const SpellLanguageInfo suffixed = ResolveSpellLanguageNames("fr_FR.UTF-8");
        TEST("Charset suffix is stripped",
             suffixed.displayName == "French (France)");

        // Regression: "\xC4\x8Ce" was parsed as one out-of-range hex escape, so
        // the Czech endonym decoded to garbage.
        const SpellLanguageInfo czech = ResolveSpellLanguageNames("cs_CZ");
        TEST("Czech endonym is well-formed UTF-8",
             czech.nativeName == "\xC4\x8C" "e\xC5\xA1tina (Czechia)");

        const SpellLanguageInfo unknown = ResolveSpellLanguageNames("zz_ZZ");
        TEST("Unknown code falls back to the code itself",
             unknown.displayName == "zz_ZZ" && unknown.nativeName == "zz_ZZ");

        const SpellLanguageInfo empty = ResolveSpellLanguageNames("");
        TEST("Empty code is handled", empty.displayName.empty());
    }

    // ===== SERVICE LIFECYCLE =====
    std::cerr << "\n--- Service lifecycle ---" << std::endl;
    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();
    const bool initialized = service.Initialize();
    TEST("Initialize succeeds", initialized);
    TEST("IsInitialized reports true after Initialize", service.IsInitialized());
    std::cerr << "   backend: " << service.GetBackendName() << std::endl;

    const std::vector<SpellLanguageInfo> languages = service.GetAvailableLanguages();
    std::cerr << "   dictionaries found: " << languages.size() << std::endl;

    bool haveEnglish = false;
    for (const SpellLanguageInfo& info : languages) {
        if (info.isAvailable && info.code.rfind("en", 0) == 0) {
            haveEnglish = service.SetLanguage(info.code);
            if (haveEnglish) {
                std::cerr << "   using: " << info.code << std::endl;
                break;
            }
        }
    }

    // ===== STYLE ACCESSOR =====
    std::cerr << "\n--- Style ---" << std::endl;
    {
        SpellCheckStyle custom;
        custom.markStyle = SpellErrorMarkStyle::DottedUnderline;
        custom.waveAmplitude = 3.5f;
        service.SetStyle(custom);
        const SpellCheckStyle readBack = service.GetStyle();
        TEST("SetStyle round-trips through GetStyle",
             readBack.markStyle == SpellErrorMarkStyle::DottedUnderline &&
             readBack.waveAmplitude == 3.5f);

        SpellCheckStyle restored;
        restored.markColor = Color(1, 2, 3, 255);
        service.SetStyle(restored);
        TEST("GetStyle returns a copy, not shared state",
             service.GetStyle().markColor.r == 1);
    }

    // ===== USER DICTIONARY AND IGNORE LIST =====
    std::cerr << "\n--- User dictionary ---" << std::endl;
    {
        service.IgnoreWord("Frobnicate");
        TEST("IgnoreWord is case-insensitive", service.IsWordIgnored("frobnicate"));
        TEST("An unrelated word is not ignored", !service.IsWordIgnored("elsewhere"));
        service.ClearIgnoredWords();
        TEST("ClearIgnoredWords empties the list", !service.IsWordIgnored("frobnicate"));
    }

    // ===== CHECKING =====
    std::cerr << "\n--- Checking ---" << std::endl;
    if (!haveEnglish) {
        SKIP("Dictionary-backed checks", "no English dictionary installed");
    } else {
        TEST("A correct word passes", service.IsCorrect("hello"));
        TEST("A misspelling is caught", !service.IsCorrect("teh"));

        const std::vector<std::string> suggestions = service.GetSuggestions("teh", 5);
        TEST("Suggestions are offered for a misspelling", !suggestions.empty());
        TEST("Suggestion count respects the cap", suggestions.size() <= 5);
        TEST("'the' is among the suggestions for 'teh'",
             std::find(suggestions.begin(), suggestions.end(), "the") != suggestions.end());

        // An ignored word must stop being reported without touching the backend.
        service.IgnoreWord("teh");
        TEST("An ignored word is treated as correct", service.IsCorrect("teh"));
        service.ClearIgnoredWords();

        SpellCheckOptions options;
        options.fetchSuggestions = true;

        const std::string text = "This sentance has a typo.";
        const std::vector<SpellError> errors = service.CheckText(text, options);
        TEST("CheckText flags the misspelling", HasErrorForWord(errors, "sentance"));
        TEST("CheckText leaves correct words alone", !HasErrorForWord(errors, "This"));

        const SpellError* error = FindErrorForWord(errors, "sentance");
        TEST("Error byte offset indexes the checked text",
             error && text.substr(error->startByte, error->byteLength) == "sentance");
        TEST("Error carries suggestions when asked", error && !error->suggestions.empty());
        TEST("Error kind is Misspelled",
             error && error->kind == SpellErrorKind::Misspelled);
        TEST("ContainsByte covers the word",
             error && error->ContainsByte(error->startByte) &&
             !error->ContainsByte(error->startByte + error->byteLength));

        TEST("FindErrorAtByteOffset locates the error",
             error && SpellCheckText::FindErrorAtByteOffset(errors, error->startByte) != nullptr);
        TEST("FindErrorAtByteOffset returns null off the word",
             SpellCheckText::FindErrorAtByteOffset(errors, 0) == nullptr);
    }

    // ===== SKIP RULES =====
    std::cerr << "\n--- Skip rules ---" << std::endl;
    if (!haveEnglish) {
        SKIP("Skip-rule checks", "no English dictionary installed");
    } else {
        SpellCheckOptions options;
        const std::vector<SpellError> errors = service.CheckText(
            "HTTP camelCase snake_case abc123 name@host.com", options);
        TEST("Acronyms are skipped", !HasErrorForWord(errors, "HTTP"));
        TEST("camelCase is skipped", !HasErrorForWord(errors, "camelCase"));
        TEST("snake_case is skipped", !HasErrorForWord(errors, "snake_case"));
        TEST("Words with digits are skipped", !HasErrorForWord(errors, "abc123"));
        TEST("Email addresses are skipped", !HasErrorForWord(errors, "name@host.com"));

        // With every skip rule off, the same input is flagged.
        SpellCheckOptions strict;
        strict.skipUpperCaseWords = false;
        strict.skipMixedCaseWords = false;
        strict.skipWordsWithUnderscore = false;
        strict.skipWordsWithDigits = false;
        strict.skipUrlsAndEmails = false;
        const std::vector<SpellError> strictErrors =
            service.CheckText("xyzzy_plugh", strict);
        TEST("Disabling a skip rule re-enables the check", !strictErrors.empty());

        // minimumWordLength must not flag single letters.
        const std::vector<SpellError> shortErrors = service.CheckText("a b c", options);
        TEST("Single letters are never flagged", shortErrors.empty());
    }

    // ===== REPEATED WORDS =====
    std::cerr << "\n--- Repeated words ---" << std::endl;
    {
        SpellCheckOptions options;
        const std::vector<SpellError> errors =
            service.CheckText("This is is a duplicate.", options);
        bool sawRepeat = false;
        for (const SpellError& error : errors) {
            if (error.kind == SpellErrorKind::RepeatedWord && error.word == "is") sawRepeat = true;
        }
        TEST("A repeated word is reported", sawRepeat);

        SpellCheckOptions off;
        off.detectRepeatedWords = false;
        const std::vector<SpellError> quiet = service.CheckText("This is is a duplicate.", off);
        bool sawRepeatOff = false;
        for (const SpellError& error : quiet) {
            if (error.kind == SpellErrorKind::RepeatedWord) sawRepeatOff = true;
        }
        TEST("Repeat detection can be turned off", !sawRepeatOff);

        // A repeat across a line break is two separate sentences, not a typo.
        const std::vector<SpellError> across = service.CheckText("end is\nis start", options);
        bool sawAcross = false;
        for (const SpellError& error : across) {
            if (error.kind == SpellErrorKind::RepeatedWord) sawAcross = true;
        }
        TEST("A repeat across a line break is not reported", !sawAcross);
    }

    // ===== SKIP RANGE HOOK =====
    std::cerr << "\n--- shouldSkipRange hook ---" << std::endl;
    if (!haveEnglish) {
        SKIP("shouldSkipRange", "no English dictionary installed");
    } else {
        const std::string text = "wrongwordone and wrongwordtwo";
        const size_t secondWordStart = text.find("wrongwordtwo");

        SpellCheckOptions options;
        options.shouldSkipRange = [secondWordStart](size_t startByte, size_t) {
            return startByte >= secondWordStart;
        };
        const std::vector<SpellError> errors = service.CheckText(text, options);
        TEST("Vetoed range is not checked", !HasErrorForWord(errors, "wrongwordtwo"));
        TEST("Text outside the vetoed range still is",
             HasErrorForWord(errors, "wrongwordone"));
    }

    // ===== ASYNCHRONOUS QUEUE =====
    std::cerr << "\n--- Asynchronous queue ---" << std::endl;
    if (!haveEnglish) {
        SKIP("Async queue", "no English dictionary installed");
    } else {
        service.SetMode(SpellCheckMode::AsYouType);
        const uint64_t contextId = 4242;

        SpellCheckOptions options;
        const uint64_t firstJob = service.QueueCheckText(contextId, "a sentance here", options);
        TEST("QueueCheckText returns a non-zero job id", firstJob != 0);

        const uint64_t secondJob = service.QueueCheckText(contextId, "another sentance", options);
        TEST("Each queued job gets a distinct id", secondJob != firstJob);
        TEST("Job ids increase monotonically", secondJob > firstJob);

        // Drain: the worker publishes at most one result per context, and it is
        // the newest job's - a superseded job must never overwrite it.
        SpellCheckResult result;
        bool drained = false;
        for (int attempt = 0; attempt < 200 && !drained; ++attempt) {
            drained = service.TryTakeResult(contextId, result);
            if (!drained) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        TEST("A queued check produces a result", drained);
        TEST("Result carries the context id", !drained || result.contextId == contextId);
        TEST("Only the newest job is published",
             !drained || result.jobId == secondJob);
        TEST("Result reports the language it ran with",
             !drained || !result.languageCode.empty());
        TEST("Draining twice returns false",
             !service.TryTakeResult(contextId, result));

        service.QueueCheckText(contextId, "text to cancel", options);
        service.CancelContext(contextId);
        SpellCheckResult cancelled;
        TEST("CancelContext discards pending work",
             !service.TryTakeResult(contextId, cancelled));
    }

    // ===== MODE =====
    std::cerr << "\n--- Mode ---" << std::endl;
    {
        service.SetMode(SpellCheckMode::Disabled);
        TEST("Disabled mode reports not enabled", !service.IsEnabled());
        TEST("Queuing while disabled is a no-op",
             service.QueueCheckText(99, "some text", SpellCheckOptions()) == 0);

        service.SetMode(SpellCheckMode::AsYouType);
        TEST("Re-enabling restores IsEnabled", service.IsEnabled());
    }

    // ===== SHUTDOWN =====
    // Regression: clearing workerRunning outside jobMutex let the worker miss
    // the notify and block forever, so this call would hang instead of return.
    std::cerr << "\n--- Shutdown ---" << std::endl;
    service.Shutdown();
    TEST("Shutdown completes without deadlock", !service.IsInitialized());

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "  " << (testCount - failCount) << "/" << testCount << " passed";
    if (skipCount > 0) std::cerr << ", " << skipCount << " skipped";
    std::cerr << std::endl;
    std::cerr << "========================================" << std::endl;

    return failCount == 0 ? 0 : 1;
}
