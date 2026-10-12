// Apps/UltraNetMonitor/ui/UltraNetMonitorWindow.cpp
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorWindow.h"

#include "UltraNetMonitorPaths.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <variant>

// ULTRANETMONITOR_VERSION comes from the build alone: CMake reads the first line
// of Docs/UltraNetMonitor/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it
// as a compile definition. No fallback here, so a build that lost it
// fails instead of reporting a wrong number.
#ifndef ULTRANETMONITOR_VERSION
#error "ULTRANETMONITOR_VERSION is not defined: build through CMake, which reads it from Docs/UltraNetMonitor/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace UltraNetMonitor {
namespace {

constexpr float kWindowWidth  = 1180.0f;
constexpr float kWindowHeight = 720.0f;
constexpr float kPagePadding  = 10.0f;   // around the toolbar and the tabs
constexpr float kTabPadding   = 8.0f;    // inside a tab's page, around its bar and list
constexpr float kSectionGap   = 8.0f;
constexpr float kControlHeight = 28.0f;
constexpr int   kInfoDialogWidth = 640;
constexpr int   kProcessColumnWidth = 470;
constexpr int   kProcessColumnMin   = 300;

constexpr unsigned kSnapshotIntervalMs = 1000;
constexpr unsigned kUiTimerMs          = 200;

// Tab order, as added below.
constexpr int kLiveTab    = 0;
constexpr int kHistoryTab = 1;
constexpr int kNamesTab   = 2;
constexpr int kEventsTab  = 3;
constexpr int kEventsShown = 1000;

// The history range dropdown's entries, in seconds.
const int64_t kRanges[] = { 3600, 24 * 3600, 7 * 24 * 3600, 30 * 24 * 3600 };
const char* const kRangeLabels[] = { "Last hour", "Last 24 hours", "Last 7 days", "Last 30 days" };
constexpr int kHistoryLimit = 5000;

// A split pane's panes are plain containers; a list fills one edge to edge.
// Basis zero and grow one rather than a percentage, so the pane's size is
// what decides and the list can never hold a size of its own against it.
void FillWith(const std::shared_ptr<UltraCanvasContainer>& container,
              const std::shared_ptr<UltraCanvasUIElement>& element) {
    container->AddChild(element);
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetFlexGrow(1);
    element->layoutItem.SetFlexShrink(1);
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void MakePlainColumn(const std::shared_ptr<UltraCanvasContainer>& container) {
    container->layout.SetFlexColumn()
                     .SetFlexGap(kSectionGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    container->SetContainerStyle(plain);
}

std::shared_ptr<UltraCanvasContainer> MakeToolbar(const std::string& id) {
    auto bar = CreateContainer(id, 0, 0, 0, 34);
    bar->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(34));
    ContainerStyle plainBar;
    plainBar.autoShowScrollbars = false;
    bar->SetContainerStyle(plainBar);
    return bar;
}

void PinToolbar(const std::shared_ptr<UltraCanvasUIElement>& bar) {
    bar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bar->layoutItem.SetFlexGrow(0);
    bar->layoutItem.SetFlexShrink(0);
}

// A toolbar button is as wide as its label: it is given no width, so the
// layout measures the text (UltraCanvasButton::MeasureOwnContent), and a
// longer label - "Stop recording" for "Record", or a translation - fits
// instead of being cut to "All applicati…". It never shrinks either; the
// status text beside it gives way.
std::shared_ptr<UltraCanvasButton> MakeToolbarButton(const std::string& id, const std::string& text) {
    auto button = CreateButton(id, 0, 0, 0, kControlHeight, text);
    button->layoutItem.SetFlexShrink(0);
    return button;
}

// The status text at the end of a bar takes what the controls leave, and
// gives it back first: basis zero, so a long text is ellipsized by the
// label rather than pushing the button after it out of the bar.
std::shared_ptr<UltraCanvasLabel> MakeBarStatus(const std::string& id, const std::string& text = "") {
    auto label = CreateLabel(id, 0, 0, 0, 24, text);
    label->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    label->layoutItem.SetFlexGrow(1);
    label->layoutItem.SetFlexShrink(1);
    return label;
}

// A tab's page: its bar above its list, padded so the bar sits as far from
// the tab frame as the list sits from the bar. No explicit size: the tabbed
// container measures its active page with an exact width and height, so
// the page must not carry a size of its own that competes with that.
std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id) {
    auto page = CreateContainer(id, 0, 0, 0, 0);
    MakePlainColumn(page);
    page->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    page->SetPadding(kTabPadding);
    return page;
}

// A note's headline: the part before its colon ("20 processes could not be
// opened in the last snapshot"); what follows is the explanation, which the
// status line has no room for and the tooltip and the Info dialog carry.
std::string NoteHeadline(const std::string& note) {
    const std::size_t colon = note.find(':');
    return colon == std::string::npos ? note : note.substr(0, colon);
}

// A start-up note about an event source rather than a name source.
bool IsEventNote(const std::string& note) {
    return note.rfind("Events", 0) == 0 || note.find("conntrack") != std::string::npos ||
           note.find("kernel network") != std::string::npos;
}

// One source on a status line: its count while it works, else one word -
// "stopped", or `troubleWord` for a source that runs but reports an error
// ("failed" for a name source, "idle" for an event source whose tracker
// has nothing to report) - the error itself is under Info… and in the
// line's tooltip, where there is room for it.
std::string SourceOnLine(const std::string& name, bool running, const std::string& lastError,
                         int64_t count, const char* unit, const char* troubleWord) {
    if (!running) return name + ": stopped";
    if (!lastError.empty()) return name + ": " + troubleWord;
    return name + ": " + std::to_string(count) + " " + unit;
}

// Free text into the dialog's Markdown: a backend note names "sock_diag"
// and "nf_conntrack", whose underscores would otherwise start italics.
std::string EscapeMarkdown(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\\' || c == '*' || c == '_' || c == '`' || c == '[' || c == ']' ||
            c == '<' || c == '>' || c == '$' || c == '#' || c == '|' || c == '~') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// The same source in full, for the tooltip and the dialog.
std::string SourceInFull(const std::string& name, bool running, const std::string& lastError,
                         int64_t count, const char* unit) {
    std::string text = name + ": " + (running ? "running" : "stopped") + ", " +
                       std::to_string(count) + " " + unit;
    if (!lastError.empty()) text += " - " + lastError;
    return text;
}

// Header clicks sort; the view shows the order but the proxy decides it.
void WireHeaderSorting(const std::shared_ptr<UltraCanvasListView>& view,
                       const std::shared_ptr<UltraCanvasListSortFilterProxy>& proxy) {
    UltraCanvasListView* rawView = view.get();
    UltraCanvasListSortFilterProxy* rawProxy = proxy.get();
    view->onHeaderClicked = [rawView, rawProxy](int column) {
        const bool ascending = !(column == rawView->GetSortColumn() && rawView->GetSortAscending());
        rawProxy->SortByColumn(column, ascending ? ListSortOrder::Ascending
                                                 : ListSortOrder::Descending);
        rawView->SetSortIndicator(column, ascending);
    };
}

// The PID a source row belongs to, through the model's SortRole so no cast
// to the concrete model is needed. 0 is the unattributed group.
int PidOfSourceRow(const IListModel& model, int sourceRow, int pidColumn) {
    const ListDataValue value = model.GetData(ListIndex{sourceRow, pidColumn}, ListDataRole::SortRole);
    if (const int* pid = std::get_if<int>(&value)) return *pid;
    return 0;
}

} // namespace

UltraNetMonitorWindow::~UltraNetMonitorWindow() {
    StopWorker();
    // The sources first: their threads are the other writers of the store.
    NetworkMonitor_StopEventSources();
    NetworkMonitor_StopNameSources();
    if (eventListener_ != 0) NetworkMonitor_RemoveEventListener(eventListener_);
    if (nameListener_ != 0) NetworkMonitor_RemoveNameListener(nameListener_);
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(uiTimer_);
    }
    std::lock_guard<std::mutex> lock(storeMutex_);
    if (store_ != NetworkMonitorInvalidStore) {
        NetworkMonitor_ApplyRetention(store_);
        NetworkMonitor_CloseStore(store_);
        store_ = NetworkMonitorInvalidStore;
    }
}

bool UltraNetMonitorWindow::Initialize(std::vector<std::string> nameNotes) {
    capabilities_ = NetworkMonitor_GetCapabilities();
    nameNotes_ = std::move(nameNotes);

    WindowConfig config;
    config.title  = std::string("UltraNetMonitor ") + ULTRANETMONITOR_VERSION;
    config.width  = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth  = 860;
    config.minHeight = 520;
    window_ = CreateWindow(config);
    if (!window_) return false;

    // Laid out, not positioned: a flex column of toolbar and tabs, sized by
    // LayoutForSize so the window's size is the page's size. The window
    // starts at the toolbar; the version is in the title bar and what the
    // backend and the sources can do is under Info….
    page_ = CreateContainer("nmPage", 0, 0, 0, 0);
    MakePlainColumn(page_);
    page_->SetPadding(kPagePadding);

    auto toolbar = BuildToolbar();
    page_->AddChild(toolbar);
    PinToolbar(toolbar);

    tabs_ = CreateTabbedContainer("nmTabs", 0, 0, 0, 0);
    tabs_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    tabs_->AddTab("Live", BuildLivePage());
    tabs_->AddTab("History", BuildHistoryPage());
    tabs_->AddTab("Names", BuildNamesPage());
    tabs_->AddTab("Events", BuildEventsPage());
    tabs_->SetActiveTab(kLiveTab);
    tabs_->onTabChange = [this](int, int newTab) {
        if (newTab == kNamesTab) RefreshNames();
        if (newTab == kEventsTab) RefreshEvents();
    };
    FillWith(page_, tabs_);

    // Every observation a source reports goes to the store while recording.
    // Runs on the source's thread; the store's mutex is the same one the
    // worker records under.
    nameListener_ = NetworkMonitor_AddNameListener([this](const DnsObservation& observation) {
        if (!recording_.load()) return;
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (store_ == NetworkMonitorInvalidStore) return;
        if (NetworkMonitor_RecordDnsObservation(store_, observation)) ++recordedObservations_;
    });
    eventListener_ = NetworkMonitor_AddEventListener([this](const NetworkConnectionEvent& event) {
        if (!recording_.load()) return;
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (store_ == NetworkMonitorInvalidStore) return;
        if (NetworkMonitor_RecordConnectionEvent(store_, event)) ++recordedEvents_;
    });

    window_->AddChild(page_);
    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };

    RefreshStatus();
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(kUiTimerMs, /*periodic=*/true,
                                   [this](TimerId) { ApplyPendingSnapshot(); });
    }
    StartWorker();
    return true;
}

void UltraNetMonitorWindow::Show() {
    if (window_) window_->Show();
}

void UltraNetMonitorWindow::LayoutForSize(float width, float height) {
    if (!page_) return;
    const float pageWidth  = std::max(400.0f, width);
    const float pageHeight = std::max(240.0f, height);
    // SetElementSize, not SetBounds: the layout engine sizes from the CSS
    // dimensions, so a bounds-only change is overwritten on the next pass.
    page_->SetElementAbsolutePosition(Point2Df(0.0f, 0.0f));
    page_->SetElementSize(Size2Df(pageWidth, pageHeight));
    page_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(
        Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== CONSTRUCTION =====

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildToolbar() {
    auto bar = MakeToolbar("nmToolbar");

    filterInput_ = CreateTextInput("nmFilter", 0, 0, 260, kControlHeight);
    filterInput_->SetPlaceholder("Filter connections (app, address, state, user)");
    filterInput_->onTextChanged = [this](const std::string& text) {
        if (connectionProxy_) connectionProxy_->SetFilterText(text);
    };
    bar->AddChild(filterInput_);

    allButton_ = MakeToolbarButton("nmAll", "All applications");
    allButton_->SetTooltip("Show every application's connections again");
    allButton_->SetOnClick([this]() { SelectProcess(std::nullopt); });
    bar->AddChild(allButton_);

    pauseButton_ = MakeToolbarButton("nmPause", "Pause");
    pauseButton_->SetTooltip("Stop reading the socket table until Resume");
    pauseButton_->SetOnClick([this]() { SetPaused(!paused_.load()); });
    bar->AddChild(pauseButton_);

    recordButton_ = MakeToolbarButton("nmRecord", "Record");
    recordButton_->SetTooltip("Write every snapshot, name and event into the activity store for the History tab");
    recordButton_->SetOnClick([this]() { ToggleRecording(); });
    bar->AddChild(recordButton_);

    statusLabel_ = MakeBarStatus("nmStatus");
    bar->AddChild(statusLabel_);

    infoButton_ = MakeToolbarButton("nmInfo", "Info…");
    infoButton_->SetTooltip("What this build can see: the backend, the name and event sources, and what each could not do");
    infoButton_->SetOnClick([this]() { ShowInfo(); });
    bar->AddChild(infoButton_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildLivePage() {
    auto live = MakeTabPage("nmLivePage");

    // Processes left, connections right. A split pane rather than a flex
    // row: the connection side keeps every pixel a wider window adds, and
    // the divider is real.
    split_ = CreateHorizontalSplitPane("nmSplit", 0, 0, 0, 0);
    split_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    SplitPaneStyle splitStyle;
    splitStyle.splitterThickness = 6;
    splitStyle.splitterHitMargin = 3;
    splitStyle.splitterColor     = Color(232, 232, 236);
    splitStyle.handle.shape      = SplitterHandleShape::RoundedSquare;
    splitStyle.handle.crossSize  = 9;
    splitStyle.handle.axisLength = 44;
    split_->SetSplitPaneStyle(splitStyle);
    auto processPane    = split_->AddPane(1.0);
    auto connectionPane = split_->AddPane(2.0);
    split_->SetPaneFixedSize(0, kProcessColumnWidth);
    split_->SetPaneMinSize(0, kProcessColumnMin);
    split_->SetPaneMinSize(1, 360);
    MakePlainColumn(processPane);
    MakePlainColumn(connectionPane);
    FillWith(processPane, BuildProcessList());
    FillWith(connectionPane, BuildConnectionList());
    FillWith(live, split_);
    return live;
}

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildHistoryPage() {
    auto history = MakeTabPage("nmHistoryPage");

    auto bar = MakeToolbar("nmHistoryBar");
    rangeDropdown_ = CreateDropdown("nmRange", 0, 0, 160, kControlHeight);
    for (const char* label : kRangeLabels) rangeDropdown_->AddItem(label);
    rangeDropdown_->SetSelectedIndex(1, /*runNotifications=*/false);
    rangeDropdown_->onSelectionChanged = [this](int, const DropdownItem&) { RefreshHistory(); };
    bar->AddChild(rangeDropdown_);
    rangeDropdown_->layoutItem.SetFlexShrink(0);

    historyFilter_ = CreateTextInput("nmHistoryFilter", 0, 0, 240, kControlHeight);
    historyFilter_->SetPlaceholder("Filter recorded flows");
    historyFilter_->onTextChanged = [this](const std::string& text) {
        if (flowProxy_) flowProxy_->SetFilterText(text);
    };
    bar->AddChild(historyFilter_);

    refreshButton_ = MakeToolbarButton("nmRefresh", "Refresh");
    refreshButton_->SetTooltip("Read the recorded flows over the chosen range again");
    refreshButton_->SetOnClick([this]() { RefreshHistory(); });
    bar->AddChild(refreshButton_);

    purgeButton_ = MakeToolbarButton("nmPurge", "Purge…");
    purgeButton_->SetTooltip("Delete everything recorded; asks a second time before it does");
    purgeButton_->SetOnClick([this]() { PurgeHistory(); });
    bar->AddChild(purgeButton_);

    historyStatus_ = MakeBarStatus("nmHistoryStatus",
                                   "Nothing loaded yet — press Refresh, or Record on the toolbar.");
    bar->AddChild(historyStatus_);

    history->AddChild(bar);
    PinToolbar(bar);
    FillWith(history, BuildFlowList());
    return history;
}

std::shared_ptr<UltraCanvasListView> UltraNetMonitorWindow::BuildProcessList() {
    processModel_ = std::make_shared<ProcessListModel>();
    processProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(processModel_);
    for (int column = ProcessListModel::Pid; column < ProcessListModel::ColumnCount; ++column) {
        if (column == ProcessListModel::Via) continue;   // text
        processProxy_->SetColumnSortKind(column, ListSortKind::Number);
    }
    processView_ = std::make_shared<UltraCanvasListView>("nmProcesses", -1, -1, 400, 300);
    processView_->SetModel(processProxy_);
    processView_->SetShowHeader(true);
    processView_->SetShowItemTooltips(true);
    WireHeaderSorting(processView_, processProxy_);

    // A click on a process narrows the connection list to it; clearing the
    // selection (or "All applications") widens it again.
    processView_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (adjustingSelection_) return;
        if (rows.empty()) { SelectProcess(std::nullopt); return; }
        const int sourceRow = processProxy_->MapToSource(rows.front());
        const ProcessTrafficSummary* summary = processModel_->At(sourceRow);
        if (!summary) { SelectProcess(std::nullopt); return; }
        SelectProcess(summary->attributed ? summary->process.pid : 0u);
    };
    processView_->onContextMenu = [this](int, const UCEvent& event) { ShowProcessMenu(event); };
    return processView_;
}

std::shared_ptr<UltraCanvasListView> UltraNetMonitorWindow::BuildConnectionList() {
    connectionModel_ = std::make_shared<ConnectionListModel>();
    connectionProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(connectionModel_);
    connectionProxy_->SetColumnSortKind(ConnectionListModel::Pid, ListSortKind::Number);
    connectionProxy_->SetColumnSortKind(ConnectionListModel::Sent, ListSortKind::Number);
    connectionProxy_->SetColumnSortKind(ConnectionListModel::Received, ListSortKind::Number);
    connectionView_ = std::make_shared<UltraCanvasListView>("nmConnections", -1, -1, 600, 300);
    connectionView_->SetModel(connectionProxy_);
    connectionView_->SetShowHeader(true);
    connectionView_->SetShowItemTooltips(true);
    WireHeaderSorting(connectionView_, connectionProxy_);
    return connectionView_;
}

std::shared_ptr<UltraCanvasListView> UltraNetMonitorWindow::BuildFlowList() {
    flowModel_ = std::make_shared<FlowListModel>();
    flowProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(flowModel_);
    for (int column : { static_cast<int>(FlowListModel::Pid), static_cast<int>(FlowListModel::FirstSeen),
                        static_cast<int>(FlowListModel::LastSeen), static_cast<int>(FlowListModel::Seen),
                        static_cast<int>(FlowListModel::Sent), static_cast<int>(FlowListModel::Received) }) {
        flowProxy_->SetColumnSortKind(column, ListSortKind::Number);
    }
    flowView_ = std::make_shared<UltraCanvasListView>("nmFlows", -1, -1, 900, 300);
    flowView_->SetModel(flowProxy_);
    flowView_->SetShowHeader(true);
    flowView_->SetShowItemTooltips(true);
    WireHeaderSorting(flowView_, flowProxy_);
    return flowView_;
}

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildNamesPage() {
    auto names = MakeTabPage("nmNamesPage");

    auto bar = MakeToolbar("nmNamesBar");
    namesFilter_ = CreateTextInput("nmNamesFilter", 0, 0, 260, kControlHeight);
    namesFilter_->SetPlaceholder("Filter names (name, address, source)");
    namesFilter_->onTextChanged = [this](const std::string& text) {
        if (nameProxy_) nameProxy_->SetFilterText(text);
    };
    bar->AddChild(namesFilter_);

    namesRefreshButton_ = MakeToolbarButton("nmNamesRefresh", "Refresh");
    namesRefreshButton_->SetTooltip("List the name table again");
    namesRefreshButton_->SetOnClick([this]() { RefreshNames(); });
    bar->AddChild(namesRefreshButton_);

    namesStatus_ = MakeBarStatus("nmNamesStatus");
    bar->AddChild(namesStatus_);

    names->AddChild(bar);
    PinToolbar(bar);
    FillWith(names, BuildNameList());
    return names;
}

std::shared_ptr<UltraCanvasListView> UltraNetMonitorWindow::BuildNameList() {
    nameModel_ = std::make_shared<NameListModel>();
    nameProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(nameModel_);
    nameProxy_->SetColumnSortKind(NameListModel::Observed, ListSortKind::Number);
    nameProxy_->SetColumnSortKind(NameListModel::Expires, ListSortKind::Number);
    nameView_ = std::make_shared<UltraCanvasListView>("nmNames", -1, -1, 900, 300);
    nameView_->SetModel(nameProxy_);
    nameView_->SetShowHeader(true);
    nameView_->SetShowItemTooltips(true);
    WireHeaderSorting(nameView_, nameProxy_);
    return nameView_;
}

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildEventsPage() {
    auto events = MakeTabPage("nmEventsPage");

    auto bar = MakeToolbar("nmEventsBar");
    eventsFilter_ = CreateTextInput("nmEventsFilter", 0, 0, 260, kControlHeight);
    eventsFilter_->SetPlaceholder("Filter events (app, address, host, kind)");
    eventsFilter_->onTextChanged = [this](const std::string& text) {
        if (eventProxy_) eventProxy_->SetFilterText(text);
    };
    bar->AddChild(eventsFilter_);

    eventsModeButton_ = MakeToolbarButton("nmEventsMode", "Show recorded");
    eventsModeButton_->SetTooltip("Switch between the events as they happen and what the store recorded over the History tab's range");
    eventsModeButton_->SetOnClick([this]() { ToggleEventsRecorded(); });
    bar->AddChild(eventsModeButton_);

    eventsRefreshButton_ = MakeToolbarButton("nmEventsRefresh", "Refresh");
    eventsRefreshButton_->SetTooltip("Read the events again");
    eventsRefreshButton_->SetOnClick([this]() { RefreshEvents(); });
    bar->AddChild(eventsRefreshButton_);

    eventsClearButton_ = MakeToolbarButton("nmEventsClear", "Clear");
    eventsClearButton_->SetTooltip("Forget the recent events; the store keeps what was recorded");
    eventsClearButton_->SetOnClick([this]() {
        NetworkMonitor_ClearRecentEvents();
        RefreshEvents();
    });
    bar->AddChild(eventsClearButton_);

    eventsStatus_ = MakeBarStatus("nmEventsStatus");
    bar->AddChild(eventsStatus_);

    events->AddChild(bar);
    PinToolbar(bar);
    FillWith(events, BuildEventList());
    return events;
}

std::shared_ptr<UltraCanvasListView> UltraNetMonitorWindow::BuildEventList() {
    eventModel_ = std::make_shared<EventListModel>();
    eventProxy_ = std::make_shared<UltraCanvasListSortFilterProxy>(eventModel_);
    for (int column : { static_cast<int>(EventListModel::Time), static_cast<int>(EventListModel::Pid),
                        static_cast<int>(EventListModel::Sent), static_cast<int>(EventListModel::Received) }) {
        eventProxy_->SetColumnSortKind(column, ListSortKind::Number);
    }
    eventView_ = std::make_shared<UltraCanvasListView>("nmEvents", -1, -1, 900, 300);
    eventView_->SetModel(eventProxy_);
    eventView_->SetShowHeader(true);
    eventView_->SetShowItemTooltips(true);
    WireHeaderSorting(eventView_, eventProxy_);
    return eventView_;
}

// ===== DATA FLOW =====

void UltraNetMonitorWindow::StartWorker() {
    stopWorker_ = false;
    worker_ = std::thread([this]() {
        using clock = std::chrono::steady_clock;
        while (!stopWorker_.load()) {
            const auto started = clock::now();
            if (!paused_.load()) {
                std::vector<NetworkConnection> connections;
                NetworkMonitorResult result = NetworkMonitor_ListConnections(connections);
                if (result && recording_.load()) {
                    std::lock_guard<std::mutex> lock(storeMutex_);
                    if (store_ != NetworkMonitorInvalidStore) {
                        const NetworkMonitorResult recorded =
                            NetworkMonitor_RecordSnapshot(store_, connections);
                        if (recorded) {
                            ++recordedSnapshots_;
                            recordError_.clear();
                        } else {
                            recordError_ = recorded.message;
                        }
                    }
                }
                std::lock_guard<std::mutex> lock(pendingMutex_);
                pendingConnections_ = std::move(connections);
                pendingResult_ = std::move(result);
                pendingDirty_ = true;
            }
            // Sleep in short steps so Stop() returns promptly.
            while (!stopWorker_.load() &&
                   clock::now() - started < std::chrono::milliseconds(kSnapshotIntervalMs)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    });
}

void UltraNetMonitorWindow::StopWorker() {
    stopWorker_ = true;
    if (worker_.joinable()) worker_.join();
}

void UltraNetMonitorWindow::ApplyPendingSnapshot() {
    std::vector<NetworkConnection> connections;
    NetworkMonitorResult result;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (!pendingDirty_) return;
        connections.swap(pendingConnections_);
        result = pendingResult_;
        pendingDirty_ = false;
    }
    lastResult_ = result;
    if (result) {
        auto summaries = NetworkMonitor_SummarizeByProcess(connections);
        lastConnectionCount_ = connections.size();
        lastProcessCount_ = summaries.size();
        processModel_->Replace(std::move(summaries));
        connectionModel_->Replace(std::move(connections));
        ReapplyProcessSelection();
        if (layoutDebug_) LogLayout();
        // The Names tab follows the sources while it is in front, every
        // other snapshot - a name table is cheap to list, not free.
        ++snapshotsApplied_;
        if (tabs_ && tabs_->GetActiveTab() == kNamesTab && (snapshotsApplied_ % 2) == 0) RefreshNames();
        // The live event list follows the ring every snapshot; the
        // recorded view only on Refresh, since a query is not free.
        if (tabs_ && tabs_->GetActiveTab() == kEventsTab && !eventsRecorded_) RefreshEvents();
    }
    // Attribution notes ("N processes could not be inspected") move with
    // each snapshot, so the capabilities are re-read here, not once.
    capabilities_ = NetworkMonitor_GetCapabilities();
    RefreshStatus();
}

// --debug-layout: one line per list, the numbers the scrollbar is computed
// from, so a list without one can be read off a log from any machine.
void UltraNetMonitorWindow::LogLayout() {
    auto describe = [](const char* name, const std::shared_ptr<UltraCanvasListView>& view) {
        if (!view) return;
        const auto b = view->GetBounds();
        const auto m = view->GetScrollMetrics();
        debugOutput << "layout " << name << ": bounds " << b.x << "," << b.y << " " << b.width << "x" << b.height
                    << " · rows " << m.rows << " × " << m.rowHeight << " = " << m.contentHeight
                    << " · viewport " << m.viewportHeight << " · range " << m.maxScroll
                    << " · offset " << m.scrollOffset << " · scrollbar " << (m.scrollbarVisible ? "shown" : "hidden")
                    << " at " << m.scrollbarBounds.x << "," << m.scrollbarBounds.y << " "
                    << m.scrollbarBounds.width << "x" << m.scrollbarBounds.height << std::endl;
    };
    if (split_) {
        const auto b = split_->GetBounds();
        debugOutput << "layout split: bounds " << b.x << "," << b.y << " " << b.width << "x" << b.height
                    << " · window " << (window_ ? window_->GetWidth() : 0) << "x"
                    << (window_ ? window_->GetHeight() : 0) << std::endl;
    }
    describe("processes", processView_);
    describe("connections", connectionView_);
}

void UltraNetMonitorWindow::RefreshStatus() {
    std::string status;
    std::string detail;   // the line's tooltip: what the line has no room for
    if (!NetworkMonitor_IsAvailable()) {
        status = "No NetworkMonitor backend for this platform in this build.";
    } else if (!lastResult_.success && !lastResult_.message.empty()) {
        status = lastResult_.message;
    } else if (lastResult_.success) {
        status = std::to_string(lastConnectionCount_) + " connections · " +
                 std::to_string(lastProcessCount_) + " applications";
        if (selectedPid_) {
            status += selectedPid_ == 0u ? " · showing unattributed"
                                         : " · showing PID " + std::to_string(*selectedPid_);
        }
        if (paused_.load()) status += " · paused";
        // The first note is the one that changes what the table means. Its
        // headline is on the line; the explanation ("named from the process
        // list, without path or user (elevate for those)") is in the tooltip
        // and under Info….
        for (const auto& note : capabilities_.notes) {
            if (note.find("could not be") != std::string::npos) {
                status += " · " + NoteHeadline(note);
                detail = note;
                break;
            }
        }
    } else {
        status = "Reading the socket table…";
    }
    if (recording_.load()) {
        std::string error;
        {
            std::lock_guard<std::mutex> lock(storeMutex_);
            error = recordError_;
        }
        status += error.empty()
            ? " · recording (" + std::to_string(recordedSnapshots_.load()) + " snapshots, " +
              std::to_string(recordedObservations_.load()) + " DNS observations, " +
              std::to_string(recordedEvents_.load()) + " events)"
            : " · recording failed: " + error;
    }
    if (statusLabel_) {
        statusLabel_->SetText(status);
        statusLabel_->SetTooltip(detail);
    }
}

// ===== INFO =====

void UltraNetMonitorWindow::ShowInfo() {
    if (!window_) return;
    DialogConfig config;
    config.title = std::string("UltraNetMonitor ") + ULTRANETMONITOR_VERSION;
    config.message = BuildInfoMarkdown();
    config.dialogType = DialogType::Information;
    config.buttons = DialogButtons::OK;
    config.showIcon = false;   // the text is the content; a badge would only narrow it
    config.width = kInfoDialogWidth;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (dialog) UltraCanvasDialogManager::ShowDialog(dialog, nullptr, window_.get());
}

std::string UltraNetMonitorWindow::BuildInfoMarkdown() {
    std::string md = "**Backend:** " + EscapeMarkdown(capabilities_.backendName.empty() ? std::string("none")
                                                                                          : capabilities_.backendName) + "\n\n";
    if (!capabilities_.socketTable) {
        md += "- no socket table: nothing can be listed on this platform in this build\n";
    } else {
        md += capabilities_.allUsers ? "- every user's processes\n"
                                     : "- this user's processes only (elevate for all)\n";
        md += capabilities_.processAttribution ? "- connections are attributed to their process\n"
                                               : "- connections cannot be attributed to a process\n";
        md += capabilities_.perConnectionBytes ? "- byte counters per connection\n"
                                               : "- no byte counters on this backend: Sent and Received show a dash\n";
    }
    for (const auto& note : capabilities_.notes) md += "- " + EscapeMarkdown(note) + "\n";

    md += "\n**Name sources**\n\n";
    std::vector<NameSourceStatus> nameSources;
    NetworkMonitor_ListNameSources(nameSources);
    if (nameSources.empty()) md += "- none running\n";
    for (const auto& source : nameSources) {
        md += "- " + EscapeMarkdown(SourceInFull(source.name, source.running, source.lastError,
                                                 source.observations, "observations")) + "\n";
    }
    for (const auto& note : nameNotes_) if (!IsEventNote(note)) md += "- " + EscapeMarkdown(note) + "\n";

    md += "\n**Event sources**\n\n";
    std::vector<EventSourceStatus> eventSources;
    NetworkMonitor_ListEventSources(eventSources);
    if (eventSources.empty()) md += "- none running\n";
    for (const auto& source : eventSources) {
        md += "- " + EscapeMarkdown(SourceInFull(source.name, source.running, source.lastError,
                                                 source.events, "events")) + "\n";
    }
    for (const auto& note : nameNotes_) if (IsEventNote(note)) md += "- " + EscapeMarkdown(note) + "\n";

    md += "\n**Activity store**\n\n";
    {
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (store_ != NetworkMonitorInvalidStore) {
            md += "- " + EscapeMarkdown(storePath_) + "\n";
        } else if (!storeError_.empty()) {
            md += "- not open: " + EscapeMarkdown(storeError_) + "\n";
        } else {
            const std::string path = storePath_.empty() ? DefaultStorePath() : storePath_;
            md += path.empty() ? "- no per-user data directory on this system\n"
                               : "- " + EscapeMarkdown(path) + " (opened on first use)\n";
        }
        if (recording_.load()) {
            md += "- recording: " + std::to_string(recordedSnapshots_.load()) + " snapshots, " +
                  std::to_string(recordedObservations_.load()) + " DNS observations, " +
                  std::to_string(recordedEvents_.load()) + " events\n";
            if (!recordError_.empty()) md += "- recording failed: " + EscapeMarkdown(recordError_) + "\n";
        } else {
            md += "- not recording\n";
        }
    }
    return md;
}

// ===== NAMES =====

void UltraNetMonitorWindow::RefreshNames() {
    std::vector<NameRecord> records;
    NetworkMonitor_ListNames(records);
    const std::size_t count = records.size();
    std::size_t observed = 0;
    for (const auto& record : records) if (NetworkMonitor_NameIsObserved(record.source)) ++observed;
    nameModel_->Replace(std::move(records));

    // The line counts; the tooltip and Info… carry the errors and the notes.
    std::string text = std::to_string(count) + " addresses named, " + std::to_string(observed) +
                       " from observed queries";
    std::string detail;
    bool trouble = false;
    std::vector<NameSourceStatus> sources;
    NetworkMonitor_ListNameSources(sources);
    for (const auto& source : sources) {
        text += " · " + SourceOnLine(source.name, source.running, source.lastError,
                                     source.observations, "observations", "failed");
        detail += (detail.empty() ? "" : "\n") +
                  SourceInFull(source.name, source.running, source.lastError, source.observations, "observations");
        if (!source.running || !source.lastError.empty()) trouble = true;
    }
    for (const auto& note : nameNotes_) {
        if (IsEventNote(note)) continue;
        detail += (detail.empty() ? "" : "\n") + note;
        trouble = true;
    }
    if (trouble) text += " · details under Info…";
    if (namesStatus_) {
        namesStatus_->SetText(text);
        namesStatus_->SetTooltip(detail);
    }
}

// ===== EVENTS =====

void UltraNetMonitorWindow::ToggleEventsRecorded() {
    eventsRecorded_ = !eventsRecorded_;
    if (eventsModeButton_) eventsModeButton_->SetText(eventsRecorded_ ? "Show live" : "Show recorded");
    if (eventsClearButton_) eventsClearButton_->SetVisible(!eventsRecorded_);
    RefreshEvents();
}

void UltraNetMonitorWindow::RefreshEvents() {
    std::vector<NetworkConnectionEvent> events;
    std::string text;
    if (eventsRecorded_) {
        std::string error;
        std::vector<RecordedConnectionEvent> recorded;
        {
            std::lock_guard<std::mutex> lock(storeMutex_);
            if (!EnsureStoreOpen()) {
                error = storeError_;
            } else {
                ActivityQuery query;
                query.since = NetworkMonitor_Now() - HistoryRangeSeconds();
                query.limit = kHistoryLimit;
                const NetworkMonitorResult read = NetworkMonitor_QueryConnectionEvents(store_, query, recorded);
                if (!read) error = read.message;
            }
        }
        if (!error.empty()) {
            if (eventsStatus_) eventsStatus_->SetText(error);
            return;
        }
        events.reserve(recorded.size());
        for (auto& r : recorded) events.push_back(std::move(r.event));
        text = std::to_string(events.size()) + " recorded events in the History tab's range";
    } else {
        NetworkMonitor_RecentEvents(events, kEventsShown);
        text = std::to_string(events.size()) + " recent events (newest " + std::to_string(kEventsShown) + " kept)";
    }
    eventModel_->Replace(std::move(events));
    // The line counts; the tooltip and Info… carry the errors and the notes.
    std::string detail;
    bool trouble = false;
    std::vector<EventSourceStatus> sources;
    NetworkMonitor_ListEventSources(sources);
    for (const auto& source : sources) {
        text += " · " + SourceOnLine(source.name, source.running, source.lastError, source.events, "events", "idle");
        detail += (detail.empty() ? "" : "\n") +
                  SourceInFull(source.name, source.running, source.lastError, source.events, "events");
        if (!source.running || !source.lastError.empty()) trouble = true;
    }
    for (const auto& note : nameNotes_) {
        if (!IsEventNote(note)) continue;
        detail += (detail.empty() ? "" : "\n") + note;
        trouble = true;
    }
    if (trouble) text += " · details under Info…";
    if (eventsStatus_) {
        eventsStatus_->SetText(text);
        eventsStatus_->SetTooltip(detail);
    }
}

void UltraNetMonitorWindow::SetPaused(bool paused) {
    paused_ = paused;
    if (pauseButton_) pauseButton_->SetText(paused ? "Resume" : "Pause");
    RefreshStatus();
}

// ===== FILTERING =====

void UltraNetMonitorWindow::SelectProcess(std::optional<uint32_t> pid) {
    selectedPid_ = pid;
    RebuildConnectionFilter();
    if (!pid && processView_ && processView_->GetSelection()) {
        adjustingSelection_ = true;
        processView_->GetSelection()->Clear();
        adjustingSelection_ = false;
    }
    RefreshStatus();
}

void UltraNetMonitorWindow::ReapplyProcessSelection() {
    // Every snapshot replaces and re-sorts the process rows, so the row that
    // was selected is now some other process. Find the same PID again.
    if (!selectedPid_ || !processView_ || !processView_->GetSelection()) return;
    adjustingSelection_ = true;
    IListSelection* selection = processView_->GetSelection();
    selection->Clear();
    for (int row = 0; row < processProxy_->GetRowCount(); ++row) {
        const int sourceRow = processProxy_->MapToSource(row);
        if (PidOfSourceRow(*processModel_, sourceRow, ProcessListModel::Pid) ==
            static_cast<int>(*selectedPid_)) {
            selection->Select(row);
            break;
        }
    }
    adjustingSelection_ = false;
}

void UltraNetMonitorWindow::RebuildConnectionFilter() {
    if (!connectionProxy_) return;
    if (!selectedPid_) {
        connectionProxy_->SetFilterPredicate(nullptr);
        return;
    }
    const int wanted = static_cast<int>(*selectedPid_);
    connectionProxy_->SetFilterPredicate([wanted](const IListModel& model, int sourceRow) {
        return PidOfSourceRow(model, sourceRow, ConnectionListModel::Pid) == wanted;
    });
}

// ===== EXPORT =====

void UltraNetMonitorWindow::ShowProcessMenu(const UCEvent& event) {
    if (!window_) return;
    processMenu_ = std::make_shared<UltraCanvasMenu>("nmProcessMenu", 0, 0, 220, 0);
    processMenu_->SetMenuType(MenuType::PopupMenu);
    processMenu_->AddItem(MenuItemData::Submenu("Export", {
        MenuItemData::Action("App list…", [this]() { ExportAppList(false); }),
        MenuItemData::Action("App list details…", [this]() { ExportAppList(true); }),
    }));
    PopupElementSettings settings;
    processMenu_->OpenMenu(event.pointerWindow, *window_, settings);
}

void UltraNetMonitorWindow::ExportAppList(bool details) {
    // The list as shown: the proxy's order, every row (the filter box
    // narrows connections, never processes).
    std::vector<ProcessTrafficSummary> summaries;
    for (int row = 0; row < processProxy_->GetRowCount(); ++row) {
        if (const ProcessTrafficSummary* s = processModel_->At(processProxy_->MapToSource(row))) {
            summaries.push_back(*s);
        }
    }
    // The details: every application's connections, grouped in that order.
    std::vector<NetworkConnection> connections;
    if (details) {
        const auto& all = connectionModel_->Rows();
        for (const auto& s : summaries) {
            for (const auto& c : all) {
                const bool belongs = s.attributed ? (c.process && c.process->pid == s.process.pid) : !c.process;
                if (belongs) connections.push_back(c);
            }
        }
    }
    if (summaries.empty()) {
        UltraCanvasDialogManager::ShowInformation("There is nothing to export yet - no snapshot has been taken.",
                                                  "Export", nullptr, window_.get());
        return;
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);

    FileDialogOptions options;
    options.SetTitle(details ? "Export the app list with its connections" : "Export the app list")
           .SetDefaultFileName(std::string(details ? "UltraNetMonitor-app-details-" : "UltraNetMonitor-apps-") +
                               stamp + ".csv")
           .AddFilter("CSV files", "csv")
           .AddFilter("All files", "*")
           .SetParentWindow(window_.get());
    UltraCanvasWindowBase* parent = window_.get();
    UltraCanvasFileLoader::SaveFileDialog(options,
        [summaries, connections, details, parent](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;   // cancelled
            int64_t rows = 0;
            const NetworkMonitorResult written = details
                ? NetworkMonitor_ExportConnectionsCsv(connections, path, &rows)
                : NetworkMonitor_ExportSummaryCsv(summaries, path, &rows);
            if (!written) {
                UltraCanvasDialogManager::ShowError(written.message, "Export failed", nullptr, parent);
                return;
            }
            UltraCanvasDialogManager::ShowInformation(
                "Wrote " + std::to_string(rows) + (details ? " connections" : " applications") + " to " + path,
                "Exported", nullptr, parent);
        });
}

// ===== THE STORE =====

bool UltraNetMonitorWindow::EnsureStoreOpen() {
    if (store_ != NetworkMonitorInvalidStore) return true;
    if (!NetworkMonitor_StoreAvailable()) {
        storeError_ = "This build has no UltraDatabase, so nothing can be recorded.";
        return false;
    }
    if (storePath_.empty()) storePath_ = DefaultStorePath();
    if (storePath_.empty()) {
        storeError_ = "No writable per-user data directory on this system.";
        return false;
    }
    NetworkMonitorStoreOptions options;
    options.path = storePath_;
    const NetworkMonitorResult opened = NetworkMonitor_OpenStore(options, store_);
    if (!opened) {
        store_ = NetworkMonitorInvalidStore;
        storeError_ = opened.message;
        return false;
    }
    storeError_.clear();
    // Whatever an earlier run left behind ages out before anything new lands.
    NetworkMonitor_ApplyRetention(store_);
    return true;
}

void UltraNetMonitorWindow::ToggleRecording() {
    if (recording_.load()) {
        recording_ = false;
        if (recordButton_) recordButton_->SetText("Record");
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (store_ != NetworkMonitorInvalidStore) NetworkMonitor_ApplyRetention(store_);
    } else {
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (!EnsureStoreOpen()) {
            if (historyStatus_) historyStatus_->SetText(storeError_);
            if (statusLabel_) statusLabel_->SetText("Cannot record: " + storeError_);
            return;
        }
        recordedSnapshots_ = 0;
        recordedObservations_ = 0;
        recordedEvents_ = 0;
        recordError_.clear();
        recording_ = true;
        if (recordButton_) recordButton_->SetText("Stop recording");
    }
    RefreshStatus();
}

int64_t UltraNetMonitorWindow::HistoryRangeSeconds() const {
    const int index = rangeDropdown_ ? rangeDropdown_->GetSelectedIndex() : 1;
    const int count = static_cast<int>(sizeof kRanges / sizeof kRanges[0]);
    return kRanges[std::clamp(index, 0, count - 1)];
}

void UltraNetMonitorWindow::RefreshHistory() {
    purgeArmed_ = false;
    if (purgeButton_) purgeButton_->SetText("Purge…");
    std::vector<RecordedFlow> flows;
    NetworkMonitorStoreStats stats;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (!EnsureStoreOpen()) {
            error = storeError_;
        } else {
            ActivityQuery query;
            query.since = NetworkMonitor_Now() - HistoryRangeSeconds();
            query.limit = kHistoryLimit;
            const NetworkMonitorResult read = NetworkMonitor_QueryFlows(store_, query, flows);
            if (!read) error = read.message;
            NetworkMonitor_StoreStats(store_, stats);
        }
    }
    if (!error.empty()) {
        if (historyStatus_) historyStatus_->SetText(error);
        return;
    }
    const std::size_t shown = flows.size();
    flowModel_->Replace(std::move(flows));
    std::string text = std::to_string(shown) + " flows in range";
    if (shown >= static_cast<std::size_t>(kHistoryLimit)) text += " (newest " + std::to_string(kHistoryLimit) + ")";
    text += " · " + std::to_string(stats.flows) + " flows and " +
            std::to_string(stats.dailyTotals) + " daily totals stored · " + storePath_;
    if (historyStatus_) historyStatus_->SetText(text);
}

void UltraNetMonitorWindow::PurgeHistory() {
    if (!purgeArmed_) {
        purgeArmed_ = true;
        if (purgeButton_) purgeButton_->SetText("Confirm purge");
        if (historyStatus_) historyStatus_->SetText(
            "Purge deletes everything recorded, irreversibly. Press \"Confirm purge\" to go ahead, "
            "or Refresh to keep it.");
        return;
    }
    purgeArmed_ = false;
    if (purgeButton_) purgeButton_->SetText("Purge…");
    std::string error;
    {
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (!EnsureStoreOpen()) {
            error = storeError_;
        } else if (const NetworkMonitorResult purged = NetworkMonitor_Purge(store_); !purged) {
            error = purged.message;
        }
    }
    if (!error.empty()) {
        if (historyStatus_) historyStatus_->SetText(error);
        return;
    }
    recordedSnapshots_ = 0;
    flowModel_->Replace({});
    if (historyStatus_) historyStatus_->SetText("Purged. Nothing is recorded now.");
}

} // namespace UltraNetMonitor
