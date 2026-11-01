// Apps/UltraDesktop/ui/UltraDesktopAppStarter.cpp
// The Apps window. See the header.
// Version: 0.3.0 - categories, Recent, tiles and tree-and-list, read after the window is open
// Version: 0.2.0 - pin to the taskbar
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopAppStarter.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <thread>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 600;
constexpr float kTileWidth = 116;
constexpr float kTileHeight = 140;   // the icon, then four lines of the name
constexpr int kTileIcon = 48;
constexpr float kGap = 6;
constexpr int kListIcon = 24;
constexpr float kTreeWidth = 210;
constexpr auto kListFresh = std::chrono::minutes(2);   // a kept list older than this is read again

const Color kHeadingColor(40, 40, 40, 255);
const Color kSubheadingColor(105, 105, 105, 255);
const Color kStatusColor(110, 110, 110, 255);

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string IconsDir() {
    return NormalizePath(GetResourcesDir() + "media/icons/appstarter/");
}

// ===== THE KEPT LIST =====
// One list for every Apps window: read once ahead of time by the desktop
// (Preload), shown at once by the next Open and read again behind it.
struct KeptList {
    std::mutex mutex;
    AppList apps;
    std::chrono::steady_clock::time_point read;
    std::atomic<bool> reading{false};
};

KeptList& Kept() {
    static KeptList* kept = new KeptList;   // never destroyed: a reader may outlive main
    return *kept;
}

AppList ReadApplications() {
    auto apps = std::make_shared<const std::vector<UCDesktopEntry>>(
        UltraCanvasDesktopShell::ListApplications(kTileIcon));
    KeptList& kept = Kept();
    std::lock_guard<std::mutex> lock(kept.mutex);
    kept.apps = apps;
    kept.read = std::chrono::steady_clock::now();
    return apps;
}

bool SameApplications(const AppList& a, const AppList& b) {
    if (!a || !b || a->size() != b->size()) return false;
    for (size_t i = 0; i < a->size(); ++i) {
        const UCDesktopEntry& x = (*a)[i];
        const UCDesktopEntry& y = (*b)[i];
        if (x.name != y.name || x.exec != y.exec || x.iconFile != y.iconFile || x.categories != y.categories) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<UltraCanvasLabel> MakeHeading(const std::string& id, const std::string& text, float size,
                                              const Color& color) {
    auto label = CreateLabel(id, text);
    label->SetFontSize(size);
    label->SetFontWeight(FontWeight::Bold);
    label->SetTextColor(color);
    label->layoutItem.SetFlexShrink(0.0f);
    return label;
}

} // namespace

UltraDesktopAppStarter::UltraDesktopAppStarter(Hooks hooks) : hooks_(std::move(hooks)), view_(hooks_.view) {
    if (view_ != "tree") view_ = "tiles";
}

UltraDesktopAppStarter::~UltraDesktopAppStarter() = default;

void UltraDesktopAppStarter::Preload() {
    KeptList& kept = Kept();
    if (kept.reading.exchange(true)) return;
    std::thread([]() {
        ReadApplications();
        Kept().reading.store(false);
    }).detach();
}

void UltraDesktopAppStarter::Open() {
    WindowConfig config;
    config.title = "Applications";
    config.width = kWindowWidth;
    config.height = kWindowHeight;
    config.minWidth = 460;
    config.minHeight = 320;
    window_ = CreateWindow(config);
    if (!window_) return;
    open_ = true;
    window_->onWindowClosed = [this]() { open_ = false; };

    auto page = CreateContainer("appsPage", 0, 0, 0, 0);
    page->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(10);

    // The head: the view switch at the top left, the filter, the count.
    auto head = CreateContainer("appsHead", 0, 0, 0, 0);
    head->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    head->layoutItem.SetFlexShrink(0.0f);
    viewSwitch_ = std::make_shared<UltraCanvasSegmentedControl>("appsView", 0, 0, 76, 30);
    {
        SegmentedControlStyle style = viewSwitch_->GetStyle();
        style.iconSize = 18;
        style.iconAsMask = true;
        style.paddingVertical = 2;
        style.paddingHorizontal = 8;
        viewSwitch_->SetStyle(style);
    }
    viewSwitch_->AddSegment("", IconsDir() + "view-tiles.svg");
    viewSwitch_->AddSegment("", IconsDir() + "view-tree.svg");
    viewSwitch_->SetSelectedIndex(view_ == "tree" ? 1 : 0);
    viewSwitch_->SetTooltip("Tiles, or the categories as a tree beside a list");
    viewSwitch_->onSegmentSelected = [this](int index) { SetView(index == 1 ? "tree" : "tiles"); };
    viewSwitch_->layoutItem.SetFlexShrink(0.0f);
    head->AddChild(viewSwitch_);
    filterBox_ = CreateTextInput("appsFilter", 0, 0, 260, 28);
    filterBox_->SetPlaceholder("Find an application…");
    filterBox_->onTextChanged = [this](const std::string& text) {
        filter_ = Lower(text);
        Rebuild();
    };
    filterBox_->layoutItem.SetFlexGrow(1.0f);
    head->AddChild(filterBox_);
    status_ = CreateLabel("appsStatus", "Loading applications…");
    status_->SetTextColor(kStatusColor);
    status_->layoutItem.SetFlexShrink(0.0f);
    head->AddChild(status_);
    page->AddChild(head);

    body_ = CreateContainer("appsBody", 0, 0, 0, 0);
    body_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    body_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page->AddChild(body_);

    window_->AddChild(page);
    {
        // Sized to the window explicitly and on every resize: a percentage
        // would resolve against nothing at the window level.
        auto fit = [page = page.get(), win = window_.get()](int w, int h) {
            page->SetElementAbsolutePosition(Point2Df(0, 0));
            page->SetElementSize(Size2Df(static_cast<float>(std::max(1, w)), static_cast<float>(std::max(1, h))));
            page->InvalidateLayout();
            win->AddDirtyRectangle(Rect2Di(0, 0, w, h));
        };
        int w = 0, h = 0;
        window_->GetWindowSize(w, h);
        fit(w, h);
        window_->onWindowResize = fit;
    }

    // The window first; the applications after it is on screen. A list
    // kept from before is shown as the first thing the UI thread does next,
    // and read again behind it when it is old; without one, the list is
    // read on a thread of its own.
    window_->Show();
    filterBox_->SetFocus(true);

    AppList kept;
    bool fresh = false;
    {
        KeptList& k = Kept();
        std::lock_guard<std::mutex> lock(k.mutex);
        kept = k.apps;
        fresh = kept && std::chrono::steady_clock::now() - k.read < kListFresh;
    }
    if (kept) {
        work_.Run<AppList>([kept]() { return kept; }, [this](AppList apps) { ShowApps(std::move(apps)); });
    }
    if (!fresh) {
        work_.Run<AppList>([]() { return ReadApplications(); }, [this](AppList apps) {
            if (!SameApplications(apps, apps_)) ShowApps(std::move(apps));
        });
    }
}

void UltraDesktopAppStarter::Raise() {
    if (window_) window_->RaiseAndFocus();
}

void UltraDesktopAppStarter::ShowApps(AppList apps) {
    if (!open_ || !window_ || !apps) return;
    apps_ = std::move(apps);
    Rebuild();
}

void UltraDesktopAppStarter::SetView(const std::string& view) {
    if (view == view_) return;
    view_ = view;
    if (hooks_.setView) hooks_.setView(view_);
    Rebuild();
}

void UltraDesktopAppStarter::Rebuild() {
    if (!body_) return;
    body_->ClearChildren();
    tree_.reset();
    list_.reset();
    listModel_.reset();
    nodeApps_.clear();
    listed_.clear();
    if (!apps_) {
        status_->SetText("Loading applications…");
        return;
    }

    AppGroupingOptions options;
    options.keyOf = hooks_.keyOf;
    if (hooks_.recent) options.recent = hooks_.recent();
    options.recentShown = hooks_.recentShown;
    options.splitAbove = hooks_.splitAbove;
    options.filter = filter_;
    const std::vector<AppGroup> groups = GroupApplications(*apps_, options);

    size_t shown = 0;
    for (const AppGroup& group : groups) {
        if (group.category != AppCategory::Recent) shown += group.count;
    }
    status_->SetText(std::to_string(shown) + " of " + std::to_string(apps_->size()) + " applications");

    if (groups.empty()) {
        auto none = CreateLabel("appsNone", filter_.empty() ? "No applications were found."
                                                            : "No application matches \"" + filter_ + "\".");
        none->SetTextColor(kStatusColor);
        body_->AddChild(none);
    } else if (view_ == "tree") {
        BuildTreeAndList(groups);
    } else {
        BuildTiles(groups);
    }
    body_->InvalidateLayout();
    body_->RequestRedraw();
}

// ===== TILES =====

void UltraDesktopAppStarter::BuildTiles(const std::vector<AppGroup>& groups) {
    auto scroll = CreateScrollableContainer("appsTiles", 0, 0, 0, 0, true, false);
    scroll->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    scroll->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    scroll->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    int tileNo = 0;
    for (const AppGroup& group : groups) {
        // The category's heading: its icon and its name.
        auto heading = CreateContainer("appsHead:" + group.key, 0, 0, 0, 0);
        heading->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        heading->layoutItem.SetFlexShrink(0.0f);
        heading->SetMargin(tileNo == 0 ? 2 : 12, 0, 2, 4);
        auto icon = std::make_shared<UltraCanvasImageElement>("appsIcon:" + group.key, 0, 0, 22, 22);
        icon->SetFitMode(ImageFitMode::Contain);
        icon->LoadFromFile(IconsDir() + group.icon);
        icon->SetElementSize(Size2Df(22, 22));
        icon->layoutItem.SetFlexShrink(0.0f);
        heading->AddChild(icon);
        heading->AddChild(MakeHeading("appsTitle:" + group.key, group.title, 16, kHeadingColor));
        scroll->AddChild(heading);

        for (size_t s = 0; s < group.sections.size(); ++s) {
            const AppSection& section = group.sections[s];
            if (!section.title.empty()) {
                auto sub = MakeHeading("appsSub:" + group.key + ":" + std::to_string(s), section.title, 13,
                                       kSubheadingColor);
                sub->SetMargin(6, 0, 0, 34);
                scroll->AddChild(sub);
            }
            auto grid = CreateContainer("appsGrid:" + group.key + ":" + std::to_string(s), 0, 0, 0, 0);
            grid->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(kGap)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Start);
            grid->layoutItem.SetFlexShrink(0.0f);
            grid->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            for (size_t index : section.apps) {
                const UCDesktopEntry& entry = (*apps_)[index];
                auto tile = std::make_shared<UltraCanvasButton>("app:" + std::to_string(tileNo++), 0, 0,
                                                                kTileWidth, kTileHeight, entry.name);
                if (!entry.iconFile.empty()) {
                    tile->SetIcon(entry.iconFile);
                    tile->SetIconSize(kTileIcon, kTileIcon);
                    tile->SetIconPosition(ButtonIconPosition::Top);
                }
                ButtonStyle style = tile->GetStyle();
                style.normalColor = Colors::Transparent;
                style.borderWidth = 0;
                style.cornerRadius = 10;
                style.fontSize = 12;
                style.iconSpacing = 6;
                // The whole name, on as many lines as the tile holds.
                style.wrap = TextWrap::WrapWord;
                tile->SetStyle(style);
                tile->SetGrowToFitText(false);
                tile->SetPadding(8, 4, 4, 4);
                // The whole name - a long one ends in "…" on the tile - and
                // what the application is for.
                const std::string about = entry.comment.empty() ? entry.genericName : entry.comment;
                tile->SetTooltip(about.empty() ? entry.name : entry.name + "\n" + about);
                tile->layoutItem.SetFlexShrink(0.0f);
                const UCDesktopEntry launched = entry;
                tile->onClick = [this, launched]() { Launch(launched); };
                tile->onContextMenu = [this, launched](int x, int y) { ShowAppMenu(launched, x, y); };
                grid->AddChild(tile);
            }
            scroll->AddChild(grid);
        }
    }
    body_->AddChild(scroll);
}

// ===== TREE AND LIST =====

void UltraDesktopAppStarter::BuildTreeAndList(const std::vector<AppGroup>& groups) {
    auto pane = CreateContainer("appsPane", 0, 0, 0, 0);
    pane->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pane->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    pane->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    tree_ = std::make_shared<UltraCanvasTreeView>("appsTree");
    tree_->SetRowHeight(28);
    tree_->SetSelectionMode(TreeSelectionMode::Single);
    tree_->SetLineStyle(TreeLineStyle::NoLine);
    tree_->SetShowFirstChildOnExpand(false);
    tree_->size.width = CSSLayout::Dimension::Px(kTreeWidth);
    tree_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tree_->SetRootNode(TreeNodeData("apps", "Applications"));
    tree_->SetRootVisible(false);
    for (const AppGroup& group : groups) {
        const std::string id = "cat:" + group.key;
        TreeNodeData data(id, group.title + "  (" + std::to_string(group.count) + ")");
        data.leftIcon = TreeNodeIcon(IconsDir() + group.icon, 18, 18);
        data.showFirstChildOnExpand = false;
        tree_->AddNode("apps", data);
        std::vector<size_t>& all = nodeApps_[id];
        for (size_t s = 0; s < group.sections.size(); ++s) {
            const AppSection& section = group.sections[s];
            all.insert(all.end(), section.apps.begin(), section.apps.end());
            if (section.title.empty()) continue;
            const std::string subId = id + ":" + std::to_string(s);
            tree_->AddNode(id, TreeNodeData(subId, section.title + "  (" + std::to_string(section.apps.size()) + ")"));
            nodeApps_[subId] = section.apps;
        }
        // A split category lists its applications by name.
        if (group.sections.size() > 1) {
            std::sort(all.begin(), all.end());
        }
    }
    tree_->onNodeSelected = [this](TreeNode* node) {
        if (node) ShowListOf(node->data.nodeId);
    };
    pane->AddChild(tree_);

    list_ = std::make_shared<UltraCanvasListView>("appsList", 0, 0, 400, 300);
    listModel_ = std::make_shared<UltraCanvasSimpleListModel>();
    list_->SetModel(listModel_);
    auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
    delegate->SetFontSize(13.0f);
    delegate->SetIconSize(kListIcon);
    delegate->SetIconSpacing(10);
    delegate->SetTextPadding(8);
    delegate->SetRowHeight(36);
    list_->SetDelegate(delegate);
    list_->SetRowHeight(36);
    list_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    list_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // One click starts, as on the tiles; Return starts the selected row.
    list_->onItemClicked = [this](int row) {
        if (row >= 0 && static_cast<size_t>(row) < listed_.size()) Launch((*apps_)[listed_[row]]);
    };
    list_->onItemActivated = list_->onItemClicked;
    list_->onContextMenu = [this](int row, const UCEvent& event) {
        if (row < 0 || static_cast<size_t>(row) >= listed_.size()) return;
        ShowAppMenu((*apps_)[listed_[row]], event.pointerWindow.x, event.pointerWindow.y);
    };
    pane->AddChild(list_);
    body_->AddChild(pane);

    // Where the window opens: the node chosen before (the filter rebuilt
    // the tree), else the first category - Recent when there is a history.
    std::string start = nodeApps_.count(selectedNode_) ? selectedNode_ : "cat:" + groups.front().key;
    if (TreeNode* node = tree_->FindNode(start)) {
        tree_->SelectNode(node);
        if (!node->children.empty()) tree_->ExpandNode(node);
    }
    ShowListOf(start);
}

void UltraDesktopAppStarter::ShowListOf(const std::string& nodeId) {
    if (!listModel_ || !apps_) return;
    const auto found = nodeApps_.find(nodeId);
    if (found == nodeApps_.end()) return;
    selectedNode_ = nodeId;
    listed_ = found->second;
    std::vector<ListItem> items;
    items.reserve(listed_.size());
    for (size_t index : listed_) {
        const UCDesktopEntry& entry = (*apps_)[index];
        const std::string about = entry.comment.empty() ? entry.genericName : entry.comment;
        items.emplace_back(entry.name, entry.iconFile, about);
    }
    listModel_->SetItems(items);
    if (list_) {
        list_->InvalidateLayout();
        list_->RequestRedraw();
    }
}

// ===== STARTING =====

void UltraDesktopAppStarter::ShowAppMenu(const UCDesktopEntry& entry, int windowX, int windowY) {
    if (!window_) return;
    menu_ = std::make_shared<UltraCanvasMenu>("appsTileMenu", 0, 0, 220, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Header(entry.name));
    menu_->AddItem(MenuItemData::Action("Open", [this, entry]() { Launch(entry); }));
    if (hooks_.isPinned && hooks_.setPinned) {
        const bool pinned = hooks_.isPinned(entry);
        menu_->AddItem(MenuItemData::Action(pinned ? "Unpin from taskbar" : "Pin to taskbar",
                                            [this, entry, pinned]() { hooks_.setPinned(entry, !pinned); }));
    }
    PopupElementSettings settings;
    menu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

void UltraDesktopAppStarter::Launch(const UCDesktopEntry& entry) {
    std::string error;
    if (!UltraCanvasDesktopShell::LaunchApplication(entry, {}, &error)) {
        debugOutput << "UltraDesktop: could not start " << entry.name << ": " << error << std::endl;
        if (status_) status_->SetText("Could not start " + entry.name + ": " + error);
        return;
    }
    if (hooks_.started) hooks_.started(entry);
    if (window_) window_->Close();
}

} // namespace UltraDesktop
