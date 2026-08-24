// include/UltraCanvasProgressDialog.h
// Modal progress window for a long operation: a circular progress ring
// (UltraCanvasCircularProgressChart) showing the percentage in its centre, a
// caption line above it, a detail line under it for what is being worked on
// right now, and a Cancel button.
//
// The dialog does not run the work and does not block: the caller keeps the
// operation on its own thread (or splits it across timer ticks) and pushes the
// numbers in from the UI thread as they arrive.
//
//   auto dlg = UltraCanvasProgressDialog::Show(window, "Compressing",
//                                              "Creating photos.zip", onCancel);
//   dlg->SetProgress(0.42);              // 42 %
//   dlg->SetDetail("DSC_0042.jpg");
//   dlg->Close();
//
// A negative fraction means "no total known": the ring shows a busy sweep and
// the centre reads "…" instead of a percentage.
//
// Version: 1.0.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    class UltraCanvasWindowBase;
    class UltraCanvasModalDialog;
    class UltraCanvasCircularProgressChart;

    class UltraCanvasProgressDialog
            : public std::enable_shared_from_this<UltraCanvasProgressDialog> {
    public:
        // Opens the window over `parent`. `onCancel` is called on the UI thread
        // when the user presses Cancel (or closes the window); the caller is
        // responsible for actually stopping its work. Returns nullptr when no
        // dialog could be created (dialogs disabled / headless) — callers must
        // handle that and simply run without a progress window.
        static std::shared_ptr<UltraCanvasProgressDialog> Show(
                UltraCanvasWindowBase* parent,
                const std::string& title,
                const std::string& caption,
                std::function<void()> onCancel = nullptr);

        ~UltraCanvasProgressDialog();

        // 0..1 (clamped); negative = unknown total, shown as a busy ring.
        void SetProgress(double fraction);
        double GetProgress() const { return fraction; }

        void SetCaption(const std::string& text);
        // What is being worked on now (a file name). Long text is ellipsized so
        // the window keeps the height it was opened with.
        void SetDetail(const std::string& text);

        // True once the user asked to cancel (the same moment onCancel fired).
        bool IsCancelled() const { return cancelled; }

        // Take the window down. Safe to call more than once, and safe after the
        // user already closed it.
        void Close();

    private:
        UltraCanvasProgressDialog() = default;
        void UpdateText();

        std::shared_ptr<UltraCanvasModalDialog> dialog;
        std::shared_ptr<UltraCanvasCircularProgressChart> ring;
        std::string caption;
        std::string detail;
        double fraction = 0.0;
        bool cancelled = false;
        bool closed = false;
        std::function<void()> onCancel;
    };

} // namespace UltraCanvas
