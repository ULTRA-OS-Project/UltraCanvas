<!-- Generated from Apps/UltraCanvasStart/README.md by scripts/generate_llms_txt.py; edit that file, then rerun the script. -->
# UltraCanvasStart

## Overview

UltraCanvasStart sets a computer up for writing UltraCanvas applications. It
is the application form of [`Docs/GettingStarted.md`](../GettingStarted.md):
instead of reading the per-platform steps and typing them, the programmer
opens UltraCanvasStart, answers a few questions and lets it check, install
and write.

Its first page is the same platform selector the guide opens with,
preselected to the platform it runs on. Another platform can be picked to
read that platform's way in; only the detected one can be checked and
installed.

## Getting it

UltraCanvasStart has to reach a computer that has nothing yet, so building it
from source is not the way in. Every release of the framework carries it on
its own, built for each platform, beside the SDK archives it downloads:

```
https://github.com/ULTRA-OS-Project/UltraCanvas/releases/download/v<version>/UltraCanvasStart-<OS>-<version>-<arch>.<ext>
```

| Platform | Archive | Inside |
|---|---|---|
| Linux | `UltraCanvasStart-Linux-<version>-<x86_64\|arm64>.tar.xz` | `UltraCanvasStart` (the launcher), `bin/`, `lib/` with the libraries it loads, `share/media` |
| Windows | `UltraCanvasStart-Windows-<version>-<x86_64\|arm64>.zip` | `UltraCanvasStart.exe` with its DLLs, `cacert.pem`, `Resources/media`, the `uc-diagnose` launchers |
| macOS | `UltraCanvasStart-MacOS-<version>-<x86_64\|arm64>.dmg` | `UltraCanvasStart.app` with its own `Frameworks/`, signed and notarized |

`<version>` is the framework's, the same as the SDK's and the release tag's.
The archives are cut out of the suite packages by
`scripts/package-ultracanvasstart.sh` (Linux and Windows) and
`package-macos.sh --start-app` (macOS) on every CI leg; the script runs the
packaged application before it is done, so an archive that does not start is
a red check, not a download. The Windows executable is not Authenticode
signed, so SmartScreen asks once.

The archives are not small - the Linux one unpacks to about 220 MB - because
the application loads the framework's shared core, and the core links
everything the framework can do. What that weight is made of, and what would
and would not reduce it, is measured in
[`Docs/UltraCanvas/StandaloneSizeInvestigation.md`](../UltraCanvas/StandaloneSizeInvestigation.md).

## Steps

The window is a stepper (`Docs/UltraCanvasStart/WorkflowProposal.md`): five
steps in the order the work happens, Back and Next below the pane, and the
Guide and the Report as dialogs from the header at any time. It is styled
like UltraMail (`ui/UltraCanvasStartTheme.h` carries UltraMail's colours,
type sizes and metrics): white cards on a near-white page, one accent
button per step. The prose comes from the engine as Markdown
(`engine/StartGuide`) and is shown in read-only Markdown views, so every
address is a link that opens in the browser, everything typed stands out
as `code`, and each numbered step of a guide is one action.

| Step | What it shows | What it decides |
|---|---|---|
| **1 Your computer** | What was detected, as key/value rows: OS, architecture, distribution, the package manager (a found / not-found badge and its path), the MSYS2 root on Windows (a link to install it when missing), home, the AI assistants found, git, the framework version. Under it the guide: Linux, macOS or Windows in a segmented control, preselected to the detected one, and that platform's way in as a numbered guide with the SDK archive named for this version and linked to its release. | Nothing; it is where the reader starts. |
| **2 Features** | The feature groups (toolchain and framework core always; CDR, PDF, OCR, Vectorizer, audio, barcode, networking extras as options). The assistant: Claude Code, Codex, Copilot, Gemini or another in a segmented control; whether its instruction file goes into the project and the first prompt is prepared; whether the work goes through GitHub alone. For the chosen assistant: whether its command-line tool is installed and how to install and sign in, how it picks up the repository's guidance (`CLAUDE.md`, `AGENTS.md`, `GEMINI.md`, or pasted instructions and `llms-full.txt` for a chat), how an assistant works on an UltraCanvas application, and the no-local-compiler workflow of [`GettingStarted-Cloud.md`](../GettingStarted-Cloud.md). | Which packages step 3 checks; which file the project gets. |
| **3 Tools** | The checks run on entering: every tool (`--version`) and library (`pkg-config --modversion`) the chosen groups need, each with a mark, the version found and, when missing, the package that provides it; a badge says *not checked yet*, *N missing* or *everything installed*. *Install what is missing* (enabled only while something is) runs the package manager after a confirmation that shows the exact command: `apt-get` / `dnf` / `pacman` / `zypper` behind pkexec or sudo, `brew` as the user, MSYS2's `pacman` into the MSYS2 tree. The output is shown in a console and the checks run again. Next goes on even with something missing; the step keeps the error marker until the checks pass. | Whether the toolchain and libraries are there. |
| **4 Framework** | *The prebuilt SDK* or *A clone, built from source*. For the SDK: the version, the matching archive, its download address, the release page, the workflow artifacts while a release is still building; *Download and unpack...* fetches the archive from the release into a folder you pick, unpacks it and fills the prefix in; *Find...* looks for `lib/cmake/UltraCanvas/UltraCanvasConfig.cmake` under a chosen folder. For the clone: the clone command and how the project finds the checkout. The step carries the error marker while the SDK is chosen and no prefix is set. | Where `find_package(UltraCanvas)` will look. |
| **5 Project** | Application name and folder, the files previewed, *Create the project*, which writes `CMakeLists.txt`, `main.cpp`, `CMakePresets.json`, `README.md` and the chosen assistant's instruction file. Then the **Done** page: what was written, the three build commands, the first prompt to give the assistant with *Copy the first prompt*, and *Copy the report*. | The application. |

**Guide** (header): the way in for any platform, the same guide as step 1,
in a dialog. **Report** (header): the system, the checks, the plan and the
notes as text, with *Copy*.

## Command line

```
UltraCanvasStart --check            # detect, check, print the report; exit 2 if something is missing
UltraCanvasStart --plan             # ...and the install plan
UltraCanvasStart --plan --for macos # the plan for another platform
UltraCanvasStart --check --all      # every feature group
UltraCanvasStart --step 3           # open the window on a step (for screenshots and support)
UltraCanvasStart --page report      # ...or open the Report (or Guide) dialog; the old page names map to steps
UltraCanvasStart --for windows      # ...with that platform's guide preselected
UltraCanvasStart --plan --assistant codex   # the plan and the project for another assistant
UltraCanvasStart --version
```

`--check` and `--plan` open no window, so they work over ssh and in CI, which
runs `--check` on every platform leg.

## Engine

`UltraCanvasStartEngine` (namespace `UltraCanvasStart`, `engine/`) is a
static library the GUI, the CLI modes and the tests share:

| Module | Contents |
|---|---|
| `StartTypes` | `Platform`, `PackageManager`, `SystemProfile`, `Dependency`, `CheckResult`, `Assistant`, `Choices`, `PlanStep`, `Plan`; `CompareVersions`, `ExtractVersion` |
| `StartSystem` | `DetectSystem` (hardware snapshot, os-release, MSYS2 root), `ParseOsRelease`, `PackageManagerForDistribution`, `FindProgram` |
| `StartPackages` | the dependency table with the package per manager (apt, dnf, pacman, zypper, Homebrew, MSYS2), `InstallStep`, MSYS2 architecture prefixes |
| `StartChecks` | `CheckTool`, `CheckPkgConfigModule`, `RunChecks` |
| `StartPlan` | `BuildPlan`, `RenderReport`, `PlatformNotes`, `DisplayCommand` |
| `StartSdk` | artifact names matching the CI workflow, download page and release URL, `DownloadSdk` (UltraNet, when built), `UnpackStep`, `FindSdkPrefix` |
| `StartProject` | the scaffold files and `ScaffoldProject`, `CloneStep` |
| `StartAi` | `DetectAi` (claude, codex, copilot, gemini), `AssistantInstallInstructions`, `AssistantGuidanceNotes`, `FirstPrompt`, `CloudChecklist` |
| `StartGuide` | the pages' prose as Markdown: `PlatformGuide` (one action per step, the SDK named and linked), `ChecksMarkdown`, `AiGuide`; `PlainText` for the report, `Linkify` |
| `StartRunner` | `RunStep` through `RunProcessCaptured`, with pkexec/sudo for elevated steps |

Every program runs through `RunProcessCaptured` with an argument list; no
shell ever sees a string. The dependency table mirrors step 1 of
`Docs/GettingStarted.md` and the CI install steps: change those together.

## Tests

`Tests/UltraCanvasStart` (`-DULTRACANVAS_BUILD_ULTRACANVASSTART_TESTS=ON`,
target `UltraCanvasStartEngineTests`): os-release parsing, version
comparison, the table, the install command, SDK names, the plan, the
scaffold over a temporary folder with a non-ASCII name, and the guides (one
action per numbered step, the SDK named for real, the report plain).

## Versioning

The version is the first line of `Docs/UltraCanvasStart/CHANGELOG.md`, read by
`cmake/UltraCanvasVersion.cmake` into `ULTRACANVASSTART_VERSION`. Every change
to the application gets a new entry there.
