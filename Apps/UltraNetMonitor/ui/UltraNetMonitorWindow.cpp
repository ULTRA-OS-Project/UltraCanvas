// Apps/UltraNetMonitor/ui/UltraNetMonitorWindow.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetMonitorWindow.h"

#include "UltraCanvasApplication.h"

#include <algorithm>
#include <chrono>
#include <variant>

// The app's own version, from the first line of Docs/UltraNetMonitor/CHANGELOG.md
// through cmake/UltraCanvasVersion.cmake. Independent of the framework's.
#ifndef ULTRANETMONITOR_VERSION
#define ULTRANETMONITOR_VERSION "0.0-dev"
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

// A split pane's panes are plain containers; a list fills one edge to edge.
// Basis zero and grow one rather than a percentage, so the pane's size is
// what decides and the list can never hold a size of its own against it.
void FillPaneWith(const std::shared_ptr<UltraCanvasContainer>& pane,
                  const std::shared_ptr<UltraCanvasUIElement>& element) {
    pane->layout.SetFlexColumn()
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plainPane;
    plainPane.autoShowScrollbars = false;
    pane->SetContainerStyle(plainPane);
    pane->AddChild(element);
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetFlexGrow(1);
    element->layoutItem.SetFlexShrink(1);
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
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

    // Laid out, not positioned: a flex column of toolbar and split pane,
    // sized by LayoutForSize so the window's size is the page's size.
    page_ = CreateContainer("nmPage", 0, 0, 0, 0);
    page_->layout.SetFlexColumn()
                 .SetFlexGap(kSectionGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page_->SetPadding(kPagePadding);
    ContainerStyle plainPage;
    plainPage.autoShowScrollbars = false;
    page_->SetContainerStyle(plainPage);

    auto toolbar = BuildToolbar();
    page_->AddChild(toolbar);
    toolbar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    toolbar->layoutItem.SetFlexGrow(0);
    toolbar->layoutItem.SetFlexShrink(0);

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
    FillPaneWith(processPane, BuildProcessList());
    FillPaneWith(connectionPane, BuildConnectionList());
    page_->AddChild(split_);
    split_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    split_->layoutItem.SetFlexGrow(1);
    split_->layoutItem.SetFlexShrink(1);
    split_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

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
    auto bar = CreateContainer("nmToolbar", 0, 0, 0, 34);
    bar->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(34));
    ContainerStyle plainBar;
    plainBar.autoShowScrollbars = false;
    bar->SetContainerStyle(plainBar);

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

    statusLabel_ = CreateLabel("nmStatus", 0, 0, 0, 24, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    statusLabel_->layoutItem.SetFlexShrink(1);
    bar->AddChild(statusLabel_);
    return bar;
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
            if (note.find("could not be inspected") != std::string::npos) {
                status += " · " + note;
                break;
            }
        }
    } else {
        status = "Reading the socket table…";
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

} // namespace UltraNetMonitor
