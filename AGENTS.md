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
  now been fixed three times - in CSS, in SVG, and across the file-format
  readers and writers in 0.9.42 - which is why the rule is now checked rather
  than remembered: `scripts/check_locale_numbers.py` blocks a new one, and
  `scripts/locale_numbers_baseline.txt` lists the sites still to fix. A number
  a person typed or reads follows their locale on purpose and says so at the
  site with `// locale-ok: <why>`.
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
cycle, and stay `shared_ptr`. `scripts/check_callback_cycles.py` enforces this
and runs in CI; a genuine exception opts out with
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
| `Docs/DeviceExplorer/CHANGELOG.md` | DeviceExplorer |
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
   that too.
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

1. **`## Delivery`** — where the code actually IS, in three facts, every time
   any code was written:
   - **how much**: files changed and `+added/-removed` lines, from
     `git diff --shortstat` against the base branch — not a prose estimate;
   - **committed and pushed?** the branch name and short SHA, or plainly that
     the work is still only in the working tree;
   - **is it a pull request?** the number and link, or the words **no pull
     request** — never silence. "Pushed" is not "in review": a branch nobody
     has opened a PR for reaches no reviewer and no `main`, and a reader who
     is told a change is "done and pushed" will reasonably assume otherwise.
     If a PR exists, say its state too (open / merged / CI red / waiting on
     review), because an open PR that is failing is not delivered either.

   Write it even when the answer is unwelcome — *"3 commits, 20 files,
   +1130/−85, pushed to `claude/…`, **no pull request**"* is exactly the line
   that must not be left out. Omit the block only for a reply that changed no
   code at all (a question answered, a file read).
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

For maintainers:

7. **Merge with the PR title, not the branch name.** GitHub's default merge
   commit is `Merge pull request #N from <owner>/<branch>`, which puts the
   branch's random words into `main`'s history. In the repository's
   *Settings → General → Pull Requests*, set the merge-commit default message
   to **Pull request title** (or *Pull request title and description*), and
   squash merges to **Pull request title** as well. Until that is set, edit
   the commit message in the merge dialog before confirming.

8. **Do not merge a session's PR while the session may still push to it.**
   Merge after the session says it is done — or, if merging early, tell the
   session so it restarts from `main` and opens a fresh PR for the rest.
