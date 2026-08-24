// OS/MSWindows/UltraCanvasSpellCheckSupport.cpp
// Windows native spell check backend built on the ISpellChecker COM API
// Version: 1.0.2
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// Uses the Windows 8+ Spell Checking API (spellcheck.h), which exposes the
// same dictionaries and personal word lists as Word and Edge. On Windows 7 the
// factory returns nullptr and UltraCanvasSpellChecker falls back to Hunspell.
//
// Link: Ole32.lib
// Build flag: ULTRACANVAS_HAS_WINSPELLCHECK (define when targeting Windows 8+)
//
// THREADING: UltraCanvasSpellChecker calls this backend from two threads - the
// render thread for language management and the worker thread for checking - so
// the COM objects are created in the multi-threaded apartment and every entry
// point joins the MTA before touching them. Initialising a single apartment in
// Initialize() would make every worker-thread call fail with RPC_E_WRONG_THREAD.

// spellcheck.h gates the whole API on NTDDI_VERSION >= NTDDI_WIN8
// (MIN_SPELLING_NTDDI), and the mingw-w64 default target is far below that -
// without this the interfaces are forward declarations only and every use of
// them is an incomplete type. Same reason and same shape as the bump at the top
// of UltraCanvasWindowsFileAssociations.cpp.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0602
#endif
#if !defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06020000
#  undef NTDDI_VERSION
#  define NTDDI_VERSION 0x06020000
#endif

#include "ISpellCheckBackend.h"

#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32) && defined(ULTRACANVAS_HAS_WINSPELLCHECK)

// NOMINMAX keeps windows.h from defining min/max macros that break std::min.
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <spellcheck.h>
#include <objbase.h>
#include <algorithm>

namespace UltraCanvas {

namespace {

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) return std::wstring();

    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        &wide[0], required);
    return wide;
}

std::string WideToUtf8(const wchar_t* text) {
    if (!text) return std::string();
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                             nullptr, 0, nullptr, nullptr);
    if (required <= 1) return std::string();

    std::string narrow(static_cast<size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &narrow[0], required, nullptr, nullptr);
    return narrow;
}

// Every thread that touches COM must join an apartment first, and it must be
// the same thread that later leaves it. A thread_local scope object does both:
// it joins on first use and its destructor runs at thread exit.
//
// RPC_E_CHANGED_MODE means the host already put this thread in a single-threaded
// apartment. That is not an error and not ours to undo, so nothing is released
// in that case.
struct ComThreadScope {
    bool joinedHere = false;

    ComThreadScope() {
        joinedHere = (CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK);
    }
    ~ComThreadScope() {
        if (joinedHere) CoUninitialize();
    }

    ComThreadScope(const ComThreadScope&) = delete;
    ComThreadScope& operator=(const ComThreadScope&) = delete;
};

void EnsureComForThisThread() {
    thread_local ComThreadScope scope;
    (void)scope;
}

} // namespace

class SpellCheckBackendWindows : public ISpellCheckBackend {
public:
    ~SpellCheckBackendWindows() override {
        Shutdown();
    }

    std::string GetBackendName() const override { return "Windows ISpellChecker"; }

    bool Initialize() override {
        EnsureComForThisThread();
        std::lock_guard<std::mutex> lock(mutex);
        if (factory) return true;

        const HRESULT createResult = CoCreateInstance(
            __uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));

        return SUCCEEDED(createResult) && factory != nullptr;
    }

    void Shutdown() override {
        std::lock_guard<std::mutex> lock(mutex);
        ReleaseCheckerLocked();
        if (factory) {
            factory->Release();
            factory = nullptr;
        }
        // COM apartment membership belongs to each thread that joined it and is
        // released by that thread's ComThreadScope destructor, never here.
        currentLanguage.clear();
    }

    std::vector<SpellLanguageInfo> EnumerateLanguages() override {
        EnsureComForThisThread();
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<SpellLanguageInfo> languages;
        if (!factory) return languages;

        IEnumString* enumerator = nullptr;
        if (FAILED(factory->get_SupportedLanguages(&enumerator)) || !enumerator) {
            return languages;
        }

        LPOLESTR entry = nullptr;
        while (enumerator->Next(1, &entry, nullptr) == S_OK && entry) {
            const std::string code = WideToUtf8(entry);
            CoTaskMemFree(entry);
            entry = nullptr;
            if (code.empty()) continue;

            SpellLanguageInfo info = ResolveSpellLanguageNames(code);
            info.code = code;
            info.isAvailable = true;
            languages.push_back(std::move(info));
        }

        enumerator->Release();
        return languages;
    }

    bool SetLanguage(const std::string& languageCode) override {
        EnsureComForThisThread();
        std::lock_guard<std::mutex> lock(mutex);
        if (!factory) return false;

        const std::wstring wide = Utf8ToWide(languageCode);
        if (wide.empty()) return false;

        BOOL supported = FALSE;
        if (FAILED(factory->IsSupported(wide.c_str(), &supported)) || !supported) {
            return false;
        }

        ISpellChecker* requested = nullptr;
        if (FAILED(factory->CreateSpellChecker(wide.c_str(), &requested)) || !requested) {
            return false;
        }

        ReleaseCheckerLocked();
        checker = requested;
        currentLanguage = languageCode;
        return true;
    }

    std::string GetLanguage() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return currentLanguage;
    }

    bool IsWordCorrect(const std::string& word) override {
        EnsureComForThisThread();
        std::lock_guard<std::mutex> lock(mutex);
        if (!checker) return true;

        const std::wstring wide = Utf8ToWide(word);
        if (wide.empty()) return true;

        IEnumSpellingError* errors = nullptr;
        if (FAILED(checker->Check(wide.c_str(), &errors)) || !errors) return true;

        ISpellingError* firstError = nullptr;
        const bool hasError = (errors->Next(&firstError) == S_OK && firstError != nullptr);

        if (firstError) firstError->Release();
        errors->Release();
        return !hasError;
    }

    std::vector<std::string> GetSuggestions(const std::string& word, int maxCount) override {
        EnsureComForThisThread();
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> suggestions;
        if (!checker) return suggestions;

        const std::wstring wide = Utf8ToWide(word);
        if (wide.empty()) return suggestions;

        IEnumString* enumerator = nullptr;
        if (FAILED(checker->Suggest(wide.c_str(), &enumerator)) || !enumerator) {
            return suggestions;
        }

        LPOLESTR entry = nullptr;
        while (enumerator->Next(1, &entry, nullptr) == S_OK && entry) {
            std::string suggestion = WideToUtf8(entry);
            CoTaskMemFree(entry);
            entry = nullptr;
            if (!suggestion.empty()) suggestions.push_back(std::move(suggestion));
            if (maxCount > 0 && suggestions.size() >= static_cast<size_t>(maxCount)) break;
        }

        enumerator->Release();
        return suggestions;
    }

    bool AddWordToBackendDictionary(const std::string& word) override {
        EnsureComForThisThread();
        std::lock_guard<std::mutex> lock(mutex);
        if (!checker) return false;

        const std::wstring wide = Utf8ToWide(word);
        if (wide.empty()) return false;

        // Adds to the per-user Windows custom dictionary shared with Office.
        return SUCCEEDED(checker->Add(wide.c_str()));
    }

    bool IsThreadSafe() const override { return false; }

private:
    void ReleaseCheckerLocked() {
        if (checker) {
            checker->Release();
            checker = nullptr;
        }
    }

    mutable std::mutex mutex;
    ISpellCheckerFactory* factory = nullptr;
    ISpellChecker* checker = nullptr;
    std::string currentLanguage;
};

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return std::make_unique<SpellCheckBackendWindows>();
}

} // namespace UltraCanvas

#else // Windows 7 or build flag not set

namespace UltraCanvas {

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return nullptr;   // Service falls back to the Hunspell backend
}

} // namespace UltraCanvas

#endif
