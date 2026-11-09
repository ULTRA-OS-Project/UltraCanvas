// Apps/UltraDesktop/ui/UltraDesktopDesktopIcons.cpp
// The Desktop folder as icons on the wallpaper. See UltraDesktopDesktopIcons.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopDesktopIcons.h"
#include "UltraDesktopCategoryBoard.h"   // MakeFilePicture
#include "UltraDesktopWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTrash.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace UltraDesktop {
namespace {

constexpr float kTileWidth = 88;
constexpr float kTileHeight = 92;
constexpr float kPicture = 48;
constexpr float kCellWidth = 96;     // the grid new icons are placed on
constexpr float kCellHeight = 100;
constexpr float kMargin = 16;
constexpr float kGridTop = 56;       // below the notes' "+" in the top-left corner
constexpr float kDragStart = 4;      // pixels the pointer moves before a press is a drag
constexpr size_t kNameShown = 20;    // bytes under the icon (two lines); the tooltip has the rest

// Long names are cut, at a character boundary, with "…".
std::string ShortName(const std::string& name) {
    if (name.size() <= kNameShown) return name;
    size_t cut = kNameShown - 1;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
    return name.substr(0, cut) + "…";
}

// "photo.jpg" -> "photo (2).jpg" ... the first name not taken in `folder`.
fs::path FreeName(const fs::path& folder, const fs::path& name) {
    std::error_code ec;
    fs::path target = folder / name;
    if (!fs::exists(target, ec)) return target;
    const std::string stem = PathToUtf8(name.stem());
    const std::string ext = PathToUtf8(name.extension());
    for (int n = 2; n < 10000; ++n) {
        target = folder / PathFromUtf8(stem + " (" + std::to_string(n) + ")" + ext);
        if (!fs::exists(target, ec)) return target;
    }
    return folder / name;
}

} // namespace

UltraDesktopDesktopIcons::UltraDesktopDesktopIcons(const std::string& identifier, UltraDesktopWindow* desktop)
    : UltraCanvasContainer(identifier, 0, 0, 0, 0), desktop_(desktop) {
    SetBackgroundColor(Colors::Transparent);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    SetContainerStyle(plain);
}

UltraDesktopDesktopIcons::~UltraDesktopDesktopIcons() {
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        if (dragging_ || !pressedName_.empty()) app->ReleaseMouse();
    }
}

std::string UltraDesktopDesktopIcons::DesktopFolder() {
    for (const UserFolderInfo& folder : GetWellKnownUserFolders()) {
        if (folder.kind == UserFolderKind::Desktop) return folder.path;
    }
    std::string home = GetEnvUtf8("HOME");
    if (home.empty()) home = GetEnvUtf8("USERPROFILE");
    if (home.empty()) return "";
    return PathToUtf8(PathFromUtf8(home) / "Desktop");
}

// ===== READING THE FOLDER =====

void UltraDesktopDesktopIcons::Refresh(bool force) {
    const std::string folder = DesktopFolder();
    if (folder.empty()) return;
    std::error_code ec;
    const fs::path dir = PathFromUtf8(folder);
    const auto stamp = fs::last_write_time(dir, ec);
    if (ec) {
        // No Desktop folder (yet): no icons.
        if (!icons_.empty()) {
            for (auto& [name, icon] : icons_) RemoveChild(icon.tile);
            icons_.clear();
            RequestRedraw();
        }
        haveStamp_ = false;
        return;
    }
    if (!force && haveStamp_ && stamp == folderStamp_) return;
    folderStamp_ = stamp;
    haveStamp_ = true;

    std::set<std::string> present;
    std::vector<std::pair<std::string, std::string>> fresh;   // name, path
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const fs::path& entry = it->path();
        if (IsHiddenFileSystemEntry(entry)) continue;
        const std::string name = PathToUtf8(entry.filename());
        if (name.empty()) continue;
        present.insert(name);
        if (icons_.count(name) == 0) fresh.emplace_back(name, PathToUtf8(entry));
    }
    bool changed = false;
    for (auto it = icons_.begin(); it != icons_.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        RemoveChild(it->second.tile);
        if (selected_ == it->first) selected_.clear();
        it = icons_.erase(it);
        changed = true;
    }
    // New entries in name order, so the grid fills predictably.
    std::sort(fresh.begin(), fresh.end());
    for (const auto& [name, path] : fresh) {
        BuildIcon(name, path);
        changed = true;
    }
    // The places of entries that are gone are not kept.
    if (desktop_) {
        auto& places = desktop_->Settings().desktopIcons;
        bool pruned = false;
        for (auto it = places.begin(); it != places.end();) {
            if (present.count(it->first)) {
                ++it;
            } else {
                it = places.erase(it);
                pruned = true;
            }
        }
        if (pruned) desktop_->SaveSettings();
    }
    if (changed) {
        InvalidateLayout();
        RequestRedraw();
    }
}

void UltraDesktopDesktopIcons::BuildIcon(const std::string& name, const std::string& path) {
    Icon icon;
    icon.name = name;
    icon.path = path;
    const std::string id = "DesktopIcon." + std::to_string(icons_.size()) + "." + std::to_string(std::hash<std::string>{}(name));

    icon.tile = CreateContainer(id, 0, 0, kTileWidth, kTileHeight);
    icon.tile->SetBackgroundColor(Colors::Transparent);
    icon.tile->SetBorders(1, Colors::Transparent, 6);
    icon.tile->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(2);
    icon.tile->SetPadding(4);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    icon.tile->SetContainerStyle(plain);
    icon.tile->SetTooltip(name);

    icon.picture = std::make_shared<UltraCanvasImageElement>(id + ".Picture", 0, 0, kPicture, kPicture);
    icon.picture->SetFitMode(ImageFitMode::Contain);
    icon.picture->SetElementSize(Size2Df(kPicture, kPicture));
    icon.picture->layoutItem.SetFlexShrink(0.0f);
    icon.tile->AddChild(icon.picture);

    icon.label = CreateLabel(id + ".Name", ShortName(name));
    icon.label->SetFontSize(11.0f);
    icon.label->SetTextColor(Colors::White);
    icon.label->SetWrap(TextWrap::WrapWord);
    icon.label->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
    LabelStyle style = icon.label->GetStyle();
    style.hasShadow = true;
    style.shadowColor = Color(0, 0, 0, 200);
    style.shadowOffset = Point2Di(1, 1);
    icon.label->SetStyle(style);
    icon.label->SetElementSize(CSSLayout::Dimension::Px(kTileWidth - 8), CSSLayout::Dimension::Auto());
    icon.label->SetPadding(0);
    icon.tile->AddChild(icon.label);

    Point2Df at;
    bool placed = false;
    if (desktop_) {
        const auto& places = desktop_->Settings().desktopIcons;
        auto found = places.find(name);
        if (found != places.end()) {
            at = Point2Df(found->second.x, found->second.y);
            placed = true;
        }
    }
    if (!placed) at = FreeCell();
    AddChild(icon.tile);
    icons_[name] = icon;
    PlaceIcon(icons_[name], at);
    LoadPicture(name);
}

void UltraDesktopDesktopIcons::LoadPicture(const std::string& name) {
    auto found = icons_.find(name);
    if (found == icons_.end()) return;
    const std::string path = found->second.path;
    const std::string icons = NormalizePath(GetResourcesDir() + "media/icons/");
    const int pixels = static_cast<int>(kPicture);
    // Off the UI thread: a photo decodes, the system is asked for an icon.
    work_.Run<std::shared_ptr<UCPixmap>>(
        [path, pixels, icons]() { return MakeFilePicture(path, pixels, icons); },
        [this, name, path](std::shared_ptr<UCPixmap> pixmap) {
            auto icon = icons_.find(name);
            if (icon == icons_.end() || icon->second.path != path || !pixmap) return;
            icon->second.picture->LoadFromPixmap(pixmap);
        });
}

void UltraDesktopDesktopIcons::PlaceIcon(Icon& icon, const Point2Df& at) {
    icon.place = Point2Df(std::max(0.0f, at.x), std::max(0.0f, at.y));
    PlaceChildAt(icon.tile, Rect2Df(icon.place.x, icon.place.y, kTileWidth, kTileHeight));
}

Point2Df UltraDesktopDesktopIcons::FreeCell() const {
    const Rect2Df area = GetLocalBounds();
    const float height = area.height > kCellHeight ? area.height : 600.0f;
    const int rows = std::max(1, static_cast<int>((height - kGridTop) / kCellHeight));
    for (int column = 0; column < 200; ++column) {
        for (int row = 0; row < rows; ++row) {
            const Rect2Df cell(kMargin + column * kCellWidth, kGridTop + row * kCellHeight, kTileWidth, kTileHeight);
            bool taken = false;
            for (const auto& [name, icon] : icons_) {
                const Rect2Df other(icon.place.x, icon.place.y, kTileWidth, kTileHeight);
                if (other.Intersects(cell)) {
                    taken = true;
                    break;
                }
            }
            if (!taken) return Point2Df(cell.x, cell.y);
        }
    }
    return Point2Df(kMargin, kGridTop);
}

void UltraDesktopDesktopIcons::ArrangeIcons() {
    std::vector<std::string> names;
    for (const auto& [name, icon] : icons_) names.push_back(name);   // a map: in name order
    // Everything off the grid first, so FreeCell sees an empty desktop.
    for (auto& [name, icon] : icons_) icon.place = Point2Df(-10000, -10000);
    for (const std::string& name : names) PlaceIcon(icons_[name], FreeCell());
    if (desktop_) {
        desktop_->Settings().desktopIcons.clear();
        desktop_->SaveSettings();
    }
    InvalidateLayout();
    RequestRedraw();
}

void UltraDesktopDesktopIcons::SavePlace(const Icon& icon) {
    if (!desktop_) return;
    DesktopSettings::IconPlace place;
    place.x = icon.place.x;
    place.y = icon.place.y;
    desktop_->Settings().desktopIcons[icon.name] = place;
    desktop_->SaveSettings();
}

// ===== FILES DROPPED =====

bool UltraDesktopDesktopIcons::TakeFiles(const std::vector<std::string>& files, const Point2Df& at,
                                         std::string& error) {
    const std::string folderText = DesktopFolder();
    if (folderText.empty()) {
        error = Tr("There is no Desktop folder to put the files in.");
        return false;
    }
    std::error_code ec;
    const fs::path folder = PathFromUtf8(folderText);
    fs::create_directories(folder, ec);
    ec.clear();
    bool ok = true;
    Point2Df next = at;
    std::vector<std::pair<std::string, Point2Df>> placed;
    for (const std::string& file : files) {
        if (file.empty()) continue;
        fs::path source = PathFromUtf8(file);
        if (!source.has_filename()) source = source.parent_path();
        if (!fs::exists(source, ec)) continue;
        fs::path target;
        if (fs::equivalent(source.parent_path(), folder, ec)) {
            target = source;   // on the Desktop already: only its place changes
        } else {
            target = FreeName(folder, source.filename());
            fs::rename(source, target, ec);
            if (ec) {
                // Another drive: copied, the original left where it was.
                ec.clear();
                fs::copy(source, target, fs::copy_options::recursive, ec);
                if (ec) {
                    error = PathToUtf8(source.filename()) + ": " + ec.message();
                    ok = false;
                    ec.clear();
                    continue;
                }
            }
        }
        placed.emplace_back(PathToUtf8(target.filename()), next);
        next = Point2Df(next.x + kCellWidth / 3, next.y + kCellHeight / 3);
    }
    // The dropped files lie where they were dropped (the icon centred under
    // the pointer), the next ones a little below.
    if (desktop_) {
        for (const auto& [name, point] : placed) {
            DesktopSettings::IconPlace place;
            place.x = std::max(0.0f, point.x - kTileWidth / 2);
            place.y = std::max(0.0f, point.y - kPicture / 2);
            desktop_->Settings().desktopIcons[name] = place;
            auto icon = icons_.find(name);
            if (icon != icons_.end()) PlaceIcon(icon->second, Point2Df(place.x, place.y));
        }
        desktop_->SaveSettings();
    }
    Refresh(true);
    if (!placed.empty()) Select(placed.back().first);
    InvalidateLayout();
    RequestRedraw();
    return ok;
}

// ===== WHAT AN ICON DOES =====

void UltraDesktopDesktopIcons::Select(const std::string& name) {
    if (selected_ == name) return;
    auto old = icons_.find(selected_);
    if (old != icons_.end()) {
        old->second.tile->SetBackgroundColor(Colors::Transparent);
        old->second.tile->SetBorders(1, Colors::Transparent, 6);
    }
    selected_ = name;
    auto now = icons_.find(selected_);
    if (now != icons_.end()) {
        now->second.tile->SetBackgroundColor(Color(255, 255, 255, 60));
        now->second.tile->SetBorders(1, Color(255, 255, 255, 140), 6);
    }
    RequestRedraw();
}

void UltraDesktopDesktopIcons::Open(const std::string& name) {
    auto found = icons_.find(name);
    if (found == icons_.end()) return;
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication({found->second.path}, error)) {
        UltraCanvasDialogManager::ShowWarning(TrF("\"{0}\" could not be opened: {1}", name, error), Tr("Desktop"),
                                              nullptr, GetWindow());
    }
}

void UltraDesktopDesktopIcons::Delete(const std::string& name) {
    auto found = icons_.find(name);
    if (found == icons_.end()) return;
    const std::string file = found->second.path;
    const bool trash = TrashAvailable();
    const std::string question = trash ? TrF("Move \"{0}\" to the {1}?", name, TrashDisplayName())
                                       : TrF("Delete \"{0}\" for good? It cannot be brought back.", name);
    UltraCanvasDialogManager::ShowConfirmation(question, Tr("Desktop"), [this, name, file, trash](bool yes) {
        if (!yes) return;
        std::string error;
        bool gone = false;
        if (trash) {
            gone = MoveToTrash(file, error);
        } else {
            std::error_code ec;
            fs::remove_all(PathFromUtf8(file), ec);
            gone = !ec;
            if (ec) error = ec.message();
        }
        if (!gone) {
            UltraCanvasDialogManager::ShowWarning(TrF("\"{0}\" was not deleted: {1}", name, error), Tr("Desktop"),
                                                  nullptr, GetWindow());
            return;
        }
        Refresh(true);
    }, GetWindow());
}

void UltraDesktopDesktopIcons::ShowMenu(const std::string& name, int windowX, int windowY) {
    auto* window = GetWindow();
    if (!window) return;
    menu_ = std::make_shared<UltraCanvasMenu>("DesktopIconMenu", 0, 0, 240, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action(Tr("Open"), [this, name]() { Open(name); }));
    menu_->AddItem(MenuItemData::Action(Tr("Show in the file manager"), [this]() {
        if (desktop_) desktop_->OpenFiler(DesktopFolder());
    }));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::Action(TrashAvailable() ? Tr("Delete (move to the trash)") : Tr("Delete"),
                                        [this, name]() { Delete(name); }));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::Action(Tr("Arrange icons"), [this]() { ArrangeIcons(); }));
    PopupElementSettings settings;
    menu_->OpenMenu(Point2Di(windowX, windowY), *window, settings);
}

// ===== EVENTS =====

UltraDesktopDesktopIcons::Icon* UltraDesktopDesktopIcons::IconAt(const Point2Df& point) {
    for (auto& [name, icon] : icons_) {
        if (icon.tile && icon.tile->GetBounds().Contains(point)) return &icon;
    }
    return nullptr;
}

bool UltraDesktopDesktopIcons::Contains(const Point2Df& point) {
    if (!IsVisible()) return false;
    if (dragging_ || !pressedName_.empty()) return true;   // the drag follows the pointer anywhere
    for (const auto& [name, icon] : icons_) {
        if (icon.tile && icon.tile->GetBounds().Contains(point)) return true;
    }
    return false;
}

bool UltraDesktopDesktopIcons::OnEvent(const UCEvent& event) {
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::MouseDown: {
            Icon* icon = IconAt(event.pointer);
            if (!icon) break;
            Select(icon->name);
            if (event.button == UCMouseButton::Right) {
                rightPressPending_ = true;   // the menu opens on the release
                return true;
            }
            if (event.button == UCMouseButton::Left) {
                pressedName_ = icon->name;
                pressPoint_ = event.pointer;
                dragOffset_ = Point2Df(event.pointer.x - icon->place.x, event.pointer.y - icon->place.y);
                dragging_ = false;
                if (app) app->CaptureMouse(this);
                return true;
            }
            break;
        }
        case UCEventType::MouseDoubleClick: {
            if (Icon* icon = IconAt(event.pointer)) {
                Open(icon->name);
                return true;
            }
            break;
        }
        case UCEventType::MouseMove:
            if (!pressedName_.empty()) {
                if (!dragging_ && std::hypot(event.pointer.x - pressPoint_.x, event.pointer.y - pressPoint_.y) >= kDragStart) {
                    dragging_ = true;
                }
                if (dragging_) {
                    auto found = icons_.find(pressedName_);
                    if (found != icons_.end()) {
                        PlaceIcon(found->second, Point2Df(event.pointer.x - dragOffset_.x, event.pointer.y - dragOffset_.y));
                        InvalidateLayout();
                        RequestRedraw();
                    }
                }
                return true;
            }
            break;
        case UCEventType::MouseUp:
            if (event.button == UCMouseButton::Right && rightPressPending_) {
                rightPressPending_ = false;
                if (Icon* icon = IconAt(event.pointer)) {
                    ShowMenu(icon->name, event.pointerWindow.x, event.pointerWindow.y);
                }
                return true;
            }
            if (!pressedName_.empty()) {
                const std::string name = pressedName_;
                const bool moved = dragging_;
                pressedName_.clear();
                dragging_ = false;
                if (app) app->ReleaseMouse();
                if (moved) {
                    auto found = icons_.find(name);
                    if (found != icons_.end()) SavePlace(found->second);
                }
                return true;
            }
            break;
        case UCEventType::DragEnter:
        case UCEventType::DragOver:
            return true;   // a drop on an icon goes where a drop on the wallpaper goes
        case UCEventType::Drop:
            if (event.droppedFiles.empty() || !desktop_) return false;
            desktop_->DropFiles(event.droppedFiles, event.pointer);
            return true;
        default:
            break;
    }
    return UltraCanvasContainer::OnEvent(event);
}

} // namespace UltraDesktop
