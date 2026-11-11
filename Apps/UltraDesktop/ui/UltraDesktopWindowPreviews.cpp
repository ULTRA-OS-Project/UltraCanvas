// Apps/UltraDesktop/ui/UltraDesktopWindowPreviews.cpp
// The pictures of an application's windows beside its taskbar button. See
// the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopWindowPreviews.h"
#include "UltraDesktopPanelTheme.h"

#include "UltraDesktopPanelWork.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <utility>

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {

// Logical pixels.
constexpr int kPictureW = 208;
constexpr int kPictureH = 128;
constexpr int kHeaderH = 22;
constexpr int kTilePad = 6;
constexpr int kTileW = kPictureW + 2 * kTilePad;
constexpr int kTileH = kTilePad + kHeaderH + 4 + kPictureH + kTilePad;
constexpr int kGap = 6;
constexpr int kPanelPad = 8;
constexpr int kButtonGap = 6;   // between the bar's button and the strip
constexpr size_t kTitleChars = 30;


// `text` cut to `limit` characters (UTF-8 sequences kept whole), with an
// ellipsis when anything was cut.
std::string Shorten(const std::string& text, size_t limit) {
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (chars == limit) return text.substr(0, i) + "\xE2\x80\xA6";   // …
        ++chars;
    }
    return text;
}

using Captures = std::vector<std::pair<uint64_t, DesktopScreenImage>>;

} // namespace

UltraDesktopWindowPreviews::UltraDesktopWindowPreviews() : work_(std::make_unique<UltraDesktopPanelWork>()) {}

UltraDesktopWindowPreviews::~UltraDesktopWindowPreviews() {
    Close();
}

std::vector<uint64_t> UltraDesktopWindowPreviews::WindowIds() const {
    std::vector<uint64_t> ids;
    for (const Tile& tile : tiles_) ids.push_back(tile.item.id);
    return ids;
}

void UltraDesktopWindowPreviews::Show(const std::string& key, const std::vector<WindowPreviewItem>& items,
                                      int buttonX, int buttonY, int buttonW, int buttonH, PreviewSide side,
                                      int areaX, int areaY, int areaW, int areaH, float scale) {
    Close();
    if (items.empty()) return;
    if (scale <= 0.0f) scale = 1.0f;
    ++generation_;
    key_ = key;

    // As many tiles in a line as the monitor holds: a row above or below a
    // bar at the top or bottom, a column beside the bar on the left.
    const int count = static_cast<int>(items.size());
    const int logicalW = areaW > 0 ? static_cast<int>(areaW / scale) : 1920;
    const int logicalH = areaH > 0 ? static_cast<int>(areaH / scale) : 1080;
    int columns = 1, rows = 1;
    if (side == PreviewSide::Right) {
        const int fit = std::max(1, (logicalH - 2 * kPanelPad + kGap) / (kTileH + kGap));
        rows = std::min(count, fit);
        columns = (count + rows - 1) / rows;
    } else {
        const int fit = std::max(1, (logicalW - 2 * kPanelPad + kGap) / (kTileW + kGap));
        columns = std::min(count, fit);
        rows = (count + columns - 1) / columns;
    }
    const int width = 2 * kPanelPad + columns * kTileW + (columns - 1) * kGap;
    const int height = 2 * kPanelPad + rows * kTileH + (rows - 1) * kGap;

    WindowConfig config;
    config.title = Tr("Window previews");
    config.type = WindowType::Notification;   // never takes the focus
    config.width = width;
    config.height = height;
    config.resizable = false;
    config.minimizable = false;
    config.maximizable = false;
    config.minWidth = width;
    config.minHeight = height;
    config.backgroundColor = PanelColors().panel;
    config.alwaysOnTop = true;
    window_ = CreateWindow(config);
    if (!window_) {
        key_.clear();
        return;
    }
    const int physW = window_->LogicalToPhysical(width);
    const int physH = window_->LogicalToPhysical(height);
    const int gap = window_->LogicalToPhysical(kButtonGap);
    int x = 0, y = 0;
    switch (side) {
        case PreviewSide::Right:
            x = buttonX + buttonW + gap;
            y = buttonY + buttonH / 2 - physH / 2;
            break;
        case PreviewSide::Down:
            x = buttonX + buttonW / 2 - physW / 2;
            y = buttonY + buttonH + gap;
            break;
        case PreviewSide::Up:
            x = buttonX + buttonW / 2 - physW / 2;
            y = buttonY - physH - gap;
            break;
    }
    if (areaW > 0) x = std::clamp(x, areaX, std::max(areaX, areaX + areaW - physW));
    if (areaH > 0) y = std::clamp(y, areaY, std::max(areaY, areaY + areaH - physH));
    window_->SetWindowPosition(x, y);
    window_->SetBackgroundColor(PanelColors().panel);
    window_->SetBorders(1.0f, PanelColors().edge);

    UltraCanvasWindow& window = *window_;
    window.layout.SetFlexColumn().SetFlexGap(kGap).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    window.SetPadding(kPanelPad, kPanelPad, kPanelPad, kPanelPad);

    const CSSLayout::Position fill{CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0),
                                   CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0)};
    // Line by line (rows of columns, or columns of rows beside a left bar),
    // the windows in the bar's order.
    const bool byColumn = side == PreviewSide::Right;
    if (byColumn) window.layout.SetFlexRow();
    const int lines = byColumn ? columns : rows;
    const int perLine = byColumn ? rows : columns;
    for (int line = 0, index = 0; line < lines; ++line) {
        auto strip = std::make_shared<UltraCanvasContainer>("previews.line" + std::to_string(line));
        if (byColumn) strip->layout.SetFlexColumn();
        else strip->layout.SetFlexRow();
        strip->layout.SetFlexGap(kGap);
        strip->layoutItem.SetFlexShrink(0.0f);
        window.AddChild(strip);
        for (int i = 0; i < perLine && index < count; ++i, ++index) {
            const WindowPreviewItem& item = items[static_cast<size_t>(index)];
            Tile tile;
            tile.item = item;
            const std::string id = "previews.tile" + std::to_string(index);

            tile.box = std::make_shared<UltraCanvasContainer>(id);
            tile.box->SetElementSize(Size2Df(static_cast<float>(kTileW), static_cast<float>(kTileH)));
            tile.box->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            tile.box->SetPadding(kTilePad, kTilePad, kTilePad, kTilePad);
            tile.box->SetBackgroundColor(PanelColors().tile);
            // The window that has the focus is framed.
            tile.box->SetBorders(item.active && !item.minimized ? 2.0f : 0.0f, PanelColors().active, 6.0f);
            tile.box->SetBorderRadius(6.0f);
            tile.box->layoutItem.SetFlexShrink(0.0f);
            tile.box->SetTooltip(item.title);

            auto header = std::make_shared<UltraCanvasContainer>(id + ".header");
            header->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            header->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kHeaderH));
            header->layoutItem.SetFlexShrink(0.0f);
            if (!item.iconFile.empty()) {
                auto icon = CreateImageElement(id + ".icon", 0, 0, 16, 16);
                icon->SetFitMode(ImageFitMode::Contain);
                icon->LoadFromFile(item.iconFile);
                icon->layoutItem.SetFlexShrink(0.0f);
                header->AddChild(icon);
            }
            std::string title = item.title.empty() ? item.letter : item.title;
            if (item.minimized) title = TrF("{0} (minimized)", title);
            auto label = CreateLabel(id + ".title", 0, 0, 150, kHeaderH, Shorten(title, kTitleChars));
            label->SetFontSize(9.5f);
            label->SetTextColor(PanelColors().text);
            label->SetWrap(TextWrap::WrapNone);
            label->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f);
            header->AddChild(label);
            tile.close = CreateLabel(id + ".close", 0, 0, 18, 18, "\xC3\x97");   // ×
            tile.close->SetFontSize(12.0f);
            tile.close->SetTextColor(PanelColors().muted);
            tile.close->SetAlignment(TextAlignment::Center);
            tile.close->SetTooltip(Tr("Close window"));
            tile.close->layoutItem.SetFlexShrink(0.0f);
            header->AddChild(tile.close);
            tile.box->AddChild(header);

            auto frame = std::make_shared<UltraCanvasContainer>(id + ".frame");
            frame->SetElementSize(Size2Df(static_cast<float>(kPictureW), static_cast<float>(kPictureH)));
            frame->SetBackgroundColor(PanelColors().pictureGround);
            frame->layoutItem.SetFlexShrink(0.0f);
            // Until the picture comes (and for good when the window is
            // minimized): the application's icon, else its letter.
            if (!item.iconFile.empty()) {
                tile.picture = CreateImageElement(id + ".picture", 0, 0, kPictureW, kPictureH);
                tile.picture->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
                tile.picture->SetFitMode(ImageFitMode::ScaleDown);
                tile.picture->LoadFromFile(item.iconFile);
            } else {
                tile.picture = CreateImageElement(id + ".picture", 0, 0, kPictureW, kPictureH);
                tile.picture->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
                tile.picture->SetFitMode(ImageFitMode::Contain);
                auto letter = CreateLabel(id + ".letter", 0, 0, kPictureW, kPictureH, item.letter);
                letter->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
                letter->SetFontSize(36.0f);
                letter->SetFontWeight(FontWeight::Bold);
                letter->SetTextColor(PanelColors().muted);
                letter->SetAlignment(TextAlignment::Center);
                frame->AddChild(letter);
            }
            frame->AddChild(tile.picture);
            tile.box->AddChild(frame);
            strip->AddChild(tile.box);
            tiles_.push_back(std::move(tile));
        }
    }

    window_->onWindowClosed = [this]() {
        window_.reset();
        tiles_.clear();
        key_.clear();
        highlighted_ = -1;
        if (pointerInside_) {
            pointerInside_ = false;
            if (onPointerOutside) onPointerOutside();
        }
    };
    window_->InstallEventFilter("window-previews", [this](const UCEvent& event) { return OnEvent(event); },
                                {UCEventType::MouseMove, UCEventType::MouseEnter, UCEventType::MouseLeave,
                                 UCEventType::MouseDown});
    window_->Show();
    CapturePictures(window_->LogicalToPhysical(kPictureW), window_->LogicalToPhysical(kPictureH));
}

void UltraDesktopWindowPreviews::CapturePictures(int pictureW, int pictureH) {
    std::vector<uint64_t> ids;
    for (const Tile& tile : tiles_) {
        if (!tile.item.minimized) ids.push_back(tile.item.id);
    }
    if (ids.empty()) return;
    const uint64_t generation = generation_;
    work_->Run<Captures>(
        [ids, pictureW, pictureH]() {
            Captures captures;
            for (const uint64_t id : ids) {
                DesktopScreenImage image;
                if (UltraCanvasDesktopShell::CaptureWindowImage(id, pictureW, pictureH, image)) {
                    captures.emplace_back(id, std::move(image));
                }
            }
            return captures;
        },
        [this, generation](Captures captures) {
            if (generation != generation_ || !window_) return;
            for (auto& [id, image] : captures) {
                for (Tile& tile : tiles_) {
                    if (tile.item.id != id || !tile.picture) continue;
                    auto picture = UCImage::FromBgra(image.pixels.data(), image.width, image.height, image.stride,
                                                     /*opaque=*/true);
                    if (!picture) break;
                    tile.picture->SetFitMode(ImageFitMode::Contain);
                    tile.picture->LoadFromImage(picture);
                    break;
                }
            }
            window_->RequestRedraw();
        });
}

void UltraDesktopWindowPreviews::Close() {
    ++generation_;
    if (window_) {
        window_->onWindowClosed = nullptr;
        window_->Close();
        window_.reset();
    }
    tiles_.clear();
    key_.clear();
    highlighted_ = -1;
    pointerInside_ = false;
}

int UltraDesktopWindowPreviews::TileAt(float x, float y) const {
    for (size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i].box && tiles_[i].box->GetBoundsInWindow().Contains(Point2Df(x, y))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool UltraDesktopWindowPreviews::OnCloseMark(int index, float x, float y) const {
    if (index < 0 || index >= static_cast<int>(tiles_.size())) return false;
    const auto& close = tiles_[static_cast<size_t>(index)].close;
    if (!close) return false;
    // A little more than the mark itself: it is small.
    Rect2Df bounds = close->GetBoundsInWindow();
    bounds.x -= 4;
    bounds.y -= 4;
    bounds.width += 8;
    bounds.height += 8;
    return bounds.Contains(Point2Df(x, y));
}

void UltraDesktopWindowPreviews::Highlight(int index) {
    if (index == highlighted_) return;
    if (highlighted_ >= 0 && highlighted_ < static_cast<int>(tiles_.size())) {
        tiles_[static_cast<size_t>(highlighted_)].box->SetBackgroundColor(PanelColors().tile);
        tiles_[static_cast<size_t>(highlighted_)].close->SetTextColor(PanelColors().muted);
    }
    highlighted_ = index;
    if (highlighted_ >= 0 && highlighted_ < static_cast<int>(tiles_.size())) {
        tiles_[static_cast<size_t>(highlighted_)].box->SetBackgroundColor(PanelColors().tileHover);
        tiles_[static_cast<size_t>(highlighted_)].close->SetTextColor(PanelColors().text);
    }
    if (window_) window_->RequestRedraw();
}

bool UltraDesktopWindowPreviews::OnEvent(const UCEvent& event) {
    const float x = static_cast<float>(event.pointerWindow.x);
    const float y = static_cast<float>(event.pointerWindow.y);
    switch (event.type) {
        case UCEventType::MouseEnter:
        case UCEventType::MouseMove:
            if (!pointerInside_) {
                pointerInside_ = true;
                if (onPointerInside) onPointerInside();
            }
            Highlight(TileAt(x, y));
            return false;
        case UCEventType::MouseLeave:
            Highlight(-1);
            if (pointerInside_) {
                pointerInside_ = false;
                if (onPointerOutside) onPointerOutside();
            }
            return false;
        case UCEventType::MouseDown: {
            if (event.button != UCMouseButton::Left) return true;
            const int index = TileAt(x, y);
            if (index < 0) return true;
            Finish(tiles_[static_cast<size_t>(index)].item.id, OnCloseMark(index, x, y));
            return true;
        }
        default:
            return false;
    }
}

void UltraDesktopWindowPreviews::Finish(uint64_t windowId, bool closeIt) {
    // Not from inside the strip's own event: the window goes first, then
    // the choice is carried out.
    const uint64_t generation = generation_;
    UltraCanvasApplication::GetInstance()->StartTimer(1, false, [this, generation, windowId, closeIt](TimerId) {
        if (generation != generation_) return;
        Close();
        if (closeIt) {
            if (onCloseWindow) onCloseWindow(windowId);
        } else if (onChoose) {
            onChoose(windowId);
        }
    });
}

} // namespace UltraDesktop
