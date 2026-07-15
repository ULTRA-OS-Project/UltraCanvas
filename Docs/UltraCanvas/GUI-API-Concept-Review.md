# UltraCanvas GUI API — Concept Review (Remaining Problems)

**Original review:** 2026-07-02 (commit `13a91ef`)
**Rechecked:** 2026-07-15 against current `main` (commit `80699fd`). Every finding of the original review was re-verified against the current sources. **Items confirmed fixed have been removed from this document**; what follows is only what remains. Line numbers refer to the current code.

**Verified fixed since the original review** (removed from this doc): queued-event `targetElement` scrubbing + null-guarded dispatch; destruction-order UB in the element/window teardown chain; event dispatch into destructors on focus teardown; `ClosePopup` use-after-free ordering; focus/blur dispatch bug on click-to-focus; `ProcessTimers` data race and dangling reference; radio menu items not firing; TreeView `backgroundColor` shadowing; `GetScreenPosition` uninitialized read (API removed); `IsKeyPressed` undefined / `keyStates` uninitialized; `StrokeText` using fill paint; `Arrange`/`SetBounds` invalidation ignoring scroll offset; scrollbar self-invalidation frame; `position:Fixed` double-offset; `MapFromLocal`/`MapToLocal` off-by-one-level; `SetMargin`/`SetPadding` argument-order mismatch; `UCKeys` redefined as a portable virtual-key enum (no longer X11 keysyms); `InvalidateLayout` no longer wedges (always bubbles); `PostToUIThread` helper added with event-loop wakeup; `MeasureResult` cache now context-keyed.

---

## Verdict (unchanged in substance)

The core concept remains **confirmed**: shared base + thin native layer, a spec-shaped two-phase CSS layout engine over a single element tree, element-local rendering with retained per-layer surfaces, and a blocking wake-able event loop. The recent fixes removed the entire memory-safety family that was the review's biggest concern. The remaining problems concentrate in: (1) the rendering abstraction's Cairo/Pango coupling and composition cost, (2) platform parity and dead platform code, (3) the non-Px unit seam between the layout engine and the UI getters, and (4) widget-API consistency and stale docs.

---

## A. Ownership & lifetime — remaining items

**A1. `CloseAllPopups` — iterator invalidation UB and leaked vetoed popups.**
`UltraCanvasWindow.cpp:556-564` reverse-iterates `popupElements` while calling `ClosePopup`, which erases from the same container mid-iteration — undefined behavior. It then ends with an unconditional `popupElements.clear()`, dropping entries whose `OnPopupAboutToClose` vetoed the close without cleanup (their `isPopup` flag, child link, and render context are never cleared).

**A2. `UnInstallWindowEventFilter` never removes anything.**
`UltraCanvasWindow.cpp:214-226`: the guard is inverted (`if (eventFilters.empty())`) and the erase operates on a copy (`auto funcs = ef.second;`). Leaked filters are a residual vector for callbacks capturing dead elements.

**A3. Hiding a container leaves a hidden descendant focused and receiving keys.**
`SetVisible(false)` clears focus only if the element *itself* is focused (`UltraCanvasUIElement.cpp:264-276`); a focused child inside the hidden subtree keeps `_focusedElement`, and keyboard dispatch (`UltraCanvasApplication.cpp:806-827`) has no visibility check.

**A4. `OpenPopup`/`ClosePopup` `shared_from_this` footgun.**
The UAF ordering was fixed (commit `0ca288f`), but both still take `UltraCanvasUIElement&` and call `elem.shared_from_this()` (`UltraCanvasWindow.cpp:496,504,534`) — a stack- or `unique_ptr`-owned element throws `bad_weak_ptr`. Taking `shared_ptr` in the signature would make the precondition explicit.

**A5. Event-queue scrub races if producers run off-thread.**
The new `CleanupElementReferences` scrub (`UltraCanvasApplication.cpp:394-404`) iterates `eventQueue` without taking `eventQueueMutex`, while `PushEvent` locks it — inconsistent locking on a queue documented as a cross-thread channel.

---

## B. Rendering — remaining items

**B1. `IRenderContext` is still hard-wired to Cairo/Pango.**
The abstract header still includes `<cairo/cairo.h>` and `<pango/pangocairo.h>` (`UltraCanvasRenderContext.h:10-11`), the public factory still exposes `CreateFontDescFromPango(const PangoFontDescription*)` (`:589`), and `TextAttributeType` (`:489-528`) mirrors Pango 1:1. A second (non-Cairo) backend cannot be implemented against this interface, and every client TU compiles against Cairo/Pango. (The Cairo context moving to `libspecific/Cairo/` is a good structural step; the header coupling is the remaining problem.)

**B2. No enforcement of the state-stack invariant; escape hatches break the render contract.**
`PopState()` on an empty shadow stack now logs, but still calls `cairo_restore` unconditionally (`libspecific/Cairo/RenderContextCairo.cpp:406-414`) — one unbalanced pop puts the `cairo_t` into permanent error state and silently blanks the window. `ClearClipRect`/`ResetTransform`/`SetTransform`/`ResetState` remain public on the element-facing interface (`UltraCanvasRenderContext.h:167-181`); `ClearClipRect` = `cairo_reset_clip` (cpp:464-470) discards the window's dirty-region clip and all container clips. Recommended: a RAII `ScopedRenderState` + balance assertion around `Render()`, and a restricted element-facing paint facade (or documenting these as compositor-only).

**B3. Composition still throws away what dirty-rects saved.**
`FlushToSurface` has no region parameter and paints the entire backbuffer with `CAIRO_OPERATOR_SOURCE` (cpp:1846-1864); any dirty rect sets `_needsWindowComposition`, triggering a full-window flush plus re-flush of every popup surface plus a fresh tooltip render each frame (`UltraCanvasWindow.cpp:403-466`). Compounding it, `UltraCanvasContainer::InvalidateLayout()` still calls `RequestRedraw()` at every bubble step (`UltraCanvasContainer.h:141-145`), so any layout-affecting property set dirties the whole window. Damage is already computed from old∪new bounds after Arrange — the eager per-bubble redraw is redundant.

**B4. Text-layout cache defects.**
`libspecific/Cairo/RenderContextCairo.cpp`: cache key still ends with `text.substr(0,300)` (:145) — two texts identical in their first 300 bytes collide and render as each other; the key omits `letterSpacing`; `lineHeight` is in the key but never applied ("Fixme! Need to set line height", :741), so `SetTextLineHeight` is a silent no-op for `DrawText`; and the cache returns mutable `shared_ptr<ITextLayout>` (:720-760) — any caller mutating a cached layout corrupts it for every other user.

**B5. Rendering minor items (all unchanged):**
- `SetTransform`/`Transform` don't update the tracked `currentState.translation/rotation/scale` while `Translate/Rotate/Scale` do (cpp:426-455) — the shadow state is untrustworthy.
- `SetAlpha`/global alpha has three different behaviors depending on code path (cpp:1536-1544 vs :108-123 vs :957-963); no real group-opacity concept.
- Two text pipelines with different anchoring: baseline-anchored cairo toy-font `FillText`/`StrokeText` (cpp:1581-1602) vs top-left Pango `DrawText` (:774-800).
- 1px clip-inflation hack compensating float→int truncation (`UltraCanvasContainer.cpp:96-97`); rect-type proliferation (`Rect2Dd`/`Rect2Df`/`Rect2Di`) persists across the render path (scrollbar signature was aligned to `Rect2Df` — partial improvement).
- `DrawRoundedRectangleWidthBorders`: 18 parameters, "Width" typo intact (`UltraCanvasRenderContext.h:196-207`).
- Dead `HandleScrollWheel` in container — zero callers, gates on the wrong condition (`UltraCanvasContainer.cpp:263-279`).
- Per-text-draw `ostringstream` key construction + mutex-guarded cache lookup in the hot path (cpp:125-148, `UltraCanvasUtils.h:134-136`).
- `g_Instances` static registry unsynchronized (cpp:47,264,274); `ClearClipRect` logs every call (:465); `DrawText` applies the text source twice (:781 + :767).

---

## C. Platform abstraction — remaining items

**C1. Platform headers still leak into every TU.**
`UCKeys` is fixed (portable enum), but `UltraCanvasCommonTypes.h:15-44` still includes `<windows.h>`/`<X11/Xlib.h>` into every framework and client TU (with 7 `#undef`s of Win32 macros), and `UCEvent` still carries `nativeWindowHandle` behind an `#ifdef` ladder (`UltraCanvasEvent.h:328-336`). An opaque handle type would remove the leak.

**C2. Event semantics still diverge per platform.**
- Windows emits **two** `KeyDown` per character keystroke (WM_KEYDOWN, then WM_CHAR with `virtualKey = Unknown`) (`OS/MSWindows/UltraCanvasWindowsApplication.cpp:374-383,422-428`); Linux emits one with both key and text. The `TextInput`/`KeyChar` event types are still produced by no backend.
- macOS: no `MouseDoubleClick` (clickCount never inspected), no `MouseEnter`/`MouseLeave` (no NSTrackingArea), no horizontal wheel (`OS/MacOS/UltraCanvasMacOSApplication.mm:375-438`); wheel delta is raw truncated `scrollingDeltaY` (:402) vs ±5-per-notch on Linux/Windows — trackpad deltas truncate to 0; mouse capture is a no-op stub (:610-634); no drag-and-drop implementation at all despite `DragStart..Drop` being public API.

**C3. BSD/WASM dead code is still unreachable and non-compiling; iOS/Android still phantom.**
`OS/BSD` and `OS/WASM` still override the removed virtual `RunNative()`; the selection macros at the bottom of `UltraCanvasApplication.h`/`UltraCanvasWindow.h` still test `__WASM__` (Emscripten defines `__EMSCRIPTEN__`) and include nonexistent `../OS/Web/...` headers; `defined(__unix__)` still selects the Linux implementation (with `<sys/eventfd.h>`) on BSD; `OS/iOS`/`OS/Android` are referenced but do not exist. Recommend deleting or quarantining until re-based on the current API, and documenting the real matrix (Linux/Windows/macOS).

**C4. Threading contract — residuals.**
`PostToUIThread` (with loop wakeup) is a solid addition. Still open: `bool volatile running/initialized` instead of `std::atomic` (`UltraCanvasApplication.h:55-56`); the dirty-rect path (`RequestRedraw` → `DirtyRectManager::Add`) remains unsynchronized and does not wake the loop — safe only on the UI thread, which is still undocumented; the dead Linux `StartEventThread`/`EventThreadFunction` (never called, unsafe if ever enabled) is still present (`OS/Linux/UltraCanvasLinuxApplication.cpp:818-892`).

**C5. Process-global singleton.**
Per-platform `static instance` assigned unguarded in the constructor, never cleared (`OS/Linux/UltraCanvasLinuxApplication.cpp:24,37`); `GetInstance()` hardwired across ~25 core files. The new `GetCurrent()` documents the single-app assumption rather than removing it. Consequences unchanged: one app per process, init-order dependency, widget tests need a live display.

**C6. Platform minor items (all unchanged):**
- Linux `GetClipboardText()` dead stub returns `""` while the real `UltraCanvasLinuxClipboard` backend exists (`OS/Linux/UltraCanvasLinuxApplication.cpp:895-902`).
- `XIOErrorHandler` calls `exit(1)` on X connection loss (:913-916) — no orderly shutdown.
- Fragile event classifiers: `IsMouseEvent` still omits Enter/Leave/horizontal wheel; `IsWindowEvent` is still an enum-order range check excluding `WindowCloseRequest`/`WindowRepaint` (`UltraCanvasEvent.h:352-380`).
- `windowClassSuffix` (Windows-only) still in the cross-platform `WindowConfig` (now at least documented).
- Linux rejects windows larger than 4096px (`OS/Linux/UltraCanvasLinuxWindow.cpp:113-117`) — fails on 5K/6K monitors.
- Dead `DOUBLE_CLICK_TIME = 0` / `DOUBLE_CLICK_DISTANCE = 0` constants (`UltraCanvasApplication.h:107-108`).

---

## D. Layout engine — remaining items

**D1. `dimPx` collapses every non-Px unit to 0 across the UI geometry API.**
Unchanged (`UltraCanvasUIElement.h:96-98`): `GetContentRect`, `GetOriginalSize`, all margin/padding/border getters return 0 for `%`/`em`/`vw`/… values that the engine resolves correctly during layout, and container scrollbar placement, scroll range and wheel hit-testing consume these getters (`UltraCanvasContainer.cpp:115-116,168-169,223-235,265-266,287-288,451-452`). Either resolve via the last `LayoutContext` or document non-Px box values as unsupported on UI elements.

**D2. `InvalidateSubtree` still doesn't reach the window's geometry gate.**
The permanent-wedge is fixed (`InvalidateLayout` now always bubbles, `Element.cpp:171-188`), but `InvalidateSubtree` remains downward-only (`Element.cpp:190-196`) while the window gates the geometry pass on the root's `arrangeValid` (`UltraCanvasWindow.cpp:383-400`) — a bare mid-tree `InvalidateSubtree` still triggers no re-layout; the TabbedContainer workaround is still needed (`UltraCanvasTabbedContainer.cpp:63-80`).

**D3. Two invalidation disciplines on the same data.**
`size`, `box`, `layout`, `layoutItem` remain public mutable (`CSSLayout.h:346-367`) and the chainable `Layout`/`LayoutItem` setters cannot invalidate (no back-pointer, `:273-331`). `el->layout.SetFlexGap(8)` still silently does nothing until something else invalidates.

**D4. Constructor-value-driven positioning mode.**
`(x > 0 || y > 0)` still stamps `AbsoluteUI` (`UltraCanvasUIElement.h:136-156`). Now well-commented, but the hidden mode switch stands: `(id,0,0,w,h)` and `(id,0,1,w,h)` land in different layout regimes, and absolute placement at (0,0) is inexpressible via the constructor.

**D5. Cache-design nits.**
Flex/grid `LayoutComputed` state is still keyed by constraints only — no `ctxKey` (`FlexLayout.cpp:544-545`, `GridLayout.cpp:528-529`), repeating the staleness bug the measure cache already fixed; the measure cache remains single-slot (`CSSLayout.h:208-215`) and thrashes on multi-constraint passes.

**D6. Block layout still ignores margins entirely.**
`MeasureBlock`/`ArrangeBlock` stack children by `measuredHeight` with zero margin handling (`Element.cpp:315-323,401-425`) while flex/grid resolve margins correctly. `SetMargin()` in the default block parent is a silent no-op that still invalidates layout. At minimum, document it alongside the existing "no margin collapsing" note in `Docs/CSSLayout.md`.

---

## E. Widget API & documentation — remaining items

**E1. Four coexisting creation idioms, none blessed.**
Free-function `CreateX` factories, static `Checkbox::CreateCheckbox`, the `UltraCanvasUIElementFactory::Create<>` template, and two incompatible Builder architectures (wrap-live `ButtonBuilder` vs accumulate-then-build `TextInputBuilder`) all coexist; TreeView's factory is still commented out. The framework's own apps overwhelmingly use raw `make_shared` — consider blessing that (+ constructors), keeping factories as sugar, and retiring or regularizing the Builders.

**E2. Single-string constructor semantics (partially fixed).**
Button's text-only constructor was removed (commit `80699fd`) — good. `UltraCanvasLabel(const std::string&)` (`Label.h:86-87`) and `UltraCanvasCheckbox` (`Checkbox.h:79-80`) still treat the single string as **text** while TextInput/Dropdown/Menu/TreeView treat it as the **identifier**.

**E3. Callback API inconsistencies.**
`onClick` is `void()` on Button/Label but `void(const UCEvent&)` on Slider (`Slider.h:301`); veto conventions vary (`onTabClose bool(int)`, `onClosing bool(DialogResult)`, `onPopupAboutToClose bool(ClosePopupReason)`, `onTabDragIn` int/-1); selection callbacks drift (`onSelectionChanged` / `onItemSelected` / `onNodeSelected` / `onTabChange`+`onTabSelect`).

**E4. Naming & semantic collisions.**
`Is*` vs `Get*` for booleans (`GetShowExpandButtons` vs `IsReadOnly`, hybrid `IsShowPlaceholderAlways`); `SetColors(...)` means four different tuples on Button/Checkbox/Slider/TreeViewBuilder; `GetZOrder()`/`GetZIndex()` duplicates; `SetOnClick()` setter coexisting with the public `onClick` member; `SetSize`/`SetPosition` kept with a "don't use" comment.

**E5. TabbedContainer state exposure.**
Entire state (tabs vector, indices, 20+ colors, drag state) is public *and* mirrored by setters; several setters (`SetNewTabButtonWidth`, `SetInactiveTabBackgroundColor`, `SetActiveTabBackgroundColor`, `SetInactiveTabTextColor`, `SetNewButtonColor`, `TabbedContainer.h:252-256`) skip `InvalidateTabbar()` while neighbors invalidate.

**E6. Menu `Input`/`Custom` half-removed.**
The inline `Input()` factory bodies are commented out, but the enum values and the *declared* factories remain (`Menu.h:59-60,140-141`) — calling them is now a linker error rather than a silent no-op. Remove the declarations and enum values (or implement them).

**E7. Header hygiene.**
`UltraCanvasMenu.h` still ~767 lines with heavy inline bodies; `TextInput.h` includes `<regex>` unused in the header; `TabbedContainer.h` still pulls in AutoComplete/Menu/Button; commented-out code blocks persist in public headers (TreeView factory, Menu state API, Menu Input factories, TextInput factory). TextInput factory param types still mix `int`/`float`/`long` (`TextInput.h:639-696`). TreeView's three ~40-line constructors are still copy-paste rather than delegating (`core/UltraCanvasTreeView.cpp:157-275`). Dead `prevDisplayType` member on the base element (`UltraCanvasUIElement.h:107`).

**E8. Per-widget docs are still a full API generation behind.**
`UltraCanvasButtonExamples.md`, `UltraCanvasLabelExamples.md`, `UltraCanvasCheckbox.md`, `UltraCanvasDropDownExamples.md` still document the removed `long id` constructor generation, phantom `CreateAutoButton`/`DropdownBuilder`, and renamed style fields. The developer guides (coordinate system, CSSLayout, bitmap architecture) remain accurate; the per-widget examples actively mislead and should be regenerated or marked untrusted.

**E9. No theme system.**
Per-widget style structs + presets only; no app-wide theme or dark-mode switch; "where does a border live" still differs per widget (LabelStyle excludes background/border, ButtonStyle embeds them).

---

## F. Recommendations (updated priority order)

1. **Small correctness fixes first:** `CloseAllPopups` iterator invalidation + vetoed-entry cleanup (A1); `UnInstallWindowEventFilter` (A2); lock `eventQueueMutex` in the cleanup scrub (A5); clear focus from hidden subtrees (A3); text-layout cache key/immutability (B4).
2. **Harden the render contract:** RAII state guard + balance assertion per `Render()`; restrict or document `ClearClipRect`/`ResetTransform` as compositor-only (B2); add a region parameter to `FlushToSurface` and drop the per-bubble `RequestRedraw` in `InvalidateLayout` (B3).
3. **De-leak the public headers:** opaque native-handle type to remove `<windows.h>`/`<X11/Xlib.h>` from `UltraCanvasCommonTypes.h` (C1); move Cairo/Pango includes and the Pango factory hook out of `UltraCanvasRenderContext.h` (B1).
4. **Make the platform story honest:** delete/quarantine `OS/BSD`, `OS/WASM`, and the iOS/Android/Web references; fix or remove the selection macros (C3). Close the macOS interaction gaps (double-click, enter/leave, wheel scale, capture, DnD) before claiming parity (C2).
5. **Finish the threading contract:** atomics for `running`/`initialized`; document "UI thread only, except `PushEvent`/timers/`PostToUIThread`"; remove the dead Linux event-thread code (C4).
6. **Layout seam:** decide the non-Px story for the UI getters (D1); make `InvalidateSubtree` bubble or assert root-only (D2); route the chainable layout setters through invalidation or make the fields non-public (D3); replace the `(x>0||y>0)` heuristic with an explicit tag (D4); implement block-flow margins or document their absence (D6).
7. **Widget API consolidation:** bless one creation idiom (E1); unify the remaining single-string ctor semantics on Label/Checkbox (E2); align `onClick` signatures and veto conventions (E3); remove the half-deleted Menu Input/Custom API (E6).
8. **Regenerate the four stale per-widget docs from current headers** (E8).

---

*Recheck produced by automated multi-agent re-verification of every original finding against the current sources; fixed items were confirmed with code evidence before removal from this document.*
