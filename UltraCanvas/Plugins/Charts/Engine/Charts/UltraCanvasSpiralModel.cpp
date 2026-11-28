// Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.cpp
// The UI-free model of a spiral plot.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A small, portable generator for the samples: the same numbers on every
// platform and standard library.
struct SampleRandom {
    uint64_t state;
    explicit SampleRandom(uint64_t seed) : state(seed) {}
    double Uniform() {                       // SplitMix64, [0, 1)
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }
    double Normal() {                        // Box-Muller
        const double u1 = std::max(Uniform(), 1e-12);
        const double u2 = Uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
    }
};

} // namespace

double UltraCanvasSpiralModel::Start() const {
    if (start) return *start;
    return samples.empty() ? 0.0 : samples.front().t;
}

double UltraCanvasSpiralModel::TurnsOf(double t) const {
    return period > 0.0 ? (t - Start()) / period : 0.0;
}

double UltraCanvasSpiralModel::TypicalStep() const {
    std::vector<double> steps;
    for (size_t i = 1; i < samples.size(); ++i) {
        const double d = samples[i].t - samples[i - 1].t;
        if (d > 0.0 && std::isfinite(d)) steps.push_back(d);
    }
    if (steps.empty()) return period > 0.0 ? period / 100.0 : 1.0;
    std::nth_element(steps.begin(), steps.begin() + static_cast<std::ptrdiff_t>(steps.size() / 2), steps.end());
    return steps[steps.size() / 2];
}

double UltraCanvasSpiralModel::SampleSpan(size_t i) const {
    if (i >= samples.size()) return 0.0;
    const double typical = TypicalStep();
    if (i + 1 >= samples.size()) return typical;
    const double step = samples[i + 1].t - samples[i].t;
    return (step > 0.0 && step <= 3.0 * typical) ? step : typical;
}

int UltraCanvasSpiralModel::TurnCount() const {
    double most = 0.0;
    if (!samples.empty()) {
        const double typical = TypicalStep();
        // Every sample covers at most a typical step past the one before the
        // last, and the last a typical step: the end of the data.
        most = TurnsOf(samples.back().t + typical);
        for (const SpiralSample& s : samples) most = std::max(most, TurnsOf(s.t));
    }
    return std::max(1, static_cast<int>(std::ceil(most - 1e-9)));
}

double UltraCanvasSpiralModel::RingWidth() const {
    const double inner = std::clamp(innerRadius, 0.0, 0.6);
    return (1.0 - inner) / (TurnCount() + 1.0);
}

SpiralPosition UltraCanvasSpiralModel::Position(double t, double offset) const {
    const double inner = std::clamp(innerRadius, 0.0, 0.6);
    const double u = TurnsOf(t);
    SpiralPosition p;
    p.radius = inner + RingWidth() * (u + 0.5 + offset);
    const double base = startAngle * kPi / 180.0;
    p.angle = clockwise ? base - 2.0 * kPi * u : base + 2.0 * kPi * u;
    return p;
}

std::pair<double, double> UltraCanvasSpiralModel::ValueRange() const {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const SpiralSample& s : samples) {
        if (!std::isfinite(s.value)) continue;
        lo = std::min(lo, s.value);
        hi = std::max(hi, s.value);
    }
    if (!std::isfinite(lo)) { lo = 0.0; hi = 1.0; }
    if (valueMin) lo = *valueMin;
    if (valueMax) hi = *valueMax;
    if (!(hi > lo)) hi = lo + 1.0;
    return {lo, hi};
}

double UltraCanvasSpiralModel::Normalize(double value) const {
    if (!std::isfinite(value)) return value;
    const auto [lo, hi] = ValueRange();
    return std::clamp((value - lo) / (hi - lo), 0.0, 1.0);
}

size_t UltraCanvasSpiralModel::NearestSample(double t) const {
    if (samples.empty()) return SIZE_MAX;
    auto it = std::lower_bound(samples.begin(), samples.end(), t,
                               [](const SpiralSample& s, double v) { return s.t < v; });
    if (it == samples.begin()) return 0;
    if (it == samples.end()) return samples.size() - 1;
    const size_t after = static_cast<size_t>(it - samples.begin());
    return (t - samples[after - 1].t <= samples[after].t - t) ? after - 1 : after;
}

std::vector<std::string> UltraCanvasSpiralModel::Validate() const {
    std::vector<std::string> problems;
    if (samples.empty()) problems.push_back("The model has no samples.");
    if (!(period > 0.0)) problems.push_back("The period must be above 0.");
    if (innerRadius < 0.0 || innerRadius > 0.6) problems.push_back("The inner radius must be 0 to 0.6.");
    for (size_t i = 1; i < samples.size(); ++i) {
        if (!(samples[i].t > samples[i - 1].t)) {
            problems.push_back("Sample " + std::to_string(i) + ": the times must increase.");
            break;
        }
    }
    if (start && !samples.empty() && samples.front().t < *start) {
        problems.push_back("The first sample is before the start.");
    }
    if (valueMin && valueMax && !(*valueMax > *valueMin)) problems.push_back("valueMax must be above valueMin.");
    return problems;
}

// =============================================================================
// SAMPLES
// =============================================================================

UltraCanvasSpiralModel UltraCanvasSpiralModel::DailyTemperature() {
    // Invented: a temperate city's daily mean, coldest in mid-January and
    // warmest in mid-July, with weather noise that lingers for a few days and
    // a slow warming from one year to the next.
    UltraCanvasSpiralModel model;
    model.title = "A year per turn";
    model.subtitle = "Invented daily mean temperatures, three years from 1 January";
    model.valueName = "Temperature (\xC2\xB0" "C)";
    model.period = 365.0;
    model.start = 0.0;
    model.sectorLabels = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    model.turnLabels = {"2021", "2022", "2023"};
    SampleRandom random(20210101);
    double weather = 0.0;
    for (int day = 0; day < 3 * 365; ++day) {
        weather = 0.7 * weather + 1.8 * random.Normal();          // a few days' memory
        const double season = 10.5 - 9.5 * std::cos(2.0 * kPi * (day - 15.0) / 365.0);
        const double warming = 0.4 * day / 365.0;
        SpiralSample s;
        s.t = day;
        s.value = std::round((season + warming + weather) * 10.0) / 10.0;
        model.samples.push_back(s);
    }
    return model;
}

UltraCanvasSpiralModel UltraCanvasSpiralModel::HourlyVisits() {
    // Invented: visits per hour to a shop's website, from Monday 00:00 over
    // four weeks. Weekdays are busy from 9 to 17 with a dip at lunch and a
    // smaller evening peak; nights and weekends are quiet; the third Thursday
    // has a sale.
    UltraCanvasSpiralModel model;
    model.title = "A week per turn";
    model.subtitle = "Invented visits per hour to a shop's website, four weeks from a Monday";
    model.valueName = "Visits per hour";
    model.period = 168.0;
    model.start = 0.0;
    model.sectorLabels = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    model.turnLabels = {"Week 1", "Week 2", "Week 3", "Week 4"};
    SampleRandom random(20240101);
    for (int hour = 0; hour < 4 * 168; ++hour) {
        const int day = (hour / 24) % 7;              // 0 = Monday
        const double h = hour % 24;
        auto bump = [](double x, double centre, double width) {
            const double z = (x - centre) / width;
            return std::exp(-0.5 * z * z);
        };
        double visits = 8.0;
        if (day < 5) {
            visits += 110.0 * (bump(h, 10.5, 1.6) + bump(h, 15.0, 1.8)) + 45.0 * bump(h, 20.5, 1.4);
        } else {
            visits += 40.0 * bump(h, 14.0, 3.0);
        }
        SpiralSample s;
        s.t = hour;
        if (hour / 168 == 2 && day == 3 && h >= 8 && h <= 22) {
            visits *= 2.4;
            s.note = "The sale";
        }
        visits *= 1.0 + 0.12 * random.Normal();
        s.value = std::max(0.0, std::round(visits));
        model.samples.push_back(s);
    }
    return model;
}

} // namespace UltraCanvas
