// Apps/UltraVideo/UltraVideoMapping.cpp
// Document <-> timeline model (UltraVideoMapping.h).
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraVideoMapping.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraVideo {

namespace {

constexpr double kMaxTransitionSeconds = 5.0;

const VideoFXSegment* SegmentPtr(const Document& doc, int index) {
    return index >= 0 && static_cast<size_t>(index) < doc.project.segments.size()
               ? &doc.project.segments[static_cast<size_t>(index)] : nullptr;
}

bool HasTextOverlay(const VideoFXSegment& s) {
    for (const VideoFXOverlay& o : s.overlays) if (o.kind == VideoFXOverlayKind::Text) return true;
    return false;
}

// A clip's edit written back into its segment: the range for a file (in
// source seconds, with the speed), the seconds for a still or a card
void ClipToSegment(const Document& doc, const TimelineClip& clip, VideoFXSegment& s) {
    if (s.kind == VideoFXSourceKind::File) {
        s.start = std::max(0.0, clip.sourceIn);
        s.end = clip.sourceIn + clip.duration * clip.speed;
        const MediaItem* m = doc.FindMedia(s.path);
        if (m && m->duration > 0.0) s.end = std::min(s.end, m->duration);
        s.speed = clip.speed;
    } else {
        s.duration = std::max(0.04, clip.duration);
    }
    s.mute = clip.muted;
}

} // namespace

const std::vector<TransitionChoice>& TransitionChoices() {
    static const std::vector<TransitionChoice> choices = {
        {VideoFXTransitionType::Cut, "cut", "Cut"},
        {VideoFXTransitionType::Crossfade, "crossfade", "Cross dissolve"},
        {VideoFXTransitionType::Dissolve, "dissolve", "Dissolve"},
        {VideoFXTransitionType::FadeThroughBlack, "fadeblack", "Fade through black"},
        {VideoFXTransitionType::FadeThroughWhite, "fadewhite", "Fade through white"},
        {VideoFXTransitionType::WipeLeft, "wipeleft", "Wipe left"},
        {VideoFXTransitionType::WipeRight, "wiperight", "Wipe right"},
        {VideoFXTransitionType::WipeUp, "wipeup", "Wipe up"},
        {VideoFXTransitionType::WipeDown, "wipedown", "Wipe down"},
        {VideoFXTransitionType::SlideLeft, "slideleft", "Slide left"},
        {VideoFXTransitionType::SlideRight, "slideright", "Slide right"},
        {VideoFXTransitionType::SlideUp, "slideup", "Slide up"},
        {VideoFXTransitionType::SlideDown, "slidedown", "Slide down"},
        {VideoFXTransitionType::SmoothLeft, "smoothleft", "Smooth left"},
        {VideoFXTransitionType::SmoothRight, "smoothright", "Smooth right"},
        {VideoFXTransitionType::SmoothUp, "smoothup", "Smooth up"},
        {VideoFXTransitionType::SmoothDown, "smoothdown", "Smooth down"},
        {VideoFXTransitionType::CircleOpen, "circleopen", "Circle open"},
        {VideoFXTransitionType::CircleClose, "circleclose", "Circle close"},
        {VideoFXTransitionType::CircleCrop, "circlecrop", "Circle crop"},
        {VideoFXTransitionType::RectCrop, "rectcrop", "Rectangle crop"},
        {VideoFXTransitionType::Radial, "radial", "Radial"},
        {VideoFXTransitionType::Pixelize, "pixelize", "Pixelize"},
        {VideoFXTransitionType::Blur, "blur", "Blur"},
        {VideoFXTransitionType::Distance, "distance", "Distance"},
        {VideoFXTransitionType::DiagonalTopLeft, "diagtl", "Diagonal from top left"},
        {VideoFXTransitionType::DiagonalTopRight, "diagtr", "Diagonal from top right"},
        {VideoFXTransitionType::DiagonalBottomLeft, "diagbl", "Diagonal from bottom left"},
        {VideoFXTransitionType::DiagonalBottomRight, "diagbr", "Diagonal from bottom right"},
        {VideoFXTransitionType::SqueezeHorizontal, "squeezeh", "Squeeze horizontally"},
        {VideoFXTransitionType::SqueezeVertical, "squeezev", "Squeeze vertically"},
    };
    return choices;
}

const TransitionChoice& ChoiceOf(VideoFXTransitionType type) {
    for (const TransitionChoice& c : TransitionChoices()) if (c.type == type) return c;
    return TransitionChoices().front();
}

int SegmentOfClip(const Document& doc, const std::string& clipId) {
    for (size_t i = 0; i < doc.clipIds.size(); ++i) if (doc.clipIds[i] == clipId) return static_cast<int>(i);
    return -1;
}

std::string ClipName(const Document& doc, size_t index) {
    const VideoFXSegment* s = SegmentPtr(doc, static_cast<int>(index));
    if (!s) return "";
    switch (s->kind) {
        case VideoFXSourceKind::File:
        case VideoFXSourceKind::Image: {
            if (const MediaItem* m = doc.FindMedia(s->path)) return m->name;
            return PathToUtf8(PathFromUtf8(s->path).filename());
        }
        case VideoFXSourceKind::Color: return "Colour card";
        case VideoFXSourceKind::TestPattern: return "Test pattern";
    }
    return "";
}

TimelineModel BuildTimelineModel(const Document& doc) {
    TimelineModel model;
    model.frameRate = doc.FrameRate();
    model.markers = doc.markers;
    model.rangeIn = doc.view.rangeIn;
    model.rangeOut = doc.view.rangeOut;

    // The title lane: free spans over the main track
    TimelineTrack titleTrack;
    titleTrack.id = kTitleTrackId;
    titleTrack.kind = TimelineTrackKind::Title;
    titleTrack.name = "Titles";
    titleTrack.magnetic = false;
    for (const TitleSpan& t : doc.titles) {
        TimelineClip clip;
        clip.id = t.id;
        clip.kind = TimelineClipKind::Title;
        clip.name = t.Name();
        clip.start = t.start;
        clip.duration = std::max(0.04, t.duration);
        clip.hasTitle = true;
        clip.fadeIn = t.fadeIn;
        clip.fadeOut = t.fadeOut;
        titleTrack.clips.push_back(clip);
    }
    std::stable_sort(titleTrack.clips.begin(), titleTrack.clips.end(),
                     [](const TimelineClip& a, const TimelineClip& b) { return a.start < b.start; });
    model.tracks.push_back(titleTrack);

    TimelineTrack main;
    main.id = kMainTrackId;
    main.kind = TimelineTrackKind::MainVideo;
    main.name = "Main track";
    main.magnetic = true;
    for (size_t i = 0; i < doc.project.segments.size(); ++i) {
        const VideoFXSegment& s = doc.project.segments[i];
        TimelineClip clip;
        clip.id = i < doc.clipIds.size() ? doc.clipIds[i] : "c?" + std::to_string(i);
        clip.name = ClipName(doc, i);
        clip.duration = std::max(0.04, doc.SegmentSeconds(i));
        clip.speed = s.speed;
        clip.muted = s.mute;
        clip.hasEffects = !s.effects.empty();
        clip.hasTitle = HasTextOverlay(s);
        switch (s.kind) {
            case VideoFXSourceKind::File: {
                const MediaItem* m = doc.FindMedia(s.path);
                clip.kind = (m && m->kind == MediaKind::Audio) ? TimelineClipKind::Audio : TimelineClipKind::Video;
                clip.source = s.path;
                clip.sourceIn = s.start;
                clip.sourceLength = m ? m->duration : 0.0;
                break;
            }
            case VideoFXSourceKind::Image:
                clip.kind = TimelineClipKind::Image;
                clip.source = s.path;
                clip.hasMotion = s.motion.style != VideoFXMotionStyle::Still;
                break;
            case VideoFXSourceKind::Color:
                clip.kind = TimelineClipKind::Card;
                clip.color = s.color;
                break;
            case VideoFXSourceKind::TestPattern:
                clip.kind = TimelineClipKind::Video;
                break;
        }
        main.clips.push_back(clip);
        if (i > 0 && !s.transitionIn.IsCut()) {
            TimelineTransition t;
            t.clipId = clip.id;
            t.type = ChoiceOf(s.transitionIn.type).key;
            t.seconds = s.transitionIn.duration;
            t.maxSeconds = std::min({kMaxTransitionSeconds, clip.duration, doc.SegmentSeconds(i - 1)});
            main.transitions.push_back(t);
        }
    }
    TimelineDeriveStarts(main);
    model.tracks.push_back(main);

    // The music lane: one bar per song, edited in the inspector (locked here)
    TimelineTrack music;
    music.id = kMusicTrackId;
    music.kind = TimelineTrackKind::MainAudio;
    music.name = "Music";
    music.magnetic = true;
    music.locked = true;
    const VideoFXMusic& m = doc.project.settings.music;
    if (m.IsSet()) {
        const std::vector<std::string> songs = m.Songs();
        const double length = doc.Length();
        for (size_t i = 0; i < songs.size(); ++i) {
            TimelineClip song;
            song.id = "music:" + std::to_string(i);
            song.kind = TimelineClipKind::Audio;
            song.source = songs[i];
            const MediaItem* item = doc.FindMedia(songs[i]);
            song.name = item ? item->name : PathToUtf8(PathFromUtf8(songs[i]).filename());
            song.sourceIn = i == 0 ? m.start : 0.0;
            song.sourceLength = item ? item->duration : 0.0;
            double seconds = song.sourceLength > 0.0 ? song.sourceLength - song.sourceIn : length;
            if (songs.size() == 1 && length > 0.0) seconds = std::min(seconds, length);   // fitted to the video
            song.duration = std::max(0.04, seconds);
            song.fadeIn = i == 0 ? m.fadeIn : 0.0;
            song.fadeOut = i + 1 == songs.size() ? m.fadeOut : 0.0;
            music.clips.push_back(song);
            if (i > 0 && m.crossfade > 0.0) {
                TimelineTransition t;
                t.clipId = song.id;
                t.type = "crossfade";
                t.seconds = m.crossfade;
                t.maxSeconds = 30.0;
                music.transitions.push_back(t);
            }
        }
        TimelineDeriveStarts(music);
    }
    model.tracks.push_back(music);
    return model;
}

std::string DescribeChange(const TimelineChange& c) {
    switch (c.kind) {
        case TimelineChangeKind::Move: return "Move clip";
        case TimelineChangeKind::Trim: return "Trim clip";
        case TimelineChangeKind::Split: return "Split clip";
        case TimelineChangeKind::Delete: return c.before.size() > 1 ? "Delete clips" : "Delete clip";
        case TimelineChangeKind::Duplicate: return "Duplicate clip";
        case TimelineChangeKind::TransitionSet: return "Set transition";
        case TimelineChangeKind::TransitionRemove: return "Remove transition";
        case TimelineChangeKind::MarkerAdd: return "Add marker";
        case TimelineChangeKind::MarkerRemove: return "Remove marker";
        case TimelineChangeKind::FadeSet: return "Set fade";
    }
    return "Edit";
}

bool ApplyChange(Document& doc, const TimelineChange& c) {
    if (c.kind == TimelineChangeKind::MarkerAdd) {
        if (!c.markerAfter) return false;
        doc.markers.push_back(*c.markerAfter);
        return true;
    }
    if (c.kind == TimelineChangeKind::MarkerRemove) {
        if (!c.markerBefore) return false;
        const size_t before = doc.markers.size();
        doc.markers.erase(std::remove_if(doc.markers.begin(), doc.markers.end(),
                                         [&](const TimelineMarker& m) { return m.id == c.markerBefore->id; }),
                          doc.markers.end());
        return doc.markers.size() != before;
    }
    if (c.trackId == kTitleTrackId) {
        // A free lane: the change names the spans it touches with their new place
        switch (c.kind) {
            case TimelineChangeKind::Move:
            case TimelineChangeKind::Trim: {
                for (const TimelineClip& clip : c.after) {
                    const int index = doc.TitleIndex(clip.id);
                    if (index < 0) return false;
                    TitleSpan& t = doc.titles[static_cast<size_t>(index)];
                    t.start = std::max(0.0, clip.start);
                    t.duration = std::max(0.04, clip.duration);
                }
                return !c.after.empty();
            }
            case TimelineChangeKind::Split: {
                if (c.before.size() != 1 || c.after.size() != 2) return false;
                const int index = doc.TitleIndex(c.before[0].id);
                if (index < 0) return false;
                TitleSpan right = doc.titles[static_cast<size_t>(index)];
                TitleSpan& left = doc.titles[static_cast<size_t>(index)];
                left.start = c.after[0].start;
                left.duration = std::max(0.04, c.after[0].duration);
                left.fadeOut = 0.0;
                right.id = doc.NewTitleId();
                right.start = c.after[1].start;
                right.duration = std::max(0.04, c.after[1].duration);
                right.fadeIn = 0.0;
                doc.titles.push_back(right);
                return true;
            }
            case TimelineChangeKind::Delete: {
                size_t removed = 0;
                for (const TimelineClip& clip : c.before) {
                    const int index = doc.TitleIndex(clip.id);
                    if (index < 0) continue;
                    doc.titles.erase(doc.titles.begin() + index);
                    ++removed;
                }
                return removed > 0;
            }
            case TimelineChangeKind::Duplicate: {
                if (c.before.size() != 1) return false;
                const int index = doc.TitleIndex(c.before[0].id);
                if (index < 0) return false;
                TitleSpan copy = doc.titles[static_cast<size_t>(index)];
                copy.id = doc.NewTitleId();
                for (const TimelineClip& clip : c.after) {
                    if (clip.id != c.before[0].id) { copy.start = clip.start; copy.duration = std::max(0.04, clip.duration); }
                }
                doc.titles.push_back(copy);
                return true;
            }
            default:
                return false;
        }
    }
    if (c.trackId != kMainTrackId) return false;   // the music lane is edited in the inspector

    std::vector<VideoFXSegment>& segments = doc.project.segments;
    std::vector<std::string>& ids = doc.clipIds;
    if (ids.size() != segments.size()) return false;

    switch (c.kind) {
        case TimelineChangeKind::Move:
        case TimelineChangeKind::Trim:
        case TimelineChangeKind::Delete:
        case TimelineChangeKind::Duplicate: {
            // `after` is the whole magnetic sequence: rebuild the segments in that order
            std::map<std::string, size_t> indexOf;
            for (size_t i = 0; i < ids.size(); ++i) indexOf[ids[i]] = i;
            std::vector<VideoFXSegment> rebuilt;
            std::vector<std::string> rebuiltIds;
            for (const TimelineClip& clip : c.after) {
                std::string id = clip.id;
                auto found = indexOf.find(id);
                if (found == indexOf.end()) {
                    // a copy: `<id>:copy` of the original
                    const std::string::size_type colon = id.rfind(':');
                    if (colon == std::string::npos) return false;
                    found = indexOf.find(id.substr(0, colon));
                    if (found == indexOf.end()) return false;
                    id = doc.NewClipId();
                }
                VideoFXSegment s = segments[found->second];
                ClipToSegment(doc, clip, s);
                rebuilt.push_back(s);
                rebuiltIds.push_back(id);
            }
            if (c.kind != TimelineChangeKind::Delete && rebuilt.empty()) return false;
            // Transitions stay with the clip they lead into; the first clip has none
            segments = rebuilt;
            ids = rebuiltIds;
            if (!segments.empty()) segments.front().transitionIn = VideoFXTransition{};
            return true;
        }
        case TimelineChangeKind::Split: {
            if (c.before.size() != 1 || c.after.size() != 2) return false;
            const int index = SegmentOfClip(doc, c.before[0].id);
            if (index < 0) return false;
            VideoFXSegment left = segments[static_cast<size_t>(index)];
            VideoFXSegment right = left;
            ClipToSegment(doc, c.after[0], left);
            ClipToSegment(doc, c.after[1], right);
            right.transitionIn = VideoFXTransition{};
            segments[static_cast<size_t>(index)] = left;
            segments.insert(segments.begin() + index + 1, right);
            ids.insert(ids.begin() + index + 1, doc.NewClipId());
            return true;
        }
        case TimelineChangeKind::TransitionSet: {
            if (!c.transitionAfter) return false;
            const int index = SegmentOfClip(doc, c.transitionAfter->clipId);
            if (index <= 0) return false;   // nothing leads into the first clip
            VideoFXTransitionType type = VideoFXTransitionType::Crossfade;
            for (const TransitionChoice& choice : TransitionChoices())
                if (c.transitionAfter->type == choice.key) type = choice.type;
            if (type == VideoFXTransitionType::Cut) {
                segments[static_cast<size_t>(index)].transitionIn = VideoFXTransition{};
            } else {
                segments[static_cast<size_t>(index)].transitionIn =
                    VideoFXTransition::Make(type, std::clamp(c.transitionAfter->seconds, 0.04, 10.0));
            }
            return true;
        }
        case TimelineChangeKind::TransitionRemove: {
            if (!c.transitionBefore) return false;
            const int index = SegmentOfClip(doc, c.transitionBefore->clipId);
            if (index < 0) return false;
            segments[static_cast<size_t>(index)].transitionIn = VideoFXTransition{};
            return true;
        }
        case TimelineChangeKind::FadeSet:
        case TimelineChangeKind::MarkerAdd:
        case TimelineChangeKind::MarkerRemove:
            return false;
    }
    return false;
}

} // namespace UltraVideo
