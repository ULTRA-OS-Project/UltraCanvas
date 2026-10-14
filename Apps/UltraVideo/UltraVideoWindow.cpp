// Apps/UltraVideo/UltraVideoWindow.cpp
// The editor window (UltraVideoWindow.h).
// Version: 0.5.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#include "UltraVideoWindow.h"
#include "UltraVideoMapping.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasVideoTimelineModel.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <filesystem>
#include <system_error>

// ULTRAVIDEO_VERSION comes from the build alone: CMake reads the first line
// of Docs/UltraVideo/CHANGELOG.md and passes it as a compile definition.
#ifndef ULTRAVIDEO_VERSION
#error "ULTRAVIDEO_VERSION is not defined: build through CMake, which reads it from Docs/UltraVideo/CHANGELOG.md"
#endif

using namespace UltraCanvas;
using namespace VideoFX;
namespace fs = std::filesystem;

namespace UltraVideo {

namespace {

constexpr unsigned kTickMs = 20;
constexpr int kMessageTicks = 200;       // 4 s
constexpr double kMinZoom = 2.0;
constexpr double kMaxZoom = 2000.0;

const std::vector<std::string>& MediaExtensions() {
    static const std::vector<std::string> exts = {"mp4", "mov", "mkv", "webm", "avi", "m4v", "mpg", "mpeg", "mts", "ts",
                                                  "jpg", "jpeg", "png", "webp", "heic", "heif", "tif", "tiff", "bmp", "avif",
                                                  "mp3", "m4a", "aac", "wav", "flac", "ogg", "opus"};
    return exts;
}

bool IsProjectFile(const std::string& path) {
    return PathToUtf8(PathFromUtf8(path).extension()) == ".ultravideo";
}

double ZoomToSlider(double pixelsPerSecond) {
    return std::log(std::clamp(pixelsPerSecond, kMinZoom, kMaxZoom) / kMinZoom) / std::log(kMaxZoom / kMinZoom);
}

double SliderToZoom(double value) {
    return kMinZoom * std::pow(kMaxZoom / kMinZoom, std::clamp(value, 0.0, 1.0));
}

} // namespace

UltraVideoWindow::UltraVideoWindow(UltraCanvasApplication& app) : app_(app) {}

UltraVideoWindow::~UltraVideoWindow() {
    if (timer_) app_.StopTimer(timer_);
    if (job_) job_->Cancel();
    if (draftJob_) draftJob_->Cancel();
    viewer_.HideDraft();
    RemoveDraftFile(draftPath_);
    RemoveDraftFile(draftPending_);
}

// ============================================================================
// building
// ============================================================================
bool UltraVideoWindow::Create(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = std::string("Untitled - UltraVideo ") + ULTRAVIDEO_VERSION;
    config.width = 1440;
    config.height = 860;
    config.minWidth = 960;
    config.minHeight = 600;
    config.backgroundColor = ToColor(Tokens::Dark.Bg000);
    window_ = CreateWindow(config);
    if (!window_) return false;
    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    window->AddChild(BuildAppBar());

    // The work area over the timeline dock
    rows_ = CreateVerticalSplitPane("uv.rows", 0, 0, 1440, 820);
    rows_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto work = rows_->AddPane(100.0 - document_.view.dockPercent);
    auto dockPane = rows_->AddPane(document_.view.dockPercent);
    rows_->SetPaneMinSize(0, 240);
    rows_->SetPaneMinSize(1, 160);
    rows_->onWeightsChanged = [this](const std::vector<double>& weights) {
        if (weights.size() == 2 && weights[0] + weights[1] > 0.0)
            document_.view.dockPercent = static_cast<int>(std::lround(100.0 * weights[1] / (weights[0] + weights[1])));
    };

    // The three columns
    columns_ = CreateHorizontalSplitPane("uv.columns", 0, 0, 1440, 540);
    columns_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto binPane = columns_->AddPane(static_cast<double>(Tokens::BinW));
    viewerPane_ = columns_->AddPane(1440.0 - Tokens::BinW - Tokens::InspectorW);
    inspectorPane_ = columns_->AddPane(static_cast<double>(Tokens::InspectorW));
    columns_->SetPaneMinSize(0, 220);
    columns_->SetPaneMinSize(1, 320);
    columns_->SetPaneMinSize(2, 260);
    for (auto& pane : {binPane, viewerPane_, inspectorPane_}) {
        pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    }
    binPanel_ = binPane;
    binPane->AddChild(BuildMediaBin());
    viewerPane_->AddChild(viewer_.Build(*palette_));
    inspectorTabs_ = inspector_.Build(*palette_);
    inspector_.onEdit = [this](const std::string& label, std::function<void(Document&)> edit, const std::string& mergeKey) {
        Edit(label, edit, mergeKey);
    };
    inspector_.onEditEnd = [this]() { history_.EndMerge(); };
    inspector_.onAddMusic = [this]() { ImportMedia(); };
    inspector_.onUseProxies = [this](bool on) { SetUseProxies(on); };
    proxies_.onDone = [this](const std::string& clip, ProxyMaker::State state) { ProxyDone(clip, state); };
    proxies_.onProgress = [this]() { RefreshBin(); };
    inspectorPane_->AddChild(inspectorTabs_);
    work->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    work->AddChild(columns_);

    dockPane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dockPane->AddChild(BuildTimelineDock());
    window->AddChild(rows_);

    // the viewer's transport
    viewer_.onToStart = [this]() { StopPlayback(); SetPlayhead(0.0, false); };
    viewer_.onToEnd = [this]() { StopPlayback(); SetPlayhead(document_.Length(), false); };
    viewer_.onStepBack = [this]() { StopPlayback(); StepFrames(-1); };
    viewer_.onStepForward = [this]() { StopPlayback(); StepFrames(1); };
    viewer_.onPlayPause = [this]() { TogglePlay(); };

    // the engine's answers
    preview_.onFrame = [this](double, const VideoFXFrame& frame) { viewer_.ShowFrame(frame); };
    preview_.onOpened = [this]() { RequestFrame(); };
    preview_.onError = [this](const std::string& error) { Message("Preview: " + error); };

    window->InstallEventFilter("ultravideo-keys", [this](const UCEvent& event) { return OnKey(event); },
                               {UCEventType::KeyDown, UCEventType::Drop});
    // A file dragged out of the bin follows the pointer to the timeline
    window->InstallEventFilter("ultravideo-bindrag", [this](const UCEvent& event) {
        if (binDrag_.empty()) return false;
        if (event.type == UCEventType::MouseMove) { DragToTimeline(event, false); return true; }
        if (event.type == UCEventType::MouseUp) {
            DragToTimeline(event, true);
            binDrag_.clear();
            return true;
        }
        return false;
    }, {UCEventType::MouseMove, UCEventType::MouseUp});
    window->onWindowClosing = [this]() {
        if (history_.IsClean()) {
            app_.RequestExit();
            return true;
        }
        ConfirmThen([this]() { app_.RequestExit(); });
        return false;
    };

    ApplyPalette();
    SweepOldDrafts();

    // What the command line brought
    if (paths.size() == 1 && IsProjectFile(paths.front())) {
        OpenProjectFile(paths.front());
    } else {
        RefreshAll();
        if (!paths.empty()) ImportFiles(paths);
    }
    history_.MarkClean();
    RefreshAppBar();
    timer_ = app_.StartTimer(kTickMs, true, [this](TimerId) { Tick(); });
    return true;
}

void UltraVideoWindow::Show() {
    if (window_) window_->Show();
}

std::shared_ptr<UltraCanvasContainer> UltraVideoWindow::BuildAppBar() {
    const Palette& p = *palette_;
    appBar_ = std::make_shared<UltraCanvasContainer>("uv.appbar", 0, 0, 1440, static_cast<int>(Tokens::AppbarH));
    appBar_->layout.SetFlexRow().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    appBar_->layoutItem.SetFlexShrink(0).SetFlexBasis(CSSLayout::Dimension::Px(Tokens::AppbarH));
    appBar_->SetPadding(0, static_cast<float>(Tokens::Space3));

    auto logo = std::make_shared<UltraCanvasButton>("uv.logo", 0, 0, 28, 28, "");
    logo->SetStyle(GhostButtonStyle(p));
    logo->SetIcon(IconPath("logo"));
    logo->SetIconSize(18, 18);
    logo->SetTooltip(std::string("UltraVideo ") + ULTRAVIDEO_VERSION);
    logo->layoutItem.SetFlexShrink(0);
    appBar_->AddChild(logo);

    title_ = CreateLabel("uv.title", 0, 0, 200, 28, "Untitled");
    title_->SetFont(Tokens::Title.family, Tokens::Title.size * 0.75f, FontWeight::Bold);
    title_->layoutItem.SetFlexShrink(0);
    appBar_->AddChild(title_);
    status_ = CreateLabel("uv.status", 0, 0, 400, 28, "");
    status_->SetFont(Tokens::Body.family, Tokens::Body.size * 0.75f);
    status_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    appBar_->AddChild(status_);

    undo_ = MakeIconButton("uv.undo", "undo", "Undo  Ctrl+Z", p);
    undo_->SetOnClick([this]() { if (history_.Undo(document_)) { RefreshAll(); Message("Undid " + history_.RedoLabel()); } });
    appBar_->AddChild(undo_);
    redo_ = MakeIconButton("uv.redo", "redo", "Redo  Ctrl+Shift+Z", p);
    redo_->SetOnClick([this]() { if (history_.Redo(document_)) { RefreshAll(); Message("Redid " + history_.UndoLabel()); } });
    appBar_->AddChild(redo_);
    auto gap = std::make_shared<UltraCanvasContainer>("uv.appbar.gap", 0, 0, 12, 28);
    gap->layoutItem.SetFlexShrink(0);
    appBar_->AddChild(gap);
    open_ = MakeIconButton("uv.open", "film", "Open project  Ctrl+O", p);
    open_->SetOnClick([this]() { OpenProject(); });
    appBar_->AddChild(open_);
    save_ = MakeIconButton("uv.save", "duplicate", "Save  Ctrl+S", p);
    save_->SetOnClick([this]() { SaveProject(); });
    appBar_->AddChild(save_);
    import_ = MakeIconButton("uv.import", "upload", "Import media  Ctrl+I", p);
    import_->SetOnClick([this]() { ImportMedia(); });
    appBar_->AddChild(import_);
    story_ = MakeIconButton("uv.story", "image", "Make a story from photos and clips  Ctrl+Shift+N", p);
    story_->SetOnClick([this]() { MakeStory(); });
    appBar_->AddChild(story_);
    theme_ = MakeIconButton("uv.theme", "eye", "Light or dark", p);
    theme_->SetOnClick([this]() {
        dark_ = !dark_;
        document_.view.dark = dark_;
        palette_ = &PaletteFor(dark_);
        ApplyPalette();
    });
    appBar_->AddChild(theme_);
    export_ = MakeTextButton("uv.export", "Export", p, 96, true);
    export_->SetIcon(IconPath("export"));
    export_->SetIconSize(16, 16);
    export_->SetTooltip("Export the video  Ctrl+E");
    export_->SetOnClick([this]() { Export(); });
    appBar_->AddChild(export_);
    return appBar_;
}

std::shared_ptr<UltraCanvasTabbedContainer> UltraVideoWindow::BuildMediaBin() {
    const Palette& p = *palette_;
    binTabs_ = std::make_shared<UltraCanvasTabbedContainer>("uv.bin.tabs", 0, 0, static_cast<int>(Tokens::BinW), 500);
    binTabs_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    binTabs_->SetCloseMode(TabCloseMode::NoClose);
    binTabs_->SetTabMinWidth(52);
    binTabs_->SetTabStyle(TabStyle::Flat);

    // Media: the search field over the grid, or the empty state
    auto mediaPage = std::make_shared<UltraCanvasContainer>("uv.bin.media", 0, 0, static_cast<int>(Tokens::BinW), 480);
    mediaPage->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    mediaPage->SetPadding(static_cast<float>(Tokens::Space2));
    binSearch_ = CreateTextInput("uv.bin.search", 0, 0, 200, static_cast<int>(Tokens::ControlH));
    binSearch_->SetPlaceholder("Search media");
    binSearch_->onTextChanged = [this](const std::string&) { RefreshBin(); };
    binSearch_->layoutItem.SetFlexShrink(0);
    mediaPage->AddChild(binSearch_);

    auto makeAlbum = [&](const std::string& id, std::shared_ptr<UltraCanvasAlbum>& album, bool music) {
        album = std::make_shared<UltraCanvasAlbum>(id, 0, 0, 260, 400);
        album->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        album->SetMode(AlbumMode::UserEdit);
        AlbumAction add;
        add.id = "add";
        add.label = music ? "Use as music" : "Add to the timeline";
        add.iconPath = IconPath("plus");
        add.onTrigger = [this, music](size_t row) {
            const std::vector<size_t>& rows = music ? musicRows_ : binRows_;
            if (row < rows.size() && rows[row] < document_.media.size()) AddMediaToTimeline(document_.media[rows[row]].path);
        };
        album->AddAction(add);
        if (!music) {
            AlbumAction over;
            over.id = "overlay";
            over.label = "Over the picture (V2)";
            over.iconPath = IconPath("image");
            over.onTrigger = [this](size_t row) {
                if (row < binRows_.size() && binRows_[row] < document_.media.size())
                    AddOverlay(document_.media[binRows_[row]].path, document_.view.playhead);
            };
            album->AddAction(over);
        } else {
            AlbumAction voice;
            voice.id = "voice";
            voice.label = "Voice-over at the playhead (A2)";
            voice.iconPath = IconPath("wave");
            voice.onTrigger = [this](size_t row) {
                if (row < musicRows_.size() && musicRows_[row] < document_.media.size())
                    AddSound(document_.media[musicRows_[row]].path, document_.view.playhead, true);
            };
            album->AddAction(voice);
            AlbumAction effect;
            effect.id = "effect";
            effect.label = "Sound effect at the playhead (A2)";
            effect.iconPath = IconPath("speaker");
            effect.onTrigger = [this](size_t row) {
                if (row < musicRows_.size() && musicRows_[row] < document_.media.size())
                    AddSound(document_.media[musicRows_[row]].path, document_.view.playhead, false);
            };
            album->AddAction(effect);
        }
        AlbumAction remove;
        remove.id = "remove";
        remove.label = "Remove from the bin";
        remove.iconPath = IconPath("trash");
        remove.onTrigger = [this, music](size_t row) {
            const std::vector<size_t>& rows = music ? musicRows_ : binRows_;
            if (row >= rows.size() || rows[row] >= document_.media.size()) return;
            const std::string path = document_.media[rows[row]].path;
            if (document_.MediaInUse(path)) {
                Message("The timeline still uses it: remove its clips first.");
                return;
            }
            Edit("Remove media", [path](Document& d) { d.RemoveMedia(path); });
        };
        album->AddAction(remove);
        album->onItemActivated = [this, music](size_t row) {
            const std::vector<size_t>& rows = music ? musicRows_ : binRows_;
            if (row < rows.size() && rows[row] < document_.media.size()) AddMediaToTimeline(document_.media[rows[row]].path);
        };
        album->onItemDraggedOut = [this, music](size_t row, const Point2Di&) {
            const std::vector<size_t>& rows = music ? musicRows_ : binRows_;
            if (row < rows.size() && rows[row] < document_.media.size()) BeginBinDrag(document_.media[rows[row]].path);
        };
    };
    makeAlbum("uv.bin.album", binAlbum_, false);
    mediaPage->AddChild(binAlbum_);

    // The empty state: the drop zone with the two ways in
    binEmpty_ = CreateLabel("uv.bin.empty", 0, 0, 240, 120,
                            "Drop clips here to start\n\nVideos, photos and songs. Or import them with the button above, "
                            "or make a story from photos with the picture button.");
    binEmpty_->SetFont(Tokens::Body.family, Tokens::Body.size * 0.75f);
    binEmpty_->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    binEmpty_->layoutItem.SetFlexShrink(0);
    binEmpty_->SetPadding(static_cast<float>(Tokens::Space4));
    mediaPage->AddChild(binEmpty_);
    binTabs_->AddTab("Media", mediaPage);

    auto musicPage = std::make_shared<UltraCanvasContainer>("uv.bin.musicPage", 0, 0, static_cast<int>(Tokens::BinW), 480);
    musicPage->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    musicPage->SetPadding(static_cast<float>(Tokens::Space2));
    makeAlbum("uv.bin.music", musicAlbum_, true);
    musicPage->AddChild(musicAlbum_);
    binTabs_->AddTab("Music", musicPage);

    // Titles: one button per style, each adding a span at the playhead
    auto titlesPage = std::make_shared<UltraCanvasContainer>("uv.bin.titlesPage", 0, 0, static_cast<int>(Tokens::BinW), 480);
    titlesPage->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    titlesPage->SetPadding(static_cast<float>(Tokens::Space3));
    titlesNote_ = CreateLabel("uv.bin.titles", 0, 0, 240, 40,
                              "A title goes on the title lane at the playhead; drag it, trim it, and set its text in the inspector.");
    titlesNote_->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    titlesNote_->layoutItem.SetFlexShrink(0);
    titlesPage->AddChild(titlesNote_);
    for (const TitleStyle& st : TitleStyles()) {
        auto button = MakeTextButton(std::string("uv.bin.title.") + st.key, st.label, p, 240);
        button->SetIcon(IconPath("title"));
        button->SetIconSize(16, 16);
        button->SetTooltip(st.hint);
        button->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        const std::string key = st.key;
        button->SetOnClick([this, key]() { AddTitle(key, document_.view.playhead); });
        titleButtons_.push_back(button);
        titlesPage->AddChild(button);
        auto hint = CreateLabel(std::string("uv.bin.titleHint.") + st.key, 0, 0, 240, 18, st.hint);
        hint->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
        hint->layoutItem.SetFlexShrink(0);
        titleHints_.push_back(hint);
        titlesPage->AddChild(hint);
    }
    binTabs_->AddTab("Titles", titlesPage);

    // Effects: one button per preset, each putting it on the selected clip
    auto effectsPage = std::make_shared<UltraCanvasContainer>("uv.bin.effectsPage", 0, 0, static_cast<int>(Tokens::BinW), 480);
    effectsPage->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space1)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    effectsPage->SetPadding(static_cast<float>(Tokens::Space3));
    effectsNote_ = CreateLabel("uv.bin.effects", 0, 0, 240, 40,
                               "A click puts the effect on the selected clip; the inspector lists the clip's effects and removes them.");
    effectsNote_->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    effectsNote_->layoutItem.SetFlexShrink(0);
    effectsPage->AddChild(effectsNote_);
    for (const bool blurGroup : {false, true}) {
        auto heading = CreateLabel(blurGroup ? "uv.bin.effects.detail" : "uv.bin.effects.looks", 0, 0, 240, 22,
                                   blurGroup ? "Blur and detail" : "Colour looks");
        heading->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
        heading->layoutItem.SetFlexShrink(0);
        effectHeadings_.push_back(heading);
        effectsPage->AddChild(heading);
        for (const EffectPreset& e : EffectPresets()) {
            if (e.blur != blurGroup) continue;
            auto button = MakeTextButton(std::string("uv.bin.effect.") + e.key, e.label, p, 240);
            button->SetIcon(IconPath("fx"));
            button->SetIconSize(16, 16);
            button->SetTooltip(e.hint);
            button->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
            const std::string key = e.key;
            button->SetOnClick([this, key]() { ApplyEffect(key); });
            effectButtons_.push_back(button);
            effectsPage->AddChild(button);
        }
    }
    binTabs_->AddTab("Effects", effectsPage);

    (void)p;
    return binTabs_;
}

std::shared_ptr<UltraCanvasToolbar> UltraVideoWindow::BuildTimelineToolbar() {
    const Palette& p = *palette_;
    timelineBar_ = std::make_shared<UltraCanvasToolbar>("uv.timelineBar", 0, 0, 1440, static_cast<int>(Tokens::ToolbarH));
    timelineBar_->layoutItem.SetFlexShrink(0).SetFlexBasis(CSSLayout::Dimension::Px(Tokens::ToolbarH))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    toolSelect_ = MakeIconButton("uv.tool.select", "select", "Select  V", p);
    toolSelect_->SetOnClick([this]() { SetTool(TimelineTool::Select); });
    toolBlade_ = MakeIconButton("uv.tool.blade", "blade", "Blade  B", p);
    toolBlade_->SetOnClick([this]() { SetTool(TimelineTool::Blade); });
    toolTrim_ = MakeIconButton("uv.tool.trim", "clipIn", "Trim  T", p);
    toolTrim_->SetOnClick([this]() { SetTool(TimelineTool::Trim); });
    toolSlip_ = MakeIconButton("uv.tool.slip", "slip", "Slip  Y", p);
    toolSlip_->SetOnClick([this]() { SetTool(TimelineTool::Slip); });
    for (auto& b : {toolSelect_, toolBlade_, toolTrim_, toolSlip_}) timelineBar_->AddChild(b);
    timelineBar_->AddSeparator("uv.sep1");
    split_ = MakeIconButton("uv.act.split", "split", "Split at the playhead  S", p);
    split_->SetOnClick([this]() { SplitAtPlayhead(); });
    delete_ = MakeIconButton("uv.act.delete", "trash", "Delete  Del", p);
    delete_->SetOnClick([this]() { DeleteSelection(); });
    marker_ = MakeIconButton("uv.act.marker", "marker", "Add a marker  M", p);
    marker_->SetOnClick([this]() { AddMarker(); });
    for (auto& b : {split_, delete_, marker_}) timelineBar_->AddChild(b);
    timelineBar_->AddSeparator("uv.sep2");
    magnet_ = MakeIconButton("uv.mode.magnet", "magnet", "Magnetic main track  P", p);
    magnet_->SetOnClick([this]() {
        TimelineTrack* track = nullptr;
        TimelineModel model = BuildTimelineModel(document_);
        track = model.FindTrack(kMainTrackId);
        const bool on = track ? !track->magnetic : true;
        Message(on ? "The main track closes its gaps again." : "Gaps on the main track stay as they are.");
        (void)on;
    });
    magnet_->SetDisabled(true);   // the main track is always magnetic in this release
    snap_ = MakeIconButton("uv.mode.snap", "snap", "Snapping  N", p);
    snap_->SetOnClick([this]() {
        snapping_ = !snapping_;
        if (timeline_) timeline_->SetSnapping(snapping_);
        RefreshToolbarLooks();
    });
    beats_ = MakeIconButton("uv.mode.beats", "beat", "Beat marks", p);
    beats_->SetOnClick([this]() {
        showBeats_ = !showBeats_;
        if (timeline_) timeline_->SetShowBeats(showBeats_);
        RefreshToolbarLooks();
        if (showBeats_) RequestBeats();
        RefreshTimeline();
    });
    for (auto& b : {magnet_, snap_, beats_}) timelineBar_->AddChild(b);
    timelineBar_->AddStretch();

    playheadField_ = CreateTimecodeField("uv.playhead", 0, 0, 120, static_cast<int>(Tokens::ControlH));
    playheadField_->onCommit = [this](long long frames) { StopPlayback(); SetPlayhead(frames / document_.FrameRate(), false); };
    playheadField_->layoutItem.SetFlexShrink(0);
    timelineBar_->AddChild(playheadField_);
    lengthLabel_ = CreateLabel("uv.length", 0, 0, 100, static_cast<int>(Tokens::ControlH), "/ 00:00:00:00");
    lengthLabel_->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
    lengthLabel_->layoutItem.SetFlexShrink(0);
    timelineBar_->AddChild(lengthLabel_);
    timelineBar_->AddStretch();

    zoomOut_ = MakeIconButton("uv.zoom.out", "zoomOut", "Zoom out  -", p);
    zoomOut_->SetOnClick([this]() { if (timeline_) timeline_->ZoomBy(0.8, document_.view.playhead); });
    timelineBar_->AddChild(zoomOut_);
    zoom_ = CreateSlider("uv.zoom", 0, 0, 120, static_cast<int>(Tokens::ControlH));
    zoom_->SetRange(0.0f, 1.0f);
    zoom_->SetValue(static_cast<float>(ZoomToSlider(document_.view.pixelsPerSecond)));
    zoom_->SetValueDisplay(SliderValueDisplay::NoDisplay);
    zoom_->onValueChanged = [this](float v) { if (timeline_) timeline_->SetPixelsPerSecond(SliderToZoom(v)); };
    zoom_->layoutItem.SetFlexShrink(0);
    timelineBar_->AddChild(zoom_);
    zoomIn_ = MakeIconButton("uv.zoom.in", "zoomIn", "Zoom in  =", p);
    zoomIn_->SetOnClick([this]() { if (timeline_) timeline_->ZoomBy(1.25, document_.view.playhead); });
    timelineBar_->AddChild(zoomIn_);
    zoomFit_ = MakeIconButton("uv.zoom.fit", "fit", "Fit the project  Shift+Z", p);
    zoomFit_->SetOnClick([this]() { if (timeline_) timeline_->ZoomToFit(); });
    timelineBar_->AddChild(zoomFit_);
    timelineBar_->AddSpacer(static_cast<int>(Tokens::Space2));
    return timelineBar_;
}

std::shared_ptr<UltraCanvasContainer> UltraVideoWindow::BuildTimelineDock() {
    dock_ = std::make_shared<UltraCanvasContainer>("uv.dock");
    dock_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dock_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    dock_->AddChild(BuildTimelineToolbar());

    timeline_ = CreateVideoTimeline("uv.timeline", 0, 0, 1440, 260);
    timeline_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    timeline_->SetSnapping(snapping_);
    timeline_->SetShowBeats(showBeats_);

    // Thumbnails and peaks: decoded by the engine on its worker, delivered on the UI thread
    timeline_->thumbnailProvider = [this](const std::string& source, double in, double out, int frames, int height,
                                          std::function<void(std::vector<UCPixmapPtr>)> done) {
        const bool still = Document::KindOfPath(source) == MediaKind::Image;
        auto decoded = std::make_shared<std::vector<VideoFXFrame>>();
        preview_.Run(
            [source, in, out, frames, height, still, decoded]() {
                const int count = still ? 1 : std::max(1, frames);
                for (int i = 0; i < count; ++i) {
                    const double t = still ? 0.0 : (count > 1 ? in + (out - in) * i / (count - 1) : in);
                    VideoFXFrame frame;
                    if (VideoFX_ExtractFrame(source, t, frame, 0, height) == VideoFXResult::Ok) decoded->push_back(std::move(frame));
                    else break;
                }
            },
            [decoded, frames, still, done]() {
                std::vector<UCPixmapPtr> strip;
                for (const VideoFXFrame& f : *decoded) {
                    auto pix = std::make_shared<UCPixmap>(f.width, f.height);
                    uint32_t* out = pix->GetPixelData();
                    if (!out) continue;
                    const uint8_t* in = f.pixels.data();
                    const size_t n = static_cast<size_t>(f.width) * f.height;
                    for (size_t i = 0; i < n; ++i, in += 4)
                        out[i] = 0xFF000000u | (static_cast<uint32_t>(in[0]) << 16) | (static_cast<uint32_t>(in[1]) << 8) | in[2];
                    pix->MarkDirty();
                    strip.push_back(pix);
                }
                if (still && !strip.empty()) strip.resize(static_cast<size_t>(std::max(1, frames)), strip.front());
                done(std::move(strip));
            });
    };
    timeline_->peaksProvider = [this](const std::string& source, double in, double out, double blocksPerSecond,
                                      std::function<void(WaveformChannelData)> done) {
        auto waveform = std::make_shared<VideoFXWaveform>();
        preview_.Run(
            [source, in, out, blocksPerSecond, waveform]() {
                VideoFX_ExtractWaveform(source, *waveform, blocksPerSecond, in, out);
            },
            [waveform, done]() {
                WaveformChannelData data;
                data.minValues = waveform->minValues;
                data.maxValues = waveform->maxValues;
                data.rmsValues = waveform->rmsValues;
                data.samplesPerBlock = waveform->samplesPerBlock;
                data.totalBlocks = waveform->totalBlocks;
                data.duration = waveform->duration;
                done(std::move(data));
            });
    };

    // The change loop: the element reports, the window applies and feeds back
    timeline_->onChangeRequested = [this](const TimelineChange& change) {
        Document trial = document_;
        if (!ApplyChange(trial, change)) {
            Message("That edit is not possible here.");
            return false;
        }
        Edit(DescribeChange(change), [&change](Document& d) { ApplyChange(d, change); });
        return true;
    };
    timeline_->onSelectionChanged = [this](const std::vector<std::string>& ids) {
        selectedClip_ = ids.empty() ? "" : ids.back();
        RefreshInspector();
        if (SongOfClip(selectedClip_) >= 0) inspector_.ShowAudioTab();
    };
    timeline_->onClipActivated = [this](const std::string& id) {
        selectedClip_ = id;
        RefreshInspector();
        if (SongOfClip(id) >= 0) inspector_.ShowAudioTab();
        else inspector_.ShowClipTab();
    };
    timeline_->onTransitionActivated = [this](const std::string& id) {
        selectedClip_ = id;
        RefreshInspector();
        inspector_.ShowClipTab();
    };
    timeline_->onPlayheadChanged = [this](double seconds) { StopPlayback(); SetPlayhead(seconds, true); };
    timeline_->onRangeChanged = [this](double in, double out) {
        document_.view.rangeIn = in;
        document_.view.rangeOut = out;
        RefreshTimeline();
    };
    timeline_->onFilesDropped = [this](const std::vector<std::string>& files, const std::string& trackId, double seconds) {
        if (trackId == kOverlayTrackId) {
            double at = seconds;
            for (const std::string& f : files) {
                AddOverlay(f, at);
                if (const OverlaySpan* v = document_.overlays.empty() ? nullptr : &document_.overlays.back()) at = v->End();
            }
            return;
        }
        if (trackId == kSoundTrackId) {
            double at = seconds;
            for (const std::string& f : files) {
                const size_t before = document_.sounds.size();
                AddSound(f, at, true);
                if (document_.sounds.size() > before) at = document_.sounds.back().End();
            }
            return;
        }
        ImportFiles(files, seconds);
    };
    timeline_->onContextMenuRequested = [this](const std::string& trackId, double seconds, const std::string& clipId, int x, int y) {
        ShowTimelineMenu(trackId, seconds, clipId, x, y);
    };
    timeline_->onTrackToggled = [this](const std::string&, TimelineTrackToggle toggle, bool) {
        if (toggle == TimelineTrackToggle::Magnetic) Message("The main track is always magnetic in this release.");
        RefreshTimeline();
    };
    timeline_->onZoomChanged = [this](double pixelsPerSecond) {
        document_.view.pixelsPerSecond = pixelsPerSecond;
        if (zoom_) zoom_->SetValue(static_cast<float>(ZoomToSlider(pixelsPerSecond)));
    };
    timeline_->onToolChanged = [this](TimelineTool) { RefreshToolbarLooks(); };
    dock_->AddChild(timeline_);
    return dock_;
}

void UltraVideoWindow::ApplyPalette() {
    const Palette& p = *palette_;
    window_->SetBackgroundColor(ToColor(p.Bg000));
    appBar_->SetBackgroundColor(ToColor(p.Bg100));
    appBar_->SetBorderBottom(1, ToColor(p.Line));
    title_->SetTextColor(ToColor(p.Ink));
    status_->SetTextColor(ToColor(p.InkMuted));
    for (auto& b : {undo_, redo_, open_, save_, import_, story_, theme_}) b->SetStyle(GhostButtonStyle(p));
    export_->SetStyle(PrimaryButtonStyle(p));
    theme_->SetIcon(IconPath(dark_ ? "eye" : "eyeOff"));

    SplitPaneStyle split = columns_->GetSplitPaneStyle();
    split.splitterColor = ToColor(p.Bg000);
    split.splitterHoverColor = ToColor(p.LineStrong);
    split.splitterActiveColor = ToColor(p.Accent);
    split.splitterThickness = static_cast<int>(Tokens::SplitterW);
    columns_->SetSplitPaneStyle(split);
    rows_->SetSplitPaneStyle(split);

    binPanel_->SetBackgroundColor(ToColor(p.Bg100));
    inspectorPane_->SetBackgroundColor(ToColor(p.Bg100));
    viewerPane_->SetBackgroundColor(ToColor(p.Bg000));
    binTabs_->SetBackgroundColor(ToColor(p.Bg100));
    binTabs_->SetTabBarColor(ToColor(p.Bg100));
    binTabs_->SetInactiveTabBackgroundColor(ToColor(p.Bg100));
    binTabs_->SetActiveTabBackgroundColor(ToColor(p.Bg100));
    binTabs_->SetHoveredTabBackgroundColor(ToColor(p.Bg400));
    binTabs_->SetInactiveTabTextColor(ToColor(p.InkMuted));
    binTabs_->SetActiveTabTextColor(ToColor(p.Ink));
    binTabs_->SetActiveTabIndicatorColor(ToColor(p.Accent));
    binTabs_->SetActiveTabBorderColor(ToColor(p.Line));
    binTabs_->SetInactiveTabBorderColor(ToColor(p.Line));
    binTabs_->SetHoveredTabBorderColor(ToColor(p.Line));
    binTabs_->contentAreaColor = ToColor(p.Bg100);
    binTabs_->tabContentBorderColor = ToColor(p.Line);
    binTabs_->navButtonBackgroundColor = ToColor(p.Bg300);
    binTabs_->navButtonIconColor = ToColor(p.Ink);
    TextInputStyle search = binSearch_->GetStyle();
    search.backgroundColor = ToColor(p.Bg200);
    search.borderColor = ToColor(p.LineStrong);
    search.focusBorderColor = ToColor(p.Focus);
    search.textColor = ToColor(p.Ink);
    search.placeholderColor = ToColor(p.InkFaint);
    search.caretColor = ToColor(p.Ink);
    search.borderRadius = static_cast<int>(Tokens::RadiusSm);
    binSearch_->SetStyle(search);
    for (auto& album : {binAlbum_, musicAlbum_}) {
        AlbumConfig cfg;
        cfg.layout = AlbumLayout::UniformGrid;
        cfg.sizingMode = AlbumSizingMode::ItemsPerRow;
        cfg.itemsPerRow = 2;
        cfg.gap = static_cast<int>(Tokens::Space2);
        cfg.outerPadding = 0;
        cfg.imageDisplay = AlbumImageDisplay::Crop;
        cfg.actionDisplay = AlbumActionDisplay::OnHover;
        cfg.backgroundColor = ToColor(p.Bg100);
        cfg.itemBackgroundColor = ToColor(p.Bg200);
        cfg.showBorder = false;
        cfg.captionColor = ToColor(p.Ink);
        cfg.subtitleColor = ToColor(p.InkMuted);
        cfg.allowSelection = true;
        cfg.selectionColor = ToColor(p.Accent);
        cfg.mode = AlbumMode::UserEdit;      // tiles can be dragged (out to the timeline)
        album->SetConfig(cfg);
    }
    binEmpty_->SetTextColor(ToColor(p.InkMuted));
    binEmpty_->SetBorders(1, ToColor(p.LineStrong), Tokens::RadiusLg, UCDashPattern({6.0, 4.0}));
    titlesNote_->SetTextColor(ToColor(p.InkMuted));
    for (auto& b : titleButtons_) b->SetStyle(PlainButtonStyle(p));
    for (auto& h : titleHints_) h->SetTextColor(ToColor(p.InkFaint));
    effectsNote_->SetTextColor(ToColor(p.InkMuted));
    for (auto& b : effectButtons_) b->SetStyle(PlainButtonStyle(p));
    for (auto& h : effectHeadings_) h->SetTextColor(ToColor(p.InkFaint));
    viewer_.ApplyPalette(p);
    inspector_.ApplyPalette(p);

    dock_->SetBackgroundColor(ToColor(p.Bg100));
    ToolbarAppearance bar;
    bar.style = ToolbarStyle::Flat;
    bar.backgroundColor = ToColor(p.Bg100);
    bar.foregroundColor = ToColor(p.Ink);
    bar.separatorColor = ToColor(p.Line);
    bar.hoverBackgroundColor = ToColor(p.Bg400);
    bar.activeBackgroundColor = ToColor(p.Bg500);
    bar.showIconLabels = false;
    bar.hasShadow = false;
    bar.iconSize = ToolbarIconSize::Small;
    timelineBar_->SetAppearance(bar);
    for (auto& b : {split_, delete_, marker_, magnet_, zoomOut_, zoomIn_, zoomFit_}) b->SetStyle(GhostButtonStyle(p));
    playheadField_->SetStyle(TimecodeStyleFor(p));
    lengthLabel_->SetTextColor(ToColor(p.InkMuted));
    SliderVisualStyle slider;
    slider.trackColor = ToColor(p.Bg500);
    slider.activeTrackColor = ToColor(p.Accent);
    slider.handleColor = ToColor(p.Ink);
    slider.handleBorderColor = ToColor(p.Bg100);
    slider.trackHeight = 4.0f;
    slider.handleSize = 12.0f;
    zoom_->SetStyle(slider);
    timeline_->SetStyle(TimelineStyleFor(p));
    RefreshToolbarLooks();
    window_->RequestRedraw();
}

// ============================================================================
// the document
// ============================================================================
void UltraVideoWindow::Edit(const std::string& label, const std::function<void(Document&)>& edit,
                            const std::string& mergeKey) {
    Document before = document_;
    edit(document_);
    history_.Push(label, before, document_, mergeKey);
    InvalidateDraft();
    RefreshAll();
}

void UltraVideoWindow::SetDocument(Document document) {
    StopPlayback();
    document_ = std::move(document);
    InvalidateDraft();
    dark_ = document_.view.dark;
    palette_ = &PaletteFor(dark_);
    history_.Clear();
    selectedClip_.clear();
    if (timeline_) {
        timeline_->ClearCaches();
        timeline_->SetPixelsPerSecond(document_.view.pixelsPerSecond);
        timeline_->SetScrollSeconds(document_.view.scrollSeconds);
    }
    ApplyPalette();
    RefreshAll();
    SetPlayhead(document_.view.playhead, false);
}

void UltraVideoWindow::RefreshAll() {
    RefreshTimeline();
    RefreshBin();
    RefreshInspector();
    RefreshPreview();
    RefreshAppBar();
}

void UltraVideoWindow::RefreshAppBar() {
    title_->SetText(document_.Name());
    const std::string state = history_.IsClean() ? "Saved" : "Edited";
    if (messageTicks_ <= 0) status_->SetText(document_.Describe() + " \xC2\xB7 " + state);
    undo_->SetDisabled(!history_.CanUndo());
    undo_->SetTooltip(history_.CanUndo() ? "Undo " + history_.UndoLabel() + "  Ctrl+Z" : "Nothing to undo");
    redo_->SetDisabled(!history_.CanRedo());
    redo_->SetTooltip(history_.CanRedo() ? "Redo " + history_.RedoLabel() + "  Ctrl+Shift+Z" : "Nothing to redo");
    export_->SetDisabled(document_.project.segments.empty());
    if (window_) window_->SetWindowTitle(document_.Name() + (history_.IsClean() ? "" : " *") + " - UltraVideo " ULTRAVIDEO_VERSION);
}

void UltraVideoWindow::RefreshBin() {
    const std::string filter = binSearch_ ? binSearch_->GetText() : "";
    auto matches = [&](const MediaItem& m) {
        if (filter.empty()) return true;
        std::string a = m.name, b = filter;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a.find(b) != std::string::npos;
    };
    std::vector<AlbumItem> items, songs;
    binRows_.clear();
    musicRows_.clear();
    // every readable video gets a proxy while proxies are on; ones gone from
    // the bin stop being made
    std::vector<std::string> videos;
    for (const MediaItem& m : document_.media)
        if (m.kind == MediaKind::Video && m.error.empty()) videos.push_back(m.path);
    if (!document_.view.useProxies) videos.clear();
    proxies_.Keep(videos);
    for (const std::string& v : videos) proxies_.Want(v);
    for (size_t i = 0; i < document_.media.size(); ++i) {
        const MediaItem& m = document_.media[i];
        AlbumItem item;
        item.mediaPath = m.path;
        item.title = m.name;
        item.subtitle = m.Badge();
        if (m.kind == MediaKind::Video && document_.view.useProxies) {
            const std::string proxy = proxies_.Badge(m.path);
            if (!proxy.empty()) item.subtitle += " \xC2\xB7 " + proxy;
        }
        item.id = m.path;
        if (m.kind == MediaKind::Audio) {
            item.mediaType = AlbumMediaType::Music;
            item.thumbnailPath = IconPath("music");
            songs.push_back(item);
            musicRows_.push_back(i);
            continue;
        }
        if (!matches(m)) continue;
        item.mediaType = m.kind == MediaKind::Image ? AlbumMediaType::Photo : AlbumMediaType::Video;
        if (!m.error.empty()) item.thumbnailPath = IconPath("film");
        items.push_back(item);
        binRows_.push_back(i);
    }
    binAlbum_->SetItems(items);
    musicAlbum_->SetItems(songs);
    const bool empty = document_.media.empty();
    binEmpty_->SetVisible(empty);
    binAlbum_->SetVisible(!empty);
}

void UltraVideoWindow::RefreshTimeline() {
    if (!timeline_) return;
    TimelineModel model = BuildTimelineModel(document_);
    // the first song's beats, as timeline times (the song may start into itself)
    const VideoFXMusic& music = document_.project.settings.music;
    if (showBeats_ && music.IsSet()) {
        const std::vector<std::string> songs = music.Songs();
        auto found = songBeats_.find(songs.front());
        if (found != songBeats_.end()) {
            const double length = document_.Length();
            for (double b : found->second) {
                const double t = b - music.start;
                if (t >= 0.0 && (length <= 0.0 || t <= length)) model.beats.push_back(t);
            }
        } else {
            RequestBeats();
        }
    }
    timeline_->SetModel(model);
    if (!selectedClip_.empty() && SegmentOfClip(document_, selectedClip_) < 0 && document_.TitleIndex(selectedClip_) < 0 &&
        document_.OverlayIndex(selectedClip_) < 0 && document_.SoundIndex(selectedClip_) < 0 &&
        !(SongOfClip(selectedClip_) >= 0 && static_cast<size_t>(SongOfClip(selectedClip_)) < music.Songs().size()))
        selectedClip_.clear();
    const double length = document_.Length();
    const double fps = document_.FrameRate();
    playheadField_->SetFrameRate(fps);
    playheadField_->SetMaximumFrames(static_cast<long long>(length * fps));
    lengthLabel_->SetText("/ " + TimelineTimecode(length, fps));
    viewer_.SetFrameRate(fps);
    viewer_.SetAspect(static_cast<double>(document_.Width()) / std::max(1, document_.Height()));
    viewer_.SetTime(document_.view.playhead, length);
    if (document_.view.playhead > length) SetPlayhead(length, false);
}

void UltraVideoWindow::RefreshInspector() {
    inspector_.Show(document_, SegmentOfClip(document_, selectedClip_), document_.TitleIndex(selectedClip_),
                    document_.OverlayIndex(selectedClip_), document_.SoundIndex(selectedClip_));
}

void UltraVideoWindow::RefreshPreview() {
    VideoFXExportSettings settings = document_.project.settings;
    settings.width = document_.Width();
    settings.height = document_.Height();
    settings.frameRate = document_.FrameRate();
    preview_.Open(SegmentsToPlay(), settings);
    if (document_.project.segments.empty()) viewer_.ClearFrame();
}

// ============================================================================
// files
// ============================================================================
void UltraVideoWindow::ConfirmThen(std::function<void()> then) {
    if (history_.IsClean()) {
        then();
        return;
    }
    AskToSave(window_.get(), document_.Name(), [this, then](DiscardAnswer answer) {
        if (answer == DiscardAnswer::Cancel) return;
        if (answer == DiscardAnswer::Save && !SaveProject()) return;
        then();
    });
}

void UltraVideoWindow::NewProject() {
    ConfirmThen([this]() {
        Document fresh;
        fresh.view.dark = dark_;
        SetDocument(std::move(fresh));
        Message("A new project.");
    });
}

void UltraVideoWindow::OpenProject() {
    ConfirmThen([this]() {
        FileDialogOptions opts;
        opts.SetTitle("Open project").SetParentWindow(window_.get());
        opts.AddFilter("UltraVideo project", "ultravideo");
        UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) OpenProjectFile(path);
        });
    });
}

void UltraVideoWindow::OpenProjectFile(const std::string& path) {
    Document loaded;
    std::string error;
    std::vector<std::string> missing;
    if (!Document::Load(path, loaded, error, &missing)) {
        UltraCanvasAlert::Error(error, "Open project", nullptr, window_.get());
        return;
    }
    SetDocument(std::move(loaded));
    history_.MarkClean();
    RefreshAppBar();
    if (!missing.empty()) {
        std::string list;
        for (const std::string& m : missing) list += "\n" + PathToUtf8(PathFromUtf8(m).filename());
        UltraCanvasAlert::Warning("These files are missing; their clips stay on the timeline until they are found:" + list,
                                  "Open project", nullptr, window_.get());
    }
    Message("Opened " + document_.Name() + ".");
}

bool UltraVideoWindow::SaveProject() {
    if (document_.path.empty()) return SaveProjectAs();
    return SaveTo(document_.path);
}

bool UltraVideoWindow::SaveProjectAs() {
    FileDialogOptions opts;
    opts.SetTitle("Save project as").SetDefaultFileName(document_.Name() + ".ultravideo").SetDefaultExtension("ultravideo")
        .SetConfirmOverwrite(true).SetParentWindow(window_.get());
    opts.AddFilter("UltraVideo project", "ultravideo");
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) SaveTo(path);
    });
    return false;   // the dialog decides later; callers that need the save to be done ask again
}

bool UltraVideoWindow::SaveTo(const std::string& path) {
    std::string error;
    if (!document_.Save(path, error)) {
        UltraCanvasAlert::Error(error, "Save project", nullptr, window_.get());
        return false;
    }
    history_.MarkClean();
    RefreshAppBar();
    Message("Saved " + PathToUtf8(PathFromUtf8(path).filename()) + ".");
    return true;
}

void UltraVideoWindow::ImportMedia() {
    FileDialogOptions opts;
    opts.SetTitle("Import media").SetParentWindow(window_.get());
    opts.AddFilter("Video, pictures and sound", MediaExtensions());
    UltraCanvasFileLoader::OpenMultipleFilesDialog(opts, [this](DialogResult result, const std::vector<std::string>& files) {
        if (result == DialogResult::OK && !files.empty()) ImportFiles(files);
    });
}

void UltraVideoWindow::MakeStory() {
    ConfirmThen([this]() {
        // what the bin holds goes in as a start
        std::vector<std::string> files;
        for (const MediaItem& m : document_.media) if (m.error.empty()) files.push_back(m.path);
        const bool dark = dark_;
        ShowStoryWizard(window_.get(), *palette_, files, [this, dark](const StoryChoice& choice) {
            Message("Planning the story\xE2\x80\xA6");
            auto segments = std::make_shared<std::vector<VideoFXSegment>>();
            auto settings = std::make_shared<VideoFXExportSettings>();
            auto error = std::make_shared<std::string>();
            auto ok = std::make_shared<bool>(false);
            preview_.Run(
                [choice, segments, settings, error, ok]() { *ok = PlanStory(choice, *segments, *settings, *error); },
                [this, choice, segments, settings, error, ok, dark]() {
                    if (!*ok) {
                        UltraCanvasAlert::Error("The story could not be planned: " + *error, "Make a story", nullptr, window_.get());
                        Message("The story was not made.");
                        return;
                    }
                    Document story = BuildStoryDocument(choice, *segments, *settings);
                    story.view.dark = dark;
                    SetDocument(std::move(story));
                    if (timeline_) timeline_->ZoomToFit();
                    Message("The story is on the timeline: change anything, then export.");
                });
        });
    });
}

void UltraVideoWindow::ImportFiles(const std::vector<std::string>& files, double at) {
    if (files.empty()) return;
    if (files.size() == 1 && IsProjectFile(files.front())) {
        ConfirmThen([this, path = files.front()]() { OpenProjectFile(path); });
        return;
    }
    std::vector<std::string> paths;
    for (const std::string& f : files) {
        std::error_code ec;
        if (fs::is_directory(PathFromUtf8(f), ec)) {
            for (const auto& entry : fs::directory_iterator(PathFromUtf8(f), ec)) {
                if (entry.is_regular_file(ec) && Document::KindOfPath(PathToUtf8(entry.path())) != MediaKind::Unknown)
                    paths.push_back(PathToUtf8(entry.path()));
            }
        } else {
            paths.push_back(f);
        }
    }
    std::sort(paths.begin(), paths.end());
    const bool onto = at >= 0.0 || document_.project.segments.empty();
    Edit(paths.size() == 1 ? "Import " + PathToUtf8(PathFromUtf8(paths.front()).filename()) : "Import " + std::to_string(paths.size()) + " files",
         [paths, onto, at](Document& d) {
             size_t index = d.project.segments.size();
             if (at >= 0.0) {
                 TimelineModel model = BuildTimelineModel(d);
                 if (const TimelineTrack* main = model.FindTrack(kMainTrackId)) {
                     index = 0;
                     for (const TimelineClip& c : main->clips) if (c.start + c.duration / 2.0 < at) ++index;
                 }
             }
             for (const std::string& p : paths) {
                 const MediaItem& item = d.Import(p);
                 if (onto && item.error.empty() && item.kind != MediaKind::Audio) {
                     const int placed = d.InsertMedia(index, item);
                     if (placed >= 0) index = static_cast<size_t>(placed) + 1;
                 } else if (onto && item.error.empty() && item.kind == MediaKind::Audio) {
                     d.AppendMedia(item);   // sound becomes music
                 }
             }
         });
    size_t unreadable = 0;
    for (const std::string& p : paths) if (const MediaItem* m = document_.FindMedia(p)) if (!m->error.empty()) ++unreadable;
    if (unreadable > 0) Message(std::to_string(unreadable) + " of the files cannot be read; the bin shows them with the reason.");
    else Message(paths.size() == 1 ? "Imported." : "Imported " + std::to_string(paths.size()) + " files.");
}

void UltraVideoWindow::BeginBinDrag(const std::string& path) {
    const MediaItem* m = document_.FindMedia(path);
    if (!m || !m->error.empty()) return;
    binDrag_ = path;
    Message(m->kind == MediaKind::Audio ? "Drop it on the music lane to make it the music, or on Voice & sound (A2) at a time."
                                        : "Drop it on the main track at a time, or on the overlay lane to put it over the picture.");
}

// The element's drop path takes files from the window system; a bin tile
// is carried to it the same way, as synthetic drag events with the file
void UltraVideoWindow::DragToTimeline(const UCEvent& source, bool drop) {
    if (!timeline_ || binDrag_.empty()) return;
    const Point2Df inWindow(static_cast<float>(source.pointerWindow.x), static_cast<float>(source.pointerWindow.y));
    UCEvent e = source;
    e.targetElement = timeline_.get();
    e.droppedFiles = {binDrag_};
    const Point2Df local = timeline_->MapToLocal(inWindow);
    e.pointer = Point2Di(static_cast<int>(local.x), static_cast<int>(local.y));
    if (!timeline_->IsVisible() || !timeline_->ContainsInWindow(inWindow)) {
        e.type = UCEventType::DragLeave;
        timeline_->OnEvent(e);
        if (drop) Message("Dropped outside the timeline: nothing added.");
        return;
    }
    e.type = drop ? UCEventType::Drop : UCEventType::DragOver;
    timeline_->OnEvent(e);
}

void UltraVideoWindow::AddMediaToTimeline(const std::string& path, double at) {
    const MediaItem* m = document_.FindMedia(path);
    if (!m || !m->error.empty()) {
        Message("That file cannot be read.");
        return;
    }
    const MediaItem item = *m;
    Edit(item.kind == MediaKind::Audio ? "Add music" : "Add " + item.name, [item, at](Document& d) {
        size_t index = d.project.segments.size();
        if (at >= 0.0) {
            TimelineModel model = BuildTimelineModel(d);
            if (const TimelineTrack* main = model.FindTrack(kMainTrackId)) {
                index = 0;
                for (const TimelineClip& c : main->clips) if (c.start + c.duration / 2.0 < at) ++index;
            }
        }
        d.InsertMedia(index, item);
    });
}

// ============================================================================
// playback and the playhead
// ============================================================================
void UltraVideoWindow::SetPlayhead(double seconds, bool fromTimeline) {
    const double length = document_.Length();
    seconds = std::clamp(seconds, 0.0, std::max(0.0, length));
    document_.view.playhead = seconds;
    if (!fromTimeline && timeline_) timeline_->SetPlayhead(seconds);
    if (playheadField_ && !playheadField_->IsEditing()) playheadField_->SetSeconds(seconds);
    viewer_.SetTime(seconds, length);
    RequestFrame();
}

void UltraVideoWindow::RequestFrame() {
    frameWanted_ = true;
}

void UltraVideoWindow::StepFrames(long long frames) {
    const double fps = document_.FrameRate();
    const double now = TimelineToFrame(document_.view.playhead, fps);
    SetPlayhead(now + frames / fps, false);
}

void UltraVideoWindow::TogglePlay() {
    if (playing_) {
        StopPlayback();
        return;
    }
    if (document_.project.segments.empty()) return;
    const double length = document_.Length();
    if (document_.view.playhead >= length - 1e-6) SetPlayhead(0.0, false);
    playing_ = true;
    playFrom_ = document_.view.playhead;
    playStarted_ = std::chrono::steady_clock::now();
    viewer_.SetPlaying(true);
    if (timeline_) timeline_->SetFollowPlayhead(true);
    // With sound when the draft of this very edit is ready; else frame by
    // frame while it renders, and over to the draft the moment it is
    if (!draftPath_.empty() && draftRevision_ == documentRevision_) StartDraftPlayback();
    else RenderDraft();
}

void UltraVideoWindow::StopPlayback() {
    if (!playing_) return;
    playing_ = false;
    viewer_.SetPlaying(false);
    if (timeline_) timeline_->SetFollowPlayhead(false);
    if (draftPlaying_ || draftStarting_) {
        const double at = draftPlaying_ ? viewer_.DraftPosition() : document_.view.playhead;
        viewer_.DraftPause();
        viewer_.HideDraft();
        draftPlaying_ = false;
        draftStarting_ = false;
        SetPlayhead(at, false);        // the surface shows the frame the sound stopped at
    }
}

void UltraVideoWindow::RenderDraft() {
    if (draftUnavailable_) return;
    if (draftJob_ && draftJob_->IsRunning()) return;
    if (!draftPath_.empty() && draftRevision_ == documentRevision_) return;
    std::vector<VideoFXSegment> segments = SegmentsToPlay();
    if (segments.empty()) return;
    VideoFXExportSettings settings = VideoFXExportSettings::Draft(360);
    settings.music = document_.project.settings.music;
    PutSounds(document_, settings);
    settings.fitMode = document_.project.settings.fitMode;
    settings.frameRate = document_.FrameRate();
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec) {
        Message("No place for a draft render: playing frame by frame, without sound.");
        return;
    }
    const std::string name = "ultravideo-draft-" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000)) +
                             "-" + std::to_string(++draftFiles_) + ".mp4";
    RemoveDraftFile(draftPending_);
    draftPending_ = PathToUtf8(dir / PathFromUtf8(name));
    draftJob_ = std::make_unique<VideoFXExportJob>();
    draftRevision_ = documentRevision_;
    if (!draftJob_->Start(segments, draftPending_, settings)) {
        Message("The draft render could not start (" + draftJob_->GetError() + "): playing without sound.");
        draftJob_.reset();
        draftPending_.clear();
        return;
    }
    Message("Rendering a draft for playback with sound; playing frame by frame until it is ready.");
}

void UltraVideoWindow::PollDraft() {
    if (draftStarting_) {
        // the player has the file: start it once it is ready, give up if it
        // cannot (an error, or nothing loaded after a few seconds)
        const VideoPlaybackState state = viewer_.DraftState();
        const bool failed = state == VideoPlaybackState::Error ||
                            (state == VideoPlaybackState::Idle && ++draftStartTicks_ > 250);
        if (failed) {
            viewer_.HideDraft();
            draftStarting_ = false;
            RemoveDraftFile(draftPath_);
            draftPath_.clear();
            draftUnavailable_ = true;
            Message("The player cannot play the draft here: playing frame by frame, without sound.");
            playFrom_ = document_.view.playhead;
            playStarted_ = std::chrono::steady_clock::now();
        } else if (state != VideoPlaybackState::Loading && state != VideoPlaybackState::Buffering &&
                   state != VideoPlaybackState::Idle) {
            viewer_.DraftSeek(document_.view.playhead);
            viewer_.DraftPlay();
            draftStarting_ = false;
            draftPlaying_ = true;
        }
    }
    if (!draftJob_ || draftJob_->IsRunning()) return;
    const VideoFXResult result = draftJob_->GetResult();
    const std::string error = draftJob_->GetError();
    draftJob_.reset();
    const std::string file = draftPending_;
    draftPending_.clear();
    if (result != VideoFXResult::Ok) {
        RemoveDraftFile(file);
        if (result != VideoFXResult::Cancelled)
            Message("The draft render failed (" + (error.empty() ? VideoFX_ResultToString(result) : error) + "): playing without sound.");
        return;
    }
    if (draftRevision_ != documentRevision_) {   // edited meanwhile: stale before it was done
        RemoveDraftFile(file);
        if (playing_) RenderDraft();
        return;
    }
    RemoveDraftFile(draftPath_);
    draftPath_ = file;
    if (playing_ && !draftPlaying_ && !draftStarting_) StartDraftPlayback();
    else if (!playing_) Message("The draft is ready: Play now runs with sound.");
}

void UltraVideoWindow::StartDraftPlayback() {
    if (draftPath_.empty()) return;
    if (!viewer_.ShowDraft(draftPath_)) {
        Message("The player cannot open the draft here: playing frame by frame, without sound.");
        RemoveDraftFile(draftPath_);
        draftPath_.clear();
        draftUnavailable_ = true;      // no more drafts this session
        return;
    }
    draftStarting_ = true;
    draftStartTicks_ = 0;
    Message("Playing with sound.");
}

// Drafts are removed when the window closes; ones a crash or a kill left
// behind go when they are an hour old
void UltraVideoWindow::SweepOldDrafts() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec) return;
    const auto now = fs::file_time_type::clock::now();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string name = PathToUtf8(entry.path().filename());
        if (name.rfind("ultravideo-draft-", 0) != 0) continue;
        const auto written = entry.last_write_time(ec);
        if (!ec && now - written > std::chrono::hours(1)) fs::remove(entry.path(), ec);
    }
}

void UltraVideoWindow::InvalidateDraft() {
    ++documentRevision_;
    if (draftJob_ && draftJob_->IsRunning()) draftJob_->Cancel();   // PollDraft drops its file
    if (draftPlaying_ || draftStarting_) {
        // the picture changed under the sound: back to frames from where it was
        const double at = draftPlaying_ ? viewer_.DraftPosition() : document_.view.playhead;
        viewer_.DraftPause();
        viewer_.HideDraft();
        draftPlaying_ = false;
        draftStarting_ = false;
        document_.view.playhead = at;
        playFrom_ = at;
        playStarted_ = std::chrono::steady_clock::now();
        if (playing_) RenderDraft();
    }
    RemoveDraftFile(draftPath_);
    draftPath_.clear();
}

void UltraVideoWindow::RemoveDraftFile(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    fs::remove(PathFromUtf8(path), ec);
}

std::vector<VideoFXSegment> UltraVideoWindow::SegmentsToPlay() const {
    std::vector<VideoFXSegment> segments = document_.SegmentsForEngine();
    return document_.view.useProxies ? proxies_.Use(segments) : segments;
}

void UltraVideoWindow::ProxyDone(const std::string& clip, ProxyMaker::State state) {
    RefreshBin();
    if (state == ProxyMaker::State::Failed) {
        const MediaItem* m = document_.FindMedia(clip);
        Message("No proxy for " + (m ? m->name : clip) + " (" + proxies_.Error(clip) +
                "): it plays from the original.");
    }
    if (state != ProxyMaker::State::Ready) return;
    ++proxiesMade_;
    // a clip on the timeline: the viewer plays from the proxy from now on;
    // a draft already rendered stays (it is the same picture)
    bool used = false;
    for (const VideoFXSegment& s : document_.project.segments) used = used || s.path == clip;
    for (const OverlaySpan& o : document_.overlays) used = used || o.path == clip;
    if (used) {
        RefreshPreview();
        RequestFrame();
    }
    if (proxies_.Pending() == 0) {
        Message(proxiesMade_ == 1 ? "A proxy is ready: the viewer plays the clip from it."
                                  : std::to_string(proxiesMade_) + " proxies are ready: the viewer plays the clips from them.");
        proxiesMade_ = 0;
    }
}

void UltraVideoWindow::SetUseProxies(bool on) {
    if (document_.view.useProxies == on) return;
    document_.view.useProxies = on;
    InvalidateDraft();
    RefreshBin();
    RefreshPreview();
    RequestFrame();
    Message(on ? "The viewer plays clips from their proxies (the export uses the originals)."
               : "The viewer plays the original clips.");
}

void UltraVideoWindow::Tick() {
    preview_.Pump();
    proxies_.Pump();
    PollDraft();
    if (playing_ && draftPlaying_) {
        const double at = viewer_.DraftPosition();
        const double length = document_.Length();
        const VideoPlaybackState state = viewer_.DraftState();
        if (at >= length - 0.02 || state == VideoPlaybackState::Stopped || state == VideoPlaybackState::Idle ||
            state == VideoPlaybackState::Error) {
            StopPlayback();
            SetPlayhead(length, false);
        } else {
            SetPlayhead(at, false);
        }
    } else if (playing_) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - playStarted_).count();
        const double at = playFrom_ + elapsed;
        const double length = document_.Length();
        if (at >= length) {
            SetPlayhead(length, false);
            StopPlayback();
        } else {
            SetPlayhead(at, false);
        }
    }
    // One frame request per tick at most, and never a repeat of the same
    // time; none while the draft's picture is on
    if (frameWanted_ && !preview_.IsBusy() && !draftPlaying_ && !draftStarting_) {
        frameWanted_ = false;
        if (!document_.project.segments.empty() && preview_.IsOpen()) {
            preview_.RequestFrame(document_.view.playhead, viewer_.SurfaceWidth(), viewer_.SurfaceHeight());
            lastRequested_ = document_.view.playhead;
        }
    }
    if (job_) PollExport();
    if (messageTicks_ > 0 && --messageTicks_ == 0) RefreshAppBar();
}

// ============================================================================
// the timeline toolbar
// ============================================================================
void UltraVideoWindow::SetTool(TimelineTool tool) {
    if (timeline_) timeline_->SetTool(tool);
    RefreshToolbarLooks();
}

void UltraVideoWindow::RefreshToolbarLooks() {
    if (!timeline_) return;
    const Palette& p = *palette_;
    const TimelineTool tool = timeline_->GetTool();
    SetToggleLook(*toolSelect_, tool == TimelineTool::Select, p);
    SetToggleLook(*toolBlade_, tool == TimelineTool::Blade, p);
    SetToggleLook(*toolTrim_, tool == TimelineTool::Trim, p);
    SetToggleLook(*toolSlip_, tool == TimelineTool::Slip, p);
    SetToggleLook(*magnet_, true, p);
    SetToggleLook(*snap_, snapping_, p);
    SetToggleLook(*beats_, showBeats_, p);
}

void UltraVideoWindow::SplitAtPlayhead() {
    if (!timeline_) return;
    const double at = document_.view.playhead;
    TimelineModel model = BuildTimelineModel(document_);
    const TimelineTrack* main = model.FindTrack(kMainTrackId);
    if (!main) return;
    if (document_.TitleIndex(selectedClip_) >= 0) main = model.FindTrack(kTitleTrackId);   // a selected title splits instead
    if (document_.OverlayIndex(selectedClip_) >= 0) main = model.FindTrack(kOverlayTrackId);   // or overlay
    if (document_.SoundIndex(selectedClip_) >= 0) main = model.FindTrack(kSoundTrackId);       // or sound
    if (!main) return;
    std::string target = selectedClip_;
    if (target.empty() || !main->FindClip(target)) {
        for (const TimelineClip& c : main->clips) if (at > c.start && at < c.End()) target = c.id;
    }
    const TimelineClip* clip = main->FindClip(target);
    if (!clip || at <= clip->start || at >= clip->End()) {
        Message("Put the playhead inside a clip to split it.");
        return;
    }
    TimelineTrack track = *main;
    if (!TimelineSplitClip(track, target, at, document_.FrameRate(), target + ":2")) {
        Message("The split is too close to the clip's edge.");
        return;
    }
    TimelineChange change;
    change.kind = TimelineChangeKind::Split;
    change.trackId = main->id;
    change.before = {*clip};
    change.after = {*track.FindClip(target), *track.FindClip(target + ":2")};
    change.at = at;
    Edit(main->id == kTitleTrackId ? "Split title" : main->id == kOverlayTrackId ? "Split overlay"
         : main->id == kSoundTrackId ? "Split sound" : "Split clip",
         [&change](Document& d) { ApplyChange(d, change); });
}

void UltraVideoWindow::DeleteSelection() {
    if (!timeline_) return;
    std::vector<std::string> ids = timeline_->GetSelection();
    if (ids.empty() && !selectedClip_.empty()) ids.push_back(selectedClip_);
    if (ids.empty()) {
        Message("Select a clip first.");
        return;
    }
    Edit(ids.size() > 1 ? "Delete clips" : "Delete clip", [ids](Document& d) {
        std::vector<int> songs;
        for (const std::string& id : ids) {
            const int song = SongOfClip(id);
            if (song >= 0) songs.push_back(song);
        }
        std::sort(songs.begin(), songs.end(), std::greater<int>());
        for (int song : songs) RemoveSong(d, static_cast<size_t>(song));
        for (const std::string& id : ids) {
            if (SongOfClip(id) >= 0) continue;
            const int titleIndex = d.TitleIndex(id);
            if (titleIndex >= 0) {
                d.titles.erase(d.titles.begin() + titleIndex);
                continue;
            }
            const int overlayIndex = d.OverlayIndex(id);
            if (overlayIndex >= 0) {
                d.overlays.erase(d.overlays.begin() + overlayIndex);
                continue;
            }
            const int soundIndex = d.SoundIndex(id);
            if (soundIndex >= 0) {
                d.sounds.erase(d.sounds.begin() + soundIndex);
                continue;
            }
            const int index = SegmentOfClip(d, id);
            if (index < 0) continue;
            d.project.segments.erase(d.project.segments.begin() + index);
            d.clipIds.erase(d.clipIds.begin() + index);
        }
        if (!d.project.segments.empty()) d.project.segments.front().transitionIn = VideoFXTransition{};
    });
    selectedClip_.clear();
}

void UltraVideoWindow::AddTitle(const std::string& styleKey, double at) {
    std::string id;
    Edit("Add title", [styleKey, at, &id](Document& d) {
        const int index = d.AddTitle(styleKey, at);
        if (index >= 0) id = d.titles[static_cast<size_t>(index)].id;
    });
    if (!id.empty()) {
        selectedClip_ = id;
        if (timeline_) timeline_->SetSelection({id});
        RefreshInspector();
        inspector_.ShowClipTab();
        Message("A title at the playhead: set its text in the inspector.");
    }
}

void UltraVideoWindow::ApplyEffect(const std::string& key) {
    const EffectPreset* preset = FindEffectPreset(key);
    if (!preset) return;
    // a main-track clip, or an overlay clip's own picture
    const EffectTarget target{SegmentOfClip(document_, selectedClip_), document_.OverlayIndex(selectedClip_)};
    if (target.segment < 0 && target.overlay < 0) {
        Message("Select a clip on the main track or the overlay lane first: an effect goes on a clip.");
        return;
    }
    Document trial = document_;
    if (!trial.ApplyEffectPreset(target, key)) {
        Message(std::string(preset->label) + " is already on this clip.");
        return;
    }
    Edit(std::string("Apply ") + preset->label, [target, key](Document& d) { d.ApplyEffectPreset(target, key); });
    inspector_.ShowClipTab();
    const std::string name = target.segment >= 0 ? ClipName(document_, static_cast<size_t>(target.segment))
                                                 : document_.overlays[static_cast<size_t>(target.overlay)].Name();
    Message(std::string(preset->label) + " on " + name + ": the inspector lists its effects.");
}

void UltraVideoWindow::AddOverlay(const std::string& path, double at) {
    if (document_.project.segments.empty()) {
        Message("Put a clip on the main track first: an overlay sits over the picture.");
        return;
    }
    std::string id;
    Edit("Add overlay", [path, at, &id](Document& d) {
        const int index = d.AddOverlay(path, at);
        if (index >= 0) id = d.overlays[static_cast<size_t>(index)].id;
    });
    if (id.empty()) {
        Message("That file cannot go over the picture: it has no picture of its own.");
        return;
    }
    selectedClip_ = id;
    if (timeline_) timeline_->SetSelection({id});
    RefreshInspector();
    inspector_.ShowClipTab();
    Message("Over the picture, bottom right: place and size it in the inspector.");
}

void UltraVideoWindow::ChooseOverlay(double at) {
    FileDialogOptions opts;
    opts.SetTitle("A file over the picture").SetParentWindow(window_.get());
    std::vector<std::string> exts;
    for (const std::string& e : MediaExtensions())
        if (Document::KindOfPath("x." + e) != MediaKind::Audio) exts.push_back(e);
    opts.AddFilter("Video and pictures", exts);
    UltraCanvasFileLoader::OpenFileDialog(opts, [this, at](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) AddOverlay(path, at);
    });
}

void UltraVideoWindow::AddSound(const std::string& path, double at, bool voiceOver) {
    std::string id;
    Edit(voiceOver ? "Add voice-over" : "Add sound effect", [path, at, voiceOver, &id](Document& d) {
        const int index = d.AddSound(path, at, voiceOver);
        if (index >= 0) id = d.sounds[static_cast<size_t>(index)].id;
    });
    if (id.empty()) {
        Message("That file has no sound to put on the timeline.");
        return;
    }
    selectedClip_ = id;
    if (timeline_) timeline_->SetSelection({id});
    RefreshInspector();
    inspector_.ShowClipTab();
    Message(voiceOver ? "A voice-over on A2: the music dips under it. Volume and fades are in the inspector."
                      : "A sound effect on A2, over the music. Volume and fades are in the inspector.");
}

void UltraVideoWindow::ChooseSound(double at, bool voiceOver) {
    FileDialogOptions opts;
    opts.SetTitle(voiceOver ? "A voice-over" : "A sound effect").SetParentWindow(window_.get());
    std::vector<std::string> exts;
    for (const std::string& e : MediaExtensions())
        if (Document::KindOfPath("x." + e) != MediaKind::Image) exts.push_back(e);
    opts.AddFilter("Sound, and video with sound", exts);
    UltraCanvasFileLoader::OpenFileDialog(opts, [this, at, voiceOver](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) AddSound(path, at, voiceOver);
    });
}

void UltraVideoWindow::PutSounds(const Document& document, VideoFXExportSettings& settings) const {
    settings.audioClips = document.AudioClipsForEngine();
}

void UltraVideoWindow::RequestBeats() {
    const VideoFXMusic& music = document_.project.settings.music;
    if (!music.IsSet()) return;
    const std::string song = music.Songs().front();
    if (songBeats_.count(song) || beatsPending_ == song) return;
    beatsPending_ = song;
    auto info = std::make_shared<VideoFXBeatInfo>();
    preview_.Run([song, info]() { VideoFX_DetectBeats(song, *info); },
                 [this, song, info]() {
                     songBeats_[song] = info->beats;
                     if (beatsPending_ == song) beatsPending_.clear();
                     if (info->HasBeat()) {
                         char text[96];
                         std::snprintf(text, sizeof text, "Beat marks: %.0f beats a minute in %s.", info->bpm,
                                       PathToUtf8(PathFromUtf8(song).filename()).c_str());
                         Message(text);
                     } else {
                         Message("No steady beat found in " + PathToUtf8(PathFromUtf8(song).filename()) + ".");
                     }
                     RefreshTimeline();
                 });
}

void UltraVideoWindow::AddMarker() {
    const double at = document_.view.playhead;
    Edit("Add marker", [at](Document& d) {
        TimelineMarker m;
        m.id = "m" + std::to_string(d.markers.size() + 1) + "-" + std::to_string(static_cast<long long>(at * 1000));
        m.seconds = at;
        d.markers.push_back(m);
    });
}

void UltraVideoWindow::ShowTimelineMenu(const std::string& trackId, double seconds, const std::string& clipId, int x, int y) {
    menu_ = std::make_shared<UltraCanvasMenu>("uv.timelineMenu", 0, 0, 240, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    const int index = SegmentOfClip(document_, clipId);
    const int titleIndex = document_.TitleIndex(clipId);
    const int overlayIndex = document_.OverlayIndex(clipId);
    if (trackId == kOverlayTrackId && overlayIndex >= 0) {
        menu_->AddItem(MenuItemData::Header(document_.overlays[static_cast<size_t>(overlayIndex)].Name()));
        menu_->AddItem(MenuItemData::Action("Place, size and fades\xE2\x80\xA6", [this, clipId]() {
            selectedClip_ = clipId;
            RefreshInspector();
            inspector_.ShowClipTab();
        }));
        menu_->AddItem(MenuItemData::ActionWithShortcut("Split at the playhead", "S", [this, clipId]() {
            selectedClip_ = clipId;
            SplitAtPlayhead();
        }));
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut("Delete", "Del", [this, clipId]() {
            selectedClip_ = clipId;
            if (timeline_) timeline_->SetSelection({clipId});
            DeleteSelection();
        }));
    } else if (trackId == kOverlayTrackId) {
        menu_->AddItem(MenuItemData::Header("Overlay video"));
        menu_->AddItem(MenuItemData::Action("Put a file over the picture here\xE2\x80\xA6", [this, seconds]() { ChooseOverlay(seconds); }));
        size_t offered = 0;
        for (const MediaItem& m : document_.media) {
            if (!m.error.empty() || m.kind == MediaKind::Audio || m.kind == MediaKind::Unknown) continue;
            if (offered++ == 0) menu_->AddItem(MenuItemData::Separator());
            if (offered > 8) break;
            const std::string path = m.path;
            menu_->AddItem(MenuItemData::Action(m.name + " here", [this, path, seconds]() { AddOverlay(path, seconds); }));
        }
    } else if (trackId == kSoundTrackId && document_.SoundIndex(clipId) >= 0) {
        const SoundSpan& a = document_.sounds[static_cast<size_t>(document_.SoundIndex(clipId))];
        menu_->AddItem(MenuItemData::Header(a.Name()));
        menu_->AddItem(MenuItemData::Action("Volume, fades and ducking\xE2\x80\xA6", [this, clipId]() {
            selectedClip_ = clipId;
            RefreshInspector();
            inspector_.ShowClipTab();
        }));
        menu_->AddItem(MenuItemData::Action(a.ducksMusic ? "Make it a sound effect (the music stays)" : "Make it a voice-over (the music dips)",
                                            [this, clipId, ducks = !a.ducksMusic]() {
            Edit(ducks ? "Make voice-over" : "Make sound effect", [clipId, ducks](Document& d) {
                const int i = d.SoundIndex(clipId);
                if (i >= 0) d.sounds[static_cast<size_t>(i)].ducksMusic = ducks;
            });
        }));
        menu_->AddItem(MenuItemData::ActionWithShortcut("Split at the playhead", "S", [this, clipId]() {
            selectedClip_ = clipId;
            SplitAtPlayhead();
        }));
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut("Delete", "Del", [this, clipId]() {
            selectedClip_ = clipId;
            if (timeline_) timeline_->SetSelection({clipId});
            DeleteSelection();
        }));
    } else if (trackId == kSoundTrackId) {
        menu_->AddItem(MenuItemData::Header("Voice & sound"));
        menu_->AddItem(MenuItemData::Action("Add a voice-over here\xE2\x80\xA6", [this, seconds]() { ChooseSound(seconds, true); }));
        menu_->AddItem(MenuItemData::Action("Add a sound effect here\xE2\x80\xA6", [this, seconds]() { ChooseSound(seconds, false); }));
        size_t offered = 0;
        for (const MediaItem& m : document_.media) {
            if (!m.error.empty() || !(m.kind == MediaKind::Audio || (m.kind == MediaKind::Video && m.hasAudio))) continue;
            if (offered++ == 0) menu_->AddItem(MenuItemData::Separator());
            if (offered > 8) break;
            const std::string path = m.path;
            menu_->AddItem(MenuItemData::Action(m.name + " here", [this, path, seconds]() { AddSound(path, seconds, true); }));
        }
    } else if (trackId == kTitleTrackId && titleIndex >= 0) {
        menu_->AddItem(MenuItemData::Header(document_.titles[static_cast<size_t>(titleIndex)].Name()));
        menu_->AddItem(MenuItemData::Action("Edit the text\xE2\x80\xA6", [this, clipId]() {
            selectedClip_ = clipId;
            RefreshInspector();
            inspector_.ShowClipTab();
        }));
        menu_->AddItem(MenuItemData::ActionWithShortcut("Split at the playhead", "S", [this, clipId]() {
            selectedClip_ = clipId;
            SplitAtPlayhead();
        }));
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut("Delete", "Del", [this, clipId]() {
            selectedClip_ = clipId;
            if (timeline_) timeline_->SetSelection({clipId});
            DeleteSelection();
        }));
    } else if (trackId == kTitleTrackId) {
        menu_->AddItem(MenuItemData::Header("Titles"));
        for (const TitleStyle& st : TitleStyles()) {
            const std::string key = st.key;
            menu_->AddItem(MenuItemData::Action(std::string(st.label) + " here", [this, key, seconds]() { AddTitle(key, seconds); }));
        }
    } else if (trackId == kMusicTrackId && SongOfClip(clipId) >= 0) {
        const int song = SongOfClip(clipId);
        const std::vector<std::string> songs = document_.project.settings.music.Songs();
        const std::string name = static_cast<size_t>(song) < songs.size() ? PathToUtf8(PathFromUtf8(songs[static_cast<size_t>(song)]).filename()) : "Song";
        menu_->AddItem(MenuItemData::Header(name));
        menu_->AddItem(MenuItemData::Action("Volume, fades and ducking\xE2\x80\xA6", [this, clipId]() {
            selectedClip_ = clipId;
            RefreshInspector();
            inspector_.ShowAudioTab();
        }));
        menu_->AddItem(MenuItemData::Action("Add a song after it\xE2\x80\xA6", [this]() { ImportMedia(); }));
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut("Remove this song", "Del", [this, song]() {
            Edit("Remove song", [song](Document& d) { RemoveSong(d, static_cast<size_t>(song)); });
            selectedClip_.clear();
        }));
    } else if (trackId == kMainTrackId && index >= 0) {
        const VideoFXSegment& s = document_.project.segments[static_cast<size_t>(index)];
        menu_->AddItem(MenuItemData::Header(ClipName(document_, static_cast<size_t>(index))));
        menu_->AddItem(MenuItemData::ActionWithShortcut("Split at the playhead", "S", [this, clipId]() {
            selectedClip_ = clipId;
            SplitAtPlayhead();
        }));
        menu_->AddItem(MenuItemData::ActionWithShortcut("Duplicate", "Ctrl+D", [this, index]() {
            Edit("Duplicate clip", [index](Document& d) {
                if (static_cast<size_t>(index) >= d.project.segments.size()) return;
                VideoFXSegment copy = d.project.segments[static_cast<size_t>(index)];
                copy.transitionIn = VideoFXTransition{};
                d.project.segments.insert(d.project.segments.begin() + index + 1, copy);
                d.clipIds.insert(d.clipIds.begin() + index + 1, d.NewClipId());
            });
        }));
        menu_->AddItem(MenuItemData::Action(s.mute ? "Unmute" : "Mute", [this, index, muted = !s.mute]() {
            Edit(muted ? "Mute clip" : "Unmute clip", [index, muted](Document& d) {
                if (static_cast<size_t>(index) < d.project.segments.size()) d.project.segments[static_cast<size_t>(index)].mute = muted;
            });
        }));
        if (index > 0) {
            menu_->AddItem(MenuItemData::Separator());
            if (s.transitionIn.IsCut()) {
                menu_->AddItem(MenuItemData::Action("Cross dissolve into it (1 s)", [this, index]() {
                    Edit("Set transition", [index](Document& d) {
                        if (static_cast<size_t>(index) < d.project.segments.size())
                            d.project.segments[static_cast<size_t>(index)].transitionIn = VideoFXTransition::Crossfade(1.0);
                    });
                }));
            } else {
                menu_->AddItem(MenuItemData::Action("Change the transition\xE2\x80\xA6", [this, clipId]() {
                    selectedClip_ = clipId;
                    RefreshInspector();
                    inspector_.ShowClipTab();
                }));
                menu_->AddItem(MenuItemData::Action("Remove the transition (back to a cut)", [this, index]() {
                    Edit("Remove transition", [index](Document& d) {
                        if (static_cast<size_t>(index) < d.project.segments.size())
                            d.project.segments[static_cast<size_t>(index)].transitionIn = VideoFXTransition{};
                    });
                }));
            }
        }
        menu_->AddItem(MenuItemData::Separator());
        menu_->AddItem(MenuItemData::ActionWithShortcut("Delete", "Del", [this, clipId]() {
            selectedClip_ = clipId;
            if (timeline_) timeline_->SetSelection({clipId});
            DeleteSelection();
        }));
    } else {
        menu_->AddItem(MenuItemData::Header(trackId == kMusicTrackId ? "Music" : "Main track"));
        if (trackId == kMusicTrackId) {
            menu_->AddItem(MenuItemData::Action("Add a song\xE2\x80\xA6", [this]() { ImportMedia(); }));
            if (document_.project.settings.music.IsSet())
                menu_->AddItem(MenuItemData::Action("Remove the music", [this]() {
                    Edit("Remove music", [](Document& d) { d.project.settings.music = VideoFXMusic{}; });
                }));
        } else {
            menu_->AddItem(MenuItemData::ActionWithShortcut("Import media here\xE2\x80\xA6", "Ctrl+I", [this, seconds]() {
                FileDialogOptions opts;
                opts.SetTitle("Import media").SetParentWindow(window_.get());
                opts.AddFilter("Video, pictures and sound", MediaExtensions());
                UltraCanvasFileLoader::OpenMultipleFilesDialog(opts, [this, seconds](DialogResult result, const std::vector<std::string>& files) {
                    if (result == DialogResult::OK && !files.empty()) ImportFiles(files, seconds);
                });
            }));
        }
        menu_->AddItem(MenuItemData::ActionWithShortcut("Add a marker", "M", [this, seconds]() {
            SetPlayhead(seconds, false);
            AddMarker();
        }));
    }
    PopupElementSettings settings;
    menu_->OpenMenu(Point2Di(x, y), *window_, settings);
}

void UltraVideoWindow::ShowBinMenu(size_t, int, int) {}

// ============================================================================
// export
// ============================================================================
void UltraVideoWindow::Export() {
    if (document_.project.segments.empty()) {
        Message("Put something on the timeline first.");
        return;
    }
    if (job_ && job_->IsRunning()) {
        Message("An export is already running.");
        return;
    }
    ShowExportDialog(window_.get(), document_, *palette_, [this](const ExportChoice& choice) { StartExport(choice); });
}

void UltraVideoWindow::StartExport(const ExportChoice& choice) {
    Document exported = document_;
    if (choice.workAreaOnly && document_.view.rangeOut > document_.view.rangeIn) {
        // Keep what the work area covers: trim the clips at its edges, and
        // the titles with them
        const double rangeIn = document_.view.rangeIn, rangeOut = document_.view.rangeOut;
        TimelineModel model = BuildTimelineModel(document_);
        const TimelineTrack* main = model.FindTrack(kMainTrackId);
        std::vector<VideoFXSegment> kept;
        std::vector<std::string> keptIds;
        if (main) {
            for (size_t i = 0; i < main->clips.size() && i < document_.project.segments.size(); ++i) {
                const TimelineClip& c = main->clips[i];
                const double in = std::max(c.start, rangeIn);
                const double out = std::min(c.End(), rangeOut);
                if (out - in < 0.04) continue;
                VideoFXSegment s = document_.project.segments[i];
                if (s.kind == VideoFXSourceKind::File) {
                    s.start = c.sourceIn + (in - c.start) * c.speed;
                    s.end = c.sourceIn + (out - c.start) * c.speed;
                } else {
                    s.duration = out - in;
                }
                if (in > c.start) s.transitionIn = VideoFXTransition{};
                kept.push_back(s);
                keptIds.push_back(document_.clipIds[i]);
            }
        }
        if (!kept.empty()) kept.front().transitionIn = VideoFXTransition{};
        exported.project.segments = kept;
        exported.clipIds = keptIds;
        std::vector<TitleSpan> titles;
        for (TitleSpan t : document_.titles) {
            const double a = std::max(t.start, rangeIn), b = std::min(t.End(), rangeOut);
            if (b - a < 0.04) continue;
            t.start = a - rangeIn;
            t.duration = b - a;
            titles.push_back(t);
        }
        exported.titles = titles;
        std::vector<OverlaySpan> pips;
        for (OverlaySpan v : document_.overlays) {
            const double a = std::max(v.start, rangeIn), b = std::min(v.End(), rangeOut);
            if (b - a < 0.04) continue;
            v.sourceIn += a - v.start;
            v.start = a - rangeIn;
            v.duration = b - a;
            pips.push_back(v);
        }
        exported.overlays = pips;
        std::vector<SoundSpan> lane;
        for (SoundSpan a : document_.sounds) {
            const double from = std::max(a.start, rangeIn), to = std::min(a.End(), rangeOut);
            if (to - from < 0.04) continue;
            a.sourceIn += from - a.start;
            if (from > a.start) a.fadeIn = 0.0;
            if (to < a.End()) a.fadeOut = 0.0;
            a.start = from - rangeIn;
            a.duration = to - from;
            lane.push_back(a);
        }
        exported.sounds = lane;
    }
    std::vector<VideoFXSegment> segments = exported.SegmentsForEngine();
    if (segments.empty()) {
        Message("The work area holds no clip.");
        return;
    }
    VideoFXExportSettings settings = choice.settings;
    settings.music = document_.project.settings.music;
    PutSounds(exported, settings);
    if (settings.frameRate <= 0.0 && document_.project.settings.frameRate > 0.0) settings.frameRate = document_.project.settings.frameRate;
    settings.fitMode = document_.project.settings.fitMode;
    job_ = std::make_unique<VideoFXExportJob>();
    exportPath_ = choice.outputPath;
    if (!job_->Start(segments, exportPath_, settings)) {
        UltraCanvasAlert::Error("The export could not start: " + job_->GetError(), "Export", nullptr, window_.get());
        job_.reset();
        return;
    }
    progress_ = UltraCanvasProgressDialog::Show(window_.get(), "Exporting", PathToUtf8(PathFromUtf8(exportPath_).filename()),
                                                [this]() { if (job_) job_->Cancel(); });
    Message("Exporting\xE2\x80\xA6");
}

void UltraVideoWindow::PollExport() {
    if (!job_) return;
    if (job_->IsRunning()) {
        if (progress_) progress_->SetProgress(job_->GetProgress());
        return;
    }
    const VideoFXResult result = job_->GetResult();
    const std::string error = job_->GetError();
    job_.reset();
    if (progress_) {
        progress_->Close();
        progress_.reset();
    }
    if (result == VideoFXResult::Ok) {
        Message("Exported " + PathToUtf8(PathFromUtf8(exportPath_).filename()) + ".");
        UltraCanvasAlert::Successful("Saved " + exportPath_, "Export", nullptr, window_.get());
    } else if (result == VideoFXResult::Cancelled) {
        Message("Export cancelled.");
    } else {
        Message("Export failed.");
        UltraCanvasAlert::Error(error.empty() ? VideoFX_ResultToString(result) : error, "Export", nullptr, window_.get());
    }
}

// ============================================================================
// keys, drops, messages
// ============================================================================
bool UltraVideoWindow::TextHasFocus() const {
    UltraCanvasUIElement* focused = window_ ? window_->GetFocusedElement() : nullptr;
    return focused && (dynamic_cast<UltraCanvasTextInput*>(focused) || dynamic_cast<UltraCanvasTimecodeField*>(focused));
}

bool UltraVideoWindow::OnKey(const UCEvent& event) {
    if (event.type == UCEventType::Drop) {
        if (event.droppedFiles.empty()) return false;
        ImportFiles(event.droppedFiles);
        return true;
    }
    if (event.type != UCEventType::KeyDown) return false;
    const bool ctrl = event.ctrl || event.meta;
    if (ctrl) {
        switch (event.virtualKey) {
            case UCKeys::Z:
                if (event.shift) { if (history_.Redo(document_)) { RefreshAll(); } }
                else if (history_.Undo(document_)) { RefreshAll(); }
                return true;
            case UCKeys::Y: if (history_.Redo(document_)) RefreshAll(); return true;
            case UCKeys::S: if (event.shift) SaveProjectAs(); else SaveProject(); return true;
            case UCKeys::O: OpenProject(); return true;
            case UCKeys::N: if (event.shift) MakeStory(); else NewProject(); return true;
            case UCKeys::I: ImportMedia(); return true;
            case UCKeys::E: Export(); return true;
            default: break;
        }
        return false;
    }
    if (TextHasFocus()) return false;
    switch (event.virtualKey) {
        case UCKeys::Escape:
            if (!binDrag_.empty()) {   // a drag from the bin let go
                binDrag_.clear();
                UCEvent leave;
                leave.type = UCEventType::DragLeave;
                if (timeline_) timeline_->OnEvent(leave);
                Message("Drag cancelled.");
                return true;
            }
            return false;
        case UCKeys::Space: TogglePlay(); return true;
        case UCKeys::V: SetTool(TimelineTool::Select); return true;
        case UCKeys::B: SetTool(TimelineTool::Blade); return true;
        case UCKeys::T: SetTool(TimelineTool::Trim); return true;
        case UCKeys::Y: SetTool(TimelineTool::Slip); return true;
        case UCKeys::N: snapping_ = !snapping_; if (timeline_) timeline_->SetSnapping(snapping_); RefreshToolbarLooks(); return true;
        default: break;
    }
    return false;
}

void UltraVideoWindow::Message(const std::string& text) {
    if (status_) status_->SetText(text);
    messageTicks_ = kMessageTicks;
}

} // namespace UltraVideo
