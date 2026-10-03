# Getting started: building an application with UltraCanvas and an AI assistant

**Version:** 1.0.0
**Last Modified:** 2026-10-01
**Author:** UltraCanvas Framework

This page is the step list for a programmer who has never built on UltraCanvas
and wants to ship an application with it, using an AI coding assistant for most
of the typing. It is written around **Claude Code**, because the repository
carries its configuration (`CLAUDE.md`, `AGENTS.md`, `.claude/settings.json`
and the hooks), but every step names what to do with another assistant too.

If you have no compiler at all and work through an AI assistant and GitHub
only, read [`GettingStarted-Cloud.md`](GettingStarted-Cloud.md) alongside
this: it says where each step happens when CI is the compiler.

The short version: **the repository already teaches the assistant how to work
here.** Your job is to build it once, keep the assistant pointed at the docs,
ask for one bounded change at a time, and run the checks before every push.
Skip a step and the assistant starts guessing APIs from other frameworks,
hand-paints controls, or leaves work in a container that is thrown away.

---

## Step 1 — Install the toolchain

UltraCanvas is C++20 built with CMake ≥ 3.16. Clang is the default compiler on
Linux.

```bash
# Ubuntu / Debian — the core set
sudo apt install build-essential cmake pkg-config clang \
    libcairo2-dev libpango1.0-dev libharfbuzz-dev libfreetype6-dev \
    libvips-dev libglib2.0-dev libtinyxml2-dev libfmt-dev \
    libx11-dev libxcursor-dev libgl1-mesa-dev libgtk-3-dev

# macOS
brew install cmake cairo pango freetype vips harfbuzz

# Windows — MSYS2 CLANG64 shell (see build-win.cmd for the package list)
pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake \
    mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf \
    mingw-w64-clang-x86_64-cairo mingw-w64-clang-x86_64-pango \
    mingw-w64-clang-x86_64-harfbuzz mingw-w64-clang-x86_64-glib2 \
    mingw-w64-clang-x86_64-freetype mingw-w64-clang-x86_64-tinyxml2 \
    mingw-w64-clang-x86_64-libvips
```

The complete, current three-OS lists are the ones CI installs in
`.github/workflows/build.yml`; the optional plugins (CDR, PDF via MuPDF, OCR,
Vectorizer, audio, video) each add a library, and every one of them can be
switched off with a `-DULTRACANVAS_PLUGIN_<NAME>=OFF` option when you do not
need it. `Docs/Dependencies.md` says which library each feature pulls in.

## Step 2 — Clone and build the whole tree once

```bash
git clone https://github.com/ULTRA-OS-Project/UltraCanvas.git
cd UltraCanvas
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/UltraCanvasDemo
```

Build everything the first time, even though you only want your own app. Two
reasons:

- **The demo application is the live catalogue.** Every element, chart,
  diagram and module has a page in `UltraCanvasDemo`, and the source of each
  page is one file under `Apps/DemoApp/` (`UltraCanvasButtonExamples.cpp`,
  `UltraCanvasLineChartExamples.cpp`, …). When you want to know what a
  widget looks like and how it is wired, open the page and then the file.
- **A green build is your baseline.** When the assistant's first change
  breaks the build you want to know it was the change.

Turn off the applications you do not need afterwards to shorten rebuilds:
`-DBUILD_ULTRAFILER_APP=OFF -DBUILD_ULTRAPAINT_APP=OFF …` (the full option
list is at the top of the root `CMakeLists.txt`).

## Step 3 — Point the AI assistant at the repository's own guidance

The repository is written to be read by an assistant. Make sure yours actually
reads it.

**Claude Code** (terminal, desktop, web, or IDE extension):

1. Open the repository root. Claude Code loads `CLAUDE.md` automatically, and
   that file sends it to `AGENTS.md`, the module registry and the docs map.
   Nothing to configure.
2. `.claude/settings.json` is committed, so every clone gets the same hooks
   and permissions: a `SessionStart` hook that restates the delivery rule, a
   `Stop` hook (`.claude/hooks/check-delivery.sh`) that refuses to end a
   turn silently on uncommitted or unpushed work, and pre-approved read-only
   `git` and `scripts/check_*.py` commands so the checks run without
   prompting. Leave the file in place; it is what keeps a long session
   honest.
3. The first thing to say in a fresh session is what you are building, which
   platforms it must run on and which modules you expect to use. The
   assistant then knows which docs to read before writing anything.

**Any other assistant** (Copilot, Cursor, Windsurf, ChatGPT, Gemini, …):

- `AGENTS.md` is the vendor-neutral file; most agents pick it up by name.
  If yours does not, paste it into the system prompt or project instructions.
- `llms.txt` at the repo root is an index of the whole docs corpus with one
  line per document; `llms-full.txt` is the entire corpus concatenated.
  Upload `llms-full.txt` to a Claude.ai Project, a custom GPT or a Gemini
  Gem and the chat can answer API questions without the repository open.
- `context7.json` registers the docs with Context7, so an agent with the
  Context7 MCP server resolves `ultracanvas` to the real docs.
- There is no vendored MCP documentation server yet; `Docs/AI-Agent-Integration.md`
  is the plan for one.

**Your own app, outside this repository:** keep UltraCanvas as a git
submodule and copy `CLAUDE.md`/`AGENTS.md` (or a two-line pointer to the
submodule's copies) into your root. An assistant that cannot open
`Docs/UltraCanvas/` will invent `canvas->AddButton(...)`-style APIs.

## Step 4 — Decide where the app lives, then create the skeleton

Two layouts are supported.

| Layout | When | How it links |
|---|---|---|
| **In-tree**, `Apps/<Name>/` | The app belongs to the ULTRA OS family or you want the six-platform CI to build it | One `option(BUILD_<NAME>)` + `add_subdirectory(Apps/<Name>)` block in the root `CMakeLists.txt`, after the framework; link `${ULTRACANVAS_LIBRARY}` |
| **Out-of-tree**, your own repo | A product that merely depends on the framework | Download the SDK artifact CI builds for your platform ([`UltraCanvasSDK.md`](UltraCanvasSDK.md)), or install the framework yourself once (`cmake --install build --prefix <prefix>`), then `find_package(UltraCanvas CONFIG REQUIRED)` and link `UltraCanvas::UltraCanvas`; point `CMAKE_PREFIX_PATH` at the prefix. The alternative is a submodule plus `add_subdirectory()` with the bundled apps switched off, which exports `ULTRACANVAS_LIBRARY`, `ULTRACANVAS_PLUGIN_TARGETS` and `ULTRACANVAS_INCLUDE_DIRS` to the parent scope (`Apps/Texter/CMakeLists.txt` shows the sibling-directory form) |

Both routes are validated on every pull request: CI builds the in-tree
applications, and it installs the framework into a scratch prefix and builds
`Tests/PackageConsumer` against it with nothing but `find_package`. That
directory is the complete out-of-tree starting point, two files long:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)
find_package(UltraCanvas CONFIG REQUIRED)
add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE UltraCanvas::UltraCanvas)
```

The package carries the whole element catalogue, FileLoader, JSON, and
UltraDatabase and UltraNet when the installed build enabled them, plus
`UltraCanvas::UltraCrypt` and `UltraCanvas::UltraVault` as separate targets.
The file-format plug-ins the build produced (CDR, XAR, EPS, the vector and
3D-model converters, OCR, Vectorizer) are imported as
`UltraCanvas::UltraCanvas<Name>Plugin` and listed in
`ULTRACANVAS_PLUGIN_TARGETS`; link that list plus
`UltraCanvas::UltraCanvasAllFormats` and every format is registered with
FileLoader before `main()`, exactly as in-tree applications do it. The
package re-finds cairo, pango, freetype, glib, tinyxml2 and libvips through
pkg-config on your machine, so the `-dev` packages from step 1 are needed
there too. A static framework built against the vendored libcurl cannot be
packaged at all and says so at configure time; build it shared, or against
a system libcurl with WebSocket support.

Ask the assistant to **copy a small existing app, not to invent a structure**.
`Apps/UltraAuthenticator/` is the cleanest template: a headless core library,
a GUI target on top, `--version` from the changelog, an icon, a desktop entry.
The minimum is this:

`Apps/MyApp/CMakeLists.txt`

```cmake
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] MyApp - SKIPPED (framework not built)")
    return()
endif()

add_executable(MyApp main.cpp MyAppWindow.cpp)

# The version is the first line of Docs/MyApp/CHANGELOG.md, read by
# cmake/UltraCanvasVersion.cmake once the app is declared there.
target_compile_definitions(MyApp PRIVATE MYAPP_VERSION="${MYAPP_VERSION}")

target_include_directories(MyApp PRIVATE ${ULTRACANVAS_INCLUDE_DIRS}
                                         ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MyApp PRIVATE ${ULTRACANVAS_LIBRARY})
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(MyApp PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(MyApp PRIVATE cxx_std_20)
set_target_properties(MyApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
install(TARGETS MyApp RUNTIME DESTINATION bin)
```

`Apps/MyApp/main.cpp`

```cpp
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif
#include <iostream>

#ifndef MYAPP_VERSION
#error "MYAPP_VERSION must come from the CMake target (see cmake/UltraCanvasVersion.cmake)"
#endif

using namespace UltraCanvas;

#ifdef __linux__
// The one call a signal handler may make; Run() turns it into RequestExit().
static void OnSignal(int) { UltraCanvasApplicationBase::RequestExitFromSignal(); }
#endif

int main(int argc, char* argv[]) {
#ifdef __linux__
    XInitThreads();
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    if (!app.Initialize("MyApp")) {
        std::cerr << "Failed to initialize UltraCanvas\n";
        return EXIT_FAILURE;
    }

    WindowConfig config;
    config.title  = "MyApp " MYAPP_VERSION;   // every app shows its version in the title
    config.width  = 800;
    config.height = 600;
    auto window = CreateWindow(config);

    auto label  = CreateLabel("greeting", 20, 20, 300, 24, "Hello from UltraCanvas");
    auto button = CreateButton("quit", 20, 60, 120, 32, "Quit");
    // Capture the window raw: the button lives inside it, and a shared_ptr
    // here would close an ownership cycle (scripts/check_callback_cycles.py).
    button->onClick = [&app]() { app.RequestExit(); };

    window->AddChild(label);
    window->AddChild(button);
    window->Show();

    app.Run();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
```

Then, in the same change: a `Docs/MyApp/CHANGELOG.md` whose first line is
`#### 2026-10-01 *0.1.0*`, one `_ultracanvas_declare_product()` line in
`cmake/UltraCanvasVersion.cmake`, and the `option()`/`add_subdirectory()`
block in the root `CMakeLists.txt`. That is the whole registration; the
version then flows from the changelog into the title and `--version`.

## Step 5 — Build the UI from the element catalogue

This is where AI-written code goes wrong most often, so the rule is a
prohibition: **if it takes input, shows a picture or presents a value, it is
an element.** The assistant must not paint a text field, button, progress bar
or gauge with `DrawText` / `FillRoundedRectangle` and a private buffer.

The working order for every screen:

1. Read `Docs/UltraCanvas/UltraCanvasUIElements.md` — the catalogue of the
   ~60 core elements and ~70 plugin elements (charts, diagrams, gauges,
   codes, document views), each with its header.
2. For each element you will use, read its `Docs/UltraCanvas/UltraCanvas<Name>*.md`
   and, for a non-trivial one, its page in `Apps/DemoApp/`.
3. Lay out with the layout engines, not with hand-computed coordinates:
   `UltraCanvasBoxLayout`, `UltraCanvasGridLayout`, `UltraCanvasFlexLayout`
   (`Docs/UltraCanvas/UltraCanvasLayoutExamples.md`) or CSS-style layout
   (`Docs/CSSLayout.md`). Inside a self-rendered view, position children
   with `PlaceChildAt()`, never `SetBounds()`.
4. Use the factories (`CreateButton`, `CreateTextInput`, …) and keep every
   widget `std::shared_ptr`-managed.
5. Dialogs, menus, tooltips and toasts come from `UltraCanvasModalDialog`,
   `UltraCanvasMenu`, `UltraCanvasTooltipManager` and
   `UltraCanvasDialogManager`; a progress bar is
   `UltraCanvasGaugeDiagramElement` in `GaugeMode::LinearBar`.

A prompt that gets this right names the element:

> Add a settings dialog to MyApp using `UltraCanvasModalDialog`, with an
> `UltraCanvasTextInput` for the server name, an `UltraCanvasSpinner` for
> the port and an `UltraCanvasSwitch` for TLS. Read the three component
> docs in `Docs/UltraCanvas/` first and follow the dialog keyboard rules in
> `UltraCanvasDialogKeyboard.md`.

A prompt that gets it wrong says "draw a settings panel".

Four conventions the checks enforce, and that the assistant must be told
about when it works on an out-of-tree app (in-tree it reads them from
`AGENTS.md`):

- A callback stored on a widget captures that widget, or any container above
  it, as a raw pointer, never as a `shared_ptr`.
- File paths are UTF-8 everywhere: `PathToUtf8` / `PathFromUtf8` /
  `OpenFileUtf8` from `UltraCanvasPathUtf8.h`, never `p.string()` or
  `fs::path(str)`.
- Numbers in file formats and protocols are dot-decimal: `TryParseFloat` /
  `FormatFloatClassic`, never `std::stof` / `std::to_string(double)`.
- No function named like a Win32 A/W macro (`CreateFile`, `LoadImage`,
  `SendMessage`), and platform code only under `UltraCanvas/OS/<Platform>/`.

## Step 6 — Add the modules you need, through their public surfaces

Every module is wrapped behind an UltraCanvas-owned API; app code never calls
a vendored third-party library directly. Read the module's entry in
`Masterfile_modules.md` and its `Docs/Modules/<Name>/README.md` before asking
the assistant to use it, and tell the assistant to do the same.

| You need | Module | CMake target | Doc |
|---|---|---|---|
| HTTP, WebSocket, FTP, TCP/UDP, TLS, DNS | UltraNet | `UltraNet` (built when `ULTRACANVAS_ENABLE_NET=ON`) | `Docs/Modules/UltraNet/README.md`; run `UltraNetApiStatus` before relying on an endpoint |
| SQLite / PostgreSQL with parameter binding, migrations | UltraDatabase | `UltraDatabase` | `Docs/Modules/UltraDatabase/README.md` |
| Chat, embeddings, speech, image/video/music generation, vision, translation, code assist | UltraAI | `UltraAI::UltraAI` (+ `UltraAI::Core`) | `Docs/Modules/UltraAI/README.md`, `Docs/UltraCanvas/UltraCanvasAIExamples.md` |
| Open or save any image, audio, video, document, 3D model | FileLoader | part of the core library; link `UltraCanvasAllFormats` to register every built format before `main()` | `Docs/Modules/FileLoader/README.md` |
| Encryption, sealed envelopes, secure buffers | UltraCrypt | `UltraCrypt` | `Docs/Modules/UltraCrypt/` |
| Secrets and API keys | UltraVault | with UltraCrypt | `Docs/Modules/UltraVault/` |
| Virtual filesystem, archives, compression | VirtualFS | `VirtualFS::VirtualFS` | `Docs/Modules/VirtualFS/` |
| Video probe, trim, effects, export | VideoFX | `VideoFX::VideoFX` | `Docs/Modules/VideoFX/` |
| Notifications and the message centre | UltraMessage | `UltraMessage`, `UltraMessageCenter` | `Docs/Modules/UltraMessage/` |
| JSON | UltraCanvasJSON (core) | core | `Docs/UltraCanvas/UltraCanvasJSON.md` |
| Cloud sync, OCR, QR codes, PDF, smart-home, network monitor, Windows tier | UltraCloud, OCR, QRCode, PDF, SmartHome, NetworkMonitor, UltraWin | see each README | `Docs/Modules/<Name>/` |

Two rules carry across all of them: **TLS verification stays on by default**
and **SQL uses parameter binding only**. An assistant that proposes
`verify = false` or string-concatenated SQL is to be corrected, not merged.

If your application itself talks to Claude (an in-app assistant, a summariser,
a translator), use UltraAI's `ITextLLM` with the `anthropic` adapter rather
than calling the HTTP API by hand: the adapter owns the key vault, retries,
SSE streaming and tool calls, and the `mock` adapter lets your tests run with
no network.

## Step 7 — Verify before every push

Run what CI runs, locally, and make the assistant run it too. The
`.claude/settings.json` permissions pre-approve exactly these commands so
Claude Code never has to ask.

```bash
cmake --build build --parallel                # it still builds
python3 scripts/check_ui_reuse.py --strict    # no hand-painted controls
python3 scripts/check_callback_cycles.py      # no shared_ptr cycles in callbacks
python3 scripts/check_path_string.py          # UTF-8 paths only
python3 scripts/check_locale_numbers.py       # dot-decimal in file formats
python3 scripts/check_win32_names.py          # no A/W macro collisions
python3 scripts/check_element_catalogue.py    # every element is on the catalogue page
git fetch origin main && python3 scripts/check_changelog.py --base origin/main
```

Tests live under `Tests/` (`-DBUILD_TESTS=ON`, then `ctest --test-dir build`);
module suites have their own switches (`ULTRACANVAS_BUILD_NET_TESTS`,
`ULTRACANVAS_BUILD_DATABASE_TESTS`, …). An app with logic worth testing
splits it into a headless core library the way UltraAuthenticator does, so
the tests run in CI without a display.

Then **run the app and look at it.** A screenshot from the assistant is
evidence; a description of what it "should" look like is not.

## Step 8 — Version, document, regenerate

- **The app's changelog is its version.** A release is a new
  `#### YYYY-MM-DD *x.y.z*` line at the top of `Docs/MyApp/CHANGELOG.md`;
  nothing else is hand-edited. A framework change the app needed goes in
  `Docs/UltraCanvas/changelog.d/<change-name>.md` as bullets with no header;
  CI assigns the framework number on `main`.
- **A new or changed public API gets its doc in the same change**:
  `Docs/UltraCanvas/UltraCanvas<Name>.md` for a component, a row on the
  catalogue page for a new element, the module README for a module.
- **After any docs edit**: `python3 scripts/generate_llms_txt.py`. CI
  verifies `llms.txt` and `llms-full.txt` are in sync, and they are what the
  next assistant session reads.
- A new third-party dependency is recorded in `Docs/Dependencies.md`,
  `master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md`, or it does not go
  in.

## Step 9 — Work in bounded sessions, on a branch, through a pull request

How you drive the assistant matters as much as what it knows.

1. **One change per session.** "Add the settings dialog" is a session; "build
   the app" is not. A session that runs for hours loses the thread, and its
   container is discarded at the end, so work that was never pushed is
   gone, not pending.
2. **Branch, commit, push, then open a pull request.** CI (`build.yml`, the
   checks, the llms sync) runs on pull requests and on `main`, not on a bare
   feature branch. A branch with no PR gets no CI validation.
   Titles say what changed and why, never the branch name.
3. **Read the `## Delivery` block.** Every reply from an assistant session
   here ends with three blocks (`## Delivery`, `## Next Task`,
   `## Other recommendations`) and the line `Code needs to be PRed (N lines)`.
   The `Stop` hook measures `N` and blocks a reply that misreports it. If
   `Delivery` says *uncommitted* or *no pull request*, nothing has reached
   anyone yet; act on it.
4. **Do not merge while the session may still push.** A merged PR cannot
   receive new commits; a session that keeps pushing to a merged branch
   strands its work. Merge when the session says it is done, or tell it to
   restart from `main`.
5. **When the assistant pushes back, read why.** `AGENTS.md` tells it to
   state a concern and continue, and to put out-of-scope defects under
   `## Other recommendations` rather than fix them silently. Those bullets
   are your backlog.

## Step 10 — Package

- Linux: `./package-linux.sh` (portable tree with every app and a shared
  `lib/`) or `appimage/` for a single-app AppImage.
- Windows: `./package-win.sh`; `./set-version.sh` refreshes the `.rc` and
  manifest literals that windres needs.
- macOS: `./package-macos.sh`, or `package_and_notarize-macos.sh` for a
  signed, notarised bundle (`Docs/UltraCanvas/UltraCanvasMacBundle.md`).
- WebAssembly is experimental: `Docs/UltraCanvas/UltraCanvasWebAssembly.md`
  lists what differs in the browser (`Run()` never returns, no synchronous
  file pickers, text-only clipboard).

---

## Prompt recipes for Claude Code

What to say, in the order a project needs it. Each one names the documents the
assistant must read, which is what stops it guessing.

**Starting the app**

> Create a new application `Apps/MyApp` modelled on `Apps/UltraAuthenticator`:
> a headless core library plus a GUI target, `--version` from
> `Docs/MyApp/CHANGELOG.md` through `cmake/UltraCanvasVersion.cmake`, the
> version in the window title, an icon under `media/appicon/`, and the
> `option()` block in the root `CMakeLists.txt`. It should open one window with
> an `UltraCanvasToolbar` and an `UltraCanvasSplitPane`. Read `AGENTS.md`,
> `Docs/UltraCanvas/UltraCanvasUIElements.md` and the two component docs first.

**Adding a screen**

> Add an import page: an `UltraCanvasListView` of the files on the left and an
> `UltraCanvasMediaViewer` preview on the right, inside the existing split
> pane. Files are chosen with `UltraCanvasDialogManager`. Read
> `UltraCanvasListViewExamples.md` and `UltraCanvasMediaViewer.md` before
> writing code, and use `UltraCanvasBoxLayout` for the toolbar row.

**Using a module**

> Fetch the catalogue JSON over HTTPS with UltraNet (`Docs/Modules/UltraNet/README.md`,
> `Masterfile_modules.md` §UltraNet), parse it with `UltraCanvasJSON`, and
> cache it in an UltraDatabase SQLite table with parameter binding. Keep TLS
> verification on. Add a test under `Tests/` that runs against the mock.

**Before pushing**

> Run the build and every `scripts/check_*.py`, fix what they report, regenerate
> `llms.txt` if you touched docs, commit with a subject that says what changed
> and why, push, and report the `## Delivery` block.

**When something looks wrong**

> Run `./build/bin/MyApp`, take a screenshot of the import page, and compare it
> with `Apps/DemoApp`'s list view page. Tell me what differs before changing
> anything.

## Checklist

- [ ] Toolchain installed; the full tree builds and `UltraCanvasDemo` runs
- [ ] The assistant has read `AGENTS.md` (Claude Code: automatic via `CLAUDE.md`)
- [ ] App skeleton copied from an existing app (in-tree) or from `Tests/PackageConsumer` (out-of-tree); changelog, version line and CMake option in place
- [ ] Every control comes from the catalogue; layouts from the layout engines
- [ ] Modules used only through their public surfaces; TLS on, SQL bound
- [ ] Build and all `scripts/check_*.py` are green locally
- [ ] Docs updated and `llms.txt` regenerated in the same change
- [ ] Committed, pushed, pull request open, `## Delivery` block read
- [ ] App run and looked at on each platform you ship to
