// Apps/UltraVideo/UltraVideoDocument.h
// The document UltraVideo edits: the VideoFX project (its segments, settings
// and music) as the engine's truth, a stable id per segment for the timeline
// element, the media bin, the markers and the view state - and the
// `.ultravideo` file that holds all of it (Docs/Research/UltraVideoDesignProposal.md
// §5.6, §9.2). The project inside the file is what VideoFX_ProjectToJson
// writes, so `videofx render` works on it once extracted.
// Version: 0.4.0
// Last Modified: 2026-10-13
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
    bool useProxies = true;         // the viewer and the playback draft play from proxies
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

// A clip on the overlay-video lane (proposal §5.3, §8.3): another file's
// picture over the main track - picture-in-picture, or a logo when the file
// is a still. Like a title span it is cut into one overlay per segment it
// crosses when the engine renders; `look` carries the place, size, opacity.
struct OverlaySpan {
    std::string id;                 // the element's key ("o1")
    std::string path;               // the file, UTF-8, absolute
    double start = 0.0;             // seconds on the timeline
    double duration = 4.0;
    double sourceIn = 0.0;          // seconds into the file where it begins (a video)
    double fadeIn = 0.0;            // seconds, at the span's own ends
    double fadeOut = 0.0;
    VideoFX::VideoFXOverlay look;   // anchor, margin, imageHeight (the size), opacity

    double End() const { return start + duration; }
    std::string Name() const;       // the file name
    bool IsStill() const;           // a picture rather than a video
};

// A clip on the voice-and-sound lane (proposal §5.3): a file's sound at any
// time, over the clips' own sound and the music - a voice-over, which the
// music dips under, or a sound effect, which it does not. Each becomes one
// of the export's audio clips (VideoFXAudioClip, VideoFX 0.13.0).
struct SoundSpan {
    std::string id;                 // the element's key ("a1")
    std::string path;               // a sound file, or a video whose sound is wanted; UTF-8, absolute
    double start = 0.0;             // seconds on the timeline
    double duration = 1.0;
    double sourceIn = 0.0;          // seconds into the file where it begins
    double volume = 1.0;            // 0..4 (1 = as recorded)
    double fadeIn = 0.0;            // seconds, at the span's own ends
    double fadeOut = 0.0;
    bool ducksMusic = true;         // a voice-over; false for a sound effect

    double End() const { return start + duration; }
    std::string Name() const;       // the file name
    VideoFX::VideoFXAudioClip ToAudioClip() const;
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

// The effect presets the bin offers (proposal §5.2): one click puts a colour
// look or a blur on the selected clip as the engine's own effects, which
// the inspector then lists one by one. A blur's radius is a share of the
// clip's picture height, so it looks the same on a phone clip and a 4K one.
struct EffectPreset {
    const char* key;
    const char* label;
    const char* hint;
    bool blur;                                  // the Blur and detail group, not a colour look
    std::vector<VideoFX::VideoFXEffect> effects; // a Blur's amount: the share of the picture's height
};
const std::vector<EffectPreset>& EffectPresets();
const EffectPreset* FindEffectPreset(const std::string& key);
// What one effect does, for the inspector's list: "Warmer 30 %", "Blur 4 px"
std::string DescribeEffect(const VideoFX::VideoFXEffect& effect);
// How far the inspector's slider moves an effect's amount, in the engine's
// own units; not adjustable for a switch (black and white, a flip, a
// quarter turn) or a crop rectangle
struct EffectRange {
    bool adjustable = false;
    double low = 0.0, high = 0.0, step = 0.0;
};
EffectRange RangeOfEffect(VideoFX::VideoFXEffectType type);

class Document {
public:
    VideoFX::VideoFXProject project;        // segments, settings (with the music), title
    std::vector<std::string> clipIds;       // one per segment, in order: the element's keys
    std::vector<TitleSpan> titles;          // the title lane, in any order
    std::vector<OverlaySpan> overlays;      // the overlay-video lane, in any order
    std::vector<SoundSpan> sounds;          // the voice-and-sound lane, in any order; the truth for
                                            // the export's audio clips (project.settings.audioClips stays empty)
    std::vector<MediaItem> media;           // the bin
    std::vector<UltraCanvas::TimelineMarker> markers;
    ViewState view;
    std::string path;                       // the .ultravideo file; "" until saved
    int nextClipId = 1;
    int nextTitleId = 1;
    int nextOverlayId = 1;
    int nextSoundId = 1;

    std::string Name() const;               // the title, else the file's stem, else "Untitled"
    std::string Describe() const;           // "1920 x 1080 · 25 fps"
    int Width() const;                      // the settings', else the first picture's, else 1920
    int Height() const;
    double FrameRate() const;               // the settings', else the first video's, else 25
    double Length() const;                  // seconds of timeline, overlaps subtracted

    std::string NewClipId();
    std::string NewTitleId();
    std::string NewOverlayId();
    std::string NewSoundId();
    // Where the segments sit on the timeline: the start of each, derived as
    // the element derives them (transition overlaps subtracted)
    std::vector<double> SegmentStarts() const;
    // The segments as the engine should get them: the title and overlay
    // spans cut into one overlay per segment they cross (text, or the file's
    // picture from the right second into it), fades on the outer ends only
    std::vector<VideoFX::VideoFXSegment> SegmentsForEngine() const;
    // A new title at a time, from a style; returns its index
    int AddTitle(const std::string& styleKey, double at, const std::string& text = "");
    int TitleIndex(const std::string& id) const;   // -1 when absent
    // A file over the picture at a time: a video for its length, a still for
    // the design's four seconds; imported into the bin first when it is not
    // there. Returns its index, -1 when the file is sound or unreadable.
    int AddOverlay(const std::string& path, double at);
    int OverlayIndex(const std::string& id) const;
    // A file's sound on the voice-and-sound lane at a time, for its length:
    // a voice-over (the music dips under it) or a sound effect. Imported
    // into the bin first when it is not there. Returns its index, -1 when
    // the file has no sound or cannot be read.
    int AddSound(const std::string& path, double at, bool voiceOver = true);
    int SoundIndex(const std::string& id) const;
    // The lane as the engine's audio clips (VideoFXExportSettings::audioClips)
    std::vector<VideoFX::VideoFXAudioClip> AudioClipsForEngine() const;
    // Put a preset's effects on a segment, after the ones it has. False when
    // there is no such segment or preset, or the segment ends with exactly
    // these effects already (a second click does not double a look).
    bool ApplyEffectPreset(size_t segment, const std::string& key);
    // One effect's amount, clamped to RangeOfEffect. False when there is no
    // such segment or effect, or its amount is not adjustable.
    bool SetEffectAmount(size_t segment, size_t effect, double amount);
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
