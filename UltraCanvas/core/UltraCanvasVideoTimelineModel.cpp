// UltraCanvasVideoTimelineModel.cpp
// The pure operations on the timeline model: deriving a magnetic track's
// starts, splitting, trimming, moving, deleting and duplicating clips, the
// ruler's steps, timecode and snapping. No framework dependency.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraCanvasVideoTimelineModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace UltraCanvas {

namespace {

double FrameSeconds(double frameRate) { return frameRate > 0.0 ? 1.0 / frameRate : 0.04; }

double TransitionInto(const TimelineTrack& track, const std::string& clipId) {
    const TimelineTransition* t = track.FindTransition(clipId);
    return t ? std::max(0.0, t->seconds) : 0.0;
}

// The clips of a free track in start order, as indices
std::vector<size_t> OrderByStart(const TimelineTrack& track) {
    std::vector<size_t> order(track.clips.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return track.clips[a].start < track.clips[b].start; });
    return order;
}

// The neighbours of `index` on a free track by time: the end of the clip
// before it and the start of the clip after it (infinity when none)
void FreeNeighbours(const TimelineTrack& track, size_t index, double& prevEnd, double& nextStart) {
    prevEnd = 0.0;
    nextStart = std::numeric_limits<double>::infinity();
    const TimelineClip& me = track.clips[index];
    for (size_t i = 0; i < track.clips.size(); ++i) {
        if (i == index) continue;
        const TimelineClip& c = track.clips[i];
        if (c.End() <= me.start + 1e-9) prevEnd = std::max(prevEnd, c.End());
        else if (c.start >= me.End() - 1e-9) nextStart = std::min(nextStart, c.start);
    }
}

} // namespace

// ---- track and model lookups ----

const TimelineClip* TimelineTrack::FindClip(const std::string& clipId) const {
    for (const TimelineClip& c : clips) if (c.id == clipId) return &c;
    return nullptr;
}
TimelineClip* TimelineTrack::FindClip(const std::string& clipId) {
    for (TimelineClip& c : clips) if (c.id == clipId) return &c;
    return nullptr;
}
const TimelineTransition* TimelineTrack::FindTransition(const std::string& clipId) const {
    for (const TimelineTransition& t : transitions) if (t.clipId == clipId) return &t;
    return nullptr;
}
TimelineTransition* TimelineTrack::FindTransition(const std::string& clipId) {
    for (TimelineTransition& t : transitions) if (t.clipId == clipId) return &t;
    return nullptr;
}
int TimelineTrack::ClipIndex(const std::string& clipId) const {
    for (size_t i = 0; i < clips.size(); ++i) if (clips[i].id == clipId) return static_cast<int>(i);
    return -1;
}
double TimelineTrack::End() const {
    double end = 0.0;
    for (const TimelineClip& c : clips) end = std::max(end, c.End());
    return end;
}

double TimelineModel::Length() const {
    if (length > 0.0) return length;
    double end = 0.0;
    for (const TimelineTrack& t : tracks) end = std::max(end, t.End());
    return end;
}
const TimelineTrack* TimelineModel::FindTrack(const std::string& trackId) const {
    for (const TimelineTrack& t : tracks) if (t.id == trackId) return &t;
    return nullptr;
}
TimelineTrack* TimelineModel::FindTrack(const std::string& trackId) {
    for (TimelineTrack& t : tracks) if (t.id == trackId) return &t;
    return nullptr;
}
const TimelineTrack* TimelineModel::TrackOfClip(const std::string& clipId) const {
    for (const TimelineTrack& t : tracks) if (t.FindClip(clipId)) return &t;
    return nullptr;
}
TimelineTrack* TimelineModel::TrackOfClip(const std::string& clipId) {
    for (TimelineTrack& t : tracks) if (t.FindClip(clipId)) return &t;
    return nullptr;
}

// ---- operations ----

void TimelineDeriveStarts(TimelineTrack& track) {
    if (!track.IsMagnetic()) return;
    double at = 0.0;
    for (size_t i = 0; i < track.clips.size(); ++i) {
        TimelineClip& c = track.clips[i];
        if (i > 0) at -= std::min(TransitionInto(track, c.id), c.duration);
        if (at < 0.0) at = 0.0;
        c.start = at;
        at += c.duration;
    }
}

std::vector<double> TimelineCuts(const TimelineTrack& track) {
    std::vector<double> cuts;
    cuts.reserve(track.clips.size() * 2);
    for (const TimelineClip& c : track.clips) {
        cuts.push_back(c.start);
        cuts.push_back(c.End());
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) { return std::fabs(a - b) < 1e-6; }),
               cuts.end());
    return cuts;
}

bool TimelineSplitClip(TimelineTrack& track, const std::string& clipId, double at, double frameRate,
                       const std::string& rightId) {
    const int index = track.ClipIndex(clipId);
    if (index < 0 || rightId.empty() || track.FindClip(rightId)) return false;
    TimelineClip& clip = track.clips[static_cast<size_t>(index)];
    const double frame = FrameSeconds(frameRate);
    if (at < clip.start + frame - 1e-9 || at > clip.End() - frame + 1e-9) return false;

    TimelineClip right = clip;
    right.id = rightId;
    const double leftDuration = at - clip.start;
    right.sourceIn = clip.sourceIn + leftDuration * clip.speed;
    right.duration = clip.duration - leftDuration;
    right.start = at;
    right.fadeIn = 0.0;
    clip.duration = leftDuration;
    clip.fadeOut = 0.0;
    track.clips.insert(track.clips.begin() + index + 1, right);
    TimelineDeriveStarts(track);
    return true;
}

bool TimelineTrimClip(TimelineTrack& track, const std::string& clipId, bool leftEdge, double newTime,
                      double frameRate, double* applied) {
    const int index = track.ClipIndex(clipId);
    if (index < 0) return false;
    TimelineClip& clip = track.clips[static_cast<size_t>(index)];
    const double frame = FrameSeconds(frameRate);
    const bool magnetic = track.IsMagnetic();
    double prevEnd = 0.0, nextStart = std::numeric_limits<double>::infinity();
    if (!magnetic) FreeNeighbours(track, static_cast<size_t>(index), prevEnd, nextStart);

    double time = TimelineToFrame(newTime, frameRate);
    if (leftEdge) {
        double lo = clip.End() - (clip.SourceBefore() + clip.duration);   // all the source before the in point
        if (clip.speed <= 0.0) lo = clip.start;
        if (!magnetic) lo = std::max(lo, prevEnd);
        lo = std::max(lo, 0.0);
        const double hi = clip.End() - frame;
        time = std::clamp(time, lo, hi);
        const double delta = time - clip.start;
        if (std::fabs(delta) < 1e-9) return false;
        clip.sourceIn = std::max(0.0, clip.sourceIn + delta * clip.speed);
        clip.duration -= delta;
        clip.start = time;
    } else {
        const double lo = clip.start + frame;
        double hi = std::numeric_limits<double>::infinity();
        if (clip.sourceLength > 0.0 && clip.speed > 0.0) hi = clip.start + (clip.sourceLength - clip.sourceIn) / clip.speed;
        if (!magnetic) hi = std::min(hi, nextStart);
        if (hi < lo) hi = lo;
        time = std::clamp(time, lo, hi);
        const double delta = time - clip.End();
        if (std::fabs(delta) < 1e-9) return false;
        clip.duration += delta;
    }
    if (applied) *applied = time;
    TimelineDeriveStarts(track);
    return true;
}

bool TimelineMoveClip(TimelineTrack& track, const std::string& clipId, int newIndex, double newStart) {
    const int index = track.ClipIndex(clipId);
    if (index < 0) return false;
    if (track.IsMagnetic()) {
        const int count = static_cast<int>(track.clips.size());
        newIndex = std::clamp(newIndex, 0, count - 1);
        if (newIndex == index) return false;
        TimelineClip clip = track.clips[static_cast<size_t>(index)];
        track.clips.erase(track.clips.begin() + index);
        track.clips.insert(track.clips.begin() + newIndex, clip);
        TimelineDeriveStarts(track);
        return true;
    }
    TimelineClip& clip = track.clips[static_cast<size_t>(index)];
    double start = std::max(0.0, newStart);
    // Against the neighbours it would have at the new place
    double lo = 0.0, hi = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < track.clips.size(); ++i) {
        if (static_cast<int>(i) == index) continue;
        const TimelineClip& c = track.clips[i];
        if (c.End() <= start + 1e-9) lo = std::max(lo, c.End());
        else if (c.start >= start + clip.duration - 1e-9) hi = std::min(hi, c.start - clip.duration);
        else {   // the new place overlaps c: land before or after it, whichever is nearer
            if (start + clip.duration / 2.0 < c.start + c.duration / 2.0) hi = std::min(hi, c.start - clip.duration);
            else lo = std::max(lo, c.End());
        }
    }
    if (hi < lo) return false;
    start = std::clamp(start, lo, hi);
    if (std::fabs(start - clip.start) < 1e-9) return false;
    clip.start = start;
    return true;
}

bool TimelineDeleteClips(TimelineTrack& track, const std::vector<std::string>& clipIds, bool ripple) {
    bool any = false;
    for (const std::string& id : clipIds) {
        const int index = track.ClipIndex(id);
        if (index < 0) continue;
        const TimelineClip removed = track.clips[static_cast<size_t>(index)];
        track.clips.erase(track.clips.begin() + index);
        track.transitions.erase(std::remove_if(track.transitions.begin(), track.transitions.end(),
                                               [&](const TimelineTransition& t) { return t.clipId == id; }),
                                track.transitions.end());
        if (!track.IsMagnetic() && ripple) {
            for (TimelineClip& c : track.clips)
                if (c.start >= removed.End() - 1e-9) c.start = std::max(0.0, c.start - removed.duration);
        }
        any = true;
    }
    if (any) TimelineDeriveStarts(track);
    return any;
}

bool TimelineDuplicateClip(TimelineTrack& track, const std::string& clipId, const std::string& copyId) {
    const int index = track.ClipIndex(clipId);
    if (index < 0 || copyId.empty() || track.FindClip(copyId)) return false;
    TimelineClip copy = track.clips[static_cast<size_t>(index)];
    copy.id = copyId;
    if (track.IsMagnetic()) {
        track.clips.insert(track.clips.begin() + index + 1, copy);
        TimelineDeriveStarts(track);
        return true;
    }
    // The first gap after the original that holds the copy
    double at = copy.End();
    const std::vector<size_t> order = OrderByStart(track);
    for (size_t i : order) {
        const TimelineClip& c = track.clips[i];
        if (c.End() <= at + 1e-9) continue;
        if (c.start >= at + copy.duration - 1e-9) break;
        at = c.End();
    }
    copy.start = at;
    track.clips.push_back(copy);
    return true;
}

void TimelineRulerSteps(double pixelsPerSecond, double frameRate, double& minorStep, double& majorStep,
                        double minMinorPx, double minMajorPx) {
    const double frame = FrameSeconds(frameRate);
    // The "nice" steps a ruler shows, frames first, then seconds and minutes
    const double steps[] = {frame, 5.0 * frame, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0,
                            120.0, 300.0, 600.0, 1800.0, 3600.0};
    const size_t count = sizeof steps / sizeof steps[0];
    minorStep = steps[count - 1];
    majorStep = minorStep;
    if (pixelsPerSecond <= 0.0) return;
    size_t minorIndex = count - 1;
    for (size_t i = 0; i < count; ++i) {
        if (steps[i] * pixelsPerSecond >= minMinorPx) { minorIndex = i; break; }
    }
    minorStep = steps[minorIndex];
    // The major step is a nice step too, a whole multiple of the minor one,
    // far enough apart for a label
    for (size_t i = minorIndex; i < count; ++i) {
        const double ratio = steps[i] / minorStep;
        const bool multiple = std::fabs(ratio - std::round(ratio)) < 1e-6;
        if (multiple && steps[i] * pixelsPerSecond >= minMajorPx) { majorStep = steps[i]; return; }
    }
    majorStep = minorStep;
    while (majorStep * pixelsPerSecond < minMajorPx) majorStep += minorStep;
}

std::string TimelineTimecode(double seconds, double frameRate, bool shortForm) {
    const bool negative = seconds < 0.0;
    if (negative) seconds = -seconds;
    const double fps = frameRate > 0.0 ? frameRate : 25.0;
    long long frames = static_cast<long long>(std::llround(seconds * fps));
    const long long perSecond = static_cast<long long>(std::llround(fps));
    const long long ff = perSecond > 0 ? frames % perSecond : 0;
    long long total = perSecond > 0 ? frames / perSecond : 0;
    const long long hh = total / 3600;
    total %= 3600;
    const long long mm = total / 60;
    const long long ss = total % 60;
    char buffer[40];
    if (shortForm) {
        if (hh > 0) std::snprintf(buffer, sizeof buffer, "%s%lld:%02lld:%02lld", negative ? "-" : "", hh, mm, ss);
        else std::snprintf(buffer, sizeof buffer, "%s%02lld:%02lld", negative ? "-" : "", mm, ss);
    } else {
        std::snprintf(buffer, sizeof buffer, "%s%02lld:%02lld:%02lld:%02lld", negative ? "-" : "", hh, mm, ss, ff);
    }
    return buffer;
}

std::optional<double> TimelineSnap(double seconds, const std::vector<double>& candidates, double tolerance) {
    std::optional<double> best;
    double bestDistance = tolerance;
    for (double c : candidates) {
        const double d = std::fabs(c - seconds);
        if (d <= bestDistance) {
            bestDistance = d;
            best = c;
        }
    }
    return best;
}

double TimelineToFrame(double seconds, double frameRate) {
    const double fps = frameRate > 0.0 ? frameRate : 25.0;
    return std::round(seconds * fps) / fps;
}

} // namespace UltraCanvas
