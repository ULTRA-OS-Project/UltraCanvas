# UltraCanvasAlbum Documentation

## Overview

**UltraCanvasAlbum** is a self-rendered media gallery widget for displaying
**photo / video / music** albums. It is the grid-shaped companion to
`UltraCanvasSlideshow`: where the slideshow shows one timed image at a time, the
album shows many items at once in a selectable **layout design**, with per-item
crop / zoom / stretch fitting, optional action icons and three viewer modes
(visitor display, owner edit, admin).

Like the slideshow it paints all tiles, captions, badges and action buttons
directly in `Render()` — there are no child UI elements — so a large album stays
cheap and the whole look can be swapped by a single enum.

**Version:** 1.0.0
**Header:** `include/UltraCanvasAlbum.h`
**Source:** `core/UltraCanvasAlbum.cpp`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasContainer`

## Features

- **Six layout designs** — Uniform Grid, Justified, Masonry, Mosaic, Filmstrip, Cards
- **Mixed media** — photo / video / music items with type badges and cover/poster thumbnails
- **Three viewer modes** — `Display` (visitor), `UserEdit` (owner), `Admin`
- **Sizing by items-per-row OR by target image size**
- **Image fit per item** — `Crop` (with focus point), `Zoom`, `Stretch`, `Fit`
- **Action icons** — `AlwaysVisible`, `OnHover`, `ContextMenu` (with or without a kebab icon), `Hidden`; actions are gated per mode
- **Captions** — below the image, overlaid on a scrim, or hidden
- **Appearance** — background colour, item background, border + border colour, corner radius, drop shadow
- **Selection + drag-to-reorder** in edit / admin modes
- **Manual scrolling** — vertical for grid layouts, horizontal for the filmstrip

## Header Include

```cpp
#include "UltraCanvasAlbum.h"
```

## Album Layout Designs (research)

The layout catalogue was drawn from the common photo-album / media-gallery
products and chosen so each design covers a distinct real-world need. They are
exposed through `AlbumLayout`:

| Design | Reference products | Behaviour | Best for |
| --- | --- | --- | --- |
| **UniformGrid** | Google Photos squares, classic CMS galleries | Fixed number of equal cells per row; every tile the same size. | Predictable, dense thumbnail walls. |
| **Justified** | Flickr, Google Photos "comfortable", Lightroom | Rows share a common height; each item keeps its aspect ratio so widths vary and the row's right edge stays flush. The last (short) row is not stretched. | Photo sets with mixed aspect ratios. |
| **Masonry** | Pinterest, Unsplash | Fixed column width; each item flows into the currently shortest column so heights vary and there are no vertical gaps. | Tall/short mixes, scrapbook feel. |
| **Mosaic** | Magazine / "featured cover" front pages | A featured hero spans a 2×2 block, the remaining items fill a 1×1 grid around it. | Album covers with one highlight. |
| **Filmstrip** | Music cover shelves, story reels, "more like this" | A single horizontally-scrolling row. | Carousels, music/video shelves. |
| **Cards** | Album / playlist browsers, store fronts | A uniform grid where every tile is a captioned square card with a frame and shadow. | Catalogues where the title matters as much as the image. |

Sizing is independent of the design and is driven either by a fixed **items per
row** (`AlbumSizingMode::ItemsPerRow`) or a **target image size**
(`AlbumSizingMode::ImageSize`), from which the column count / row height is
derived to fill the available width.

## Class Reference

### Construction & Factory

```cpp
UltraCanvasAlbum(const std::string& id, float x, float y, float w, float h);

auto album = CreateAlbum("media-album", 20, 80, 940, 560);
```

### Items

```cpp
struct AlbumItem {
    std::string mediaPath;      // photo, or the video / music file
    std::string thumbnailPath;  // explicit cover / poster (falls back to mediaPath)
    std::string title;          // primary caption line
    std::string subtitle;       // secondary line (artist, date, duration, ...)
    std::string id;             // optional stable id for callbacks
    AlbumMediaType mediaType = AlbumMediaType::Photo;  // Photo | Video | Music
    Point2Df focusPoint{0.5f, 0.5f};   // 0..1 crop / zoom focus
    double   aspectRatio = 0.0;        // 0 = derive from bitmap
    bool     featured = false;         // promoted in the Mosaic layout
};

album->AddItem({ "media/images/landscape.jpg", "", "Mountain Dawn",
                 "Photo · 2024", "", AlbumMediaType::Photo });
```

Video and music items draw `thumbnailPath` (cover art / poster frame) plus a
type badge. If no thumbnail decodes, a neutral placeholder with a play / note
glyph is drawn instead.

### Configuration

```cpp
AlbumConfig cfg;
cfg.layout        = AlbumLayout::UniformGrid;
cfg.mode          = AlbumMode::Display;
cfg.sizingMode    = AlbumSizingMode::ItemsPerRow;  // or ImageSize
cfg.itemsPerRow   = 4;
cfg.targetImageWidth = 220;   // used when sizingMode == ImageSize
cfg.targetRowHeight  = 200;   // Justified row height / Filmstrip height
cfg.gap           = 10;
cfg.imageDisplay  = AlbumImageDisplay::Crop;        // Crop | Zoom | Stretch | Fit
cfg.zoomFactor    = 1.25f;                          // extra magnification for Zoom
cfg.actionDisplay = AlbumActionDisplay::OnHover;    // AlwaysVisible | OnHover | ContextMenu | Hidden
cfg.showMenuIcon  = true;                           // ContextMenu: draw a kebab (⋮)
cfg.captionPlacement = AlbumCaptionPlacement::BelowImage;  // BelowImage | OverlayBottom | Hidden
cfg.backgroundColor     = Color(245, 245, 247, 255);
cfg.itemBackgroundColor = Color(255, 255, 255, 255);
cfg.showBorder    = true;
cfg.borderColor   = Color(208, 208, 214, 255);
cfg.borderWidth   = 1.0f;
cfg.cornerRadius  = 6.0f;
cfg.dropShadow    = true;
album->SetConfig(cfg);
```

Convenience setters: `SetLayout`, `SetMode`, `SetImageDisplay`,
`SetActionDisplay`, `SetItemsPerRow`, `SetTargetImageSize`.

### Image fit

| `AlbumImageDisplay` | Mapping | Notes |
| --- | --- | --- |
| `Crop` | Cover + crop | Keeps aspect; the `focusPoint` chooses which part survives the crop. |
| `Zoom` | Cover × `zoomFactor` | Like Crop but magnified further around the focus point. |
| `Stretch` | Fill | Distorts the image to the exact cell. |
| `Fit` | Contain | Whole image, letterboxed onto the item background. |

### Actions & modes

```cpp
struct AlbumAction {
    std::string id, label, iconPath;
    bool inDisplay = true, inUserEdit = true, inAdmin = true;  // per-mode gating
    std::function<void(size_t index)> onTrigger;
};

AlbumAction del;
del.id = "delete"; del.label = "Delete"; del.iconPath = "media/icons/trash.png";
del.inDisplay = false;                 // hidden from visitors
del.onTrigger = [album](size_t i){ album->RemoveItem(i); };
album->AddAction(del);
```

`AlbumMode` selects which actions are offered and whether tiles can be reordered:

- `Display` — visitor: view only, click opens / plays an item.
- `UserEdit` — owner: drag tiles to reorder, owner actions, selection.
- `Admin` — everything in `UserEdit` plus admin-only moderation actions.

`AlbumActionDisplay` controls how the visible actions surface:
`AlwaysVisible` (always painted), `OnHover` (shown while hovered),
`ContextMenu` (right-click popup, with an optional kebab icon via
`showMenuIcon`), or `Hidden`.

### Events

```cpp
std::function<void(size_t)> onItemClicked;       // single click / tap
std::function<void(size_t)> onItemActivated;     // double click: open / play
std::function<void(size_t,size_t)> onItemsReordered;  // (from, to) — edit modes
std::function<void(size_t)> onItemRemoved;       // after RemoveItem
std::function<void(const std::vector<size_t>&)> onSelectionChanged;
```

## Interaction

- **Click** a tile → `onItemClicked` (and toggles selection in edit/admin when `allowSelection`).
- **Double-click** → `onItemActivated` (open / play).
- **Right-click** (or the kebab icon) → per-item action menu.
- **Drag** a tile (edit / admin) → reorder; fires `onItemsReordered`.
- **Mouse wheel** → scroll (vertical for grids, horizontal for the filmstrip).

## Demo

The demo page (`Apps/DemoApp/UltraCanvasAlbumExamples.cpp`, Widgets → Album)
shows a mixed photo/video/music album with button rows to switch the layout
design, image-fit mode, viewer mode, action-icon display, items-per-row and
caption placement at runtime.
