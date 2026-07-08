# UltraCanvas Pagination Documentation

## Overview

**UltraCanvasPagination** is a page-navigation strip for lists, tables and
search results. It renders First/Prev controls, a windowed run of page numbers
with ellipsis (…) gaps for large page counts, Next/Last controls, and an
optional "page info" readout.

**File Location**: `include/UltraCanvasPagination.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

- ✅ **Numbered / Compact / Simple** modes
- ✅ **Ellipsis windowing** — `boundaryCount` pages at each end, `siblingCount`
  pages either side of the current page, ellipses fill the gaps
- ✅ Drive from a **page count**, or from **total items + page size**
- ✅ First / Prev / Next / Last controls (individually toggleable)
- ✅ Optional **page info** (`Page X of Y`, or `1–10 of 95` when items are known)
- ✅ Custom **info formatter** callback
- ✅ Keyboard navigation (Left / Right / Home / End)
- ✅ Hover / pressed / disabled cell states and full styling
- ✅ `onPageChanged` callback; item-window accessors for the current page

## Modes (`PaginationMode`)

| Mode | Renders |
|------|---------|
| `Numbered` | `«  ‹  1 … 4 [5] 6 … 20  ›  »` — full page-number strip |
| `Compact`  | `‹  Page 5 of 20  ›` — prev / info / next |
| `Simple`   | `‹ Prev            Next ›` — prev / next only |

## Quick Start

```cpp
#include "UltraCanvasPagination.h"
using namespace UltraCanvas;

// From a page count
auto pg = CreatePagination("pager", 20, 20, 600, 34, /*pageCount*/ 20, /*current*/ 1);
pg->onPageChanged = [](int page) { loadPage(page); };

// From an item count (adds a "1–10 of 95" readout)
auto pg2 = CreatePaginationForItems("pager2", 20, 60, 640, 34,
                                    /*totalItems*/ 95, /*pageSize*/ 10);
pg2->onPageChanged = [pg2](int) {
    int from = pg2->GetPageStartItem();   // 1, 11, 21, ...
    int to   = pg2->GetPageEndItem();     // 10, 20, ..., 95
    fetchRange(from, to);
};

// Compact / simple
auto compact = CreatePagination("c", 20, 100, 260, 34, 12, 3);
compact->SetMode(PaginationMode::Compact);
```

## Windowing

The visible page numbers follow the boundary/sibling algorithm used by
Material UI's `usePagination`:

```
boundaryCount = 1, siblingCount = 1, 20 pages:
  page 1   ->  1 2 3 4 5 … 20
  page 5   ->  1 … 4 5 6 … 20
  page 10  ->  1 … 9 10 11 … 20
  page 20  ->  1 … 16 17 18 19 20

siblingCount = 2, 30 pages, page 15:
  1 … 13 14 15 16 17 … 30
```

- `SetBoundaryCount(n)` — pages always shown at each end (min 1).
- `SetSiblingCount(n)` — pages shown either side of the current page (min 0).

## Factory Functions

| Function | Result |
|----------|--------|
| `CreatePagination(id, x, y, w, h, pageCount, currentPage)` | Page-count driven |
| `CreatePaginationForItems(id, x, y, w, h, totalItems, pageSize, currentPage)` | Item-count driven, page info on |

## Key API

```cpp
// Model
void SetPageCount(int count);
void SetTotalItems(int total);     // with SetPageSize -> derives page count
void SetPageSize(int size);
void SetCurrentPage(int page, bool runCallback = true);
int  GetCurrentPage() const;
int  GetPageCount() const;
void FirstPage(); void PrevPage(); void NextPage(); void LastPage();

// Item window for the current page (needs a page size)
int GetPageStartItem() const;      // 1-based, 0 if no page size
int GetPageEndItem() const;

// Windowing
void SetSiblingCount(int n);
void SetBoundaryCount(int n);

// Appearance / features
void SetMode(PaginationMode m);
void SetShowFirstLast(bool);
void SetShowPrevNext(bool);
void SetShowPageInfo(bool);
void SetInfoFormatter(std::function<std::string(int page, int pageCount,
                                                int totalItems, int pageSize)>);
PaginationStyle& GetStyle();
float GetPreferredWidth() const;

// Callback
std::function<void(int page)> onPageChanged;
```

## Keyboard

| Key | Action |
|-----|--------|
| `Left`  | Previous page |
| `Right` | Next page |
| `Home`  | First page |
| `End`   | Last page |

## Notes

- The layout (cell rects) is computed during `Render` and reused for hit-testing,
  so text-measured cells (page info, "Prev"/"Next") stay pixel-accurate.
- Clicking the current page, an ellipsis, or a disabled end control does nothing.
- `GetPreferredWidth()` gives a rough width for the current configuration (square
  cells plus estimated text cells) for use when sizing the control in a layout.
