// Apps/UltraOffice/UltraOfficeWindow.cpp
// The UltraOffice window: tabs, the Start tab, the per-document panes, the
// File menu, opening, saving, closing, and the recovery copies.
//
// Version: 0.11.0 - the Start tab's note on where the tools went (once, until dismissed); the tab strip's
//                   keyboard focus and its ring are the framework's (SetKeyboardFocusOnRequest)
// Version: 0.10.0 - the tools in panels at the left by default (UltraOfficePanels.h), the toolbar a setting
//                   switched at once; one top row with the view group last; the panel column's splitter and
//                   each kind's panel order, open panels and width kept; the icon rail; F6 / Shift+F6
// Version: 0.9.0 - the tab strip's + goes to the Start tab (it opened the File menu)
// Version: 0.8.0 - the view group at the toolbar's right end; the Settings window (File > Settings...,
//                  Ctrl+, , the Start tab); Ctrl++ / Ctrl+- / Ctrl+0; autosave to the file and the
//                  recovery copies every few minutes as set; the recent files' count; reopening the files
//                  open at the last close
// Version: 0.7.0 - each tab gets the settings (AttachSettings) and the keys the window leaves alone (F5)
// Version: 0.6.0 - PowerPoint 97-2003 decks (.ppt, .pps, .pot) are read, not written: Save asks for a name
// Version: 0.5.0 - new from template: the Start tab's gallery, File > New from template, a template
//                  opened as a new untitled document; Save as template
// Version: 0.4.0 - new presentations (the slide editor)
// Version: 0.3.0 - recovered documents offered on the Start tab, so the window never waits on the question
// Version: 0.2.0 - autosave every minute and crash recovery
// Version: 0.1.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeWindow.h"
#include "UltraOfficeSettingsDialog.h"
#include "UltraOfficeTemplates.h"
#include "UltraOfficeToolbar.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

constexpr int kStatusHeight = 26;
constexpr int kStartPageWidth = 620;
constexpr unsigned kMinuteMs = 60 * 1000;

const char* KindName(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return "spreadsheet";
        case OfficeKind::Presentation: return "presentation";
        default: return "document";
    }
}

OfficeKind KindFromName(const std::string& name) {
    if (name == "spreadsheet") return OfficeKind::Spreadsheet;
    if (name == "presentation") return OfficeKind::Presentation;
    if (name == "document") return OfficeKind::Document;
    return OfficeKind::Unknown;
}

std::string FileNameOf(const std::string& path) { return PathToUtf8(PathFromUtf8(path).filename()); }

std::string StemOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string LowerExtension(const std::string& path) {
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Files that are read but not written: saving one asks for a new name.
bool IsReadOnlyFormat(const std::string& path) {
    const std::string ext = LowerExtension(path);
    return ext == "doc" || ext == "xls" || ext == "ppt" || ext == "pps" || ext == "pot" || ext == "md" ||
           ext == "markdown" || ext == "tex" || ext == "txt";
}

const char* KindIcon(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return "office/spreadsheet.svg";
        case OfficeKind::Presentation: return "office/presentation.svg";
        default: return "office/document.svg";
    }
}

int KindSlot(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return 1;
        case OfficeKind::Presentation: return 2;
        default: return 0;
    }
}

bool IsInside(const UltraCanvasUIElement* element, const UltraCanvasContainer* container) {
    return element && container && (element == container || element->IsDescendantOf(container));
}

} // namespace

UltraOfficeWindow::UltraOfficeWindow() : UltraCanvasWindow(), settings_("UltraOffice") {
    settings_.Load();
    runTag_ = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count());
}

UltraOfficeWindow::~UltraOfficeWindow() {
    if (GetSettingsDialog()) CloseSettingsDialog();   // its controls act on this window
    if (autosaveTimer_)
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(autosaveTimer_);
}

// ===== BUILDING =====

void UltraOfficeWindow::Initialize(const std::vector<std::string>& paths) {
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    InstallEventFilter("office-shortcuts", [this](const UCEvent& event) { return HandleShortcut(event); },
                       {UCEventType::KeyDown});

    tabs_ = std::make_shared<UltraCanvasTabbedContainer>("office-tabs");
    // The strip takes the keyboard when F6 brings it there (Left / Right go
    // from tab to tab, a focus ring round the open tab) and not from a
    // click, so the document keeps it.
    tabs_->SetKeyboardFocusOnRequest(true);
    tabs_->SetTabStyle(TabStyle::Modern);
    tabs_->SetCloseMode(TabCloseMode::ClosableExceptFirst);
    tabs_->SetShowNewTabButton(true);
    tabs_->SetNewTabButtonPosition(NewTabButtonPosition::AfterTabs);
    tabs_->SetTabHeight(34);
    tabs_->SetIconSize(16);
    tabs_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    UltraCanvasTabbedContainer* tabs = tabs_.get();
    tabs_->onTabChange = [this](int, int) {
        // A tab chosen with the keyboard on the strip (F6, then Left / Right)
        // leaves the keyboard there.
        const bool onStrip = GetFocusedElement() == tabs_.get();
        if (OpenDocument* doc = Active(); doc && !onStrip) doc->host->Focus();
        UpdateTitle();
    };
    tabs_->onTabClose = [this](int index) {
        for (auto& doc : documents_) {
            if (TabIndexOf(*doc) == index) { RequestCloseDocument(*doc); break; }
        }
        return false;   // RequestCloseDocument removes the tab once it may go
    };
    tabs_->onNewTabRequest = [this, tabs]() {
        // The new-tab button goes to the Start tab (always the first): new
        // documents of each kind, templates, the recent files and Open.
        (void)tabs;
        if (startPane_) tabs_->SetActiveTab(0);
    };
    AddChild(tabs_);

    BuildStartPage();

    // Closing the window asks once about every document with unsaved changes.
    onWindowClosing = [this]() -> bool {
        // A window that closes takes its recovery copies with it: only a run
        // that ends without closing leaves them.
        if (closingConfirmed_) { RememberOpenFiles(); RemoveAllRecovery(); return true; }
        int unsaved = 0;
        for (const auto& doc : documents_) if (doc->host->IsModified()) ++unsaved;
        if (unsaved == 0) { RememberOpenFiles(); RemoveAllRecovery(); return true; }
        UltraCanvasDialogManager::ShowConfirmation(
            TrN("{n} document has unsaved changes. Close UltraOffice and lose them?",
                "{n} documents have unsaved changes. Close UltraOffice and lose them?", unsaved),
            "UltraOffice",
            [this](bool yes) { if (yes) { closingConfirmed_ = true; Close(); } }, this);
        return false;
    };

    ApplyTheme();
    OfferRecovery();
    // The files open at the last close, when the settings say so and the
    // start named none of its own.
    if (paths.empty() && prefs_.GetReopenFiles()) {
        std::vector<std::string> reopen;
        for (const std::string& path : prefs_.GetOpenFiles()) {
            std::error_code ec;
            if (std::filesystem::exists(PathFromUtf8(path), ec)) reopen.push_back(path);
        }
        OpenPaths(reopen);
    } else {
        OpenPaths(paths);
    }
    UpdateTitle();
    StartAutosave();
}

void UltraOfficeWindow::BuildStartPage() {
    auto page = std::make_shared<UltraCanvasStartPage>("office-start", kStartPageWidth);
    page->SetTitle("UltraOffice");
    page->SetHint(Tr("Documents, spreadsheets and presentations in one window. Each opens in a tab of its own, "
                     "with the tools for its kind."));
    std::vector<std::string> recent;
    for (const std::string& path : prefs_.GetRecentFiles()) {
        std::error_code ec;
        if (std::filesystem::exists(PathFromUtf8(path), ec)) recent.push_back(path);
    }
    // Documents a run that ended without closing had unsaved changes in.
    if (!recoveryNotes_.empty()) {
        page->AddSection(Tr("Recovered documents"));
        const int count = static_cast<int>(recoveryNotes_.size());
        page->AddNote(TrN("UltraOffice ended without closing while {n} document had unsaved changes. A copy of it was kept.",
                          "UltraOffice ended without closing while {n} documents had unsaved changes. A copy of each was kept.",
                          count));
        std::string names;
        for (const auto& notePath : recoveryNotes_) {
            UltraCanvasAppSettings note = UltraCanvasAppSettings::AtFile(PathToUtf8(notePath));
            note.Load();
            const std::string file = note.GetString("file");
            names += (names.empty() ? "" : ", ") + note.GetString("name", FileNameOf(file));
        }
        page->AddNote(names, 30);
        page->AddButton(Tr("Restore"), [this]() { RestoreAllRecovered(); });
        page->AddButton(Tr("Discard the copies"), [this]() { DiscardAllRecovered(); });
    }
    page->SetRecent(Tr("Recent"), recent, [this](const std::string& path) { OpenPath(path); }, prefs_.GetRecentCount());
    page->AddAction(Tr("Open a file..."), [this]() { ShowOpenDialog(); },
                    Tr("Word processing, spreadsheet and presentation files: ODF, Office Open XML, CSV and more."));
    // Once, until dismissed: where the tools went, and the way back to the
    // toolbar (proposal, "Familiarity").
    if (prefs_.GetToolLayout() == ToolLayout::Panels && !prefs_.GetToolsNoteDismissed()) {
        page->AddSection(Tr("Where the tools are"));
        const std::string note = TrF("The tools are now in panels at the left of each document: open, close and drag "
                                     "them as you like. To have the toolbar at the top again, choose it in {0}, {1}, {2}.",
                                     Tr("Settings"), Tr("General"), Tr("Tools"));
        // As many 18 px lines as the language's text needs at the note's
        // 12 px (about 7 px a letter, a CJK one twice that), and one spare.
        int units = 0;
        for (size_t i = 0; i < note.size();) {
            const unsigned char c = static_cast<unsigned char>(note[i]);
            const size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            units += length >= 3 ? 2 : 1;
            i += length;
        }
        const int lines = 2 + units * 7 / std::max(1, page->ContentWidth());
        page->AddNote(note, lines * 18);
        page->AddButton(Tr("Settings"), [this]() { ShowSettings(); })->SetIdentifier("office-start-tools-settings");
        page->AddButton(Tr("Hide this note"), [this]() { DismissToolsNote(); })->SetIdentifier("office-start-tools-dismiss");
    }
    page->AddSection(Tr("Start something new"));
    page->AddButton(Tr("New document"), [this]() { NewDocument(OfficeKind::Document); });
    page->AddButton(Tr("New spreadsheet"), [this]() { NewDocument(OfficeKind::Spreadsheet); });
    page->AddButton(Tr("New presentation"), [this]() { NewDocument(OfficeKind::Presentation); });

    // New from template: the shipped templates and the user's own, each a
    // card with a picture of it.
    const std::vector<OfficeTemplate> templates = ListTemplates(OwnTemplatesFolder());
    const int galleryWidth = page->ContentWidth();
    page->AddSection(Tr("New from template"), TemplateGalleryHeight(templates.size(), galleryWidth))
        ->AddChild(BuildTemplateGallery(templates, galleryWidth,
                                        [this](const std::string& path) { NewFromTemplate(path); }));
    page->AddSection(Tr("Your templates"));
    page->AddNote(Tr("A document kept with File, Save as template is offered here with the others."), 30);
    page->AddButton(Tr("Open a template file"), [this]() { ShowTemplateOpenDialog(); });

    // The Settings window, behind its gear, as File > Settings... is.
    page->AddSection(Tr("Settings"));
    page->AddNote(Tr("Your name, language, look, saving, and what new files start with."), 30);
    auto gear = page->AddButton(Tr("Settings..."), [this]() { ShowSettings(); });
    gear->SetIcon(IconPath("menu/settings.svg"));
    gear->SetIconSize(18, 18);

    // The page is a fixed-width column: centred in the tab, scrolled when the
    // tab is shorter than it.
    auto pane = CreateScrollableContainer("office-start-pane", 0, 0, 0, 0, true, false);
    pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    pane->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page->SetMargin(32, 0, 0, 0);
    page->layoutItem.SetFlexShrink(0);   // its own height, which the pane scrolls
    pane->AddChild(page);

    if (!startPane_) {
        startPane_ = pane;
        const int index = tabs_->AddTab(Tr("Start"), startPane_);
        tabs_->SetTabIcon(index, IconPath("office/start.svg"));
    } else {
        startPane_ = pane;
        tabs_->SetTabContent(0, startPane_);
    }
}

UltraOfficeWindow::OpenDocument* UltraOfficeWindow::AddDocument(std::unique_ptr<OfficeHost> host, const std::string& path,
                                                                const std::string& name, const std::string& password) {
    auto doc = std::make_unique<OpenDocument>();
    doc->id = nextId_++;
    doc->path = path;
    doc->name = name;
    doc->password = password;
    doc->host = std::move(host);
    doc->host->AttachSettings(&settings_);   // again for a new one: harmless
    const int id = doc->id;
    doc->host->onChanged = [this, id]() {
        if (OpenDocument* d = FindById(id)) { d->changedSinceCopy = true; Refresh(*d); }
    };
    BuildPane(*doc);
    OpenDocument* raw = doc.get();
    documents_.push_back(std::move(doc));
    const int index = tabs_->AddTab(raw->name, raw->pane);
    tabs_->SetTabIcon(index, IconPath(KindIcon(raw->host->GetKind())));
    tabs_->SetTabTooltip(index, path.empty() ? raw->name : path);
    tabs_->SetActiveTab(index);
    Refresh(*raw);
    raw->host->Focus();
    return raw;
}

void UltraOfficeWindow::BuildPane(OpenDocument& doc) {
    const std::string id = "office-doc-" + std::to_string(doc.id);
    doc.pane = CreateContainer(id);
    doc.pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    doc.pane->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    doc.status = std::make_shared<UltraCanvasLabel>(id + "-status");
    doc.status->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kStatusHeight));
    doc.status->SetPadding(0, 10, 0, 10);
    LayoutTools(doc);
}

void UltraOfficeWindow::BuildCommandRow(OpenDocument& doc, bool withTabs) {
    // The general functions: the File menu, Undo, Redo, Save; in toolbar
    // mode the kind's command tabs after them.
    const std::string id = "office-doc-" + std::to_string(doc.id);
    doc.commandRow = MakeToolbar(id + "-commands");
    const int docId = doc.id;
    auto file = AddTool(*doc.commandRow, id + "-file", Tr("File"), "menu/chevron-down.svg", Tr("Open, save, export, print"), nullptr);
    UltraCanvasButton* fileButton = file.get();
    doc.fileButton = fileButton;
    // F6 comes here: the File button takes the keyboard (Enter or Space opens the menu).
    file->SetAcceptsFocus(true);
    file->onClick = [this, fileButton]() { ShowFileMenu(fileButton); };
    AddTool(*doc.commandRow, id + "-undo", "", "menu/undo.svg", Tr("Undo (Ctrl+Z)"),
            [this, docId]() { if (OpenDocument* d = FindById(docId)) { d->host->Undo(); Refresh(*d); } });
    AddTool(*doc.commandRow, id + "-redo", "", "menu/redo.svg", Tr("Redo (Ctrl+Y)"),
            [this, docId]() { if (OpenDocument* d = FindById(docId)) { d->host->Redo(); Refresh(*d); } });
    AddTool(*doc.commandRow, id + "-save", "", "menu/save.svg", Tr("Save (Ctrl+S)"),
            [this, docId]() { if (OpenDocument* d = FindById(docId)) Save(*d, false); });
    doc.commandButtons.clear();
    if (!withTabs) return;
    doc.commandRow->AddSeparator();
    const auto& commandTabs = doc.host->GetCommandTabs();
    for (size_t i = 0; i < commandTabs.size(); ++i) {
        const int index = static_cast<int>(i);
        auto button = AddToggleTool(*doc.commandRow, id + "-tab-" + std::to_string(i), commandTabs[i].name, "",
                                    commandTabs[i].name,
                                    [this, docId, index]() { if (OpenDocument* d = FindById(docId)) ShowCommandTab(*d, index); });
        doc.commandButtons.push_back(button.get());
    }
}

void UltraOfficeWindow::LayoutTools(OpenDocument& doc) {
    const std::string id = "office-doc-" + std::to_string(doc.id);
    const ToolLayout layout = prefs_.GetToolLayout();
    const auto& commandTabs = doc.host->GetCommandTabs();
    std::shared_ptr<UltraCanvasToolbar> view = doc.host->GetViewToolbar();
    std::shared_ptr<UltraCanvasUIElement> content = doc.host->GetContent();

    // Taken apart first: the panels go, giving the host back the elements
    // of its own they hold while the tree they are in is whole; the host's
    // bars and its document go into the new layout.
    doc.panels.reset();
    if (doc.commandSlot && doc.commandTab >= 0 && doc.commandTab < static_cast<int>(commandTabs.size()))
        doc.commandSlot->RemoveChild(commandTabs[static_cast<size_t>(doc.commandTab)].toolbar);
    doc.commandTab = -1;
    if (view)
        if (auto* parent = view->GetParentContainer()) parent->RemoveChild(view);
    if (auto* parent = content->GetParentContainer()) parent->RemoveChild(content);
    doc.pane->ClearChildren();
    doc.panelSplit.reset();
    doc.toolbarSlot.reset();
    doc.commandSlot.reset();
    doc.fileButton = nullptr;
    doc.layout = layout;

    if (layout == ToolLayout::Toolbar) {
        // The command row with the command tabs, and under it the command
        // tab's bar, which gives way - its tools behind its chevron - and
        // the view group at the right end, which does not.
        BuildCommandRow(doc, true);
        doc.pane->AddChild(doc.commandRow);
        doc.toolbarSlot = CreateContainer(id + "-toolbar");
        doc.toolbarSlot->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        doc.toolbarSlot->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kToolbarHeight));
        doc.commandSlot = CreateContainer(id + "-command-bar");
        doc.commandSlot->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        doc.commandSlot->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        doc.commandSlot->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        doc.commandSlot->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kToolbarHeight));
        ContainerStyle plain;
        plain.autoShowScrollbars = false;
        doc.commandSlot->SetContainerStyle(plain);
        doc.toolbarSlot->AddChild(doc.commandSlot);
        if (view) doc.toolbarSlot->AddChild(view);
        doc.pane->AddChild(doc.toolbarSlot);
        doc.pane->AddChild(content);
        doc.pane->AddChild(doc.status);
        ShowCommandTab(doc, 0);
        return;
    }

    // One row: the general functions, and the view group last at its right
    // end; the panels at the left of the document, a splitter between.
    BuildCommandRow(doc, false);
    doc.toolbarSlot = CreateContainer(id + "-toolbar");
    doc.toolbarSlot->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    doc.toolbarSlot->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kToolbarHeight));
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    doc.toolbarSlot->SetContainerStyle(plain);
    doc.commandRow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kToolbarHeight));
    doc.commandRow->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    doc.commandRow->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    doc.toolbarSlot->AddChild(doc.commandRow);
    if (view) doc.toolbarSlot->AddChild(view);
    doc.pane->AddChild(doc.toolbarSlot);
    BuildPanels(doc);
    doc.pane->AddChild(doc.status);
}

void UltraOfficeWindow::BuildPanels(OpenDocument& doc) {
    const std::string id = "office-doc-" + std::to_string(doc.id);
    const OfficeKind kind = doc.host->GetKind();
    doc.panels = std::make_unique<ToolPanels>(doc.host->GetTools(), id + "-panels");
    UltraCanvasToolPanelStack& stack = *doc.panels->Stack();
    // The kind's order and open panels as kept; a panel the state leaves
    // out (one added since) takes its default place. The rail is the
    // Settings window's choice, and a narrow window's.
    stack.RestoreState(ToolPanelStackState::FromString(prefs_.GetPanelState(kind)));
    stack.SetRailMode(prefs_.GetToolRail());
    stack.SetAutoRailBelowWidth(static_cast<float>(OfficeSettings::kAutoRailWidth));
    stack.SetColumnWidth(static_cast<float>(prefs_.GetPanelWidth(kind)));
    const int docId = doc.id;
    stack.onOrderChanged = [this, docId](const std::vector<std::string>&) {
        if (OpenDocument* d = FindById(docId)) SavePanels(*d);
    };
    stack.onPanelOpenChanged = [this, docId](const std::string&, bool) {
        if (OpenDocument* d = FindById(docId)) SavePanels(*d);
    };
    stack.onRailModeChanged = [this, docId](bool) {
        if (OpenDocument* d = FindById(docId)) FitPanelSplit(*d);
    };

    doc.panelSplit = std::make_shared<UltraCanvasSplitPane>(id + "-panel-split", SplitOrientation::Horizontal);
    doc.panelSplit->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    doc.panelSplit->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto fill = [](const std::shared_ptr<UltraCanvasContainer>& pane, const std::shared_ptr<UltraCanvasUIElement>& element) {
        pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        ContainerStyle plain;
        plain.autoShowScrollbars = false;
        pane->SetContainerStyle(plain);
        pane->SetFollowTheme(true);
        pane->AddChild(element);
        element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        element->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };
    fill(doc.panelSplit->AddPane(0.0), doc.panels->Stack());
    fill(doc.panelSplit->AddPane(1.0), doc.host->GetContent());
    // The width the splitter is dragged to is the column's, and kept.
    doc.panelSplit->onSplitterDragEnd = [this, docId](size_t) {
        OpenDocument* d = FindById(docId);
        if (!d || !d->panelSplit || !d->panels || d->railShown) return;
        const int width = d->panelSplit->GetPaneFixedSize(0);
        if (width <= 0) return;
        const OfficeKind dragged = d->host->GetKind();
        prefs_.SetPanelWidth(dragged, width);
        settings_.Save();
        // This tab's column, and the kind's other tabs'.
        for (auto& other : documents_) {
            if (!other->panels || other->host->GetKind() != dragged) continue;
            other->panels->Stack()->SetColumnWidth(static_cast<float>(prefs_.GetPanelWidth(dragged)));
            FitPanelSplit(*other);
        }
    };
    FitPanelSplit(doc);
    doc.pane->AddChild(doc.panelSplit);
}

void UltraOfficeWindow::FitPanelSplit(OpenDocument& doc) {
    if (!doc.panelSplit || !doc.panels) return;
    UltraCanvasToolPanelStack& stack = *doc.panels->Stack();
    doc.railShown = stack.IsRailMode();
    if (doc.railShown) {
        // The rail is as wide as it is: nothing to drag.
        const int rail = static_cast<int>(std::lround(stack.GetStyle().railWidth));
        doc.panelSplit->SetPaneMinSize(0, rail);
        doc.panelSplit->SetPaneMaxSize(0, rail);
        doc.panelSplit->SetPaneFixedSize(0, rail);
    } else {
        const int width = std::clamp(static_cast<int>(std::lround(stack.GetColumnWidth())), OfficeSettings::kMinPanelWidth,
                                     OfficeSettings::kMaxPanelWidth);
        doc.panelSplit->SetPaneMaxSize(0, OfficeSettings::kMaxPanelWidth);
        doc.panelSplit->SetPaneMinSize(0, OfficeSettings::kMinPanelWidth);
        doc.panelSplit->SetPaneFixedSize(0, width);
    }
    doc.panelSplit->InvalidateLayout();
    doc.panelSplit->RequestRedraw();
}

void UltraOfficeWindow::SavePanels(OpenDocument& doc) {
    if (!doc.panels) return;
    const OfficeKind kind = doc.host->GetKind();
    const ToolPanelStackState state = doc.panels->Stack()->SaveState();
    prefs_.SetPanelState(kind, state.ToString());
    settings_.Save();
    // The kind's other tabs: the same order and open panels.
    for (auto& other : documents_) {
        if (other->id == doc.id || !other->panels || other->host->GetKind() != kind) continue;
        UltraCanvasToolPanelStack& stack = *other->panels->Stack();
        if (stack.SaveState() == state) continue;
        stack.RestoreState(state);
        stack.SetRailMode(prefs_.GetToolRail());
    }
}

ToolPanels* UltraOfficeWindow::GetToolPanels() {
    OpenDocument* doc = Active();
    return doc ? doc->panels.get() : nullptr;
}

void UltraOfficeWindow::ShowCommandTab(OpenDocument& doc, int index) {
    const auto& commandTabs = doc.host->GetCommandTabs();
    if (index < 0 || index >= static_cast<int>(commandTabs.size())) return;
    if (doc.commandTab >= 0 && doc.commandTab < static_cast<int>(commandTabs.size()))
        doc.commandSlot->RemoveChild(commandTabs[static_cast<size_t>(doc.commandTab)].toolbar);
    doc.commandTab = index;
    doc.commandSlot->AddChild(commandTabs[static_cast<size_t>(index)].toolbar);
    for (size_t i = 0; i < doc.commandButtons.size(); ++i)
        if (doc.commandButtons[i]) doc.commandButtons[i]->SetPressed(static_cast<int>(i) == index);
    doc.host->Focus();
}

// ===== OPENING =====

void UltraOfficeWindow::NewDocument(OfficeKind kind) {
    std::unique_ptr<OfficeHost> host;
    switch (kind) {
        case OfficeKind::Document: host = CreateDocumentHost(this); break;
        case OfficeKind::Spreadsheet: host = CreateSpreadsheetHost(this); break;
        case OfficeKind::Presentation: host = CreatePresentationHost(this); break;
        default: return;
    }
    host->AttachSettings(&settings_);   // a new one starts with the settings' font, page, slide size
    host->NewBlank();
    const int n = ++untitled_[KindSlot(kind)];
    const std::string name = kind == OfficeKind::Spreadsheet  ? TrF("Spreadsheet {0}", n)
                           : kind == OfficeKind::Presentation ? TrF("Presentation {0}", n)
                                                              : TrF("Document {0}", n);
    AddDocument(std::move(host), "", name, "");
}

void UltraOfficeWindow::NewFromTemplate(const std::string& path) {
    const OfficeKind kind = UltraCanvasFileLoader::DetectOfficeDocumentKind(path);
    if (kind == OfficeKind::Unknown) {
        ShowError(Tr("New from template"), TrF("{0} is not a document, spreadsheet or presentation UltraOffice can open.",
                                                FileNameOf(path)));
        return;
    }
    UltraCanvasFileLoader::RequestDocumentPassword(path, this, [this, path, kind](bool proceed, const std::string& password) {
        if (!proceed) return;
        std::unique_ptr<OfficeHost> host = kind == OfficeKind::Spreadsheet  ? CreateSpreadsheetHost(this)
                                         : kind == OfficeKind::Presentation ? CreatePresentationHost(this)
                                                                            : CreateDocumentHost(this);
        std::string error;
        if (!host->Load(path, password, error)) {
            ShowError(Tr("New from template"), TrF("{0} could not be opened: {1}", FileNameOf(path), error));
            return;
        }
        // Untitled and without the template's password: Save asks for a name,
        // so the template stays as it is.
        host->SetModified(false);
        const int n = ++untitled_[KindSlot(kind)];
        const std::string name = kind == OfficeKind::Spreadsheet  ? TrF("Spreadsheet {0}", n)
                               : kind == OfficeKind::Presentation ? TrF("Presentation {0}", n)
                                                                  : TrF("Document {0}", n);
        AddDocument(std::move(host), "", name, "");
    });
}

void UltraOfficeWindow::OpenPaths(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) OpenPath(path);
}

void UltraOfficeWindow::OpenPath(const std::string& given) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(PathFromUtf8(given), ec);
    const std::string path = ec ? given : PathToUtf8(absolute.lexically_normal());

    for (auto& doc : documents_) {
        if (doc->path == path) { tabs_->SetActiveTab(TabIndexOf(*doc)); return; }
    }
    if (!std::filesystem::exists(PathFromUtf8(path), ec)) {
        ShowError(Tr("Open"), TrF("The file {0} does not exist.", path));
        prefs_.ForgetRecentFile(path);
        settings_.Save();
        return;
    }
    // A template starts a new document, whichever way it was opened.
    if (UltraCanvasFileLoader::IsOfficeTemplate(path)) {
        Remember(path);
        NewFromTemplate(path);
        return;
    }
    const OfficeKind kind = UltraCanvasFileLoader::DetectOfficeDocumentKind(path);
    if (kind == OfficeKind::Unknown) {
        ShowError(Tr("Open"), TrF("{0} is not a document, spreadsheet or presentation UltraOffice can open.", FileNameOf(path)));
        return;
    }
    // A protected file asks for its password first.
    UltraCanvasFileLoader::RequestDocumentPassword(path, this, [this, path, kind](bool proceed, const std::string& password) {
        if (!proceed) return;
        std::unique_ptr<OfficeHost> host = kind == OfficeKind::Spreadsheet  ? CreateSpreadsheetHost(this)
                                         : kind == OfficeKind::Presentation ? CreatePresentationHost(this)
                                                                            : CreateDocumentHost(this);
        std::string error;
        if (!host->Load(path, password, error)) {
            ShowError(Tr("Open"), TrF("{0} could not be opened: {1}", FileNameOf(path), error));
            return;
        }
        AddDocument(std::move(host), path, FileNameOf(path), password);
        Remember(path);
        UltraCanvasFileLoader::NotifyRecentFile(path);
    });
}

void UltraOfficeWindow::ShowOpenDialog() {
    FileDialogOptions opts;
    opts.title = Tr("Open");
    UltraCanvasFileLoader::AddOfficeDocumentLoadTypes(opts);
    opts.AddFilter(Tr("All files"), "*");
    opts.parentWindow = this;
    if (OpenDocument* doc = Active(); doc && !doc->path.empty())
        opts.initialDirectory = PathToUtf8(PathFromUtf8(doc->path).parent_path());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) OpenPath(path);
    });
}

// ===== THE FILE MENU =====

void UltraOfficeWindow::ShowFileMenu(UltraCanvasUIElement* anchor) {
    if (!menu_) {
        menu_ = std::make_shared<UltraCanvasMenu>("office-file-menu", 0, 0, 260, 0);
        menu_->SetMenuType(MenuType::PopupMenu);
    }
    menu_->Clear();
    OpenDocument* doc = Active();
    const int docId = doc ? doc->id : -1;
    auto withDoc = [this, docId](std::function<void(OpenDocument&)> action) {
        return [this, docId, action]() { if (OpenDocument* d = FindById(docId)) action(*d); };
    };
    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("New document"), "Ctrl+N", [this]() { NewDocument(OfficeKind::Document); }));
    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("New spreadsheet"), "Ctrl+Shift+N", [this]() { NewDocument(OfficeKind::Spreadsheet); }));
    menu_->AddItem(MenuItemData::Action(Tr("New presentation"), [this]() { NewDocument(OfficeKind::Presentation); }));
    std::vector<MenuItemData> fromTemplate;
    for (const OfficeTemplate& t : ListTemplates(OwnTemplatesFolder())) {
        const std::string path = t.path;
        fromTemplate.push_back(MenuItemData::Action(t.name, [this, path]() { NewFromTemplate(path); }));
    }
    if (!fromTemplate.empty()) fromTemplate.push_back(MenuItemData::Separator());
    fromTemplate.push_back(MenuItemData::Action(Tr("Other template..."), [this]() { ShowTemplateOpenDialog(); }));
    menu_->AddItem(MenuItemData::Submenu(Tr("New from template..."), fromTemplate));
    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Open..."), "Ctrl+O", [this]() { ShowOpenDialog(); }));
    std::vector<MenuItemData> recent;
    for (const std::string& path : prefs_.GetRecentFiles())
        recent.push_back(MenuItemData::Action(FileNameOf(path), [this, path]() { OpenPath(path); }));
    if (!recent.empty()) menu_->AddItem(MenuItemData::Submenu(Tr("Open recent"), recent));
    if (doc) {
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Save"), "Ctrl+S", withDoc([this](OpenDocument& d) { Save(d, false); })));
        menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Save as..."), "Ctrl+Shift+S", withDoc([this](OpenDocument& d) { Save(d, true); })));
        menu_->AddItem(MenuItemData::Action(Tr("Save as template..."), withDoc([this](OpenDocument& d) { SaveAsTemplate(d); })));
        if (doc->host->CanExportPdf()) {
            menu_->AddItem(MenuItemData::Action(Tr("Export as PDF..."), withDoc([this](OpenDocument& d) { ExportPdf(d); })));
            menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Print..."), "Ctrl+P", withDoc([this](OpenDocument& d) { Print(d); })));
        }
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Close"), "Ctrl+W", withDoc([this](OpenDocument& d) { RequestCloseDocument(d); })));
    }
    menu_->AddItem(MenuItemData::Separator());
    const SaveFormat format = GetSaveFormat();
    menu_->AddItem(MenuItemData::Submenu(Tr("New files are saved as"), {
        MenuItemData::Radio(Tr("Office Open XML (.docx, .xlsx, .pptx)"), 1, format == SaveFormat::OfficeOpenXml,
                            [this]() { SetSaveFormat(SaveFormat::OfficeOpenXml); }),
        MenuItemData::Radio(Tr("OpenDocument (.odt, .ods, .odp)"), 1, format == SaveFormat::OpenDocument,
                            [this]() { SetSaveFormat(SaveFormat::OpenDocument); })}));
    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Settings..."), "Ctrl+,", [this]() { ShowSettings(); }));
    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Quit"), "Ctrl+Q", [this]() { Close(); }));

    PopupElementSettings popup;
    popup.closeByEscapeKey = true;
    popup.closeByClickOutside = true;
    if (anchor) {
        const Point2Df at = anchor->GetPositionInWindow();
        menu_->OpenMenuAnchored(Rect2Di(static_cast<int>(at.x), static_cast<int>(at.y),
                                        static_cast<int>(anchor->GetWidth()), static_cast<int>(anchor->GetHeight())),
                                *this, popup);
    } else {
        menu_->OpenMenu(Point2Di(12, 40), *this, popup);
    }
}

// ===== SAVING =====

void UltraOfficeWindow::Save(OpenDocument& doc, bool askForName, std::function<void(bool)> done) {
    const bool inPlace = !askForName && doc.host->CanSaveInPlace() && !doc.path.empty() && !IsReadOnlyFormat(doc.path);
    if (inPlace) {
        const bool ok = WriteTo(doc, doc.path);
        if (done) done(ok);
        return;
    }
    FileDialogOptions opts;
    opts.title = Tr("Save as");
    const std::string ext = doc.path.empty() || IsReadOnlyFormat(doc.path) || !doc.host->CanSaveInPlace()
        ? doc.host->DefaultExtension(GetSaveFormat()) : LowerExtension(doc.path);
    opts.defaultFileName = StemOf(doc.name) + "." + ext;
    if (!doc.path.empty()) opts.initialDirectory = PathToUtf8(PathFromUtf8(doc.path).parent_path());
    doc.host->AddSaveTypes(opts);
    opts.parentWindow = this;
    const int docId = doc.id;
    UltraCanvasFileLoader::SaveFileDialog(opts, [this, docId, done](DialogResult result, const std::string& chosen) {
        OpenDocument* d = FindById(docId);
        if (!d || result != DialogResult::OK || chosen.empty()) { if (done) done(false); return; }
        std::string path = chosen;
        if (LowerExtension(path).empty()) path += "." + d->host->DefaultExtension(GetSaveFormat());
        const bool ok = WriteTo(*d, path);
        if (ok && d->host->CanSaveInPlace()) {
            d->path = path;
            d->name = FileNameOf(path);
            const int index = TabIndexOf(*d);
            tabs_->SetTabTitle(index, d->name);
            tabs_->SetTabTooltip(index, path);
            Remember(path);
        }
        if (done) done(ok);
    });
}

bool UltraOfficeWindow::WriteTo(OpenDocument& doc, const std::string& path) {
    std::string error;
    if (!doc.host->Save(path, doc.password, error)) {
        ShowError(Tr("Save"), TrF("{0} could not be saved: {1}", FileNameOf(path), error));
        return false;
    }
    UltraCanvasFileLoader::NotifyRecentFile(path);
    RemoveRecovery(doc);   // saved: its copy is no longer needed
    Refresh(doc);
    return true;
}

void UltraOfficeWindow::ExportPdf(OpenDocument& doc) {
    FileDialogOptions opts;
    opts.title = Tr("Export as PDF");
    opts.filters = {FileFilter(Tr("PDF document"), std::vector<std::string>{"pdf"})};
    opts.defaultFileName = StemOf(doc.name) + ".pdf";
    if (!doc.path.empty()) opts.initialDirectory = PathToUtf8(PathFromUtf8(doc.path).parent_path());
    opts.parentWindow = this;
    const int docId = doc.id;
    UltraCanvasFileLoader::SaveFileDialog(opts, [this, docId](DialogResult result, const std::string& path) {
        OpenDocument* d = FindById(docId);
        if (!d || result != DialogResult::OK || path.empty()) return;
        std::string error;
        if (!d->host->ExportPdf(path, error)) ShowError(Tr("Export as PDF"), error);
    });
}

void UltraOfficeWindow::Print(OpenDocument& doc) {
    std::string error;
    if (!doc.host->Print(doc.name, this, error)) ShowError(Tr("Print"), error);
}

// ===== TEMPLATES =====

std::vector<UltraOfficeWindow::DocumentSummary> UltraOfficeWindow::GetDocuments() const {
    std::vector<DocumentSummary> out;
    for (int i = 0; tabs_ && i < tabs_->GetTabCount(); ++i) {
        for (const auto& doc : documents_) {
            if (tabs_->GetTabContent(i) != doc->pane) continue;
            out.push_back({doc->path, doc->name, doc->host->GetKind(), doc->host->IsModified()});
        }
    }
    return out;
}

std::filesystem::path UltraOfficeWindow::OwnTemplatesFolder() const {
    const std::string file = settings_.File();
    if (file.empty()) return {};
    return PathFromUtf8(file).parent_path() / "templates";
}

void UltraOfficeWindow::ShowTemplateOpenDialog() {
    FileDialogOptions opts;
    opts.title = Tr("New from template");
    UltraCanvasFileLoader::AddOfficeDocumentLoadTypes(opts);
    opts.AddFilter(Tr("All files"), "*");
    opts.parentWindow = this;
    std::error_code ec;
    const std::filesystem::path own = OwnTemplatesFolder();
    if (!own.empty() && std::filesystem::is_directory(own, ec)) opts.initialDirectory = PathToUtf8(own);
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) NewFromTemplate(path);
    });
}

void UltraOfficeWindow::SaveAsTemplate(OpenDocument& doc) {
    const OfficeKind kind = doc.host->GetKind();
    FileDialogOptions opts;
    opts.title = Tr("Save as template");
    opts.defaultFileName = StemOf(doc.name) + "." + TemplateExtension(kind, GetSaveFormat());
    UltraCanvasFileLoader::AddOfficeTemplateSaveTypes(opts, kind);
    opts.parentWindow = this;
    // The user's templates folder, so the gallery offers it from then on.
    const std::filesystem::path own = OwnTemplatesFolder();
    if (!own.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(own, ec);
        if (std::filesystem::is_directory(own, ec)) opts.initialDirectory = PathToUtf8(own);
    }
    const int docId = doc.id;
    UltraCanvasFileLoader::SaveFileDialog(opts, [this, docId, kind](DialogResult result, const std::string& chosen) {
        OpenDocument* d = FindById(docId);
        if (!d || result != DialogResult::OK || chosen.empty()) return;
        std::string path = chosen;
        if (!UltraCanvasFileLoader::IsOfficeTemplateExtension(path)) path += "." + TemplateExtension(kind, GetSaveFormat());
        // Written as a copy: the document keeps its own file, name and
        // unsaved changes.
        std::string error;
        if (!d->host->Write(path, "", error)) {
            ShowError(Tr("Save as template"), TrF("{0} could not be saved: {1}", FileNameOf(path), error));
            return;
        }
        RebuildStartPageLater();   // the gallery shows it
    });
}

// ===== CLOSING =====

void UltraOfficeWindow::RequestCloseDocument(OpenDocument& doc) {
    if (!doc.host->IsModified()) { CloseDocument(doc); return; }
    const int docId = doc.id;
    UltraCanvasDialogManager::ShowMessage(
        TrF("Save the changes to {0} before closing it?", doc.name), "UltraOffice",
        DialogType::Question, DialogButtons::YesNoCancel,
        [this, docId](DialogResult result) {
            OpenDocument* d = FindById(docId);
            if (!d || result == DialogResult::Cancel || result == DialogResult::NoResult) return;
            if (result == DialogResult::No) { CloseDocument(*d); return; }
            Save(*d, false, [this, docId](bool saved) {
                if (OpenDocument* again = FindById(docId); again && saved) CloseDocument(*again);
            });
        }, this);
}

void UltraOfficeWindow::CloseDocument(OpenDocument& doc) {
    RemoveRecovery(doc);
    const int index = TabIndexOf(doc);
    const int id = doc.id;
    if (index >= 0) tabs_->RemoveTab(index);
    documents_.erase(std::remove_if(documents_.begin(), documents_.end(),
                                    [id](const std::unique_ptr<OpenDocument>& d) { return d->id == id; }),
                     documents_.end());
    UpdateTitle();
}

// ===== RECOVERY =====

std::filesystem::path UltraOfficeWindow::RecoveryFolder() const {
    const std::string file = settings_.File();
    if (file.empty()) return {};
    return PathFromUtf8(file).parent_path() / "recovery";
}

void UltraOfficeWindow::StartAutosave() {
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app || RecoveryFolder().empty()) return;
    const unsigned interval = static_cast<unsigned>(prefs_.GetAutosaveMinutes()) * kMinuteMs;
    autosaveTimer_ = app->StartTimer(interval, true, [this](TimerId) { Autosave(); });
}

void UltraOfficeWindow::RestartAutosave() {
    if (autosaveTimer_)
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(autosaveTimer_);
    autosaveTimer_ = 0;
    StartAutosave();
}

void UltraOfficeWindow::Autosave() {
    const bool toFile = prefs_.GetAutosave();
    const bool copies = prefs_.GetRecoveryCopies();
    for (auto& doc : documents_) {
        // Autosave: back into its own file, when it has one it can be
        // written to; quietly - a document it cannot write keeps its
        // recovery copy, and Save says why.
        if (toFile && doc->host->IsModified() && !doc->path.empty() && doc->host->CanSaveInPlace() &&
            !IsReadOnlyFormat(doc->path)) {
            std::string error;
            if (doc->host->Save(doc->path, doc->password, error)) {
                RemoveRecovery(*doc);
                Refresh(*doc);
                continue;
            }
        }
        if (copies && doc->host->IsModified()) {
            if (doc->changedSinceCopy || doc->recoveryCopy.empty()) WriteRecovery(*doc);
        } else if (!doc->recoveryCopy.empty()) {
            RemoveRecovery(*doc);   // undone back to the saved state, or no copies wanted
        }
    }
}

void UltraOfficeWindow::WriteRecovery(OpenDocument& doc) {
    const std::filesystem::path folder = RecoveryFolder();
    if (folder.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    // In its own type where it can be written back there, so a restored
    // document saves as it would have; else the default type of its kind.
    const std::string ext = !doc.path.empty() && !IsReadOnlyFormat(doc.path) && doc.host->CanSaveInPlace()
        ? LowerExtension(doc.path) : doc.host->DefaultExtension(GetSaveFormat());
    const std::string stem = runTag_ + "-" + std::to_string(doc.id);
    const std::filesystem::path copy = folder / PathFromUtf8(stem + "." + ext);
    // Written under another name first and moved over the last copy, so a
    // crash while writing still leaves that one whole.
    const std::filesystem::path writing = folder / PathFromUtf8(stem + ".writing." + ext);
    std::string error;
    if (!doc.host->Write(PathToUtf8(writing), doc.password, error)) {
        std::filesystem::remove(writing, ec);
        return;
    }
    std::filesystem::rename(writing, copy, ec);
    if (ec) { std::filesystem::remove(writing, ec); return; }
    if (!doc.recoveryCopy.empty() && doc.recoveryCopy != copy) std::filesystem::remove(doc.recoveryCopy, ec);
    doc.recoveryCopy = copy;
    doc.changedSinceCopy = false;

    UltraCanvasAppSettings note = UltraCanvasAppSettings::AtFile(PathToUtf8(folder / PathFromUtf8(stem + ".json")));
    note.Set("kind", KindName(doc.host->GetKind()));
    note.Set("file", doc.path);
    note.Set("name", doc.name);
    note.Set("copy", PathToUtf8(copy.filename()));
    note.Save();
}

void UltraOfficeWindow::RemoveRecovery(OpenDocument& doc) {
    if (doc.recoveryCopy.empty()) return;
    std::error_code ec;
    std::filesystem::remove(doc.recoveryCopy, ec);
    std::filesystem::remove(RecoveryFolder() / PathFromUtf8(runTag_ + "-" + std::to_string(doc.id) + ".json"), ec);
    doc.recoveryCopy.clear();
    doc.changedSinceCopy = true;
}

void UltraOfficeWindow::RemoveAllRecovery() {
    for (auto& doc : documents_) RemoveRecovery(*doc);
}

void UltraOfficeWindow::OfferRecovery() {
    const std::filesystem::path folder = RecoveryFolder();
    if (folder.empty()) return;
    // The notes there now are an earlier run's: this run's copies are named
    // with its own tag, so the list does not change while it is offered.
    recoveryNotes_.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") recoveryNotes_.push_back(entry.path());
    }
    if (recoveryNotes_.empty()) return;
    std::sort(recoveryNotes_.begin(), recoveryNotes_.end());
    BuildStartPage();
    ShowRecoveryBadge();
}

// The Start tab counts the recovered documents waiting there.
void UltraOfficeWindow::ShowRecoveryBadge() {
    if (recoveryNotes_.empty()) tabs_->ClearTabBadge(0);
    else tabs_->SetTabBadge(0, std::to_string(recoveryNotes_.size()));
}

void UltraOfficeWindow::RebuildStartPageLater() {
    // Not from inside one of the page's own buttons: the page goes once the
    // click is over.
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    app->StartTimer(1, false, [this](TimerId) {
        BuildStartPage();
        ShowRecoveryBadge();
    });
}

void UltraOfficeWindow::RestoreAllRecovered() {
    std::vector<std::filesystem::path> notes;
    notes.swap(recoveryNotes_);
    RebuildStartPageLater();
    RestoreRecovered(notes, 0);
}

void UltraOfficeWindow::DiscardAllRecovered() {
    // The copies are the only place those changes are: asked first.
    UltraCanvasDialogManager::ShowConfirmation(
        Tr("Delete the copies of the documents that were not saved? Their changes cannot be brought back."),
        Tr("Recovered documents"),
        [this](bool yes) {
            if (!yes) return;
            std::error_code ec;
            for (const auto& note : recoveryNotes_) {
                UltraCanvasAppSettings kept = UltraCanvasAppSettings::AtFile(PathToUtf8(note));
                kept.Load();
                const std::string copy = kept.GetString("copy");
                if (!copy.empty()) std::filesystem::remove(note.parent_path() / PathFromUtf8(copy), ec);
                std::filesystem::remove(note, ec);
            }
            recoveryNotes_.clear();
            RebuildStartPageLater();
        }, this);
}

void UltraOfficeWindow::RestoreRecovered(const std::vector<std::filesystem::path>& notes, size_t next) {
    if (next >= notes.size()) return;
    const std::filesystem::path notePath = notes[next];
    UltraCanvasAppSettings note = UltraCanvasAppSettings::AtFile(PathToUtf8(notePath));
    note.Load();
    const OfficeKind kind = KindFromName(note.GetString("kind"));
    const std::string copyName = note.GetString("copy");
    const std::filesystem::path copy = notePath.parent_path() / PathFromUtf8(copyName);
    std::error_code ec;
    if (kind == OfficeKind::Unknown || copyName.empty()
        || !std::filesystem::exists(copy, ec)) {
        std::filesystem::remove(notePath, ec);
        RestoreRecovered(notes, next + 1);
        return;
    }
    const std::string file = note.GetString("file");
    const std::string name = note.GetString("name", FileNameOf(file));
    // A protected document's copy is protected with its password too.
    UltraCanvasFileLoader::RequestDocumentPassword(PathToUtf8(copy), this,
        [this, notes, next, notePath, copy, kind, file, name](bool proceed, const std::string& password) {
            if (proceed) {
                std::unique_ptr<OfficeHost> host = kind == OfficeKind::Spreadsheet  ? CreateSpreadsheetHost(this)
                                                 : kind == OfficeKind::Presentation ? CreatePresentationHost(this)
                                                                                    : CreateDocumentHost(this);
                std::string error;
                if (host->Load(PathToUtf8(copy), password, error)) {
                    // Back under its own file and name, with its changes
                    // still to save.
                    OpenDocument* doc = AddDocument(std::move(host), file, name, password);
                    if (file.empty()) ++untitled_[KindSlot(kind)];   // the next new one is not "Document 1" again
                    doc->host->SetModified(true);
                    Refresh(*doc);
                    std::error_code removeError;
                    std::filesystem::remove(copy, removeError);
                    std::filesystem::remove(notePath, removeError);
                } else {
                    ShowError(Tr("Restore documents"), TrF("{0} could not be restored: {1}", name, error));
                }
            }
            RestoreRecovered(notes, next + 1);
        });
}

// ===== KEYBOARD =====

// A filter, not OnEvent: the window's own keys come before the element with
// the keyboard, which would otherwise take Ctrl+S or Ctrl+N for itself.
bool UltraOfficeWindow::HandleShortcut(const UCEvent& event) {
    // F6 / Shift+F6: the tab strip, the toolbar, the panels, the document.
    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::F6 && !event.ctrl && !event.alt) {
        CycleFocus(event.shift);
        return true;
    }
    // The tab's own keys first (F5: the document's navigator).
    if (event.type == UCEventType::KeyDown)
        if (OpenDocument* doc = Active(); doc && doc->host->HandleShortcut(event)) return true;
    if (event.type == UCEventType::KeyDown && event.ctrl && !event.alt) {
        OpenDocument* doc = Active();
        // The zoom of the tab open: Ctrl++ (or Ctrl+=, the same key), Ctrl+-,
        // Ctrl+0.
        if (doc) {
            switch (event.virtualKey) {
                case UCKeys::Plus: case UCKeys::Equal: case UCKeys::NumPadPlus: doc->host->ZoomIn(); return true;
                case UCKeys::Minus: case UCKeys::NumPadMinus: doc->host->ZoomOut(); return true;
                case UCKeys::Key0: if (!event.shift) { doc->host->ZoomToActualSize(); return true; } break;
                default: break;
            }
        }
        switch (event.virtualKey) {
            case UCKeys::Comma: ShowSettings(); return true;
            case UCKeys::N: NewDocument(event.shift ? OfficeKind::Spreadsheet : OfficeKind::Document); return true;
            case UCKeys::O: ShowOpenDialog(); return true;
            case UCKeys::S: if (doc) { Save(*doc, event.shift); return true; } break;
            case UCKeys::P: if (doc && doc->host->CanExportPdf()) { Print(*doc); return true; } break;
            case UCKeys::W: if (doc) { RequestCloseDocument(*doc); return true; } break;
            case UCKeys::Q: Close(); return true;
            default: break;
        }
    }
    return false;
}

void UltraOfficeWindow::CycleFocus(bool backwards) {
    OpenDocument* doc = Active();
    UltraCanvasUIElement* focused = GetFocusedElement();
    // Where the keyboard is: 0 the tab strip, 1 the toolbar, 2 the panels, 3 the document.
    int at = 3;
    if (focused == tabs_.get()) at = 0;
    else if (doc && doc->toolbarSlot && IsInside(focused, doc->toolbarSlot.get())) at = 1;
    else if (doc && doc->panels && IsInside(focused, doc->panels->Stack().get())) at = 2;
    else if (doc && doc->panels && doc->panels->Stack()->IsFlyoutOpen()) at = 2;
    else if (!doc) at = 0;   // the Start tab: from the strip to its page and back
    for (int step = 1; step <= 4; ++step) {
        const int next = ((at + (backwards ? -step : step)) % 4 + 4) % 4;
        switch (next) {
            case 0:
                if (tabs_->FocusTabStrip()) return;
                break;
            case 1:
                if (doc && doc->fileButton && doc->fileButton->SetFocus(true)) return;
                break;
            case 2:
                if (doc && doc->panels && doc->panels->Stack()->FocusFirst()) return;
                break;
            default:
                if (doc) {
                    doc->host->Focus();
                    return;
                }
                break;
        }
    }
}

// ===== BOOKKEEPING =====

UltraOfficeWindow::OpenDocument* UltraOfficeWindow::Active() {
    const int index = tabs_ ? tabs_->GetActiveTab() : -1;
    for (auto& doc : documents_) if (TabIndexOf(*doc) == index) return doc.get();
    return nullptr;
}

UltraOfficeWindow::OpenDocument* UltraOfficeWindow::FindById(int id) {
    for (auto& doc : documents_) if (doc->id == id) return doc.get();
    return nullptr;
}

int UltraOfficeWindow::TabIndexOf(const OpenDocument& doc) const {
    for (int i = 0; i < tabs_->GetTabCount(); ++i)
        if (tabs_->GetTabContent(i) == doc.pane) return i;
    return -1;
}

void UltraOfficeWindow::Refresh(OpenDocument& doc) {
    const int index = TabIndexOf(doc);
    if (index >= 0) tabs_->SetTabShowMarker(index, doc.host->IsModified());
    if (doc.status) doc.status->SetText(doc.host->GetStatusText());
    UpdateTitle();
}

void UltraOfficeWindow::UpdateTitle() {
    OpenDocument* doc = Active();
    SetWindowTitle(doc ? doc->name + (doc->host->IsModified() ? " *" : "") + " - UltraOffice" : std::string("UltraOffice"));
}

void UltraOfficeWindow::Remember(const std::string& path) {
    prefs_.RememberRecentFile(path);
    settings_.Save();
}

void UltraOfficeWindow::RememberOpenFiles() {
    std::vector<std::string> open;
    for (const DocumentSummary& doc : GetDocuments())
        if (!doc.path.empty()) open.push_back(doc.path);
    prefs_.SetOpenFiles(open);
    settings_.Save();
}

SaveFormat UltraOfficeWindow::GetSaveFormat() const { return prefs_.GetSaveFormat(); }

void UltraOfficeWindow::SetSaveFormat(SaveFormat format) {
    prefs_.SetSaveFormat(format);
    settings_.Save();
}

// ===== SETTINGS =====

void UltraOfficeWindow::ShowSettings() { ShowSettingsDialog(*this); }

void UltraOfficeWindow::DismissToolsNote() {
    if (prefs_.GetToolsNoteDismissed()) return;
    prefs_.SetToolsNoteDismissed(true);
    settings_.Save();
    RebuildStartPageLater();
}

bool UltraOfficeWindow::IsToolsNoteShown() const {
    return startPane_ && startPane_->FindChildById("office-start-tools-dismiss") != nullptr;
}

void UltraOfficeWindow::ApplyTheme() {
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    const ThemeChoice theme = prefs_.GetTheme();
    const ThemePolicy wanted = theme == ThemeChoice::Light ? ThemePolicy::Light
                             : theme == ThemeChoice::Dark  ? ThemePolicy::Dark
                                                           : ThemePolicy::FollowHost;
    // Set only on a change: SetThemePolicy builds and applies the theme.
    if (app->GetThemePolicy() != wanted) app->SetThemePolicy(wanted);
}

void UltraOfficeWindow::ApplySettings() {
    ApplyTheme();
    RestartAutosave();
    for (auto& doc : documents_) {
        doc->host->ApplySettings();
        // The tools: panels or the toolbar, at once; the rail as chosen.
        if (doc->layout != prefs_.GetToolLayout()) {
            LayoutTools(*doc);
            if (doc.get() == Active()) doc->host->Focus();
        } else if (doc->panels) {
            doc->panels->Stack()->SetRailMode(prefs_.GetToolRail());
            FitPanelSplit(*doc);
        }
        Refresh(*doc);
    }
    RebuildStartPageLater();   // the recent files' count
}

void UltraOfficeWindow::ClearRecentFiles() {
    prefs_.ClearRecentFiles();
    settings_.Save();
    RebuildStartPageLater();
}

void UltraOfficeWindow::ShowError(const std::string& title, const std::string& message) {
    UltraCanvasDialogManager::ShowError(message, title, nullptr, this);
}

} // namespace UltraOffice
