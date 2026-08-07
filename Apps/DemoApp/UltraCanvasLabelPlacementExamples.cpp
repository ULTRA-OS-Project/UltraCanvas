// Apps/DemoApp/UltraCanvasLabelPlacementExamples.cpp
// Interactive showcase for the layout engine's label placement solver
// (core/UltraCanvasLabelPlacement.cpp): labels around rectangles and circles,
// with lines acting as keep-out obstacles.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabelPlacement.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// SCENE CANVAS
// =============================================================================

// Draws a small scene of rectangles, circles and lines and labels every shape
// through PlaceShapeLabels(). The toggles let the viewer compare the solved
// placement against the naive one and see each solver feature in isolation.
    class LabelPlacementCanvas : public UltraCanvasUIElement {
    public:
        // How the labels want to sit relative to their shape.
        enum class Style {
            Outside,   // beside the shape (chart convention)
            Inside,    // centred on the shape, overflow allowed (node convention)
            Border     // straddling the outline at 2 o'clock
        };

        LabelPlacementCanvas(const std::string& id, float x, float y, float w, float h)
                : UltraCanvasUIElement(id, x, y, w, h) {
            BuildScene();
        }

        void SetStyle(Style s)          { style = s; RequestRedraw(); }
        Style GetStyle() const          { return style; }
        void SetSolverEnabled(bool on)  { solverEnabled = on; RequestRedraw(); }
        bool GetSolverEnabled() const   { return solverEnabled; }
        void SetLinesAreObstacles(bool on) { linesAreObstacles = on; RequestRedraw(); }
        bool GetLinesAreObstacles() const  { return linesAreObstacles; }

        void Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) override {
            if (!ctx) return;

            ctx->SetFillPaint(Color(252, 252, 253, 255));
            ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()));
            ctx->SetStrokePaint(Color(210, 210, 215, 255));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(Rect2Dd(0.5, 0.5, GetWidth() - 1.0, GetHeight() - 1.0));

            DrawLines(ctx);
            DrawShapes(ctx);
            DrawLabels(ctx);
        }

    private:
        struct SceneShape {
            LabelShapeType type = LabelShapeType::Circle;
            Point2Dd center;
            double radius = 0.0;
            Size2Dd size;
            std::string label;
            Color fill;
        };
        struct SceneLine {
            Point2Dd a, b;
        };

        std::vector<SceneShape> shapes;
        std::vector<SceneLine> lines;
        Style style = Style::Outside;
        bool solverEnabled = true;
        bool linesAreObstacles = true;

        static Color Fill(int i) {
            static const Color palette[] = {
                    Color(126, 172, 224, 255), Color(226, 138, 128, 255),
                    Color(129, 199, 156, 255), Color(233, 196, 122, 255),
                    Color(168, 152, 214, 255), Color(150, 150, 158, 255),
            };
            return palette[i % 6];
        }

        // A deliberately crowded scene: shapes are close enough that the naive
        // placement collides, so the difference the solver makes is visible.
        void BuildScene() {
            const char* names[] = {
                    "Reception", "Archive", "Studio", "Server Room",
                    "Kitchenette", "Lift", "Print / Copy", "Terrace"
            };
            struct Def { bool circle; double x, y, a, b; };
            const Def defs[] = {
                    {true,  120,  90,  46, 0},   {true,  232, 118,  26, 0},
                    {true,  318,  74,  34, 0},   {true,  126, 226,  22, 0},
                    {true,  236, 232,  30, 0},   {true,  330, 210,  18, 0},
                    {false, 442, 104, 104, 56},  {false, 446, 236,  92, 48},
            };
            for (int i = 0; i < 8; ++i) {
                SceneShape s;
                s.type = defs[i].circle ? LabelShapeType::Circle : LabelShapeType::Rectangle;
                s.center = Point2Dd(defs[i].x, defs[i].y);
                s.radius = defs[i].a;
                s.size = Size2Dd(defs[i].a, defs[i].b);
                s.label = names[i];
                s.fill = Fill(i);
                shapes.push_back(s);
            }
            // Connector lines the labels must keep clear of.
            lines.push_back({Point2Dd(120, 90), Point2Dd(232, 118)});
            lines.push_back({Point2Dd(232, 118), Point2Dd(318, 74)});
            lines.push_back({Point2Dd(126, 226), Point2Dd(236, 232)});
            lines.push_back({Point2Dd(236, 232), Point2Dd(330, 210)});
            lines.push_back({Point2Dd(318, 74), Point2Dd(442, 104)});
        }

        // A line is handed to the solver as the bounding rects of short
        // sub-segments, which approximates a diagonal far better than one
        // bounding box around the whole line.
        void AppendLineObstacles(std::vector<Rect2Dd>& out, const SceneLine& l) const {
            const int steps = 12;
            const double halfThickness = 3.0;
            for (int i = 0; i < steps; ++i) {
                double t0 = static_cast<double>(i) / steps;
                double t1 = static_cast<double>(i + 1) / steps;
                double x0 = l.a.x + (l.b.x - l.a.x) * t0;
                double y0 = l.a.y + (l.b.y - l.a.y) * t0;
                double x1 = l.a.x + (l.b.x - l.a.x) * t1;
                double y1 = l.a.y + (l.b.y - l.a.y) * t1;
                out.push_back(Rect2Dd(std::min(x0, x1) - halfThickness,
                                      std::min(y0, y1) - halfThickness,
                                      std::abs(x1 - x0) + halfThickness * 2,
                                      std::abs(y1 - y0) + halfThickness * 2));
            }
        }

        void DrawLines(IRenderContext* ctx) const {
            ctx->SetStrokePaint(linesAreObstacles ? Color(90, 110, 160, 220)
                                                  : Color(200, 200, 205, 200));
            ctx->SetStrokeWidth(linesAreObstacles ? 2.0f : 1.5f);
            for (const auto& l : lines) {
                ctx->DrawLine(l.a, l.b);
            }
        }

        void DrawShapes(IRenderContext* ctx) const {
            for (const auto& s : shapes) {
                ctx->SetFillPaint(s.fill);
                ctx->SetStrokePaint(Color(70, 70, 78, 200));
                ctx->SetStrokeWidth(1.2f);
                if (s.type == LabelShapeType::Circle) {
                    ctx->FillCircle(s.center, s.radius);
                    ctx->DrawCircle(s.center, s.radius);
                } else {
                    Rect2Dd r(s.center.x - s.size.width * 0.5,
                              s.center.y - s.size.height * 0.5,
                              s.size.width, s.size.height);
                    ctx->FillRectangle(r);
                    ctx->DrawRectangle(r);
                }
            }
        }

        void DrawHalo(IRenderContext* ctx, const std::string& text, const Point2Dd& at) const {
            ctx->SetTextPaint(Color(255, 255, 255, 225));
            static const double d[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
            for (const auto& o : d) {
                ctx->DrawText(text, Point2Dd(at.x + o[0], at.y + o[1]));
            }
        }

        void DrawLabels(IRenderContext* ctx) {
            ctx->SetFontSize(11.0f);

            std::vector<LabelShape> solverShapes(shapes.size());
            std::vector<ShapeLabel> labels;
            for (size_t i = 0; i < shapes.size(); ++i) {
                solverShapes[i].type = shapes[i].type;
                solverShapes[i].center = shapes[i].center;
                solverShapes[i].radius = shapes[i].radius;
                solverShapes[i].size = shapes[i].size;

                ShapeLabel l;
                l.text = shapes[i].label;
                l.shapeIndex = i;
                Size2Di ts = ctx->GetTextLineDimensions(shapes[i].label);
                l.textSize = Size2Dd(ts.width, ts.height);
                switch (style) {
                    case Style::Outside:
                        l.preferredSide = LabelSide::Auto;
                        break;
                    case Style::Inside:
                        l.preferredSide = LabelSide::Inside;
                        l.tolerateShapeOverflow = true;
                        l.allowOutsideFallback = true;
                        break;
                    case Style::Border:
                        l.preferredSide = LabelSide::Border;
                        l.borderAngles = {60.0, 120.0, 300.0, 240.0};
                        break;
                }
                labels.push_back(l);
            }

            // Smallest shapes first - the cramped ones get first pick.
            std::stable_sort(labels.begin(), labels.end(),
                             [&](const ShapeLabel& a, const ShapeLabel& b) {
                                 const auto& sa = shapes[a.shapeIndex];
                                 const auto& sb = shapes[b.shapeIndex];
                                 double ra = sa.type == LabelShapeType::Circle
                                             ? sa.radius : std::min(sa.size.width, sa.size.height) * 0.5;
                                 double rb = sb.type == LabelShapeType::Circle
                                             ? sb.radius : std::min(sb.size.width, sb.size.height) * 0.5;
                                 return ra < rb;
                             });

            std::vector<PlacedShapeLabel> placed;
            if (solverEnabled) {
                LabelPlacementOptions opts;
                opts.bounds = Rect2Dd(4, 4, GetWidth() - 8, GetHeight() - 8);
                opts.shapeMargin = 5.0;
                opts.labelMargin = 3.0;
                if (linesAreObstacles) {
                    for (const auto& l : lines) AppendLineObstacles(opts.obstacles, l);
                }
                placed = PlaceShapeLabels(solverShapes, labels, opts);
            } else {
                // Naive reference placement: whatever the style asks for, applied
                // blindly with no collision handling at all.
                placed.resize(labels.size());
                for (size_t i = 0; i < labels.size(); ++i) {
                    const SceneShape& s = shapes[labels[i].shapeIndex];
                    double halfH = s.type == LabelShapeType::Circle ? s.radius
                                                                    : s.size.height * 0.5;
                    double cy = (style == Style::Outside)
                                ? s.center.y - halfH - 5.0 - labels[i].textSize.height
                                : s.center.y - labels[i].textSize.height * 0.5;
                    placed[i].bounds = Rect2Dd(s.center.x - labels[i].textSize.width * 0.5,
                                               cy, labels[i].textSize.width,
                                               labels[i].textSize.height);
                    placed[i].side = (style == Style::Outside) ? LabelSide::Top
                                                               : LabelSide::Inside;
                }
            }

            // Highlight anything still overlapping so the comparison is honest.
            std::vector<bool> clashes(placed.size(), false);
            for (size_t i = 0; i < placed.size(); ++i) {
                for (size_t j = i + 1; j < placed.size(); ++j) {
                    const Rect2Dd& a = placed[i].bounds;
                    const Rect2Dd& b = placed[j].bounds;
                    if (a.Left() < b.Right() && b.Left() < a.Right() &&
                        a.Top() < b.Bottom() && b.Top() < a.Bottom()) {
                        clashes[i] = clashes[j] = true;
                    }
                }
            }

            for (size_t i = 0; i < placed.size(); ++i) {
                Point2Dd at = placed[i].bounds.TopLeft();
                bool overShape = (placed[i].side == LabelSide::Inside ||
                                  placed[i].side == LabelSide::Border);
                if (overShape) DrawHalo(ctx, labels[i].text, at);
                ctx->SetTextPaint(clashes[i] ? Color(200, 30, 30, 255)
                                             : Color(35, 35, 40, 255));
                ctx->DrawText(labels[i].text, at);
            }
        }
    };

// =============================================================================
// TAB CONTENT
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> CreateLabelPlacementLayoutTab() {
        auto tab = std::make_shared<UltraCanvasContainer>("LabelPlacementTab", 0, 0, 1020, 640);
        tab->SetBackgroundColor(Colors::White);

        auto title = std::make_shared<UltraCanvasLabel>("LPTitle", 20, 12, 900, 26);
        title->SetText("Label Placement Solver");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        tab->AddChild(title);

        auto desc = std::make_shared<UltraCanvasLabel>("LPDesc", 20, 40, 960, 56);
        desc->SetText("PlaceShapeLabels() is part of the layout engine (core/UltraCanvasLabelPlacement.cpp), "
                      "so every chart and diagram shares one implementation. Each label names a shape "
                      "(rectangle or circle), asks for a preferred position, and the solver moves it only when "
                      "that position would collide with another label, another shape, a keep-out obstacle such "
                      "as a connector line, or the bounds. Overlapping labels are drawn in red.");
        desc->SetFontSize(11.5f);
        desc->SetTextColor(Color(80, 80, 80, 255));
        desc->SetWrap(TextWrap::WrapWord);
        tab->AddChild(desc);

        auto canvas = std::make_shared<LabelPlacementCanvas>("LPCanvas", 20, 140, 560, 320);
        tab->AddChild(canvas);

        auto status = std::make_shared<UltraCanvasLabel>("LPStatus", 600, 404, 400, 56);
        status->SetFontSize(11);
        status->SetTextColor(Color(60, 60, 60, 255));
        status->SetWrap(TextWrap::WrapWord);
        tab->AddChild(status);

        auto refreshStatus = [canvas, status]() {
            std::string s = "Solver: ";
            s += canvas->GetSolverEnabled() ? "on" : "off (naive placement)";
            s += "\nStyle: ";
            switch (canvas->GetStyle()) {
                case LabelPlacementCanvas::Style::Outside: s += "outside the shape"; break;
                case LabelPlacementCanvas::Style::Inside:  s += "centred on the shape (overflow allowed)"; break;
                case LabelPlacementCanvas::Style::Border:  s += "straddling the outline at 2 o'clock"; break;
            }
            s += "\nLines as obstacles: ";
            s += canvas->GetLinesAreObstacles() ? "yes" : "no";
            status->SetText(s);
        };

        float bx = 600.0f, by = 140.0f;
        auto addButton = [&](const std::string& id, const std::string& text,
                             std::function<void()> action) {
            auto b = std::make_shared<UltraCanvasButton>(id, bx, by, 190.0f, 30.0f, text);
            b->SetOnClick([action, refreshStatus]() { action(); refreshStatus(); });
            tab->AddChild(b);
            by += 36.0f;
        };

        auto sectionLabel = [&](const std::string& id, const std::string& text) {
            auto l = std::make_shared<UltraCanvasLabel>(id, static_cast<long>(bx),
                                                        static_cast<long>(by), 260, 20);
            l->SetText(text);
            l->SetFontSize(11);
            l->SetFontWeight(FontWeight::Bold);
            l->SetTextColor(Color(50, 50, 150, 255));
            tab->AddChild(l);
            by += 24.0f;
        };

        auto addSwitch = [&](const std::string& id, const std::string& text, bool initial,
                             std::function<void(bool)> action) {
            auto s = UltraCanvasSwitch::Create(id, bx, by + 4.0f, text, initial);
            s->onStateChanged = [action, refreshStatus](CheckedState, CheckedState newState) {
                action(newState == CheckedState::Checked);
                refreshStatus();
            };
            tab->AddChild(s);
            by += 36.0f;
        };

        sectionLabel("LPSecSolver", "Solver");
        addSwitch("LPToggleSolver", "Solver", canvas->GetSolverEnabled(),
                  [canvas](bool on) { canvas->SetSolverEnabled(on); });
        addSwitch("LPToggleLines", "Lines as obstacles", canvas->GetLinesAreObstacles(),
                  [canvas](bool on) { canvas->SetLinesAreObstacles(on); });

        by += 8.0f;
        sectionLabel("LPSecStyle", "Preferred position");
        addButton("LPStyleOutside", "Outside the shape",
                  [canvas]() { canvas->SetStyle(LabelPlacementCanvas::Style::Outside); });
        addButton("LPStyleInside", "Centred on the shape",
                  [canvas]() { canvas->SetStyle(LabelPlacementCanvas::Style::Inside); });
        addButton("LPStyleBorder", "On the outline (2 o'clock)",
                  [canvas]() { canvas->SetStyle(LabelPlacementCanvas::Style::Border); });

        refreshStatus();

        auto note = std::make_shared<UltraCanvasLabel>("LPNote", 20, 476, 960, 130);
        note->SetText(
                "What each control shows:\n"
                "• Solver off — every label goes to its preferred spot regardless of what is already there; "
                "the red labels are the collisions this produces.\n"
                "• Lines as obstacles — connector lines become keep-out rectangles, so labels step aside "
                "instead of sitting on a line. Charts use the same mechanism for legends and axis titles.\n"
                "• Outside / Centred / On the outline — the three conventions. Centred tolerates text spilling "
                "over the outline (node diagrams, drawn with a halo) but still refuses to stack two labels; "
                "on-the-outline places the text on the border at a clock-face angle.\n"
                "Placement is greedy in input order, so submission order is the priority: this scene submits the "
                "smallest shapes first, because their labels have the least room to move.");
        note->SetFontSize(11);
        note->SetTextColor(Color(80, 80, 80, 255));
        note->SetWrap(TextWrap::WrapWord);
        tab->AddChild(note);

        return tab;
    }

} // namespace UltraCanvas
