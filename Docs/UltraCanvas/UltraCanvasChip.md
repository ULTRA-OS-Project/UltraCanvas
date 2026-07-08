# UltraCanvas Chip & Tag Input Documentation

## Overview

`UltraCanvasChip.h` provides two related controls:

- **UltraCanvasChip** — a single compact "chip": a pill with a label, an optional
  leading icon (vector/image), an optional remove (×) button, and an optional
  selectable state (filter / choice chips). Filled or outlined.
- **UltraCanvasTagInput** — a token field: type text and press **Enter** (or a
  comma) to add a removable chip; click **×** or press **Backspace** on an empty
  field to remove one. Chips **wrap across rows** and the field grows to fit.

Both are self-rendered (no child widgets) and follow the standard element
conventions.

**File Location**: `include/UltraCanvasChip.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

---

## UltraCanvasChip

### Features
- ✅ Filled or outlined variant
- ✅ Optional leading **icon** (SVG/image via `UCImage::Get`)
- ✅ Optional **remove (×)** button (`onClose`)
- ✅ Optional **selectable** state for filter/choice chips (`onSelectedChanged`)
- ✅ Hover / pressed / selected / disabled states
- ✅ **Auto-sizes its width** to the content (icon + label + close)

### Quick Start
```cpp
#include "UltraCanvasChip.h"
using namespace UltraCanvas;

// Basic + closable chip
auto c = CreateChip("c1", 20, 20, "Removable", /*closable*/ true);
c->onClose = [c]{ /* remove it from your model / view */ };

// Outlined
c->SetVariant(ChipVariant::Outlined);

// Leading icon
c->SetIcon("media/icons/tag.svg");

// Filter chip (toggle selection)
auto f = CreateFilterChip("f1", 20, 60, "Engineering");
f->onSelectedChanged = [](bool on){ applyFilter(on); };
```

### Key API
```cpp
void SetLabel(const std::string&);
void SetIcon(const std::string& path);
void SetClosable(bool);
void SetSelectable(bool);
void SetSelected(bool);
void SetVariant(ChipVariant);          // Filled | Outlined
float GetPreferredWidth(IRenderContext*) const;

std::function<void()> onClick;
std::function<void()> onClose;
std::function<void(bool)> onSelectedChanged;
```

The chip sizes its own width to its content by default (`SetAutoWidth(false)` to
size it yourself).

---

## UltraCanvasTagInput

### Features
- ✅ Add tags by typing + **Enter** or **comma**
- ✅ Remove via each chip's **×**, or **Backspace** on the empty field
- ✅ Chips **wrap across multiple rows**; the field **auto-grows** its height
- ✅ **Max-tag** limit and optional **no-duplicates**
- ✅ Custom **validator** to accept/reject candidate tags
- ✅ Placeholder text; `onTagsChanged` / `onTagAdded` / `onTagRemoved`

### Quick Start
```cpp
auto tags = CreateTagInput("tags", 20, 20, 620, 40);
tags->SetTags({"c++", "ultracanvas", "ui"});
tags->SetPlaceholder("Add a tag…");
tags->SetMaxTags(10);
tags->SetAllowDuplicates(false);
tags->validator = [](const std::string& t){ return t.size() <= 20; };
tags->onTagsChanged = [](const std::vector<std::string>& t){ save(t); };
```

### Key API
```cpp
bool AddTag(const std::string& tag, bool runCallback = true);
void RemoveTag(int index, bool runCallback = true);
bool RemoveTag(const std::string& tag, bool runCallback = true);
void ClearTags(bool runCallback = true);
void SetTags(const std::vector<std::string>&);
const std::vector<std::string>& GetTags() const;
bool HasTag(const std::string&) const;

void SetPlaceholder(const std::string&);
void SetAllowDuplicates(bool);
void SetMaxTags(int);                   // 0 = unlimited
void SetAutoHeight(bool);               // grow to fit rows (default true)
const std::string& GetInputText() const;

std::function<void(const std::vector<std::string>&)> onTagsChanged;
std::function<void(const std::string&)> onTagAdded;
std::function<void(const std::string&)> onTagRemoved;
std::function<bool(const std::string&)> validator;   // return false to reject
```

### Keyboard
| Key | Action |
|-----|--------|
| printable | Type into the inline editor |
| `Enter` / `,` | Commit the typed text as a tag |
| `Backspace` (text present) | Delete a character |
| `Backspace` (empty field) | Remove the last tag |

## Notes

- `AddTag` trims surrounding whitespace and returns `false` if the tag is empty,
  a duplicate (when duplicates are disallowed), over the `maxTags` limit, or
  rejected by the `validator`. A rejected commit keeps the typed text so the user
  can correct it.
- The layout (chip rects, close-button hit areas, input rect) is computed in
  `Render` with real text measurement and reused for hit-testing.
- For transient, non-editable label pills, use `UltraCanvasChip` directly; for
  free-form multi-value entry, use `UltraCanvasTagInput`.
