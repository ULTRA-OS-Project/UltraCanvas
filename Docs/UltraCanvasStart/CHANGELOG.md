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
