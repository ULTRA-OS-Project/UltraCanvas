// UltraCanvasVectorStorage.cpp
// Implementation of the Vector Graphics Storage System for UltraCanvas
// Version: 1.1.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat / ParseFloatClassic - dot-decimal, non-throwing
#include "DataFormats/UltraCanvasVectorPathOps.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <locale>
#include <cstring>
#include <regex>
#include <numeric>

namespace UltraCanvas {
namespace VectorStorage {

// ===== MATRIX3X3 IMPLEMENTATION =====

Matrix3x3::Matrix3x3() {
    // Initialize as identity matrix
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }
}

Matrix3x3 Matrix3x3::Identity() {
    return Matrix3x3();
}

Matrix3x3 Matrix3x3::Translate(double tx, double ty) {
    Matrix3x3 result;
    result.m[0][2] = tx;
    result.m[1][2] = ty;
    return result;
}

Matrix3x3 Matrix3x3::Scale(double sx, double sy) {
    Matrix3x3 result;
    result.m[0][0] = sx;
    result.m[1][1] = sy;
    return result;
}

Matrix3x3 Matrix3x3::Rotate(double angle) {
    Matrix3x3 result;
    double c = std::cos(angle);
    double s = std::sin(angle);
    result.m[0][0] = c;
    result.m[0][1] = -s;
    result.m[1][0] = s;
    result.m[1][1] = c;
    return result;
}

Matrix3x3 Matrix3x3::RotateDegrees(double degrees) {
    return Rotate(degrees * M_PI / 180.0);
}

Matrix3x3 Matrix3x3::SkewX(double angle) {
    Matrix3x3 result;
    result.m[0][1] = std::tan(angle);
    return result;
}

Matrix3x3 Matrix3x3::SkewY(double angle) {
    Matrix3x3 result;
    result.m[1][0] = std::tan(angle);
    return result;
}

Matrix3x3 Matrix3x3::FromValues(double a, double b, double c, double d, double e, double f) {
    Matrix3x3 result;
    result.m[0][0] = a;
    result.m[0][1] = b;
    result.m[0][2] = e;
    result.m[1][0] = c;
    result.m[1][1] = d;
    result.m[1][2] = f;
    result.m[2][0] = 0;
    result.m[2][1] = 0;
    result.m[2][2] = 1;
    return result;
}

Matrix3x3 Matrix3x3::operator*(const Matrix3x3& other) const {
    Matrix3x3 result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.m[i][j] = 0;
            for (int k = 0; k < 3; k++) {
                result.m[i][j] += m[i][k] * other.m[k][j];
            }
        }
    }
    return result;
}

Point2Dd Matrix3x3::Transform(const Point2Dd& point) const {
    Point2Dd result;
    result.x = m[0][0] * point.x + m[0][1] * point.y + m[0][2];
    result.y = m[1][0] * point.x + m[1][1] * point.y + m[1][2];
    return result;
}

Rect2Dd Matrix3x3::Transform(const Rect2Dd& rect) const {
    Point2Dd corners[4] = {
        {rect.x, rect.y},
        {rect.x + rect.width, rect.y},
        {rect.x, rect.y + rect.height},
        {rect.x + rect.width, rect.y + rect.height}
    };
    
    for (int i = 0; i < 4; i++) {
        corners[i] = Transform(corners[i]);
    }
    
    double minX = corners[0].x, maxX = corners[0].x;
    double minY = corners[0].y, maxY = corners[0].y;
    
    for (int i = 1; i < 4; i++) {
        minX = std::min(minX, corners[i].x);
        maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxY = std::max(maxY, corners[i].y);
    }
    
    return Rect2Dd{minX, minY, maxX - minX, maxY - minY};
}

double Matrix3x3::Determinant() const {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

bool Matrix3x3::IsIdentity(double epsilon) const {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (std::abs(m[i][j] - (i == j ? 1.0 : 0.0)) > epsilon) return false;
        }
    }
    return true;
}

Matrix3x3 Matrix3x3::Inverse() const {
    double det = Determinant();
    if (std::abs(det) < 1e-300) {
        return Identity(); // Return identity if not invertible
    }
    
    Matrix3x3 result;
    double invDet = 1.0 / det;
    
    result.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet;
    result.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invDet;
    result.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;
    
    result.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invDet;
    result.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
    result.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * invDet;
    
    result.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet;
    result.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * invDet;
    result.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;
    
    return result;
}

// ===== VECTOR STYLE IMPLEMENTATION =====

// ===== STROKE / TRANSPARENCY RAMPS =====

float StrokeData::WidthAt(float t) const {
    if (WidthProfile.size() < 2) return Width;
    if (t <= WidthProfile.front().T) return Width * WidthProfile.front().Factor;
    if (t >= WidthProfile.back().T) return Width * WidthProfile.back().Factor;
    for (size_t i = 1; i < WidthProfile.size(); ++i) {
        const WidthSample& a = WidthProfile[i - 1];
        const WidthSample& b = WidthProfile[i];
        if (t <= b.T) {
            const float span = b.T - a.T;
            const float u = span > 1e-6f ? (t - a.T) / span : 1.0f;
            return Width * (a.Factor + (b.Factor - a.Factor) * u);
        }
    }
    return Width * WidthProfile.back().Factor;
}

float TransparencyData::LevelAt(double t) const {
    if (!IsGradient()) return Level;
    if (t <= Stops.front().Position) return Stops.front().Level;
    if (t >= Stops.back().Position) return Stops.back().Level;
    for (size_t i = 1; i < Stops.size(); ++i) {
        const TransparencyStop& a = Stops[i - 1];
        const TransparencyStop& b = Stops[i];
        if (t <= b.Position) {
            const double span = b.Position - a.Position;
            const double u = span > 1e-9 ? (t - a.Position) / span : 1.0;
            return static_cast<float>(a.Level + (b.Level - a.Level) * u);
        }
    }
    return Stops.back().Level;
}

void VectorStyle::Inherit(const VectorStyle& parent) {
    // Inherit properties that weren't explicitly set
    if (!Fill.has_value() && parent.Fill.has_value()) {
        Fill = parent.Fill;
    }
    if (!Stroke.has_value() && parent.Stroke.has_value()) {
        Stroke = parent.Stroke;
    }
    
    if (!Transparency.has_value() && parent.Transparency.has_value()) {
        Transparency = parent.Transparency;
    }

    // Multiply opacity values
    Opacity *= parent.Opacity;
    FillOpacity *= parent.FillOpacity;
    StrokeOpacity *= parent.StrokeOpacity;
    
    // Inherit other properties if not set
    if (ClipPath == std::nullopt && parent.ClipPath != std::nullopt) {
        ClipPath = parent.ClipPath;
    }
    if (Mask == std::nullopt && parent.Mask != std::nullopt) {
        Mask = parent.Mask;
    }
    
    // Inherit visibility
    Visible = Visible && parent.Visible;
    Display = Display && parent.Display;
}

// ===== VECTOR ELEMENT BASE CLASS IMPLEMENTATION =====

Matrix3x3 VectorElement::GetGlobalTransform() const {
    Matrix3x3 global = Transform.value_or(Matrix3x3::Identity());
    
    if (auto parentPtr = Parent.lock()) {
        global = parentPtr->GetGlobalTransform() * global;
    }
    
    return global;
}

Point2Dd VectorElement::LocalToGlobal(const Point2Dd& point) const {
    return GetGlobalTransform().Transform(point);
}

Point2Dd VectorElement::GlobalToLocal(const Point2Dd& point) const {
    return GetGlobalTransform().Inverse().Transform(point);
}

// ===== BASIC SHAPE IMPLEMENTATIONS =====

// VectorRect
Rect2Dd VectorRect::GetBoundingBox() const {
    Rect2Dd bbox = Bounds;
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorRect::Clone() const {
    auto clone = std::make_shared<VectorRect>(*this);
    clone->Parent.reset();
    return clone;
}

// VectorCircle
Rect2Dd VectorCircle::GetBoundingBox() const {
    Rect2Dd bbox{
        Center.x - Radius,
        Center.y - Radius,
        Radius * 2,
        Radius * 2
    };
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorCircle::Clone() const {
    auto clone = std::make_shared<VectorCircle>(*this);
    clone->Parent.reset();
    return clone;
}

// VectorEllipse
Rect2Dd VectorEllipse::GetBoundingBox() const {
    Rect2Dd bbox{
        Center.x - RadiusX,
        Center.y - RadiusY,
        RadiusX * 2,
        RadiusY * 2
    };
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorEllipse::Clone() const {
    auto clone = std::make_shared<VectorEllipse>(*this);
    clone->Parent.reset();
    return clone;
}

// VectorLine
Rect2Dd VectorLine::GetBoundingBox() const {
    float minX = std::min(Start.x, End.x);
    float minY = std::min(Start.y, End.y);
    float maxX = std::max(Start.x, End.x);
    float maxY = std::max(Start.y, End.y);
    
    Rect2Dd bbox{minX, minY, maxX - minX, maxY - minY};
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorLine::Clone() const {
    auto clone = std::make_shared<VectorLine>(*this);
    clone->Parent.reset();
    return clone;
}

// VectorPolyline
Rect2Dd VectorPolyline::GetBoundingBox() const {
    if (Points.empty()) {
        return Rect2Dd{0, 0, 0, 0};
    }
    
    double minX = Points[0].x, maxX = Points[0].x;
    double minY = Points[0].y, maxY = Points[0].y;
    
    for (const auto& point : Points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    
    Rect2Dd bbox{minX, minY, maxX - minX, maxY - minY};
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorPolyline::Clone() const {
    auto clone = std::make_shared<VectorPolyline>(*this);
    clone->Parent.reset();
    return clone;
}

// VectorPolygon
Rect2Dd VectorPolygon::GetBoundingBox() const {
    if (Points.empty()) {
        return Rect2Dd{0, 0, 0, 0};
    }
    
    double minX = Points[0].x, maxX = Points[0].x;
    double minY = Points[0].y, maxY = Points[0].y;
    
    for (const auto& point : Points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    
    Rect2Dd bbox{minX, minY, maxX - minX, maxY - minY};
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorPolygon::Clone() const {
    auto clone = std::make_shared<VectorPolygon>(*this);
    clone->Parent.reset();
    return clone;
}

// ===== VECTOR PATH IMPLEMENTATION =====

Rect2Dd VectorPath::GetBoundingBox() const {
    if (!Path.cachedBounds) {
        if (Path.commands.empty()) return {0, 0, 0, 0};
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        Point2Dd cur{0, 0};
        for (const auto &c: Path.commands) {
            if ((c.Type == PathCommandType::MoveTo || c.Type == PathCommandType::LineTo) &&
                c.Parameters.size() >= 2) {
                float x = c.Relative ? cur.x + c.Parameters[0] : c.Parameters[0];
                float y = c.Relative ? cur.y + c.Parameters[1] : c.Parameters[1];
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                cur = {x, y};
            } else if (c.Type == PathCommandType::CurveTo && c.Parameters.size() >= 6) {
                for (int i = 0; i < 6; i += 2) {
                    float x = c.Parameters[i], y = c.Parameters[i + 1];
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
                cur = {c.Parameters[4], c.Parameters[5]};
            }
        }
        if (minX > maxX) return {0, 0, 0, 0};
        Path.cachedBounds = Rect2Dd{minX, minY, maxX - minX, maxY - minY};
    }

    Rect2Dd bbox = *Path.cachedBounds;
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorPath::Clone() const {
    auto clone = std::make_shared<VectorPath>(*this);
    clone->Parent.reset();
    return clone;
}

void VectorPath::AddCommand(const PathCommand& cmd) {
    Path.commands.push_back(cmd);
    Path.cachedBounds.reset();  // Invalidate cached bbox
    Path.length.reset();
    Path.flattenedPoints.reset();
}

void VectorPath::MoveTo(float x, float y, bool relative) {
    PathCommand cmd;
    cmd.Type = PathCommandType::MoveTo;
    cmd.Parameters = {x, y};
    cmd.Relative = relative;
    AddCommand(cmd);
}

void VectorPath::LineTo(float x, float y, bool relative) {
    PathCommand cmd;
    cmd.Type = PathCommandType::LineTo;
    cmd.Parameters = {x, y};
    cmd.Relative = relative;
    AddCommand(cmd);
}

void VectorPath::CurveTo(float x1, float y1, float x2, float y2, float x, float y, bool relative) {
    PathCommand cmd;
    cmd.Type = PathCommandType::CurveTo;
    cmd.Parameters = {x1, y1, x2, y2, x, y};
    cmd.Relative = relative;
    AddCommand(cmd);
}

void VectorPath::QuadraticTo(float x1, float y1, float x, float y, bool relative) {
    PathCommand cmd;
    cmd.Type = PathCommandType::QuadraticTo;
    cmd.Parameters = {x1, y1, x, y};
    cmd.Relative = relative;
    AddCommand(cmd);
}

void VectorPath::ArcTo(float rx, float ry, float rotation, bool largeArc, bool sweep, float x, float y, bool relative) {
    PathCommand cmd;
    cmd.Type = PathCommandType::ArcTo;
    cmd.Parameters = {rx, ry, rotation, 
                     static_cast<float>(largeArc), 
                     static_cast<float>(sweep), x, y};
    cmd.Relative = relative;
    AddCommand(cmd);
}

void VectorPath::ClosePath() {
    PathCommand cmd;
    cmd.Type = PathCommandType::ClosePath;
    AddCommand(cmd);
    Path.Closed = true;
}

float VectorPath::GetLength() const {
    if (!Path.length.has_value()) {
        auto points = Flatten(0);
        float length = 0;
        for (size_t i = 1; i < points.size(); i++) {
            float dx = points[i].x - points[i-1].x;
            float dy = points[i].y - points[i-1].y;
            length += std::sqrt(dx * dx + dy * dy);
        }
        Path.length = length;
    }
    return Path.length.value();
}

Point2Dd VectorPath::GetPointAtLength(float length) const {
    auto points = Flatten(0.25f);
    if (points.empty()) return Point2Dd{0, 0};
    
    float currentLength = 0;
    for (size_t i = 1; i < points.size(); i++) {
        float dx = points[i].x - points[i-1].x;
        float dy = points[i].y - points[i-1].y;
        float segmentLength = std::sqrt(dx * dx + dy * dy);
        
        if (currentLength + segmentLength >= length) {
            float t = (length - currentLength) / segmentLength;
            return Point2Dd(points[i-1].x + t * dx,
                            points[i-1].y + t * dy);
        }
        currentLength += segmentLength;
    }
    
    return points.back();
}

float VectorPath::GetAngleAtLength(float length) const {
    auto points = Flatten(0.25f);
    if (points.size() < 2) return 0;
    
    float currentLength = 0;
    for (size_t i = 1; i < points.size(); i++) {
        float dx = points[i].x - points[i-1].x;
        float dy = points[i].y - points[i-1].y;
        float segmentLength = std::sqrt(dx * dx + dy * dy);
        
        if (currentLength + segmentLength >= length) {
            return std::atan2(dy, dx);
        }
        currentLength += segmentLength;
    }
    
    // Return angle of last segment
    float dx = points.back().x - points[points.size()-2].x;
    float dy = points.back().y - points[points.size()-2].y;
    return std::atan2(dy, dx);
}

std::vector<Point2Dd> VectorPath::Flatten(float tolerance) const {
    if (!Path.flattenedPoints.has_value()) {
        std::vector<Point2Dd> result;
        Point2Dd currentPoint{0, 0};
        Point2Dd startPoint{0, 0};
        
        for (const auto& cmd : Path.commands) {
            switch (cmd.Type) {
                case PathCommandType::MoveTo: {
                    if (cmd.Relative && !result.empty()) {
                        currentPoint.x += cmd.Parameters[0];
                        currentPoint.y += cmd.Parameters[1];
                    } else {
                        currentPoint.x = cmd.Parameters[0];
                        currentPoint.y = cmd.Parameters[1];
                    }
                    startPoint = currentPoint;
                    result.push_back(currentPoint);
                    break;
                }
                
                case PathCommandType::LineTo: {
                    if (cmd.Relative) {
                        currentPoint.x += cmd.Parameters[0];
                        currentPoint.y += cmd.Parameters[1];
                    } else {
                        currentPoint.x = cmd.Parameters[0];
                        currentPoint.y = cmd.Parameters[1];
                    }
                    result.push_back(currentPoint);
                    break;
                }
                
                case PathCommandType::CurveTo: {
                    // Flatten cubic bezier curve
                    Point2Dd p0 = currentPoint;
                    Point2Dd p1, p2, p3;
                    
                    if (cmd.Relative) {
                        p1 = {currentPoint.x + cmd.Parameters[0], 
                              currentPoint.y + cmd.Parameters[1]};
                        p2 = {currentPoint.x + cmd.Parameters[2], 
                              currentPoint.y + cmd.Parameters[3]};
                        p3 = {currentPoint.x + cmd.Parameters[4], 
                              currentPoint.y + cmd.Parameters[5]};
                    } else {
                        p1 = {cmd.Parameters[0], cmd.Parameters[1]};
                        p2 = {cmd.Parameters[2], cmd.Parameters[3]};
                        p3 = {cmd.Parameters[4], cmd.Parameters[5]};
                    }
                    
                    // Adaptive subdivision based on tolerance
                    int steps = std::max(2, static_cast<int>(
                        std::sqrt(std::pow(p3.x - p0.x, 2) + 
                                 std::pow(p3.y - p0.y, 2)) / tolerance));
                    
                    for (int i = 1; i <= steps; i++) {
                        float t = static_cast<float>(i) / steps;
                        float t2 = t * t;
                        float t3 = t2 * t;
                        float mt = 1 - t;
                        float mt2 = mt * mt;
                        float mt3 = mt2 * mt;
                        
                        Point2Dd point;
                        point.x = mt3 * p0.x + 3 * mt2 * t * p1.x + 
                                 3 * mt * t2 * p2.x + t3 * p3.x;
                        point.y = mt3 * p0.y + 3 * mt2 * t * p1.y + 
                                 3 * mt * t2 * p2.y + t3 * p3.y;
                        result.push_back(point);
                    }
                    
                    currentPoint = p3;
                    break;
                }
                
                case PathCommandType::QuadraticTo: {
                    // Flatten quadratic bezier curve
                    Point2Dd p0 = currentPoint;
                    Point2Dd p1, p2;
                    
                    if (cmd.Relative) {
                        p1 = {currentPoint.x + cmd.Parameters[0], 
                              currentPoint.y + cmd.Parameters[1]};
                        p2 = {currentPoint.x + cmd.Parameters[2], 
                              currentPoint.y + cmd.Parameters[3]};
                    } else {
                        p1 = {cmd.Parameters[0], cmd.Parameters[1]};
                        p2 = {cmd.Parameters[2], cmd.Parameters[3]};
                    }
                    
                    int steps = std::max(2, static_cast<int>(
                        std::sqrt(std::pow(p2.x - p0.x, 2) + 
                                 std::pow(p2.y - p0.y, 2)) / tolerance));
                    
                    for (int i = 1; i <= steps; i++) {
                        float t = static_cast<float>(i) / steps;
                        float mt = 1 - t;
                        
                        Point2Dd point;
                        point.x = mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x;
                        point.y = mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y;
                        result.push_back(point);
                    }
                    
                    currentPoint = p2;
                    break;
                }
                
                case PathCommandType::ArcTo: {
                    // Convert arc to bezier curves and flatten
                    // Simplified implementation - full SVG arc implementation is complex
                    float rx = cmd.Parameters[0];
                    float ry = cmd.Parameters[1];
                    float rotation = cmd.Parameters[2] * M_PI / 180.0f;
                    bool largeArc = cmd.Parameters[3] > 0.5f;
                    bool sweep = cmd.Parameters[4] > 0.5f;
                    
                    Point2Dd endPoint;
                    if (cmd.Relative) {
                        endPoint = {currentPoint.x + cmd.Parameters[5],
                                   currentPoint.y + cmd.Parameters[6]};
                    } else {
                        endPoint = {cmd.Parameters[5], cmd.Parameters[6]};
                    }
                    
                    // For now, approximate with a line
                    // TODO: Implement full arc to bezier conversion
                    result.push_back(endPoint);
                    currentPoint = endPoint;
                    break;
                }
                
                case PathCommandType::ClosePath: {
                    if (currentPoint.x != startPoint.x || 
                        currentPoint.y != startPoint.y) {
                        result.push_back(startPoint);
                        currentPoint = startPoint;
                    }
                    break;
                }
                
                default:
                    break;
            }
        }
        
        Path.flattenedPoints = result;
    }
    
    return Path.flattenedPoints.value();
}

// ===== TEXT ELEMENTS IMPLEMENTATION =====

// VectorText
Rect2Dd VectorText::GetBoundingBox() const {
    // Simplified bounding box calculation
    // In real implementation, this would use font metrics
    float width = 0;
    float height = BaseStyle.FontSize * 1.2f;
    
    for (const auto& span : Spans) {
        // Approximate width based on character count
        width += span.Text.length() * BaseStyle.FontSize * 0.6f;
    }
    
    Rect2Dd bbox{Position.x, Position.y - BaseStyle.FontSize, width, height};
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorText::Clone() const {
    auto clone = std::make_shared<VectorText>(*this);
    clone->Parent.reset();
    return clone;
}

void VectorText::SetText(const std::string& text) {
    Spans.clear();
    TextSpanData span;
    span.Text = text;
    span.Style = BaseStyle;
    Spans.push_back(span);
}

void VectorText::AddSpan(const TextSpanData& span) {
    Spans.push_back(span);
}

std::string VectorText::GetPlainText() const {
    std::string result;
    for (const auto& span : Spans) {
        result += span.Text;
    }
    return result;
}

// VectorTextPath
Rect2Dd VectorTextPath::GetBoundingBox() const {
    // Would need the actual path to calculate proper bounds
    // For now, return empty bounds
    return Rect2Dd{0, 0, 0, 0};
}

std::shared_ptr<VectorElement> VectorTextPath::Clone() const {
    auto clone = std::make_shared<VectorTextPath>(*this);
    clone->Parent.reset();
    return clone;
}


// ===== CONTAINER ELEMENTS IMPLEMENTATION =====

// An element that has nothing to measure (an empty group, a clip path,
// a mask, an unresolved use) reports the all-zero rectangle; a union must
// skip it, or every such element drags the bounds to the origin.
static bool IsEmptyBounds(const Rect2Dd& r) {
    return r.width <= 0 && r.height <= 0 && r.x == 0 && r.y == 0;
}

static Rect2Dd UnionBounds(const Rect2Dd& a, const Rect2Dd& b) {
    if (IsEmptyBounds(a)) return b;
    if (IsEmptyBounds(b)) return a;
    double minX = std::min(a.x, b.x);
    double minY = std::min(a.y, b.y);
    double maxX = std::max(a.x + a.width, b.x + b.width);
    double maxY = std::max(a.y + a.height, b.y + b.height);
    return Rect2Dd{minX, minY, maxX - minX, maxY - minY};
}

// VectorGroup
Rect2Dd VectorGroup::GetBoundingBox() const {
    Rect2Dd bbox{0, 0, 0, 0};
    for (const auto& child : Children) {
        if (child) bbox = UnionBounds(bbox, child->GetBoundingBox());
    }
    if (Transform.has_value() && !IsEmptyBounds(bbox)) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorGroup::Clone() const {
    auto clone = std::make_shared<VectorGroup>(*this);
    clone->Parent.reset();
    
    // Deep clone children, re-parented to the clone (a child cloned by
    // copy would otherwise keep pointing at the original group, and
    // GetGlobalTransform on the copy would walk the wrong tree).
    clone->Children.clear();
    for (const auto& child : Children) {
        if (!child) continue;
        auto childClone = child->Clone();
        childClone->Parent = clone;
        clone->Children.push_back(childClone);
    }
    
    return clone;
}


void VectorGroup::AddChild(std::shared_ptr<VectorElement> child) {
    if (child) {
        Children.push_back(child);
        child->Parent = shared_from_this();
    }
}

void VectorGroup::RemoveChild(const std::string& id) {
    Children.erase(
        std::remove_if(Children.begin(), Children.end(),
            [&id](const std::shared_ptr<VectorElement>& child) {
                return child->Id == id;
            }),
        Children.end()
    );
}

std::shared_ptr<VectorElement> VectorGroup::FindChild(const std::string& id) const {
    for (const auto& child : Children) {
        if (child->Id == id) {
            return child;
        }
        
        // Recursive search in groups
        if (auto group = std::dynamic_pointer_cast<VectorGroup>(child)) {
            if (auto found = group->FindChild(id)) {
                return found;
            }
        }
    }
    return nullptr;
}

void VectorGroup::ClearChildren() {
    Children.clear();
}

// VectorSymbol
std::shared_ptr<VectorElement> VectorSymbol::Clone() const {
    auto clone = std::make_shared<VectorSymbol>(*this);
    clone->Parent.reset();
    
    // Deep clone children, re-parented to the clone (see VectorGroup::Clone).
    clone->Children.clear();
    for (const auto& child : Children) {
        if (!child) continue;
        auto childClone = child->Clone();
        childClone->Parent = clone;
        clone->Children.push_back(childClone);
    }
    
    return clone;
}


// VectorClipView

namespace {
    template <class G>
    std::shared_ptr<G> CloneGroupAs(const G& src) {
        auto clone = std::make_shared<G>(src);
        clone->Parent.reset();
        clone->Children.clear();
        for (const auto& child : src.Children) {
            if (!child) continue;
            auto childClone = child->Clone();
            childClone->Parent = clone;
            clone->Children.push_back(childClone);
        }
        return clone;
    }
    Rect2Dd PathDataBounds(const PathData& pd) {
        Rect2Dd b{0, 0, 0, 0};
        bool any = false;
        for (const FlatSubpath& sub : FlattenPathData(pd))
            for (const auto& p : sub.Points) {
                if (!any) { b = Rect2Dd(p.x, p.y, 0, 0); any = true; continue; }
                const double x0 = std::min(b.x, p.x), y0 = std::min(b.y, p.y);
                const double x1 = std::max(b.x + b.width, p.x), y1 = std::max(b.y + b.height, p.y);
                b = Rect2Dd(x0, y0, x1 - x0, y1 - y0);
            }
        return b;
    }
}

Rect2Dd VectorClipView::GetBoundingBox() const {
    Rect2Dd bbox{0, 0, 0, 0};
    for (const auto& k : KeyholeShapes()) if (k) bbox = UnionBounds(bbox, k->GetBoundingBox());
    if (Transform.has_value() && !IsEmptyBounds(bbox)) bbox = Transform->Transform(bbox);
    return bbox;
}

std::shared_ptr<VectorElement> VectorClipView::Clone() const { return CloneGroupAs(*this); }

std::vector<std::shared_ptr<VectorElement>> VectorClipView::KeyholeShapes() const {
    std::vector<std::shared_ptr<VectorElement>> out;
    const int n = std::max(0, std::min(Keyholes, static_cast<int>(Children.size())));
    out.assign(Children.begin(), Children.begin() + n);
    return out;
}

std::vector<std::shared_ptr<VectorElement>> VectorClipView::Contents() const {
    std::vector<std::shared_ptr<VectorElement>> out;
    const int n = std::max(0, std::min(Keyholes, static_cast<int>(Children.size())));
    out.assign(Children.begin() + n, Children.end());
    return out;
}

// VectorBlend

std::shared_ptr<VectorElement> VectorBlend::Clone() const { return CloneGroupAs(*this); }

// VectorMould

Rect2Dd VectorMould::GetBoundingBox() const {
    Rect2Dd bbox = PathDataBounds(Shape);
    if (IsEmptyBounds(bbox)) bbox = VectorGroup::GetBoundingBox();
    else if (Transform.has_value()) bbox = Transform->Transform(bbox);
    return bbox;
}

std::shared_ptr<VectorElement> VectorMould::Clone() const { return CloneGroupAs(*this); }

Rect2Dd VectorMould::EffectiveSourceBounds() const {
    if (SourceBounds.width > 0 && SourceBounds.height > 0) return SourceBounds;
    Rect2Dd bbox{0, 0, 0, 0};
    for (const auto& child : Children) if (child) bbox = UnionBounds(bbox, child->GetBoundingBox());
    return bbox;
}

PathData VectorMould::IdentityShape(MouldKind kind, const Rect2Dd& b) {
    const Point2Dd c[4] = {Point2Dd(b.x, b.y), Point2Dd(b.x + b.width, b.y),
                           Point2Dd(b.x + b.width, b.y + b.height), Point2Dd(b.x, b.y + b.height)};
    PathData pd;
    PathCommand m; m.Type = PathCommandType::MoveTo; m.Parameters = {static_cast<float>(c[0].x), static_cast<float>(c[0].y)};
    pd.commands.push_back(m);
    for (int i = 0; i < 4; ++i) {
        const Point2Dd& a = c[i];
        const Point2Dd& z = c[(i + 1) % 4];
        PathCommand cmd;
        if (kind == MouldKind::Envelope) {
            cmd.Type = PathCommandType::CurveTo;
            cmd.Parameters = {static_cast<float>(a.x + (z.x - a.x) / 3), static_cast<float>(a.y + (z.y - a.y) / 3),
                              static_cast<float>(a.x + 2 * (z.x - a.x) / 3), static_cast<float>(a.y + 2 * (z.y - a.y) / 3),
                              static_cast<float>(z.x), static_cast<float>(z.y)};
        } else {
            cmd.Type = PathCommandType::LineTo;
            cmd.Parameters = {static_cast<float>(z.x), static_cast<float>(z.y)};
        }
        pd.commands.push_back(cmd);
    }
    PathCommand close; close.Type = PathCommandType::ClosePath;
    pd.commands.push_back(close);
    pd.Closed = true;
    return pd;
}

namespace {
    // The shape's four sides as cubics (a line side has its controls on
    // the line), in order: top, right, bottom, left.
    struct MouldSide { Point2Dd p0, p1, p2, p3; };
    bool MouldSides(const PathData& shape, MouldSide sides[4]) {
        int n = 0;
        Point2Dd cur(0, 0), start(0, 0);
        for (const auto& c : shape.commands) {
            const auto& p = c.Parameters;
            switch (c.Type) {
                case PathCommandType::MoveTo:
                    if (p.size() >= 2) { cur = start = Point2Dd(p[0], p[1]); }
                    break;
                case PathCommandType::LineTo:
                    if (p.size() >= 2 && n < 4) {
                        const Point2Dd z(p[0], p[1]);
                        sides[n++] = {cur, Point2Dd(cur.x + (z.x - cur.x) / 3, cur.y + (z.y - cur.y) / 3),
                                      Point2Dd(cur.x + 2 * (z.x - cur.x) / 3, cur.y + 2 * (z.y - cur.y) / 3), z};
                        cur = z;
                    }
                    break;
                case PathCommandType::CurveTo:
                    if (p.size() >= 6 && n < 4) {
                        sides[n++] = {cur, Point2Dd(p[0], p[1]), Point2Dd(p[2], p[3]), Point2Dd(p[4], p[5])};
                        cur = Point2Dd(p[4], p[5]);
                    }
                    break;
                case PathCommandType::ClosePath:
                    if (n == 3) {
                        sides[n++] = {cur, Point2Dd(cur.x + (start.x - cur.x) / 3, cur.y + (start.y - cur.y) / 3),
                                      Point2Dd(cur.x + 2 * (start.x - cur.x) / 3, cur.y + 2 * (start.y - cur.y) / 3), start};
                    }
                    break;
                default:
                    break;
            }
        }
        return n == 4;
    }
    Point2Dd Bez(const MouldSide& s, double t) {
        const double u = 1 - t;
        const double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        return Point2Dd(a * s.p0.x + b * s.p1.x + c * s.p2.x + d * s.p3.x,
                        a * s.p0.y + b * s.p1.y + c * s.p2.y + d * s.p3.y);
    }
}

bool VectorMould::ShapeCorners(Point2Dd corners[4]) const {
    MouldSide sides[4];
    if (!MouldSides(Shape, sides)) return false;
    for (int i = 0; i < 4; ++i) corners[i] = sides[i].p0;
    return true;
}

Point2Dd VectorMould::Warp(const Point2Dd& p) const {
    MouldSide sides[4];
    if (!MouldSides(Shape, sides)) return p;
    const Rect2Dd src = EffectiveSourceBounds();
    if (src.width <= 0 || src.height <= 0) return p;
    const double u = std::min(1.5, std::max(-0.5, (p.x - src.x) / src.width));
    const double v = std::min(1.5, std::max(-0.5, (p.y - src.y) / src.height));
    const Point2Dd P00 = sides[0].p0, P10 = sides[1].p0, P11 = sides[2].p0, P01 = sides[3].p0;
    if (Kind == MouldKind::Perspective) {
        // Projective map of the unit square onto the four corners.
        const double dx1 = P10.x - P11.x, dx2 = P01.x - P11.x, dx3 = P00.x - P10.x + P11.x - P01.x;
        const double dy1 = P10.y - P11.y, dy2 = P01.y - P11.y, dy3 = P00.y - P10.y + P11.y - P01.y;
        double g = 0, h = 0;
        const double den = dx1 * dy2 - dx2 * dy1;
        if (std::fabs(den) > 1e-12) {
            g = (dx3 * dy2 - dx2 * dy3) / den;
            h = (dx1 * dy3 - dx3 * dy1) / den;
        }
        const double a = P10.x - P00.x + g * P10.x, b = P01.x - P00.x + h * P01.x, c = P00.x;
        const double d = P10.y - P00.y + g * P10.y, e = P01.y - P00.y + h * P01.y, f = P00.y;
        const double w = g * u + h * v + 1;
        if (std::fabs(w) < 1e-12) return p;
        return Point2Dd((a * u + b * v + c) / w, (d * u + e * v + f) / w);
    }
    // Coons patch: top and bottom run left to right, left and right run
    // top to bottom (the shape's right side runs down, its bottom and
    // left sides run backwards).
    const Point2Dd top = Bez(sides[0], u);
    const Point2Dd right = Bez(sides[1], v);
    const Point2Dd bottom = Bez(sides[2], 1 - u);
    const Point2Dd left = Bez(sides[3], 1 - v);
    const double x = (1 - v) * top.x + v * bottom.x + (1 - u) * left.x + u * right.x -
                     ((1 - u) * (1 - v) * P00.x + u * (1 - v) * P10.x + u * v * P11.x + (1 - u) * v * P01.x);
    const double y = (1 - v) * top.y + v * bottom.y + (1 - u) * left.y + u * right.y -
                     ((1 - u) * (1 - v) * P00.y + u * (1 - v) * P10.y + u * v * P11.y + (1 - u) * v * P01.y);
    return Point2Dd(x, y);
}

// VectorUse
Rect2Dd VectorUse::GetBoundingBox() const {
    Rect2Dd bbox{Position.x, Position.y, Size.width, Size.height};
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorUse::Clone() const {
    auto clone = std::make_shared<VectorUse>(*this);
    clone->Parent.reset();
    return clone;
}


// ===== SPECIAL ELEMENTS IMPLEMENTATION =====

// VectorImage
Rect2Dd VectorImage::GetBoundingBox() const {
    Rect2Dd bbox = Bounds;
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorImage::Clone() const {
    auto clone = std::make_shared<VectorImage>(*this);
    clone->Parent.reset();
    return clone;
}

// ===== DEFINITION ELEMENTS IMPLEMENTATION =====

// VectorGradient
std::shared_ptr<VectorElement> VectorGradient::Clone() const {
    auto clone = std::make_shared<VectorGradient>(*this);
    clone->Parent.reset();
    return clone;
}


// VectorPattern
std::shared_ptr<VectorElement> VectorPattern::Clone() const {
    auto clone = std::make_shared<VectorPattern>(*this);
    clone->Parent.reset();
    
    // Deep clone pattern content
    if (clone->Data.Content) {
        clone->Data.Content = std::dynamic_pointer_cast<VectorGroup>(Data.Content->Clone());
    }
    
    return clone;
}


// VectorFilter
std::shared_ptr<VectorElement> VectorFilter::Clone() const {
    auto clone = std::make_shared<VectorFilter>(*this);
    clone->Parent.reset();
    return clone;
}


// VectorClipPath
Rect2Dd VectorClipPath::GetBoundingBox() const {
    if (Data.Elements.empty()) {
        return Rect2Dd{0, 0, 0, 0};
    }
    
    Rect2Dd bbox = Data.Elements[0]->GetBoundingBox();
    
    for (size_t i = 1; i < Data.Elements.size(); i++) {
        Rect2Dd elemBox = Data.Elements[i]->GetBoundingBox();
        
        float minX = std::min(bbox.x, elemBox.x);
        float minY = std::min(bbox.y, elemBox.y);
        float maxX = std::max(bbox.x + bbox.width, elemBox.x + elemBox.width);
        float maxY = std::max(bbox.y + bbox.height, elemBox.y + elemBox.height);
        
        bbox = Rect2Dd{minX, minY, maxX - minX, maxY - minY};
    }
    
    return bbox;
}

std::shared_ptr<VectorElement> VectorClipPath::Clone() const {
    auto clone = std::make_shared<VectorClipPath>(*this);
    clone->Parent.reset();
    
    // Deep clone clipping elements
    clone->Data.Elements.clear();
    for (const auto& elem : Data.Elements) {
        clone->Data.Elements.push_back(elem->Clone());
    }
    
    return clone;
}


// VectorMask
std::shared_ptr<VectorElement> VectorMask::Clone() const {
    auto clone = std::make_shared<VectorMask>(*this);
    clone->Parent.reset();
    
    // Deep clone mask elements
    clone->Data.Elements.clear();
    for (const auto& elem : Data.Elements) {
        clone->Data.Elements.push_back(elem->Clone());
    }
    
    return clone;
}


// VectorMarker
std::shared_ptr<VectorElement> VectorMarker::Clone() const {
    auto clone = std::make_shared<VectorMarker>(*this);
    clone->Parent.reset();
    
    // Deep clone marker content
    if (clone->Data.Content) {
        clone->Data.Content = std::dynamic_pointer_cast<VectorGroup>(Data.Content->Clone());
    }
    
    return clone;
}


// VectorLayer
std::shared_ptr<VectorElement> VectorLayer::Clone() const {
    auto clone = std::make_shared<VectorLayer>(*this);
    clone->Parent.reset();
    
    // Deep clone children, re-parented to the clone (see VectorGroup::Clone).
    clone->Children.clear();
    for (const auto& child : Children) {
        if (!child) continue;
        auto childClone = child->Clone();
        childClone->Parent = clone;
        clone->Children.push_back(childClone);
    }
    
    return clone;
}


// ===== UNITS =====

double PointsPerUnit(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Unspecified: return 0.0;
        case LengthUnit::Point:       return 1.0;
        case LengthUnit::Pixel:       return 72.0 / 96.0;
        case LengthUnit::Inch:        return 72.0;
        case LengthUnit::Foot:        return 72.0 * 12.0;
        case LengthUnit::Yard:        return 72.0 * 36.0;
        case LengthUnit::Mile:        return 72.0 * 63360.0;
        case LengthUnit::Mil:         return 72.0 / 1000.0;
        case LengthUnit::Millimeter:  return 72.0 / 25.4;
        case LengthUnit::Centimeter:  return 72.0 / 2.54;
        case LengthUnit::Decimeter:   return 72.0 / 0.254;
        case LengthUnit::Meter:       return 72.0 / 0.0254;
        case LengthUnit::Kilometer:   return 72.0 / 0.0000254;
        case LengthUnit::Micrometer:  return 72.0 / 25400.0;
        case LengthUnit::Nanometer:   return 72.0 / 25400000.0;
    }
    return 0.0;
}

const char* LengthUnitSymbol(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Unspecified: return "";
        case LengthUnit::Point:       return "pt";
        case LengthUnit::Pixel:       return "px";
        case LengthUnit::Inch:        return "in";
        case LengthUnit::Foot:        return "ft";
        case LengthUnit::Yard:        return "yd";
        case LengthUnit::Mile:        return "mi";
        case LengthUnit::Mil:         return "mil";
        case LengthUnit::Millimeter:  return "mm";
        case LengthUnit::Centimeter:  return "cm";
        case LengthUnit::Decimeter:   return "dm";
        case LengthUnit::Meter:       return "m";
        case LengthUnit::Kilometer:   return "km";
        case LengthUnit::Micrometer:  return "um";
        case LengthUnit::Nanometer:   return "nm";
    }
    return "";
}

// ===== VECTOR DOCUMENT IMPLEMENTATION =====

std::shared_ptr<VectorLayer> VectorDocument::AddLayer(const std::string& name) {
    auto layer = std::make_shared<VectorLayer>();
    layer->Name = name;
    layer->Id = "layer_" + std::to_string(Layers.size());
    Layers.push_back(layer);
    return layer;
}

void VectorDocument::RemoveLayer(const std::string& name) {
    Layers.erase(
        std::remove_if(Layers.begin(), Layers.end(),
            [&name](const std::shared_ptr<VectorLayer>& layer) {
                return layer->Name == name;
            }),
        Layers.end()
    );
}

std::shared_ptr<VectorLayer> VectorDocument::GetLayer(const std::string& name) const {
    for (const auto& layer : Layers) {
        if (layer->Name == name) {
            return layer;
        }
    }
    return nullptr;
}

void VectorDocument::AddDefinition(const std::string& id, std::shared_ptr<VectorElement> element) {
    Definitions[id] = element;
}

std::shared_ptr<VectorElement> VectorDocument::GetDefinition(const std::string& id) const {
    auto it = Definitions.find(id);
    if (it != Definitions.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<VectorElement> VectorDocument::FindElementById(const std::string& id) const {
    // Search in layers
    for (const auto& layer : Layers) {
        if (layer->Id == id) {
            return layer;
        }
        if (auto found = layer->FindChild(id)) {
            return found;
        }
    }
    
    // Search in definitions
    auto it = Definitions.find(id);
    if (it != Definitions.end()) {
        return it->second;
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<VectorElement>> VectorDocument::FindElementsByClass(const std::string& className) const {
    std::vector<std::shared_ptr<VectorElement>> result;
    
    // Lambda for recursive search
    std::function<void(const std::shared_ptr<VectorElement>&)> searchElement = 
        [&](const std::shared_ptr<VectorElement>& elem) {
            if (elem->HasClass(className)) {
                result.push_back(elem);
            }
            
            if (auto group = std::dynamic_pointer_cast<VectorGroup>(elem)) {
                for (const auto& child : group->Children) {
                    searchElement(child);
                }
            }
        };
    
    for (const auto& layer : Layers) {
        searchElement(layer);
    }
    
    return result;
}

Rect2Dd VectorDocument::GetBoundingBox() const {
    Rect2Dd bbox{0, 0, 0, 0};
    for (const auto& layer : Layers) {
        if (layer) bbox = UnionBounds(bbox, layer->GetBoundingBox());
    }
    if (IsEmptyBounds(bbox)) {
        return Rect2Dd{0, 0, Size.width, Size.height};
    }
    return bbox;
}

// ===== WHERE THE DRAWING ACTUALLY IS =====
// ContentBounds(): GetBoundingBox() with far-away specks left out. See the
// header for what this is for and what it promises.
namespace {

    // A run of drawables is a speck to ignore only when it holds at most this
    // fraction of them AND stands at least this much of the drawing's extent
    // clear of the rest. Both have to hold: a few entities at the edge of a
    // dense drawing (a frame, a north arrow) are not specks because they are
    // not far away, and a quarter of the drawing is not a speck however far
    // off it sits - it is the second half of a two-part sheet.
    constexpr double kSpeckFraction = 0.01;
    constexpr double kGapFraction   = 0.20;
    // Below this, every drawable is a meaningful part of the picture.
    constexpr size_t kMinDrawables  = 50;

    void CollectDrawableBounds(const VectorElement& element, const Matrix3x3& parent,
                               std::vector<Rect2Dd>& out) {
        if (!element.Style.Visible || !element.Style.Display) return;
        if (const auto* group = dynamic_cast<const VectorGroup*>(&element)) {
            // A group's own GetBoundingBox() applies its transform to the
            // union of its children, so the accumulated matrix picks it up
            // here and the children are walked in their own space.
            Matrix3x3 here = element.Transform.has_value() ? parent * element.Transform.value()
                                                           : parent;
            for (const auto& child : group->Children) {
                if (child) CollectDrawableBounds(*child, here, out);
            }
            return;
        }
        Rect2Dd box = element.GetBoundingBox();     // the element's own transform is in it
        if (IsEmptyBounds(box)) return;
        out.push_back(parent.IsIdentity() ? box : parent.Transform(box));
    }

    // The indices that survive on one axis: the drawables are grouped into
    // runs separated by gaps of at least `gap`, and the runs too small to
    // matter are dropped - never all of them, and never more than the speck
    // budget in total.
    std::vector<size_t> DropDistantRuns(const std::vector<Rect2Dd>& boxes,
                                        std::vector<size_t> keep, bool vertical, double gap) {
        if (keep.size() < kMinDrawables || !(gap > 0)) return keep;
        auto lo = [&](size_t i) { return vertical ? boxes[i].y : boxes[i].x; };
        auto hi = [&](size_t i) { return vertical ? boxes[i].y + boxes[i].height
                                                  : boxes[i].x + boxes[i].width; };
        std::sort(keep.begin(), keep.end(), [&](size_t a, size_t b) { return lo(a) < lo(b); });

        std::vector<std::vector<size_t>> runs;
        double reach = 0;
        for (size_t i : keep) {
            if (runs.empty() || lo(i) - reach > gap) {
                runs.push_back({i});
                reach = hi(i);
            } else {
                runs.back().push_back(i);
                reach = std::max(reach, hi(i));
            }
        }
        if (runs.size() < 2) return keep;

        // Smallest first, so the budget is spent on the specks.
        std::vector<size_t> order(runs.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return runs[a].size() < runs[b].size(); });

        std::vector<bool> dropped(runs.size(), false);
        size_t budget = static_cast<size_t>(keep.size() * kSpeckFraction);
        size_t left = runs.size();
        for (size_t r : order) {
            if (left <= 1) break;
            if (runs[r].size() > budget) break;     // and so is every larger run
            budget -= runs[r].size();
            dropped[r] = true;
            --left;
        }

        std::vector<size_t> survivors;
        survivors.reserve(keep.size());
        for (size_t r = 0; r < runs.size(); ++r) {
            if (!dropped[r]) survivors.insert(survivors.end(), runs[r].begin(), runs[r].end());
        }
        return survivors;
    }

}   // namespace

Rect2Dd ContentBounds(const VectorDocument& document) {
    std::vector<Rect2Dd> boxes;
    for (const auto& layer : document.Layers) {
        if (layer && layer->Visible) CollectDrawableBounds(*layer, Matrix3x3::Identity(), boxes);
    }
    Rect2Dd full{0, 0, 0, 0};
    for (const Rect2Dd& b : boxes) full = UnionBounds(full, b);
    if (boxes.size() < kMinDrawables || IsEmptyBounds(full)) return document.GetBoundingBox();

    std::vector<size_t> keep(boxes.size());
    std::iota(keep.begin(), keep.end(), size_t{0});
    keep = DropDistantRuns(boxes, std::move(keep), false, full.width * kGapFraction);
    keep = DropDistantRuns(boxes, std::move(keep), true, full.height * kGapFraction);
    if (keep.size() == boxes.size()) return full;

    Rect2Dd dense{0, 0, 0, 0};
    for (size_t i : keep) dense = UnionBounds(dense, boxes[i]);
    return IsEmptyBounds(dense) ? full : dense;
}

void VectorDocument::FitToContent(float padding) {
    Rect2Dd bbox = GetBoundingBox();
    
    ViewBox = Rect2Dd{
        bbox.x - padding,
        bbox.y - padding,
        bbox.width + 2 * padding,
        bbox.height + 2 * padding
    };
    
    Size = Size2Dd{ViewBox.width, ViewBox.height};
}

void VectorDocument::Clear() {
    Layers.clear();
    Definitions.clear();
    NamedStyles.clear();
}

std::shared_ptr<VectorDocument> VectorDocument::Clone() const {
    auto clone = std::make_shared<VectorDocument>(*this);
    
    // Deep clone layers
    clone->Layers.clear();
    for (const auto& layer : Layers) {
        clone->Layers.push_back(
            std::dynamic_pointer_cast<VectorLayer>(layer->Clone())
        );
    }
    
    // Deep clone definitions
    clone->Definitions.clear();
    for (const auto& [id, elem] : Definitions) {
        clone->Definitions[id] = elem->Clone();
    }
    
    return clone;
}

// ===== UTILITY FUNCTIONS IMPLEMENTATION =====

// ===== OUTLINES =====

bool BuildOutlinePath(const VectorElement& element, PathData& out) {
    using namespace VectorConverter::PathOps;
    switch (element.Type) {
        case VectorElementType::Rectangle:
        case VectorElementType::RoundedRectangle: {
            const auto& r = static_cast<const VectorRect&>(element);
            out = (r.RadiusX > 0 || r.RadiusY > 0) ? SegsToPathData(RoundedRectSegs(r.Bounds, r.RadiusX, r.RadiusY))
                                                   : SegsToPathData(RectSegs(r.Bounds));
            return true;
        }
        case VectorElementType::Circle: {
            const auto& c = static_cast<const VectorCircle&>(element);
            out = SegsToPathData(EllipseSegs(c.Center, c.Radius, c.Radius));
            return true;
        }
        case VectorElementType::Ellipse: {
            const auto& e = static_cast<const VectorEllipse&>(element);
            out = SegsToPathData(EllipseSegs(e.Center, e.RadiusX, e.RadiusY));
            return true;
        }
        case VectorElementType::Line: {
            const auto& l = static_cast<const VectorLine&>(element);
            PathData d;
            PathCommand m; m.Type = PathCommandType::MoveTo;
            m.Parameters = {static_cast<float>(l.Start.x), static_cast<float>(l.Start.y)};
            PathCommand n; n.Type = PathCommandType::LineTo;
            n.Parameters = {static_cast<float>(l.End.x), static_cast<float>(l.End.y)};
            d.commands = {m, n};
            out = d;
            return true;
        }
        case VectorElementType::Polyline:
        case VectorElementType::Polygon: {
            const auto* pts = element.Type == VectorElementType::Polyline
                              ? &static_cast<const VectorPolyline&>(element).Points
                              : &static_cast<const VectorPolygon&>(element).Points;
            if (pts->empty()) return false;
            PathData d;
            for (size_t i = 0; i < pts->size(); ++i) {
                PathCommand c;
                c.Type = i == 0 ? PathCommandType::MoveTo : PathCommandType::LineTo;
                c.Parameters = {static_cast<float>((*pts)[i].x), static_cast<float>((*pts)[i].y)};
                d.commands.push_back(c);
            }
            if (element.Type == VectorElementType::Polygon) {
                PathCommand z; z.Type = PathCommandType::ClosePath;
                d.commands.push_back(z);
                d.Closed = true;
            }
            out = d;
            return true;
        }
        case VectorElementType::Path:
            out = static_cast<const VectorPath&>(element).Path;
            return !out.commands.empty();
        default:
            return false;
    }
}

// ===== LINE GALLERY GEOMETRY =====

namespace {
    Point2Dd UnitVector(const Point2Dd& v) {
        const double l = std::hypot(v.x, v.y);
        return l > 1e-12 ? Point2Dd(v.x / l, v.y / l) : Point2Dd(1, 0);
    }
    void AppendPolyline(PathData& out, const std::vector<Point2Dd>& pts, bool close) {
        for (size_t i = 0; i < pts.size(); ++i) {
            PathCommand c;
            c.Type = i == 0 ? PathCommandType::MoveTo : PathCommandType::LineTo;
            c.Parameters = {static_cast<float>(pts[i].x), static_cast<float>(pts[i].y)};
            out.commands.push_back(c);
        }
        if (close && !pts.empty()) {
            PathCommand z; z.Type = PathCommandType::ClosePath;
            out.commands.push_back(z);
        }
    }
}

std::vector<FlatSubpath> FlattenPathData(const PathData& path) {
    using namespace VectorConverter::PathOps;
    std::vector<FlatSubpath> out;
    Point2Dd cur{0, 0};
    for (const auto& s : NormalizePath(path)) {
        switch (s.kind) {
            case FlatSeg::Move:
                out.push_back({});
                out.back().Points.push_back(s.p[0]);
                cur = s.p[0];
                break;
            case FlatSeg::Line:
                if (out.empty()) { out.push_back({}); out.back().Points.push_back(cur); }
                out.back().Points.push_back(s.p[0]);
                cur = s.p[0];
                break;
            case FlatSeg::Cubic: {
                if (out.empty()) { out.push_back({}); out.back().Points.push_back(cur); }
                const double len = std::hypot(s.p[0].x - cur.x, s.p[0].y - cur.y) +
                                   std::hypot(s.p[1].x - s.p[0].x, s.p[1].y - s.p[0].y) +
                                   std::hypot(s.p[2].x - s.p[1].x, s.p[2].y - s.p[1].y);
                const int n = std::min(64, std::max(4, static_cast<int>(std::ceil(len / 3.0))));
                for (int i = 1; i <= n; ++i) {
                    const double u = static_cast<double>(i) / n, v = 1.0 - u;
                    out.back().Points.emplace_back(
                            v * v * v * cur.x + 3 * v * v * u * s.p[0].x + 3 * v * u * u * s.p[1].x + u * u * u * s.p[2].x,
                            v * v * v * cur.y + 3 * v * v * u * s.p[0].y + 3 * v * u * u * s.p[1].y + u * u * u * s.p[2].y);
                }
                cur = s.p[2];
                break;
            }
        }
        if (s.closeAfter && !out.empty()) {
            out.back().Closed = true;
            if (!out.back().Points.empty()) cur = out.back().Points.front();
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const FlatSubpath& l) { return l.Points.size() < 2; }), out.end());
    return out;
}

bool PathEndpoints(const PathData& path, Point2Dd& start, Point2Dd& startDir, Point2Dd& end, Point2Dd& endDir) {
    const auto lines = FlattenPathData(path);
    if (lines.empty()) return false;
    const FlatSubpath& first = lines.front();
    const FlatSubpath& last = lines.back();
    if (first.Closed || last.Closed) return false;
    size_t k = 1;
    while (k + 1 < first.Points.size() &&
           std::hypot(first.Points[k].x - first.Points[0].x, first.Points[k].y - first.Points[0].y) < 1e-9) ++k;
    start = first.Points[0];
    startDir = UnitVector(Point2Dd(first.Points[0].x - first.Points[k].x, first.Points[0].y - first.Points[k].y));
    const size_t n = last.Points.size();
    size_t j = n - 2;
    while (j > 0 && std::hypot(last.Points[n - 1].x - last.Points[j].x, last.Points[n - 1].y - last.Points[j].y) < 1e-9) --j;
    end = last.Points[n - 1];
    endDir = UnitVector(Point2Dd(last.Points[n - 1].x - last.Points[j].x, last.Points[n - 1].y - last.Points[j].y));
    return true;
}

// Xara's default arrowheads, as its source defines them (Kernel/arrows.cpp,
// ArrowRec::CreateStockArrow): path data in millipoints for a line 36000
// millipoints wide, x pointing away from the line's end, scaled by
// (arrow size * line width / 36000) and placed with `centre` on the end
// point (Kernel/arrows.cpp, ArrowRec::GetArrowMatrix). Xara's default size
// is 3, which is this model's Scale 1. The hollow diamond's inner subpath
// is wound the other way so a non-zero fill leaves the hole.
namespace {
    struct XaraStock { const char* Spec; double Cx, Cy; };
    const XaraStock* StockArrow(ArrowheadKind k) {
        static const XaraStock straight{"M -9000 54000 L -9000 -54000 L 117000 0 Z", 0, 0};
        static const XaraStock angled{"M -27000 54000 L -9000 0 L -27000 -54000 L 135000 0 Z", 0, 0};
        static const XaraStock rounded{
            "M -9000 0 L -9000 -45000 C -9000 -51708 2808 -56580 9000 -54000 L 117000 -9000 "
            "C 120916 -7369 126000 -4242 126000 0 C 126000 4242 120916 7369 117000 9000 "
            "L 9000 54000 C 2808 56580 -9000 51708 -9000 45000 Z", 0, 0};
        static const XaraStock spot{
            "M -54000 0 C -54000 29807 -29807 54000 0 54000 C 29807 54000 54000 29807 54000 0 "
            "C 54000 -29807 29807 -54000 0 -54000 C -29807 -54000 -54000 -29807 -54000 0 Z", 0, 0};
        static const XaraStock diamond{"M -63000 0 L 0 63000 L 63000 0 L 0 -63000 Z", 0, 0};
        static const XaraStock feather{
            "M 18000 -54000 L 108000 -54000 L 63000 0 L 108000 54000 L 18000 54000 L -36000 0 Z", 0, 0};
        static const XaraStock feather2{
            "M -36000 0 L 18000 -54000 L 54000 -54000 L 18000 -18000 L 27000 -18000 L 63000 -54000 "
            "L 99000 -54000 L 63000 -18000 L 72000 -18000 L 108000 -54000 L 144000 -54000 L 90000 0 "
            "L 144000 54000 L 108000 54000 L 72000 18000 L 63000 18000 L 99000 54000 L 63000 54000 "
            "L 27000 18000 L 18000 18000 L 54000 54000 L 18000 54000 Z", 0, 0};
        static const XaraStock hollow{
            "M 0 63000 L -63000 0 L 0 -63000 L 63000 0 Z M 0 45000 L 45000 0 L 0 -45000 L -45000 0 Z", -45000, 0};
        switch (k) {
            case ArrowheadKind::StraightArrow: return &straight;
            case ArrowheadKind::AngledArrow: return &angled;
            case ArrowheadKind::RoundedArrow: return &rounded;
            case ArrowheadKind::Spot: return &spot;
            case ArrowheadKind::SolidDiamond: return &diamond;
            case ArrowheadKind::Feather: return &feather;
            case ArrowheadKind::Feather2: return &feather2;
            case ArrowheadKind::HollowDiamond: return &hollow;
            default: return nullptr;
        }
    }

    PathData XaraStockArrowhead(const ArrowheadData& arrow, const Point2Dd& tip, const Point2Dd& d, double W) {
        PathData out;
        const XaraStock* stock = StockArrow(arrow.Kind);
        if (!stock) return out;
        const double k = 3.0 * arrow.Scale * W / 36000.0;
        const Point2Dd n(-d.y, d.x);
        // Stock x runs away from the line, stock y across it.
        auto at = [&](double x, double y) {
            const double ax = (x - stock->Cx) * k, ay = (y - stock->Cy) * k;
            return Point2Dd(tip.x + d.x * ax + n.x * ay, tip.y + d.y * ax + n.y * ay);
        };
        const char* s = stock->Spec;
        // The arrowhead spec is a built-in dot-decimal string, so it must be
        // read as one: strtod goes through LC_NUMERIC and would stop at the
        // first '.' on a comma-decimal desktop, truncating every arrowhead.
        auto num = [&]() {
            double v = 0.0;
            s = ParseFloatClassic(s, s + std::strlen(s), v);
            return v;
        };
        while (*s) {
            while (*s == ' ') ++s;
            const char verb = *s;
            if (!verb) break;
            ++s;
            PathCommand c;
            if (verb == 'Z') {
                c.Type = PathCommandType::ClosePath;
            } else {
                const int pts = verb == 'C' ? 3 : 1;
                c.Type = verb == 'M' ? PathCommandType::MoveTo : verb == 'C' ? PathCommandType::CurveTo : PathCommandType::LineTo;
                for (int i = 0; i < pts; ++i) {
                    const double x = num(), y = num();
                    const Point2Dd p = at(x, y);
                    c.Parameters.push_back(static_cast<float>(p.x));
                    c.Parameters.push_back(static_cast<float>(p.y));
                }
            }
            out.commands.push_back(c);
        }
        out.Closed = true;
        return out;
    }
}   // namespace

PathData ArrowheadOutline(const ArrowheadData& arrow, const Point2Dd& tip, const Point2Dd& d, float width, bool& stroked) {
    PathData out;
    stroked = false;
    if (!arrow.IsSet()) return out;
    const double W = std::max(0.5f, width);
    const double L = 4.0 * W * arrow.Scale, H = 2.0 * W * arrow.Scale;
    const Point2Dd n(-d.y, d.x);
    // `along` back from the tip, `across` to the side.
    auto P = [&](double along, double across) {
        return Point2Dd(tip.x - d.x * along + n.x * across, tip.y - d.y * along + n.y * across);
    };
    switch (arrow.Kind) {
        case ArrowheadKind::Triangle:
            AppendPolyline(out, {tip, P(L, H / 2), P(L, -H / 2)}, true);
            break;
        case ArrowheadKind::OpenArrow:
            AppendPolyline(out, {P(L, H / 2), tip, P(L, -H / 2)}, false);
            stroked = true;
            break;
        case ArrowheadKind::Circle: {
            const Point2Dd c = P(H / 2, 0);
            out = VectorConverter::PathOps::SegsToPathData(VectorConverter::PathOps::EllipseSegs(c, H / 2, H / 2));
            break;
        }
        case ArrowheadKind::Square:
            AppendPolyline(out, {P(0, H / 2), P(H, H / 2), P(H, -H / 2), P(0, -H / 2)}, true);
            break;
        case ArrowheadKind::Diamond:
            AppendPolyline(out, {tip, P(L / 2, H / 2), P(L, 0), P(L / 2, -H / 2)}, true);
            break;
        case ArrowheadKind::Bar:
            AppendPolyline(out, {P(0, H / 2), P(0, -H / 2)}, false);
            stroked = true;
            break;
        case ArrowheadKind::StraightArrow:
        case ArrowheadKind::AngledArrow:
        case ArrowheadKind::RoundedArrow:
        case ArrowheadKind::Spot:
        case ArrowheadKind::SolidDiamond:
        case ArrowheadKind::Feather:
        case ArrowheadKind::Feather2:
        case ArrowheadKind::HollowDiamond:
            out = XaraStockArrowhead(arrow, tip, d, W);
            break;
        case ArrowheadKind::NoArrowhead:
        default:
            break;
    }
    if (!IsXaraArrowhead(arrow.Kind)) out.Closed = !stroked;
    return out;
}

PathData VariableWidthOutline(const PathData& path, const StrokeData& stroke) {
    PathData out;
    for (const FlatSubpath& sub : FlattenPathData(path)) {
        std::vector<Point2Dd> pts = sub.Points;
        const bool closed = sub.Closed;
        if (closed && pts.size() > 2 &&
            std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) < 1e-9)
            pts.pop_back();
        const size_t n = pts.size();
        if (n < 2) continue;
        std::vector<double> cum(n, 0.0);
        for (size_t i = 1; i < n; ++i) cum[i] = cum[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        const double total = closed ? cum.back() + std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y)
                                    : cum.back();
        if (total <= 1e-9) continue;
        std::vector<Point2Dd> left(n), right(n);
        Point2Dd lastT(1, 0);
        for (size_t i = 0; i < n; ++i) {
            const Point2Dd& prev = i > 0 ? pts[i - 1] : (closed ? pts[n - 1] : pts[i]);
            const Point2Dd& next = i + 1 < n ? pts[i + 1] : (closed ? pts[0] : pts[i]);
            Point2Dd t(next.x - prev.x, next.y - prev.y);
            if (std::hypot(t.x, t.y) < 1e-12) t = lastT; else t = UnitVector(t);
            lastT = t;
            const double half = stroke.WidthAt(static_cast<float>(cum[i] / total)) / 2.0;
            left[i] = Point2Dd(pts[i].x - t.y * half, pts[i].y + t.x * half);
            right[i] = Point2Dd(pts[i].x + t.y * half, pts[i].y - t.x * half);
        }
        if (closed) {
            AppendPolyline(out, left, true);
            AppendPolyline(out, right, true);
        } else {
            std::vector<Point2Dd> ring = left;
            for (size_t i = n; i-- > 0;) ring.push_back(right[i]);
            AppendPolyline(out, ring, true);
        }
    }
    out.Closed = true;
    return out;
}

PathData ParsePathString(const std::string& pathStr) {
    PathData result;
    std::istringstream iss(pathStr);
    char cmd = 0;
    bool relative = false;
    
    while (iss >> cmd) {
        PathCommand pathCmd;
        
        // Check if lowercase (relative) or uppercase (absolute)
        relative = std::islower(cmd);
        cmd = std::toupper(cmd);
        
        pathCmd.Relative = relative;
        
        switch (cmd) {
            case 'M':  // MoveTo
                pathCmd.Type = PathCommandType::MoveTo;
                pathCmd.Parameters.resize(2);
                iss >> pathCmd.Parameters[0] >> pathCmd.Parameters[1];
                break;
                
            case 'L':  // LineTo
                pathCmd.Type = PathCommandType::LineTo;
                pathCmd.Parameters.resize(2);
                iss >> pathCmd.Parameters[0] >> pathCmd.Parameters[1];
                break;
                
            case 'H':  // HorizontalLineTo
                pathCmd.Type = PathCommandType::HorizontalLineTo;
                pathCmd.Parameters.resize(1);
                iss >> pathCmd.Parameters[0];
                break;
                
            case 'V':  // VerticalLineTo
                pathCmd.Type = PathCommandType::VerticalLineTo;
                pathCmd.Parameters.resize(1);
                iss >> pathCmd.Parameters[0];
                break;
                
            case 'C':  // CurveTo
                pathCmd.Type = PathCommandType::CurveTo;
                pathCmd.Parameters.resize(6);
                for (int i = 0; i < 6; i++) {
                    iss >> pathCmd.Parameters[i];
                }
                break;
                
            case 'S':  // SmoothCurveTo
                pathCmd.Type = PathCommandType::SmoothCurveTo;
                pathCmd.Parameters.resize(4);
                for (int i = 0; i < 4; i++) {
                    iss >> pathCmd.Parameters[i];
                }
                break;
                
            case 'Q':  // QuadraticTo
                pathCmd.Type = PathCommandType::QuadraticTo;
                pathCmd.Parameters.resize(4);
                for (int i = 0; i < 4; i++) {
                    iss >> pathCmd.Parameters[i];
                }
                break;
                
            case 'T':  // SmoothQuadraticTo
                pathCmd.Type = PathCommandType::SmoothQuadraticTo;
                pathCmd.Parameters.resize(2);
                iss >> pathCmd.Parameters[0] >> pathCmd.Parameters[1];
                break;
                
            case 'A':  // ArcTo
                pathCmd.Type = PathCommandType::ArcTo;
                pathCmd.Parameters.resize(7);
                for (int i = 0; i < 7; i++) {
                    iss >> pathCmd.Parameters[i];
                }
                break;
                
            case 'Z':  // ClosePath
                pathCmd.Type = PathCommandType::ClosePath;
                result.Closed = true;
                break;
        }
        
        result.commands.push_back(pathCmd);
    }
    
    return result;
}

std::string SerializePathData(const PathData& path) {
    std::ostringstream oss;
    // Path parameters are separated by spaces and commas, so a comma decimal
    // point does not just misread - it changes the number of coordinates.
    // `M 1.5 2` written on a comma-decimal desktop becomes `M 1,5 2`, which
    // reads back as the point (1, 5). This is the defect that was fixed in
    // the SVG converter and left here.
    oss.imbue(std::locale::classic());
    
    for (const auto& cmd : path.commands) {
        char cmdChar = 0;
        
        switch (cmd.Type) {
            case PathCommandType::MoveTo: cmdChar = 'M'; break;
            case PathCommandType::LineTo: cmdChar = 'L'; break;
            case PathCommandType::HorizontalLineTo: cmdChar = 'H'; break;
            case PathCommandType::VerticalLineTo: cmdChar = 'V'; break;
            case PathCommandType::CurveTo: cmdChar = 'C'; break;
            case PathCommandType::SmoothCurveTo: cmdChar = 'S'; break;
            case PathCommandType::QuadraticTo: cmdChar = 'Q'; break;
            case PathCommandType::SmoothQuadraticTo: cmdChar = 'T'; break;
            case PathCommandType::ArcTo: cmdChar = 'A'; break;
            case PathCommandType::ClosePath: cmdChar = 'Z'; break;
            default: continue;
        }
        
        if (cmd.Relative) {
            cmdChar = std::tolower(cmdChar);
        }
        
        oss << cmdChar;
        
        for (float param : cmd.Parameters) {
            oss << " " << param;
        }
        
        oss << " ";
    }
    
    return oss.str();
}

Color ParseColorString(const std::string& colorStr) {
    Color result{0, 0, 0, 255};
    
    if (colorStr.empty()) {
        return result;
    }
    
    // Handle hex colors (#RRGGBB or #RGB)
    if (colorStr[0] == '#') {
        std::string hex = colorStr.substr(1);
        
        if (hex.length() == 3) {
            // #RGB format
            result.r = std::stoi(hex.substr(0, 1) + hex.substr(0, 1), nullptr, 16);
            result.g = std::stoi(hex.substr(1, 1) + hex.substr(1, 1), nullptr, 16);
            result.b = std::stoi(hex.substr(2, 1) + hex.substr(2, 1), nullptr, 16);
        } else if (hex.length() == 6) {
            // #RRGGBB format
            result.r = std::stoi(hex.substr(0, 2), nullptr, 16);
            result.g = std::stoi(hex.substr(2, 2), nullptr, 16);
            result.b = std::stoi(hex.substr(4, 2), nullptr, 16);
        }
    }
    // Handle rgb() format
    else if (colorStr.find("rgb(") == 0) {
        std::regex rgbRegex(R"(rgb\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))");
        std::smatch match;
        if (std::regex_match(colorStr, match, rgbRegex)) {
            result.r = std::stoi(match[1]);
            result.g = std::stoi(match[2]);
            result.b = std::stoi(match[3]);
        }
    }
    // Handle rgba() format
    else if (colorStr.find("rgba(") == 0) {
        std::regex rgbaRegex(R"(rgba\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)\s*\))");
        std::smatch match;
        if (std::regex_match(colorStr, match, rgbaRegex)) {
            result.r = std::stoi(match[1]);
            result.g = std::stoi(match[2]);
            result.b = std::stoi(match[3]);
            float alpha = 1.0f;
            TryParseFloat(match[4].str(), alpha);   // rgba() alpha: dot-decimal
            result.a = static_cast<uint8_t>(alpha * 255);
        }
    }
    // Handle named colors (basic set)
    else {
        static std::map<std::string, Color> namedColors = {
            {"black", {0, 0, 0, 255}},
            {"white", {255, 255, 255, 255}},
            {"red", {255, 0, 0, 255}},
            {"green", {0, 128, 0, 255}},
            {"blue", {0, 0, 255, 255}},
            {"yellow", {255, 255, 0, 255}},
            {"cyan", {0, 255, 255, 255}},
            {"magenta", {255, 0, 255, 255}},
            {"gray", {128, 128, 128, 255}},
            {"grey", {128, 128, 128, 255}},
            {"transparent", {0, 0, 0, 0}}
        };
        
        auto it = namedColors.find(colorStr);
        if (it != namedColors.end()) {
            result = it->second;
        }
    }
    
    return result;
}

std::string SerializeColor(const Color& color) {
    if (color.a < 255) {
        // Use rgba format if transparency
        // The alpha must not be written through LC_NUMERIC: std::to_string
        // renders 0.5 as "0,500000" on a comma-decimal desktop, and a comma
        // inside rgba() is the channel separator - the colour would read back
        // as a five-argument function, not as a transparent one.
        return "rgba(" + std::to_string(color.r) + "," +
               std::to_string(color.g) + "," +
               std::to_string(color.b) + "," +
               FormatFloatClassic(color.a / 255.0f) + ")";
    } else {
        // Use hex format for opaque colors
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02x%02x%02x", color.r, color.g, color.b);
        return std::string(hex);
    }
}

Matrix3x3 ParseTransformString(const std::string& transformStr) {
    Matrix3x3 result = Matrix3x3::Identity();
    
    // Parse transform functions
    std::regex transformRegex(R"((\w+)\s*\(([^)]+)\))");
    std::sregex_iterator it(transformStr.begin(), transformStr.end(), transformRegex);
    std::sregex_iterator end;
    
    while (it != end) {
        std::string func = (*it)[1];
        std::string params = (*it)[2];
        
        std::vector<float> values;
        std::istringstream iss(params);
        float val;
        while (iss >> val) {
            values.push_back(val);
            char comma;
            iss >> comma;  // Skip comma or space
        }
        
        if (func == "translate" && values.size() >= 1) {
            float tx = values[0];
            float ty = values.size() > 1 ? values[1] : 0;
            result = result * Matrix3x3::Translate(tx, ty);
        } else if (func == "scale" && values.size() >= 1) {
            float sx = values[0];
            float sy = values.size() > 1 ? values[1] : sx;
            result = result * Matrix3x3::Scale(sx, sy);
        } else if (func == "rotate" && values.size() >= 1) {
            result = result * Matrix3x3::RotateDegrees(values[0]);
        } else if (func == "skewX" && values.size() >= 1) {
            result = result * Matrix3x3::SkewX(values[0] * M_PI / 180.0f);
        } else if (func == "skewY" && values.size() >= 1) {
            result = result * Matrix3x3::SkewY(values[0] * M_PI / 180.0f);
        } else if (func == "matrix" && values.size() >= 6) {
            // SVG matrix(a,b,c,d,e,f) is column-major: x' = a*x + c*y + e.
            // FromValues(A,B,...) is row-major (x' = A*x + B*y + e), so b and
            // c swap places.  SerializeTransform writes the mirror image.
            result = result * Matrix3x3::FromValues(
                values[0], values[2], values[1],
                values[3], values[4], values[5]
            );
        }
        
        ++it;
    }
    
    return result;
}

std::string SerializeTransform(const Matrix3x3& transform) {
    // Check if it's identity
    bool isIdentity = true;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float expected = (i == j) ? 1.0f : 0.0f;
            if (std::abs(transform.m[i][j] - expected) > 0.0001f) {
                isIdentity = false;
                break;
            }
        }
    }
    
    if (isIdentity) {
        return "";
    }
    
    // Output as matrix
    return "matrix(" + 
           std::to_string(transform.m[0][0]) + "," +
           std::to_string(transform.m[1][0]) + "," +
           std::to_string(transform.m[0][1]) + "," +
           std::to_string(transform.m[1][1]) + "," +
           std::to_string(transform.m[0][2]) + "," +
           std::to_string(transform.m[1][2]) + ")";
}

Rect2Dd CalculateTextBounds(const std::vector<TextSpanData>& spans, const VectorTextStyle& style) {
    // Simplified implementation
    // In real implementation, this would use proper font metrics
    
    if (spans.empty()) {
        return Rect2Dd{0, 0, 0, 0};
    }
    
    float totalWidth = 0;
    float maxHeight = style.FontSize * style.LineHeight;
    
    for (const auto& span : spans) {
        // Approximate width calculation
        float charWidth = span.Style.FontSize * 0.6f;
        totalWidth += span.Text.length() * charWidth;
    }
    
    return Rect2Dd{0, 0, totalWidth, maxHeight};
}

std::vector<Point2Dd> PathToPolygon(const PathData& path, float tolerance = 0.0) {
    VectorPath tempPath;
    tempPath.Path = path;
    return tempPath.Flatten(tolerance);
}

PathData PolygonToPath(const std::vector<Point2Dd>& points, bool closed) {
    PathData result;
    
    if (points.empty()) {
        return result;
    }
    
    // MoveTo first point
    PathCommand moveCmd;
    moveCmd.Type = PathCommandType::MoveTo;
    moveCmd.Parameters = {static_cast<float>(points[0].x), static_cast<float>(points[0].y)};
    moveCmd.Relative = false;
    result.commands.push_back(moveCmd);
    
    // LineTo remaining points
    for (size_t i = 1; i < points.size(); i++) {
        PathCommand lineCmd;
        lineCmd.Type = PathCommandType::LineTo;
        lineCmd.Parameters = {static_cast<float>(points[i].x), static_cast<float>(points[i].y)};
        lineCmd.Relative = false;
        result.commands.push_back(lineCmd);
    }
    
    // Close path if requested
    if (closed) {
        PathCommand closeCmd;
        closeCmd.Type = PathCommandType::ClosePath;
        result.commands.push_back(closeCmd);
        result.Closed = true;
    }
    
    return result;
}

PathData SimplifyPath(const PathData& path, float tolerance) {
    // Douglas-Peucker algorithm for path simplification
    auto points = PathToPolygon(path, tolerance / 10);
    
    if (points.size() <= 2) {
        return path;  // Can't simplify further
    }
    
    // Find point with maximum distance from line
    std::function<std::vector<Point2Dd>(const std::vector<Point2Dd>&, float)> simplifySection = 
        [&](const std::vector<Point2Dd>& pts, float tol) -> std::vector<Point2Dd> {
        if (pts.size() <= 2) {
            return pts;
        }
        
        // Find point with max distance to line between first and last
        float maxDist = 0;
        size_t maxIndex = 0;
        
        Point2Dd start = pts.front();
        Point2Dd end = pts.back();
        
        for (size_t i = 1; i < pts.size() - 1; i++) {
            // Calculate perpendicular distance to line
            float dx = end.x - start.x;
            float dy = end.y - start.y;
            float len = std::sqrt(dx * dx + dy * dy);
            
            float dist = 0;
            if (len > 0) {
                dist = std::abs((pts[i].y - start.y) * dx - 
                               (pts[i].x - start.x) * dy) / len;
            }
            
            if (dist > maxDist) {
                maxDist = dist;
                maxIndex = i;
            }
        }
        
        // If max distance is greater than tolerance, recursively simplify
        if (maxDist > tol) {
            std::vector<Point2Dd> left(pts.begin(), pts.begin() + maxIndex + 1);
            std::vector<Point2Dd> right(pts.begin() + maxIndex, pts.end());
            
            auto simplifiedLeft = simplifySection(left, tol);
            auto simplifiedRight = simplifySection(right, tol);
            
            // Combine results
            std::vector<Point2Dd> result(simplifiedLeft.begin(), simplifiedLeft.end() - 1);
            result.insert(result.end(), simplifiedRight.begin(), simplifiedRight.end());
            
            return result;
        } else {
            // Return just the endpoints
            return {pts.front(), pts.back()};
        }
    };
    
    auto simplified = simplifySection(points, tolerance);
    return PolygonToPath(simplified, path.Closed);
}

bool IsPointInPath(const PathData& path, const Point2Dd& point, FillRule rule) {
    auto polygon = PathToPolygon(path);
    
    if (polygon.size() < 3) {
        return false;
    }
    
    // Ray casting algorithm
    int crossings = 0;
    
    for (size_t i = 0; i < polygon.size(); i++) {
        Point2Dd p1 = polygon[i];
        Point2Dd p2 = polygon[(i + 1) % polygon.size()];
        
        // Check if ray from point going right crosses edge
        if ((p1.y <= point.y && p2.y > point.y) ||
            (p1.y > point.y && p2.y <= point.y)) {
            
            float vt = (point.y - p1.y) / (p2.y - p1.y);
            float xIntersect = p1.x + vt * (p2.x - p1.x);
            
            if (point.x < xIntersect) {
                crossings++;
            }
        }
    }
    
    // Apply fill rule
    if (rule == FillRule::EvenOdd) {
        return (crossings % 2) != 0;
    } else {  // NonZero
        return crossings != 0;
    }
}

PathData OffsetPath(const PathData& path, float offset) {
    // Simplified offset implementation
    // Full implementation would handle complex cases like self-intersections
    
    auto points = PathToPolygon(path);
    if (points.size() < 2) {
        return path;
    }
    
    std::vector<Point2Dd> offsetPoints;
    
    for (size_t i = 0; i < points.size(); i++) {
        size_t prev = (i + points.size() - 1) % points.size();
        size_t next = (i + 1) % points.size();
        
        // Calculate normals
        Point2Dd v1 = {points[i].x - points[prev].x, 
                      points[i].y - points[prev].y};
        Point2Dd v2 = {points[next].x - points[i].x, 
                      points[next].y - points[i].y};
        
        // Normalize
        float len1 = std::sqrt(v1.x * v1.x + v1.y * v1.y);
        float len2 = std::sqrt(v2.x * v2.x + v2.y * v2.y);
        
        if (len1 > 0) {
            v1.x /= len1;
            v1.y /= len1;
        }
        if (len2 > 0) {
            v2.x /= len2;
            v2.y /= len2;
        }
        
        // Get perpendiculars
        Point2Dd n1 = {-v1.y, v1.x};
        Point2Dd n2 = {-v2.y, v2.x};
        
        // Average normal
        Point2Dd avgNormal = {(n1.x + n2.x) / 2, (n1.y + n2.y) / 2};
        float avgLen = std::sqrt(avgNormal.x * avgNormal.x + avgNormal.y * avgNormal.y);
        
        if (avgLen > 0) {
            avgNormal.x /= avgLen;
            avgNormal.y /= avgLen;
            
            // Offset point
            offsetPoints.push_back({
                points[i].x + avgNormal.x * offset,
                points[i].y + avgNormal.y * offset
            });
        } else {
            offsetPoints.push_back(points[i]);
        }
    }
    
    return PolygonToPath(offsetPoints, path.Closed);
}

PathData CombinePaths(const PathData& path1, const PathData& path2, bool union_op) {
    // Simplified implementation
    // Full implementation would use a proper polygon boolean operation library
    
    PathData result = path1;
    
    // For now, just append path2 commands
    for (const auto& cmd : path2.commands) {
        result.commands.push_back(cmd);
    }
    
    return result;
}

} // namespace VectorStorage
} // namespace UltraCanvas
