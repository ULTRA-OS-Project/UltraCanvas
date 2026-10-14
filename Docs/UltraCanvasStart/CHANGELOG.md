#### 2026-10-12 *0.4.0*
- **Step 6, Build: the project compiled from the window.** The proof that
  the installation works: the two commands a programmer runs are shown for
  the project folder, *Build the project* runs them on the worker thread
  (`engine/StartBuild`: `cmake -S <folder> --preset default`, then
  `cmake --build <folder>/build --parallel N`, with the cmake the checks
  found and, on Windows, the MSYS2 bin directories on the child's PATH),
  cmake's output lands in a console, and a badge says *not built yet*,
  *building...*, *built* or *failed*. A failed build marks the step; so does
  leaving step 5 without a project. The first prompt and *Copy the report*
  move here, after the build, where the hand-over happens; step 5 keeps
  what was written. `--step 6` and `--page build` open it.
- **The engine tests compile the skeleton** against the SDK that
  `ULTRACANVAS_SDK_PREFIX` names, through `BuildProject`, and are skipped
  without the variable. CI sets it on every leg, so the guard runs there
  as well as on a developer's machine before a push.
- **The consoles are dark again.** The theme set a text area's
  `style.backgroundColor`, which the element never reads; the Install and
  Build output were light grey text on white. The background is set on
  the element now (`UltraCanvasStartTheme.h` 0.3.1), and the light
  consoles (the files preview, the first prompt) take the sidebar tint.

#### 2026-10-11 *0.3.2*
- **The project skeleton builds again.** Its `main.cpp` called
  `app.CreateWindow(config)`, a method the application class does not
  have, so `cmake --build` failed on the first line that mattered; it calls
  the framework's `CreateWindow(config)` now, as `Docs/GettingStarted.md`
  does. `test_project.cpp` pins the line.
- **`--create <folder>` writes the skeleton headless** (`--name <app>`,
  `--sdk <prefix>`, `--assistant <ai>`), and CI builds what it writes
  against the SDK of every leg with `cmake --preset default`, the way a
  programmer does, so the skeleton cannot stop compiling unnoticed again.

#### 2026-10-10 *0.3.1*
- **The Tools step shows each check with a switch, not a mark.** The list
  wrote a check mark or a cross before each tool and library, characters the
  bundled Ubuntu font does not have, and on Windows every line began with a
  box of hex digits. Each check is now a row: a switch that shows whether it
  is there (on and green: installed; off: missing, or not checkable without
  pkg-config), the name in bold, the version found or why it is missing and
  the package that provides it, the reason in orange. The switches show and
  do not set: a click changes nothing.
- **What *Install what is missing* will do is shown above the list.** A
  line under the buttons names the package manager, how many are missing
  and whether it asks for administrator rights, with the exact command
  under it - or says that no package manager was found, so Install cannot
  run here. The button is off in that case too, and the status line no
  longer says it runs the package manager.
- **The step header's check marks are drawn too**, by the framework's
  `UltraCanvasStepper` (`Docs/UltraCanvas/changelog.d/stepper-drawn-check.md`).
- The *Install output* console is smaller, giving the list the room, and
  every scrollbar in the application is half as wide and fully rounded.

#### 2026-10-10 *0.3.0*
- **A stepper instead of tabs** (`Docs/UltraCanvasStart/WorkflowProposal.md`,
  implemented). Five steps in the order the work happens, with Back and
  Next below the pane: **Your computer** (what was found, and the guide for
  any platform under it), **Features** (what the application needs, which
  assistant and how to install it), **Tools** (the checks run on entering;
  *Install what is missing*; going on with something missing is allowed and
  the step keeps the error marker until the checks pass), **Framework** (the
  prebuilt SDK, with *Download and unpack...* and *Find...*, or a clone built
  from source, with the clone command; the step carries the marker while
  the SDK is chosen and no prefix is set), **Project** (name, folder,
  *Create the project*; then the Done page with what was written, the three
  build commands, the first prompt for the assistant and *Copy the
  report*). The Guide and the Report are dialogs from the header, at any
  time, so neither is a step. The status band says which step it is and
  what Next does.
- **`--step <n>` opens the window on a step**; the old `--page` names map to
  their steps, and `--page guide` / `--page report` open the dialogs.

#### 2026-10-10 *0.2.0*
- **The assistant is a choice: Claude Code, Codex, Copilot, Gemini or
  another.** The repository is written for any of them (`AGENTS.md` is the
  vendor-neutral file, `llms-full.txt` the corpus), so the AI page offers
  the choice and shows that assistant's setup: whether its command-line
  tool was found (`claude`, `codex`, `copilot`, `gemini`), how to install
  and sign in, one action per line, how it picks up the repository's
  guidance (`CLAUDE.md`, `AGENTS.md`, `GEMINI.md`, or pasted instructions
  and the corpus for a chat), and the no-local-compiler workflow in its
  shape. The project gets the file that assistant reads, the first prompt
  names it, the plan and the report say which one, and `--assistant <ai>`
  chooses it on the command line. The System page lists every assistant
  found.
- **Segmented controls choose the platform and the assistant**, in the
  application's colours: the accent for the chosen segment, white for the
  others, a hairline border.
- **The window looks like UltraMail's.** A theme header (`ui/UltraCanvasStartTheme.h`,
  UltraMail's colours, type sizes and metrics) styles every page: a
  near-white page with white cards, one filled accent button per page and
  quiet secondary buttons, a header band with the name, version and what
  was detected, and a status band below the pages.
- **Links open, commands stand out, important things are bold.** The pages'
  prose is Markdown now (`engine/StartGuide`: the per-platform way in, the
  checks, the assistant page), shown in read-only Markdown views: every
  address is a link that opens in the browser, everything typed is `code` on
  a tinted chip, and what matters is bold. The System page's addresses are
  links too, and its paths can be selected and copied.
- **One action per instruction line.** The platform guides are numbered
  lists where each step is one thing to do; alternatives (the prebuilt SDK
  or a clone) are sub-bullets of their step. The guide names the SDK
  archive for the real version and architecture and links it to its
  release. The platform notes follow the same rule, and the report prints
  them plain. A test enforces it.
- **A structured System page.** Key/value rows in cards: *This computer*
  (platform, OS, architecture, distribution, the package manager with a
  found/not-found badge and its path, MSYS2 with a link to install it when
  missing, home, Claude Code with its version and path, git) and *The
  framework* (the version, the matching SDK archive, its download address,
  the release page, the workflow artifacts while a release is still
  building, and how this application fetches it), then the notes.
- **The Install page says where it stands.** A badge next to the buttons
  (*not checked yet*, *N missing*, *everything installed*); *Install what is
  missing* is enabled only while something is; the checks are a list with a
  mark, the version and the package to install; the package manager's
  output goes to a dark console.
- **The Markdown views' links are not underlined.** An underline runs
  through the hyphens and underscores of an archive name or an address; the
  accent colour and the hand cursor mark the links instead (the renderer
  honours `linkUnderline` now, framework changelog `markdown-link-underline`).
- **The application's docs reach the LLM corpus.** `Docs/UltraCanvasStart/`
  is indexed by `scripts/generate_llms_txt.py`, with the application's README
  mirrored into it as the module READMEs are.
- **`--page <name>` opens the window on a page** (platform, system, choices,
  install, project, ai, report) and `--for <os>` preselects that platform's
  guide in the window too, for screenshots and support.
- **A proposal for a stepper instead of tabs:**
  `Docs/UltraCanvasStart/WorkflowProposal.md` says what the tabs hide
  about the order of things and lays out the five-step flow that would
  replace Choices, Install and the SDK download on the Project page. Not
  implemented in this version.

#### 2026-10-09 *0.1.3*
- **The Linux SDK it downloads is a `.tar.xz`.** CI packs the Linux SDK
  with xz now, about 28% smaller than the gzip it was, so the Project page's
  *Download...* and the plan's SDK step name `UltraCanvas-SDK-Linux-<version>-<arch>.tar.xz`;
  macOS keeps `.tar.gz` and Windows `.zip`. The unpack step reads the archive
  by its contents, as before.

#### 2026-10-08 *0.1.2*
- **The Project page downloads the SDK.** Every release build of `main` now
  attaches the six SDK archives to the GitHub release `v<version>`, so
  *Download...* next to the SDK prefix fetches this version's archive for
  this platform and architecture from that fixed address into a folder you
  pick, unpacks it there and fills the prefix in. The plan's SDK step, the
  System page and the "not an SDK folder" message name that address; the
  Actions page stays the fallback while a release build is still running. A
  build without the network module puts the address on the clipboard
  instead.

#### 2026-10-05 *0.1.1*
- **The home folder and the MSYS2 installation are found under any user
  name.** `USERPROFILE`, `ProgramFiles`, `MSYS2_ROOT` and `WD` were read
  through the narrow `getenv`, which answers in the ANSI code page, so a
  home folder named in Thai or Cyrillic under code page 1252 was proposed
  with `?` in it and an MSYS2 under such a folder was not found. They are
  read with the framework's `GetEnvUtf8` now (framework changelog:
  `env-narrow`).

#### 2026-10-03 *0.1.0*
- **UltraCanvasStart: the setup application.** Sets a computer up for
  UltraCanvas development. It detects the platform, OS, architecture and
  package manager (apt, dnf, pacman, zypper, Homebrew, MSYS2), checks which
  tools and development libraries the chosen features need (programs by
  `--version`, libraries by `pkg-config`), installs the missing packages
  through the package manager (pkexec/sudo on Linux), names the matching
  prebuilt SDK artifact, writes a project skeleton (CMakeLists.txt, main.cpp,
  CMakePresets.json, README.md, CLAUDE.md) and explains how to work with
  Claude Code locally or through GitHub alone. The first page is the platform
  selector from `Docs/GettingStarted.md`, preselected to the detected OS, so
  another platform's instructions can be read too. Headless `--check`,
  `--plan`, `--for <os>`, `--all` modes for the terminal and CI.
