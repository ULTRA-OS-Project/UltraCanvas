// UltraCanvas/include/UltraCanvasVideoFileInfo.h
// The technical facts of a video file - container, picture size, frame rate,
// codecs with their profiles, bit rates, the sound's sample rate and channels,
// the track counts and the title / encoder the file carries - read from the
// file's own headers, without a decoder and without starting playback.
//
// Why not the player's VideoStreamInfo: that is filled by the platform's
// playback backend, and how much of it depends on the backend (Media
// Foundation reports the size and the duration and nothing else). This reads
// the same file the same way on every platform, so the media viewer's Details
// panel and status bar can say what a clip *is* before - or without - its
// first frame being decoded.
//
// Read: ISO base media (MP4, M4V, MOV, 3GP), Matroska / WebM, AVI. Any other
// file returns false; a caller then falls back on the player's stream info.
// The reader is bounded - it walks headers, never sample data - and a damaged
// or truncated file yields whatever was readable, never an exception.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>

namespace UltraCanvas {

// One file's facts. A number that the file does not state is 0, a text that
// it does not state is empty - say nothing rather than guess.
struct VideoFileInfo {
    // ----- the file -----
    std::string container;         // "MPEG-4", "QuickTime", "Matroska", "WebM", "AVI"
    std::string brand;             // ISO major brand ("isom", "mp42", "qt  "), else empty
    double  duration     = 0.0;    // seconds
    int64_t overallBitRate = 0;    // bits per second, file size over duration

    // ----- the first video track -----
    bool        hasVideo = false;
    std::string videoCodec;        // "H.264", "H.265", "VP9", "AV1", ...
    std::string videoProfile;      // "High, level 4.0" / "Main 10" - empty if unknown
    int     width  = 0;            // coded picture, pixels
    int     height = 0;
    int     displayWidth  = 0;     // after the pixel aspect ratio (== width when square)
    int     displayHeight = 0;
    double  frameRate  = 0.0;      // frames per second (average)
    int64_t frameCount = 0;
    int64_t videoBitRate = 0;      // bits per second
    int     bitDepth = 0;          // bits per colour component, when the codec says
    int     rotation = 0;          // 0, 90, 180 or 270 - the clockwise turn a player
                                   // applies to show it upright (a phone's portrait clip: 90)

    // ----- the first sound track -----
    bool        hasAudio = false;
    std::string audioCodec;        // "AAC LC", "HE-AAC", "Opus", "AC-3", "PCM", ...
    int     sampleRate    = 0;     // Hz
    int     channels      = 0;
    int     bitsPerSample = 0;     // stated for PCM-like codecs only
    int64_t audioBitRate  = 0;     // bits per second
    std::string audioLanguage;     // ISO 639-2 ("eng"), empty when undetermined

    // ----- all tracks -----
    int videoTracks    = 0;
    int audioTracks    = 0;
    int subtitleTracks = 0;

    // ----- the file's own tags -----
    std::string title;
    std::string encoder;           // the writing application / library
    std::string creationTime;      // "2026-11-10 14:03:22" (UTC), empty if unset
};

// Reads `utf8Path`'s headers into `out` (reset first). True when the file is
// one of the containers above and at least its container was recognised;
// false for any other file or one that cannot be opened.
bool ReadVideoFileInfo(const std::string& utf8Path, VideoFileInfo& out);

// "Mono", "Stereo", "5.1", "7.1", otherwise "<n> channels"; empty for 0.
std::string DescribeAudioChannels(int channels);

// "1.68 Mb/s", "128 kb/s", "900 b/s"; empty for 0.
std::string FormatBitRate(int64_t bitsPerSecond);

// "29.97", "25", "23.976" - at most three decimals, trailing zeros dropped;
// empty for 0. Always dot-decimal.
std::string FormatFrameRate(double fps);

// "48 kHz", "44.1 kHz", "22.05 kHz"; empty for 0. Always dot-decimal.
std::string FormatSampleRate(int hertz);

// "16:9", "4:3", "2.39:1" - the display aspect of a width and height.
std::string DescribeAspectRatio(int width, int height);

// "1:23" / "1:02:03" (with ".456" for `withMillis`); empty for <= 0.
std::string FormatMediaDuration(double seconds, bool withMillis = false);

} // namespace UltraCanvas
