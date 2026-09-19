// UltraCanvas/include/UltraMessage/UltraMessageUltraCanvas.h
// The bridge between UltraMessage and an UltraCanvas application: installs
// UltraCanvasApplicationBase::PostToUIThread as the module's UI dispatcher so
// every subscription callback runs on the UI thread. Header-only on purpose:
// the UltraMessage library stays free of the widget layer (like UltraDatabase
// and UltraVault), and only programs that already link UltraCanvas include
// this file.
//
//   #include <UltraMessage/UltraMessageUltraCanvas.h>
//   UltraMsg_UseUltraCanvasApplication();   // once, after the application exists
//
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessage.h"
#include "../UltraCanvasApplication.h"

#include <utility>

inline void UltraMsg_UseUltraCanvasApplication() {
    UltraMsg_SetUIDispatcher([](std::function<void()> task) {
        if (auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread(std::move(task));
        } else if (task) {
            task();
        }
    });
}
