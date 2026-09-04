# Running Android APKs on Linux

**Status:** Investigation, no code changes. Nothing below has been executed:
the environment this was written in has no Android SDK, no NDK, and none of the
kernel features an Android runtime needs (§8 records what was actually probed).
Treat the commands as the plan to try first, not as a transcript.

Companion to [`AndroidPortInvestigation.md`](AndroidPortInvestigation.md),
which answers *"can UltraCanvas become an APK"*. This document answers the two
questions that come after it:

1. **Once an APK exists, can it be run and debugged on a Linux machine with no
   Android hardware in the room?** — the practical question for this project,
   because the Android backend is complete on paper and
   [has never run](../../UltraCanvas/OS/Android/README.md).
2. **Could ULTRA OS's Linux desktop run third-party Android apps?** — the
   product question, given ULTRA OS is "Linux for desktop and Android for
   mobile" ([`Docs/Modules/ULTRA-OS/README.md`](../Modules/ULTRA-OS/README.md)).

---

## 1. Executive summary

- **Yes — four viable runtimes**, and they are not interchangeable: the SDK
  **Android Emulator** (AVD), **Cuttlefish**, **Waydroid** and **redroid**.
  For validating *our* APK the answer is the emulator; for Android apps on an
  ULTRA OS desktop the answer is Waydroid. §2 separates them.
- **The decision that matters is the ABI, not the runtime.** Everything in the
  tree today targets `arm64-v8a` alone — `abiFilters 'arm64-v8a'` in
  [`packaging/build.gradle.template`](../../UltraCanvas/OS/Android/packaging/build.gradle.template)
  and the default of
  [`scripts/android-bootstrap-sysroot.sh`](../../scripts/android-bootstrap-sysroot.sh).
  Every Android runtime on an x86_64 Linux box runs x86_64 guest code; an
  arm64-only APK there needs either an unofficial binary-translation layer
  (libndk / libhoudini) or whole-CPU emulation, which is unusable for
  interactive UI. **Build the `x64-android` sysroot first.** The bootstrap
  script already accepts `x86_64` and maps it to vcpkg's `x64-android` triplet,
  so this is a second run of an existing script, not new code (§3).
- **This unblocks nothing on its own.** The cross-compiled dependency sysroot
  remains the single blocker between "compiles in CI" and "runs"
  ([port investigation §4](AndroidPortInvestigation.md)). What this document
  changes is which sysroot to build first, and what to do in the hour after it
  succeeds.
- **Fidelity is high enough to be worth it.** The emulator runs the real
  Android framework — real `ANativeWindow`, real IME, real SAF/DocumentsUI,
  real `ClipboardManager`, real activity lifecycle — so every Android-specific
  path this backend added (§4) is exercisable without a phone. The parts an
  emulator does *not* prove are GPU-driver behaviour and real touch hardware.
- **One concrete gap to close before the first run:** the backend logs through
  `std::cerr`/`std::cout`, and on Android those go nowhere. Fix that before
  spending a day guessing why a black screen is black (§5.3).
- **Neither this container nor the repository's CI can run any of it today.**
  No `/dev/kvm`, no `vmx`/`svm`, and `CONFIG_ANDROID_BINDER_IPC` is unset (§8).
  The first run happens on a workstation or on a GitHub-hosted `ubuntu-latest`
  runner, which does expose `/dev/kvm`.

---

## 2. The four runtimes

| Runtime | What it is | Needs KVM | Needs binder in host kernel | GPU path | Guest ABI on an x86_64 host | Headless / CI | Best for |
|---|---|---|---|---|---|---|---|
| **Android Emulator (AVD)** | Google's QEMU fork + official system images | Yes (else unusably slow) | No | Host GL, or SwiftShader (software) | x86_64 images; arm64 only under full emulation | Yes — `-no-window`, `adb`, designed for it | **The first run, and CI** |
| **Cuttlefish** (`cvd`) | AOSP's own virtual reference device (crosvm) | Yes | No | Host GL / software | x86_64 (`aosp_cf_x86_64_phone`) | Yes — headless by design, `adb` + optional WebRTC view | Fleets, AOSP-tracking, many devices per host |
| **Waydroid** | Full Android in an LXC container on the *host* kernel, rendered into a Wayland surface | No | **Yes** (binder; ashmem or its modern replacement) | Host Mesa directly — real driver | x86_64 image; arm64 apps need libndk/libhoudini | Poor — wants a Wayland session | **ULTRA OS desktop**, and GPU-real testing |
| **redroid** | The same container idea packaged as a Docker image | No | **Yes** | Host GL or software | x86_64 and arm64 images (matching the host) | Yes — headless container, `adb` over TCP | Self-hosted CI where KVM is unavailable but the kernel is yours |

### 2.1 Android Emulator (AVD) — the right first target

It is the only option that is simultaneously official, scriptable, headless,
version-selectable and installable from a package manager. It matters here for
three specific reasons:

- **API level shopping.** The backend's floor is API 26
  (`ClipDescription.getTimestamp` for clipboard change detection,
  `minSdk 26` in the Gradle template) and its ceiling assumptions are
  `targetSdk 34`. Only the emulator lets you install *both* ends and see where
  behaviour diverges — Android 10's clipboard-read-requires-focus rule, scoped
  storage, and SAF changes all landed between those two.
- **SwiftShader is a correctness oracle, not just a fallback.**
  `-gpu swiftshader_indirect` runs GLES in software, so
  `GLContextManagerEGL_Android.cpp`'s ES 3 FBO path (`GL_RGBA8` sized formats,
  `GL_RGBA` readback + swizzle in `ICompositeStrategy.cpp`) is tested against a
  reference implementation rather than a vendor driver. Slow, and exactly what
  you want for a first "is the swizzle right" answer. Cairo does all the real
  drawing on the CPU anyway, so software GL costs less here than it would in a
  game.
- **`adb` is the whole debugging story.** `logcat`, `screencap`, `pull`,
  tombstones, `ndk-stack`. §5 spells this out.

Cost: KVM, and one x86_64 sysroot.

### 2.2 Waydroid — the desktop answer

Waydroid runs Android's userspace as a container against the *host* kernel
(no VM), which is why it is fast, why it uses the real GPU driver, and why it
needs `binder` from the host kernel. On mainstream 6.x kernels binder is
generally built in; on custom or minimal kernels it is the usual first-install
failure. Its system image is LineageOS-derived (Android 13 base as of 2026),
and it targets a Wayland session — which makes it a poor CI citizen and a good
desktop one.

For this project it has one unique property: **it is the only runtime that
tests our GLES code against a real Mesa driver on a real GPU** without buying
hardware. It is also the answer to question 2 (§7).

### 2.3 Cuttlefish

AOSP's own virtualized reference device — the thing Google tests Android with.
Debian packages (`cuttlefish-base`, `cuttlefish-user`), images from
`aosp_cf_x86_64_phone-img-*.zip` plus a matching `cvd-host_package.tar.gz`,
headless by default, `adb`-driven, with an optional WebRTC screen.

It is better than the emulator when you need many devices per host or you are
tracking AOSP itself. It is worse as a first step: heavier setup, and the
version matrix (API 26 ↔ 34) that this backend actually needs is easier to get
from the SDK's image repository. Keep it on the list for later; do not start
here.

### 2.4 redroid

Android in a Docker container. No KVM — which makes it the option for CI hosts
without nested virtualization — but it needs the same host-kernel binder as
Waydroid (`binder_linux`/`ashmem_linux` modules, or `CONFIG_ANDROID_BINDERFS`
built in). That rules out GitHub-hosted runners, where the kernel is not yours,
and rules it *in* for a self-hosted runner you control. Multi-arch images
exist, but the guest arch must match the host.

### 2.5 Also-rans, and why they are not on the list

| Option | Verdict |
|---|---|
| **Genymotion Desktop** | Works (VirtualBox/QEMU VM, x86_64), free only for personal use, and adds a licence question for CI. No capability the emulator lacks. |
| **Android-x86 / Bliss OS / PrimeOS in QEMU or VirtualBox** | A full desktop-oriented Android distribution in a VM. Fine for eyeballing an app; no `adb`-first automation story, and images lag AOSP. |
| **Anbox** | The ancestor of Waydroid, effectively superseded by it. Do not start new work on it. |
| **ARChon / ARC Welder** (Chrome) | Dead for years. |
| **BlueStacks, Google Play Games on PC** | Windows/macOS, closed, gaming-oriented. Not applicable. |
| **Running the APK's `.so` directly on glibc** | Not possible, and worth stating plainly: the library is bionic-linked, expects a JVM, an `AAssetManager`, an `ANativeActivity` and JNI peers that only exist inside Android. There is no maintained "Wine for Android" loader — every working option above ships the whole Android userspace, which is precisely why they need either a VM or the host kernel's binder. |

---

## 3. The ABI decision — the one thing to change now

Today the packaging scaffolding builds `arm64-v8a` only, and the sysroot script
defaults to it. On an x86_64 Linux host that leaves three options:

| Approach | What it costs | Verdict |
|---|---|---|
| **Build an `x64-android` sysroot too** | One more run of `scripts/android-bootstrap-sysroot.sh x86_64` (it already maps `x86_64` → vcpkg triplet `x64-android`), one more `jniLibs` directory, `abiFilters 'arm64-v8a', 'x86_64'`. Doubles sysroot build time and APK size — the latter fixable later with per-ABI splits. | **Do this.** It is the difference between "runs natively on any Linux box and in CI" and "needs a translation layer or a phone". |
| **arm64 APK + libndk / libhoudini translation in Waydroid** | Unofficial layers extracted from other Android distributions, installed by third-party scripts, with per-CPU-vendor quirks (libndk favoured on AMD, libhoudini on Intel) and app-by-app failure modes. | Only as a fallback, and never as the thing CI depends on. A failure then means "the translation layer" or "our code" and you cannot tell which. |
| **arm64 AVD image on an x86_64 host** | Whole-CPU emulation, no KVM. | Correct in principle, unusable in practice for an interactive UI. Fine only for a headless "does it reach `ultracanvas_app_main`" check. |

Nothing in the framework is arm-specific, so the second sysroot is a build-time
cost only. Note also that the x86_64 build is a genuine second target for the
NDK toolchain and will catch anything that silently assumed the first one.

**Recommendation: make `x86_64` the sysroot you get working first**, precisely
because it is the one you can run. arm64 follows once the pipeline is proven,
and is what actually ships.

---

## 4. What a Linux-hosted Android can actually prove about this backend

Mapping the emulator/Waydroid to the features
[`OS/Android/README.md`](../../UltraCanvas/OS/Android/README.md) lists as
"designed and reviewed, not observed":

| Backend behaviour | Emulator | Waydroid | Notes |
|---|---|---|---|
| `ANativeWindow_lock` → xRGB→RGBX row copy → `unlockAndPost` | Yes | Yes | The single highest-value first test: if the byte order is wrong you see it instantly. Compare `adb exec-out screencap` against a Linux screenshot of the same widget. |
| Cairo/Pango text with bundled fonts + `/system/fonts` | Yes | Yes | Exercises `SetupBundledFontconfig`'s Android arm and the Roboto / Droid Sans Mono defaults. |
| Asset unpack to `$HOME/share` on first launch | Yes | Yes | Emulator is better: uninstall/reinstall cycles are one `adb` command, and the mtime+size stamp logic needs exactly that. |
| `QueryNativeDeviceScale` = `AConfiguration_getDensity`/160 | Yes — AVDs let you set density independently of resolution | Host-DPI dependent | Emulator wins; create two AVDs (mdpi and xxhdpi) and the HiDPI path is covered. |
| EGL / GLES 3 FBO path | Yes, via SwiftShader (software, reference-correct) or host GL | Yes, real Mesa driver | Use both: SwiftShader answers "is it correct", Waydroid answers "does a real driver agree". |
| Soft keyboard + `InputConnection` IME (autocorrect, candidates, gesture typing) | Yes — but **disable the hardware keyboard on the AVD**, or host keystrokes bypass the very path you are testing | Yes | The IME work is the largest untested surface in the backend; this is the reason to bother with a runtime at all. |
| SAF open (`ACTION_OPEN_DOCUMENT`) + copy-to-cache | Yes — DocumentsUI ships in the images | Yes | Also the only way to observe the documented "callers read a snapshot" consequence. |
| `SaveContent` (`ACTION_CREATE_DOCUMENT`) | Yes | Yes | |
| `AlertDialog` message dialogs through the nested-loop bridge | Yes | Yes | The sync-over-async pump is the riskiest concurrency code in the backend; a deadlock shows up here or nowhere. |
| Clipboard via `ClipboardManager` | Yes | Yes, plus host clipboard integration | Test on API 26 *and* 29+ — the focus restriction on reads only exists on the latter. |
| Lifecycle: background/foreground, `TERM_WINDOW`/`INIT_WINDOW` | Yes — `adb shell input keyevent HOME`, then relaunch | Yes | |
| Rotation with `configChanges` set, and (deliberately) without | Yes — emulator rotation is one keystroke or `adb emu rotate` | Awkward | Emulator only, effectively. |
| Back button → `WindowCloseRequest` | Yes — `adb shell input keyevent BACK` | Yes | |
| Multi-touch, pinch/rotate gesture recognition | Partly — the emulator synthesizes a two-finger pinch from a modifier + drag | Only with a real touchscreen | The weakest spot: neither substitutes for a phone for gesture tuning. `adb shell input` and `sendevent` can script raw multi-pointer streams if it matters. |
| UltraNet over the sandbox (`INTERNET` permission, c-ares DNS, TLS trust store) | Yes — NAT'd through the host | Yes | Verifies the manifest permission and that c-ares actually resolves without libresolv. |
| Native crash triage | Yes — tombstones + `ndk-stack` | Yes | |

**What no Linux-hosted Android proves:** vendor GPU driver quirks, real touch
digitizer behaviour and palm rejection, thermal/battery behaviour, and OEM
skins' IME implementations. Those need a device. Everything above does not.

---

## 5. First run on a workstation

Preconditions: an `x86_64` sysroot, an APK that Gradle actually produced, and a
signature — even a debug install needs one (`apksigner`, per the packaging
README).

### 5.1 Bring up the emulator

```sh
# SDK command-line tools; API 30 x86_64 is a good middle target between the
# backend's floor (26) and the Gradle template's targetSdk (34).
sdkmanager "platform-tools" "emulator" "system-images;android-30;default;x86_64"
avdmanager create avd -n uc-x86_64 -k "system-images;android-30;default;x86_64"

# Headless. Drop -no-window when you want to watch it.
emulator -avd uc-x86_64 -no-window -no-audio -no-boot-anim \
         -gpu swiftshader_indirect -no-snapshot &
adb wait-for-device
adb shell 'while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 1; done'
```

Create a second AVD with the hardware keyboard **off** for IME testing, and a
third at a different density for the HiDPI path (§4).

### 5.2 Install, launch, look

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.ultraos.ultracanvas.demo/org.ultraos.ultracanvas.UltraCanvasActivity

adb exec-out screencap -p > first-run.png     # the milestone evidence
adb logcat -v time                            # see §5.3 first
adb shell input keyevent BACK                 # WindowCloseRequest path
adb emu rotate                                # configChanges path
```

For a native crash, `adb bugreport` or `adb pull /data/tombstones/` and then:

```sh
adb logcat | $ANDROID_NDK_HOME/ndk-stack -sym path/to/obj/local/x86_64
```

### 5.3 Make the first run debuggable — do this *before* the first run

The backend currently emits diagnostics through `std::cerr`/`std::cout` (the
only such calls under `OS/Android/` are the nine in
`GLContextManagerEGL_Android.cpp`, and the framework's shared logging uses the
same streams). **On Android, a native app's stdout and stderr go to
`/dev/null`.** A first run therefore produces a black screen and no
explanation.

Two ways out, in order of preference:

1. **Add a logcat sink** — route the framework's logging through
   `__android_log_print` under `#ifdef __ANDROID__` (tag e.g. `UltraCanvas`,
   which then makes `adb logcat -s UltraCanvas:V` the one command a newcomer
   needs). This is a small, self-contained change and the right permanent
   answer. It is *not* made in this document because nothing here can compile
   it — the environment has no NDK, and the repository's rule is that Android
   code lands compiled ([port investigation §6, lesson 3](AndroidPortInvestigation.md)).
2. **Redirect stdio at runtime**, as a stopgap on an emulator only:
   `adb root && adb shell setprop log.redirect-stdio true`, then restart the
   app. Requires a non-Play (`default`/`aosp`) image and only affects processes
   started afterwards.

This is the highest-value follow-up in this document: it costs an hour and it
determines whether the first device run takes a day or a week.

---

## 6. CI

**Do not add an emulator job yet.** The `android-check` job in
`.github/workflows/build.yml` is deliberately a *compile* gate, and adding a
runtime job that can only ever be skipped would undo the discipline it encodes.
The moment an APK is reproducible, the job below becomes worth its minutes.

GitHub-hosted `ubuntu-latest` x86_64 runners do expose `/dev/kvm`; they need
the group-permission fix, which is the standard first step of every emulator
action:

```yaml
  android-smoke:
    name: Android smoke test          # ONLY once an APK can be built
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v5
      - name: Enable KVM
        run: |
          echo 'KERNEL=="kvm", GROUP="kvm", MODE="0666", OPTIONS+="static_node=kvm"' \
            | sudo tee /etc/udev/rules.d/99-kvm4all.rules
          sudo udevadm control --reload-rules
          sudo udevadm trigger --name-match=kvm
      # ... restore the cached x64-android sysroot, build the APK ...
      - uses: reactivecircus/android-emulator-runner@v2
        with:
          api-level: 30
          arch: x86_64
          target: default
          emulator-options: -no-window -gpu swiftshader_indirect -noaudio -no-boot-anim
          script: |
            adb install -r app-debug.apk
            adb shell am start -n org.ultraos.ultracanvas.demo/org.ultraos.ultracanvas.UltraCanvasActivity
            adb shell sleep 10
            adb exec-out screencap -p > screenshot.png
            adb logcat -d > logcat.txt
      - uses: actions/upload-artifact@v6
        with: { name: android-smoke, path: "screenshot.png\nlogcat.txt" }
```

Notes that decide whether this is useful or noise:

- **The assertion must be real.** "The activity started" is nearly worthless —
  a crashed native library still leaves an activity. Assert on a line the app
  logs after its first successful composite (which needs §5.3 done first), and
  keep the screenshot as an artifact for the human.
- **Two ABIs, one job.** Once `arm64-v8a` and `x86_64` both build, CI runs the
  x86_64 one; arm64 is covered by the compile gate plus a manual device run.
- **The sysroot must be cached**, not rebuilt: a from-scratch vcpkg build of
  cairo/pango/glib is far longer than the rest of the workflow. Key the cache
  on `packaging/vcpkg.json` + the NDK version.
- **No KVM on GitHub's arm64 runners**, so do not plan an arm64 emulator row.
- Self-hosted alternative if KVM is ever unavailable: redroid, which needs no
  KVM but does need binder in *your* kernel (§2.4).

---

## 7. The ULTRA OS question: Android apps on a Linux desktop

Distinct from everything above, and the answer is **Waydroid**, with three
commitments attached:

1. **Kernel.** The desktop kernel must provide binder (and the memory-sharing
   primitive the image expects). On mainstream 6.x kernels this is usually
   built in; if ULTRA OS ships its own kernel config, this becomes a supported
   feature to guarantee, not an accident to inherit.
2. **Session.** Waydroid renders into Wayland. An X11-only session is a
   second-class citizen.
3. **Apps and ABI.** The x86_64 image runs x86_64 Android apps natively; a
   large part of the real-world Android catalogue is arm64-only and needs the
   unofficial translation layers of §3 — which is a support burden, not a
   feature. And Google Play/GMS cannot simply be shipped: distributing it
   requires certification, which is why Waydroid ships a vanilla image and
   users add GApps or microG themselves.

**The counterpoint worth stating for ULTRA OS's own apps:** an APK is the wrong
delivery vehicle for them on the desktop. UltraCanvas already has a native
Linux backend — the same source, the same widgets, one build. Shipping an ULTRA
OS app to an ULTRA OS desktop through an Android container would add a
container, a translation layer and Android's own UI conventions to something
that already runs natively and faster. Android-app compatibility on the desktop
is worth having for *other people's* apps; it is not a distribution strategy
for ours.

---

## 8. What was actually probed here, and the dead ends

This container cannot run any Android runtime, verified rather than assumed:

| Probe | Result | Consequence |
|---|---|---|
| `/dev/kvm` | absent | No emulator with acceleration, no Cuttlefish |
| `vmx`/`svm` in `/proc/cpuinfo` | absent | No nested virtualization to fall back on |
| `CONFIG_ANDROID_BINDER_IPC` in `/proc/config.gz` | `is not set` | No Waydroid, no redroid |
| `/dev/binder*`, `/dev/binderfs` | absent | as above |
| Android SDK / NDK / `adb` / `emulator` | none installed | Nothing Android-related can even be compiled here |

So the first run happens on a workstation or a GitHub-hosted runner. Nothing
about that is unusual — it is the same reason the `android-check` CI job is a
compile gate rather than a test.

---

## 9. Recommended order of work

1. **Add the logcat sink** (§5.3). One hour, and every step after it is
   cheaper. Land it compiled, through the existing `android-syntax-check.sh`
   gate.
2. **Build the `x64-android` sysroot first**:
   `scripts/android-bootstrap-sysroot.sh x86_64` (then `--with-net`). Expect
   the iteration the script's header predicts — pango, cairo, glib.
3. **`abiFilters 'x86_64'` for the first APK**, adding `'arm64-v8a'` once the
   pipeline works end to end.
4. **First run on an x86_64 AVD at API 30**, headless, `screencap` as the
   evidence. Then API 26 and 34 for the clipboard/SAF divergences (§4).
5. **Then Waydroid**, for the real-Mesa GLES pass and as the ULTRA OS
   experiment.
6. **Then a physical arm64 device**, for gestures, drivers and the things no
   host-side Android can answer.
7. **Then the CI smoke job** (§6), once step 4 is reproducible by a script
   rather than by a person.

---

## Sources

- [Waydroid — ArchWiki](https://wiki.archlinux.org/title/Waydroid)
- [The mess of kernel modules with Waydroid — waydroid/waydroid#885](https://github.com/waydroid/waydroid/issues/885)
- [ARM translation (libndk and libhoudini) — casualsnek/waydroid_script](https://deepwiki.com/casualsnek/waydroid_script/5.4-arm-translation-(libndk-and-libhoudini))
- [Waydroid: running Android natively on Linux](https://pbxscience.com/waydroid-running-android-natively-on-linux-what-it-is-how-it-works-and-what-you-should-know/)
- [redroid-doc — system requirements](https://deepwiki.com/remote-android/redroid-doc/2.1-system-requirements)
- [remote-android/redroid-modules](https://github.com/remote-android/redroid-modules)
- [Cuttlefish — get started (AOSP)](https://source.android.com/docs/devices/cuttlefish/get-started)
- [The Android Cuttlefish emulator — 2net.co.uk](https://2net.co.uk/blog/cuttlefish-android12.html)
- [Configure hardware acceleration for the Android Emulator](https://developer.android.com/studio/run/emulator-acceleration)
- [ReactiveCircus/android-emulator-runner](https://github.com/marketplace/actions/android-emulator-runner)
