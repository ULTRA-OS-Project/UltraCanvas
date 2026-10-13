// Tests/UltraVideoDocumentTest.cpp
// UltraVideo's document, mapping and history without a display: importing
// media the engine probes, segments from media, the timeline model derived
// from the document, every reported change written back, undo and redo, and
// the .ultravideo file round trip with relative media paths and missing
// media reported; the title, overlay and voice-and-sound lanes; the effect
// presets. Needs the VideoFX engine (FFmpeg) for the probes and the
// clips it generates; without it the suite skips itself.
// Version: 0.4.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#include "UltraVideoDocument.h"
#include "UltraVideoHistory.h"
#include "UltraVideoMapping.h"
#include "UltraVideoProxies.h"

#include "UltraCanvasPathUtf8.h"
#include "VideoFX/VideoFX.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;
using namespace UltraVideo;
using namespace VideoFX;
namespace fs = std::filesystem;

static int testCount = 0;
static int failCount = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        const bool ok = (cond);                                                \
        std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", msg);                  \
        if (!ok) ++failCount;                                                  \
        ++testCount;                                                           \
    } while (0)

static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

static std::string TempPath(const std::string& name) {
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / ("ultravideo-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(d / "media");
        return d;
    }();
    return PathToUtf8(dir / PathFromUtf8(name));
}

static VideoFXFrame Solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    VideoFXFrame f;
    f.width = w;
    f.height = h;
    f.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < f.pixels.size(); i += 4) {
        f.pixels[i] = r; f.pixels[i + 1] = g; f.pixels[i + 2] = b; f.pixels[i + 3] = 255;
    }
    return f;
}

static const TimelineTrack* Main(const TimelineModel& m) { return m.FindTrack(kMainTrackId); }

int main() {
    std::printf("UltraVideo document suite (VideoFX %s)\n", VideoFX_GetVersion().c_str());
    if (!VideoFX_IsAvailable()) {
        std::printf("SKIP: the VideoFX engine is not built (no FFmpeg)\n");
        return 0;
    }

    // ---- media the engine makes ----
    const std::string clip = TempPath("media/pattern.mkv");
    const std::string photo = TempPath("media/photo.png");
    const std::string song = TempPath("media/tone.wav");
    const std::string junk = TempPath("media/notes.txt");
    {
        VideoFXExportSettings s;
        s.container = VideoFXContainer::MKV;
        s.videoCodec = VideoFXVideoCodec::MPEG4;
        s.audioCodec = VideoFXAudioCodec::AAC;
        s.width = 320; s.height = 240; s.frameRate = 25.0;
        CHECK(VideoFX_Export({VideoFXSegment::TestPattern(3.0)}, clip, s) == VideoFXResult::Ok, "a 3 s clip");
        CHECK(VideoFX_SaveFrameImage(Solid(160, 120, 255, 0, 0), photo) == VideoFXResult::Ok, "a photo");
        CHECK(VideoFX_Export({VideoFXSegment::TestPattern(5.0)}, song, VideoFXExportSettings::AudioOnlyWAV()) == VideoFXResult::Ok, "a song");
        std::FILE* f = std::fopen(junk.c_str(), "w");
        if (f) { std::fputs("not media\n", f); std::fclose(f); }
    }

    std::printf("\n-- import --\n");
    Document doc;
    // Import returns a reference into the bin, which later imports may move: copy what the test keeps
    const MediaItem video = doc.Import(clip);
    CHECK(video.error.empty() && video.kind == MediaKind::Video, "a video is a video");
    CHECK(Near(video.duration, 3.0, 0.1) && video.width == 320 && video.height == 240 && Near(video.frameRate, 25.0, 0.01), "with its size, length and rate");
    CHECK(video.Badge() == "0:03", "its badge is its length");
    const MediaItem picture = doc.Import(photo);
    CHECK(picture.error.empty() && picture.kind == MediaKind::Image && picture.duration == 0.0, "a PNG is a picture");
    CHECK(picture.Badge() == "photo", "badged photo");
    const MediaItem tone = doc.Import(song);
    CHECK(tone.error.empty() && tone.kind == MediaKind::Audio && Near(tone.duration, 5.0, 0.1), "a WAV is sound");
    const MediaItem bad = doc.Import(junk);
    CHECK(!bad.error.empty() && bad.Badge() == "unreadable", "a text file is unreadable, with the reason");
    doc.Import(clip);
    CHECK(doc.media.size() == 4, "importing a file again does not duplicate it");
    CHECK(doc.Width() == 1920 && doc.Height() == 1080 && Near(doc.FrameRate(), 25.0, 0.01), "an empty timeline is 1080p at 25");

    std::printf("\n-- segments from media --\n");
    CHECK(doc.AppendMedia(picture) == 0, "a picture becomes the first segment");
    CHECK(doc.project.segments[0].kind == VideoFXSourceKind::Image && Near(doc.project.segments[0].duration, 4.0, 1e-9), "a still of four seconds");
    CHECK(doc.AppendMedia(video) == 1, "a video the second");
    CHECK(doc.project.segments[1].kind == VideoFXSourceKind::File && doc.project.segments[1].path == clip, "the whole file");
    CHECK(doc.AppendMedia(tone) == -1 && doc.project.settings.music.path == song, "sound becomes the music");
    CHECK(doc.AppendMedia(bad) == -1 && doc.project.segments.size() == 2, "an unreadable file goes nowhere");
    CHECK(doc.clipIds.size() == 2 && doc.clipIds[0] != doc.clipIds[1], "every segment has its own id");
    CHECK(doc.Width() == 320 && doc.Height() == 240, "the first video on the timeline sets the size");
    CHECK(doc.Describe() == "320 \xC3\x97 240 \xC2\xB7 25 fps", "described as size and rate");
    CHECK(Near(doc.SegmentSeconds(1), 3.0, 0.1) && Near(doc.Length(), 7.0, 0.1), "4 s of picture and 3 s of clip");
    CHECK(doc.InsertMedia(0, video) == 0 && doc.project.segments.size() == 3 && doc.clipIds.size() == 3, "inserted at the front");
    CHECK(!doc.RemoveMedia(clip) && doc.MediaInUse(clip), "media on the timeline cannot leave the bin");
    CHECK(doc.RemoveMedia(junk) && doc.media.size() == 3, "unused media can");

    std::printf("\n-- the timeline model --\n");
    doc.project.segments[1].transitionIn = VideoFXTransition::Crossfade(1.0);
    TimelineMarker marker;
    marker.id = "m1";
    marker.seconds = 2.5;
    doc.markers.push_back(marker);
    doc.view.rangeIn = 1.0;
    doc.view.rangeOut = 6.0;
    TimelineModel model = BuildTimelineModel(doc);
    const TimelineTrack* main = Main(model);
    CHECK(main && main->kind == TimelineTrackKind::MainVideo && main->IsMagnetic(), "a magnetic main track");
    CHECK(main && main->clips.size() == 3 && main->clips[0].kind == TimelineClipKind::Video && main->clips[1].kind == TimelineClipKind::Image, "its clips in order, by kind");
    CHECK(main && main->clips[0].id == doc.clipIds[0] && main->clips[1].name == "photo.png", "ids and names from the document");
    CHECK(main && Near(main->clips[0].sourceLength, 3.0, 0.1) && Near(main->clips[0].duration, 3.0, 0.1), "a file clip knows its source length");
    CHECK(main && main->transitions.size() == 1 && main->transitions[0].clipId == doc.clipIds[1] && main->transitions[0].type == "crossfade", "the transition on the cut into the picture");
    CHECK(main && Near(main->clips[1].start, 2.0, 0.1) && Near(main->clips[2].start, 6.0, 0.1), "starts derived with the overlap");
    const TimelineTrack* music = model.FindTrack(kMusicTrackId);
    CHECK(music && !music->locked && music->clips.size() == 1 && music->clips[0].kind == TimelineClipKind::Audio, "the music lane with the song");
    CHECK(music && Near(music->clips[0].duration, 5.0, 0.1) && Near(music->clips[0].fadeIn, 1.0, 1e-9), "the song's length and fade");
    CHECK(model.markers.size() == 1 && model.rangeIn == 1.0 && model.rangeOut == 6.0 && Near(model.frameRate, 25.0, 0.01), "markers, the work area and the rate");
    CHECK(TransitionChoices().size() == 31 && ChoiceOf(VideoFXTransitionType::WipeLeft).key == std::string("wipeleft"), "every transition type has a name");

    std::printf("\n-- changes --\n");
    const std::string first = doc.clipIds[0], second = doc.clipIds[1], third = doc.clipIds[2];
    // Move: the picture to the front
    TimelineChange move;
    move.kind = TimelineChangeKind::Move;
    move.trackId = kMainTrackId;
    move.before = main->clips;
    move.after = {main->clips[1], main->clips[0], main->clips[2]};
    CHECK(ApplyChange(doc, move) && doc.clipIds[0] == second && doc.project.segments[0].kind == VideoFXSourceKind::Image, "a move reorders the segments");
    CHECK(doc.project.segments[0].transitionIn.IsCut(), "the first segment has no transition");
    CHECK(DescribeChange(move) == "Move clip", "named for the history");
    model = BuildTimelineModel(doc);
    main = Main(model);
    // Trim: the video (now second) to its first two seconds
    TimelineChange trim;
    trim.kind = TimelineChangeKind::Trim;
    trim.trackId = kMainTrackId;
    trim.before = main->clips;
    trim.after = main->clips;
    trim.after[1].duration = 2.0;
    CHECK(ApplyChange(doc, trim) && Near(doc.project.segments[1].end, 2.0, 1e-6) && Near(doc.SegmentSeconds(1), 2.0, 1e-6), "a trim sets the file's range");
    trim.after[1].sourceIn = 0.5;
    trim.after[1].duration = 1.0;
    trim.after[1].speed = 2.0;
    CHECK(ApplyChange(doc, trim) && Near(doc.project.segments[1].start, 0.5, 1e-6) && Near(doc.project.segments[1].end, 2.5, 1e-6) && Near(doc.SegmentSeconds(1), 1.0, 1e-6), "with the speed: the source used is duration times speed");
    model = BuildTimelineModel(doc);
    main = Main(model);
    // Split the picture at 1.5 s
    TimelineTrack track = *main;
    CHECK(TimelineSplitClip(track, second, 1.5, 25.0, second + ":2"), "the model splits");
    TimelineChange split;
    split.kind = TimelineChangeKind::Split;
    split.trackId = kMainTrackId;
    split.before = {*main->FindClip(second)};
    split.after = {*track.FindClip(second), *track.FindClip(second + ":2")};
    split.at = 1.5;
    CHECK(ApplyChange(doc, split) && doc.project.segments.size() == 4 && doc.clipIds.size() == 4, "a split makes two segments");
    CHECK(Near(doc.project.segments[0].duration, 1.5, 1e-6) && Near(doc.project.segments[1].duration, 2.5, 1e-6) && doc.clipIds[1] != second + ":2", "of the right lengths, the right one with a fresh id");
    const std::string rightHalf = doc.clipIds[1];
    // A transition set on the cut into the right half, then removed
    TimelineChange setT;
    setT.kind = TimelineChangeKind::TransitionSet;
    setT.trackId = kMainTrackId;
    TimelineTransition t;
    t.clipId = rightHalf;
    t.type = "wipeleft";
    t.seconds = 0.5;
    setT.transitionAfter = t;
    CHECK(ApplyChange(doc, setT) && doc.project.segments[1].transitionIn.type == VideoFXTransitionType::WipeLeft && Near(doc.project.segments[1].transitionIn.duration, 0.5, 1e-9), "a transition is set by name");
    TimelineChange removeT;
    removeT.kind = TimelineChangeKind::TransitionRemove;
    removeT.trackId = kMainTrackId;
    removeT.transitionBefore = t;
    CHECK(ApplyChange(doc, removeT) && doc.project.segments[1].transitionIn.IsCut(), "and removed");
    setT.transitionAfter->clipId = doc.clipIds[0];
    CHECK(!ApplyChange(doc, setT), "nothing leads into the first clip");
    // Duplicate the right half
    model = BuildTimelineModel(doc);
    main = Main(model);
    track = *main;
    CHECK(TimelineDuplicateClip(track, rightHalf, rightHalf + ":copy"), "the model duplicates");
    TimelineChange dup;
    dup.kind = TimelineChangeKind::Duplicate;
    dup.trackId = kMainTrackId;
    dup.before = {*main->FindClip(rightHalf)};
    dup.after = track.clips;
    CHECK(ApplyChange(doc, dup) && doc.project.segments.size() == 5 && doc.clipIds[2] != rightHalf + ":copy" && doc.project.segments[2].kind == VideoFXSourceKind::Image, "a copy placed after the original, with its own id");
    // Delete the copy
    model = BuildTimelineModel(doc);
    main = Main(model);
    TimelineChange del;
    del.kind = TimelineChangeKind::Delete;
    del.trackId = kMainTrackId;
    del.before = {main->clips[2]};
    del.after = main->clips;
    del.after.erase(del.after.begin() + 2);
    CHECK(ApplyChange(doc, del) && doc.project.segments.size() == 4, "a delete removes the segment");
    // Markers
    TimelineChange addM;
    addM.kind = TimelineChangeKind::MarkerAdd;
    TimelineMarker m2;
    m2.id = "m2";
    m2.seconds = 4.0;
    addM.markerAfter = m2;
    CHECK(ApplyChange(doc, addM) && doc.markers.size() == 2, "a marker added");
    TimelineChange removeM;
    removeM.kind = TimelineChangeKind::MarkerRemove;
    removeM.markerBefore = marker;
    CHECK(ApplyChange(doc, removeM) && doc.markers.size() == 1 && doc.markers[0].id == "m2", "and one removed");
    std::printf("\n-- the music lane --\n");
    {
        Document md = doc;
        md.project.settings.music = VideoFXMusic::FromFiles({song, song});
        CHECK(SongIds(md).size() == 2 && SongOfClip("music:1") == 1 && SongOfClip("c3") == -1, "songs have ids in play order");
        TimelineModel mm = BuildTimelineModel(md);
        const TimelineTrack* lane = mm.FindTrack(kMusicTrackId);
        CHECK(lane && !lane->locked && lane->clips.size() == 2 && lane->transitions.size() == 1 && Near(lane->transitions[0].seconds, 3.0, 1e-9), "an unlocked music lane with two songs and their crossfade");
        TimelineChange fade;
        fade.kind = TimelineChangeKind::FadeSet;
        fade.trackId = kMusicTrackId;
        fade.before = {lane->clips[0]};
        fade.after = {lane->clips[0]};
        fade.after[0].fadeIn = 2.5;
        CHECK(ApplyChange(md, fade) && Near(md.project.settings.music.fadeIn, 2.5, 1e-9), "the first song's fade in is the music's");
        fade.before = {lane->clips[1]};
        fade.after = {lane->clips[1]};
        fade.after[0].fadeOut = 4.0;
        CHECK(ApplyChange(md, fade) && Near(md.project.settings.music.fadeOut, 4.0, 1e-9), "the last song's fade out too");
        fade.after[0].fadeOut = 4.0;   // as the music has it now
        fade.after[0].fadeIn = 9.0;
        CHECK(!ApplyChange(md, fade), "a fade in on a later song changes nothing");
        TimelineChange cross;
        cross.kind = TimelineChangeKind::TransitionSet;
        cross.trackId = kMusicTrackId;
        TimelineTransition ct = lane->transitions[0];
        ct.seconds = 5.0;
        cross.transitionAfter = ct;
        CHECK(ApplyChange(md, cross) && Near(md.project.settings.music.crossfade, 5.0, 1e-9), "the crossfade's length");
        TimelineChange trimSong;
        trimSong.kind = TimelineChangeKind::Trim;
        trimSong.trackId = kMusicTrackId;
        trimSong.after = lane->clips;
        trimSong.after[0].sourceIn = 1.5;
        CHECK(ApplyChange(md, trimSong) && Near(md.project.settings.music.start, 1.5, 1e-9), "the first song's left edge is where the music begins");
        md.project.settings.music = VideoFXMusic::FromFiles({song, photo});   // two different paths, so the order shows
        TimelineChange reorder;
        reorder.kind = TimelineChangeKind::Move;
        reorder.trackId = kMusicTrackId;
        mm = BuildTimelineModel(md);
        lane = mm.FindTrack(kMusicTrackId);
        reorder.after = {lane->clips[1], lane->clips[0]};
        CHECK(ApplyChange(md, reorder) && md.project.settings.music.path == photo && md.project.settings.music.playlist == std::vector<std::string>{song}, "songs reorder");
        TimelineChange dropSong;
        dropSong.kind = TimelineChangeKind::Delete;
        dropSong.trackId = kMusicTrackId;
        dropSong.before = {lane->clips[0]};   // music:0, now the photo path
        CHECK(ApplyChange(md, dropSong) && md.project.settings.music.path == song && md.project.settings.music.playlist.empty(), "a song is removed and the next moves up");
        CHECK(RemoveSong(md, 0) && !md.project.settings.music.IsSet(), "removing the last song clears the music");
        TimelineChange splitSong = del;
        splitSong.kind = TimelineChangeKind::Split;
        splitSong.trackId = kMusicTrackId;
        CHECK(!ApplyChange(md, splitSong), "a song is not split on the timeline");
    }
    (void)first; (void)third;

    std::printf("\n-- titles --\n");
    {
        Document td;
        td.Import(clip);
        td.AppendMedia(*td.FindMedia(clip));          // 0..3 s
        td.AppendMedia(*td.FindMedia(clip));          // 3..6 s
        td.project.segments[1].transitionIn = VideoFXTransition::Crossfade(1.0);   // so the second starts at 2 s
        CHECK(TitleStyles().size() == 4 && FindTitleStyle("lower") && !FindTitleStyle("none"), "four title styles by key");
        const int first = td.AddTitle("opening", 1.0, "Summer trip");
        CHECK(first == 0 && td.titles.size() == 1 && td.titles[0].id == "t1" && td.titles[0].look.text == "Summer trip", "a title from a style, with the text");
        CHECK(td.titles[0].look.anchor == VideoFXAnchor::Center && Near(td.titles[0].duration, 4.0, 1e-9) && Near(td.titles[0].fadeIn, 0.6, 1e-9), "the style's look, length and fades");
        CHECK(td.titles[0].Name() == "Summer trip", "named by its text");
        const int second = td.AddTitle("lower", 2.0);
        CHECK(second == 1 && Near(td.titles[1].start, 5.0, 1e-9) && td.titles[1].look.text == "Lower third", "a second title goes after the first, with the style's name as text");
        td.titles[1].look.text = "Anna\nat the beach";
        CHECK(td.titles[1].Name() == "Anna", "the first line names it");
        CHECK(td.TitleIndex("t2") == 1 && td.TitleIndex("c1") == -1, "titles are found by id");

        const std::vector<double> starts = td.SegmentStarts();
        const double clipSeconds = td.SegmentSeconds(0);   // the probe's length, 3 s or a little over
        CHECK(starts.size() == 2 && starts[0] == 0.0 && Near(starts[1], clipSeconds - 1.0, 1e-9), "segment starts as the timeline derives them");
        std::vector<VideoFXSegment> engine = td.SegmentsForEngine();
        CHECK(td.project.segments[0].overlays.empty() && engine.size() == 2, "the document's segments stay clean; the engine's copy gets the overlays");
        CHECK(engine[0].overlays.size() == 1 && Near(engine[0].overlays[0].start, 1.0, 1e-9) && engine[0].overlays[0].end == 0.0, "the first segment: the title from 1 s to its end");
        CHECK(Near(engine[0].overlays[0].fadeIn, 0.6, 1e-9) && engine[0].overlays[0].fadeOut == 0.0, "fading in at the span's start, not out at the cut");
        CHECK(engine[1].overlays.size() == 2 && engine[1].overlays[0].start == 0.0 && Near(engine[1].overlays[0].end, 5.0 - starts[1], 1e-9), "the second segment: the same title from its start to the span's end");
        CHECK(engine[1].overlays[0].fadeIn == 0.0 && Near(engine[1].overlays[0].fadeOut, 0.6, 1e-9), "fading out at the span's end only");
        CHECK(Near(engine[1].overlays[1].start, 5.0 - starts[1], 1e-9) && engine[1].overlays[1].box && engine[1].overlays[1].text == "Anna\nat the beach", "the lower third on the second segment with its band");

        TimelineModel tm = BuildTimelineModel(td);
        const TimelineTrack* lane = tm.FindTrack(kTitleTrackId);
        CHECK(lane && lane->kind == TimelineTrackKind::Title && !lane->IsMagnetic() && lane->clips.size() == 2, "a free title lane with both spans");
        CHECK(lane && lane->clips[0].kind == TimelineClipKind::Title && lane->clips[0].name == "Summer trip" && Near(lane->clips[0].start, 1.0, 1e-9), "as title clips at their times");
        CHECK(tm.tracks.size() == 5 && tm.tracks[0].id == kTitleTrackId && tm.tracks[1].id == kOverlayTrackId && tm.tracks[2].id == kMainTrackId, "the title and overlay lanes sit above the main track");

        TimelineChange moveT;
        moveT.kind = TimelineChangeKind::Move;
        moveT.trackId = kTitleTrackId;
        moveT.before = {lane->clips[0]};
        moveT.after = {lane->clips[0]};
        moveT.after[0].start = 0.5;
        CHECK(ApplyChange(td, moveT) && Near(td.titles[0].start, 0.5, 1e-9), "a title moves");
        TimelineChange trimT = moveT;
        trimT.kind = TimelineChangeKind::Trim;
        trimT.after[0].duration = 2.0;
        CHECK(ApplyChange(td, trimT) && Near(td.titles[0].duration, 2.0, 1e-9), "and trims");
        tm = BuildTimelineModel(td);
        lane = tm.FindTrack(kTitleTrackId);
        TimelineTrack lt = *lane;
        CHECK(TimelineSplitClip(lt, "t1", 1.5, 25.0, "t1:2"), "the model splits a title");
        TimelineChange splitT;
        splitT.kind = TimelineChangeKind::Split;
        splitT.trackId = kTitleTrackId;
        splitT.before = {*lane->FindClip("t1")};
        splitT.after = {*lt.FindClip("t1"), *lt.FindClip("t1:2")};
        CHECK(ApplyChange(td, splitT) && td.titles.size() == 3 && Near(td.titles[0].duration, 1.0, 1e-9) && td.titles[2].id == "t3" && Near(td.titles[2].start, 1.5, 1e-9), "a split makes two spans, the right one with a fresh id");
        CHECK(td.titles[0].fadeOut == 0.0 && td.titles[2].fadeIn == 0.0 && td.titles[2].look.text == "Summer trip", "no fade at the new cut; the text on both");
        TimelineChange delT;
        delT.kind = TimelineChangeKind::Delete;
        delT.trackId = kTitleTrackId;
        delT.before = {*lt.FindClip("t1:2")};
        delT.before[0].id = "t3";
        CHECK(ApplyChange(td, delT) && td.titles.size() == 2, "a title deletes");
        tm = BuildTimelineModel(td);
        lane = tm.FindTrack(kTitleTrackId);
        TimelineTrack lt2 = *lane;
        CHECK(TimelineDuplicateClip(lt2, "t2", "t2:copy"), "the model duplicates a title");
        TimelineChange dupT;
        dupT.kind = TimelineChangeKind::Duplicate;
        dupT.trackId = kTitleTrackId;
        dupT.before = {*lane->FindClip("t2")};
        dupT.after = {*lt2.FindClip("t2"), *lt2.FindClip("t2:copy")};
        CHECK(ApplyChange(td, dupT) && td.titles.size() == 3 && td.titles[2].look.text == td.titles[1].look.text && td.titles[2].start > td.titles[1].start, "a copy after the original");

        const std::string tfile = TempPath("titles.ultravideo");
        std::string terror;
        CHECK(td.Save(tfile, terror), ("save with titles: " + terror).c_str());
        {
            std::FILE* f = std::fopen(tfile.c_str(), "rb");
            std::string text;
            if (f) { char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n); std::fclose(f); }
            CHECK(text.find("\"titles\"") != std::string::npos && text.find("\"overlays\"") != std::string::npos, "the file carries the spans and the engine's overlays");
        }
        Document back;
        CHECK(Document::Load(tfile, back, terror, nullptr), ("load with titles: " + terror).c_str());
        CHECK(back.titles.size() == 3 && back.titles[1].look.text == "Anna\nat the beach" && back.titles[1].look.box && back.titles[1].style == "lower", "the spans come back whole");
        CHECK(back.project.segments[0].overlays.empty() && back.project.segments[1].overlays.empty(), "the derived overlays are dropped on load");
        CHECK(back.nextTitleId == td.nextTitleId && back.SegmentsForEngine()[1].overlays.size() == td.SegmentsForEngine()[1].overlays.size(), "and derive the same again");
    }

    std::printf("\n-- the overlay-video lane --\n");
    {
        Document od;
        od.Import(clip);
        od.AppendMedia(*od.FindMedia(clip));          // 0..3 s
        od.AppendMedia(*od.FindMedia(clip));          // 3..6 s
        od.project.segments[1].transitionIn = VideoFXTransition::Crossfade(1.0);   // the second starts at 2 s
        const double clipSeconds = od.SegmentSeconds(0);
        CHECK(od.AddOverlay(song, 0.0) == -1 && od.overlays.empty(), "sound cannot go over the picture");
        const int first = od.AddOverlay(clip, 1.0);
        CHECK(first == 0 && od.overlays.size() == 1 && od.overlays[0].id == "o1" && od.overlays[0].path == clip, "a video over the picture, with an id");
        CHECK(Near(od.overlays[0].start, 1.0, 1e-9) && Near(od.overlays[0].duration, clipSeconds, 1e-9) && od.overlays[0].sourceIn == 0.0, "for the file's length, from its start");
        CHECK(od.overlays[0].look.anchor == VideoFXAnchor::BottomRight && Near(od.overlays[0].look.imageHeight, 0.3, 1e-9) && !od.overlays[0].IsStill(), "bottom right, a third high");
        CHECK(od.overlays[0].Name() == "pattern.mkv" && od.OverlayIndex("o1") == 0 && od.OverlayIndex("c1") == -1 && od.TitleIndex("o1") == -1, "named by its file, found by id, not a title");
        CHECK(od.MediaInUse(clip) && !od.RemoveMedia(clip), "an overlay keeps its file in the bin");
        const int logo = od.AddOverlay(photo, 1.5);
        CHECK(logo == 1 && Near(od.overlays[1].start, od.overlays[0].End(), 1e-9) && Near(od.overlays[1].duration, 4.0, 1e-9) && od.overlays[1].IsStill(), "a picture goes after the first, for four seconds");
        od.overlays[1].start = 0.0;
        od.overlays[1].duration = 1.0;

        const std::vector<double> starts = od.SegmentStarts();
        std::vector<VideoFXSegment> engine = od.SegmentsForEngine();
        CHECK(od.project.segments[0].overlays.empty() && engine.size() == 2, "the document's segments stay clean");
        CHECK(engine[0].overlays.size() == 2 && engine[0].overlays[0].kind == VideoFXOverlayKind::Video && engine[0].overlays[0].videoPath == clip, "the first segment: the video overlay from 1 s");
        CHECK(Near(engine[0].overlays[0].start, 1.0, 1e-9) && engine[0].overlays[0].end == 0.0 && engine[0].overlays[0].videoStart == 0.0, "to the segment's end, the file from 0");
        CHECK(engine[0].overlays[1].kind == VideoFXOverlayKind::Image && engine[0].overlays[1].imagePath == photo && Near(engine[0].overlays[1].end, 1.0, 1e-9), "the picture as an image overlay in its window");
        CHECK(engine[1].overlays.size() == 1 && Near(engine[1].overlays[0].videoStart, starts[1] - 1.0, 1e-9) && engine[1].overlays[0].start == 0.0, "the second segment: the same file, from the second its piece begins at");

        TimelineModel om = BuildTimelineModel(od);
        const TimelineTrack* lane = om.FindTrack(kOverlayTrackId);
        CHECK(lane && lane->kind == TimelineTrackKind::Video && !lane->IsMagnetic() && lane->clips.size() == 2, "a free video lane with both overlays");
        CHECK(lane && lane->clips[1].id == "o1" && lane->clips[1].kind == TimelineClipKind::Video && lane->clips[1].source == clip && Near(lane->clips[1].sourceLength, clipSeconds, 1e-9), "a video clip with its source, so it trims to the file's length");
        CHECK(lane && lane->clips[0].id == "o2" && lane->clips[0].kind == TimelineClipKind::Image && lane->clips[0].name == "photo.png", "the picture as an image clip");

        TimelineTrack ot = *lane;
        double applied = 0.0;
        CHECK(TimelineTrimClip(ot, "o1", true, 1.5, 25.0, &applied), "the model trims the overlay's left edge");
        TimelineChange trimO;
        trimO.kind = TimelineChangeKind::Trim;
        trimO.trackId = kOverlayTrackId;
        trimO.before = {*lane->FindClip("o1")};
        trimO.after = {*ot.FindClip("o1")};
        const double into = applied - 1.0;   // the trim snapped to the frame grid; so far into the file now
        CHECK(ApplyChange(od, trimO) && Near(od.overlays[0].start, applied, 1e-9) && Near(od.overlays[0].sourceIn, into, 1e-9), "a left trim moves into the file too");
        TimelineChange moveO = trimO;
        moveO.kind = TimelineChangeKind::Move;
        moveO.after[0].start = 2.0;
        CHECK(ApplyChange(od, moveO) && Near(od.overlays[0].start, 2.0, 1e-9) && Near(od.overlays[0].sourceIn, into, 1e-9), "a move keeps the file's second");
        om = BuildTimelineModel(od);
        lane = om.FindTrack(kOverlayTrackId);
        ot = *lane;
        CHECK(TimelineSplitClip(ot, "o1", 3.0, 25.0, "o1:2"), "the model splits an overlay");
        TimelineChange splitO;
        splitO.kind = TimelineChangeKind::Split;
        splitO.trackId = kOverlayTrackId;
        splitO.before = {*lane->FindClip("o1")};
        splitO.after = {*ot.FindClip("o1"), *ot.FindClip("o1:2")};
        CHECK(ApplyChange(od, splitO) && od.overlays.size() == 3 && Near(od.overlays[0].duration, 1.0, 1e-9) && od.overlays[2].id == "o3", "a split makes two overlays");
        CHECK(Near(od.overlays[2].start, 3.0, 1e-9) && Near(od.overlays[2].sourceIn, into + 1.0, 1e-9) && od.overlays[2].path == clip, "the right one goes on in the file where the cut was");
        TimelineChange delO;
        delO.kind = TimelineChangeKind::Delete;
        delO.trackId = kOverlayTrackId;
        delO.before = {*ot.FindClip("o1:2")};
        delO.before[0].id = "o3";
        CHECK(ApplyChange(od, delO) && od.overlays.size() == 2, "an overlay deletes");
        CHECK(DescribeChange(delO) == "Delete clip", "named for the history");

        const std::string ofile = TempPath("overlays.ultravideo");
        std::string oerror;
        CHECK(od.Save(ofile, oerror), ("save with overlays: " + oerror).c_str());
        Document back;
        CHECK(Document::Load(ofile, back, oerror, nullptr), ("load with overlays: " + oerror).c_str());
        CHECK(back.overlays.size() == 2 && back.overlays[0].path == clip && Near(back.overlays[0].sourceIn, into, 1e-9) && Near(back.overlays[0].start, 2.0, 1e-9), "the spans come back with their file and second");
        CHECK(back.overlays[0].look.anchor == VideoFXAnchor::BottomRight && Near(back.overlays[0].look.imageHeight, 0.3, 1e-9) && back.overlays[1].path == photo, "place and size too, and the picture");
        CHECK(back.project.segments[0].overlays.empty() && back.project.segments[1].overlays.empty(), "the derived overlays are dropped on load");
        CHECK(back.nextOverlayId == od.nextOverlayId && back.SegmentsForEngine()[0].overlays.size() == od.SegmentsForEngine()[0].overlays.size(), "and derive the same again");
    }

    std::printf("\n-- the voice-and-sound lane --\n");
    {
        Document ad;
        ad.Import(clip);
        ad.AppendMedia(*ad.FindMedia(clip));          // 0..3 s
        ad.AppendMedia(*ad.FindMedia(clip));          // 3..6 s
        const double songSeconds = ad.Import(song).duration;
        CHECK(ad.AddSound(photo, 0.0) == -1 && ad.sounds.empty(), "a picture has no sound");
        CHECK(ad.AddSound(junk, 0.0) == -1 && ad.sounds.empty(), "an unreadable file goes nowhere");
        const int voice = ad.AddSound(song, 1.0);
        CHECK(voice == 0 && ad.sounds[0].id == "a1" && ad.sounds[0].path == song && ad.sounds[0].ducksMusic, "a voice-over, with an id, the music dipping under it");
        CHECK(Near(ad.sounds[0].start, 1.0, 1e-9) && Near(ad.sounds[0].duration, songSeconds, 1e-9) && ad.sounds[0].sourceIn == 0.0, "for the file's length, from its start");
        CHECK(ad.SoundIndex("a1") == 0 && ad.SoundIndex("o1") == -1 && ad.OverlayIndex("a1") == -1 && SegmentOfClip(ad, "a1") == -1, "found by id, nothing else");
        CHECK(ad.MediaInUse(song) && !ad.RemoveMedia(song), "a sound keeps its file in the bin");
        const int effect = ad.AddSound(clip, 0.5, false);
        CHECK(effect == 1 && Near(ad.sounds[1].start, ad.sounds[0].End(), 1e-9) && !ad.sounds[1].ducksMusic, "a video's sound as an effect goes after the first");
        CHECK(Near(ad.Length(), 6.0, 0.1), "sounds do not lengthen the picture");

        ad.sounds[0].fadeIn = 0.5;
        ad.sounds[0].volume = 0.8;
        std::vector<VideoFXAudioClip> clips = ad.AudioClipsForEngine();
        CHECK(clips.size() == 2 && clips[0].path == song && Near(clips[0].at, 1.0, 1e-9) && clips[0].start == 0.0 && Near(clips[0].end, songSeconds, 1e-9), "the engine's audio clips: at 1 s, the whole file");
        CHECK(Near(clips[0].volume, 0.8, 1e-9) && Near(clips[0].fadeIn, 0.5, 1e-9) && clips[0].ducksMusic && !clips[1].ducksMusic, "with volume, fades and ducking");
        CHECK(ad.project.settings.audioClips.empty(), "the project's own audio clips stay empty: the spans are the truth");

        TimelineModel am = BuildTimelineModel(ad);
        const TimelineTrack* lane = am.FindTrack(kSoundTrackId);
        CHECK(am.tracks.size() == 5 && am.tracks[3].id == kSoundTrackId && am.tracks[4].id == kMusicTrackId, "the lane sits between the main track and the music");
        CHECK(lane && lane->kind == TimelineTrackKind::Audio && !lane->IsMagnetic() && lane->clips.size() == 2, "a free audio lane with both sounds");
        CHECK(lane && lane->clips[0].id == "a1" && lane->clips[0].kind == TimelineClipKind::Audio && lane->clips[0].source == song &&
              Near(lane->clips[0].sourceLength, songSeconds, 1e-9) && Near(lane->clips[0].fadeIn, 0.5, 1e-9), "an audio clip with its source and fades, for the waveform and trims");

        TimelineTrack at = *lane;
        double applied = 0.0;
        CHECK(TimelineTrimClip(at, "a1", true, 1.5, 25.0, &applied), "the model trims the sound's left edge");
        TimelineChange trimA;
        trimA.kind = TimelineChangeKind::Trim;
        trimA.trackId = kSoundTrackId;
        trimA.before = {*lane->FindClip("a1")};
        trimA.after = {*at.FindClip("a1")};
        const double into = applied - 1.0;
        CHECK(ApplyChange(ad, trimA) && Near(ad.sounds[0].start, applied, 1e-9) && Near(ad.sounds[0].sourceIn, into, 1e-9), "a left trim moves into the file too");
        CHECK(Near(ad.AudioClipsForEngine()[0].start, into, 1e-9) && Near(ad.AudioClipsForEngine()[0].end, songSeconds, 1e-6), "and the engine's clip starts there in the file");
        TimelineChange fade;
        fade.kind = TimelineChangeKind::FadeSet;
        fade.trackId = kSoundTrackId;
        fade.after = {*at.FindClip("a1")};
        fade.after[0].fadeOut = 1.0;
        CHECK(ApplyChange(ad, fade) && Near(ad.sounds[0].fadeOut, 1.0, 1e-9), "a fade handle sets the fade");
        am = BuildTimelineModel(ad);
        lane = am.FindTrack(kSoundTrackId);
        at = *lane;
        const double cut = ad.sounds[0].start + 1.0;
        CHECK(TimelineSplitClip(at, "a1", cut, 25.0, "a1:2"), "the model splits a sound");
        TimelineChange splitA;
        splitA.kind = TimelineChangeKind::Split;
        splitA.trackId = kSoundTrackId;
        splitA.before = {*lane->FindClip("a1")};
        splitA.after = {*at.FindClip("a1"), *at.FindClip("a1:2")};
        CHECK(ApplyChange(ad, splitA) && ad.sounds.size() == 3 && Near(ad.sounds[0].duration, 1.0, 1e-9) && ad.sounds[2].id == "a3", "a split makes two sounds");
        CHECK(Near(ad.sounds[2].sourceIn, into + 1.0, 1e-9) && ad.sounds[0].fadeOut == 0.0 && ad.sounds[2].fadeIn == 0.0 && Near(ad.sounds[2].fadeOut, 1.0, 1e-9),
              "the right one goes on in the file, the fades stay at the outer ends");
        TimelineChange delA;
        delA.kind = TimelineChangeKind::Delete;
        delA.trackId = kSoundTrackId;
        delA.before = {*at.FindClip("a1:2")};
        delA.before[0].id = "a2";
        CHECK(ApplyChange(ad, delA) && ad.sounds.size() == 2 && ad.SoundIndex("a2") == -1, "a sound deletes");

        const std::string afile = TempPath("sounds.ultravideo");
        std::string aerror;
        CHECK(ad.Save(afile, aerror), ("save with sounds: " + aerror).c_str());
        {
            std::FILE* f = std::fopen(afile.c_str(), "rb");
            std::string text;
            if (f) { char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n); std::fclose(f); }
            CHECK(text.find("\"sounds\"") != std::string::npos && text.find("\"audioClips\"") != std::string::npos, "the file carries the spans and the engine's audio clips");
        }
        Document back;
        CHECK(Document::Load(afile, back, aerror, nullptr), ("load with sounds: " + aerror).c_str());
        CHECK(back.sounds.size() == 2 && back.sounds[0].path == song && Near(back.sounds[0].sourceIn, into, 1e-9) && Near(back.sounds[0].volume, 0.8, 1e-9), "the spans come back with their file, second and volume");
        CHECK(back.sounds[0].ducksMusic && Near(back.sounds[0].fadeIn, 0.5, 1e-9) && back.sounds[1].id == "a3" && Near(back.sounds[1].fadeOut, 1.0, 1e-9), "ducking, fades and ids too");
        CHECK(back.project.settings.audioClips.empty() && back.nextSoundId == ad.nextSoundId && back.AudioClipsForEngine().size() == 2, "the engine's clips are dropped on load and derive the same again");

        // A project from `videofx` carries audio clips but no spans: they become spans
        VideoFXProject plain;
        plain.segments = {VideoFXSegment::FromFile(clip)};
        plain.settings.audioClips = {VideoFXAudioClip::Sound(song, 0.5, 0.7)};
        std::string plainText;
        CHECK(VideoFX_ProjectToJson(plain, plainText, "") == VideoFXResult::Ok, "a plain VideoFX project");
        const std::string pfile = TempPath("plain.ultravideo");
        if (std::FILE* f = std::fopen(pfile.c_str(), "wb")) {
            const std::string text = "{\"format\": \"ultravideo\", \"version\": 1, \"project\": " + plainText + "}\n";
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
        Document fromPlain;
        CHECK(Document::Load(pfile, fromPlain, aerror, nullptr), ("load a plain project: " + aerror).c_str());
        CHECK(fromPlain.sounds.size() == 1 && fromPlain.sounds[0].id == "a1" && Near(fromPlain.sounds[0].start, 0.5, 1e-9) && !fromPlain.sounds[0].ducksMusic, "its audio clip becomes a sound span");
        CHECK(Near(fromPlain.sounds[0].duration, songSeconds, 1e-6) && Near(fromPlain.sounds[0].volume, 0.7, 1e-9) && fromPlain.project.settings.audioClips.empty(), "for the file's length, at its volume");
    }

    std::printf("\n-- effect presets --\n");
    {
        Document ed;
        ed.Import(clip);
        ed.Import(photo);
        ed.AppendMedia(*ed.FindMedia(clip));          // 320 x 240
        ed.AppendMedia(*ed.FindMedia(photo));
        CHECK(EffectPresets().size() >= 10 && FindEffectPreset("warm") && FindEffectPreset("softblur") && !FindEffectPreset("nope"), "the presets, found by key");
        bool looks = false, blurs = false;
        for (const EffectPreset& e : EffectPresets()) { looks |= !e.blur; blurs |= e.blur; }
        CHECK(looks && blurs, "colour looks and blurs");
        CHECK(!ed.ApplyEffectPreset(0, "nope") && !ed.ApplyEffectPreset(5, "warm") && ed.project.segments[0].effects.empty(), "no preset or no clip: nothing");
        CHECK(ed.ApplyEffectPreset(0, "warm") && ed.project.segments[0].effects.size() == 2, "Warm puts its two effects on the clip");
        CHECK(ed.project.segments[0].effects[0].type == VideoFXEffectType::Temperature && Near(ed.project.segments[0].effects[0].amount, 0.3, 1e-9), "the engine's own effects");
        CHECK(!ed.ApplyEffectPreset(0, "warm") && ed.project.segments[0].effects.size() == 2, "a second click does not double it");
        CHECK(ed.ApplyEffectPreset(0, "strongblur") && ed.project.segments[0].effects.size() == 3, "a blur goes after the look");
        CHECK(Near(ed.project.segments[0].effects[2].amount, 3.5, 1e-9), "its radius from the clip's 240 lines (1.5 % of the height, to half a pixel)");
        CHECK(ed.ApplyEffectPreset(1, "softblur") && Near(ed.project.segments[1].effects[0].amount, 0.5, 1e-9), "a photo's blur from its own 120 lines, at least half a pixel");
        CHECK(DescribeEffect(ed.project.segments[0].effects[0]) == "Warmer 30 %" && DescribeEffect(ed.project.segments[0].effects[1]) == "Saturation +10 %", "the inspector's names for them");
        CHECK(DescribeEffect(ed.project.segments[0].effects[2]) == "Blur 3.5 px" && DescribeEffect(VideoFXEffect::Temperature(-0.3)) == "Cooler 30 %", "a blur in pixels, cool as cooler");
        CHECK(DescribeEffect(VideoFXEffect::Grayscale()) == "Black and white" && DescribeEffect(VideoFXEffect::Vignette(0.5)) == "Dark corners 50 %", "and the others by what they do");
        const TimelineModel em = BuildTimelineModel(ed);
        CHECK(Main(em) && Main(em)->clips[0].hasEffects, "the clip shows the effects badge");

        // every preset renders: the engine takes each one's effects
        bool all = true;
        std::string failed;
        for (const EffectPreset& e : EffectPresets()) {
            Document one;
            one.Import(clip);
            one.AppendMedia(*one.FindMedia(clip));
            one.ApplyEffectPreset(0, e.key);
            VideoFXFrame frame;
            if (VideoFX_RenderFrame(one.SegmentsForEngine(), 1.0, frame, {}, 160, 120) != VideoFXResult::Ok || !frame.IsValid()) {
                all = false;
                failed += std::string(" ") + e.key;
            }
        }
        CHECK(all, ("every preset renders a frame" + failed).c_str());
        VideoFXFrame plainFrame, bwFrame;
        Document plain;
        plain.Import(clip);
        plain.AppendMedia(*plain.FindMedia(clip));
        Document grey = plain;
        grey.ApplyEffectPreset(0, "bw");
        CHECK(VideoFX_RenderFrame(plain.SegmentsForEngine(), 1.0, plainFrame, {}, 160, 120) == VideoFXResult::Ok &&
              VideoFX_RenderFrame(grey.SegmentsForEngine(), 1.0, bwFrame, {}, 160, 120) == VideoFXResult::Ok, "a frame with and without a look");
        bool allGrey = bwFrame.IsValid(), differs = false;
        for (size_t i = 0; allGrey && i + 3 < bwFrame.pixels.size(); i += 4) {
            const int r = bwFrame.pixels[i], g = bwFrame.pixels[i + 1], b = bwFrame.pixels[i + 2];
            allGrey = std::abs(r - g) <= 6 && std::abs(g - b) <= 6;
        }
        for (size_t i = 0; i < plainFrame.pixels.size() && i < bwFrame.pixels.size() && !differs; ++i) differs = plainFrame.pixels[i] != bwFrame.pixels[i];
        CHECK(allGrey && differs, "Black and white turns the test pattern grey");

        const std::string efile = TempPath("effects.ultravideo");
        std::string eerror;
        CHECK(ed.Save(efile, eerror), ("save with effects: " + eerror).c_str());
        Document back;
        CHECK(Document::Load(efile, back, eerror, nullptr), ("load with effects: " + eerror).c_str());
        CHECK(back.project.segments.size() == 2 && back.project.segments[0].effects.size() == 3 && Near(back.project.segments[0].effects[2].amount, 3.5, 1e-9), "the effects come back with the project");
    }

    std::printf("\n-- the story wizard --\n");
    {
        const std::string beach = TempPath("media/at_the-beach.png");
        CHECK(VideoFX_SaveFrameImage(Solid(160, 120, 0, 0, 255), beach) == VideoFXResult::Ok, "a second photo");
        StoryChoice choice;
        choice.files = {photo, clip, beach};
        choice.secondsPerPhoto = 2.0;
        choice.songs = {song};
        choice.captionsFromNames = true;
        choice.transition = VideoFXTransitionType::WipeLeft;
        choice.transitionSeconds = 0.5;
        std::vector<VideoFXSegment> planned;
        VideoFXExportSettings settings;
        std::string why;
        CHECK(PlanStory(choice, planned, settings, why), ("plan: " + why).c_str());
        CHECK(planned.size() == 2 && Near(planned[0].duration, 2.0, 1e-6), "the engine plans the photos alone, at the chosen pace");
        CHECK(settings.music.Songs() == std::vector<std::string>{song} && Near(settings.music.duckingThresholdDb, -36.5, 1e-9), "the song and the ducking are in the settings");
        CHECK(settings.fitMode == VideoFXFitMode::Letterbox, "clips of another shape are shown whole, like the photos");
        StoryChoice filling = choice;
        filling.portraitFit = VideoFXImageFit::Cover;
        CHECK(PlanStory(filling, planned, settings, why) && settings.fitMode == VideoFXFitMode::Fill, "or fill the frame when the photos do");
        const Document story = BuildStoryDocument(choice, planned, settings);
        CHECK(story.project.title == "Story" && story.project.segments.size() == 3 && story.clipIds.size() == 3, "photo, clip, photo");
        CHECK(story.project.segments[0].kind == VideoFXSourceKind::Image && story.project.segments[1].kind == VideoFXSourceKind::File &&
              story.project.segments[1].path == clip && story.project.segments[2].path == beach, "in the chosen order");
        CHECK(story.project.segments[1].transitionIn.type == VideoFXTransitionType::WipeLeft && Near(story.project.segments[1].transitionIn.duration, 0.5, 1e-9) &&
              story.project.segments[2].transitionIn.type == VideoFXTransitionType::WipeLeft, "the style's transition leads into every later segment");
        CHECK(story.project.segments[0].transitionIn.type == VideoFXTransitionType::Cut, "nothing leads into the first");
        CHECK(!story.project.segments[0].effects.empty() && story.project.segments[0].effects.front().type == VideoFXEffectType::FadeIn, "the fade in on the first segment");
        CHECK(!story.project.segments[2].effects.empty() && story.project.segments[2].effects.back().type == VideoFXEffectType::FadeOut, "the fade out on the last");
        bool middleFades = false;
        for (const VideoFXEffect& e : story.project.segments[1].effects) middleFades |= e.type == VideoFXEffectType::FadeIn || e.type == VideoFXEffectType::FadeOut;
        CHECK(!middleFades, "and none in between");
        CHECK(story.titles.size() == 3 && story.titles[0].look.text == "photo" && story.titles[1].look.text == "pattern" && story.titles[2].look.text == "at the beach", "captions from the file names, as title spans");
        CHECK(story.titles[1].style == "caption" && Near(story.titles[1].start, 1.5, 1e-6) && Near(story.titles[1].duration, story.SegmentSeconds(1), 1e-6), "each over its own segment, which begins under the wipe");
        bool textOverlays = false;
        for (const VideoFXSegment& sg : story.project.segments) for (const VideoFXOverlay& o : sg.overlays) textOverlays |= o.kind == VideoFXOverlayKind::Text;
        CHECK(!textOverlays, "the engine's caption overlays are not kept twice");
        CHECK(story.project.settings.music.Songs() == std::vector<std::string>{song} && story.media.size() == 4 && story.FindMedia(song), "the music set and everything in the bin");
        const TimelineModel storyModel = BuildTimelineModel(story);
        CHECK(storyModel.tracks.size() == 5 && storyModel.tracks[0].clips.size() == 3 && storyModel.tracks[2].clips.size() == 3, "a timeline with the captions and the clips");

        StoryChoice plain;
        plain.files = {clip};
        plain.fadeInOut = false;
        CHECK(PlanStory(plain, planned, settings, why) && planned.empty() && !settings.music.IsSet(), "clips alone need no plan");
        const Document clips = BuildStoryDocument(plain, planned, settings);
        CHECK(clips.project.segments.size() == 1 && clips.project.segments[0].effects.empty() && clips.titles.empty(), "one clip, no fades, no captions");
    }

    std::printf("\n-- history --\n");
    History history;
    Document before = doc;
    doc.project.segments[0].mute = true;
    history.Push("Mute clip", before, doc);
    CHECK(history.CanUndo() && !history.CanRedo() && history.UndoLabel() == "Mute clip" && !history.IsClean(), "an edit is undoable and dirties");
    doc.view.playhead = 3.0;
    CHECK(history.Undo(doc) && !doc.project.segments[0].mute && doc.view.playhead == 3.0, "undo restores the edit and keeps the view");
    CHECK(history.IsClean() && history.CanRedo() && history.RedoLabel() == "Mute clip", "clean again, redo offered");
    CHECK(history.Redo(doc) && doc.project.segments[0].mute, "redo");
    history.MarkClean();
    CHECK(history.IsClean(), "saved here");
    history.Undo(doc);
    CHECK(!history.IsClean(), "and dirty when stepped away from the save");
    history.Redo(doc);
    history.Push("Another", doc, doc);
    CHECK(!history.CanRedo(), "a new edit drops the redo branch");

    std::printf("\n-- the file --\n");
    const std::string file = TempPath("trip.ultravideo");
    doc.project.title = "Summer trip";
    doc.view.pixelsPerSecond = 120.0;
    doc.view.dark = false;
    std::string error;
    CHECK(doc.Save(file, error), ("save: " + error).c_str());
    CHECK(doc.path == file, "the document remembers its file");
    {
        std::FILE* f = std::fopen(file.c_str(), "rb");
        std::string text;
        if (f) { char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n); std::fclose(f); }
        CHECK(text.find("\"format\": \"ultravideo\"") != std::string::npos, "an ultravideo file");
        CHECK(text.find("media/pattern.mkv") != std::string::npos && text.find(TempPath("media/pattern.mkv")) == std::string::npos, "media paths relative to the file");
    }
    Document loaded;
    std::vector<std::string> missing;
    CHECK(Document::Load(file, loaded, error, &missing), ("load: " + error).c_str());
    CHECK(loaded.project.title == "Summer trip" && loaded.project.segments.size() == 4 && loaded.clipIds == doc.clipIds, "title, segments and ids back");
    CHECK(loaded.project.segments[2].kind == VideoFXSourceKind::File && loaded.project.segments[2].path == clip, "file paths absolute again");
    CHECK(loaded.project.settings.music.path == song && loaded.media.size() == 3, "the music and the bin");
    CHECK(loaded.markers.size() == 1 && loaded.markers[0].id == "m2", "the markers");
    CHECK(loaded.view.pixelsPerSecond == 120.0 && !loaded.view.dark && loaded.view.rangeOut == 6.0, "the view");
    CHECK(loaded.view.useProxies, "proxies on by default, and kept");
    CHECK(missing.empty() && loaded.nextClipId == doc.nextClipId, "nothing missing; ids go on from where they were");
    std::error_code ec;
    fs::remove(PathFromUtf8(photo), ec);
    Document again;
    CHECK(Document::Load(file, again, error, &missing) && missing.size() == 1 && missing[0] == photo, "a missing photo is reported, the project still loads");
    CHECK(again.FindMedia(photo) && !again.FindMedia(photo)->error.empty(), "and stays in the bin, marked");
    CHECK(!Document::Load(TempPath("nowhere.ultravideo"), again, error, nullptr) && !error.empty(), "a missing file fails with a reason");
    CHECK(!Document::Load(junk, again, error, nullptr), "a text file is not a project");

    std::printf("\n-- proxies --\n");
    {
        // a 640x360 clip: taller than a 240-line proxy; the 320x240 one is not
        const std::string big = TempPath("media/big.mkv");
        VideoFXExportSettings s;
        s.container = VideoFXContainer::MKV;
        s.videoCodec = VideoFXVideoCodec::MPEG4;
        s.audioCodec = VideoFXAudioCodec::AAC;
        s.width = 640; s.height = 360; s.frameRate = 25.0;
        CHECK(VideoFX_Export({VideoFXSegment::TestPattern(2.0)}, big, s) == VideoFXResult::Ok, "a 640x360 clip");
        CHECK(ProxyMaker::EditorSettings().acceleration == VideoFXAcceleration::Hardware &&
              ProxyMaker::EditorSettings().height == VideoFXProxySettings{}.height,
              "the editor's proxies: VideoFX's size, on the graphics hardware where one works");
        VideoFXProxySettings ps = ProxyMaker::EditorSettings();   // the editor's own, so its encoder is exercised
        ps.height = 240;
        ps.cacheDirectory = TempPath("proxies");
        std::vector<std::pair<std::string, ProxyMaker::State>> done;
        auto waitFor = [&](ProxyMaker& m, size_t count) {
            for (int i = 0; i < 600 && done.size() < count; ++i) {
                m.Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            m.Pump();
        };
        {
            ProxyMaker maker(ps);
            maker.onDone = [&](const std::string& c, ProxyMaker::State st) { done.emplace_back(c, st); };
            CHECK(maker.StateOf(big) == ProxyMaker::State::Unknown, "unknown before it is asked for");
            maker.Want(big);
            maker.Want(clip);
            maker.Want(big);                                   // asked twice: made once
            CHECK(maker.Pending() >= 1, "queued");
            waitFor(maker, 2);
            CHECK(done.size() == 2, "both settled, reported on Pump");
            CHECK(maker.StateOf(big) == ProxyMaker::State::Ready && VideoFX_HasProxy(big, ps), "the big clip has its proxy");
            CHECK(maker.StateOf(clip) == ProxyMaker::State::NotNeeded && !VideoFX_HasProxy(clip, ps),
                  "the small one needs none");
            CHECK(maker.Badge(big) == "proxy" && maker.Badge(clip).empty() && maker.Pending() == 0, "badges, nothing pending");
            const std::vector<VideoFXSegment> played = maker.Use({VideoFXSegment::FromFile(big), VideoFXSegment::FromFile(clip)});
            CHECK(played.size() == 2 && played[0].path == VideoFX_ProxyPath(big, ps) && played[1].path == clip,
                  "the timeline plays the big clip from its proxy, the small one as it is");
        }
        {
            // a new session: the proxy from the cache is ready at once
            done.clear();
            ProxyMaker maker(ps);
            maker.onDone = [&](const std::string& c, ProxyMaker::State st) { done.emplace_back(c, st); };
            maker.Want(big);
            CHECK(maker.StateOf(big) == ProxyMaker::State::Ready, "made in an earlier session: ready at once");
            maker.Pump();
            CHECK(done.size() == 1 && done[0].second == ProxyMaker::State::Ready, "and reported");
        }
        {
            // dropped from the bin before its turn: never made
            VideoFXProxySettings other = ps;
            other.height = 120;
            done.clear();
            ProxyMaker maker(other);
            maker.onDone = [&](const std::string& c, ProxyMaker::State st) { done.emplace_back(c, st); };
            maker.Want(big);
            maker.Keep({});
            for (int i = 0; i < 40; ++i) { maker.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
            CHECK(maker.StateOf(big) == ProxyMaker::State::Unknown && maker.Pending() == 0, "forgotten when it leaves the bin");
            CHECK(!VideoFX_HasProxy(big, other), "and no proxy left behind");
        }
        {
            // a missing file fails; the window falls back to the original
            done.clear();
            ProxyMaker maker(ps);
            maker.onDone = [&](const std::string& c, ProxyMaker::State st) { done.emplace_back(c, st); };
            maker.Want(TempPath("media/gone.mkv"));
            waitFor(maker, 1);
            CHECK(done.size() == 1 && done[0].second == ProxyMaker::State::Failed &&
                  !maker.Error(TempPath("media/gone.mkv")).empty(), "a missing clip fails, with the reason");
        }
        VideoFX_ClearProxies(ps);
    }

    fs::remove_all(PathFromUtf8(TempPath("")), ec);
    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
