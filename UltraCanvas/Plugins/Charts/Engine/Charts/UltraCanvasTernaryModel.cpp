// Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.cpp
// The UI-free model of the ternary chart and its sample models.
// Version: 1.1.0 - regions and RegionOf, paths, bubble sizes, curves, JSON and
//   CSV interchange, the SoilTexture / DeFinetti / VoteTrajectories samples
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasCSVImport.h"
#include "UltraCanvasTextUtils.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace UltraCanvas {

// =============================================================================
// MODEL
// =============================================================================

    namespace {
        bool SharesOf(double a, double b, double c, double& sa, double& sb, double& sc) {
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return false;
            if (a < 0.0 || b < 0.0 || c < 0.0) return false;
            const double sum = a + b + c;
            if (!(sum > 0.0) || !std::isfinite(sum)) return false;
            sa = a / sum;
            sb = b / sum;
            sc = c / sum;
            return true;
        }

        // Whether (u, v) is inside the polygon or on its outline, in share
        // space (u = share of B, v = share of C - the projection's own
        // coordinates, so the test matches the drawing exactly).
        bool InsideOrOn(const std::vector<std::pair<double, double>>& poly, double u, double v) {
            const double eps = 1e-9;
            const size_t n = poly.size();
            for (size_t i = 0; i < n; ++i) {
                const auto& p = poly[i];
                const auto& q = poly[(i + 1) % n];
                const double cross = (q.first - p.first) * (v - p.second) - (q.second - p.second) * (u - p.first);
                const double length = std::hypot(q.first - p.first, q.second - p.second);
                if (std::abs(cross) <= eps * std::max(1.0, length) &&
                    u >= std::min(p.first, q.first) - eps && u <= std::max(p.first, q.first) + eps &&
                    v >= std::min(p.second, q.second) - eps && v <= std::max(p.second, q.second) + eps) {
                    return true;   // on the outline
                }
            }
            bool inside = false;
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const auto& p = poly[i];
                const auto& q = poly[j];
                if ((p.second > v) != (q.second > v) &&
                    u < (q.first - p.first) * (v - p.second) / (q.second - p.second) + p.first) {
                    inside = !inside;
                }
            }
            return inside;
        }
    }

    bool TernaryComposition::Shares(double& sa, double& sb, double& sc) const {
        return SharesOf(a, b, c, sa, sb, sc);
    }

    bool TernaryPoint::Shares(double& sa, double& sb, double& sc) const {
        return SharesOf(a, b, c, sa, sb, sc);
    }

    int TernaryModel::LabelEdgeStart(int corner) const {
        // Clockwise on screen is B -> A -> C -> B, so each axis starts at the
        // corner before its own: A from B, C from A, B from C.
        return reading == TernaryReading::Clockwise ? (corner + 1) % 3 : (corner + 2) % 3;
    }

    std::vector<double> TernaryModel::Ticks(int corner) const {
        std::vector<double> ticks;
        if (corner < 0 || corner > 2) return ticks;
        const double step = axes[corner].tickStep;
        if (!(step > 0.0) || step > 0.5) return {0.0, 1.0};
        // Counted, not accumulated, so 0.1 * 10 lands on 1 exactly.
        const int count = static_cast<int>(std::floor(1.0 / step + 1e-9));
        for (int k = 0; k <= count; ++k) ticks.push_back(std::min(1.0, k * step));
        if (ticks.back() < 1.0 - 1e-9) ticks.push_back(1.0);
        return ticks;
    }

    size_t TernaryModel::PointCount() const {
        size_t n = 0;
        for (const TernarySeries& s : series) n += s.points.size();
        return n;
    }

    double TernaryModel::MaxPointSize() const {
        double largest = 0.0;
        for (const TernarySeries& s : series) {
            for (const TernaryPoint& p : s.points) {
                if (std::isfinite(p.size)) largest = std::max(largest, p.size);
            }
        }
        return largest;
    }

    int TernaryModel::RegionOf(double a, double b, double c) const {
        double sa = 0.0, sb = 0.0, sc = 0.0;
        if (!SharesOf(a, b, c, sa, sb, sc)) return -1;
        for (size_t r = 0; r < regions.size(); ++r) {
            std::vector<std::pair<double, double>> poly;
            for (const TernaryComposition& corner : regions[r].outline) {
                double ca = 0.0, cb = 0.0, cc = 0.0;
                if (corner.Shares(ca, cb, cc)) poly.emplace_back(cb, cc);
            }
            if (poly.size() >= 3 && InsideOrOn(poly, sb, sc)) return static_cast<int>(r);
        }
        return -1;
    }

    std::vector<std::string> TernaryModel::Validate() const {
        std::vector<std::string> problems;
        const char* corners[3] = {"A", "B", "C"};
        for (int i = 0; i < 3; ++i) {
            if (axes[i].title.empty()) problems.push_back(std::string("Axis ") + corners[i] + " has no title");
            if (!(axes[i].tickStep > 0.0) || axes[i].tickStep > 0.5) {
                problems.push_back(std::string("Axis ") + corners[i] + " has a tick step outside (0, 0.5]");
            }
        }
        for (const TernarySeries& s : series) {
            for (size_t p = 0; p < s.points.size(); ++p) {
                double sa = 0.0, sb = 0.0, sc = 0.0;
                if (s.points[p].Shares(sa, sb, sc)) continue;
                const std::string name = s.points[p].label.empty() ? "point " + std::to_string(p)
                                                                   : "\"" + s.points[p].label + "\"";
                problems.push_back("In \"" + s.name + "\", " + name +
                                   " has a negative or missing part, or parts summing to 0");
            }
            for (const TernaryPoint& point : s.points) {
                if (!(point.size >= 0.0)) {
                    problems.push_back("In \"" + s.name + "\", \"" + point.label + "\" has a negative or missing size");
                }
            }
        }
        for (const TernaryRegion& region : regions) {
            const std::string name = "Region \"" + (region.name.empty() ? region.id : region.name) + "\"";
            if (region.outline.size() < 3) problems.push_back(name + " has fewer than three corners");
            for (const TernaryComposition& corner : region.outline) {
                double sa = 0.0, sb = 0.0, sc = 0.0;
                if (!corner.Shares(sa, sb, sc)) {
                    problems.push_back(name + " has a corner with no shares");
                    break;
                }
            }
        }
        for (const TernaryCurve& curve : curves) {
            if (curve.points.size() < 2) problems.push_back("Curve \"" + curve.name + "\" has fewer than two points");
        }
        return problems;
    }

    std::string FormatTernaryShare(double share) {
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f%%", share * 100.0);   // locale-ok: a tooltip a person reads
        return text;
    }

// =============================================================================
// JSON
// =============================================================================

    namespace {
        const char* kTernaryFormat = "ultracanvas-ternary";
        constexpr int64_t kTernaryFormatVersion = 1;

        const std::pair<ChartMarkerShape, const char*> kMarkerNames[] = {
            {ChartMarkerShape::Circle, "circle"},     {ChartMarkerShape::Square, "square"},
            {ChartMarkerShape::Diamond, "diamond"},   {ChartMarkerShape::TriangleUp, "triangle-up"},
            {ChartMarkerShape::TriangleDown, "triangle-down"}, {ChartMarkerShape::Cross, "cross"},
            {ChartMarkerShape::Plus, "plus"},         {ChartMarkerShape::NoMarker, "none"}};

        template <typename E, size_t N>
        const char* NameOf(const std::pair<E, const char*> (&table)[N], E value) {
            for (const auto& entry : table) if (entry.first == value) return entry.second;
            return table[0].second;
        }
        template <typename E, size_t N>
        bool ValueOf(const std::pair<E, const char*> (&table)[N], const std::string& name, E& out) {
            for (const auto& entry : table) {
                if (name == entry.second) { out = entry.first; return true; }
            }
            return false;
        }

        void SetText(JSONValue& object, const char* key, const std::string& text) {
            if (!text.empty()) object.Set(key, text);
        }
        void SetColor(JSONValue& object, const char* key, const Color& color) {
            if (color.a != 0) object.Set(key, JSON::FromColor(color));
        }
        bool ReadColor(const JSONValue& object, const char* key, Color& out) {
            if (!object.Contains(key)) return true;
            return JSON::ToColor(object.Get(key), out);
        }
        JSONValue CompositionToJson(const TernaryComposition& c) {
            JSONValue array = JSONValue::MakeArray();
            array.Append(JSONValue(c.a));
            array.Append(JSONValue(c.b));
            array.Append(JSONValue(c.c));
            return array;
        }
        bool CompositionFromJson(const JSONValue& value, TernaryComposition& out) {
            const std::vector<JSONValue>& e = value.GetElements();
            if (e.size() != 3 || !e[0].IsNumber() || !e[1].IsNumber() || !e[2].IsNumber()) return false;
            out = TernaryComposition(e[0].GetNumber(), e[1].GetNumber(), e[2].GetNumber());
            return true;
        }
    }

    JSONValue TernaryModelToJson(const TernaryModel& model) {
        JSONValue doc = JSONValue::MakeObject();
        doc.Set("format", kTernaryFormat);
        doc.Set("version", kTernaryFormatVersion);
        SetText(doc, "title", model.title);
        SetText(doc, "subtitle", model.subtitle);
        doc.Set("reading", model.reading == TernaryReading::Clockwise ? "clockwise" : "anticlockwise");
        JSONValue axes = JSONValue::MakeArray();
        for (const TernaryAxis& axis : model.axes) {
            JSONValue a = JSONValue::MakeObject();
            a.Set("title", axis.title);
            if (axis.unit != "%") a.Set("unit", axis.unit);
            if (axis.tickStep != 0.1) a.Set("tickStep", axis.tickStep);
            axes.Append(a);
        }
        doc.Set("axes", axes);

        JSONValue series = JSONValue::MakeArray();
        for (const TernarySeries& s : model.series) {
            JSONValue j = JSONValue::MakeObject();
            j.Set("name", s.name);
            if (s.kind == TernarySeriesKind::Path) j.Set("kind", "path");
            if (s.arrows) j.Set("arrows", true);
            SetColor(j, "color", s.color);
            if (s.marker != ChartMarkerShape::Circle) j.Set("marker", NameOf(kMarkerNames, s.marker));
            if (s.markerRadius != 4.5f) j.Set("markerRadius", static_cast<double>(s.markerRadius));
            JSONValue points = JSONValue::MakeArray();
            for (const TernaryPoint& p : s.points) {
                JSONValue q = JSONValue::MakeObject();
                q.Set("parts", CompositionToJson(TernaryComposition(p.a, p.b, p.c)));
                SetText(q, "label", p.label);
                SetText(q, "note", p.note);
                if (p.size != 0.0) q.Set("size", p.size);
                points.Append(q);
            }
            j.Set("points", points);
            series.Append(j);
        }
        doc.Set("series", series);

        if (!model.regions.empty()) {
            JSONValue regions = JSONValue::MakeArray();
            for (const TernaryRegion& r : model.regions) {
                JSONValue j = JSONValue::MakeObject();
                SetText(j, "id", r.id);
                SetText(j, "name", r.name);
                SetColor(j, "fill", r.fill);
                SetText(j, "note", r.note);
                JSONValue outline = JSONValue::MakeArray();
                for (const TernaryComposition& c : r.outline) outline.Append(CompositionToJson(c));
                j.Set("outline", outline);
                regions.Append(j);
            }
            doc.Set("regions", regions);
        }
        if (!model.curves.empty()) {
            JSONValue curves = JSONValue::MakeArray();
            for (const TernaryCurve& c : model.curves) {
                JSONValue j = JSONValue::MakeObject();
                SetText(j, "name", c.name);
                j.Set("color", JSON::FromColor(c.color));
                if (c.width != 1.6f) j.Set("width", static_cast<double>(c.width));
                if (!c.dashed) j.Set("dashed", false);
                JSONValue points = JSONValue::MakeArray();
                for (const TernaryComposition& p : c.points) points.Append(CompositionToJson(p));
                j.Set("points", points);
                curves.Append(j);
            }
            doc.Set("curves", curves);
        }
        return doc;
    }

    std::string ExportTernaryJson(const TernaryModel& model, bool pretty) {
        JSONSerializeOptions options;
        options.pretty = pretty;
        options.indentWidth = 2;
        return JSON::Serialize(TernaryModelToJson(model), options);
    }

    bool TernaryModelFromJson(const JSONValue& json, TernaryModel& out, std::string* error) {
        auto failWith = [error](const std::string& message) {
            if (error) *error = message;
            return false;
        };
        if (!json.IsObject()) return failWith("the document is not a JSON object");
        if (json.Get("format").GetString() != kTernaryFormat) {
            return failWith("not a ternary chart (format is not \"" + std::string(kTernaryFormat) + "\")");
        }
        const int64_t version = json.Get("version").GetInteger(0);
        if (version < 1 || version > kTernaryFormatVersion) {
            return failWith("format version " + std::to_string(version) + " is not one this build reads");
        }

        TernaryModel model;
        model.title = json.Get("title").GetString();
        model.subtitle = json.Get("subtitle").GetString();
        const std::string reading = json.Get("reading").GetString("clockwise");
        if (reading == "clockwise") model.reading = TernaryReading::Clockwise;
        else if (reading == "anticlockwise") model.reading = TernaryReading::Anticlockwise;
        else return failWith("unknown reading '" + reading + "'");
        const std::vector<JSONValue>& axes = json.Get("axes").GetElements();
        if (axes.size() != 3) return failWith("the document has " + std::to_string(axes.size()) + " axes, not 3");
        for (size_t i = 0; i < 3; ++i) {
            model.axes[i].title = axes[i].Get("title").GetString();
            model.axes[i].unit = axes[i].Get("unit").GetString("%");
            model.axes[i].tickStep = axes[i].Get("tickStep").GetNumber(0.1);
        }

        for (const JSONValue& j : json.Get("series").GetElements()) {
            TernarySeries s(j.Get("name").GetString());
            const std::string kind = j.Get("kind").GetString("scatter");
            if (kind == "path") s.kind = TernarySeriesKind::Path;
            else if (kind != "scatter") return failWith("series '" + s.name + "' has an unknown kind '" + kind + "'");
            s.arrows = j.Get("arrows").GetBoolean(false);
            if (!ReadColor(j, "color", s.color)) return failWith("series '" + s.name + "' has an unreadable colour");
            const std::string marker = j.Get("marker").GetString("circle");
            if (!ValueOf(kMarkerNames, marker, s.marker)) {
                return failWith("series '" + s.name + "' has an unknown marker '" + marker + "'");
            }
            s.markerRadius = static_cast<float>(j.Get("markerRadius").GetNumber(4.5));
            const std::vector<JSONValue>& points = j.Get("points").GetElements();
            for (size_t n = 0; n < points.size(); ++n) {
                TernaryComposition parts;
                if (!CompositionFromJson(points[n].Get("parts"), parts)) {
                    return failWith("series '" + s.name + "', point " + std::to_string(n) +
                                    ": \"parts\" is not three numbers");
                }
                TernaryPoint p(parts.a, parts.b, parts.c, points[n].Get("label").GetString());
                p.note = points[n].Get("note").GetString();
                p.size = points[n].Get("size").GetNumber(0.0);
                s.points.push_back(p);
            }
            model.series.push_back(s);
        }
        for (const JSONValue& j : json.Get("regions").GetElements()) {
            TernaryRegion r;
            r.id = j.Get("id").GetString();
            r.name = j.Get("name").GetString();
            r.note = j.Get("note").GetString();
            if (!ReadColor(j, "fill", r.fill)) return failWith("region '" + r.name + "' has an unreadable fill");
            for (const JSONValue& corner : j.Get("outline").GetElements()) {
                TernaryComposition c;
                if (!CompositionFromJson(corner, c)) return failWith("region '" + r.name + "' has a corner that is not three numbers");
                r.outline.push_back(c);
            }
            model.regions.push_back(r);
        }
        for (const JSONValue& j : json.Get("curves").GetElements()) {
            TernaryCurve c;
            c.name = j.Get("name").GetString();
            if (!ReadColor(j, "color", c.color)) return failWith("curve '" + c.name + "' has an unreadable colour");
            c.width = static_cast<float>(j.Get("width").GetNumber(1.6));
            c.dashed = j.Get("dashed").GetBoolean(true);
            for (const JSONValue& point : j.Get("points").GetElements()) {
                TernaryComposition p;
                if (!CompositionFromJson(point, p)) return failWith("curve '" + c.name + "' has a point that is not three numbers");
                c.points.push_back(p);
            }
            model.curves.push_back(c);
        }

        const std::vector<std::string> problems = model.Validate();
        if (!problems.empty()) return failWith(problems.front());
        out = model;
        return true;
    }

    bool ImportTernaryJson(const std::string& text, TernaryModel& out, std::string* error) {
        JSONParseResult parsed;
        const JSONValue json = JSON::Parse(text, &parsed);
        if (!parsed.success) {
            if (error) {
                *error = "not valid JSON: " + parsed.errorMessage + " at line " + std::to_string(parsed.errorLine) +
                         ", column " + std::to_string(parsed.errorColumn);
            }
            return false;
        }
        return TernaryModelFromJson(json, out, error);
    }

// =============================================================================
// CSV
// =============================================================================

    namespace {
        std::string LowerTrim(const std::string& text) {
            size_t first = 0, last = text.size();
            while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
            while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
            std::string lower = text.substr(first, last - first);
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lower;
        }
        // A whole field as a number, dot-decimal: "3.5" yes, "3,5" or "3.5 g" no.
        bool ParseNumber(const std::string& text, double& out) {
            const std::string t = Trim(text);
            if (t.empty()) return false;
            double value = 0.0;
            const char* end = ParseFloatClassic(t.data(), t.data() + t.size(), value);
            if (end != t.data() + t.size() || !std::isfinite(value)) return false;
            out = value;
            return true;
        }
    }

    std::string ExportTernaryCsv(const TernaryModel& model) {
        CSVExportOptions options;
        std::string csv = CSVFormatRecord({"label", "series", model.axes[0].title, model.axes[1].title,
                                           model.axes[2].title, "size", "note"}, options);
        for (const TernarySeries& s : model.series) {
            for (const TernaryPoint& p : s.points) {
                csv += CSVFormatRecord({p.label, s.name, FormatFloatClassic(p.a, 10), FormatFloatClassic(p.b, 10),
                                        FormatFloatClassic(p.c, 10),
                                        p.size != 0.0 ? FormatFloatClassic(p.size, 10) : std::string(), p.note},
                                       options);
            }
        }
        return csv;
    }

    bool ImportTernaryCsv(const std::string& csv, TernaryModel& model, std::string* error) {
        auto failWith = [error](const std::string& message) {
            if (error) *error = message;
            return false;
        };
        CSVImportOptions options;
        options.detectNumbers = false;
        const CSVGrid grid = CSVParse(CSVDecodeToUtf8(csv, CSVImportOptions::Encoding::UTF8), options);
        auto blank = [](const CSVRow& row) { return row.size() == 1 && Trim(row[0].text).empty(); };
        size_t line = 0;
        while (line < grid.size() && blank(grid[line])) ++line;
        if (line == grid.size()) return failWith("the file is empty");

        const CSVRow& header = grid[line];
        std::map<std::string, size_t> columns;
        for (size_t c = 0; c < header.size(); ++c) {
            const std::string name = LowerTrim(header[c].text);
            if (!name.empty() && !columns.count(name)) columns[name] = c;
        }
        // The part columns: a column named exactly as an axis first, then one
        // that differs only in letter case and no other axis has taken (the
        // genotypes AA, Aa and aa differ only in case), then a / b / c.
        size_t part[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
        auto taken = [&part](size_t column) { return part[0] == column || part[1] == column || part[2] == column; };
        for (int i = 0; i < 3; ++i) {
            for (size_t c = 0; c < header.size() && part[i] == SIZE_MAX; ++c) {
                if (!model.axes[i].title.empty() && Trim(header[c].text) == model.axes[i].title && !taken(c)) part[i] = c;
            }
        }
        const char* fallback[3] = {"a", "b", "c"};
        for (int i = 0; i < 3; ++i) {
            const std::string title = LowerTrim(model.axes[i].title);
            for (size_t c = 0; c < header.size() && part[i] == SIZE_MAX && !title.empty(); ++c) {
                if (LowerTrim(header[c].text) == title && !taken(c)) part[i] = c;
            }
            if (part[i] == SIZE_MAX && columns.count(fallback[i]) && !taken(columns[fallback[i]])) {
                part[i] = columns[fallback[i]];
            }
            if (part[i] == SIZE_MAX) {
                return failWith("the header has no column for " +
                                (model.axes[i].title.empty() ? std::string(fallback[i]) : "'" + model.axes[i].title + "'"));
            }
        }
        auto field = [&columns](const CSVRow& row, const char* name) -> std::string {
            const auto it = columns.find(name);
            return it == columns.end() ? std::string() : Trim(row[it->second].text);
        };

        std::vector<TernarySeries> series;
        for (++line; line < grid.size(); ++line) {
            const CSVRow& row = grid[line];
            if (blank(row)) continue;
            const std::string where = "line " + std::to_string(line + 1);
            if (row.size() != header.size()) {
                return failWith(where + " has " + std::to_string(row.size()) + " fields, the header " +
                                std::to_string(header.size()));
            }
            double v[3] = {0.0, 0.0, 0.0};
            for (int i = 0; i < 3; ++i) {
                if (!ParseNumber(row[part[i]].text, v[i])) {
                    return failWith(where + ": '" + Trim(row[part[i]].text) + "' is not a number");
                }
            }
            TernaryPoint p(v[0], v[1], v[2], field(row, "label"));
            p.note = field(row, "note");
            const std::string size = field(row, "size");
            if (!size.empty() && !ParseNumber(size, p.size)) {
                return failWith(where + ": size '" + size + "' is not a number");
            }
            std::string name = field(row, "series");
            if (name.empty()) name = "Points";
            auto it = std::find_if(series.begin(), series.end(), [&name](const TernarySeries& s) { return s.name == name; });
            if (it == series.end()) {
                // A series the model already has keeps its look.
                auto old = std::find_if(model.series.begin(), model.series.end(),
                                        [&name](const TernarySeries& s) { return s.name == name; });
                TernarySeries fresh = old != model.series.end() ? *old : TernarySeries(name);
                fresh.points.clear();
                series.push_back(fresh);
                it = series.end() - 1;
            }
            it->points.push_back(p);
        }
        model.series = std::move(series);
        return true;
    }

// =============================================================================
// SAMPLES
// =============================================================================

    namespace TernarySamples {

        TernaryModel ThreePartyVotes() {
            TernaryModel model;
            model.title = "Three-party vote by region (invented)";
            model.subtitle = "Each point is one region; votes cast, shown as shares";
            model.axes[0] = TernaryAxis("Blue Party");
            model.axes[1] = TernaryAxis("Red Party");
            model.axes[2] = TernaryAxis("Gold Party");
            model.reading = TernaryReading::Anticlockwise;

            // Votes per region: Blue, Red, Gold. Every number is made up.
            struct Row { const char* region; double blue, red, gold; };
            const Row earlier[] = {
                {"Northfield", 41200, 35100, 18900}, {"Harbourside", 22800, 51300, 20400},
                {"Lakeview", 30500, 29800, 33700},   {"Old Town", 18700, 44100, 31900},
                {"Westmoor", 52400, 21900, 14300},   {"Riverbend", 35600, 38800, 22100},
                {"Hillcrest", 47100, 26400, 25200},  {"Eastgate", 26300, 47700, 17800},
                {"Southbay", 28900, 31200, 41500},   {"Milltown", 33800, 42600, 12900},
                {"Greenway", 24100, 27300, 46800},   {"Kingsport", 44900, 33500, 20700},
            };
            const Row later[] = {
                {"Northfield", 38900, 31700, 26300}, {"Harbourside", 20100, 49200, 27700},
                {"Lakeview", 27400, 26100, 41900},   {"Old Town", 16900, 40800, 38600},
                {"Westmoor", 50800, 19600, 19900},   {"Riverbend", 33100, 35300, 29400},
                {"Hillcrest", 45600, 23900, 30100},  {"Eastgate", 24800, 46100, 23500},
                {"Southbay", 25200, 27900, 48600},   {"Milltown", 32900, 41700, 17600},
                {"Greenway", 21800, 24600, 53400},   {"Kingsport", 42700, 30800, 26900},
            };
            TernarySeries first("2022 election");
            for (const Row& r : earlier) first.points.emplace_back(r.blue, r.red, r.gold, r.region);
            TernarySeries second("2026 election");
            second.marker = ChartMarkerShape::Diamond;
            for (const Row& r : later) second.points.emplace_back(r.blue, r.red, r.gold, r.region);
            model.series = {first, second};
            return model;
        }

        TernaryModel SoilSamples() {
            TernaryModel model;
            model.title = "Soil samples (invented)";
            model.subtitle = "Particle-size shares of each sample";
            model.axes[0] = TernaryAxis("Clay");
            model.axes[1] = TernaryAxis("Sand");
            model.axes[2] = TernaryAxis("Silt");
            model.reading = TernaryReading::Clockwise;

            // Clay, sand, silt in percent, as a lab reports them - they do not
            // always add up to exactly 100. Every sample is made up.
            struct Row { const char* id; double clay, sand, silt; };
            const Row river[] = {
                {"R-01", 8.2, 71.5, 20.1}, {"R-02", 11.4, 62.0, 26.9}, {"R-03", 6.1, 82.3, 11.4},
                {"R-04", 14.8, 55.2, 30.3}, {"R-05", 9.7, 68.8, 21.6}, {"R-06", 4.3, 88.1, 7.5},
                {"R-07", 18.2, 47.9, 33.6}, {"R-08", 12.6, 58.4, 29.1},
            };
            const Row upland[] = {
                {"U-01", 27.3, 22.4, 50.1}, {"U-02", 33.8, 18.1, 48.0}, {"U-03", 22.1, 30.5, 47.6},
                {"U-04", 41.5, 15.2, 43.4}, {"U-05", 18.9, 25.7, 55.2}, {"U-06", 36.2, 28.0, 35.9},
                {"U-07", 12.4, 14.3, 73.1}, {"U-08", 29.9, 33.6, 36.4},
            };
            const Row basin[] = {
                {"B-01", 52.3, 12.8, 34.7}, {"B-02", 46.1, 21.5, 32.5}, {"B-03", 61.8, 9.4, 28.9},
                {"B-04", 43.7, 38.2, 18.0}, {"B-05", 38.5, 44.9, 16.8}, {"B-06", 57.0, 18.6, 24.3},
            };
            auto add = [&model](const char* name, const Row* rows, size_t count, ChartMarkerShape marker) {
                TernarySeries s(name);
                s.marker = marker;
                for (size_t i = 0; i < count; ++i) s.points.emplace_back(rows[i].clay, rows[i].sand, rows[i].silt, rows[i].id);
                model.series.push_back(s);
            };
            add("River terrace", river, sizeof(river) / sizeof(river[0]), ChartMarkerShape::Circle);
            add("Upland", upland, sizeof(upland) / sizeof(upland[0]), ChartMarkerShape::Square);
            add("Basin", basin, sizeof(basin) / sizeof(basin[0]), ChartMarkerShape::TriangleUp);
            return model;
        }

        TernaryModel SoilTexture() {
            TernaryModel model;
            model.title = "USDA soil texture triangle";
            model.subtitle = "The twelve texture classes by particle size";
            model.axes[0] = TernaryAxis("Clay");
            model.axes[1] = TernaryAxis("Sand");
            model.axes[2] = TernaryAxis("Silt");
            model.reading = TernaryReading::Clockwise;

            // The outlines are derived from the class rules of the USDA Soil
            // Survey Manual by clipping the triangle with each rule's limits,
            // e.g. loam: clay 7-27 %, silt 28-50 %, sand at most 52 %; silt
            // loam: silt at least 50 % with clay 12-27 %, or silt 50-80 % with
            // clay under 12 %. Corners are (clay, sand, silt) in percent, and
            // TernaryModelTest checks RegionOf against the rules on a grid.
            struct Corner { double clay, sand, silt; };
            struct Class { const char* id; const char* name; std::vector<Corner> outline; Color fill; };
            const std::vector<Class> classes = {
                {"sand", "Sand", {{10, 90, 0}, {0, 100, 0}, {0, 85, 15}}, Color(244, 226, 182, 255)},
                {"loamy-sand", "Loamy sand", {{15, 85, 0}, {10, 90, 0}, {0, 85, 15}, {0, 70, 30}},
                 Color(240, 210, 160, 255)},
                {"sandy-loam", "Sandy loam",
                 {{20, 80, 0}, {15, 85, 0}, {7, 77, 16}, {0, 70, 30}, {0, 50, 50}, {7, 43, 50}, {7, 52, 41}, {20, 52, 28}},
                 Color(232, 196, 150, 255)},
                {"loam", "Loam", {{7, 52, 41}, {7, 43, 50}, {27, 23, 50}, {27, 45, 28}, {20, 52, 28}},
                 Color(214, 196, 160, 255)},
                {"silt-loam", "Silt loam",
                 {{0, 50, 50}, {0, 20, 80}, {12, 8, 80}, {12, 0, 88}, {27, 0, 73}, {27, 23, 50}, {12, 38, 50}},
                 Color(205, 214, 178, 255)},
                {"silt", "Silt", {{0, 20, 80}, {0, 0, 100}, {12, 0, 88}, {12, 8, 80}}, Color(222, 232, 196, 255)},
                {"sandy-clay-loam", "Sandy clay loam", {{35, 65, 0}, {20, 80, 0}, {20, 52, 28}, {27, 45, 28}, {35, 45, 20}},
                 Color(226, 178, 146, 255)},
                {"clay-loam", "Clay loam", {{27, 45, 28}, {27, 20, 53}, {40, 20, 40}, {40, 45, 15}},
                 Color(200, 178, 156, 255)},
                {"silty-clay-loam", "Silty clay loam", {{27, 20, 53}, {27, 0, 73}, {40, 0, 60}, {40, 20, 40}},
                 Color(188, 200, 170, 255)},
                {"sandy-clay", "Sandy clay", {{55, 45, 0}, {35, 65, 0}, {35, 45, 20}}, Color(214, 160, 140, 255)},
                {"silty-clay", "Silty clay", {{40, 20, 40}, {40, 0, 60}, {60, 0, 40}}, Color(176, 190, 176, 255)},
                {"clay", "Clay", {{100, 0, 0}, {55, 45, 0}, {40, 45, 15}, {40, 20, 40}, {60, 0, 40}},
                 Color(196, 168, 168, 255)},
            };
            for (const Class& c : classes) {
                TernaryRegion region;
                region.id = c.id;
                region.name = c.name;
                region.fill = c.fill;
                for (const Corner& corner : c.outline) region.outline.emplace_back(corner.clay, corner.sand, corner.silt);
                model.regions.push_back(region);
            }
            return model;
        }

        TernaryModel DeFinetti() {
            TernaryModel model;
            model.title = "Genotype shares of invented populations";
            model.subtitle = "Marker area: individuals sampled. Dashed: Hardy-Weinberg equilibrium";
            model.axes[0] = TernaryAxis("Aa");
            model.axes[1] = TernaryAxis("AA");
            model.axes[2] = TernaryAxis("aa");
            model.reading = TernaryReading::Anticlockwise;

            // Hardy-Weinberg: AA = p^2, Aa = 2pq, aa = q^2 for allele share p.
            TernaryCurve hw;
            hw.name = "Hardy-Weinberg equilibrium";
            for (int k = 0; k <= 40; ++k) {
                const double p = k / 40.0, q = 1.0 - p;
                hw.points.emplace_back(2.0 * p * q, p * p, q * q);
            }
            model.curves.push_back(hw);

            // Counts from p and an inbreeding coefficient F: AA = p^2 + Fpq,
            // Aa = 2pq(1 - F), aa = q^2 + Fpq. Every population is made up.
            struct Population { const char* name; double p, f; int n; };
            const Population outbred[] = {
                {"Valley A", 0.30, 0.01, 420}, {"Valley B", 0.45, 0.00, 260}, {"Coast", 0.62, 0.02, 610},
                {"Plateau", 0.78, 0.01, 180}, {"Delta", 0.52, 0.03, 340}, {"Forest", 0.18, 0.02, 150},
            };
            const Population inbred[] = {
                {"Island 1", 0.40, 0.35, 90}, {"Island 2", 0.65, 0.28, 120}, {"Mountain", 0.25, 0.42, 70},
                {"Oasis", 0.55, 0.20, 110},
            };
            auto add = [&model](const char* name, const Population* pops, size_t count, ChartMarkerShape marker) {
                TernarySeries s(name);
                s.marker = marker;
                for (size_t i = 0; i < count; ++i) {
                    const Population& pop = pops[i];
                    const double p = pop.p, q = 1.0 - p;
                    const double aa = std::round(pop.n * (p * p + pop.f * p * q));
                    const double het = std::round(pop.n * 2.0 * p * q * (1.0 - pop.f));
                    const double recessive = pop.n - aa - het;
                    TernaryPoint point(het, aa, recessive, pop.name);
                    point.size = pop.n;
                    point.note = std::to_string(pop.n) + " individuals sampled";
                    s.points.push_back(point);
                }
                model.series.push_back(s);
            };
            add("Mainland", outbred, sizeof(outbred) / sizeof(outbred[0]), ChartMarkerShape::Circle);
            add("Isolated", inbred, sizeof(inbred) / sizeof(inbred[0]), ChartMarkerShape::Square);
            return model;
        }

        TernaryModel VoteTrajectories() {
            TernaryModel model = ThreePartyVotes();
            model.title = "How four regions voted, 2010-2026 (invented)";
            model.subtitle = "Each path runs from the first election to the last";
            model.series.clear();
            struct Election { const char* year; double blue, red, gold; };
            struct Region { const char* name; Election elections[5]; };
            const Region regions[] = {
                {"Northfield", {{"2010", 52, 38, 10}, {"2014", 49, 36, 15}, {"2018", 45, 35, 20},
                                {"2022", 43, 35, 22}, {"2026", 40, 32, 28}}},
                {"Harbourside", {{"2010", 30, 60, 10}, {"2014", 28, 58, 14}, {"2018", 25, 55, 20},
                                 {"2022", 24, 54, 22}, {"2026", 21, 51, 28}}},
                {"Greenway", {{"2010", 35, 40, 25}, {"2014", 31, 37, 32}, {"2018", 28, 33, 39},
                              {"2022", 25, 28, 47}, {"2026", 22, 24, 54}}},
                {"Westmoor", {{"2010", 58, 30, 12}, {"2014", 60, 26, 14}, {"2018", 61, 22, 17},
                              {"2022", 59, 22, 19}, {"2026", 56, 21, 23}}},
            };
            for (const Region& r : regions) {
                TernarySeries s(r.name);
                s.kind = TernarySeriesKind::Path;
                s.arrows = true;
                s.markerRadius = 3.5f;
                for (const Election& e : r.elections) s.points.emplace_back(e.blue, e.red, e.gold, e.year);
                model.series.push_back(s);
            }
            return model;
        }

    } // namespace TernarySamples

} // namespace UltraCanvas
