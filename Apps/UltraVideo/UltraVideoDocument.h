// Apps/UltraVideo/UltraVideoDocument.h
// The document UltraVideo edits: the VideoFX project (its segments, settings
// and music) as the engine's truth, a stable id per segment for the timeline
// element, the media bin, the markers and the view state - and the
// `.ultravideo` file that holds all of it (Docs/Research/UltraVideoDesignProposal.md
// §5.6, §9.2). The project inside the file is what VideoFX_ProjectToJson
// writes, so `videofx render` works on it once extracted.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVideoTimelineModel.h"
#include "VideoFX/VideoFX.h"

#include <cstddef>
#include <string>
#include <vector>

namespace UltraVideo {

enum class MediaKind { Video, Image, Audio, Unknown };

// One file in the media bin, with what the engine's probe said about it
struct MediaItem {
    std::string path;               // UTF-8, absolute
    std::string name;               // the file name, shown in the bin and on clips
    MediaKind kind = MediaKind::Unknown;
    double duration = 0.0;          // seconds; 0 for an image or an unread file
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    bool hasAudio = false;
    std::string error;              // why the engine cannot read it; empty when it can

    std::string Badge() const;      // "0:12", "photo", or the error's first words
};

// What the window remembers between sessions of the same project
struct ViewState {
    double pixelsPerSecond = 80.0;
    double scrollSeconds = 0.0;
    double playhead = 0.0;
    double rangeIn = 0.0;
    double rangeOut = 0.0;
    bool dark = true;               // the design's default
    int binWidth = 280;
    int inspectorWidth = 300;
    int dockPercent = 35;           // the timeline dock's share of the height
};

// A title on the title lane (Docs/Research/UltraVideoDesignProposal.md §5.3):
// one span the user sees, which becomes a text overlay on every segment it
// crosses when the engine renders. `look` carries the text and its style;
// its start, end and fades are derived per segment and never read here.
struct TitleSpan {
    std::string id;                 // the element's key ("t1")
    double start = 0.0;             // seconds on the timeline
    double duration = 4.0;
    double fadeIn = 0.5;            // seconds, at the span's own ends
    double fadeOut = 0.5;
    std::string style;              // the preset it was made from, for the inspector
    VideoFX::VideoFXOverlay look;   // text, anchor, size, colour, shadow, box

    double End() const { return start + duration; }
    std::string Name() const;       // the first line of the text
};

// The title styles the bin offers (opening title, lower third, caption)
struct TitleStyle {
    const char* key;
    const char* label;
    const char* hint;
    VideoFX::VideoFXOverlay look;
    double fadeIn;
    double fadeOut;
    double seconds;
};
const std::vector<TitleStyle>& TitleStyles();
const TitleStyle* FindTitleStyle(const std::string& key);

class Document {
public:
    VideoFX::VideoFXProject project;        // segments, settings (with the music), title
    std::vector<std::string> clipIds;       // one per segment, in order: the element's keys
    std::vector<TitleSpan> titles;          // the title lane, in any order
    std::vector<MediaItem> media;           // the bin
    std::vector<UltraCanvas::TimelineMarker> markers;
    ViewState view;
    std::string path;                       // the .ultravideo file; "" until saved
    int nextClipId = 1;
    int nextTitleId = 1;

    std::string Name() const;               // the title, else the file's stem, else "Untitled"
    std::string Describe() const;           // "1920 x 1080 · 25 fps"
    int Width() const;                      // the settings', else the first picture's, else 1920
    int Height() const;
    double FrameRate() const;               // the settings', else the first video's, else 25
    double Length() const;                  // seconds of timeline, overlaps subtracted

    std::string NewClipId();
    std::string NewTitleId();
    // Where the segments sit on the timeline: the start of each, derived as
    // the element derives them (transition overlaps subtracted)
    std::vector<double> SegmentStarts() const;
    // The segments as the engine should get them: the title spans cut into
    // one text overlay per segment they cross, fades on the outer ends only
    std::vector<VideoFX::VideoFXSegment> SegmentsForEngine() const;
    // A new title at a time, from a style; returns its index
    int AddTitle(const std::string& styleKey, double at, const std::string& text = "");
    int TitleIndex(const std::string& id) const;   // -1 when absent
    // Timeline seconds of a segment: a file's range over its speed, a still's seconds
    double SegmentSeconds(size_t index) const;
    const MediaItem* FindMedia(const std::string& path) const;
    MediaItem* FindMedia(const std::string& path);
    // Probe a file and add it to the bin (or refresh it when already there).
    // The reference points into `media`: copy it before importing again.
    MediaItem& Import(const std::string& path);
    bool RemoveMedia(const std::string& path);   // false when a segment still uses it
    bool MediaInUse(const std::string& path) const;

    // Segments from media: a picture for `seconds` (0 = the design's four), a
    // video whole; sound becomes music. Each returns the new segment's index,
    // or -1 when the file is sound (set as music) or unknown.
    int AppendMedia(const MediaItem& item, double seconds = 0.0);
    int InsertMedia(size_t index, const MediaItem& item, double seconds = 0.0);

    // The .ultravideo file
    bool Save(const std::string& toPath, std::string& error);
    static bool Load(const std::string& fromPath, Document& doc, std::string& error,
                     std::vector<std::string>* missingMedia = nullptr);

    // What a segment is, for the bin and the clips
    static MediaKind KindOfPath(const std::string& path);
};

} // namespace UltraVideo
