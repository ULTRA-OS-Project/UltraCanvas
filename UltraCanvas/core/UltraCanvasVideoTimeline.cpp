// UltraCanvasVideoTimeline.cpp
// The media timeline editor element: layout, drawing (ruler, track headers,
// lanes, clips with filmstrips and waveforms, transitions, guides, playhead,
// drag feedback, readouts), hit testing and the interactions that turn into
// reported changes. The model operations it previews with live in
// UltraCanvasVideoTimelineModel.cpp.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraCanvasVideoTimeline.h"
#include "UltraCanvasEvent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace UltraCanvas {

namespace {

constexpr double kMinPixelsPerSecond = 2.0;
constexpr double kMaxPixelsPerSecond = 2000.0;
constexpr double kMinTransitionSeconds = 0.04;
constexpr float kToggleSize = 16.0f;
constexpr float kToggleGap = 2.0f;
constexpr float kMarkerStripHeight = 11.0f;
constexpr float kTransitionHeight = 18.0f;

std::string SecondsLabel(double seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f s", seconds);
    return buffer;
}

std::string SpeedLabel(double speed) {
    char buffer[32];
    if (std::fabs(speed - std::round(speed)) < 1e-6) std::snprintf(buffer, sizeof buffer, "%.0fx", speed);
    else std::snprintf(buffer, sizeof buffer, "%.2gx", speed);
    return buffer;
}

// `text` cut to fit `maxWidth` with an ellipsis, at a UTF-8 boundary
std::string Truncated(IRenderContext* ctx, const std::string& text, double maxWidth) {
    if (text.empty() || maxWidth <= 0.0) return std::string();
    if (ctx->GetTextDimensions(text, Size2Di(0, 0)).width <= maxWidth) return text;
    const std::string ellipsis = "\xE2\x80\xA6";
    size_t lo = 0, hi = text.size();
    auto boundary = [&](size_t i) {
        while (i > 0 && i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) --i;
        return i;
    };
    while (lo < hi) {
        size_t mid = boundary((lo + hi + 1) / 2);
        if (mid <= lo) break;
        if (ctx->GetTextDimensions(text.substr(0, mid) + ellipsis, Size2Di(0, 0)).width <= maxWidth) lo = mid;
        else hi = mid - 1;
    }
    lo = boundary(lo);
    return lo == 0 ? std::string() : text.substr(0, lo) + ellipsis;
}

Color FromRGB(uint32_t rgb) {
    return Color(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                 static_cast<uint8_t>(rgb & 0xFF), 255);
}

Color WithAlpha(const Color& c, uint8_t a) { return Color(c.r, c.g, c.b, a); }

// The toggles a track header shows, left to right
std::vector<TimelineTrackToggle> TogglesFor(const TimelineTrack& track) {
    std::vector<TimelineTrackToggle> toggles;
    if (track.IsMain()) toggles.push_back(TimelineTrackToggle::Magnetic);
    if (track.kind != TimelineTrackKind::Audio && track.kind != TimelineTrackKind::MainAudio)
        toggles.push_back(TimelineTrackToggle::Visible);
    if (track.kind != TimelineTrackKind::Title) toggles.push_back(TimelineTrackToggle::Muted);
    toggles.push_back(TimelineTrackToggle::Locked);
    return toggles;
}

bool ToggleState(const TimelineTrack& track, TimelineTrackToggle toggle) {
    switch (toggle) {
        case TimelineTrackToggle::Magnetic: return track.magnetic;
        case TimelineTrackToggle::Visible: return track.visible;
        case TimelineTrackToggle::Muted: return track.muted;
        case TimelineTrackToggle::Locked: return track.locked;
    }
    return false;
}

} // namespace

// ============================================================================
// STYLE
// ============================================================================

VideoTimelineStyle VideoTimelineStyle::LightPalette() { return VideoTimelineStyle(); }

VideoTimelineStyle VideoTimelineStyle::DarkPalette() {
    VideoTimelineStyle s;
    s.background = Color(21, 23, 26);
    s.lane = Color(27, 30, 34);
    s.laneAlternate = Color(24, 27, 30);
    s.header = Color(35, 39, 44);
    s.headerSelected = Color(58, 44, 20);
    s.line = Color(42, 46, 52);
    s.lineStrong = Color(115, 123, 133);
    s.ink = Color(236, 238, 240);
    s.inkMuted = Color(166, 173, 181);
    s.inkFaint = Color(138, 145, 154);
    s.accent = Color(242, 165, 58);
    s.accentSoft = Color(58, 44, 20);
    s.onAccent = Color(27, 19, 5);
    s.playhead = Color(255, 93, 79);
    s.focus = Color(91, 196, 245);
    s.danger = Color(255, 111, 97);
    s.clipVideo = Color(35, 74, 95);
    s.clipVideoEdge = Color(79, 147, 181);
    s.clipImage = Color(77, 63, 34);
    s.clipImageEdge = Color(168, 135, 63);
    s.clipCard = Color(52, 56, 62);
    s.clipCardEdge = Color(122, 130, 139);
    s.clipTitle = Color(74, 47, 79);
    s.clipTitleEdge = Color(165, 113, 173);
    s.clipAudio = Color(31, 74, 51);
    s.clipAudioEdge = Color(79, 158, 114);
    s.waveform = Color(143, 214, 170);
    s.transitionFill = Color(217, 221, 226);
    s.onTransition = Color(21, 23, 26);
    s.beat = Color(255, 255, 255, 41);
    s.trimOverflow = Color(255, 255, 255, 26);
    s.dropSoft = Color(91, 196, 245, 41);
    s.rangeFill = Color(242, 165, 58, 41);
    s.mutedHatch = Color(13, 14, 16, 140);
    s.scrollbar = Color(166, 173, 181, 120);
    return s;
}

// ============================================================================
// CONSTRUCTION AND SETTERS
// ============================================================================

UltraCanvasVideoTimeline::UltraCanvasVideoTimeline(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    SetMouseCursor(UCMouseCursor::Default);
}

void UltraCanvasVideoTimeline::SetModel(const TimelineModel& m) {
    model = m;
    for (TimelineTrack& track : model.tracks) TimelineDeriveStarts(track);
    // A selection that is gone leaves
    for (auto it = selection.begin(); it != selection.end();) {
        if (model.TrackOfClip(*it)) ++it;
        else it = selection.erase(it);
    }
    if (!selectedTransition.empty()) {
        const TimelineTrack* t = model.TrackOfClip(selectedTransition);
        if (!t || !t->FindTransition(selectedTransition)) selectedTransition.clear();
    }
    if (!selectedTrack.empty() && !model.FindTrack(selectedTrack)) selectedTrack.clear();
    if (selectedTrack.empty()) {
        for (const TimelineTrack& t : model.tracks) if (t.kind == TimelineTrackKind::MainVideo) { selectedTrack = t.id; break; }
    }
    drag = Drag::None;
    ClampScroll();
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetPixelsPerSecond(double pps) {
    const double clamped = std::clamp(pps, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    if (clamped == pixelsPerSecond) return;
    pixelsPerSecond = clamped;
    ClampScroll();
    if (onZoomChanged) onZoomChanged(pixelsPerSecond);
    RequestRedraw();
}

void UltraCanvasVideoTimeline::ZoomToFit() {
    const Rect2Df body = BodyRect();
    const double length = std::max(model.Length(), 1.0);
    if (body.width <= 16.0f) return;
    scrollSeconds = 0.0;
    SetPixelsPerSecond((body.width - 16.0) / length);
}

void UltraCanvasVideoTimeline::ZoomBy(double factor, double aroundSeconds) {
    const double before = pixelsPerSecond;
    const double after = std::clamp(before * factor, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    if (after == before) return;
    scrollSeconds = aroundSeconds - (aroundSeconds - scrollSeconds) * (before / after);
    SetPixelsPerSecond(after);
    ClampScroll();
}

void UltraCanvasVideoTimeline::SetScrollSeconds(double seconds) {
    scrollSeconds = seconds;
    ClampScroll();
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetPlayhead(double seconds) {
    playhead = std::max(0.0, seconds);
    if (followPlayhead) EnsurePlayheadVisible();
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetTool(TimelineTool t) {
    if (tool == t) return;
    tool = t;
    if (onToolChanged) onToolChanged(tool);
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetShowBeats(bool on) {
    showBeats = on;
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetStyle(const VideoTimelineStyle& s) {
    style = s;
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetSelection(const std::vector<std::string>& clipIds) {
    selection.clear();
    for (const std::string& id : clipIds) if (model.TrackOfClip(id)) selection.insert(id);
    RequestRedraw();
}

std::vector<std::string> UltraCanvasVideoTimeline::GetSelection() const {
    return std::vector<std::string>(selection.begin(), selection.end());
}

void UltraCanvasVideoTimeline::SetSelectedTransition(const std::string& clipId) {
    selectedTransition = clipId;
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SetSelectedTrack(const std::string& trackId) {
    selectedTrack = trackId;
    RequestRedraw();
}

void UltraCanvasVideoTimeline::ClearCaches() {
    filmstrips.clear();
    filmstripsPending.clear();
    peaks.clear();
    peaksPending.clear();
    RequestRedraw();
}

// ============================================================================
// GEOMETRY
// ============================================================================

Rect2Df UltraCanvasVideoTimeline::BodyRect() const {
    const Rect2Df b = GetLocalBounds();
    return Rect2Df(style.headerWidth, 0.0f, std::max(0.0f, b.width - style.headerWidth), b.height);
}

Rect2Df UltraCanvasVideoTimeline::LanesRect() const {
    const Rect2Df body = BodyRect();
    return Rect2Df(body.x, style.rulerHeight, body.width,
                   std::max(0.0f, body.height - style.rulerHeight - style.scrollbarHeight));
}

float UltraCanvasVideoTimeline::TrackDefaultHeight(const TimelineTrack& track) const {
    if (track.collapsed) return style.trackHeightCollapsed;
    if (track.height > 0) return static_cast<float>(track.height);
    switch (track.kind) {
        case TimelineTrackKind::Title: return style.trackHeightOverlay;
        case TimelineTrackKind::Audio:
        case TimelineTrackKind::MainAudio: return style.trackHeightAudio;
        default: return style.trackHeightVideo;
    }
}

std::vector<UltraCanvasVideoTimeline::TrackLayout> UltraCanvasVideoTimeline::LayoutTracks() const {
    std::vector<TrackLayout> lanes;
    float top = style.rulerHeight;
    for (size_t i = 0; i < model.tracks.size(); ++i) {
        TrackLayout l;
        l.index = i;
        l.top = top;
        l.height = TrackDefaultHeight(model.tracks[i]);
        lanes.push_back(l);
        top += l.height + 1.0f;   // a 1 px line between lanes
    }
    return lanes;
}

double UltraCanvasVideoTimeline::XToSeconds(float x) const {
    return scrollSeconds + (x - style.headerWidth) / pixelsPerSecond;
}

float UltraCanvasVideoTimeline::SecondsToX(double seconds) const {
    return style.headerWidth + static_cast<float>((seconds - scrollSeconds) * pixelsPerSecond);
}

double UltraCanvasVideoTimeline::VisibleSeconds() const {
    return BodyRect().width / pixelsPerSecond;
}

void UltraCanvasVideoTimeline::ClampScroll() {
    const double length = model.Length();
    const double visible = VisibleSeconds();
    const double maxScroll = std::max(0.0, length + visible * 0.25 - visible);
    scrollSeconds = std::clamp(scrollSeconds, 0.0, maxScroll);
}

void UltraCanvasVideoTimeline::EnsurePlayheadVisible() {
    const double visible = VisibleSeconds();
    if (visible <= 0.0) return;
    if (playhead < scrollSeconds || playhead > scrollSeconds + visible) {
        scrollSeconds = std::max(0.0, playhead - visible * 0.1);
    }
}

Rect2Df UltraCanvasVideoTimeline::ClipRect(const TimelineClip& clip, const TrackLayout& lane) const {
    const float x0 = SecondsToX(clip.start);
    const float x1 = SecondsToX(clip.End());
    return Rect2Df(x0, lane.top + 1.0f, std::max(0.0f, x1 - x0), lane.height - 2.0f);
}

Rect2Df UltraCanvasVideoTimeline::TransitionRect(const TimelineTrack& track, const TimelineTransition& t,
                                                 const TrackLayout& lane) const {
    const TimelineClip* clip = track.FindClip(t.clipId);
    if (!clip) return Rect2Df();
    const float cut = SecondsToX(clip->start);
    const float width = std::max(style.minTransitionPx, static_cast<float>(t.seconds * pixelsPerSecond));
    return Rect2Df(cut - width / 2.0f, lane.top + lane.height - 3.0f - kTransitionHeight, width, kTransitionHeight);
}

UltraCanvasVideoTimeline::Hit UltraCanvasVideoTimeline::HitTest(float x, float y) const {
    Hit hit;
    const Rect2Df b = GetLocalBounds();
    if (x < 0.0f || y < 0.0f || x > b.width || y > b.height) return hit;
    const Rect2Df body = BodyRect();
    const std::vector<TrackLayout> lanes = LayoutTracks();

    if (x >= body.x && y < style.rulerHeight) {
        hit.kind = Hit::Ruler;
        hit.seconds = std::max(0.0, XToSeconds(x));
        if (y < kMarkerStripHeight + 2.0f) {
            for (const TimelineMarker& m : model.markers) {
                if (std::fabs(SecondsToX(m.seconds) - x) <= 6.0f) {
                    hit.kind = Hit::Marker;
                    hit.markerId = m.id;
                    break;
                }
            }
        }
        return hit;
    }
    if (x >= body.x && y >= b.height - style.scrollbarHeight) {
        hit.kind = Hit::Scrollbar;
        return hit;
    }
    for (const TrackLayout& lane : lanes) {
        if (y < lane.top || y >= lane.top + lane.height) continue;
        hit.trackIndex = lane.index;
        const TimelineTrack& track = model.tracks[lane.index];
        if (x < body.x) {
            // header: toggles sit at the right, 16 px each
            const std::vector<TimelineTrackToggle> toggles = TogglesFor(track);
            float right = style.headerWidth - 6.0f;
            for (size_t i = toggles.size(); i-- > 0;) {
                const float left = right - kToggleSize;
                if (x >= left - 2.0f && x <= right + 2.0f) {
                    hit.kind = Hit::HeaderToggle;
                    hit.toggle = toggles[i];
                    return hit;
                }
                right = left - kToggleGap;
            }
            hit.kind = Hit::HeaderName;
            return hit;
        }
        hit.kind = Hit::Lane;
        hit.seconds = std::max(0.0, XToSeconds(x));
        // transitions sit over the clips
        for (const TimelineTransition& t : track.transitions) {
            const Rect2Df r = TransitionRect(track, t, lane);
            if (r.width > 0.0f && r.Contains(Point2Df(x, y))) {
                hit.clipId = t.clipId;
                if (x < r.x + 4.0f) hit.kind = Hit::TransitionLeftEdge;
                else if (x > r.x + r.width - 4.0f) hit.kind = Hit::TransitionRightEdge;
                else hit.kind = Hit::TransitionBody;
                return hit;
            }
        }
        for (const TimelineClip& clip : track.clips) {
            const Rect2Df r = ClipRect(clip, lane);
            if (x < r.x || x > r.x + r.width) continue;
            hit.clipId = clip.id;
            const float handle = std::min(style.handleWidth, r.width / 3.0f);
            if (x <= r.x + handle) hit.kind = Hit::ClipLeftEdge;
            else if (x >= r.x + r.width - handle) hit.kind = Hit::ClipRightEdge;
            else hit.kind = Hit::ClipBody;
            return hit;
        }
        return hit;
    }
    return hit;
}

std::string UltraCanvasVideoTimeline::ClipAt(float x, float y, std::string* trackId) const {
    const Hit hit = HitTest(x, y);
    if (trackId) *trackId = hit.kind != Hit::Nothing && hit.kind != Hit::Ruler && hit.kind != Hit::Marker &&
                                    hit.kind != Hit::Scrollbar && hit.trackIndex < model.tracks.size()
                                ? model.tracks[hit.trackIndex].id
                                : "";
    if (hit.kind == Hit::ClipBody || hit.kind == Hit::ClipLeftEdge || hit.kind == Hit::ClipRightEdge) return hit.clipId;
    return "";
}

std::vector<double> UltraCanvasVideoTimeline::SnapCandidates(const std::string& ignoreClipId) const {
    std::vector<double> c;
    c.push_back(playhead);
    c.push_back(0.0);
    for (const TimelineTrack& t : model.tracks) {
        for (const TimelineClip& clip : t.clips) {
            if (clip.id == ignoreClipId) continue;
            c.push_back(clip.start);
            c.push_back(clip.End());
        }
    }
    for (const TimelineMarker& m : model.markers) c.push_back(m.seconds);
    if (model.HasRange()) { c.push_back(model.rangeIn); c.push_back(model.rangeOut); }
    if (showBeats) c.insert(c.end(), model.beats.begin(), model.beats.end());
    return c;
}

double UltraCanvasVideoTimeline::SnapTime(double seconds, const std::string& ignoreClipId, bool& snapped) const {
    snapped = false;
    if (!snapping) return seconds;
    const double tolerance = style.snapTolerancePx / pixelsPerSecond;
    const std::optional<double> s = TimelineSnap(seconds, SnapCandidates(ignoreClipId), tolerance);
    if (!s) return seconds;
    snapped = true;
    return *s;
}

// ============================================================================
// DRAWING
// ============================================================================

void UltraCanvasVideoTimeline::SetLabelFont(IRenderContext* ctx, float size, bool mono, bool bold) {
    ctx->SetFontFace(mono ? style.monoFamily : style.fontFamily, bold ? FontWeight::Bold : FontWeight::Normal,
                     FontSlant::Normal);
    ctx->SetFontSize(size * 0.75f);   // the style's sizes are pixels; the context takes points
}

void UltraCanvasVideoTimeline::ClipFillAndEdge(const TimelineClip& clip, Color& fill, Color& edge) const {
    switch (clip.kind) {
        case TimelineClipKind::Video: fill = style.clipVideo; edge = style.clipVideoEdge; break;
        case TimelineClipKind::Image: fill = style.clipImage; edge = style.clipImageEdge; break;
        case TimelineClipKind::Card: fill = style.clipCard; edge = style.clipCardEdge; break;
        case TimelineClipKind::Title: fill = style.clipTitle; edge = style.clipTitleEdge; break;
        case TimelineClipKind::Audio: fill = style.clipAudio; edge = style.clipAudioEdge; break;
    }
}

void UltraCanvasVideoTimeline::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    const Rect2Df b = GetLocalBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;
    const Rect2Df body = BodyRect();
    const Rect2Df lanes = LanesRect();
    const std::vector<TrackLayout> layout = LayoutTracks();

    ctx->SetFillPaint(style.background);
    ctx->FillRectangle(Rect2Dd(b.x, b.y, b.width, b.height));

    // lanes, clipped to the body
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(lanes.x, lanes.y, lanes.width, lanes.height));
    for (size_t row = 0; row < layout.size(); ++row) {
        const TrackLayout& lane = layout[row];
        if (lane.top > lanes.y + lanes.height) break;
        DrawLane(ctx, model.tracks[lane.index], lane, row);
    }
    DrawGuides(ctx, lanes);
    DrawDragFeedback(ctx, lanes);
    ctx->PopState();

    // the header column
    ctx->SetFillPaint(style.header);
    ctx->FillRectangle(Rect2Dd(0.0, 0.0, style.headerWidth, b.height));
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(0.0, lanes.y, style.headerWidth, lanes.height));
    for (const TrackLayout& lane : layout) {
        if (lane.top > lanes.y + lanes.height) break;
        const TimelineTrack& track = model.tracks[lane.index];
        DrawHeader(ctx, track, lane, track.id == selectedTrack);
    }
    ctx->PopState();
    ctx->SetStrokePaint(style.line);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Dd(style.headerWidth - 0.5, 0.0), Point2Dd(style.headerWidth - 0.5, b.height));

    // the ruler, then the playhead over everything, then the scrollbar
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(body.x, 0.0, body.width, style.rulerHeight));
    DrawRuler(ctx, Rect2Df(body.x, 0.0f, body.width, style.rulerHeight));
    ctx->PopState();
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(body.x, 0.0, body.width, body.height));
    DrawPlayhead(ctx, Rect2Df(body.x, 0.0f, body.width, lanes.y + lanes.height));
    if (drag != Drag::None && !dragReadout.empty()) DrawReadout(ctx, dragReadout, lastPointer.x, lastPointer.y);
    ctx->PopState();
    DrawScrollbar(ctx, body);
    // the timecode over the header column, where the ruler would be
    ctx->SetFillPaint(style.header);
    ctx->FillRectangle(Rect2Dd(0.0, 0.0, style.headerWidth, style.rulerHeight));
    ctx->SetStrokePaint(style.line);
    ctx->DrawLine(Point2Dd(0.0, style.rulerHeight - 0.5), Point2Dd(b.width, style.rulerHeight - 0.5));
    SetLabelFont(ctx, style.readoutSize, true, true);
    ctx->SetTextPaint(style.ink);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(TimelineTimecode(playhead, model.frameRate), Rect2Dd(10.0, 0.0, style.headerWidth - 12.0, style.rulerHeight));
}

void UltraCanvasVideoTimeline::DrawRuler(IRenderContext* ctx, const Rect2Df& ruler) {
    ctx->SetFillPaint(style.header);
    ctx->FillRectangle(Rect2Dd(ruler.x, ruler.y, ruler.width, ruler.height));
    const float bottom = ruler.y + ruler.height;
    if (model.HasRange()) {
        const float x0 = SecondsToX(model.rangeIn);
        const float x1 = SecondsToX(model.rangeOut);
        ctx->SetFillPaint(style.rangeFill);
        ctx->FillRectangle(Rect2Dd(x0, ruler.y + kMarkerStripHeight, x1 - x0, ruler.height - kMarkerStripHeight));
        ctx->SetStrokePaint(style.accent);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLine(Point2Dd(x0 + 1.0, ruler.y + kMarkerStripHeight), Point2Dd(x0 + 1.0, bottom));
        ctx->DrawLine(Point2Dd(x1 - 1.0, ruler.y + kMarkerStripHeight), Point2Dd(x1 - 1.0, bottom));
    }
    double minor = 1.0, major = 1.0;
    TimelineRulerSteps(pixelsPerSecond, model.frameRate, minor, major);
    const double first = std::floor(scrollSeconds / minor) * minor;
    const double last = scrollSeconds + VisibleSeconds() + minor;
    ctx->SetStrokeWidth(1.0f);
    SetLabelFont(ctx, style.rulerSize, true);
    ctx->SetTextPaint(style.inkFaint);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    const bool shortLabels = major >= 1.0 - 1e-9;
    for (double t = std::max(0.0, first); t <= last; t += minor) {
        const double x = std::floor(SecondsToX(t)) + 0.5;
        const double ratio = t / major;
        const bool isMajor = std::fabs(ratio - std::round(ratio)) < 1e-6;
        ctx->SetStrokePaint(isMajor ? style.lineStrong : style.line);
        const double tick = isMajor ? 9.0 : 4.0;
        ctx->DrawLine(Point2Dd(x, bottom - tick), Point2Dd(x, bottom));
        if (isMajor) {
            ctx->DrawTextInRect(TimelineTimecode(t, model.frameRate, shortLabels),
                                Rect2Dd(x + 3.0, ruler.y + kMarkerStripHeight, 90.0, ruler.height - kMarkerStripHeight - 9.0));
        }
    }
    // markers in the top strip
    SetLabelFont(ctx, style.rulerSize, false, true);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    for (const TimelineMarker& m : model.markers) {
        const float x = SecondsToX(m.seconds);
        if (x < ruler.x - 80.0f || x > ruler.x + ruler.width + 10.0f) continue;
        const Color c = FromRGB(m.color);
        ctx->SetStrokePaint(c);
        ctx->DrawLine(Point2Dd(std::floor(x) + 0.5, ruler.y), Point2Dd(std::floor(x) + 0.5, bottom));
        const Size2Di size = ctx->GetTextDimensions(m.label, Size2Di(0, 0));
        const double w = std::max(10.0, size.width + 8.0);
        ctx->SetFillPaint(c);
        ctx->FillRoundedRectangle(Rect2Dd(x, ruler.y + 1.0, w, kMarkerStripHeight - 2.0), 2.0);
        ctx->SetTextPaint(style.onAccent);
        ctx->DrawTextInRect(m.label, Rect2Dd(x + 4.0, ruler.y + 1.0, w - 4.0, kMarkerStripHeight - 2.0));
    }
    if (drag != Drag::None && dragSnapped) {
        const float x = SecondsToX(dragSnapTime);
        ctx->SetFillPaint(style.focus);
        ctx->FillLinePath({Point2Dd(x, bottom - 10.0), Point2Dd(x + 4.0, bottom - 6.0), Point2Dd(x, bottom - 2.0),
                           Point2Dd(x - 4.0, bottom - 6.0)});
    }
    ctx->SetStrokePaint(style.line);
    ctx->DrawLine(Point2Dd(ruler.x, bottom - 0.5), Point2Dd(ruler.x + ruler.width, bottom - 0.5));
}

void UltraCanvasVideoTimeline::DrawToggleGlyph(IRenderContext* ctx, TimelineTrackToggle toggle, bool on,
                                               const Rect2Df& box, const Color& color) {
    const double x = box.x, y = box.y;
    ctx->SetStrokePaint(color);
    ctx->SetFillPaint(color);
    ctx->SetStrokeWidth(1.5f);
    ctx->SetLineCap(LineCap::Round);
    ctx->SetLineJoin(LineJoin::Round);
    switch (toggle) {
        case TimelineTrackToggle::Magnetic:
            ctx->DrawLinePath({Point2Dd(x + 4.5, y + 2.5), Point2Dd(x + 4.5, y + 8.0), Point2Dd(x + 5.5, y + 10.5),
                               Point2Dd(x + 8.0, y + 11.5), Point2Dd(x + 10.5, y + 10.5), Point2Dd(x + 11.5, y + 8.0),
                               Point2Dd(x + 11.5, y + 2.5)},
                              false);
            ctx->DrawLine(Point2Dd(x + 4.5, y + 5.5), Point2Dd(x + 6.5, y + 5.5));
            ctx->DrawLine(Point2Dd(x + 9.5, y + 5.5), Point2Dd(x + 11.5, y + 5.5));
            break;
        case TimelineTrackToggle::Visible:
            ctx->DrawEllipse(Rect2Dd(x + 2.5, y + 5.0, 11.0, 6.0));
            ctx->FillCircle(Point2Dd(x + 8.0, y + 8.0), 1.8);
            if (!on) {
                ctx->SetStrokePaint(style.danger);
                ctx->DrawLine(Point2Dd(x + 3.0, y + 13.0), Point2Dd(x + 13.0, y + 3.0));
            }
            break;
        case TimelineTrackToggle::Muted:
            ctx->FillLinePath({Point2Dd(x + 2.5, y + 6.0), Point2Dd(x + 5.5, y + 6.0), Point2Dd(x + 9.0, y + 3.0),
                               Point2Dd(x + 9.0, y + 13.0), Point2Dd(x + 5.5, y + 10.0), Point2Dd(x + 2.5, y + 10.0)});
            if (!on) {
                ctx->DrawLine(Point2Dd(x + 11.0, y + 6.0), Point2Dd(x + 11.0, y + 10.0));
                ctx->DrawLine(Point2Dd(x + 13.0, y + 4.5), Point2Dd(x + 13.0, y + 11.5));
            } else {
                ctx->SetStrokePaint(style.danger);
                ctx->DrawLine(Point2Dd(x + 11.0, y + 6.0), Point2Dd(x + 14.5, y + 10.0));
                ctx->DrawLine(Point2Dd(x + 14.5, y + 6.0), Point2Dd(x + 11.0, y + 10.0));
            }
            break;
        case TimelineTrackToggle::Locked:
            ctx->DrawRectangle(Rect2Dd(x + 4.0, y + 8.0, 8.0, 5.5));
            if (on) {
                ctx->DrawLinePath({Point2Dd(x + 5.5, y + 8.0), Point2Dd(x + 5.5, y + 5.5), Point2Dd(x + 8.0, y + 3.5),
                                   Point2Dd(x + 10.5, y + 5.5), Point2Dd(x + 10.5, y + 8.0)},
                                  false);
            } else {
                ctx->DrawLinePath({Point2Dd(x + 10.5, y + 8.0), Point2Dd(x + 10.5, y + 5.5), Point2Dd(x + 8.0, y + 3.5),
                                   Point2Dd(x + 5.5, y + 5.0)},
                                  false);
            }
            break;
    }
}

void UltraCanvasVideoTimeline::DrawHeader(IRenderContext* ctx, const TimelineTrack& track, const TrackLayout& lane,
                                          bool selected) {
    const Rect2Df r(0.0f, lane.top, style.headerWidth, lane.height);
    ctx->SetFillPaint(selected ? style.headerSelected : style.header);
    ctx->FillRectangle(Rect2Dd(r.x, r.y, r.width, r.height));
    TimelineClip probe;
    probe.kind = track.kind == TimelineTrackKind::Title ? TimelineClipKind::Title
                 : (track.kind == TimelineTrackKind::Audio || track.kind == TimelineTrackKind::MainAudio)
                     ? TimelineClipKind::Audio
                     : TimelineClipKind::Video;
    Color fill, edge;
    ClipFillAndEdge(probe, fill, edge);
    ctx->SetFillPaint(edge);
    ctx->FillRectangle(Rect2Dd(0.0, r.y, 3.0, r.height));
    ctx->SetStrokePaint(style.line);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Dd(0.0, r.y + r.height + 0.5), Point2Dd(r.width, r.y + r.height + 0.5));

    const std::vector<TimelineTrackToggle> toggles = TogglesFor(track);
    float right = style.headerWidth - 6.0f;
    const float boxY = lane.top + (lane.height - kToggleSize) / 2.0f;
    for (size_t i = toggles.size(); i-- > 0;) {
        const float left = right - kToggleSize;
        const bool on = ToggleState(track, toggles[i]);
        const Rect2Df box(left, boxY, kToggleSize, kToggleSize);
        Color color = style.inkMuted;
        if (toggles[i] == TimelineTrackToggle::Magnetic && on) {
            ctx->SetFillPaint(style.accentSoft);
            ctx->FillRoundedRectangle(Rect2Dd(box.x - 2.0, box.y - 2.0, box.width + 4.0, box.height + 4.0), 3.0);
            color = style.accent;
        }
        if (toggles[i] == TimelineTrackToggle::Visible && !on) color = style.danger;
        if (toggles[i] == TimelineTrackToggle::Muted && on) color = style.danger;
        if (toggles[i] == TimelineTrackToggle::Locked && on) color = style.ink;
        if (hover.kind == Hit::HeaderToggle && hover.trackIndex == lane.index && hover.toggle == toggles[i]) {
            ctx->SetFillPaint(WithAlpha(style.inkMuted, 40));
            ctx->FillRoundedRectangle(Rect2Dd(box.x - 2.0, box.y - 2.0, box.width + 4.0, box.height + 4.0), 3.0);
        }
        DrawToggleGlyph(ctx, toggles[i], on, box, color);
        right = left - kToggleGap;
    }
    // id and name
    SetLabelFont(ctx, style.captionSize, true);
    ctx->SetTextPaint(style.inkFaint);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(track.id, Rect2Dd(10.0, lane.top, 26.0, lane.height));
    SetLabelFont(ctx, style.labelSize, false, selected);
    ctx->SetTextPaint(selected ? style.ink : style.inkMuted);
    const double nameWidth = std::max(10.0f, right - 42.0f);
    ctx->DrawTextInRect(Truncated(ctx, track.name, nameWidth), Rect2Dd(36.0, lane.top, nameWidth, lane.height));
}

void UltraCanvasVideoTimeline::DrawLane(IRenderContext* ctx, const TimelineTrack& track, const TrackLayout& lane,
                                        size_t row) {
    const Rect2Df body = BodyRect();
    ctx->SetFillPaint(row % 2 ? style.laneAlternate : style.lane);
    ctx->FillRectangle(Rect2Dd(body.x, lane.top, body.width, lane.height));
    if (dropActive && dropTrackIndex == lane.index) {
        ctx->SetFillPaint(style.dropSoft);
        ctx->FillRectangle(Rect2Dd(body.x, lane.top, body.width, lane.height));
        ctx->SetStrokePaint(style.focus);
        ctx->SetStrokeWidth(1.0f);
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
        ctx->DrawRectangle(Rect2Dd(body.x + 0.5, lane.top + 0.5, body.width - 1.0, lane.height - 1.0));
        ctx->SetLineDash(UCDashPattern());
    }
    ctx->SetStrokePaint(style.line);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Dd(body.x, lane.top + lane.height + 0.5), Point2Dd(body.x + body.width, lane.top + lane.height + 0.5));

    // the track being dragged is drawn from its preview
    const TimelineTrack* shown = &track;
    if (drag != Drag::None && dragTrackIndex == lane.index && !dragPreview.clips.empty()) shown = &dragPreview;
    ctx->PushState();
    if (track.locked || !track.visible) ctx->SetAlpha(0.4);
    for (const TimelineClip& clip : shown->clips) {
        const Rect2Df r = ClipRect(clip, lane);
        if (r.x + r.width < body.x || r.x > body.x + body.width) continue;
        DrawClip(ctx, *shown, clip, lane);
    }
    DrawTransitions(ctx, *shown, lane);
    ctx->PopState();
}

void UltraCanvasVideoTimeline::DrawBadge(IRenderContext* ctx, const std::string& text, float right, float top,
                                         float height, const Color& fill, const Color& ink, float& left) {
    SetLabelFont(ctx, style.captionSize, true);
    const Size2Di size = ctx->GetTextDimensions(text, Size2Di(0, 0));
    const float w = static_cast<float>(size.width) + 8.0f;
    left = right - w;
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(Rect2Dd(left, top + 1.0, w, height - 2.0), 2.0);
    ctx->SetTextPaint(ink);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(text, Rect2Dd(left, top, w, height));
}

void UltraCanvasVideoTimeline::DrawClip(IRenderContext* ctx, const TimelineTrack& track, const TimelineClip& clip,
                                        const TrackLayout& lane) {
    const Rect2Df r = ClipRect(clip, lane);
    Color fill, edge;
    ClipFillAndEdge(clip, fill, edge);
    if (clip.kind == TimelineClipKind::Card && !track.collapsed) fill = FromRGB(clip.color);
    const bool selected = selection.count(clip.id) > 0;
    const bool hovered = (hover.kind == Hit::ClipBody || hover.kind == Hit::ClipLeftEdge || hover.kind == Hit::ClipRightEdge) &&
                         hover.clipId == clip.id && drag == Drag::None;
    const bool ghost = drag == Drag::MoveClip && dragClipId == clip.id;
    const bool trimmingLeft = drag == Drag::TrimLeft && dragClipId == clip.id;
    const bool trimmingRight = drag == Drag::TrimRight && dragClipId == clip.id;

    if (r.width < 6.0f) {   // too narrow for a clip: a tick in the edge colour
        ctx->SetFillPaint(edge);
        ctx->FillRectangle(Rect2Dd(r.x, r.y, 2.0, r.height));
        return;
    }

    // unused source beyond the edge being trimmed, or hovered
    if ((trimmingLeft || trimmingRight || hovered) && !track.collapsed) {
        const double before = clip.SourceBefore();
        const double after = clip.SourceAfter();
        ctx->SetFillPaint(style.trimOverflow);
        if (before > 0.0) {
            const float w = static_cast<float>(before * pixelsPerSecond);
            ctx->FillRoundedRectangle(Rect2Dd(r.x - w, r.y, w, r.height), style.cornerRadius);
        }
        if (after > 0.0) {
            const float w = static_cast<float>(after * pixelsPerSecond);
            ctx->FillRoundedRectangle(Rect2Dd(r.x + r.width, r.y, w, r.height), style.cornerRadius);
        }
    }

    ctx->PushState();
    if (ghost) ctx->SetAlpha(0.7);
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(Rect2Dd(r.x, r.y, r.width, r.height), style.cornerRadius);
    // the 3 px top stripe and the border
    ctx->SetFillPaint(edge);
    ctx->FillRoundedRectangle(Rect2Dd(r.x, r.y, r.width, 3.0), 1.5);
    ctx->SetStrokePaint(edge);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRoundedRectangle(Rect2Dd(r.x + 0.5, r.y + 0.5, r.width - 1.0, r.height - 1.0), style.cornerRadius);

    const float bandH = track.collapsed ? r.height - 3.0f : style.clipBandHeight;
    const Rect2Df content(r.x + 1.0f, r.y + 3.0f + bandH, r.width - 2.0f, std::max(0.0f, r.height - 4.0f - bandH));

    // content
    if (content.height > 4.0f) {
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(content.x, content.y, content.width, content.height));
        if (clip.kind == TimelineClipKind::Video || clip.kind == TimelineClipKind::Image) DrawFilmstrip(ctx, clip, content);
        else if (clip.kind == TimelineClipKind::Audio) DrawPeaks(ctx, clip, content);
        ctx->PopState();
    }
    if (clip.muted || track.muted) {
        ctx->SetFillPaint(style.mutedHatch);
        ctx->FillRoundedRectangle(Rect2Dd(r.x, r.y + 3.0, r.width, r.height - 3.0), style.cornerRadius);
    }

    // the band: badges from the right, then the name in what is left
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(r.x + 1.0, r.y + 3.0, r.width - 2.0, bandH));
    float right = r.x + r.width - 3.0f;
    const Color badgeFill = WithAlpha(style.ink, 24);
    if (r.width >= 110.0f) DrawBadge(ctx, SecondsLabel(clip.duration), right, r.y + 3.0f, bandH, badgeFill, style.inkMuted, right);
    if (r.width >= 70.0f) {
        right -= 2.0f;
        if (std::fabs(clip.speed - 1.0) > 1e-6) { DrawBadge(ctx, SpeedLabel(clip.speed), right, r.y + 3.0f, bandH, badgeFill, style.ink, right); right -= 2.0f; }
        if (clip.muted) { DrawBadge(ctx, "mute", right, r.y + 3.0f, bandH, badgeFill, style.danger, right); right -= 2.0f; }
        if (clip.hasTitle) { DrawBadge(ctx, "T", right, r.y + 3.0f, bandH, badgeFill, style.ink, right); right -= 2.0f; }
        if (clip.hasEffects) { DrawBadge(ctx, "Fx", right, r.y + 3.0f, bandH, badgeFill, style.ink, right); right -= 2.0f; }
        if (clip.hasMotion) { DrawBadge(ctx, "pan", right, r.y + 3.0f, bandH, badgeFill, style.ink, right); right -= 2.0f; }
    }
    SetLabelFont(ctx, style.labelSize, false, true);
    ctx->SetTextPaint(style.ink);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    const double nameWidth = std::max(4.0f, right - r.x - 8.0f);
    ctx->DrawTextInRect(Truncated(ctx, clip.name, nameWidth), Rect2Dd(r.x + 6.0, r.y + 3.0, nameWidth, bandH));
    ctx->PopState();

    // trim grips on hover; the active edge while trimming
    if (hovered || trimmingLeft || trimmingRight) {
        ctx->SetFillPaint(trimmingLeft ? style.accent : edge);
        ctx->FillRoundedRectangle(Rect2Dd(r.x + 1.0, r.y + 3.0 + bandH + 2.0, 3.0, std::max(4.0f, r.height - bandH - 8.0f)), 1.5);
        ctx->SetFillPaint(trimmingRight ? style.accent : edge);
        ctx->FillRoundedRectangle(Rect2Dd(r.x + r.width - 4.0, r.y + 3.0 + bandH + 2.0, 3.0, std::max(4.0f, r.height - bandH - 8.0f)), 1.5);
    }
    if (selected) {
        ctx->SetStrokePaint(style.accent);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(r.x + 1.0, r.y + 1.0, r.width - 2.0, r.height - 2.0), style.cornerRadius);
    }
    ctx->PopState();
}

void UltraCanvasVideoTimeline::DrawFilmstrip(IRenderContext* ctx, const TimelineClip& clip, const Rect2Df& content) {
    const int height = static_cast<int>(std::lround(content.height));
    if (height < 4 || clip.source.empty()) return;
    const int frameW = std::max(8, static_cast<int>(std::lround(height * 16.0 / 9.0)));
    int frames = clip.kind == TimelineClipKind::Image ? 1 : static_cast<int>(std::ceil(content.width / frameW));
    frames = std::clamp(frames, 1, 200);
    if (clip.kind != TimelineClipKind::Image) frames = ((frames + 3) / 4) * 4;   // fewer distinct requests across zooms
    const std::vector<UCPixmapPtr>* strip = Filmstrip(clip, frames, height);
    if (!strip || strip->empty()) return;
    float x = content.x;
    for (size_t i = 0; i < strip->size() && x < content.x + content.width; ++i) {
        const UCPixmapPtr& pix = (*strip)[i];
        if (pix && pix->IsValid()) ctx->DrawPixmap(*pix, Rect2Dd(x, content.y, frameW, height), ImageFitMode::Cover);
        x += frameW;
        if (clip.kind == TimelineClipKind::Image) {   // one photo, repeated along the clip
            while (x < content.x + content.width && pix && pix->IsValid()) {
                ctx->DrawPixmap(*pix, Rect2Dd(x, content.y, frameW, height), ImageFitMode::Cover);
                x += frameW;
            }
        }
    }
}

void UltraCanvasVideoTimeline::DrawPeaks(IRenderContext* ctx, const TimelineClip& clip, const Rect2Df& content) {
    const WaveformChannelData* data = Peaks(clip);
    const double mid = content.y + content.height / 2.0;
    const double half = content.height / 2.0 - 1.0;
    if (data && data->IsValid() && data->duration > 0.0) {
        ctx->SetStrokePaint(style.waveform);
        ctx->SetStrokeWidth(1.0f);
        const double blocksPerSecond = data->totalBlocks / data->duration;
        const int columns = static_cast<int>(content.width);
        for (int px = 0; px < columns; ++px) {
            const double t = px / pixelsPerSecond * clip.speed;     // seconds into the clip's source range
            const int block = static_cast<int>(t * blocksPerSecond);
            if (block < 0 || block >= static_cast<int>(data->maxValues.size())) break;
            const double top = mid - std::clamp(static_cast<double>(data->maxValues[static_cast<size_t>(block)]), 0.0, 1.0) * half;
            const double bottom = mid - std::clamp(static_cast<double>(data->minValues[static_cast<size_t>(block)]), -1.0, 0.0) * half;
            const double x = std::floor(content.x + px) + 0.5;
            ctx->DrawLine(Point2Dd(x, top), Point2Dd(x, std::max(bottom, top + 1.0)));
        }
    } else {
        ctx->SetStrokePaint(WithAlpha(style.waveform, 90));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(content.x, mid), Point2Dd(content.x + content.width, mid));
    }
    // fades, as ramps from the corners
    Color fill, edge;
    ClipFillAndEdge(clip, fill, edge);
    ctx->SetStrokePaint(edge);
    ctx->SetStrokeWidth(1.25f);
    if (clip.fadeIn > 0.0) {
        const double w = clip.fadeIn * pixelsPerSecond;
        ctx->DrawLine(Point2Dd(content.x, content.y + content.height), Point2Dd(content.x + w, content.y));
    }
    if (clip.fadeOut > 0.0) {
        const double w = clip.fadeOut * pixelsPerSecond;
        ctx->DrawLine(Point2Dd(content.x + content.width - w, content.y),
                      Point2Dd(content.x + content.width, content.y + content.height));
    }
}

void UltraCanvasVideoTimeline::DrawTransitions(IRenderContext* ctx, const TimelineTrack& track, const TrackLayout& lane) {
    const Rect2Df body = BodyRect();
    for (const TimelineTransition& t : track.transitions) {
        const Rect2Df r = TransitionRect(track, t, lane);
        if (r.width <= 0.0f || r.x + r.width < body.x || r.x > body.x + body.width) continue;
        const bool selected = selectedTransition == t.clipId;
        ctx->SetFillPaint(style.transitionFill);
        ctx->FillRoundedRectangle(Rect2Dd(r.x, r.y, r.width, r.height), 3.0);
        // the bowtie
        const double cx = r.x + 9.0, cy = r.y + r.height / 2.0;
        ctx->SetStrokePaint(style.onTransition);
        ctx->SetStrokeWidth(1.2f);
        ctx->DrawLinePath({Point2Dd(cx - 4.0, cy - 3.5), Point2Dd(cx + 4.0, cy + 3.5), Point2Dd(cx + 4.0, cy - 3.5),
                           Point2Dd(cx - 4.0, cy + 3.5)},
                          true);
        if (r.width >= 44.0f) {
            SetLabelFont(ctx, style.captionSize, true);
            ctx->SetTextPaint(style.onTransition);
            ctx->SetTextAlignment(TextAlignment::Left);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->DrawTextInRect(SecondsLabel(t.seconds), Rect2Dd(r.x + 17.0, r.y, r.width - 18.0, r.height));
        }
        if (selected) {
            ctx->SetStrokePaint(style.accent);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawRoundedRectangle(Rect2Dd(r.x + 1.0, r.y + 1.0, r.width - 2.0, r.height - 2.0), 3.0);
            ctx->SetFillPaint(style.accent);
            ctx->FillRectangle(Rect2Dd(r.x, r.y, 4.0, r.height));
            ctx->FillRectangle(Rect2Dd(r.x + r.width - 4.0, r.y, 4.0, r.height));
        }
    }
}

void UltraCanvasVideoTimeline::DrawGuides(IRenderContext* ctx, const Rect2Df& lanes) {
    if (model.HasRange()) {
        const float x0 = SecondsToX(model.rangeIn);
        const float x1 = SecondsToX(model.rangeOut);
        ctx->SetFillPaint(style.rangeFill);
        ctx->FillRectangle(Rect2Dd(x0, lanes.y, x1 - x0, lanes.height));
    }
    if (showBeats) {
        ctx->SetStrokePaint(style.beat);
        ctx->SetStrokeWidth(1.0f);
        for (double beat : model.beats) {
            const float x = SecondsToX(beat);
            if (x < lanes.x || x > lanes.x + lanes.width) continue;
            ctx->DrawLine(Point2Dd(std::floor(x) + 0.5, lanes.y), Point2Dd(std::floor(x) + 0.5, lanes.y + lanes.height));
        }
    }
    if (drag != Drag::None && dragSnapped) {
        const float x = SecondsToX(dragSnapTime);
        ctx->SetStrokePaint(style.focus);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(std::floor(x) + 0.5, lanes.y), Point2Dd(std::floor(x) + 0.5, lanes.y + lanes.height));
    }
}

void UltraCanvasVideoTimeline::DrawPlayhead(IRenderContext* ctx, const Rect2Df& body) {
    const float x = SecondsToX(playhead);
    if (x < body.x - 8.0f || x > body.x + body.width + 8.0f) return;
    const double cx = std::floor(x) + (style.playheadWidth > 1.5f ? 0.0 : 0.5);
    ctx->SetStrokePaint(style.playhead);
    ctx->SetStrokeWidth(style.playheadWidth);
    ctx->DrawLine(Point2Dd(cx, kMarkerStripHeight), Point2Dd(cx, body.y + body.height));
    ctx->SetFillPaint(style.playhead);
    ctx->FillLinePath({Point2Dd(cx - 6.0, kMarkerStripHeight + 1.0), Point2Dd(cx + 6.0, kMarkerStripHeight + 1.0),
                       Point2Dd(cx + 6.0, kMarkerStripHeight + 8.0), Point2Dd(cx, kMarkerStripHeight + 13.0),
                       Point2Dd(cx - 6.0, kMarkerStripHeight + 8.0)});
}

void UltraCanvasVideoTimeline::DrawDragFeedback(IRenderContext* ctx, const Rect2Df& lanes) {
    const std::vector<TrackLayout> layout = LayoutTracks();
    if (drag == Drag::MoveClip && dragInsertIndex >= 0 && dragTrackIndex < layout.size()) {
        const TimelineTrack& track = model.tracks[dragTrackIndex];
        if (track.IsMagnetic()) {
            // the insertion marker: before the clip at dragInsertIndex, or at the end
            double at = track.End();
            int shown = 0;
            for (const TimelineClip& c : track.clips) {
                if (c.id == dragClipId) continue;
                if (shown == dragInsertIndex) { at = c.start; break; }
                ++shown;
            }
            const TrackLayout& lane = layout[dragTrackIndex];
            const double x = std::floor(SecondsToX(at));
            ctx->SetFillPaint(style.accent);
            ctx->FillRectangle(Rect2Dd(x - 1.0, lane.top, 2.0, lane.height));
            ctx->FillLinePath({Point2Dd(x - 5.0, lane.top), Point2Dd(x + 5.0, lane.top), Point2Dd(x, lane.top + 5.0)});
            ctx->FillLinePath({Point2Dd(x - 5.0, lane.top + lane.height), Point2Dd(x + 5.0, lane.top + lane.height),
                               Point2Dd(x, lane.top + lane.height - 5.0)});
        }
    }
    if (drag == Drag::RubberBand) {
        ctx->SetFillPaint(style.dropSoft);
        ctx->FillRectangle(Rect2Dd(rubberBand.x, rubberBand.y, rubberBand.width, rubberBand.height));
        ctx->SetStrokePaint(style.focus);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Dd(rubberBand.x + 0.5, rubberBand.y + 0.5, rubberBand.width, rubberBand.height));
    }
    if (dropActive && dropTrackIndex < layout.size()) {
        const TrackLayout& lane = layout[dropTrackIndex];
        const double x = std::floor(SecondsToX(dropSeconds));
        ctx->SetFillPaint(style.focus);
        ctx->FillRectangle(Rect2Dd(x - 1.0, lane.top, 2.0, lane.height));
        std::string text = dropCount > 0 ? std::to_string(dropCount) + (dropCount == 1 ? " file" : " files") + " at " : "Insert at ";
        text += TimelineTimecode(dropSeconds, model.frameRate);
        DrawReadout(ctx, text, static_cast<float>(x) + 8.0f, lane.top + 4.0f);
    }
    if (tool == TimelineTool::Blade && drag == Drag::None && hover.kind == Hit::ClipBody) {
        const double x = std::floor(lastPointer.x) + 0.5;
        ctx->SetStrokePaint(style.ink);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(x, lanes.y), Point2Dd(x, lanes.y + lanes.height));
    }
}

void UltraCanvasVideoTimeline::DrawReadout(IRenderContext* ctx, const std::string& text, float x, float y) {
    SetLabelFont(ctx, style.readoutSize, true);
    const Size2Di size = ctx->GetTextDimensions(text, Size2Di(0, 0));
    const float w = static_cast<float>(size.width) + 12.0f;
    const float h = static_cast<float>(size.height) + 6.0f;
    const Rect2Df b = GetLocalBounds();
    float left = x + 10.0f;
    float top = y - h - 6.0f;
    if (left + w > b.width) left = b.width - w - 2.0f;
    if (top < style.rulerHeight) top = y + 16.0f;
    ctx->SetFillPaint(style.transitionFill);
    ctx->FillRoundedRectangle(Rect2Dd(left, top, w, h), 3.0);
    ctx->SetTextPaint(style.onTransition);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(text, Rect2Dd(left, top, w, h));
}

void UltraCanvasVideoTimeline::DrawScrollbar(IRenderContext* ctx, const Rect2Df& body) {
    const double visible = VisibleSeconds();
    const double total = std::max(model.Length() + visible * 0.25, visible);
    if (total <= 0.0) return;
    const float top = body.y + body.height - style.scrollbarHeight;
    ctx->SetFillPaint(style.header);
    ctx->FillRectangle(Rect2Dd(body.x, top, body.width, style.scrollbarHeight));
    const float thumbW = std::max(24.0f, static_cast<float>(body.width * visible / total));
    const float thumbX = body.x + static_cast<float>((body.width - thumbW) * (scrollSeconds / std::max(1e-9, total - visible)));
    ctx->SetFillPaint(style.scrollbar);
    ctx->FillRoundedRectangle(Rect2Dd(thumbX, top + 2.0, thumbW, style.scrollbarHeight - 4.0), 2.0);
}

// ============================================================================
// CACHES
// ============================================================================

const std::vector<UCPixmapPtr>* UltraCanvasVideoTimeline::Filmstrip(const TimelineClip& clip, int frames, int height) {
    const FilmstripKey key{clip.source, clip.sourceIn, clip.SourceOut(), frames, height};
    auto found = filmstrips.find(key);
    if (found != filmstrips.end()) return &found->second;
    if (!thumbnailProvider || filmstripsPending.count(key)) return nullptr;
    filmstripsPending.insert(key);
    std::weak_ptr<bool> alive = aliveToken;
    thumbnailProvider(clip.source, clip.sourceIn, clip.SourceOut(), frames, height,
                      [this, alive, key](std::vector<UCPixmapPtr> pixmaps) {
                          if (alive.expired()) return;
                          filmstripsPending.erase(key);
                          filmstrips[key] = std::move(pixmaps);
                          RequestRedraw();
                      });
    found = filmstrips.find(key);   // a provider may answer at once
    return found != filmstrips.end() ? &found->second : nullptr;
}

const WaveformChannelData* UltraCanvasVideoTimeline::Peaks(const TimelineClip& clip) {
    std::ostringstream key;
    key << clip.source << '|' << clip.sourceIn << '|' << clip.SourceOut();
    const std::string k = key.str();
    auto found = peaks.find(k);
    if (found != peaks.end()) return &found->second;
    if (!peaksProvider || peaksPending.count(k) || clip.source.empty()) return nullptr;
    peaksPending.insert(k);
    std::weak_ptr<bool> alive = aliveToken;
    peaksProvider(clip.source, clip.sourceIn, clip.SourceOut(), 50.0, [this, alive, k](WaveformChannelData data) {
        if (alive.expired()) return;
        peaksPending.erase(k);
        peaks[k] = std::move(data);
        RequestRedraw();
    });
    found = peaks.find(k);
    return found != peaks.end() ? &found->second : nullptr;
}

// ============================================================================
// EVENTS
// ============================================================================

UCMouseCursor UltraCanvasVideoTimeline::GetMouseCursor() const {
    if (drag == Drag::MoveClip) return UCMouseCursor::SizeAll;
    if (drag == Drag::TrimLeft || drag == Drag::TrimRight || drag == Drag::Transition) return UCMouseCursor::SizeWE;
    switch (hover.kind) {
        case Hit::ClipLeftEdge:
        case Hit::ClipRightEdge:
        case Hit::TransitionLeftEdge:
        case Hit::TransitionRightEdge:
            return UCMouseCursor::SizeWE;
        case Hit::ClipBody:
            if (tool == TimelineTool::Blade) return UCMouseCursor::Cross;
            if (tool == TimelineTool::Trim || tool == TimelineTool::Slip) return UCMouseCursor::SizeWE;
            return UCMouseCursor::Default;
        case Hit::HeaderToggle:
        case Hit::Marker:
            return UCMouseCursor::Hand;
        default:
            return UCMouseCursor::Default;
    }
}

bool UltraCanvasVideoTimeline::OnEvent(const UCEvent& event) {
    if (UltraCanvasUIElement::OnEvent(event)) return true;
    if (!IsVisible() || IsDisabled()) return false;
    const Point2Df inWindow = GetPositionInWindow();
    const float x = static_cast<float>(event.pointer.x) - inWindow.x;
    const float y = static_cast<float>(event.pointer.y) - inWindow.y;
    switch (event.type) {
        case UCEventType::MouseDown: return HandleMouseDown(event, x, y);
        case UCEventType::MouseMove: return HandleMouseMove(event, x, y);
        case UCEventType::MouseUp: return HandleMouseUp(event, x, y);
        case UCEventType::MouseDoubleClick: return HandleDoubleClick(event, x, y);
        case UCEventType::MouseWheel: return HandleWheel(event, x, y, false);
        case UCEventType::MouseWheelHorizontal: return HandleWheel(event, x, y, true);
        case UCEventType::MouseLeave:
            if (drag == Drag::None && hover.kind != Hit::Nothing) {
                hover = Hit();
                RequestRedraw();
            }
            return false;
        case UCEventType::KeyDown: return IsFocused() ? HandleKey(event) : false;
        case UCEventType::DragEnter:
        case UCEventType::DragOver: return HandleDrop(event, x, y, false);
        case UCEventType::DragLeave:
            if (dropActive) {
                dropActive = false;
                RequestRedraw();
            }
            return false;
        case UCEventType::Drop: return HandleDrop(event, x, y, true);
        default: return false;
    }
}

void UltraCanvasVideoTimeline::MovePlayheadTo(double seconds, bool notify) {
    playhead = TimelineToFrame(std::max(0.0, seconds), model.frameRate);
    if (notify && onPlayheadChanged) onPlayheadChanged(playhead);
    RequestRedraw();
}

void UltraCanvasVideoTimeline::SelectClip(const std::string& clipId, bool extend, bool toggle) {
    if (toggle) {
        if (selection.count(clipId)) selection.erase(clipId);
        else selection.insert(clipId);
    } else if (extend && !selection.empty()) {
        // the range between the first selected clip and this one, on its track
        const TimelineTrack* track = model.TrackOfClip(clipId);
        if (track) {
            int lo = track->ClipIndex(clipId), hi = lo;
            for (const std::string& id : selection) {
                const int i = track->ClipIndex(id);
                if (i < 0) continue;
                lo = std::min(lo, i);
                hi = std::max(hi, i);
            }
            for (int i = lo; i <= hi; ++i) selection.insert(track->clips[static_cast<size_t>(i)].id);
        }
    } else {
        selection.clear();
        selection.insert(clipId);
    }
    selectedTransition.clear();
    NotifySelection();
}

void UltraCanvasVideoTimeline::ClearSelection() {
    if (selection.empty() && selectedTransition.empty()) return;
    selection.clear();
    selectedTransition.clear();
    NotifySelection();
}

void UltraCanvasVideoTimeline::NotifySelection() {
    if (onSelectionChanged) onSelectionChanged(GetSelection());
    RequestRedraw();
}

bool UltraCanvasVideoTimeline::Request(const TimelineChange& change) {
    if (!onChangeRequested) return false;
    const bool accepted = onChangeRequested(change);
    RequestRedraw();
    return accepted;
}

bool UltraCanvasVideoTimeline::HandleMouseDown(const UCEvent& event, float x, float y) {
    const Hit hit = HitTest(x, y);
    if (hit.kind == Hit::Nothing) return false;
    SetFocus(true);
    lastPointer = Point2Df(x, y);
    dragStart = lastPointer;
    dragMoved = false;
    dragReadout.clear();
    dragSnapped = false;

    if (event.button == UCMouseButton::Right) {
        if ((hit.kind == Hit::ClipBody || hit.kind == Hit::ClipLeftEdge || hit.kind == Hit::ClipRightEdge) &&
            !selection.count(hit.clipId))
            SelectClip(hit.clipId, false, false);
        if (onContextMenuRequested) {
            const std::string trackId = hit.trackIndex < model.tracks.size() && hit.kind != Hit::Ruler && hit.kind != Hit::Scrollbar
                                            ? model.tracks[hit.trackIndex].id
                                            : "";
            const std::string clipId = hit.kind == Hit::ClipBody || hit.kind == Hit::ClipLeftEdge ||
                                               hit.kind == Hit::ClipRightEdge || hit.kind == Hit::TransitionBody
                                           ? hit.clipId
                                           : "";
            onContextMenuRequested(trackId, hit.seconds, clipId, event.pointer.x, event.pointer.y);
        }
        return true;
    }
    if (event.button != UCMouseButton::Left) return false;

    switch (hit.kind) {
        case Hit::Ruler:
        case Hit::Marker:
            drag = Drag::Playhead;
            MovePlayheadTo(hit.seconds, true);
            return true;
        case Hit::Scrollbar:
            drag = Drag::Scrollbar;
            scrollDragStart = scrollSeconds;
            return true;
        case Hit::HeaderToggle: {
            const TimelineTrack& track = model.tracks[hit.trackIndex];
            if (onTrackToggled) onTrackToggled(track.id, hit.toggle, !ToggleState(track, hit.toggle));
            return true;
        }
        case Hit::HeaderName:
            selectedTrack = model.tracks[hit.trackIndex].id;
            RequestRedraw();
            return true;
        case Hit::Lane:
            selectedTrack = model.tracks[hit.trackIndex].id;
            if (!event.shift && !event.ctrl) ClearSelection();
            drag = Drag::RubberBand;
            dragTrackIndex = hit.trackIndex;
            rubberBand = Rect2Df(x, y, 0.0f, 0.0f);
            RequestRedraw();
            return true;
        case Hit::TransitionBody:
        case Hit::TransitionLeftEdge:
        case Hit::TransitionRightEdge: {
            const TimelineTrack& track = model.tracks[hit.trackIndex];
            selection.clear();
            selectedTransition = hit.clipId;
            selectedTrack = track.id;
            NotifySelection();
            if (track.locked) return true;
            if (hit.kind != Hit::TransitionBody) {
                drag = Drag::Transition;
                dragTrackIndex = hit.trackIndex;
                dragClipId = hit.clipId;
                dragPreview = track;
                const TimelineTransition* t = track.FindTransition(hit.clipId);
                dragTime = t ? t->seconds : 1.0;
            }
            return true;
        }
        case Hit::ClipBody:
        case Hit::ClipLeftEdge:
        case Hit::ClipRightEdge: {
            const TimelineTrack& track = model.tracks[hit.trackIndex];
            selectedTrack = track.id;
            if (tool == TimelineTool::Blade && hit.kind == Hit::ClipBody) {
                RequestSplitAt(hit.seconds, hit.clipId);
                return true;
            }
            if (!(event.ctrl || event.meta) && !(selection.count(hit.clipId) && !event.shift))
                SelectClip(hit.clipId, event.shift, false);
            else if (event.ctrl || event.meta) SelectClip(hit.clipId, false, true);
            if (track.locked) return true;
            const TimelineClip* clip = track.FindClip(hit.clipId);
            if (!clip) return true;
            dragClipId = hit.clipId;
            dragTrackIndex = hit.trackIndex;
            dragClipOriginal = *clip;
            dragPreview = track;
            dragOffsetSeconds = hit.seconds - clip->start;
            const bool edge = hit.kind != Hit::ClipBody;
            if (edge || tool == TimelineTool::Trim) {
                const bool left = hit.kind == Hit::ClipLeftEdge ||
                                  (tool == TimelineTool::Trim && hit.seconds < clip->start + clip->duration / 2.0);
                drag = left ? Drag::TrimLeft : Drag::TrimRight;
                dragTime = left ? clip->start : clip->End();
            } else {
                drag = Drag::MoveClip;
                dragInsertIndex = -1;
            }
            return true;
        }
        default:
            return false;
    }
}

bool UltraCanvasVideoTimeline::HandleMouseMove(const UCEvent& event, float x, float y) {
    lastPointer = Point2Df(x, y);
    if (drag == Drag::None) {
        const Hit h = HitTest(x, y);
        const bool changed = h.kind != hover.kind || h.clipId != hover.clipId || h.trackIndex != hover.trackIndex ||
                             h.toggle != hover.toggle || tool == TimelineTool::Blade;
        hover = h;
        if (changed) RequestRedraw();
        return h.kind != Hit::Nothing;
    }
    if (std::fabs(x - dragStart.x) > 3.0f || std::fabs(y - dragStart.y) > 3.0f) dragMoved = true;
    const double pointerSeconds = XToSeconds(x);
    switch (drag) {
        case Drag::Playhead:
            MovePlayheadTo(std::max(0.0, pointerSeconds), true);
            dragReadout = TimelineTimecode(playhead, model.frameRate);
            return true;
        case Drag::Scrollbar: {
            const Rect2Df body = BodyRect();
            const double visible = VisibleSeconds();
            const double total = std::max(model.Length() + visible * 0.25, visible);
            const double perPixel = total / std::max(1.0f, body.width);
            scrollSeconds = scrollDragStart + (x - dragStart.x) * perPixel;
            ClampScroll();
            RequestRedraw();
            return true;
        }
        case Drag::RubberBand: {
            rubberBand = Rect2Df(std::min(x, dragStart.x), std::min(y, dragStart.y), std::fabs(x - dragStart.x),
                                 std::fabs(y - dragStart.y));
            // clips the band touches, live
            std::set<std::string> touched;
            const std::vector<TrackLayout> layout = LayoutTracks();
            for (const TrackLayout& lane : layout) {
                if (lane.top + lane.height < rubberBand.y || lane.top > rubberBand.y + rubberBand.height) continue;
                for (const TimelineClip& c : model.tracks[lane.index].clips) {
                    const Rect2Df r = ClipRect(c, lane);
                    if (r.x + r.width >= rubberBand.x && r.x <= rubberBand.x + rubberBand.width) touched.insert(c.id);
                }
            }
            if (touched != selection) {
                selection = touched;
                NotifySelection();
            }
            RequestRedraw();
            return true;
        }
        case Drag::MoveClip: {
            if (!dragMoved) return true;
            TimelineTrack& track = dragPreview;
            if (track.IsMagnetic()) {
                // the insertion index among the other clips, from the pointer against their midpoints
                int index = 0;
                for (const TimelineClip& c : model.tracks[dragTrackIndex].clips) {
                    if (c.id == dragClipId) continue;
                    if (pointerSeconds - dragOffsetSeconds + dragClipOriginal.duration / 2.0 > c.start + c.duration / 2.0) ++index;
                }
                dragInsertIndex = index;
                dragPreview = model.tracks[dragTrackIndex];
                TimelineMoveClip(dragPreview, dragClipId, index, 0.0);
                // the ghost rides with the pointer: show it at the pointer, the rest in their new order
                if (TimelineClip* ghost = dragPreview.FindClip(dragClipId)) ghost->start = std::max(0.0, pointerSeconds - dragOffsetSeconds);
                std::string landing = "Move to the end";
                int shown = 0;
                for (const TimelineClip& c : model.tracks[dragTrackIndex].clips) {
                    if (c.id == dragClipId) continue;
                    if (shown == index) { landing = "Move before " + c.name; break; }
                    ++shown;
                }
                dragReadout = landing;
                dragSnapped = false;
            } else {
                double start = std::max(0.0, pointerSeconds - dragOffsetSeconds);
                bool snapped = false;
                const double snappedStart = SnapTime(start, dragClipId, snapped);
                double snappedEnd = start;
                bool endSnapped = false;
                if (!snapped) {
                    snappedEnd = SnapTime(start + dragClipOriginal.duration, dragClipId, endSnapped);
                    if (endSnapped) start = snappedEnd - dragClipOriginal.duration;
                } else {
                    start = snappedStart;
                }
                dragSnapped = snapped || endSnapped;
                dragSnapTime = snapped ? snappedStart : snappedEnd;
                dragPreview = model.tracks[dragTrackIndex];
                TimelineMoveClip(dragPreview, dragClipId, 0, start);
                const TimelineClip* moved = dragPreview.FindClip(dragClipId);
                dragReadout = TimelineTimecode(moved ? moved->start : start, model.frameRate);
            }
            RequestRedraw();
            return true;
        }
        case Drag::TrimLeft:
        case Drag::TrimRight: {
            const bool left = drag == Drag::TrimLeft;
            bool snapped = false;
            double time = SnapTime(pointerSeconds, dragClipId, snapped);
            dragSnapped = snapped;
            dragSnapTime = time;
            dragPreview = model.tracks[dragTrackIndex];
            double applied = time;
            if (!TimelineTrimClip(dragPreview, dragClipId, left, time, model.frameRate, &applied)) {
                applied = left ? dragClipOriginal.start : dragClipOriginal.End();
            }
            dragTime = applied;
            const double delta = applied - (left ? dragClipOriginal.start : dragClipOriginal.End());
            dragReadout = std::string(left ? "In " : "Out ") + TimelineTimecode(applied, model.frameRate) + " \xC2\xB7 " +
                          (delta >= 0.0 ? "+" : "") + TimelineTimecode(delta, model.frameRate);
            RequestRedraw();
            return true;
        }
        case Drag::Transition: {
            dragPreview = model.tracks[dragTrackIndex];
            TimelineTransition* t = dragPreview.FindTransition(dragClipId);
            const TimelineClip* clip = dragPreview.FindClip(dragClipId);
            if (!t || !clip) return true;
            const double cut = clip->start;
            double seconds = 2.0 * std::fabs(pointerSeconds - cut);
            seconds = std::clamp(seconds, kMinTransitionSeconds, std::max(kMinTransitionSeconds, t->maxSeconds));
            seconds = TimelineToFrame(seconds, model.frameRate);
            if (seconds < kMinTransitionSeconds) seconds = kMinTransitionSeconds;
            t->seconds = seconds;
            TimelineDeriveStarts(dragPreview);
            dragTime = seconds;
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.1f s \xC2\xB7 max %.1f s", seconds, t->maxSeconds);
            dragReadout = buffer;
            RequestRedraw();
            return true;
        }
        default:
            return false;
    }
}

bool UltraCanvasVideoTimeline::HandleMouseUp(const UCEvent& event, float x, float y) {
    (void)event;
    if (drag == Drag::None) return false;
    FinishDrag(x, y);
    return true;
}

void UltraCanvasVideoTimeline::FinishDrag(float x, float y) {
    (void)x;
    (void)y;
    const Drag finished = drag;
    drag = Drag::None;
    dragReadout.clear();
    dragSnapped = false;
    if (dragTrackIndex >= model.tracks.size()) { RequestRedraw(); return; }
    const TimelineTrack& track = model.tracks[dragTrackIndex];
    TimelineChange change;
    change.trackId = track.id;
    switch (finished) {
        case Drag::MoveClip: {
            if (!dragMoved) break;
            change.kind = TimelineChangeKind::Move;
            change.ripple = track.IsMagnetic();
            if (track.IsMagnetic()) {
                TimelineTrack after = model.tracks[dragTrackIndex];
                if (!TimelineMoveClip(after, dragClipId, dragInsertIndex, 0.0)) break;
                change.before = track.clips;
                change.after = after.clips;
            } else {
                const TimelineClip* moved = dragPreview.FindClip(dragClipId);
                if (!moved || std::fabs(moved->start - dragClipOriginal.start) < 1e-9) break;
                change.before = {dragClipOriginal};
                change.after = {*moved};
                change.at = moved->start;
            }
            Request(change);
            break;
        }
        case Drag::TrimLeft:
        case Drag::TrimRight: {
            if (!dragMoved) break;
            const TimelineClip* trimmed = dragPreview.FindClip(dragClipId);
            if (!trimmed || (std::fabs(trimmed->start - dragClipOriginal.start) < 1e-9 &&
                             std::fabs(trimmed->duration - dragClipOriginal.duration) < 1e-9))
                break;
            change.kind = TimelineChangeKind::Trim;
            change.ripple = track.IsMagnetic();
            change.at = dragTime;
            if (track.IsMagnetic()) {
                change.before = track.clips;
                change.after = dragPreview.clips;
            } else {
                change.before = {dragClipOriginal};
                change.after = {*trimmed};
            }
            Request(change);
            break;
        }
        case Drag::Transition: {
            const TimelineTransition* before = track.FindTransition(dragClipId);
            const TimelineTransition* after = dragPreview.FindTransition(dragClipId);
            if (!before || !after || std::fabs(before->seconds - after->seconds) < 1e-9) break;
            change.kind = TimelineChangeKind::TransitionSet;
            if (const TimelineClip* clip = track.FindClip(dragClipId)) change.before = {*clip};
            change.transitionBefore = *before;
            change.transitionAfter = *after;
            change.ripple = track.IsMagnetic();
            Request(change);
            break;
        }
        default:
            break;
    }
    dragPreview = TimelineTrack();
    RequestRedraw();
}

bool UltraCanvasVideoTimeline::HandleDoubleClick(const UCEvent& event, float x, float y) {
    if (event.button != UCMouseButton::Left && event.button != UCMouseButton::NoneButton) return false;
    const Hit hit = HitTest(x, y);
    drag = Drag::None;
    switch (hit.kind) {
        case Hit::ClipBody:
        case Hit::ClipLeftEdge:
        case Hit::ClipRightEdge:
            if (onClipActivated) onClipActivated(hit.clipId);
            return true;
        case Hit::TransitionBody:
        case Hit::TransitionLeftEdge:
        case Hit::TransitionRightEdge:
            if (onTransitionActivated) onTransitionActivated(hit.clipId);
            return true;
        case Hit::Marker:
            for (const TimelineMarker& m : model.markers) {
                if (m.id == hit.markerId && onMarkerActivated) onMarkerActivated(m);
            }
            return true;
        default:
            return false;
    }
}

bool UltraCanvasVideoTimeline::HandleWheel(const UCEvent& event, float x, float y, bool horizontal) {
    const Rect2Df b = GetLocalBounds();
    if (x < 0.0f || y < 0.0f || x > b.width || y > b.height) return false;
    const int delta = event.wheelDelta;
    if (delta == 0) return false;
    if (horizontal || event.shift) {
        const double step = VisibleSeconds() * 0.1 * (delta > 0 ? -1.0 : 1.0);
        scrollSeconds += step;
        ClampScroll();
        RequestRedraw();
        return true;
    }
    const double around = XToSeconds(std::max(x, style.headerWidth));
    ZoomBy(delta > 0 ? 1.25 : 0.8, around);
    return true;
}

std::string UltraCanvasVideoTimeline::NextCut(double from, bool forward) const {
    double best = forward ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
    const double epsilon = 0.5 / std::max(1.0, model.frameRate);
    for (const TimelineTrack& t : model.tracks) {
        for (double cut : TimelineCuts(t)) {
            if (forward && cut > from + epsilon && cut < best) best = cut;
            if (!forward && cut < from - epsilon && cut > best) best = cut;
        }
    }
    if (!std::isfinite(best)) return "";
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6f", best);
    return buffer;
}

void UltraCanvasVideoTimeline::RequestSplitAt(double seconds, const std::string& clipId) {
    const TimelineTrack* track = model.TrackOfClip(clipId);
    if (!track || track->locked) return;
    TimelineTrack after = *track;
    const std::string rightId = clipId + ":2";
    if (!TimelineSplitClip(after, clipId, TimelineToFrame(seconds, model.frameRate), model.frameRate, rightId)) return;
    TimelineChange change;
    change.kind = TimelineChangeKind::Split;
    change.trackId = track->id;
    change.at = TimelineToFrame(seconds, model.frameRate);
    change.before = {*track->FindClip(clipId)};
    change.after = {*after.FindClip(clipId), *after.FindClip(rightId)};
    change.ripple = track->IsMagnetic();
    Request(change);
}

void UltraCanvasVideoTimeline::RequestDeleteSelection(bool invertRipple) {
    if (!selectedTransition.empty()) {
        const TimelineTrack* track = model.TrackOfClip(selectedTransition);
        const TimelineTransition* t = track ? track->FindTransition(selectedTransition) : nullptr;
        if (!track || !t || track->locked) return;
        TimelineChange change;
        change.kind = TimelineChangeKind::TransitionRemove;
        change.trackId = track->id;
        change.before = {*track->FindClip(selectedTransition)};
        change.transitionBefore = *t;
        change.ripple = track->IsMagnetic();
        if (Request(change)) { selectedTransition.clear(); NotifySelection(); }
        return;
    }
    if (selection.empty()) return;
    // one change per track that has selected clips
    for (const TimelineTrack& track : model.tracks) {
        if (track.locked) continue;
        TimelineChange change;
        change.kind = TimelineChangeKind::Delete;
        change.trackId = track.id;
        for (const TimelineClip& c : track.clips) if (selection.count(c.id)) change.before.push_back(c);
        if (change.before.empty()) continue;
        change.ripple = track.IsMagnetic() ? !invertRipple : invertRipple;
        TimelineTrack after = track;
        std::vector<std::string> ids;
        for (const TimelineClip& c : change.before) ids.push_back(c.id);
        TimelineDeleteClips(after, ids, change.ripple);
        if (track.IsMagnetic()) change.after = after.clips;
        Request(change);
    }
    selection.clear();
    NotifySelection();
}

void UltraCanvasVideoTimeline::RequestDuplicateSelection() {
    if (selection.size() != 1) return;
    const std::string id = *selection.begin();
    const TimelineTrack* track = model.TrackOfClip(id);
    if (!track || track->locked) return;
    TimelineTrack after = *track;
    const std::string copyId = id + ":copy";
    if (!TimelineDuplicateClip(after, id, copyId)) return;
    TimelineChange change;
    change.kind = TimelineChangeKind::Duplicate;
    change.trackId = track->id;
    change.before = {*track->FindClip(id)};
    change.after = track->IsMagnetic() ? after.clips : std::vector<TimelineClip>{*after.FindClip(copyId)};
    change.at = after.FindClip(copyId)->start;
    change.ripple = track->IsMagnetic();
    Request(change);
}

void UltraCanvasVideoTimeline::RequestNudgeSelection(int frames) {
    if (selection.size() != 1) return;
    const std::string id = *selection.begin();
    const TimelineTrack* track = model.TrackOfClip(id);
    if (!track || track->locked || track->IsMagnetic()) return;
    const TimelineClip* clip = track->FindClip(id);
    TimelineTrack after = *track;
    if (!TimelineMoveClip(after, id, 0, clip->start + frames / std::max(1.0, model.frameRate))) return;
    TimelineChange change;
    change.kind = TimelineChangeKind::Move;
    change.trackId = track->id;
    change.before = {*clip};
    change.after = {*after.FindClip(id)};
    change.at = change.after[0].start;
    Request(change);
}

void UltraCanvasVideoTimeline::RequestMarkerAt(double seconds) {
    TimelineChange change;
    change.kind = TimelineChangeKind::MarkerAdd;
    change.at = seconds;
    TimelineMarker m;
    m.id = "marker:" + std::to_string(static_cast<long long>(std::llround(seconds * 1000.0)));
    m.seconds = seconds;
    m.label = "Marker " + std::to_string(model.markers.size() + 1);
    change.markerAfter = m;
    Request(change);
}

bool UltraCanvasVideoTimeline::HandleKey(const UCEvent& event) {
    const bool control = event.ctrl || event.meta;
    const double frame = 1.0 / std::max(1.0, model.frameRate);
    switch (event.virtualKey) {
        case UCKeys::Left:
            MovePlayheadTo(playhead - (event.shift ? 1.0 : frame), true);
            if (followPlayhead) EnsurePlayheadVisible();
            return true;
        case UCKeys::Right:
            MovePlayheadTo(playhead + (event.shift ? 1.0 : frame), true);
            if (followPlayhead) EnsurePlayheadVisible();
            return true;
        case UCKeys::Up:
        case UCKeys::Down: {
            const std::string next = NextCut(playhead, event.virtualKey == UCKeys::Down);
            if (!next.empty()) {
                MovePlayheadTo(std::stod(next), true);   // locale-ok: our own "%.6f" text, parsed back the same way
                if (followPlayhead) EnsurePlayheadVisible();
            }
            return true;
        }
        case UCKeys::Home:
            MovePlayheadTo(0.0, true);
            scrollSeconds = 0.0;
            RequestRedraw();
            return true;
        case UCKeys::End:
            MovePlayheadTo(model.Length(), true);
            if (followPlayhead) EnsurePlayheadVisible();
            return true;
        case UCKeys::Equal:
            ZoomBy(1.25, playhead);
            return true;
        case UCKeys::Minus:
            ZoomBy(0.8, playhead);
            return true;
        case UCKeys::Comma:
            RequestNudgeSelection(-1);
            return true;
        case UCKeys::Period:
            RequestNudgeSelection(1);
            return true;
        case UCKeys::Delete:
        case UCKeys::Backspace:
            RequestDeleteSelection(event.shift);
            return true;
        case UCKeys::Escape:
            if (drag != Drag::None) {
                drag = Drag::None;
                dragPreview = TimelineTrack();
                dragReadout.clear();
                RequestRedraw();
            } else {
                ClearSelection();
            }
            return true;
        case UCKeys::A:
            if (control) {
                selection.clear();
                for (const TimelineTrack& t : model.tracks) {
                    if (!selectedTrack.empty() && t.id != selectedTrack) continue;
                    for (const TimelineClip& c : t.clips) selection.insert(c.id);
                }
                NotifySelection();
                return true;
            }
            return false;
        case UCKeys::B: SetTool(TimelineTool::Blade); return true;
        case UCKeys::D:
            if (control) { RequestDuplicateSelection(); return true; }
            return false;
        case UCKeys::I:
            if (onRangeChanged) onRangeChanged(playhead, model.HasRange() ? std::max(playhead, model.rangeOut) : 0.0);
            return true;
        case UCKeys::M: RequestMarkerAt(playhead); return true;
        case UCKeys::N: snapping = !snapping; RequestRedraw(); return true;
        case UCKeys::O:
            if (onRangeChanged) onRangeChanged(model.HasRange() ? std::min(playhead, model.rangeIn) : 0.0, playhead);
            return true;
        case UCKeys::P: {
            const TimelineTrack* track = model.FindTrack(selectedTrack);
            for (const TimelineTrack& t : model.tracks) if (!track && t.IsMain()) { track = &t; break; }
            if (track && track->IsMain() && onTrackToggled) onTrackToggled(track->id, TimelineTrackToggle::Magnetic, !track->magnetic);
            return true;
        }
        case UCKeys::S: {
            // split the selected clip under the playhead, or the clip under it on the selected track
            std::string clipId;
            for (const std::string& id : selection) {
                const TimelineTrack* t = model.TrackOfClip(id);
                const TimelineClip* c = t ? t->FindClip(id) : nullptr;
                if (c && playhead > c->start && playhead < c->End()) { clipId = id; break; }
            }
            if (clipId.empty()) {
                const TimelineTrack* t = model.FindTrack(selectedTrack);
                if (t) for (const TimelineClip& c : t->clips) if (playhead > c.start && playhead < c.End()) { clipId = c.id; break; }
            }
            if (!clipId.empty()) RequestSplitAt(playhead, clipId);
            return true;
        }
        case UCKeys::T: SetTool(TimelineTool::Trim); return true;
        case UCKeys::V: SetTool(TimelineTool::Select); return true;
        case UCKeys::Y: SetTool(TimelineTool::Slip); return true;
        case UCKeys::Z:
            if (event.shift && !control) { ZoomToFit(); return true; }
            return false;
        default:
            return false;
    }
}

bool UltraCanvasVideoTimeline::HandleDrop(const UCEvent& event, float x, float y, bool drop) {
    const Hit hit = HitTest(x, y);
    const bool onLane = hit.kind == Hit::Lane || hit.kind == Hit::ClipBody || hit.kind == Hit::ClipLeftEdge ||
                        hit.kind == Hit::ClipRightEdge || hit.kind == Hit::TransitionBody;
    if (!onLane || hit.trackIndex >= model.tracks.size()) {
        if (dropActive) { dropActive = false; RequestRedraw(); }
        return false;
    }
    const TimelineTrack& track = model.tracks[hit.trackIndex];
    double seconds = std::max(0.0, hit.seconds);
    if (track.IsMagnetic()) {   // the nearest cut
        double best = track.End();
        for (double cut : TimelineCuts(track)) if (std::fabs(cut - seconds) < std::fabs(best - seconds)) best = cut;
        seconds = best;
    } else {
        bool snapped = false;
        seconds = SnapTime(seconds, "", snapped);
    }
    if (drop) {
        dropActive = false;
        RequestRedraw();
        if (onFilesDropped && !event.droppedFiles.empty()) onFilesDropped(event.droppedFiles, track.id, seconds);
        return true;
    }
    dropActive = true;
    dropTrackIndex = hit.trackIndex;
    dropSeconds = seconds;
    dropCount = static_cast<int>(event.droppedFiles.size());
    RequestRedraw();
    return true;
}

// ============================================================================
// FACTORY
// ============================================================================

std::shared_ptr<UltraCanvasVideoTimeline> CreateVideoTimeline(const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasVideoTimeline>(id, x, y, w, h);
}

} // namespace UltraCanvas
