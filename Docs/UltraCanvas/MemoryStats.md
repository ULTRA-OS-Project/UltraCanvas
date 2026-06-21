# Memory Usage Statistics

UltraCanvas can attribute every tracked allocation to a **(scope, category, type)**
triple so an application can answer two questions at runtime:

| Axis | Question | Example |
|------|----------|---------|
| **Scope** | *Who* owns this memory? | Tab #1, Tab #2, Global |
| **Category** | *What kind* of data is it? | Image pixels, text, geometry, video frames |

This powers things like a browser **task manager** ("how much memory does each tab
use, and what for?").

The feature is controlled by the CMake option `ULTRACANVAS_ENABLE_MEMORY_STATS`
(ON by default), which defines `UC_ENABLE_MEMORY_STATS`. When disabled, the element
factory falls back to `std::make_shared`, the `UC_TRACK_*` macros become no-ops, and
the public read API still compiles but returns empty/zero results — so application
code never needs `#ifdef`s.

## Headers

- `UltraCanvasMemoryStats.h` — categories, scopes, the tracker, the public
  `MemoryStats::` read API, `MemoryScopeGuard`, `TrackedAllocator`, `TrackedBuffer`,
  and the `UC_TRACK_ALLOC` / `UC_TRACK_FREE` macros.
- `UltraCanvasMemoryStatsPanel.h` — a ready-made widget that visualizes the stats.

## Attributing memory

There are three ways memory is attributed to a scope:

### 1. Ambient scope (recommended default)

Set the active scope for a block with `MemoryScopeGuard`. Everything allocated
underneath — through the element factory or the raw hooks — inherits it. The scope
is resolved to a concrete id **eagerly at allocation time**, so the matching free is
attributed to the same scope even if it happens later, under a different scope.

```cpp
MemoryScopeId tab = MemoryStats::CreateScope("Tab: example.com");
{
    MemoryScopeGuard g(tab);
    tab->BuildContent();          // all allocations land in this tab's buckets
}
```

### 2. Explicit factory tag

Override the category and/or pin a specific scope:

```cpp
auto img = UltraCanvasUIElementFactory::CreateTagged<UltraCanvasImageElement>(
    MemoryTag{MemoryCategory::ImagePixels, tab}, "img", 0, 0, 0, 0);
```

### 3. Raw buffers (where the big bytes are)

Pixel buffers, decoded video frames, geometry, PDF data, etc. don't go through the
element factory. Use `TrackedBuffer` (RAII, refunds against the same scope) or the
macros:

```cpp
// RAII — safest for owned buffers whose lifetime crosses scopes:
TrackedBuffer charge(MemoryCategory::ImagePixels, width*height*4, "DecodedBitmap");

// Or manual (pass the scope returned by alloc back to free):
MemoryScopeId s = TrackRawAlloc(MemoryCategory::VideoFrame, frameBytes, "YUVFrame");
TrackRawFree(MemoryCategory::VideoFrame, frameBytes, "YUVFrame", s);
```

## Reading the stats (public API)

`UltraCanvas::MemoryStats::` is the stable, read-only surface for application code.
Safe to call from the UI thread while other threads allocate.

```cpp
auto report = MemoryStats::Snapshot();
for (const auto& tab : report.scopes) {
    printf("%s : %s\n", tab.name.c_str(),
           MemoryStats::FormatBytes(tab.totalBytes).c_str());
    for (const auto& cat : tab.byCategory)        // what it's used for
        printf("    %s : %s (%lld live)\n",
               MemoryStats::CategoryName(cat.category).c_str(),
               MemoryStats::FormatBytes(cat.bytes).c_str(),
               (long long)cat.liveCount);
}
printf("Total: %s  (GPU %s)\n",
       MemoryStats::FormatBytes(report.hostTotalBytes).c_str(),
       MemoryStats::FormatBytes(report.gpuTotalBytes).c_str());
```

Other entry points:

| Function | Use |
|----------|-----|
| `Snapshot()` | Full breakdown (poll on a timer). |
| `BytesForScope(id)` | Allocation-free total for one tab/row. |
| `HostTotalBytes()` / `GpuTotalBytes()` | Whole-process totals. |
| `ListScopes()` | Enumerate live scopes (tabs). |
| `Generation()` | Cheap change counter — only rebuild your UI when it moves. |
| `Subscribe()` / `DispatchPendingNotifications()` | Push notifications, dispatched off the allocation hot path. |
| `CreateScope()` / `DestroyScope()` | Scope lifecycle. `DestroyScope` warns if memory is still live — a per-tab leak detector. |

> GPU memory (`MemoryCategory::GLResource`) is reported on a **separate axis**
> (`gpuBytes` / `GpuTotalBytes`) because it is device memory, not host heap.

## Task-manager dialog

The primary, ready-made way to view memory usage is the dialog
(`UltraCanvasMemoryStatsDialog`, in `UltraCanvas/dialogs/`). It opens as a window —
like a browser's task manager — with a Refresh and a Close button:

```cpp
#include "dialogs/UltraCanvasMemoryStatsDialog.h"

// One-liner: build and show.
UltraCanvasMemoryStatsDialog::Show(parentWindow);

// Or configure first:
MemoryStatsDialogConfig cfg;
cfg.title = "Task Manager";
cfg.showTypeBreakdown = true;          // also list concrete C++ types
auto dlg = CreateMemoryStatsDialog(cfg);
UltraCanvasDialogManager::ShowDialog(dlg, nullptr, parentWindow);
```

The dialog re-snapshots when its window repaints and on the **Refresh** button; an
application that wants live updates can call `dlg->RefreshNow()` on a timer.

### Embeddable panel element

The dialog is built on `UltraCanvasMemoryStatsPanel`, a drawing element you can also
embed directly (e.g. in a sidebar) if you don't want a separate window:

```cpp
auto panel = CreateMemoryStatsPanel("memPanel", 0, 0, 320, 480);
window->AddChild(panel);
```

The panel polls `Generation()`, renders one row per scope with a usage bar and a
category breakdown, and (optionally, via `SetShowTypeBreakdown(true)`) the concrete
C++ types.

## Notes

- Overhead lives behind `UC_ENABLE_MEMORY_STATS`; release builds can disable it.
- Recording is mutex-guarded; `Snapshot()` briefly locks to copy, then sorts outside
  contention — it does not stall allocations.
- The element factory uses `std::allocate_shared` with `TrackedAllocator`, so the
  reported bytes include the shared control block, and the breakdown is keyed by the
  concrete element type.
