# UltraCanvasSpellChecker Documentation

## Overview
**UltraCanvasSpellChecker** is the framework's cross-platform spell checking
service. It owns a backend, a user dictionary, a session ignore list and one
worker thread, and it builds ready-to-insert menus for the host application.

Checking never runs on the render thread: text is queued, a worker checks it,
and the element drains the finished result while it draws.

**Header:** `UltraCanvasSpellChecker.h`
**Implementation:** `UltraCanvasSpellChecker.cpp`
**Backend interface:** `ISpellCheckBackend.h`
**Version:** 1.0.1
**Last Modified:** 2026-08-24
**Author:** UltraCanvas Framework

## Backends

One native backend is compiled per platform, with Hunspell as the portable
fallback. `Initialize()` tries the native one and falls back automatically, so
an application never has to care which is in use.

| Platform | Backend | Source | Build flag |
|---|---|---|---|
| Linux / BSD | enchant-2 | `OS/Linux/UltraCanvasSpellCheckSupport.cpp` | `ULTRACANVAS_HAS_ENCHANT` |
| Windows 8+ | ISpellChecker (COM) | `OS/MSWindows/UltraCanvasSpellCheckSupport.cpp` | `ULTRACANVAS_HAS_WINSPELLCHECK` |
| macOS | NSSpellChecker | `OS/MacOS/UltraCanvasSpellCheckSupport.mm` | none needed |
| Android, WASM | none — Hunspell only | `OS/<Platform>/UltraCanvasSpellCheckSupport.cpp` | — |
| all | Hunspell (fallback) | `core/SpellCheckBackendHunspell.cpp` | `ULTRACANVAS_HAS_HUNSPELL` |

Every dependency is optional. With none present the module still compiles and
reports zero dictionaries — the build never fails over a missing one.

Exactly one translation unit per platform defines
`CreateNativeSpellCheckBackend()`. A new platform directory needs its own
(returning `nullptr` is a complete implementation) or the link fails.

### Dictionary discovery (Hunspell backend)

`<code>.aff` / `<code>.dic` pairs are searched in priority order; the first
directory holding a given code wins.

1. `$ULTRACANVAS_DICT_PATH` (`:` separated, `;` on Windows)
2. `$XDG_DATA_HOME/UltraCanvas/dictionaries`
3. `$HOME/.local/share/UltraCanvas/dictionaries`, `$HOME/.local/share/hunspell`, `$HOME/Library/Spelling`
4. `$APPDATA\UltraCanvas\dictionaries`
5. `./dictionaries`, `./Resources/dictionaries`
6. `/usr/share/hunspell`, `/usr/share/myspell`, `/usr/local/share/hunspell`, `/Library/Spelling`, `/System/Library/Spelling`

Hunspell expects words in the encoding the dictionary declares in its `SET`
directive, not UTF-8. That directive is read during discovery: UTF-8,
ISO-8859-1 and ISO-8859-15 dictionaries are converted as needed, and one in any
other encoding is reported `isAvailable = false` rather than loaded and left to
flag every accented word.

## Quick start

```cpp
#include "UltraCanvasSpellChecker.h"

UltraCanvasSpellChecker& spell = UltraCanvasSpellChecker::Instance();
spell.Initialize();   // native backend, else Hunspell; language from LANG/LC_ALL
```

### Host application menu

```cpp
menuBar->AddItem(MenuItemData::Submenu("Tools", {
    UltraCanvasSpellChecker::BuildSpellCheckMenu(),
    MenuItemData::Separator(),
    MenuItemData::Action("Word Count", cmd(TexterCommand::ToolsWordCount)),
}));
```

Produces:

```
Tools
└── Spell Check
    ├── ☑ Enable Spell Check
    ├── ─────────────────────
    ├── ○ Deutsch (Germany)          ← only dictionaries actually installed
    ├── ● English (United States)
    ├── ○ Français (France)
    ├── ─────────────────────
    └── Recheck Document
```

The submenu is lambda-provided, so it is rebuilt every time it opens: the
enable check mark and the active-language radio always show live state. The
host does **not** need to rebuild its menu bar when the language changes.

Language entries are radio items sharing one group, so `UltraCanvasMenu`
enforces exclusivity itself.

To customise labels, or to splice the entries into a menu of your own:

```cpp
SpellMenuOptions options;
options.menuLabel  = "Rechtschreibung";
options.enableLabel = "Rechtschreibprüfung aktivieren";
options.useNativeLanguageNames = true;
options.iconPath = NormalizePath(GetResourcesDir() + "media/icons/spellcheck.svg");
menuBar->AddItem(UltraCanvasSpellChecker::BuildSpellCheckMenu(options));

std::vector<MenuItemData> items = UltraCanvasSpellChecker::BuildSpellCheckMenuItems(options);
std::vector<MenuItemData> languages = UltraCanvasSpellChecker::BuildLanguageMenuItems(true);
```

### Settings and style

```cpp
spell.SetMode(SpellCheckMode::AsYouType);
spell.SetLanguage("de_DE");
spell.SetUserDictionaryPath(GetConfigDir() + "/UltraTexterDictionary.txt");

SpellCheckStyle style;
style.markStyle = SpellErrorMarkStyle::WavyUnderline;
style.markColor = darkMode ? Color(248, 113, 113, 255) : Color(220, 38, 38, 255);
style.waveAmplitude = 1.6f;
spell.SetStyle(style);
```

`GetStyle()` returns **by value** — the style is mutex-guarded and another
thread may replace it while a render pass reads it.

### Callbacks

```cpp
spell.onSpellLanguageChange   = [](const std::string& code) { /* new dictionary */ };
spell.onSpellModeChange       = [](SpellCheckMode mode)     { /* enabled/disabled */ };
spell.onSpellDictionaryChange = [](const std::string& word) { /* word added/removed */ };
```

`RequestRecheck()` (what the menu's "Recheck Document" calls) clears the ignore
list and every cached result, then fires `onSpellDictionaryChange` so open
documents re-queue.

## Threading model

```
Edit lands
    ↓
QueueCheckText(contextId, text)     ← render thread, returns immediately
    ↓                                  (a pending job for the same element is
    ↓                                   replaced, so typing builds no backlog)
Worker thread: CheckText()          ← backend locked PER WORD, never per document
    ↓
completedResults[contextId]  →  context notifier fires
    ↓
TryTakeResult(contextId, result)    ← render thread, while drawing
    ↓
Store errors, redraw
```

Stale results are discarded automatically: the worker compares its `jobId`
against the newest job for that context before publishing, so a slow check that
finishes after a newer edit is dropped rather than painting marks at old
positions.

Because results are drained while rendering, a check that finishes *after* the
edit's repaint would otherwise sit undelivered. `SetContextNotifier()` closes
that gap — it fires on the worker thread once a result is published so the
caller can ask for a frame. `UltraCanvasTextArea` registers one automatically.

> **`SpellCheckOptions::shouldSkipRange` runs on the worker thread.** It must
> not touch state the render thread mutates. Capture an immutable snapshot when
> the options are built.

```cpp
SpellCheckOptions options;
options.shouldSkipRange = [snapshot](size_t startByte, size_t byteLength) -> bool {
    return snapshot->IsInsideCodeOrLinkOrMath(startByte, byteLength);
};
```

## Types

| Type | Description |
|---|---|
| `SpellCheckMode` | `Disabled`, `AsYouType`, `OnDemand`. No `None` — X11 defines it as a macro |
| `SpellErrorKind` | `Misspelled`, `RepeatedWord`, `Capitalization` |
| `SpellErrorMarkStyle` | `WavyUnderline`, `DottedUnderline`, `SolidUnderline`, `Highlight` |
| `SpellError` | One flagged word: byte, codepoint **and** line/column offsets, plus `word`, `kind`, `suggestions` |
| `SpellCheckOptions` | Skip rules, suggestion policy, and the `shouldSkipRange` veto hook |
| `SpellCheckResult` | Async result: `contextId`, `jobId`, `languageCode`, `errors` |
| `SpellCheckStyle` | Mark style, colours, wave geometry, stroke width |
| `SpellMenuOptions` | Labels and section toggles for `BuildSpellCheckMenu()` |
| `SpellLanguageInfo` | Dictionary descriptor: `code`, `displayName`, `nativeName`, `isAvailable` |
| `SpellCheckText::WordSpan` | Tokenizer output: byte/char/line/column position of one candidate word |

### Skip rules

Defaults in `SpellCheckOptions`, all individually switchable:

| Option | Default | Skips |
|---|---|---|
| `skipWordsWithDigits` | `true` | `abc123`, `H2O` |
| `skipUpperCaseWords` | `true` | `HTTP`, `UTF` |
| `skipMixedCaseWords` | `true` | `camelCase` — but not `Berlin` |
| `skipUrlsAndEmails` | `true` | `http://…`, `name@host` |
| `skipWordsWithUnderscore` | `true` | `snake_case` |
| `detectRepeatedWords` | `true` | *reports* `the the` |
| `minimumWordLength` | `2` | single letters are never flagged |

## API summary

### Lifecycle
```cpp
static UltraCanvasSpellChecker& Instance();
bool Initialize();
void Shutdown();
bool IsInitialized() const;
void SetBackend(std::unique_ptr<ISpellCheckBackend> backend);
std::string GetBackendName() const;
```

### Language
```cpp
std::vector<SpellLanguageInfo> GetAvailableLanguages() const;
bool SetLanguage(const std::string& languageCode);
std::string GetLanguage() const;
SpellLanguageInfo GetLanguageInfo(const std::string& languageCode) const;
std::string DetectPreferredLanguage() const;
```

### Mode and style
```cpp
void SetMode(SpellCheckMode mode);
SpellCheckMode GetMode() const;
bool IsEnabled() const;
void SetStyle(const SpellCheckStyle& style);
SpellCheckStyle GetStyle() const;         // by value - see above
```

### Checking
```cpp
bool IsCorrect(const std::string& word) const;
std::vector<std::string> GetSuggestions(const std::string& word, int maxCount = 8) const;
std::vector<SpellError> CheckText(const std::string& text,
                                  const SpellCheckOptions& options = SpellCheckOptions()) const;

uint64_t QueueCheckText(uint64_t contextId, const std::string& text,
                        const SpellCheckOptions& options = SpellCheckOptions());
bool TryTakeResult(uint64_t contextId, SpellCheckResult& outResult);
void CancelContext(uint64_t contextId);
void SetContextNotifier(uint64_t contextId, std::function<void()> notifier);
bool HasPendingWork() const;
```

### Dictionary
```cpp
bool AddToUserDictionary(const std::string& word);
bool RemoveFromUserDictionary(const std::string& word);
std::vector<std::string> GetUserDictionary() const;
void IgnoreWord(const std::string& word);     // session only
void ClearIgnoredWords();
bool IsWordIgnored(const std::string& word) const;
void RequestRecheck();
void SetUserDictionaryPath(const std::string& filePath);
std::string GetUserDictionaryPath() const;
bool LoadUserDictionary();
bool SaveUserDictionary() const;
```

A backend owning a persistent user dictionary (macOS, Windows, enchant) stores
the word itself and the service keeps no second copy; otherwise the word goes
to the on-disk list, whose parent directory is created on demand.

### Menus
```cpp
static MenuItemData BuildSpellCheckMenu(const SpellMenuOptions& = SpellMenuOptions());
static std::vector<MenuItemData> BuildSpellCheckMenuItems(const SpellMenuOptions& = SpellMenuOptions());
static std::vector<MenuItemData> BuildLanguageMenuItems(bool useNativeNames = true);
static std::vector<MenuItemData> BuildSuggestionMenuItems(
    const SpellError& error,
    std::function<void(const std::string&)> onApply,
    std::function<void()> onRecheck = nullptr);
```

### Text utilities — `namespace SpellCheckText`
```cpp
std::vector<WordSpan> TokenizeWords(const std::string& text);
size_t ByteOffsetToCharIndex(const std::string& text, size_t byteOffset);
size_t CharIndexToByteOffset(const std::string& text, size_t charIndex);
size_t CountCodepoints(const std::string& text);
bool ContainsDigit(const std::string& word);
bool IsAllUpperCase(const std::string& word);
bool IsMixedCase(const std::string& word);
bool LooksLikeUrlOrEmail(const std::string& word);
std::string ToLowerAscii(const std::string& word);
const SpellError* FindErrorAtByteOffset(const std::vector<SpellError>& errors, size_t byteOffset);
```

### Rendering — `namespace SpellCheckRendering`
```cpp
void DrawSpellErrorMark(IRenderContext* ctx, const Rect2Df& wordRect,
                        const SpellCheckStyle& style,
                        SpellErrorKind kind = SpellErrorKind::Misspelled);
void DrawWavyUnderline(IRenderContext* ctx, float x, float y, float width,
                       const Color& color, float amplitude, float waveLength, float strokeWidth);
void DrawDottedUnderline(IRenderContext* ctx, float x, float y, float width,
                         const Color& color, float strokeWidth);
```

Any component that can produce a word rectangle can call these — they know
nothing about elements.

## Using it in a text element

`UltraCanvasTextArea` has it wired in already; see
[UltraCanvasTextAreaExamples.md](UltraCanvasTextAreaExamples.md#spell-checking).

```cpp
textArea->SetSpellCheckEnabled(true);
```

That is the whole integration: the element queues on every edit, drains and
draws while rendering, and offers suggestions on right-click.

## Testing

| Test | What it covers |
|---|---|
| `Tests/SpellCheckerTest.cpp` | Tokenizer, skip rules, language names, dictionary, async queue, shutdown. Framework-independent |
| `Tests/TextAreaSpellCheckTest.cpp` | The element: results reaching it, byte-offset → screen-rectangle mapping, applying a suggestion. Needs a display; runs under Xvfb |

Both skip rather than fail when no dictionary (or no display) is present.

```bash
cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build
./build/bin/SpellCheckerTest
xvfb-run -a ./build/bin/TextAreaSpellCheckTest
```

## See also

- [UltraCanvasTextAreaExamples.md](UltraCanvasTextAreaExamples.md) — the element integration
- [UltraCanvasMenu.md](UltraCanvasMenu.md) — `MenuItemData`, radio groups, lambda-provided submenus
- [../Dependencies.md](../Dependencies.md) — installing hunspell / enchant and dictionaries
