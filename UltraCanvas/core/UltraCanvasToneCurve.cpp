// core/UltraCanvasToneCurve.cpp
// Tone curve model: monotone cubic interpolation, lookup table generation and
// the four-curve set an image adjustment is made of.
// See include/UltraCanvasToneCurve.h.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasToneCurve.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace UltraCanvas {

namespace {
    inline float Clamp01(float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
}

// ===========================================================================
// TONE CURVE
// ===========================================================================

void UltraCanvasToneCurve::Reset() {
    points.clear();
    points.push_back(ToneCurvePoint(0.0f, 0.0f));
    points.push_back(ToneCurvePoint(1.0f, 1.0f));
}

void UltraCanvasToneCurve::SortAndClamp() {
    for (auto& p : points) {
        p.input  = Clamp01(p.input);
        p.output = Clamp01(p.output);
    }
    std::stable_sort(points.begin(), points.end(),
                     [](const ToneCurvePoint& a, const ToneCurvePoint& b) {
                         return a.input < b.input;
                     });
    // The endpoints are always present and pinned to the edges of the input
    // range; only their output travels.
    if (points.empty() || points.front().input > 0.0f) {
        points.insert(points.begin(), ToneCurvePoint(0.0f, points.empty() ? 0.0f : points.front().output));
    }
    if (points.back().input < 1.0f) {
        points.push_back(ToneCurvePoint(1.0f, points.back().output));
    }
    points.front().input = 0.0f;
    points.back().input  = 1.0f;
}

void UltraCanvasToneCurve::SetPoints(std::vector<ToneCurvePoint> pts) {
    points = std::move(pts);
    if (points.size() < 2) Reset();
    else {
        if (static_cast<int>(points.size()) > MaxPoints) points.resize(MaxPoints);
        SortAndClamp();
    }
}

int UltraCanvasToneCurve::AddPoint(float in, float out) {
    if (static_cast<int>(points.size()) >= MaxPoints) return -1;
    in  = Clamp01(in);
    out = Clamp01(out);
    for (const auto& p : points) {
        if (std::fabs(p.input - in) < MinPointDistance) return -1;
    }
    points.push_back(ToneCurvePoint(in, out));
    SortAndClamp();
    for (size_t i = 0; i < points.size(); ++i) {
        if (std::fabs(points[i].input - in) < MinPointDistance * 0.5f) return static_cast<int>(i);
    }
    return -1;
}

int UltraCanvasToneCurve::MovePoint(int index, float in, float out) {
    if (index < 0 || index >= static_cast<int>(points.size())) return -1;
    out = Clamp01(out);

    bool isFirst = (index == 0);
    bool isLast  = (index == static_cast<int>(points.size()) - 1);
    if (isFirst || isLast) {
        // Endpoints keep their input; dragging them sets the black / white point.
        points[index].output = out;
        return index;
    }

    // Interior points stay strictly between their neighbours, so the curve
    // never folds back on itself.
    float lower = points[index - 1].input + MinPointDistance;
    float upper = points[index + 1].input - MinPointDistance;
    if (lower > upper) {   // no room left — keep the point where it is
        points[index].output = out;
        return index;
    }
    points[index].input  = std::max(lower, std::min(upper, Clamp01(in)));
    points[index].output = out;
    return index;
}

bool UltraCanvasToneCurve::RemovePoint(int index) {
    if (index <= 0 || index >= static_cast<int>(points.size()) - 1) return false;
    points.erase(points.begin() + index);
    return true;
}

int UltraCanvasToneCurve::FindPointNear(float in, float out, float radius) const {
    int best = -1;
    float bestDist = radius;
    for (size_t i = 0; i < points.size(); ++i) {
        float dx = points[i].input - in;
        float dy = points[i].output - out;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d <= bestDist) {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

float UltraCanvasToneCurve::Evaluate(float x) const {
    if (points.size() < 2) return Clamp01(x);
    x = Clamp01(x);

    size_t n = points.size();
    if (x <= points.front().input)  return Clamp01(points.front().output);
    if (x >= points.back().input)   return Clamp01(points.back().output);

    // Segment containing x.
    size_t i = 0;
    while (i + 1 < n && points[i + 1].input < x) ++i;
    float x0 = points[i].input,     y0 = points[i].output;
    float x1 = points[i + 1].input, y1 = points[i + 1].output;
    float h = x1 - x0;
    if (h <= 0.0f) return Clamp01(y1);

    // Fritsch-Carlson monotone cubic: secant slopes, then tangents limited so
    // the interpolant can never overshoot the data (a photographic curve that
    // dips below its own control points looks like a defect).
    auto Secant = [&](size_t k) -> float {
        float dx = points[k + 1].input - points[k].input;
        return dx > 0.0f ? (points[k + 1].output - points[k].output) / dx : 0.0f;
    };
    float d = Secant(i);
    float dPrev = (i == 0) ? d : Secant(i - 1);
    float dNext = (i + 2 < n) ? Secant(i + 1) : d;

    auto Tangent = [](float a, float b) -> float {
        if (a * b <= 0.0f) return 0.0f;            // local extremum: flat tangent
        return (a + b) * 0.5f;
    };
    float m0 = (i == 0) ? d : Tangent(dPrev, d);
    float m1 = (i + 2 < n) ? Tangent(d, dNext) : d;

    // Limit the tangents to keep the segment monotone.
    if (d == 0.0f) {
        m0 = m1 = 0.0f;
    } else {
        float a = m0 / d;
        float b = m1 / d;
        float s = a * a + b * b;
        if (s > 9.0f) {
            float t = 3.0f / std::sqrt(s);
            m0 = t * a * d;
            m1 = t * b * d;
        }
    }

    float t = (x - x0) / h;
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 =         t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 =         t3 -        t2;

    return Clamp01(h00 * y0 + h10 * h * m0 + h01 * y1 + h11 * h * m1);
}

std::array<uint8_t, 256> UltraCanvasToneCurve::BuildLut() const {
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        float v = Evaluate(static_cast<float>(i) / 255.0f);
        int out = static_cast<int>(std::lround(v * 255.0f));
        lut[i] = static_cast<uint8_t>(std::max(0, std::min(255, out)));
    }
    return lut;
}

bool UltraCanvasToneCurve::IsIdentity() const {
    if (points.size() != 2) return false;
    return std::fabs(points[0].output) < 1e-4f &&
           std::fabs(points[1].output - 1.0f) < 1e-4f;
}

std::string UltraCanvasToneCurve::ToString() const {
    std::ostringstream os;
    for (size_t i = 0; i < points.size(); ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f,%.3f", points[i].input, points[i].output);
        if (i) os << ';';
        os << buf;
    }
    return os.str();
}

bool UltraCanvasToneCurve::FromString(const std::string& text) {
    std::vector<ToneCurvePoint> parsed;
    std::istringstream is(text);
    std::string token;
    while (std::getline(is, token, ';')) {
        if (token.empty()) continue;
        size_t comma = token.find(',');
        if (comma == std::string::npos) return false;
        try {
            float in  = std::stof(token.substr(0, comma));
            float out = std::stof(token.substr(comma + 1));
            parsed.push_back(ToneCurvePoint(in, out));
        } catch (...) {
            return false;
        }
    }
    if (parsed.size() < 2) return false;
    SetPoints(std::move(parsed));
    return true;
}

// ===========================================================================
// TONE CURVE SET
// ===========================================================================

UltraCanvasToneCurve& ToneCurveSet::Channel(ToneCurveChannel c) {
    switch (c) {
        case ToneCurveChannel::Red:   return red;
        case ToneCurveChannel::Green: return green;
        case ToneCurveChannel::Blue:  return blue;
        case ToneCurveChannel::RGB:
        default:                      return rgb;
    }
}

const UltraCanvasToneCurve& ToneCurveSet::Channel(ToneCurveChannel c) const {
    return const_cast<ToneCurveSet*>(this)->Channel(c);
}

std::array<std::array<uint8_t, 256>, 3> ToneCurveSet::BuildChannelLuts() const {
    std::array<uint8_t, 256> master = rgb.BuildLut();
    std::array<std::array<uint8_t, 256>, 3> out{};
    const UltraCanvasToneCurve* perChannel[3] = { &red, &green, &blue };
    for (int c = 0; c < 3; ++c) {
        std::array<uint8_t, 256> own = perChannel[c]->BuildLut();
        for (int i = 0; i < 256; ++i) out[c][i] = master[own[i]];
    }
    return out;
}

std::string ToneCurveSet::ChannelName(ToneCurveChannel c) {
    switch (c) {
        case ToneCurveChannel::Red:   return "Red";
        case ToneCurveChannel::Green: return "Green";
        case ToneCurveChannel::Blue:  return "Blue";
        case ToneCurveChannel::RGB:
        default:                      return "RGB";
    }
}

} // namespace UltraCanvas
