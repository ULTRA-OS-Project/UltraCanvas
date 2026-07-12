// core/UltraCanvasImageAnimation.cpp
// Timer-driven frame stepping for animated images (GIF, animated WebP)
// Version: 1.0.0
// Last Modified: 2026-07-10
// Author: UltraCanvas Framework

#include "UltraCanvasImageAnimation.h"
#include "UltraCanvasApplication.h"
#include <algorithm>

namespace UltraCanvas {

UCImageAnimationController::~UCImageAnimationController() {
    CancelTimer();
}

void UCImageAnimationController::SetAnimation(const std::shared_ptr<UCImageAnimation>& anim) {
    CancelTimer();
    playing = false;
    animation = anim;
    currentFrame = 0;
    playedLoops = 0;
}

void UCImageAnimationController::Play() {
    if (playing) return;
    if (!animation || animation->GetFrameCount() <= 1) return;
    // Play() after a finite animation finished restarts it from the top.
    if (!loopForever && animation->loopCount > 0 && playedLoops >= animation->loopCount) {
        playedLoops = 0;
        SetCurrentFrameIndex(0);
    }
    playing = true;
    ScheduleNext();
}

void UCImageAnimationController::Pause() {
    playing = false;
    CancelTimer();
}

void UCImageAnimationController::Stop() {
    playing = false;
    CancelTimer();
    playedLoops = 0;
    SetCurrentFrameIndex(0);
}

void UCImageAnimationController::SetCurrentFrameIndex(int index) {
    if (!animation) return;
    int n = animation->GetFrameCount();
    if (n <= 0) return;
    index = std::max(0, std::min(index, n - 1));
    if (index == currentFrame) return;
    currentFrame = index;
    if (onFrameChanged) onFrameChanged();
}

std::shared_ptr<UCPixmap> UCImageAnimationController::GetCurrentFramePixmap() const {
    if (!animation) return nullptr;
    return animation->GetFramePixmap(currentFrame);
}

void UCImageAnimationController::ScheduleNext() {
    CancelTimer();
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) { playing = false; return; }
    unsigned int delay = static_cast<unsigned int>(animation->GetFrameDelayMs(currentFrame));
    timerId = app->StartTimer(delay, false, [this](TimerId) {
        timerId = InvalidTimerId;   // one-shot timers are already gone when they fire
        Advance();
    });
}

void UCImageAnimationController::Advance() {
    if (!playing || !animation) return;
    const int n = animation->GetFrameCount();
    int next = currentFrame + 1;
    if (next >= n) {
        ++playedLoops;
        if (!loopForever && animation->loopCount > 0 && playedLoops >= animation->loopCount) {
            playing = false;
            if (onEnded) onEnded();
            return;                  // freeze on the last frame
        }
        next = 0;
    }
    currentFrame = next;
    if (onFrameChanged) onFrameChanged();
    ScheduleNext();
}

void UCImageAnimationController::CancelTimer() {
    if (timerId == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timerId);
    timerId = InvalidTimerId;
}

} // namespace UltraCanvas
