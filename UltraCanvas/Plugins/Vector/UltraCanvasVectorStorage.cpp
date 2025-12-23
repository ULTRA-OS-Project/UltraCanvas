// UltraCanvasVectorStorage.cpp
// Implementation of the Vector Graphics Storage System for UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorStorage.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <regex>
#include <numeric>

namespace UltraCanvas {
namespace VectorStorage {

// ===== MATRIX3X3 IMPLEMENTATION =====

Matrix3x3::Matrix3x3() {
    // Initialize as identity matrix
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}

Matrix3x3 Matrix3x3::Identity() {
    return Matrix3x3();
}

Matrix3x3 Matrix3x3::Translate(float tx, float ty) {
    Matrix3x3 result;
    result.m[0][2] = tx;
    result.m[1][2] = ty;
    return result;
}

Matrix3x3 Matrix3x3::Scale(float sx, float sy) {
    Matrix3x3 result;
    result.m[0][0] = sx;
    result.m[1][1] = sy;
    return result;
}

Matrix3x3 Matrix3x3::Rotate(float angle) {
    Matrix3x3 result;
    float c = std::cos(angle);
    float s = std::sin(angle);
    result.m[0][0] = c;
    result.m[0][1] = -s;
    result.m[1][0] = s;
    result.m[1][1] = c;
    return result;
}

Matrix3x3 Matrix3x3::RotateDegrees(float degrees) {
    return Rotate(degrees * M_PI / 180.0f);
}

Matrix3x3 Matrix3x3::SkewX(float angle) {
    Matrix3x3 result;
    result.m[0][1] = std::tan(angle);
    return result;
}

Matrix3x3 Matrix3x3::SkewY(float angle) {
    Matrix3x3 result;
    result.m[1][0] = std::tan(angle);
    return result;
}

Matrix3x3 Matrix3x3::FromValues(float a, float b, float c, float d, float e, float f) {
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

Point2Df Matrix3x3::Transform(const Point2Df& point) const {
    Point2Df result;
    result.x = m[0][0] * point.x + m[0][1] * point.y + m[0][2];
    result.y = m[1][0] * point.x + m[1][1] * point.y + m[1][2];
    return result;
}

Rect2Df Matrix3x3::Transform(const Rect2Df& rect) const {
    Point2Df corners[4] = {
        {rect.x, rect.y},
        {rect.x + rect.width, rect.y},
        {rect.x, rect.y + rect.height},
        {rect.x + rect.width, rect.y + rect.height}
    };
    
    for (int i = 0; i < 4; i++) {
        corners[i] = Transform(corners[i]);
    }
    
    float minX = corners[0].x, maxX = corners[0].x;
    float minY = corners[0].y, maxY = corners[0].y;
    
    for (int i = 1; i < 4; i++) {
        minX = std::min(minX, corners[i].x);
        maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxY = std::max(maxY, corners[i].y);
    }
    
    return Rect2Df{minX, minY, maxX - minX, maxY - minY};
}

float Matrix3x3::Determinant() const {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

Matrix3x3 Matrix3x3::Inverse() const {
    float det = Determinant();
    if (std::abs(det) < 1e-10f) {
        return Identity(); // Return identity if not invertible
    }
    
    Matrix3x3 result;
    float invDet = 1.0f / det;
    
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

void VectorStyle::Inherit(const VectorStyle& parent) {
    // Inherit properties that weren't explicitly set
    if (!Fill.has_value() && parent.Fill.has_value()) {
        Fill = parent.Fill;
    }
    if (!Stroke.has_value() && parent.Stroke.has_value()) {
        Stroke = parent.Stroke;
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

Point2Df VectorElement::LocalToGlobal(const Point2Df& point) const {
    return GetGlobalTransform().Transform(point);
}

Point2Df VectorElement::GlobalToLocal(const Point2Df& point) const {
    return GetGlobalTransform().Inverse().Transform(point);
}

// ===== BASIC SHAPE IMPLEMENTATIONS =====

// VectorRect
Rect2Df VectorRect::GetBoundingBox() const {
    Rect2Df bbox = Bounds;
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
Rect2Df VectorCircle::GetBoundingBox() const {
    Rect2Df bbox{
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
Rect2Df VectorEllipse::GetBoundingBox() const {
    Rect2Df bbox{
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
Rect2Df VectorLine::GetBoundingBox() const {
    float minX = std::min(Start.x, End.x);
    float minY = std::min(Start.y, End.y);
    float maxX = std::max(Start.x, End.x);
    float maxY = std::max(Start.y, End.y);
    
    Rect2Df bbox{minX, minY, maxX - minX, maxY - minY};
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
Rect2Df VectorPolyline::GetBoundingBox() const {
    if (Points.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    float minX = Points[0].x, maxX = Points[0].x;
    float minY = Points[0].y, maxY = Points[0].y;
    
    for (const auto& point : Points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    
    Rect2Df bbox{minX, minY, maxX - minX, maxY - minY};
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
Rect2Df VectorPolygon::GetBoundingBox() const {
    if (Points.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    float minX = Points[0].x, maxX = Points[0].x;
    float minY = Points[0].y, maxY = Points[0].y;
    
    for (const auto& point : Points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    
    Rect2Df bbox{minX, minY, maxX - minX, maxY - minY};
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

Rect2Df VectorPath::GetBoundingBox() const {
    if (!Path.BoundingBox.has_value()) {
        Path.BoundingBox = CalculatePathBounds(Path);
    }
    
    Rect2Df bbox = Path.BoundingBox.value();
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
    Path.Commands.push_back(cmd);
    Path.BoundingBox.reset();  // Invalidate cached bbox
    Path.Length.reset();
    Path.FlattenedPoints.reset();
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
    if (!Path.Length.has_value()) {
        auto points = Flatten();
        float length = 0;
        for (size_t i = 1; i < points.size(); i++) {
            float dx = points[i].x - points[i-1].x;
            float dy = points[i].y - points[i-1].y;
            length += std::sqrt(dx * dx + dy * dy);
        }
        Path.Length = length;
    }
    return Path.Length.value();
}

Point2Df VectorPath::GetPointAtLength(float length) const {
    auto points = Flatten();
    if (points.empty()) return Point2Df{0, 0};
    
    float currentLength = 0;
    for (size_t i = 1; i < points.size(); i++) {
        float dx = points[i].x - points[i-1].x;
        float dy = points[i].y - points[i-1].y;
        float segmentLength = std::sqrt(dx * dx + dy * dy);
        
        if (currentLength + segmentLength >= length) {
            float t = (length - currentLength) / segmentLength;
            return Point2Df{
                points[i-1].x + t * dx,
                points[i-1].y + t * dy
            };
        }
        currentLength += segmentLength;
    }
    
    return points.back();
}

float VectorPath::GetAngleAtLength(float length) const {
    auto points = Flatten();
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

std::vector<Point2Df> VectorPath::Flatten(float tolerance) const {
    if (!Path.FlattenedPoints.has_value()) {
        std::vector<Point2Df> result;
        Point2Df currentPoint{0, 0};
        Point2Df startPoint{0, 0};
        
        for (const auto& cmd : Path.Commands) {
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
                    Point2Df p0 = currentPoint;
                    Point2Df p1, p2, p3;
                    
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
                        
                        Point2Df point;
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
                    Point2Df p0 = currentPoint;
                    Point2Df p1, p2;
                    
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
                        
                        Point2Df point;
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
                    
                    Point2Df endPoint;
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
        
        Path.FlattenedPoints = result;
    }
    
    return Path.FlattenedPoints.value();
}

// ===== TEXT ELEMENTS IMPLEMENTATION =====

// VectorText
Rect2Df VectorText::GetBoundingBox() const {
    // Simplified bounding box calculation
    // In real implementation, this would use font metrics
    float width = 0;
    float height = BaseStyle.FontSize * 1.2f;
    
    for (const auto& span : Spans) {
        // Approximate width based on character count
        width += span.Text.length() * BaseStyle.FontSize * 0.6f;
    }
    
    Rect2Df bbox{Position.x, Position.y - BaseStyle.FontSize, width, height};
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
Rect2Df VectorTextPath::GetBoundingBox() const {
    // Would need the actual path to calculate proper bounds
    // For now, return empty bounds
    return Rect2Df{0, 0, 0, 0};
}

std::shared_ptr<VectorElement> VectorTextPath::Clone() const {
    auto clone = std::make_shared<VectorTextPath>(*this);
    clone->Parent.reset();
    return clone;
}


// ===== CONTAINER ELEMENTS IMPLEMENTATION =====

// VectorGroup
Rect2Df VectorGroup::GetBoundingBox() const {
    if (Children.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    Rect2Df bbox = Children[0]->GetBoundingBox();
    
    for (size_t i = 1; i < Children.size(); i++) {
        Rect2Df childBox = Children[i]->GetBoundingBox();
        
        float minX = std::min(bbox.x, childBox.x);
        float minY = std::min(bbox.y, childBox.y);
        float maxX = std::max(bbox.x + bbox.width, childBox.x + childBox.width);
        float maxY = std::max(bbox.y + bbox.height, childBox.y + childBox.height);
        
        bbox = Rect2Df{minX, minY, maxX - minX, maxY - minY};
    }
    
    if (Transform.has_value()) {
        bbox = Transform->Transform(bbox);
    }
    return bbox;
}

std::shared_ptr<VectorElement> VectorGroup::Clone() const {
    auto clone = std::make_shared<VectorGroup>(*this);
    clone->Parent.reset();
    
    // Deep clone children
    clone->Children.clear();
    for (const auto& child : Children) {
        auto childClone = child->Clone();
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
    
    // Deep clone children
    clone->Children.clear();
    for (const auto& child : Children) {
        auto childClone = child->Clone();
        clone->Children.push_back(childClone);
    }
    
    return clone;
}


// VectorUse
Rect2Df VectorUse::GetBoundingBox() const {
    Rect2Df bbox{Position.x, Position.y, Size.width, Size.height};
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
Rect2Df VectorImage::GetBoundingBox() const {
    Rect2Df bbox = Bounds;
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
    if (Data.Content) {
        Data.Content = std::dynamic_pointer_cast<VectorGroup>(Data.Content->Clone());
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
Rect2Df VectorClipPath::GetBoundingBox() const {
    if (Data.Elements.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    Rect2Df bbox = Data.Elements[0]->GetBoundingBox();
    
    for (size_t i = 1; i < Data.Elements.size(); i++) {
        Rect2Df elemBox = Data.Elements[i]->GetBoundingBox();
        
        float minX = std::min(bbox.x, elemBox.x);
        float minY = std::min(bbox.y, elemBox.y);
        float maxX = std::max(bbox.x + bbox.width, elemBox.x + elemBox.width);
        float maxY = std::max(bbox.y + bbox.height, elemBox.y + elemBox.height);
        
        bbox = Rect2Df{minX, minY, maxX - minX, maxY - minY};
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
    if (Data.Content) {
        Data.Content = std::dynamic_pointer_cast<VectorGroup>(Data.Content->Clone());
    }
    
    return clone;
}


// VectorLayer
std::shared_ptr<VectorElement> VectorLayer::Clone() const {
    auto clone = std::make_shared<VectorLayer>(*this);
    clone->Parent.reset();
    
    // Deep clone children
    clone->Children.clear();
    for (const auto& child : Children) {
        auto childClone = child->Clone();
        clone->Children.push_back(childClone);
    }
    
    return clone;
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
            if (elem->Class == className ||
                std::find(elem->ClassList.begin(), elem->ClassList.end(), className) != elem->ClassList.end()) {
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

Rect2Df VectorDocument::GetBoundingBox() const {
    if (Layers.empty()) {
        return Rect2Df{0, 0, Size.width, Size.height};
    }
    
    Rect2Df bbox = Layers[0]->GetBoundingBox();
    
    for (size_t i = 1; i < Layers.size(); i++) {
        Rect2Df layerBox = Layers[i]->GetBoundingBox();
        
        float minX = std::min(bbox.x, layerBox.x);
        float minY = std::min(bbox.y, layerBox.y);
        float maxX = std::max(bbox.x + bbox.width, layerBox.x + layerBox.width);
        float maxY = std::max(bbox.y + bbox.height, layerBox.y + layerBox.height);
        
        bbox = Rect2Df{minX, minY, maxX - minX, maxY - minY};
    }
    
    return bbox;
}

void VectorDocument::FitToContent(float padding) {
    Rect2Df bbox = GetBoundingBox();
    
    ViewBox = Rect2Df{
        bbox.x - padding,
        bbox.y - padding,
        bbox.width + 2 * padding,
        bbox.height + 2 * padding
    };
    
    Size = Size2Df{ViewBox.width, ViewBox.height};
}

void VectorDocument::Clear() {
    Layers.clear();
    Definitions.clear();
    NamedStyles.clear();
    StyleSheet.clear();
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
        
        result.Commands.push_back(pathCmd);
    }
    
    return result;
}

std::string SerializePathData(const PathData& path) {
    std::ostringstream oss;
    
    for (const auto& cmd : path.Commands) {
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
            result.a = static_cast<uint8_t>(std::stof(match[4]) * 255);
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
        return "rgba(" + std::to_string(color.r) + "," + 
               std::to_string(color.g) + "," + 
               std::to_string(color.b) + "," + 
               std::to_string(color.a / 255.0f) + ")";
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
            result = result * Matrix3x3::FromValues(
                values[0], values[1], values[2], 
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

Rect2Df CalculatePathBounds(const PathData& path) {
    if (path.Commands.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    // Create temporary path to flatten
    VectorPath tempPath;
    tempPath.Path = path;
    auto points = tempPath.Flatten();
    
    if (points.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    float minX = points[0].x, maxX = points[0].x;
    float minY = points[0].y, maxY = points[0].y;
    
    for (const auto& point : points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    
    return Rect2Df{minX, minY, maxX - minX, maxY - minY};
}

Rect2Df CalculateTextBounds(const std::vector<TextSpanData>& spans, const TextStyle& style) {
    // Simplified implementation
    // In real implementation, this would use proper font metrics
    
    if (spans.empty()) {
        return Rect2Df{0, 0, 0, 0};
    }
    
    float totalWidth = 0;
    float maxHeight = style.FontSize * style.LineHeight;
    
    for (const auto& span : spans) {
        // Approximate width calculation
        float charWidth = span.Style.FontSize * 0.6f;
        totalWidth += span.Text.length() * charWidth;
    }
    
    return Rect2Df{0, 0, totalWidth, maxHeight};
}

std::vector<Point2Df> PathToPolygon(const PathData& path, float tolerance = 0.0) {
    VectorPath tempPath;
    tempPath.Path = path;
    return tempPath.Flatten(tolerance);
}

PathData PolygonToPath(const std::vector<Point2Df>& points, bool closed) {
    PathData result;
    
    if (points.empty()) {
        return result;
    }
    
    // MoveTo first point
    PathCommand moveCmd;
    moveCmd.Type = PathCommandType::MoveTo;
    moveCmd.Parameters = {points[0].x, points[0].y};
    moveCmd.Relative = false;
    result.Commands.push_back(moveCmd);
    
    // LineTo remaining points
    for (size_t i = 1; i < points.size(); i++) {
        PathCommand lineCmd;
        lineCmd.Type = PathCommandType::LineTo;
        lineCmd.Parameters = {points[i].x, points[i].y};
        lineCmd.Relative = false;
        result.Commands.push_back(lineCmd);
    }
    
    // Close path if requested
    if (closed) {
        PathCommand closeCmd;
        closeCmd.Type = PathCommandType::ClosePath;
        result.Commands.push_back(closeCmd);
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
    std::function<std::vector<Point2Df>(const std::vector<Point2Df>&, float)> simplifySection = 
        [&](const std::vector<Point2Df>& pts, float tol) -> std::vector<Point2Df> {
        if (pts.size() <= 2) {
            return pts;
        }
        
        // Find point with max distance to line between first and last
        float maxDist = 0;
        size_t maxIndex = 0;
        
        Point2Df start = pts.front();
        Point2Df end = pts.back();
        
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
            std::vector<Point2Df> left(pts.begin(), pts.begin() + maxIndex + 1);
            std::vector<Point2Df> right(pts.begin() + maxIndex, pts.end());
            
            auto simplifiedLeft = simplifySection(left, tol);
            auto simplifiedRight = simplifySection(right, tol);
            
            // Combine results
            std::vector<Point2Df> result(simplifiedLeft.begin(), simplifiedLeft.end() - 1);
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

bool IsPointInPath(const PathData& path, const Point2Df& point, FillRule rule) {
    auto polygon = PathToPolygon(path);
    
    if (polygon.size() < 3) {
        return false;
    }
    
    // Ray casting algorithm
    int crossings = 0;
    
    for (size_t i = 0; i < polygon.size(); i++) {
        Point2Df p1 = polygon[i];
        Point2Df p2 = polygon[(i + 1) % polygon.size()];
        
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
    
    std::vector<Point2Df> offsetPoints;
    
    for (size_t i = 0; i < points.size(); i++) {
        size_t prev = (i + points.size() - 1) % points.size();
        size_t next = (i + 1) % points.size();
        
        // Calculate normals
        Point2Df v1 = {points[i].x - points[prev].x, 
                      points[i].y - points[prev].y};
        Point2Df v2 = {points[next].x - points[i].x, 
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
        Point2Df n1 = {-v1.y, v1.x};
        Point2Df n2 = {-v2.y, v2.x};
        
        // Average normal
        Point2Df avgNormal = {(n1.x + n2.x) / 2, (n1.y + n2.y) / 2};
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
    for (const auto& cmd : path2.Commands) {
        result.Commands.push_back(cmd);
    }
    
    return result;
}

} // namespace VectorStorage
} // namespace UltraCanvas
