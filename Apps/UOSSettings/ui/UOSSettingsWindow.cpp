// Apps/UOSSettings/ui/UOSSettingsWindow.cpp
// The UOS-Settings window: page tree | page, Close at the foot - the layout
// of UltraFiler's settings window. See UOSSettingsWindow.h.
// Version: 0.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework / ULTRA OS

#include "UOSSettingsWindow.h"

#include "UltraDesktopSettings.h"   // Apps/UltraDesktop: the desktop's settings file

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasFileDialogSettings.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <set>
#include <utility>

using namespace UltraCanvas;

namespace UOSSettings {

namespace {
    // The look of UltraFiler's settings window, so the two read as one family.
    constexpr float kTitleFontSize = 15.0f;
    constexpr float kTextFontSize  = 10.0f;
    constexpr float kNoteFontSize  = 9.0f;
    constexpr float kTreeFontSize  = 9.0f;
    const Color kTextColor      = Color(40, 40, 44, 255);
    const Color kMutedTextColor = Color(120, 120, 128, 255);
    const Color kNoteTextColor  = Color(96, 96, 104, 255);
    const Color kNoteBackground = Color(244, 245, 248, 255);
    const Color kNoteAccent     = Color(160, 176, 200, 255);
    const Color kRuleColor      = Color(225, 225, 230, 255);
    const Color kHeaderBackground = Color(243, 243, 246, 255);
    constexpr int kPagePadding = 20;
    constexpr int kTextWidth   = 520;
    constexpr int kNoteWidth   = 510;

    // Table geometry: the application column and the switch column are
    // fixed, the folder column takes the rest.
    constexpr int kAppColumnWidth    = 170;
    constexpr int kScopeColumnWidth  = 180;
    constexpr int kRowHeight         = 34;
    constexpr int kScopeSwitchHeight = 24;
    // Room at the right of every row for the rows' vertical scrollbar, which
    // is drawn over them; the header keeps the same so the columns line up.
    constexpr int kScrollbarRoom = 22;

    constexpr const char* kPageDesktop        = "desktop";
    constexpr const char* kSectionFileDialogs = "file-dialogs";
    constexpr const char* kPageLastFolder     = "file-dialogs/last-folder";

    // Segment order of every Global | Individual switch.
    constexpr int kSegmentGlobal = 0;
    constexpr int kSegmentIndividual = 1;

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id,
                                                const std::string& text,
                                                float fontSize = kTextFontSize,
                                                const Color& color = kTextColor) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
        l->SetText(text);
        l->SetFontSize(fontSize);
        l->SetTextColor(color);
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        l->size.width  = CSSLayout::Dimension::Auto();
        l->size.height = CSSLayout::Dimension::Auto();
        return l;
    }

    std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id,
                                               const std::string& text,
                                               int width = kTextWidth,
                                               float fontSize = kTextFontSize,
                                               const Color& color = kTextColor) {
        auto l = MakeLabel(id, text, fontSize, color);
        l->SetWrap(TextWrap::WrapWord);
        l->size.width = CSSLayout::Dimension::Px(static_cast<float>(width));
        return l;
    }

    std::shared_ptr<UltraCanvasSegmentedControl> MakeScopeSwitch(const std::string& id,
                                                                 int width, int height) {
        auto sw = std::make_shared<UltraCanvasSegmentedControl>(id, 0, 0, width, height);
        SegmentedControlStyle style = sw->GetStyle();
        style.fontSize = kTextFontSize;
        style.paddingVertical = 2;
        style.paddingHorizontal = 8;
        sw->SetStyle(style);
        sw->AddSegment("Global");
        sw->AddSegment("Individual");
        sw->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return sw;
    }
} // namespace

UOSSettingsWindow::UOSSettingsWindow(std::string version) : version_(std::move(version)) {}

UOSSettingsWindow::~UOSSettingsWindow() = default;

bool UOSSettingsWindow::Create() {
    WindowConfig wc;
    // Every app shows its version in its main window title (AGENTS.md).
    wc.title = "UOS-Settings " + version_;
    wc.width = 860;
    wc.height = 580;
    wc.resizable = true;
    window_ = CreateWindow(wc);
    if (!window_ || !window_->IsCreated()) { window_.reset(); return false; }

    window_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window_->SetBackgroundColor(Color(249, 249, 251, 255));

    // ----- page tree | page area -----
    auto content = std::make_shared<UltraCanvasContainer>("uos-content");
    content->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    tree_ = std::make_shared<UltraCanvasTreeView>("uos-tree");
    tree_->SetFontSize(kTreeFontSize);
    tree_->SetRowHeight(24);
    tree_->SetSelectionMode(TreeSelectionMode::Single);
    tree_->SetLineStyle(TreeLineStyle::NoLine);
    tree_->SetBackgroundColor(Color(243, 243, 246, 255));
    tree_->SetShowFirstChildOnExpand(true);
    tree_->SetAutoExpandSelectedNode(true);
    tree_->size.width = CSSLayout::Dimension::Px(180);
    tree_->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    TreeNodeData rootData("settings", "Settings");
    tree_->SetRootNode(rootData);
    tree_->AddNode("settings", TreeNodeData(kPageDesktop, "Desktop"));
    tree_->AddNode("settings", TreeNodeData(kSectionFileDialogs, "File dialogs"));
    tree_->AddNode(kSectionFileDialogs, TreeNodeData(kPageLastFolder, "Last used folder"));
    tree_->SetRootVisible(false);
    content->AddChild(tree_);

    pageArea_ = std::make_shared<UltraCanvasContainer>("uos-pages");
    pageArea_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pageArea_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pageArea_->SetBackgroundColor(Color(255, 255, 255, 255));
    content->AddChild(pageArea_);

    BuildDesktopPage();
    BuildLastFolderPage();
    window_->AddChild(content);

    // ----- bottom bar: Close -----
    auto bottom = std::make_shared<UltraCanvasContainer>("uos-bottom");
    bottom->layout.SetFlexRow().SetFlexGap(8)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Center)
                  .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bottom->SetPadding(8, 12, 8, 12);
    bottom->SetBorderTop(1, kRuleColor);
    auto close = std::make_shared<UltraCanvasButton>("uos-close", 0, 0, 90, 28, "Close");
    close->SetFontSize(kTextFontSize);
    close->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    close->SetOnClick([win = window_.get()]() { win->Close(); });
    bottom->AddChild(close);
    window_->AddChild(bottom);

    tree_->onNodeSelected = [this](TreeNode* node) {
        if (!node) return;
        // A section has no page of its own: it shows its first page.
        TreeNode* leaf = node;
        while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
        if (leaf && leaf != node) {
            tree_->SelectNode(leaf);
            return;
        }
        ShowPage(node->data.nodeId);
    };
    // Opens on the desktop's page - what the desktop's settings button is
    // usually pressed for - with the File dialogs section open below it.
    if (TreeNode* section = tree_->FindNode(kSectionFileDialogs)) tree_->ExpandNode(section);
    if (TreeNode* page = tree_->FindNode(kPageDesktop)) tree_->SelectNode(page);
    ShowPage(kPageDesktop);

    window_->SetEventCallback([](const UCEvent& event) {
        if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
            if (auto tw = event.targetWindow.lock())
                static_cast<UltraCanvasWindow*>(tw.get())->Close();
            return true;
        }
        return false;
    });
    window_->onWindowClosed = [this]() { if (onClosed) onClosed(); };
    return true;
}

void UOSSettingsWindow::Show() {
    if (window_) window_->Show();
}

void UOSSettingsWindow::ShowPage(const std::string& pageId) {
    if (!pages_.count(pageId)) return;
    for (auto& [id, page] : pages_) page->SetVisible(id == pageId);
    // Another application may have written the file since: show it as it is.
    if (pageId == kPageLastFolder) RefreshLastFolderPage();
    if (pageId == kPageDesktop) RefreshDesktopPage();
}

// ===== DESKTOP =====

void UOSSettingsWindow::BuildDesktopPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-desktop");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-dt-title", "Desktop", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-dt-caption",
            "The ULTRA OS desktop (UltraDesktop): where its taskbar sits, the "
            "picture behind it, and what its buttons open.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    auto body = std::make_shared<UltraCanvasContainer>("uos-dt-body");
    body->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    body->SetMargin(14, 0, 0, 0);

    // caption | field [| extra], the captions in one column.
    auto addRow = [&](const std::string& id, const std::string& captionText,
                      const std::shared_ptr<UltraCanvasUIElement>& field,
                      const std::shared_ptr<UltraCanvasUIElement>& extra = nullptr) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->size.height = CSSLayout::Dimension::Px(30);
        auto label = MakeLabel(id + "-label", captionText);
        label->size.width = CSSLayout::Dimension::Px(130);
        row->AddChild(label);
        field->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        field->size.width = CSSLayout::Dimension::Px(0);
        row->AddChild(field);
        if (extra) {
            extra->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            row->AddChild(extra);
        }
        body->AddChild(row);
    };
    // A text field is written when it is left or Return is pressed - not per
    // keystroke, which would rebuild the desktop for every letter.
    auto wireText = [this](const std::shared_ptr<UltraCanvasTextInput>& input) {
        input->onEnterPressed = [this](const std::string&) {
            SaveDesktopPage();
            return true;
        };
        input->onFocusLost = [this]() { SaveDesktopPage(); };
    };

    edge_ = std::make_shared<UltraCanvasDropdown>("uos-dt-edge", 0, 0, 200, 28);
    edge_->AddItem("Left", "left");
    edge_->AddItem("Top", "top");
    edge_->AddItem("Bottom", "bottom");
    edge_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-edge-row", "Taskbar", edge_);

    wallpaper_ = std::make_shared<UltraCanvasTextInput>("uos-dt-wallpaper", 0, 0, 260, 28);
    wallpaper_->SetPlaceholder("The framework's picture");
    wireText(wallpaper_);
    auto browse = std::make_shared<UltraCanvasButton>("uos-dt-browse", 0, 0, 90, 28, "Browse...");
    browse->SetFontSize(kTextFontSize);
    browse->SetOnClick([this]() { BrowseWallpaper(); });
    addRow("uos-dt-wallpaper-row", "Wallpaper", wallpaper_, browse);

    ramDisc_ = std::make_shared<UltraCanvasTextInput>("uos-dt-ramdisc", 0, 0, 260, 28);
    wireText(ramDisc_);
    addRow("uos-dt-ramdisc-row", "RAM disc", ramDisc_);

    filer_ = std::make_shared<UltraCanvasTextInput>("uos-dt-filer", 0, 0, 260, 28);
    wireText(filer_);
    addRow("uos-dt-filer-row", "File manager", filer_);

    desktops_ = std::make_shared<UltraCanvasDropdown>("uos-dt-desktops", 0, 0, 120, 28);
    for (int i = 1; i <= 9; ++i) desktops_->AddItem(std::to_string(i), std::to_string(i));
    desktops_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-desktops-row", "Virtual desktops", desktops_);
    page->AddChild(body);

    desktopStatus_ = MakeLabel("uos-dt-status", "", kTextFontSize, kMutedTextColor);
    desktopStatus_->SetMargin(10, 0, 0, 0);
    page->AddChild(desktopStatus_);

    auto spacer = std::make_shared<UltraCanvasContainer>("uos-dt-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    spacer->size.height = CSSLayout::Dimension::Px(24);
    page->AddChild(spacer);

    auto notes = std::make_shared<UltraCanvasContainer>("uos-dt-notes");
    notes->layout.SetFlexColumn().SetFlexGap(6)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    notes->SetBackgroundColor(kNoteBackground);
    notes->SetBorderLeft(3, kNoteAccent);
    notes->SetPadding(10, 12, 10, 12);
    notes->AddChild(MakeText("uos-dt-note-1",
            "Changes are saved at once - a text field when you leave it or press "
            "Return - and a running desktop takes them over within a second.",
            kNoteWidth, kNoteFontSize, kNoteTextColor));
    notes->AddChild(MakeText("uos-dt-note-2",
            "The RAM disc is the folder the desktop's drive button opens; the file "
            "manager is the program its folder buttons start. Virtual desktops "
            "are asked of the window manager, which may offer a different number.",
            kNoteWidth, kNoteFontSize, kNoteTextColor));
    page->AddChild(notes);

    pages_[kPageDesktop] = page;
    pageArea_->AddChild(page);
}

void UOSSettingsWindow::RefreshDesktopPage() {
    UltraDesktop::DesktopSettings settings;
    std::string error;
    const std::string path = UltraDesktop::DesktopSettings::DefaultPath();
    const bool read = settings.Load(path, &error);

    refreshing_ = true;
    if (edge_) {
        edge_->SetSelectedIndex(settings.taskbarEdge == UltraDesktop::TaskbarEdge::Top ? 1
                                : settings.taskbarEdge == UltraDesktop::TaskbarEdge::Bottom ? 2 : 0,
                                false);
    }
    if (wallpaper_) wallpaper_->SetText(settings.wallpaper);
    if (ramDisc_) ramDisc_->SetText(settings.ramDiscPath);
    if (filer_) filer_->SetText(settings.filerProgram);
    if (desktops_) desktops_->SetSelectedIndex(std::clamp(settings.virtualDesktops, 1, 9) - 1, false);
    refreshing_ = false;
    if (desktopStatus_) {
        desktopStatus_->SetText(read ? "Settings file: " + path
                                     : "The settings file could not be read: " + error);
    }
}

void UOSSettingsWindow::SaveDesktopPage() {
    if (refreshing_) return;
    const std::string path = UltraDesktop::DesktopSettings::DefaultPath();
    if (path.empty()) return;
    // Read, change, write: the sticky notes in the same file are the
    // desktop's, and whatever it wrote last is kept.
    UltraDesktop::DesktopSettings settings;
    std::string error;
    settings.Load(path, &error);
    if (edge_) {
        const int index = edge_->GetSelectedIndex();
        settings.taskbarEdge = index == 1 ? UltraDesktop::TaskbarEdge::Top
                             : index == 2 ? UltraDesktop::TaskbarEdge::Bottom
                                          : UltraDesktop::TaskbarEdge::Left;
    }
    if (wallpaper_) settings.wallpaper = wallpaper_->GetText();
    if (ramDisc_ && !ramDisc_->GetText().empty()) settings.ramDiscPath = ramDisc_->GetText();
    if (filer_ && !filer_->GetText().empty()) settings.filerProgram = filer_->GetText();
    if (desktops_) settings.virtualDesktops = std::clamp(desktops_->GetSelectedIndex() + 1, 1, 9);
    if (!settings.Save(path, &error)) {
        if (desktopStatus_) desktopStatus_->SetText("Not saved: " + error);
        return;
    }
    if (desktopStatus_) desktopStatus_->SetText("Saved to " + path);
}

void UOSSettingsWindow::BrowseWallpaper() {
    FileDialogOptions options;
    options.title = "Choose a wallpaper";
    options.parentWindow = window_.get();
    options.AddFilter("Pictures", std::vector<std::string>{"jpg", "jpeg", "png", "webp",
                                                           "avif", "heic", "bmp"});
    UltraCanvasFileLoader::OpenFileDialog(options, [this](DialogResult result,
                                                          const std::string& path) {
        if (result != DialogResult::OK || path.empty() || !wallpaper_) return;
        wallpaper_->SetText(path);
        SaveDesktopPage();
    });
}

// ===== FILE DIALOGS > LAST USED FOLDER =====

void UOSSettingsWindow::BuildLastFolderPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-last-folder");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-lf-title", "Last used folder", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-lf-caption",
            "Where the file dialog of an application opens when the application "
            "does not ask for a folder itself - \"Attach file\" in UltraMail, for "
            "one.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    // ----- the switch -----
    auto modeRow = std::make_shared<UltraCanvasContainer>("uos-lf-mode-row");
    modeRow->layout.SetFlexRow().SetFlexGap(12)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    modeRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    modeRow->size.height = CSSLayout::Dimension::Px(32);
    modeRow->SetMargin(14, 0, 0, 0);
    modeRow->AddChild(MakeLabel("uos-lf-mode-label", "Applications use:"));
    modeSwitch_ = MakeScopeSwitch("uos-lf-mode", 320, 28);
    modeSwitch_->SetSegmentText(kSegmentGlobal, "One common folder");
    modeSwitch_->SetSegmentText(kSegmentIndividual, "Their own folders");
    modeSwitch_->onSegmentSelected = [this](int index) {
        if (refreshing_) return;
        SetMode(index == kSegmentIndividual);
    };
    modeRow->AddChild(modeSwitch_);
    page->AddChild(modeRow);

    // ----- the table: header + scrolling rows -----
    tableNote_ = MakeText("uos-lf-table-note", "", kTextWidth, kTextFontSize, kNoteTextColor);
    tableNote_->SetMargin(14, 0, 6, 0);
    page->AddChild(tableNote_);

    auto table = std::make_shared<UltraCanvasContainer>("uos-lf-table");
    table->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    table->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    table->size.height = CSSLayout::Dimension::Px(120);   // basis; it grows
    table->SetBorders(1.0f, kRuleColor);
    table->SetBackgroundColor(Color(255, 255, 255, 255));

    auto header = std::make_shared<UltraCanvasContainer>("uos-lf-table-header");
    header->layout.SetFlexRow().SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    header->size.height = CSSLayout::Dimension::Px(28);
    header->SetPadding(0, kScrollbarRoom, 0, 10);
    header->SetBackgroundColor(kHeaderBackground);
    header->SetBorderBottom(1, kRuleColor);
    auto addHeader = [&](const std::string& id, const std::string& text, int width) {
        auto l = MakeLabel(id, text);
        l->SetFontWeight(FontWeight::Bold);
        if (width > 0) {
            l->size.width = CSSLayout::Dimension::Px(static_cast<float>(width));
        } else {
            l->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            l->size.width = CSSLayout::Dimension::Px(0);
        }
        header->AddChild(l);
    };
    addHeader("uos-lf-h-app", "Application", kAppColumnWidth);
    addHeader("uos-lf-h-folder", "Opens in", 0);
    addHeader("uos-lf-h-scope", "Last used folder", kScopeColumnWidth);
    table->AddChild(header);

    tableRows_ = std::make_shared<UltraCanvasContainer>("uos-lf-table-rows");
    tableRows_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tableRows_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tableRows_->size.height = CSSLayout::Dimension::Px(60);   // basis; it grows
    // Opaque, like the table around it: a scroll repaints only these
    // containers, and without backgrounds of their own the strip under the
    // last row kept the pixels of the rows that were there before.
    tableRows_->SetBackgroundColor(Color(255, 255, 255, 255));
    {
        // Vertically only: a vertical bar narrows the rows, which must not
        // turn into a horizontal overflow of its own width.
        ContainerStyle cs;
        cs.autoShowScrollbars = true;
        cs.autoShowHorizontalScrollbar = false;
        tableRows_->SetContainerStyle(cs);
    }
    // A scroll repaints the rows' own box; the strip of the table just below
    // it kept the pixels of the row that was there before the scroll. The
    // whole table is repainted instead.
    tableRows_->SetScrollChangedCallback([table = table.get()](int, int) {
        table->RequestRedraw();
    });
    table->AddChild(tableRows_);
    page->AddChild(table);

    // ----- notes -----
    auto notes = std::make_shared<UltraCanvasContainer>("uos-lf-notes");
    notes->layout.SetFlexColumn().SetFlexGap(6)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    notes->SetBackgroundColor(kNoteBackground);
    notes->SetBorderLeft(3, kNoteAccent);
    notes->SetPadding(10, 12, 10, 12);
    notes->SetMargin(14, 0, 0, 0);
    notes->AddChild(MakeText("uos-lf-note-1",
            "One common folder: whichever application last used a file dialog, "
            "the next one opens there.", kNoteWidth, kNoteFontSize, kNoteTextColor));
    notes->AddChild(MakeText("uos-lf-note-2",
            "Their own folders: each application set to Individual keeps its own "
            "last folder, starting from the common one until it has used a folder "
            "of its own; one set to Global still shares the common folder. "
            "Changes are saved at once and apply the next time a file dialog opens.",
            kNoteWidth, kNoteFontSize, kNoteTextColor));
    page->AddChild(notes);

    // The applications that use the framework's file dialog, listed before
    // any of them has opened one.
    for (const std::string& app : KnownFileDialogApplications()) AddAppRow(app);

    pages_[kPageLastFolder] = page;
    pageArea_->AddChild(page);
}

void UOSSettingsWindow::AddAppRow(const std::string& appName) {
    if (rows_.count(appName) || !tableRows_) return;
    const std::string id = "uos-lf-row-" + std::to_string(rows_.size());

    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(12)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.height = CSSLayout::Dimension::Px(kRowHeight);
    row->SetPadding(0, kScrollbarRoom, 0, 10);
    row->SetBorderBottom(1, kRuleColor);

    auto name = MakeLabel(id + "-app", appName);
    name->size.width = CSSLayout::Dimension::Px(kAppColumnWidth);
    row->AddChild(name);

    AppRow entry;
    entry.folder = MakeLabel(id + "-folder", "");
    entry.folder->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    entry.folder->size.width = CSSLayout::Dimension::Px(0);
    row->AddChild(entry.folder);

    entry.scope = MakeScopeSwitch(id + "-scope", kScopeColumnWidth, kScopeSwitchHeight);
    entry.scope->onSegmentSelected = [this, appName](int index) {
        if (refreshing_) return;
        SetAppScope(appName, index == kSegmentIndividual);
    };
    row->AddChild(entry.scope);

    rows_[appName] = entry;
    tableRows_->AddChild(row);
}

void UOSSettingsWindow::RefreshLastFolderPage() {
    const FileDialogSettings settings = FileDialogSettings::Load();
    // An application that used a file dialog but is not in the known list
    // (a newer one) gets its row too.
    for (const auto& [name, app] : settings.apps) AddAppRow(name);

    refreshing_ = true;
    const bool individual = settings.lastFolderMode == LastFolderScope::Individual;
    if (modeSwitch_)
        modeSwitch_->SetSelectedIndex(individual ? kSegmentIndividual : kSegmentGlobal);
    if (tableNote_) {
        tableNote_->SetText(individual
                ? "Choose for each application whether it shares the common folder "
                  "(Global) or keeps its own (Individual):"
                : "Every application shares the common folder. Choose \"Their own "
                  "folders\" above to set this per application.");
    }
    for (auto& [name, row] : rows_) {
        const std::string folder = settings.LastFolderFor(name);
        row.folder->SetText(folder.empty() ? "(not used yet)" : folder);
        row.folder->SetTextColor(folder.empty() ? kMutedTextColor : kTextColor);
        row.folder->SetTooltip(folder);
        // Under one common folder the per-application choice does not apply:
        // both halves greyed out (the control draws "disabled" per segment).
        // Enabled before the selection is set - a disabled segment cannot be
        // selected.
        row.scope->SetSegmentEnabled(kSegmentGlobal, true);
        row.scope->SetSegmentEnabled(kSegmentIndividual, true);
        const LastFolderScope own = settings.apps.count(name)
                ? settings.apps.at(name).scope : LastFolderScope::Individual;
        row.scope->SetSelectedIndex(own == LastFolderScope::Individual
                                    ? kSegmentIndividual : kSegmentGlobal);
        row.scope->SetSegmentEnabled(kSegmentGlobal, individual);
        row.scope->SetSegmentEnabled(kSegmentIndividual, individual);
        row.scope->SetDisabled(!individual);
    }
    refreshing_ = false;
    if (window_) window_->RequestRedraw();
}

void UOSSettingsWindow::SetMode(bool individual) {
    FileDialogSettings::Update([individual](FileDialogSettings& s) {
        s.lastFolderMode = individual ? LastFolderScope::Individual : LastFolderScope::Global;
    });
    RefreshLastFolderPage();
}

void UOSSettingsWindow::SetAppScope(const std::string& appName, bool individual) {
    FileDialogSettings::Update([&appName, individual](FileDialogSettings& s) {
        s.apps[appName].scope = individual ? LastFolderScope::Individual
                                           : LastFolderScope::Global;
    });
    RefreshLastFolderPage();
}

} // namespace UOSSettings
