// Tests/UltraVideoDocumentTest.cpp
// UltraVideo's document, mapping and history without a display: importing
// media the engine probes, segments from media, the timeline model derived
// from the document, every reported change written back, undo and redo, and
// the .ultravideo file round trip with relative media paths and missing
// media reported. Needs the VideoFX engine (FFmpeg) for the probes and the
// clips it generates; without it the suite skips itself.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraVideoDocument.h"
#include "UltraVideoHistory.h"
#include "UltraVideoMapping.h"

#include "UltraCanvasPathUtf8.h"
#include "VideoFX/VideoFX.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
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
    CHECK(music && music->locked && music->clips.size() == 1 && music->clips[0].kind == TimelineClipKind::Audio, "the music lane, locked, with the song");
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
    TimelineChange onMusic = del;
    onMusic.trackId = kMusicTrackId;
    CHECK(!ApplyChange(doc, onMusic), "the music lane is not edited from the timeline");
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
        CHECK(tm.tracks.size() == 3 && tm.tracks[0].id == kTitleTrackId, "the title lane sits above the main track");

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
    CHECK(missing.empty() && loaded.nextClipId == doc.nextClipId, "nothing missing; ids go on from where they were");
    std::error_code ec;
    fs::remove(PathFromUtf8(photo), ec);
    Document again;
    CHECK(Document::Load(file, again, error, &missing) && missing.size() == 1 && missing[0] == photo, "a missing photo is reported, the project still loads");
    CHECK(again.FindMedia(photo) && !again.FindMedia(photo)->error.empty(), "and stays in the bin, marked");
    CHECK(!Document::Load(TempPath("nowhere.ultravideo"), again, error, nullptr) && !error.empty(), "a missing file fails with a reason");
    CHECK(!Document::Load(junk, again, error, nullptr), "a text file is not a project");

    fs::remove_all(PathFromUtf8(TempPath("")), ec);
    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
