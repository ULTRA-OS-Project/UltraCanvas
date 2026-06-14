// Apps/DemoApp/UltraCanvasAlbumExamples.cpp
// Demonstration of UltraCanvasAlbum: layout designs, image-fit modes, action-icon
// display options and visitor / user-edit / admin modes for a mixed photo / video
// / music album.
// Version: 2.0.0
// Last Modified: 2026-06-14
// V2.0.0: Rebuilt with a Flex-column layout (per the project layout guideline)
//   instead of a fixed 1000x990 absolutely-positioned root. The header and the
//   control rows are pinned; the album grows to fill the remaining space and
//   scrolls its own tiles internally. This removes the spurious horizontal
//   scrollbar and stops the whole example scrolling as one block.
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasAlbum.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <vector>

namespace UltraCanvas {

    namespace {
        // A plain flex layout wrapper that must never scroll itself — the album
        // is the single scroll region. Disabling scrollbars keeps these wrappers
        // from ever showing a (spurious) scrollbar of their own.
        std::shared_ptr<UltraCanvasContainer> MakeLayoutBox(const std::string& id) {
            auto c = std::make_shared<UltraCanvasContainer>(id);
            ContainerStyle st;
            st.autoShowScrollbars           = false;
            st.forceShowVerticalScrollbar   = false;
            st.forceShowHorizontalScrollbar = false;
            c->SetContainerStyle(st);
            return c;
        }

        // A horizontal control line: shrink:0, stretched on the cross axis.
        std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id) {
            auto row = MakeLayoutBox(id);
            row->layout.SetFlexRow().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            row->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            return row;
        }

        // Append a bold label followed by a row of fixed-size buttons into `row`.
        // onPick(i) fires for button i. Buttons keep their pixel size (grow/shrink 0).
        template <typename Fn>
        void AppendLabeledButtons(const std::shared_ptr<UltraCanvasContainer>& row,
                                  const std::string& idPrefix, const char* labelText,
                                  int labelW, int btnW, int btnH,
                                  const std::vector<const char*>& labels, Fn onPick) {
            auto lbl = std::make_shared<UltraCanvasLabel>(idPrefix + "lbl", 0, 0, labelW, btnH);
            lbl->SetText(labelText);
            lbl->SetFontSize(12);
            lbl->SetFontWeight(FontWeight::Bold);
            lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            row->AddChild(lbl);

            for (size_t i = 0; i < labels.size(); ++i) {
                auto b = std::make_shared<UltraCanvasButton>(
                        idPrefix + std::to_string(i), 0, 0, btnW, btnH, labels[i]);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                int idx = static_cast<int>(i);
                b->SetOnClick([idx, onPick]() { onPick(idx); });
                b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                row->AddChild(b);
            }
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAlbumExamples() {
        // Root: a flex column that fills the display area (DisplayDemoItem assigns
        // it flex-grow + stretch). Header and controls are pinned (flex-shrink:0);
        // the album grows to fill the middle and scrolls its tiles internally.
        auto root = MakeLayoutBox("AlbumExamples");
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);

        // ===== Header (pinned) =====
        auto header = MakeLayoutBox("AlbumHeader");
        header->layout.SetFlexColumn().SetFlexGap(4)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        header->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto title = std::make_shared<UltraCanvasLabel>("AlbumTitle", 0, 0, 0, 28);
        title->SetText("UltraCanvas Album — photo / video / music gallery with selectable designs");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        header->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("AlbumSubtitle", 0, 0, 0, 38);
        subtitle->SetText("Switch the layout design, image-fit mode, viewer mode and action-icon "
                          "display with the buttons. In Edit / Admin mode drag a tile to reorder; "
                          "right-click (or the kebab icon) opens the action menu.");
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(110, 110, 110, 255));
        subtitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        header->AddChild(subtitle);
        root->AddChild(header);

        // ===== The album itself (grows to fill, scrolls internally) =====
        // No explicit size — flex sizes it. An explicit w/h would override the
        // flex-grow / align-self: stretch the engine applies below.
        auto album = CreateAlbum("media-album", 0, 0, 0, 0);
        album->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        AlbumConfig cfg;
        cfg.layout        = AlbumLayout::UniformGrid;
        cfg.mode          = AlbumMode::Display;
        cfg.sizingMode    = AlbumSizingMode::ItemsPerRow;
        cfg.itemsPerRow   = 4;
        cfg.gap           = 10;
        cfg.outerPadding  = 12;
        cfg.imageDisplay  = AlbumImageDisplay::Crop;
        cfg.actionDisplay = AlbumActionDisplay::OnHover;
        cfg.captionPlacement = AlbumCaptionPlacement::BelowImage;
        cfg.showMediaBadges  = true;
        cfg.dropShadow    = true;
        cfg.cornerRadius  = 6.0f;
        cfg.hoverZoom     = true;
        album->SetConfig(cfg);

        // Build a mixed media list from images that ship in media/images.
        const std::string mediaRoot = NormalizePath(GetResourcesDir() + "media/images/");
        struct Seed {
            const char* file; const char* title; const char* subtitle;
            AlbumMediaType type; bool featured;
        };
        const Seed seeds[] = {
            { "landscape.jpg",   "Mountain Dawn",    "Photo · 2024",        AlbumMediaType::Photo, true  },
            { "portrait.jpg",    "Studio Portrait",  "Photo · 50mm",        AlbumMediaType::Photo, false },
            { "sample_photo.jpg","City Lights",      "Video · 1:24",        AlbumMediaType::Video, false },
            { "ship.jpg",        "Harbour",          "Photo · golden hour", AlbumMediaType::Photo, false },
            { "sample_hq.jpg",   "Summer Mix",       "Music · 42 tracks",   AlbumMediaType::Music, false },
            { "screenshot.png",  "Tutorial Clip",    "Video · 3:08",        AlbumMediaType::Video, false },
            { "dice.jpg",        "Game Night",       "Photo · 2023",        AlbumMediaType::Photo, false },
            { "3d-boxes.png",    "Renders",          "Photo · Blender",     AlbumMediaType::Photo, false },
            { "sample_logo.png", "Brand Reel",       "Video · 0:30",        AlbumMediaType::Video, false },
            { "test_small.png",  "Chill Beats",      "Music · 18 tracks",   AlbumMediaType::Music, false },
            { "png_68.png",      "Field Notes",      "Photo · film",        AlbumMediaType::Photo, false },
            { "webp_68.png",     "Roadtrip",         "Photo · 2022",        AlbumMediaType::Photo, false },
        };
        for (const auto& s : seeds) {
            AlbumItem it;
            it.mediaPath = mediaRoot + s.file;
            it.title = s.title;
            it.subtitle = s.subtitle;
            it.mediaType = s.type;
            it.featured = s.featured;
            album->AddItem(it);
        }

        auto* albumPtr = album.get();
        root->AddChild(album);

        // ===== Status (pinned) =====
        auto status = std::make_shared<UltraCanvasLabel>("AlbumStatus", 0, 0, 0, 22);
        status->SetText("Mode: Display · Layout: Uniform Grid · click a tile, hover for actions");
        status->SetFontSize(11);
        status->SetTextColor(Color(120, 120, 120, 255));
        status->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root->AddChild(status);
        auto* statusPtr = status.get();

        // ===== Actions (gated per mode) =====
        AlbumAction open;
        open.id = "open"; open.label = "Open";
        open.inDisplay = true; open.inUserEdit = true; open.inAdmin = true;
        AlbumAction fav;
        fav.id = "fav"; fav.label = "Favourite";
        fav.inDisplay = true; fav.inUserEdit = true; fav.inAdmin = true;
        AlbumAction del;
        del.id = "delete"; del.label = "Delete";
        del.inDisplay = false; del.inUserEdit = true; del.inAdmin = true;  // hidden from visitors
        AlbumAction hide;
        hide.id = "moderate"; hide.label = "Hide (admin)";
        hide.inDisplay = false; hide.inUserEdit = false; hide.inAdmin = true; // admin only

        open.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Open: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
        };
        fav.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Favourited: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
        };
        del.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Deleted: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
            albumPtr->RemoveItem(i);
        };
        hide.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Hidden by admin: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
        };
        album->AddAction(open);
        album->AddAction(fav);
        album->AddAction(del);
        album->AddAction(hide);

        album->onItemActivated = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Activated (open/play): " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
        };
        album->onItemsReordered = [statusPtr](size_t from, size_t to) {
            std::ostringstream o; o << "Reordered item " << from << " -> " << to;
            statusPtr->SetText(o.str());
        };

        // ===== Controls (pinned) =====
        auto controls = MakeLayoutBox("AlbumControls");
        controls->layout.SetFlexColumn().SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        controls->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        const int kLabelW = 85;

        // ----- Layout picker -----
        const AlbumLayout layoutVals[] = {
            AlbumLayout::UniformGrid, AlbumLayout::Justified, AlbumLayout::Masonry,
            AlbumLayout::Mosaic, AlbumLayout::Filmstrip, AlbumLayout::Cards
        };
        auto layoutRow = MakeRow("album_layout_row");
        AppendLabeledButtons(layoutRow, "album_layout_", "Layout:", kLabelW, 110, 28,
                      {"Uniform Grid", "Justified", "Masonry", "Mosaic", "Filmstrip", "Cards"},
                      [albumPtr, statusPtr, layoutVals](int i) {
                          albumPtr->SetLayout(layoutVals[i]);
                          const char* names[] = {"Uniform Grid","Justified","Masonry",
                                                 "Mosaic","Filmstrip","Cards"};
                          std::ostringstream o; o << "Layout: " << names[i];
                          statusPtr->SetText(o.str());
                      });
        controls->AddChild(layoutRow);

        // ----- Image-fit picker -----
        const AlbumImageDisplay fitVals[] = {
            AlbumImageDisplay::Crop, AlbumImageDisplay::Zoom,
            AlbumImageDisplay::Stretch, AlbumImageDisplay::Fit
        };
        auto fitRow = MakeRow("album_fit_row");
        AppendLabeledButtons(fitRow, "album_fit_", "Image fit:", kLabelW, 110, 28,
                      {"Crop (focus)", "Zoom", "Stretch", "Fit"},
                      [albumPtr, fitVals](int i) { albumPtr->SetImageDisplay(fitVals[i]); });
        controls->AddChild(fitRow);

        // ----- Mode picker -----
        const AlbumMode modeVals[] = { AlbumMode::Display, AlbumMode::UserEdit, AlbumMode::Admin };
        auto modeRow = MakeRow("album_mode_row");
        AppendLabeledButtons(modeRow, "album_mode_", "Mode:", kLabelW, 130, 28,
                      {"Display (visitor)", "User edit", "Admin"},
                      [albumPtr, statusPtr, modeVals](int i) {
                          albumPtr->SetMode(modeVals[i]);
                          AlbumConfig c = albumPtr->GetConfig();
                          c.allowSelection = (modeVals[i] != AlbumMode::Display);
                          albumPtr->SetConfig(c);
                          const char* names[] = {"Display","User edit","Admin"};
                          std::ostringstream o;
                          o << "Mode: " << names[i]
                            << (i == 0 ? " — view only" : " — drag to reorder, right-click for actions");
                          statusPtr->SetText(o.str());
                      });
        controls->AddChild(modeRow);

        // ----- Action-icon display picker -----
        auto actRow = MakeRow("album_act_row");
        AppendLabeledButtons(actRow, "album_act_", "Actions:", kLabelW, 130, 28,
                      {"Always", "On hover", "Menu (+icon)", "Menu (no icon)", "Hidden"},
                      [albumPtr](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          switch (i) {
                              case 0: c.actionDisplay = AlbumActionDisplay::AlwaysVisible; break;
                              case 1: c.actionDisplay = AlbumActionDisplay::OnHover; break;
                              case 2: c.actionDisplay = AlbumActionDisplay::ContextMenu; c.showMenuIcon = true; break;
                              case 3: c.actionDisplay = AlbumActionDisplay::ContextMenu; c.showMenuIcon = false; break;
                              default: c.actionDisplay = AlbumActionDisplay::Hidden; break;
                          }
                          albumPtr->SetConfig(c);
                      });
        controls->AddChild(actRow);

        // ----- Sizing + captions on one line -----
        auto sizeRow = MakeRow("album_size_row");
        AppendLabeledButtons(sizeRow, "album_cols_", "Per row:", kLabelW, 64, 28,
                      {"3", "4", "5", "6"},
                      [albumPtr](int i) { albumPtr->SetItemsPerRow(3 + i); });
        sizeRow->AddSpacer(24);
        AppendLabeledButtons(sizeRow, "album_cap_", "Caption:", 70, 92, 28,
                      {"Below", "Overlay", "Hidden"},
                      [albumPtr](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          c.captionPlacement = (i == 0) ? AlbumCaptionPlacement::BelowImage
                                             : (i == 1) ? AlbumCaptionPlacement::OverlayBottom
                                                        : AlbumCaptionPlacement::Hidden;
                          albumPtr->SetConfig(c);
                      });
        controls->AddChild(sizeRow);

        root->AddChild(controls);

        return root;
    }

} // namespace UltraCanvas
