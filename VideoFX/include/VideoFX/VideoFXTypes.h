// VideoFX/include/VideoFX/VideoFXTypes.h
// Types for the VideoFX module: results, media information, frames, effects,
// timeline segments and export settings. No FFmpeg type appears here - the
// engine behind them is private to the module and can be swapped.
// Version: 0.5.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace VideoFX {

// ============================================================================
// RESULT
// ============================================================================
// Every blocking call returns one of these. The human-readable reason for the
// last failure on the calling thread is VideoFX_GetLastError().
// (No member is called Success or None: X11's Xlib.h defines both as macros,
// and application code includes it before this header.)
enum class VideoFXResult {
    Ok = 0,
    InvalidArgument,      // empty timeline, bad trim range, unknown effect value ...
    FileNotFound,         // an input file does not exist
    OpenFailed,           // input exists but no demuxer could read it
    NoMediaStreams,       // no video/audio stream the operation needs
    DecoderNotAvailable,  // the backend has no decoder for an input stream
    EncoderNotAvailable,  // the requested codec has no encoder in this build
    UnsupportedFormat,    // container cannot hold the requested codec
    FilterError,          // effect chain could not be built or run
    DecodeError,
    EncodeError,
    WriteError,           // output could not be created or written
    Cancelled,            // the progress callback returned false
    NotAvailable          // module built without its backend
};

// ============================================================================
// MEDIA INFORMATION (VideoFX_Probe)
// ============================================================================
enum class VideoFXStreamKind { Video, Audio, Subtitle, Data, Attachment, Unknown };

struct VideoFXStreamInfo {
    int index = -1;
    VideoFXStreamKind kind = VideoFXStreamKind::Unknown;
    std::string codecName;          // "h264", "aac", ...
    std::string codecLongName;
    std::string language;           // from stream metadata, may be empty
    double duration = 0.0;          // seconds, 0 = unknown
    int64_t bitRate = 0;            // bits per second, 0 = unknown

    // Video
    int width = 0;
    int height = 0;
    double frameRate = 0.0;         // average frames per second
    int rotation = 0;               // display rotation, 0/90/180/270 clockwise
    std::string pixelFormat;        // "yuv420p", ...

    // Audio
    int sampleRate = 0;
    int channels = 0;
    std::string channelLayout;      // "stereo", "5.1", ...
    std::string sampleFormat;       // "fltp", ...
};

struct VideoFXMediaInfo {
    std::string path;
    std::string formatName;         // demuxer short name(s), e.g. "mov,mp4,m4a,3gp,3g2,mj2"
    std::string formatLongName;
    double duration = 0.0;          // seconds, 0 = unknown
    int64_t bitRate = 0;
    int64_t fileSize = 0;
    std::map<std::string, std::string> metadata;   // container tags (title, artist, ...)
    std::vector<VideoFXStreamInfo> streams;

    // Convenience copies of the default streams (-1 = none)
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    int width = 0;                  // as displayed, rotation applied
    int height = 0;
    double frameRate = 0.0;
    int sampleRate = 0;
    int channels = 0;

    bool HasVideo() const { return videoStreamIndex >= 0; }
    bool HasAudio() const { return audioStreamIndex >= 0; }
};

// ============================================================================
// FRAMES (VideoFX_ExtractFrame / VideoFX_ExtractThumbnails)
// ============================================================================
// Tightly packed 8-bit RGBA, rows top to bottom (stride == width * 4).
struct VideoFXFrame {
    int width = 0;
    int height = 0;
    double timestamp = 0.0;         // seconds into the media
    std::vector<uint8_t> pixels;

    bool IsValid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * height * 4;
    }
};

// ============================================================================
// EFFECTS
// ============================================================================
// An effect is a typed value; VideoFX turns the list into the backend's filter
// graph. Build them with the factory functions - the meaning of `amount`
// differs per type and is documented there.
enum class VideoFXEffectType {
    // Colour
    Brightness, Contrast, Saturation, Gamma, Exposure, Hue, Temperature,
    Grayscale, Sepia, Invert, LUT,
    // Detail
    Blur, Sharpen, Denoise, Vignette,
    // Geometry
    Rotate90, Rotate180, Rotate270, Rotate, FlipHorizontal, FlipVertical, Crop,
    // Time-based (video and audio together)
    FadeIn, FadeOut,
    // Audio
    Volume, NormalizeAudio
};

struct VideoFXEffect {
    VideoFXEffectType type = VideoFXEffectType::Brightness;
    double amount = 0.0;            // meaning depends on type (see factories)
    int x = 0, y = 0, width = 0, height = 0;   // Crop rectangle, pixels
    std::string path;               // LUT: .cube / .3dl file

    // amount -1..1, 0 = unchanged
    static VideoFXEffect Brightness(double amount);
    // amount -1..1, 0 = unchanged (-1 = flat grey, 1 = double contrast)
    static VideoFXEffect Contrast(double amount);
    // amount -1..1, 0 = unchanged (-1 = greyscale, 1 = strongly saturated)
    static VideoFXEffect Saturation(double amount);
    // gamma 0.1..10, 1 = unchanged
    static VideoFXEffect Gamma(double gamma);
    // exposure in stops, -3..3
    static VideoFXEffect Exposure(double stops);
    // hue rotation in degrees
    static VideoFXEffect Hue(double degrees);
    // amount -1..1: negative cooler (blue), positive warmer (orange)
    static VideoFXEffect Temperature(double amount);
    static VideoFXEffect Grayscale();
    static VideoFXEffect Sepia();
    static VideoFXEffect Invert();
    // 3D LUT file (.cube, .3dl, .dat, .m3d); strength 0..1 mixes with the source
    static VideoFXEffect LUT(const std::string& lutPath, double strength = 1.0);
    // Gaussian blur radius in pixels (sigma)
    static VideoFXEffect Blur(double radius);
    // amount 0..3 (unsharp mask luma amount)
    static VideoFXEffect Sharpen(double amount);
    // strength 0..20 (spatial luma strength)
    static VideoFXEffect Denoise(double strength);
    // amount 0..1, darkening of the corners
    static VideoFXEffect Vignette(double amount);
    static VideoFXEffect Rotate90();       // clockwise; swaps width and height
    static VideoFXEffect Rotate180();
    static VideoFXEffect Rotate270();      // = 90 counter-clockwise
    // Arbitrary angle in degrees, clockwise; frame size is kept, corners black
    static VideoFXEffect Rotate(double degrees);
    static VideoFXEffect FlipHorizontal();
    static VideoFXEffect FlipVertical();
    // Keep the rectangle (x, y, width, height) of the source frame
    static VideoFXEffect Crop(int x, int y, int width, int height);
    // Fade from black (and from silence) over the first `seconds` of the segment
    static VideoFXEffect FadeIn(double seconds);
    // Fade to black (and to silence) over the last `seconds` of the segment
    static VideoFXEffect FadeOut(double seconds);
    // Linear gain: 1 = unchanged, 0.5 = half, 2 = double
    static VideoFXEffect Volume(double gain);
    // EBU R128 loudness normalisation to `targetLufs` (typ. -23 broadcast, -14 web)
    static VideoFXEffect NormalizeAudio(double targetLufs = -16.0);
};

// ============================================================================
// TRANSITIONS (between two segments)
// ============================================================================
// Set on the segment a transition leads INTO (VideoFXSegment::transitionIn);
// the first segment's is ignored. The two segments overlap by `duration`, so
// the export gets shorter by that much, and their sound is cross-faded over
// the same span.
enum class VideoFXTransitionType {
    Cut,                            // no transition (default)
    Crossfade,                      // picture blends from one to the other
    Dissolve,                       // random-pixel dissolve
    FadeThroughBlack, FadeThroughWhite,
    WipeLeft, WipeRight, WipeUp, WipeDown,
    SlideLeft, SlideRight, SlideUp, SlideDown,
    SmoothLeft, SmoothRight, SmoothUp, SmoothDown,
    CircleOpen, CircleClose, CircleCrop, RectCrop,
    Radial, Pixelize, Blur, Distance,
    DiagonalTopLeft, DiagonalTopRight, DiagonalBottomLeft, DiagonalBottomRight,
    SqueezeHorizontal, SqueezeVertical
};

struct VideoFXTransition {
    VideoFXTransitionType type = VideoFXTransitionType::Cut;
    double duration = 1.0;          // seconds of overlap, 0.04..10

    static VideoFXTransition Make(VideoFXTransitionType type, double seconds = 1.0) {
        VideoFXTransition t;
        t.type = type;
        t.duration = seconds;
        return t;
    }
    static VideoFXTransition Crossfade(double seconds = 1.0) { return Make(VideoFXTransitionType::Crossfade, seconds); }
    bool IsCut() const { return type == VideoFXTransitionType::Cut; }
};

// ============================================================================
// OVERLAYS (text and images on top of a segment)
// ============================================================================
// Placed on the OUTPUT frame, so a title sits in the same spot whatever the
// source's size. Sizes and margins are fractions of the output height, which
// keeps a layout the same at 480p and 4K.
enum class VideoFXOverlayKind { Text, Image };

enum class VideoFXAnchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
    Custom                          // x, y below: top-left corner as fractions of the frame
};

struct VideoFXOverlay {
    VideoFXOverlayKind kind = VideoFXOverlayKind::Text;

    // ---- text ----
    std::string text;               // UTF-8; '\n' breaks lines
    std::string fontPath;           // .ttf / .otf / .ttc; empty = a default system sans font
    double fontSize = 0.06;         // fraction of output height (0.06 = 65 px at 1080p)
    uint32_t textColor = 0xFFFFFF;  // 0xRRGGBB
    bool shadow = true;             // soft drop shadow for legibility
    bool box = false;               // a band behind the text
    uint32_t boxColor = 0x000000;
    double boxOpacity = 0.5;

    // ---- image ----
    std::string imagePath;          // PNG (alpha kept) / JPEG / anything FFmpeg decodes
    VideoFXFrame image;             // or pixels in memory - used when valid
    double imageHeight = 0.0;       // fraction of output height, 0 = the image's own pixel size

    // ---- placement ----
    VideoFXAnchor anchor = VideoFXAnchor::Bottom;
    double margin = 0.05;           // distance from the frame edge, fraction of output height
    double x = 0.0, y = 0.0;        // Custom anchor only, fractions of output width / height
    double opacity = 1.0;           // 0..1

    // ---- timing, in the segment's output seconds (after speed) ----
    double start = 0.0;
    double end = 0.0;               // 0 = until the segment ends
    double fadeIn = 0.0;            // seconds
    double fadeOut = 0.0;

    static VideoFXOverlay Text(const std::string& text, VideoFXAnchor anchor = VideoFXAnchor::Bottom,
                               double fontSize = 0.06);
    static VideoFXOverlay Image(const std::string& path, VideoFXAnchor anchor = VideoFXAnchor::TopRight,
                                double heightFraction = 0.12);
    static VideoFXOverlay ImageFromFrame(const VideoFXFrame& frame, VideoFXAnchor anchor = VideoFXAnchor::TopRight,
                                         double heightFraction = 0.12);
};

// ============================================================================
// STILL IMAGES (photo segments, "Ken Burns" motion)
// ============================================================================
// How the virtual camera moves over a still image. Zoom 1 shows the largest
// part of the image that fills the frame (the image covers it, no bars);
// zoom 1.25 shows 80% of that. Centres are the point looked at, as fractions
// of the image's width / height; the view never leaves the image.
enum class VideoFXMotionStyle {
    Still,                          // no motion
    ZoomIn, ZoomOut,                // centred, 1 -> 1.25 / 1.25 -> 1
    PanLeft, PanRight,              // camera moves across at zoom 1.2
    PanUp, PanDown,
    Auto,                           // varies from segment to segment, pans along the image's long side
    Custom                          // startZoom/endZoom, start/end centres below
};

struct VideoFXImageMotion {
    VideoFXMotionStyle style = VideoFXMotionStyle::Auto;
    double startZoom = 1.0, endZoom = 1.25;     // Custom: 1..4
    double startX = 0.5, startY = 0.5;          // Custom: 0..1
    double endX = 0.5, endY = 0.5;
    bool easeInOut = true;                      // start and stop gently rather than at constant speed

    static VideoFXImageMotion Make(VideoFXMotionStyle style) {
        VideoFXImageMotion m;
        m.style = style;
        return m;
    }
    static VideoFXImageMotion Custom(double startZoom, double startX, double startY,
                                     double endZoom, double endX, double endY) {
        VideoFXImageMotion m;
        m.style = VideoFXMotionStyle::Custom;
        m.startZoom = startZoom; m.startX = startX; m.startY = startY;
        m.endZoom = endZoom; m.endX = endX; m.endY = endY;
        return m;
    }
};

// How a still image whose shape differs from the frame's is framed
enum class VideoFXImageFit {
    Auto,                           // Cover; BlurredBackground for an image much taller than
                                    // the frame (a portrait photo in a 16:9 video)
    Cover,                          // fill the frame, crop the overflow
    Contain,                        // the whole image, black bars
    BlurredBackground               // the whole image; the bars filled with a blurred,
                                    // darkened, enlarged copy of it
};

// ============================================================================
// TIMELINE SEGMENTS (VideoFX_Export)
// ============================================================================
// An export is a list of segments played one after another. A segment is a
// range of a media file, a still image shown for a while, or a generated
// clip (a solid colour, a test pattern).
enum class VideoFXSourceKind { File, Color, TestPattern, Image };

struct VideoFXSegment {
    VideoFXSourceKind kind = VideoFXSourceKind::File;
    std::string path;               // File
    double start = 0.0;             // File: first second used
    double end = 0.0;               // File: last second used, 0 = to the end
    double duration = 5.0;          // Image / Color / TestPattern length, seconds
    uint32_t color = 0x000000;      // Color: 0xRRGGBB
    double speed = 1.0;             // 0.25..4; 2 = twice as fast (audio keeps pitch)
    bool mute = false;              // drop this segment's audio (silence instead)
    std::vector<VideoFXEffect> effects;
    std::vector<VideoFXOverlay> overlays;   // drawn after effects, over the output frame
    VideoFXTransition transitionIn;         // from the previous segment into this one
    VideoFXImageMotion motion;              // Image: camera movement over the still
    VideoFXImageFit imageFit = VideoFXImageFit::Auto;   // Image: framing when its shape differs
    VideoFXFrame image;                     // Image: pixels in memory, used instead of `path` when valid

    static VideoFXSegment FromFile(const std::string& path, double start = 0.0, double end = 0.0);
    // A photo / PNG / any image FFmpeg decodes, shown for `seconds`; JPEG
    // orientation (EXIF) is honoured
    static VideoFXSegment FromImage(const std::string& path, double seconds = 5.0,
                                    VideoFXImageMotion motion = {});
    static VideoFXSegment FromImageFrame(const VideoFXFrame& rgba, double seconds = 5.0,
                                         VideoFXImageMotion motion = {});
    static VideoFXSegment SolidColor(uint32_t rgb, double seconds);
    // SMPTE-style moving test pattern with a 1 kHz tone
    static VideoFXSegment TestPattern(double seconds);
};

// ============================================================================
// EXPORT SETTINGS
// ============================================================================
enum class VideoFXContainer {
    Auto,                           // from the output file extension
    MP4, MOV, MKV, WebM, AVI, GIF,
    MP3, M4A, WAV, FLAC, OGG        // audio-only containers
};

enum class VideoFXVideoCodec {
    Auto,                           // the container's usual codec (H.264 for MP4, VP9 for WebM ...)
    H264, H265, VP8, VP9, AV1, MPEG4, MJPEG, ProRes, FFV1, GIF,
    Disabled                        // no video stream in the output
};

enum class VideoFXAudioCodec {
    Auto,                           // the container's usual codec (AAC for MP4, Opus for WebM ...)
    AAC, MP3, Opus, Vorbis, FLAC, PCM16,
    Disabled                        // no audio stream in the output
};

// How a segment whose frame shape differs from the output is fitted into it
enum class VideoFXFitMode {
    Letterbox,                      // whole picture visible, black bars
    Fill,                           // cover the frame, crop the overflow
    Stretch                         // distort to the exact size
};

// ============================================================================
// BACKGROUND MUSIC (a sound bed under the whole export)
// ============================================================================
// Ready-made ducking for the usual kinds of footage; fills in the threshold
// and timing of VideoFXMusic (the depth, duckingLevel, is left as it is).
enum class VideoFXDuckingPreset {
    Speech,      // talking in quiet rooms: the defaults (-36.5 dBFS, hold 0.6 s)
    Outdoor,     // talking over wind and traffic: -28 dBFS
    LoudEvent    // crowds, engines, concerts: -15 dBFS, back up after 0.2 s of calm
};

// Mixed in after everything else, so it runs straight through joins and
// transitions. Where the segments have sound of their own - someone speaking
// - the music dips to `duckingLevel` and comes back up after a pause.
//
// The ducking defaults are tuned for speech: sound above -36.5 dBFS counts as
// present, the music goes down within ~0.1 s and waits 0.6 s of quiet before
// coming back. A clip that is loud all the way through (a concert, traffic,
// a waterfall) holds the music down for its whole length with those values;
// raise `duckingThresholdDb` so only sound well above the clip's own
// background ducks, or set `duckingLevel` to 1 to never dip.
//
// Several songs play one after another: `path` (if set) first, then
// `playlist` in order, each blending into the next over `crossfade` seconds.
// A looping list crossfades from its last song back into its first.
struct VideoFXMusic {
    std::string path;               // any file with sound (MP3, M4A, WAV, FLAC, OGG, a video ...); "" = none
    std::vector<std::string> playlist;  // further songs, played after `path` in this order
    double crossfade = 3.0;         // seconds one song blends into the next, 0..30 (0 = back to back);
                                    // shortened where a song is under twice as long
    double volume = 0.8;            // linear gain, 0..4
    double start = 0.0;             // seconds into the first song to begin at
    bool loop = true;               // repeat when shorter than the video (false: silence after it ends)
    double fadeIn = 1.0;            // seconds at the start of the export
    double fadeOut = 2.0;           // seconds at the end of the export
    double duckingLevel = 0.3;      // gain under the segments' own sound, 0..1 (1 = never dip)
    double duckingThresholdDb = -36.5; // the segments' sound counts as present above this RMS level, dBFS, -90..0
    double duckingAttack = 0.12;    // seconds to go down (time constant), 0.001..10
    double duckingHold = 0.6;       // seconds of quiet before coming back up, 0..30
    double duckingRelease = 0.8;    // seconds to come back up (time constant), 0.001..30

    static VideoFXMusic FromFile(const std::string& path, double volume = 0.8) {
        VideoFXMusic m;
        m.path = path;
        m.volume = volume;
        return m;
    }
    // A song list, played in order
    static VideoFXMusic FromFiles(const std::vector<std::string>& paths, double volume = 0.8) {
        VideoFXMusic m;
        m.playlist = paths;
        m.volume = volume;
        return m;
    }
    bool IsSet() const { return !path.empty() || !playlist.empty(); }
    // Every song in playing order: `path`, then `playlist`
    std::vector<std::string> Songs() const {
        std::vector<std::string> songs;
        if (!path.empty()) songs.push_back(path);
        songs.insert(songs.end(), playlist.begin(), playlist.end());
        return songs;
    }

    void SetDuckingPreset(VideoFXDuckingPreset preset) {
        switch (preset) {
            case VideoFXDuckingPreset::Speech: {
                const VideoFXMusic defaults;
                duckingThresholdDb = defaults.duckingThresholdDb;
                duckingAttack = defaults.duckingAttack;
                duckingHold = defaults.duckingHold;
                duckingRelease = defaults.duckingRelease;
                break;
            }
            case VideoFXDuckingPreset::Outdoor:
                duckingThresholdDb = -28.0; duckingAttack = 0.12; duckingHold = 0.5; duckingRelease = 0.7;
                break;
            case VideoFXDuckingPreset::LoudEvent:
                duckingThresholdDb = -15.0; duckingAttack = 0.12; duckingHold = 0.2; duckingRelease = 0.4;
                break;
        }
    }
};

struct VideoFXExportSettings {
    VideoFXContainer container = VideoFXContainer::Auto;
    VideoFXVideoCodec videoCodec = VideoFXVideoCodec::Auto;
    VideoFXAudioCodec audioCodec = VideoFXAudioCodec::Auto;

    // 0 = take it from the first segment. Giving only one of width / height
    // scales the other to keep the first segment's aspect ratio.
    int width = 0;
    int height = 0;
    double frameRate = 0.0;         // 0 = first segment's rate (30 for generated clips)
    VideoFXFitMode fitMode = VideoFXFitMode::Letterbox;

    // Quality 0..100 (higher = better, bigger), -1 = the codec's sensible
    // default. Mapped to CRF / qscale per encoder. Ignored when videoBitRate > 0.
    int quality = -1;
    int64_t videoBitRate = 0;       // bits per second, 0 = quality-driven
    std::string encoderPreset;      // e.g. "veryfast" for x264/x265; empty = default

    int sampleRate = 0;             // 0 = first audio segment's rate (48000 fallback)
    int channels = 0;               // 0 = stereo (mono if every source is mono)
    int64_t audioBitRate = 0;       // 0 = encoder default for the codec

    int threads = 0;                // 0 = automatic

    VideoFXMusic music;             // background music under the whole export (none by default)

    // Presets for common targets
    static VideoFXExportSettings WebMP4(int height = 1080);     // H.264 + AAC, 1080p default
    static VideoFXExportSettings WebM(int height = 720);        // VP9 + Opus
    static VideoFXExportSettings AnimatedGif(int width = 480, double fps = 12.0);
    static VideoFXExportSettings MasterProRes();                // ProRes 422 + PCM in MOV
    static VideoFXExportSettings AudioOnlyMP3(int64_t bitRate = 192000);
    static VideoFXExportSettings AudioOnlyWAV();
};

// ============================================================================
// SLIDESHOW (VideoFX_CreateSlideshow)
// ============================================================================
struct VideoFXSlideshowOptions {
    double secondsPerImage = 4.0;               // on screen, transitions included
    VideoFXTransition transition = VideoFXTransition::Crossfade(1.0);   // between images; Cut for none
    VideoFXImageMotion motion;                  // Auto: a different move per image
    VideoFXImageFit imageFit = VideoFXImageFit::Auto;   // Auto: portraits on a blurred background
    std::vector<std::string> captions;          // optional, one per image ("" = none), bottom centre
    bool fadeInOut = true;                      // fade from and to black at the ends
    VideoFXMusic music;                         // background music; when set, used instead of settings.music
    bool matchMusicLength = false;              // choose secondsPerImage so the slideshow ends with the music
    // Change images on the music's beats: each change (a cut, or the middle of
    // a transition) moves to the beat nearest secondsPerImage after the last
    bool beatSync = false;
    int beatsPerImage = 0;                      // > 0: every image lasts exactly this many beats (4 = a bar
                                                // in 4/4); implies beatSync. 0..64
};

// The beat of a piece of music (VideoFX_DetectBeats)
struct VideoFXBeatInfo {
    double bpm = 0.0;               // tempo, beats per minute; 0 = no steady beat found
    double confidence = 0.0;        // 0..1, how clearly the music repeats at that tempo
    std::vector<double> beats;      // seconds from the start of the file

    bool HasBeat() const { return bpm > 0.0 && !beats.empty(); }
};

// Progress 0..1 of the whole export. Return false to cancel; the call then
// returns VideoFXResult::Cancelled and the partial output file is removed.
using VideoFXProgressCallback = std::function<bool(double fraction)>;

} // namespace VideoFX
