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
  UltraDatabase, FileLoader, VirtualFS, OCR, PDF, QRCode, …).
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
  auto button = CreateButton("MyButton", 101, 100, 50, 120, 40, "Click Me");
  // equivalent: std::make_shared<UltraCanvasButton>("MyButton", 101, 100, 50, 120, 40, "Click Me")
  ```

- **Never hand-roll a UI element** — see
  [Build UI out of UltraCanvas elements](#build-ui-out-of-ultracanvas-elements)
  below. If the thing you are drawing takes input, shows a picture or presents
  a value, it is an element: use the framework's, or add one.
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
  now been fixed twice, in CSS and in SVG; the remaining ~110 call sites
  elsewhere in the tree are the same defect waiting to be reported.
- **Third-party code** is vendored under `UltraCanvas/third_party/` and
  `3rdparty/` — do not modify it, and record licenses in
  `THIRD_PARTY_LICENSES.md`.

## Build UI out of UltraCanvas elements

The framework ships ~60 UI elements. New UI is assembled from them; it is not
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
is the catalogue** — every element, what it is for, and its defining header.
Start there; the per-component docs only help once you know the name. The ones
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

Two exceptions only: a **self-rendered view** may paint its own *content*
(`UltraCanvasFilerWidget`, `UltraCanvasAlbum`, charts) — but it still adds real
elements as children for fields, buttons and pickers; and **the element itself**
naturally owns its buffer and caret. Declare any other exception in the source:

```cpp
// ui-reuse-exempt: <why this one paints directly>
```

`scripts/check_ui_reuse.py` enforces this and runs in CI.
`scripts/ui_reuse_baseline.txt` — which records pre-existing offenders — is
empty, and the intent is that it stays empty. Do not add to it to silence a
finding.

## Building and testing

```bash
# Ubuntu/Debian deps
sudo apt install build-essential cmake libcairo2-dev libpango1.0-dev \
    libfreetype6-dev libvips-dev libharfbuzz-dev clang
# macOS deps
brew install cmake cairo pango freetype vips harfbuzz

mkdir build && cd build && cmake .. && make
```

The project now defaults to Clang on Linux, so install the `clang` package
alongside the existing deps. The build uses the system default linker (GNU ld,
same as CI); with a newer Clang on an older distro it automatically drops to
DWARF4 so binutils 2.38's `ld` does not choke on clang's DWARF5 output.

The full 3-OS dependency lists are in `.github/workflows/build.yml`.
UltraAI builds standalone: `cmake -S UltraAI -B build -DULTRAAI_BUILD_TESTS=ON`
then `ctest --test-dir build`. Framework tests live under `Tests/`.

`-DULTRACANVAS_BUILD_NET_TESTS=ON` adds two UltraNet binaries: `UltraNetTests`
(pass/fail suite) and `UltraNetApiStatus`, which probes every public
`UltraNet_*` entry point and prints WORKING / IMPLEMENTED / NOT IMPLEMENTED /
BROKEN per function — run it before assuming a networking API is usable in a
given build. See `Docs/Modules/UltraNet/ApiStatus.md`.

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
| `Docs/EmailCleaner/CHANGELOG.md` | EmailCleaner |
| `Docs/Ladybird/CHANGELOG.md` | The Ladybird browser port (built from its own tree, outside this repository) |
| `Docs/Modules/UltraWin/CHANGELOG.md` | UltraWin — the Windows tier, UltraWinManager and UltraWinSetup |
| `Docs/Texter/CHANGELOG.md` | UltraTexter |
| `Docs/UltraAI/CHANGELOG.md` | UltraAI and its dashboard app |
| `Docs/UltraAuthenticator/CHANGELOG.md` | UltraAuthenticator |
| `Docs/UltraCleaner/CHANGELOG.md` | UltraCleaner |
| `Docs/UltraFiler/CHANGELOG.md` | UltraFiler |
| `Docs/UltraMail/CHANGELOG.md` | UltraMail |
| `Docs/UltraNetMonitor/CHANGELOG.md` | UltraNetMonitor |
| `Docs/UltraPaint/CHANGELOG.md` | UltraPaint |
| `Docs/UltraSocial/CHANGELOG.md` | UltraSocial |
| `Docs/UltraViewer/CHANGELOG.md` | UltraViewer |

Format: `#### YYYY-MM-DD *x.y.z*`. To release, add an entry at the top of the
changelog — that is the whole bump. Do **not** hand-edit a version number
anywhere else, and never introduce a new literal copy of one:

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
- An app versions itself: it does not move when the framework releases, and a
  change to it belongs in its own file, not in the framework's. A framework
  change an app needs still goes in `Docs/UltraCanvas/CHANGELOG.md` — including
  the Ladybird-driven ones, which land in `UltraCanvas/OS/MSWindows/` and
  `UltraCanvas/core/` rather than in the port. Cross-reference such a change
  from the app's changelog when a release depends on it; never describe it in
  two files with two versions.
- The app changelogs were split out of the framework's on 2026-08-31.
  EmailCleaner's two entries were moved across verbatim (framework 0.3.87 and
  0.3.88 now point at them); every other app's earlier history was left where
  it was published, so `Docs/UltraCanvas/CHANGELOG.md` remains the record of
  what shipped in each framework release. Do not backfill it into the app
  files — that would put one change in two places under two numbers.
- **Your entry must be a NEW top entry with a number nobody else has taken.**
  Line 1 of a shared file is the most contended line in the repository, and two
  open pull requests collide there every time, in one of two ways. Either a
  branch picks the next number, `main` releases past it while the branch waits
  for review, and it merges carrying a number *lower* than versions already
  released below it — the product's version then goes backwards. Or two
  branches write the same `#### <date> *x.y.z*` line, git merges both bullet
  lists under the one header with no conflict, and two releases share a number
  while the version never increments — which is also why such a branch's
  changelog diff never settles no matter how often `main` is merged into it.
  Both have happened repeatedly; the file still carries sixteen duplicated
  numbers from before this was checked. So: re-read the top of the changelog
  just before you push, and if `main` has moved past your number, renumber your
  entry rather than leaving it — and never add bullets to an entry that is
  already on `main`. Run `python3 scripts/check_changelog.py --base origin/main`
  before pushing; CI runs it too.
  GitHub's *Update branch* button cannot do the renumbering: it merges `main`
  into the branch and, when `main` has meanwhile released the number the
  branch chose, folds the two entries under the one header (or leaves a
  conflict marker in line 1) — and the guard then fails on the very merge
  that was meant to fix it. When the check goes red after such a merge, fix
  it locally: merge `main`, split the shared header back into two entries,
  give the branch's entry the next free number, and push.
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
   [`Docs/UltraCanvas/UltraCanvasUIElements.md`](Docs/UltraCanvas/UltraCanvasUIElements.md).
   Writing `DrawText` / `FillRoundedRectangle` plus a private buffer, caret or
   `hovered` flag to make a control is a defect, not a shortcut. Run
   `python3 scripts/check_ui_reuse.py` before pushing; CI runs it too.
3. Check `Docs/UltraCanvas/<Component>*.md` (or `llms.txt`) before using a
   component; if you add or change public API, update the matching doc in
   the same change.
4. Keep platform-independent logic out of `OS/<Platform>/` and vice versa.
5. Do not introduce new third-party dependencies without updating
   `Docs/Dependencies.md`, `master_dependencies.yaml` and
   `THIRD_PARTY_LICENSES.md`.
6. Docs changes: regenerate `llms.txt`/`llms-full.txt`
   (`python3 scripts/generate_llms_txt.py`) — CI verifies they are in sync.

## Reporting back (AI sessions)

Finish every reply that reports work — the end of a task, a check-in, a
status update — with these two blocks, in this order, after the prose that
says what happened. They are headings, not prose: a reader scanning for "what
now" must find it without reading the report.

```markdown
## Next Task
...

## Other recommendations
...
```

1. **`## Next Task`** — what happens next, and who does it. One or two lines:
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

Both blocks describe the repository, not the conversation. "Waiting for the
test suite" belongs in `Next Task`; "the Alembic reader drops transforms"
belongs in `Other recommendations` whether or not anyone asked about Alembic.

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

   It lists the commits on the branch that are not in `main` and exits 1 when
   there are any. Then verify on GitHub that an *open* PR has this branch as
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

For maintainers:

6. **Do not merge a session's PR while the session may still push to it.**
   Merge after the session says it is done — or, if merging early, tell the
   session so it restarts from `main` and opens a fresh PR for the rest.
