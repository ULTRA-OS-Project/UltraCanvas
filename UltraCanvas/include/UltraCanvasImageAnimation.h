// include/UltraCanvasImageAnimation.h
// Timer-driven frame stepping for animated images (GIF, animated WebP)
// Version: 1.0.0
// Last Modified: 2026-07-10
// Author: UltraCanvas Framework
//
// UCImageAnimationController plays a UCImageAnimation frame sequence with its
// per-frame delays through the application's main-thread timer — the same
// mechanism UltraCanvasVideoPlayerElement uses for its frame ticks. The
// controller is deliberately media-agnostic: anything that can be expressed
// as "pixmaps + delays + loop count" (sprite strips, generated sequences)
// can be driven by it. Video keeps its own clock (the audio pipeline paces
// the frames there), so it is not routed through this class.
#pragma once

#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"
#include <functional>
#include <memory>

namespace UltraCanvas {

class UCImageAnimationController {
public:
    UCImageAnimationController() = default;
    ~UCImageAnimationController();

    // Non-copyable: owns a live application timer.
    UCImageAnimationController(const UCImageAnimationController&) = delete;
    UCImageAnimationController& operator=(const UCImageAnimationController&) = delete;

    // Swap in a new frame sequence (nullptr clears). Stops playback and
    // rewinds to frame 0.
    void SetAnimation(const std::shared_ptr<UCImageAnimation>& anim);
    const std::shared_ptr<UCImageAnimation>& GetAnimation() const { return animation; }

    // ===== TRANSPORT =====
    void Play();    // (re)start stepping; no-op without at least 2 frames
    void Pause();   // freeze on the current frame
    void Stop();    // freeze and rewind to frame 0
    bool IsPlaying() const { return playing; }

    // ===== FRAME ACCESS =====
    int  GetCurrentFrameIndex() const { return currentFrame; }
    void SetCurrentFrameIndex(int index);
    // Pixmap of the current frame; null when no animation is set.
    std::shared_ptr<UCPixmap> GetCurrentFramePixmap() const;

    // Ignore the file's finite loop count and repeat forever.
    void SetLoopForever(bool loop) { loopForever = loop; }
    bool GetLoopForever() const { return loopForever; }

    // ===== EVENTS (fired on the main thread) =====
    std::function<void()> onFrameChanged;   // after every frame step / rewind
    std::function<void()> onEnded;          // finite loop count exhausted

private:
    void ScheduleNext();    // one-shot timer for the current frame's delay
    void CancelTimer();
    void Advance();         // timer fired: step to the next frame

    std::shared_ptr<UCImageAnimation> animation;
    int  currentFrame = 0;
    int  playedLoops = 0;
    bool playing = false;
    bool loopForever = false;
    TimerId timerId = InvalidTimerId;
};

} // namespace UltraCanvas
