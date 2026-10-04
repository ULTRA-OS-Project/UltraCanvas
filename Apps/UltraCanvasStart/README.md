# UltraCanvasStart

## Overview

UltraCanvasStart sets a computer up for writing UltraCanvas applications. It
is the application form of [`Docs/GettingStarted.md`](../../Docs/GettingStarted.md):
instead of reading the per-platform steps and typing them, the programmer
opens UltraCanvasStart, answers a few questions and lets it check, install
and write.

Its first page is the same platform selector the guide opens with,
preselected to the platform it runs on. Another platform can be picked to
read that platform's way in; only the detected one can be checked and
installed.

## Pages

| Page | What it does |
|---|---|
| **Platform** | Linux, macOS or Windows; the detected one is preselected. The page shows that platform's setup in brief. |
| **System** | What was detected: OS, architecture, distribution, package manager, the MSYS2 root on Windows, Claude Code and git, the SDK archive that matches this build and where it is published. |
| **Choices** | Which feature groups are needed (toolchain and framework core always; CDR, PDF, OCR, Vectorizer, audio, barcode, networking extras as options), whether to use the prebuilt SDK or build from source, whether to clone the repository, and whether the programmer works with Claude Code, locally or through GitHub alone. |
| **Install** | Checks every tool (`--version`) and library (`pkg-config --modversion`) the chosen groups need and lists what is missing with the package that provides it. *Install what is missing* runs the package manager, after a confirmation that shows the exact command: `apt-get` / `dnf` / `pacman` / `zypper` behind pkexec or sudo, `brew` as the user, MSYS2's `pacman` into the MSYS2 tree. The output is shown and the checks run again. |
| **Project** | Application name, project folder, SDK prefix (*Find...* looks for `lib/cmake/UltraCanvas/UltraCanvasConfig.cmake` under a chosen folder). *Create the project* writes `CMakeLists.txt`, `main.cpp`, `CMakePresets.json`, `README.md` and, for AI users, `CLAUDE.md`. The files are previewed on the page. |
| **AI** | Whether Claude Code is installed and how to install it, how the assistant works on an UltraCanvas application, the checklist of [`GettingStarted-Cloud.md`](../../Docs/GettingStarted-Cloud.md) for the no-local-compiler workflow, and the first prompt to give the assistant, ready to copy. |
| **Report** | The system, the checks, the plan and the notes as text, for the clipboard. |

## Command line

```
UltraCanvasStart --check            # detect, check, print the report; exit 2 if something is missing
UltraCanvasStart --plan             # ...and the install plan
UltraCanvasStart --plan --for macos # the plan for another platform
UltraCanvasStart --check --all      # every feature group
UltraCanvasStart --version
```

`--check` and `--plan` open no window, so they work over ssh and in CI, which
runs `--check` on every platform leg.

## Engine

`UltraCanvasStartEngine` (namespace `UltraCanvasStart`, `engine/`) is a
static library the GUI, the CLI modes and the tests share:

| Module | Contents |
|---|---|
| `StartTypes` | `Platform`, `PackageManager`, `SystemProfile`, `Dependency`, `CheckResult`, `Choices`, `PlanStep`, `Plan`; `CompareVersions`, `ExtractVersion` |
| `StartSystem` | `DetectSystem` (hardware snapshot, os-release, MSYS2 root), `ParseOsRelease`, `PackageManagerForDistribution`, `FindProgram` |
| `StartPackages` | the dependency table with the package per manager (apt, dnf, pacman, zypper, Homebrew, MSYS2), `InstallStep`, MSYS2 architecture prefixes |
| `StartChecks` | `CheckTool`, `CheckPkgConfigModule`, `RunChecks` |
| `StartPlan` | `BuildPlan`, `RenderReport`, `PlatformNotes`, `DisplayCommand` |
| `StartSdk` | artifact names matching the CI workflow, download page and release URL, `DownloadSdk` (UltraNet, when built), `UnpackStep`, `FindSdkPrefix` |
| `StartProject` | the scaffold files and `ScaffoldProject`, `CloneStep` |
| `StartAi` | `DetectAi`, install instructions, `FirstPrompt`, `CloudChecklist` |
| `StartRunner` | `RunStep` through `RunProcessCaptured`, with pkexec/sudo for elevated steps |

Every program runs through `RunProcessCaptured` with an argument list; no
shell ever sees a string. The dependency table mirrors step 1 of
`Docs/GettingStarted.md` and the CI install steps: change those together.

## Tests

`Tests/UltraCanvasStart` (`-DULTRACANVAS_BUILD_ULTRACANVASSTART_TESTS=ON`,
target `UltraCanvasStartEngineTests`): os-release parsing, version
comparison, the table, the install command, SDK names, the plan, the
scaffold over a temporary folder with a non-ASCII name.

## Versioning

The version is the first line of `Docs/UltraCanvasStart/CHANGELOG.md`, read by
`cmake/UltraCanvasVersion.cmake` into `ULTRACANVASSTART_VERSION`. Every change
to the application gets a new entry there.
