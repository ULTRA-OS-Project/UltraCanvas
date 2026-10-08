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
