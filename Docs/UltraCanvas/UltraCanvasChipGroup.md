# UltraCanvas Chip Group Documentation

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Selection rules](#selection-rules)
- [Key API](#key-api)
- [Notes](#notes)

## Overview

`UltraCanvasChipGroup` holds a row of chips with one selection rule: **single**
(one chip at a time - a row of categories, a filter bar) or **multiple** (any
number - a set of tags to match). The chips are real `UltraCanvasChip` children
laid out in a wrapping flex row, so each keeps its own hover, focus, keyboard
(Space / Return) and theming; the group decides what a click means for the
others and tells the caller what is selected now.

**File Location**: `include/UltraCanvasChipGroup.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

---

## Quick Start
```cpp
#include "UltraCanvasChipGroup.h"
using namespace UltraCanvas;

auto group = CreateChipGroup("categories");
group->AddChip("All");
group->AddChip("Video", "media/icons/film.svg");
group->AddChip("Audio");
group->SetSelectedIndex(0);
group->onSelectionChanged = [](int index) {
    // -1 when nothing is selected (only with SetAllowNoSelection(true))
};
container->AddChild(group);
```

The group is a container: it takes the width its parent gives it and grows in
height as the chips wrap onto further rows.

## Selection rules

| Mode | A click on an unselected chip | A click on the selected chip |
|---|---|---|
| `Single` (default) | selects it and deselects the other | keeps it selected; with `SetAllowNoSelection(true)` deselects it, leaving none |
| `Multiple` | adds it | removes it |

`onSelectionChanged(int)` fires with the selected index (-1 for none) and is
meant for single mode; `onSelectedIndicesChanged(const std::vector<int>&)`
fires in either mode with every selected index, ascending. Both fire for a
click and for `SetSelectedIndex` / `SetSelectedIndices` / `ClearSelection`
when the selection changed.

## Key API
```cpp
int  AddChip(const std::string& label, const std::string& iconPath = "", const std::string& chipId = "");
int  AddChip(std::shared_ptr<UltraCanvasChip> chip);
void RemoveChip(int index);
void ClearChips();
int  GetChipCount() const;
std::shared_ptr<UltraCanvasChip> GetChip(int index) const;
int  IndexOf(const std::string& label) const;

void SetSelectionMode(ChipGroupSelectionMode mode);
void SetAllowNoSelection(bool allow);
void SetSelectedIndex(int index);
int  GetSelectedIndex() const;
void SetSelectedIndices(const std::vector<int>& indices);
std::vector<int> GetSelectedIndices() const;
bool IsSelected(int index) const;
void ClearSelection();

void SetChipStyle(const ChipStyle& style);
void SetChipVariant(ChipVariant variant);
void SetSpacing(float spacing);
```

```cpp
std::function<void(int index)> onSelectionChanged;
std::function<void(const std::vector<int>&)> onSelectedIndicesChanged;
```

## Notes

- A chip the caller adds with `AddChip(std::shared_ptr<UltraCanvasChip>)` is
  made selectable and auto-width here; its own `onSelectedChanged` is taken
  over by the group.
- Styling goes through `SetChipStyle` / `SetChipVariant`, applied to every
  chip now and later; the chips' own `ChipStyle` fields say what can be set.
- For a few mutually exclusive options that should look like one control,
  `UltraCanvasSegmentedControl` is the element; the chip group is for
  categories and tags that wrap and can be many.
