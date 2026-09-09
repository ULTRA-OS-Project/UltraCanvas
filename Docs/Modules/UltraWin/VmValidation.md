# UltraWin VM tier — real-hardware validation guide (Stage 2c)

The VM tier's machine, session, and shared-folder layers are covered by
automated tests, but the unattended **Windows installation** itself can only
be validated on a machine with KVM and real install media. This is the
step-by-step run, driven by the `ultrawin-setup` CLI (built with the
framework whenever UltraWin is: target `ultrawin-setup`).

## What you need

- A Linux machine with `/dev/kvm` (BIOS virtualization enabled) and ~6 GB
  free RAM, plus QEMU (`sudo apt install qemu-system-x86 qemu-utils
  virtiofsd`).
- A **Windows 10/11 Pro or Enterprise ISO** — the RemoteApp host role needs
  Pro or better, and the Windows license is yours. (Microsoft's media
  creation / evaluation downloads work.)
- The **virtio-win drivers ISO**:
  <https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/latest-virtio-win/virtio-win.iso>
  — carries the storage/network drivers Windows setup needs on a virtio
  machine, plus the guest tools (WinFsp + the virtiofs service).

## The run

```bash
ultrawin-setup status          # expect: qemu yes, kvm yes, vm tier yes
ultrawin-setup provision \
    --windows-iso  ~/isos/Win11_Pro.iso \
    --drivers-iso  ~/isos/virtio-win.iso
ultrawin-setup start           # boots the installer, fully unattended
ultrawin-setup watch           # polls until Windows answers on RDP
```

`watch` reports `installed=yes` once the guest's RDP port answers — that
flips the machine manifest, so later `start`s boot straight from disk
without install media. Then:

```bash
ultrawin-setup run "||notepad"                      # RemoteApp alias
ultrawin-setup run "C:\\Windows\\System32\\notepad.exe"
ultrawin-setup run ~/Documents/SomeTool.exe         # via the shared home
```

## What to report when it fails

Everything lands in the machine directory (default
`~/.local/share/ultrawin/vm`):

| File | Contents |
|---|---|
| `ultrawin-qemu.log` | QEMU's own output (device/boot problems) |
| `ultrawin-virtiofsd.log` | home-share daemon |
| `ultrawin-vm.conf` | the machine manifest |
| `unattend/autounattend.xml` | the generated answer file |

Plus the `ultrawin-setup` output itself. The most likely first-run
failures and their meaning:

- **Setup asks for a disk driver / no disk found** — the WinPE driver
  injection didn't reach the virtio-win CD (report which drive letter the
  CD got in the guest).
- **Setup stops at an interactive screen** — an answer-file gap; note
  which screen (edition selection, EULA, account, …).
- **`watch` never reaches installed** — RDP didn't come up: either
  firstlogon commands failed (guest tools install, `VirtioFsSvc`) or the
  RDP enablement didn't apply.
- **`run` connects but no window appears** — RemoteApp allow-list or RAIL
  negotiation; the `UltraCanvasRemoteAppView` element work (Stage 2b-ii)
  also lands here.

The answer file (`GenerateAutounattendXml`) and first-boot orchestration
are deliberately marked experimental until this run has passed; iteration
happens against these logs.
