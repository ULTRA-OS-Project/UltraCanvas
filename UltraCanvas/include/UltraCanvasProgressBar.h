// include/UltraCanvasProgressBar.h
// A horizontal progress bar: a track, a fill, and nothing else.
//
// The inline counterpart of UltraCanvasProgressDialog, which opens a window
// with a ring and a Cancel button. This one is a child element sized by the
// layout, for progress that belongs beside the work rather than over it - a
// status bar, a row in a list, a panel footer. It takes no input and has no
// text of its own: the caption belongs to the label next to it, which can say
// far more than a number fitted inside a bar ever could.
//
//   auto bar = CreateProgressBar("transfer", 160, 6);
//   bar->SetFraction(0.42);     // 42 %
//   bar->SetFraction(-1.0);     // working, no total known: a busy sweep
//   bar->SetVisible(false);     // nothing running
//
// A negative fraction is "something is happening, nobody knows how much of
// it" - an FTP server that sends no length, a queue still being counted. It
// is drawn as a block sliding along the track, moved by SetFraction(-1) being
// called again rather than by a timer of its own: this element owns no clock,
// so a caller that stops reporting simply leaves the bar where it stood
// instead of animating for ever over work that has died.
// Version: 1.0.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <memory>
#include <string>

namespace UltraCanvas {

    struct ProgressBarStyle {
        Color trackColor  = Color(216, 218, 224, 255);
        Color fillColor   = Color(37, 99, 235, 255);     // the accent blue
        Color borderColor = Colors::Transparent;
        float borderWidth = 0.0f;
        // Fully rounded ends by default: the radius is clamped to half the
        // bar's height, so one value suits a 4 px bar and a 20 px one.
        float cornerRadius = 999.0f;
        // How much of the track the sliding block covers while the total is
        // unknown, and how far it moves per report.
        float busyBlockFraction = 0.30f;
        float busyStepFraction  = 0.07f;
    };

    class UltraCanvasProgressBar : public UltraCanvasUIElement {
    private:
        // 0..1, or negative for "no total known".
        float fraction = 0.0f;
        // Where the busy block sits, 0..1 of the track. Advanced by each
        // SetFraction with a negative value.
        float busyOffset = 0.0f;
        ProgressBarStyle style;

    public:
        UltraCanvasProgressBar(const std::string& identifier, float x, float y,
                               float w, float h)
                : UltraCanvasUIElement(identifier, x, y, w, h) {}

        UltraCanvasProgressBar(const std::string& identifier, float w, float h)
                : UltraCanvasProgressBar(identifier, 0, 0, w, h) {}

        // 0..1, clamped. Negative means the total is unknown and advances the
        // busy block one step.
        void SetFraction(float value) {
            if (value < 0.0f) {
                fraction = -1.0f;
                busyOffset += style.busyStepFraction;
                if (busyOffset > 1.0f) busyOffset -= 1.0f;
            } else {
                fraction = std::min(1.0f, value);
                busyOffset = 0.0f;
            }
            RequestRedraw();
        }

        // The same thing said in the units the caller already has. A zero or
        // unknown total is the busy case rather than a division by zero.
        void SetProgress(uint64_t done, uint64_t total) {
            if (total == 0) { SetFraction(-1.0f); return; }
            SetFraction(static_cast<float>(static_cast<double>(done) /
                                           static_cast<double>(total)));
        }

        float GetFraction() const { return fraction; }
        bool IsIndeterminate() const { return fraction < 0.0f; }

        void SetStyle(const ProgressBarStyle& s) { style = s; RequestRedraw(); }
        const ProgressBarStyle& GetStyle() const { return style; }

        // A bar reports; it is not a control. Nothing to click, so it never
        // takes the pointer from whatever it sits on.
        bool Contains(const Point2Df& /*point*/) override { return false; }

        void Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) override {
            if (!ctx || !IsVisible()) return;
            const Rect2Df box = GetLocalBounds();
            if (box.width <= 0.0f || box.height <= 0.0f) return;

            // One value for the radius whatever the height: a 6 px bar gets
            // 3 px ends, a 20 px one gets 10, and neither has to be told.
            const float radius = std::min(style.cornerRadius, box.height * 0.5f);

            ctx->DrawFilledRectangle(Rect2Dd(box.x, box.y, box.width, box.height),
                                     style.trackColor, style.borderWidth,
                                     style.borderColor, radius);

            float fillX = box.x;
            float fillW = 0.0f;
            if (fraction < 0.0f) {
                // The busy block, clipped to the track at both ends rather
                // than wrapped around: a block that reappears on the left
                // while its tail is still on the right reads as two blocks.
                fillW = box.width * style.busyBlockFraction;
                fillX = box.x + busyOffset * box.width;
                if (fillX + fillW > box.x + box.width)
                    fillW = (box.x + box.width) - fillX;
            } else {
                fillW = box.width * fraction;
            }
            if (fillW <= 0.0f) return;
            // Below the corner radius there is no room to round anything; a
            // sliver drawn with a radius wider than itself bulges.
            const float fillRadius = std::min(radius, fillW * 0.5f);
            ctx->DrawFilledRectangle(Rect2Dd(fillX, box.y, fillW, box.height),
                                     style.fillColor, 0.0f, Colors::Transparent,
                                     fillRadius);
        }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasProgressBar>
    CreateProgressBar(const std::string& identifier, float x, float y,
                      float w, float h) {
        return std::make_shared<UltraCanvasProgressBar>(identifier, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasProgressBar>
    CreateProgressBar(const std::string& identifier, float w, float h) {
        return std::make_shared<UltraCanvasProgressBar>(identifier, 0, 0, w, h);
    }

} // namespace UltraCanvas
