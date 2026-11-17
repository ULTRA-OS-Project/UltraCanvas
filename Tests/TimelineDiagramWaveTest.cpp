// Tests/TimelineDiagramWaveTest.cpp
// The timeline diagram's Wave design, headless.
//
//  1. The half-rings chain: each one ends where the next begins, at the
//     next item's node, and they bulge to alternate sides, so the path is
//     one smooth wave; with even placement they are all the same size.
//  2. Every item's text block lies inside the element, clear of every ring
//     and of every other text block - for a horizontal, a falling, a steep,
//     a vertical and a rising axis, for 1 to 12 items, reversed, by date,
//     all on one side, and with the angle picked automatically.
//  3. Text beside the rings is flush to its ring: left-aligned on the right
//     of a ring, right-aligned on the left of one.
//  4. A ring is drawn in its item's colour, lighter once it is pending
//     (SetCurrentIndex), and the inside of the ring in the inside colour.
//  5. A click on a ring or on its text selects that item; a click between
//     them selects nothing.
//
// Pictures are drawn into an offscreen surface and read back pixel by pixel;
// geometry is read through GetItemArc / GetItemNodeCenter / GetItemContentRect.
// Version: 1.0.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasTimelineDiagram.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;
int g_count = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
    ++g_count;
}

// A white surface to draw one diagram into, read back pixel by pixel.
struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        cairo_save(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    Color At(double x, double y) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const auto px = *reinterpret_cast<const uint32_t*>(
                data + static_cast<int>(std::lround(y)) * stride + static_cast<int>(std::lround(x)) * 4);
        return Color((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, 255);
    }

    // For looking at a layout by eye: TIMELINE_WAVE_PNG_DIR=<dir>
    void Save(const std::string& name) const {
        const char* dir = std::getenv("TIMELINE_WAVE_PNG_DIR");
        if (!dir || !cr) return;
        cairo_surface_write_to_png(cairo_get_target(cr), (std::string(dir) + "/" + name + ".png").c_str());
    }
};

bool Near(const Color& a, const Color& b, int tolerance = 14) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

UCEvent Click(double x, double y) {
    UCEvent e;
    e.type = UCEventType::MouseDown;
    e.pointer = Point2Di(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    e.button = UCMouseButton::Left;
    return e;
}

struct Ring {
    Point2Dd center;
    double radius = 0.0;
    double start = 0.0;

    Point2Dd At(double angle, double r) const {
        return Point2Dd(center.x + r * std::cos(angle), center.y + r * std::sin(angle));
    }
    Point2Dd Begin() const { return At(start, radius); }
    Point2Dd End() const { return At(start + M_PI, radius); }
    Point2Dd Apex(double out = 0.0) const { return At(start + M_PI / 2.0, radius + out); }
};

double Distance(const Point2Dd& a, const Point2Dd& b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool Inside(const Rect2Dd& r, const Point2Dd& p) {
    return p.x > r.x && p.x < r.x + r.width && p.y > r.y && p.y < r.y + r.height;
}

bool Overlap(const Rect2Dd& a, const Rect2Dd& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

std::shared_ptr<UltraCanvasTimelineDiagram> MakeWave(int w, int h, size_t count) {
    auto diagram = CreateTimelineDiagramElement("Wave", 0, 0, w, h, TimelineDesign::Wave);
    std::vector<TimelineItem> list = TimelineDiagramSamples::ProjectYear(2026);
    list.resize(count);
    for (size_t i = 0; i < list.size(); ++i) list[i].caption = std::to_string(1970 + 10 * i);
    diagram->SetItems(list);
    diagram->SetTitle("Timeline");
    return diagram;
}

std::vector<Ring> Rings(const UltraCanvasTimelineDiagram& d) {
    std::vector<Ring> rings;
    for (size_t i = 0; i < d.CountItems(); ++i) {
        Ring r;
        if (!d.GetItemArc(i, r.center, r.radius, r.start)) return {};
        rings.push_back(r);
    }
    return rings;
}

// The item's node is where its ring begins along the wave; the ring is drawn
// clockwise from its start angle, so that is either end.
Point2Dd FirstEnd(const Ring& r, const Point2Dd& node) {
    return Distance(r.Begin(), node) < Distance(r.End(), node) ? r.Begin() : r.End();
}
Point2Dd SecondEnd(const Ring& r, const Point2Dd& node) {
    return Distance(r.Begin(), node) < Distance(r.End(), node) ? r.End() : r.Begin();
}

// --- 1 ---------------------------------------------------------------------

void TestRingsChain() {
    std::cout << "1. The rings chain into one wave\n";
    Canvas canvas(900, 640);
    auto d = MakeWave(900, 640, 6);
    d->Render(canvas.ctx.get(), Rect2Df(0, 0, 900, 640));
    canvas.Save("wave-6-falling");

    const std::vector<Ring> rings = Rings(*d);
    Check(rings.size() == 6, "every item has a ring");
    if (rings.size() != 6) return;

    bool chained = true, alternate = true, sameSize = true;
    for (size_t i = 0; i + 1 < rings.size(); ++i) {
        Point2Dd node, next;
        d->GetItemNodeCenter(i, node);
        d->GetItemNodeCenter(i + 1, next);
        if (Distance(FirstEnd(rings[i], node), node) > 0.5) chained = false;
        if (Distance(SecondEnd(rings[i], node), next) > 0.5) chained = false;
        // Bulging to the other side: the apexes lie on opposite sides of the axis
        const Point2Dd a = SecondEnd(rings[i], node);
        const Point2Dd b = FirstEnd(rings[i], node);
        auto sideOf = [&](const Point2Dd& p) {
            return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        };
        const Point2Dd nextApex = rings[i + 1].Apex();
        const Point2Dd ownApex = rings[i].Apex();
        // Mirror the next apex through the shared node into this ring's chord frame
        const Point2Dd mirrored(2.0 * next.x - nextApex.x, 2.0 * next.y - nextApex.y);
        if (sideOf(ownApex) * sideOf(mirrored) <= 0.0) alternate = false;
        if (std::abs(rings[i].radius - rings[i + 1].radius) > 0.5) sameSize = false;
    }
    Check(chained, "each ring starts at its item's node and ends at the next item's");
    Check(alternate, "neighbouring rings bulge to opposite sides");
    Check(sameSize, "with even placement every ring has the same radius");

    // Falling diagonal: each node is below and to the right of the one before
    bool falling = true;
    for (size_t i = 0; i + 1 < rings.size(); ++i) {
        Point2Dd a, b;
        d->GetItemNodeCenter(i, a);
        d->GetItemNodeCenter(i + 1, b);
        if (!(b.x > a.x + 1.0 && b.y > a.y + 1.0)) falling = false;
    }
    Check(falling, "the default 45 degree axis falls from the top left to the bottom right");
    Check(rings[0].Apex().y < rings[0].center.y && rings[0].Apex().x > rings[0].center.x,
          "the first ring bulges up and to the right, as in the reference");
}

// --- 2 / 3 -----------------------------------------------------------------

struct Case {
    std::string name;
    int width, height;
    size_t count;
    double angle;
    bool autoAngle = false;
    bool reverse = false;
    bool proportional = false;
    TimelineSidePolicy side = TimelineSidePolicy::Alternate;
};

void CheckLayout(const Case& c) {
    Canvas canvas(c.width, c.height);
    auto d = MakeWave(c.width, c.height, c.count);
    d->EditStyle().waveAxisAngle = c.angle;
    d->EditStyle().waveAutoAngle = c.autoAngle;
    d->StyleChanged();
    d->SetReverseOrder(c.reverse);
    d->SetSidePolicy(c.side);
    if (c.proportional) {
        // Uneven dates, so the rings differ in size
        std::vector<TimelineItem> list = d->GetItems();
        const int months[] = {1, 2, 5, 6, 7, 11, 12, 12, 12, 12, 12, 12};
        for (size_t i = 0; i < list.size(); ++i) list[i].SetDate(2026, months[i % 12], 1 + static_cast<int>(i));
        d->SetItems(list);
        d->SetPlacement(TimelinePlacement::Proportional);
    }
    d->Render(canvas.ctx.get(), Rect2Df(0, 0, static_cast<float>(c.width), static_cast<float>(c.height)));
    canvas.Save("wave-" + c.name);

    const std::vector<Ring> rings = Rings(*d);
    const double halfT = d->GetStyle().spineThickness / 2.0;
    const Rect2Dd frame(0, 0, c.width, c.height);

    bool inFrame = true, clearOfRings = true, clearOfText = true, flush = true, sized = true;
    std::string detail;
    std::vector<Rect2Dd> texts(c.count);
    for (size_t i = 0; i < c.count; ++i) {
        d->GetItemContentRect(i, texts[i]);
        const Rect2Dd& t = texts[i];
        if (t.width < 30.0 || t.height < 10.0) { sized = false; detail += " #" + std::to_string(i) + " tiny"; }
        if (t.x < -0.5 || t.y < -0.5 || t.x + t.width > c.width + 0.5 || t.y + t.height > c.height + 0.5) {
            inFrame = false;
            detail += " #" + std::to_string(i) + " out";
        }
    }
    for (size_t i = 0; i < rings.size(); ++i) {
        const Ring& r = rings[i];
        // The ring's band and the half-disc it encloses, sampled densely
        for (int k = 0; k <= 72; ++k) {
            const double a = r.start + M_PI * k / 72.0;
            for (double rr : {0.0, r.radius * 0.5, r.radius - halfT, r.radius, r.radius + halfT}) {
                const Point2Dd p = r.At(a, rr);
                if (p.x < -0.5 || p.y < -0.5 || p.x > c.width + 0.5 || p.y > c.height + 0.5) inFrame = false;
                for (size_t j = 0; j < c.count; ++j) {
                    if (Inside(texts[j], p)) {
                        clearOfRings = false;
                        detail += " text#" + std::to_string(j) + "/ring#" + std::to_string(i);
                        k = 73;
                        break;
                    }
                }
                if (k > 72) break;
            }
        }
    }
    for (size_t i = 0; i < c.count; ++i) {
        for (size_t j = i + 1; j < c.count; ++j) {
            if (Overlap(texts[i], texts[j])) {
                clearOfText = false;
                detail += " text#" + std::to_string(i) + "/text#" + std::to_string(j);
            }
        }
    }
    // Beside a ring (a steep axis), the text block's near edge faces the ring
    const bool beside = !c.autoAngle && std::abs(c.angle) >= 40.0;
    if (beside) {
        for (size_t i = 0; i < rings.size(); ++i) {
            const Point2Dd apex = rings[i].Apex();
            const Rect2Dd& t = texts[i];
            const bool right = apex.x > rings[i].center.x;
            if (right ? (t.x < apex.x) : (t.x + t.width > apex.x)) flush = false;
        }
    }

    Check(rings.size() == c.count, c.name + ": every item has a ring");
    Check(inFrame, c.name + ": rings and text stay inside the element" + (inFrame ? "" : detail));
    Check(sized, c.name + ": every text block has room" + (sized ? "" : detail));
    Check(clearOfRings, c.name + ": no text block covers a ring" + (clearOfRings ? "" : detail));
    Check(clearOfText, c.name + ": no two text blocks overlap" + (clearOfText ? "" : detail));
    if (beside) Check(flush, c.name + ": text sits on the far side of its ring's apex");
}

void TestLayouts() {
    std::cout << "2. Text beside or above the rings, clear of them and of each other\n";
    const std::vector<Case> cases = {
            {"6-falling", 900, 640, 6, 45.0},
            {"6-demo-size", 620, 510, 6, 45.0},
            {"6-horizontal", 900, 520, 6, 0.0},
            {"12-horizontal", 1100, 560, 12, 0.0},
            {"5-gentle", 900, 560, 5, 25.0},
            {"6-steep", 760, 760, 6, 60.0},
            {"5-vertical", 640, 900, 5, 90.0},
            {"6-rising", 900, 640, 6, -45.0},
            {"1-item", 500, 360, 1, 45.0},
            {"2-items", 600, 420, 2, 45.0},
            {"8-reversed", 1000, 700, 8, 45.0, false, true},
            {"7-by-date", 1000, 640, 7, 0.0, false, false, true},
            {"6-all-above", 1000, 520, 6, 0.0, false, false, false, TimelineSidePolicy::AllAbove},
            {"6-auto-wide", 1100, 420, 6, 45.0, true},
            {"6-auto-tall", 520, 900, 6, 45.0, true},
    };
    for (const Case& c : cases) CheckLayout(c);
}

// --- 4 ---------------------------------------------------------------------

void TestColours() {
    std::cout << "3. Ring colours, pending rings and the inside fill\n";
    Canvas canvas(900, 640);
    auto d = MakeWave(900, 640, 6);
    const std::vector<Color> palette = {Color(230, 30, 40, 255), Color(250, 200, 0, 255),
                                        Color(0, 130, 240, 255), Color(255, 110, 0, 255),
                                        Color(140, 170, 0, 255), Color(230, 30, 130, 255)};
    d->SetCustomPalette(palette);
    d->EditStyle().showShadows = false;
    d->StyleChanged();
    d->SetCurrentIndex(3);
    d->Render(canvas.ctx.get(), Rect2Df(0, 0, 900, 640));
    canvas.Save("wave-colours");

    const std::vector<Ring> rings = Rings(*d);
    if (rings.size() != 6) { Check(false, "rings available"); return; }
    bool coloured = true, pendingLighter = true;
    for (size_t i = 0; i < rings.size(); ++i) {
        const Color seen = canvas.At(rings[i].Apex().x, rings[i].Apex().y);
        if (i <= 3) {
            if (!Near(seen, palette[i])) coloured = false;
        } else {
            const int sum = seen.r + seen.g + seen.b;
            const int full = palette[i].r + palette[i].g + palette[i].b;
            if (!(sum > full + 60) || Near(seen, Color(255, 255, 255, 255), 6)) pendingLighter = false;
        }
    }
    Check(coloured, "the band of each ring up to the current one is in its item's colour");
    Check(pendingLighter, "a ring after the current one is drawn lighter, but still drawn");

    // Inside the ring, between the caption and the band
    const Ring& r = rings[2];
    const Point2Dd inside = r.At(r.start + M_PI * 0.2, r.radius * 0.7);
    Check(Near(canvas.At(inside.x, inside.y), d->GetStyle().waveInsideColor, 6),
          "the half-disc inside a ring is filled with the inside colour");
}

// --- 5 ---------------------------------------------------------------------

void TestClicks() {
    std::cout << "4. Clicks select by ring or by text\n";
    Canvas canvas(900, 640);
    auto d = MakeWave(900, 640, 6);
    d->Render(canvas.ctx.get(), Rect2Df(0, 0, 900, 640));
    const std::vector<Ring> rings = Rings(*d);
    if (rings.size() != 6) { Check(false, "rings available"); return; }

    d->OnEvent(Click(rings[2].Apex().x, rings[2].Apex().y));
    Check(d->GetSelectedIndex() == 2, "a click on a ring's band selects its item");

    Rect2Dd text;
    d->GetItemContentRect(4, text);
    d->OnEvent(Click(text.x + text.width / 2.0, text.y + text.height / 2.0));
    Check(d->GetSelectedIndex() == 4, "a click on an item's text selects it");

    // Just outside a ring, on the side it does not bulge to, far from any text
    const Point2Dd away = rings[0].At(rings[0].start - M_PI / 2.0, rings[0].radius * 0.9);
    bool onText = false;
    for (size_t i = 0; i < 6; ++i) {
        Rect2Dd t;
        d->GetItemContentRect(i, t);
        if (Inside(t, away)) onText = true;
    }
    if (!onText) {
        d->OnEvent(Click(away.x, away.y));
        Check(d->GetSelectedIndex() == -1, "a click beside a ring, on its open side, clears the selection");
    }
}

} // namespace

int main() {
    TestRingsChain();
    TestLayouts();
    TestColours();
    TestClicks();
    std::cout << "\n" << (g_count - g_failures) << "/" << g_count << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
