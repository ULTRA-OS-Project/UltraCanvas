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

constexpr const char* kMainTrackId = "V1";
constexpr const char* kMusicTrackId = "A1";

UltraCanvas::TimelineModel BuildTimelineModel(const Document& document);

// Apply a reported change. False leaves the document as it was: the music
// lane is edited from the inspector, not on the timeline.
bool ApplyChange(Document& document, const UltraCanvas::TimelineChange& change);

// A short name for what a change did, for the undo history
std::string DescribeChange(const UltraCanvas::TimelineChange& change);

// The segment of a clip id, or -1
int SegmentOfClip(const Document& document, const std::string& clipId);

// What a clip is called in the band and the inspector
std::string ClipName(const Document& document, size_t segment);

// The transition types by name, for the inspector and the timeline's badges
struct TransitionChoice {
    VideoFX::VideoFXTransitionType type;
    const char* key;        // what the timeline model carries ("crossfade")
    const char* label;      // what the inspector shows ("Cross dissolve")
};
const std::vector<TransitionChoice>& TransitionChoices();
const TransitionChoice& ChoiceOf(VideoFX::VideoFXTransitionType type);

} // namespace UltraVideo
