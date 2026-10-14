// Tests/VideoTimelineElementTest.cpp
// UltraCanvasVideoTimeline in a real window: geometry, hit testing, and the
// edits that real mouse and key events turn into reported changes - a trim
// with ripple, a reorder on the magnetic track, a free-track move, a split
// with the blade, a marker, a delete, a drop with its insertion point, the
// ruler and the wheel. The test plays the application: it applies each
// change to its model and feeds it back. With ULTRACANVAS_SCREENSHOT_DIR set
// it also writes the window as video-timeline.ppm. Needs a display; skips
// itself without one (runs under Xvfb).
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasVideoTimeline.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

bool Near(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }

UCEvent MouseEvent(UCEventType type, float x, float y) {
    UCEvent event;
    event.type = type;
    event.button = UCMouseButton::Left;
    // the element sits at (0, 0), so window and element-local coordinates agree
    event.pointer = Point2Di(static_cast<int>(x), static_cast<int>(y));
    event.pointerWindow = event.pointer;
    event.pointerGlobal = event.pointer;
    return event;
}

UCEvent KeyEvent(UCKeys key, bool ctrl = false, bool shift = false) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.ctrl = ctrl;
    event.shift = shift;
    return event;
}

TimelineClip Clip(const std::string& id, TimelineClipKind kind, double duration, double sourceLength) {
    TimelineClip c;
    c.id = id;
    c.name = id;
    c.kind = kind;
    c.source = "test:" + id;
    c.duration = duration;
    c.sourceLength = sourceLength;
    return c;
}

TimelineModel BuildModel() {
    TimelineModel m;
    m.frameRate = 25.0;
    TimelineTrack overlay;
    overlay.id = "V2";
    overlay.kind = TimelineTrackKind::Video;
    overlay.name = "Overlay";
    overlay.magnetic = false;
    overlay.clips.push_back(Clip("o", TimelineClipKind::Video, 2.0, 20.0));
    overlay.clips[0].start = 1.0;
    TimelineTrack main;
    main.id = "V1";
    main.kind = TimelineTrackKind::MainVideo;
    main.name = "Main track";
    main.clips = {Clip("a", TimelineClipKind::Video, 3.0, 10.0), Clip("b", TimelineClipKind::Image, 2.0, 0.0),
                  Clip("c", TimelineClipKind::Video, 3.0, 10.0)};
    main.clips[1].sourceLength = 0.0;   // an image: unbounded
    main.transitions.push_back({"c", "crossfade", 0.5, 2.0});
    TimelineTrack music;
    music.id = "A1";
    music.kind = TimelineTrackKind::MainAudio;
    music.name = "Music";
    music.clips.push_back(Clip("song", TimelineClipKind::Audio, 9.0, 200.0));
    music.clips[0].fadeIn = 1.0;
    m.tracks = {overlay, main, music};
    for (TimelineTrack& t : m.tracks) TimelineDeriveStarts(t);
    m.markers.push_back({"m1", 2.0, 0xA65A00, "Chorus"});
    for (double b = 0.25; b < 9.0; b += 0.5) m.beats.push_back(b);
    return m;
}

UCPixmapPtr PaintFrame(double seconds, int width, int height) {
    auto pix = std::make_shared<UCPixmap>();
    if (!pix->Init(width, height)) return nullptr;
    const int horizon = static_cast<int>(height * (0.5 + 0.2 * std::sin(seconds)));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) pix->SetPixel(x, y, y < horizon ? 0xFF7FB2D9 : 0xFF5A8A52);
    pix->MarkDirty();
    return pix;
}

bool WritePpm(const std::shared_ptr<UltraCanvasWindow>& window, int width, int height, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << " " << height << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
            row[static_cast<size_t>(x) * 3 + 0] = px.r;
            row[static_cast<size_t>(x) * 3 + 1] = px.g;
            row[static_cast<size_t>(x) * 3 + 2] = px.b;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(out);
}

// What the application does with a change: replace the named clips
bool Apply(TimelineModel& model, const TimelineChange& c) {
    TimelineTrack* track = model.FindTrack(c.trackId);
    switch (c.kind) {
        case TimelineChangeKind::Move:
        case TimelineChangeKind::Trim:
        case TimelineChangeKind::Delete:
        case TimelineChangeKind::Duplicate:
        case TimelineChangeKind::Split: {
            if (!track) return false;
            if (track->IsMagnetic() && c.kind != TimelineChangeKind::Split) {
                track->clips = c.after;
            } else {
                for (const TimelineClip& b : c.before)
                    track->clips.erase(std::remove_if(track->clips.begin(), track->clips.end(),
                                                      [&](const TimelineClip& x) { return x.id == b.id; }),
                                       track->clips.end());
                for (const TimelineClip& a : c.after) track->clips.push_back(a);
                std::stable_sort(track->clips.begin(), track->clips.end(),
                                 [](const TimelineClip& p, const TimelineClip& q) { return p.start < q.start; });
            }
            TimelineDeriveStarts(*track);
            return true;
        }
        case TimelineChangeKind::TransitionSet:
            if (!track || !c.transitionAfter) return false;
            if (TimelineTransition* t = track->FindTransition(c.transitionAfter->clipId)) *t = *c.transitionAfter;
            else track->transitions.push_back(*c.transitionAfter);
            TimelineDeriveStarts(*track);
            return true;
        case TimelineChangeKind::TransitionRemove:
            if (!track || !c.transitionBefore) return false;
            track->transitions.erase(std::remove_if(track->transitions.begin(), track->transitions.end(),
                                                    [&](const TimelineTransition& t) { return t.clipId == c.transitionBefore->clipId; }),
                                     track->transitions.end());
            TimelineDeriveStarts(*track);
            return true;
        case TimelineChangeKind::MarkerAdd:
            if (!c.markerAfter) return false;
            model.markers.push_back(*c.markerAfter);
            return true;
        case TimelineChangeKind::FadeSet: {
            if (!track || c.after.size() != 1) return false;
            TimelineClip* clip = track->FindClip(c.after[0].id);
            if (!clip) return false;
            clip->fadeIn = c.after[0].fadeIn;
            clip->fadeOut = c.after[0].fadeOut;
            return true;
        }
        default:
            return false;
    }
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   VideoTimeline Element Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");
    UltraCanvasApplication app;
    if (!app.Initialize("VideoTimelineElementTest")) SKIP_ALL("application would not initialise");
    WindowConfig cfg;
    cfg.title = "VideoTimelineElementTest";
    cfg.width = 1000;
    cfg.height = 400;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto timeline = std::make_shared<UltraCanvasVideoTimeline>("Timeline", 0, 0, 1000, 360);
    window->AddChild(timeline);
    VideoTimelineStyle style = VideoTimelineStyle::LightPalette();
    timeline->SetStyle(style);
    TimelineModel model = BuildModel();
    timeline->SetModel(model);
    timeline->SetPixelsPerSecond(80.0);
    timeline->SetSnapping(false);
    timeline->SetShowBeats(true);
    timeline->thumbnailProvider = [](const std::string&, double in, double out, int frames, int height,
                                     std::function<void(std::vector<UCPixmapPtr>)> done) {
        std::vector<UCPixmapPtr> strip;
        for (int i = 0; i < frames; ++i) strip.push_back(PaintFrame(in + (out - in) * i / std::max(1, frames - 1), height * 16 / 9, height));
        done(std::move(strip));
    };
    timeline->peaksProvider = [](const std::string&, double in, double out, double bps, std::function<void(WaveformChannelData)> done) {
        WaveformChannelData d;
        d.duration = out - in;
        d.totalBlocks = static_cast<int>(d.duration * bps);
        for (int i = 0; i < d.totalBlocks; ++i) {
            const float v = 0.3f + 0.3f * static_cast<float>(std::sin(i / 7.0));
            d.maxValues.push_back(v);
            d.minValues.push_back(-v);
            d.rmsValues.push_back(v * 0.6f);
        }
        done(d);
    };
    std::vector<TimelineChange> changes;
    timeline->onChangeRequested = [&](const TimelineChange& change) {
        changes.push_back(change);
        if (!Apply(model, change)) return false;
        timeline->SetModel(model);
        return true;
    };
    std::vector<std::string> lastSelection;
    timeline->onSelectionChanged = [&](const std::vector<std::string>& ids) { lastSelection = ids; };
    double playheadSeen = -1.0;
    timeline->onPlayheadChanged = [&](double seconds) { playheadSeen = seconds; };
    std::string droppedTrack;
    double droppedAt = -1.0;
    std::vector<std::string> droppedFiles;
    timeline->onFilesDropped = [&](const std::vector<std::string>& files, const std::string& trackId, double seconds) {
        droppedFiles = files;
        droppedTrack = trackId;
        droppedAt = seconds;
    };
    DisplayTest::Frame(window, {timeline});

    // lanes: ruler 28, then V2 (64), V1 (64), A1 (48) with 1 px between
    const float headerW = style.headerWidth;
    const float overlayY = style.rulerHeight + 32.0f;
    const float mainY = style.rulerHeight + 64.0f + 1.0f + 40.0f;    // below the band, in the filmstrip
    const float musicY = style.rulerHeight + 65.0f + 65.0f + 24.0f;

    std::cerr << "\n--- Geometry ---" << std::endl;
    TEST("second 0 sits at the header's right edge", Near(timeline->SecondsToX(0.0), headerW));
    TEST("x and seconds round-trip", Near(timeline->XToSeconds(timeline->SecondsToX(5.0)), 5.0, 1e-6));
    std::string trackId;
    TEST("the clip under the pointer is b", timeline->ClipAt(headerW + 80.0f * 4.0f, mainY, &trackId) == "b" && trackId == "V1");
    TEST("the music bar is found on A1", timeline->ClipAt(headerW + 100.0f, musicY, &trackId) == "song" && trackId == "A1");
    TEST("empty lane space is no clip", timeline->ClipAt(headerW + 80.0f * 6.0f, overlayY, &trackId).empty() && trackId == "V2");
    timeline->SetDropTargetClip("b");
    DisplayTest::Frame(window, {timeline});
    TEST("a drop-target clip is kept until cleared", timeline->GetDropTargetClip() == "b");
    timeline->SetDropTargetClip("");
    TEST("and cleared with an empty id", timeline->GetDropTargetClip().empty());

    std::cerr << "\n--- Selection ---" << std::endl;
    timeline->OnEvent(MouseEvent(UCEventType::MouseDown, headerW + 80.0f * 4.0f, mainY));
    timeline->OnEvent(MouseEvent(UCEventType::MouseUp, headerW + 80.0f * 4.0f, mainY));
    TEST("a click selects the clip", timeline->GetSelection() == std::vector<std::string>{"b"});
    TEST("and reports the selection", lastSelection == std::vector<std::string>{"b"});
    timeline->OnEvent(MouseEvent(UCEventType::MouseDown, headerW + 80.0f * 6.5f, overlayY));
    timeline->OnEvent(MouseEvent(UCEventType::MouseUp, headerW + 80.0f * 6.5f, overlayY));
    TEST("a click on empty space clears it", timeline->GetSelection().empty());

    std::cerr << "\n--- Trim with ripple ---" << std::endl;
    {
        const float edgeX = timeline->SecondsToX(5.0) - 3.0f;         // b's right edge, b spans 3..5
        timeline->OnEvent(MouseEvent(UCEventType::MouseDown, edgeX, mainY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, edgeX + 40.0f, mainY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, edgeX + 80.0f, mainY));
        DisplayTest::Frame(window, {timeline});
        timeline->OnEvent(MouseEvent(UCEventType::MouseUp, edgeX + 80.0f, mainY));
        TEST("one change was reported", changes.size() == 1);
        const bool trim = !changes.empty() && changes.back().kind == TimelineChangeKind::Trim;
        TEST("it is a trim on V1 with ripple", trim && changes.back().trackId == "V1" && changes.back().ripple);
        TEST("the whole sequence is in before and after", trim && changes.back().before.size() == 3 && changes.back().after.size() == 3);
        const TimelineClip* b = model.FindTrack("V1")->FindClip("b");
        const TimelineClip* c = model.FindTrack("V1")->FindClip("c");
        TEST("b grew by a second", b && Near(b->duration, 3.0, 0.05));
        TEST("c rippled a second later, less its transition", c && Near(c->start, 6.0 - 0.5, 0.05));
    }

    std::cerr << "\n--- Fade knobs on the music bar ---" << std::endl;
    {
        // the fade-in knob sits at the top of the ramp, one second (80 px) into the song
        const float knobY = style.rulerHeight + 65.0f + 65.0f + 3.0f + style.clipBandHeight;
        const float knobX = headerW + 1.0f + 80.0f;
        const size_t seen = changes.size();
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, headerW + 100.0f, musicY));   // hover the bar: the knobs show
        DisplayTest::Frame(window, {timeline});
        timeline->OnEvent(MouseEvent(UCEventType::MouseDown, knobX, knobY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, knobX + 40.0f, knobY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, knobX + 80.0f, knobY));
        DisplayTest::Frame(window, {timeline});
        timeline->OnEvent(MouseEvent(UCEventType::MouseUp, knobX + 80.0f, knobY));
        const bool fade = changes.size() == seen + 1 && changes.back().kind == TimelineChangeKind::FadeSet;
        TEST("dragging the knob reports a fade", fade && changes.back().trackId == "A1" && changes.back().after.size() == 1);
        TEST("the fade in grew to two seconds", fade && Near(changes.back().after[0].fadeIn, 2.0, 0.05) && changes.back().after[0].id == "song");
        const TimelineClip* song = model.FindTrack("A1")->FindClip("song");
        TEST("and the model shows it", song && Near(song->fadeIn, 2.0, 0.05));
        // the fade-out knob sits at the bar's right end (the song is 9 s long): a second back
        const float endX = timeline->SecondsToX(song->End()) - 1.0f;
        timeline->OnEvent(MouseEvent(UCEventType::MouseDown, endX, knobY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, endX - 40.0f, knobY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, endX - 80.0f, knobY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseUp, endX - 80.0f, knobY));
        const bool out = changes.size() == seen + 2 && changes.back().kind == TimelineChangeKind::FadeSet;
        TEST("the other knob sets the fade out", out && Near(changes.back().after[0].fadeOut, 1.0, 0.05));
        TEST("the music bar is still found under the knob row", timeline->ClipAt(knobX, knobY, &trackId) == "song");
        changes.resize(seen);   // the checks below count changes from the start
    }

    std::cerr << "\n--- Reorder on the magnetic track ---" << std::endl;
    {
        const TimelineClip* c = model.FindTrack("V1")->FindClip("c");
        const float fromX = timeline->SecondsToX(c->start + c->duration / 2.0);
        timeline->OnEvent(MouseEvent(UCEventType::MouseDown, fromX, mainY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, fromX - 100.0f, mainY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, headerW + 20.0f, mainY));
        DisplayTest::Frame(window, {timeline});
        timeline->OnEvent(MouseEvent(UCEventType::MouseUp, headerW + 20.0f, mainY));
        const bool move = changes.size() == 2 && changes.back().kind == TimelineChangeKind::Move;
        TEST("a move was reported", move);
        const TimelineTrack* v1 = model.FindTrack("V1");
        TEST("c is first now, then a and b", v1->clips.size() == 3 && v1->clips[0].id == "c" && v1->clips[1].id == "a" && v1->clips[2].id == "b");
        TEST("starts were derived", Near(v1->clips[0].start, 0.0) && Near(v1->clips[1].start, 3.0, 0.05));
        TEST("the transition into c no longer overlaps anything (c is first)", Near(v1->clips[1].start, v1->clips[0].End(), 0.05));
    }

    std::cerr << "\n--- Free-track move ---" << std::endl;
    {
        const float fromX = timeline->SecondsToX(2.0);   // o spans 1..3
        timeline->OnEvent(MouseEvent(UCEventType::MouseDown, fromX, overlayY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, fromX + 200.0f, overlayY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseMove, fromX + 400.0f, overlayY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseUp, fromX + 400.0f, overlayY));
        const bool move = changes.size() == 3 && changes.back().kind == TimelineChangeKind::Move && changes.back().trackId == "V2";
        TEST("a free-track move was reported", move);
        const TimelineClip* o = model.FindTrack("V2")->FindClip("o");
        TEST("o moved five seconds later", o && Near(o->start, 6.0, 0.05));
    }

    std::cerr << "\n--- Playhead, blade, keys ---" << std::endl;
    timeline->OnEvent(MouseEvent(UCEventType::MouseDown, timeline->SecondsToX(4.5), 20.0f));
    timeline->OnEvent(MouseEvent(UCEventType::MouseUp, timeline->SecondsToX(4.5), 20.0f));
    TEST("a click on the ruler moves the playhead", Near(timeline->GetPlayhead(), 4.5, 0.05) && Near(playheadSeen, 4.5, 0.05));
    timeline->SetTool(TimelineTool::Blade);
    {
        const TimelineClip* a = model.FindTrack("V1")->FindClip("a");   // a spans 3..6 now
        const float cutX = timeline->SecondsToX(a->start + 1.0);
        timeline->OnEvent(MouseEvent(UCEventType::MouseDown, cutX, mainY));
        timeline->OnEvent(MouseEvent(UCEventType::MouseUp, cutX, mainY));
        const bool split = changes.size() == 4 && changes.back().kind == TimelineChangeKind::Split;
        TEST("the blade reports a split", split && Near(changes.back().at, 4.0, 0.05));
        const TimelineTrack* v1 = model.FindTrack("V1");
        TEST("the track has four clips with a:2 after a", v1->clips.size() == 4 && v1->clips[1].id == "a" && v1->clips[2].id == "a:2");
        TEST("the parts share the source", v1->FindClip("a:2") && Near(v1->FindClip("a:2")->sourceIn, 1.0, 0.05));
    }
    timeline->SetTool(TimelineTool::Select);
    const bool focused = DisplayTest::FocusElement(app, window, timeline);
    if (focused) {
        timeline->OnEvent(KeyEvent(UCKeys::M));
        TEST("M adds a marker at the playhead", changes.size() == 5 && changes.back().kind == TimelineChangeKind::MarkerAdd &&
                                                    Near(changes.back().at, 4.5, 0.05) && model.markers.size() == 2);
        timeline->SetSelection({"b"});
        timeline->OnEvent(KeyEvent(UCKeys::Delete));
        TEST("Delete removes the selection with ripple", changes.size() == 6 && changes.back().kind == TimelineChangeKind::Delete &&
                                                             changes.back().ripple && !model.FindTrack("V1")->FindClip("b"));
        TEST("the selection is gone", timeline->GetSelection().empty());
        timeline->OnEvent(KeyEvent(UCKeys::Right, false, true));
        TEST("Shift+Right steps a second", Near(timeline->GetPlayhead(), 5.5, 0.05));
        timeline->OnEvent(KeyEvent(UCKeys::B));
        TEST("B picks the blade", timeline->GetTool() == TimelineTool::Blade);
        timeline->OnEvent(KeyEvent(UCKeys::V));
    } else {
        std::cerr << "SKIP: keyboard (the window could not be focused)" << std::endl;
    }

    std::cerr << "\n--- Wheel and drop ---" << std::endl;
    {
        const double before = timeline->GetPixelsPerSecond();
        UCEvent wheel = MouseEvent(UCEventType::MouseWheel, headerW + 300.0f, mainY);
        wheel.wheelDelta = 1;
        timeline->OnEvent(wheel);
        TEST("the wheel zooms in", timeline->GetPixelsPerSecond() > before);
        wheel.wheelDelta = -1;
        timeline->OnEvent(wheel);
        TEST("and out again", Near(timeline->GetPixelsPerSecond(), before, 0.01));
        timeline->SetPixelsPerSecond(80.0);
        timeline->SetScrollSeconds(0.0);
        UCEvent over = MouseEvent(UCEventType::DragOver, timeline->SecondsToX(2.8), mainY);
        timeline->OnEvent(over);
        DisplayTest::Frame(window, {timeline});
        UCEvent drop = MouseEvent(UCEventType::Drop, timeline->SecondsToX(2.8), mainY);
        drop.droppedFiles = {"/media/new.mp4", "/media/other.mp4"};
        timeline->OnEvent(drop);
        TEST("a drop reports its files on V1", droppedFiles.size() == 2 && droppedTrack == "V1");
        TEST("at the nearest cut", Near(droppedAt, 3.0, 1e-6));
    }

    timeline->SetSelection({"c"});
    timeline->SetSelectedTransition("");
    timeline->SetPlayhead(4.5);
    DisplayTest::Frame(window, {timeline});
    if (const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        const std::string path = std::string(dir) + "/video-timeline.ppm";
        std::cerr << (WritePpm(window, cfg.width, cfg.height, path) ? "wrote " : "could not write ") << path << std::endl;
    }
    Color px;
    TEST("the window has pixels", window->GetPixelColor(headerW + 10, mainY, px));

    std::cerr << "\n" << testCount - failCount << " of " << testCount << " checks passed" << std::endl;
    return failCount ? 1 : 0;
}
