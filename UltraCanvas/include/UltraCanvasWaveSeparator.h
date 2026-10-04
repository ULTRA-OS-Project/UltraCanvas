// include/UltraCanvasWaveSeparator.h
// The S-curve transition between two groups on one bar: a desktop taskbar's
// system group flowing into its running-apps group, a side panel's organiser
// flowing into its info panel. One group's colour fills the part of the
// separator on its side of the curve, the next group's colour the rest, so
// the groups read as carved into one bar rather than boxed side by side.
//
//   auto wave = std::make_shared<UltraCanvasWaveSeparator>("Taskbar.Wave1",
//                   /*verticalBar=*/true);
//   wave->SetColors(barColor, insetColor);   // group before, group after
//   bar->AddChild(wave);                     // between the two groups
//
// On a vertical bar the separator spans the bar's width and takes `length`
// px of its height; on a horizontal bar the other way round. It is a flex
// child like UltraCanvasSeparator, only shaped: it takes no events and
// publishes its own size, so a flex container needs nothing more than the
// AddChild.
// Version: 1.0.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"

namespace UltraCanvas {

    class UltraCanvasWaveSeparator : public UltraCanvasUIElement {
    public:
        // `verticalBar`: the bar the separator sits in runs top to bottom, so
        // the transition is from the group above to the group below. `length`
        // is how much of the bar's main axis the curve takes; the cross axis
        // stretches to the bar.
        UltraCanvasWaveSeparator(const std::string& identifier, bool verticalBar,
                                 float length = 36.0f);

        // The colour of the group before the separator (above on a vertical
        // bar, left on a horizontal one) and of the group after it.
        void SetColors(const Color& before, const Color& after);
        const Color& GetBeforeColor() const { return beforeColor; }
        const Color& GetAfterColor() const { return afterColor; }

        // Mirror the curve: on a vertical bar the S then runs from the right
        // edge at the top to the left edge at the bottom. Purely visual; the
        // two groups keep their sides. A bar that alternates the flip on its
        // separators reads as one continuous wave.
        void SetFlipped(bool flip);
        bool IsFlipped() const { return flipped; }

        void SetVerticalBar(bool vertical);
        bool IsVerticalBar() const { return verticalBar; }

        void SetLength(float px);
        float GetLength() const { return length; }

        // Decoration only: never hit-tested, never focused.
        bool Contains(const Point2Df& /*point*/) override { return false; }
        bool AcceptsFocus() const override { return false; }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        void ApplySize();

        bool  verticalBar;
        bool  flipped = false;
        float length;
        Color beforeColor;
        Color afterColor;
    };

    inline std::shared_ptr<UltraCanvasWaveSeparator> CreateWaveSeparator(
            const std::string& identifier, bool verticalBar,
            const Color& before, const Color& after, float length = 36.0f) {
        auto wave = std::make_shared<UltraCanvasWaveSeparator>(identifier, verticalBar, length);
        wave->SetColors(before, after);
        return wave;
    }

} // namespace UltraCanvas
