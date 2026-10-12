// Apps/UltraVideo/UltraVideoMapping.h
// The two translations between the document and the timeline element
// (Docs/Research/UltraVideoDesignProposal.md §9.2): BuildTimelineModel derives
// the element's model from the document - the main track from the segments,
// the music lane from the settings' music, the markers and the work area -
// and ApplyChange turns a change the element reports into edits of the
// document. The element never sees a VideoFX type.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoDocument.h"
#include "UltraCanvasVideoTimelineModel.h"

#include <string>
#include <vector>

namespace UltraVideo {

constexpr const char* kTitleTrackId = "T1";
constexpr const char* kOverlayTrackId = "V2";   // the overlay-video lane, free, over the main track
constexpr const char* kMainTrackId = "V1";
constexpr const char* kMusicTrackId = "A1";

UltraCanvas::TimelineModel BuildTimelineModel(const Document& document);

// Apply a reported change. False leaves the document as it was: the music
// lane is edited from the inspector, not on the timeline.
bool ApplyChange(Document& document, const UltraCanvas::TimelineChange& change);

// The songs as the music lane shows them: "music:<i>" ids in play order
std::vector<std::string> SongIds(const Document& document);
int SongOfClip(const std::string& clipId);   // -1 when not a song's id
// Take a song out of the music (the next one becomes the first)
bool RemoveSong(Document& document, size_t index);

// A short name for what a change did, for the undo history
std::string DescribeChange(const UltraCanvas::TimelineChange& change);

// The segment of a clip id, or -1 (a title's or an overlay's id gives -1:
// see Document::TitleIndex and Document::OverlayIndex)
int SegmentOfClip(const Document& document, const std::string& clipId);

// What a clip is called in the band and the inspector
std::string ClipName(const Document& document, size_t segment);

// ---- the story wizard (Docs/Research/UltraVideoDesignProposal.md §5.9, §6.11) ----
// What the wizard asks: photos and clips in order, a song or none, a style
struct StoryChoice {
    std::vector<std::string> files;             // photos and clips, in order
    double secondsPerPhoto = 4.0;
    std::vector<std::string> songs;             // none, one, or a playlist
    bool fitToMusic = false;                    // seconds per photo so the show ends with the song
    bool beatSync = false;                      // change pictures on the beat
    VideoFX::VideoFXDuckingPreset ducking = VideoFX::VideoFXDuckingPreset::Speech;
    VideoFX::VideoFXTransitionType transition = VideoFX::VideoFXTransitionType::Crossfade;
    double transitionSeconds = 1.0;
    bool keepFaces = true;                      // photos: the engine finds faces in stills only
    // Photos whose shape differs from the frame: Auto = whole, on a blurred
    // background. Clips of another shape follow it: Cover fills the frame,
    // the others show them whole (the export's fit mode)
    VideoFX::VideoFXImageFit portraitFit = VideoFX::VideoFXImageFit::Auto;
    bool captionsFromNames = false;
    bool fadeInOut = true;
};
// The engine's plan for the photos (VideoFX_PlanSlideshow: probes, beats,
// faces - run it on the worker). False with the engine's reason.
bool PlanStory(const StoryChoice& choice, std::vector<VideoFX::VideoFXSegment>& imageSegments,
               VideoFX::VideoFXExportSettings& settings, std::string& error);
// The document the plan becomes: the photos' segments interleaved with the
// clips in the chosen order, captions as title spans, the song as the music
Document BuildStoryDocument(const StoryChoice& choice, const std::vector<VideoFX::VideoFXSegment>& imageSegments,
                            const VideoFX::VideoFXExportSettings& settings);

// The transition types by name, for the inspector and the timeline's badges
struct TransitionChoice {
    VideoFX::VideoFXTransitionType type;
    const char* key;        // what the timeline model carries ("crossfade")
    const char* label;      // what the inspector shows ("Cross dissolve")
};
const std::vector<TransitionChoice>& TransitionChoices();
const TransitionChoice& ChoiceOf(VideoFX::VideoFXTransitionType type);

} // namespace UltraVideo
