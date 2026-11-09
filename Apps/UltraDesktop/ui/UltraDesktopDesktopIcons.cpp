// Apps/UltraDesktop/ui/UltraDesktopDesktopIcons.cpp
// The Desktop folder as icons on the wallpaper. See UltraDesktopDesktopIcons.h.
// Version: 0.3.0 - Delete, Ctrl+A, Enter, Escape
// Version: 0.2.0 - several icons selected (Ctrl+click, Shift+click, a rubber
//                  band from the wallpaper), moved together, and dragged out
//                  to another program as a native file drag
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopDesktopIcons.h"
#include "UltraDesktopCategoryBoard.h"   // MakeFilePicture
#include "UltraDesktopTasksWindow.h"   // Listed: the windows of other programs
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
        if (banding_ || dragging_ || !pressedName_.empty()) app->ReleaseMouse();
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
        selected_.erase(it->first);
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
    // The pointer's presses are the layer's: it selects, drags and opens.
    // A tile that took them (its picture would) would keep them from it.
    icon.tile->SetInteractive(false);

    icon.picture = std::make_shared<UltraCanvasImageElement>(id + ".Picture", 0, 0, kPicture, kPicture);
    icon.picture->SetFitMode(ImageFitMode::Contain);
    icon.picture->SetElementSize(Size2Df(kPicture, kPicture));
    icon.picture->layoutItem.SetFlexShrink(0.0f);
    icon.picture->SetInteractive(false);
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
    icon.label->SetInteractive(false);
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
    if (!placed.empty()) {
        std::set<std::string> dropped;
        for (const auto& [name, point] : placed) dropped.insert(name);
        SetSelection(dropped);
    }
    InvalidateLayout();
    RequestRedraw();
    return ok;
}

// ===== THE SELECTION =====

void UltraDesktopDesktopIcons::SetSelection(const std::set<std::string>& names) {
    std::set<std::string> wanted;
    for (const std::string& name : names) {
        if (icons_.count(name)) wanted.insert(name);
    }
    if (wanted == selected_) return;
    selected_ = std::move(wanted);
    for (auto& [name, icon] : icons_) {
        const bool on = selected_.count(name) > 0;
        icon.tile->SetBackgroundColor(on ? Color(255, 255, 255, 60) : Colors::Transparent);
        icon.tile->SetBorders(1, on ? Color(255, 255, 255, 140) : Colors::Transparent, 6);
    }
    RequestRedraw();
}

void UltraDesktopDesktopIcons::ClearSelection() {
    SetSelection({});
}

std::vector<std::string> UltraDesktopDesktopIcons::SelectedPaths() const {
    std::vector<std::string> paths;
    for (const std::string& name : selected_) {
        auto found = icons_.find(name);
        if (found != icons_.end()) paths.push_back(found->second.path);
    }
    return paths;
}

// ===== WHAT THE ICONS DO =====

void UltraDesktopDesktopIcons::OpenSelected() {
    const std::vector<std::string> paths = SelectedPaths();
    if (paths.empty()) return;
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication(paths, error)) {
        UltraCanvasDialogManager::ShowWarning(TrF("Not opened: {0}", error), Tr("Desktop"), nullptr, GetWindow());
    }
}

void UltraDesktopDesktopIcons::DeleteSelected() {
    const std::vector<std::string> files = SelectedPaths();
    if (files.empty()) return;
    const bool trash = TrashAvailable();
    const std::string what = files.size() == 1 ? *selected_.begin()
                                               : TrF("these {0} items", std::to_string(files.size()));
    const std::string question =
            trash ? (files.size() == 1 ? TrF("Move \"{0}\" to the {1}?", what, TrashDisplayName())
                                       : TrF("Move {0} to the {1}?", what, TrashDisplayName()))
                  : (files.size() == 1 ? TrF("Delete \"{0}\" for good? It cannot be brought back.", what)
                                       : TrF("Delete {0} for good? They cannot be brought back.", what));
    UltraCanvasDialogManager::ShowConfirmation(question, Tr("Desktop"), [this, files, trash](bool yes) {
        if (!yes) return;
        std::string failures;
        for (const std::string& file : files) {
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
            if (!gone) failures += "\n" + PathToUtf8(PathFromUtf8(file).filename()) + ": " + error;
        }
        Refresh(true);
        if (!failures.empty()) {
            UltraCanvasDialogManager::ShowWarning(Tr("Not deleted:") + failures, Tr("Desktop"), nullptr, GetWindow());
        }
    }, GetWindow());
}

void UltraDesktopDesktopIcons::ShowMenu(int windowX, int windowY) {
    auto* window = GetWindow();
    if (!window || selected_.empty()) return;
    menu_ = std::make_shared<UltraCanvasMenu>("DesktopIconMenu", 0, 0, 240, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    if (selected_.size() > 1) {
        menu_->AddItem(MenuItemData::Header(TrF("{0} items", std::to_string(selected_.size()))));
    }
    menu_->AddItem(MenuItemData::Action(Tr("Open"), [this]() { OpenSelected(); }));
    menu_->AddItem(MenuItemData::Action(Tr("Show in the file manager"), [this]() {
        if (desktop_) desktop_->OpenFiler(DesktopFolder());
    }));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::Action(TrashAvailable() ? Tr("Delete (move to the trash)") : Tr("Delete"),
                                        [this]() { DeleteSelected(); }));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::Action(Tr("Select all"), [this]() {
        std::set<std::string> all;
        for (const auto& [name, icon] : icons_) all.insert(name);
        SetSelection(all);
    }));
    menu_->AddItem(MenuItemData::Action(Tr("Arrange icons"), [this]() { ArrangeIcons(); }));
    PopupElementSettings settings;
    menu_->OpenMenu(Point2Di(windowX, windowY), *window, settings);
}

// ===== THE RUBBER BAND =====

void UltraDesktopDesktopIcons::BeginRubberBand(const Point2Df& at, bool add) {
    bandStart_ = at;
    bandBase_ = add ? selected_ : std::set<std::string>();
    if (!add) ClearSelection();
    banding_ = true;
    if (!band_) {
        band_ = CreateContainer("DesktopIcons.Band", 0, 0, 1, 1);
        band_->SetBackgroundColor(Color(120, 170, 240, 60));
        band_->SetBorders(1, Color(150, 195, 255, 200), 2);
        ContainerStyle plain;
        plain.autoShowScrollbars = false;
        band_->SetContainerStyle(plain);
        band_->SetInteractive(false);
    }
    band_->SetVisible(false);
    if (!HasChild(band_.get())) AddChild(band_);
    if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
}

void UltraDesktopDesktopIcons::UpdateRubberBand(const Point2Df& to) {
    const Rect2Df rect(std::min(bandStart_.x, to.x), std::min(bandStart_.y, to.y),
                       std::fabs(to.x - bandStart_.x), std::fabs(to.y - bandStart_.y));
    if (rect.width < 2 && rect.height < 2) return;
    band_->SetVisible(true);
    PlaceChildAt(band_, Rect2Df(rect.x, rect.y, std::max(1.0f, rect.width), std::max(1.0f, rect.height)));
    std::set<std::string> hit = bandBase_;
    for (const auto& [name, icon] : icons_) {
        if (Rect2Df(icon.place.x, icon.place.y, kTileWidth, kTileHeight).Intersects(rect)) hit.insert(name);
    }
    SetSelection(hit);
    InvalidateLayout();
    RequestRedraw();
}

void UltraDesktopDesktopIcons::EndRubberBand() {
    banding_ = false;
    if (band_) band_->SetVisible(false);
    if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
    RequestRedraw();
}

// ===== DRAGGING ICONS =====

// Out of the desktop: the pointer reached another program's window. The
// icons go back where they were, and the selected files continue as a
// native drag, which that program (or any other) takes.
void UltraDesktopDesktopIcons::DragOut() {
    for (const auto& [name, start] : dragStart_) {
        auto icon = icons_.find(name);
        if (icon != icons_.end()) PlaceIcon(icon->second, start);
    }
    const std::vector<std::string> paths = SelectedPaths();
    pressedName_.clear();
    dragging_ = false;
    dragStart_.clear();
    if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
    InvalidateLayout();
    RequestRedraw();
    auto* window = GetWindow();
    if (!window || paths.empty()) return;
    std::weak_ptr<UltraCanvasUIElement> weak = weak_from_this();
    window->StartNativeFileDrag(paths, [weak](bool accepted, bool moved) {
        if (!accepted || !moved) return;
        // The other program moved them away: their icons go.
        if (auto self = weak.lock()) static_cast<UltraDesktopDesktopIcons*>(self.get())->Refresh(true);
    });
}

// ===== THE KEYBOARD =====

void UltraDesktopDesktopIcons::CancelDrag() {
    for (const auto& [name, start] : dragStart_) {
        auto icon = icons_.find(name);
        if (icon != icons_.end()) PlaceIcon(icon->second, start);
    }
    dragStart_.clear();
    pressedName_.clear();
    dragging_ = false;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
    InvalidateLayout();
    RequestRedraw();
}

bool UltraDesktopDesktopIcons::OnKey(const UCEvent& event) {
    switch (event.virtualKey) {
        case UCKeys::Delete:
        case UCKeys::NumPadDelete:
            if (selected_.empty() || dragging_ || banding_) return false;
            DeleteSelected();
            return true;
        case UCKeys::Return:
        case UCKeys::NumPadEnter:
            if (selected_.empty() || dragging_ || banding_) return false;
            OpenSelected();
            return true;
        case UCKeys::A: {
            if (!event.ctrl || event.alt) return false;
            std::set<std::string> all;
            for (const auto& [name, icon] : icons_) all.insert(name);
            SetSelection(all);
            return true;
        }
        case UCKeys::Escape:
            if (banding_) {
                // The selection the band started from.
                EndRubberBand();
                SetSelection(bandBase_);
                return true;
            }
            if (!pressedName_.empty()) {
                CancelDrag();
                return true;
            }
            if (selected_.empty()) return false;
            ClearSelection();
            return true;
        default:
            return false;
    }
}

// ===== EVENTS =====

UltraDesktopDesktopIcons::Icon* UltraDesktopDesktopIcons::IconAt(const Point2Df& point) {
    // By the icons' own places, in the pointer's coordinates (the layer's).
    for (auto& [name, icon] : icons_) {
        if (Rect2Df(icon.place.x, icon.place.y, kTileWidth, kTileHeight).Contains(point)) return &icon;
    }
    return nullptr;
}

bool UltraDesktopDesktopIcons::Contains(const Point2Df& point) {
    if (!IsVisible()) return false;
    // A drag or a rubber band follows the pointer anywhere.
    if (banding_ || dragging_ || !pressedName_.empty()) return true;
    for (const auto& [name, icon] : icons_) {
        if (Rect2Df(icon.place.x, icon.place.y, kTileWidth, kTileHeight).Contains(point)) return true;
    }
    return false;
}

bool UltraDesktopDesktopIcons::OnEvent(const UCEvent& event) {
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::KeyDown:
            if (OnKey(event)) return true;
            break;
        case UCEventType::MouseDown: {
            // The keys go to the icons from the first click on them or on
            // the wallpaper between them (the window itself is not raised).
            if (!IsFocused()) SetFocus(true);
            Icon* icon = IconAt(event.pointer);
            if (!icon) {
                // The wallpaper between the icons: the layer lies over it and
                // takes its presses, so it answers for it - a rubber band, or
                // the desktop's menu on the release.
                if (event.button == UCMouseButton::Left) {
                    BeginRubberBand(event.pointer, event.ctrl);
                    return true;
                }
                if (event.button == UCMouseButton::Right) {
                    rightOnGround_ = true;
                    return true;
                }
                break;
            }
            const std::string name = icon->name;
            const bool selected = selected_.count(name) > 0;
            if (event.button == UCMouseButton::Right) {
                if (!selected) SetSelection({name});
                rightPressPending_ = true;   // the menu opens on the release
                return true;
            }
            if (event.button != UCMouseButton::Left) break;
            if (event.ctrl) {
                // Ctrl+click: in or out of the selection, nothing else.
                std::set<std::string> next = selected_;
                if (selected) next.erase(name);
                else next.insert(name);
                SetSelection(next);
                return true;
            }
            if (event.shift) {
                std::set<std::string> next = selected_;
                next.insert(name);
                SetSelection(next);
            } else if (!selected) {
                SetSelection({name});
            }
            // A press on a selected icon may drag them all; a click without a
            // drag selects that one alone on the release.
            pressedName_ = name;
            pressPoint_ = event.pointer;
            dragging_ = false;
            dragStart_.clear();
            for (const std::string& each : selected_) {
                auto found = icons_.find(each);
                if (found != icons_.end()) dragStart_[each] = found->second.place;
            }
            if (desktop_) desktop_->NoteIconDragStart();
            if (app) app->CaptureMouse(this);
            return true;
        }
        case UCEventType::MouseDoubleClick: {
            if (Icon* icon = IconAt(event.pointer)) {
                if (!selected_.count(icon->name)) SetSelection({icon->name});
                OpenSelected();
                return true;
            }
            break;
        }
        case UCEventType::MouseMove:
            if (banding_) {
                UpdateRubberBand(event.pointer);
                return true;
            }
            if (!pressedName_.empty()) {
                const float dx = event.pointer.x - pressPoint_.x;
                const float dy = event.pointer.y - pressPoint_.y;
                if (!dragging_ && std::hypot(dx, dy) >= kDragStart) dragging_ = true;
                if (!dragging_) return true;
                if (desktop_ && desktop_->PointOverOtherWindow(event.pointerWindow)) {
                    DragOut();
                    return true;
                }
                for (const auto& [name, start] : dragStart_) {
                    auto found = icons_.find(name);
                    if (found != icons_.end()) PlaceIcon(found->second, Point2Df(start.x + dx, start.y + dy));
                }
                InvalidateLayout();
                RequestRedraw();
                return true;
            }
            break;
        case UCEventType::MouseUp:
            if (banding_) {
                EndRubberBand();
                return true;
            }
            if (event.button == UCMouseButton::Right && rightOnGround_) {
                rightOnGround_ = false;
                if (desktop_) desktop_->ShowDesktopMenu(event.pointerWindow.x, event.pointerWindow.y);
                return true;
            }
            if (event.button == UCMouseButton::Right && rightPressPending_) {
                rightPressPending_ = false;
                if (IconAt(event.pointer)) ShowMenu(event.pointerWindow.x, event.pointerWindow.y);
                return true;
            }
            if (!pressedName_.empty()) {
                const std::string name = pressedName_;
                const bool moved = dragging_;
                pressedName_.clear();
                dragging_ = false;
                if (app) app->ReleaseMouse();
                if (moved) {
                    for (const auto& [each, start] : dragStart_) {
                        auto found = icons_.find(each);
                        if (found != icons_.end()) SavePlace(found->second);
                    }
                } else if (!event.shift && selected_.size() > 1) {
                    SetSelection({name});
                }
                dragStart_.clear();
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
