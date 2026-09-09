// include/UltraCanvasToneCurve.h
// The tone curve model: control points, monotone interpolation and the 256-entry
// lookup tables an 8-bit image is mapped through ("Curves" in an image editor).
//
// Deliberately free of UI and image-library includes, so the same model serves
// the interactive editor (UltraCanvasCurveEditor), the widgets that store an
// adjustment (UltraCanvasMediaViewer's MediaAdjustments) and any batch code
// that only wants the tables. Applying them to pixels is
// PixelFX::Colour::MapLut()'s job.
//
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== ONE CONTROL POINT =====
// Both coordinates are normalised 0..1 (input = original tone, output = the
// tone it is mapped to), so a curve is independent of the bit depth it is
// later applied at.
struct ToneCurvePoint {
    float input = 0.0f;
    float output = 0.0f;

    ToneCurvePoint() = default;
    ToneCurvePoint(float in, float out) : input(in), output(out) {}
};

// ===== A SINGLE TONE CURVE =====
// Always holds at least the two endpoints; they keep their input (0 and 1) but
// their output can be dragged, which is how black and white points are set.
// Interior points are added, moved and removed freely and stay sorted by input.
class UltraCanvasToneCurve {
public:
    static constexpr int MaxPoints = 16;
    // Two points closer than this in input are considered the same position
    // (a new point is not inserted, a dragged one is pushed aside).
    static constexpr float MinPointDistance = 1.0f / 255.0f;

    UltraCanvasToneCurve() { Reset(); }

    // ===== POINTS =====
    const std::vector<ToneCurvePoint>& GetPoints() const { return points; }
    int GetPointCount() const { return static_cast<int>(points.size()); }
    // Replaces the whole set; sorted, clamped and padded with endpoints so the
    // curve stays valid whatever the caller passes.
    void SetPoints(std::vector<ToneCurvePoint> pts);

    // Inserts a point and returns its index, or -1 when the curve is full or a
    // point already sits at that input.
    int  AddPoint(float in, float out);
    // Moves the point at `index`. Endpoints keep their input; interior points
    // are kept strictly between their neighbours. Returns the (possibly new)
    // index of the moved point, or -1 when `index` is out of range.
    int  MovePoint(int index, float in, float out);
    // Endpoints cannot be removed; returns false when `index` is one of them.
    bool RemovePoint(int index);
    // Index of the point within `radius` (in curve space) of (in, out), or -1.
    int  FindPointNear(float in, float out, float radius) const;
    void Reset();

    // ===== EVALUATION =====
    // Monotone cubic (Fritsch-Carlson) interpolation, clamped to 0..1.
    float Evaluate(float x) const;
    // The curve sampled into the 256 entries an 8-bit channel is mapped through.
    std::array<uint8_t, 256> BuildLut() const;
    // True while the curve is the identity mapping (two endpoints, 0->0, 1->1),
    // i.e. applying it would leave every pixel untouched.
    bool IsIdentity() const;

    // ===== SERIALISATION =====
    // "in,out;in,out;…" with three decimals — small enough for a settings file
    // or a preset list. FromString() returns false (and leaves the curve
    // untouched) when the text does not parse.
    std::string ToString() const;
    bool FromString(const std::string& text);

private:
    void SortAndClamp();

    std::vector<ToneCurvePoint> points;
};

// ===== WHICH CURVE OF A SET =====
// RGB is the master curve applied to every colour channel after that channel's
// own curve, exactly like the channel selector of an image editor's Curves box.
enum class ToneCurveChannel { RGB = 0, Red = 1, Green = 2, Blue = 3 };

// ===== THE FOUR CURVES THAT MAKE UP A COLOUR ADJUSTMENT =====
struct ToneCurveSet {
    UltraCanvasToneCurve rgb;
    UltraCanvasToneCurve red;
    UltraCanvasToneCurve green;
    UltraCanvasToneCurve blue;

    UltraCanvasToneCurve& Channel(ToneCurveChannel c);
    const UltraCanvasToneCurve& Channel(ToneCurveChannel c) const;

    bool IsIdentity() const {
        return rgb.IsIdentity() && red.IsIdentity() &&
               green.IsIdentity() && blue.IsIdentity();
    }
    void Reset() { rgb.Reset(); red.Reset(); green.Reset(); blue.Reset(); }

    // The three 256-entry tables an RGB image is mapped through: the channel's
    // own curve first, then the master curve. Ready for
    // PixelFX::Colour::MapLut().
    std::array<std::array<uint8_t, 256>, 3> BuildChannelLuts() const;

    static std::string ChannelName(ToneCurveChannel c);
};

} // namespace UltraCanvas
