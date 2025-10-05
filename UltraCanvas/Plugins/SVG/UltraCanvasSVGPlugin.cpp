// UltraCanvasSVGPlugin.cpp
// Lightweight SVG plugin implementation leveraging existing UltraCanvas infrastructure
// Version: 1.0.0
// Last Modified: 2025-07-17
// Author: UltraCanvas Framework

#include "Plugins/SVG/UltraCanvasSVGPlugin.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

// Updated ParsePath to return path commands instead of points

    void SimpleSVGParser::ParsePathCommands(const std::string& pathStr, std::vector<SVGPathCommand>& commands) {
        if (pathStr.empty()) return;

        std::istringstream iss(pathStr);

        auto skipCommaWhitespace = [&iss]() {
            while (iss.peek() == ',' || std::isspace(iss.peek())) {
                iss.ignore();
            }
        };

        auto readNumber = [&iss, &skipCommaWhitespace]() -> float {
            skipCommaWhitespace();
            float value = 0;
            if (!(iss >> value)) {
                return 0;
            }
            skipCommaWhitespace();
            return value;
        };

        char command;
        while (iss >> command) {
            skipCommaWhitespace();
            SVGPathCommand cmd;
            cmd.type = command;

            // Determine how many parameters each command needs
            switch (command) {
                case 'M': case 'm': // MoveTo: x y
                case 'L': case 'l': // LineTo: x y
                case 'T': case 't': // Smooth quadratic Bezier: x y
                    // Read pairs of coordinates
                    do {
                        float x = readNumber();
                        if (iss.fail()) break;
                        float y = readNumber();
                        if (iss.fail()) break;
                        cmd.params.push_back(x);
                        cmd.params.push_back(y);
                    } while (iss.peek() != EOF && !std::isalpha(iss.peek()));
                    break;

                case 'H': case 'h': // Horizontal line: x
                case 'V': case 'v': // Vertical line: y
                    // Read single coordinates
                    do {
                        float coord = readNumber();
                        if (iss.fail()) break;
                        cmd.params.push_back(coord);
                    } while (iss.peek() != EOF && !std::isalpha(iss.peek()));
                    break;

                case 'C': case 'c': // Cubic Bezier: x1 y1 x2 y2 x y
                    // Read sets of 6 coordinates
                    do {
                        for (int i = 0; i < 6; i++) {
                            float coord = readNumber();
                            if (iss.fail()) break;
                            cmd.params.push_back(coord);
                        }
                        if (iss.fail()) break;
                    } while (iss.peek() != EOF && !std::isalpha(iss.peek()));
                    break;

                case 'S': case 's': // Smooth cubic Bezier: x2 y2 x y
                case 'Q': case 'q': // Quadratic Bezier: x1 y1 x y
                    // Read sets of 4 coordinates
                    do {
                        for (int i = 0; i < 4; i++) {
                            float coord = readNumber();
                            if (iss.fail()) break;
                            cmd.params.push_back(coord);
                        }
                        if (iss.fail()) break;
                    } while (iss.peek() != EOF && !std::isalpha(iss.peek()));
                    break;

                case 'A': case 'a': // Arc: rx ry x-axis-rotation large-arc-flag sweep-flag x y
                    // Read sets of 7 parameters
                    do {
                        for (int i = 0; i < 7; i++) {
                            float param = readNumber();
                            if (iss.fail()) break;
                            cmd.params.push_back(param);
                        }
                        if (iss.fail()) break;
                    } while (iss.peek() != EOF && !std::isalpha(iss.peek()));
                    break;

                case 'Z': case 'z': // ClosePath: no parameters
                    // No parameters needed
                    break;

                default:
                    // Unknown command, skip until next letter
                    char c;
                    while (iss >> c && !std::isalpha(c)) {
                        // Skip
                    }
                    if (std::isalpha(c)) {
                        iss.putback(c);
                    }
                    continue;
            }

            if (!cmd.params.empty() || command == 'Z' || command == 'z') {
                commands.push_back(cmd);
            }
        }

        return;
    }

// New method to execute path commands using native Path functions

// ===== COLOR PARSING UTILITIES =====
    Color SVGAttributes::GetColor(const std::string& name, const Color& defaultValue) const {
        std::string value = Get(name);
        if (value.empty()) return defaultValue;
        return SimpleSVGParser::ParseColor(value);
    }

    Color SimpleSVGParser::ParseColor(const std::string& colorStr) {
        if (colorStr.empty() || colorStr == "none") {
            return Colors::Transparent;
        }

        // Named colors
        static const std::unordered_map<std::string, Color> namedColors = {
                {"black", Colors::Black}, {"white", Colors::White}, {"red", Colors::Red},
                {"green", Colors::Green}, {"blue", Colors::Blue}, {"yellow", Colors::Yellow},
                {"cyan", Colors::Cyan}, {"magenta", Colors::Magenta}, {"gray", Colors::Gray},
                {"orange", Color(255, 165, 0)}, {"purple", Color(128, 0, 128)},
                {"brown", Color(165, 42, 42)}, {"pink", Color(255, 192, 203)},
                {"transparent", Colors::Transparent}
        };

        std::string lower = colorStr;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        auto it = namedColors.find(lower);
        if (it != namedColors.end()) {
            return it->second;
        }

        // Hex colors
        if (colorStr[0] == '#') {
            std::string hex = colorStr.substr(1);
            if (hex.length() == 3) {
                // Expand shorthand: #RGB -> #RRGGBB
                hex = std::string(1, hex[0]) + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
            }

            if (hex.length() == 6) {
                try {
                    int r = std::stoi(hex.substr(0, 2), nullptr, 16);
                    int g = std::stoi(hex.substr(2, 2), nullptr, 16);
                    int b = std::stoi(hex.substr(4, 2), nullptr, 16);
                    return Color(r, g, b);
                } catch (...) {
                    return Colors::Black;
                }
            }
        }

        // RGB functions: rgb(255, 128, 0)
        if (colorStr.substr(0, 4) == "rgb(") {
            size_t start = 4;
            size_t end = colorStr.find(')', start);
            if (end != std::string::npos) {
                std::string params = colorStr.substr(start, end - start);
                std::replace(params.begin(), params.end(), ',', ' ');

                std::istringstream iss(params);
                int r, g, b;
                if (iss >> r >> g >> b) {
                    return Color(r, g, b);
                }
            }
        }

        return Colors::Black; // Default fallback
    }

    float SimpleSVGParser::ParseLength(const std::string& lengthStr, float referenceValue) {
        if (lengthStr.empty()) return 0.0f;

        // Remove whitespace
        std::string clean = lengthStr;
        clean.erase(std::remove_if(clean.begin(), clean.end(), ::isspace), clean.end());

        // Check for percentage
        if (clean.back() == '%') {
            float percent = std::stof(clean.substr(0, clean.length() - 1));
            return (percent / 100.0f) * referenceValue;
        }

        // Remove unit suffixes (px, pt, em, etc.) - just use the number
        std::string number;
        for (char c : clean) {
            if (std::isdigit(c) || c == '.' || c == '-') {
                number += c;
            } else {
                break; // Stop at first non-numeric character
            }
        }

        return number.empty() ? 0.0f : std::stof(number);
    }

    std::vector<Point2Df> SimpleSVGParser::ParsePoints(const std::string& pointsStr) {
        std::vector<Point2Df> points;
        if (pointsStr.empty()) return points;

        std::string clean = pointsStr;
        std::replace(clean.begin(), clean.end(), ',', ' ');

        std::istringstream iss(clean);
        float x, y;
        while (iss >> x >> y) {
            points.emplace_back(x, y);
        }

        return points;
    }

// ===== SIMPLE SVG PARSER IMPLEMENTATION =====
    void SimpleSVGParser::SkipWhitespace() {
        while (position < content.length() && std::isspace(content[position])) {
            position++;
        }
    }

    std::string SimpleSVGParser::ReadUntil(char delimiter) {
        std::string result;
        while (position < content.length() && content[position] != delimiter) {
            result += content[position++];
        }
        return result;
    }

    std::string SimpleSVGParser::ReadTagName() {
        std::string tagName;
        while (position < content.length() &&
               (std::isalnum(content[position]) || content[position] == '-' || content[position] == ':')) {
            tagName += content[position++];
        }
        return tagName;
    }

    SVGAttributes SimpleSVGParser::ParseAttributes() {
        SVGAttributes attrs;
        SkipWhitespace();

        while (position < content.length() && content[position] != '>' && content[position] != '/') {
            // Read attribute name
            std::string name = ReadTagName();
            if (name.empty()) break;

            SkipWhitespace();
            if (position < content.length() && content[position] == '=') {
                position++; // Skip '='
                SkipWhitespace();

                // Read attribute value
                std::string value;
                if (position < content.length() && (content[position] == '"' || content[position] == '\'')) {
                    char quote = content[position++];
                    value = ReadUntil(quote);
                    if (position < content.length()) position++; // Skip closing quote
                } else {
                    // Unquoted value (read until whitespace or >)
                    while (position < content.length() && !std::isspace(content[position]) &&
                           content[position] != '>' && content[position] != '/') {
                        value += content[position++];
                    }
                }

                attrs.attrs[name] = value;
            }

            SkipWhitespace();
        }

        return attrs;
    }

    std::shared_ptr<SVGElement> SimpleSVGParser::ParseElement() {
        SkipWhitespace();

        if (position >= content.length() || content[position] != '<') {
            return nullptr;
        }

        position++; // Skip '<'

        // Check for comment or other special elements
        if (position < content.length() && content[position] == '!') {
            // Skip comments and other special elements
            ReadUntil('>');
            if (position < content.length()) position++; // Skip '>'
            return ParseElement(); // Try next element
        }

        std::string tagName = ReadTagName();
        if (tagName.empty()) return nullptr;

        std::shared_ptr<SVGElement> element;
        if (tagName == "path") {
            auto elem = std::make_shared<SVGPathElement>(tagName);
            elem->attributes = ParseAttributes();
            ParsePathCommands(elem->attributes.Get("d"), elem->pathCommands);
            element = elem;
        } else {
            element = std::make_shared<SVGElement>(tagName);
            element->attributes = ParseAttributes();
        }
        SkipWhitespace();

        // Check for self-closing tag
        if (position < content.length() && content[position] == '/') {
            position++; // Skip '/'
            SkipWhitespace();
            if (position < content.length() && content[position] == '>') {
                position++; // Skip '>'
            }
            return element;
        }

        if (position < content.length() && content[position] == '>') {
            position++; // Skip '>'

            // Parse content and children
            std::string closingTag = "</" + tagName;
            size_t contentStart = position;
            size_t closingPos = content.find(closingTag, position);

            if (closingPos != std::string::npos) {
                std::string elementContent = content.substr(contentStart, closingPos - contentStart);

                // Simple text content detection
                if (elementContent.find('<') == std::string::npos) {
                    element->textContent = elementContent;
                } else {
                    // Parse child elements
                    SimpleSVGParser childParser;
                    childParser.content = elementContent;
                    childParser.position = 0;

                    while (childParser.position < childParser.content.length()) {
                        auto child = childParser.ParseElement();
                        if (child) {
                            element->children.push_back(child);
                        } else {
                            childParser.position++;
                        }
                    }
                }

                position = closingPos + closingTag.length();
                if (position < content.length() && content[position] == '>') {
                    position++; // Skip '>'
                }
            }
        }

        return element;
    }

    std::shared_ptr<SVGDocument> SimpleSVGParser::Parse(const std::string& svgContent) {
        content = svgContent;
        position = 0;

        auto document = std::make_shared<SVGDocument>();

        // Find and parse the root <svg> element
        size_t svgStart = content.find("<svg");
        if (svgStart == std::string::npos) {
            return nullptr;
        }

        position = svgStart;
        document->root = ParseElement();

        if (document->root) {
            // Parse document-level attributes
            std::string widthStr = document->root->attributes.Get("width");
            std::string heightStr = document->root->attributes.Get("height");
            std::string viewBoxStr = document->root->attributes.Get("viewBox");

            document->width = widthStr.empty() ? 100.0f : ParseLength(widthStr);
            document->height = heightStr.empty() ? 100.0f : ParseLength(heightStr);

            if (!viewBoxStr.empty()) {
                std::istringstream iss(viewBoxStr);
                float x, y, w, h;
                if (iss >> x >> y >> w >> h) {
                    document->viewBox = Rect2D(x, y, w, h);
                    document->hasViewBox = true;
                }
            }
        }

        return document;
    }

// ===== SVG ELEMENT RENDERER IMPLEMENTATION =====
    void SVGElementRenderer::ApplyStrokeStyles(const SVGElement& element) {
        // Parse and apply fill
        // Parse and apply stroke
        std::string stroke = element.attributes.Get("stroke", "none");
        if (stroke != "none") {
            Color strokeColor = SimpleSVGParser::ParseColor(stroke);
            float strokeWidth = element.attributes.GetFloat("stroke-width", 1.0f);

            ctx->PaintWithColor(strokeColor);
            ctx->SetStrokeWidth(strokeWidth);
        }

        // Apply opacity
        float opacity = element.attributes.GetFloat("opacity", 1.0f);
        if (opacity < 1.0f) {
            ctx->SetAlpha(opacity);
        }
    }

    void SVGElementRenderer::ApplyFillStyles(const SVGElement& element) {
        // Parse and apply fill
        std::string fill = element.attributes.Get("fill", "black");
        if (fill != "none") {
            Color fillColor = SimpleSVGParser::ParseColor(fill);
            ctx->PaintWithColor(fillColor);
        }

        // Apply opacity
        float opacity = element.attributes.GetFloat("opacity", 1.0f);
        if (opacity < 1.0f) {
            ctx->SetAlpha(opacity);
        }
    }

    void SVGElementRenderer::RenderRect(const SVGElement& element) {
        float x = element.attributes.GetFloat("x", 0.0f);
        float y = element.attributes.GetFloat("y", 0.0f);
        float width = element.attributes.GetFloat("width", 0.0f);
        float height = element.attributes.GetFloat("height", 0.0f);
        float rx = element.attributes.GetFloat("rx", 0.0f);
        float ry = element.attributes.GetFloat("ry", 0.0f);

        if (width <= 0 || height <= 0) return;

        Rect2Df rect(x, y, width, height);

        // Use existing UltraCanvas functions
        if (element.attributes.Get("fill", "black") != "none") {
            ApplyFillStyles(element);
            if (rx > 0 || ry > 0) {
                ctx->FillRoundedRectangle(rect, std::max(rx, ry));
            } else {
                ctx->FillRectangle(rect);
            }
        }

        if (element.attributes.Get("stroke", "none") != "none") {
            ApplyStrokeStyles(element);
            if (rx > 0 || ry > 0) {
                ctx->DrawRoundedRectangle(rect, std::max(rx, ry));
            } else {
                ctx->DrawRectangle(rect);
            }
        }
    }

    void SVGElementRenderer::RenderCircle(const SVGElement& element) {
        float cx = element.attributes.GetFloat("cx", 0.0f);
        float cy = element.attributes.GetFloat("cy", 0.0f);
        float r = element.attributes.GetFloat("r", 0.0f);

        if (r <= 0) return;

        Point2Df center(cx, cy);

        ApplyFillStyles(element);

        // Use existing UltraCanvas functions
        if (element.attributes.Get("fill", "black") != "none") {
            ctx->FillCircle(center, r);
        }

        if (element.attributes.Get("stroke", "none") != "none") {
            ctx->DrawCircle(center, r);
        }
    }

    void SVGElementRenderer::RenderEllipse(const SVGElement& element) {
        float cx = element.attributes.GetFloat("cx", 0.0f);
        float cy = element.attributes.GetFloat("cy", 0.0f);
        float rx = element.attributes.GetFloat("rx", 0.0f);
        float ry = element.attributes.GetFloat("ry", 0.0f);

        if (rx <= 0 || ry <= 0) return;

        Rect2Df ellipseRect(cx - rx, cy - ry, rx * 2, ry * 2);

        ApplyFillStyles(element);

        // Use existing UltraCanvas functions
        if (element.attributes.Get("fill", "black") != "none") {
            ctx->FillEllipse(ellipseRect);
        }

        if (element.attributes.Get("stroke", "none") != "none") {
            ctx->DrawEllipse(ellipseRect);
        }
    }

    void SVGElementRenderer::RenderLine(const SVGElement& element) {
        float x1 = element.attributes.GetFloat("x1", 0.0f);
        float y1 = element.attributes.GetFloat("y1", 0.0f);
        float x2 = element.attributes.GetFloat("x2", 0.0f);
        float y2 = element.attributes.GetFloat("y2", 0.0f);

        Point2Df start(x1, y1);
        Point2Df end(x2, y2);

        ApplyFillStyles(element);

        // Use existing UltraCanvas function
        ctx->DrawLine(start, end);
    }

    void SVGElementRenderer::RenderPolyline(const SVGElement& element) {
        std::string pointsStr = element.attributes.Get("points");
        if (pointsStr.empty()) return;

        std::vector<Point2Df> points = SimpleSVGParser::ParsePoints(pointsStr);
        if (points.size() < 2) return;

        ApplyFillStyles(element);

        // Use existing UltraCanvas drawing - draw as connected lines
        for (size_t i = 0; i < points.size() - 1; ++i) {
            ctx->DrawLine(points[i], points[i + 1]);
        }
    }

    void SVGElementRenderer::RenderPolygon(const SVGElement& element) {
        std::string pointsStr = element.attributes.Get("points");
        if (pointsStr.empty()) return;

        std::vector<Point2Df> points = SimpleSVGParser::ParsePoints(pointsStr);
        if (points.size() < 3) return;

        ApplyFillStyles(element);

        // Use existing UltraCanvas polygon function from UltraCanvasDrawingSurface
        if (element.attributes.Get("fill", "black") != "none") {
            // Use the existing DrawPolygon with fill from UltraCanvasDrawingSurface
            // For now, simulate with individual triangles from center
            Point2Df center(0, 0);
            for (const Point2Df& p : points) {
                center.x += p.x; center.y += p.y;
            }
            center.x /= points.size(); center.y /= points.size();

            for (size_t i = 0; i < points.size(); ++i) {
                size_t next = (i + 1) % points.size();
                // Create triangle: center -> point[i] -> point[next]
                std::vector<Point2Df> triangle = {center, points[i], points[next]};
                // DrawPolygon(triangle, true); // Would use if available
            }
        }

        if (element.attributes.Get("stroke", "none") != "none") {
            // Draw outline
            for (size_t i = 0; i < points.size(); ++i) {
                size_t next = (i + 1) % points.size();
                ctx->DrawLine(points[i], points[next]);
            }
        }
    }

    void SVGElementRenderer::ExecutePathCommands(IRenderContext* ctx, const std::vector<SVGPathCommand>& commands) {
        if (!ctx || commands.empty()) return;

        Point2Df currentPoint(0, 0);
        Point2Df startPoint(0, 0);
        Point2Df lastControlPoint(0, 0);
        char lastCommand = 0;

        for (const auto& cmd : commands) {
            size_t paramIndex = 0;

            switch (cmd.type) {
                case 'M': // Move to absolute
                    while (paramIndex + 1 < cmd.params.size()) {
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];
                        ctx->MoveTo(x, y);
                        currentPoint.x = x;
                        currentPoint.y = y;
                        startPoint = currentPoint;

                        // Subsequent pairs are implicit LineTo
                        while (paramIndex + 1 < cmd.params.size()) {
                            x = cmd.params[paramIndex++];
                            y = cmd.params[paramIndex++];
                            ctx->LineTo(x, y);
                            currentPoint.x = x;
                            currentPoint.y = y;
                        }
                    }
                    break;

                case 'm': // Move to relative
                    while (paramIndex + 1 < cmd.params.size()) {
                        float dx = cmd.params[paramIndex++];
                        float dy = cmd.params[paramIndex++];

                        if (paramIndex == 2) {
                            // First move is relative to current point
                            ctx->RelMoveTo(dx, dy);
                        } else {
                            // Subsequent moves are implicit relative LineTo
                            ctx->RelLineTo(dx, dy);
                        }
                        currentPoint.x += dx;
                        currentPoint.y += dy;

                        if (paramIndex == 2) {
                            startPoint = currentPoint;
                        }
                    }
                    break;

                case 'L': // Line to absolute
                    while (paramIndex + 1 < cmd.params.size()) {
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];
                        ctx->LineTo(x, y);
                        currentPoint.x = x;
                        currentPoint.y = y;
                    }
                    break;

                case 'l': // Line to relative
                    while (paramIndex + 1 < cmd.params.size()) {
                        float dx = cmd.params[paramIndex++];
                        float dy = cmd.params[paramIndex++];
                        ctx->RelLineTo(dx, dy);
                        currentPoint.x += dx;
                        currentPoint.y += dy;
                    }
                    break;

                case 'H': // Horizontal line absolute
                    while (paramIndex < cmd.params.size()) {
                        float x = cmd.params[paramIndex++];
                        ctx->LineTo(x, currentPoint.y);
                        currentPoint.x = x;
                    }
                    break;

                case 'h': // Horizontal line relative
                    while (paramIndex < cmd.params.size()) {
                        float dx = cmd.params[paramIndex++];
                        ctx->RelLineTo(dx, 0);
                        currentPoint.x += dx;
                    }
                    break;

                case 'V': // Vertical line absolute
                    while (paramIndex < cmd.params.size()) {
                        float y = cmd.params[paramIndex++];
                        ctx->LineTo(currentPoint.x, y);
                        currentPoint.y = y;
                    }
                    break;

                case 'v': // Vertical line relative
                    while (paramIndex < cmd.params.size()) {
                        float dy = cmd.params[paramIndex++];
                        ctx->RelLineTo(0, dy);
                        currentPoint.y += dy;
                    }
                    break;

                case 'C': // Cubic Bezier curve absolute
                    while (paramIndex + 5 < cmd.params.size()) {
                        float x1 = cmd.params[paramIndex++];
                        float y1 = cmd.params[paramIndex++];
                        float x2 = cmd.params[paramIndex++];
                        float y2 = cmd.params[paramIndex++];
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];

                        ctx->BezierCurveTo(x1, y1, x2, y2, x, y);
                        lastControlPoint.x = x2;
                        lastControlPoint.y = y2;
                        currentPoint.x = x;
                        currentPoint.y = y;
                    }
                    break;

                case 'c': // Cubic Bezier curve relative
                    while (paramIndex + 5 < cmd.params.size()) {
                        float dx1 = cmd.params[paramIndex++];
                        float dy1 = cmd.params[paramIndex++];
                        float dx2 = cmd.params[paramIndex++];
                        float dy2 = cmd.params[paramIndex++];
                        float dx = cmd.params[paramIndex++];
                        float dy = cmd.params[paramIndex++];

                        ctx->RelBezierCurveTo(dx1, dy1, dx2, dy2, dx, dy);
                        lastControlPoint.x = currentPoint.x + dx2;
                        lastControlPoint.y = currentPoint.y + dy2;
                        currentPoint.x += dx;
                        currentPoint.y += dy;
                    }
                    break;

                case 'S': // Smooth cubic Bezier absolute
                    while (paramIndex + 3 < cmd.params.size()) {
                        float x2 = cmd.params[paramIndex++];
                        float y2 = cmd.params[paramIndex++];
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];

                        // Calculate reflected control point
                        float x1, y1;
                        if (lastCommand == 'C' || lastCommand == 'c' ||
                            lastCommand == 'S' || lastCommand == 's') {
                            x1 = 2 * currentPoint.x - lastControlPoint.x;
                            y1 = 2 * currentPoint.y - lastControlPoint.y;
                        } else {
                            x1 = currentPoint.x;
                            y1 = currentPoint.y;
                        }

                        ctx->BezierCurveTo(x1, y1, x2, y2, x, y);
                        lastControlPoint.x = x2;
                        lastControlPoint.y = y2;
                        currentPoint.x = x;
                        currentPoint.y = y;
                    }
                    break;

                case 's': // Smooth cubic Bezier relative
                    while (paramIndex + 3 < cmd.params.size()) {
                        float dx2 = cmd.params[paramIndex++];
                        float dy2 = cmd.params[paramIndex++];
                        float dx = cmd.params[paramIndex++];
                        float dy = cmd.params[paramIndex++];

                        // Calculate reflected control point (relative)
                        float dx1, dy1;
                        if (lastCommand == 'C' || lastCommand == 'c' ||
                            lastCommand == 'S' || lastCommand == 's') {
                            dx1 = currentPoint.x - lastControlPoint.x;
                            dy1 = currentPoint.y - lastControlPoint.y;
                        } else {
                            dx1 = 0;
                            dy1 = 0;
                        }

                        ctx->RelBezierCurveTo(dx1, dy1, dx2, dy2, dx, dy);
                        lastControlPoint.x = currentPoint.x + dx2;
                        lastControlPoint.y = currentPoint.y + dy2;
                        currentPoint.x += dx;
                        currentPoint.y += dy;
                    }
                    break;

                case 'Q': // Quadratic Bezier curve absolute
                    while (paramIndex + 3 < cmd.params.size()) {
                        float x1 = cmd.params[paramIndex++];
                        float y1 = cmd.params[paramIndex++];
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];

                        // Convert quadratic to cubic Bezier
                        float cx1 = currentPoint.x + 2.0f/3.0f * (x1 - currentPoint.x);
                        float cy1 = currentPoint.y + 2.0f/3.0f * (y1 - currentPoint.y);
                        float cx2 = x + 2.0f/3.0f * (x1 - x);
                        float cy2 = y + 2.0f/3.0f * (y1 - y);

                        ctx->BezierCurveTo(cx1, cy1, cx2, cy2, x, y);
                        lastControlPoint.x = x1;
                        lastControlPoint.y = y1;
                        currentPoint.x = x;
                        currentPoint.y = y;
                    }
                    break;

                case 'q': // Quadratic Bezier curve relative
                    while (paramIndex + 3 < cmd.params.size()) {
                        float dx1 = cmd.params[paramIndex++];
                        float dy1 = cmd.params[paramIndex++];
                        float dx = cmd.params[paramIndex++];
                        float dy = cmd.params[paramIndex++];

                        // Convert quadratic to cubic Bezier (relative)
                        float cx1 = 2.0f/3.0f * dx1;
                        float cy1 = 2.0f/3.0f * dy1;
                        float cx2 = dx + 2.0f/3.0f * (dx1 - dx);
                        float cy2 = dy + 2.0f/3.0f * (dy1 - dy);

                        ctx->RelBezierCurveTo(cx1, cy1, cx2, cy2, dx, dy);
                        lastControlPoint.x = currentPoint.x + dx1;
                        lastControlPoint.y = currentPoint.y + dy1;
                        currentPoint.x += dx;
                        currentPoint.y += dy;
                    }
                    break;

                case 'T': // Smooth quadratic Bezier absolute
                    while (paramIndex + 1 < cmd.params.size()) {
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];

                        // Calculate reflected control point
                        float x1, y1;
                        if (lastCommand == 'Q' || lastCommand == 'q' ||
                            lastCommand == 'T' || lastCommand == 't') {
                            x1 = 2 * currentPoint.x - lastControlPoint.x;
                            y1 = 2 * currentPoint.y - lastControlPoint.y;
                        } else {
                            x1 = currentPoint.x;
                            y1 = currentPoint.y;
                        }

                        // Convert quadratic to cubic Bezier
                        float cx1 = currentPoint.x + 2.0f/3.0f * (x1 - currentPoint.x);
                        float cy1 = currentPoint.y + 2.0f/3.0f * (y1 - currentPoint.y);
                        float cx2 = x + 2.0f/3.0f * (x1 - x);
                        float cy2 = y + 2.0f/3.0f * (y1 - y);

                        ctx->BezierCurveTo(cx1, cy1, cx2, cy2, x, y);
                        lastControlPoint.x = x1;
                        lastControlPoint.y = y1;
                        currentPoint.x = x;
                        currentPoint.y = y;
                    }
                    break;

                case 't': // Smooth quadratic Bezier relative
                    while (paramIndex + 1 < cmd.params.size()) {
                        float dx = cmd.params[paramIndex++];
                        float dy = cmd.params[paramIndex++];

                        // Calculate reflected control point (relative)
                        float dx1, dy1;
                        if (lastCommand == 'Q' || lastCommand == 'q' ||
                            lastCommand == 'T' || lastCommand == 't') {
                            dx1 = currentPoint.x - lastControlPoint.x;
                            dy1 = currentPoint.y - lastControlPoint.y;
                        } else {
                            dx1 = 0;
                            dy1 = 0;
                        }

                        // Convert quadratic to cubic Bezier (relative)
                        float cx1 = 2.0f/3.0f * dx1;
                        float cy1 = 2.0f/3.0f * dy1;
                        float cx2 = dx + 2.0f/3.0f * (dx1 - dx);
                        float cy2 = dy + 2.0f/3.0f * (dy1 - dy);

                        ctx->RelBezierCurveTo(cx1, cy1, cx2, cy2, dx, dy);
                        lastControlPoint.x = currentPoint.x + dx1;
                        lastControlPoint.y = currentPoint.y + dy1;
                        currentPoint.x += dx;
                        currentPoint.y += dy;
                    }
                    break;

                case 'A': case 'a': // Elliptical arc
                    while (paramIndex + 6 < cmd.params.size()) {
                        float rx = cmd.params[paramIndex++];
                        float ry = cmd.params[paramIndex++];
                        float xAxisRotation = cmd.params[paramIndex++];
                        float largeArcFlag = cmd.params[paramIndex++];
                        float sweepFlag = cmd.params[paramIndex++];
                        float x = cmd.params[paramIndex++];
                        float y = cmd.params[paramIndex++];

                        // Calculate end point
                        Point2Df endPoint;
                        if (cmd.type == 'a') {
                            endPoint.x = currentPoint.x + x;
                            endPoint.y = currentPoint.y + y;
                        } else {
                            endPoint.x = x;
                            endPoint.y = y;
                        }

                        // Convert arc to cubic bezier curves
                        ConvertArcToCubicBezier(ctx, currentPoint, rx, ry, xAxisRotation,
                                                largeArcFlag != 0, sweepFlag != 0, endPoint);

                        currentPoint = endPoint;
                    }
                    break;

                case 'Z': case 'z': // Close path
                    ctx->LineTo(startPoint.x, startPoint.y);
                    ctx->ClosePath();
                    currentPoint = startPoint;
                    break;
            }

            lastCommand = cmd.type;
        }
    }

// Helper function to convert SVG arc to cubic bezier curves
    void SVGElementRenderer::ConvertArcToCubicBezier(IRenderContext* ctx, const Point2Df& start,
                                                     float rx, float ry, float xAxisRotation,
                                                     bool largeArc, bool sweep, const Point2Df& end) {
        // Handle degenerate cases
        if (rx == 0 || ry == 0) {
            ctx->LineTo(end.x, end.y);
            return;
        }

        rx = std::abs(rx);
        ry = std::abs(ry);

        float phi = xAxisRotation * M_PI / 180.0f;
        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);

        // Calculate center point using SVG arc algorithm
        float dx = (start.x - end.x) / 2.0f;
        float dy = (start.y - end.y) / 2.0f;
        float x1p = cosPhi * dx + sinPhi * dy;
        float y1p = -sinPhi * dx + cosPhi * dy;

        // Correct radii if needed
        float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lambda > 1) {
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }

        // Calculate center
        float sign = (largeArc != sweep) ? 1.0f : -1.0f;
        float sq = std::max(0.0f, (rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p) /
                                  (rx * rx * y1p * y1p + ry * ry * x1p * x1p));
        float cxp = sign * std::sqrt(sq) * rx * y1p / ry;
        float cyp = -sign * std::sqrt(sq) * ry * x1p / rx;

        float cx = cosPhi * cxp - sinPhi * cyp + (start.x + end.x) / 2.0f;
        float cy = sinPhi * cxp + cosPhi * cyp + (start.y + end.y) / 2.0f;

        // Calculate angles
        float ux = (x1p - cxp) / rx;
        float uy = (y1p - cyp) / ry;
        float startAngle = std::atan2(uy, ux);

        float vx = (-x1p - cxp) / rx;
        float vy = (-y1p - cyp) / ry;
        float deltaAngle = std::atan2(vy, vx) - startAngle;

        // Adjust angle based on sweep flag
        if (sweep && deltaAngle < 0) {
            deltaAngle += 2 * M_PI;
        } else if (!sweep && deltaAngle > 0) {
            deltaAngle -= 2 * M_PI;
        }

        // Convert arc to bezier curves (maximum 4 curves for full circle)
        const int maxCurves = 4;
        int numCurves = std::min(maxCurves, static_cast<int>(std::ceil(std::abs(deltaAngle) / (M_PI / 2))));
        float anglePerCurve = deltaAngle / numCurves;

        for (int i = 0; i < numCurves; i++) {
            float theta1 = startAngle + i * anglePerCurve;
            float theta2 = startAngle + (i + 1) * anglePerCurve;

            // Calculate control points for this segment
            float alpha = std::sin(theta2 - theta1) * (std::sqrt(4 + 3 * std::tan((theta2 - theta1) / 2) *
                                                                     std::tan((theta2 - theta1) / 2)) - 1) / 3;

            float ex1 = std::cos(theta1);
            float ey1 = std::sin(theta1);
            float ex2 = std::cos(theta2);
            float ey2 = std::sin(theta2);

            float q1x = ex1 - ey1 * alpha;
            float q1y = ey1 + ex1 * alpha;
            float q2x = ex2 + ey2 * alpha;
            float q2y = ey2 - ex2 * alpha;

            // Transform back to original coordinate system
            float cp1x = rx * q1x;
            float cp1y = ry * q1y;
            float cp2x = rx * q2x;
            float cp2y = ry * q2y;
            float epx = rx * ex2;
            float epy = ry * ey2;

            // Rotate and translate
            float c1x = cosPhi * cp1x - sinPhi * cp1y + cx;
            float c1y = sinPhi * cp1x + cosPhi * cp1y + cy;
            float c2x = cosPhi * cp2x - sinPhi * cp2y + cx;
            float c2y = sinPhi * cp2x + cosPhi * cp2y + cy;
            float endx = cosPhi * epx - sinPhi * epy + cx;
            float endy = sinPhi * epx + cosPhi * epy + cy;

            ctx->BezierCurveTo(c1x, c1y, c2x, c2y, endx, endy);
        }
    }

// Updated RenderPath method in SVGElementRenderer
    void SVGElementRenderer::RenderPath(const SVGElement& element) {
        // path commands
        const std::vector<SVGPathCommand> commands = ((SVGPathElement&)element).pathCommands;
        if (commands.empty()) return;

        // Apply styles
        ApplyFillStyles(element);

        // Execute path commands using native Path functions
        ExecutePathCommands(ctx, commands);

        // Stroke or fill based on style attributes
        std::string fill = element.attributes.Get("fill", "black");
        std::string stroke = element.attributes.Get("stroke", "none");

        if (fill != "none") {
            ctx->FillPath();
        }
        if (stroke != "none") {
            ctx->StrokePath();
        }
    }

    void SVGElementRenderer::RenderText(const SVGElement& element) {
        float x = element.attributes.GetFloat("x", 0.0f);
        float y = element.attributes.GetFloat("y", 0.0f);

        if (element.textContent.empty()) return;

        ApplyFillStyles(element);

        // Use existing UltraCanvas text function
        Point2Df position(x, y);
        ctx->DrawText(element.textContent, position);
    }

    void SVGElementRenderer::RenderGroup(const SVGElement& element) {
        ctx->PushState();

        ApplyFillStyles(element);

        // Render all children
        for (const auto& child : element.children) {
            if (child) {
                RenderElement(*child);
            }
        }
        ctx->PopState();
    }

    void SVGElementRenderer::RenderElement(const SVGElement& element) {
        if (element.tagName == "rect") {
            RenderRect(element);
        } else if (element.tagName == "circle") {
            RenderCircle(element);
        } else if (element.tagName == "ellipse") {
            RenderEllipse(element);
        } else if (element.tagName == "line") {
            RenderLine(element);
        } else if (element.tagName == "polyline") {
            RenderPolyline(element);
        } else if (element.tagName == "polygon") {
            RenderPolygon(element);
        } else if (element.tagName == "path") {
            RenderPath(element);
        } else if (element.tagName == "text") {
            RenderText(element);
        } else if (element.tagName == "g" || element.tagName == "svg") {
            RenderGroup(element);
        }
        // Note: Additional elements like gradients, patterns, etc. would be added here
    }

    void SVGElementRenderer::RenderDocument(const Rect2Df& viewport) {
        if (!document.root) return;
        ctx->PushState();

        // Apply viewport transformation if needed
        if (document.hasViewBox) {
            // Calculate scale to fit viewBox in viewport
            float scaleX = viewport.width / document.viewBox.width;
            float scaleY = viewport.height / document.viewBox.height;
            float scale = std::min(scaleX, scaleY);

            // Apply transformation
            ctx->Translate(viewport.x, viewport.y);
            ctx->Scale(scale, scale);
            ctx->Translate(-document.viewBox.x, -document.viewBox.y);
        }

        RenderElement(*document.root);

        ctx->PopState();
    }

// ===== SVG UI ELEMENT IMPLEMENTATION =====
    UltraCanvasSVGElement::UltraCanvasSVGElement(const std::string& identifier, long id, long x, long y, long w, long h)
            : UltraCanvasUIElement(identifier, id, x, y, w, h) {
    }

    UltraCanvasSVGElement::~UltraCanvasSVGElement() {
        delete renderer;
    }

    bool UltraCanvasSVGElement::LoadFromString(const std::string& svgContent) {
        this->svgContent = svgContent;

        SimpleSVGParser parser;
        document = parser.Parse(svgContent);

        if (!document) {
            if (onLoadError) {
                onLoadError("Failed to parse SVG content");
            }
            return false;
        }

        if (autoResize) {
            UpdateSizeFromSVG();
        }

        if (onLoadComplete) {
            onLoadComplete();
        }

        return true;
    }

    bool UltraCanvasSVGElement::LoadFromFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            if (onLoadError) {
                onLoadError("Failed to open file: " + filePath);
            }
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return LoadFromString(buffer.str());
    }

    void UltraCanvasSVGElement::Render() {
        IRenderContext *ctx = GetRenderContext();
        if (!document) return;

        if (!renderer) {
            renderer = new SVGElementRenderer(*document, ctx);
        }


        Rect2Df viewport(0, 0, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()));

//    if (scaleFactor != 1.0f) {
//        PushMatrix();
//        Scale(scaleFactor, scaleFactor);
//    }

        renderer->RenderDocument(viewport);

//    if (scaleFactor != 1.0f) {
//        PopMatrix();
//    }
    }

    void UltraCanvasSVGElement::UpdateSizeFromSVG() {
        if (!document) return;

        if (document->width > 0 && document->height > 0) {
            SetWidth(static_cast<int>(document->width));
            SetHeight(static_cast<int>(document->height));
        } else if (document->hasViewBox) {
            SetWidth(static_cast<int>(document->viewBox.width));
            SetHeight(static_cast<int>(document->viewBox.height));
        }
    }

// ===== SVG PLUGIN IMPLEMENTATION =====
/*
bool UltraCanvasSVGPlugin::CanHandle(const std::string& filePath) const {
    std::string ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".svg" || ext == ".svgz";
}

bool UltraCanvasSVGPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
    return fileInfo.formatType == GraphicsFormatType::Vector &&
           (fileInfo.extension == ".svg" || fileInfo.extension == ".svgz");
}

bool UltraCanvasSVGPlugin::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto document = parser.Parse(buffer.str());
    if (document) {
        currentKey = GenerateKey(filePath);
        documentCache[currentKey] = document;
        return true;
    }

    return false;
}

bool UltraCanvasSVGPlugin::LoadFromMemory(const std::vector<uint8_t>& data) {
    std::string content(data.begin(), data.end());
    auto document = parser.Parse(content);

    if (document) {
        currentKey = GenerateKey("memory_" + std::to_string(data.size()));
        documentCache[currentKey] = document;
        return true;
    }

    return false;
}

void UltraCanvasSVGPlugin::Render(const Rect2Df& bounds) {
    if (currentKey.empty()) return;

    auto it = documentCache.find(currentKey);
    if (it != documentCache.end()) {
        SVGElementRenderer renderer(*it->second);
        renderer.RenderDocument(bounds);
    }
}

Size2D UltraCanvasSVGPlugin::GetNaturalSize() const {
    if (currentKey.empty()) return Size2D(100, 100);

    auto it = documentCache.find(currentKey);
    if (it != documentCache.end()) {
        return Size2D(it->second->width, it->second->height);
    }

    return Size2D(100, 100);
}

GraphicsFileInfo UltraCanvasSVGPlugin::GetFileInfo(const std::string& filePath) {
    GraphicsFileInfo info(filePath);
    info.formatType = GraphicsFormatType::Vector;

    if (CanHandle(filePath) && LoadFromFile(filePath)) {
        auto document = GetDocument(GenerateKey(filePath));
        if (document) {
            info.width = static_cast<int>(document->width);
            info.height = static_cast<int>(document->height);
            info.metadata["scalable"] = "true";
            info.metadata["format"] = "SVG";
        }
    }

    return info;
}

std::shared_ptr<SVGDocument> UltraCanvasSVGPlugin::GetDocument(const std::string& key) const {
    auto it = documentCache.find(key);
    return (it != documentCache.end()) ? it->second : nullptr;
}

void UltraCanvasSVGPlugin::ClearCache() {
    documentCache.clear();
    currentKey.clear();
}

std::string UltraCanvasSVGPlugin::GetFileExtension(const std::string& filePath) const {
    size_t pos = filePath.find_last_of('.');
    return (pos != std::string::npos) ? filePath.substr(pos) : "";
}

std::string UltraCanvasSVGPlugin::GenerateKey(const std::string& identifier) const {
    return identifier;
}
*/
} // namespace UltraCanvas
