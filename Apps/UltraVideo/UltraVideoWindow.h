// Apps/UltraVideo/UltraVideoWindow.h
// The editor window (Docs/Research/UltraVideoDesignProposal.md §6.1): the
// app bar with the project's name and state, undo, redo and Export; the
// media bin, the viewer and the inspector in two split panes; under them the
// timeline dock with its toolbar and the UltraCanvasVideoTimeline element.
// The window owns the document, its undo history and the engine's worker:
// every edit - from the timeline, the inspector or the bin - goes through
// Edit(), which snapshots the document, applies the change, records it and
// shows the document again everywhere (the timeline model, the preview, the
// inspector, the app bar).
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoDialogs.h"
#include "UltraVideoDocument.h"
#include "UltraVideoHistory.h"
#include "UltraVideoInspector.h"
#include "UltraVideoPreview.h"
#include "UltraVideoTheme.h"
#include "UltraVideoViewer.h"

#include "UltraCanvasAlbum.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasProgressDialog.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasVideoTimeline.h"
#include "UltraCanvasWindow.h"
#include "VideoFX/VideoFX.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraVideo {

class UltraVideoWindow {
public:
    explicit UltraVideoWindow(UltraCanvas::UltraCanvasApplication& app);
    ~UltraVideoWindow();

    // `paths`: a .ultravideo project to open, or media files to import into
    // a new project
    bool Create(const std::vector<std::string>& paths);
    void Show();

private:
    using Button = UltraCanvas::UltraCanvasButton;
    using Container = UltraCanvas::UltraCanvasContainer;
    using Label = UltraCanvas::UltraCanvasLabel;

    // building
    std::shared_ptr<Container> BuildAppBar();
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> BuildMediaBin();
    std::shared_ptr<Container> BuildTimelineDock();
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> BuildTimelineToolbar();
    void ApplyPalette();

    // the document
    void Edit(const std::string& label, const std::function<void(Document&)>& edit);
    void RefreshAll();                 // after any change of the document
    void RefreshAppBar();
    void RefreshBin();
    void RefreshTimeline();
    void RefreshInspector();
    void RefreshPreview();             // plan the timeline again in the engine
    void SetDocument(Document document);

    // files
    void NewProject();
    void OpenProject();
    void OpenProjectFile(const std::string& path);
    bool SaveProject();
    bool SaveProjectAs();
    bool SaveTo(const std::string& path);
    void ImportMedia();
    void ImportFiles(const std::vector<std::string>& files, double at = -1.0);
    void AddMediaToTimeline(const std::string& path, double at = -1.0);
    void ConfirmThen(std::function<void()> then);   // asks to save first when dirty

    // playback and the playhead
    void SetPlayhead(double seconds, bool fromTimeline);
    void RequestFrame();
    void TogglePlay();
    void StopPlayback();
    void StepFrames(long long frames);
    void Tick();

    // the timeline toolbar
    void SetTool(UltraCanvas::TimelineTool tool);
    void RefreshToolbarLooks();
    void SplitAtPlayhead();
    void DeleteSelection();
    void AddMarker();
    void ShowTimelineMenu(const std::string& trackId, double seconds, const std::string& clipId, int x, int y);
    void ShowBinMenu(size_t index, int x, int y);

    // export
    void Export();
    void StartExport(const ExportChoice& choice);
    void PollExport();

    bool OnKey(const UltraCanvas::UCEvent& event);
    bool TextHasFocus() const;
    void Message(const std::string& text);

    UltraCanvas::UltraCanvasApplication& app_;
    Document document_;
    History history_;
    Preview preview_;
    bool dark_ = true;
    const Palette* palette_ = &Tokens::Dark;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    // app bar
    std::shared_ptr<Container> appBar_;
    std::shared_ptr<Label> title_, status_;
    std::shared_ptr<Button> undo_, redo_, open_, save_, import_, theme_, export_;
    // panes
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> columns_, rows_;
    std::shared_ptr<Container> binPanel_, viewerPane_, inspectorPane_, dock_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> binTabs_, inspectorTabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> binSearch_;
    std::shared_ptr<UltraCanvas::UltraCanvasAlbum> binAlbum_, musicAlbum_;
    std::shared_ptr<Label> binEmpty_, titlesNote_, effectsNote_;
    std::vector<size_t> binRows_;      // album row -> media index
    std::vector<size_t> musicRows_;
    Viewer viewer_;
    Inspector inspector_;
    // dock
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> timelineBar_;
    std::shared_ptr<Button> toolSelect_, toolBlade_, toolTrim_, toolSlip_, split_, delete_, marker_, magnet_, snap_, beats_,
        zoomOut_, zoomIn_, zoomFit_;
    std::shared_ptr<UltraCanvas::UltraCanvasTimecodeField> playheadField_;
    std::shared_ptr<Label> lengthLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> zoom_;
    std::shared_ptr<UltraCanvas::UltraCanvasVideoTimeline> timeline_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;

    // playback
    UltraCanvas::TimerId timer_ = 0;
    bool playing_ = false;
    std::chrono::steady_clock::time_point playStarted_;
    double playFrom_ = 0.0;
    double lastRequested_ = -1.0;
    bool frameWanted_ = false;
    int messageTicks_ = 0;
    std::string selectedClip_;
    bool snapping_ = true;
    bool showBeats_ = false;

    // export
    std::unique_ptr<VideoFX::VideoFXExportJob> job_;
    std::shared_ptr<UltraCanvas::UltraCanvasProgressDialog> progress_;
    std::string exportPath_;
};

} // namespace UltraVideo
