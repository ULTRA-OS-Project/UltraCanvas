// Tests/TernaryModelTest.cpp
// Unit tests for the ternary chart model: shares from raw amounts, the
// points that have none, the reading direction's label edges, the ticks,
// validation and the shipped samples. UI-free.
//
// Version: 1.2.0 - Phase 3: the zoom - spans, the mapping both ways, ticks over
//   the visible range, validation of minimums, minimums in the JSON
// Version: 1.1.0 - Phase 2: the USDA outlines against the class rules on a
//   grid, ties on shared edges, the De Finetti curve, bubble sizes, the JSON
//   round trip of every sample and its refusals, the CSV both ways
// Version: 1.0.0
// Last Modified: 2026-11-05
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <algorithm>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double tol = 1e-12) { return std::abs(a - b) <= tol; }

static void TestShares() {
    std::printf("\n[Shares]\n");
    double a = 0, b = 0, c = 0;
    CHECK(TernaryPoint(412, 300, 288).Shares(a, b, c) && Near(a, 0.412) && Near(b, 0.3) && Near(c, 0.288),
          "votes become shares of their sum");
    CHECK(Near(a + b + c, 1.0), "and the shares add up to 1");
    CHECK(TernaryPoint(33.4, 33.3, 33.0).Shares(a, b, c) && Near(a + b + c, 1.0),
          "a lab result summing to 99.7 is normalised, not refused");
    CHECK(TernaryPoint(0, 0, 5).Shares(a, b, c) && Near(c, 1.0) && Near(a, 0.0), "a pure part sits on its corner");

    a = b = c = -1.0;
    CHECK(!TernaryPoint(0, 0, 0).Shares(a, b, c) && a == -1.0, "parts summing to 0 have no shares, and leave them alone");
    CHECK(!TernaryPoint(-1, 2, 3).Shares(a, b, c), "a negative part has none");
    CHECK(!TernaryPoint(std::nan(""), 2, 3).Shares(a, b, c), "a missing part has none");
    CHECK(!TernaryPoint(std::numeric_limits<double>::infinity(), 2, 3).Shares(a, b, c), "nor an infinite one");
}

static void TestReading() {
    std::printf("\n[Reading direction]\n");
    TernaryModel model;
    model.reading = TernaryReading::Clockwise;
    CHECK(model.LabelEdgeStart(0) == 1 && model.LabelEdgeStart(1) == 2 && model.LabelEdgeStart(2) == 0,
          "clockwise: A reads from B, B from C, C from A (the USDA triangle)");
    model.reading = TernaryReading::Anticlockwise;
    CHECK(model.LabelEdgeStart(0) == 2 && model.LabelEdgeStart(1) == 0 && model.LabelEdgeStart(2) == 1,
          "anticlockwise: A reads from C, B from A, C from B");
}

static void TestTicks() {
    std::printf("\n[Ticks]\n");
    TernaryModel model;
    const std::vector<double> ten = model.Ticks(0);
    CHECK(ten.size() == 11 && ten.front() == 0.0 && ten.back() == 1.0, "every 10 %: eleven ticks from 0 to 1");
    CHECK(Near(ten[3], 0.3), "counted, not accumulated: the fourth tick is exactly 0.3");
    model.axes[1].tickStep = 0.25;
    CHECK(model.Ticks(1).size() == 5, "every 25 %: five ticks");
    model.axes[2].tickStep = 0.3;
    const std::vector<double> odd = model.Ticks(2);
    CHECK(odd.size() == 5 && odd.back() == 1.0, "a step that does not divide 1 still ends at 1");
    model.axes[2].tickStep = 0.0;
    CHECK(model.Ticks(2).size() == 2, "a bad step falls back to the two corners");
}

static void TestValidation() {
    std::printf("\n[Validation]\n");
    TernaryModel model = TernarySamples::ThreePartyVotes();
    CHECK(model.Validate().empty(), "the vote sample is valid");
    TernaryModel bad = model;
    bad.axes[1].title.clear();
    CHECK(!bad.Validate().empty(), "an axis without a title is reported");
    bad = model;
    bad.axes[0].tickStep = 0.8;
    CHECK(!bad.Validate().empty(), "a tick step past 0.5 is reported");
    bad = model;
    bad.series[0].points.emplace_back(0, 0, 0, "Nowhere");
    const std::vector<std::string> problems = bad.Validate();
    CHECK(problems.size() == 1 && problems[0].find("Nowhere") != std::string::npos,
          "a point with no shares is named");
}

static void TestSamples() {
    std::printf("\n[Samples]\n");
    const TernaryModel votes = TernarySamples::ThreePartyVotes();
    CHECK(votes.series.size() == 2 && votes.PointCount() == 24, "two elections of twelve regions");
    CHECK(votes.reading == TernaryReading::Anticlockwise, "the vote sample reads anticlockwise");
    const TernaryModel soil = TernarySamples::SoilSamples();
    CHECK(soil.Validate().empty() && soil.series.size() == 3, "the soil sample is valid, in three groups");
    CHECK(soil.axes[0].title == "Clay" && soil.axes[1].title == "Sand" && soil.axes[2].title == "Silt",
          "on the USDA layout: clay at the apex, sand left, silt right");
    CHECK(soil.reading == TernaryReading::Clockwise, "read clockwise, as the USDA triangle is");
    bool notAllHundred = false;
    for (const TernarySeries& s : soil.series) {
        for (const TernaryPoint& p : s.points) notAllHundred |= std::abs(p.Sum() - 100.0) > 1e-9;
    }
    CHECK(notAllHundred, "some samples sum to a little more or less than 100, as lab results do");
    std::set<std::string> labels;
    for (const TernarySeries& s : soil.series) for (const TernaryPoint& p : s.points) labels.insert(p.label);
    CHECK(labels.size() == soil.PointCount(), "every sample has its own id");
    CHECK(FormatTernaryShare(0.38125).find("38.1") == 0 || FormatTernaryShare(0.38125).find("38,1") == 0,
          "a share reads as a percentage with one decimal");
}

// The USDA texture class of (clay, sand, silt), straight from the class
// rules - the reference the derived outlines are checked against.
static std::string UsdaRule(double clay, double sand, double silt) {
    if (sand >= 85 && silt + 1.5 * clay <= 15) return "sand";
    if (sand >= 70 && sand <= 91 && silt + 1.5 * clay >= 15 && silt + 2 * clay <= 30) return "loamy-sand";
    if ((clay >= 7 && clay <= 20 && sand >= 52 && silt + 2 * clay >= 30) ||
        (clay <= 7 && silt <= 50 && silt + 2 * clay >= 30)) return "sandy-loam";
    if (clay >= 7 && clay <= 27 && silt >= 28 && silt <= 50 && sand <= 52) return "loam";
    if ((silt >= 50 && clay >= 12 && clay <= 27) || (silt >= 50 && silt <= 80 && clay <= 12)) return "silt-loam";
    if (silt >= 80 && clay <= 12) return "silt";
    if (clay >= 20 && clay <= 35 && silt <= 28 && sand >= 45) return "sandy-clay-loam";
    if (clay >= 27 && clay <= 40 && sand >= 20 && sand <= 45) return "clay-loam";
    if (clay >= 27 && clay <= 40 && sand <= 20) return "silty-clay-loam";
    if (clay >= 35 && sand >= 45) return "sandy-clay";
    if (clay >= 40 && silt >= 40) return "silty-clay";
    if (clay >= 40 && sand <= 45 && silt <= 40) return "clay";
    return "";
}

static void TestSoilTexture() {
    std::printf("\n[USDA soil texture classes]\n");
    const TernaryModel soil = TernarySamples::SoilTexture();
    CHECK(soil.regions.size() == 12 && soil.Validate().empty(), "twelve valid classes");
    // A grid offset by a quarter percent never lands on a class boundary,
    // so every point has exactly one class by the rules.
    int checked = 0, agree = 0, unclassified = 0;
    for (double clay = 0.25; clay < 100.0; clay += 0.5) {
        for (double sand = 0.25; clay + sand < 100.0; sand += 0.5) {
            const double silt = 100.0 - clay - sand;
            const std::string rule = UsdaRule(clay, sand, silt);
            const int region = soil.RegionOf(clay, sand, silt);
            ++checked;
            if (region < 0) { ++unclassified; continue; }
            if (soil.regions[static_cast<size_t>(region)].id == rule) ++agree;
        }
    }
    std::printf("    (%d grid points, %d agree, %d unclassified)\n", checked, agree, unclassified);
    CHECK(unclassified == 0, "the outlines cover the whole triangle");
    CHECK(agree == checked, "every grid point gets the class the USDA rules give it");

    // Ties: a point on an edge two classes share goes to the one listed first.
    const int onEdge = soil.RegionOf(7.0, 48.0, 45.0);   // sandy loam | loam, at clay 7 %
    CHECK(onEdge >= 0 && soil.regions[static_cast<size_t>(onEdge)].id == "sandy-loam",
          "a point on the sandy loam / loam edge goes to sandy loam, listed first");
    CHECK(soil.RegionOf(0, 0, 0) == -1, "a point with no shares has no class");
    CHECK(soil.RegionOf(60, 400, 340) == soil.RegionOf(7.5, 50, 42.5), "amounts in any unit classify by their shares");
    CHECK(soil.regions[static_cast<size_t>(soil.RegionOf(100, 0, 0))].id == "clay", "pure clay is clay");
}

static void TestDeFinetti() {
    std::printf("\n[De Finetti]\n");
    const TernaryModel model = TernarySamples::DeFinetti();
    CHECK(model.Validate().empty() && model.curves.size() == 1, "valid, with one reference curve");
    bool onCurve = true;
    for (const TernaryComposition& c : model.curves[0].points) {
        onCurve &= std::abs(c.a * c.a - 4.0 * c.b * c.c) < 1e-12;   // Aa^2 = 4 AA aa
    }
    CHECK(onCurve, "the curve is the Hardy-Weinberg parabola");
    bool inbredBelow = true;
    for (const TernaryPoint& p : model.series[1].points) {
        double het = 0, aa = 0, rec = 0;
        p.Shares(het, aa, rec);
        inbredBelow &= het * het < 4.0 * aa * rec;   // fewer heterozygotes than equilibrium
    }
    CHECK(inbredBelow, "the isolated populations lack heterozygotes, as inbreeding does");
    CHECK(model.MaxPointSize() == 610.0, "the largest sample size scales the bubbles");
}

static void TestTrajectories() {
    std::printf("\n[Trajectories]\n");
    const TernaryModel model = TernarySamples::VoteTrajectories();
    bool paths = model.series.size() == 4;
    for (const TernarySeries& s : model.series) paths &= s.kind == TernarySeriesKind::Path && s.arrows && s.points.size() == 5;
    CHECK(paths && model.Validate().empty(), "four regions as paths of five elections, with arrows");
}

static void TestValidationPhase2() {
    std::printf("\n[Validation of regions, curves and sizes]\n");
    TernaryModel model = TernarySamples::SoilTexture();
    model.regions[0].outline.resize(2);
    CHECK(!model.Validate().empty(), "a region with two corners is reported");
    model = TernarySamples::SoilTexture();
    model.regions[1].outline[0] = TernaryComposition(0, 0, 0);
    CHECK(!model.Validate().empty(), "a corner with no shares is reported");
    model = TernarySamples::DeFinetti();
    model.curves[0].points.resize(1);
    CHECK(!model.Validate().empty(), "a curve of one point is reported");
    model = TernarySamples::DeFinetti();
    model.series[0].points[0].size = -3;
    CHECK(!model.Validate().empty(), "a negative size is reported");
}

static bool SameModel(const TernaryModel& x, const TernaryModel& y) {
    auto sameColor = [](const Color& p, const Color& q) { return p.r == q.r && p.g == q.g && p.b == q.b && p.a == q.a; };
    auto sameComp = [](const TernaryComposition& p, const TernaryComposition& q) {
        return Near(p.a, q.a, 1e-9) && Near(p.b, q.b, 1e-9) && Near(p.c, q.c, 1e-9);
    };
    if (x.title != y.title || x.subtitle != y.subtitle || x.reading != y.reading ||
        x.series.size() != y.series.size() || x.regions.size() != y.regions.size() || x.curves.size() != y.curves.size()) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (x.axes[i].title != y.axes[i].title || x.axes[i].unit != y.axes[i].unit ||
            !Near(x.axes[i].tickStep, y.axes[i].tickStep) || !Near(x.axes[i].minimum, y.axes[i].minimum)) return false;
    }
    for (size_t i = 0; i < x.series.size(); ++i) {
        const TernarySeries& p = x.series[i];
        const TernarySeries& q = y.series[i];
        if (p.name != q.name || p.kind != q.kind || p.arrows != q.arrows || !sameColor(p.color, q.color) ||
            p.marker != q.marker || p.markerRadius != q.markerRadius || p.points.size() != q.points.size()) return false;
        for (size_t k = 0; k < p.points.size(); ++k) {
            const TernaryPoint& a = p.points[k];
            const TernaryPoint& b = q.points[k];
            if (!sameComp({a.a, a.b, a.c}, {b.a, b.b, b.c}) || a.label != b.label || a.note != b.note ||
                !Near(a.size, b.size, 1e-9)) return false;
        }
    }
    for (size_t i = 0; i < x.regions.size(); ++i) {
        const TernaryRegion& p = x.regions[i];
        const TernaryRegion& q = y.regions[i];
        if (p.id != q.id || p.name != q.name || p.note != q.note || !sameColor(p.fill, q.fill) ||
            p.outline.size() != q.outline.size()) return false;
        for (size_t k = 0; k < p.outline.size(); ++k) if (!sameComp(p.outline[k], q.outline[k])) return false;
    }
    for (size_t i = 0; i < x.curves.size(); ++i) {
        const TernaryCurve& p = x.curves[i];
        const TernaryCurve& q = y.curves[i];
        if (p.name != q.name || !sameColor(p.color, q.color) || p.width != q.width || p.dashed != q.dashed ||
            p.points.size() != q.points.size()) return false;
        for (size_t k = 0; k < p.points.size(); ++k) if (!sameComp(p.points[k], q.points[k])) return false;
    }
    return true;
}

static void TestJson() {
    std::printf("\n[JSON round trip]\n");
    TernaryModel soil = TernarySamples::SoilTexture();
    soil.series = TernarySamples::SoilSamples().series;
    soil.series[0].color = Color(10, 20, 30, 255);
    soil.axes[2].unit = "g";
    soil.axes[1].tickStep = 0.2;
    const TernaryModel samples[] = {soil, TernarySamples::ThreePartyVotes(), TernarySamples::DeFinetti(),
                                    TernarySamples::VoteTrajectories()};
    bool all = true;
    for (const TernaryModel& m : samples) {
        TernaryModel back;
        std::string error;
        all &= ImportTernaryJson(ExportTernaryJson(m), back, &error) && SameModel(m, back);
        if (!error.empty()) std::printf("    (%s)\n", error.c_str());
    }
    CHECK(all, "every sample, with regions, curves, paths, sizes and styles, comes back unchanged");
    CHECK(ExportTernaryJson(TernarySamples::ThreePartyVotes(), false).find("\"format\":\"ultracanvas-ternary\"") !=
          std::string::npos, "the format is named");

    auto refuses = [](const std::string& text, const char* expect) {
        TernaryModel m = TernarySamples::DeFinetti();
        const std::string before = ExportTernaryJson(m);
        std::string why;
        const bool ok = ImportTernaryJson(text, m, &why);
        if (!ok) std::printf("    (%s)\n", why.c_str());
        return !ok && why.find(expect) != std::string::npos && ExportTernaryJson(m) == before;
    };
    const std::string head = R"({"format":"ultracanvas-ternary","version":1,"axes":[{"title":"A"},{"title":"B"},{"title":"C"}],)";
    CHECK(refuses("{ nope", "not valid JSON"), "broken JSON is refused, the model untouched");
    CHECK(refuses(R"({"format":"ultracanvas-matrix","version":1})", "not a ternary chart"), "another format is refused");
    CHECK(refuses(R"({"format":"ultracanvas-ternary","version":7})", "version 7"), "a newer version is refused");
    CHECK(refuses(head + R"("reading":"sideways"})", "reading"), "an unknown reading is refused");
    CHECK(refuses(head + R"("series":[{"name":"S","kind":"spiral","points":[]}]})", "kind"), "an unknown kind is refused");
    CHECK(refuses(head + R"("series":[{"name":"S","points":[{"parts":[1,2]}]}]})", "three numbers"),
          "a point without three parts is refused");
    CHECK(refuses(head + R"("series":[{"name":"S","points":[{"parts":[0,0,0],"label":"Zero"}]}]})", "Zero"),
          "a model that does not validate is refused");
}

static void TestCsv() {
    std::printf("\n[CSV of the points]\n");
    TernaryModel model = TernarySamples::DeFinetti();
    model.series[0].points[0].note = "Said \"hi\", twice";
    const std::string csv = ExportTernaryCsv(model);
    CHECK(csv.rfind("label,series,Aa,AA,aa,size,note\n", 0) == 0, "the header names the parts by their titles");
    TernaryModel back = TernarySamples::DeFinetti();
    back.series[1].marker = ChartMarkerShape::Diamond;   // a look the import must keep
    std::string error;
    const bool read = ImportTernaryCsv(csv, back, &error);
    bool same = read && back.series.size() == model.series.size();
    for (size_t s = 0; same && s < model.series.size(); ++s) {
        same = back.series[s].name == model.series[s].name && back.series[s].points.size() == model.series[s].points.size();
        for (size_t k = 0; same && k < model.series[s].points.size(); ++k) {
            const TernaryPoint& p = model.series[s].points[k];
            const TernaryPoint& q = back.series[s].points[k];
            same = p.label == q.label && p.note == q.note && Near(p.a, q.a, 1e-9) && Near(p.b, q.b, 1e-9) &&
                   Near(p.c, q.c, 1e-9) && Near(p.size, q.size, 1e-9);
        }
    }
    CHECK(same, "every point comes back, quotes and commas included");
    CHECK(back.series[1].marker == ChartMarkerShape::Diamond && back.curves.size() == 1,
          "a series the model has keeps its look, and the curves stay");

    TernaryModel soil = TernarySamples::SoilTexture();
    const std::string sheet = "\xEF\xBB\xBF" "SILT,Clay,sand,Label\r\n50,20,30,S-1\r\n10,5,85,S-2\r\n";
    CHECK(ImportTernaryCsv(sheet, soil, &error) && soil.series.size() == 1 && soil.series[0].name == "Points" &&
          soil.series[0].points.size() == 2 && Near(soil.series[0].points[0].a, 20) && Near(soil.series[0].points[0].c, 50),
          "a spreadsheet's file: parts by title in any order and case, no series column");
    TernaryModel abc = TernarySamples::ThreePartyVotes();
    CHECK(ImportTernaryCsv("a,b,c\n1,2,3\n", abc, &error) && abc.PointCount() == 1, "or as a, b, c");

    auto refuses = [](const std::string& text, const char* expect) {
        TernaryModel m = TernarySamples::DeFinetti();
        const size_t before = m.PointCount();
        std::string why;
        const bool ok = ImportTernaryCsv(text, m, &why);
        if (!ok) std::printf("    (%s)\n", why.c_str());
        return !ok && why.find(expect) != std::string::npos && m.PointCount() == before;
    };
    CHECK(refuses("", "empty"), "an empty file is refused, the points untouched");
    CHECK(refuses("label,AA,aa\nX,1,2\n", "'Aa'"), "a missing part column is named");
    CHECK(refuses("AA,Aa,aa\n1,2\n", "line 2"), "a short line is refused with its number");
    CHECK(refuses("AA,Aa,aa\n1,2,3\n1,\"2,5\",3\n", "line 3: '2,5'"), "a comma decimal is refused, not misread");
    CHECK(refuses("AA,Aa,aa,size\n1,2,3,big\n", "size 'big'"), "a size that is not a number is refused");
}

static void TestZoom() {
    std::printf("\n[Zoom]\n");
    TernaryModel model = TernarySamples::SoilTexture();
    CHECK(!model.IsZoomed() && Near(model.ZoomSpan(), 1.0), "a model without minimums shows the whole triangle");
    model.axes[0].minimum = 0.2;
    model.axes[1].minimum = 0.1;
    model.axes[2].minimum = 0.3;
    CHECK(model.IsZoomed() && Near(model.ZoomSpan(), 0.4), "minimums of 20, 10 and 30 % leave a triangle 40 % across");
    double za = 0, zb = 0, zc = 0;
    model.ToZoomed(0.6, 0.1, 0.3, za, zb, zc);
    CHECK(Near(za, 1.0) && Near(zb, 0.0) && Near(zc, 0.0), "the zoomed triangle's A corner is 60 / 10 / 30");
    model.ToZoomed(0.1, 0.5, 0.4, za, zb, zc);
    CHECK(za < 0.0, "a composition under a minimum falls outside it");
    double sa = 0, sb = 0, sc = 0;
    model.FromZoomed(0.25, 0.25, 0.5, sa, sb, sc);
    model.ToZoomed(sa, sb, sc, za, zb, zc);
    CHECK(Near(za, 0.25) && Near(zb, 0.25) && Near(zc, 0.5) && Near(sa + sb + sc, 1.0), "the mapping round-trips and keeps the sum");

    const std::vector<double> ticks = model.Ticks(0);   // clay from 20 % to 60 %
    CHECK(Near(ticks.front(), 0.2) && Near(ticks.back(), 0.6), "an axis ticks from its minimum to its minimum plus the span");
    CHECK(ticks.size() == 5 && Near(ticks[1], 0.3), "every 10 %, as before, while that still gives three steps");
    model.axes[0].minimum = 0.3;
    model.axes[2].minimum = 0.55;    // span 0.05
    const std::vector<double> fine = model.Ticks(0);
    bool inside = fine.size() >= 4;
    for (double t : fine) inside &= t >= 0.3 - 1e-9 && t <= 0.35 + 1e-9;
    CHECK(inside, "zoomed in to a span of 5 %, a finer step keeps at least three intervals");

    TernaryModel bad = TernarySamples::SoilTexture();
    bad.axes[1].minimum = -0.1;
    CHECK(!bad.Validate().empty(), "a negative minimum is reported");
    bad.axes[1].minimum = 0.5;
    bad.axes[2].minimum = 0.495;
    CHECK(!bad.Validate().empty(), "minimums that leave less than 1 % are reported");

    TernaryModel zoomed = TernarySamples::SoilTexture();
    zoomed.axes[0].minimum = 0.1;
    TernaryModel back;
    std::string error;
    CHECK(ImportTernaryJson(ExportTernaryJson(zoomed), back, &error) && Near(back.axes[0].minimum, 0.1),
          "the zoom is saved with the model");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Ternary Model\n");
    std::printf("====================================\n");

    TestShares();
    TestReading();
    TestTicks();
    TestValidation();
    TestSamples();
    TestSoilTexture();
    TestDeFinetti();
    TestTrajectories();
    TestValidationPhase2();
    TestJson();
    TestCsv();
    TestZoom();

    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
