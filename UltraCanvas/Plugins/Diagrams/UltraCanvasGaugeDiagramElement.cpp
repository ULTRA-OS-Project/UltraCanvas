// Plugins/Diagrams/UltraCanvasGaugeDiagramElement.cpp
// Implementation of comprehensive gauge element with 17 visual modes
// Version: 2.9.1
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
// V2.9.1 changelog: Round-gauge (CircularRing) fixes:
//   - Ring radius reduced by 5% (applied after the size clamp, so even
//     clamp-limited gauges like the playground ring shrink).
//   - Rounded segment caps no longer spill outside their segment slot: arc
//     endpoints are inset by the cap overhang (thickness/2 + border), so
//     chunky SegmentedRing segments and rounded Blocks stop overlapping
//     their neighbours at large thickness / high segment counts.
// V2.9.0 changelog: Round-gauge (CircularRing) enhancements:
//   - Configurable ring start/null angle (SetRingStartAngleDeg) so the zero
//     position can sit anywhere (e.g. the bottom of the circle); the indicator
//     still fills a full 360° clockwise from there.
//   - Value-driven indicator colour bands (SetRingValueColorBands): green when
//     high, fading through yellow then orange to red when low.
//   - Dots indicator style now draws a small border around each dot.
//   - CircularRing reclaims the dial-mode top/bottom padding (it has no external
//     tick labels), letting the ring roughly double in compact cards.
// V2.7.1 changelog: Fixed CircularRing (round gauge) sizing — the value is drawn
//   in the centre of the ring, so the layout no longer reserves a value/unit zone
//   below it (and trims tick clearance), letting the ring grow to fill its card
//   (~2x larger). The centre value text now scales to the inner ring diameter and
//   shrinks to fit, so it never spills outside the ring on small gauges.
// V2.7.0 changelog: Round-gauge (CircularRing) style system for the new "Round
//   Gauges" demo tab. Adds configurable ring thickness, indicator style
//   (SolidArc / Segmented / Dashed), segment shape (Bars / Dots / Blocks),
//   liquid disc fill (NoFill / StraightLevel / WavedLevel) and a track colour.
//   RenderCircularRing rewritten around DrawRingTrackAndValue + DrawRingLiquidFill.
// V2.6.3 changelog: Speedometer value display normalized to match other cards
//   - Removed the dark LCD box + "0" inside the disc.
//   - Value + unit now render below the disc in the standard card style
//     (large grey number + small unit), like Sales/Wind cards.
// V2.6.2 changelog: Speedometer display tweaks per design request
//   - Tick numeric labels smaller (font 12→10).
//   - LCD value box moved lower (0.46→0.58 radius) so it sits below the disc.
//   - Odometer "km:" strip removed entirely.
// V2.6.1 changelog: Speedometer dark-look refinements (Option B)
//   - Removed sub-dial from the Speedometer disc (shared DrawSubDial is a light
//     180° semicircle that reads poorly on black; other modes still use it).
//   - Brighter near-white amber ticks at top, deeper orange toward bottom.
//   - Numeric labels pulled inward (radius-32) + smaller font (12) to stop the
//     120/160 (top) and 0/240 (bottom) labels from crowding.
//   - LCD value box nudged lower (0.40→0.46 radius) to clear the 0/240 labels.
// V2.6.0 changelog: Speedometer dark automotive redesign (Option A)
//   - RenderSpeedometer fully rewritten as a self-contained DARK disc that floats
//     inside the existing LIGHT card (card bgColor/textColor untouched).
//   - Glossy black bezel via 3 concentric solid discs + top rim highlight.
//   - Graduated tick marks: per-tick color interpolated by vertical position
//     (bright amber/white at top, deep orange toward 0/240) — mimics the
//     reference photo's warm gradient without needing gradient primitives.
//   - White numeric labels moved INSIDE the tick ring.
//   - Tapered orange needle (quad with short back-tail) + orange glow underlay,
//     white-ringed center hub.
//   - Dark LCD value box with bold orange digits + odometer strip ("km: ...")
//     placed in the lower disc, replacing the old below-card value/unit text.
//   - Inner sub-dial (MPH/RPM) recentred (offsetYRatio -0.06) and slightly larger.
//   - Uses only primitives already proven in this file (FillCircle, Fill/Draw-
//     RoundedRectangle, strokes, triangles); no header/registry changes.
// V2.5.2 changelog: Speed gauge size fix
//   - Realization: the Speedometer gauge was being made TOO SMALL by aggressive
//     tickLabelClearance (50). Wind Direction (Compass) uses only 36 and looks great
//     with proper size. Reverted Speedometer to base clearance (20) so the gauge
//     uses the full available space, like Wind Direction does.
//   - Title is still pushed up via kPaddingTop=32 from V2.5.1, providing
//     breathing room from the card top.
// V2.5.1 changelog: Speed card breathing room (kPaddingTop 26->32, clearance arch fix)
// V2.5.0 changelog: RADICAL sub-dial redesign — text moved OUTSIDE the circle
// V2.4 changelog: Fixed 6 residual visual problems:
//   1) Speed sub-dial overlap with tick "30" - reduced sub-dial size and repositioned
//   2,4,7) Title overlap with tick labels - added extraTopClearance for tick label modes
//   3) Sales range labels overlap with tick numbers - moved range labels inward + smaller font
//   5) Stopwatch sub-dial numbers cluttered - simplified to only 0 and 30 at extremes
//   6) Stopwatch digital readout crossed by needle - moved readout above the gauge proper
//   8) Ring 75/% overlap - max separation: value at -14, unit at +28 with bigger gap
// V2.3 changelog: Fixed 5 residual visual problems (tick labels invading title,
// value/unit overlap in circular gauges, Stopwatch sub-dial empty, Ring value/% overlap,
// LED Display title cut off).

#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// LAYOUT CONSTANTS (V2.1: tuned for proper title/value/unit spacing)
// =============================================================================

namespace {
    constexpr float kTitleHeight   = 30.0f;   // V2.3: Increased from 22 to clear tick labels invading from top
    constexpr float kValueHeight   = 28.0f;   // V2.3: Slightly increased
    constexpr float kUnitHeight    = 18.0f;
    constexpr float kValueUnitGap  = 14.0f;   // V2.3: Increased from 4 to give real separation
    constexpr float kPaddingSide   = 12.0f;
    constexpr float kPaddingTop    = 32.0f;
    constexpr float kTitleRaise    = 34.0f;   // V2.8: title raised a further 12px (was 22) to free room for the larger gauge
    constexpr float kPaddingBottom = 22.0f;
    constexpr float kGaugeScale    = 1.30f;   // V2.8: enlarge round example gauges by 30%
    constexpr float kRingShrink    = 0.95f;   // V2.9.1: round (CircularRing) gauges 5% smaller
}

// =============================================================================
// CONSTRUCTOR
// =============================================================================

UltraCanvasGaugeDiagramElement::UltraCanvasGaugeDiagramElement(
    const std::string& id, float x, float y, float width, float height)
    : UltraCanvasUIElement(id, x, y,width, height) {
}

UltraCanvasGaugeDiagramElement::~UltraCanvasGaugeDiagramElement() {
    StopwatchStop();
    auto app = UltraCanvasApplication::GetInstance();

    if (clockTimerId) {
        app->StopTimer(clockTimerId);
        clockTimerId = 0;
    }
}

// =============================================================================
// PUBLIC SETTERS
// =============================================================================

void UltraCanvasGaugeDiagramElement::SetMode(GaugeMode m) {
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        // Stop stopwatch timer if leaving Stopwatch mode
        if (mode == GaugeMode::Stopwatch && m != GaugeMode::Stopwatch && stopwatchTimerId) {
            app->StopTimer(stopwatchTimerId);
            stopwatchTimerId = 0;
        }
    }
    // Reset stopwatch state when entering Stopwatch mode
    if (m == GaugeMode::Stopwatch) {
        stopwatchRunning = false;
        stopwatchAccumulated = 0.0f;
    }
    mode = m;
    if (m == GaugeMode::Speedometer || m == GaugeMode::Stopwatch) {
        arcStartDeg = 135.0;
        arcEndDeg = 405.0;
    } else if (m == GaugeMode::Semicircular) {
        arcStartDeg = 180.0;
        arcEndDeg = 360.0;
    } else if (m == GaugeMode::Quadrant) {
        arcStartDeg = 180.0;
        arcEndDeg = 270.0;
    } else if (m == GaugeMode::Compass) {
        arcStartDeg = 0.0;
        arcEndDeg = 360.0;
    }
    // Start/stop the 1-second redraw timer for live displays.
    UpdateClockTimer();
    RequestRedraw();
}

// Starts the shared 1-second timer when a live display is active (AnalogClock,
// or Digital in clock mode) and stops it otherwise.
void UltraCanvasGaugeDiagramElement::UpdateClockTimer() {
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    bool needTimer = (mode == GaugeMode::AnalogClock) ||
                     (mode == GaugeMode::Digital && digitalClock);
    if (needTimer && !clockTimerId) {
        clockTimerId = app->StartTimer(1000, true, [this](TimerId) {
            if (IsVisible()) RequestRedraw();
        });
    } else if (!needTimer && clockTimerId) {
        app->StopTimer(clockTimerId);
        clockTimerId = 0;
    }
}

void UltraCanvasGaugeDiagramElement::SetValue(double val) {
    double clamped = std::max(minValue, std::min(maxValue, val));
    if (std::abs(clamped - currentValue) < 0.0001) return;
    currentValue = clamped;
    if (onGaugeValueChange) onGaugeValueChange(currentValue);
    RequestRedraw();
}

void UltraCanvasGaugeDiagramElement::StopwatchStart() {
    if (stopwatchRunning || mode != GaugeMode::Stopwatch) return;
    stopwatchRunning = true;
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        stopwatchTimerId = app->StartTimer(16, true, [this](TimerId) {
            if (!stopwatchRunning) return;
            stopwatchAccumulated += 0.016f;
            SetValue(stopwatchAccumulated);
            if (IsVisible()) RequestRedraw();
        });
    }
}

void UltraCanvasGaugeDiagramElement::StopwatchStop() {
    stopwatchRunning = false;
    if (stopwatchTimerId) {
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            app->StopTimer(stopwatchTimerId);
            stopwatchTimerId = 0;
        }
    }
}

void UltraCanvasGaugeDiagramElement::StopwatchReset() {
    StopwatchStop();
    stopwatchAccumulated = 0.0f;
    SetValue(0.0);
}

void UltraCanvasGaugeDiagramElement::SetMinValue(double v) { minValue = v; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetMaxValue(double v) { maxValue = v; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetTitle(const std::string& t) { title = t; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetUnit(const std::string& u) { unit = u; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetNeedleStyle(GaugeNeedleStyle s) { needleStyle = s; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetTickPosition(GaugeTickPosition p) { tickPos = p; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetOrientation(GaugeOrientation o) { orientation = o; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetBatteryStyle(GaugeBatteryStyle s) { batteryStyle = s; RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::SetRingThickness(float t) { ringThickness = std::max(1.0f, t); RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingStyle(GaugeRingStyle s) { ringStyle = s; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingSegmentStyle(GaugeRingSegmentStyle s) { ringSegmentStyle = s; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingSegmentCount(int count) { ringSegmentCount = std::max(4, count); RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingSegmentRounded(bool rounded) { ringSegmentRounded = rounded; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingBorder(bool enabled) { ringBorder = enabled; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingBorderColor(const Color& c) { ringBorderColor = c; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetFillStyle(GaugeFillStyle s) { fillStyle = s; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetTrackColor(const Color& c) { trackColor = c; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingFaded(bool faded) { ringFaded = faded; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetFillFaded(bool faded) { fillFaded = faded; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingGradientColors(const std::vector<Color>& colors) {
    ringGradientColors = colors;
    if (ringGradientColors.size() > 100) ringGradientColors.resize(100);  // cap at 100 stops
    RequestRedraw();
}
void UltraCanvasGaugeDiagramElement::SetRingCenterContent(GaugeRingCenterContent c) { ringCenterContent = c; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingCenterLabel(const std::string& label) { ringCenterLabel = label; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingCenterIcon(GaugeRingIcon icon) { ringCenterIcon = icon; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingStartAngleDeg(float deg) { ringStartAngleDeg = deg; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetRingValueColorBands(bool enabled) { ringValueColorBands = enabled; RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::SetArcAngles(double startDeg, double endDeg) {
    arcStartDeg = startDeg;
    arcEndDeg = endDeg;
    RequestRedraw();
}

void UltraCanvasGaugeDiagramElement::SetGaugeColor(const Color& c) { gaugeColor = c; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetBackgroundColor(const Color& c) { bgColor = c; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetTextColor(const Color& c) { textColor = c; RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::AddRange(const GaugeRangeSegment& range) {
    ranges.push_back(range); RequestRedraw();
}
void UltraCanvasGaugeDiagramElement::ClearRanges() { ranges.clear(); RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::AddThreshold(const GaugeThreshold& t) {
    thresholds.push_back(t); RequestRedraw();
}
void UltraCanvasGaugeDiagramElement::ClearThresholds() { thresholds.clear(); RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::AddExternalPointer(const GaugeExternalPointer& p) {
    externalPointers.push_back(p); RequestRedraw();
}
void UltraCanvasGaugeDiagramElement::ClearExternalPointers() { externalPointers.clear(); RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::SetSubDial(const GaugeSubDial& sub) { subDial = sub; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetSubDialValue(double val) {
    subDial.currentValue = std::max(subDial.minValue, std::min(subDial.maxValue, val));
    RequestRedraw();
}
void UltraCanvasGaugeDiagramElement::SetSubDialEnabled(bool en) { subDial.enabled = en; RequestRedraw(); }

void UltraCanvasGaugeDiagramElement::SetDecimalPlaces(int places) { decimalPlaces = std::max(0, places); RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetShowGlow(bool glow) { showGlow = glow; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetShowBolt(bool show) { showBolt = show; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetShowLabels(bool show) { showLabels = show; RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetDigitalClock(bool en) { digitalClock = en; UpdateClockTimer(); RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetDigitalFontFamily(const std::string& family) {
    digitalFontFamily = family.empty() ? "Sans" : family; RequestRedraw();
}
void UltraCanvasGaugeDiagramElement::SetMajorTickCount(int count) { majorTickCount = std::max(1, count); RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetMinorTickCount(int count) { minorTickCount = std::max(0, count); RequestRedraw(); }
void UltraCanvasGaugeDiagramElement::SetSegmentCount(int count) { segmentCount = std::max(2, count); RequestRedraw(); }

// =============================================================================
// HELPER METHODS
// =============================================================================

double UltraCanvasGaugeDiagramElement::ValueToAngle(double val) const {
    double range = maxValue - minValue;
    if (std::abs(range) < 1e-10) return arcStartDeg;
    double ratio = (val - minValue) / range;
    return arcStartDeg + ratio * (arcEndDeg - arcStartDeg);
}

double UltraCanvasGaugeDiagramElement::ValueToRatio(double val) const {
    double range = maxValue - minValue;
    if (std::abs(range) < 1e-10) return 0.0;
    return (val - minValue) / range;
}

Point2Df UltraCanvasGaugeDiagramElement::AngleToPoint(
    double angleDeg, double radius, const Point2Df& center) const {
    double rad = angleDeg * M_PI / 180.0;
    return Point2Df(
        static_cast<float>(center.x + radius * std::cos(rad)),
        static_cast<float>(center.y + radius * std::sin(rad)));
}

// V2.1 FIX: Center now accounts for title at top + value/unit at bottom
// V2.3 FIX: Include kValueUnitGap in bottomSpace so gauge moves up enough
// V2.5.1 FIX: Also subtract tickLabelClearance from the available height so the
//             center shifts DOWN proportionally, keeping the gauge visually centered
//             in the actually-available space (between title-zone and value-zone).
Point2Df UltraCanvasGaugeDiagramElement::GetGaugeCenter() const {
    const auto b = GetLocalBounds();
    float titleSpace = title.empty() ? 0.0f : kTitleHeight;
    // V2.7.1: CircularRing draws the value in the centre of the ring, not in a
    // separate zone below it, so it must not reserve bottomSpace — doing so was
    // shrinking the ring (height-limited) and pushing the value text out of it.
    float bottomSpace = (mode == GaugeMode::CircularRing)
        ? 0.0f
        : kValueHeight + (unit.empty() ? 0.0f : (kUnitHeight + kValueUnitGap));
    // V2.5.1: Match the clearance used in GetGaugeRadius so center is consistent
    // V2.5.2: Speedometer back to base clearance — was being made too small.
    //         Compass uses 36 and looks great with proper size, so Speedometer
    //         (similar geometry) should also stay at base 20.
    float tickLabelClearance = 0.0f;
    if (mode == GaugeMode::Semicircular || mode == GaugeMode::Compass) {
        tickLabelClearance = 36.0f;
    } else if (mode == GaugeMode::CircularRing) {
        tickLabelClearance = 8.0f;  // V2.7.1: keep consistent with GetGaugeRadius
    }
    // V2.9: CircularRing has no external tick labels, so it needs far less top/
    // bottom breathing than the dial modes (kPaddingTop=32 exists to clear the
    // tick numbers the dials draw above the arc). Reclaiming it lets the ring
    // roughly double in the compact showcase cards.
    float padTop    = (mode == GaugeMode::CircularRing) ? 12.0f : kPaddingTop;
    float padBottom = (mode == GaugeMode::CircularRing) ? 10.0f : kPaddingBottom;
    float availH = static_cast<float>(b.height) - titleSpace - bottomSpace
                   - padTop - padBottom - tickLabelClearance;
    // Center the gauge in the available (clearance-adjusted) space, with the clearance
    // half going to the TOP (between title and arc) — this is the side that needs the gap
    float cy = static_cast<float>(b.y) + padTop + titleSpace
               + tickLabelClearance + availH / 2.0f;
    return Point2Df(static_cast<float>(b.x + b.width / 2), cy);
}

// V2.1 FIX: Radius is based on AVAILABLE space (excluding title/value/unit zones)
// V2.3 FIX: Include kValueUnitGap in bottomSpace + bigger safety margin for tick labels
// V2.4 FIX: Per-mode extra clearance for modes with external tick labels — they extend
//           outward from the radius by ~18px, so we need to shrink radius accordingly
float UltraCanvasGaugeDiagramElement::GetGaugeRadius() const {
    const auto b = GetLocalBounds();
    float titleSpace = title.empty() ? 0.0f : kTitleHeight;
    // V2.7.1: CircularRing centres its value inside the ring, so it reserves no
    // bottom value/unit zone — this lets the ring grow to fill the card.
    float bottomSpace = (mode == GaugeMode::CircularRing)
        ? 0.0f
        : kValueHeight + (unit.empty() ? 0.0f : (kUnitHeight + kValueUnitGap));

    // V2.4: Modes that draw tick labels OUTSIDE the arc need extra room
    // V2.4.1: Added Speedometer — its tick labels (120, 150) at the top of the
    //         270° arc were invading the title zone above
    // V2.5.2: Speedometer back to base clearance — was being made too small.
    //         Wind Direction (Compass) uses only 36 and looks great, Speedometer
    //         was using 50 which made the gauge significantly smaller. Title
    //         breathing room is provided by kPaddingTop=32, not by shrinking the gauge.
    float tickLabelClearance = 20.0f;  // base safety
    if (mode == GaugeMode::Semicircular || mode == GaugeMode::Compass) {
        tickLabelClearance = 36.0f;  // labels at radius+18 stick out further
    } else if (mode == GaugeMode::CircularRing) {
        tickLabelClearance = 8.0f;   // V2.7.1: no external labels, only short radial bars
    }

    // V2.9: CircularRing reclaims the dial top/bottom padding (see GetGaugeCenter)
    // and needs only stroke-overhang room on the sides, not the dials' 40px tick-
    // label allowance — so the ring fills the card.
    float padTop    = (mode == GaugeMode::CircularRing) ? 12.0f : kPaddingTop;
    float padBottom = (mode == GaugeMode::CircularRing) ? 10.0f : kPaddingBottom;
    float sideExtra = (mode == GaugeMode::CircularRing) ? 18.0f : 40.0f;
    float availH = static_cast<float>(b.height) - titleSpace - bottomSpace
                   - padTop - padBottom - tickLabelClearance;
    float availW = static_cast<float>(b.width) - 2.0f * kPaddingSide - sideExtra;
    float r = std::min(availW, availH) / 2.0f;

    // V2.8: Enlarge every round example gauge by 30%. The previous size left the
    // segmented-ring centre content (battery icon / "Battery" label) sitting too
    // close to the outer ring, so it overlapped the bottom segments. A bigger ring
    // gives the centred value + icon/label real breathing room inside the circle.
    r *= kGaugeScale;
    // Clamp so the enlarged gauge can grow into the reserved margins but never
    // spills past the card's padding box (width or the title-adjusted height).
    float hardMaxR = std::min(
        (static_cast<float>(b.width)  - 2.0f * kPaddingSide) / 2.0f,
        (static_cast<float>(b.height) - padTop - padBottom - titleSpace) / 2.0f);
    r = std::min(r, hardMaxR);
    // V2.9.1: round gauges 5% smaller — after the clamp, so gauges whose size
    // is limited by hardMaxR (e.g. the playground ring) shrink too.
    if (mode == GaugeMode::CircularRing) r *= kRingShrink;
    return std::max(20.0f, r);
}

Color UltraCanvasGaugeDiagramElement::GetColorForValue(double val) const {
    for (const auto& r : ranges) {
        if (val >= r.startValue && val <= r.endValue) {
            return r.color;
        }
    }
    return gaugeColor;
}

std::string UltraCanvasGaugeDiagramElement::FormatValue(double val) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimalPlaces) << val;
    return oss.str();
}

// =============================================================================
// MAIN RENDER DISPATCH
// =============================================================================

void UltraCanvasGaugeDiagramElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;

    ctx->PushState();

    switch (mode) {
        case GaugeMode::Speedometer:
            RenderSpeedometer(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::Semicircular:
            RenderSemicircular(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::Quadrant:
            RenderQuadrant(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::Compass:
            RenderCompass(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::AnalogClock:
            RenderAnalogClock(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::Stopwatch:
            RenderStopwatch(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::LinearBar:
            RenderLinearBar(ctx);
            break;
        case GaugeMode::LinearLED:
            RenderLinearLED(ctx);
            break;
        case GaugeMode::LinearSegmented:
            RenderLinearSegmented(ctx);
            break;
        case GaugeMode::LinearMultiPointer:
            RenderLinearMultiPointer(ctx);
            break;
        case GaugeMode::LinearWithArrow:
            RenderLinearWithArrow(ctx);
            break;
        case GaugeMode::LinearScale:
            RenderLinearScale(ctx);
            break;
        case GaugeMode::CircularRing:
            RenderCircularRing(ctx, GetGaugeCenter(), GetGaugeRadius());
            break;
        case GaugeMode::Battery:
            RenderBattery(ctx);
            break;
        case GaugeMode::Thermometer:
            RenderThermometer(ctx);
            break;
        case GaugeMode::Cylinder:
            RenderCylinder(ctx);
            break;
        case GaugeMode::Digital:
            RenderDigital(ctx);
            break;
    }

    ctx->PopState();
}

// =============================================================================
// EVENT HANDLING
// =============================================================================

bool UltraCanvasGaugeDiagramElement::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;

    if (mode == GaugeMode::Speedometer || mode == GaugeMode::Semicircular ||
        mode == GaugeMode::Quadrant || mode == GaugeMode::CircularRing) {
        if (event.type == UCEventType::MouseDown) {
            Point2Df center = GetGaugeCenter();
            double dx = static_cast<double>(event.pointer.x) - center.x;
            double dy = static_cast<double>(event.pointer.y) - center.y;
            double clickAngle = std::atan2(dy, dx) * 180.0 / M_PI;
            if (clickAngle < 0) clickAngle += 360.0;
            double sweep = arcEndDeg - arcStartDeg;
            if (std::abs(sweep) < 1e-10) return false;
            double normalizedAngle = clickAngle;
            double startMod = std::fmod(arcStartDeg, 360.0);
            if (startMod < 0) startMod += 360.0;
            double diff = normalizedAngle - startMod;
            if (diff < 0) diff += 360.0;
            double ratio = diff / sweep;
            ratio = std::max(0.0, std::min(1.0, ratio));
            SetValue(minValue + ratio * (maxValue - minValue));
            return true;
        }
    }

    if (mode == GaugeMode::Thermometer || mode == GaugeMode::Cylinder ||
        mode == GaugeMode::LinearLED || mode == GaugeMode::LinearSegmented) {
        if (event.type == UCEventType::MouseDown) {
            const auto b = GetLocalBounds();
            double ratio;
            if (orientation == GaugeOrientation::Vertical ||
                mode == GaugeMode::Thermometer || mode == GaugeMode::Cylinder) {
                ratio = 1.0 - static_cast<double>(event.pointer.y - b.y) / static_cast<double>(b.height);
            } else {
                ratio = static_cast<double>(event.pointer.x - b.x) / static_cast<double>(b.width);
            }
            ratio = std::max(0.0, std::min(1.0, ratio));
            SetValue(minValue + ratio * (maxValue - minValue));
            return true;
        }
    }

    return UltraCanvasUIElement::OnEvent(event);
}

// =============================================================================
// COMMON DRAWING PRIMITIVES
// =============================================================================

void UltraCanvasGaugeDiagramElement::DrawArcSection(
    IRenderContext* ctx, const Point2Df& center, float radius,
    float startA, float endA, const Color& color, float thickness) {
    ctx->ClearPath();
    ctx->Arc(center.x, center.y, radius, startA, endA);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(thickness);
    ctx->SetLineCap(LineCap::Butt);
    ctx->Stroke();
}

void UltraCanvasGaugeDiagramElement::DrawNeedle(
    IRenderContext* ctx, const Point2Df& center, double angleDeg, float length) {
    if (needleStyle == GaugeNeedleStyle::NoNeedle) return;

    double rad = angleDeg * M_PI / 180.0;
    float tipX = static_cast<float>(center.x + length * std::cos(rad));
    float tipY = static_cast<float>(center.y + length * std::sin(rad));

    if (needleStyle == GaugeNeedleStyle::Classic || needleStyle == GaugeNeedleStyle::Thin) {
        ctx->SetStrokePaint(gaugeColor);
        ctx->SetStrokeWidth(needleStyle == GaugeNeedleStyle::Thin ? 1.2f : 2.0f);
        ctx->SetLineCap(LineCap::Round);
        ctx->ClearPath();
        ctx->MoveTo(center.x, center.y);
        ctx->LineTo(tipX, tipY);
        ctx->Stroke();
    } else if (needleStyle == GaugeNeedleStyle::Arrow ||
               needleStyle == GaugeNeedleStyle::Triangle) {
        double perpAngle = rad + M_PI / 2.0;
        float baseHalfWidth = 4.0f;
        float bx1 = static_cast<float>(center.x + baseHalfWidth * std::cos(perpAngle));
        float by1 = static_cast<float>(center.y + baseHalfWidth * std::sin(perpAngle));
        float bx2 = static_cast<float>(center.x - baseHalfWidth * std::cos(perpAngle));
        float by2 = static_cast<float>(center.y - baseHalfWidth * std::sin(perpAngle));
        ctx->SetFillPaint(gaugeColor);
        ctx->ClearPath();
        ctx->MoveTo(tipX, tipY);
        ctx->LineTo(bx1, by1);
        ctx->LineTo(bx2, by2);
        ctx->ClosePath();
        ctx->Fill();
    }

    ctx->SetFillPaint(gaugeColor);
    ctx->FillCircle(center, 3.0f);
}

void UltraCanvasGaugeDiagramElement::DrawValueText(
    IRenderContext* ctx, const std::string& text, const Point2Df& pos,
    float fontSize, const Color& color, bool centered) {
    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(fontSize);
    ctx->SetTextPaint(color);
    ctx->SetFontWeight(FontWeight::Light);
    Size2Di dims = ctx->GetTextLineDimensions(text);
    float x = centered ? (pos.x - dims.width / 2.0f) : pos.x;
    ctx->DrawText(text, Point2Df(x, pos.y));
}

void UltraCanvasGaugeDiagramElement::DrawTitleText(IRenderContext* ctx, const Point2Df& pos) {
    if (title.empty()) return;
    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(12.0f);
    ctx->SetTextPaint(Color(130, 130, 145, 255));
    ctx->SetFontWeight(FontWeight::Normal);
    Size2Di dims = ctx->GetTextLineDimensions(title);
    ctx->DrawText(title, Point2Df(pos.x - dims.width / 2.0f, pos.y + dims.height));
}

void UltraCanvasGaugeDiagramElement::DrawUnitText(IRenderContext* ctx, const Point2Df& pos) {
    if (unit.empty()) return;
    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(10.0f);
    ctx->SetTextPaint(Color(150, 150, 160, 255));
    Size2Di dims = ctx->GetTextLineDimensions(unit);
    ctx->DrawText(unit, Point2Df(pos.x - dims.width / 2.0f, pos.y));
}

void UltraCanvasGaugeDiagramElement::DrawArcTicks(
    IRenderContext* ctx, const Point2Df& center, float radius,
    double startAngle, double endAngle) {
    if (tickPos == GaugeTickPosition::NoTicks) return;

    double totalSweep = endAngle - startAngle;
    int majorCount = std::max(majorTickCount, 1);
    double valRange = maxValue - minValue;
    double majorStep = std::abs(valRange) > 1e-10 ? valRange / static_cast<double>(majorCount) : 10.0;
    double degStep = totalSweep / static_cast<double>(majorCount);

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(9.0f);
    ctx->SetTextPaint(Color(130, 130, 145, 255));

    for (int i = 0; i <= majorCount; i++) {
        double deg = startAngle + i * degStep;
        double rad = deg * M_PI / 180.0;
        float innerR = radius - 8.0f;
        float outerR = radius - 1.0f;

        ctx->SetStrokePaint(Color(160, 160, 175, 200));
        ctx->SetStrokeWidth(1.5f);
        ctx->SetLineCap(LineCap::Round);
        ctx->ClearPath();
        ctx->MoveTo(
            static_cast<float>(center.x + innerR * std::cos(rad)),
            static_cast<float>(center.y + innerR * std::sin(rad)));
        ctx->LineTo(
            static_cast<float>(center.x + outerR * std::cos(rad)),
            static_cast<float>(center.y + outerR * std::sin(rad)));
        ctx->Stroke();

        if (showLabels) {
            float labelR = radius + 14.0f;
            double val = minValue + i * majorStep;
            std::string txt = FormatValue(val);
            Size2Di dims = ctx->GetTextLineDimensions(txt);
            float lx = static_cast<float>(center.x + labelR * std::cos(rad) - dims.width / 2);
            float ly = static_cast<float>(center.y + labelR * std::sin(rad) - dims.height / 2);
            ctx->DrawText(txt, Point2Df(lx, ly));
        }

        if (i < majorCount && minorTickCount > 0) {
            for (int j = 1; j <= minorTickCount; j++) {
                double mDeg = deg + j * degStep / (minorTickCount + 1);
                double mRad = mDeg * M_PI / 180.0;
                float mInnerR = radius - 5.0f;
                ctx->SetStrokePaint(Color(180, 180, 195, 150));
                ctx->SetStrokeWidth(0.8f);
                ctx->ClearPath();
                ctx->MoveTo(
                    static_cast<float>(center.x + mInnerR * std::cos(mRad)),
                    static_cast<float>(center.y + mInnerR * std::sin(mRad)));
                ctx->LineTo(
                    static_cast<float>(center.x + outerR * std::cos(mRad)),
                    static_cast<float>(center.y + outerR * std::sin(mRad)));
                ctx->Stroke();
            }
        }
    }
}

void UltraCanvasGaugeDiagramElement::DrawExternalPointers(
    IRenderContext* ctx, float barX, float barY, float barW, float barH) {
    if (externalPointers.empty()) return;

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(10.0f);
    ctx->SetTextPaint(textColor);

    bool vertical = (orientation == GaugeOrientation::Vertical);
    float arrowSize = 6.0f;
    for (const auto& p : externalPointers) {
        double ratio = ValueToRatio(p.value);
        ratio = std::max(0.0, std::min(1.0, ratio));

        float pX, pY;
        if (vertical) {
            pX = barX + barW;
            pY = barY + barH - static_cast<float>(ratio * barH);
            // V2.2 FIX: Clamp Y so arrow tip stays visually within the bar (not overshooting top/bottom)
            float halfArrow = arrowSize / 2.0f + 1.0f;
            if (pY < barY + halfArrow) pY = barY + halfArrow;
            if (pY > barY + barH - halfArrow) pY = barY + barH - halfArrow;
        } else {
            pX = barX + static_cast<float>(ratio * barW);
            pY = barY + barH / 2.0f;
        }

        ctx->SetFillPaint(p.color);
        ctx->ClearPath();
        if (vertical) {
            ctx->MoveTo(pX + 2.0f, pY);
            ctx->LineTo(pX + 2.0f + arrowSize, pY - arrowSize / 2.0f);
            ctx->LineTo(pX + 2.0f + arrowSize, pY + arrowSize / 2.0f);
        } else {
            ctx->MoveTo(pX, pY - barH / 2.0f - 2.0f);
            ctx->LineTo(pX - arrowSize / 2.0f, pY - barH / 2.0f - 2.0f - arrowSize);
            ctx->LineTo(pX + arrowSize / 2.0f, pY - barH / 2.0f - 2.0f - arrowSize);
        }
        ctx->ClosePath();
        ctx->Fill();

        if (!p.label.empty()) {
            // V2.1 FIX: Show value AND label, e.g. "1500 ml - Pointer 1"
            std::ostringstream lbl;
            lbl << std::fixed << std::setprecision(decimalPlaces) << p.value;
            if (!unit.empty()) lbl << " " << unit;
            lbl << " - " << p.label;
            Size2Di d = ctx->GetTextLineDimensions(lbl.str());
            float lx = vertical ? (pX + arrowSize + 6.0f) : (pX - d.width / 2.0f);
            float ly = vertical ? (pY - d.height / 2.0f) : (pY - barH / 2.0f - arrowSize - 8.0f);
            ctx->DrawText(lbl.str(), Point2Df(lx, ly));
        }
    }
}

void UltraCanvasGaugeDiagramElement::DrawSubDial(
    IRenderContext* ctx, const Point2Df& mainCenter, float mainRadius) {
    if (!subDial.enabled) return;

    float subRadius = mainRadius * static_cast<float>(subDial.relativeSize);
    Point2Df subCenter(
        mainCenter.x + mainRadius * static_cast<float>(subDial.offsetXRatio),
        mainCenter.y + mainRadius * static_cast<float>(subDial.offsetYRatio));

    double subRange = subDial.maxValue - subDial.minValue;
    double subRatio = std::abs(subRange) < 1e-10 ? 0.0
                    : (subDial.currentValue - subDial.minValue) / subRange;

    int subTicks = 4;
    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(7.0f);
    ctx->SetTextPaint(textColor);

    for (int i = 0; i <= subTicks; i++) {
        double a = 180.0 + (180.0 * i / subTicks);
        double rad = a * M_PI / 180.0;
        float innerR = subRadius - 4.0f;
        float outerR = subRadius - 1.0f;
        ctx->SetStrokePaint(Color(180, 180, 195, 150));
        ctx->SetStrokeWidth(0.8f);
        ctx->ClearPath();
        ctx->MoveTo(
            static_cast<float>(subCenter.x + innerR * std::cos(rad)),
            static_cast<float>(subCenter.y + innerR * std::sin(rad)));
        ctx->LineTo(
            static_cast<float>(subCenter.x + outerR * std::cos(rad)),
            static_cast<float>(subCenter.y + outerR * std::sin(rad)));
        ctx->Stroke();
    }

    if (mode != GaugeMode::Stopwatch) {
        constexpr float subTextFontSize = 6.0f;
        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(subTextFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(Color(130, 130, 145, 255));
        std::string subVal = "0";
        if (!subDial.unit.empty()) subVal += " " + subDial.unit;
        Size2Di sd = ctx->GetTextLineDimensions(subVal);
        float tx = subCenter.x - sd.width / 2.0f;
        float ty = subCenter.y + subRadius * 0.35f;
        ctx->DrawText(subVal, Point2Df(tx, ty));
    } else {
        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(7.0f);
        ctx->SetTextPaint(Color(130, 130, 145, 200));
        ctx->SetFontWeight(FontWeight::Normal);
        const char* edgeMarks[] = {"0", "30"};
        const double edgeAngles[] = {180.0, 360.0};
        for (int i = 0; i < 2; i++) {
            double rad = edgeAngles[i] * M_PI / 180.0;
            float labelR = subRadius - 9.0f;
            Size2Di sd = ctx->GetTextLineDimensions(edgeMarks[i]);
            float lx = static_cast<float>(subCenter.x + labelR * std::cos(rad) - sd.width / 2.0f);
            float ly = static_cast<float>(subCenter.y + labelR * std::sin(rad) - sd.height / 2.0f);
            ctx->DrawText(edgeMarks[i], Point2Df(lx, ly));
        }
    }

    double subAngle = 180.0 + 180.0 * subRatio;
    double subRad = subAngle * M_PI / 180.0;
    float tipX = static_cast<float>(subCenter.x + (subRadius - 6.0f) * std::cos(subRad));
    float tipY = static_cast<float>(subCenter.y + (subRadius - 6.0f) * std::sin(subRad));
    ctx->SetStrokePaint(gaugeColor);
    ctx->SetStrokeWidth(1.5f);
    ctx->SetLineCap(LineCap::Round);
    ctx->ClearPath();
    ctx->MoveTo(subCenter.x, subCenter.y);
    ctx->LineTo(tipX, tipY);
    ctx->Stroke();
    ctx->SetFillPaint(gaugeColor);
    ctx->FillCircle(subCenter, 1.5f);
}

void UltraCanvasGaugeDiagramElement::DrawThresholdMarkers(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    if (thresholds.empty()) return;
    for (const auto& t : thresholds) {
        double angle = ValueToAngle(t.value);
        double rad = angle * M_PI / 180.0;
        float innerR = radius - 16.0f;
        float outerR = radius - 2.0f;
        ctx->SetStrokePaint(t.color);
        ctx->SetStrokeWidth(2.0f);
        ctx->SetLineCap(LineCap::Round);
        ctx->ClearPath();
        ctx->MoveTo(
            static_cast<float>(center.x + innerR * std::cos(rad)),
            static_cast<float>(center.y + innerR * std::sin(rad)));
        ctx->LineTo(
            static_cast<float>(center.x + outerR * std::cos(rad)),
            static_cast<float>(center.y + outerR * std::sin(rad)));
        ctx->Stroke();
    }
}

// =============================================================================
// ANALOG MODE RENDERERS
// =============================================================================

// V2.6.0: Dark automotive redesign — self-contained dark disc inside a light card.
//   Mimics a glossy black dashboard speedometer: orange→amber graduated ticks
//   (warm/light toward the top), white numeric labels, blue inner sub-dial,
//   tapered orange needle with white-ringed hub, dark LCD value box + odometer
//   strip. Intentionally ignores the shared light-card bgColor/textColor/tick
//   helpers so the surrounding card stays light while the disc is dark.
void UltraCanvasGaugeDiagramElement::RenderSpeedometer(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();
    double totalSweep = arcEndDeg - arcStartDeg;

    // --- Dark glossy bezel (concentric solid discs, no gradients needed) ---
    const Color kBezelOuter(18, 18, 22, 255);
    const Color kBezelRim  (60, 62, 70, 255);
    const Color kFaceColor (28, 29, 34, 255);
    const Color kAccentOrange(255, 120, 0, 255);

    ctx->SetFillPaint(kBezelOuter);
    ctx->FillCircle(center, radius + 6.0f);
    ctx->SetFillPaint(kBezelRim);
    ctx->FillCircle(center, radius + 2.0f);
    ctx->SetFillPaint(kFaceColor);
    ctx->FillCircle(center, radius);

    // Subtle top rim highlight
    ctx->SetStrokePaint(Color(120, 124, 135, 70));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(center, radius - 1.0f);

    // --- Graduated tick marks (orange at top, fading amber toward 0/240) ---
    if (tickPos != GaugeTickPosition::NoTicks) {
        int majorCount = std::max(majorTickCount, 1);
        double valRange = maxValue - minValue;
        double majorStep = std::abs(valRange) > 1e-10
                           ? valRange / static_cast<double>(majorCount) : 10.0;
        double degStep = totalSweep / static_cast<double>(majorCount);

        // Helper: interpolate tick color by vertical position (top = brightest)
        auto tickColorFor = [&](double deg) -> Color {
            double rad = deg * M_PI / 180.0;
            // sin(deg) ranges -1 (top) .. +1 (bottom) in screen coords (y grows down)
            double t = (std::sin(rad) + 1.0) / 2.0;   // 0 at top, 1 at bottom
            // V2.6.1: brighter near-white amber at top, deep orange toward bottom
            int gg = static_cast<int>(170 + (1.0 - t) * 85.0);   // 255 top → 170 bottom
            int bb = static_cast<int>(40 + (1.0 - t) * 175.0);   // 215 top → 40 bottom
            return Color(255, std::min(gg, 255), std::min(bb, 255), 255);
        };

        for (int i = 0; i <= majorCount; i++) {
            double deg = arcStartDeg + i * degStep;
            double rad = deg * M_PI / 180.0;
            Color tc = tickColorFor(deg);

            // Major tick (thick)
            float innerR = radius - 14.0f;
            float outerR = radius - 3.0f;
            ctx->SetStrokePaint(tc);
            ctx->SetStrokeWidth(3.0f);
            ctx->SetLineCap(LineCap::Round);
            ctx->ClearPath();
            ctx->MoveTo(static_cast<float>(center.x + innerR * std::cos(rad)),
                        static_cast<float>(center.y + innerR * std::sin(rad)));
            ctx->LineTo(static_cast<float>(center.x + outerR * std::cos(rad)),
                        static_cast<float>(center.y + outerR * std::sin(rad)));
            ctx->Stroke();

            // White numeric label, inside the ticks
            // V2.6.1: pulled further inward (radius-32) + smaller font (12) so the
            //   top labels (120/160) and bottom labels (0/240) stop crowding.
            if (showLabels) {
                float labelR = radius - 32.0f;
                double val = minValue + i * majorStep;
                std::string txt = FormatValue(val);
                ctx->SetFontFamily("Sans");
                ctx->SetFontSize(10.0f);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetTextPaint(Color(235, 236, 240, 255));
                Size2Di dims = ctx->GetTextLineDimensions(txt);
                float lx = static_cast<float>(center.x + labelR * std::cos(rad) - dims.width / 2.0f);
                float ly = static_cast<float>(center.y + labelR * std::sin(rad) - dims.height / 2.0f);
                ctx->DrawText(txt, Point2Df(lx, ly));
            }

            // Minor ticks (thin, dimmer)
            if (i < majorCount && minorTickCount > 0) {
                for (int j = 1; j <= minorTickCount; j++) {
                    double mDeg = deg + j * degStep / (minorTickCount + 1);
                    double mRad = mDeg * M_PI / 180.0;
                    Color mc = tickColorFor(mDeg);
                    mc.a = 180;
                    float mInnerR = radius - 9.0f;
                    ctx->SetStrokePaint(mc);
                    ctx->SetStrokeWidth(1.4f);
                    ctx->ClearPath();
                    ctx->MoveTo(static_cast<float>(center.x + mInnerR * std::cos(mRad)),
                                static_cast<float>(center.y + mInnerR * std::sin(mRad)));
                    ctx->LineTo(static_cast<float>(center.x + (radius - 3.0f) * std::cos(mRad)),
                                static_cast<float>(center.y + (radius - 3.0f) * std::sin(mRad)));
                    ctx->Stroke();
                }
            }
        }
    }

    DrawThresholdMarkers(ctx, center, radius);

    // --- Optional range arcs (kept, drawn just inside the ticks) ---
    if (!ranges.empty() && std::abs(totalSweep) > 1e-10) {
        for (const auto& r : ranges) {
            double rs = (r.startValue - minValue) / (maxValue - minValue) * totalSweep + arcStartDeg;
            double re = (r.endValue - minValue) / (maxValue - minValue) * totalSweep + arcStartDeg;
            float rsRad = static_cast<float>(rs * M_PI / 180.0);
            float reRad = static_cast<float>(re * M_PI / 180.0);
            DrawArcSection(ctx, center, radius - 18.0f, rsRad, reRad, r.color, 3.0f);
        }
    }

    // V2.6.1: Sub-dial intentionally NOT drawn for the dark automotive look —
    //   the shared DrawSubDial is a light-themed 180° semicircle that reads poorly
    //   on black and clutters the disc. (Stopwatch/other modes still use it.)

    // --- Tapered orange needle ---
    double curAngle = ValueToAngle(currentValue);
    double curRad = curAngle * M_PI / 180.0;
    float needleLen = radius * 0.74f;
    float tipX = static_cast<float>(center.x + needleLen * std::cos(curRad));
    float tipY = static_cast<float>(center.y + needleLen * std::sin(curRad));

    // Back-tail (short, opposite direction)
    float tailLen = radius * 0.12f;
    float tailX = static_cast<float>(center.x - tailLen * std::cos(curRad));
    float tailY = static_cast<float>(center.y - tailLen * std::sin(curRad));

    // Glow underlay
    ctx->SetStrokePaint(Color(255, 120, 0, 70));
    ctx->SetStrokeWidth(7.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->ClearPath();
    ctx->MoveTo(tailX, tailY);
    ctx->LineTo(tipX, tipY);
    ctx->Stroke();

    // Solid needle as a tapered quad (wide at hub, point at tip)
    double perp = curRad + M_PI / 2.0;
    float halfW = 4.0f;
    float baseX1 = static_cast<float>(center.x + halfW * std::cos(perp));
    float baseY1 = static_cast<float>(center.y + halfW * std::sin(perp));
    float baseX2 = static_cast<float>(center.x - halfW * std::cos(perp));
    float baseY2 = static_cast<float>(center.y - halfW * std::sin(perp));
    ctx->SetFillPaint(kAccentOrange);
    ctx->ClearPath();
    ctx->MoveTo(tipX, tipY);
    ctx->LineTo(baseX1, baseY1);
    ctx->LineTo(tailX, tailY);
    ctx->LineTo(baseX2, baseY2);
    ctx->ClosePath();
    ctx->Fill();

    // --- Center hub with white ring ---
    ctx->SetFillPaint(Color(60, 62, 70, 255));
    ctx->FillCircle(center, 9.0f);
    ctx->SetFillPaint(kAccentOrange);
    ctx->FillCircle(center, 5.5f);
    ctx->SetFillPaint(Color(255, 255, 255, 230));
    ctx->FillCircle(center, 2.0f);

    // --- Title (on the light card, above the disc) ---
    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(center.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    // --- Value + unit below the disc, styled like the other cards (V2.6.3) ---
    float unitH = unit.empty() ? 0.0f : kUnitHeight;
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - unitH
                   - (unit.empty() ? 0.0f : kValueUnitGap);
    DrawValueText(ctx, FormatValue(currentValue),
                  Point2Df(center.x, valueY), 24.0f, textColor);
    if (!unit.empty()) {
        DrawUnitText(ctx, Point2Df(center.x, valueY + kValueUnitGap + 24.0f));
    }
}

void UltraCanvasGaugeDiagramElement::RenderSemicircular(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();
    Point2Df adjCenter = center;
    adjCenter.y += radius * 0.35f;
    float adjRadius = radius * 1.20f;
    double totalSweep = arcEndDeg - arcStartDeg;

    // Background semicircle
    ctx->SetFillPaint(bgColor);
    float startRadBg = static_cast<float>(arcStartDeg * M_PI / 180.0);
    float endRadBg = static_cast<float>(arcEndDeg * M_PI / 180.0);
    ctx->ClearPath();
    ctx->Arc(adjCenter.x, adjCenter.y, adjRadius + 4.0f, startRadBg, endRadBg);
    ctx->LineTo(adjCenter.x, adjCenter.y);
    ctx->ClosePath();
    ctx->Fill();

    // Range arcs — thin strokes
    if (!ranges.empty() && std::abs(totalSweep) > 1e-10) {
        for (const auto& r : ranges) {
            double rs = (r.startValue - minValue) / (maxValue - minValue) * totalSweep + arcStartDeg;
            double re = (r.endValue - minValue) / (maxValue - minValue) * totalSweep + arcStartDeg;
            float rsRad = static_cast<float>(rs * M_PI / 180.0);
            float reRad = static_cast<float>(re * M_PI / 180.0);
            DrawArcSection(ctx, adjCenter, adjRadius - 2.0f, rsRad, reRad, r.color, 5.0f);

            if (!r.label.empty() && showLabels) {
                double midAngle = (rs + re) / 2.0;
                double midRad = midAngle * M_PI / 180.0;
                float lx = static_cast<float>(adjCenter.x + (adjRadius * 0.55f) * std::cos(midRad));
                float ly = static_cast<float>(adjCenter.y + (adjRadius * 0.55f) * std::sin(midRad));
                ctx->SetFontFamily("Sans");
                ctx->SetFontSize(9.0f);
                ctx->SetTextPaint(Color(130, 130, 145, 255));
                Size2Di d = ctx->GetTextLineDimensions(r.label);
                ctx->DrawText(r.label, Point2Df(lx - d.width / 2.0f, ly - d.height / 2.0f));
            }
        }
    }

    // Tick labels outside arc
    if (showLabels && tickPos != GaugeTickPosition::NoTicks) {
        int majorCount = std::max(majorTickCount, 1);
        double valRange = maxValue - minValue;
        double majorStep = std::abs(valRange) > 1e-10 ? valRange / static_cast<double>(majorCount) : 10.0;
        double degStep = totalSweep / static_cast<double>(majorCount);

        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(9.0f);
        ctx->SetTextPaint(Color(130, 130, 145, 255));

        for (int i = 0; i <= majorCount; i++) {
            double deg = arcStartDeg + i * degStep;
            double rad = deg * M_PI / 180.0;

            float innerR = adjRadius + 2.0f;
            float outerR = adjRadius + 6.0f;
            ctx->SetStrokePaint(Color(160, 160, 175, 200));
            ctx->SetStrokeWidth(1.2f);
            ctx->ClearPath();
            ctx->MoveTo(
                static_cast<float>(adjCenter.x + innerR * std::cos(rad)),
                static_cast<float>(adjCenter.y + innerR * std::sin(rad)));
            ctx->LineTo(
                static_cast<float>(adjCenter.x + outerR * std::cos(rad)),
                static_cast<float>(adjCenter.y + outerR * std::sin(rad)));
            ctx->Stroke();

            float labelR = adjRadius + 16.0f;
            double val = minValue + i * majorStep;
            std::string txt = FormatValue(val);
            Size2Di dims = ctx->GetTextLineDimensions(txt);
            float lx = static_cast<float>(adjCenter.x + labelR * std::cos(rad) - dims.width / 2);
            float ly = static_cast<float>(adjCenter.y + labelR * std::sin(rad) - dims.height / 2);
            ctx->DrawText(txt, Point2Df(lx, ly));
        }
    }

    DrawThresholdMarkers(ctx, adjCenter, adjRadius);
    DrawNeedle(ctx, adjCenter, ValueToAngle(currentValue), adjRadius * 0.7f);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(adjCenter.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float unitH = unit.empty() ? 0.0f : kUnitHeight;
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - unitH
                   - (unit.empty() ? 0.0f : kValueUnitGap);
    DrawValueText(ctx, FormatValue(currentValue),
                  Point2Df(adjCenter.x, valueY), 22.0f, textColor);
    if (!unit.empty()) {
        DrawUnitText(ctx, Point2Df(adjCenter.x, valueY + kValueUnitGap + 22.0f));
    }
}

void UltraCanvasGaugeDiagramElement::RenderQuadrant(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();
    Point2Df c = center;
    c.x += radius * 0.20f;
    c.y += radius * 0.40f;
    radius *= 1.30f;
    double totalSweep = arcEndDeg - arcStartDeg;

    // Background wedge
    ctx->SetFillPaint(bgColor);
    float startRadBg = static_cast<float>(arcStartDeg * M_PI / 180.0);
    float endRadBg = static_cast<float>(arcEndDeg * M_PI / 180.0);
    ctx->ClearPath();
    ctx->MoveTo(c.x, c.y);
    ctx->Arc(c.x, c.y, radius + 4.0f, startRadBg, endRadBg);
    ctx->ClosePath();
    ctx->Fill();

    // Range arcs — thin strokes
    if (!ranges.empty() && std::abs(totalSweep) > 1e-10) {
        for (const auto& r : ranges) {
            double rs = (r.startValue - minValue) / (maxValue - minValue) * totalSweep + arcStartDeg;
            double re = (r.endValue - minValue) / (maxValue - minValue) * totalSweep + arcStartDeg;
            float rsRad = static_cast<float>(rs * M_PI / 180.0);
            float reRad = static_cast<float>(re * M_PI / 180.0);
            DrawArcSection(ctx, c, radius - 2.0f, rsRad, reRad, r.color, 5.0f);
        }
    }

    // Tick labels inside arc
    if (showLabels && tickPos != GaugeTickPosition::NoTicks) {
        int majorCount = std::max(majorTickCount, 1);
        double valRange = maxValue - minValue;
        double majorStep = std::abs(valRange) > 1e-10 ? valRange / static_cast<double>(majorCount) : 10.0;
        double degStep = totalSweep / static_cast<double>(majorCount);

        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(9.0f);
        ctx->SetTextPaint(Color(130, 130, 145, 255));

        for (int i = 0; i <= majorCount; i++) {
            double deg = arcStartDeg + i * degStep;
            double rad = deg * M_PI / 180.0;

            float innerR = radius - 9.0f;
            float outerR = radius - 2.0f;
            ctx->SetStrokePaint(Color(160, 160, 175, 200));
            ctx->SetStrokeWidth(1.2f);
            ctx->ClearPath();
            ctx->MoveTo(
                static_cast<float>(c.x + innerR * std::cos(rad)),
                static_cast<float>(c.y + innerR * std::sin(rad)));
            ctx->LineTo(
                static_cast<float>(c.x + outerR * std::cos(rad)),
                static_cast<float>(c.y + outerR * std::sin(rad)));
            ctx->Stroke();

            float labelR = radius - 20.0f;
            double val = minValue + i * majorStep;
            std::string txt = FormatValue(val);
            Size2Di dims = ctx->GetTextLineDimensions(txt);
            float lx = static_cast<float>(c.x + labelR * std::cos(rad) - dims.width / 2);
            float ly = static_cast<float>(c.y + labelR * std::sin(rad) - dims.height / 2);
            ctx->DrawText(txt, Point2Df(lx, ly));
        }
    }

    DrawThresholdMarkers(ctx, c, radius);
    DrawNeedle(ctx, c, ValueToAngle(currentValue), radius * 0.7f);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(center.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float unitH = unit.empty() ? 0.0f : kUnitHeight;
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - unitH
                   - (unit.empty() ? 0.0f : kValueUnitGap);
    DrawValueText(ctx, FormatValue(currentValue),
                  Point2Df(center.x, valueY), 20.0f, textColor);
    if (!unit.empty()) {
        DrawUnitText(ctx, Point2Df(center.x, valueY + kValueUnitGap + 20.0f));
    }
}

void UltraCanvasGaugeDiagramElement::RenderCompass(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();
    Point2Df adjCenter = center;
    adjCenter.y -= radius * 0.25f;

    ctx->SetFillPaint(bgColor);
    ctx->FillCircle(adjCenter, radius + 6.0f);
    ctx->SetStrokePaint(Color(200, 200, 215, 255));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(adjCenter, radius);

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(8.0f);
    ctx->SetTextPaint(Color(130, 130, 145, 255));

    for (int deg = 0; deg < 360; deg += 30) {
        double mathDeg = deg - 90.0;
        double rad = mathDeg * M_PI / 180.0;
        bool isMajor = (deg % 90) == 0;
        float innerR = isMajor ? radius - 12.0f : radius - 7.0f;
        float outerR = radius - 2.0f;

        ctx->SetStrokePaint(Color(160, 160, 175, isMajor ? 220 : 150));
        ctx->SetStrokeWidth(isMajor ? 1.2f : 0.8f);
        ctx->ClearPath();
        ctx->MoveTo(
            static_cast<float>(adjCenter.x + innerR * std::cos(rad)),
            static_cast<float>(adjCenter.y + innerR * std::sin(rad)));
        ctx->LineTo(
            static_cast<float>(adjCenter.x + outerR * std::cos(rad)),
            static_cast<float>(adjCenter.y + outerR * std::sin(rad)));
        ctx->Stroke();

        if (showLabels) {
            std::ostringstream oss;
            oss << deg;
            std::string label = oss.str() + "\xC2\xB0";
            float labelR = radius + 12.0f;
            Size2Di d = ctx->GetTextLineDimensions(label);
            float lx = static_cast<float>(adjCenter.x + labelR * std::cos(rad) - d.width / 2);
            float ly = static_cast<float>(adjCenter.y + labelR * std::sin(rad) - d.height / 2);
            ctx->DrawText(label, Point2Df(lx, ly));
        }
    }

    const char* dirs[] = {"N", "E", "S", "W"};
    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(13.0f);
    ctx->SetFontWeight(FontWeight::Normal);
    for (int i = 0; i < 4; i++) {
        double deg = i * 90.0 - 90.0;
        double rad = deg * M_PI / 180.0;
        float labelR = radius - 24.0f;
        ctx->SetTextPaint((i == 0) ? Color(220, 60, 60, 255) : Color(100, 100, 115, 255));
        Size2Di d = ctx->GetTextLineDimensions(dirs[i]);
        float lx = static_cast<float>(adjCenter.x + labelR * std::cos(rad) - d.width / 2);
        float ly = static_cast<float>(adjCenter.y + labelR * std::sin(rad) - d.height / 2);
        ctx->DrawText(dirs[i], Point2Df(lx, ly));
    }

    double heading = currentValue;
    double headingRad = (heading - 90.0) * M_PI / 180.0;
    float needleLen = radius * 0.62f;
    float tipX = static_cast<float>(adjCenter.x + needleLen * std::cos(headingRad));
    float tipY = static_cast<float>(adjCenter.y + needleLen * std::sin(headingRad));
    float backX = static_cast<float>(adjCenter.x - needleLen * 0.3 * std::cos(headingRad));
    float backY = static_cast<float>(adjCenter.y - needleLen * 0.3 * std::sin(headingRad));

    ctx->SetStrokePaint(Color(220, 60, 60, 255));
    ctx->SetStrokeWidth(2.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->ClearPath();
    ctx->MoveTo(adjCenter.x, adjCenter.y);
    ctx->LineTo(tipX, tipY);
    ctx->Stroke();
    ctx->SetStrokePaint(Color(160, 160, 175, 255));
    ctx->ClearPath();
    ctx->MoveTo(adjCenter.x, adjCenter.y);
    ctx->LineTo(backX, backY);
    ctx->Stroke();
    ctx->SetFillPaint(Color(160, 160, 175, 255));
    ctx->FillCircle(adjCenter, 3.0f);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(adjCenter.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << heading << "\xC2\xB0";
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - kValueHeight + 18.0f;
    DrawValueText(ctx, oss.str(), Point2Df(adjCenter.x, valueY), 20.0f, textColor);
}

void UltraCanvasGaugeDiagramElement::RenderAnalogClock(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();

    ctx->SetFillPaint(bgColor);
    ctx->FillCircle(center, radius + 4.0f);
    ctx->SetStrokePaint(Color(200, 200, 215, 255));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(center, radius);

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(12.0f);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(Color(100, 100, 115, 255));

    for (int h = 1; h <= 12; h++) {
        double deg = h * 30.0 - 90.0;
        double rad = deg * M_PI / 180.0;
        float innerR = radius - 10.0f;
        float outerR = radius - 2.0f;
        ctx->SetStrokePaint(Color(180, 180, 195, 220));
        ctx->SetStrokeWidth(1.2f);
        ctx->SetLineCap(LineCap::Round);
        ctx->ClearPath();
        ctx->MoveTo(
            static_cast<float>(center.x + innerR * std::cos(rad)),
            static_cast<float>(center.y + innerR * std::sin(rad)));
        ctx->LineTo(
            static_cast<float>(center.x + outerR * std::cos(rad)),
            static_cast<float>(center.y + outerR * std::sin(rad)));
        ctx->Stroke();

        if (showLabels) {
            std::ostringstream oss;
            oss << h;
            float labelR = radius - 22.0f;
            Size2Di d = ctx->GetTextLineDimensions(oss.str());
            float lx = static_cast<float>(center.x + labelR * std::cos(rad) - d.width / 2);
            float ly = static_cast<float>(center.y + labelR * std::sin(rad) - d.height / 2);
            ctx->DrawText(oss.str(), Point2Df(lx, ly));
        }
    }

    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    int hour = lt->tm_hour % 12;
    int minute = lt->tm_min;
    int second = lt->tm_sec;

    double hourAngle = (hour + minute / 60.0) * 30.0 - 90.0;
    double hourRad = hourAngle * M_PI / 180.0;
    ctx->SetStrokePaint(Color(60, 60, 75, 255));
    ctx->SetStrokeWidth(3.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->ClearPath();
    ctx->MoveTo(center.x, center.y);
    ctx->LineTo(
        static_cast<float>(center.x + (radius * 0.5f) * std::cos(hourRad)),
        static_cast<float>(center.y + (radius * 0.5f) * std::sin(hourRad)));
    ctx->Stroke();

    double minAngle = minute * 6.0 - 90.0;
    double minRad = minAngle * M_PI / 180.0;
    ctx->SetStrokePaint(Color(80, 80, 95, 255));
    ctx->SetStrokeWidth(1.8f);
    ctx->ClearPath();
    ctx->MoveTo(center.x, center.y);
    ctx->LineTo(
        static_cast<float>(center.x + (radius * 0.72f) * std::cos(minRad)),
        static_cast<float>(center.y + (radius * 0.72f) * std::sin(minRad)));
    ctx->Stroke();

    double secAngle = second * 6.0 - 90.0;
    double secRad = secAngle * M_PI / 180.0;
    ctx->SetStrokePaint(Color(220, 60, 60, 255));
    ctx->SetStrokeWidth(1.2f);
    ctx->ClearPath();
    ctx->MoveTo(center.x, center.y);
    ctx->LineTo(
        static_cast<float>(center.x + (radius * 0.82f) * std::cos(secRad)),
        static_cast<float>(center.y + (radius * 0.82f) * std::sin(secRad)));
    ctx->Stroke();

    ctx->SetFillPaint(Color(60, 60, 75, 255));
    ctx->FillCircle(center, 3.0f);
    ctx->SetFillPaint(Color(220, 60, 60, 255));
    ctx->FillCircle(center, 1.5f);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(center.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }
}

void UltraCanvasGaugeDiagramElement::RenderStopwatch(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();
    Point2Df c = center;

    ctx->SetFillPaint(bgColor);
    ctx->FillCircle(c, radius + 4.0f);
    ctx->SetStrokePaint(Color(200, 200, 215, 255));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawCircle(c, radius);

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(9.0f);
    ctx->SetTextPaint(Color(130, 130, 145, 255));

    for (int i = 0; i < 60; i++) {
        double deg = i * 6.0 - 90.0;
        double rad = deg * M_PI / 180.0;
        bool isMajor = (i % 5) == 0;
        float innerR = isMajor ? radius - 10.0f : radius - 5.0f;
        float outerR = radius - 2.0f;
        ctx->SetStrokePaint(isMajor ? Color(160, 160, 175, 220) : Color(190, 190, 205, 150));
        ctx->SetStrokeWidth(isMajor ? 1.2f : 0.7f);
        ctx->SetLineCap(LineCap::Round);
        ctx->ClearPath();
        ctx->MoveTo(
            static_cast<float>(c.x + innerR * std::cos(rad)),
            static_cast<float>(c.y + innerR * std::sin(rad)));
        ctx->LineTo(
            static_cast<float>(c.x + outerR * std::cos(rad)),
            static_cast<float>(c.y + outerR * std::sin(rad)));
        ctx->Stroke();

        if (isMajor && showLabels) {
            std::ostringstream oss;
            int label = (i == 0) ? 60 : i;
            oss << std::setw(2) << std::setfill('0') << label;
            float labelR = radius - 20.0f;
            Size2Di d = ctx->GetTextLineDimensions(oss.str());
            float lx = static_cast<float>(c.x + labelR * std::cos(rad) - d.width / 2);
            float ly = static_cast<float>(c.y + labelR * std::sin(rad) - d.height / 2);
            ctx->DrawText(oss.str(), Point2Df(lx, ly));
        }
    }

    int minutes = static_cast<int>(currentValue) / 60;
    double sec = currentValue - minutes * 60;
    std::ostringstream timeStr;
    timeStr << std::setw(2) << std::setfill('0') << minutes << ":"
            << std::fixed << std::setprecision(2) << std::setw(5) << std::setfill('0') << sec;

    if (subDial.enabled) {
        GaugeSubDial original = subDial;
        const_cast<GaugeSubDial&>(subDial).offsetXRatio = 0.0;
        const_cast<GaugeSubDial&>(subDial).offsetYRatio = 0.40;
        const_cast<GaugeSubDial&>(subDial).relativeSize = 0.22;
        DrawSubDial(ctx, c, radius);
        const_cast<GaugeSubDial&>(subDial) = original;
    }

    double secValue = std::fmod(currentValue, 60.0);
    double secAngle = secValue * 6.0 - 90.0;
    double secRad = secAngle * M_PI / 180.0;
    float tipX = static_cast<float>(c.x + (radius * 0.85f) * std::cos(secRad));
    float tipY = static_cast<float>(c.y + (radius * 0.85f) * std::sin(secRad));
    float backX = static_cast<float>(c.x - (radius * 0.15f) * std::cos(secRad));
    float backY = static_cast<float>(c.y - (radius * 0.15f) * std::sin(secRad));

    ctx->SetStrokePaint(gaugeColor);
    ctx->SetStrokeWidth(2.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->ClearPath();
    ctx->MoveTo(backX, backY);
    ctx->LineTo(tipX, tipY);
    ctx->Stroke();

    ctx->SetFillPaint(gaugeColor);
    ctx->FillCircle(c, 4.0f);
    ctx->SetFillPaint(Color(238, 239, 245, 255));
    ctx->FillCircle(c, 2.0f);

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(12.0f);
    ctx->SetFontWeight(FontWeight::Light);
    Size2Di d = ctx->GetTextLineDimensions(timeStr.str());
    float textX = c.x - d.width / 2.0f;
    float textY = c.y - radius * 0.55f;
    ctx->SetTextPaint(gaugeColor);
    ctx->DrawText(timeStr.str(), Point2Df(textX, textY));

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(c.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }
}

// =============================================================================
// LINEAR / PROGRESS RENDERERS
// =============================================================================

// V2.1 FIX: Title at top with proper padding, bar in center, value below
void UltraCanvasGaugeDiagramElement::RenderLinearBar(IRenderContext* ctx) {
    const auto b = GetLocalBounds();
    bool vertical = (orientation == GaugeOrientation::Vertical);

    // Title at top
    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 8.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    float barX, barY, barW, barH;
    if (vertical) {
        barW = 36.0f;
        barH = static_cast<float>(b.height) - topReserved - bottomReserved;
        barX = static_cast<float>(b.x + b.width / 2) - barW / 2.0f;
        barY = static_cast<float>(b.y) + topReserved;
    } else {
        barW = static_cast<float>(b.width) - 2.0f * kPaddingSide;
        barH = 28.0f;
        barX = static_cast<float>(b.x) + kPaddingSide;
        // Center vertically in available area
        float availTop = static_cast<float>(b.y) + topReserved;
        float availBottom = static_cast<float>(b.y + b.height) - bottomReserved;
        barY = availTop + (availBottom - availTop - barH) / 2.0f;
    }

    ctx->SetFillPaint(Color(225, 226, 235, 255));
    ctx->FillRoundedRectangle(Rect2Df(barX, barY, barW, barH), barH / 2.0f);

    // Fill
    double ratio = ValueToRatio(currentValue);
    Color fillC = GetColorForValue(currentValue);
    ctx->SetFillPaint(fillC);

    if (vertical) {
        float fillH = static_cast<float>(barH * ratio);
        if (fillH > 1.0f) {
            // Clamp the corner radius to half of the smaller dimension so a
            // short fill renders as a proper pill instead of overlapping arcs
            // (which collapse into a circle for small values).
            float radius = std::min(barW, fillH) / 2.0f;
            ctx->FillRoundedRectangle(
                Rect2Df(barX, barY + barH - fillH, barW, fillH), radius);
        }
    } else {
        float fillW = static_cast<float>(barW * ratio);
        if (fillW > 1.0f) {
            // Clamp the corner radius to half of the smaller dimension so a
            // short fill renders as a proper pill instead of overlapping arcs
            // (which collapse into a circle for small values).
            float radius = std::min(fillW, barH) / 2.0f;
            ctx->FillRoundedRectangle(
                Rect2Df(barX, barY, fillW, barH), radius);
        }
    }

    // Value below
    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

// V2.1 FIX: Title at top, segments centered, value visible below
void UltraCanvasGaugeDiagramElement::RenderLinearLED(IRenderContext* ctx) {
    const auto b = GetLocalBounds();
    bool vertical = (orientation == GaugeOrientation::Vertical);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 8.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    float barX, barY, barW, barH;
    if (vertical) {
        barW = 50.0f;
        barH = static_cast<float>(b.height) - topReserved - bottomReserved;
        barX = static_cast<float>(b.x + b.width / 2) - barW / 2.0f;
        barY = static_cast<float>(b.y) + topReserved;
    } else {
        barW = static_cast<float>(b.width) - 2.0f * kPaddingSide;
        barH = 44.0f;
        barX = static_cast<float>(b.x) + kPaddingSide;
        float availTop = static_cast<float>(b.y) + topReserved;
        float availBottom = static_cast<float>(b.y + b.height) - bottomReserved;
        barY = availTop + (availBottom - availTop - barH) / 2.0f;
    }

    double ratio = ValueToRatio(currentValue);
    int litSegments = static_cast<int>(ratio * segmentCount);
    float gap = 2.0f;

    for (int i = 0; i < segmentCount; i++) {
        bool lit = (i < litSegments);
        Color segColor;
        if (lit) {
            double segValue = minValue + (i + 0.5) * (maxValue - minValue) / segmentCount;
            segColor = GetColorForValue(segValue);
        } else {
            segColor = Color(225, 226, 235, 255);
        }
        ctx->SetFillPaint(segColor);

        if (vertical) {
            float segH = (barH - gap * (segmentCount - 1)) / segmentCount;
            float sy = barY + barH - (i + 1) * (segH + gap) + gap;
            ctx->FillRectangle(Rect2Df(barX, sy, barW, segH));
        } else {
            float segW = (barW - gap * (segmentCount - 1)) / segmentCount;
            float sx = barX + i * (segW + gap);
            ctx->FillRectangle(Rect2Df(sx, barY, segW, barH));
        }
    }

    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

void UltraCanvasGaugeDiagramElement::RenderLinearSegmented(IRenderContext* ctx) {
    const auto b = GetLocalBounds();

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 14.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    float barW = static_cast<float>(b.width) - 2.0f * kPaddingSide;
    float barH = 28.0f;
    float barX = static_cast<float>(b.x) + kPaddingSide;
    float availTop = static_cast<float>(b.y) + topReserved;
    float availBottom = static_cast<float>(b.y + b.height) - bottomReserved;
    float barY = availTop + (availBottom - availTop - barH) / 2.0f;

    double ratio = ValueToRatio(currentValue);
    int litSegments = static_cast<int>(ratio * segmentCount);
    float gap = 2.0f;
    float segW = (barW - gap * (segmentCount - 1)) / segmentCount;

    for (int i = 0; i < segmentCount; i++) {
        bool lit = (i < litSegments);
        double segValue = minValue + (i + 0.5) * (maxValue - minValue) / segmentCount;
        Color segColor = lit ? GetColorForValue(segValue) : Color(225, 226, 235, 255);
        ctx->SetFillPaint(segColor);
        float sx = barX + i * (segW + gap);
        ctx->FillRoundedRectangle(Rect2Df(sx, barY, segW, barH), 3.0f);
    }

    float actualX = barX + static_cast<float>(barW * ratio);
    ctx->SetStrokePaint(gaugeColor);
    ctx->SetStrokeWidth(2.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->ClearPath();
    ctx->MoveTo(barX, barY - 6.0f);
    ctx->LineTo(actualX, barY - 6.0f);
    ctx->Stroke();

    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

void UltraCanvasGaugeDiagramElement::RenderLinearMultiPointer(IRenderContext* ctx) {
    const auto b = GetLocalBounds();
    bool vertical = (orientation == GaugeOrientation::Vertical);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 8.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    float barX, barY, barW, barH;
    if (vertical) {
        // V2.1 FIX: Bar on LEFT side, pointers extend to right with enough space
        barW = 50.0f;
        barH = static_cast<float>(b.height) - topReserved - bottomReserved;
        barX = static_cast<float>(b.x) + kPaddingSide + 4.0f;
        barY = static_cast<float>(b.y) + topReserved;
    } else {
        barW = static_cast<float>(b.width) - 2.0f * kPaddingSide - 80.0f;
        barH = 28.0f;
        barX = static_cast<float>(b.x) + kPaddingSide;
        float availTop = static_cast<float>(b.y) + topReserved;
        float availBottom = static_cast<float>(b.y + b.height) - bottomReserved;
        barY = availTop + (availBottom - availTop - barH) / 2.0f;
    }

    ctx->SetFillPaint(Color(225, 226, 235, 255));
    ctx->FillRectangle(Rect2Df(barX, barY, barW, barH));

    double ratio = ValueToRatio(currentValue);
    Color fillC = GetColorForValue(currentValue);
    ctx->SetFillPaint(fillC);
    if (vertical) {
        float fillH = static_cast<float>(barH * ratio);
        if (fillH > 1.0f) {
            ctx->FillRectangle(Rect2Df(barX, barY + barH - fillH, barW, fillH));
        }
    } else {
        float fillW = static_cast<float>(barW * ratio);
        if (fillW > 1.0f) {
            ctx->FillRectangle(Rect2Df(barX, barY, fillW, barH));
        }
    }

    DrawExternalPointers(ctx, barX, barY, barW, barH);

    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

void UltraCanvasGaugeDiagramElement::RenderLinearWithArrow(IRenderContext* ctx) {
    const auto b = GetLocalBounds();
    bool vertical = (orientation == GaugeOrientation::Vertical);

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 8.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    float barX, barY, barW, barH;
    if (vertical) {
        // V2.1 FIX: Centered with labels space on left, arrow on right
        barW = 38.0f;
        barH = static_cast<float>(b.height) - topReserved - bottomReserved;
        barX = static_cast<float>(b.x + b.width / 2) - barW / 2.0f - 10.0f;
        barY = static_cast<float>(b.y) + topReserved;
    } else {
        barW = static_cast<float>(b.width) - 2.0f * kPaddingSide - 60.0f;
        barH = 28.0f;
        barX = static_cast<float>(b.x) + kPaddingSide;
        float availTop = static_cast<float>(b.y) + topReserved;
        float availBottom = static_cast<float>(b.y + b.height) - bottomReserved;
        barY = availTop + (availBottom - availTop - barH) / 2.0f;
    }

    // Multicolor bar from ranges
    if (!ranges.empty()) {
        for (const auto& r : ranges) {
            double rs = ValueToRatio(r.startValue);
            double re = ValueToRatio(r.endValue);
            ctx->SetFillPaint(r.color);
            if (vertical) {
                float rsY = barY + barH - static_cast<float>(re * barH);
                float reY = barY + barH - static_cast<float>(rs * barH);
                ctx->FillRectangle(Rect2Df(barX, rsY, barW, reY - rsY));
            } else {
                float rsX = barX + static_cast<float>(rs * barW);
                float reX = barX + static_cast<float>(re * barW);
                ctx->FillRectangle(Rect2Df(rsX, barY, reX - rsX, barH));
            }
        }
    } else {
        ctx->SetFillPaint(gaugeColor);
        ctx->FillRectangle(Rect2Df(barX, barY, barW, barH));
    }

    // Arrow + label
    double ratio = ValueToRatio(currentValue);
    float arrowX, arrowY;
    if (vertical) {
        arrowX = barX + barW + 2.0f;
        arrowY = barY + barH - static_cast<float>(ratio * barH);
    } else {
        arrowX = barX + static_cast<float>(ratio * barW);
        arrowY = barY + barH + 2.0f;
    }

    float arrowSize = 8.0f;
    ctx->SetFillPaint(gaugeColor);
    ctx->ClearPath();
    if (vertical) {
        ctx->MoveTo(arrowX, arrowY);
        ctx->LineTo(arrowX + arrowSize, arrowY - arrowSize / 2.0f);
        ctx->LineTo(arrowX + arrowSize, arrowY + arrowSize / 2.0f);
    } else {
        ctx->MoveTo(arrowX, arrowY);
        ctx->LineTo(arrowX - arrowSize / 2.0f, arrowY + arrowSize);
        ctx->LineTo(arrowX + arrowSize / 2.0f, arrowY + arrowSize);
    }
    ctx->ClosePath();
    ctx->Fill();

    std::string label = FormatValue(currentValue);
    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(12.0f);
    ctx->SetFontWeight(FontWeight::Light);
    ctx->SetTextPaint(textColor);
    Size2Di d = ctx->GetTextLineDimensions(label);
    if (vertical) {
        ctx->DrawText(label, Point2Df(arrowX + arrowSize + 4.0f, arrowY - d.height / 2.0f));
    } else {
        ctx->DrawText(label, Point2Df(arrowX - d.width / 2.0f, arrowY + arrowSize + d.height + 2.0f));
    }

    // V2.1 FIX: Min/max labels for vertical mode (left side)
    if (showLabels && vertical) {
        std::string minLabel = FormatValue(minValue) + (unit.empty() ? "" : (" " + unit));
        std::string maxLabel = FormatValue(maxValue) + (unit.empty() ? "" : (" " + unit));
        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(9.0f);
        ctx->SetTextPaint(Color(140, 140, 150, 255));
        Size2Di dMax = ctx->GetTextLineDimensions(maxLabel);
        Size2Di dMin = ctx->GetTextLineDimensions(minLabel);
        ctx->DrawText(maxLabel, Point2Df(barX - dMax.width - 6.0f, barY - dMax.height / 2.0f));
        ctx->DrawText(minLabel, Point2Df(barX - dMin.width - 6.0f, barY + barH - dMin.height / 2.0f));
    }
}

// V2.1 FIX: Radio tuner - more padding, labels positioned to fit
// V2.2 FIX: Detect label overlaps and alternate stations between two label rows
//           (top row for label, bottom row for value, OR staircase if too dense)
void UltraCanvasGaugeDiagramElement::RenderLinearScale(IRenderContext* ctx) {
    const auto b = GetLocalBounds();

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 8.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    // V2.1 FIX: Wider side padding so station names don't get cut
    float sidePad = kPaddingSide + 20.0f;
    float barX = static_cast<float>(b.x) + sidePad;
    float barW = static_cast<float>(b.width) - 2.0f * sidePad;
    float barH = 16.0f;
    float availTop = static_cast<float>(b.y) + topReserved;
    float availBottom = static_cast<float>(b.y + b.height) - bottomReserved;
    float barY = availTop + (availBottom - availTop - barH) / 2.0f;

    ctx->SetFillPaint(Color(225, 226, 235, 255));
    ctx->FillRoundedRectangle(Rect2Df(barX, barY, barW, barH), 4.0f);

    // V2.6: Piecewise ratio — each segment between consecutive thresholds gets equal bar width
    auto segmentRatio = [&](double val) -> double {
        if (thresholds.size() < 2) return ValueToRatio(val);
        if (val <= thresholds.front().value) return 0.0;
        if (val >= thresholds.back().value) return 1.0;
        double segCount = static_cast<double>(thresholds.size() - 1);
        for (size_t i = 0; i < thresholds.size() - 1; ++i) {
            if (val >= thresholds[i].value && val <= thresholds[i + 1].value) {
                double segPos = (val - thresholds[i].value) / (thresholds[i + 1].value - thresholds[i].value);
                return (static_cast<double>(i) + segPos) / segCount;
            }
        }
        return ValueToRatio(val);
    };

    // V2.2: Pre-compute label dimensions to detect overlaps
    struct LabelInfo {
        float centerX;
        float labelHalfW;
        float valHalfW;
        bool useUpperRow;  // staircase: alternate above/below
        bool valUpperRow;  // separate staircase for value labels below bar
    };
    std::vector<LabelInfo> labelInfos;
    labelInfos.reserve(thresholds.size());

    ctx->SetFontFamily("Sans");
    ctx->SetFontSize(10.0f);
    for (const auto& t : thresholds) {
        double ratio = segmentRatio(t.value);
        float mx = barX + static_cast<float>(ratio * barW);
        Size2Di d = ctx->GetTextLineDimensions(t.label);
        std::string valLabel = FormatValue(t.value) + (unit.empty() ? "" : (" " + unit));
        Size2Di dv = ctx->GetTextLineDimensions(valLabel);
        labelInfos.push_back({mx, d.width / 2.0f, dv.width / 2.0f, false, false});
    }

    // V2.2: Detect overlap with previous label, alternate to staircase row if needed
    // V2.6: Prefer upper row so all labels sit at Simpsons' height
    if (!labelInfos.empty()) labelInfos[0].useUpperRow = true;
    for (size_t i = 1; i < labelInfos.size(); ++i) {
        // Find last label on row 0
        float prevRow0X = 0, prevRow0HalfW = 0;
        bool foundRow0 = false;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (!labelInfos[j].useUpperRow) {
                prevRow0X = labelInfos[j].centerX;
                prevRow0HalfW = labelInfos[j].labelHalfW;
                foundRow0 = true;
                break;
            }
        }
        float gapRow0 = foundRow0 ? (labelInfos[i].centerX - prevRow0X - prevRow0HalfW - labelInfos[i].labelHalfW) : 1e10f;

        // Find last label on row 1
        float prevRow1X = 0, prevRow1HalfW = 0;
        bool foundRow1 = false;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (labelInfos[j].useUpperRow) {
                prevRow1X = labelInfos[j].centerX;
                prevRow1HalfW = labelInfos[j].labelHalfW;
                foundRow1 = true;
                break;
            }
        }
        float gapRow1 = foundRow1 ? (labelInfos[i].centerX - prevRow1X - prevRow1HalfW - labelInfos[i].labelHalfW) : 1e10f;

        if (gapRow1 >= 4.0f) {
            labelInfos[i].useUpperRow = true;
        } else if (gapRow0 >= 4.0f) {
            labelInfos[i].useUpperRow = false;
        } else {
            labelInfos[i].useUpperRow = (gapRow1 > gapRow0);
        }
    }

    // V2.6: Separate overlap detection for value labels below the bar
    // V2.6: Prefer upper row so value labels align with station labels
    if (!labelInfos.empty()) labelInfos[0].valUpperRow = true;
    for (size_t i = 1; i < labelInfos.size(); ++i) {
        float prevRow0X = 0, prevRow0HalfW = 0;
        bool foundRow0 = false;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (!labelInfos[j].valUpperRow) {
                prevRow0X = labelInfos[j].centerX;
                prevRow0HalfW = labelInfos[j].valHalfW;
                foundRow0 = true;
                break;
            }
        }
        float gapRow0 = foundRow0 ? (labelInfos[i].centerX - prevRow0X - prevRow0HalfW - labelInfos[i].valHalfW) : 1e10f;

        float prevRow1X = 0, prevRow1HalfW = 0;
        bool foundRow1 = false;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (labelInfos[j].valUpperRow) {
                prevRow1X = labelInfos[j].centerX;
                prevRow1HalfW = labelInfos[j].valHalfW;
                foundRow1 = true;
                break;
            }
        }
        float gapRow1 = foundRow1 ? (labelInfos[i].centerX - prevRow1X - prevRow1HalfW - labelInfos[i].valHalfW) : 1e10f;

        if (gapRow1 >= 4.0f) {
            labelInfos[i].valUpperRow = true;
        } else if (gapRow0 >= 4.0f) {
            labelInfos[i].valUpperRow = false;
        } else {
            labelInfos[i].valUpperRow = (gapRow1 > gapRow0);
        }
    }

    size_t idx = 0;
    for (const auto& t : thresholds) {
        double ratio = segmentRatio(t.value);
        float mx = barX + static_cast<float>(ratio * barW);
        ctx->SetFillPaint(t.color);
        ctx->FillRectangle(Rect2Df(mx - 1.0f, barY - 4.0f, 2.0f, barH + 8.0f));

        if (!t.label.empty() && showLabels) {
            ctx->SetFontFamily("Sans");
            ctx->SetFontSize(10.0f);
            ctx->SetTextPaint(textColor);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di d = ctx->GetTextLineDimensions(t.label);
            float labelX = mx - d.width / 2.0f;
            // Clamp to card bounds (not bar bounds — labels can extend over padding zone)
            if (labelX < static_cast<float>(b.x) + 2.0f) labelX = static_cast<float>(b.x) + 2.0f;
            if (labelX + d.width > static_cast<float>(b.x + b.width) - 2.0f) {
                labelX = static_cast<float>(b.x + b.width) - 2.0f - d.width;
            }
            // V2.2: Staircase Y — upper row is even higher (above bar - 24)
            float labelY = labelInfos[idx].useUpperRow ? (barY - 34.0f) : (barY - 18.0f);
            ctx->DrawText(t.label, Point2Df(labelX, labelY));

            std::string valLabel = FormatValue(t.value) + (unit.empty() ? "" : (" " + unit));
            ctx->SetFontFamily("Sans");
            ctx->SetFontSize(9.0f);
            ctx->SetTextPaint(Color(140, 140, 155, 255));
            Size2Di dv = ctx->GetTextLineDimensions(valLabel);
            float valLabelX = mx - dv.width / 2.0f;
            if (valLabelX < static_cast<float>(b.x) + 2.0f) valLabelX = static_cast<float>(b.x) + 2.0f;
            if (valLabelX + dv.width > static_cast<float>(b.x + b.width) - 2.0f) {
                valLabelX = static_cast<float>(b.x + b.width) - 2.0f - dv.width;
            }
            // V2.2: Staircase Y for value labels too — alternate rows below bar
            float valLabelY = labelInfos[idx].valUpperRow ? (barY + barH + 38.0f) : (barY + barH + 22.0f);
            ctx->DrawText(valLabel, Point2Df(valLabelX, valLabelY));
        }
        ++idx;
    }

    double ratio = segmentRatio(currentValue);
    float ptrX = barX + static_cast<float>(ratio * barW);
    ctx->SetFillPaint(gaugeColor);
    ctx->FillRoundedRectangle(Rect2Df(ptrX - 2.0f, barY - 6.0f, 4.0f, barH + 12.0f), 2.0f);

    // Bottom value
    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

// V2.7.0: Configurable round-gauge renderer.
//   The CircularRing now honours a small style system so a dedicated demo tab can
//   showcase the breadth of round gauges:
//     * ringThickness     — width of the indicator (slider-driven)
//     * ringStyle         — SolidArc | Segmented | Dashed
//     * ringSegmentStyle  — Bars | Dots | Blocks (shape of each segment)
//     * fillStyle         — NoFill | StraightLevel | WavedLevel (liquid disc fill)
//     * trackColor        — colour of the unfilled portion
//   Order of drawing: liquid fill (behind) → track → value indicator → centre text.
void UltraCanvasGaugeDiagramElement::RenderCircularRing(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    const auto b = GetLocalBounds();

    // Liquid fill sits on the disc surface, inside the indicator ring.
    if (fillStyle != GaugeFillStyle::NoFill) {
        float innerR = std::max(6.0f, radius - ringThickness - 3.0f);
        DrawRingLiquidFill(ctx, center, innerR);
    }

    DrawRingTrackAndValue(ctx, center, radius);

    // Centre value text. When a liquid fill covers the disc the configured
    // textColor is used as-is (the demo picks a contrasting colour per card).
    {
        std::string label = FormatValue(currentValue) + (unit.empty() ? "" : unit);
        ctx->SetFontFamily("Sans");
        ctx->SetTextPaint(textColor);
        ctx->SetFontWeight(FontWeight::Light);

        // V2.7.1: Scale the value text to the ring so it always fits inside the
        // open centre. Start from a radius-proportional size, then measure and
        // shrink if the rendered width would spill past the inner diameter.
        float innerR   = std::max(8.0f, radius - ringThickness);
        float fontSize = std::min(40.0f, std::max(10.0f, innerR * 0.78f));
        ctx->SetFontSize(fontSize);
        Size2Di d = ctx->GetTextLineDimensions(label);
        float maxTextW = innerR * 1.7f;   // fit within the inner circle with margin
        if (d.width > maxTextW && d.width > 0) {
            fontSize = std::max(8.0f, fontSize * (maxTextW / static_cast<float>(d.width)));
            ctx->SetFontSize(fontSize);
            d = ctx->GetTextLineDimensions(label);
        }

        // When a centre icon/label is shown the value is lifted to make room,
        // so the value + content read as a single vertically-centred stack.
        bool hasContent =
            (ringCenterContent == GaugeRingCenterContent::TextLabel && !ringCenterLabel.empty()) ||
            (ringCenterContent == GaugeRingCenterContent::Icon);
        float valueTop = center.y - d.height / 2.0f;
        if (hasContent) valueTop -= d.height * 0.32f;
        ctx->DrawText(label, Point2Df(center.x - d.width / 2.0f, valueTop));

        if (ringCenterContent == GaugeRingCenterContent::TextLabel && !ringCenterLabel.empty()) {
            ctx->SetFontSize(std::min(13.0f, std::max(9.0f, fontSize * 0.5f)));
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(textColor);
            Size2Di ld = ctx->GetTextLineDimensions(ringCenterLabel);
            ctx->DrawText(ringCenterLabel,
                          Point2Df(center.x - ld.width / 2.0f, valueTop + d.height + 6.0f));
        } else if (ringCenterContent == GaugeRingCenterContent::Icon) {
            DrawRingCenterIcon(ctx, Point2Df(center.x, valueTop + d.height + 12.0f), 13.0f);
        }
    }

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(center.x, static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }
}

// Health-band colour for a 0..1 value ratio: green (high) -> yellow -> orange
// -> red (low). The 30%..70% band fades smoothly between yellow and green so
// the indicator "turns into yellow" as the value drops.
static Color RingValueBandColor(double ratio) {
    auto lerp = [](const Color& a, const Color& b, float t) {
        return Color(static_cast<uint8_t>(a.r + (b.r - a.r) * t),
                     static_cast<uint8_t>(a.g + (b.g - a.g) * t),
                     static_cast<uint8_t>(a.b + (b.b - a.b) * t), 255);
    };
    const Color green (40, 200, 80, 255);
    const Color yellow(240, 210, 40, 255);
    const Color orange(245, 150, 40, 255);
    const Color red   (230, 55, 45, 255);
    double pct = ratio * 100.0;
    if (pct >= 70.0) return green;                                   // 100..70%
    if (pct >= 30.0) return lerp(yellow, green,
                                 static_cast<float>((pct - 30.0) / 40.0)); // 69..30%
    if (pct >= 11.0) return orange;                                 // 29..11%
    return red;                                                     // <=10%
}

// Draws the unfilled track plus the value indicator using the active ring style.
void UltraCanvasGaugeDiagramElement::DrawRingTrackAndValue(
    IRenderContext* ctx, const Point2Df& center, float radius) {
    // Ring zero/null position is configurable (default -90° = 12 o'clock); the
    // indicator always fills a full 360° clockwise from there.
    const float startRad = static_cast<float>(ringStartAngleDeg * M_PI / 180.0);
    const float sweep    = static_cast<float>(2.0 * M_PI);     // full 360°
    const float endRad   = startRad + sweep;
    const double ratio   = std::max(0.0, std::min(1.0, ValueToRatio(currentValue)));
    const Color fillC    = ringValueColorBands ? RingValueBandColor(ratio)
                                               : GetColorForValue(currentValue);
    const float thickness = ringThickness;

    // When faded, lit portions of the indicator use a soft lightened gradient
    // instead of the flat colour; the track stays as configured.
    std::shared_ptr<IPaintPattern> fadePat = ringFaded ? MakeFadedPaint(ctx, fillC, center, radius) : nullptr;
    auto setLitStroke = [&]() {
        if (fadePat) ctx->SetStrokePaint(fadePat); else ctx->SetStrokePaint(fillC);
    };

    if (ringStyle == GaugeRingStyle::SolidArc) {
        // Continuous track + rounded value arc.
        DrawArcSection(ctx, center, radius, startRad, endRad, trackColor, thickness);
        if (ratio > 0.0001) {
            float fillEnd = startRad + static_cast<float>(ratio * sweep);
            ctx->ClearPath();
            ctx->Arc(center.x, center.y, radius, startRad, fillEnd);
            setLitStroke();
            ctx->SetStrokeWidth(thickness);
            ctx->SetLineCap(LineCap::Round);
            ctx->Stroke();
        }
        return;
    }

    if (ringStyle == GaugeRingStyle::Spectrum) {
        // A smooth fade through up to 100 user colours. The whole ring shows the
        // colour scale faintly (the available indication range); the arc up to the
        // current value is drawn at full opacity, so the leading-edge colour
        // pinpoints the value among up to 100 distinct levels.
        const int stopCount = std::max(2, static_cast<int>(ringGradientColors.size()));
        const int steps = std::max(stopCount, 120);   // enough sub-arcs for a smooth blend
        const float segSweep = sweep / static_cast<float>(steps);
        const float overlap  = segSweep * 0.6f;        // overlap butt-capped arcs to hide seams
        for (int i = 0; i < steps; i++) {
            float s = startRad + i * segSweep;
            float e = s + segSweep + overlap;
            double t = (i + 0.5) / static_cast<double>(steps);
            Color col = SampleGradientColor(t);
            if (t > ratio) col.a = static_cast<uint8_t>(col.a * 0.20f);  // faint scale past the value
            ctx->ClearPath();
            ctx->Arc(center.x, center.y, radius, s, e);
            ctx->SetStrokePaint(col);
            ctx->SetStrokeWidth(thickness);
            ctx->SetLineCap(LineCap::Butt);
            ctx->Stroke();
        }
        // Round the lit leading edge so the value tip reads cleanly.
        if (ratio > 0.0001) {
            float tipEnd = startRad + static_cast<float>(ratio * sweep);
            ctx->ClearPath();
            ctx->Arc(center.x, center.y, radius, std::max(startRad, tipEnd - segSweep), tipEnd);
            ctx->SetStrokePaint(SampleGradientColor(ratio));
            ctx->SetStrokeWidth(thickness);
            ctx->SetLineCap(LineCap::Round);
            ctx->Stroke();
        }
        return;
    }

    if (ringStyle == GaugeRingStyle::SegmentedRing) {
        // A few chunky arc segments around the circle (the battery / activity-ring
        // look). Honours: segment count, rounded vs sharp ends, and an optional
        // border drawn as a slightly wider outline beneath each segment.
        int count = std::max(4, ringSegmentCount);   // floor of 4 (matches SetRingSegmentCount)
        int litCount = static_cast<int>(std::ceil(ratio * count - 1e-6));
        LineCap cap = ringSegmentRounded ? LineCap::Round : LineCap::Butt;
        float gapFrac = 0.30f;                       // generous gaps between chunks
        float segSweep = sweep / static_cast<float>(count);
        float gap = segSweep * gapFrac;
        float borderW = ringBorder ? std::max(1.5f, thickness * 0.16f) : 0.0f;
        // V2.9.1: rounded caps overhang the arc endpoints by the stroke's cap
        // radius (the border pass is wider by borderW per side). Inset the
        // endpoints by that overhang — converted to radians at this radius —
        // so each chunk stays inside its segment slot instead of spilling
        // into, and overlapping, its neighbours.
        float capInset = (cap == LineCap::Round)
            ? (thickness * 0.5f + borderW) / radius : 0.0f;
        for (int i = 0; i < count; i++) {
            float s = startRad + i * segSweep + gap * 0.5f + capInset;
            float e = startRad + (i + 1) * segSweep - gap * 0.5f - capInset;
            if (e < s) {   // caps ate the whole slot: collapse to a round dot
                float mid = startRad + (i + 0.5f) * segSweep;
                s = mid - 1e-3f;
                e = mid + 1e-3f;
            }
            bool lit = (i < litCount);
            if (borderW > 0.0f) {
                ctx->ClearPath();
                ctx->Arc(center.x, center.y, radius, s, e);
                ctx->SetStrokePaint(ringBorderColor);
                ctx->SetStrokeWidth(thickness + 2.0f * borderW);
                ctx->SetLineCap(cap);
                ctx->Stroke();
            }
            ctx->ClearPath();
            ctx->Arc(center.x, center.y, radius, s, e);
            if (lit) setLitStroke(); else ctx->SetStrokePaint(trackColor);
            ctx->SetStrokeWidth(thickness);
            ctx->SetLineCap(cap);
            ctx->Stroke();
        }
        return;
    }

    // ----- Segmented / Dashed: a series of discrete marks around the circle -----
    int count = std::max(4, ringSegmentCount);
    int litCount = static_cast<int>(std::ceil(ratio * count - 1e-6));

    if (ringSegmentStyle == GaugeRingSegmentStyle::Blocks) {
        // Chunky rounded arc blocks with a small gap between them.
        float gapFrac = (ringStyle == GaugeRingStyle::Dashed) ? 0.55f : 0.25f;
        float segSweep = sweep / static_cast<float>(count);
        float gap = segSweep * gapFrac;
        // V2.9.1: same rounded-cap overhang correction as SegmentedRing above,
        // so thick blocks don't bleed past their slot and overlap neighbours.
        float capInset = (thickness * 0.5f) / radius;
        for (int i = 0; i < count; i++) {
            float s = startRad + i * segSweep + gap * 0.5f + capInset;
            float e = startRad + (i + 1) * segSweep - gap * 0.5f - capInset;
            if (e < s) {   // caps ate the whole slot: collapse to a round dot
                float mid = startRad + (i + 0.5f) * segSweep;
                s = mid - 1e-3f;
                e = mid + 1e-3f;
            }
            ctx->ClearPath();
            ctx->Arc(center.x, center.y, radius, s, e);
            if (i < litCount) setLitStroke(); else ctx->SetStrokePaint(trackColor);
            ctx->SetStrokeWidth(thickness);
            ctx->SetLineCap(LineCap::Round);
            ctx->Stroke();
        }
    } else if (ringSegmentStyle == GaugeRingSegmentStyle::Bars) {
        // Thin radial bars / ticks (tachymeter look). Bars straddle the radius.
        float halfLen = thickness * 0.5f + 4.0f;
        float barW = std::max(1.5f, (2.0f * static_cast<float>(M_PI) * radius)
                                    / static_cast<float>(count) * 0.45f);
        for (int i = 0; i < count; i++) {
            float a = startRad + (i + 0.5f) * (sweep / static_cast<float>(count));
            float cosA = std::cos(a), sinA = std::sin(a);
            float ix = center.x + (radius - halfLen) * cosA;
            float iy = center.y + (radius - halfLen) * sinA;
            float ox = center.x + (radius + halfLen) * cosA;
            float oy = center.y + (radius + halfLen) * sinA;
            if (i < litCount) setLitStroke(); else ctx->SetStrokePaint(trackColor);
            ctx->SetStrokeWidth(barW);
            ctx->SetLineCap(LineCap::Butt);
            ctx->ClearPath();
            ctx->MoveTo(ix, iy);
            ctx->LineTo(ox, oy);
            ctx->Stroke();
        }
    } else { // Dots
        float dotR = std::max(2.0f, thickness * 0.5f);
        // A small outline around each dot makes them read crisply, especially
        // the lit value-coloured dots against the pale track dots.
        float borderW = std::max(1.0f, dotR * 0.22f);
        for (int i = 0; i < count; i++) {
            float a = startRad + (i + 0.5f) * (sweep / static_cast<float>(count));
            Point2Df p(center.x + radius * std::cos(a), center.y + radius * std::sin(a));
            Color dotColor = (i < litCount) ? fillC : trackColor;
            ctx->DrawFilledCircle(p, dotR, dotColor, ringBorderColor, borderW);
        }
    }
}

// Fills the disc surface up to the current value, clipped to a circle, with either
// a flat (StraightLevel) or sinusoidal (WavedLevel) surface — the liquid-gauge look.
void UltraCanvasGaugeDiagramElement::DrawRingLiquidFill(
    IRenderContext* ctx, const Point2Df& center, float innerRadius) {
    double ratio = std::max(0.0, std::min(1.0, ValueToRatio(currentValue)));
    Color fillC = GetColorForValue(currentValue);
    // Faded fill: a soft two-tone gradient across the disc (light tint -> darker
    // shade) so the liquid clearly reads as faded. A flat lightened colour (the
    // old behaviour) showed no visible fading.
    std::shared_ptr<IPaintPattern> fillFadePat =
        fillFaded ? MakeFadedPaint(ctx, fillC, center, innerRadius) : nullptr;

    ctx->PushState();
    // Clip everything that follows to the inner disc.
    ctx->ClearPath();
    ctx->Circle(center.x, center.y, innerRadius);
    ctx->ClipPath();

    // Soft track behind the liquid so an empty area still reads as a disc.
    Color discBg = trackColor;
    discBg.a = 60;
    ctx->SetFillPaint(discBg);
    ctx->FillCircle(center, innerRadius);

    float top = center.y - innerRadius;
    float bottom = center.y + innerRadius;
    float level = bottom - static_cast<float>(ratio) * (bottom - top);
    float left = center.x - innerRadius - 4.0f;
    float right = center.x + innerRadius + 4.0f;

    float amp = (fillStyle == GaugeFillStyle::WavedLevel)
                ? std::max(3.0f, innerRadius * 0.10f) : 0.0f;
    float wavelength = std::max(20.0f, innerRadius); // ~2 crests across the disc

    if (fillFadePat) ctx->SetFillPaint(fillFadePat); else ctx->SetFillPaint(fillC);
    ctx->ClearPath();
    ctx->MoveTo(left, bottom + 4.0f);
    ctx->LineTo(left, level);
    if (amp > 0.0f) {
        const int steps = 48;
        for (int i = 0; i <= steps; i++) {
            float x = left + (right - left) * (static_cast<float>(i) / steps);
            float phase = (x - left) / wavelength * 2.0f * static_cast<float>(M_PI);
            float y = level + amp * std::sin(phase);
            ctx->LineTo(x, y);
        }
    } else {
        ctx->LineTo(right, level);
    }
    ctx->LineTo(right, bottom + 4.0f);
    ctx->ClosePath();
    ctx->Fill();

    ctx->PopState();
}

// Draws a small built-in icon glyph centred at `center`, sized so its height is
// roughly `size` pixels. Used as round-gauge centre content beneath the value.
// The glyph is tinted with the current value colour to match the indicator.
void UltraCanvasGaugeDiagramElement::DrawRingCenterIcon(
    IRenderContext* ctx, const Point2Df& center, float size) {
    Color c = GetColorForValue(currentValue);

    if (ringCenterIcon == GaugeRingIcon::Battery) {
        // Horizontal battery: body outline + terminal nub + a level fill that
        // tracks the current value.
        float h = size;
        float w = h * 2.0f;
        float x = center.x - (w + 3.0f) / 2.0f;  // include the terminal in centring
        float y = center.y - h / 2.0f;
        float termW = std::max(2.0f, h * 0.22f);
        float termH = h * 0.5f;

        ctx->SetStrokePaint(c);
        ctx->SetStrokeWidth(std::max(1.2f, h * 0.12f));
        ctx->DrawRoundedRectangle(Rect2Df(x, y, w, h), std::max(1.5f, h * 0.16f));

        ctx->SetFillPaint(c);
        ctx->FillRectangle(Rect2Df(x + w + 1.0f, y + (h - termH) / 2.0f, termW, termH));

        float pad = std::max(1.5f, h * 0.18f);
        double ratio = std::max(0.0, std::min(1.0, ValueToRatio(currentValue)));
        float fillW = (w - 2.0f * pad) * static_cast<float>(ratio);
        if (fillW > 0.5f) {
            ctx->SetFillPaint(c);
            ctx->FillRectangle(Rect2Df(x + pad, y + pad, fillW, h - 2.0f * pad));
        }
    } else { // Bolt
        float cx = center.x, cy = center.y;
        float s = size / 12.0f;  // scale relative to the original 24px bolt
        ctx->SetFillPaint(c);
        ctx->ClearPath();
        ctx->MoveTo(cx - 5.0f * s, cy - 12.0f * s);
        ctx->LineTo(cx + 3.0f * s, cy - 2.0f * s);
        ctx->LineTo(cx - 1.0f * s, cy);
        ctx->LineTo(cx + 5.0f * s, cy + 12.0f * s);
        ctx->LineTo(cx - 3.0f * s, cy + 2.0f * s);
        ctx->LineTo(cx + 1.0f * s, cy);
        ctx->ClosePath();
        ctx->Fill();
    }
}

// Builds a soft diagonal linear gradient that runs from a lightened tint of the
// base colour to the full colour across the element bounds. Used for the "faded"
// ring look — strokes/segments at different positions pick up different shades,
// giving a gentle two-tone sweep around the ring.
std::shared_ptr<IPaintPattern> UltraCanvasGaugeDiagramElement::MakeFadedPaint(
    IRenderContext* ctx, const Color& base, const Point2Df& center, float radius) const {
    // A diagonal two-tone sweep from a pale tint to a darker shade of the base
    // colour. The gradient spans the gauge circle (not the whole element), so the
    // full light->dark range is actually visible across the ring/disc — spanning
    // the element bounds previously left only a near-uniform sliver on the ring.
    std::vector<GradientStop> stops{
        GradientStop(0.0, base.Lighten(0.70f)),
        GradientStop(1.0, base.Darken(0.30f)),
    };
    return ctx->CreateLinearGradientPattern(
        static_cast<double>(center.x - radius), static_cast<double>(center.y - radius),
        static_cast<double>(center.x + radius), static_cast<double>(center.y + radius),
        stops);
}

// Linear-interpolates the Spectrum colour list at t in [0,1]. With N colours the
// stops sit at 0, 1/(N-1), ... 1, so the ring fades smoothly through all of them.
Color UltraCanvasGaugeDiagramElement::SampleGradientColor(double t) const {
    if (ringGradientColors.empty()) return gaugeColor;
    if (ringGradientColors.size() == 1) return ringGradientColors.front();
    t = std::max(0.0, std::min(1.0, t));
    const int last = static_cast<int>(ringGradientColors.size()) - 1;
    double pos = t * last;
    int i = static_cast<int>(std::floor(pos));
    if (i >= last) return ringGradientColors.back();
    double f = pos - i;
    const Color& a = ringGradientColors[i];
    const Color& b = ringGradientColors[i + 1];
    auto lerp = [f](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::lround(x + (static_cast<double>(y) - x) * f));
    };
    return Color(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a));
}

// V2.1 FIX: Battery title at top with reserved space, bolt + percent visible
void UltraCanvasGaugeDiagramElement::RenderBattery(IRenderContext* ctx) {
    const auto b = GetLocalBounds();

    // Title at top FIRST
    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 12.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;
    float availH = static_cast<float>(b.height) - topReserved - bottomReserved;

    float batW = std::min(static_cast<float>(b.width) - 2.0f * kPaddingSide, 180.0f);
    float batH = std::min(availH - 8.0f, 90.0f);
    float batX = static_cast<float>(b.x) + (static_cast<float>(b.width) - batW) / 2.0f;
    float batY = static_cast<float>(b.y) + topReserved + (availH - batH) / 2.0f;
    float terminalW = 6.0f;
    float terminalH = batH / 3.0f;

    double ratio = ValueToRatio(currentValue);

    ctx->SetStrokePaint(Color(180, 180, 195, 255));
    ctx->SetStrokeWidth(2.0f);
    ctx->DrawRoundedRectangle(Rect2Df(batX, batY, batW, batH), 4.0f);

    ctx->SetFillPaint(Color(180, 180, 195, 255));
    ctx->FillRectangle(Rect2Df(batX + batW, batY + (batH - terminalH) / 2.0f, terminalW, terminalH));

    Color fillC = GetColorForValue(currentValue);
    if (ranges.empty()) {
        if (ratio < 0.2) fillC = Color(255, 80, 80, 255);
        else if (ratio < 0.5) fillC = Color(255, 190, 60, 255);
        else fillC = Color(80, 200, 120, 255);
    }

    float fillMargin = 4.0f;

    if (batteryStyle == GaugeBatteryStyle::BarPointer) {
        float fillW = (batW - 2.0f * fillMargin) * static_cast<float>(ratio);
        if (fillW > 0.5f) {
            ctx->SetFillPaint(fillC);
            ctx->FillRectangle(Rect2Df(batX + fillMargin, batY + fillMargin,
                                       fillW, batH - 2.0f * fillMargin));
        }
    } else {
        int segs = std::max(2, segmentCount);
        int litSegs = static_cast<int>(ratio * segs);
        float gap = 2.0f;
        float segW = (batW - 2.0f * fillMargin - gap * (segs - 1)) / segs;
        for (int i = 0; i < segs; i++) {
            if (i < litSegs) ctx->SetFillPaint(fillC);
            else ctx->SetFillPaint(Color(225, 226, 235, 255));
            float sx = batX + fillMargin + i * (segW + gap);
            ctx->FillRectangle(Rect2Df(sx, batY + fillMargin, segW, batH - 2.0f * fillMargin));
        }
    }

    // Bolt
    if (showBolt && ratio > 0.1) {
        float cx = batX + batW / 2.0f;
        float cy = batY + batH / 2.0f;
        ctx->SetFillPaint(Color(255, 255, 255, 220));
        ctx->ClearPath();
        ctx->MoveTo(cx - 5.0f, cy - 12.0f);
        ctx->LineTo(cx + 3.0f, cy - 2.0f);
        ctx->LineTo(cx - 1.0f, cy);
        ctx->LineTo(cx + 5.0f, cy + 12.0f);
        ctx->LineTo(cx - 3.0f, cy + 2.0f);
        ctx->LineTo(cx + 1.0f, cy);
        ctx->ClosePath();
        ctx->Fill();
    }

    // Percent below
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << (ratio * 100.0) << "%";
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, oss.str(),
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  16.0f, textColor);
}

// V2.1 FIX: Thermometer with proper layout for tube + bulb + value below
void UltraCanvasGaugeDiagramElement::RenderThermometer(IRenderContext* ctx) {
    const auto b = GetLocalBounds();

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 8.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;

    float bulbR = 18.0f;
    float tubeW = 22.0f;
    float availH = static_cast<float>(b.height) - topReserved - bottomReserved;
    float tubeH = availH - bulbR * 2.0f - 6.0f;
    float tubeX = static_cast<float>(b.x + b.width / 2) - tubeW / 2.0f;
    float tubeY = static_cast<float>(b.y) + topReserved;
    float bulbY = tubeY + tubeH;

    double ratio = ValueToRatio(currentValue);
    float fillH = static_cast<float>(tubeH * ratio);

    ctx->SetStrokePaint(Color(180, 180, 195, 255));
    ctx->SetStrokeWidth(1.2f);
    ctx->DrawRoundedRectangle(Rect2Df(tubeX, tubeY, tubeW, tubeH), 6.0f);

    ctx->SetStrokePaint(Color(180, 180, 195, 255));
    ctx->DrawCircle(Point2Df(tubeX + tubeW / 2.0f, bulbY + bulbR), bulbR);

    // Fill
    Color fillC = GetColorForValue(currentValue);
    ctx->SetFillPaint(fillC);
    float fillStartY = tubeY + tubeH - fillH;
    ctx->FillRectangle(Rect2Df(tubeX + 3.0f, fillStartY, tubeW - 6.0f, fillH));
    ctx->FillCircle(Point2Df(tubeX + tubeW / 2.0f, bulbY + bulbR), bulbR - 3.0f);

    if (showLabels) {
        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(9.0f);
        ctx->SetTextPaint(Color(140, 140, 150, 255));
        int marks = std::max(2, majorTickCount);
        for (int i = 0; i <= marks; i++) {
            double val = minValue + (maxValue - minValue) * i / marks;
            float my = tubeY + tubeH - static_cast<float>(static_cast<double>(tubeH) * i / marks);
            ctx->SetStrokePaint(Color(180, 180, 195, 200));
            ctx->SetStrokeWidth(0.8f);
            ctx->ClearPath();
            ctx->MoveTo(tubeX + tubeW + 2.0f, my);
            ctx->LineTo(tubeX + tubeW + 7.0f, my);
            ctx->Stroke();
            std::string lbl = FormatValue(val) + (unit.empty() ? "" : "\xC2\xB0");
            Size2Di ld = ctx->GetTextLineDimensions(lbl);
            ctx->DrawText(lbl, Point2Df(tubeX + tubeW + 11.0f, my - ld.height / 2.0f));
        }
    }

    // Value below bulb
    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

// V2.1 FIX: Cylinder with proper layout - title top, cylinder middle, value bottom
// V2.2 FIX: Increased top reservation so title isn't covered by top ellipse arc
void UltraCanvasGaugeDiagramElement::RenderCylinder(IRenderContext* ctx) {
    const auto b = GetLocalBounds();

    if (!title.empty()) {
        DrawTitleText(ctx, Point2Df(static_cast<float>(b.x + b.width / 2),
                                     static_cast<float>(b.y) + kPaddingTop - kTitleRaise));
    }

    // V2.2: Add extra space to clear the top ellipse rim above cylY
    float topReserved = kPaddingTop + (title.empty() ? 0.0f : kTitleHeight + 20.0f);
    float bottomReserved = kPaddingBottom + kValueHeight + 4.0f;
    float availH = static_cast<float>(b.height) - topReserved - bottomReserved;

    float cylW = std::min(static_cast<float>(b.width) - 2.0f * kPaddingSide, 100.0f);
    float cylH = availH;
    float cylX = static_cast<float>(b.x + b.width / 2) - cylW / 2.0f;
    float cylY = static_cast<float>(b.y) + topReserved;
    float ellipseH = 16.0f;

    double ratio = ValueToRatio(currentValue);

    ctx->SetStrokePaint(Color(180, 180, 195, 255));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRoundedRectangle(Rect2Df(cylX, cylY, cylW, cylH), ellipseH / 2.0f);

    Color fillC = GetColorForValue(currentValue);
    float fillH = static_cast<float>((cylH - ellipseH) * ratio);
    if (fillH > 0.5f) {
        Color fillDark(
            static_cast<uint8_t>(std::max(0, static_cast<int>(fillC.r) - 30)),
            static_cast<uint8_t>(std::max(0, static_cast<int>(fillC.g) - 30)),
            static_cast<uint8_t>(std::max(0, static_cast<int>(fillC.b) - 30)),
            fillC.a);
        float fillTop = cylY + cylH - ellipseH - fillH;
        ctx->SetFillPaint(fillC);
        ctx->FillRectangle(Rect2Df(cylX, fillTop + ellipseH / 2.0f, cylW, fillH));
        ctx->SetFillPaint(fillDark);
        ctx->FillEllipse(Rect2Df(cylX, fillTop, cylW, ellipseH));
        ctx->SetFillPaint(fillC);
        ctx->FillEllipse(Rect2Df(cylX, cylY + cylH - ellipseH, cylW, ellipseH));
    }

    // Value label at bottom (clear position)
    std::string vt = FormatValue(currentValue) + (unit.empty() ? "" : (" " + unit));
    float valueY = static_cast<float>(b.y + b.height) - kPaddingBottom - 4.0f;
    DrawValueText(ctx, vt,
                  Point2Df(static_cast<float>(b.x + b.width / 2), valueY),
                  13.0f, textColor);
}

// V2.1 FIX: Digital - value centered, title at bottom (LCD style)
void UltraCanvasGaugeDiagramElement::RenderDigital(IRenderContext* ctx) {
    const auto b = GetLocalBounds();
    float margin = 8.0f;
    float dispX = static_cast<float>(b.x) + margin;
    float dispY = static_cast<float>(b.y) + margin;
    float dispW = static_cast<float>(b.width) - 2.0f * margin;
    float dispH = static_cast<float>(b.height) - 2.0f * margin;

    ctx->SetFillPaint(Color(20, 20, 25, 240));
    ctx->FillRoundedRectangle(Rect2Df(dispX, dispY, dispW, dispH), 6.0f);

    if (showGlow) {
        Color glowC = GetColorForValue(currentValue);
        Color softGlow(glowC.r, glowC.g, glowC.b, 25);
        ctx->SetFillPaint(softGlow);
        ctx->FillRoundedRectangle(Rect2Df(dispX + 2.0f, dispY + 2.0f, dispW - 4.0f, dispH - 4.0f), 4.0f);
    }

    std::string displayText;
    if (digitalClock) {
        std::time_t now = std::time(nullptr);
        std::tm* lt = std::localtime(&now);
        std::ostringstream css;
        css << std::setfill('0')
            << std::setw(2) << lt->tm_hour << ":"
            << std::setw(2) << lt->tm_min << ":"
            << std::setw(2) << lt->tm_sec;
        displayText = css.str();
    } else {
        displayText = FormatValue(currentValue);
        if (!unit.empty()) displayText += " " + unit;
    }

    Color dispColor = GetColorForValue(currentValue);
    // No ranges: clock uses the configured gauge colour (so an LED green/red can
    // be picked); a plain numeric readout keeps the classic cyan.
    if (ranges.empty()) dispColor = digitalClock ? gaugeColor : Color(0, 180, 255, 255);

    // V2.1 FIX: Better font sizing logic with title space reservation
    float titleSpace = title.empty() ? 0.0f : 18.0f;
    float fontSize = std::min(dispH - 24.0f - titleSpace, 44.0f);
    ctx->SetFontFamily(digitalFontFamily);
    ctx->SetFontSize(fontSize);
    ctx->SetFontWeight(FontWeight::Light);
    ctx->SetTextPaint(dispColor);
    Size2Di dims = ctx->GetTextLineDimensions(displayText);

    while (dims.width > dispW - 16.0f && fontSize > 10.0f) {
        fontSize -= 2.0f;
        ctx->SetFontSize(fontSize);
        dims = ctx->GetTextLineDimensions(displayText);
    }

    // Center vertically with offset for title
    float textY = dispY + (dispH - titleSpace - dims.height) / 2.0f;
    ctx->DrawText(displayText, Point2Df(dispX + (dispW - dims.width) / 2.0f, textY));

    if (!title.empty()) {
        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(10.0f);
        ctx->SetTextPaint(Color(140, 140, 150, 255));
        ctx->SetFontWeight(FontWeight::Normal);
        Size2Di td = ctx->GetTextLineDimensions(title);
        ctx->DrawText(title, Point2Df(
            dispX + (dispW - td.width) / 2.0f,
            dispY + dispH - 22.0f));
    }
}

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

std::shared_ptr<UltraCanvasGaugeDiagramElement> CreateGaugeDiagramElement(
    const std::string& id, long x, long y, long width, long height) {
    return std::make_shared<UltraCanvasGaugeDiagramElement>(id, x, y, width, height);
}

} // namespace UltraCanvas