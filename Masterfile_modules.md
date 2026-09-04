# Masterfile_modules

Authoritative module registry for the ULTRA OS / UltraCanvas framework.
This file enumerates the top-level modules and the rules each module must
follow. Each section pairs a one-paragraph purpose with the public function
surface that downstream applications and plugins can depend on.

---

### **1. UltraCanvas**

UI widget framework — windows, controls, layout, rendering. Sources under
`UltraCanvas/{include,core,libspecific,OS/<Platform>,Plugins}`.

**DataFormats section** — framework-wide structured-data facilities under
`UltraCanvas/{include,core}/DataFormats/`. These are core services (usable by
core, plugins and applications alike; never implemented inside a file-type
plugin). Public engines are always wrapped behind an UltraCanvas-owned API so
the backing implementation can be replaced without affecting callers.

- **UltraCanvasJSON** (`DataFormats/UltraCanvasJSON.h`) — general-purpose JSON
  parsing and serialization, backed by the vendored yyjson engine
  (`UltraCanvas/third_party/yyjson`, MIT). Public surface:
  - `JSONValue` — value-semantic DOM (Null / Boolean / Number / String /
    Array / Object; objects preserve insertion order); accessors
    `GetBoolean/GetInteger/GetNumber/GetString` (fallback-based, never throw),
    structure access `At/Find/Get/Contains/GetSize/operator[]`, mutation
    `Append/Set/Remove`, builders `MakeArray/MakeObject`.
  - `JSON::Parse`, `JSON::ParseFile` — strict RFC 8259 by default; opt-in
    comments / trailing commas / Inf-NaN via `JSONParseOptions`; errors are
    reported through `JSONParseResult` (message, byte position, line, column);
    nesting depth is limited to defend against hostile input.
  - `JSON::Serialize`, `JSON::SerializeToFile` — compact or pretty output via
    `JSONSerializeOptions`; output is always strictly valid JSON.
  - `JSON::EscapeString` and framework-type helpers
    `FromColor/ToColor`, `FromPoint/ToPoint`, `FromRect/ToRect`.

- **UltraCanvasFileAssociations** (`UltraCanvasFileAssociations.h`) — the
  cross-platform "Open with" service: which applications the OS registers
  for a file, and detached launching. Core worker/cache in
  `core/UltraCanvasFileAssociations.cpp`; per-platform backends behind the
  internal `UltraCanvasFileAssociationsBackend.h` under `OS/<Platform>/`
  (Linux/BSD: freedesktop; Windows: SHAssocEnumHandlers / IAssocHandler;
  macOS: NSWorkspace / Launch Services — all three enumerate, launch and
  extract application icons).
  Public surface (`namespace FileAssociations` + `FileAssociationApp`):
  - `GetApplicationsForFiles` — candidates for a selection (intersection),
    default application first, cache-served once prewarmed.
  - `OpenWithDefaultApplication` / `OpenWithApplication` /
    `OpenWithApplicationPath` — detached launches (default handler /
    enumerated app / user-picked executable).
  - `GetApplicationFilter` / `GetApplicationsDirectory` — file-dialog setup
    for an "Other application…" picker (the picker UI lives with the caller).
  - `PrewarmAsync` / `PrewarmExtensionsAsync` — background-worker warm-up;
    the worker only exists once a caller asks for it.
  See `Docs/UltraCanvas/UltraCanvasFileAssociations.md`.

- **UltraCanvasFontFile** (`UltraCanvasFontFile.h`) — reads a font definition
  file (ttf / ttc / otf / otc / woff / woff2 / Type 1 / bdf / pcf / fon) as a
  document rather than as something to render text with: its name records are
  metadata, and a line of its own glyphs is a thumbnail. Implemented in
  `core/UltraCanvasFontFile.cpp` on FreeType alone — no fontconfig, no Pango,
  no render context and no installed font, with one `FT_Library` per call so
  the whole surface is safe on background threads (which is what lets the
  filer thumbnail a folder of fonts). Public surface:
  - `IsFontFileExtension` / `FontFormatForExtension` / `FontFormatName` —
    recognition by extension, before a file is opened.
  - `ReadFontFileInfo` — `FontFileInfo` (container format, file size, face
    count) with one `FontFaceInfo` per face: the decoded name records
    (family, subfamily, full/PostScript name, version, copyright, trademark,
    manufacturer, designer, license, license URL, sample text) plus glyph
    count, units per em, scalable / fixed-width / kerning / bold / italic and
    the strike sizes of a bitmap face. Never throws on a malformed file.
  - `RenderFontSpecimenPixmap` (+ `FontSpecimenOptions`) — a ready-to-draw
    `UCPixmap` card carrying a line of the font's own glyphs, fitted to the
    box; a symbol face with no Latin glyphs falls back to its own first
    glyphs. No shaping: glyph lookup plus kerning, which is what is possible
    without a registered font and a Pango context.

  Registration for actual text rendering is the application's, not this
  module's: `UltraCanvasApplicationBase::RegisterFontFile` /
  `IsFontFileRegistered` / `GetRegisteredFontFiles` add a file's faces to the
  process by name (FontConfig on Linux/Android/WASM, GDI `FR_PRIVATE` +
  FontConfig on Windows, CoreText process scope on macOS), followed by
  `RefreshFontConfiguration()` so the new family resolves in the next layout.
  Process-private and permanent: there is no unregister.
  See `Docs/UltraCanvas/UltraCanvasFontFile.md`.

- **UltraCanvasVolumeMonitor** (`UltraCanvasVolumeMonitor.h`) — the mounted
  volumes of the machine, and a notification when that set changes: a USB
  stick, card, optical disc, network share or disk image connected or removed.
  Core enumeration + polling fallback in `core/UltraCanvasVolumeMonitor.cpp`;
  per-platform backends under `OS/<Platform>/` (Linux/BSD: `poll()` on
  `/proc/self/mountinfo`; Windows: `WM_DEVICECHANGE` on a hidden top-level
  window; macOS: `NSWorkspace` mount notifications). Public surface:
  - `ListMountedVolumes` — every mounted volume as `MountedVolume`
    (`path`, `label`, `isSystemRoot`), system root first. **The framework's
    single volume enumeration**: a drive list anywhere else — a folder tree,
    a path strip's *Computer* dropdown, a places list — calls this rather than
    scanning directories of its own, so two lists cannot disagree about what
    is mounted. Never reads a volume label off the medium (that stalls on an
    empty optical drive and on every disconnected network mapping).
  - `ListVolumeRoots` — the same list, mount points only.
  - `ListPlatformMountPoints` — the platform's own mount table, used by the
    enumeration to decide whether a directory is really a mount point; empty
    where no such table is readable.
  - `UltraCanvasVolumeMonitor` — `Start(onChanged)` / `Stop()` (joins, so no
    callback survives it), `IsRunning`, `IsNative`, `SetPollIntervalMs` /
    `GetPollIntervalMs`, static `NativeBackendAvailable`. The callback runs on
    the monitor's thread and must only hand the news over; one insertion
    produces several callbacks, so the receiver coalesces.
  Wired up per platform by the `ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR`
  condition in the **top-level** `CMakeLists.txt`; without it the core file
  compiles null-returning fallbacks and the monitor polls.
  See `Docs/UltraCanvas/UltraCanvasVolumeMonitor.md`.

- **UltraCanvasSmoothScroll** (`UltraCanvasSmoothScroll.h`) — framework-wide
  smooth scrolling and wheel zoom. Scrolling glides to its target instead of
  jumping, in every UltraCanvas application, with no opt-in: a wheel notch, a
  page step, a keyboard reveal or a zoom step is eased over ~150 ms with an
  ease-out cubic. Public surface:
  - Application-wide defaults, read at the moment a scroll starts:
    `SetSmoothScrollingEnabled` / `IsSmoothScrollingEnabled`,
    `SetSmoothScrollDuration` / `GetSmoothScrollDuration`. `ScrollbarStyle`
    initialises from these, so one switch governs the scrollbar-backed elements
    and the self-rendered views alike.
  - `UltraCanvasSmoothScroll` — one animator per scalar (a scroll offset, a zoom
    level). `Bind(read, write)`, `AnimateBy` / `AnimateTo` (chaining, so a fast
    wheel spin is one glide), `PendingValue`, `Jump`, `Cancel`, `SetDuration`.
    Holds a ~60 Hz timer only while a glide runs.
  - `UltraCanvasSmoothZoom` — eases a multiplicative zoom in log space and hands
    the element a run of small incremental factors, which it applies with its own
    zoom-about-cursor code; no transform maths is duplicated. `Bind(apply,
    repaint)`, `ZoomBy(factor, currentZoom, minZoom, maxZoom)`, `Cancel`.
  Anything that positions the view rather than scrolls it — a thumb drag, a pan,
  keeping the text caret on screen, opening a folder, fitting a diagram — cancels
  the glide and lands at once. Row-indexed views (spreadsheet, hex dump, the two
  dialog file lists) are not converted: their scroll position is a row index, so
  they need a pixel offset in the paint path first.
  See `Docs/UltraCanvas/UltraCanvasSmoothScroll.md`.

- **UltraCanvasHardwareInfo** (`UltraCanvasHardwareInfo.h`) — read-only
  inventory and sensors for the machine the application runs on: CPU (cache
  sizes, hybrid core tiers, instruction sets, temperature, load), GPU, NPU,
  memory down to the individual module, storage (bus, connector, on-drive
  cache, temperature, volumes), network interfaces including Wi-Fi
  association, USB controllers and attached devices, and Bluetooth adapters
  with their connections. Distinct from **IODeviceManager**, which *operates*
  peripherals (scanners, cameras, printers: handles, protocols, a device
  lifecycle) — this module only *describes* the host and holds nothing. Shared
  logic in `core/UltraCanvasHardwareInfo.cpp`; per-platform probes behind the
  internal `UltraCanvasHardwareInfoBackend.h` under `OS/<Platform>/` (Linux:
  procfs/sysfs; Windows: registry, SMBIOS, storage IOCTLs, IP Helper, WLAN,
  SetupAPI and the Bluetooth API — no COM or WMI; macOS: sysctl and the IOKit
  C API), with a fallback for platforms that have none. No new third-party
  dependency on any platform. Public surface:
  - `Capture(HardwareQuery, forceRefresh)` — one consistent snapshot;
    `HardwareQuery` is a bit set (`System`/`CPU`/`GPU`/`NPU`/`Memory`/
    `Storage`/`Network`/`USB`/`Bluetooth`/`Sensors`/`All`) because probing
    costs differ by orders of magnitude, and `HardwareSnapshot::Has` reports
    which categories were actually filled.
  - `RefreshSensors(snapshot)` — re-reads temperatures, clocks, utilisation,
    free memory and link state in place, never adding or removing a device, so
    a monitor loop keeps its indices.
  - Single-category helpers `GetCPU` / `GetMemory` / `GetSystem` / `ListGPUs` /
    `ListNPUs` / `ListStorageDevices` / `ListNetworkInterfaces` /
    `ListUSBDevices` / `ListUSBControllers` / `ListBluetoothAdapters`.
  - `SetOptions` / `GetOptions` (`HardwareInfoOptions`): identifier masking
    (on by default — serial numbers, MACs and BSSIDs keep only their tail),
    sensor inclusion, USB hubs, snapshot cache lifetime; `MaskIdentifier`
    applies the same rule to a caller's own string.
  - `BuildReport` → `HardwarePropertyGroup` tree, and the `ToText` / `ToJSON`
    renderings built on it; `FormatBytes` / `FormatFrequencyMHz` /
    `FormatTemperature` / `FormatBitrateMbps` / `FormatDuration`.
  - `GetBackendName` (`"sysfs"` / `"win32"` / `"iokit"` / `"null"`),
    `IsAvailable`. A value that cannot be read never becomes a zero: the
    reason goes into `HardwareSnapshot::warnings` in words a user can act on.
  - **UltraCanvasHardwareInfoPanel** (`UltraCanvasHardwareInfoPanel.h`) — the
    ready-made system-information view, an `UltraCanvasColumnsTreeView` that
    fills itself from a snapshot: `Refresh`, `RefreshSensors` (updates values
    in place, so expansion, selection and scroll position survive),
    `SetQuery`, `SetSnapshot`, `SetSectionsExpanded`, `ToText` / `ToJSON`,
    `onSnapshotChanged`, and the `CreateHardwareInfoPanel` factory.
  See `Docs/UltraCanvas/UltraCanvasHardwareInfo.md`.

- **UltraCanvasSpellChecker** (`UltraCanvasSpellChecker.h`) — cross-platform
  spell checking. A singleton service owning one backend, the user dictionary,
  a session ignore list and a worker thread, so checking never runs on the
  render thread. Backends are wrapped behind the UltraCanvas-owned
  `ISpellCheckBackend` (`ISpellCheckBackend.h`), never exposed: enchant-2 on
  Linux, ISpellChecker on Windows 8+, NSSpellChecker on macOS, each in
  `OS/<Platform>/UltraCanvasSpellCheckSupport.*`, with Hunspell
  (`core/SpellCheckBackendHunspell.cpp`) as the portable fallback and the only
  backend on Android and WASM. Every dependency is optional — the service falls
  back native → Hunspell → a no-op reporting zero dictionaries, so a missing
  one never fails a build. Public surface:
  - Lifecycle and backend: `Initialize` / `Shutdown` / `IsInitialized`,
    `SetBackend`, `GetBackendName`.
  - Language: `GetAvailableLanguages` / `SetLanguage` / `GetLanguage` /
    `GetLanguageInfo` / `DetectPreferredLanguage` (from `LC_ALL` / `LANG`).
  - Checking: `IsCorrect`, `GetSuggestions`, `CheckText` (synchronous), and the
    asynchronous `QueueCheckText` / `TryTakeResult` / `CancelContext` /
    `SetContextNotifier` pair-with-drain used by text elements.
  - Dictionary: `AddToUserDictionary` / `RemoveFromUserDictionary` /
    `IgnoreWord` / `ClearIgnoredWords` / `RequestRecheck`, with
    `SetUserDictionaryPath` / `Load` / `Save`.
  - Menus: `BuildSpellCheckMenu` (a lambda-provided submenu that shows live
    state), `BuildSpellCheckMenuItems`, `BuildLanguageMenuItems` (radio group),
    `BuildSuggestionMenuItems` (right-click list).
  - `namespace SpellCheckText` — UTF-8 tokenizer and byte/codepoint mapping;
    `namespace SpellCheckRendering` — squiggle drawing over `IRenderContext`,
    usable by any component that can produce a word rectangle.
  Wired into `UltraCanvasTextArea` via `SetSpellCheckEnabled`; the byte-range →
  screen-rectangle mapping it needs is the element's own
  `GetCharacterRangeBounds`, which is equally usable for search highlighting,
  diff marks and comment anchors. Two element hooks exist for host
  applications: `onContextMenu` takes the right-click before the built-in
  suggestion popup, so an application with its own editor menu splices the
  suggestions into it; `onPrepareSpellCheck` hands over the exact text about to
  be checked plus a per-check copy of the options, which is what
  `SpellCheckOptions::shouldSkipRange` needs — its ranges are byte offsets and
  go stale on the first edit otherwise.
  UltraTexter is the reference consumer: **Edit → Spelling**, an editor context
  menu carrying the suggestions, and a markdown skip scanner
  (`Apps/Texter/UltraCanvasMarkdownSpellRanges.h` — a dependency-free byte
  scanner covering fenced and indented code, inline code spans, link and image
  targets, autolinks, inline HTML, math and YAML front matter, with the
  application half in `Apps/Texter/UltraCanvasTextEditorSpellCheck.cpp`).
  See `Docs/UltraCanvas/UltraCanvasSpellChecker.md`.

### **2. UltraAI**

Provider-agnostic AI capabilities (LLM, embeddings, STT, TTS, vision,
image / video / music generation, translation, code assist). See
`Docs/Modules/UltraAI/README.md`.

### **3. FileLoader**

Universal file loading / saving / converting facade. See
`Docs/Modules/FileLoader/README.md`. Generic byte-level entry point
`UltraCanvasFileLoader::LoadFile(pathOrUrl, autoDecompress = true)` accepts
both filesystem paths and `http(s)://` URLs (URLs require UltraNet to be
built in). Compressed content (gzip/zlib/Zstandard/LZ4, detected by magic
bytes) is transparently decompressed via the VirtualFS compression API, so
applications never deal with compression formats themselves;
`FileBytesResult::decompressedFrom` records the source format and
`autoDecompress = false` opts out.

### **4. Plug-ins for File Types**

Per-format plug-ins under `UltraCanvas/Plugins/` (Charts, Diagrams, Text,
Vector, SVG, Documents, Videos, Graphs).

### **5. UltraNet**

The UltraNet module provides comprehensive functionality to equip programmers
with all kinds of network communication services to cover all client and
server connectivity tasks.

UltraNet elements must comply with the following rules:
- Clear structure
- Function and call names must be easily understandable by their names
- Comprehensive options to cover all use cases
- New protocols must use the same function and extension naming patterns as
  previously created functions and callbacks
- TLS verification is ON by default; insecure modes require explicit opt-in
- All blocking operations return `UltraNetResult`; all connection-oriented
  operations return `UltraNetHandle`

UltraNet uses open-source libraries (libcurl, OpenSSL, libssh2, c-ares) and
encapsulates their functionality in an easy-to-understand and usable way to
remain flexible for extending or replacing supporting libraries in the
future.

**Available Functions (Core, Tier 1):**
- `UltraNet_HttpGet`, `UltraNet_HttpPost`, `UltraNet_HttpPut`,
  `UltraNet_HttpDelete`, `UltraNet_HttpHead`, `UltraNet_HttpPatch`
- `UltraNet_HttpRequest`, `UltraNet_HttpRequestAsync`
- `UltraNet_HttpDownloadFile`, `UltraNet_HttpUploadFile`
- `UltraNet_WebSocketConnect`, `UltraNet_WebSocketSendText`,
  `UltraNet_WebSocketSendBinary`, `UltraNet_WebSocketClose`
- `UltraNet_FtpDownload`, `UltraNet_FtpUpload`, `UltraNet_FtpListDirectory`,
  `UltraNet_FtpDelete`, `UltraNet_FtpRename`
- `UltraNet_TcpConnect`, `UltraNet_TcpListen`, `UltraNet_TcpAccept`,
  `UltraNet_TcpSend`, `UltraNet_TcpReceive`, `UltraNet_SocketLocalEndpoint`
- `UltraNet_OAuth2GeneratePkce`, `UltraNet_OAuth2ChallengeFromVerifier`,
  `UltraNet_OAuth2GenerateState`, `UltraNet_OAuth2BuildAuthUrl`,
  `UltraNet_OAuth2WaitForCallback`, `UltraNet_OAuth2ExchangeCode`,
  `UltraNet_OAuth2Refresh`, `UltraNet_OAuth2ParseTokenResponse`,
  `UltraNet_OAuth2AuthorizeInteractive`
- `UltraNet_UdpOpen`, `UltraNet_UdpSend`, `UltraNet_UdpReceive`
- `UltraNet_TlsWrap`, `UltraNet_TlsHandshake`, `UltraNet_TlsGetInfo`
- `UltraNet_DnsResolve`, `UltraNet_DnsResolveAsync`, `UltraNet_DnsReverseLookup`
- `UltraNet_CreateSession`, `UltraNet_SessionHttpGet`, `UltraNet_SessionHttpPost`
- `UltraNet_ParseUrl`, `UltraNet_BuildUrl`, `UltraNet_UrlEncode`,
  `UltraNet_UrlDecode`
- `UltraNet_CancelRequest`, `UltraNet_GetTransferStats`
- `UltraNet_RegisterPlugin`, `UltraNet_GetSupportedSchemes`

**Plugin Categories (Tier 2 / Tier 3, located under `Plugins/UltraNet/`):**
- Mail: SMTP, IMAP, POP3
- Messaging: MQTT, AMQP
- Remote Access: SSH, Telnet
- Directory: LDAP, LDAPS
- Streaming: RTSP, RTMP, RTP, SIP
- IoT: CoAP, MQTT-SN, SNMP
- Discovery: mDNS / Bonjour / Zeroconf
- Web Modern: gRPC, HTTP/3 (QUIC), WebDAV

UltraNet is the recommended communication module for ULTRA Store,
UltraTexter, UltraFiler, and any other UltraCanvas-based application that
requires network connectivity. FileLoader uses UltraNet internally when
loading files from `http://`, `https://`, `ftp://`, etc. URLs.

**Implementation status (this branch):** Stage 2/3 — the synchronous HTTP
verbs, `UltraNet_HttpRequest`, download/upload, async via a curl_multi
worker (`UltraNet_HttpRequestAsync` with chunked `onDataChunk` streaming,
`UltraNet_CancelRequest` / `UltraNet_IsRequestActive` /
`UltraNet_GetTransferStats`), SSE (`UltraNet_SseStream[Async]` +
`UltraNetSseParser`), WebSocket (on libcurl ≥ 7.86 with ws support),
sessions/cookies, TLS layering, DNS, and the URL utilities are implemented
on libcurl. Remaining gaps are per-function and environment-dependent —
build and run the `UltraNetApiStatus` probe tool for ground truth on a
given machine. See `Docs/Modules/UltraNet/README.md`.

### **6. UltraDatabase**

Unified database access layer. Central registry of **named connections**
that apps define once and reference everywhere; the backing engine is a
configuration detail. Bundles **SQLite** for zero-config embedded storage
and adds other engines (PostgreSQL, MySQL/MariaDB, MSSQL, Redis, MongoDB,
DuckDB) as driver plug-ins under `Plugins/UltraDatabase/` via the
`IDatabaseDriverPlugin` interface — the core binary does not change to add
an engine.

UltraDatabase elements must comply with the same rules as UltraNet:
- Clear structure; call names understandable by their names
- New drivers use the same function and callback naming patterns
- **Parameter binding always** — SQL text and values travel separately, so
  injection is not possible through the public API (no string-built SQL)
- TLS verification ON by default for networked engines; credentials come
  from UltraVault, never from app code or config files
- All blocking operations return `UltraDbResult`; all statement /
  transaction / cursor / async operations return `UltraDbHandle`

Like UltraNet, it encapsulates open-source libraries (bundled SQLite,
`libpq`, `libmariadb`, ODBC, `hiredis`) so a backing library can be
extended or replaced without changing callers.

**Available Functions (Core, Tier 1):**
- `UltraDb_RegisterConnection`, `UltraDb_CloseConnection`,
  `UltraDb_GetConnectionInfo`
- `UltraDb_Query`, `UltraDb_Exec`, `UltraDb_QueryAsync`
- `UltraDb_Prepare`, `UltraDb_ExecPrepared`, `UltraDb_Finalize`
- `UltraDb_Begin`, `UltraDb_ExecInTx`, `UltraDb_Commit`, `UltraDb_Rollback`
- `UltraDb_OpenCursor`, `UltraDb_FetchRow`, `UltraDb_CloseCursor`
- `UltraDb_Migrate`, `UltraDb_CancelQuery`
- `UltraDb_RegisterDriver`, `UltraDb_GetSupportedDrivers`

**Driver Categories (Tier 2 / Tier 3, located under `Plugins/UltraDatabase/`):**
- Relational (SQL): PostgreSQL, MySQL / MariaDB, Microsoft SQL Server
- Embedded: SQLite (core, bundled), DuckDB (analytics)
- Key-value: Redis / Valkey
- Document: MongoDB

UltraDatabase is the recommended persistence module for UltraMail, ULTRA
Store, and any other UltraCanvas-based application that needs local or
networked storage. It depends on UltraVault for credentials and,
optionally, on UltraNet for TLS transport to networked engines.

**Implementation status (this branch):** Concept / design only. Public
surface specified in `Docs/Modules/UltraDatabase/README.md`; suggested
rollout is SQLite core + registry + query/transaction/migration API
(Stage 1), async + pooling + PostgreSQL/MySQL drivers (Stage 2),
remaining drivers and at-rest encryption (Stage 3).

### **7. UltraWin**

The UltraWin module runs Windows applications on Linux / ULTRA OS as single
native windows — never a Windows desktop — with the user's own folders
visible to the applications under a unified drive letter.

UltraWin elements must comply with the following rules:
- Clear structure; function and call names must be easily understandable
- Blocking operations return `UltraWinResult`; application instances are
  opaque `UltraWinHandle`s
- No Windows desktop is ever displayed; no full-desktop viewer paths
- Engines are never linked: Wine (and later QEMU) run as spawned child
  processes, keeping LGPL/GPL licensing outside the framework binaries
- Wine's default `Z:` → `/` host-root exposure is off by default; the
  user's home is mapped as `U:` in every environment

UltraWin uses open-source engines (Wine for the API-translation tier;
QEMU/KVM + FreeRDP RemoteApp planned for the full-virtualisation tier) and
encapsulates them so backings can be swapped — see
`Docs/Research/UltraWinDesignProposal.md`.

**Available Functions (Stage 1, Wine tier):**
- `UltraWin_Initialize`, `UltraWin_Shutdown`, `UltraWin_IsInitialized`,
  `UltraWin_GetConfig`, `UltraWin_SetConfig`, `UltraWin_GetCapabilities`,
  `UltraWin_GetVersion`
- `UltraWin_CreateEnvironment`, `UltraWin_DeleteEnvironment`,
  `UltraWin_ListEnvironments`, `UltraWin_EnvironmentExists`
- `UltraWin_MapFolder`, `UltraWin_UnmapFolder`, `UltraWin_ListMappings`
- `UltraWin_EnvironmentForPath`, `UltraWin_GetAssociation`,
  `UltraWin_SetAssociation`, `UltraWin_RemoveAssociation`,
  `UltraWin_SuggestEnvironment` (program→environment linkage: owning
  prefix, remembered picker choices, and picker defaults)
- `UltraWin_InstallComponent`, `UltraWin_ListComponents` (winetricks-verb
  components: VC++ runtimes, fonts, .NET, DXVK, … — spawned winetricks)
- `UltraWin_RunApp` (extension-routed: `.exe` direct, `.msi` via msiexec,
  `.lnk` via `start /wait`), `UltraWin_CloseApp`, `UltraWin_KillApp`,
  `UltraWin_GetAppInfo`, `UltraWin_GetAppState`, `UltraWin_ListApps`,
  `UltraWin_WaitApp`, `UltraWin_ReleaseApp`
- `UltraWin_ListPrograms` (Start-Menu shortcuts an installer created — what
  an ULTRA OS launcher shows; entries are launchable via `UltraWin_RunApp`)

- `UltraWin_VmProvision`, `UltraWin_VmStart`, `UltraWin_VmStop`,
  `UltraWin_VmKill`, `UltraWin_VmSuspend`, `UltraWin_VmResume`,
  `UltraWin_VmGetState`, `UltraWin_VmGetInfo` (Stage 2a machine backbone:
  the single shared headless QEMU/KVM guest — spawned, never linked —
  controlled over QMP; RDP port forwarded for the RemoteApp integration)

- `UltraWin_RunApp(forceTier = Vm)` — RemoteApp (RAIL) launches into the
  running guest over FreeRDP (the one linked engine, Apache 2, optional;
  guest paths, `||aliases`, and host paths under the shared home)
- virtiofs home share: `UltraWin_VmStart` exports `$HOME` into the guest
  (spawned virtiofsd + vhost-user-fs, tag `ultrawin_home`) so both tiers
  present the user's files under the same unified drive letter

**Planned (Stage 2b-ii/2c, 3):** the `UltraCanvasRemoteAppView` element
rendering RAIL window surfaces, guest provisioning validated against real
install media (incl. the guest-side virtiofs mount service), and
`UltraWin_QueryCompatibility` tier routing.

UltraWin is the recommended way for UltraFiler and any UltraCanvas-based
application to launch Windows executables. Linux / ULTRA OS only.

**Implementation status (this branch):** Stage 1 of the rollout — module
lifecycle, capability probing, environments (isolated Wine prefixes with
persisted drive mappings), application launch/supervision, and the
component installer (winetricks wrapper) are implemented; the VM tier and
compatibility routing are planned for Stages 2-3. See
`Docs/Modules/UltraWin/README.md`.

### **8. UltraCrypt**

Cryptographic services — the single place where hashing, message
authentication, authenticated encryption, key derivation and secure random
generation are implemented, so no application, plugin or sibling module has
to write its own. Sources under `UltraCanvas/{include,core}/UltraCrypt/`;
see `Docs/Modules/UltraCrypt/README.md`.

Scope is **data at rest and data integrity**. Transport security stays in
UltraNet (per-platform and OS-native); credential *storage and policy* stay
in UltraVault, which consumes UltraCrypt for its file-backed fallback
backend.

UltraCrypt elements must comply with the same rules as UltraNet and
UltraDatabase:
- Clear structure; call names understandable by their names
- New algorithms use the same function and type naming patterns
- **AEAD only** — no raw block-cipher or unauthenticated-mode surface is
  exposed, so a caller cannot accidentally ship unauthenticated ciphertext
- Nonces are generated by the module by default; reuse must be opted into
- The CSPRNG never falls back to a PRNG — entropy failure is an error
- Secrets travel in `UltraCryptSecureBuffer` (move-only, zeroized on
  destruction, page-locked where the OS permits), never in `std::string`
- All blocking operations return `UltraCryptResult`

Like UltraNet and UltraDatabase, it encapsulates an open-source library so
the backing implementation can be replaced without affecting callers; the
backing library is never visible in a public header.

**Available Functions (Core, Tier 1):**
- `UltraCrypt_Initialize`, `UltraCrypt_Shutdown`, `UltraCrypt_IsAvailable`,
  `UltraCrypt_GetBackendName`
- `UltraCrypt_Hash`, `UltraCrypt_HashFile`, `UltraCrypt_GetDigestSize`,
  `UltraCrypt_IsHashAvailable`
- `UltraCrypt_Hmac`
- `UltraCrypt_AeadSeal`, `UltraCrypt_AeadOpen`, `UltraCrypt_GetKeySize`,
  `UltraCrypt_GetNonceSize`, `UltraCrypt_GetTagSize`,
  `UltraCrypt_IsAeadAvailable`
- `UltraCrypt_DeriveKeyFromPassword`, `UltraCrypt_DeriveKeyHkdf`,
  `UltraCrypt_RecommendedKdfParams`
- `UltraCrypt_RandomBytes`, `UltraCrypt_RandomSecureBuffer`,
  `UltraCrypt_RandomUInt32`, `UltraCrypt_GenerateUuidV4`
- `UltraCrypt_SecureZero`, `UltraCrypt_ConstantTimeEquals`
- `UltraCrypt_ToHex`, `UltraCrypt_FromHex`, `UltraCrypt_Base32Decode` (into a
  secure buffer). The general RFC 4648 codecs — `Base32Encode`, `Base32Decode`,
  `Base64Encode`, `Base64Decode` — live in UltraCanvasUtils
  (`UltraCanvasTextUtils.h`, the platform-free text helpers compiled into the
  `UltraCanvasTextUtils` library that both the framework and UltraCrypt link);
  UltraCrypt's former copies were removed.

Streaming classes: `UltraCryptHasher`, `UltraCryptHmacHasher`.

UltraCrypt is the required cryptographic module for the UCD v2 file format,
UltraVault, UltraDatabase at-rest encryption, UltraAuthenticator and any
other UltraCanvas-based code that hashes, signs or encrypts stored data.
Hand-rolled crypto and direct calls into a vendored crypto library are
defects, not shortcuts.

**One cipher, one KDF.** Everything UltraCanvas writes is encrypted with
**XChaCha20-Poly1305** and keyed with **Argon2id**; no algorithm menu is
offered, because the formats are ours and a second choice would only be a
second code path in every reader. AES-256-GCM is present solely for reading
foreign data and is hardware-gated. Algorithm agility lives in format
version numbers, not in per-file algorithm identifiers.

**Implementation status (this branch):** Implemented on libsodium
(`UltraCanvas/{include,core}/UltraCrypt/`, target `UltraCrypt`): secure
buffers, secure zero, constant-time compare, CSPRNG, SHA-1/SHA-2 (one-shot,
streaming, file), HMAC, XChaCha20-Poly1305 AEAD, Argon2id, HKDF, and the
RFC 4648 Base16/32/64 companions — with published-vector unit tests
(`Tests/UltraCryptTests.cpp`). Built without libsodium every operation
fails closed with `BackendUnavailable`. First consumers: the UCD document
envelope and UltraVault's encrypted-file backend.

### **9. UltraVault**

Credential and secret storage — the single system-level home for API keys,
tokens and passphrases, so no application or module rolls its own
(`UltraAI/Docs/UltraVault.md` is the design document). Sources under
`UltraCanvas/{include,core}/UltraVault/`, target `UltraVault`, header
`<UltraVault/UltraVault.h>`, `namespace UltraVault`.

Public surface: `Result`/`ResultCode`, `SecretValue` (bytes + MIME type),
`SecretAcl`, `Initialize`/`Shutdown`/`IsAvailable`/`GetBackendName`,
`Put`/`Get`/`Delete`/`List(prefix)`, and namespaced keys
(`<vendor>.<app>.<purpose>`, e.g. `ai.anthropic.api_key`). UltraAI resolves
`ProviderConfig::apiKeyVaultRef` through `UltraVault::Get` when built with
`ULTRAAI_USE_ULTRAVAULT` (on by default in-tree).

**Implementation status (this branch):** v0.1 — memory backend (CI /
ephemeral) and encrypted-file backend (Argon2id-derived key, stored cost
parameters, XChaCha20-Poly1305 with the header as associated data; wrong
passphrase and file tampering are deliberately indistinguishable). Unit
tests in `Tests/UltraVaultTests.cpp`; the UltraAI resolution path is
covered by `Tests/UltraAIVaultIntegrationTests.cpp`. Platform-native
backends (libsecret / Keychain / Credential Manager) and
`Import`/`PromptUserForSecret` are planned.
persisted drive mappings), application launch/supervision, and the
component installer (winetricks wrapper) are implemented; the VM tier and
compatibility routing are planned for Stages 2-3. See
`Docs/Modules/UltraWin/README.md`.

---

### **10. VirtualFS**

Virtual file system — the single place where archive traversal, format
detection, decompression, entry caching, password handling and RAM disc
provisioning are implemented, so no application, plugin or sibling module
opens a ZIP by hand. Files inside an archive are reached as if they were
regular folders, including archives nested inside archives. Sources under
`VirtualFS/{include,core,providers,OS/<Platform>}`, target `VirtualFS`,
header `<VirtualFS/VirtualFS.h>`, `namespace VirtualFS`; see
`Docs/Modules/VirtualFS/README.md` and the full function list in
`VirtualFS/VirtualFS_Master_Registry_V1.md`.

Scope is **archives and scratch storage**. Loading and converting a file
once it has been reached stays in FileLoader; the UltraCanvas-facing
compression shims live in `UltraCanvasVirtualFSBridge`.

VirtualFS elements must comply with the same rules as UltraNet, UltraCrypt
and UltraDatabase:
- Clear structure; call names understandable by their names
- New formats arrive as an `IVirtualFSProvider`, never as a special case
  inside the manager
- Every call returns `VirtualFSResult` (`operator bool` for quick checks,
  `VirtualFSResultToString()` for messages)
- Paths use forward slashes and are normalized automatically; archive
  boundaries are detected by extension against 40+ known formats
- **Never** write `ZipFile`, `TarArchive` or `Uncompress()` at module level
  — go through `VirtualFS_*` or `UCVFSBridge::*`
- A RAM disc always reports its backing; the Windows disk fallback is never
  presented as memory (see `VirtualFSRamDisk::IsTrueRam()`)

Like UltraNet and UltraCrypt, it encapsulates an open-source library
(libarchive) so the backing implementation can be replaced without
affecting callers; the backing library is never visible in a public header.

**Available Functions (Core, Tier 1):**
- Lifecycle: `VirtualFS_Initialize`, `VirtualFS_Shutdown`,
  `VirtualFS_IsInitialized`, `VirtualFS_GetVersion`
- Reading: `VirtualFS_ReadFile`, `VirtualFS_ReadFileString`,
  `VirtualFS_ReadFilePartial`, `VirtualFS_OpenStream`,
  `VirtualFS_ExtractToMemory`
- Listing: `VirtualFS_ListDirectory`, `VirtualFS_ListDirectoryFiltered`,
  `VirtualFS_ListDirectoryRecursive`, `VirtualFS_EnumerateDirectory`
- Queries: `VirtualFS_Exists`, `VirtualFS_IsFile`, `VirtualFS_IsDirectory`,
  `VirtualFS_IsArchive`, `VirtualFS_IsInsideArchive`, `VirtualFS_GetInfo`,
  `VirtualFS_GetSize`, `VirtualFS_GetType`, `VirtualFS_GetMimeType`,
  `VirtualFS_DetectFormat`, `VirtualFS_GetArchiveInfo`
- Extraction: `VirtualFS_ExtractFile`, `VirtualFS_ExtractAll`,
  `VirtualFS_ExtractFiltered`
- Writing: `VirtualFS_CreateArchive`, `VirtualFS_AddToArchive`,
  `VirtualFS_DeleteFromArchive`
- Validation: `VirtualFS_ValidateArchive`, `VirtualFS_TestArchive`
- Paths: `VirtualFS_NormalizePath`, `VirtualFS_ResolvePath`,
  `VirtualFS_JoinPath`, `VirtualFS_GetParentPath`,
  `VirtualFS_GetFileName`, `VirtualFS_GetExtension`
- Providers: `VirtualFS_RegisterProvider`, `VirtualFS_UnregisterProvider`,
  `VirtualFS_GetProviderForPath`, `VirtualFS_GetRegisteredProviders`,
  `VirtualFS_GetSupportedExtensions`
- Configuration: `VirtualFS_SetPasswordCallback`,
  `VirtualFS_SetErrorCallback`, `VirtualFS_GetErrorMessage`,
  `VirtualFS_SetTempDirectory`, `VirtualFS_GetTempDirectory`,
  `VirtualFS_SetDefaultEncoding`
- Cache: `VirtualFS_SetCacheEnabled`, `VirtualFS_SetMaxCacheSize`,
  `VirtualFS_GetCacheSize`, `VirtualFS_ClearCache`,
  `VirtualFS_ClearCacheForPath`

**Raw buffer compression** (`VirtualFSCompression.h`) — the compression
codecs without an archive container, for callers such as the UltraWeb
bundler and FileLoader: `VirtualFS_CompressBuffer`,
`VirtualFS_DecompressBuffer`, `VirtualFS_DetectCompressionMethod`,
`VirtualFS_IsCompressionMethodAvailable`. Methods: `Store`, `Deflate`,
`Zstd`, `LZ4` (frame format), `Brotli`, each subject to its
`VIRTUALFS_USE_*` build option.

**RAM discs** (`VirtualFSRamDisk.h`) — OS-visible scratch volumes, backed by
each platform's own facility (`/dev/shm` tmpfs on Linux, `hdiutil
attach ram://` on macOS, the ImDisk driver on Windows when installed, an
overwritten `%TEMP%` directory otherwise): `VirtualFS_CreateRamDisk`,
`VirtualFS_DestroyRamDisk`, `VirtualFS_ListRamDisks`,
`VirtualFS_UseRamDiskForTemp`, `VirtualFS_IsTrueRamDiskAvailable`,
`VirtualFS_GetPreferredRamDiskBacking`. Discs are private to the calling
user and do not survive a reboot.

**Provider interface** (`IVirtualFSProvider`) — one implementation per
format family. `LibArchive` covers 40+ formats (ZIP, 7z, TAR family, RAR
read-only, ISO/UDF, CAB, CPIO, DEB, RPM, and the ZIP-derived app bundles);
CHM (libmspack) and WIM (wimlib) providers are planned. Optional
`OpenFromMemory()` lets a provider open an archive from a buffer, which is
how nested archives are traversed without writing a temp file; providers
that implement it advertise `VirtualFSCapability::MemoryOpen`.

**Implementation status (this branch):** VirtualFSManager, VirtualFSPath,
the LibArchive provider and the UltraCanvas bridge are complete;
memory-backed nested archives and the Linux and macOS RAM disc back ends
are complete. On Windows a true RAM disc requires an ImDisk installation —
the driver is detected, never bundled — and without it the disc degrades to
storage-backed and reports itself as such. Regression tests:
`Tests/VirtualFSPathTest.cpp`, `Tests/VirtualFSDeleteTest.cpp`,
`Tests/VirtualFSNestedMemoryTest.cpp`, `Tests/VirtualFSRamDiskTest.cpp`.

---

### **11. UltraCloud**

Cloud storage — the single home for cloud accounts, the default account, and
"upload this and give me a share link", so no application talks to a cloud
provider on its own. Providers are stateless plug-ins behind `ICloudProvider`
(Verify / List / MakeDirectory / Upload / Download / CreateShareLink /
SignIn / RefreshCredentials / AccountInfo); v0.2 ships Nextcloud / ownCloud
(WebDAV + OCS share API, password and expiry on links), generic WebDAV (links
through a public web-folder URL), Dropbox, OneDrive and Google Drive (OAuth2 +
PKCE through the system browser via UltraNet, tokens refreshed automatically;
the OAuth client id is configuration, `SetOAuthApp` or
`ULTRACLOUD_<PROVIDER>_CLIENT_ID`), and an in-memory demo provider. Providers
can also ship as plug-in libraries (`UltraCloud_PluginInit`,
`LoadProviderPlugins`).
Accounts persist on UltraDatabase (`AccountStore`), secrets go to UltraVault
(`VaultSecretStore`) or the per-app obfuscated fallback (`FileSecretStore`),
HTTP goes through UltraNet. `CloudService` is the app-facing facade;
`UltraCloudUI` holds the shared add-account and link-picker dialogs.
Sources under `UltraCloud/{include,core,providers,ui}`, targets `UltraCloud`
and `UltraCloudUI`, header `<UltraCloud/UltraCloud.h>`, `namespace UltraCloud`;
see `Docs/Modules/UltraCloud/README.md`.

---

### **12. UltraAndroid**

The UltraAndroid module runs Android applications on Linux / ULTRA OS as
single native windows — never an Android home screen — with the user's own
folders visible to the applications at one fixed mount point.

**Not to be confused with `UltraCanvas/OS/Android/`**, which is the opposite
direction: that backend runs *our* apps *on* Android. UltraAndroid runs *other
people's* Android apps *on Linux*. No shared code; they meet only in that the
runtimes UltraAndroid provisions are also the test beds that backend needs.

UltraAndroid elements must comply with the following rules:
- Clear structure; function and call names must be easily understandable
- Blocking operations return `UltraAndroidResult`; runtime, application and
  share instances are opaque `UltraAndroidHandle`s
- No Android home screen, launcher or notification shade is ever displayed;
  every Android app window is a native ULTRA OS window
- Engines are never linked: the container manager, LXC, `adb` and QEMU run as
  spawned child processes, keeping GPL licensing outside the framework
  binaries
- Google Play / GMS is never bundled, and ARM translation layers
  (libndk/libhoudini) are never redistributed — both are detected and
  reported, never shipped
- The user's folders are shared, not copied, and appear at the same guest
  path in both tiers

UltraAndroid uses open-source runtimes (a Waydroid-class LXC container on the
host kernel for the default tier; Cuttlefish or the AOSP emulator under KVM
for the fallback tier) and encapsulates them so backings can be swapped — see
`Docs/Research/UltraAndroidDesignProposal.md`, and
`Docs/UltraCanvas/AndroidOnLinuxInvestigation.md` for the survey that selected
them.

**Proposed Functions (Stage 1 — host-side, no runtime required):**
- `UltraAndroid_Initialize`, `UltraAndroid_Shutdown`,
  `UltraAndroid_IsInitialized`, `UltraAndroid_GetConfig`,
  `UltraAndroid_SetConfig`, `UltraAndroid_GetCapabilities`,
  `UltraAndroid_GetVersion`
- `UltraAndroid_InspectApk` (package, label, icon, minSdk/targetSdk and native
  ABIs, read straight out of the APK — zip via VirtualFS plus an AXML
  decoder), `UltraAndroid_QueryCompatibility` (runs natively / needs
  translation / needs the other tier / cannot run)

**Proposed Functions (Stage 2 — container tier):**
- `UltraAndroid_ListImages`, `UltraAndroid_InstallImage`,
  `UltraAndroid_RemoveImage`, `UltraAndroid_GetImageInfo`
- `UltraAndroid_StartRuntime`, `UltraAndroid_StopRuntime`,
  `UltraAndroid_GetRuntimeState`, `UltraAndroid_GetRuntimeInfo`
- `UltraAndroid_ShareFolder`, `UltraAndroid_UnshareFolder`,
  `UltraAndroid_ListShares`
- `UltraAndroid_InstallApk`, `UltraAndroid_UninstallApp`,
  `UltraAndroid_ListApps`, `UltraAndroid_GetAppInfo`, `UltraAndroid_RunApp`,
  `UltraAndroid_CloseApp`, `UltraAndroid_KillApp`, `UltraAndroid_GetAppState`,
  `UltraAndroid_WaitApp`, `UltraAndroid_ReleaseApp`

**Planned (Stage 3):** the VM tier behind the same API for hosts whose kernel
has no binder, ULTRA OS launcher entries, audio routing per app window, and
host↔guest clipboard.

UltraAndroid is intended to be the recommended way for UltraFiler and any
UltraCanvas-based application to describe and launch `.apk` files.
Linux / ULTRA OS only.

**Implementation status:** none — named and specified only. The design
proposal is written; no code, no `Docs/Modules/UltraAndroid/README.md` and no
demo entry exist yet, and those land with Stage 1 rather than before it.
