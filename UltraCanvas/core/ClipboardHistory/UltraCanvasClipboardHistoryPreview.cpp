// core/ClipboardHistory/UltraCanvasClipboardHistoryPreview.cpp
// The hover preview of a clipboard history list. See
// include/UltraCanvasClipboardHistoryPreview.h.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardHistoryPreview.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboardHistoryView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace UltraCanvas {
namespace {

constexpr int kDelayMs = 150;    // the pointer rests on a thumbnail this long before the preview opens
constexpr int kTickMs = 250;     // the leave check, and following a resize
constexpr const char* kFilterId = "clipboard-history-preview-pointer";

} // namespace

ClipboardHistoryPreview::ClipboardHistoryPreview(UltraCanvasClipboardHistory* history, UltraCanvasListView* list,
                                                 ClipboardHistoryListModel* model, ClipboardHistoryRowDelegate* rows)
    : history_(history), list_(list), model_(model), rows_(rows) {}

ClipboardHistoryPreview::~ClipboardHistoryPreview() {
    auto* app = UltraCanvasApplication::GetInstance();
    if (app) {
        if (delayTimer_) app->StopTimer(delayTimer_);
        if (tickTimer_) app->StopTimer(tickTimer_);
    }
    if (window_) window_->UnInstallWindowEventFilter(kFilterId);
    if (viewer_) viewer_->CloseFile();
    // The images written for the viewer are this preview's; the folder stays
    // (the UltraPaint hand-off shares it).
    std::error_code ec;
    for (const auto& [id, file] : files_) std::filesystem::remove(PathFromUtf8(file), ec);
}

// A frame with the media viewer inside: the frame's padding is the 12 px
// border around the picture, so a small image still arrives as a visible
// panel. Out of the window's flow (absolute), and hidden until a thumbnail
// is rested on.
void ClipboardHistoryPreview::Build() {
    const ClipboardRowStyle& look = rows_->Style();
    panel_ = std::make_shared<UltraCanvasContainer>("clipboard-history-preview");
    panel_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    panel_->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute);
    panel_->SetPadding(frame_);
    panel_->SetBackgroundColor(look.previewFrame);
    panel_->SetBorders(1, look.previewBorder);
    panel_->SetVisible(false);

    viewer_ = CreateMediaViewer("clipboard-history-preview-viewer", 0, 0, 0, 0);
    viewer_->SetTopBarsVisible(false);        // a preview: the picture and, in Modern, its info line
    viewer_->SetGrabFocusOnAttach(false);     // the host's search field keeps the keyboard
    viewer_->SetTransparentBackground(TransparentImageBackground::Checkered);
    viewer_->SetFitEnlarges(false);           // a small image at its own size; the wheel zooms
    viewer_->SetVideoPreviewMode(VideoPreviewMode::Still);   // a copied video: its first frame, silent
    viewer_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    panel_->AddChild(viewer_);
    ApplyStyle();
}

void ClipboardHistoryPreview::Attach(UltraCanvasWindowBase* window) {
    if (!window || window_ || !list_ || !model_ || !rows_) return;
    window_ = window;
    Build();
    // Last, so it is drawn over the list and takes the pointer before it.
    window_->AddChild(panel_);
    window_->InstallEventFilter(kFilterId, [this](const UCEvent& event) { return OnPointerMoved(event); },
                                {UCEventType::MouseMove});
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        tickTimer_ = app->StartTimer(kTickMs, true, [this](TimerId) { Tick(); });
    }
}

// From the list, on every pointer move over it and again when it scrolls
// under a still pointer: a thumbnail opens its entry's preview; the preview
// stays while the pointer is on that entry's row, or on the preview itself.
// The list reports no row when the pointer moves onto the preview (a leave,
// dispatched before the move itself reaches OnPointerMoved), below its rows,
// or out of the window: the move that follows settles which, and Tick the
// case where none follows.
void ClipboardHistoryPreview::OnCellHovered(int row, const Point2Di& posInCell) {
    if (!window_) return;
    const ClipboardHistoryEntry* entry = row >= 0 ? model_->GetEntry(row) : nullptr;
    if (entry && rows_->IsOverThumbnail(posInCell)) {
        RequestPreview(entry->id);
        return;
    }
    wantedId_ = 0;
    if (previewId_ == 0) return;
    if (entry && entry->id == previewId_) return;
    if (row < 0) {
        leaveCheck_ = true;
        return;
    }
    Hide();   // another entry's row, a header
}

bool ClipboardHistoryPreview::HandleKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown || event.virtualKey != UCKeys::Escape || previewId_ == 0) return false;
    Hide();
    return true;
}

// Every pointer move in the window, before the element under it sees it:
// where the pointer is, and whether the preview is done - the list lost the
// pointer to somewhere but the preview, or the pointer is off both.
bool ClipboardHistoryPreview::OnPointerMoved(const UCEvent& event) {
    pointer_ = event.pointerWindow;
    if (previewId_ == 0 || !panel_) return false;
    const bool onPreview = panel_->GetBoundsInWindow().Contains(pointer_);
    if (leaveCheck_) {
        leaveCheck_ = false;
        if (!onPreview) Hide();
        return false;
    }
    if (onPreview) return false;
    if (list_->GetBoundsInWindow().Contains(pointer_)) return false;   // the list decides
    Hide();
    return false;
}

void ClipboardHistoryPreview::Tick() {
    // The list lost the pointer and no move followed: it left the window.
    if (leaveCheck_) {
        leaveCheck_ = false;
        if (previewId_ != 0 && panel_ && !panel_->GetBoundsInWindow().Contains(pointer_)) Hide();
    }
    // The window was resized under an open preview: it keeps its share of the list.
    if (previewId_ != 0) {
        const Rect2Df bounds = list_->GetBoundsInWindow();
        if (bounds.x != listBounds_.x || bounds.y != listBounds_.y || bounds.width != listBounds_.width ||
            bounds.height != listBounds_.height) {
            Place();
        }
    }
}

void ClipboardHistoryPreview::RequestPreview(int64_t id) {
    if (id == previewId_) {
        wantedId_ = 0;
        return;
    }
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    // Sweeping down the thumbnails opens nothing until the pointer rests.
    wantedId_ = id;
    if (delayTimer_) app->StopTimer(delayTimer_);
    delayTimer_ = app->StartTimer(kDelayMs, false, [this](TimerId) {
        delayTimer_ = 0;
        const int64_t wanted = wantedId_;
        wantedId_ = 0;
        if (wanted == 0 || wanted == previewId_) return;
        if (const ClipboardHistoryEntry* entry = model_->GetEntry(model_->FindRow(wanted))) Show(*entry);
    });
}

void ClipboardHistoryPreview::Show(const ClipboardHistoryEntry& entry) {
    if (!panel_ || !viewer_) return;
    const std::string file = FileFor(entry);
    if (file.empty()) {
        Hide();
        return;
    }
    previewId_ = entry.id;
    pictureWidth_ = entry.kind == ClipboardEntryKind::Image ? entry.width : 0;
    pictureHeight_ = entry.kind == ClipboardEntryKind::Image ? entry.height : 0;
    Place();
    panel_->SetVisible(true);
    // The row's tooltip (the entry's title) would sit on the thumbnail while
    // the picture itself is beside it; back when the preview goes.
    UltraCanvasTooltipManager::HideTooltip();
    list_->SetShowItemTooltips(false);
    viewer_->SetFiles({file});
    if (window_) window_->RequestRedraw();
}

void ClipboardHistoryPreview::Hide() {
    wantedId_ = 0;
    leaveCheck_ = false;
    if (delayTimer_) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(delayTimer_);
        delayTimer_ = 0;
    }
    if (previewId_ == 0) return;
    previewId_ = 0;
    list_->SetShowItemTooltips(true);
    if (viewer_) viewer_->StopPlayback();
    if (panel_) panel_->SetVisible(false);
    if (window_) window_->RequestRedraw();
}

void ClipboardHistoryPreview::SetStyle(ClipboardPreviewStyle style) {
    if (style == style_) return;
    style_ = style;
    ApplyStyle();
}

void ClipboardHistoryPreview::SetShare(float share) {
    share_ = std::clamp(share, 0.2f, 1.0f);
    if (previewId_ != 0) Place();
}

void ClipboardHistoryPreview::SetFrame(float pixels) {
    frame_ = std::max(0.0f, pixels);
    if (panel_) panel_->SetPadding(frame_);
    if (previewId_ != 0) Place();
}

// Modern keeps the viewer's info line (name, size, zoom) and its backdrop
// strip under a transparent image; Classic is the picture alone.
void ClipboardHistoryPreview::ApplyStyle() {
    if (!viewer_) return;
    const bool modern = style_ == ClipboardPreviewStyle::Modern;
    viewer_->SetInfoBarVisible(modern);
    viewer_->SetTransparencyPaletteVisible(modern);
    if (previewId_ != 0) Place();
}

// Modern: the right share of the list, its full height - the thumbnails and
// the start of every title stay readable on the left. Classic: centred over
// the list, the frame hugging the picture at the size it fits in that share
// of the list's width and the list's height, never enlarged; a file of
// unknown size takes that whole box.
void ClipboardHistoryPreview::Place() {
    if (!panel_) return;
    const Rect2Df list = list_->GetBoundsInWindow();
    listBounds_ = list;
    float width = std::round(list.width * share_);
    float height = list.height;
    float left = list.x + list.width - width;
    float top = list.y;
    if (style_ == ClipboardPreviewStyle::Classic) {
        const float edge = 2 * (frame_ + 1);   // the frame and the 1 px border, both sides
        if (pictureWidth_ > 0 && pictureHeight_ > 0) {
            const float scale = std::min({(width - edge) / pictureWidth_, (height - edge) / pictureHeight_, 1.0f});
            width = std::round(pictureWidth_ * scale) + edge;
            height = std::round(pictureHeight_ * scale) + edge;
        }
        left = std::round(list.x + (list.width - width) / 2);
        top = std::round(list.y + (list.height - height) / 2);
    }
    CSSLayout::Position at;
    at.left = CSSLayout::Dimension::Px(left);
    at.top = CSSLayout::Dimension::Px(top);
    panel_->layoutItem.SetPositionInsets(at);
    panel_->SetElementSize(CSSLayout::Dimension::Px(width), CSSLayout::Dimension::Px(height));
}

std::string ClipboardHistoryPreview::FileFor(const ClipboardHistoryEntry& entry) {
    namespace fs = std::filesystem;
    if (entry.kind == ClipboardEntryKind::Files) {
        // The first file, when the viewer shows its kind (no sound on a hover).
        const std::string first = entry.preview.substr(0, entry.preview.find('\n'));
        std::error_code ec;
        if (first.empty() || !fs::is_regular_file(PathFromUtf8(first), ec)) return "";
        if (!UltraCanvasMediaViewer::IsSupportedMedia(first)) return "";
        switch (UltraCanvasMediaViewer::ClassifyFile(first)) {
            case MediaKind::Image:
            case MediaKind::Document:
            case MediaKind::Vector:
            case MediaKind::Model:
            case MediaKind::Video:
                return first;
            default:
                return "";
        }
    }
    if (entry.kind != ClipboardEntryKind::Image || !history_) return "";
    if (const auto found = files_.find(entry.id); found != files_.end()) {
        std::error_code ec;
        if (fs::is_regular_file(found->second, ec)) return PathToUtf8(found->second);
        files_.erase(found);
    }
    // The image as a file, in the folder images are handed to other programs in.
    std::vector<ClipboardFormat> formats;
    ClipboardSnapshot snapshot;
    if (!history_->ReadFormats(entry.id, formats)) return "";
    snapshot.formats = std::move(formats);
    const ClipboardFormat* image = snapshot.FindImage();
    if (!image) return "";
    std::string extension;
    const std::vector<uint8_t> bytes = ClipboardImageFile(*image, extension);
    const fs::path folder = ClipboardHandOffFolder();
    if (folder.empty()) return "";
    const fs::path file = folder / PathFromUtf8("preview-" + std::to_string(entry.id) + "." + extension);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) return "";
    files_[entry.id] = file;
    return PathToUtf8(file);
}

} // namespace UltraCanvas
