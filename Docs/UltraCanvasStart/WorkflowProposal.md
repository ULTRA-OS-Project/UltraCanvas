# UltraCanvasStart: a stepper instead of tabs — Proposal

**Status:** Proposal, not implemented. UltraCanvasStart 0.2.0 keeps the
seven tabs and restyles them; this document is the next change, for the
author to accept, alter or decline.
**Scope:** `Apps/UltraCanvasStart/ui/`. The engine (`engine/`) stays as it
is: it already produces the plan as steps.
**Last Modified:** 2026-10-10

## 1. What the tabs hide

The seven tabs are Platform, System, Choices, Install, Project, AI and
Report. They are the chapters of `Docs/GettingStarted.md`, which is why the
first version used them, but a chapter list is not a workflow:

- **The order is not visible.** Nothing says that Install depends on
  Choices, that Project needs the SDK, or that AI comes after Project. A
  reader lands on Platform, which asks a question most people have no reason
  to answer (the platform was detected), and does not know where to go next;
  the status line is the only guide.
- **The pages mix three kinds of thing.** Platform and AI are reading
  matter. System and Report are reports. Choices, Install and Project are
  the work. Tabs give all seven the same weight.
- **Getting the framework is hidden.** The SDK download, the one step every
  programmer needs, is a *Download...* button on the Project page next to
  the SDK prefix, and the alternative (clone and build) is a checkbox on
  the Choices page. The plan the engine builds lists *Get the SDK* as a
  step of its own; the window does not.
- **Nothing says what is done.** The checks run when the Install page is
  opened, the install runs on a button, the project is written on another,
  and no page shows which of these has happened. The report does, as text.

## 2. The proposed structure

A horizontal `UltraCanvasStepper` (`Docs/UltraCanvas/UltraCanvasStepper.md`:
numbered markers, `Linear` navigation, a check on completed steps, an error
marker where something is missing) above one pane that shows the current
step, and Back / Next buttons below it. Five steps, in the order the work
happens:

```
  (1) Your computer ── (2) Features ── (3) Tools ── (4) Framework ── (5) Project
       detected           what for      checks,      SDK or clone     name, folder,
       + the guide        and whether   install                       create, done
                          Claude Code
```

| Step | What it shows | What it decides | Next is... |
|---|---|---|---|
| **1 Your computer** | Today's System page: the key/value cards. A *Read the guide for* dropdown (Linux, macOS, Windows, preselected to the detected one) with the platform guide under it, so another OS's way in is one click away without being a step. | Nothing; it is where the reader starts. | always on |
| **2 Features** | Today's Choices page: the feature groups, the assistant. The *prebuilt SDK / clone* choice moves to step 4. | Which packages step 3 checks; whether CLAUDE.md and the first prompt are written. | always on |
| **3 Tools** | Today's Install page. The checks run on entering, the list shows what was found, *Install what is missing* runs the package manager, the output console below. | Whether the toolchain and libraries are there. | on when nothing is missing; otherwise a *Continue anyway* that marks the step with the error marker |
| **4 Framework** | New: *Use the prebuilt SDK* (the archive named and linked, *Download and unpack...* into a chosen folder, the prefix shown when found, *Find...* for one already unpacked) or *Clone and build the repository* (the clone command, the folder). | Where `find_package(UltraCanvas)` will look. | on when a prefix or a checkout is set, or with *I'll do it later* |
| **5 Project** | Today's Project page: name, folder, the files previewed, *Create the project*. After the creation the pane becomes the **Done** page: what was written, the three commands to build, the first prompt for Claude Code with its *Copy* button when the assistant was chosen, and *Copy the report*. | The application. | *Create the project*, then *Finish* |

Two things stop being steps and become **reference panes** reachable from
the header at any time, through two quiet buttons next to the detected
platform: **Guide** (the platform guide for any OS, as on step 1) and
**Report** (today's Report page, in a dialog with *Copy*). The AI page's
explanations (how the assistant works, the cloud checklist) go to the Done
page and to the guide, where the reader is when they matter.

## 3. Behaviour

- **The stepper is the progress.** Steps before the current one show the
  check mark; step 3 shows the error marker while something is missing and
  the reader went on anyway; step 4 shows it while no framework is set.
  Clicking a completed marker goes back to it (`Linear` mode); the future
  ones are not clickable.
- **Next is the one primary button** on every pane, bottom right, and the
  pane's own action (*Install what is missing*, *Download and unpack...*,
  *Create the project*) is the second accent button when it exists. Back is
  secondary. While a worker runs (checks, install, download) both are
  disabled and the status band says what is happening.
- **Each pane opens with one sentence** saying what the step decides, in
  the secondary grey under its heading, as the cards do today.
- **The platform question goes away.** The guide for another OS is reading
  matter, offered on step 1 and from the header; the stepper itself is
  always for the computer it runs on.
- **`--page` becomes `--step <n>`**, and `--for <os>` opens the guide pane
  on that OS.

## 4. What it costs

The engine needs nothing: `BuildPlan` already produces Install, Download,
Clone, Scaffold and Manual steps, and `RenderReport` the text. In the
window:

| Today | Then |
|---|---|
| `UltraCanvasTabbedContainer` with seven pages | `UltraCanvasStepper` + one container that shows the current pane; a Back / Next row |
| `BuildPlatformPage` | the guide pane (dropdown + Markdown view), used by step 1 and the header's *Guide* |
| `BuildSystemPage` | step 1 (the cards, plus the guide below them) |
| `BuildChoicesPage` | step 2, minus the SDK / clone boxes |
| `BuildInstallPage` | step 3, plus the Next rule |
| the SDK row of `BuildProjectPage` and the clone box | step 4 (new pane, the existing `DownloadAndUnpackSdk` and `CloneStep` behind it) |
| `BuildProjectPage` and `BuildAiPage` | step 5 and its Done state |
| `BuildReportPage` | a dialog from the header |
| `OnTabEntered` | `OnStepEntered`, the same hooks |

About three hundred lines move and a hundred are new, all in
`UltraCanvasStartWindow.cpp`; the theme header and `StartGuide` carry over
unchanged. One session's work, with the screenshots of each step as the
check (`xvfb-run` and `--step`, as 0.2.0's screenshots were made).

## 5. Open questions

1. **Should step 3 block Next while something is missing?** The proposal
   says no (*Continue anyway* with the error marker), because a programmer
   who installs by hand must be able to go on. Blocking would be simpler to
   explain.
2. **Should the SDK download start by itself** when *prebuilt SDK* is the
   choice and the folder is known, or stay a button? A button, in the
   proposal: it writes a hundred megabytes somewhere.
3. **Does the Report deserve a step?** The proposal makes it a dialog. If
   the report is how most people hand a problem to the assistant, a sixth
   step *Hand over* with the report and the first prompt side by side would
   say so.
