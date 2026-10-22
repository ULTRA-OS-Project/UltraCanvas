- **No framework enumerator is named `None` any more.** X11's `None` macro
  rewrote such an enumerator in any file that included a window header
  first, so 28 headers and sources carried an `#undef None` and others
  worked around it with include order. Every `None` enumerator in the
  framework's headers is renamed and every `#undef None` is gone:
  `HardwareQuery::Nothing`; spreadsheet `CellErrorType::NoError`,
  `BorderStyle::NoBorder`, `UnderlineStyle::NoUnderline`,
  `FillPattern::NoFill`, `FilterOperator::NoFilter`,
  `ClipboardOperation::NoOperation`, `PasteOperation::NoOperation`,
  `HitArea::Nowhere`; `WaveformOverlay::NoOverlay`; QR code
  `QRLogoStyle::NoLogo`, `QRGradientType::NoGradient`; charts
  `ConnectionCapStyle::NoCap`, `SunburstLabelContent::NoLabel`,
  `SunburstShadeMode::NoShade`, `ValueVisualizationType::NoVisualization`,
  `DecorativeRingType::NoRing`, `ConnectionLineType::NoLine`,
  `NestedAreaLabelPosition::NoLabels`, `ChordLabelPlacement::NoLabels`, pie
  `LabelPosition::NoLabels`; diagrams `ArcValueDisplay::NoLabel`,
  `GroupLabelPlacement::NoLabels`, `GroupFillMode::NoFill`,
  `ConfidenceDisplayMode::Hidden`, `FlowChartArrowStyle::NoArrow`; UltraNet
  `UltraNetAuthType::NoAuth`, `UltraNetProxyType::NoProxy`,
  `UltraNetMailAuth::NoAuth`, `UltraNetMailFlags::NoFlags`; UltraVault
  `SignInMethod::NoSignIn`; VirtualFS `VirtualFSCapability::NoCapabilities`,
  `VirtualFSRamDiskBacking::NotCreated`, `UCVFSCompressionType::Uncompressed`;
  printers `IODuplexMode::OneSided`, `IOSupplyColor::NoColor`; network
  monitor `LoopbackRole::NotLoopback`, `NameSource::NoSource`; video timeline
  `Drag::NoDrag`; LaTeX `MathAtomClass::NoClass`; and in the modules that
  arrived meanwhile, `UltraIMCapabilityFlags::NoCapabilities`,
  `UltraIMResultCode::Ok` and `UltraRtcResultCode::Ok`. The strings the file
  formats write are unchanged. (`NoValue` is also an X11 macro, hence
  `NoVisualization`.)
- **Nor is any named `Success`.** X11's `Success` macro (0) did the same to
  the result-code enumerators, guarded by `#undef Success` in UltraNet,
  UltraWin, UltraMessage, UltraDatabase, UltraCrypt, UltraVault, IODevice,
  EBook and VirtualFS headers and two core sources. The enumerator is `Ok`
  in every result code - `VirtualFSResult::Ok`, `UltraNetResultCode::Ok`,
  `UltraWinResultCode::Ok`, `UltraMsgResultCode::Ok`, `UltraDbResultCode::Ok`,
  `UltraCryptResultCode::Ok`, `UltraVault::ResultCode::Ok`,
  `IODeviceResultCode::Ok`, `NetworkMonitorResultCode::Ok`,
  `StoreResultCode::Ok` - and the factories `WasmStatus::Success()` and
  `VaultResult::Success()` are `WasmStatus::Ok()` (like `UltraNetResult::Ok()`)
  and `VaultResult::NoError()` (`VaultResult::Ok()` is its query). Every
  `#undef Success` is gone.
- **And no `#undef` of an X11 macro is left at all.** The `Status`, `Always`
  and `Bool` guards in UltraNet, UltraWin, UltraDatabase, IODevice, EBook,
  Spreadsheet and RequirementModel headers are gone with the identifiers
  they covered: `RequirementField::Status` is `State` (the ReqIF field
  names are unchanged), `UltraDbType::Boolean` and `ZWaveValueType::Bool` are
  `Boolean`, `AiStatus::Status()` is `StatusOf()` and the IMAP response's
  `Status()` is `StatusWord()`. Nothing was named `Always` any more.
- **CI keeps it that way.** `scripts/check_x11_names.py` (`x11-names.yml`)
  fails on an enumerator, function, method or member named like any of the
  481 object-like macros `<X11/X.h>`, `Xlib.h` and `Xutil.h` define, and on
  an `#undef` or `push_macro` of one; the list is generated from the headers
  into `scripts/x11_macro_names.txt`, the baseline is empty, and a site that
  is correct as it stands says `// x11-name-ok: <why>`. The names it found
  on its first run are gone too: `ArcEdgeType` and `ArcLabelSide` say
  `AboveBaseline` / `BelowBaseline`, the IPP tag is `IppTag::NoValueTag`, the
  Zigbee frames' `Status` fields are `StatusCode`, and a handful of file-local
  helpers named `Bool`, `Below` or `CurrentTime` are renamed.
- **And the Objective-C runtime's names the same way.** In a `.mm` file
  `YES`, `NO`, `nil` and `Nil` are macros and `id`, `Class`, `SEL`, `IMP`,
  `BOOL` and `Protocol` are global typedefs. `scripts/check_objc_names.py`
  (`objc-names.yml`) fails on an enumerator or member named like a macro and
  on a file-scope declaration named like a type; inside a namespace, a
  class, an enum or a parameter list the names are free, as clang confirms,
  so the framework's `int id` members and `const std::string& id` parameters
  are not findings. Its first run found nothing; the baseline is empty, and
  a site that is correct as it stands says `// objc-name-ok: <why>`.
- **One script for the three name checks.** `scripts/check_platform_names.py`
  replaces `check_win32_names.py`, `check_x11_names.py` and
  `check_objc_names.py`: the same rules and finding kinds (`win32-name-*`,
  `x11-*`, `objc-*`), the same `// <platform>-name-ok` markers, one
  workflow (`platform-names.yml`) and one baseline
  (`scripts/platform_names_baseline.txt`, carrying the Win32 sites still
  being worked off). `--platform win32|x11|objc` narrows a run,
  `--self-test` covers all three, and `--generate-names` takes the platform
  whose list to rebuild.
- **`MacTypes.h` is covered too.** Every Foundation import reaches
  CoreServices' `MacTypes.h`, which puts `Rect`, `Point`, `Boolean`, `Byte`,
  `Size`, `Handle`, `Ptr`, `Fixed`, `Style`, `OSErr`, `UInt8`, `Str255` and
  a hundred more typedefs at global scope, the Style constants `normal`,
  `bold`, `italic`, `underline`, ... as global enumerators, and `kInvalidID`
  as a macro. `scripts/mactypes_global_names.txt` is generated from the
  header itself (`--platform objc --generate-names <SDK>/usr/include/MacTypes.h`;
  the file is byte-identical in the 10.15 and 11.3 SDKs), and the
  Objective-C file-scope rule reads it. `Length`, `Debugger68k` and the
  macro form of `StrLength` are left out: a C++ translation unit never sees
  them. The tree is clean; the Objective-C rules now skip the Windows, Linux,
  BSD, WASM and Android glue, which no `.mm` file reaches.
