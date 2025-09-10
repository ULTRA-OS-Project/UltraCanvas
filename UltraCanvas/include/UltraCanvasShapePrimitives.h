// UltraCanvasShapePrimitives.h
// Comprehensive geometric shape primitives with standard UltraCanvas properties
// Version: 1.1.2
// Last Modified: 2025-01-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <memory>
#include <cmath>

namespace UltraCanvas {

// ===== SHAPE ENUMS AND STRUCTURES =====

    enum class ShapeType {
        NoneShape = 0,
        Circle = 1,
        Ellipse = 2,
        Rectangle = 3,
        RoundedRectangle = 4,
        Polygon = 5,
        Triangle = 6,
        Line = 7,
        Arc = 8,
        BezierCurve = 9,
        Spline = 10,
        Star = 11,
        Arrow = 12,
        RegularPolygon = 13
    };

// *** REMOVED DUPLICATE ENUMS - Using ones from UltraCanvasRenderInterface.h ***
// FillMode, LineJoin, LineCap are now imported from UltraCanvasRenderInterface.h

    enum class GradientDirection {
        Horizontal = 0,
        Vertical = 1,
        Diagonal = 2,
        Custom = 3
    };

// *** REMOVED DUPLICATE STRUCT - Using GradientStop from UltraCanvasRenderInterface.h ***

    struct ShapeStyle {
        // Fill properties
        FillMode fillMode;
        Color fillColor;
        std::vector<GradientStop> gradientStops;
        GradientDirection gradientDirection;
        Point2D gradientStart, gradientEnd;

        // Stroke properties
        bool hasStroke;
        Color strokeColor;
        float strokeWidth;
        LineJoin lineJoin;
        LineCap lineCap;
        std::vector<float> dashPattern;

        // Shadow properties
        bool hasShadow;
        Color shadowColor;
        Point2D shadowOffset;
        float shadowBlur;

        // Default constructor
        ShapeStyle() {
            fillMode = FillMode::Solid;
            fillColor = Color(128, 128, 128, 255);
            gradientDirection = GradientDirection::Horizontal;
            hasStroke = true;
            strokeColor = Color(0, 0, 0, 255);
            strokeWidth = 1.0f;
            lineJoin = LineJoin::Miter;
            lineCap = LineCap::Butt;
            hasShadow = false;
            shadowColor = Color(0, 0, 0, 128);
            shadowOffset = Point2D(2, 2);
            shadowBlur = 2.0f;
        }
    };

// ===== BASE SHAPE CLASS =====
    class UltraCanvasShape : public UltraCanvasElement {
    protected:
    protected:
        // ===== SHAPE-SPECIFIC PROPERTIES =====
        ShapeType shapeType;
        ShapeStyle style;
        bool antiAliased;
        float rotationAngle;
        Point2D rotationCenter;
        bool isDirty;        // Needs redraw
        bool isSelected;     // Selection state
        bool isDragging;     // Drag state

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasShape(const std::string& id, long uid, long x, long y, long w, long h, ShapeType type = ShapeType::Rectangle)
                : UltraCanvasElement(id, uid, x, y, w, h)
                , shapeType(type)
                , antiAliased(true)
                , rotationAngle(0.0f)
                , rotationCenter(Point2D(w/2.0f, h/2.0f))
                , isDirty(true)
                , isSelected(false)
                , isDragging(false) {
            // Properties are automatically managed by the base class
        }

        virtual ~UltraCanvasShape() = default;

        // ===== INHERITED PROPERTY ACCESSORS =====
        // Note: GetX(), SetX(), GetY(), SetY(), GetWidth(), SetHeight(), etc.
        // are automatically inherited from UltraCanvasElement base class

        // ===== SHAPE PROPERTIES =====
        ShapeType GetShapeType() const { return shapeType; }
        void SetShapeType(ShapeType type) {
            if (shapeType != type) {
                shapeType = type;
                MarkDirty();
            }
        }

        const ShapeStyle& GetShapeStyle() const { return style; }
        void SetShapeStyle(const ShapeStyle& newStyle) {
            style = newStyle;
            MarkDirty();
        }

        // ===== APPEARANCE PROPERTIES =====
        void SetFillColor(const Color& color) {
            style.fillColor = color;
            style.fillMode = FillMode::Solid;
            MarkDirty();
        }

        void SetStrokeColor(const Color& color) {
            style.strokeColor = color;
            style.hasStroke = true;
            MarkDirty();
        }

        void ctx->SetStrokeWidth(float width) {
            style.strokeWidth = std::max(0.0f, width);
            MarkDirty();
        }

        void SetFillMode(FillMode mode) {
            style.fillMode = mode;
            MarkDirty();
        }

        void SetLineJoin(LineJoin join) {
            style.lineJoin = join;
            MarkDirty();
        }

        void SetLineCap(LineCap cap) {
            style.lineCap = cap;
            MarkDirty();
        }

        // ===== GRADIENT SUPPORT =====
        void SetLinearGradient(const Point2D& start, const Point2D& end, const std::vector<GradientStop>& stops) {
            style.fillMode = FillMode::Gradient;
            style.gradientStart = start;
            style.gradientEnd = end;
            style.gradientStops = stops;
            style.gradientDirection = GradientDirection::Custom;
            MarkDirty();
        }

        void AddGradientStop(float position, const Color& color) {
            style.gradientStops.push_back(GradientStop(position, color));
            std::sort(style.gradientStops.begin(), style.gradientStops.end(),
                      [](const GradientStop& a, const GradientStop& b) {
                          return a.position < b.position;
                      });
            MarkDirty();
        }

        void ClearGradientStops() {
            style.gradientStops.clear();
            MarkDirty();
        }

        // ===== DASH PATTERN SUPPORT =====
        void SetDashPattern(const std::vector<float>& pattern) {
            style.dashPattern = pattern;
            MarkDirty();
        }

        void SetSolidStroke() {
            style.dashPattern.clear();
            MarkDirty();
        }

        // ===== SHADOW EFFECTS =====
        void SetShadow(const Color& color, const Point2D& offset, float blur) {
            style.hasShadow = true;
            style.shadowColor = color;
            style.shadowOffset = offset;
            style.shadowBlur = blur;
            MarkDirty();
        }

        void RemoveShadow() {
            style.hasShadow = false;
            MarkDirty();
        }

        // ===== TRANSFORMATION =====
        void SetRotation(float angle, const Point2D& center) {
            rotationAngle = angle;
            rotationCenter = center;
            MarkDirty();
        }

        void SetRotation(float angle) {
            SetRotation(angle, Point2D(GetWidth()/2.0f, GetHeight()/2.0f));
        }

        float GetRotation() const { return rotationAngle; }
        Point2D GetRotationCenter() const { return rotationCenter; }

        // ===== RENDERING CONTROL =====
        void SetAntiAliased(bool enabled) {
            antiAliased = enabled;
            MarkDirty();
        }

        bool IsAntiAliased() const { return antiAliased; }

        void MarkDirty() { isDirty = true; }
        void MarkClean() { isDirty = false; }
        bool IsDirty() const { return isDirty; }

        // ===== SELECTION AND INTERACTION =====
        bool IsShapeSelected() const { return isSelected; }
        void SetShapeSelected(bool selected) {
            isSelected = selected;
            MarkDirty();
        }

        bool IsShapeDragging() const { return isDragging; }
        void SetShapeDragging(bool dragging) { isDragging = dragging; }

        // ===== ABSTRACT RENDERING METHOD =====
        virtual void Render() override {
            if (!IsVisible()) return;

            ctx->PushState();

            // Apply transformation if needed
            if (rotationAngle != 0.0f) {
                PushRenderState();
                Translate(rotationCenter.x, rotationCenter.y);
                Rotate(rotationAngle);
                Translate(-rotationCenter.x, -rotationCenter.y);
            }

            // Draw shadow first if enabled
            if (style.hasShadow) {
                DrawShadow();
            }

            // Draw the actual shape
            DrawShape();

            // Draw selection indicators if selected
            if (isSelected) {
                DrawSelectionHandles();
            }

            // Restore transformation
            if (rotationAngle != 0.0f) {
                PopRenderState();
            }

            MarkClean();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;

            switch (event.type) {
                case UCEventType::MouseDown:
                    HandleMouseDown(event);
                    break;

                case UCEventType::MouseMove:
                    HandleMouseMove(event);
                    break;

                case UCEventType::MouseUp:
                    HandleMouseUp(event);
                    break;

                case UCEventType::MouseEnter:
                    SetHovered(true);
                    break;

                case UCEventType::MouseLeave:
                    SetHovered(false);
                    isDragging = false;
                    break;
            }
            return false;
        }

    protected:
        // ===== ABSTRACT DRAWING METHODS =====
        virtual void DrawShape() = 0;
        virtual void DrawShadow() {}
        virtual void DrawSelectionHandles() {}

        // ===== EVENT HANDLERS =====
        virtual void HandleMouseDown(const UCEvent& event) {
            if (Contains(event.x, event.y)) {
                SetFocus(true);
                isDragging = true;
                isSelected = true;
                MarkDirty();
            }
        }

        virtual void HandleMouseMove(const UCEvent& event) {
            if (isDragging) {
                // Basic drag behavior - can be overridden
                SetPosition(event.x - GetWidth()/2, event.y - GetHeight()/2);
                MarkDirty();
            }
        }

        virtual void HandleMouseUp(const UCEvent& event) {
            isDragging = false;
        }

        // ===== UTILITY METHODS =====
        Point2D GetCenter() const {
            return Point2D(GetX() + GetWidth()/2.0f, GetY() + GetHeight()/2.0f);
        }

        Rect2D GetShapeBounds() const {
            return Rect2D(GetX(), GetY(), GetWidth(), GetHeight());
        }
    };

// ===== CONCRETE SHAPE IMPLEMENTATIONS =====

    class UltraCanvasRectangle : public UltraCanvasShape {
    public:
        UltraCanvasRectangle(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasShape(id, uid, x, y, w, h, ShapeType::Rectangle) {}

    protected:
        void DrawShape() override {
            Rect2D bounds = GetShapeBounds();

            // Set fill style
            if (style.fillMode != FillMode::NoneFill) {
                UltraCanvas::DrawFilledRect(bounds, style.fillColor, Colors::Transparent);
            }

            // Set stroke style
            if (style.hasStroke && style.strokeWidth > 0) {
                ctx->SetStrokeColor(style.strokeColor);
                ctx->SetStrokeWidth(style.strokeWidth);
                ctx->DrawRectangle(bounds);
            }
        }
    };

    class UltraCanvasCircle : public UltraCanvasShape {
    public:
        UltraCanvasCircle(const std::string& id, long uid, long x, long y, long radius)
                : UltraCanvasShape(id, uid, x, y, radius*2, radius*2, ShapeType::Circle) {}

        float GetRadius() const {
            return std::min(GetWidth(), GetHeight()) / 2.0f;
        }

        void SetRadius(float radius) {
            SetSize(static_cast<long>(radius * 2), static_cast<long>(radius * 2));
            MarkDirty();
        }

    protected:
        void DrawShape() override {
            Point2D center = GetCenter();
            float radius = GetRadius();

            // Set fill style
            if (style.fillMode != FillMode::NoneFill) {
                ctx->SetFillColor(style.fillColor);
                ctx->DrawCircle(center, radius);
            }

            // Set stroke style
            if (style.hasStroke && style.strokeWidth > 0) {
                ctx->SetStrokeColor(style.strokeColor);
                ctx->SetStrokeWidth(style.strokeWidth);
                ctx->DrawCircle(center, radius);
            }
        }
    };

    class UltraCanvasEllipse : public UltraCanvasShape {
    public:
        UltraCanvasEllipse(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasShape(id, uid, x, y, w, h, ShapeType::Ellipse) {}

        float GetRadiusX() const { return GetWidth() / 2.0f; }
        float GetRadiusY() const { return GetHeight() / 2.0f; }

    protected:
        void DrawShape() override {
            Point2D center = GetCenter();
            float radiusX = GetRadiusX();
            float radiusY = GetRadiusY();

            // UltraCanvas doesn't have DrawEllipse, so we'll use multiple arcs or approximate with lines
            // For simplicity, draw as circle with average radius
            float avgRadius = (radiusX + radiusY) / 2.0f;

            // Set fill style
            if (style.fillMode != FillMode::NoneFill) {
                SetFillColor(style.fillColor);
                ctx->DrawCircle(center, avgRadius);
            }

            // Set stroke style
            if (style.hasStroke && style.strokeWidth > 0) {
                SetStrokeColor(style.strokeColor);
                ctx->SetStrokeWidth(style.strokeWidth);
                ctx->DrawCircle(center, avgRadius);
            }
        }
    };

    class UltraCanvasLine : public UltraCanvasShape {
    private:
        Point2D startPoint, endPoint;

    public:
        UltraCanvasLine(const std::string& id, long uid, const Point2D& start, const Point2D& end)
                : UltraCanvasShape(id, uid,
                                   static_cast<long>(std::min(start.x, end.x)),
                                   static_cast<long>(std::min(start.y, end.y)),
                                   static_cast<long>(std::abs(end.x - start.x)),
                                   static_cast<long>(std::abs(end.y - start.y)),
                                   ShapeType::Line)
                , startPoint(start), endPoint(end) {}

        void SetStartPoint(const Point2D& start) {
            startPoint = start;
            UpdateBoundsFromPoints();
            MarkDirty();
        }

        void SetEndPoint(const Point2D& end) {
            endPoint = end;
            UpdateBoundsFromPoints();
            MarkDirty();
        }

        Point2D GetStartPoint() const { return startPoint; }
        Point2D GetEndPoint() const { return endPoint; }

        float GetLength() const {
            return startPoint.Distance(endPoint);
        }

    protected:
        void DrawShape() override {
            // Set stroke style
            if (style.hasStroke && style.strokeWidth > 0) {
                SetStrokeColor(style.strokeColor);
                ctx->SetStrokeWidth(style.strokeWidth);
                ctx->DrawLine(startPoint, endPoint);
            }
        }

    private:
        void UpdateBoundsFromPoints() {
            long minX = static_cast<long>(std::min(startPoint.x, endPoint.x));
            long minY = static_cast<long>(std::min(startPoint.y, endPoint.y));
            long maxX = static_cast<long>(std::max(startPoint.x, endPoint.x));
            long maxY = static_cast<long>(std::max(startPoint.y, endPoint.y));

            SetBounds(minX, minY, maxX - minX, maxY - minY);
        }
    };

    class UltraCanvasPolygon : public UltraCanvasShape {
    private:
        std::vector<Point2D> points;

    public:
        UltraCanvasPolygon(const std::string& id, long uid, const std::vector<Point2D>& polygonPoints)
                : UltraCanvasShape(id, uid, 0, 0, 100, 100, ShapeType::Polygon) {
            SetPoints(polygonPoints);
        }

        void SetPoints(const std::vector<Point2D>& polygonPoints) {
            points = polygonPoints;
            if (!points.empty()) {
                UpdateBoundsFromPoints();
            }
            MarkDirty();
        }

        const std::vector<Point2D>& GetPoints() const { return points; }

        void AddPoint(const Point2D& point) {
            points.push_back(point);
            UpdateBoundsFromPoints();
            MarkDirty();
        }

        void RemovePoint(size_t index) {
            if (index < points.size()) {
                points.erase(points.begin() + index);
                UpdateBoundsFromPoints();
                MarkDirty();
            }
        }

    protected:
        void DrawShape() override {
            if (points.size() < 3) return;

            // UltraCanvas DrawPolygon expects Point2D* and count, not std::vector
            // For fill, we'll use multiple lines to approximate filled polygon
            if (style.fillMode != FillMode::NoneFill && points.size() >= 3) {
                // Simple polygon fill approximation - draw lines from center to edges
                Point2D center = GetCenter();
                SetFillColor(style.fillColor);

                for (size_t i = 0; i < points.size(); ++i) {
                    size_t next = (i + 1) % points.size();
                    ctx->DrawLine(center, points[i]);
                    ctx->DrawLine(points[i], points[next]);
                }
            }

            // Set stroke style - draw polygon outline
            if (style.hasStroke && style.strokeWidth > 0) {
                SetStrokeColor(style.strokeColor);
                ctx->SetStrokeWidth(style.strokeWidth);

                // Draw polygon outline
                for (size_t i = 0; i < points.size(); ++i) {
                    size_t next = (i + 1) % points.size();
                    ctx->DrawLine(points[i], points[next]);
                }
            }
        }

    private:
        void UpdateBoundsFromPoints() {
            if (points.empty()) return;

            float minX = points[0].x, maxX = points[0].x;
            float minY = points[0].y, maxY = points[0].y;

            for (const auto& point : points) {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }

            SetBounds(static_cast<long>(minX), static_cast<long>(minY),
                      static_cast<long>(maxX - minX), static_cast<long>(maxY - minY));
        }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasRectangle> CreateRectangleShape(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasRectangle>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasCircle> CreateCircleShape(
            const std::string& id, long uid, long x, long y, long radius) {
        return std::make_shared<UltraCanvasCircle>(id, uid, x, y, radius);
    }

    inline std::shared_ptr<UltraCanvasEllipse> CreateEllipseShape(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasEllipse>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasLine> CreateLineShape(
            const std::string& id, long uid, const Point2D& start, const Point2D& end) {
        return std::make_shared<UltraCanvasLine>(id, uid, start, end);
    }

    inline std::shared_ptr<UltraCanvasPolygon> CreatePolygonShape(
            const std::string& id, long uid, const std::vector<Point2D>& points) {
        return std::make_shared<UltraCanvasPolygon>(id, uid, points);
    }

} // namespace UltraCanvas