// core/UltraCanvasSlideShowPlayer.cpp
// The slide show player, the presenter view and StartSlideShow
// (include/UltraCanvasSlideShowPlayer.h).
//
// Version: 1.1.0 - StartSlideShow: showOnSecondScreen
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasSlideShowPlayer.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlideTransition.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {

constexpr int64_t kDefaultSlideWidth = 12192000;    // 16:9, as the renderer assumes
constexpr int64_t kDefaultSlideHeight = 6858000;
constexpr unsigned int kFrameMs = 16;               // ~60 frames a second in a transition
constexpr size_t kCachedSlides = 4;

double SteadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

SlideTransitionEffect EffectOf(PptxTransitionKind kind) {
    switch (kind) {
        case PptxTransitionKind::Fade: return SlideTransitionEffect::CrossFade;
        case PptxTransitionKind::Push: return SlideTransitionEffect::PushLeft;
        case PptxTransitionKind::Wipe: return SlideTransitionEffect::Wipe;
        case PptxTransitionKind::Split: return SlideTransitionEffect::Split;
        case PptxTransitionKind::Cover: return SlideTransitionEffect::Cover;
        case PptxTransitionKind::Zoom: return SlideTransitionEffect::Zoom;
        case PptxTransitionKind::NoTransition: break;
    }
    return SlideTransitionEffect::Cut;
}

// The digit a key types, or -1.
int DigitOf(const UCEvent& event) {
    const int key = static_cast<int>(event.virtualKey);
    if (key >= static_cast<int>(UCKeys::Key0) && key <= static_cast<int>(UCKeys::Key9))
        return key - static_cast<int>(UCKeys::Key0);
    if (key >= static_cast<int>(UCKeys::NumPad0) && key <= static_cast<int>(UCKeys::NumPad9))
        return key - static_cast<int>(UCKeys::NumPad0);
    if (event.virtualKey == UCKeys::Unknown && event.character >= '0' && event.character <= '9')
        return event.character - '0';
    return -1;
}

std::string NotesText(const PptxSlide& slide) {
    std::string text;
    for (const PptxParagraph& paragraph : slide.notes) {
        if (!text.empty()) text += "\n";
        text += paragraph.Text();
    }
    return text;
}

} // namespace

// ===== THE PLAYER =====

UltraCanvasSlideShowPlayer::UltraCanvasSlideShowPlayer(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasUIElement(id, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                           static_cast<float>(h)) {}

UltraCanvasSlideShowPlayer::~UltraCanvasSlideShowPlayer() { StopTimer(); }

void UltraCanvasSlideShowPlayer::SetDeck(std::shared_ptr<PptxPresentation> deck) {
    deck_ = std::move(deck);
    cache_.clear();
    if (!playing_) return;
    transitioning_ = false;
    from_ = -1;
    if (current_ >= SlideCount()) {
        current_ = -1;
        Changed(true);
    } else {
        Changed(false);
    }
    UpdateTimer();
    RequestRedraw();
}

void UltraCanvasSlideShowPlayer::SetClock(std::function<double()> clock) { clock_ = std::move(clock); }

double UltraCanvasSlideShowPlayer::Now() const { return clock_ ? clock_() : SteadySeconds(); }

int UltraCanvasSlideShowPlayer::SlideCount() const { return deck_ ? static_cast<int>(deck_->slides.size()) : 0; }

bool UltraCanvasSlideShowPlayer::IsShown(int index) const {
    return index >= 0 && index < SlideCount() && !deck_->slides[static_cast<size_t>(index)].hidden;
}

int UltraCanvasSlideShowPlayer::NextShown(int after) const {
    for (int i = std::max(-1, after) + 1; i < SlideCount(); ++i)
        if (IsShown(i)) return i;
    return -1;
}

int UltraCanvasSlideShowPlayer::PreviousShown(int before) const {
    for (int i = std::min(before, SlideCount()) - 1; i >= 0; --i)
        if (IsShown(i)) return i;
    return -1;
}

// ===== THE SHOW =====

void UltraCanvasSlideShowPlayer::Start(int fromSlide) {
    playing_ = true;
    transitioning_ = false;
    from_ = -1;
    blank_ = SlideShowBlank::NotBlank;
    typed_.clear();
    current_ = NextShown(std::max(0, fromSlide) - 1);
    shownAt_ = Now();
    SetFocus(true);
    Changed(true);
    UpdateTimer();
    RequestRedraw();
}

void UltraCanvasSlideShowPlayer::Stop() {
    if (!playing_) return;
    playing_ = false;
    transitioning_ = false;
    from_ = -1;
    blank_ = SlideShowBlank::NotBlank;
    typed_.clear();
    StopTimer();
    Changed(false);
    RequestRedraw();
    if (onShowEnd) onShowEnd();
}

void UltraCanvasSlideShowPlayer::GoToSlide(int index) {
    if (!playing_ || index < 0 || index >= SlideCount()) return;
    blank_ = SlideShowBlank::NotBlank;
    ShowSlide(index, false);
}

void UltraCanvasSlideShowPlayer::Next() {
    if (!playing_) return;
    if (transitioning_) FinishTransition();
    if (current_ < 0) {          // on from the end screen: the show is over
        Stop();
        return;
    }
    const int next = NextShown(current_);
    if (next < 0) ShowEndScreen();
    else ShowSlide(next, true);
}

void UltraCanvasSlideShowPlayer::Previous() {
    if (!playing_) return;
    if (transitioning_) FinishTransition();
    const int previous = current_ < 0 ? PreviousShown(SlideCount()) : PreviousShown(current_);
    if (previous >= 0) ShowSlide(previous, false);
}

void UltraCanvasSlideShowPlayer::ShowSlide(int index, bool withTransition) {
    const int from = current_;
    transitioning_ = false;
    typed_.clear();
    current_ = index;
    from_ = -1;
    const PptxTransition& transition = deck_->slides[static_cast<size_t>(index)].transition;
    if (withTransition && from >= 0 && from != index && transition.kind != PptxTransitionKind::NoTransition &&
        transition.durationSec > 0) {
        // Both pictures ready before the clock starts, so the first frames
        // are not lost to drawing the slide.
        if (cacheWidthPx_ > 0) {
            SlidePixmap(from, cacheWidthPx_);
            SlidePixmap(index, cacheWidthPx_);
        }
        from_ = from;
        transitioning_ = true;
        transitionKind_ = transition.kind;
        transitionDuration_ = transition.durationSec;
        transitionStart_ = Now();
        shownAt_ = transitionStart_ + transitionDuration_;
    } else {
        shownAt_ = Now();
    }
    Changed(true);
    UpdateTimer();
    RequestRedraw();
}

void UltraCanvasSlideShowPlayer::ShowEndScreen() {
    transitioning_ = false;
    from_ = -1;
    typed_.clear();
    current_ = -1;
    shownAt_ = Now();
    Changed(true);
    UpdateTimer();
    RequestRedraw();
}

void UltraCanvasSlideShowPlayer::FinishTransition() {
    if (!transitioning_) return;
    transitioning_ = false;
    from_ = -1;
    shownAt_ = transitionStart_ + transitionDuration_;
    Changed(false);
    UpdateTimer();
    RequestRedraw();
}

double UltraCanvasSlideShowPlayer::GetTransitionProgress() const {
    if (!IsTransitioning() || transitionDuration_ <= 0) return 1.0;
    return std::max(0.0, std::min(1.0, (Now() - transitionStart_) / transitionDuration_));
}

void UltraCanvasSlideShowPlayer::SetBlank(SlideShowBlank blank) {
    if (blank_ == blank) return;
    blank_ = blank;
    // The slide's timing starts again when it is shown again.
    if (blank_ == SlideShowBlank::NotBlank) shownAt_ = Now();
    Changed(false);
    UpdateTimer();
    RequestRedraw();
}

// ===== TIME =====

void UltraCanvasSlideShowPlayer::Tick() {
    if (!playing_) {
        StopTimer();
        return;
    }
    const double now = Now();
    if (transitioning_) {
        if (now - transitionStart_ >= transitionDuration_) FinishTransition();
        else RequestRedraw();
    }
    if (!transitioning_ && current_ >= 0 && blank_ == SlideShowBlank::NotBlank) {
        const double after = deck_->slides[static_cast<size_t>(current_)].transition.advanceAfterSec;
        if (after >= 0 && now - shownAt_ >= after) Next();
    }
    UpdateTimer();
}

void UltraCanvasSlideShowPlayer::UpdateTimer() {
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    if (playing_ && transitioning_) {
        if (timer_ && timerPeriodic_) return;
        StopTimer();
        timerPeriodic_ = true;
        timer_ = app->StartTimer(kFrameMs, true, [this](TimerId) { Tick(); });
        return;
    }
    double dueIn = -1;
    if (playing_ && current_ >= 0 && blank_ == SlideShowBlank::NotBlank) {
        const double after = deck_->slides[static_cast<size_t>(current_)].transition.advanceAfterSec;
        if (after >= 0) dueIn = std::max(0.0, shownAt_ + after - Now());
    }
    StopTimer();
    if (dueIn < 0) return;
    timerPeriodic_ = false;
    const auto ms = static_cast<unsigned int>(std::max(1.0, std::ceil(dueIn * 1000.0)));
    timer_ = app->StartTimer(ms, false, [this](TimerId) {
        timer_ = 0;
        Tick();
    });
}

void UltraCanvasSlideShowPlayer::StopTimer() {
    if (!timer_) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
    timer_ = 0;
    timerPeriodic_ = false;
}

int UltraCanvasSlideShowPlayer::AddObserver(std::function<void()> changed) {
    const int id = nextObserver_++;
    observers_.emplace_back(id, std::move(changed));
    return id;
}

void UltraCanvasSlideShowPlayer::RemoveObserver(int id) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [id](const auto& observer) { return observer.first == id; }),
                     observers_.end());
}

void UltraCanvasSlideShowPlayer::Changed(bool slideChanged) {
    if (slideChanged && onSlideChanged) onSlideChanged(GetCurrentSlide());
    // A copy: an observer may remove itself.
    const auto observers = observers_;
    for (const auto& observer : observers)
        if (observer.second) observer.second();
}

// ===== EVENTS =====

bool UltraCanvasSlideShowPlayer::OnEvent(const UCEvent& event) {
    if (!playing_) return UltraCanvasUIElement::OnEvent(event);
    switch (event.type) {
        case UCEventType::MouseDown:
            typed_.clear();
            if (transitioning_) FinishTransition();
            else if (blank_ != SlideShowBlank::NotBlank) SetBlank(SlideShowBlank::NotBlank);
            else if (event.button == UCMouseButton::Right) Previous();
            else if (event.button == UCMouseButton::Left) {
                // A slide that moves on only by its timing ignores the click.
                if (current_ < 0 || deck_->slides[static_cast<size_t>(current_)].transition.advanceOnClick) Next();
            }
            return true;
        case UCEventType::MouseUp:
        case UCEventType::MouseDoubleClick:
            return true;
        case UCEventType::MouseWheel:
            if (transitioning_) FinishTransition();
            else if (blank_ != SlideShowBlank::NotBlank) SetBlank(SlideShowBlank::NotBlank);
            else if (event.wheelDelta > 0) Previous();
            else Next();
            return true;
        case UCEventType::KeyDown:
            return OnKey(event);
        case UCEventType::KeyUp:
            return true;
        default:
            return UltraCanvasUIElement::OnEvent(event);
    }
}

bool UltraCanvasSlideShowPlayer::OnKey(const UCEvent& event) {
    const UCKeys key = event.virtualKey;
    if (key == UCKeys::Escape) {
        Stop();
        return true;
    }
    // A slide's number, then Enter.
    const int digit = DigitOf(event);
    if (digit >= 0) {
        if (typed_.size() < 6) typed_ += static_cast<char>('0' + digit);
        RequestRedraw();
        return true;
    }
    if (!typed_.empty()) {
        if (key == UCKeys::Return || key == UCKeys::NumPadEnter) {
            int number = 0;
            for (char c : typed_) number = number * 10 + (c - '0');
            typed_.clear();
            if (transitioning_) FinishTransition();
            GoToSlide(number - 1);
            RequestRedraw();
            return true;
        }
        if (key == UCKeys::Backspace) {
            typed_.pop_back();
            RequestRedraw();
            return true;
        }
        typed_.clear();
        RequestRedraw();
    }
    if (transitioning_) {
        FinishTransition();
        return true;
    }
    if (key == UCKeys::B || key == UCKeys::Period) {
        SetBlank(blank_ == SlideShowBlank::BlackScreen ? SlideShowBlank::NotBlank : SlideShowBlank::BlackScreen);
        return true;
    }
    if (key == UCKeys::W || key == UCKeys::Comma) {
        SetBlank(blank_ == SlideShowBlank::WhiteScreen ? SlideShowBlank::NotBlank : SlideShowBlank::WhiteScreen);
        return true;
    }
    if (blank_ != SlideShowBlank::NotBlank) {   // any other key shows the slide again
        SetBlank(SlideShowBlank::NotBlank);
        return true;
    }
    switch (key) {
        case UCKeys::Right: case UCKeys::Down: case UCKeys::PageDown: case UCKeys::Space: case UCKeys::Return:
        case UCKeys::NumPadEnter: case UCKeys::N:
            Next();
            return true;
        case UCKeys::Left: case UCKeys::Up: case UCKeys::PageUp: case UCKeys::Backspace: case UCKeys::P:
            Previous();
            return true;
        case UCKeys::Home: {
            const int first = NextShown(-1);
            if (first >= 0) ShowSlide(first, false);
            return true;
        }
        case UCKeys::End: {
            const int last = PreviousShown(SlideCount());
            if (last >= 0) ShowSlide(last, false);
            return true;
        }
        default:
            return true;   // nothing else happens during the show
    }
}

// ===== DRAWING =====

Rect2Dd UltraCanvasSlideShowPlayer::SlideArea() const {
    const Rect2Df b = GetLocalBounds();
    const double slideW = static_cast<double>(deck_ && deck_->slideWidth > 0 ? deck_->slideWidth : kDefaultSlideWidth);
    const double slideH =
            static_cast<double>(deck_ && deck_->slideHeight > 0 ? deck_->slideHeight : kDefaultSlideHeight);
    const double scale = std::min(b.width / slideW, b.height / slideH);
    const double w = slideW * scale, h = slideH * scale;
    return Rect2Dd((b.width - w) / 2.0, (b.height - h) / 2.0, w, h);
}

std::shared_ptr<UCPixmap> UltraCanvasSlideShowPlayer::SlidePixmap(int index, int widthPx) {
    if (index < 0 || index >= SlideCount() || widthPx <= 0) return nullptr;
    if (widthPx != cacheWidthPx_) {   // a new size: every slide again
        cache_.clear();
        cacheWidthPx_ = widthPx;
    }
    for (size_t i = 0; i < cache_.size(); ++i) {
        if (cache_[i].index != index) continue;
        // The most recently used last.
        CachedSlide hit = cache_[i];
        cache_.erase(cache_.begin() + static_cast<std::ptrdiff_t>(i));
        cache_.push_back(hit);
        return hit.pixmap;
    }
    CachedSlide made;
    made.index = index;
    made.pixmap = RenderPptxSlide(*deck_, static_cast<size_t>(index), widthPx);
    if (!made.pixmap) return nullptr;
    cache_.push_back(made);
    // Keep a few; never the two of a transition.
    for (size_t i = 0; cache_.size() > kCachedSlides && i < cache_.size();) {
        if (cache_[i].index != current_ && cache_[i].index != from_) cache_.erase(cache_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    return made.pixmap;
}

void UltraCanvasSlideShowPlayer::DrawSlide(IRenderContext* ctx, int index, const Rect2Dd& area, double alpha,
                                           double offsetX, double offsetY, double scale, const Rect2Dd& clip) {
    if (index < 0 || index >= SlideCount() || scale <= 0 || alpha <= 0) return;
    const double w = area.width * scale, h = area.height * scale;
    const Rect2Dd at(area.x + (area.width - w) / 2.0 + offsetX, area.y + (area.height - h) / 2.0 + offsetY, w, h);
    ctx->PushState();
    ctx->ClipRect(clip);
    ctx->SetAlpha(std::max(0.0, std::min(1.0, alpha)));
    const int widthPx = static_cast<int>(std::lround(area.width * std::max(1.0f, ctx->GetDeviceScale())));
    if (auto pixmap = SlidePixmap(index, widthPx)) ctx->DrawPixmap(*pixmap, at, ImageFitMode::Fill);
    else DrawPptxSlide(ctx, *deck_, deck_->slides[static_cast<size_t>(index)], at);
    ctx->PopState();
}

void UltraCanvasSlideShowPlayer::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!IsVisible()) return;
    const Rect2Df b = GetLocalBounds();
    ctx->PushState();
    ctx->SetFillPaint(blank_ == SlideShowBlank::WhiteScreen ? Colors::White : Colors::Black);
    ctx->FillRectangle(Rect2Dd(0, 0, b.width, b.height));
    if (playing_ && blank_ == SlideShowBlank::NotBlank) {
        const Rect2Dd area = SlideArea();
        if (current_ >= 0 && current_ < SlideCount()) {
            if (transitioning_) {
                const double progress = SlideTransitionEase(GetTransitionProgress());
                for (const SlideTransitionLayer& layer : SlideTransitionFrame(EffectOf(transitionKind_), area, progress, true))
                    DrawSlide(ctx, layer.incoming ? current_ : from_, area, layer.alpha, layer.offsetX, layer.offsetY,
                              layer.scale, layer.clip);
            } else {
                DrawSlide(ctx, current_, area, 1.0, 0, 0, 1.0, area);
            }
        } else {
            FontStyle font;
            font.fontSize = 14;
            ctx->SetFontStyle(font);
            ctx->SetTextPaint(Color(200, 200, 200));
            ctx->DrawText(Tr("End of the slide show. Click to leave."), Point2Dd(24, 24));
        }
    }
    // The number being typed, small in the corner, as the presenter types it.
    if (playing_ && !typed_.empty()) {
        FontStyle font;
        font.fontSize = 18;
        ctx->SetFontStyle(font);
        ctx->SetTextPaint(blank_ == SlideShowBlank::WhiteScreen ? Color(60, 60, 60) : Color(200, 200, 200));
        ctx->DrawText(typed_, Point2Dd(24, b.height - 40));
    }
    ctx->PopState();
}

// ===== THE PRESENTER VIEW =====

UltraCanvasPresenterView::UltraCanvasPresenterView(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasContainer(id, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                           static_cast<float>(h)) {
    const Color background(24, 24, 28);
    const Color ink(235, 235, 240);
    const Color muted(160, 160, 170);
    SetBackgroundColor(background);
    SetPadding(16);
    layout.SetFlexColumn().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // The slide shown, large, and beside it the next one and the notes.
    auto body = std::make_shared<UltraCanvasContainer>("PresenterBody");
    body->layout.SetFlexRow().SetFlexGap(16).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    AddChild(body);

    current_ = std::make_shared<UltraCanvasImageElement>("PresenterCurrentSlide");
    current_->SetFitMode(ImageFitMode::Contain);
    current_->layoutItem.SetFlexGrow(3).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    body->AddChild(current_);

    auto side = std::make_shared<UltraCanvasContainer>("PresenterSide");
    side->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    side->layoutItem.SetFlexGrow(2).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    body->AddChild(side);

    nextTitle_ = std::make_shared<UltraCanvasLabel>("PresenterNextTitle", Tr("Next slide"));
    nextTitle_->SetTextColor(muted);
    nextTitle_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    side->AddChild(nextTitle_);

    next_ = std::make_shared<UltraCanvasImageElement>("PresenterNextSlide");
    next_->SetFitMode(ImageFitMode::Contain);
    next_->layoutItem.SetFlexGrow(2).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    side->AddChild(next_);

    auto notesTitle = std::make_shared<UltraCanvasLabel>("PresenterNotesTitle", Tr("Speaker notes"));
    notesTitle->SetTextColor(muted);
    notesTitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    side->AddChild(notesTitle);

    notes_ = std::make_shared<UltraCanvasTextArea>("PresenterNotes");
    notes_->SetReadOnly(true);
    notes_->SetWordWrap(true);
    notes_->layoutItem.SetFlexGrow(3).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    side->AddChild(notes_);

    // The bar: the time, its buttons, the slides' buttons, the slide's number.
    auto bar = std::make_shared<UltraCanvasContainer>("PresenterBar");
    bar->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    AddChild(bar);

    clockLabel_ = std::make_shared<UltraCanvasLabel>("PresenterElapsed", "0:00:00");
    clockLabel_->SetFontSize(22);
    clockLabel_->SetTextColor(ink);
    clockLabel_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->AddChild(clockLabel_);

    pause_ = std::make_shared<UltraCanvasButton>("PresenterPause", Tr("Pause"));
    pause_->onClick = [this]() { SetTimerPaused(!paused_); };
    bar->AddChild(pause_);
    auto reset = std::make_shared<UltraCanvasButton>("PresenterReset", Tr("Reset"));
    reset->onClick = [this]() { ResetTimer(); };
    bar->AddChild(reset);

    auto spacer = std::make_shared<UltraCanvasContainer>("PresenterSpacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bar->AddChild(spacer);

    auto previous = std::make_shared<UltraCanvasButton>("PresenterPrevious", Tr("Previous slide"));
    previous->onClick = [this]() {
        if (player_) player_->Previous();
    };
    bar->AddChild(previous);
    auto next = std::make_shared<UltraCanvasButton>("PresenterNext", Tr("Next slide"));
    next->onClick = [this]() {
        if (player_) player_->Next();
    };
    bar->AddChild(next);

    position_ = std::make_shared<UltraCanvasLabel>("PresenterPosition", "");
    position_->SetFontSize(16);
    position_->SetTextColor(ink);
    position_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->AddChild(position_);
}

UltraCanvasPresenterView::~UltraCanvasPresenterView() {
    if (timer_)
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
    if (player_ && observer_) player_->RemoveObserver(observer_);
}

void UltraCanvasPresenterView::SetPlayer(std::shared_ptr<UltraCanvasSlideShowPlayer> player) {
    if (player_ && observer_) player_->RemoveObserver(observer_);
    observer_ = 0;
    player_ = std::move(player);
    started_ = false;
    paused_ = false;
    elapsed_ = 0;
    if (player_) observer_ = player_->AddObserver([this]() { Follow(); });
    Follow();
}

void UltraCanvasPresenterView::SetClock(std::function<double()> clock) { clock_ = std::move(clock); }

double UltraCanvasPresenterView::Now() const { return clock_ ? clock_() : SteadySeconds(); }

void UltraCanvasPresenterView::Follow() {
    const bool playing = player_ && player_->IsPlaying();
    if (playing && !started_) StartClock();
    if (!playing && started_ && !paused_) {   // the show ended: the time stops there
        elapsed_ = GetElapsedSeconds();
        paused_ = true;
        started_ = false;
    }
    shown_ = playing ? player_->GetCurrentSlide() : -1;
    const std::shared_ptr<PptxPresentation> deck = player_ ? player_->GetDeck() : nullptr;
    const int count = deck ? static_cast<int>(deck->slides.size()) : 0;

    // The pictures, drawn again only for another slide.
    if (shown_ != picturesOf_ || deck != picturesDeck_) {
        picturesOf_ = shown_;
        picturesDeck_ = deck;
        std::shared_ptr<UCPixmap> now, then;
        int following = -1;
        if (deck && shown_ >= 0 && shown_ < count) {
            now = RenderPptxSlide(*deck, static_cast<size_t>(shown_), 1280);
            for (int i = shown_ + 1; i < count; ++i)
                if (!deck->slides[static_cast<size_t>(i)].hidden) { following = i; break; }
            if (following >= 0) then = RenderPptxSlide(*deck, static_cast<size_t>(following), 640);
        }
        current_->LoadFromPixmap(now);
        next_->LoadFromPixmap(then);
        nextTitle_->SetText(following >= 0 || shown_ < 0 ? Tr("Next slide") : Tr("End of slideshow"));
        notes_->SetText(deck && shown_ >= 0 && shown_ < count ? NotesText(deck->slides[static_cast<size_t>(shown_)]) : "",
                        false);
    }
    position_->SetText(GetPositionText());
    RefreshElapsed();
    UpdateTimer();
}

std::string UltraCanvasPresenterView::GetPositionText() const {
    const std::shared_ptr<PptxPresentation> deck = player_ ? player_->GetDeck() : nullptr;
    if (!deck || deck->slides.empty()) return "";
    if (shown_ < 0) return Tr("End of slideshow");
    return TrF("Slide {0} of {1}", shown_ + 1, static_cast<int>(deck->slides.size()));
}

std::string UltraCanvasPresenterView::GetNotesText() const { return notes_->GetText(); }

std::string UltraCanvasPresenterView::GetElapsedText() const {
    const long seconds = static_cast<long>(std::floor(GetElapsedSeconds()));
    char text[32];
    std::snprintf(text, sizeof(text), "%ld:%02ld:%02ld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    return text;
}

// ===== THE TIME =====

void UltraCanvasPresenterView::StartClock() {
    started_ = true;
    paused_ = false;
    elapsed_ = 0;
    runningSince_ = Now();
}

double UltraCanvasPresenterView::GetElapsedSeconds() const {
    if (!started_ || paused_) return elapsed_;
    return elapsed_ + std::max(0.0, Now() - runningSince_);
}

void UltraCanvasPresenterView::SetTimerPaused(bool paused) {
    if (paused == paused_) return;
    if (paused) {
        elapsed_ = GetElapsedSeconds();
        paused_ = true;
    } else {
        paused_ = false;
        runningSince_ = Now();
        if (!started_) started_ = true;
    }
    pause_->SetText(paused_ ? Tr("Resume") : Tr("Pause"));
    RefreshElapsed();
    UpdateTimer();
}

void UltraCanvasPresenterView::ResetTimer() {
    elapsed_ = 0;
    runningSince_ = Now();
    RefreshElapsed();
}

void UltraCanvasPresenterView::RefreshElapsed() {
    const std::string text = GetElapsedText();
    if (clockLabel_->GetText() != text) clockLabel_->SetText(text);
}

void UltraCanvasPresenterView::UpdateTimer() {
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    const bool running = started_ && !paused_;
    if (running && !timer_) timer_ = app->StartTimer(1000, true, [this](TimerId) { RefreshElapsed(); });
    if (!running && timer_) {
        app->StopTimer(timer_);
        timer_ = 0;
    }
}

bool UltraCanvasPresenterView::OnEvent(const UCEvent& event) {
    // The show's keys work from the presenter's screen too.
    if (player_ && player_->IsPlaying() && (event.type == UCEventType::KeyDown || event.type == UCEventType::KeyUp))
        return player_->OnEvent(event);
    return UltraCanvasContainer::OnEvent(event);
}

// ===== OPENING THE SHOW =====

namespace {

// A window full screen on `screen` (null: where the platform puts it).
std::shared_ptr<UltraCanvasWindow> OpenFullScreenWindow(const std::string& title, const DesktopScreenInfo* screen) {
    WindowConfig config;
    config.title = title;
    config.width = 1280;
    config.height = 720;
    auto window = CreateWindow(config);
    if (!window) return nullptr;
    window->SetBackgroundColor(Colors::Black);
    // Onto that monitor first (screen pixels, as ListScreens gives them):
    // full screen then fills the monitor the window is on.
    if (screen) window->SetWindowPosition(screen->x + 32, screen->y + 32);
    return window;
}

} // namespace

std::shared_ptr<SlideShowWindows> StartSlideShow(std::shared_ptr<PptxPresentation> deck, int fromSlide,
                                                 bool presenterView, bool showOnSecondScreen) {
    const std::vector<DesktopScreenInfo> screens = UltraCanvasDesktopShell::ListScreens();   // the primary first
    const bool twoScreens = screens.size() >= 2;
    const size_t showScreen = showOnSecondScreen ? 1 : 0;
    auto show = std::make_shared<SlideShowWindows>();
    show->showWindow = OpenFullScreenWindow(Tr("Slide show"), twoScreens ? &screens[showScreen] : nullptr);
    if (!show->showWindow) return nullptr;
    show->player = CreateSlideShowPlayer("SlideShowPlayer", 0, 0, 1280, 720);
    show->player->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    show->player->SetDeck(deck);
    show->showWindow->AddChild(show->player);

    if (twoScreens && presenterView) {
        show->presenterWindow = OpenFullScreenWindow(Tr("Presenter view"), &screens[1 - showScreen]);
        if (show->presenterWindow) {
            show->presenter = CreatePresenterView("PresenterView", 0, 0, 1280, 720);
            show->presenter->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
            show->presenterWindow->AddChild(show->presenter);
        }
    }

    // The show ended, or one of its windows was closed: both close, after
    // the event that ended it is handled, then onEnd.
    std::weak_ptr<SlideShowWindows> weak = show;
    auto finish = [weak]() {
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        app->StartTimer(1, false, [weak](TimerId) {
            auto ended = weak.lock();
            if (!ended || !ended->showWindow) return;
            auto showWindow = std::move(ended->showWindow);
            auto presenterWindow = std::move(ended->presenterWindow);
            if (ended->presenter) ended->presenter->SetPlayer(nullptr);
            showWindow->onWindowClosed = nullptr;
            showWindow->Close();
            if (presenterWindow) {
                presenterWindow->onWindowClosed = nullptr;
                presenterWindow->Close();
            }
            if (ended->onEnd) ended->onEnd();
        });
    };
    show->player->onShowEnd = finish;
    show->showWindow->onWindowClosed = finish;
    if (show->presenterWindow) show->presenterWindow->onWindowClosed = finish;

    show->showWindow->Show();
    show->showWindow->SetFullscreen(true);
    if (show->presenterWindow) {
        show->presenterWindow->Show();
        show->presenterWindow->SetFullscreen(true);
    }
    show->player->Start(fromSlide);
    if (show->presenter) show->presenter->SetPlayer(show->player);
    return show;
}

} // namespace UltraCanvas
