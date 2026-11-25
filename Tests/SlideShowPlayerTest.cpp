// Tests/SlideShowPlayerTest.cpp
// The slide show player and the presenter view, driven by events and a
// clock the test sets (no window, no real time):
//  - hidden slides are skipped, a black end screen follows the last slide
//    and on from it ends the show; Escape ends it (onShowEnd);
//  - going on plays the slide's transition: mid-way the slide coming on is
//    the current one, a key finishes it, and a mid-fade frame mixes both
//    slides' colours; going back shows the slide without one;
//  - a slide's timing moves on by itself, and a slide that moves on only by
//    its timing ignores a click;
//  - a number typed and Enter go to that slide; B blacks the screen, W
//    whitens it, a key brings the slide back;
//  - the presenter view follows the player: the slide, "Slide N of M", the
//    notes, the time with pause and reset;
//  - the shared transition frames (UltraCanvasSlideTransition).
//
// Usage: SlideShowPlayerTest
// Exit code is the number of failed checks.

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlideShowPlayer.h"
#include "UltraCanvasSlideTransition.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

UCEvent Key(UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    return e;
}

UCEvent Click(UCMouseButton button = UCMouseButton::Left) {
    UCEvent e;
    e.type = UCEventType::MouseDown;
    e.pointer = Point2Di(100, 100);
    e.pointerWindow = e.pointer;
    e.button = button;
    return e;
}

PptxSlide Plain(const std::string& background) {
    PptxSlide slide;
    slide.background = background;
    return slide;
}

struct Pixel { int r, g, b; };

// One pixel of a raster page.
Pixel PixelAt(RasterPageTarget& page, int x, int y) {
    Pixel found{-1, -1, -1};
    int row = 0;
    page.ForEachRow([&](const uint8_t* rgba, int width) {
        if (row++ == y && x < width) {
            found = {rgba[x * 4], rgba[x * 4 + 1], rgba[x * 4 + 2]};
            return false;
        }
        return true;
    });
    return found;
}

std::string Show(const Pixel& p) {
    return std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b);
}

} // namespace

int main() {
    std::printf("=== Slide show player ===\n");
    auto deck = std::make_shared<PptxPresentation>();
    deck->slideWidth = 12192000;
    deck->slideHeight = 6858000;
    deck->slides.push_back(Plain("FF0000"));                 // 1: red
    deck->slides.push_back(Plain("00FF00"));                 // 2: green, hidden
    deck->slides.back().hidden = true;
    deck->slides.push_back(Plain("0000FF"));                 // 3: blue, fades in over a second
    deck->slides.back().transition.kind = PptxTransitionKind::Fade;
    deck->slides.back().transition.durationSec = 1.0;
    deck->slides.push_back(Plain("FFFF00"));                 // 4: yellow, pushed in, moves on after 2 s, not by a click
    deck->slides.back().transition.kind = PptxTransitionKind::Push;
    deck->slides.back().transition.durationSec = 0.5;
    deck->slides.back().transition.advanceAfterSec = 2.0;
    deck->slides.back().transition.advanceOnClick = false;
    deck->slides.push_back(Plain("FFFFFF"));                 // 5: white, with notes
    {
        PptxParagraph line;
        line.runs.push_back(PptxRun());
        line.runs.back().text = "Thank everyone";
        deck->slides.back().notes.push_back(line);
        line.runs.back().text = "Questions";
        deck->slides.back().notes.push_back(line);
    }

    double now = 100.0;
    auto clock = [&now]() { return now; };
    auto player = CreateSlideShowPlayer("show", 0, 0, 320, 180);
    player->SetClock(clock);
    player->SetDeck(deck);
    int ended = 0;
    std::vector<int> changes;
    player->onShowEnd = [&ended]() { ++ended; };
    player->onSlideChanged = [&changes](int slide) { changes.push_back(slide); };

    std::printf("\n-- starting, hidden slides, going back --\n");
    player->Start(0);
    Check(player->IsPlaying() && player->GetCurrentSlide() == 0 && !player->IsTransitioning(),
          "started on the first slide, without a transition");
    player->OnEvent(Key(UCKeys::Right));
    Check(player->GetCurrentSlide() == 2, "on: the hidden slide is skipped");
    Check(player->IsTransitioning() && player->GetTransitionFromSlide() == 0,
          "the slide fades in: mid-transition the slide coming on is the current one");
    now += 0.5;
    Check(std::fabs(player->GetTransitionProgress() - 0.5) < 1e-9, "half way after half its second");

    std::printf("\n-- a mid-fade frame --\n");
    RasterPageTarget page(320, 180, 96, 96);
    Check(page.IsValid(), "an off-screen page");
    page.BeginPage();
    player->Render(page.GetRenderContext(), Rect2Df(0, 0, 320, 180));
    const Pixel mixed = PixelAt(page, 160, 90);
    Check(mixed.r > 90 && mixed.r < 165 && mixed.b > 90 && mixed.b < 165 && mixed.g < 30,
          "red and blue mixed half and half [" + Show(mixed) + "]");

    player->OnEvent(Key(UCKeys::Space));
    Check(!player->IsTransitioning() && player->GetCurrentSlide() == 2, "a key finishes the transition, and only that");
    page.BeginPage();
    player->Render(page.GetRenderContext(), Rect2Df(0, 0, 320, 180));
    const Pixel blue = PixelAt(page, 160, 90);
    Check(blue.b > 240 && blue.r < 15, "the blue slide alone [" + Show(blue) + "]");

    player->OnEvent(Key(UCKeys::Left));
    Check(player->GetCurrentSlide() == 0 && !player->IsTransitioning(), "back: the slide before, no transition");
    player->OnEvent(Click());
    Check(player->GetCurrentSlide() == 2 && player->IsTransitioning(), "a click goes on, with the fade");
    player->OnEvent(Click());
    Check(player->GetCurrentSlide() == 2 && !player->IsTransitioning(), "a click during it finishes it");

    std::printf("\n-- timings --\n");
    player->Next();
    Check(player->GetCurrentSlide() == 3 && player->IsTransitioning(), "pushed in");
    now += 0.5;
    player->Tick();
    Check(!player->IsTransitioning(), "the push is over after its half second");
    player->OnEvent(Click());
    Check(player->GetCurrentSlide() == 3, "a click does not move on from a slide that moves on by its timing");
    now += 1.9;
    player->Tick();
    Check(player->GetCurrentSlide() == 3, "not yet after 1.9 s");
    now += 0.2;
    player->Tick();
    Check(player->GetCurrentSlide() == 4, "on by itself 2 s after it was fully shown");
    player->OnEvent(Key(UCKeys::Left));
    Check(player->GetCurrentSlide() == 3, "back to it, a key works");
    player->OnEvent(Key(UCKeys::B));
    now += 5;
    player->Tick();
    Check(player->GetCurrentSlide() == 3, "a black screen holds the timing");
    player->OnEvent(Key(UCKeys::B));
    player->OnEvent(Key(UCKeys::PageDown));
    Check(player->GetCurrentSlide() == 4, "and a key moves on from it");

    std::printf("\n-- the end screen --\n");
    player->OnEvent(Key(UCKeys::Down));
    Check(player->IsPlaying() && player->GetCurrentSlide() == -1 && changes.back() == -1,
          "after the last slide, the end screen");
    player->OnEvent(Key(UCKeys::Backspace));
    Check(player->GetCurrentSlide() == 4, "back from it: the last slide");
    player->OnEvent(Key(UCKeys::N));
    player->OnEvent(Click());
    Check(!player->IsPlaying() && ended == 1, "a click on the end screen ends the show (onShowEnd)");

    std::printf("\n-- a slide by number, Home, End --\n");
    player->Start(1);
    Check(player->GetCurrentSlide() == 2, "started from a hidden slide: the next one shown");
    player->OnEvent(Key(UCKeys::Key5));
    Check(player->GetTypedNumber() == "5", "typed 5");
    player->OnEvent(Key(UCKeys::Return));
    Check(player->GetCurrentSlide() == 4 && player->GetTypedNumber().empty(), "5 Enter: slide 5");
    player->OnEvent(Key(UCKeys::Key3));
    player->OnEvent(Key(UCKeys::Enter));
    Check(player->GetCurrentSlide() == 2 && !player->IsTransitioning(), "3 Enter: slide 3, at once");
    player->OnEvent(Key(UCKeys::Key2));
    player->OnEvent(Key(UCKeys::Return));
    Check(player->GetCurrentSlide() == 1, "2 Enter: the hidden slide 2 too");
    player->OnEvent(Key(UCKeys::Key9));
    player->OnEvent(Key(UCKeys::Return));
    Check(player->GetCurrentSlide() == 1, "9 Enter: no such slide, nothing happens");
    player->OnEvent(Key(UCKeys::End));
    Check(player->GetCurrentSlide() == 4, "End: the last slide");
    player->OnEvent(Key(UCKeys::Home));
    Check(player->GetCurrentSlide() == 0, "Home: the first");

    std::printf("\n-- black and white --\n");
    player->OnEvent(Key(UCKeys::B));
    Check(player->GetBlank() == SlideShowBlank::BlackScreen, "B: a black screen");
    page.BeginPage();
    player->Render(page.GetRenderContext(), Rect2Df(0, 0, 320, 180));
    const Pixel black = PixelAt(page, 160, 90);
    Check(black.r < 10 && black.g < 10 && black.b < 10, "drawn black, not the red slide [" + Show(black) + "]");
    player->OnEvent(Key(UCKeys::B));
    Check(player->GetBlank() == SlideShowBlank::NotBlank && player->GetCurrentSlide() == 0, "B again: the slide");
    player->OnEvent(Key(UCKeys::W));
    page.BeginPage();
    player->Render(page.GetRenderContext(), Rect2Df(0, 0, 320, 180));
    const Pixel white = PixelAt(page, 160, 90);
    Check(player->GetBlank() == SlideShowBlank::WhiteScreen && white.g > 245, "W: a white screen [" + Show(white) + "]");
    player->OnEvent(Key(UCKeys::Space));
    Check(player->GetBlank() == SlideShowBlank::NotBlank && player->GetCurrentSlide() == 0,
          "any key brings the slide back without moving on");

    std::printf("\n-- Escape --\n");
    player->OnEvent(Key(UCKeys::Escape));
    Check(!player->IsPlaying() && ended == 2 && player->GetCurrentSlide() == -1, "Escape ends the show (onShowEnd)");

    std::printf("\n-- the presenter view --\n");
    auto presenter = CreatePresenterView("presenter", 0, 0, 1280, 720);
    presenter->SetClock(clock);
    presenter->SetPlayer(player);
    Check(presenter->GetShownSlide() == -1 && presenter->GetElapsedText() == "0:00:00", "nothing before the show");
    player->Start(0);
    Check(presenter->GetShownSlide() == 0 && presenter->GetPositionText() == "Slide 1 of 5",
          "follows the start [" + presenter->GetPositionText() + "]");
    player->Next();
    Check(presenter->GetShownSlide() == 2 && presenter->GetPositionText() == "Slide 3 of 5",
          "follows a slide on [" + presenter->GetPositionText() + "]");
    player->GoToSlide(4);
    Check(presenter->GetShownSlide() == 4 && presenter->GetNotesText() == "Thank everyone\nQuestions",
          "shows the slide's notes [" + presenter->GetNotesText() + "]");
    UCEvent left = Key(UCKeys::Left);
    presenter->OnEvent(left);
    Check(player->GetCurrentSlide() == 3 && presenter->GetShownSlide() == 3 && presenter->GetNotesText().empty(),
          "a key on the presenter's screen drives the show");
    now += 65;
    Check(presenter->GetElapsedText() == "0:01:05", "the time since the start [" + presenter->GetElapsedText() + "]");
    presenter->SetTimerPaused(true);
    now += 30;
    Check(presenter->IsTimerPaused() && presenter->GetElapsedText() == "0:01:05", "paused, it stands");
    presenter->SetTimerPaused(false);
    now += 5;
    Check(presenter->GetElapsedText() == "0:01:10", "resumed, it goes on");
    presenter->ResetTimer();
    now += 2;
    Check(presenter->GetElapsedText() == "0:00:02", "reset to nought");
    player->Stop();
    Check(presenter->GetShownSlide() == -1 && ended == 3, "the show ended");

    std::printf("\n-- the transition frames --\n");
    const Rect2Dd area(0, 0, 200, 100);
    auto fade = SlideTransitionFrame(SlideTransitionEffect::CrossFade, area, 0.25);
    Check(fade.size() == 2 && !fade[0].incoming && fade[1].incoming && std::fabs(fade[1].alpha - 0.25) < 1e-9,
          "a cross-fade: the old under the new at a quarter");
    auto opaqueFade = SlideTransitionFrame(SlideTransitionEffect::CrossFade, area, 0.25, true);
    Check(opaqueFade.size() == 2 && opaqueFade[0].alpha == 1.0 && std::fabs(opaqueFade[1].alpha - 0.25) < 1e-9,
          "between opaque pictures the old stays whole under the new");
    auto wipe = SlideTransitionFrame(SlideTransitionEffect::Wipe, area, 0.25);
    Check(wipe.size() == 2 && std::fabs(wipe[1].clip.width - 50) < 1e-9 && wipe[1].clip.x == 0,
          "a wipe: the new uncovered from the left");
    auto split = SlideTransitionFrame(SlideTransitionEffect::Split, area, 0.5);
    Check(split.size() == 2 && std::fabs(split[1].clip.x - 50) < 1e-9 && std::fabs(split[1].clip.width - 100) < 1e-9,
          "a split: opening from the centre");
    auto push = SlideTransitionFrame(SlideTransitionEffect::PushLeft, area, 0.5);
    Check(push.size() == 2 && push[0].offsetX == -100 && push[1].offsetX == 100, "a push: both move left");
    auto cover = SlideTransitionFrame(SlideTransitionEffect::Cover, area, 0.5);
    Check(cover.size() == 2 && cover[0].offsetX == 0 && cover[1].offsetX == 100, "a cover: the old stays");
    auto zoom = SlideTransitionFrame(SlideTransitionEffect::Zoom, area, 0.5);
    Check(zoom.size() == 2 && zoom[1].scale == 0.5, "a zoom: the new grows");
    Check(SlideTransitionFrame(SlideTransitionEffect::Zoom, area, 0).size() == 1, "nothing of a picture: left out");

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
