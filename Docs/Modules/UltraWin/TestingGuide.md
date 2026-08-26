# UltraWin testing guide — Tier 1 (Wine) and Tier 2 (VM), step by step

Hands-on validation of the Windows-application support in ULTRA OS, as a
user would exercise it. Tier 1 needs only a Linux desktop with Wine; Tier 2
additionally needs KVM and Windows install media. Every step says what
success looks like, so a failing step plus its output is a complete bug
report.

The driver for most steps is the **`ultrawin-setup`** CLI (built with the
framework), plus **UltraFiler** for the desktop-integration checks.

---

## Part 0 — Build (once)

```bash
sudo apt install build-essential cmake pkg-config \
    libcairo2-dev libpango1.0-dev libharfbuzz-dev libvips-dev \
    libglib2.0-dev libfreetype6-dev libtinyxml2-dev libfmt-dev \
    libx11-dev libxcursor-dev libgl1-mesa-dev libgtk-3-dev \
    libcdr-dev librevenge-dev zlib1g-dev libzbar-dev \
    freerdp2-dev    # Ubuntu 24.04+: freerdp3-dev

git clone https://github.com/ULTRA-OS-Project/UltraCanvas.git
cd UltraCanvas
cmake -B build -DULTRACANVAS_BUILD_ULTRAWIN_TESTS=ON
cmake --build build -j$(nproc) --target ultrawin-setup UltraFiler UltraWinTests
```

**Check:** the configure output shows
`[✓] UltraWin - ENABLED` and `[✓] UltraWin RemoteApp client - ENABLED`.

```bash
./build/UltraWinTests
```

**Check:** all tests pass. Without Wine/QEMU installed some self-skip —
after Part 1 below, re-run and expect **0 skipped** (the RDP handshake
test may still skip on hosts without IPv6).

---

## Part 1 — Tier 1: Wine

### 1.1 Install the engines

```bash
sudo apt install wine winetricks
./build/ultrawin-setup status
```

**Check:** `wine: yes`, `winetricks: yes`.

### 1.2 First launch from the command line

Use any Windows program you have (a portable `.exe` is easiest —
e.g. 7-Zip's installer, Notepad++ installer, PhotoFiltre):

```bash
./build/ultrawin-setup run-wine ~/Downloads/npp.8.6.Installer.x64.exe
```

**Check:** the installer opens **as a normal window** (no Windows desktop
anywhere). The first launch prints nothing for ~30–60 s while the
program's isolated environment is created — that is once per program.
When you close the program, the CLI prints its exit code.

### 1.3 The user's folders — the U: drive

Inside any running Windows program: File → Open (or Save).

**Check:** drive **U:** exists and shows your home directory — your real
files, live. Drive **Z:** (the whole host filesystem) must NOT exist.
Save a file to `U:\` and confirm it appears in `$HOME` on Linux.

### 1.4 Desktop integration — UltraFiler

```bash
./build/UltraFiler
```

- Double-click a `.exe` → status bar shows "Launching …", the program
  opens as a native window.
- Double-click a `.msi` → status bar shows "Installing …" and the
  installer runs (through msiexec).
- With Wine **uninstalled**, double-clicking shows the install hint in
  the status bar instead of failing silently.

### 1.5 Installed programs reach the launcher

After running an installer (1.2 or 1.4):

```bash
./build/ultrawin-setup programs
```

**Check:** the program's Start-Menu entries are listed with their
environment and shortcut paths. Launch one:

```bash
./build/ultrawin-setup run-wine "<the listed .lnk path>"
```

### 1.6 Components fix missing runtimes

If a program complains about VC++/fonts/.NET:

```bash
./build/ultrawin-setup component <EnvironmentName> vcrun2019
./build/ultrawin-setup component <EnvironmentName> corefonts
```

(Environment names = the directories under
`~/.local/share/ultrawin/environments/`, one per program.)

**Check:** the command reports the installed components; the program's
complaint is gone on the next launch. Failures name a log file
(`ultrawin-install.log` in the environment) — send that file.

---

## Part 2 — Tier 2: the Windows VM

Follow [`VmValidation.md`](VmValidation.md) for the full detail; the
short form:

### 2.1 Prerequisites

```bash
sudo apt install qemu-system-x86 qemu-utils virtiofsd
./build/ultrawin-setup status
```

**Check:** `qemu: yes`, `kvm: yes`, `virtiofsd: yes`, `vm tier: yes`.
(`kvm: no` → enable virtualization in BIOS/UEFI.)

You also need, downloaded once:
- a **Windows 10/11 Pro** ISO (your license),
- the **virtio-win** drivers ISO:
  <https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/latest-virtio-win/virtio-win.iso>

### 2.2 Provision and install — fully unattended

```bash
./build/ultrawin-setup provision \
    --windows-iso ~/isos/Win11_Pro.iso \
    --drivers-iso ~/isos/virtio-win.iso
./build/ultrawin-setup start
./build/ultrawin-setup watch
```

**Check:** `watch` polls every 10 s and finishes with `installed=yes`
(expect 20–45 min for the unattended install). No interaction should ever
be needed. If `watch` reports `stopped` or never finishes, collect
`~/.local/share/ultrawin/vm/ultrawin-qemu.log` and the `watch` output —
that plus the screen described in VmValidation.md's failure table is the
bug report.

### 2.3 Launch Windows apps into the guest

```bash
./build/ultrawin-setup run "||notepad"
./build/ultrawin-setup run "C:\\Windows\\System32\\notepad.exe"
./build/ultrawin-setup run ~/Documents/SomeWindowsTool.exe   # via shared home
```

**Check:** the session connects and the CLI waits until the program ends.
(Until the `UltraCanvasRemoteAppView` element lands, the window surface
itself is not yet composed onto the desktop — a connected, program-bound
session that ends when the app closes is the pass criterion; full window
display is the next milestone.)

### 2.4 The shared home inside the guest

In the guest (e.g. via `run "C:\\Windows\\explorer.exe"` once 2b-ii
lands, or any file dialog): **U:** shows the same home directory as
Tier 1 and Linux.

### 2.5 Machine lifecycle

```bash
./build/ultrawin-setup suspend   # freeze the guest (RAM stays)
./build/ultrawin-setup resume
./build/ultrawin-setup stop      # graceful shutdown; 'start' boots from disk
```

**Check:** `status` reflects each transition; after `stop`+`start` the
machine boots straight from disk (no install media) because
`installed: yes`.

---

## Reporting results

For every failing step, include:
1. The exact command and its full output.
2. `./build/ultrawin-setup status` output.
3. Tier 1: the environment's `ultrawin-install.log` (component failures).
   Tier 2: `ultrawin-qemu.log`, `ultrawin-virtiofsd.log` and
   `ultrawin-vm.conf` from `~/.local/share/ultrawin/vm/`.
4. Host details: distro/version, Wine version (`wine --version`), QEMU
   version (`qemu-system-x86_64 --version`).
