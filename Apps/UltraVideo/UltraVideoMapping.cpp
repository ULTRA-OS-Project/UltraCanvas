// Apps/UltraVideo/UltraVideoMapping.cpp
// Document <-> timeline model (UltraVideoMapping.h).
// Version: 0.4.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#include "UltraVideoMapping.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cmath>
#include <functional>
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

// ============================================================================
// the story wizard
// ============================================================================
namespace {
bool IsPictureFile(const std::string& path) { return Document::KindOfPath(path) == MediaKind::Image; }
bool IsSoundFile(const std::string& path) { return Document::KindOfPath(path) == MediaKind::Audio; }

std::string CaptionFromName(const std::string& path) {
    std::string stem = PathToUtf8(PathFromUtf8(path).stem());
    for (char& c : stem) if (c == '_' || c == '-') c = ' ';
    return stem;
}
}

bool PlanStory(const StoryChoice& choice, std::vector<VideoFXSegment>& imageSegments, VideoFXExportSettings& settings,
               std::string& error) {
    error.clear();
    imageSegments.clear();
    std::vector<std::string> pictures;
    for (const std::string& f : choice.files) if (IsPictureFile(f)) pictures.push_back(f);
    settings = VideoFXExportSettings{};
    if (!choice.songs.empty()) {
        settings.music = VideoFXMusic::FromFiles(choice.songs);
        settings.music.SetDuckingPreset(choice.ducking);
    }
    // The framing choice is for the photos, and the clips follow it: a
    // clip whose shape differs from the frame fills it or is shown whole
    // (the engine frames files with the export's fit mode, not per clip)
    settings.fitMode = choice.portraitFit == VideoFXImageFit::Cover ? VideoFXFitMode::Fill : VideoFXFitMode::Letterbox;
    if (pictures.empty()) return true;   // clips alone: nothing for the engine to plan

    VideoFXSlideshowOptions options;
    options.secondsPerImage = std::clamp(choice.secondsPerPhoto, 0.5, 60.0);
    options.transition = choice.transition == VideoFXTransitionType::Cut
                             ? VideoFXTransition{}
                             : VideoFXTransition::Make(choice.transition, std::clamp(choice.transitionSeconds, 0.04, 5.0));
    options.imageFit = choice.portraitFit;
    options.keepFacesInView = choice.keepFaces;
    options.fadeInOut = choice.fadeInOut;
    options.matchMusicLength = choice.fitToMusic && !choice.songs.empty();
    options.beatSync = choice.beatSync && !choice.songs.empty();
    if (choice.captionsFromNames) for (const std::string& p : pictures) options.captions.push_back(CaptionFromName(p));
    VideoFXExportSettings planned;
    const VideoFXResult r = VideoFX_PlanSlideshow(pictures, options, settings, imageSegments, planned);
    if (r != VideoFXResult::Ok) {
        error = VideoFX_GetLastError();
        if (error.empty()) error = VideoFX_ResultToString(r);
        return false;
    }
    settings = planned;
    settings.fitMode = choice.portraitFit == VideoFXImageFit::Cover ? VideoFXFitMode::Fill : VideoFXFitMode::Letterbox;
    return true;
}

Document BuildStoryDocument(const StoryChoice& choice, const std::vector<VideoFXSegment>& imageSegments,
                            const VideoFXExportSettings& settings) {
    Document doc;
    doc.project.title = "Story";
    doc.project.settings = settings;
    for (const std::string& f : choice.files) doc.Import(f);
    for (const std::string& song : choice.songs) doc.Import(song);
    const VideoFXTransition transition = choice.transition == VideoFXTransitionType::Cut
                                             ? VideoFXTransition{}
                                             : VideoFXTransition::Make(choice.transition, std::clamp(choice.transitionSeconds, 0.04, 5.0));
    // The photos' segments in order, the clips between them where they were chosen
    size_t nextImage = 0;
    std::vector<std::string> captions;   // one per segment, "" for none
    for (const std::string& f : choice.files) {
        if (IsSoundFile(f)) continue;
        VideoFXSegment segment;
        std::string caption;
        if (IsPictureFile(f)) {
            if (nextImage >= imageSegments.size()) continue;
            segment = imageSegments[nextImage++];
            for (const VideoFXOverlay& o : segment.overlays) if (o.kind == VideoFXOverlayKind::Text && caption.empty()) caption = o.text;
            segment.overlays.erase(std::remove_if(segment.overlays.begin(), segment.overlays.end(),
                                                  [](const VideoFXOverlay& o) { return o.kind == VideoFXOverlayKind::Text; }),
                                   segment.overlays.end());
        } else {
            const MediaItem* m = doc.FindMedia(f);
            if (!m || !m->error.empty()) continue;
            segment = VideoFXSegment::FromFile(m->path);
            segment.transitionIn = transition;
            if (choice.captionsFromNames) caption = CaptionFromName(f);
        }
        doc.project.segments.push_back(segment);
        doc.clipIds.push_back(doc.NewClipId());
        captions.push_back(caption);
    }
    // The fades at the ends belong to the first and the last segment, whatever they are
    for (size_t i = 0; i < doc.project.segments.size(); ++i) {
        std::vector<VideoFXEffect>& effects = doc.project.segments[i].effects;
        effects.erase(std::remove_if(effects.begin(), effects.end(), [](const VideoFXEffect& e) {
                          return e.type == VideoFXEffectType::FadeIn || e.type == VideoFXEffectType::FadeOut; }),
                      effects.end());
        if (choice.fadeInOut && i == 0) effects.insert(effects.begin(), VideoFXEffect::FadeIn(0.5));
        if (choice.fadeInOut && i + 1 == doc.project.segments.size()) effects.push_back(VideoFXEffect::FadeOut(0.5));
    }
    if (!doc.project.segments.empty()) doc.project.segments.front().transitionIn = VideoFXTransition{};
    // Captions become title spans, one per captioned segment
    const std::vector<double> starts = doc.SegmentStarts();
    for (size_t i = 0; i < captions.size() && i < starts.size(); ++i) {
        if (captions[i].empty()) continue;
        TitleSpan span;
        span.id = doc.NewTitleId();
        const TitleStyle* style = FindTitleStyle("caption");
        span.style = style ? style->key : "caption";
        if (style) span.look = style->look;
        span.look.text = captions[i];
        span.start = starts[i];
        span.duration = std::max(0.04, doc.SegmentSeconds(i));
        span.fadeIn = style ? style->fadeIn : 0.25;
        span.fadeOut = style ? style->fadeOut : 0.25;
        doc.titles.push_back(span);
    }
    return doc;
}

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

std::vector<std::string> SongIds(const Document& doc) {
    std::vector<std::string> ids;
    const size_t n = doc.project.settings.music.Songs().size();
    for (size_t i = 0; i < n; ++i) ids.push_back("music:" + std::to_string(i));
    return ids;
}

int SongOfClip(const std::string& clipId) {
    if (clipId.rfind("music:", 0) != 0) return -1;
    try {
        return std::stoi(clipId.substr(6));
    } catch (...) {
        return -1;
    }
}

bool RemoveSong(Document& doc, size_t index) {
    VideoFXMusic& m = doc.project.settings.music;
    std::vector<std::string> songs = m.Songs();
    if (index >= songs.size()) return false;
    songs.erase(songs.begin() + static_cast<std::ptrdiff_t>(index));
    if (index == 0) m.start = 0.0;
    if (songs.empty()) {
        m = VideoFXMusic{};
        return true;
    }
    m.path = songs.front();
    m.playlist.assign(songs.begin() + 1, songs.end());
    return true;
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

    // The overlay-video lane: free clips of other files' pictures over the main track
    TimelineTrack overlayTrack;
    overlayTrack.id = kOverlayTrackId;
    overlayTrack.kind = TimelineTrackKind::Video;
    overlayTrack.name = "Overlay video";
    overlayTrack.magnetic = false;
    for (const OverlaySpan& v : doc.overlays) {
        TimelineClip clip;
        clip.id = v.id;
        clip.kind = v.IsStill() ? TimelineClipKind::Image : TimelineClipKind::Video;
        clip.name = v.Name();
        clip.source = v.path;
        clip.start = v.start;
        clip.duration = std::max(0.04, v.duration);
        clip.sourceIn = v.sourceIn;
        const MediaItem* m = doc.FindMedia(v.path);
        clip.sourceLength = m && !v.IsStill() ? m->duration : 0.0;
        clip.hasEffects = !v.look.effects.empty();
        overlayTrack.clips.push_back(clip);
    }
    std::stable_sort(overlayTrack.clips.begin(), overlayTrack.clips.end(),
                     [](const TimelineClip& a, const TimelineClip& b) { return a.start < b.start; });
    model.tracks.push_back(overlayTrack);

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

    // The voice-and-sound lane: free clips of sound under the main track
    TimelineTrack soundTrack;
    soundTrack.id = kSoundTrackId;
    soundTrack.kind = TimelineTrackKind::Audio;
    soundTrack.name = "Voice & sound";
    soundTrack.magnetic = false;
    for (const SoundSpan& a : doc.sounds) {
        TimelineClip clip;
        clip.id = a.id;
        clip.kind = TimelineClipKind::Audio;
        clip.name = a.Name();
        clip.source = a.path;
        clip.start = a.start;
        clip.duration = std::max(0.04, a.duration);
        clip.sourceIn = a.sourceIn;
        const MediaItem* m = doc.FindMedia(a.path);
        clip.sourceLength = m ? m->duration : 0.0;
        clip.muted = a.volume <= 0.0;
        clip.fadeIn = a.fadeIn;
        clip.fadeOut = a.fadeOut;
        soundTrack.clips.push_back(clip);
    }
    std::stable_sort(soundTrack.clips.begin(), soundTrack.clips.end(),
                     [](const TimelineClip& a, const TimelineClip& b) { return a.start < b.start; });
    model.tracks.push_back(soundTrack);

    // The music lane: one bar per song, edited in the inspector (locked here)
    TimelineTrack music;
    music.id = kMusicTrackId;
    music.kind = TimelineTrackKind::MainAudio;
    music.name = "Music";
    music.magnetic = true;
    music.locked = false;
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
    if (c.trackId == kOverlayTrackId) {
        // A free lane like the titles', whose clips also carry where in the file they begin
        switch (c.kind) {
            case TimelineChangeKind::Move:
            case TimelineChangeKind::Trim: {
                for (const TimelineClip& clip : c.after) {
                    const int index = doc.OverlayIndex(clip.id);
                    if (index < 0) return false;
                    OverlaySpan& v = doc.overlays[static_cast<size_t>(index)];
                    v.start = std::max(0.0, clip.start);
                    v.duration = std::max(0.04, clip.duration);
                    v.sourceIn = std::max(0.0, clip.sourceIn);
                }
                return !c.after.empty();
            }
            case TimelineChangeKind::Split: {
                if (c.before.size() != 1 || c.after.size() != 2) return false;
                const int index = doc.OverlayIndex(c.before[0].id);
                if (index < 0) return false;
                OverlaySpan right = doc.overlays[static_cast<size_t>(index)];
                OverlaySpan& left = doc.overlays[static_cast<size_t>(index)];
                left.start = c.after[0].start;
                left.duration = std::max(0.04, c.after[0].duration);
                left.fadeOut = 0.0;
                right.id = doc.NewOverlayId();
                right.start = c.after[1].start;
                right.duration = std::max(0.04, c.after[1].duration);
                right.sourceIn = std::max(0.0, c.after[1].sourceIn);
                right.fadeIn = 0.0;
                doc.overlays.push_back(right);
                return true;
            }
            case TimelineChangeKind::Delete: {
                size_t removed = 0;
                for (const TimelineClip& clip : c.before) {
                    const int index = doc.OverlayIndex(clip.id);
                    if (index < 0) continue;
                    doc.overlays.erase(doc.overlays.begin() + index);
                    ++removed;
                }
                return removed > 0;
            }
            case TimelineChangeKind::Duplicate: {
                if (c.before.size() != 1) return false;
                const int index = doc.OverlayIndex(c.before[0].id);
                if (index < 0) return false;
                OverlaySpan copy = doc.overlays[static_cast<size_t>(index)];
                copy.id = doc.NewOverlayId();
                for (const TimelineClip& clip : c.after) {
                    if (clip.id != c.before[0].id) { copy.start = clip.start; copy.duration = std::max(0.04, clip.duration); }
                }
                doc.overlays.push_back(copy);
                return true;
            }
            default:
                return false;
        }
    }
    if (c.trackId == kSoundTrackId) {
        // A free lane like the overlays', whose fades are edited on the clip too
        switch (c.kind) {
            case TimelineChangeKind::Move:
            case TimelineChangeKind::Trim: {
                for (const TimelineClip& clip : c.after) {
                    const int index = doc.SoundIndex(clip.id);
                    if (index < 0) return false;
                    SoundSpan& a = doc.sounds[static_cast<size_t>(index)];
                    a.start = std::max(0.0, clip.start);
                    a.duration = std::max(0.04, clip.duration);
                    a.sourceIn = std::max(0.0, clip.sourceIn);
                }
                return !c.after.empty();
            }
            case TimelineChangeKind::FadeSet: {
                if (c.after.size() != 1) return false;
                const int index = doc.SoundIndex(c.after[0].id);
                if (index < 0) return false;
                SoundSpan& a = doc.sounds[static_cast<size_t>(index)];
                a.fadeIn = std::max(0.0, c.after[0].fadeIn);
                a.fadeOut = std::max(0.0, c.after[0].fadeOut);
                return true;
            }
            case TimelineChangeKind::Split: {
                if (c.before.size() != 1 || c.after.size() != 2) return false;
                const int index = doc.SoundIndex(c.before[0].id);
                if (index < 0) return false;
                SoundSpan right = doc.sounds[static_cast<size_t>(index)];
                SoundSpan& left = doc.sounds[static_cast<size_t>(index)];
                left.start = c.after[0].start;
                left.duration = std::max(0.04, c.after[0].duration);
                left.fadeOut = 0.0;
                right.id = doc.NewSoundId();
                right.start = c.after[1].start;
                right.duration = std::max(0.04, c.after[1].duration);
                right.sourceIn = std::max(0.0, c.after[1].sourceIn);
                right.fadeIn = 0.0;
                doc.sounds.push_back(right);
                return true;
            }
            case TimelineChangeKind::Delete: {
                size_t removed = 0;
                for (const TimelineClip& clip : c.before) {
                    const int index = doc.SoundIndex(clip.id);
                    if (index < 0) continue;
                    doc.sounds.erase(doc.sounds.begin() + index);
                    ++removed;
                }
                return removed > 0;
            }
            case TimelineChangeKind::Duplicate: {
                if (c.before.size() != 1) return false;
                const int index = doc.SoundIndex(c.before[0].id);
                if (index < 0) return false;
                SoundSpan copy = doc.sounds[static_cast<size_t>(index)];
                copy.id = doc.NewSoundId();
                for (const TimelineClip& clip : c.after) {
                    if (clip.id != c.before[0].id) { copy.start = clip.start; copy.duration = std::max(0.04, clip.duration); }
                }
                doc.sounds.push_back(copy);
                return true;
            }
            default:
                return false;
        }
    }
    if (c.trackId == kMusicTrackId) {
        VideoFXMusic& m = doc.project.settings.music;
        std::vector<std::string> songs = m.Songs();
        switch (c.kind) {
            case TimelineChangeKind::FadeSet: {
                // the first song's fade in and the last song's fade out are the music's
                if (c.after.size() != 1) return false;
                const int index = SongOfClip(c.after[0].id);
                if (index < 0 || static_cast<size_t>(index) >= songs.size()) return false;
                bool changed = false;
                if (index == 0 && std::fabs(c.after[0].fadeIn - m.fadeIn) > 1e-9) { m.fadeIn = std::max(0.0, c.after[0].fadeIn); changed = true; }
                if (static_cast<size_t>(index) + 1 == songs.size() && std::fabs(c.after[0].fadeOut - m.fadeOut) > 1e-9) {
                    m.fadeOut = std::max(0.0, c.after[0].fadeOut);
                    changed = true;
                }
                return changed;
            }
            case TimelineChangeKind::Move: {
                // the whole sequence in its new order
                std::vector<std::string> reordered;
                for (const TimelineClip& clip : c.after) {
                    const int index = SongOfClip(clip.id);
                    if (index < 0 || static_cast<size_t>(index) >= songs.size()) return false;
                    reordered.push_back(songs[static_cast<size_t>(index)]);
                }
                if (reordered.size() != songs.size()) return false;
                if (!reordered.empty() && reordered.front() != songs.front()) m.start = 0.0;
                m.path = reordered.front();
                m.playlist.assign(reordered.begin() + 1, reordered.end());
                return true;
            }
            case TimelineChangeKind::Trim: {
                // only the first song's left edge: where in it the music begins
                for (const TimelineClip& clip : c.after) {
                    if (SongOfClip(clip.id) != 0) continue;
                    m.start = std::max(0.0, clip.sourceIn);
                    return true;
                }
                return false;
            }
            case TimelineChangeKind::Delete: {
                std::vector<int> gone;
                for (const TimelineClip& clip : c.before) {
                    const int index = SongOfClip(clip.id);
                    if (index >= 0) gone.push_back(index);
                }
                std::sort(gone.begin(), gone.end(), std::greater<int>());
                bool any = false;
                for (int index : gone) any = RemoveSong(doc, static_cast<size_t>(index)) || any;
                return any;
            }
            case TimelineChangeKind::TransitionSet: {
                if (!c.transitionAfter) return false;
                m.crossfade = std::clamp(c.transitionAfter->seconds, 0.0, 30.0);
                return true;
            }
            case TimelineChangeKind::TransitionRemove:
                m.crossfade = 0.0;
                return true;
            default:
                return false;   // a song is not split or copied here
        }
    }
    if (c.trackId != kMainTrackId) return false;

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
