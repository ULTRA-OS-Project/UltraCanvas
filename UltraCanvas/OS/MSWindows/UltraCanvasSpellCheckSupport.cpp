// OS/MSWindows/UltraCanvasSpellCheckSupport.cpp
// Windows native spell check backend built on the ISpellChecker COM API
// Version: 1.1.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
//
// Uses the Windows 8+ Spell Checking API (spellcheck.h), which exposes the
// same dictionaries and personal word lists as Word and Edge. On Windows 7 the
// factory returns nullptr and UltraCanvasSpellChecker falls back to Hunspell.
//
// Link: Ole32.lib
// Build flag: ULTRACANVAS_HAS_WINSPELLCHECK (define when targeting Windows 8+)
//
// THREADING - why every COM object here is per thread
//   UltraCanvasSpellChecker calls this backend from two threads: the UI thread
//   manages the language and asks for suggestions, the spell worker thread runs
//   the checks. A COM object belongs to the apartment of the thread that
//   created it, and UltraCanvas always calls OleInitialize() on the UI thread
//   (UltraCanvasWindowsApplication.cpp), which puts that thread in a *single
//   threaded* apartment. A factory created there is therefore STA-bound, and
//   calling ISpellChecker::Check() on it from the worker thread is a
//   cross-apartment call on a raw pointer - unsupported, and in practice it
//   fails, which the service reads as "every word is spelled correctly": no
//   squiggle ever appears while the menus keep working, because everything the
//   menus do runs on the UI thread.
//
//   An earlier revision tried to put the objects in the MTA by joining it in
//   every entry point. That cannot work: a thread already in an STA stays
//   there, CoInitializeEx returns RPC_E_CHANGED_MODE, and the objects end up in
//   the STA anyway.
//
//   So no COM pointer is shared between threads. Each thread creates its own
//   factory and its own ISpellChecker in its own apartment, and releases them
//   when it exits. Only the selected language is shared state; a generation
//   counter tells each thread when its checker is out of date. This is legal
//   for an in-process server, needs no marshalling, and lets the UI and worker
//   threads check words at the same time.

// spellcheck.h gates the whole API on NTDDI_VERSION >= NTDDI_WIN8
// (MIN_SPELLING_NTDDI), and the mingw-w64 default target is far below that -
// without this the interfaces are forward declarations only and every use of
// them is an incomplete type. Same reason and same shape as the bump at the top
// of UltraCanvasWindowsFileAssociations.cpp.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0602
#endif
// Keep NTDDI_VERSION consistent with _WIN32_WINNT: the Windows SDK's sdkddkver.h errors on a
// mismatch when the host build already sets a higher _WIN32_WINNT (e.g. Ladybird's 0x0A00). NTDDI's
// high word IS the _WIN32_WINNT value, so derive it the way the SDK does by default; the floor above
// keeps _WIN32_WINNT (hence NTDDI) >= Win8. Works under both MinGW-w64 and MSVC/clang-cl.
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "ISpellCheckBackend.h"

#include <atomic>
#include <cstdint>
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
// apartment - which is exactly what the UI thread is, because UltraCanvas calls
// OleInitialize() there for drag and drop. That is not an error and not ours to
// undo, so nothing is released in that case and the objects this thread creates
// simply live in that STA.
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

// One factory and one checker per thread, in that thread's own apartment - see
// the threading note at the top of the file.
//
// The constructor joins an apartment before anything is created, so the
// thread_local ComThreadScope is constructed first and therefore destroyed
// last: COM is still alive when this object releases its pointers at thread
// exit. `owner` identifies the backend instance the pointers belong to, so a
// service that was shut down and initialised again does not leave a thread
// holding a checker from the previous one.
struct ThreadCheckerState;

// Set while this thread's state exists. A raw pointer is constant-initialised
// and has no destructor of its own, so it can be read at any point in a
// thread's life - including while static and thread-local objects are being
// torn down, where merely naming the state below could resurrect an object
// that has already been destroyed.
thread_local ThreadCheckerState* activeThreadState = nullptr;

struct ThreadCheckerState {
    ThreadCheckerState() {
        EnsureComForThisThread();
        activeThreadState = this;
    }
    ~ThreadCheckerState() {
        Release();
        activeThreadState = nullptr;
    }

    void ReleaseChecker() {
        if (checker) {
            checker->Release();
            checker = nullptr;
        }
        language.clear();
        generation = 0;
    }

    void Release() {
        ReleaseChecker();
        if (factory) {
            factory->Release();
            factory = nullptr;
        }
        owner = 0;
    }

    ISpellCheckerFactory* factory = nullptr;
    ISpellChecker* checker = nullptr;
    std::string language;      // language `checker` was created for
    uint64_t generation = 0;   // owner's languageGeneration at that moment
    uint64_t owner = 0;        // backend instance the pointers belong to

    ThreadCheckerState(const ThreadCheckerState&) = delete;
    ThreadCheckerState& operator=(const ThreadCheckerState&) = delete;
};

ThreadCheckerState& CurrentThreadState() {
    thread_local ThreadCheckerState state;
    return state;
}

// This thread's state only if it already exists - never creates one.
ThreadCheckerState* ExistingThreadState() {
    return activeThreadState;
}

uint64_t NextBackendInstanceId() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

} // namespace

class SpellCheckBackendWindows : public ISpellCheckBackend {
public:
    SpellCheckBackendWindows() : instanceId(NextBackendInstanceId()) {}

    ~SpellCheckBackendWindows() override {
        Shutdown();
    }

    std::string GetBackendName() const override { return "Windows ISpellChecker"; }

    bool Initialize() override {
        return EnsureFactory(CurrentThreadState()) != nullptr;
    }

    void Shutdown() override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            currentLanguage.clear();
            ++languageGeneration;
        }
        // Only this thread's objects can be released from here; every other
        // thread drops its own on its next call (the owner id no longer
        // matches) or at thread exit. COM apartment membership likewise belongs
        // to each thread that joined it.
        //
        // Nothing is created here: Shutdown() also runs from the destructor,
        // which on the main thread happens while everything else is being torn
        // down, and a thread that never checked anything has nothing to release.
        if (ThreadCheckerState* state = ExistingThreadState()) state->Release();
    }

    std::vector<SpellLanguageInfo> EnumerateLanguages() override {
        std::vector<SpellLanguageInfo> languages;

        ISpellCheckerFactory* factory = EnsureFactory(CurrentThreadState());
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
        ThreadCheckerState& state = CurrentThreadState();
        ISpellCheckerFactory* factory = EnsureFactory(state);
        if (!factory) return false;

        const std::wstring wide = Utf8ToWide(languageCode);
        if (wide.empty()) return false;

        BOOL supported = FALSE;
        if (FAILED(factory->IsSupported(wide.c_str(), &supported)) || !supported) {
            return false;
        }

        // Create the checker before publishing the language, so a failure here
        // leaves the previous dictionary in place rather than none at all.
        ISpellChecker* requested = nullptr;
        if (FAILED(factory->CreateSpellChecker(wide.c_str(), &requested)) || !requested) {
            return false;
        }

        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            currentLanguage = languageCode;
            generation = ++languageGeneration;
        }

        // This thread is already up to date; the others rebuild on their next
        // call, when they see the new generation.
        state.ReleaseChecker();
        state.checker = requested;
        state.language = languageCode;
        state.generation = generation;
        return true;
    }

    std::string GetLanguage() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return currentLanguage;
    }

    // The locale Windows itself is set to, so a first run picks the user's own
    // language instead of whatever dictionary happens to enumerate first.
    std::string GetPreferredLanguageHint() override {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {0};
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
            return WideToUtf8(name);
        }
        return std::string();
    }

    bool IsWordCorrect(const std::string& word) override {
        ISpellChecker* checker = EnsureChecker(CurrentThreadState());
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
        std::vector<std::string> suggestions;

        ISpellChecker* checker = EnsureChecker(CurrentThreadState());
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
        ISpellChecker* checker = EnsureChecker(CurrentThreadState());
        if (!checker) return false;

        const std::wstring wide = Utf8ToWide(word);
        if (wide.empty()) return false;

        // Adds to the per-user Windows custom dictionary shared with Office.
        // ISpellChecker::Add raises its own change event, so every checker for
        // this language - including the other threads' - sees the word.
        return SUCCEEDED(checker->Add(wide.c_str()));
    }

    // Each thread owns its COM objects, so no call here needs the service's
    // per-word serialisation.
    bool IsThreadSafe() const override { return true; }

private:
    // Creates this thread's factory on first use, and drops anything left over
    // from a previous backend instance.
    ISpellCheckerFactory* EnsureFactory(ThreadCheckerState& state) {
        if (state.owner != instanceId) {
            state.Release();
            state.owner = instanceId;
        }
        if (state.factory) return state.factory;

        const HRESULT createResult = CoCreateInstance(
            __uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&state.factory));
        if (FAILED(createResult)) state.factory = nullptr;
        return state.factory;
    }

    // Returns this thread's checker for the selected language, creating or
    // rebuilding it when the language changed since the thread last looked.
    ISpellChecker* EnsureChecker(ThreadCheckerState& state) {
        std::string wanted;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            wanted = currentLanguage;
            generation = languageGeneration;
        }

        if (!EnsureFactory(state)) return nullptr;
        if (state.checker && state.generation == generation && state.language == wanted) {
            return state.checker;
        }

        state.ReleaseChecker();
        if (wanted.empty()) return nullptr;

        const std::wstring wide = Utf8ToWide(wanted);
        if (wide.empty()) return nullptr;

        ISpellChecker* created = nullptr;
        if (FAILED(state.factory->CreateSpellChecker(wide.c_str(), &created)) || !created) {
            return nullptr;
        }

        state.checker = created;
        state.language = wanted;
        state.generation = generation;
        return state.checker;
    }

    const uint64_t instanceId;

    // Guards the selected language only - no COM pointer is shared.
    mutable std::mutex mutex;
    std::string currentLanguage;
    uint64_t languageGeneration = 0;
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
