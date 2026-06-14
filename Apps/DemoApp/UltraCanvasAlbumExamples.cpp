// Apps/DemoApp/UltraCanvasAlbumExamples.cpp
// Demonstration of UltraCanvasAlbum: layout designs, image-fit modes, action-icon
// display options and visitor / user-edit / admin modes for a mixed photo / video
// / music album.
// Version: 1.0.0
// Last Modified: 2026-06-13
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
        // A small row-of-buttons helper so each option group reads the same way.
        template <typename Fn>
        int MakeButtonRow(std::shared_ptr<UltraCanvasContainer>& root,
                          const std::string& idPrefix, int x, int y, int btnW, int btnH,
                          const std::vector<const char*>& labels, Fn onPick) {
            int bx = x;
            for (size_t i = 0; i < labels.size(); ++i) {
                auto b = std::make_shared<UltraCanvasButton>(
                        idPrefix + std::to_string(i), bx, y, btnW, btnH, labels[i]);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                int idx = static_cast<int>(i);
                b->SetOnClick([idx, onPick]() { onPick(idx); });
                root->AddChild(b);
                bx += btnW + 6;
            }
            return bx;
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAlbumExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("AlbumExamples", 0, 0, 1000, 990);
        root->SetPadding(0, 5, 5, 0);

        auto title = std::make_shared<UltraCanvasLabel>("AlbumTitle", 20, 10, 900, 30);
        title->SetText("UltraCanvas Album — photo / video / music gallery with selectable designs");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("AlbumSubtitle", 20, 42, 940, 36);
        subtitle->SetText("Switch the layout design, image-fit mode, viewer mode and action-icon "
                          "display with the buttons. In Edit / Admin mode drag a tile to reorder; "
                          "right-click (or the kebab icon) opens the action menu.");
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(110, 110, 110, 255));
        root->AddChild(subtitle);

        // ===== The album itself =====
        auto album = CreateAlbum("media-album", 20, 86, 940, 560);

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

        auto* albumPtr = album.get();
        root->AddChild(album);

        // Status label.
        auto status = std::make_shared<UltraCanvasLabel>("AlbumStatus", 20, 652, 940, 22);
        status->SetText("Mode: Display · Layout: Uniform Grid · click a tile, hover for actions");
        status->SetFontSize(11);
        status->SetTextColor(Color(120, 120, 120, 255));
        root->AddChild(status);
        auto* statusPtr = status.get();

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

        // ===== Layout picker =====
        int y = 684;
        auto layoutTitle = std::make_shared<UltraCanvasLabel>("AlbumLayoutLbl", 20, y + 4, 90, 22);
        layoutTitle->SetText("Layout:");
        layoutTitle->SetFontSize(12);
        layoutTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(layoutTitle);
        const AlbumLayout layoutVals[] = {
            AlbumLayout::UniformGrid, AlbumLayout::Justified, AlbumLayout::Masonry,
            AlbumLayout::Mosaic, AlbumLayout::Filmstrip, AlbumLayout::Cards
        };
        MakeButtonRow(root, "album_layout_", 110, y, 110, 28,
                      {"Uniform Grid", "Justified", "Masonry", "Mosaic", "Filmstrip", "Cards"},
                      [albumPtr, statusPtr, layoutVals](int i) {
                          albumPtr->SetLayout(layoutVals[i]);
                          const char* names[] = {"Uniform Grid","Justified","Masonry",
                                                 "Mosaic","Filmstrip","Cards"};
                          std::ostringstream o; o << "Layout: " << names[i];
                          statusPtr->SetText(o.str());
                      });

        // ===== Image-fit picker =====
        y += 36;
        auto fitTitle = std::make_shared<UltraCanvasLabel>("AlbumFitLbl", 20, y + 4, 90, 22);
        fitTitle->SetText("Image fit:");
        fitTitle->SetFontSize(12);
        fitTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(fitTitle);
        const AlbumImageDisplay fitVals[] = {
            AlbumImageDisplay::Crop, AlbumImageDisplay::Zoom,
            AlbumImageDisplay::Stretch, AlbumImageDisplay::Fit
        };
        MakeButtonRow(root, "album_fit_", 110, y, 110, 28,
                      {"Crop (focus)", "Zoom", "Stretch", "Fit"},
                      [albumPtr, fitVals](int i) { albumPtr->SetImageDisplay(fitVals[i]); });

        // ===== Mode picker =====
        y += 36;
        auto modeTitle = std::make_shared<UltraCanvasLabel>("AlbumModeLbl", 20, y + 4, 90, 22);
        modeTitle->SetText("Mode:");
        modeTitle->SetFontSize(12);
        modeTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(modeTitle);
        const AlbumMode modeVals[] = { AlbumMode::Display, AlbumMode::UserEdit, AlbumMode::Admin };
        MakeButtonRow(root, "album_mode_", 110, y, 130, 28,
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

        // ===== Action-icon display picker =====
        y += 36;
        auto actTitle = std::make_shared<UltraCanvasLabel>("AlbumActLbl", 20, y + 4, 90, 22);
        actTitle->SetText("Actions:");
        actTitle->SetFontSize(12);
        actTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(actTitle);
        MakeButtonRow(root, "album_act_", 110, y, 130, 28,
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

        // ===== Sizing + captions toggles =====
        y += 36;
        auto sizeTitle = std::make_shared<UltraCanvasLabel>("AlbumSizeLbl", 20, y + 4, 90, 22);
        sizeTitle->SetText("Per row:");
        sizeTitle->SetFontSize(12);
        sizeTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(sizeTitle);
        MakeButtonRow(root, "album_cols_", 110, y, 64, 28,
                      {"3", "4", "5", "6"},
                      [albumPtr](int i) { albumPtr->SetItemsPerRow(3 + i); });

        auto capTitle = std::make_shared<UltraCanvasLabel>("AlbumCapLbl", 380, y + 4, 70, 22);
        capTitle->SetText("Caption:");
        capTitle->SetFontSize(12);
        capTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(capTitle);
        MakeButtonRow(root, "album_cap_", 450, y, 92, 28,
                      {"Below", "Overlay", "Hidden"},
                      [albumPtr](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          c.captionPlacement = (i == 0) ? AlbumCaptionPlacement::BelowImage
                                             : (i == 1) ? AlbumCaptionPlacement::OverlayBottom
                                                        : AlbumCaptionPlacement::Hidden;
                          albumPtr->SetConfig(c);
                      });

        return root;
    }

} // namespace UltraCanvas
