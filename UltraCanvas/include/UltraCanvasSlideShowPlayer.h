// include/UltraCanvasSlideShowPlayer.h
// A presentation played as a slide show: UltraCanvasSlideShowPlayer shows a
// deck's slides one after another, each as large as the element on black
// (letterboxed), the hidden ones left out, with each slide's transition
// (PptxSlide::transition) as it comes on and its timing; a black end screen
// follows the last slide. UltraCanvasPresenterView is the presenter's screen
// beside it: the slide shown and the next one, the speaker notes, the time
// and the slide's number, and buttons that drive the player.
// StartSlideShow opens both, each full screen on a monitor of its own when
// there are two. The player replaces UltraCanvasSlideEdit's show mode.
//
// The player's keys and clicks, as PowerPoint's:
//   on      click, Space, Enter, Right, Down, Page Down, N, wheel down
//   back    right-click, Backspace, Left, Up, Page Up, P, wheel up
//   Home / End   the first / the last slide shown
//   a number, then Enter   that slide (a hidden one too)
//   B or .  a black screen;  W or ,  a white one (again, or any key: back)
//   Escape  ends the show (onShowEnd)
// A key or click during a transition finishes it at once. A slide whose
// advanceOnClick is false does not move on for a click (the keys still do);
// one with advanceAfterSec >= 0 moves on by itself that long after it is
// fully shown.
//
// doc-check: std::shared_ptr<PptxPresentation> deck;
// doc-check: example
//   auto player = CreateSlideShowPlayer("show", 0, 0, 1280, 720);
//   player->SetDeck(deck);
//   player->onShowEnd = []() { /* close the show's window */ };
//   player->Start(0);
//
// Version: 1.1.0 - StartSlideShow(..., showOnSecondScreen): the show on the first monitor, the presenter
//                  view on the second, when false
// Version: 1.0.0 - transitions (Fade, Push, Wipe, Split, Cover, Zoom), timings, a slide by number, black and
//                  white screens; the presenter view; StartSlideShow on two monitors
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"   // UltraCanvasWindow is the platform's window type, not a class to forward-declare

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasButton;
class UltraCanvasImageElement;
class UltraCanvasLabel;
class UltraCanvasTextArea;

// What covers the slides for a moment (B and W in the show).
enum class SlideShowBlank { NotBlank, BlackScreen, WhiteScreen };

class UltraCanvasSlideShowPlayer : public UltraCanvasUIElement {
public:
    UltraCanvasSlideShowPlayer(const std::string& id, int x, int y, int w, int h);
    ~UltraCanvasSlideShowPlayer() override;

    // ===== THE DECK =====
    // Read only: the player never changes it. A new deck while playing keeps
    // the slide shown where it still exists.
    void SetDeck(std::shared_ptr<PptxPresentation> deck);
    const std::shared_ptr<PptxPresentation>& GetDeck() const { return deck_; }

    // ===== THE SHOW =====
    // From `fromSlide` (0-based; the first slide shown at or after it), without
    // a transition, and takes the keyboard.
    void Start(int fromSlide = 0);
    // Ends the show: onShowEnd.
    void Stop();
    bool IsPlaying() const { return playing_; }
    // The slide shown, 0-based - during a transition the one coming on;
    // -1 on the end screen (and when not playing).
    int GetCurrentSlide() const { return playing_ ? current_ : -1; }
    // Slide `index` (0-based, a hidden one too) at once, without a transition.
    void GoToSlide(int index);
    // The next slide shown, with its transition; after the last the end
    // screen, and from the end screen the show ends.
    void Next();
    // The slide shown before this one, without a transition; from the end
    // screen the last slide.
    void Previous();

    // ===== WHILE IT PLAYS =====
    bool IsTransitioning() const { return playing_ && transitioning_; }
    // How far the transition is, 0..1 (1 when there is none).
    double GetTransitionProgress() const;
    // The slide going off in the transition; -1 when there is none.
    int GetTransitionFromSlide() const { return IsTransitioning() ? from_ : -1; }
    void SetBlank(SlideShowBlank blank);
    SlideShowBlank GetBlank() const { return blank_; }
    // The digits typed so far for a slide's number (Enter goes there).
    const std::string& GetTypedNumber() const { return typed_; }

    // ===== TIME =====
    // The clock the transitions and timings run by, in seconds; the default
    // reads std::chrono::steady_clock. A test replaces it and calls Tick.
    void SetClock(std::function<double()> clock);
    // Brings the show to the clock's time: ends a transition that is over,
    // moves on from a slide whose time is up. The player's timer calls it
    // (about 60 times a second during a transition, once when a slide's time
    // is up, not at all otherwise).
    void Tick();

    // ===== CALLBACKS =====
    std::function<void()> onShowEnd;                 // Escape, or on from the end screen
    std::function<void(int slide)> onSlideChanged;   // the slide shown changed; -1: the end screen
    // For a view that follows the player (UltraCanvasPresenterView): `changed`
    // runs after any change - the slide, a transition ending, the show
    // starting or ending, a blank screen. Returns an id for RemoveObserver.
    int AddObserver(std::function<void()> changed);
    void RemoveObserver(int id);

    // ===== ELEMENT =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

    // Where the slide is drawn, in the element's coordinates (letterboxed).
    Rect2Dd SlideArea() const;

private:
    std::shared_ptr<PptxPresentation> deck_;
    bool playing_ = false;
    int current_ = -1;                // -1: the end screen
    int from_ = -1;                   // the slide going off in a transition
    bool transitioning_ = false;
    PptxTransitionKind transitionKind_ = PptxTransitionKind::NoTransition;
    double transitionStart_ = 0;
    double transitionDuration_ = 0;
    double shownAt_ = 0;              // when the slide shown was fully shown (its timing counts from then)
    SlideShowBlank blank_ = SlideShowBlank::NotBlank;
    std::string typed_;
    std::function<double()> clock_;
    TimerId timer_ = 0;
    bool timerPeriodic_ = false;
    std::vector<std::pair<int, std::function<void()>>> observers_;
    int nextObserver_ = 1;

    // The slides drawn once at the size shown, for the transitions; a few
    // kept (the slide going off, the one coming on, the next).
    struct CachedSlide {
        int index = -1;
        std::shared_ptr<UCPixmap> pixmap;
    };
    std::vector<CachedSlide> cache_;
    int cacheWidthPx_ = 0;

    double Now() const;
    int SlideCount() const;
    bool IsShown(int index) const;
    int NextShown(int after) const;        // -1: none
    int PreviousShown(int before) const;   // -1: none
    void ShowSlide(int index, bool withTransition);
    void ShowEndScreen();
    void FinishTransition();
    void Changed(bool slideChanged);
    void UpdateTimer();
    void StopTimer();
    bool OnKey(const UCEvent& event);
    std::shared_ptr<UCPixmap> SlidePixmap(int index, int widthPx);
    void DrawSlide(IRenderContext* ctx, int index, const Rect2Dd& area, double alpha, double offsetX,
                   double offsetY, double scale, const Rect2Dd& clip);
};

inline std::shared_ptr<UltraCanvasSlideShowPlayer> CreateSlideShowPlayer(const std::string& id, int x, int y, int w,
                                                                       int h) {
    return std::make_shared<UltraCanvasSlideShowPlayer>(id, x, y, w, h);
}

// ===== THE PRESENTER VIEW =====
// The presenter's screen: the slide shown, large; the next slide, small; the
// slide's speaker notes (scrollable); the time since the show began, with
// Pause / Resume and Reset; Previous slide and Next slide buttons; and
// "Slide N of M". It follows a player (SetPlayer) and drives it: its buttons
// and the keys typed into it go to the player, so the presenter can work the
// show from either screen.
class UltraCanvasPresenterView : public UltraCanvasContainer {
public:
    UltraCanvasPresenterView(const std::string& id, int x, int y, int w, int h);
    ~UltraCanvasPresenterView() override;

    // The player followed and driven; null: none. Kept alive while set.
    void SetPlayer(std::shared_ptr<UltraCanvasSlideShowPlayer> player);
    const std::shared_ptr<UltraCanvasSlideShowPlayer>& GetPlayer() const { return player_; }

    // The slide shown now (the player's), -1 on the end screen.
    int GetShownSlide() const { return shown_; }
    // What the view says: "Slide 2 of 9", the notes as plain text, the time.
    std::string GetPositionText() const;
    std::string GetNotesText() const;
    std::string GetElapsedText() const;

    // ===== THE TIME =====
    // Runs from when the show starts (or SetPlayer finds it playing).
    double GetElapsedSeconds() const;
    void SetTimerPaused(bool paused);
    bool IsTimerPaused() const { return paused_; }
    void ResetTimer();
    // The clock the time is read from, in seconds (default: steady_clock).
    void SetClock(std::function<double()> clock);
    // Refreshes the time shown (a timer calls it every second).
    void RefreshElapsed();

    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

private:
    std::shared_ptr<UltraCanvasSlideShowPlayer> player_;
    int observer_ = 0;
    int shown_ = -1;
    int picturesOf_ = -2;             // the slide the pictures were drawn for
    std::shared_ptr<PptxPresentation> picturesDeck_;

    // The time: `elapsed_` banked while paused, plus since `runningSince_`.
    std::function<double()> clock_;
    bool started_ = false;
    bool paused_ = false;
    double elapsed_ = 0;
    double runningSince_ = 0;
    TimerId timer_ = 0;

    std::shared_ptr<UltraCanvasImageElement> current_;
    std::shared_ptr<UltraCanvasImageElement> next_;
    std::shared_ptr<UltraCanvasLabel> nextTitle_;
    std::shared_ptr<UltraCanvasTextArea> notes_;
    std::shared_ptr<UltraCanvasLabel> clockLabel_;
    std::shared_ptr<UltraCanvasButton> pause_;
    std::shared_ptr<UltraCanvasLabel> position_;

    double Now() const;
    void Follow();                    // the player changed: show what it shows
    void StartClock();
    void UpdateTimer();
};

inline std::shared_ptr<UltraCanvasPresenterView> CreatePresenterView(const std::string& id, int x, int y, int w,
                                                                   int h) {
    return std::make_shared<UltraCanvasPresenterView>(id, x, y, w, h);
}

// ===== OPENING THE SHOW =====
// The windows of a show StartSlideShow opened. Keep it until onEnd: the
// windows close when the show ends (Escape, on from the end screen, or a
// window closed), and then onEnd runs.
struct SlideShowWindows {
    std::shared_ptr<UltraCanvasWindow> showWindow;
    std::shared_ptr<UltraCanvasSlideShowPlayer> player;
    // On the first monitor when there are two; null otherwise.
    std::shared_ptr<UltraCanvasWindow> presenterWindow;
    std::shared_ptr<UltraCanvasPresenterView> presenter;
    std::function<void()> onEnd;
};

// Plays `deck` from `fromSlide`: with two monitors or more
// (UltraCanvasDesktopShell::ListScreens) the show full screen on the second
// and, with `presenterView`, the presenter view full screen on the first -
// the other way round with `showOnSecondScreen` false; with one, the show
// alone full screen. Null when no window could be made. Where the platform
// places windows itself (Wayland) or lists no monitors (macOS for now) the
// windows go where it puts them.
std::shared_ptr<SlideShowWindows> StartSlideShow(std::shared_ptr<PptxPresentation> deck, int fromSlide = 0,
                                                 bool presenterView = true, bool showOnSecondScreen = true);

} // namespace UltraCanvas
