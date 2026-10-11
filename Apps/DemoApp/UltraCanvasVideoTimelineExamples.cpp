// Apps/DemoApp/UltraCanvasVideoTimelineExamples.cpp
// UltraCanvasVideoTimeline in the DemoApp: a timeline of generated clips on a
// title lane, a free video track, the magnetic main track and a music lane,
// with transitions, markers and beats. The demo plays the application's
// part: it installs providers that paint thumbnails and peaks on the spot,
// and it answers every reported change by applying it to its own model and
// feeding the model back - the loop a real editor runs against its engine.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraCanvasDemo.h"
#include "UltraCanvasVideoTimeline.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace UltraCanvas {

namespace {

// The demo's "document": the model, and the changes applied to it
struct TimelineDemoState {
    TimelineModel model;
    int changes = 0;
};

TimelineModel BuildDemoModel() {
    TimelineModel m;
    m.frameRate = 25.0;

    TimelineTrack titles;
    titles.id = "T1";
    titles.kind = TimelineTrackKind::Title;
    titles.name = "Titles";
    titles.magnetic = false;
    {
        TimelineClip t;
        t.id = "title-open"; t.kind = TimelineClipKind::Title; t.name = "Opening title";
        t.start = 0.5; t.duration = 4.0;
        titles.clips.push_back(t);
        TimelineClip lower;
        lower.id = "title-lower"; lower.kind = TimelineClipKind::Title; lower.name = "Lower third - Anna";
        lower.start = 18.0; lower.duration = 5.0;
        titles.clips.push_back(lower);
    }

    TimelineTrack overlay;
    overlay.id = "V2";
    overlay.kind = TimelineTrackKind::Video;
    overlay.name = "Overlay video";
    overlay.magnetic = false;
    {
        TimelineClip drone;
        drone.id = "drone"; drone.kind = TimelineClipKind::Video; drone.name = "Drone_pass.mp4";
        drone.source = "demo:drone"; drone.start = 14.0; drone.duration = 4.6; drone.sourceIn = 3.0; drone.sourceLength = 31.0;
        overlay.clips.push_back(drone);
    }

    TimelineTrack main;
    main.id = "V1";
    main.kind = TimelineTrackKind::MainVideo;
    main.name = "Main track";
    {
        TimelineClip black;
        black.id = "black"; black.kind = TimelineClipKind::Card; black.name = "Black"; black.duration = 1.5; black.color = 0x101010;
        main.clips.push_back(black);
        TimelineClip beach;
        beach.id = "beach"; beach.kind = TimelineClipKind::Video; beach.name = "Beach_wide.mp4";
        beach.source = "demo:beach"; beach.duration = 6.5; beach.sourceIn = 2.0; beach.sourceLength = 12.0; beach.hasTitle = true;
        main.clips.push_back(beach);
        TimelineClip forest;
        forest.id = "forest"; forest.kind = TimelineClipKind::Video; forest.name = "Forest_walk.mp4";
        forest.source = "demo:forest"; forest.duration = 5.5; forest.speed = 1.5; forest.sourceLength = 24.0; forest.hasEffects = true;
        main.clips.push_back(forest);
        TimelineClip photo;
        photo.id = "photo"; photo.kind = TimelineClipKind::Image; photo.name = "IMG_2041.jpg";
        photo.source = "demo:photo"; photo.duration = 4.0; photo.hasMotion = true;
        main.clips.push_back(photo);
        TimelineClip interview;
        interview.id = "interview"; interview.kind = TimelineClipKind::Video; interview.name = "Interview_A.mov";
        interview.source = "demo:interview"; interview.duration = 7.0; interview.sourceIn = 40.0; interview.sourceLength = 221.0;
        interview.hasEffects = true;
        main.clips.push_back(interview);
        TimelineClip sunset;
        sunset.id = "sunset"; sunset.kind = TimelineClipKind::Video; sunset.name = "Sunset.mp4";
        sunset.source = "demo:sunset"; sunset.duration = 5.5; sunset.sourceLength = 9.0; sunset.muted = true;
        main.clips.push_back(sunset);
        main.transitions.push_back({"forest", "crossfade", 1.0, 5.0});
        main.transitions.push_back({"interview", "wipeleft", 1.6, 4.0});
        main.transitions.push_back({"sunset", "crossfade", 0.5, 5.0});
    }

    TimelineTrack music;
    music.id = "A1";
    music.kind = TimelineTrackKind::MainAudio;
    music.name = "Music";
    {
        TimelineClip song;
        song.id = "song"; song.kind = TimelineClipKind::Audio; song.name = "Summer Drive - Night Lines (playlist, 2 songs)";
        song.source = "demo:song"; song.duration = 29.0; song.sourceLength = 212.0; song.fadeIn = 2.0; song.fadeOut = 3.0;
        music.clips.push_back(song);
    }

    m.tracks = {titles, overlay, main, music};
    for (TimelineTrack& t : m.tracks) TimelineDeriveStarts(t);
    m.markers.push_back({"m1", 8.0, 0xA65A00, "Chorus"});
    m.markers.push_back({"m2", 20.0, 0x0B6AA6, "Check colour"});
    for (double b = 0.25; b < 30.0; b += 0.5) m.beats.push_back(b);   // 120 BPM
    return m;
}

// A painted "frame": a sky, a horizon that moves with time, a sun. Enough
// to see the filmstrip follow the clip's source range.
UCPixmapPtr PaintFrame(const std::string& source, double seconds, int width, int height) {
    auto pix = std::make_shared<UCPixmap>();
    if (!pix->Init(width, height)) return nullptr;
    uint32_t sky = 0xFF7FB2D9, ground = 0xFF5A8A52;
    if (source == "demo:forest") { sky = 0xFFB7C9A4; ground = 0xFF3E6B3A; }
    else if (source == "demo:photo") { sky = 0xFFE8B47A; ground = 0xFF6C4A3A; }
    else if (source == "demo:interview") { sky = 0xFF9A8878; ground = 0xFF5B4A3E; }
    else if (source == "demo:sunset") { sky = 0xFF3A3A5A; ground = 0xFF2A2A3A; }
    else if (source == "demo:drone") { sky = 0xFFA8C8E0; ground = 0xFF6E8E9E; }
    const int horizon = static_cast<int>(height * (0.55 + 0.15 * std::sin(seconds * 0.7)));
    const int sunX = static_cast<int>(width * std::fmod(0.15 + seconds * 0.03, 1.0));
    const int sunY = static_cast<int>(height * 0.3);
    const int sunR = std::max(2, height / 6);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t c = y < horizon ? sky : ground;
            const int dx = x - sunX, dy = y - sunY;
            if (dx * dx + dy * dy <= sunR * sunR) c = 0xFFFFE08A;
            pix->SetPixel(x, y, c);
        }
    }
    pix->MarkDirty();
    return pix;
}

// A loudness envelope that looks like music: a slow swell with beats on it
WaveformChannelData PaintPeaks(double in, double out, double blocksPerSecond) {
    WaveformChannelData d;
    const double seconds = std::max(0.0, out - in);
    const int blocks = static_cast<int>(seconds * blocksPerSecond);
    d.samplesPerBlock = 882;
    d.totalBlocks = blocks;
    d.duration = seconds;
    for (int i = 0; i < blocks; ++i) {
        const double t = in + i / blocksPerSecond;
        const double swell = 0.35 + 0.25 * std::sin(t * 0.4) + 0.1 * std::sin(t * 2.3);
        const double beat = std::fmod(t, 0.5) < 0.06 ? 0.3 : 0.0;
        const float v = static_cast<float>(std::clamp(swell + beat, 0.05, 1.0));
        d.maxValues.push_back(v);
        d.minValues.push_back(-v * 0.9f);
        d.rmsValues.push_back(v * 0.6f);
    }
    return d;
}

std::string Describe(const TimelineChange& c) {
    switch (c.kind) {
        case TimelineChangeKind::Move: return "Move on " + c.trackId;
        case TimelineChangeKind::Trim: return "Trim on " + c.trackId + " to " + TimelineTimecode(c.at, 25.0);
        case TimelineChangeKind::Split: return "Split " + (c.before.empty() ? std::string() : c.before[0].name) + " at " + TimelineTimecode(c.at, 25.0);
        case TimelineChangeKind::Delete: return "Delete " + std::to_string(c.before.size()) + " clip(s) on " + c.trackId + (c.ripple ? ", ripple" : ", gap");
        case TimelineChangeKind::Duplicate: return "Duplicate on " + c.trackId;
        case TimelineChangeKind::TransitionSet: return "Transition " + (c.transitionAfter ? std::to_string(c.transitionAfter->seconds).substr(0, 4) + " s" : std::string());
        case TimelineChangeKind::TransitionRemove: return "Transition removed";
        case TimelineChangeKind::MarkerAdd: return "Marker at " + TimelineTimecode(c.at, 25.0);
        case TimelineChangeKind::MarkerRemove: return "Marker removed";
        case TimelineChangeKind::FadeSet: return "Fade set";
    }
    return "Change";
}

// The application's job: apply a reported change to the document. Here the
// document is the model itself, so each change replaces what it names.
bool ApplyChange(TimelineModel& model, const TimelineChange& c) {
    TimelineTrack* track = model.FindTrack(c.trackId);
    switch (c.kind) {
        case TimelineChangeKind::Move:
        case TimelineChangeKind::Trim:
        case TimelineChangeKind::Duplicate:
        case TimelineChangeKind::Split:
        case TimelineChangeKind::Delete: {
            if (!track) return false;
            if (track->IsMagnetic() && (c.kind != TimelineChangeKind::Split)) {
                if (c.kind == TimelineChangeKind::Delete || !c.after.empty()) track->clips = c.after;
            } else {
                // free track, or a split: replace the named clips in place
                std::vector<TimelineClip> clips = track->clips;
                for (const TimelineClip& b : c.before) {
                    clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const TimelineClip& x) { return x.id == b.id; }), clips.end());
                }
                for (const TimelineClip& a : c.after) clips.push_back(a);
                std::stable_sort(clips.begin(), clips.end(), [](const TimelineClip& p, const TimelineClip& q) { return p.start < q.start; });
                track->clips = clips;
                if (c.kind == TimelineChangeKind::Delete) {
                    for (const TimelineClip& b : c.before)
                        track->transitions.erase(std::remove_if(track->transitions.begin(), track->transitions.end(),
                                                                [&](const TimelineTransition& t) { return t.clipId == b.id; }),
                                                 track->transitions.end());
                }
            }
            TimelineDeriveStarts(*track);
            return true;
        }
        case TimelineChangeKind::TransitionSet: {
            if (!track || !c.transitionAfter) return false;
            TimelineTransition* t = track->FindTransition(c.transitionAfter->clipId);
            if (t) *t = *c.transitionAfter;
            else track->transitions.push_back(*c.transitionAfter);
            TimelineDeriveStarts(*track);
            return true;
        }
        case TimelineChangeKind::TransitionRemove: {
            if (!track || !c.transitionBefore) return false;
            track->transitions.erase(std::remove_if(track->transitions.begin(), track->transitions.end(),
                                                    [&](const TimelineTransition& t) { return t.clipId == c.transitionBefore->clipId; }),
                                     track->transitions.end());
            TimelineDeriveStarts(*track);
            return true;
        }
        case TimelineChangeKind::MarkerAdd:
            if (!c.markerAfter) return false;
            model.markers.push_back(*c.markerAfter);
            return true;
        case TimelineChangeKind::MarkerRemove:
            if (!c.markerBefore) return false;
            model.markers.erase(std::remove_if(model.markers.begin(), model.markers.end(),
                                               [&](const TimelineMarker& m) { return m.id == c.markerBefore->id; }),
                                model.markers.end());
            return true;
        case TimelineChangeKind::FadeSet:
            return false;
    }
    return false;
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVideoTimelineExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("VideoTimelineExamples", 0, 0, 1000, 620);

    auto title = CreateLabel("VideoTimelineTitle", 20, 12, 900, 28, "Video Timeline");
    title->SetFontSize(18);
    container->AddChild(title);
    auto subtitle = CreateLabel("VideoTimelineSubtitle", 20, 40, 960, 40,
                                "Drag clips to reorder the magnetic main track, drag their edges to trim, drag the overlay clip to any "
                                "time, drag a transition's edge for its length. B is the blade, S splits at the playhead, Del deletes, "
                                "M adds a marker, I and O set the work area, the wheel zooms and Shift+wheel scrolls.");
    container->AddChild(subtitle);

    auto state = std::make_shared<TimelineDemoState>();
    state->model = BuildDemoModel();

    auto status = CreateLabel("VideoTimelineStatus", 20, 88, 960, 22, "No change reported yet.");
    container->AddChild(status);

    auto timeline = CreateVideoTimeline("VideoTimeline", 20, 116, 960, 300);
    timeline->SetModel(state->model);
    timeline->SetShowBeats(true);
    timeline->thumbnailProvider = [](const std::string& source, double in, double out, int frames, int height,
                                     std::function<void(std::vector<UCPixmapPtr>)> done) {
        std::vector<UCPixmapPtr> strip;
        const int width = std::max(8, static_cast<int>(std::lround(height * 16.0 / 9.0)));
        for (int i = 0; i < frames; ++i) {
            const double t = frames > 1 ? in + (out - in) * i / (frames - 1) : in;
            strip.push_back(PaintFrame(source, t, width, height));
        }
        done(std::move(strip));
    };
    timeline->peaksProvider = [](const std::string&, double in, double out, double blocksPerSecond,
                                 std::function<void(WaveformChannelData)> done) {
        done(PaintPeaks(in, out, blocksPerSecond));
    };
    // The change loop: validate (everything is allowed here), apply, feed back
    timeline->onChangeRequested = [timeline = timeline.get(), state, status = status.get()](const TimelineChange& change) {
        if (!ApplyChange(state->model, change)) {
            status->SetText("Refused: " + Describe(change));
            return false;
        }
        ++state->changes;
        timeline->SetModel(state->model);
        status->SetText(std::to_string(state->changes) + ". " + Describe(change));
        return true;
    };
    timeline->onTrackToggled = [timeline = timeline.get(), state](const std::string& trackId, TimelineTrackToggle toggle, bool on) {
        TimelineTrack* track = state->model.FindTrack(trackId);
        if (!track) return;
        switch (toggle) {
            case TimelineTrackToggle::Magnetic: track->magnetic = on; break;
            case TimelineTrackToggle::Visible: track->visible = on; break;
            case TimelineTrackToggle::Muted: track->muted = on; break;
            case TimelineTrackToggle::Locked: track->locked = on; break;
        }
        timeline->SetModel(state->model);
    };
    timeline->onRangeChanged = [timeline = timeline.get(), state](double in, double out) {
        state->model.rangeIn = in;
        state->model.rangeOut = out;
        timeline->SetModel(state->model);
    };
    timeline->onSelectionChanged = [status = status.get()](const std::vector<std::string>& ids) {
        if (ids.empty()) return;
        std::string text = "Selected:";
        for (const std::string& id : ids) text += " " + id;
        status->SetText(text);
    };
    timeline->onClipActivated = [status = status.get()](const std::string& id) { status->SetText("Open the inspector for " + id); };
    container->AddChild(timeline);

    // controls under the timeline
    auto toolLabel = CreateLabel("VideoTimelineToolLabel", 20, 432, 60, 30, "Tool");
    container->AddChild(toolLabel);
    auto toolDrop = CreateDropdown("VideoTimelineTool", 70, 432, 140, 30);
    toolDrop->AddItem("Select  V");
    toolDrop->AddItem("Blade  B");
    toolDrop->AddItem("Trim  T");
    toolDrop->AddItem("Slip  Y");
    toolDrop->SetSelectedIndex(0);
    toolDrop->onSelectionChanged = [timeline = timeline.get()](int index, const DropdownItem&) {
        timeline->SetTool(index == 1 ? TimelineTool::Blade : index == 2 ? TimelineTool::Trim : index == 3 ? TimelineTool::Slip : TimelineTool::Select);
    };
    container->AddChild(toolDrop);
    timeline->onToolChanged = [toolDrop = toolDrop.get()](TimelineTool tool) {
        toolDrop->SetSelectedIndex(tool == TimelineTool::Blade ? 1 : tool == TimelineTool::Trim ? 2 : tool == TimelineTool::Slip ? 3 : 0);
    };

    auto snapBox = UltraCanvasCheckbox::CreateCheckbox("VideoTimelineSnap", 230, 432, 120, 30, "Snapping  N");
    snapBox->SetChecked(true);
    snapBox->onStateChanged = [timeline = timeline.get()](CheckedState, CheckedState state) { timeline->SetSnapping(state == CheckedState::Checked); };
    container->AddChild(snapBox);
    auto beatsBox = UltraCanvasCheckbox::CreateCheckbox("VideoTimelineBeats", 360, 432, 120, 30, "Beat marks");
    beatsBox->SetChecked(true);
    beatsBox->onStateChanged = [timeline = timeline.get()](CheckedState, CheckedState state) { timeline->SetShowBeats(state == CheckedState::Checked); };
    container->AddChild(beatsBox);
    auto darkBox = UltraCanvasCheckbox::CreateCheckbox("VideoTimelineDark", 490, 432, 130, 30, "Dark palette");
    darkBox->onStateChanged = [timeline = timeline.get()](CheckedState, CheckedState state) {
        timeline->SetStyle(state == CheckedState::Checked ? VideoTimelineStyle::DarkPalette() : VideoTimelineStyle::LightPalette());
    };
    container->AddChild(darkBox);

    auto fitButton = CreateButton("VideoTimelineFit", 640, 432, 100, 30, "Fit  Shift+Z");
    fitButton->onClick = [timeline = timeline.get()]() { timeline->ZoomToFit(); };
    container->AddChild(fitButton);
    auto resetButton = CreateButton("VideoTimelineReset", 750, 432, 100, 30, "Reset");
    resetButton->onClick = [timeline = timeline.get(), state, status = status.get()]() {
        state->model = BuildDemoModel();
        state->changes = 0;
        timeline->SetModel(state->model);
        status->SetText("Reset.");
    };
    container->AddChild(resetButton);

    // the playhead as a timecode field (type a time, Enter) and as the transport's readout
    auto playheadLabel = CreateLabel("VideoTimelinePlayheadLabel", 20, 470, 70, 28, "Playhead");
    container->AddChild(playheadLabel);
    auto playheadField = CreateTimecodeField("VideoTimelinePlayhead", 90, 470, 120, 28);
    playheadField->SetFrameRate(25.0);
    playheadField->SetMaximumFrames(static_cast<long long>(state->model.Length() * 25.0));
    playheadField->onCommit = [timeline = timeline.get()](long long frames) { timeline->SetPlayhead(frames / 25.0); };
    container->AddChild(playheadField);
    auto readout = CreateTimecodeField("VideoTimelineReadout", 230, 470, 170, 28);
    readout->SetFrameRate(25.0);
    readout->SetReadout(true);
    container->AddChild(readout);
    auto hint = CreateLabel("VideoTimelinePlayheadHint", 410, 470, 560, 28,
                            "Click a segment and press the arrow keys, or type digits and Enter; +12 is twelve frames later.");
    container->AddChild(hint);
    timeline->onPlayheadChanged = [playheadField = playheadField.get(), readout = readout.get()](double seconds) {
        playheadField->SetSeconds(seconds);
        readout->SetSeconds(seconds);
    };

    auto notes = CreateLabel("VideoTimelineNotes", 20, 498, 960, 60,
                             "The element never applies an edit: it reports it with the clips before and after, and this demo, "
                             "standing in for the application, applies it to its model and calls SetModel. Thumbnails and the "
                             "waveform are painted by the demo's providers; a real app decodes them with VideoFX.");
    container->AddChild(notes);
    return container;
}

} // namespace UltraCanvas
