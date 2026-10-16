// UltraCanvasVideoTimeline.h
// A media timeline editor: clips on tracks under a time ruler, with a
// playhead, trimming, reordering, splitting, transitions, markers, beat
// marks and a work-area range. It edits the module-free model in
// UltraCanvasVideoTimelineModel.h, draws thumbnails and waveforms that
// providers deliver later, and *reports* every edit through
// onChangeRequested rather than applying it: the application validates the
// change against its engine, applies it to its document and calls SetModel
// again. The element never plays media; the app links a player's position
// to SetPlayhead and onPlayheadChanged to the player's Seek.
//
// Tracks: the one MainVideo (and MainAudio) track is magnetic - its clips
// are a sequence, reordered rather than placed, gaps closed - and every other
// track places clips freely. That is the shape of an engine whose export is
// a list of segments (VideoFX) with free overlays and music beside it.
//
// Version: 1.2.0 - SetDropTargetClip: the clip an application-carried drop (an effect) would land on
// Version: 1.1.0 - ApplyTheme
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasTheme.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasVideoTimelineModel.h"
#include "UltraCanvasWaveformElement.h"   // WaveformChannelData, the peaks a provider delivers

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class TimelineTool { Select, Blade, Trim, Slip };

enum class TimelineTrackToggle { Magnetic, Visible, Muted, Locked };

// Colours, sizes and fonts. The defaults are the light palette of the
// UltraVideo design; DarkPalette() gives the dark one. Sizes are logical
// pixels, as everywhere in the framework.
struct VideoTimelineStyle {
    // surfaces and lines
    Color background      = Color(244, 245, 247);   // the body behind the lanes
    Color lane            = Color(236, 238, 241);
    Color laneAlternate   = Color(240, 242, 244);
    Color header          = Color(255, 255, 255);   // the track header column and the ruler
    Color headerSelected  = Color(246, 227, 198);
    Color line            = Color(211, 215, 220);
    Color lineStrong      = Color(122, 130, 139);
    Color ink             = Color(22, 24, 27);
    Color inkMuted        = Color(74, 81, 89);
    Color inkFaint        = Color(95, 102, 110);
    Color accent          = Color(166, 90, 0);
    Color accentSoft      = Color(246, 227, 198);
    Color onAccent        = Color(255, 255, 255);
    Color playhead        = Color(210, 52, 31);
    Color focus           = Color(11, 106, 166);
    Color danger          = Color(192, 50, 31);
    // clip kinds: fill and edge (the top stripe and border)
    Color clipVideo       = Color(205, 226, 238);
    Color clipVideoEdge   = Color(61, 124, 156);
    Color clipImage       = Color(239, 224, 191);
    Color clipImageEdge   = Color(143, 106, 29);
    Color clipCard        = Color(225, 228, 232);
    Color clipCardEdge    = Color(124, 131, 139);
    Color clipTitle       = Color(234, 217, 238);
    Color clipTitleEdge   = Color(135, 80, 143);
    Color clipAudio       = Color(207, 232, 216);
    Color clipAudioEdge   = Color(47, 122, 77);
    Color waveform        = Color(47, 122, 77);
    Color transitionFill  = Color(42, 46, 52);     // inverted, so the badge reads over any clip
    Color onTransition    = Color(244, 245, 247);
    // translucent guides
    Color beat            = Color(0, 0, 0, 41);
    Color trimOverflow    = Color(0, 0, 0, 20);
    Color dropSoft        = Color(11, 106, 166, 31);
    Color rangeFill       = Color(166, 90, 0, 36);
    Color mutedHatch      = Color(244, 245, 247, 153);
    Color scrollbar       = Color(122, 130, 139, 120);
    // sizes
    float rulerHeight     = 28.0f;
    float headerWidth     = 200.0f;
    float trackHeightVideo   = 64.0f;
    float trackHeightAudio   = 48.0f;
    float trackHeightOverlay = 28.0f;
    float trackHeightCollapsed = 24.0f;
    float clipBandHeight  = 16.0f;
    float handleWidth     = 8.0f;    // hit width of a trim handle; drawn 3 px wide
    float playheadWidth   = 2.0f;
    float scrollbarHeight = 8.0f;
    float snapTolerancePx = 8.0f;
    float minTransitionPx = 22.0f;
    float cornerRadius    = 4.0f;
    // fonts; sizes in pixels, as the sizes above
    std::string fontFamily = "Ubuntu";
    std::string monoFamily = "Ubuntu Mono";
    float labelSize   = 12.0f;
    float captionSize = 11.0f;
    float rulerSize   = 10.0f;
    float readoutSize = 12.0f;

    static VideoTimelineStyle LightPalette();
    static VideoTimelineStyle DarkPalette();
    // The colours from a theme: the shared roles for the surfaces, lines,
    // ink, accent, focus and danger, and the element's own roles - named
    // "video-timeline.<role>", one per colour below from `playhead` on
    // ("video-timeline.playhead", "video-timeline.clip-video-edge",
    // "video-timeline.beat" ...) - where the theme names them, else the
    // light or dark palette's. Sizes and fonts are the defaults.
    static VideoTimelineStyle FromTheme(const UltraCanvasTheme& theme);
    static VideoTimelineStyle FromTheme() { return FromTheme(UltraCanvasTheme::Current()); }
};

// A thumbnail provider: `frames` pictures spread over seconds `in`..`out` of
// `source`, each `height` pixels tall, delivered through `done` on the UI
// thread, at once or later. The element caches what it gets.
using TimelineThumbnailProvider =
    std::function<void(const std::string& source, double in, double out, int frames, int height,
                       std::function<void(std::vector<UCPixmapPtr>)> done)>;

// A peaks provider: the loudness envelope of seconds `in`..`out` of
// `source` at about `blocksPerSecond`, in the shape the waveform element
// draws (VideoFX_ExtractWaveform's VideoFXWaveform has the same members).
using TimelinePeaksProvider =
    std::function<void(const std::string& source, double in, double out, double blocksPerSecond,
                       std::function<void(WaveformChannelData)> done)>;

class UltraCanvasVideoTimeline : public UltraCanvasUIElement {
public:
    UltraCanvasVideoTimeline(const std::string& id, int x, int y, int w, int h);
    ~UltraCanvasVideoTimeline() override = default;

    // ===== MODEL =====
    // The app re-feeds the model after every change it applied. Selection,
    // zoom and the playhead survive a SetModel; a selected clip that is gone
    // leaves the selection.
    void SetModel(const TimelineModel& model);
    const TimelineModel& GetModel() const { return model; }

    // ===== VIEW =====
    void SetPixelsPerSecond(double pixelsPerSecond);   // zoom; clamped to 2..2000
    double GetPixelsPerSecond() const { return pixelsPerSecond; }
    void ZoomToFit();                                   // the whole project in the body
    void ZoomBy(double factor, double aroundSeconds);   // keeps `aroundSeconds` where it is
    void SetScrollSeconds(double seconds);              // the time at the body's left edge
    double GetScrollSeconds() const { return scrollSeconds; }
    void SetPlayhead(double seconds);                   // from the player; fires no callback
    double GetPlayhead() const { return playhead; }
    void SetFollowPlayhead(bool follow) { followPlayhead = follow; }
    void SetTool(TimelineTool tool);
    TimelineTool GetTool() const { return tool; }
    void SetSnapping(bool on) { snapping = on; }
    bool GetSnapping() const { return snapping; }
    void SetShowBeats(bool on);
    bool GetShowBeats() const { return showBeats; }
    void SetStyle(const VideoTimelineStyle& s);
    const VideoTimelineStyle& GetStyle() const { return style; }
    void SetSelection(const std::vector<std::string>& clipIds);
    std::vector<std::string> GetSelection() const;
    void SetSelectedTransition(const std::string& clipId);   // "" clears
    std::string GetSelectedTransition() const { return selectedTransition; }
    void SetSelectedTrack(const std::string& trackId);
    std::string GetSelectedTrack() const { return selectedTrack; }

    // Seconds <-> body x (local coordinates), for an app drawing over the element
    double XToSeconds(float x) const;
    float SecondsToX(double seconds) const;
    // The clip under a local point, or "" (and the track, when `trackId` is given)
    std::string ClipAt(float x, float y, std::string* trackId = nullptr) const;
    // The clip a drop the application carries itself (an effect dragged
    // from its bin) would land on: drawn with a dashed focus ring until
    // cleared with "". The element's own drop path (files) does not use it.
    void SetDropTargetClip(const std::string& clipId);
    std::string GetDropTargetClip() const { return dropTargetClip; }

    // ===== PROVIDERS =====
    TimelineThumbnailProvider thumbnailProvider;
    TimelinePeaksProvider peaksProvider;
    void ClearCaches();   // forget every thumbnail and peak delivered so far

    // ===== CHANGES (report, never apply) =====
    // The app validates, applies to its document and calls SetModel. A
    // handler returning false means the edit was refused; the element then
    // shows the model as it is. With no handler every edit is refused.
    std::function<bool(const TimelineChange&)> onChangeRequested;
    std::function<void(const std::vector<std::string>& clipIds)> onSelectionChanged;
    std::function<void(double seconds)> onPlayheadChanged;            // the user moved it
    std::function<void(double in, double out)> onRangeChanged;        // I / O keys
    std::function<void(const std::string& clipId)> onClipActivated;   // double-click
    std::function<void(const std::string& clipId)> onTransitionActivated;   // double-click the badge
    std::function<void(const TimelineMarker& marker)> onMarkerActivated;    // double-click on the ruler
    std::function<void(const std::vector<std::string>& files, const std::string& trackId, double seconds)> onFilesDropped;
    std::function<void(const std::string& trackId, double seconds, const std::string& clipId, int windowX, int windowY)>
        onContextMenuRequested;
    std::function<void(const std::string& trackId, TimelineTrackToggle toggle, bool on)> onTrackToggled;
    std::function<void(double pixelsPerSecond)> onZoomChanged;
    std::function<void(TimelineTool tool)> onToolChanged;

    // ===== UIElement OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void ApplyTheme(const UltraCanvasTheme& theme) override { SetStyle(VideoTimelineStyle::FromTheme(theme)); }
    bool AcceptsFocus() const override { return true; }
    UCMouseCursor GetMouseCursor() const override;
    AccessibleRole GetAccessibleRole() const override { return AccessibleRole::List; }

private:
    // ---- geometry ----
    struct TrackLayout {
        size_t index = 0;        // into model.tracks
        float top = 0.0f;        // local y of the lane
        float height = 0.0f;
    };
    struct Hit {
        enum Kind { Nothing, Ruler, Marker, HeaderName, HeaderToggle, Lane, ClipBody, ClipLeftEdge, ClipRightEdge,
                    FadeInKnob, FadeOutKnob,   // an audio clip's fade handles, at the top of its ramps
                    TransitionBody, TransitionLeftEdge, TransitionRightEdge, Scrollbar } kind = Nothing;
        bool IsClip() const {
            return kind == ClipBody || kind == ClipLeftEdge || kind == ClipRightEdge || kind == FadeInKnob || kind == FadeOutKnob;
        }
        size_t trackIndex = 0;
        std::string clipId;
        std::string markerId;
        TimelineTrackToggle toggle = TimelineTrackToggle::Visible;
        double seconds = 0.0;
    };
    enum class Drag { None, Playhead, MoveClip, TrimLeft, TrimRight, FadeIn, FadeOut, Transition, RubberBand, Scrollbar };

    Rect2Df BodyRect() const;          // the lanes and the ruler, right of the header column
    Rect2Df LanesRect() const;         // the lanes only
    std::vector<TrackLayout> LayoutTracks() const;
    float TrackDefaultHeight(const TimelineTrack& track) const;
    Hit HitTest(float x, float y) const;
    Rect2Df ClipRect(const TimelineClip& clip, const TrackLayout& lane) const;
    // An audio clip's fade knobs: the top of each ramp. False when the clip has none to show.
    bool FadeKnobs(const TimelineClip& clip, const TimelineTrack& track, const TrackLayout& lane, Point2Df& in, Point2Df& out) const;
    Rect2Df TransitionRect(const TimelineTrack& track, const TimelineTransition& t, const TrackLayout& lane) const;
    std::vector<double> SnapCandidates(const std::string& ignoreClipId) const;
    double SnapTime(double seconds, const std::string& ignoreClipId, bool& snapped) const;
    double VisibleSeconds() const;
    void ClampScroll();
    void EnsurePlayheadVisible();

    // ---- drawing ----
    void DrawRuler(IRenderContext* ctx, const Rect2Df& ruler);
    void DrawHeader(IRenderContext* ctx, const TimelineTrack& track, const TrackLayout& lane, bool selected);
    void DrawToggleGlyph(IRenderContext* ctx, TimelineTrackToggle toggle, bool on, const Rect2Df& box, const Color& color);
    void DrawLane(IRenderContext* ctx, const TimelineTrack& track, const TrackLayout& lane, size_t row);
    void DrawClip(IRenderContext* ctx, const TimelineTrack& track, const TimelineClip& clip, const TrackLayout& lane);
    void DrawFilmstrip(IRenderContext* ctx, const TimelineClip& clip, const Rect2Df& content);
    void DrawPeaks(IRenderContext* ctx, const TimelineClip& clip, const Rect2Df& content);
    void DrawTransitions(IRenderContext* ctx, const TimelineTrack& track, const TrackLayout& lane);
    void DrawGuides(IRenderContext* ctx, const Rect2Df& lanes);
    void DrawPlayhead(IRenderContext* ctx, const Rect2Df& body);
    void DrawDragFeedback(IRenderContext* ctx, const Rect2Df& lanes);
    void DrawReadout(IRenderContext* ctx, const std::string& text, float x, float y);
    void DrawScrollbar(IRenderContext* ctx, const Rect2Df& body);
    void DrawBadge(IRenderContext* ctx, const std::string& text, float right, float top, float height,
                   const Color& fill, const Color& ink, float& left);
    void SetLabelFont(IRenderContext* ctx, float size, bool mono, bool bold = false);
    void ClipFillAndEdge(const TimelineClip& clip, Color& fill, Color& edge) const;

    // ---- interaction ----
    bool HandleMouseDown(const UCEvent& event, float x, float y);
    bool HandleMouseMove(const UCEvent& event, float x, float y);
    bool HandleMouseUp(const UCEvent& event, float x, float y);
    bool HandleDoubleClick(const UCEvent& event, float x, float y);
    bool HandleWheel(const UCEvent& event, float x, float y, bool horizontal);
    bool HandleKey(const UCEvent& event);
    bool HandleDrop(const UCEvent& event, float x, float y, bool drop);
    void FinishDrag(float x, float y);
    void SelectClip(const std::string& clipId, bool extend, bool toggle);
    void ClearSelection();
    void NotifySelection();
    void MovePlayheadTo(double seconds, bool notify);
    bool Request(const TimelineChange& change);
    void RequestSplitAt(double seconds, const std::string& clipId);
    void RequestDeleteSelection(bool invertRipple);
    void RequestDuplicateSelection();
    void RequestNudgeSelection(int frames);
    void RequestMarkerAt(double seconds);
    std::string NextCut(double from, bool forward) const;
    std::string ReadoutText() const;

    // ---- caches ----
    struct FilmstripKey {
        std::string source; double in; double out; int frames; int height;
        bool operator<(const FilmstripKey& o) const {
            if (source != o.source) return source < o.source;
            if (in != o.in) return in < o.in;
            if (out != o.out) return out < o.out;
            if (frames != o.frames) return frames < o.frames;
            return height < o.height;
        }
    };
    const std::vector<UCPixmapPtr>* Filmstrip(const TimelineClip& clip, int frames, int height);
    const WaveformChannelData* Peaks(const TimelineClip& clip);

    // ---- state ----
    TimelineModel model;
    VideoTimelineStyle style;
    double pixelsPerSecond = 80.0;
    double scrollSeconds = 0.0;
    double playhead = 0.0;
    bool followPlayhead = true;
    TimelineTool tool = TimelineTool::Select;
    bool snapping = true;
    bool showBeats = false;
    std::set<std::string> selection;
    std::string selectedTransition;
    std::string selectedTrack;

    Point2Df lastPointer;
    Hit hover;
    Drag drag = Drag::None;
    Point2Df dragStart;
    bool dragMoved = false;
    std::string dragClipId;
    size_t dragTrackIndex = 0;
    TimelineClip dragClipOriginal;
    TimelineTrack dragPreview;          // the dragged track with the edit applied, drawn instead of the model's
    double dragOffsetSeconds = 0.0;    // pointer to clip start, at the press
    double dragTime = 0.0;             // the current edge / start / playhead time
    int dragInsertIndex = -1;          // magnetic reorder target
    bool dragSnapped = false;
    double dragSnapTime = 0.0;
    std::string dragReadout;
    Rect2Df rubberBand;
    bool dropActive = false;
    size_t dropTrackIndex = 0;
    double dropSeconds = 0.0;
    int dropCount = 0;
    std::string dropTargetClip;        // SetDropTargetClip
    double scrollDragStart = 0.0;

    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);   // providers answering after the element is gone are ignored
    std::map<FilmstripKey, std::vector<UCPixmapPtr>> filmstrips;
    std::set<FilmstripKey> filmstripsPending;
    std::map<std::string, WaveformChannelData> peaks;   // key: source|in|out
    std::set<std::string> peaksPending;
};

std::shared_ptr<UltraCanvasVideoTimeline> CreateVideoTimeline(const std::string& id, int x, int y, int w, int h);

} // namespace UltraCanvas
