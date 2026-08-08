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
| FileLoader | Universal file load/save/convert facade | `Docs/Modules/FileLoader` |
| VirtualFS | Virtual filesystem and compression | `VirtualFS/` |
| File-type plugins | Charts, diagrams, vector, documents, video, … | `UltraCanvas/Plugins/` |

The authoritative module registry — purpose and public function surface of
every module — is [`Masterfile_modules.md`](Masterfile_modules.md). Read it
before adding cross-module code.

## Documentation map (read before using a component)

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
- **Third-party code** is vendored under `UltraCanvas/third_party/` and
  `3rdparty/` — do not modify it, and record licenses in
  `THIRD_PARTY_LICENSES.md`.

## Building and testing

```bash
# Ubuntu/Debian deps
sudo apt install build-essential cmake libcairo2-dev libpango1.0-dev \
    libfreetype6-dev libvips-dev libharfbuzz-dev
# macOS deps
brew install cmake cairo pango freetype vips harfbuzz

mkdir build && cd build && cmake .. && make
```

The full 3-OS dependency lists are in `.github/workflows/build.yml`.
UltraAI builds standalone: `cmake -S UltraAI -B build -DULTRAAI_BUILD_TESTS=ON`
then `ctest --test-dir build`. Framework tests live under `Tests/`.

## Versioning

The **first line of a changelog is the single source of truth** for a version:

| Changelog | Drives |
|---|---|
| `Docs/UltraCanvas/CHANGELOG.md` | UltraCanvas core, DemoApp, UltraFiler, UltraViewer, … |
| `Docs/Texter/CHANGELOG.md` | UltraTexter |

Format: `#### YYYY-MM-DD *x.y.z*`. To release, add an entry at the top of the
changelog — that is the whole bump. Do **not** hand-edit a version number
anywhere else, and never introduce a new literal copy of one:

- `cmake/UltraCanvasVersion.cmake` parses the first line at configure time and
  sets `ULTRACANVAS_VERSION` / `ULTRATEXTER_VERSION` (plus `_DOT4` / `_COMMA4`
  variants for Windows resources). It feeds every `project(VERSION …)` and the
  matching compile definitions. Editing a changelog re-triggers the configure
  step, so existing build trees follow along.
- Code that displays a version reads those defines —
  `UltraCanvas::versionString` (`UltraCanvasUtils.cpp`, shown in the demo app's
  info window) and `UltraCanvasTextEditor::version` (shown in Texter's splash).
- The packaging scripts (`build-demoapp-appimage.sh`, `package-win.sh`,
  `package-macos.sh`) parse the same line for artefact file names.
- Only `Apps/Texter/UltraTexter.rc` and `Apps/Texter/UltraTexter.manifest` still
  hold literals, because windres reads them from disk. Run `./set-version.sh`
  after bumping the UltraTexter version; a CMake configure on any platform
  warns when they are stale.

## House rules for AI-generated changes

1. Match the style of the file you are editing; PascalCase everywhere.
2. Check `Docs/UltraCanvas/<Component>*.md` (or `llms.txt`) before using a
   component; if you add or change public API, update the matching doc in
   the same change.
3. Keep platform-independent logic out of `OS/<Platform>/` and vice versa.
4. Do not introduce new third-party dependencies without updating
   `Docs/Dependencies.md`, `master_dependencies.yaml` and
   `THIRD_PARTY_LICENSES.md`.
5. Docs changes: regenerate `llms.txt`/`llms-full.txt`
   (`python3 scripts/generate_llms_txt.py`) — CI verifies they are in sync.

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
3. **Confirm delivery after pushing.** Verify an *open* PR exists for the
   branch and that its head is the commit just pushed; report the PR number
   and head SHA. "Pushed" is not "delivered" — only a commit reachable from
   an open PR (or `main`) counts.
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
