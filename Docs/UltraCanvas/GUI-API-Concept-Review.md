# UltraCanvas GUI API — Concept Review

**Date:** 2026-07-02
**Scope:** Public API concept and architecture: core headers (`UltraCanvasUIElement.h`, `UltraCanvasEvent.h`, `UltraCanvasApplication.h`, `UltraCanvasWindow.h`, `UltraCanvasRenderContext.h`, `UltraCanvasContainer.h`), the CSS layout engine (`include/CSSLayout`, `core/CSSLayout`), the platform layer (`OS/Linux`, `OS/MSWindows`, `OS/MacOS`, `OS/BSD`, `OS/WASM`), a 10-widget sample of the component layer, the dirty-rect/rendering pipeline, and the `Docs/` folder. All findings were verified against source; file:line references are given per finding.

---

## Verdict

**The concept is sound and is confirmed — with reservations in three specific areas.**

The architectural core holds up well:

- **Shared base + thin native layer.** `UltraCanvasApplicationBase`/`UltraCanvasWindowBase` keep all dispatch, focus, modal, popup, tooltip, timer and dirty-rect logic platform-independent; a backend implements ~15 pure virtuals. The Linux/Windows pair proves the decomposition works.
- **The CSS layout engine is above average for a from-scratch framework.** Two-phase Measure/Arrange with Flutter-style constraints (`Exact/AtMost/Unbounded`), spec-shaped flexbox (css-flexbox-1 structure, citation comments), a CSS 2.1 §10.3.7-correct absolute solver shared between measure and arrange, disciplined caching with context-aware keys, and zero knowledge of widgets or rendering. There is **one** element tree (children/geometry live only on `CSSLayout::Element`; `UltraCanvasContainer` is a typed view) — the classic dual-hierarchy divergence bug is designed out.
- **The element-local rendering contract is coherent and the code matches the docs.** Containers push/clip/translate so each element renders at (0,0) with a local dirty rect and receives pre-mapped local event coordinates (`UltraCanvasContainer.cpp:74-85`, `UltraCanvasCoordinateSystemGuide.md`). This eliminates the double-offset bug class by construction.
- **The compositor-in-miniature is a good design.** Window content, each popup, and tooltips render to separate retained surfaces with independent dirty queues; closing a popup recomposites instead of repainting what was underneath.
- **The event loop is done right.** Blocking (zero idle CPU), timer-deadline-driven timeouts, per-platform wakeup primitives (eventfd / auto-reset event / CFRunLoopSource), thread-safe inbound event queue, starvation guard.

The three areas of genuine concern:

1. **The ownership/lifetime model is the weakest part of the concept.** `shared_ptr`-owned children coexist with five kinds of *raw* element pointers (`parent`, `window`, `_focusedElement`, app-level hovered/captured/dragged, `UCEvent::targetElement`) held together by manual cleanup hooks — and the cleanup has real, traceable gaps that produce use-after-free / undefined behavior (findings A1–A4).
2. **The "cross-platform" abstraction is an X11 design that other platforms impersonate,** and it leaks Cairo/Pango/Xlib/windows.h into every client translation unit. It has never been validated against a genuinely different backend (the WASM render context no longer compiles against the current interface). The claimed platform matrix (Linux, Windows, macOS, BSD, WASM, iOS, Android) is really Linux + Windows + a lagging macOS.
3. **The threading contract is implicit and violated by the framework's own timer system.** Only `PushEvent` and `StartTimer/StopTimer` are safe off-thread; nothing documents this, `RequestRedraw` is unsynchronized and cannot wake the blocked loop, and `ProcessTimers` has a data race plus a dangling-reference bug even single-threaded.

None of these require architectural upheaval to fix. Concrete remedies are in the Recommendations section.

---

## A. Ownership & lifetime (Critical)

**A1. Queued events hold raw `targetElement` that is never scrubbed on element destruction.**
`UCEvent::targetElement` is a raw pointer (`UltraCanvasEvent.h:299`). Widgets enqueue command events pointing at themselves (`UltraCanvasMenu.cpp:1119/1144/1176`, `UltraCanvasDropdown.cpp:249`); dispatch dereferences the stored pointer without a registry check (`UltraCanvasApplication.cpp:706-707`, `:727-741`, `:760-762`). `CleanupElementReferences` (`UltraCanvasApplication.cpp:334-348`) clears hovered/captured/dragged/focus but does **not** scan `eventQueue`. An element that posts an event and is destroyed before dispatch (menu destroyed by its own action, popup closed) leaves a dangling pointer that is dereferenced. Stale *windows* are guarded (`:468-476`); elements are not.

**A2. Destruction-order UB: children destruct after their owning window is torn down, then call back into it.**
`~UltraCanvasUIElement` calls `CleanupElementReferences(this)`, which reads `window->IsCreated()`, `GetState()`, `GetFocusedElement()` and makes the virtual call `SetFocusedElement(nullptr)` (`UltraCanvasUIElement.cpp:15-20`, `UltraCanvasApplication.cpp:344-347`). Children are owned by the `children` vector on the most-base class `CSSLayout::Element` (`CSSLayout.h:460`), so on window destruction the child destructors run *after* `~UltraCanvasWindowBase`/`~UltraCanvasContainer` have completed and the vtable has been demoted — reads of destructed members and virtual dispatch through a degraded vtable. It survives today only because `PerformClose` happens to leave `_created = false` behind; dropping a window's last `shared_ptr` without `Close()` is hard UB.

**A3. Focus teardown dispatches events into an element already inside its destructor.**
Dying focused element → `SetFocusedElement(nullptr)` → `SendFocusLostEvent(prev)` → `prev->OnEvent(...)` + `prev->RequestRedraw()` (`UltraCanvasWindow.cpp:48-65`, `:173-180`). The derived destructor has already run; the base `OnEvent` still invokes the user's `eventCallback`, whose lambda typically captures the derived `this`. Cleanup hooks should null pointers, never dispatch.

**A4. `ClosePopup` can free the element mid-function; `OpenPopup` is a `shared_from_this` footgun.**
`RemoveChild(elem.shared_from_this()); elem.renderContext.reset(); elem.OnPopupClosed(reason);` (`UltraCanvasWindow.cpp:483-485`) — if the children vector held the last reference, the uses after `RemoveChild` are use-after-free. `UltraCanvasMenu.cpp:1044` guards this exact pattern ("ExecuteItem may destroy *this"); this site doesn't. `OpenPopup(..., UltraCanvasUIElement&, ...)` calls `shared_from_this()` on a by-reference parameter (`:447-455`) — a stack- or `unique_ptr`-owned element throws `bad_weak_ptr`; the signature invites the misuse.

*Also in this family:* hiding a container leaves a hidden descendant focused and still receiving keys (`UltraCanvasUIElement.cpp:245-257`, `UltraCanvasApplication.cpp:628-649`); `UnInstallWindowEventFilter` never removes anything (inverted `empty()` check + erasing from a copy, `UltraCanvasWindow.cpp:212-224`), so leaked filters are another vector for callbacks capturing dead elements; `CloseAllPopups` clears vetoed entries without cleanup (`UltraCanvasWindow.cpp:507-515`).

---

## B. Confirmed functional defects (fix before further porting/feature work)

| # | Defect | Evidence |
|---|--------|----------|
| B1 | **Focus/blur events never delivered on click-to-focus** — synthesized `UCEvent ev{WindowBlur/WindowFocus}` is built but the code dispatches `event` (the MouseDown) instead of `ev`, on all platforms | `core/UltraCanvasApplication.cpp:496-507` |
| B2 | **`ProcessTimers` data race + dangling reference** — iterates and fires callbacks without `timersMutex_` while `StartTimer/StopTimer` are documented cross-thread; and `auto& timer = timers_[i]` is held across a callback that may `push_back` → reallocation → UB even single-threaded | `core/UltraCanvasApplication.cpp:799-878` |
| B3 | **Radio menu items never fire** — `MenuItemData::Radio()` stores the callback in `onToggle`, but `ExecuteItem`'s Radio branch invokes `onClick`; the framework's own demo (`UltraCanvasMenuExamples.cpp:179-186`) is affected | `UltraCanvasMenu.h:650-668`, `core/UltraCanvasMenu.cpp:1148-1178` |
| B4 | **`MenuItemType::Input`/`Custom` are dead API** — declared, factory-constructed (placeholder arg silently discarded), zero handling in rendering or `ExecuteItem` | `UltraCanvasMenu.h:53-62,133-134,743-758`, `core/UltraCanvasMenu.cpp:1185` |
| B5 | **TreeView shadows base `backgroundColor`** — derived private member is never initialized (stays opaque black); `SetBackgroundColor()` writes the base member, row rendering reads the derived one | `UltraCanvasTreeView.h:143`, `core/UltraCanvasTreeView.cpp:657,666,193/233/273` |
| B6 | **`GetScreenPosition` default impl reads its own uninitialized out-params** (`x = config_.x + x;`); Linux/Windows override it, **macOS does not** → garbage on macOS | `UltraCanvasWindow.h:212-215` |
| B7 | **`IsKeyPressed` declared but never defined** (link error for any caller); `keyStates[256]` never zero-initialized; indexed by platform-specific `nativeKeyCode`, so it can't be portable anyway | `UltraCanvasApplication.h:90,130`, `core/UltraCanvasApplication.cpp:522-524` |
| B8 | **`StrokeText` strokes with the fill paint** — `ApplyStrokeSource()` applies `fillSourceColor/Pattern`; `Stroke()` uses the correct stroke source | `RenderContextCairo.h:70-72`, `RenderContextCairo.cpp:1584,1820-1827` |
| B9 | **Text-layout cache can return the wrong layout** — key ends at `text.substr(0,300)` (collisions after 300 bytes); omits `letterSpacing`; `lineHeight` is in the key but never applied ("Fixme"), so `SetTextLineHeight()` is a silent no-op; cached layouts are returned as mutable `shared_ptr` (one caller's mutation corrupts everyone's) | `RenderContextCairo.cpp:125-148,718-758` |
| B10 | **Invalidation from `Arrange`/`SetBounds` ignores the parent's scroll offset** — damage passed in unscrolled parent-frame coords into an API expecting the visual local frame; dirty rect lands `scrollOffset` pixels away inside scrolled containers | `core/UltraCanvasUIElement.cpp:298-316,65-90`, `core/UltraCanvasContainer.cpp:65-67` |
| B11 | **Scrollbar self-invalidation computed in the wrong frame** — displaced by `(contentOrigin + scroll)` once the container has border/padding or is scrolled; the comment claiming it works (`UltraCanvasContainer.cpp:565-568`) is wrong | `core/UltraCanvasContainer.cpp:94-104`, `core/UltraCanvasUIElement.cpp:180-195` |
| B12 | **Block layout ignores margins entirely** — `SetMargin()` works in a flex parent, silent no-op in the default block parent (still invalidates layout); flex resolves margins correctly | `core/CSSLayout/Element.cpp:306-312,404-413` vs `FlexLayout.cpp:84-100` |
| B13 | **`position: Fixed` double-offsets** — viewport-space coords written into parent-relative `finalBounds`; ancestor offsets applied twice for any Fixed child not parented at the window origin | `Element.cpp:435-438`, `FlexLayout.cpp:769-772`, `GridLayout.cpp:669-672` |
| B14 | **`MapFromLocal/MapToLocal` with explicit target parent are off by one level** (adds the target's own offset before breaking; `nullptr`/window-frame callers unaffected) | `core/UltraCanvasUIElement.cpp:30-39` |

---

## C. Platform abstraction (X11-centric; matrix overstated)

**C1. The abstraction vocabulary was lifted from X11, not designed neutrally.**
`UCKeys` values are literal X11 keysyms (`Escape = 0xFF1B`, `F1 = 0xFFBE`, XF86 media keys; `UltraCanvasEvent.h:85-274`); `UCMouseButton` encodes wheel directions as X11-style buttons. Linux converts nearly identity; Windows and macOS each maintain 100+-entry emulation tables (`OS/MSWindows/...Application.cpp:630-728`, `OS/MacOS/...Application.mm:475-596`); unmapped keys silently become `Unknown`. Public `UCEvent` carries `NativeWindowHandle` behind a 4-way `#ifdef` ladder, and `UltraCanvasCommonTypes.h:15-53` `#include`s `<windows.h>`/`<X11/Xlib.h>` into **every** framework and client TU, requiring `#undef DrawText/CreateWindow/RGB/Rect` macro surgery.

**C2. Event semantics diverge per platform.**
Windows delivers two `KeyDown` events per character keystroke (one from `WM_KEYDOWN`, one from `WM_CHAR` with `virtualKey = Unknown`); Linux delivers one with both keysym and text. The dedicated `TextInput`/`KeyChar` event types exist but are unused by all three backends. macOS emits no `MouseDoubleClick`, no `MouseEnter`/`MouseLeave` (no NSTrackingArea anywhere), no horizontal wheel; wheel deltas are ±5-per-notch on Linux/Windows but raw truncated `scrollingDeltaY` on macOS (small trackpad deltas truncate to 0). Mouse capture is a no-op stub on macOS; there is no macOS drag-and-drop implementation at all, despite `DragStart..Drop` being public API.

**C3. BSD and WASM are unreachable dead code; iOS/Android don't exist.**
BSD/WASM override virtuals that no longer exist in the base and cannot compile (`OS/BSD/UltraCanvasBSDApplication.h:118-121`, `OS/WASM/UltraCanvasWASMApplication.h:55`). Selection macros can't reach them anyway: `__WASM__` is tested (Emscripten defines `__EMSCRIPTEN__`) and the include path points at a nonexistent `OS/Web/`; on BSD, `defined(__unix__)` selects the **Linux** implementation, which includes `<sys/eventfd.h>`/`<linux/limits.h>`. `OS/iOS/` and `OS/Android/`, referenced from the public headers, are not in the tree. CMake wires only Linux/Windows/macOS.

**C4. Threading contract is implicit.**
Safe cross-thread channels are exactly `PushEvent` and `StartTimer/StopTimer` (modulo B2) — documented nowhere. `RequestRedraw` → dirty-rect path has no synchronization and no `WakeUpEventLoop()`, so a worker thread calling it races on the rect vector *and* the repaint waits for the next user input (the loop blocks indefinitely when no timers run). There is no `RunOnUIThread` helper. `running`/`initialized` are `bool volatile`, not atomics. Linux carries a complete, never-called event-thread implementation (`OS/Linux/...Application.cpp:788-862`) — a vestige of an abandoned two-thread design that would be unsafe if ever enabled.

**C5. Process-global singleton.**
Each platform app self-registers a static `instance` with no double-instantiation guard; `GetInstance()` is called from 84 sites in 30 files including widgets, the Cairo context and plugins. Consequences: one app per process, hidden init-order dependency, and widget unit tests require a live X11 connection.

---

## D. Rendering API (good contract, leaky abstraction, one big performance hole)

**D1. `IRenderContext` is hard-wired to Cairo/Pango.**
The *abstract* header includes `<cairo/cairo.h>` and `<pango/pangocairo.h>` (`UltraCanvasRenderContext.h:10-11`); the public factory exposes `CreateFontDescFromPango(const PangoFontDescription*)` (`:589`); the text-attribute enum mirrors Pango 1:1. A second backend can't be implemented without Pango semantics — and the only attempted non-Cairo backend (WASM) is bit-rotted against an older interface, i.e. the abstraction has never been re-validated.

**D2. No enforcement of the state-stack invariant; escape hatches break the rendering contract.**
An unbalanced `PopState()` still calls `cairo_restore` on an empty shadow stack, putting the `cairo_t` into permanent error state — one misbehaving widget silently blanks the window (`RenderContextCairo.cpp:406-414`). Meanwhile `ClearClipRect`/`ResetTransform`/`SetTransform`/`ResetState` are public on the same interface handed to `Render()`; `ClearClipRect` = `cairo_reset_clip`, which discards the window's dirty-region clip and every container clip. Elements are handed the whole-surface API where a restricted painting facade (plus a RAII state guard) is needed.

**D3. Composition throws away what dirty-rects saved.**
`FlushToSurface` has no region parameter and paints the entire backbuffer with `CAIRO_OPERATOR_SOURCE`; any dirty rect (a blinking caret) triggers a full-window blit plus re-flush of every popup plus a fresh tooltip render (`RenderContextCairo.cpp:1835-1853`, `UltraCanvasWindow.cpp:398-417`). On large/HiDPI windows this O(window-area) per-frame cost dominates. Related amplification: `UltraCanvasContainer::InvalidateLayout` calls `RequestRedraw()` on every bubble step, so any layout-affecting property set dirties the whole window (`UltraCanvasContainer.h:141-145`).

**D4. Coherence nits.** Two text pipelines with different anchoring (baseline-anchored toy-font `FillText` vs top-left Pango `DrawText`); `SetAlpha` has three different behaviors depending on code path; rect types proliferate (`Rect2Dd`/`Rect2Df`/`Rect2Di` across one hand-off chain) with a compensating 1px clip-inflation hack (`UltraCanvasContainer.cpp:75-76`); ~120 virtuals on one interface; `DrawRoundedRectangleWidthBorders` takes 17 parameters (and typos "Width" for "With"); per-text-draw `ostringstream` cache-key construction in the hot path.

---

## E. Layout engine (sound core, sharp edges at the UI seam)

**E1. `dimPx` collapses every non-Px unit to 0 across the UI geometry API** (`UltraCanvasUIElement.h:96-98`). The engine resolves `%`/`em`/`vw`/… correctly during layout, but `GetContentRect`, margin/padding/border getters, scrollbar geometry, scroll range and wheel hit-testing all read 0 for non-Px values and disagree with the engine (`UltraCanvasContainer.cpp:147-148,196-199,244-248,266-267,430-431`). `GetOriginalSize()` on a `Pct(100)`-sized element returns `{0,0}`. The engine advertises 11 units; the UI layer round-trips one. Resolve via the last `LayoutContext`, or document non-Px box values as unsupported on UI elements.

**E2. One-way invalidation trap.** `InvalidateLayout` only ascends; `InvalidateSubtree` only descends; the window gates the geometry pass on the *root's* `arrangeValid`. Calling `InvalidateSubtree` mid-tree leaves the root valid → no re-layout runs, and descendants' subsequent `InvalidateLayout` calls hit the already-invalid early-out → the tree wedges. `UltraCanvasTabbedContainer.cpp:64-78` contains a manual workaround that documents the trap.

**E3. Two invalidation disciplines on the same data.** `size`, `box`, `layout`, `layoutItem` are public mutable fields, and the chainable `Layout`/`LayoutItem` setters never invalidate (they can't — they don't know the Element). Only the UI-layer setters uphold the contract. `el->layout.SetFlexGap(8)` silently does nothing until something else invalidates.

**E4. Constructor-value-driven positioning mode.** `(x > 0 || y > 0)` in the base constructor flips the element into `AbsoluteUI` positioning (`UltraCanvasUIElement.h:143-154`). `AbsoluteUI` itself is a defensible, honestly-documented compat extension — but the trigger means `("id",0,0,w,h)` and `("id",0,1,w,h)` land in different layout regimes, absolute placement at (0,0) is inexpressible, and `UltraCanvasContainer` must pass sentinel `(-1,-1)` to dodge it. An explicit tag/setter would carry the same compatibility without the hidden mode switch.

**E5. Cache-design nits.** Flex/grid `LayoutComputed` state is keyed by constraints only (missing the `ctxKey` that the measure cache gained after the same staleness bug); the measure cache is single-slot while flex/grid measure each child under up to three constraint sets per pass (thrash on deep auto-sized trees).

---

## F. Widget API consistency & documentation

- **Four coexisting creation idioms** (free-function `CreateX`, static member factory, `UltraCanvasUIElementFactory::Create<>`, two different Builder architectures) with no blessed one; the framework's own apps vote with 216 raw `make_shared` calls vs 39 `CreateButton` and 2 Builder uses.
- **Semantic collisions on shared names:** single-string constructor means *text* on Button/Label/Checkbox but *identifier* on TextInput/Dropdown/Menu/TreeView; `SetMargin(vertical, horizontal)` vs `SetPadding(horizontal, vertical)` (`UltraCanvasUIElement.h:243,264`); `onClick` is `void()` on Button/Label but `void(const UCEvent&)` on Slider; `SetColors(...)` means four different tuples on four widgets.
- **Naming drift:** `Is*` vs `Get*` for booleans; `onSelectionChanged`/`onItemSelected`/`onNodeSelected`/`onTabChange`+`onTabSelect` for the same concept; three different veto-callback conventions.
- **TabbedContainer exposes its entire state as public data members** mirrored by setters — two write paths, one of which bypasses invalidation.
- **No theme system.** Per-widget style structs with named presets is a coherent idiom, but there's no application-wide theme or dark-mode switch, and "where does a border live" differs per widget (Label's style excludes background/border, Button's still embeds them).
- **Documentation is systematically stale.** All four spot-checked component docs describe a removed constructor generation (`identifier, long id, x, y, ...`), phantom factories (`CreateAutoButton`, `DropdownBuilder`), and renamed style fields. The developer guides (coordinate system, CSSLayout contract, bitmap architecture) are accurate and unusually good; the per-widget examples should be regenerated or clearly marked untrusted.
- **Header hygiene:** `UltraCanvasTabbedContainer.h` pulls in AutoComplete + Menu + Button; `<regex>` included in `TextInput.h` but used only in the .cpp; commented-out code blocks shipped in public headers; `UCEvent::IsWindowEvent()`-style enum-order-fragile range checks (excludes `WindowCloseRequest`/`WindowRepaint`; `IsMouseEvent` excludes Enter/Leave/horizontal wheel).

---

## G. What the design does well (confirmations)

1. Single element tree; no duplicated hierarchy between layout engine and UI layer.
2. Clean engine layering: `core/CSSLayout` has zero widget/window/render knowledge; widgets integrate through exactly two virtuals with a well-written contract doc.
3. Element-local rendering + local dirty rects + pre-mapped local event coordinates — consistent between docs and code; the "don't do" documentation is honest and concrete.
4. Retained per-layer surfaces (window/popups/tooltips) with independent dirty queues; popup close = recomposite, not repaint.
5. Blocking, wake-able event loop implemented three times with matching semantics and correct timer-timeout integration; thread-safe inbound event queue.
6. Clipboard and native dialogs are abstracted *properly* (interface + per-OS backend selected in one .cpp; no platform types leak) — proof the team knows how to build the clean boundary that the event/window layer lacks.
7. HiDPI concept: logical units everywhere, `GetDeviceScale()` documented as a resource-selection hint; Pango resolution pinning; bundled-font strategy unified across Fontconfig/GDI/CoreText.
8. Separate fill/stroke/text paint slots (avoids the HTML-canvas single-current-color footgun); pattern lifetimes tied to `shared_ptr`.
9. Real platform craftsmanship where implemented: XIM UTF-8 input, Win32 surrogate-pair + AltGr handling, per-monitor-v2 DPI, OLE/XDnD drag-drop.
10. Widget-level uniformity in the *shape* of the API: consistent `(id, x, y, w, h)` constructor families, lowercase `on*` `std::function` members, identical `OnEvent`/`Render` contracts, `UltraCanvasLabeledToggleBase` factoring, style-struct + preset idiom.

---

## H. Recommendations (priority order)

1. **Fix the outright defects (section B).** B1, B2, B3, B5, B6, B8 are cheap, user-visible wins; B9–B13 need small design decisions first.
2. **Close the lifetime gaps without re-architecting:** make `UCEvent::targetElement`, `_focusedElement`, and the app-level hovered/captured/dragged pointers `weak_ptr<UltraCanvasUIElement>` (the class already inherits `enable_shared_from_this`); forbid event dispatch/redraw from destructor paths (cleanup hooks only null pointers); make `OpenPopup` take a `shared_ptr`; guard `ClosePopup` with a local `shared_ptr` for the duration of the call.
3. **Make the platform story honest:** delete or quarantine `OS/BSD` and `OS/WASM` (plus the iOS/Android references) until re-based on the current API; fix the `__WASM__`/`OS/Web` selection macros when WASM returns; document the real matrix.
4. **Document and enforce the threading contract:** "UI thread only, except `PushEvent`/timers"; fix `ProcessTimers` locking; either make `RequestRedraw` thread-safe + loop-waking or assert it's called on the UI thread; consider a `RunOnUIThread(fn)` helper (trivially built on `PushEvent`).
5. **De-leak the public headers:** move `<X11/Xlib.h>`/`<windows.h>` out of `UltraCanvasCommonTypes.h` (opaque handle type), remove `cairo/pango` includes from `UltraCanvasRenderContext.h` (the Pango factory hook can live in a backend-specific header). Keeping X11 keysym *values* for `UCKeys` is defensible as an ABI choice, but say so in the header, and route all key translation through shared tables.
6. **Harden the render contract:** RAII state guard (`ScopedRenderState`) + balance assertion per `Render()` call; split a restricted element-facing paint interface from the surface-owner interface (or at least document `ClearClipRect`/`ResetTransform` as compositor-only); add a region parameter to `FlushToSurface`.
7. **Bless one widget-creation idiom** (the evidence says: constructors + `make_shared`, keep free-function factories as sugar, retire the per-widget Builders or generate them uniformly); align `SetMargin`/`SetPadding` argument order (breaking, but it's a live footgun); unify the single-string constructor semantics.
8. **Regenerate the per-widget docs from the current headers** — they are a full API generation behind and actively misleading.
9. **Layout-engine follow-ups:** implement block-flow margins (B12); make `InvalidateSubtree` bubble up (E2); decide the non-Px story for the UI getters (E1); replace the `(x>0||y>0)` constructor heuristic with an explicit tag (E4); compute damage from old∪new bounds after Arrange instead of whole-window redraw per invalidation bubble (D3/m3).

---

*Review produced by automated multi-agent analysis of the repository at commit `13a91ef`; every finding was verified against the source files cited.*
