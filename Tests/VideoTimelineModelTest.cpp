// Tests/VideoTimelineModelTest.cpp
// The timeline model behind UltraCanvasVideoTimeline, without a display: a
// magnetic track's derived starts with transitions, cuts, splitting, trimming
// (limits, ripple, free-track neighbours), moving (reorder on a magnetic
// track, placement against neighbours on a free one), deleting with and
// without ripple, duplicating, the ruler's steps, timecode and snapping.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraCanvasVideoTimelineModel.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int failures = 0;

static bool Near(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d)\n", msg, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static TimelineClip Clip(const std::string& id, double duration, double sourceLength = 0.0, double sourceIn = 0.0) {
    TimelineClip c;
    c.id = id;
    c.name = id;
    c.duration = duration;
    c.sourceLength = sourceLength;
    c.sourceIn = sourceIn;
    return c;
}

static TimelineTrack MainTrack() {
    TimelineTrack t;
    t.id = "V1";
    t.kind = TimelineTrackKind::MainVideo;
    t.clips = {Clip("a", 4.0, 10.0), Clip("b", 3.0, 8.0, 2.0), Clip("c", 5.0, 5.0)};
    TimelineDeriveStarts(t);
    return t;
}

static TimelineTrack FreeTrack() {
    TimelineTrack t;
    t.id = "V2";
    t.kind = TimelineTrackKind::Video;
    t.clips = {Clip("x", 2.0, 6.0), Clip("y", 2.0, 6.0, 3.0)};   // y has 3 s of source before its in point
    t.clips[0].start = 1.0;
    t.clips[1].start = 5.0;
    return t;
}

static void TestDerivedStarts() {
    std::printf("Derived starts\n");
    TimelineTrack t = MainTrack();
    CHECK(Near(t.clips[0].start, 0.0) && Near(t.clips[1].start, 4.0) && Near(t.clips[2].start, 7.0), "sequential starts");
    CHECK(Near(t.End(), 12.0), "the track ends after the last clip");
    t.transitions.push_back({"b", "crossfade", 1.0, 3.0});
    TimelineDeriveStarts(t);
    CHECK(Near(t.clips[1].start, 3.0) && Near(t.clips[2].start, 6.0), "a 1 s transition into b pulls b and c 1 s earlier");
    std::vector<double> cuts = TimelineCuts(t);
    CHECK(cuts.size() == 5 && Near(cuts[0], 0.0) && Near(cuts[1], 3.0) && Near(cuts[2], 4.0) && Near(cuts[3], 6.0) &&
              Near(cuts[4], 11.0),
          "cuts: 0, 3, 4, 6, 11");
    TimelineTrack f = FreeTrack();
    TimelineDeriveStarts(f);
    CHECK(Near(f.clips[0].start, 1.0) && Near(f.clips[1].start, 5.0), "a free track keeps its starts");
    TimelineModel m;
    m.tracks = {t, f};
    CHECK(Near(m.Length(), 11.0), "the model's length is the furthest end");
    CHECK(m.TrackOfClip("y") && m.TrackOfClip("y")->id == "V2" && !m.TrackOfClip("q"), "TrackOfClip");
}

static void TestSplit() {
    std::printf("Split\n");
    TimelineTrack t = MainTrack();
    CHECK(TimelineSplitClip(t, "b", 5.5, 25.0, "b2"), "split b at 5.5 s");
    CHECK(t.clips.size() == 4 && t.clips[1].id == "b" && t.clips[2].id == "b2", "b2 follows b");
    CHECK(Near(t.clips[1].duration, 1.5) && Near(t.clips[2].duration, 1.5), "1.5 s each");
    CHECK(Near(t.clips[2].sourceIn, 3.5) && Near(t.clips[2].start, 5.5), "the right part continues the source and starts at the cut");
    CHECK(Near(t.clips[3].start, 7.0), "c has not moved");
    CHECK(!TimelineSplitClip(t, "a", 0.01, 25.0, "a2"), "no split within a frame of an edge");
    CHECK(!TimelineSplitClip(t, "a", 2.0, 25.0, "b"), "no split onto an id in use");
    CHECK(!TimelineSplitClip(t, "zz", 2.0, 25.0, "z2"), "no split of an absent clip");
}

static void TestTrim() {
    std::printf("Trim\n");
    TimelineTrack t = MainTrack();
    double applied = 0.0;
    CHECK(TimelineTrimClip(t, "a", false, 3.0, 25.0, &applied), "a's right edge to 3 s");
    CHECK(Near(t.clips[0].duration, 3.0) && Near(t.clips[1].start, 3.0) && Near(t.clips[2].start, 6.0),
          "b and c ripple earlier on the magnetic track");
    CHECK(TimelineTrimClip(t, "a", false, 20.0, 25.0, &applied), "a's right edge beyond the source");
    CHECK(Near(applied, 10.0) && Near(t.clips[0].duration, 10.0), "clamped to the 10 s of source");
    CHECK(TimelineTrimClip(t, "b", true, 11.0, 25.0, &applied), "b's left edge 1 s later");
    CHECK(Near(t.clips[1].duration, 2.0) && Near(t.clips[1].sourceIn, 3.0) && Near(t.clips[1].start, 10.0) &&
              Near(t.clips[2].start, 12.0),
          "b lost a second of source at its start and stays at a's end; c ripples earlier");
    CHECK(TimelineTrimClip(t, "b", true, 0.0, 25.0, &applied), "b's left edge as early as it goes");
    CHECK(Near(t.clips[1].sourceIn, 0.0) && Near(t.clips[1].duration, 5.0), "no source before 0: in point 0, 5 s long");
    CHECK(!TimelineTrimClip(t, "c", false, 25.0, 25.0, &applied) && Near(t.clips[2].duration, 5.0), "c cannot grow past its 5 s of source");

    TimelineTrack f = FreeTrack();
    CHECK(TimelineTrimClip(f, "x", false, 6.0, 25.0, &applied), "x's right edge towards y");
    CHECK(Near(applied, 5.0) && Near(f.clips[0].End(), 5.0), "stops at y's start");
    CHECK(!TimelineTrimClip(f, "x", false, 5.0, 25.0, &applied), "a trim that changes nothing is reported as none");
    f = FreeTrack();
    CHECK(TimelineTrimClip(f, "y", true, 2.0, 25.0, &applied), "y's left edge towards x");
    CHECK(Near(applied, 3.0) && Near(f.clips[1].start, 3.0) && Near(f.clips[1].sourceIn, 1.0), "stops at x's end, two seconds earlier in the source");
    f = FreeTrack();
    f.clips[0].start = 0.0;                                    // x at 0..2: y's source is the limit now
    CHECK(TimelineTrimClip(f, "y", true, 0.0, 25.0, &applied), "y's left edge as far as its source goes");
    CHECK(Near(applied, 2.0) && Near(f.clips[1].sourceIn, 0.0), "3 s of source before the in point: stops at 2 s");
    TimelineTrack s = MainTrack();
    s.clips[0].speed = 2.0;
    s.clips[0].duration = 2.0;            // 4 s of source at 2x
    TimelineDeriveStarts(s);
    CHECK(TimelineTrimClip(s, "a", false, 5.0, 25.0, &applied), "a sped-up clip's right edge");
    CHECK(Near(applied, 5.0) && Near(s.clips[0].SourceOut(), 10.0), "5 s at 2x uses all 10 s of source");
    CHECK(!TimelineTrimClip(s, "a", false, 6.0, 25.0, &applied), "and no further");
}

static void TestMove() {
    std::printf("Move\n");
    TimelineTrack t = MainTrack();
    CHECK(TimelineMoveClip(t, "c", 0, 0.0), "c to the front");
    CHECK(t.clips[0].id == "c" && Near(t.clips[0].start, 0.0) && Near(t.clips[1].start, 5.0) && Near(t.clips[2].start, 9.0),
          "c, a, b with derived starts");
    CHECK(!TimelineMoveClip(t, "c", 0, 0.0), "moving to the same place changes nothing");
    CHECK(TimelineMoveClip(t, "c", 7, 0.0), "an index past the end lands last");
    CHECK(t.clips[2].id == "c", "c is last");

    TimelineTrack f = FreeTrack();
    CHECK(TimelineMoveClip(f, "y", 0, 9.0), "y to 9 s");
    CHECK(Near(f.clips[1].start, 9.0), "placed");
    CHECK(TimelineMoveClip(f, "y", 0, 2.5), "y onto x");
    CHECK(Near(f.clips[1].start, 3.0), "lands right after x");
    CHECK(!TimelineMoveClip(f, "y", 0, -4.0) && Near(f.clips[1].start, 3.0), "y before 0 cannot pass x: it stays after it");
    f = FreeTrack();
    CHECK(TimelineMoveClip(f, "x", 0, -4.0) && Near(f.clips[0].start, 0.0), "x before 0 lands at 0");
}

static void TestDeleteAndDuplicate() {
    std::printf("Delete and duplicate\n");
    TimelineTrack t = MainTrack();
    t.transitions.push_back({"b", "crossfade", 0.5, 3.0});
    CHECK(TimelineDeleteClips(t, {"b"}, true), "delete b");
    CHECK(t.clips.size() == 2 && t.clips[1].id == "c" && Near(t.clips[1].start, 4.0), "c closes up");
    CHECK(t.transitions.empty(), "the transition into b went with it");
    CHECK(!TimelineDeleteClips(t, {"nothing"}, true), "nothing to delete");

    TimelineTrack f = FreeTrack();
    CHECK(TimelineDeleteClips(f, {"x"}, false), "delete x, no ripple");
    CHECK(f.clips.size() == 1 && Near(f.clips[0].start, 5.0), "y stays");
    f = FreeTrack();
    CHECK(TimelineDeleteClips(f, {"x"}, true), "delete x with ripple");
    CHECK(Near(f.clips[0].start, 3.0), "y moves 2 s earlier");

    t = MainTrack();
    CHECK(TimelineDuplicateClip(t, "a", "a-copy"), "duplicate a");
    CHECK(t.clips.size() == 4 && t.clips[1].id == "a-copy" && Near(t.clips[1].start, 4.0) && Near(t.clips[2].start, 8.0),
          "the copy follows a and b moves on");
    f = FreeTrack();
    CHECK(TimelineDuplicateClip(f, "x", "x-copy"), "duplicate x on a free track");
    CHECK(f.clips.size() == 3 && Near(f.clips[2].start, 3.0), "in the gap after x");
    f.clips[1].start = 3.0;                                   // y right after x: no gap
    CHECK(TimelineDuplicateClip(f, "x", "x-copy2"), "duplicate x again");
    CHECK(Near(f.clips[3].start, 5.0) || Near(f.clips[3].start, 7.0), "after y, or after the first copy");
}

static void TestRulerTimecodeSnap() {
    std::printf("Ruler, timecode, snap\n");
    double minor = 0.0, major = 0.0;
    TimelineRulerSteps(80.0, 25.0, minor, major);
    CHECK(Near(minor, 0.2) && Near(major, 1.0), "80 px/s at 25 fps: minor 5 frames, major 1 s");
    TimelineRulerSteps(8.0, 25.0, minor, major);
    CHECK(Near(minor, 1.0) && Near(major, 10.0), "8 px/s: minor 1 s, major 10 s");
    TimelineRulerSteps(600.0, 25.0, minor, major);
    CHECK(Near(minor, 0.04) && Near(major, 0.2), "600 px/s: minor one frame, major five frames");
    TimelineRulerSteps(0.5, 25.0, minor, major);
    CHECK(Near(minor, 30.0) && Near(major, 300.0), "0.5 px/s: half-minute ticks, labelled every five minutes");
    TimelineRulerSteps(0.0, 25.0, minor, major);
    CHECK(minor > 0.0 && major >= minor, "no zoom: still sane steps");

    CHECK(TimelineTimecode(0.0, 25.0) == "00:00:00:00", "zero");
    CHECK(TimelineTimecode(11.2, 25.0) == "00:00:11:05", "11.2 s at 25 fps is frame 5");
    CHECK(TimelineTimecode(3725.96, 25.0) == "01:02:05:24", "an hour, two minutes, five seconds, frame 24");
    CHECK(TimelineTimecode(-0.48, 25.0) == "-00:00:00:12", "negative");
    CHECK(TimelineTimecode(11.2, 25.0, true) == "00:11", "short form");
    CHECK(TimelineTimecode(3725.0, 25.0, true) == "1:02:05", "short form past an hour");
    CHECK(TimelineTimecode(1.0, 29.97, true) == "00:01", "a fractional rate rounds its frames");

    std::vector<double> candidates = {4.0, 7.0, 12.0};
    CHECK(TimelineSnap(6.9, candidates, 0.2).value_or(-1.0) == 7.0, "snaps to the nearest within tolerance");
    CHECK(!TimelineSnap(5.5, candidates, 0.2), "nothing within tolerance");
    CHECK(TimelineSnap(5.4, candidates, 2.0).value_or(-1.0) == 4.0, "the nearer of two");
    CHECK(Near(TimelineToFrame(1.013, 25.0), 1.0) && Near(TimelineToFrame(1.03, 25.0), 1.04), "rounded to the frame grid");
}

int main() {
    std::printf("UltraCanvasVideoTimeline model\n");
    TestDerivedStarts();
    TestSplit();
    TestTrim();
    TestMove();
    TestDeleteAndDuplicate();
    TestRulerTimecodeSnap();
    if (failures) {
        std::printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("All checks passed\n");
    return 0;
}
