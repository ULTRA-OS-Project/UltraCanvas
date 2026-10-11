// UltraCanvasVideoTimelineModel.h
// The neutral model UltraCanvasVideoTimeline edits - clips on tracks,
// transitions on cuts, markers, beats and a work-area range - and the pure
// operations on it: deriving a magnetic track's clip starts, splitting,
// trimming, moving and deleting clips, the ruler's tick steps, timecode and
// snapping. Nothing here draws or depends on the framework, so the model
// builds and is tested without a display, and an application maps it to and
// from its own media engine (VideoFX segments, say) without the element ever
// knowing that engine's types.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

// What a clip is, which decides how it is drawn and what the app maps it to
enum class TimelineClipKind { Video, Image, Card, Title, Audio };

// What a track holds. MainVideo and MainAudio are the one magnetic track of
// each kind: their clips are a sequence (reordered, never placed at a free
// time, gaps closed), the others place clips freely.
enum class TimelineTrackKind { Title, Video, MainVideo, Audio, MainAudio };

struct TimelineClip {
    std::string id;                 // the app's key; the element never changes it
    TimelineClipKind kind = TimelineClipKind::Video;
    std::string name;               // shown in the band, truncated with an ellipsis
    std::string source;             // path for the thumbnail / peaks providers; empty for a card or a title
    double start = 0.0;             // seconds on the timeline (derived on a magnetic track)
    double duration = 1.0;          // seconds on the timeline, after speed
    double sourceIn = 0.0;          // seconds into the source where the clip begins
    double sourceLength = 0.0;      // seconds of source that exist; 0 = unknown or unbounded (a card, a title)
    double speed = 1.0;             // 0.25..4; the source used is duration * speed
    bool muted = false;
    bool hasEffects = false;        // badges only: the app keeps the real lists
    bool hasTitle = false;
    bool hasMotion = false;         // Image: a pan or zoom
    uint32_t color = 0x000000;      // Card: 0xRRGGBB
    double fadeIn = 0.0;            // Audio: seconds
    double fadeOut = 0.0;

    double End() const { return start + duration; }
    double SourceOut() const { return sourceIn + duration * speed; }
    // Source left beyond each edge, seconds of *timeline* time; 0 when unknown
    double SourceBefore() const { return speed > 0.0 ? sourceIn / speed : 0.0; }
    double SourceAfter() const {
        return sourceLength > 0.0 && speed > 0.0 ? (sourceLength - SourceOut()) / speed : 0.0;
    }
};

// A transition on the cut *into* the clip `clipId` from the one before it
struct TimelineTransition {
    std::string clipId;
    std::string type = "crossfade";   // the engine's name; drawn as its icon or label
    double seconds = 1.0;
    double maxSeconds = 5.0;          // what the readout shows as the limit
};

struct TimelineMarker {
    std::string id;
    double seconds = 0.0;
    uint32_t color = 0xF2A53A;        // 0xRRGGBB
    std::string label;
};

struct TimelineTrack {
    std::string id;                   // "V1", "A1", "T1"...
    TimelineTrackKind kind = TimelineTrackKind::Video;
    std::string name;
    bool magnetic = true;             // MainVideo / MainAudio only: ripple on trim and delete
    bool visible = true;
    bool muted = false;
    bool locked = false;
    bool collapsed = false;
    int height = 0;                   // logical pixels; 0 = the style's default for the kind
    std::vector<TimelineClip> clips;
    std::vector<TimelineTransition> transitions;

    bool IsMain() const {
        return kind == TimelineTrackKind::MainVideo || kind == TimelineTrackKind::MainAudio;
    }
    bool IsMagnetic() const { return IsMain() && magnetic; }
    const TimelineClip* FindClip(const std::string& clipId) const;
    TimelineClip* FindClip(const std::string& clipId);
    const TimelineTransition* FindTransition(const std::string& clipId) const;
    TimelineTransition* FindTransition(const std::string& clipId);
    int ClipIndex(const std::string& clipId) const;   // -1 when absent
    double End() const;                               // the end of the last clip
};

struct TimelineModel {
    double frameRate = 25.0;
    double length = 0.0;              // seconds; 0 = the end of the last clip on any track
    std::vector<TimelineTrack> tracks;   // top to bottom
    std::vector<TimelineMarker> markers;
    std::vector<double> beats;        // seconds; drawn when beat marks are shown
    double rangeIn = 0.0;             // work area; both 0 = none
    double rangeOut = 0.0;

    bool HasRange() const { return rangeOut > rangeIn; }
    double Length() const;            // `length`, or the furthest clip end
    const TimelineTrack* FindTrack(const std::string& trackId) const;
    TimelineTrack* FindTrack(const std::string& trackId);
    // The track holding `clipId`, or nullptr
    const TimelineTrack* TrackOfClip(const std::string& clipId) const;
    TimelineTrack* TrackOfClip(const std::string& clipId);
};

// ============================================================================
// CHANGES - what the element reports and the application applies
// ============================================================================
enum class TimelineChangeKind {
    Move,               // a clip moved: reordered on a magnetic track, placed on a free one
    Trim,               // an edge dragged
    Split,              // one clip became two at `at`
    Delete,             // the clips in `before` removed
    Duplicate,          // `after` holds the copy, placed after the original
    TransitionSet,      // `transitionAfter` added or changed on the cut into `before[0]`
    TransitionRemove,   // `transitionBefore` removed
    MarkerAdd,          // `markerAfter` added at `at`
    MarkerRemove,       // `markerBefore` removed
    FadeSet,            // Audio: `after[0]` carries the new fadeIn / fadeOut
};

struct TimelineChange {
    TimelineChangeKind kind = TimelineChangeKind::Move;
    std::string trackId;
    // The clips the change touches, as they were and as they would be. For a
    // magnetic track `after` holds every clip of the track in its new order
    // with derived starts, so the app sees the whole sequence it is asked for.
    std::vector<TimelineClip> before;
    std::vector<TimelineClip> after;
    std::optional<TimelineTransition> transitionBefore;
    std::optional<TimelineTransition> transitionAfter;
    std::optional<TimelineMarker> markerBefore;
    std::optional<TimelineMarker> markerAfter;
    double at = 0.0;        // split time, drop time, marker time (seconds on the timeline)
    bool ripple = false;    // what the magnet decided for this change
};

// ============================================================================
// OPERATIONS - pure functions; each returns false and leaves its track alone
// when the request cannot be met
// ============================================================================

// Set every clip's `start` of a magnetic track from the order of its clips:
// each begins where the previous ends, less the length of the transition
// into it. Free tracks are left as they are.
void TimelineDeriveStarts(TimelineTrack& track);

// The cut times of a track (every clip start and end), ascending, unique
std::vector<double> TimelineCuts(const TimelineTrack& track);

// Split the clip holding `at` into two at that time. The left keeps the id
// and everything else; the right gets `rightId`, the rest of the source and
// the same flags. False when `at` is not inside a clip or within a frame of
// its edges. Transitions into the clip stay on the left part.
bool TimelineSplitClip(TimelineTrack& track, const std::string& clipId, double at, double frameRate,
                       const std::string& rightId);

// Drag an edge of a clip to `newTime` (seconds on the timeline). The left
// edge keeps the clip's end and changes sourceIn; the right edge keeps its
// start. Limits: at least one frame long, no source before 0 or beyond
// sourceLength (when known), and on a free track not into a neighbour. On a
// magnetic track the clips after it follow (`TimelineDeriveStarts`). Returns
// the clamped time it used through `applied`, or false when nothing changed.
bool TimelineTrimClip(TimelineTrack& track, const std::string& clipId, bool leftEdge, double newTime,
                      double frameRate, double* applied = nullptr);

// Move a clip. On a magnetic track `newIndex` is its new place in the order
// and `newStart` is ignored; on a free track the clip is placed at `newStart`
// (clamped at 0 and against its neighbours) and `newIndex` is ignored.
bool TimelineMoveClip(TimelineTrack& track, const std::string& clipId, int newIndex, double newStart);

// Remove clips. On a magnetic track the rest close up; on a free track
// `ripple` moves every later clip left by the removed length.
bool TimelineDeleteClips(TimelineTrack& track, const std::vector<std::string>& clipIds, bool ripple);

// A copy of the clip placed right after it (a magnetic track) or in the
// first gap after it that holds it (a free track), with `copyId`
bool TimelineDuplicateClip(TimelineTrack& track, const std::string& clipId, const std::string& copyId);

// The ruler's tick steps for a zoom: the minor step is the smallest of one
// frame, five frames, 0.5, 1, 2, 5, 10, 30 and 60 seconds that puts minor
// ticks at least `minMinorPx` apart; the major step the smallest multiple of
// it that puts labels at least `minMajorPx` apart.
void TimelineRulerSteps(double pixelsPerSecond, double frameRate, double& minorStep, double& majorStep,
                        double minMinorPx = 8.0, double minMajorPx = 64.0);

// `HH:MM:SS:FF`; with `shortForm` `MM:SS` (or `H:MM:SS` past an hour).
// Negative times carry a leading `-`.
std::string TimelineTimecode(double seconds, double frameRate, bool shortForm = false);

// The candidate within `tolerance` seconds of `seconds`, nearest first, or
// nothing. Candidates need not be sorted.
std::optional<double> TimelineSnap(double seconds, const std::vector<double>& candidates, double tolerance);

// Round a time to the frame grid
double TimelineToFrame(double seconds, double frameRate);

// Whole frames <-> `HH:MM:SS:FF`, at the rounded frame rate (29.97 counts 30
// frames a second). A negative count carries a leading `-`.
std::string TimelineFramesToTimecode(long long frames, double frameRate);
// Parse `HH:MM:SS:FF`, or fewer groups counted from the right (`12` is 12
// frames, `5:12` five seconds and twelve frames, `1:05:12` a minute more), with
// `:`, `.`, `;` or a space between groups and an optional leading `+` or `-`.
// False for anything else, a group out of range (seconds 60, frame FF at or
// past the rate) or a count with more than four groups.
bool TimelineTimecodeToFrames(const std::string& text, double frameRate, long long& frames);

} // namespace UltraCanvas
