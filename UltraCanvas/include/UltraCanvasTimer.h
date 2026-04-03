// include/UltraCanvasTimer.h
// Cross-platform timer system for UltraCanvas Framework
// Supports one-shot and periodic timers with callback or event-based firing
#pragma once

#ifndef ULTRACANVAS_TIMER_H
#define ULTRACANVAS_TIMER_H

#include <cstdint>
#include <chrono>
#include <functional>

namespace UltraCanvas {

    using TimerId = uint32_t;
    constexpr TimerId InvalidTimerId = 0;

    struct UltraCanvasTimer {
        TimerId id = InvalidTimerId;
        std::chrono::steady_clock::time_point nextFire;
        std::chrono::milliseconds interval{0};
        bool periodic = false;
        bool active = true;
        // If set, callback is invoked on main thread when timer fires.
        // If null, a UCEvent with type=Timer and userDataInt=timerId is pushed instead.
        std::function<void(TimerId)> callback;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_TIMER_H
