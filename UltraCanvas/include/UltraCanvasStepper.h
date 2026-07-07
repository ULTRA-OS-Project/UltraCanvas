// include/UltraCanvasStepper.h
// Stepper (a.k.a. wizard / progress steps): a sequence of numbered or iconed
// steps that shows progress through a multi-step flow (Step 1 → 2 → 3), with
// completed / active / upcoming / error / disabled states and connector lines.
//
// This is the progress-indicator "Stepper" (Material UI Stepper, Ant Design
// Steps). It is distinct from the value −/+ stepper, which ships as
// CreateStepper() on UltraCanvasSpinner.
//
// Design choices are exposed as configuration:
//   * Orientation      — Horizontal or Vertical
//   * Labels           — marker-only, or title (+ optional description)
//   * Marker style     — NumberedCircle, IconCheck, Dot, or Custom (SVG/image)
//   * Navigation       — Linear, NonLinear, or Display-only
//
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {

// ===== ENUMS =====
    enum class StepperOrientation {
        Horizontal,   // markers left-to-right, labels below
        Vertical      // markers top-to-bottom, labels to the right
    };

    enum class StepMarkerStyle {
        NumberedCircle,   // circle with the step number (check when completed)
        IconCheck,        // check / number / error glyph per state
        Dot,              // minimal filled dot (no number)
        Custom            // user-supplied vector/image icon per step
    };

    enum class StepperNavigation {
        Linear,      // only completed/active steps are clickable (go back); advance via NextStep()
        NonLinear,   // any enabled step is clickable
        Display      // not interactive — a pure progress indicator
    };

    // Derived (or explicitly forced) state of a single step.
    enum class StepState {
        Upcoming,
        Active,
        Completed,
        Error,
        Disabled
    };

// ===== STEP ITEM =====
    struct StepItem {
        std::string title;
        std::string description;
        bool error = false;       // force the Error state
        bool disabled = false;    // force the Disabled state (not clickable)

        // Optional custom marker (used when markerStyle == Custom). One image is
        // used for all states; states are conveyed by the connector/label colours.
        std::string iconPath;
        std::shared_ptr<UCImage> iconImage;

        StepItem() = default;
        StepItem(const std::string& t, const std::string& d = "") : title(t), description(d) {}
    };

// ===== STEPPER STYLE =====
    struct StepperStyle {
        // Marker fills / borders per state
        Color completedColor = Colors::Selection;         // 0,120,215
        Color activeColor    = Colors::Selection;
        Color upcomingColor  = Color(200, 200, 206, 255);
        Color errorColor     = Color(220, 53, 69, 255);
        Color disabledColor  = Color(225, 225, 228, 255);
        Color markerBackground = Colors::White;           // upcoming marker interior

        // Glyph / number colours
        Color completedGlyphColor = Colors::White;
        Color activeGlyphColor    = Colors::White;
        Color upcomingGlyphColor  = Color(140, 140, 146, 255);
        Color errorGlyphColor     = Colors::White;

        // Connectors
        Color connectorColor          = Color(210, 210, 214, 255);
        Color connectorCompletedColor = Colors::Selection;

        // Labels
        Color titleColor        = Color(40, 40, 44, 255);
        Color activeTitleColor  = Colors::Selection;
        Color descriptionColor  = Color(130, 130, 136, 255);
        Color disabledTextColor = Color(180, 180, 186, 255);

        // Metrics
        float markerSize        = 30.0f;
        float connectorThickness = 2.0f;
        float edgePadding       = 4.0f;   // gap before first / after last marker
        float labelGap          = 8.0f;   // marker → label spacing
        float titleFontSize     = 13.0f;
        float descriptionFontSize = 11.0f;
        bool  completedShowsCheck = true; // completed marker shows a check (else the number)

        std::string fontFamily;
    };

// ===== STEPPER COMPONENT =====
    class UltraCanvasStepper : public UltraCanvasUIElement {
    private:
        std::vector<StepItem> steps;
        int currentStep = 0;

        StepperOrientation orientation = StepperOrientation::Horizontal;
        StepMarkerStyle    markerStyle = StepMarkerStyle::NumberedCircle;
        StepperNavigation  navigation  = StepperNavigation::Linear;
        bool showLabels = true;

        StepperStyle style;

        int hoveredStep = -1;

    public:
        // ===== CONSTRUCTORS (REQUIRED PATTERN) =====
        UltraCanvasStepper(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasStepper(const std::string& identifier, float w, float h)
            : UltraCanvasStepper(identifier, -1, -1, w, h) {}
        explicit UltraCanvasStepper(const std::string& identifier)
            : UltraCanvasStepper(identifier, -1, -1, -1, -1) {}

        // ===== STEPS =====
        int  AddStep(const std::string& title, const std::string& description = "");
        void SetSteps(const std::vector<StepItem>& newSteps);
        void ClearSteps();
        int  GetStepCount() const { return static_cast<int>(steps.size()); }
        StepItem* GetStep(int index);

        void SetStepError(int index, bool error);
        void SetStepDisabled(int index, bool disabled);
        void SetStepIcon(int index, const std::string& iconPath);

        StepState GetStepState(int index) const;

        // ===== CURRENT STEP =====
        void SetCurrentStep(int index, bool runCallback = false);
        int  GetCurrentStep() const { return currentStep; }
        bool NextStep(bool runCallback = true);   // advance to next enabled step
        bool PrevStep(bool runCallback = true);   // go back to previous enabled step
        bool IsComplete() const;                  // current is the last step and none error

        // ===== DESIGN CHOICES =====
        void SetOrientation(StepperOrientation o) { orientation = o; RequestRedraw(); }
        StepperOrientation GetOrientation() const { return orientation; }

        void SetMarkerStyle(StepMarkerStyle s) { markerStyle = s; RequestRedraw(); }
        StepMarkerStyle GetMarkerStyle() const { return markerStyle; }

        void SetNavigation(StepperNavigation n) { navigation = n; RequestRedraw(); }
        StepperNavigation GetNavigation() const { return navigation; }

        void SetShowLabels(bool s) { showLabels = s; RequestRedraw(); }
        bool GetShowLabels() const { return showLabels; }

        StepperStyle& GetStyle() { return style; }
        const StepperStyle& GetStyle() const { return style; }
        void SetStyle(const StepperStyle& s) { style = s; RequestRedraw(); }

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return navigation != StepperNavigation::Display; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(int)> onStepChanged;       // new current step
        std::function<void(int)> onStepClicked;       // a step marker/label was clicked

    private:
        bool IsClickable(int index) const;

        // Layout: marker square rects + clickable slot rects (element-local).
        void ComputeLayout(std::vector<Rect2Df>& markerRects,
                           std::vector<Rect2Df>& slotRects) const;

        void RenderConnector(IRenderContext* ctx, const Rect2Df& a, const Rect2Df& b,
                             bool completed);
        void RenderMarker(IRenderContext* ctx, const Rect2Df& rect, int index);
        void RenderLabel(IRenderContext* ctx, const Rect2Df& markerRect,
                         const Rect2Df& slotRect, int index);

        int HitTest(const Point2Di& localPos) const;
        bool HandleKeyDown(const UCEvent& event);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasStepper> CreateStepper(
            const std::string& identifier, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasStepper>(identifier, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasStepper> CreateStepperWizard(
            const std::string& identifier, float x, float y, float w, float h,
            const std::vector<std::string>& titles) {
        auto s = std::make_shared<UltraCanvasStepper>(identifier, x, y, w, h);
        for (const auto& t : titles) s->AddStep(t);
        return s;
    }

    inline std::shared_ptr<UltraCanvasStepper> CreateVerticalStepper(
            const std::string& identifier, float x, float y, float w, float h) {
        auto s = std::make_shared<UltraCanvasStepper>(identifier, x, y, w, h);
        s->SetOrientation(StepperOrientation::Vertical);
        return s;
    }

} // namespace UltraCanvas
