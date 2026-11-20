// include/UltraCanvasProgressDialog.h
// Modal progress window for a long operation: a circular progress ring
// (UltraCanvasCircularProgressChart) with the percentage in its centre; beside it the operation's headline, where it goes from and to
// (two chips), and what is being worked on right now; under them, for a
// transfer, a strip of figures - speed, time left, items, size; and Cancel,
// with Pause / Resume where the work can wait.
//
// The dialog does not run the work and does not block: the caller keeps the
// operation on its own thread (or splits it across timer ticks) and pushes the
// numbers in from the UI thread as they arrive. It is built to stay cheap
// however often they arrive: the ring follows every SetProgress, the texts
// change at most four times a second, and speed and time left are worked out
// here from the numbers handed in - the caller never counts anything for the
// window's sake.
//
// doc-check: std::function<void()> onCancel;
// doc-check: example
//   auto dlg = UltraCanvasProgressDialog::Show(window.get(), "Compressing",
//                                              "Creating photos.zip", onCancel);
//   dlg->SetProgress(0.42);              // 42 %
//   dlg->SetDetail("DSC_0042.jpg");
//   dlg->Close();
//
// A negative fraction means "no total known": the ring shows a busy sweep and
// the centre reads "…" instead of a percentage.
//
// The window carries no severity badge by default: the ring is the dialog's
// graphic, and an icon column to its left would push it off centre. Pass
// showIcon = true to get the classic Information badge back.
//
// Version: 2.1.0 - ProgressDialogOptions::onBackground: a "Run in background" button that closes the
//                  window without cancelling (IsInBackground); startPaused, for a window opened again
//                  on paused work
// Version: 2.0.0 - the transfer layout: route chips, a strip of figures (speed, time left, items,
//                  size; ProgressDialogOptions / SetFigures), Pause / Resume; texts at most four times a second, as plain labels
// Version: 1.1.0 - the ring, its track and the percentage take the theme's colours, and again on a
//                  theme change while the window is open (the percentage was dark grey on a dark dialog)
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    class UltraCanvasWindowBase;
    class UltraCanvasModalDialog;
    class UltraCanvasCircularProgressChart;
    class UltraCanvasContainer;
    class UltraCanvasLabel;
    class UltraCanvasButton;

    // What the window shows besides the ring, fixed when it opens so its
    // height never changes while it is up.
    struct ProgressDialogOptions {
        bool showIcon = false;        // the Information badge left of the text
        // Where the work goes: two chips, "ULTRA OS" -> "Projekte". `to`
        // empty: the `from` chip alone (a delete). Both empty: no chips, and
        // the caption is the headline, as before.
        std::string from;
        std::string to;
        // The strip of figures under the ring: speed and time left, and an
        // Items cell (`items`) and a Size cell (`bytes`) as the work counts
        // them. Speed is in bytes per second with `bytes`, else items per
        // second.
        bool figures = false;
        bool items = false;
        bool bytes = false;
        // A Pause / Resume button; called with true to pause, false to go on.
        // The caller holds its work between files (or blocks) while paused.
        std::function<void(bool paused)> onPause;
        // The work is paused already (a window opened again for it): the
        // button reads Resume from the start.
        bool startPaused = false;
        // A "Run in background" button: the window closes and the work goes
        // on - not a cancel; the caller shows its progress elsewhere (a
        // status bar) from then on, and may open a new window later.
        std::function<void()> onBackground;
    };

    // The counts behind the strip, as far as the work knows them. A total of
    // 0 is "not known": the cell shows what is done alone ("26,731 items").
    struct ProgressFigures {
        uint64_t doneItems = 0;
        uint64_t totalItems = 0;
        uint64_t doneBytes = 0;
        uint64_t totalBytes = 0;
    };

    class UltraCanvasProgressDialog
            : public std::enable_shared_from_this<UltraCanvasProgressDialog> {
    public:
        // Opens the window over `parent`. `onCancel` is called on the UI thread
        // when the user presses Cancel (or closes the window); the caller is
        // responsible for actually stopping its work. Returns nullptr when no
        // dialog could be created (dialogs disabled / headless) — callers must
        // handle that and simply run without a progress window.
        // `showIcon` adds the coloured Information badge on the left of the
        // caption; with it off (the default) the ring is centred in the window.
        static std::shared_ptr<UltraCanvasProgressDialog> Show(
                UltraCanvasWindowBase* parent,
                const std::string& title,
                const std::string& caption,
                std::function<void()> onCancel = nullptr,
                bool showIcon = false);
        // The same with the route, the strip of figures and Pause (above).
        // With a route the window's title is the headline and `caption` is
        // not shown.
        static std::shared_ptr<UltraCanvasProgressDialog> Show(
                UltraCanvasWindowBase* parent,
                const std::string& title,
                const std::string& caption,
                std::function<void()> onCancel,
                const ProgressDialogOptions& options);

        ~UltraCanvasProgressDialog();

        // 0..1 (clamped); negative = unknown total, shown as a busy ring.
        void SetProgress(double fraction);
        double GetProgress() const { return fraction; }

        // The counts for the strip; cheap to call on every tick.
        void SetFigures(const ProgressFigures& figures);

        void SetCaption(const std::string& text);
        // What is being worked on now (a file name). Long text is ellipsized so
        // the window keeps the height it was opened with.
        void SetDetail(const std::string& text);

        // True once the user asked to cancel (the same moment onCancel fired).
        bool IsCancelled() const { return cancelled; }
        // True while Pause is pressed.
        bool IsPaused() const { return paused; }
        // True once "Run in background" closed the window.
        bool IsInBackground() const { return backgrounded; }

        // Take the window down. Safe to call more than once, and safe after the
        // user already closed it.
        void Close();

        // "9 s", "2 min", "1 h 5 min" - a span of time as the window shows it.
        static std::string FormatDuration(double seconds);
        // "742 MB", "1.2 GB" - a byte count as the window shows it.
        static std::string FormatBytes(uint64_t bytes);

    private:
        UltraCanvasProgressDialog() = default;
        void BuildContent(const std::string& title, const std::string& caption,
                          const ProgressDialogOptions& options);
        // The texts from the numbers held: now, or when `force` is false at
        // most every kTextRefreshMs.
        void RefreshTexts(bool force);
        // Speed (units per second) over the recent samples; 0 while there are
        // too few.
        double RecentRate(int which) const;
        void TogglePause();
        // The colours the theme gives the ring, the chips and the strip.
        void ApplyThemeColors();

        std::shared_ptr<UltraCanvasModalDialog> dialog;
        std::shared_ptr<UltraCanvasCircularProgressChart> ring;
        std::shared_ptr<UltraCanvasLabel> headlineLabel;
        std::shared_ptr<UltraCanvasLabel> detailLabel;
        // The route: each chip a box with its name in it, and the arrow.
        std::shared_ptr<UltraCanvasContainer> fromChip;
        std::shared_ptr<UltraCanvasContainer> toChip;
        std::shared_ptr<UltraCanvasLabel> fromChipText;
        std::shared_ptr<UltraCanvasLabel> toChipText;
        std::shared_ptr<UltraCanvasContainer> strip;
        std::shared_ptr<UltraCanvasContainer> stripCells[4];   // speed, time left, items, size
        std::shared_ptr<UltraCanvasLabel> stripValues[4];
        std::shared_ptr<UltraCanvasButton> pauseButton;
        std::shared_ptr<UltraCanvasButton> backgroundButton;
        bool backgrounded = false;
        void SendToBackground();

        ProgressDialogOptions options;
        std::string caption;
        std::string detail;
        bool routeShown = false;
        double fraction = 0.0;
        ProgressFigures figures;
        bool cancelled = false;
        bool paused = false;
        bool closed = false;
        std::function<void()> onCancel;
        int themeHandler = 0;   // UltraCanvasTheme change handler: ApplyThemeColors

        // Recent readings for speed and time left, a few seconds' worth.
        struct Sample {
            std::chrono::steady_clock::time_point at;
            double fraction;
            uint64_t bytes;
            uint64_t items;
        };
        std::deque<Sample> samples;
        std::chrono::steady_clock::time_point lastTextRefresh{};
        static constexpr int kTextRefreshMs = 250;
    };

} // namespace UltraCanvas
