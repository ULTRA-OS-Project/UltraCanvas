// Apps/DemoApp/UltraCanvasAlbumExamples.cpp
// Demonstration of UltraCanvasAlbum: layout designs, image-fit modes, action-icon
// display options and visitor / user-edit / admin modes for a mixed photo / video
// / music album.
// Version: 2.9.0
// Last Modified: 2026-06-25
// V2.9.0: Refreshed the sample media — tile 3 is now a real bundled clip
//   (media/videos/Video A more competition.mp4), tile 6 is the YouTube clip
//   (moved inline from the old trailing entry), and tiles 7 / 11 use new images.
//   The seed table can now carry a real video file (poster extracted) and a link
//   icon, so every tile — including the videos — is built in one ordered loop.
// V2.8.0: Video tiles now play in their own window — a "Play" action icon (video
//   only) and a double-click both open the clip in a UltraCanvasVideoPlayerElement
//   window. Control buttons are more compact and the header is shorter so the
//   album gets enough vertical room to show two full grid rows.
// V2.7.0: The video tile now extracts a poster frame via SaveVideoThumbnail
//   (cached as a QOI) for its cover when the UltraCanvasVideoThumbnail module is
//   present; otherwise it falls back to the video placeholder.
// V2.6.0: Added a YouTube video tile (media/videos/Lola Lexy - No kings.mp4)
//   whose second row links to YouTube, preceded by a YouTube icon
//   (AlbumItem::linkIconPath + media/icons/youtube.svg).
// V2.5.0: Compact action icons (actionButtonSize 20) and an "Icon bg" option
//   row (round / square / rounded-square) exercising AlbumActionIconBackground.
// V2.4.0: Photo tiles now show their source on the second caption row as a
//   clickable link (blue + underline); clicking it fires onLinkClicked rather
//   than selecting the tile. Added "Icon corner" (8 image / text-block anchors)
//   and "Image corners" (square / rounded) option rows to exercise the new
//   AlbumActionAnchor and imageCornerRadius config.
// V2.3.0: Option buttons are now radio groups that highlight the active choice
//   (gold border + warm fill), matching the slideshow example, so the page
//   always shows which album options are currently selected.
// V2.2.0: Added a photo-only "View full size" action (and double-click) that
//   opens the image in a lightbox window with a related-text panel (title,
//   subtitle and a longer per-photo description).
// V2.1.0: Action icons use SVG art (edit / add_text / delete) with hover
//   tooltips; option labels dropped the trailing ":" and are vertically
//   centred; the image-fit "Crop" button is plain and a "Crop focus" row was
//   added that activates only under the Crop / Zoom fits.
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
#include "UltraCanvasImageElement.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasWindow.h"
#include <fstream>
#include <sstream>
#include <vector>

// Video poster-frame extraction (UltraCanvasVideoThumbnail) lives in its own
// module; use it when present so video tiles get a real cover, otherwise fall
// back to the built-in video placeholder.
#if defined(__has_include)
#  if __has_include("UltraCanvasVideoThumbnail.h")
#    include "UltraCanvasVideoThumbnail.h"
#    define ULTRACANVAS_DEMO_HAVE_VIDEO_THUMBNAIL 1
#  endif
#endif

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

        // Paint an option button in its normal or selected state. Mirrors the
        // slideshow example: the active choice gets a gold border + warm fill so
        // the page always shows which option is currently applied.
        inline void StyleOptionButton(UltraCanvasButton* b, bool selected) {
            const Color baseBg(255, 255, 255, 255);
            const Color selBg(246, 214, 158, 255);     // warm highlight
            const Color baseBorder(0, 0, 0, 77);
            const Color selBorder(201, 143, 46, 255);  // #C98F2E gold
            const Color text(40, 40, 40, 255);
            b->SetColors(selected ? selBg : baseBg, selBg);
            b->SetTextColors(text);
            b->SetBorder(selected ? 2.0f : 1.0f, selected ? selBorder : baseBorder);
        }

        // Append a bold label followed by a radio row of fixed-size buttons into
        // `row`. onPick(i) fires for button i; clicking a button highlights it and
        // clears its siblings, so the row always reflects the current selection.
        // `selectedIndex` marks the initially-active button (-1 = none). Returns
        // the created buttons so callers can enable / disable them.
        template <typename Fn>
        std::vector<std::shared_ptr<UltraCanvasButton>> AppendLabeledButtons(
                                  const std::shared_ptr<UltraCanvasContainer>& row,
                                  const std::string& idPrefix, const char* labelText,
                                  int labelW, int btnW, int btnH,
                                  const std::vector<const char*>& labels, Fn onPick,
                                  int selectedIndex = -1) {
            auto lbl = std::make_shared<UltraCanvasLabel>(idPrefix + "lbl", 0, 0, labelW, btnH);
            lbl->SetText(labelText);
            lbl->SetFontSize(12);
            lbl->SetFontWeight(FontWeight::Bold);
            // Vertically centre the label text against the buttons (matches the
            // slideshow example, where option labels are mid-aligned to their row).
            lbl->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            row->AddChild(lbl);

            // Shared list of the row's buttons so a click can restyle siblings.
            auto group = std::make_shared<std::vector<UltraCanvasButton*>>();
            std::vector<std::shared_ptr<UltraCanvasButton>> buttons;
            for (size_t i = 0; i < labels.size(); ++i) {
                auto b = std::make_shared<UltraCanvasButton>(
                        idPrefix + std::to_string(i), 0, 0, btnW, btnH, labels[i]);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                StyleOptionButton(b.get(), static_cast<int>(i) == selectedIndex);
                int idx = static_cast<int>(i);
                auto* raw = b.get();
                b->SetOnClick([idx, onPick, group, raw]() {
                    for (auto* gb : *group) StyleOptionButton(gb, gb == raw);
                    onPick(idx);
                });
                b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                row->AddChild(b);
                group->push_back(raw);
                buttons.push_back(b);
            }
            return buttons;
        }

        // A lightbox-style viewer: opens the photo at full size in its own window
        // with a related-text panel (title, subtitle and the longer description).
        // One instance is shared by the album so re-opening reuses the window.
        class AlbumPhotoViewer {
        public:
            void Show(const AlbumItem& item) {
                // Reuse a single window: just refresh its contents if it is open.
                if (window) {
                    window->Close();
                    window.reset();
                }

                WindowConfig cfg;
                cfg.title     = item.title.empty() ? "Photo" : item.title;
                cfg.width     = 1040;
                cfg.height    = 720;
                cfg.type      = WindowType::Standard;
                cfg.resizable = true;
                window = CreateWindow(cfg);
                if (!window) return;
                window->SetBackgroundColor(Color(22, 22, 26, 255));

                // Column: the image fills the top, the text panel is pinned below.
                window->layout.SetFlexColumn()
                              .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

                auto image = std::make_shared<UltraCanvasImageElement>("ViewerImage", 0, 0, 0, 0);
                image->SetFitMode(ImageFitMode::Contain);
                image->LoadFromFile(item.mediaPath.empty() ? item.thumbnailPath : item.mediaPath);
                image->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                window->AddChild(image);

                // Related-text panel.
                auto panel = std::make_shared<UltraCanvasContainer>("ViewerTextPanel");
                panel->SetBackgroundColor(Color(30, 30, 36, 255));
                panel->layout.SetFlexColumn().SetFlexGap(4)
                             .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
                panel->SetPadding(14, 18, 14, 18);
                panel->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

                auto titleLbl = std::make_shared<UltraCanvasLabel>("ViewerTitle", 0, 0, 0, 26);
                titleLbl->SetText(item.title);
                titleLbl->SetFontSize(18);
                titleLbl->SetFontWeight(FontWeight::Bold);
                titleLbl->SetTextColor(Color(245, 245, 248, 255));
                titleLbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                panel->AddChild(titleLbl);

                if (!item.subtitle.empty()) {
                    auto subLbl = std::make_shared<UltraCanvasLabel>("ViewerSubtitle", 0, 0, 0, 18);
                    subLbl->SetText(item.subtitle);
                    subLbl->SetFontSize(12);
                    subLbl->SetTextColor(Color(170, 170, 178, 255));
                    subLbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    panel->AddChild(subLbl);
                }

                // The related text (falls back to a generic line if none supplied).
                std::string related = item.description.empty()
                        ? "No additional description for this photo."
                        : item.description;
                auto descLbl = std::make_shared<UltraCanvasLabel>("ViewerDescription", 0, 0, 0, 48);
                descLbl->SetText(related);
                descLbl->SetFontSize(13);
                descLbl->SetTextColor(Color(214, 214, 220, 255));
                descLbl->SetWrap(TextWrap::WrapWord);
                descLbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                panel->AddChild(descLbl);

                auto hint = std::make_shared<UltraCanvasLabel>("ViewerHint", 0, 0, 0, 16);
                hint->SetText("Press ESC to close");
                hint->SetFontSize(10);
                hint->SetTextColor(Color(120, 120, 128, 255));
                hint->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                panel->AddChild(hint);

                window->AddChild(panel);

                // ESC closes the viewer.
                window->eventCallback = [this](const UCEvent& event) {
                    if (event.type == UCEventType::KeyUp &&
                        event.virtualKey == UCKeys::Escape) {
                        if (window) { window->Close(); window.reset(); }
                        return true;
                    }
                    return false;
                };

                window->Show();
            }

            // Play a video item in its own window: a video-player element fills
            // the window and starts playing immediately. Reuses the single shared
            // window (closing any open photo / video view first).
            void ShowVideo(const AlbumItem& item) {
                if (window) {
                    window->Close();
                    window.reset();
                }

                WindowConfig cfg;
                cfg.title     = item.title.empty() ? "Video" : item.title;
                cfg.width     = 960;
                cfg.height    = 600;
                cfg.type      = WindowType::Standard;
                cfg.resizable = true;
                window = CreateWindow(cfg);
                if (!window) return;
                window->SetBackgroundColor(Color(0, 0, 0, 255));
                window->layout.SetFlexColumn()
                              .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

                auto player = CreateVideoPlayer("AlbumVideoPlayer", 0, 0, 0, 0);
                player->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                  .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                const std::string path =
                        item.mediaPath.empty() ? item.thumbnailPath : item.mediaPath;
                player->LoadFromFile(path);
                window->AddChild(player);

                // ESC closes the viewer.
                window->eventCallback = [this](const UCEvent& event) {
                    if (event.type == UCEventType::KeyUp &&
                        event.virtualKey == UCKeys::Escape) {
                        if (window) { window->Close(); window.reset(); }
                        return true;
                    }
                    return false;
                };

                window->Show();
                // Start playback once the window is realized so the player's
                // frame timer runs against a live window.
                player->Play();
            }

        private:
            std::shared_ptr<UltraCanvasWindow> window;
        };
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

        auto subtitle = std::make_shared<UltraCanvasLabel>("AlbumSubtitle", 0, 0, 0, 20);
        subtitle->SetText("Switch the design with the buttons below. Double-click a photo to view "
                          "it full size, or a video to play it (or use the View / Play icon).");
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
        cfg.gap           = 8;
        cfg.outerPadding  = 10;
        cfg.imageDisplay  = AlbumImageDisplay::Crop;
        cfg.actionDisplay = AlbumActionDisplay::OnHover;
        cfg.actionButtonSize = 20.0f;   // compact action icons (default is 28)
        cfg.actionIconBackground = AlbumActionIconBackground::Round;
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
            AlbumMediaType type; bool featured; const char* description;
            const char* link;      // when set, the subtitle row is a clickable link
            const char* linkIcon;  // optional icon (under media/icons/) before the link
            bool isVideoFile;      // true: `file` is a real clip under media/videos/
                                   // (its mediaPath plays, and a poster frame is
                                   // extracted for the tile cover)
        };
        // Photo tiles carry a source domain on the second row, shown as a link
        // (matching a search-results / gallery "image · source" layout); video /
        // music tiles keep a plain metadata subtitle with no link.
        const Seed seeds[] = {
            { "landscape.jpg",   "Mountain Dawn",    "naturepix.example",   AlbumMediaType::Photo, true,
              "First light spilling over the ridge line. Shot handheld just after "
              "sunrise, the low sun rakes across the slopes and pulls out every fold "
              "in the terrain.", "https://naturepix.example/mountain-dawn" },
            { "portrait.jpg",    "Studio Portrait",  "studioshots.example", AlbumMediaType::Photo, false,
              "A classic studio headshot taken with a fast 50mm lens and a single "
              "softbox to camera-left for soft, directional light.",
              "https://studioshots.example/portrait" },
            { "Video A more competition.mp4", "Competition", "Video", AlbumMediaType::Video, false,
              "A short competition clip bundled in media/videos.", "", "", true },
            { "ship.jpg",        "Harbour",          "harbourlife.example", AlbumMediaType::Photo, false,
              "Boats resting in the harbour during the golden hour, the warm light "
              "reflecting off the still water.", "https://harbourlife.example/golden-hour" },
            { "sample_hq.jpg",   "Summer Mix",       "Music · 42 tracks",   AlbumMediaType::Music, false, "", "" },
            { "Lola Lexy - No kings.mp4", "Lola Lexy - No kings", "youtube.com", AlbumMediaType::Video, false,
              "Linked from YouTube.", "https://www.youtube.com/watch?v=Tl15Os47lG0", "youtube.svg", true },
            { "freepicOIP-3521821688.jpg", "Game Night",  "boardgames.example",  AlbumMediaType::Photo, false,
              "Coloured dice mid-roll on game night — a quick macro grab to freeze "
              "the action on the table.", "https://boardgames.example/game-night" },
            { "3d-boxes.png",    "Renders",          "blenderhub.example",  AlbumMediaType::Photo, false,
              "A set of stacked 3D boxes rendered in Blender to test material and "
              "lighting setups.", "https://blenderhub.example/renders" },
            { "sample_logo.png", "Brand Reel",       "Video · 0:30",        AlbumMediaType::Video, false, "", "" },
            { "test_small.png",  "Chill Beats",      "Music · 18 tracks",   AlbumMediaType::Music, false, "", "" },
            { "testcard_rgba.qoi", "Field Notes",    "filmdiary.example",   AlbumMediaType::Photo, false,
              "Scanned 35mm film frame from a walk in the field — grainy, warm and "
              "full of character.", "https://filmdiary.example/field-notes" },
            { "webp_68.png",     "Roadtrip",         "openroad.example",    AlbumMediaType::Photo, false,
              "A snapshot from the 2022 summer road trip, somewhere along an open "
              "stretch of highway.", "https://openroad.example/roadtrip" },
        };
        // Video tiles backed by a real clip (Seed::isVideoFile) point mediaPath at
        // media/videos and get a poster frame extracted once via SaveVideoThumbnail
        // (cached as a QOI next to the clip) for their cover; if the thumbnail
        // module or a video backend is unavailable the tile shows the built-in
        // video placeholder instead. A double-click (or the Play icon) plays them.
        for (const auto& s : seeds) {
            AlbumItem it;
            if (s.isVideoFile) {
                const std::string videoPath =
                        NormalizePath(GetResourcesDir() + "media/videos/" + s.file);
                it.mediaPath = videoPath;
#ifdef ULTRACANVAS_DEMO_HAVE_VIDEO_THUMBNAIL
                // Cache the poster next to the clip, swapping the extension to .qoi.
                std::string posterPath = videoPath;
                const auto dot = posterPath.find_last_of('.');
                if (dot != std::string::npos)
                    posterPath.replace(dot, std::string::npos, ".qoi");
                bool havePoster = std::ifstream(posterPath, std::ios::binary).good();
                if (!havePoster) {
                    VideoThumbnailRequest req;
                    req.maxWidth  = 640;   // downscale the poster to a sensible cover size
                    req.maxHeight = 480;
                    havePoster = SaveVideoThumbnail(videoPath, posterPath, req);
                }
                if (havePoster) it.thumbnailPath = posterPath;
#endif
            } else {
                it.mediaPath = mediaRoot + s.file;
            }
            it.title = s.title;
            it.subtitle = s.subtitle;
            it.description = s.description;
            it.mediaType = s.type;
            it.featured = s.featured;
            it.link = s.link;
            if (s.linkIcon && *s.linkIcon) {
                it.linkIconPath = NormalizePath(GetResourcesDir() + "media/icons/" + s.linkIcon);
            }
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
        // Each action carries an SVG icon (drawn on the overlay button and in the
        // context menu) and a label that doubles as the icon's hover tooltip.
        const std::string iconRoot = NormalizePath(GetResourcesDir() + "media/icons/");

        // Shared media viewer used by the "View" / "Play" actions and double-click:
        // photos open full size in a lightbox, videos play in their own window.
        auto viewer = std::make_shared<AlbumPhotoViewer>();
        auto openMedia = [albumPtr, statusPtr, viewer](size_t i) {
            const AlbumItem& it = albumPtr->GetItems()[i];
            if (it.mediaType == AlbumMediaType::Video) {
                std::ostringstream o; o << "Playing video: " << it.title;
                statusPtr->SetText(o.str());
                viewer->ShowVideo(it);
                return;
            }
            if (it.mediaType != AlbumMediaType::Photo) {
                std::ostringstream o; o << "Open / play is wired for photos and video: "
                                        << it.title;
                statusPtr->SetText(o.str());
                return;
            }
            std::ostringstream o; o << "View full size: " << it.title;
            statusPtr->SetText(o.str());
            viewer->Show(it);
        };

        // "View full size" — photos only (gated by media type, so the icon is not
        // drawn on video / music tiles) and available in every mode.
        AlbumAction view;
        view.id = "view"; view.label = "View full size";
        view.iconPath = iconRoot + "view.svg";
        view.mediaTypes = { AlbumMediaType::Photo };
        view.inDisplay = true; view.inUserEdit = true; view.inAdmin = true;
        view.onTrigger = openMedia;
        album->AddAction(view);

        // "Play" — videos only (gated by media type, so the play icon is drawn on
        // video tiles only), available in every mode. Clicking it opens the clip
        // in its own player window (same path as a double-click on the tile).
        AlbumAction play;
        play.id = "play"; play.label = "Play video";
        play.iconPath = iconRoot + "play.svg";
        play.mediaTypes = { AlbumMediaType::Video };
        play.inDisplay = true; play.inUserEdit = true; play.inAdmin = true;
        play.onTrigger = openMedia;
        album->AddAction(play);

        AlbumAction comment;
        comment.id = "comment"; comment.label = "Add comment";
        comment.iconPath = iconRoot + "add_text.svg";
        comment.inDisplay = true; comment.inUserEdit = true; comment.inAdmin = true;
        AlbumAction edit;
        edit.id = "edit"; edit.label = "Edit";
        edit.iconPath = iconRoot + "edit.svg";
        edit.inDisplay = false; edit.inUserEdit = true; edit.inAdmin = true;  // hidden from visitors
        AlbumAction del;
        del.id = "delete"; del.label = "Delete";
        del.iconPath = iconRoot + "delete.svg";
        del.inDisplay = false; del.inUserEdit = true; del.inAdmin = true;     // hidden from visitors

        comment.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Add comment: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
        };
        edit.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Edit: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
        };
        del.onTrigger = [albumPtr, statusPtr](size_t i) {
            std::ostringstream o; o << "Deleted: " << albumPtr->GetItems()[i].title;
            statusPtr->SetText(o.str());
            albumPtr->RemoveItem(i);
        };
        album->AddAction(comment);
        album->AddAction(edit);
        album->AddAction(del);

        // Clicking the second-row source link (blue, underlined) reports the
        // target rather than selecting the tile.
        album->onLinkClicked = [albumPtr, statusPtr](size_t i) {
            const AlbumItem& it = albumPtr->GetItems()[i];
            std::ostringstream o; o << "Open link: " << it.link;
            statusPtr->SetText(o.str());
        };

        // Double-click opens the same media viewer (photo lightbox / video play).
        album->onItemActivated = openMedia;
        album->onItemsReordered = [statusPtr](size_t from, size_t to) {
            std::ostringstream o; o << "Reordered item " << from << " -> " << to;
            statusPtr->SetText(o.str());
        };

        // ===== Controls (pinned) =====
        auto controls = MakeLayoutBox("AlbumControls");
        controls->layout.SetFlexColumn().SetFlexGap(4)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        controls->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        const int kLabelW = 85;
        // Compact control buttons so the album gets more vertical room (two full
        // rows of the default grid are visible without scrolling).
        const int kBtnH = 24;

        // ----- Layout picker -----
        const AlbumLayout layoutVals[] = {
            AlbumLayout::UniformGrid, AlbumLayout::Justified, AlbumLayout::Masonry,
            AlbumLayout::Mosaic, AlbumLayout::Filmstrip, AlbumLayout::Cards
        };
        auto layoutRow = MakeRow("album_layout_row");
        AppendLabeledButtons(layoutRow, "album_layout_", "Layout", kLabelW, 110, kBtnH,
                      {"Uniform Grid", "Justified", "Masonry", "Mosaic", "Filmstrip", "Cards"},
                      [albumPtr, statusPtr, layoutVals](int i) {
                          albumPtr->SetLayout(layoutVals[i]);
                          const char* names[] = {"Uniform Grid","Justified","Masonry",
                                                 "Mosaic","Filmstrip","Cards"};
                          std::ostringstream o; o << "Layout: " << names[i];
                          statusPtr->SetText(o.str());
                      }, 0);  // UniformGrid is the default
        controls->AddChild(layoutRow);

        // ----- Crop-focus picker (built first so the image-fit callback can
        //       enable / disable it). The focus point only has a visible effect
        //       under the Crop / Zoom fits, so it is greyed out otherwise. -----
        auto focusRow = MakeRow("album_focus_row");
        auto focusBtns = AppendLabeledButtons(focusRow, "album_focus_", "Crop focus",
                      kLabelW, 110, kBtnH,
                      {"Top-left", "Center", "Bottom-right"},
                      [albumPtr](int i) {
                          const Point2Df fv[] = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}};
                          albumPtr->SetAllItemsFocus(fv[i]);
                      }, 1);  // Center is the default focus
        auto setFocusEnabled = [focusBtns](bool enabled) {
            for (auto& b : focusBtns) b->SetDisabled(!enabled);
        };

        // ----- Image-fit picker -----
        const AlbumImageDisplay fitVals[] = {
            AlbumImageDisplay::Crop, AlbumImageDisplay::Zoom,
            AlbumImageDisplay::Stretch, AlbumImageDisplay::Fit
        };
        auto fitRow = MakeRow("album_fit_row");
        AppendLabeledButtons(fitRow, "album_fit_", "Image fit", kLabelW, 110, kBtnH,
                      {"Crop", "Zoom", "Stretch", "Fit"},
                      [albumPtr, fitVals, setFocusEnabled](int i) {
                          albumPtr->SetImageDisplay(fitVals[i]);
                          // Crop focus applies to the Crop (and Zoom) fits only.
                          setFocusEnabled(fitVals[i] == AlbumImageDisplay::Crop ||
                                          fitVals[i] == AlbumImageDisplay::Zoom);
                      }, 0);  // Crop is the default image fit
        controls->AddChild(fitRow);

        // Crop is the default image fit, so the focus picker starts enabled.
        setFocusEnabled(true);
        controls->AddChild(focusRow);

        // ----- Mode picker -----
        const AlbumMode modeVals[] = { AlbumMode::Display, AlbumMode::UserEdit, AlbumMode::Admin };
        auto modeRow = MakeRow("album_mode_row");
        AppendLabeledButtons(modeRow, "album_mode_", "Mode", kLabelW, 130, kBtnH,
                      {"Visitor", "User", "Admin"},
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
                      }, 0);  // Display (visitor) is the default
        controls->AddChild(modeRow);

        // ----- Action-icon display picker -----
        auto actRow = MakeRow("album_act_row");
        AppendLabeledButtons(actRow, "album_act_", "Show menu", kLabelW, 130, kBtnH,
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
                      }, 1);  // On hover is the default
        controls->AddChild(actRow);

        // ----- Action-icon corner picker (image vs caption text-block) -----
        const AlbumActionAnchor anchorVals[] = {
            AlbumActionAnchor::TopLeftImage,     AlbumActionAnchor::TopRightImage,
            AlbumActionAnchor::BottomLeftImage,  AlbumActionAnchor::BottomRightImage,
            AlbumActionAnchor::TopLeftTextBlock, AlbumActionAnchor::TopRightTextBlock,
            AlbumActionAnchor::BottomLeftTextBlock, AlbumActionAnchor::BottomRightTextBlock
        };
        auto anchorRow = MakeRow("album_anchor_row");
        AppendLabeledButtons(anchorRow, "album_anchor_", "Icon corner", kLabelW, 70, kBtnH,
                      {"Img TL", "Img TR", "Img BL", "Img BR",
                       "Txt TL", "Txt TR", "Txt BL", "Txt BR"},
                      [albumPtr, anchorVals](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          c.actionAnchor = anchorVals[i];
                          albumPtr->SetConfig(c);
                      }, 1);  // Top-right of the image is the default
        anchorRow->AddSpacer(24);
        const AlbumActionIconBackground iconBgVals[] = {
            AlbumActionIconBackground::Round,
            AlbumActionIconBackground::Square,
            AlbumActionIconBackground::RoundedSquare
        };
        AppendLabeledButtons(anchorRow, "album_iconbg_", "Icon bg", 56, 84, kBtnH,
                      {"Round", "Square", "Rounded"},
                      [albumPtr, iconBgVals](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          c.actionIconBackground = iconBgVals[i];
                          albumPtr->SetConfig(c);
                      }, 0);  // Round is the default
        controls->AddChild(anchorRow);

        // ----- Sizing + captions on one line -----
        auto sizeRow = MakeRow("album_size_row");
        AppendLabeledButtons(sizeRow, "album_cols_", "Per row", kLabelW, 64, kBtnH,
                      {"3", "4", "5", "6"},
                      [albumPtr](int i) { albumPtr->SetItemsPerRow(3 + i); }, 1);  // 4 per row
        sizeRow->AddSpacer(24);
        AppendLabeledButtons(sizeRow, "album_cap_", "Caption", 70, 92, kBtnH,
                      {"Below", "Overlay", "Hidden"},
                      [albumPtr](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          c.captionPlacement = (i == 0) ? AlbumCaptionPlacement::BelowImage
                                             : (i == 1) ? AlbumCaptionPlacement::OverlayBottom
                                                        : AlbumCaptionPlacement::Hidden;
                          albumPtr->SetConfig(c);
                      }, 0);  // Below is the default
        sizeRow->AddSpacer(24);
        AppendLabeledButtons(sizeRow, "album_imgcorner_", "Corners", 64, 86, kBtnH,
                      {"Square", "Rounded"},
                      [albumPtr](int i) {
                          AlbumConfig c = albumPtr->GetConfig();
                          // Square: flatten both the frame and the image corners.
                          // Rounded: a rounded frame with the image following it.
                          c.cornerRadius      = (i == 0) ? 0.0f : 6.0f;
                          c.imageCornerRadius = (i == 0) ? 0.0f : -1.0f;
                          albumPtr->SetConfig(c);
                      }, 1);  // Rounded is the default
        controls->AddChild(sizeRow);

        root->AddChild(controls);

        return root;
    }

} // namespace UltraCanvas
