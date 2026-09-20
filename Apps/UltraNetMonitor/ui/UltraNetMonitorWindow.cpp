// Apps/UltraNetMonitor/ui/UltraNetMonitorWindow.cpp
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorWindow.h"

#include "UltraNetMonitorPaths.h"

#include "UltraCanvasApplication.h"

#include <algorithm>
#include <chrono>
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
constexpr float kSideMargin   = 8.0f;
constexpr float kPageTop      = 44.0f;
constexpr float kPagePadding  = 10.0f;
constexpr float kSectionGap   = 8.0f;
constexpr int   kProcessColumnWidth = 470;
constexpr int   kProcessColumnMin   = 300;

constexpr unsigned kSnapshotIntervalMs = 1000;
constexpr unsigned kUiTimerMs          = 200;

// Tab order, as added below.
constexpr int kLiveTab    = 0;
constexpr int kHistoryTab = 1;

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

bool UltraNetMonitorWindow::Initialize() {
    capabilities_ = NetworkMonitor_GetCapabilities();

    WindowConfig config;
    config.title  = std::string("UltraNetMonitor ") + ULTRANETMONITOR_VERSION;
    config.width  = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth  = 860;
    config.minHeight = 520;
    window_ = CreateWindow(config);
    if (!window_) return false;

    auto title = CreateLabel("nmTitle", 16, 10, 260, 26, "UltraNetMonitor");
    title->SetFontSize(18);
    window_->AddChild(title);
    subtitleLabel_ = CreateLabel("nmSubtitle", 200, 14, 900, 22, "");
    window_->AddChild(subtitleLabel_);

    // Laid out, not positioned: a flex column of toolbar and tabs, sized by
    // LayoutForSize so the window's size is the page's size.
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
    tabs_->SetActiveTab(kLiveTab);
    FillWith(page_, tabs_);

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
    const float pageWidth  = std::max(400.0f, width  - 2 * kSideMargin);
    const float pageHeight = std::max(240.0f, height - kPageTop - kSideMargin);
    // SetElementSize, not SetBounds: the layout engine sizes from the CSS
    // dimensions, so a bounds-only change is overwritten on the next pass.
    page_->SetElementAbsolutePosition(Point2Df(kSideMargin, kPageTop));
    page_->SetElementSize(Size2Df(pageWidth, pageHeight));
    page_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(
        Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== CONSTRUCTION =====

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildToolbar() {
    auto bar = MakeToolbar("nmToolbar");

    filterInput_ = CreateTextInput("nmFilter", 0, 0, 260, 28);
    filterInput_->SetPlaceholder("Filter connections (app, address, state, user)");
    filterInput_->onTextChanged = [this](const std::string& text) {
        if (connectionProxy_) connectionProxy_->SetFilterText(text);
    };
    bar->AddChild(filterInput_);

    allButton_ = CreateButton("nmAll", 0, 0, 130, 28, "All applications");
    allButton_->SetOnClick([this]() { SelectProcess(std::nullopt); });
    bar->AddChild(allButton_);

    pauseButton_ = CreateButton("nmPause", 0, 0, 90, 28, "Pause");
    pauseButton_->SetOnClick([this]() { SetPaused(!paused_.load()); });
    bar->AddChild(pauseButton_);

    recordButton_ = CreateButton("nmRecord", 0, 0, 130, 28, "Record");
    recordButton_->SetOnClick([this]() { ToggleRecording(); });
    bar->AddChild(recordButton_);

    statusLabel_ = CreateLabel("nmStatus", 0, 0, 0, 24, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    statusLabel_->layoutItem.SetFlexShrink(1);
    bar->AddChild(statusLabel_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraNetMonitorWindow::BuildLivePage() {
    // No explicit size: the tabbed container measures its active page with
    // an exact width and height, so the page must not carry a size of its
    // own that competes with that.
    auto live = CreateContainer("nmLivePage", 0, 0, 0, 0);
    MakePlainColumn(live);
    live->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());

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
    auto history = CreateContainer("nmHistoryPage", 0, 0, 0, 0);
    MakePlainColumn(history);
    history->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());

    auto bar = MakeToolbar("nmHistoryBar");
    rangeDropdown_ = CreateDropdown("nmRange", 0, 0, 160, 28);
    for (const char* label : kRangeLabels) rangeDropdown_->AddItem(label);
    rangeDropdown_->SetSelectedIndex(1, /*runNotifications=*/false);
    rangeDropdown_->onSelectionChanged = [this](int, const DropdownItem&) { RefreshHistory(); };
    bar->AddChild(rangeDropdown_);
    rangeDropdown_->layoutItem.SetFlexShrink(0);

    historyFilter_ = CreateTextInput("nmHistoryFilter", 0, 0, 240, 28);
    historyFilter_->SetPlaceholder("Filter recorded flows");
    historyFilter_->onTextChanged = [this](const std::string& text) {
        if (flowProxy_) flowProxy_->SetFilterText(text);
    };
    bar->AddChild(historyFilter_);

    refreshButton_ = CreateButton("nmRefresh", 0, 0, 90, 28, "Refresh");
    refreshButton_->SetOnClick([this]() { RefreshHistory(); });
    bar->AddChild(refreshButton_);

    purgeButton_ = CreateButton("nmPurge", 0, 0, 120, 28, "Purge…");
    purgeButton_->SetOnClick([this]() { PurgeHistory(); });
    bar->AddChild(purgeButton_);

    historyStatus_ = CreateLabel("nmHistoryStatus", 0, 0, 0, 24,
                                 "Nothing loaded yet — press Refresh, or Record on the toolbar.");
    historyStatus_->layoutItem.SetFlexGrow(1);
    historyStatus_->layoutItem.SetFlexShrink(1);
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
    }
    // Attribution notes ("N processes could not be inspected") move with
    // each snapshot, so the capabilities are re-read here, not once.
    capabilities_ = NetworkMonitor_GetCapabilities();
    RefreshStatus();
}

void UltraNetMonitorWindow::RefreshStatus() {
    std::string subtitle = std::string(ULTRANETMONITOR_VERSION) + " · backend: " +
                           capabilities_.backendName;
    if (capabilities_.socketTable) {
        subtitle += capabilities_.allUsers ? " · every process"
                                           : " · this user's processes only (elevate for all)";
        subtitle += capabilities_.perConnectionBytes ? " · byte counters"
                                                     : " · no byte counters on this backend";
    }
    if (subtitleLabel_) subtitleLabel_->SetText(subtitle);

    std::string status;
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
        // The first note is the one that changes what the table means.
        for (const auto& note : capabilities_.notes) {
            if (note.find("could not be") != std::string::npos) {
                status += " · " + note;
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
            ? " · recording (" + std::to_string(recordedSnapshots_.load()) + " snapshots)"
            : " · recording failed: " + error;
    }
    if (statusLabel_) statusLabel_->SetText(status);
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
