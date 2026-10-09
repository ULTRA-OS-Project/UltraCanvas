# AGENTS.md — guidance for AI coding assistants

This file tells AI assistants (Claude, Copilot, Cursor, Windsurf, …) how to
work productively in this repository and in applications built on it.
Human-oriented docs start at [README.md](README.md).

## What this repository is

UltraCanvas is a modular, cross-platform **C++20 UI and rendering framework**
(Windows, Linux, macOS, WebAssembly, ULTRA OS) plus sibling modules:

| Module | Purpose | Where |
|---|---|---|
| UltraCanvas | UI widgets, layout, rendering, events | `UltraCanvas/{include,core,libspecific,OS/<Platform>,Plugins}` |
| UltraAI | Provider-agnostic AI capabilities (LLM, STT, TTS, image/video/music gen, …) | `UltraAI/` |
| UltraNet | Networking (HTTP, WebSocket, FTP, TCP/UDP, TLS, DNS) | `UltraCanvas/core/UltraNet`, `Docs/Modules/UltraNet` |
| NetworkMonitor | System-wide socket table with the owning process (not UltraNet: observes other processes) | `UltraCanvas/{include,core}/NetworkMonitor`, `Docs/Modules/NetworkMonitor` |
| FileLoader | Universal file load/save/convert facade | `Docs/Modules/FileLoader` |
| VirtualFS | Virtual filesystem and compression | `VirtualFS/` |
| VideoFX | Video probing, frames, trim / effects / joins / export (on FFmpeg) | `VideoFX/`, `Docs/Modules/VideoFX` |
| File-type plugins | Charts, diagrams, vector, documents, video, … | `UltraCanvas/Plugins/` |

The authoritative module registry — purpose and public function surface of
every module — is [`Masterfile_modules.md`](Masterfile_modules.md). Read it
before adding cross-module code.

## Documentation map (read before using a component)

- `Docs/UltraCanvas/UltraCanvasUIElements.md` — **the UI element catalogue:
  what exists, and which element to use for what.** Start here when building
  UI; the per-component docs below only help once you know the name.
- `Docs/UltraCanvas/` — ~100 per-component/per-subsystem docs with buildable
  C++ examples. Naming: `UltraCanvas<Component>.md` or
  `UltraCanvas<Component>Examples.md` (e.g. `UltraCanvasButtonExamples.md`,
  `UltraCanvasLineChartElement.md`, `UltraCanvasJSON.md`).
  **Consult the matching doc before writing code that uses a component —
  do not guess APIs from other frameworks.**
- `Docs/Modules/<Name>/README.md` — sibling-module docs (UltraAI, UltraNet,
  UltraDatabase, FileLoader, VirtualFS, OCR, PDF, QRCode, …). The UltraAI,
  UltraNet and VirtualFS ones are mirrors of `<Name>/README.md`: edit the
  module's copy and run `python3 scripts/generate_llms_txt.py`; CI fails when
  a mirror is stale (`MIRRORED_READMES` in the script lists them).
- `Docs/CSSLayout.md`, `Docs/Dependencies.md` — layout engine and
  third-party dependency policy.
- `llms.txt` / `llms-full.txt` (repo root, generated) — machine-readable
  index / full concatenation of the docs corpus for LLM consumption.
  Regenerate with `python3 scripts/generate_llms_txt.py` after editing docs.

## Core conventions

- **Language:** C++20 (Objective-C++ for macOS backends). Build: CMake ≥ 3.16.
- **Naming:** PascalCase for all identifiers (`GetText`, `SetStyle`,
  `UltraCanvasButton`). New APIs must be understandable from their names.
- **Namespace:** framework code lives in `namespace UltraCanvas`.
- **Widget creation:** widgets are `std::shared_ptr`-managed; use the factory
  helpers where they exist:

  ```cpp
  auto button = CreateButton("MyButton", 100, 50, 120, 40, "Click Me");   // identifier, x, y, w, h, text
  // equivalent: std::make_shared<UltraCanvasButton>("MyButton", 100, 50, 120, 40, "Click Me")
  // There is no numeric id argument on any element factory or constructor.
  ```

- **Never hand-roll a UI element** — see
  [Build UI out of UltraCanvas elements](#build-ui-out-of-ultracanvas-elements)
  below. If the thing you are drawing takes input, shows a picture or presents
  a value, it is an element: use the framework's, or add one.
- **HTML, CSS and HTML entities are read through the HTMLReader module
  (`UltraCanvas/{include,core}/HTMLReader/`, always built) and nowhere else.**
  It has the parser and DOM (`HTML::Parser`, `HTML::Document`), the CSS
  parser with selector matching for any tree (`HTML::StyleSheet`,
  `HTML::MatchingRules<Traits>`), the cascade (`HTML::StyleResolver`), the
  builder of native element trees (`HTML::ElementBuilder`), the importer into
  an editable `UCRichDocument` (`ImportHTMLToRichDocument`), and the two
  helpers that keep being rewritten: `HTML::DecodeEntities` and
  `HTML::ExtractPlainText`. Do not write another tag stripper, entity table,
  `style=""` splitter or selector matcher — seven had accumulated by
  2026-10, in the Filer preview, two places in UltraMail, EmailCleaner, the
  rich document's paste path and two SVG readers, each with a different
  handful of entities. A tree of your own (SVG, XML) matches CSS selectors by
  supplying a ten-line Traits type, not a matcher. What the module lacks is
  added to it, so the next caller finds it. `scripts/check_html_reuse.py`
  enforces this in CI (`html-reuse.yml`); `scripts/html_reuse_baseline.txt`
  lists the sites that predate the rule and only shrinks. A site that must
  stay says why with `// html-reuse-exempt: <why>`. Doc:
  `Docs/UltraCanvas/UltraCanvasHTMLReader.md`.
- **Application bootstrap:** apps are built around `UltraCanvasApplication`
  (see `Apps/Texter/main.cpp` and `Apps/DemoApp/` for canonical structure).
- **Platform separation:** platform-specific code goes only under
  `UltraCanvas/OS/<Platform>/`; shared logic in `UltraCanvas/core/`;
  library-specific rendering backends in `UltraCanvas/libspecific/`.
- **Wrapped engines:** public engines are always wrapped behind an
  UltraCanvas-owned API (e.g. `UltraCanvasJSON` wraps yyjson) so backing
  implementations can be swapped. Never expose a third-party type in a
  public header; never call vendored libraries directly from app code.
- **UltraNet/UltraDatabase rules:** TLS verification ON by default;
  blocking ops return `UltraNetResult`/`UltraDbResult`; connection/handle
  ops return `UltraNetHandle`/`UltraDbHandle`; SQL uses parameter binding
  only. Follow existing naming patterns when adding protocols/drivers.
- **Numbers in file formats are dot-decimal, never locale-decimal.** Parse
  them with `TryParseFloat` / `ParseFloatClassic` (`UltraCanvasTextUtils.h`),
  never `std::stof` / `std::stod` / `atof` / `strtod`, and format them through
  a `std::locale::classic()`-imbued stream, never `snprintf("%f")` or
  `std::to_string(double)`. Those all honour `LC_NUMERIC`, and the Linux
  backend calls `setlocale(LC_ALL, "")` for XIM — so on a comma-decimal
  desktop an SVG `opacity="0.25"` read as 0 and the shape vanished, and the
  SVG writer emitted `M 1,5`, which reads back as the point (1, 5). This has
  now been fixed three times - in CSS, in SVG, and across the file-format
  readers and writers in 0.9.42 - which is why the rule is now checked rather
  than remembered: `scripts/check_locale_numbers.py` blocks a new one, and
  `scripts/locale_numbers_baseline.txt` lists the sites still to fix. A number
  a person typed or reads follows their locale on purpose and says so at the
  site with `// locale-ok: <why>`.
- **File paths and names are UTF-8, on every platform, in every
  application.** Any file or folder name must work, whatever script it is
  in: Thai, CJK, Cyrillic, emoji, accents. Convert between
  `std::filesystem::path` and `std::string` only with `PathToUtf8(p)` and
  `PathFromUtf8(s)`, and open a UTF-8 path with `OpenFileUtf8(s, mode)`
  instead of `std::fopen` (all three are in `UltraCanvasPathUtf8.h`, which is
  header-only, C++17 and has no link dependency, so headless engines and
  VirtualFS use it too). Never write `p.string()`, `p.generic_string()` or
  `fs::path(someString)`. On Windows the builds use libc++, where those go
  through the ANSI code page: `.string()` *throws* ("filesystem error: in
  __wide_to_char: Illegal byte sequence") on a name the code page cannot
  hold, and `fs::path(utf8)` quietly names a different file. That is how
  UltraFiler quit on a Thai Windows 10 machine the moment it opened a folder.
  The manifests' UTF-8 `activeCodePage` does not make this safe, because
  Windows before 10 version 1903 ignores it. For the same reason, hand a
  path to a Win32 call through the `W` API (`LoadLibraryW(p.c_str())`), not
  the `A` one. `scripts/check_path_string.py` enforces this in CI
  (`path-strings.yml`), and `scripts/path_string_baseline.txt` is empty and
  must stay that way. A path built from a wide string or a `std::u8string`
  is already correct; say so at the site with `// path-string-ok: <why>`.
  The implicit forms are just as wrong and are checked too: a UTF-8 string
  handed straight to `fs::exists(str)`, `fs::remove(str, ec)`,
  `fs::directory_iterator(str)`, `std::ifstream f(str)`, `f.open(str)` or
  `fs::path p = str;` converts through the code page as well (`path-implicit`),
  and so does a string joined onto a path with `/` or `/=`:
  `PathFromUtf8(dir) / accountId / folder` cached UltraMail's bodies under a
  mangled folder name on Windows until 0.10.18 - write
  `PathFromUtf8(dir) / PathFromUtf8(accountId) / PathFromUtf8(folder)`. A bare
  literal (`/ "mail"`) is ASCII and fine. `fopen(name, mode)` reads the name
  in the code page too (`fopen-narrow`). Write
  `fs::exists(PathFromUtf8(str))` and `OpenFileUtf8(name, mode)`.
  `PathFromUtf8` also takes a C string, a `string_view` and a path (passed
  through), so wrapping is never wrong. The check reads the file's own
  declarations and those of the repository headers it includes directly to
  tell a string from a path - so a class member declared in its header and a
  call to a function declared as returning `std::string`
  (`fs::exists(DeviceKeyPath())`) count - and reads a call that spans lines
  whole. A member access (`env.accountId`) is looked up through the whole
  include chain, since the struct is often a header or two further down. A
  string it still cannot see the type of (an `auto`, a member declared two
  different ways, a type from outside the repository) is review's to catch.
  A string that is not UTF-8 to begin with is not fixed by wrapping it in
  `PathFromUtf8`. The environment is the common case: Windows keeps the
  profile folders and the user's name there (`APPDATA`, `LOCALAPPDATA`,
  `USERPROFILE`, `TEMP`, `USERNAME` ...), and the narrow `getenv` /
  `_dupenv_s` answers in the ANSI code page. Read every variable with
  `GetEnvUtf8(name)` (`UltraCanvasPathUtf8.h`): UTF-8 on every platform, from
  `GetEnvironmentVariableW` on Windows. The check reports a narrow read
  (`env-narrow`) - of one of those names, anywhere in Windows-only code, or
  through a helper of the same file that reads narrowly - except for a
  deliberate one that says why (`// path-string-ok: ASCII 0/1 flag`). An
  `...A` Win32 call (`GetVolumeInformationA`, `GetTempPathA`) has the same
  problem and is review's to catch: call the `...W` one and convert with
  `PathToUtf8` or `PathUtf8Detail::Utf16ToUtf8`.
  `Tests/PathUtf8Test.cpp` runs every one of these calls on a Thai-and-emoji
  folder in Windows CI, under code page 1252.
- **No function of ours is named like a Win32 A/W macro.** `<windows.h>`
  `#define`s thousands of names to their `W` variant (`CreateFile` →
  `CreateFileW`, `LoadImage`, `SendMessage`, `GetMessage`, `ReplaceText`, …),
  and the macro renames our methods too — but only in the files that see
  windows.h. The declaring and the calling file then disagree and the Windows
  link fails with an undefined `…W` symbol that Linux and macOS never show;
  `UltraCanvasPdfSurface::CreateFile` broke both Windows builds that way.
  Pick another name (`CreateForFile`, `LoadImageFile`). A name the framework
  must keep is made safe by an `#undef` in the Windows platform headers next
  to the existing `#undef DrawText` (`UltraCanvasWindowsApplication.h`,
  `UltraCanvasWindowsWindow.h`). `scripts/check_win32_names.py` enforces this
  in CI (`win32-names.yml`) from the name list in
  `scripts/win32_aw_macros.txt`; `scripts/win32_names_baseline.txt` holds the
  sites that predate the check and only shrinks. A site that is correct as it
  stands says so with `// win32-name-ok: <why>`.
- **An inline body in a public header uses all its parameters.** It is
  compiled into every file that includes the header, so a parameter it
  leaves unused warns in every build with `-Wextra` (the Models plugin, the
  tests, Texter, AnchorPoint). A default virtual body that ignores one marks
  it `(void)name;`, as `UltraCanvasRenderContext.h` does; the signature and
  the name stay. `PublicHeadersUnusedParamTest` (Tests/CMakeLists.txt)
  includes the framework's public headers with `-Werror=unused-parameter`,
  so a new one fails the build there.
- **Third-party code** is vendored under `UltraCanvas/third_party/` and
  `3rdparty/` — do not modify it, and record licenses in
  `THIRD_PARTY_LICENSES.md`.

## Build UI out of UltraCanvas elements

The framework ships ~60 UI elements in `UltraCanvas/include/`, plus ~70 more
under `UltraCanvas/include/Plugins/` — charts, diagrams, gauges, codes and
document views, in the same library. New UI is assembled from them; it is not
painted from scratch. This is the single most-repeated mistake in this
repository, so the rule is a prohibition rather than a lookup:

> **If it takes input, shows a picture, or presents a value, it is an
> element.** Never build one out of `ctx->DrawText` / `FillRoundedRectangle`
> plus a private buffer, caret, `hovered`/`focused` flag or key handler. Take
> the element from the catalogue, or add a new one to
> `UltraCanvas/{include,core}` so the next caller finds it too.

Hand-rolled controls look fine in a screenshot and then fail everywhere the
framework already solved the problem: no caret or selection, no clipboard, no
undo, no IME or multi-byte input, no keyboard-focus semantics, no theming, no
DPI scaling, no tooltip. A real case: the Filer's compress dialog painted its
own name field, so the name could only be appended to and backspaced at the
end, and it stopped answering the keyboard entirely as soon as another element
took the focus.

**[`Docs/UltraCanvas/UltraCanvasUIElements.md`](Docs/UltraCanvas/UltraCanvasUIElements.md)
is the catalogue** — every element, what it is for, and its defining header,
including the ones under `Plugins/`. Start there; the per-component docs only
help once you know the name. Searching `include/` alone is how a second
progress bar came to be written in 2026-09 while
`UltraCanvasGaugeDiagramElement` sat in `include/Plugins/Diagrams/`. The ones
reinvented most often:

| You need | Element |
|---|---|
| Text entry (single line / multi-line / with suggestions / tags / numeric) | `UltraCanvasTextInput`, `UltraCanvasTextArea`, `UltraCanvasAutoComplete`, `UltraCanvasTagInput`, `UltraCanvasSpinner` |
| A button | `UltraCanvasButton` |
| Checkbox, radio, on-off switch | `UltraCanvasCheckbox`, `UltraCanvasRadio`, `UltraCanvasSwitch` |
| Pick one of a list | `UltraCanvasDropdown`, `UltraCanvasSegmentedControl` |
| A value on a range | `UltraCanvasSlider`, `UltraCanvasRating`, `UltraCanvasStepper` |
| Static text, status pill | `UltraCanvasLabel`, `UltraCanvasBadge`, `UltraCanvasChip` |
| Show an image / any media file | `UltraCanvasImageElement`, `UltraCanvasMediaViewer` |
| Scrolling, panes, tabs, toolbars | `UltraCanvasContainer`, `UltraCanvasSplitPane`, `UltraCanvasTabbedContainer`, `UltraCanvasToolbar` |
| Menus, modal dialogs, tooltips | `UltraCanvasMenu`, `UltraCanvasModalDialog`, `UltraCanvasTooltipManager` |
| **A progress bar, or any gauge** | `UltraCanvasGaugeDiagramElement` in `GaugeMode::LinearBar` — `Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h` |
| A chart, a diagram, a QR code or a barcode | one of the ~70 plugin elements — see the catalogue's *Charts, diagrams and codes* section |

Two exceptions only: a **self-rendered view** may paint its own *content*
(`UltraCanvasFilerWidget`, `UltraCanvasAlbum`, charts) — but it still adds real
elements as children for fields, buttons and pickers; and **the element itself**
naturally owns its buffer and caret. Declare any other exception in the source:

```cpp
// ui-reuse-exempt: <why this one paints directly>
```

`scripts/check_ui_reuse.py` enforces this and runs in CI, and
`scripts/check_element_catalogue.py` enforces the other half of it: an element
that exists but is not on that page cannot be reused, because nobody can find
it.
`scripts/ui_reuse_baseline.txt` — which records pre-existing offenders — is
empty, and the intent is that it stays empty. Do not add to it to silence a
finding.

A second rule follows from using the elements: **a callback stored on a widget
must not capture a `std::shared_ptr` to that widget, or to any container above
it.** The widget owns the callback, so the callback owning it back closes a
cycle neither end escapes and the whole subtree leaks. Capture the
back-reference raw — `[button = button.get(), status]` — which is valid for as
long as the callback can run, because the thing holding the callback is the
thing being pointed at. Captures pointing the other way (a popup the lambda
keeps alive, a sibling it updates, `make_shared` state) are ownership, not a
cycle, and stay `shared_ptr`. It makes no difference where the `shared_ptr`
came from or how the lambda is stored: a parameter the function was handed
(`AddRadioButton(std::shared_ptr<UltraCanvasRadio> button)` storing
`[this, button]` on `button` leaked every radio), a setter
(`x->SetOnClick(...)`) as much as an assignment (`x->onClick = ...`), and
`[=]` or `[p = x]` as much as `[x]`. `scripts/check_callback_cycles.py`
enforces this and runs in CI; a genuine exception opts out with
`// callback-cycle-exempt: <why>`.

## Building and testing

```bash
# Ubuntu/Debian deps
sudo apt install build-essential cmake libcairo2-dev libpango1.0-dev \
    libfreetype6-dev libvips-dev libharfbuzz-dev clang
# macOS deps
brew install cmake cairo pango freetype vips harfbuzz

mkdir build && cd build && cmake .. && make
```

The executables land in `build/`, and configuring also links
`build/share/media` and `build/share/Docs` to the repository's directories
(a symlink; on Windows a directory junction when a symlink needs privileges
the build does not have, and a copy as the last resort), which is where
`GetResourcesDir()` looks after the platform's packaged place
(`exe/Resources/` on Windows, the bundle's `Contents/Resources/` on macOS).
An application started straight from the build tree therefore finds its
icons, fonts, wallpapers and bundled documents on every desktop platform
without an install step.

The project now defaults to Clang on Linux, so install the `clang` package
alongside the existing deps. The build uses the system default linker (GNU ld,
same as CI); with a newer Clang on an older distro it automatically drops to
DWARF4 so binutils 2.38's `ld` does not choke on clang's DWARF5 output.

The full 3-OS dependency lists are in `.github/workflows/build.yml`. CI
installs Ubuntu packages with `scripts/ci-apt.sh install`, not
`sudo apt-get install`: it stops and retries a download that has stopped
dead, which apt itself waits out for as long as the job lasts.
UltraAI builds standalone: `cmake -S UltraAI -B build -DULTRAAI_BUILD_TESTS=ON`
then `ctest --test-dir build`. Framework tests live under `Tests/`.

**Cloud sessions.** The Claude Code cloud image is a general Ubuntu 24.04
without most of the libraries CI installs, and CMake leaves a missing optional
library out without a word - so `.claude/settings.json` also runs
`.claude/hooks/session-start.sh` on `SessionStart`. In the cloud only
(`$CLAUDE_CODE_REMOTE=true`) it installs every package in its `PACKAGES` list
that is not installed: CI's Linux list (`.github/workflows/build.yml`) under
Ubuntu 24.04's names, with 24.04's own MuPDF, libopusenc and c-ares where CI
builds them from source. Without it, UltraCrypt built without libsodium and
every credential-vault test failed here while passing in CI, and VideoFX, the
PDF plugin, UltraWin, UltraNet's resolver and UltraFIBU's multi-user server
were not built at all. A cold container takes about a minute, a warm one a
fraction of a second; it never blocks the session, and when an install fails
it says so in one line in the session's context. The services CI starts for
its live tests (PostgreSQL, Avahi, the IPP printer) are not set up; those
tests skip. A test that fails here but passes in CI because a library is
missing is fixed by adding the package to that list, not by treating the
failure as expected; re-run `cmake` on a build directory configured before
the install. To build what CI builds, configure with the options of CI's
*Configure CMake (macOS/Linux)* step.

**Tests that need a display.** A few tests under `Tests/` open a real window
and read the composited pixels back (`CaretStackingTest`,
`TextMetricsScreenshotTest`, `TextAreaSpellCheckTest`). They skip themselves
without a `DISPLAY`, so a bare CI machine passes them; to run one, give it a
display with `xvfb-run -a ./build/bin/TextMetricsScreenshotTest`. Two things
about a window under Xvfb catch people out:

- There is no window manager, so the window is never activated, and a window
  that is never activated draws no caret and reports no focused element. That
  is correct behaviour, not a bug in the test. A test that needs focus hands the
  application the activation event the backend would have delivered and then
  focuses the element: `DisplayTest::FocusElement(app, window, element)` in
  `Tests/DisplayTestSupport.h` does both and says whether it worked. Driving
  it from outside with `xdotool windowfocus` works too but is slower and needs
  another package.
- There is no event loop unless the test runs one, so frames are driven by
  hand: `DisplayTest::Frame(window, {elements})` marks them dirty and renders
  once, and `DisplayTest::WaitForCaret` drives frames until the shared caret is
  claimed. A text input shows its caret only while nothing is selected, so
  measure the caret before making a selection.

`TextMetricsScreenshotTest` doubles as the screenshot fixture for the
text-metrics and crisp-border rules: with `ULTRACANVAS_SCREENSHOT_DIR=<dir>` it
writes the window as PPM files (the caret, a selection, the popup menu) that
any image tool converts. With `GDK_SCALE=2` it renders at 2x and skips its
pixel assertions, which are written for whole logical pixels; the PPM is
still read back at logical size, so for the actual 2x pixels take an X
screenshot of the Xvfb display instead (`import -window root shot.png`).

`-DULTRACANVAS_BUILD_NET_TESTS=ON` adds two UltraNet binaries: `UltraNetTests`
(pass/fail suite) and `UltraNetApiStatus`, which probes every public
`UltraNet_*` entry point and prints WORKING / IMPLEMENTED / NOT IMPLEMENTED /
BROKEN per function — run it before assuming a networking API is usable in a
given build. See `Docs/Modules/UltraNet/ApiStatus.md`.

### One shared core, on every platform

The framework is **one shared library per platform** - `libUltraCanvas.so`,
`libUltraCanvas.dylib`, `libUltraCanvas.dll` (`ULTRACANVAS_BUILD_SHARED`, the
default and what every CI row passes) - and the UI-free modules are **inside
it**: UltraNet, UltraWin, UltraCrypt, UltraVault, UltraDatabase, UltraMessage,
NetworkMonitor and VirtualFS are each built as a static archive that the
shared core absorbs whole (`$<LINK_LIBRARY:WHOLE_ARCHIVE,…>`, "MODULE HOMES" in
`UltraCanvas/CMakeLists.txt`), so the core exports their complete API and
every running application shares one copy of the code and its global state.
Until October 2026 the core was static on macOS, so each of the ~20 `.app`
executables carried the whole framework, and on Linux and Windows an app that
linked a module archive next to the shared core got a second copy of that
module - two registries, two connection tables. The rules:

- **Link a module by its public name** - `UltraDatabase`, `UltraVault`,
  `UltraCrypt`, `UltraMessage`, `NetworkMonitor` - never by its archive
  (`uc-database`, `uc-vault`, …). The public name is an INTERFACE target that
  resolves to the shared core, or to the archive under a static core, and
  carries the module's headers and switches either way. Only
  `UltraCanvas/CMakeLists.txt` and the other archives name an archive.
- **A new UI-free module follows the pattern**: `add_library(uc-<name> STATIC …)`,
  dependencies on other modules by *their* archive names,
  `_ultracanvas_module_home(<Name> uc-<name>)`, and its archive added to the
  one `_ultracanvas_absorb_modules(…)` call, dependents before the modules
  they use. A module that links the core (UltraClipboardHistory,
  UltraMessageCenter) is not absorbed; it links the core and the homes it needs.
- **Do not link a module archive and the core on one line**, and do not add a
  `_uc_core_shared` conditional of your own: that was the workaround for
  UltraNet and UltraWin before the homes existed.

### Packaging a new app for macOS

`package-macos.sh` ships the apps as one suite folder, `UltraCanvas/`, with a
single shared `Frameworks/` that every `.app` loads its dylibs from
(`@executable_path/../../../Frameworks/`) - the shared core,
`libUltraCanvas.1.dylib`, among them. Linux (`lib/`) and Windows (one
`dist/` folder) already shared their libraries; macOS gave each `.app` its own
copy of the ~90 Homebrew dylibs, so every new app added ~95 MB to the
download - two apps added in October 2026 took the macOS DMG from 431 MB back
to 556 MB - and, until October 2026, its own statically linked copy of the
framework on top. The rules:

- **Add an app with a `build_app_bundle` call** in `package-macos.sh`, above
  `finish_suite`. That is the whole job: its libraries go to the shared
  `Frameworks/`, and it is signed and notarized with the suite.
- **Never give an app its own `Contents/Frameworks/`**, copy dylibs into a
  bundle by hand, or point a load command anywhere but the shared folder.
  `verify_suite` fails the packaging run (and CI) when an app carries
  `Contents/Frameworks/`, when a binary needs a dylib missing from the shared
  folder, when one still loads from Homebrew or the build tree, or when the
  shared folder holds no `libUltraCanvas.*.dylib` - a static core on macOS is
  a packaging error now, not a configuration.
- **A command-line tool** goes through `build_cli_tool`: it lands in the suite
  folder as `<name>/bin/<name>` and loads from the same shared `Frameworks/`
  (`@executable_path/../../Frameworks`), never from a folder of its own.
- **Check the size** in the job summary - "macOS suite sizes", and "Package
  sizes" on the Linux and Windows rows, which list the core library, the sum
  of the application executables and each one: a new app should add roughly
  its executable and resources, a few MB - not a second copy of the libraries
  or of the framework.
- **Do not write `LSMinimumSystemVersion` yourself.** `finish_suite` reads the
  minimum macOS from the app's binaries and the shared `Frameworks/`, writes
  it into the app's `Info.plist`, and fails when something needs a newer
  macOS than `MACOSX_DEPLOYMENT_TARGET` (CI: 14.0) - dyld refuses such a
  binary whatever the plist says.
- **An app that opens the camera or the microphone gets a line in
  `camera_usage` / `microphone_usage`** in `package-macos.sh`, with the
  reason the user reads in the macOS prompt. That one line writes the
  `NS*UsageDescription` key into its `Info.plist` and the hardened-runtime
  device entitlement into what it is signed with. A signed app needs both:
  without the key TCC terminates it, and without the entitlement the device
  is refused before the user is asked. Do not add device entitlements to
  `MacOS/entitlements.plist`, which every app is signed with. Every app gets
  the local-network reason and `NSBonjourServices`, because any app that
  prints browses for IPP printers. A new Bonjour service type the framework
  browses goes into `BONJOUR_SERVICES` there.
- **A library a new app needs goes into `MacOS/deps/vcpkg.json`**, not into
  a `brew install` in CI: the libraries CI bundles are built with vcpkg for
  that macOS (`MacOS/deps/README.md`), and one taken from Homebrew carries the
  runner's macOS and fails the check above.
- The apps only run inside the suite folder; users install by dragging the
  whole `UltraCanvas` folder to Applications. Say so wherever the macOS
  install is described.

## Versioning

The **first line of a changelog is the single source of truth** for a version,
and **every application keeps its own changelog and versions itself**. The
framework changelog covers the framework — `UltraCanvas/`, the modules, the
build system, CI — plus DemoApp, which is the framework's showcase and is named
`UCDemo-<ULTRACANVAS_VERSION>` by the packaging scripts.

| Changelog | Drives |
|---|---|
| `Docs/UltraCanvas/CHANGELOG.md` | UltraCanvas core, the modules, the build system, DemoApp |
| `Docs/AnchorPoint/CHANGELOG.md` | AnchorPoint |
| `Docs/ArtCreator/CHANGELOG.md` | ArtCreator |
| `Docs/DeviceExplorer/CHANGELOG.md` | DeviceExplorer |
| `Docs/UltraDesktop/CHANGELOG.md` | UltraDesktop — the ULTRA OS desktop |
| `Docs/EmailCleaner/CHANGELOG.md` | EmailCleaner |
| `Docs/Modules/UltraWin/CHANGELOG.md` | UltraWin — the Windows tier, UltraWinManager and UltraWinSetup |
| `Docs/Texter/CHANGELOG.md` | UltraTexter |
| `Docs/UltraAI/CHANGELOG.md` | UltraAI and its dashboard app |
| `Docs/UltraAuthenticator/CHANGELOG.md` | UltraAuthenticator |
| `Docs/UltraCleaner/CHANGELOG.md` | UltraCleaner |
| `Docs/UltraClaude/CHANGELOG.md` | UltraClaude — chat with Claude through the Claude Code CLI |
| `Docs/UltraClipboard/CHANGELOG.md` | UltraClipboard — the clipboard history |
| `Docs/UOSSettings/CHANGELOG.md` | UOS-Settings — the ULTRA OS settings |
| `Docs/UltraFiler/CHANGELOG.md` | UltraFiler |
| `Docs/UltraMail/CHANGELOG.md` | UltraMail |
| `Docs/UltraNetMonitor/CHANGELOG.md` | UltraNetMonitor |
| `Docs/UltraPassword/CHANGELOG.md` | UltraPassword — the password vault |
| `Docs/UltraPaint/CHANGELOG.md` | UltraPaint |
| `Docs/UltraSocial/CHANGELOG.md` | UltraSocial |
| `Docs/UltraViewer/CHANGELOG.md` | UltraViewer |
| `Docs/UltraWeb/CHANGELOG.md` | UltraWeb — the browser for WebAssembly apps |

Format: `#### YYYY-MM-DD *x.y.z*`. **For the framework changelog you do not
write that line at all**: drop your bullets in a new file under
[`Docs/UltraCanvas/changelog.d/`](Docs/UltraCanvas/changelog.d/README.md) with
no header and no number, and CI assigns the number on `main` after the merge
(see *Pending entries* below). For an application changelog, adding the entry
at the top is still the whole bump. Either way, do **not** hand-edit a version
number anywhere else, and never introduce a new literal copy of one:

- `cmake/UltraCanvasVersion.cmake` parses the first line of each file at
  configure time and sets one `<PREFIX>_VERSION` per row of the table above —
  `ULTRACANVAS_VERSION`, `EMAILCLEANER_VERSION`, `ULTRAFILER_VERSION` and the
  rest — plus `_DOT4` / `_COMMA4` variants for Windows resources and
  `<PREFIX>_VERSION_DATE`, the date on that same changelog line. An
  application that shows when its version shipped takes it from there: it is
  the release's date, so every build of one release agrees, which a build
  clock would not. Adding an
  application is one `_ultracanvas_declare_product()` line there plus its
  changelog file. It feeds every `project(VERSION …)` and the matching compile
  definitions. Several of those variables have no consumer yet; they are set
  anyway so that when an app needs to show its version it reads it from the
  changelog rather than growing a second copy of the number. Editing a
  changelog re-triggers the configure step, so existing build trees follow
  along.
- Code that displays a version reads those defines —
  `UltraCanvas::versionString` (`UltraCanvasUtils.cpp`, shown in the demo app's
  info window), `UltraCanvasTextEditor::version` (shown in Texter's splash) and
  `ULTRACLEANER_VERSION` (UltraCleaner's window title, header line and
  `--version`).
- **Every app shows its version in its main window title** — `"UltraMail "
  ULTRAMAIL_VERSION`, and `"<document> - UltraPaint " ULTRAPAINT_VERSION` where
  the title follows the open document — so a screenshot or bug report names the
  build. A new app passes its `<APP>_VERSION` as a compile definition from its
  CMake target and guards it with `#ifndef … #error` rather than a fallback
  string.
- An app versions itself: it does not move when the framework releases, and a
  change to it belongs in its own file, not in the framework's. A framework
  change an app needs still goes in `Docs/UltraCanvas/CHANGELOG.md` — including
  one a host application outside this repository asked for, which lands in
  `UltraCanvas/OS/<Platform>/` and `UltraCanvas/core/`, not in that host.
  Cross-reference such a change from the app's changelog when a release
  depends on it; never describe it in two files with two versions.
- The app changelogs were split out of the framework's on 2026-08-31.
  EmailCleaner's two entries were moved across verbatim (framework 0.3.87 and
  0.3.88 now point at them); every other app's earlier history was left where
  it was published, so `Docs/UltraCanvas/CHANGELOG.md` remains the record of
  what shipped in each framework release. Do not backfill it into the app
  files — that would put one change in two places under two numbers.
- **Pending entries: the framework's number is assigned on `main`, not by
  you.** Line 1 of a shared file was the most contended line in the
  repository, and two open pull requests collided there every time, in one of
  two ways. Either a branch picked the next number, `main` released past it
  while the branch waited for review, and it merged carrying a number *lower*
  than versions already released below it — the product's version then goes
  backwards. Or two branches wrote the same `#### <date> *x.y.z*` line, git
  merged both bullet lists under the one header with no conflict, and two
  releases shared a number while the version never incremented. Both happened
  repeatedly; the file still carries sixteen duplicated numbers from before
  any of this was checked, and on 2026-09-23 one branch was renumbered five
  times in a morning (0.9.23 → 0.9.27 → 0.9.28 → 0.9.29 → 0.9.31), each
  renumber throwing away a six-platform CI matrix, with 0.9.29 consumed and
  lost in the churn.
  So the routine case no longer touches line 1: write
  `Docs/UltraCanvas/changelog.d/<change-name>.md` containing just the bullets.
  Two branches adding two files cannot collide, there is nothing to renumber
  when `main` moves, and `.github/workflows/changelog-fold.yml` folds whatever
  is pending into `CHANGELOG.md` under the next free version once it lands —
  `scripts/fold_changelog.py` does the same locally if you want to see it.
  Name the file after the change, not the branch. Never put a `####` header in
  one: a number chosen on a branch is the collision this ends, and
  `scripts/check_changelog.py` refuses it.
  A release that must carry a *specific* number — a hand-cut hotfix — can
  still be written straight into the changelog as a top entry, and the same
  rules apply to it: unique, and above every version below it. Application
  changelogs are unchanged; they see little contention, one product to a file.
  Run `git fetch origin main` and then
  `python3 scripts/check_changelog.py --base origin/main` before pushing; CI
  runs it too. The check compares a top entry with line 1 of `main`'s copy of
  the file, so it is only as current as your `origin/main` - an unfetched one
  lets a stale number through. It also refuses a number more than ten past
  the release before it: open pull requests each hold one number, so a small
  gap is normal, but 0.9.120 over a `main` on 0.9.32 once passed the
  "strictly greater" rule and would have become the released version.
  GitHub's *Update branch* button cannot renumber a top entry: it merges
  `main` into the branch and, when `main` has meanwhile released the number
  the branch chose, folds the two entries under the one header (or leaves a
  conflict marker in line 1) — and the guard then fails on the very merge
  that was meant to fix it. When the check goes red after such a merge, fix
  it locally: merge `main`, split the shared header back into two entries,
  give the branch's entry the next free number, and push. A pending
  `changelog.d/` entry has none of this to do.
- **Do not add a version number to a compile definition that anything but its
  own consumers see.** `ULTRACANVAS_VERSION` was `PUBLIC` on the core library,
  so it sat on the compile command line of 621 of the build's 1136 objects
  although exactly two sources read it — and every changelog edit, in any pull
  request, rebuilt nearly the whole tree and invalidated every other branch's
  cache. It is attached to those two sources with
  `set_source_files_properties` now. A version a new consumer needs goes on
  that list, not into the target's interface.
- The packaging scripts (`build-demoapp-appimage.sh`, `package-win.sh`,
  `package-macos.sh`) parse the same line for artefact file names.
- Only the Windows resource files still hold literals, because windres reads
  them from disk: `Apps/Texter/UltraTexter.{rc,manifest}` and
  `Apps/UltraFiler/UltraFiler.{rc,manifest}`. Run `./set-version.sh` after
  bumping either app's version; a CMake configure on any platform warns when
  they are stale. Nothing else may hold a literal — UltraFiler's compile
  definition did, and titled its window `UltraFiler 0.8.0` for thirteen
  releases while its changelog said 1.17.0.

## House rules for AI-generated changes

1. Match the style of the file you are editing; PascalCase everywhere.
2. **Before painting any UI, check whether the element already exists** — the
   catalogue is
   [`Docs/UltraCanvas/UltraCanvasUIElements.md`](Docs/UltraCanvas/UltraCanvasUIElements.md),
   and it covers `include/Plugins/` as well as `include/`: searching one
   directory is how a second progress bar got written while the gauge sat in
   the other. Writing `DrawText` / `FillRoundedRectangle` plus a private
   buffer, caret or `hovered` flag to make a control is a defect, not a
   shortcut. Run `python3 scripts/check_ui_reuse.py` before pushing; CI runs
   it too. Adding an element? Add its row to the catalogue in the same change
   — `python3 scripts/check_element_catalogue.py` fails without it.
   Wiring a callback on that element? It must not capture a `shared_ptr` to
   the element or to a container above it — capture it raw. Run
   `python3 scripts/check_callback_cycles.py`; CI runs that too.
   Writing a number into a file format or a protocol? Read it with
   `TryParseFloat` / `ParseFloatClassic` and write it with
   `FormatFloatClassic`, never `std::stof` / `atof` / `std::to_string(double)`
   / `snprintf("%g")`. Run `python3 scripts/check_locale_numbers.py`; CI runs
   that too. Naming a function? Not after a Win32 A/W macro (`CreateFile`,
   `LoadImage`, `SendMessage`) — run `python3 scripts/check_win32_names.py`;
   CI runs that too. Reading HTML, CSS or an `&entity;`? Through the
   HTMLReader module (`HTML::Parser`, `HTML::StyleSheet`,
   `HTML::ExtractPlainText`, `HTML::DecodeEntities`), never a stripper or
   entity table of your own — run `python3 scripts/check_html_reuse.py`; CI
   runs that too.
3. Check `Docs/UltraCanvas/<Component>*.md` (or `llms.txt`) before using a
   component; if you add or change public API, update the matching doc in
   the same change. Then run `python3 scripts/check_doc_examples.py <doc>`:
   it compiles the doc's C++ against the headers and reports each function,
   field or signature the headers don't have (Linux, clang++). All
   `*Examples.md` docs pass it. CI runs it over every doc under
   `Docs/UltraCanvas/` but the changelog (`--all --strict`,
   `doc-examples.yml`) whenever a doc or a public header changes, and fails
   on any finding a component doc has. The design documents (a Proposal,
   Plan or Investigation) describe APIs not written yet, so their findings
   are listed in `scripts/doc_examples_baseline.txt` until the API exists;
   a component doc must never be added there.
   Changing a header can therefore fail a doc you did not touch: fix that
   doc in the same change. A name a snippet takes from the application - a
   `window`, a callback the reader writes, a version macro - is declared in
   a `<!-- doc-check: ... -->` comment with its real type, never a framework
   API that does not exist.
4. Keep platform-independent logic out of `OS/<Platform>/` and vice versa.
5. Do not introduce new third-party dependencies without updating
   `Docs/Dependencies.md`, `master_dependencies.yaml` and
   `THIRD_PARTY_LICENSES.md`.
6. Docs changes: regenerate `llms.txt`/`llms-full.txt`
   (`python3 scripts/generate_llms_txt.py`) — CI verifies they are in sync.

## Reporting back (AI sessions)

Finish every reply that reports work — the end of a task, a check-in, a
status update — with these three blocks, in this order, after the prose that
says what happened. They are headings, not prose: a reader scanning for "what
now" must find it without reading the report.

```markdown
## Delivery
...

## Next Task
...

## Other recommendations
...
```

1. **`## Delivery`** — where the code actually IS, every time any code was
   written. **Run `git status --short` and `git diff --shortstat <base>...HEAD`
   before writing it** and report what they print, not what you remember: the
   whole point is to catch the gap between what you believe you delivered and
   what the repository holds.

   Three facts, in this order of danger:

   - **Is anything still uncommitted?** This is the one that loses work.
     These sessions run in a container that is reclaimed when the session
     ends, and its clone goes with it: a file edited and not committed is not
     "pending", it is *gone*, and a reply that describes it as written reads
     as a delivery that never existed. **Never end a reply reporting finished
     work while an edit to a tracked file is uncommitted.** Commit it — or,
     if it is genuinely not ready, say in this block, in as many words, that
     it is uncommitted and will be lost. A clean `git status` is the normal
     end state; anything else is stated, never left for the reader to
     discover.
   - **How much, and is it pushed?** Files changed and `+added/-removed`
     lines from `git diff --shortstat`, then the branch name and short SHA —
     or plainly that the commits are local and unpushed. A commit that never
     left the container dies with it exactly as an uncommitted edit does.
   - **Is it a pull request?** The number and link, or the words **no pull
     request** — never silence. "Pushed" is not "in review": a branch nobody
     has opened a PR for reaches no reviewer and no `main`, and a reader told
     a change is "done and pushed" will reasonably assume otherwise. If a PR
     exists, say its state too (open / merged / CI red / waiting on review),
     because an open PR that is failing is not delivered either.

   **This one is checked, not remembered.** `.claude/settings.json` (committed,
   so every clone has it) runs `.claude/hooks/check-delivery.sh` on `Stop`: a
   turn that would end with an uncommitted tracked file or an unpushed commit
   is blocked once, with the offending paths and commits listed. Stopping
   again is allowed — the check refuses silence, not unfinished work — so
   commit and push, or write the block and say what you are leaving behind.
   The same script runs on `SessionStart` with `--brief`, which restates the
   rule and reports anything a previous session left behind.

   Write it even when the answer is unwelcome — *"3 commits, 20 files,
   +1130/−85, pushed to `claude/…`, **no pull request**"* and *"the parser
   change is written but **not committed**"* are exactly the lines that must
   not be left out. Omit the block only for a reply that changed no code at
   all (a question answered, a file read).
2. **`## Next Task`** — what happens next, and who does it. One or two lines:
   the next step you intend to take, the thing you are waiting on (a CI run, a
   review, a merge), or the decision you need from the user. When the work is
   finished and nothing follows, write `None — <what was delivered> is
   complete.` Never leave the block out because the answer is "nothing": an
   explicit "none" is the difference between finished and forgotten.
2. **`## Other recommendations`** — defects and smells found *outside* the
   change you were asked to make: a bug in another module, a stale document, a
   test asserting something untrue, a dependency that no longer resolves.
   One bullet each, naming the file or module, what is wrong, and why it was
   not fixed here. Write `None.` when a session turned up nothing.

Two rules about the second block, because it is the one that goes wrong:

- **Finding something is not permission to fix it.** Out-of-scope repairs
  belong in this block, not in the diff — unless the user asks for them, or
  the change cannot work without them, in which case say so in the prose.
- **It is not a place to park work you were asked to do.** Anything inside the
  task's scope gets finished or explicitly reported as blocked; it does not
  become a recommendation.

### The closing line

The **last reply before the chat waits for the user** — every chat, whether
or not code was written in it — ends with one line, after the three blocks:

```
Code needs to be PRed (N lines)
```

`N` is how many lines this checkout differs from the default branch:
insertions plus deletions of the working tree against the merge base, plus
every line of an untracked, non-ignored file — committed, uncommitted and
untracked alike, because all of it still has to reach a pull request. Measure
it, do not recall it:

```
git fetch origin main
git diff --shortstat $(git merge-base origin/main HEAD)
git ls-files -z --others --exclude-standard | xargs -0 -r cat | wc -l
```

Write `(0 lines)` when nothing differs — a missing line and a zero are not the
same thing to a reader. When a pull request is already open for the branch,
keep the line and add ` — open as PR #<n>` after it, so "needs to be PRed"
is never read as "nobody has opened one" when someone has.

**This one is checked too.** The same `Stop` hook,
`.claude/hooks/check-delivery.sh`, measures `N` itself and reads the reply
being finished (`last_assistant_message`, or the transcript's last assistant
text on older Claude Code builds). A reply whose last non-blank line is not
`Code needs to be PRed (N lines)` with the measured `N` is blocked once, with
the line it found and the line it expected; surrounding backticks or bold and
the ` — open as PR #<n>` suffix are accepted. The hook does not fetch, so it
measures against `origin/main` as the clone last saw it — fetch before
measuring and the two agree. The `SessionStart --brief` message states the
rule and the current `N`.

`Next Task` and `Other recommendations` describe the repository, not the
conversation. "Waiting for the test suite" belongs in `Next Task`; "the
Alembic reader drops transforms" belongs in `Other recommendations` whether or
not anyone asked about Alembic. `Delivery` is the exception: it describes
where the work sits right now, and it is the block a reader checks to find out
whether anything they were told about has actually reached anyone.

## Branch and pull-request rules (AI sessions)

Why these exist: in a long session the working branch's PR can be merged
mid-session (which deletes the branch on GitHub). A later `git push` then
silently *recreates* the branch — but a merged PR never receives new
commits, so everything pushed after the merge is stranded on an untracked
branch and never reaches `main`. This has happened; the rules below prevent
a repeat.

For assistants:

1. **Check the branch before every follow-up push.** Run
   `git ls-remote origin <branch>` first. If the branch is gone from the
   remote — or the push output says `* [new branch]` where an update to an
   existing branch was expected — the PR was merged (or closed) and the
   branch deleted. Do not just push and move on.
2. **Never stack new work onto merged history.** When the branch's PR is
   merged: `git fetch origin main`, rebase the still-unmerged commits onto
   `origin/main` (keep the same branch name), push with
   `--force-with-lease`, and tell the user a **new** PR is needed — a
   merged/closed PR cannot track new commits, and GitHub will not reopen it
   for a recreated branch.
3. **Confirm delivery after pushing, and say so unprompted.** Run

   ```
   python3 scripts/check_publication.py          # this branch
   python3 scripts/check_publication.py --all    # every branch on the remote
   ```

   It fetches `main` and the branch first (a stale `origin/main` otherwise
   makes already-merged commits look undelivered), lists the commits on the
   branch that are not in `main`, and exits 1 when there are any — or when the
   branch is gone from the remote, which usually means its PR was merged and
   the branch deleted (rule 2 applies). Then verify on GitHub that an *open* PR has this branch as
   its head and that its head is the commit just pushed, and report the PR
   number and head SHA.

   "Pushed" is not "delivered" — only a commit reachable from an open PR (or
   `main`) counts. If there is no open PR, **say so in the reply, without
   being asked**: name the branch, the number of commits, and that they are
   not in `main` and will not reach it until a PR is opened and merged. Do
   this at the end of every session that pushed, and whenever the user
   reports that a change "did not arrive" — that report is almost always
   this, and the first thing to check is whether `main` has the code at all
   (`git fetch origin main && git log origin/main --oneline -- <file>`), not
   the code itself.

   Not opening a PR unasked is the rule; leaving the user to *discover* that
   nothing was published is not. A session that ends with unpublished commits
   and no warning has produced nothing.
4. **Keep the base fresh.** Before the rebase in rule 2, always fetch —
   `main` usually moved while the session ran; resolve conflicts locally so
   the new PR is mergeable from the start.

5. **CI needs an open PR.** The Build and llms.txt workflows run on pull
   requests and on pushes to `main` — *not* on pushes to feature branches.
   A commit pushed to a `claude/**` branch with no open PR gets no 3-OS
   validation. Open the PR (draft counts) or dispatch the workflow manually
   against the branch when a change needs checking before review.

6. **Titles say what changed and why — never the branch name.** Session
   branches get random names (`claude/exciting-davinci-v0zksb`) that tell a
   reader of `git log` nothing. So:

   - **PR title**: `<Module or area>: <what changes, and why>`, in the same
     style as commit subjects — e.g. `check_changelog: refuse a version behind
     the base branch's, not only equal`. Never the branch name, a session
     nickname, `WIP`, `Update <file>`, `Fixes` or any other word that does
     not describe the change. When follow-up pushes change what the PR does,
     update the title (and description) to match.
   - **Merge commits you create** (bringing `main` into the branch): write the
     subject yourself with `git merge -m`, naming the branch's purpose and the
     reason for the merge — e.g. `Merge main into the UltraCalendar proposal
     branch, and take 0.9.28`. Never keep git's default
     `Merge branch 'main' into claude/<random-name>`.
   - **Commit subjects** follow the same rule: the reason for the change, not
     the tool, session or branch that produced it.

7. **Put the PR number at the front of the chat title.** As soon as you open
   a pull request, rename the session so its title starts with the number:
   `#<n> <current title>` — e.g. `#412 UltraMail: wrap long subjects in the
   list`. In a Claude Code Remote session call `set_session_title` (the
   Claude Code Remote MCP server, which builds name either
   `mcp__claude-code-remote__…` or `mcp__Claude_Code_Remote__…` — the same
   tool) right after `create_pull_request` returns;
   where no such tool exists, tell the user the number to add instead. One
   number per chat: when a later PR replaces a merged or closed one (rule 2),
   swap the old number for the new one rather than stacking them, and never
   rename to the bare number — keep the rest of the title so the chat list
   still says what the work is. `.claude/hooks/check-chat-title.sh` enforces
   this in Claude Code Remote sessions: on `PostToolUse` it records the PR a
   session opens and the number each `set_session_title` gives it, and on
   `Stop` it blocks once when the PR the session opened — or the one its
   closing line names with ` — open as PR #<n>`, which covers a PR opened
   from the Claude UI — is not the number the title was given.

For maintainers:

8. **Merge with the PR title, not the branch name.** GitHub's default merge
   commit is `Merge pull request #N from <owner>/<branch>`, which puts the
   branch's random words into `main`'s history. In the repository's
   *Settings → General → Pull Requests*, set the merge-commit default message
   to **Pull request title** (or *Pull request title and description*), and
   squash merges to **Pull request title** as well. Until that is set, edit
   the commit message in the merge dialog before confirming.

9. **Do not merge a session's PR while the session may still push to it.**
   Merge after the session says it is done — or, if merging early, tell the
   session so it restarts from `main` and opens a fresh PR for the rest.
