// UltraCanvasSVGPlugin.cpp
// Complete SVG rendering plugin implementation
// Version: 2.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "Plugins/SVG/UltraCanvasSVGPlugin.h"
#include "UltraCanvasUtils.h"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <iomanip>

namespace UltraCanvas {

    //UltraCanvasSVGPlugin* UltraCanvasSVGPlugin::instance = nullptr;

// Helper functions
    static float ParseFloatAttribute(const tinyxml2::XMLElement* elem, const char* name, float defaultValue = 0.0f) {
        const char* attr = elem->Attribute(name);
        if (attr) {
            try {
                return std::stof(attr);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    static std::string GetAttribute(const tinyxml2::XMLElement* elem, const char* name, const std::string& defaultValue = "") {
        const char* attr = elem->Attribute(name);
        return attr ? attr : defaultValue;
    }

// SVGTransform implementation
    void SVGTransform::ApplyToContext(IRenderContext* ctx) const {
        switch (type) {
            case Matrix:
                if (values.size() >= 6) {
                    ctx->Transform(values[0], values[1], values[2], values[3], values[4], values[5]);
                }
                break;
            case Translate:
                if (values.size() >= 1) {
                    float tx = values[0];
                    float ty = values.size() >= 2 ? values[1] : 0;
                    ctx->Translate(tx, ty);
                }
                break;
            case Scale:
                if (values.size() >= 1) {
                    float sx = values[0];
                    float sy = values.size() >= 2 ? values[1] : sx;
                    ctx->Scale(sx, sy);
                }
                break;
            case Rotate:
                if (values.size() >= 1) {
                    float angle = values[0] * M_PI / 180.0f; // Convert to radians
                    if (values.size() >= 3) {
                        float cx = values[1];
                        float cy = values[2];
                        ctx->Translate(cx, cy);
                        ctx->Rotate(angle);
                        ctx->Translate(-cx, -cy);
                    } else {
                        ctx->Rotate(angle);
                    }
                }
                break;
            case SkewX:
                if (values.size() >= 1) {
                    float angle = values[0] * M_PI / 180.0f;
                    ctx->Transform(1, 0, std::tan(angle), 1, 0, 0);
                }
                break;
            case SkewY:
                if (values.size() >= 1) {
                    float angle = values[0] * M_PI / 180.0f;
                    ctx->Transform(1, std::tan(angle), 0, 1, 0, 0);
                }
                break;
        }
    }

// SVGStyle implementation
    void SVGStyle::ParseFromAttributes(const tinyxml2::XMLElement* elem) {
        // Parse fill
        const char* fill = elem->Attribute("fill");
        if (fill) {
            std::string fillStr(fill);

            if (fillStr.find("url(#") == 0) {
                size_t start = 5;
                size_t end = fillStr.find(')', start);
                if (end != std::string::npos) {
                    fillGradientId = fillStr.substr(start, end - start);
                }
            } else {
                // Parse color - simplified for now
                fillColor = ParseColor(fillStr);
            }
        }

        // Parse stroke
        const char* stroke = elem->Attribute("stroke");
        if (stroke) {
            std::string strokeStr(stroke);
            if (strokeStr.find("url(#") == 0) {
                size_t start = 5;
                size_t end = strokeStr.find(')', start);
                if (end != std::string::npos) {
                    strokeGradientId = strokeStr.substr(start, end - start);
                }
            } else {
                strokeColor = ParseColor(strokeStr);
            }
        }

        // Parse other attributes
        strokeWidth = ParseFloatAttribute(elem, "stroke-width", strokeWidth);
        opacity = ParseFloatAttribute(elem, "opacity", opacity);
        fillOpacity = ParseFloatAttribute(elem, "fill-opacity", fillOpacity);
        strokeOpacity = ParseFloatAttribute(elem, "stroke-opacity", strokeOpacity);

        // Parse style attribute if present
        const char* style = elem->Attribute("style");
        if (style) {
            ParseFromStyle(style);
        }
    }

    void SVGStyle::ParseFromStyle(const std::string& styleStr) {
        std::istringstream iss(styleStr);
        std::string property;

        while (std::getline(iss, property, ';')) {
            size_t colonPos = property.find(':');
            if (colonPos != std::string::npos) {
                std::string key = property.substr(0, colonPos);
                std::string value = property.substr(colonPos + 1);

                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                // Parse properties
                if (key == "fill") {
                    fillColor = ParseColor(value);
//                    if (value[0] == '#') {
//                        unsigned int rgb = std::stoul(value.substr(1), nullptr, 16);
//                        fillColor.r = (rgb >> 16) & 0xFF;
//                        fillColor.g = (rgb >> 8) & 0xFF;
//                        fillColor.b = rgb & 0xFF;
//                        fillColor.a = 255;
//                    }
                } else if (key == "stroke") {
                    strokeColor = ParseColor(value);
//                    if (value[0] == '#') {
//                        unsigned int rgb = std::stoul(value.substr(1), nullptr, 16);
//                        strokeColor.r = (rgb >> 16) & 0xFF;
//                        strokeColor.g = (rgb >> 8) & 0xFF;
//                        strokeColor.b = rgb & 0xFF;
//                        strokeColor.a = 255;
//                    }
                } else if (key == "stroke-width") {
                    strokeWidth = std::stof(value);
                } else if (key == "opacity") {
                    opacity = std::stof(value);
                }
            }
        }
    }

// SVGLinearGradient implementation
    std::shared_ptr<IPaintPattern> SVGLinearGradient::CreatePattern(IRenderContext* ctx, const Rect2Df& bounds) {
        float actualX1 = x1, actualY1 = y1, actualX2 = x2, actualY2 = y2;

        if (units == "userSpaceOnUse") {
            // Use absolute coordinates
        } else {
            // objectBoundingBox - scale to bounds
            actualX1 = bounds.x + x1 * bounds.width;
            actualY1 = bounds.y + y1 * bounds.height;
            actualX2 = bounds.x + x2 * bounds.width;
            actualY2 = bounds.y + y2 * bounds.height;
        }

        return ctx->CreateLinearGradientPattern(actualX1, actualY1, actualX2, actualY2, stops);
    }

// SVGRadialGradient implementation
    std::shared_ptr<IPaintPattern> SVGRadialGradient::CreatePattern(IRenderContext* ctx, const Rect2Df& bounds) {
        float actualCx = cx, actualCy = cy, actualR = r;
        float actualFx = fx, actualFy = fy;

        if (units == "userSpaceOnUse") {
            // Use absolute coordinates
        } else {
            // objectBoundingBox - scale to bounds
            actualCx = bounds.x + cx * bounds.width;
            actualCy = bounds.y + cy * bounds.height;
            actualR = r * std::max(bounds.width, bounds.height);
            actualFx = bounds.x + fx * bounds.width;
            actualFy = bounds.y + fy * bounds.height;
        }
        return ctx->CreateRadialGradientPattern(actualFx, actualFy, 0, actualCx, actualCy, actualR, stops);;
    }

// SVGGaussianBlur implementation
    void SVGGaussianBlur::Apply(IRenderContext* ctx) {
        //ctx->ApplyGaussianBlur(stdDeviationX, stdDeviationY);
    }

// SVGFilter implementation
    void SVGFilter::Apply(IRenderContext* ctx, const Rect2Df& bounds) {
        for (const auto& effect : effects) {
            effect->Apply(ctx);
        }
    }

// SVGPathParser implementation
    std::vector<PathCommand> SVGPathParser::Parse(const std::string& pathData) {
        std::vector<PathCommand> commands;
        size_t pos = 0;

        while (pos < pathData.length()) {
            SkipWhitespace(pathData, pos);
            if (pos >= pathData.length()) break;

            char cmd = pathData[pos];
            if (std::isalpha(cmd)) {
                pos++;
                PathCommand command;
                command.type = cmd;
                command.params = ParseNumbers(pathData, pos);
                commands.push_back(command);
            } else {
                // Implicit command repetition
                if (!commands.empty()) {
                    PathCommand command;
                    command.type = commands.back().type;
                    if (command.type == 'M') command.type = 'L';
                    if (command.type == 'm') command.type = 'l';
                    command.params = ParseNumbers(pathData, pos);
                    if (!command.params.empty()) {
                        commands.push_back(command);
                    }
                }
            }
        }

        return commands;
    }

    void SVGPathParser::RenderPath(IRenderContext* ctx, const std::vector<PathCommand>& commands) {
        float currentX = 0, currentY = 0;
        float startX = 0, startY = 0;
        float controlX = 0, controlY = 0;

        ctx->ClearPath();

        for (const auto& cmd : commands) {
            switch (cmd.type) {
                case 'M': // Move to absolute
                    if (cmd.params.size() >= 2) {
                        currentX = cmd.params[0];
                        currentY = cmd.params[1];
                        startX = currentX;
                        startY = currentY;
                        ctx->MoveTo(currentX, currentY);

                        // Additional points are line-to commands
                        for (size_t i = 2; i + 1 < cmd.params.size(); i += 2) {
                            currentX = cmd.params[i];
                            currentY = cmd.params[i + 1];
                            ctx->LineTo(currentX, currentY);
                        }
                    }
                    break;

                case 'm': // Move to relative
                    if (cmd.params.size() >= 2) {
                        if (currentX == 0 && currentY == 0) {
                            // First move is absolute
                            currentX = cmd.params[0];
                            currentY = cmd.params[1];
                            ctx->MoveTo(currentX, currentY);
                        } else {
                            ctx->RelMoveTo(cmd.params[0], cmd.params[1]);
                            currentX += cmd.params[0];
                            currentY += cmd.params[1];
                        }
                        startX = currentX;
                        startY = currentY;

                        for (size_t i = 2; i + 1 < cmd.params.size(); i += 2) {
                            ctx->RelLineTo(cmd.params[i], cmd.params[i + 1]);
                            currentX += cmd.params[i];
                            currentY += cmd.params[i + 1];
                        }
                    }
                    break;

                case 'L': // Line to absolute
                    for (size_t i = 0; i + 1 < cmd.params.size(); i += 2) {
                        currentX = cmd.params[i];
                        currentY = cmd.params[i + 1];
                        ctx->LineTo(currentX, currentY);
                    }
                    break;

                case 'l': // Line to relative
                    for (size_t i = 0; i + 1 < cmd.params.size(); i += 2) {
                        ctx->RelLineTo(cmd.params[i], cmd.params[i + 1]);
                        currentX += cmd.params[i];
                        currentY += cmd.params[i + 1];
                    }
                    break;

                case 'H': // Horizontal line absolute
                    for (size_t i = 0; i < cmd.params.size(); i++) {
                        currentX = cmd.params[i];
                        ctx->LineTo(currentX, currentY);
                    }
                    break;

                case 'h': // Horizontal line relative
                    for (size_t i = 0; i < cmd.params.size(); i++) {
                        ctx->RelLineTo(cmd.params[i], 0);
                        currentX += cmd.params[i];
                    }
                    break;

                case 'V': // Vertical line absolute
                    for (size_t i = 0; i < cmd.params.size(); i++) {
                        currentY = cmd.params[i];
                        ctx->LineTo(currentX, currentY);
                    }
                    break;

                case 'v': // Vertical line relative
                    for (size_t i = 0; i < cmd.params.size(); i++) {
                        ctx->RelLineTo(0, cmd.params[i]);
                        currentY += cmd.params[i];
                    }
                    break;

                case 'C': // Cubic Bezier absolute
                    for (size_t i = 0; i + 5 < cmd.params.size(); i += 6) {
                        float cp1x = cmd.params[i];
                        float cp1y = cmd.params[i + 1];
                        float cp2x = cmd.params[i + 2];
                        float cp2y = cmd.params[i + 3];
                        currentX = cmd.params[i + 4];
                        currentY = cmd.params[i + 5];
                        ctx->BezierCurveTo(cp1x, cp1y, cp2x, cp2y, currentX, currentY);
                        controlX = cp2x;
                        controlY = cp2y;
                    }
                    break;

                case 'c': // Cubic Bezier relative
                    for (size_t i = 0; i + 5 < cmd.params.size(); i += 6) {
                        float cp1x = cmd.params[i];
                        float cp1y = cmd.params[i + 1];
                        float cp2x = cmd.params[i + 2];
                        float cp2y = cmd.params[i + 3];
                        float dx = cmd.params[i + 4];
                        float dy = cmd.params[i + 5];
                        ctx->RelBezierCurveTo(cp1x, cp1y, cp2x, cp2y, dx, dy);
                        controlX = currentX + cp2x;
                        controlY = currentY + cp2y;
                        currentX += dx;
                        currentY += dy;
                    }
                    break;

                case 'S': // Smooth cubic Bezier absolute
                    for (size_t i = 0; i + 3 < cmd.params.size(); i += 4) {
                        float cp1x = 2 * currentX - controlX;
                        float cp1y = 2 * currentY - controlY;
                        float cp2x = cmd.params[i];
                        float cp2y = cmd.params[i + 1];
                        currentX = cmd.params[i + 2];
                        currentY = cmd.params[i + 3];
                        ctx->BezierCurveTo(cp1x, cp1y, cp2x, cp2y, currentX, currentY);
                        controlX = cp2x;
                        controlY = cp2y;
                    }
                    break;

                case 's': // Smooth cubic Bezier relative
                    for (size_t i = 0; i + 3 < cmd.params.size(); i += 4) {
                        float cp1x = currentX - controlX;
                        float cp1y = currentY - controlY;
                        float cp2x = cmd.params[i];
                        float cp2y = cmd.params[i + 1];
                        float dx = cmd.params[i + 2];
                        float dy = cmd.params[i + 3];
                        ctx->RelBezierCurveTo(cp1x, cp1y, cp2x, cp2y, dx, dy);
                        controlX = currentX + cp2x;
                        controlY = currentY + cp2y;
                        currentX += dx;
                        currentY += dy;
                    }
                    break;

                case 'Q': // Quadratic Bezier absolute
                    for (size_t i = 0; i + 3 < cmd.params.size(); i += 4) {
                        float cpx = cmd.params[i];
                        float cpy = cmd.params[i + 1];
                        currentX = cmd.params[i + 2];
                        currentY = cmd.params[i + 3];
                        ctx->QuadraticCurveTo(cpx, cpy, currentX, currentY);
                        controlX = cpx;
                        controlY = cpy;
                    }
                    break;

                case 'q': // Quadratic Bezier relative
                    for (size_t i = 0; i + 3 < cmd.params.size(); i += 4) {
                        float cpx = cmd.params[i];
                        float cpy = cmd.params[i + 1];
                        float dx = cmd.params[i + 2];
                        float dy = cmd.params[i + 3];
                        controlX = currentX + cpx;
                        controlY = currentY + cpy;
                        currentX += dx;
                        currentY += dy;
                        ctx->QuadraticCurveTo(controlX, controlY, currentX, currentY);
                    }
                    break;

                case 'T': // Smooth quadratic Bezier absolute
                    for (size_t i = 0; i + 1 < cmd.params.size(); i += 2) {
                        float cpx = 2 * currentX - controlX;
                        float cpy = 2 * currentY - controlY;
                        currentX = cmd.params[i];
                        currentY = cmd.params[i + 1];
                        ctx->QuadraticCurveTo(cpx, cpy, currentX, currentY);
                        controlX = cpx;
                        controlY = cpy;
                    }
                    break;

                case 't': // Smooth quadratic Bezier relative
                    for (size_t i = 0; i + 1 < cmd.params.size(); i += 2) {
                        float cpx = currentX - controlX;
                        float cpy = currentY - controlY;
                        float dx = cmd.params[i];
                        float dy = cmd.params[i + 1];
                        controlX = currentX + cpx;
                        controlY = currentY + cpy;
                        currentX += dx;
                        currentY += dy;
                        ctx->QuadraticCurveTo(controlX, controlY, currentX, currentY);
                    }
                    break;

                case 'A': // Arc absolute
                case 'a': // Arc relative
                    for (size_t i = 0; i + 6 < cmd.params.size(); i += 7) {
                        float rx = cmd.params[i];
                        float ry = cmd.params[i + 1];
                        float rotation = cmd.params[i + 2];
                        float largeArc = cmd.params[i + 3];
                        float sweep = cmd.params[i + 4];
                        float endX = (cmd.type == 'A') ? cmd.params[i + 5] : currentX + cmd.params[i + 5];
                        float endY = (cmd.type == 'A') ? cmd.params[i + 6] : currentY + cmd.params[i + 6];

                        ctx->ArcTo(currentX, currentY, endX, endY, std::max(rx, ry));

                        currentX = endX;
                        currentY = endY;
                    }
                    break;

                case 'Z':
                case 'z': // Close path
                    ctx->ClosePath();
                    currentX = startX;
                    currentY = startY;
                    break;
            }
        }
    }

    std::vector<float> SVGPathParser::ParseNumbers(const std::string& str, size_t& pos) {
        std::vector<float> numbers;

        while (pos < str.length()) {
            SkipWhitespace(str, pos);
            if (pos >= str.length()) break;

            // Check if next character is a command letter
            if (std::isalpha(str[pos])) break;

            // Parse number
            size_t startPos = pos;
            bool hasDecimal = false;
            bool hasExponent = false;

            if (str[pos] == '-' || str[pos] == '+') pos++;

            while (pos < str.length()) {
                char c = str[pos];
                if (std::isdigit(c)) {
                    pos++;
                } else if (c == '.' && !hasDecimal && !hasExponent) {
                    hasDecimal = true;
                    pos++;
                } else if ((c == 'e' || c == 'E') && !hasExponent) {
                    hasExponent = true;
                    pos++;
                    if (pos < str.length() && (str[pos] == '-' || str[pos] == '+')) {
                        pos++;
                    }
                } else {
                    break;
                }
            }

            if (pos > startPos) {
                std::string numStr = str.substr(startPos, pos - startPos);
                try {
                    numbers.push_back(std::stof(numStr));
                } catch (...) {
                    // Invalid number, skip
                }
            }

            // Skip comma if present
            SkipWhitespace(str, pos);
            if (pos < str.length() && str[pos] == ',') {
                pos++;
            }
        }

        return numbers;
    }

    void SVGPathParser::SkipWhitespace(const std::string& str, size_t& pos) {
        while (pos < str.length() && std::isspace(str[pos])) {
            pos++;
        }
    }

// SVGDocument implementation
    SVGDocument::SVGDocument() {}

    SVGDocument::~SVGDocument() {}

    bool SVGDocument::LoadFromFile(const std::string& filepath) {
        if (xmlDoc.LoadFile(filepath.c_str()) != tinyxml2::XML_SUCCESS) {
            return false;
        }

        root = xmlDoc.FirstChildElement("svg");
        if (!root) {
            return false;
        }

        // Parse dimensions
        width = ParseFloatAttribute(root, "width", 0);
        height = ParseFloatAttribute(root, "height", 0);

        // Parse viewBox
        const char* viewBoxAttr = root->Attribute("viewBox");
        if (viewBoxAttr) {
            ParseViewBox(viewBoxAttr);
            if (width == 0 || height == 0) {
                width = viewBox.width;
                height = viewBox.height;
            }
        } else {
            if (width == 0 || height == 0) {
                width = 100;
                height = 100;
            }
            viewBox = {0, 0, width, height};
        }

        // Parse defs
        tinyxml2::XMLElement* defs = root->FirstChildElement("defs");
        if (defs) {
            ParseDefs(defs);
        }

        return true;
    }

    bool SVGDocument::LoadFromString(const std::string& svgContent) {
        if (xmlDoc.Parse(svgContent.c_str()) != tinyxml2::XML_SUCCESS) {
            return false;
        }

        root = xmlDoc.FirstChildElement("svg");
        if (!root) {
            return false;
        }

        // Parse dimensions
        width = ParseFloatAttribute(root, "width", 100);
        height = ParseFloatAttribute(root, "height", 100);

        // Parse viewBox
        const char* viewBoxAttr = root->Attribute("viewBox");
        if (viewBoxAttr) {
            ParseViewBox(viewBoxAttr);
        } else {
            viewBox = {0, 0, width, height};
        }

        // Parse defs
        tinyxml2::XMLElement* defs = root->FirstChildElement("defs");
        if (defs) {
            ParseDefs(defs);
        }

        return true;
    }

    void SVGDocument::ParseViewBox(const std::string& viewBoxStr) {
        std::istringstream iss(viewBoxStr);
        iss >> viewBox.x >> viewBox.y >> viewBox.width >> viewBox.height;
    }

    void SVGDocument::ParseDefs(tinyxml2::XMLElement* defs) {
        for (tinyxml2::XMLElement* elem = defs->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string name = elem->Name();

            if (name == "linearGradient" || name == "radialGradient") {
                ParseGradient(elem);
            } else if (name == "filter") {
                ParseFilter(elem);
            } else if (name == "clipPath") {
                ParseClipPath(elem);
            }
        }
    }

    void SVGDocument::ParseGradient(tinyxml2::XMLElement* elem) {
        std::string type = elem->Name();
        std::string id = GetAttribute(elem, "id");

        if (id.empty()) return;

        std::unique_ptr<SVGGradient> gradient;

        if (type == "linearGradient") {
            auto linear = std::make_unique<SVGLinearGradient>();
            linear->x1 = ParseFloatAttribute(elem, "x1", 0);
            linear->y1 = ParseFloatAttribute(elem, "y1", 0);
            linear->x2 = ParseFloatAttribute(elem, "x2", 1);
            linear->y2 = ParseFloatAttribute(elem, "y2", 0);
            gradient = std::move(linear);
        } else if (type == "radialGradient") {
            auto radial = std::make_unique<SVGRadialGradient>();
            radial->cx = ParseFloatAttribute(elem, "cx", 0.5f);
            radial->cy = ParseFloatAttribute(elem, "cy", 0.5f);
            radial->r = ParseFloatAttribute(elem, "r", 0.5f);
            radial->fx = ParseFloatAttribute(elem, "fx", radial->cx);
            radial->fy = ParseFloatAttribute(elem, "fy", radial->cy);
            gradient = std::move(radial);
        }

        if (gradient) {
            gradient->id = id;
            gradient->units = GetAttribute(elem, "gradientUnits", "objectBoundingBox");
            gradient->spreadMethod = GetAttribute(elem, "spreadMethod", "pad");
            gradient->href = GetAttribute(elem, "xlink:href");

            // Parse stops
            for (tinyxml2::XMLElement* stop = elem->FirstChildElement("stop"); stop; stop = stop->NextSiblingElement("stop")) {
                GradientStop gradStop;
                const char* offsetStr = stop->Attribute("offset");
                if (!offsetStr || strlen(offsetStr) == 0) continue;
                float offsetFloat = ParseFloatAttribute(stop, "offset", 0);
                if (offsetStr[strlen(offsetStr) - 1] == '%') {
                    gradStop.position = offsetFloat / 100.0;
                } else {
                    gradStop.position = offsetFloat;
                }

                const char* stopColor = stop->Attribute("stop-color");
                if (stopColor) {
                    gradStop.color = ParseColor(stopColor);
                }

                gradStop.color.a = static_cast<uint8_t >(255.0 / ParseFloatAttribute(stop, "stop-opacity", 1));
                gradient->stops.push_back(gradStop);
            }

            gradients[id] = std::move(gradient);
        }
    }

    void SVGDocument::ParseFilter(tinyxml2::XMLElement* elem) {
        std::string id = GetAttribute(elem, "id");
        if (id.empty()) return;

        auto filter = std::make_unique<SVGFilter>();
        filter->id = id;
        filter->filterUnits = GetAttribute(elem, "filterUnits", "objectBoundingBox");

        // Parse filter effects
        for (tinyxml2::XMLElement* effect = elem->FirstChildElement(); effect; effect = effect->NextSiblingElement()) {
            std::string effectType = effect->Name();

            if (effectType == "feGaussianBlur") {
                auto blur = std::make_unique<SVGGaussianBlur>();
                float stdDev = ParseFloatAttribute(effect, "stdDeviation", 0);
                blur->stdDeviationX = stdDev;
                blur->stdDeviationY = stdDev;
                blur->in = GetAttribute(effect, "in");
                blur->result = GetAttribute(effect, "result");
                filter->effects.push_back(std::move(blur));
            }
            // Add more filter effects as needed
        }

        filters[id] = std::move(filter);
    }

    void SVGDocument::ParseClipPath(tinyxml2::XMLElement* elem) {
        std::string id = GetAttribute(elem, "id");
        if (id.empty()) return;

        // Parse first path element in clipPath
        tinyxml2::XMLElement* pathElem = elem->FirstChildElement("path");
        if (pathElem) {
            const char* d = pathElem->Attribute("d");
            if (d) {
                clipPaths[id] = SVGPathParser::Parse(d);
            }
        }
    }

    void SVGDocument::AddGradient(std::unique_ptr<SVGGradient> gradient) {
        if (gradient && !gradient->id.empty()) {
            gradients[gradient->id] = std::move(gradient);
        }
    }

    void SVGDocument::AddFilter(std::unique_ptr<SVGFilter> filter) {
        if (filter && !filter->id.empty()) {
            filters[filter->id] = std::move(filter);
        }
    }

    void SVGDocument::AddClipPath(const std::string& id, std::vector<PathCommand> path) {
        if (!id.empty()) {
            clipPaths[id] = std::move(path);
        }
    }

    SVGGradient* SVGDocument::GetGradient(const std::string& id) {
        auto it = gradients.find(id);
        return (it != gradients.end()) ? it->second.get() : nullptr;
    }

    SVGFilter* SVGDocument::GetFilter(const std::string& id) {
        auto it = filters.find(id);
        return (it != filters.end()) ? it->second.get() : nullptr;
    }

    std::vector<PathCommand>* SVGDocument::GetClipPath(const std::string& id) {
        auto it = clipPaths.find(id);
        return (it != clipPaths.end()) ? &it->second : nullptr;
    }

// SVGElementRenderer implementation
    SVGElementRenderer::SVGElementRenderer(const SVGDocument& doc, IRenderContext* ctx)
            : document(doc), context(ctx) {
        // Initialize with default style
        SVGStyle defaultStyle;
        styleStack.push(defaultStyle);
    }

    SVGElementRenderer::~SVGElementRenderer() {}

    void SVGElementRenderer::Render() {
        if (!document.root) return;

        // Set up viewport transformation
        if (document.viewBox.width > 0 && document.viewBox.height > 0) {
            float scaleX = document.width / document.viewBox.width;
            float scaleY = document.height / document.viewBox.height;

            context->PushState();
            if (scaleX != 1 || scaleY != 1) {
                context->Scale(scaleX, scaleY);
            }
            if (document.viewBox.x > 0 || document.viewBox.y > 0) {
                context->Translate(-document.viewBox.x, -document.viewBox.y);
            }
        }

        // Render all child elements
        for (tinyxml2::XMLElement* elem = document.root->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            RenderElement(elem);
        }

        if (document.viewBox.width > 0 && document.viewBox.height > 0) {
            context->PopState();
        }
    }

    void SVGElementRenderer::RenderElement(tinyxml2::XMLElement* elem) {
        if (!elem) return;

        std::string name = elem->Name();

        // Save context state
        context->PushState();

        // Parse and apply style
        SVGStyle style = ParseStyle(elem);
        PushStyle(style);

        // Parse and apply transform
        const char* transformAttr = elem->Attribute("transform");
        if (transformAttr) {
            SVGTransform transform = ParseTransform(transformAttr);
            transform.ApplyToContext(context);
        }

        // Apply opacity
        if (style.opacity < 1.0f) {
            context->SetAlpha(style.opacity);
        }

        // Render based on element type
        if (name == "g") {
            RenderGroup(elem);
        } else if (name == "path") {
            RenderPath(elem);
        } else if (name == "rect") {
            RenderRect(elem);
        } else if (name == "circle") {
            RenderCircle(elem);
        } else if (name == "ellipse") {
            RenderEllipse(elem);
        } else if (name == "line") {
            RenderLine(elem);
        } else if (name == "polyline") {
            RenderPolyline(elem);
        } else if (name == "polygon") {
            RenderPolygon(elem);
        } else if (name == "text") {
            RenderText(elem);
        } else if (name == "image") {
            RenderImage(elem);
        } else if (name == "use") {
            RenderUse(elem);
        }

        // Pop style
        PopStyle();

        // Restore context state
        context->PopState();
    }

    void SVGElementRenderer::RenderGroup(tinyxml2::XMLElement* elem) {
        // Render all children
        for (tinyxml2::XMLElement* child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            RenderElement(child);
        }
    }

    void SVGElementRenderer::RenderPath(tinyxml2::XMLElement* elem) {
        const char* d = elem->Attribute("d");
        if (!d) return;

        std::vector<PathCommand> commands = SVGPathParser::Parse(d);
        SVGPathParser::RenderPath(context, commands);

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = GetElementBounds(elem);

        FillAndStroke(style, bounds);

        context->ClearPath();
    }

    void SVGElementRenderer::RenderRect(tinyxml2::XMLElement* elem) {
        float x = ParseFloatAttribute(elem, "x", 0);
        float y = ParseFloatAttribute(elem, "y", 0);
        float width = ParseFloatAttribute(elem, "width", 0);
        float height = ParseFloatAttribute(elem, "height", 0);
        float rx = ParseFloatAttribute(elem, "rx", 0);
        float ry = ParseFloatAttribute(elem, "ry", 0);

        context->ClearPath();

        if (rx > 0 || ry > 0) {
            // Rounded rectangle
            context->RoundedRect(x, y, width, height, std::max(rx, ry));
        } else {
            // Regular rectangle
            context->Rect(x, y, width, height);
        }

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = {x, y, width, height};

        FillAndStroke(style, bounds);
    }

    void SVGElementRenderer::RenderCircle(tinyxml2::XMLElement* elem) {
        float cx = ParseFloatAttribute(elem, "cx", 0);
        float cy = ParseFloatAttribute(elem, "cy", 0);
        float r = ParseFloatAttribute(elem, "r", 0);

        context->ClearPath();
        context->Circle(cx, cy, r);

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = {cx - r, cy - r, 2 * r, 2 * r};

        FillAndStroke(style, bounds);
    }

    void SVGElementRenderer::RenderEllipse(tinyxml2::XMLElement* elem) {
        float cx = ParseFloatAttribute(elem, "cx", 0);
        float cy = ParseFloatAttribute(elem, "cy", 0);
        float rx = ParseFloatAttribute(elem, "rx", 0);
        float ry = ParseFloatAttribute(elem, "ry", 0);

        context->ClearPath();
        context->Ellipse(cx, cy, rx, ry, 0, 0, 2 * M_PI);

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = {cx - rx, cy - ry, 2 * rx, 2 * ry};

        FillAndStroke(style, bounds);
    }

    void SVGElementRenderer::RenderPolygon(tinyxml2::XMLElement* elem) {
        const char* pointsAttr = elem->Attribute("points");
        if (!pointsAttr) return;

        std::vector<Point2Df> points = ParsePoints(pointsAttr);
        if (points.size() < 3) return;

        context->ClearPath();
        context->MoveTo(points[0].x, points[0].y);

        for (size_t i = 1; i < points.size(); i++) {
            context->LineTo(points[i].x, points[i].y);
        }

        context->ClosePath();

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = GetElementBounds(elem);

        FillAndStroke(style, bounds);
    }

    void SVGElementRenderer::RenderLine(tinyxml2::XMLElement* elem) {
        float x1 = ParseFloatAttribute(elem, "x1", 0);
        float y1 = ParseFloatAttribute(elem, "y1", 0);
        float x2 = ParseFloatAttribute(elem, "x2", 0);
        float y2 = ParseFloatAttribute(elem, "y2", 0);

        context->ClearPath();
        context->MoveTo(x1, y1);
        context->LineTo(x2, y2);

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = {std::min(x1, x2), std::min(y1, y2),
                       std::abs(x2 - x1), std::abs(y2 - y1)};

        // Lines only have stroke
        if (style.strokeColor.a > 0 || !style.strokeGradientId.empty()) {
            ApplyStroke(style, bounds);
            context->Stroke();
        }
    }

    void SVGElementRenderer::RenderPolyline(tinyxml2::XMLElement* elem) {
        const char* pointsAttr = elem->Attribute("points");
        if (!pointsAttr) return;

        std::vector<Point2Df> points = ParsePoints(pointsAttr);
        if (points.size() < 2) return;

        context->ClearPath();
        context->MoveTo(points[0].x, points[0].y);

        for (size_t i = 1; i < points.size(); i++) {
            context->LineTo(points[i].x, points[i].y);
        }

        SVGStyle& style = styleStack.top();
        Rect2Df bounds = GetElementBounds(elem);

        // Polylines typically only have stroke
        if (style.strokeColor.a > 0 || !style.strokeGradientId.empty()) {
            ApplyStroke(style, bounds);
            context->Stroke();
        }
    }

    void SVGElementRenderer::RenderText(tinyxml2::XMLElement* elem) {
        float x = ParseFloatAttribute(elem, "x", 0);
        float y = ParseFloatAttribute(elem, "y", 0);

        const char* text = elem->GetText();
        if (!text) return;

        SVGStyle& style = styleStack.top();

        // Set font properties
        const char* fontFamily = elem->Attribute("font-family");
        float fontSize = ParseFloatAttribute(elem, "font-size", 12);
        const char* fontWeight = elem->Attribute("font-weight");
        FontWeight fw = FontWeight::Normal;
        if (fontWeight && strcmp(fontWeight, "bold") == 0) {
            fw = FontWeight::Bold;
        }
        context->SetFontFace(fontFamily ? fontFamily : "Arial", fw, FontSlant::Normal);
        context->SetFontSize(fontSize);

        // Apply fill for text
        if (style.fillColor.a > 0) {
            context->SetFillPaint(style.fillColor);
            context->FillText(text, x, y);
        }

        // Apply stroke for text
        if (style.strokeColor.a > 0 && style.strokeWidth > 0) {
            context->SetStrokePaint(style.strokeColor);
            context->SetStrokeWidth(style.strokeWidth);
            context->StrokeText(text, x, y);
        }
    }

    void SVGElementRenderer::RenderImage(tinyxml2::XMLElement* elem) {
        float x = ParseFloatAttribute(elem, "x", 0);
        float y = ParseFloatAttribute(elem, "y", 0);
        float width = ParseFloatAttribute(elem, "width", 0);
        float height = ParseFloatAttribute(elem, "height", 0);

        const char* href = elem->Attribute("xlink:href");
        if (!href) href = elem->Attribute("href");

        if (href) {
            // Load and render image
            // This would need actual image loading implementation
            context->DrawImage(href, x, y, width, height);
        }
    }

    void SVGElementRenderer::RenderUse(tinyxml2::XMLElement* elem) {
        const char* href = elem->Attribute("xlink:href");
        if (!href) href = elem->Attribute("href");

        if (href && href[0] == '#') {
            std::string id = href + 1;

            // Find referenced element
            // This would need to search through the document for the element with matching id
            // For now, simplified implementation
        }
    }

    void SVGElementRenderer::FillAndStroke(const SVGStyle& style, const Rect2Df& bounds) {
        // Apply fill
        if (style.fillColor.a > 0 || !style.fillGradientId.empty()) {
            ApplyFill(style, bounds);
            context->FillPathPreserve();
        }

        // Apply stroke
        if (style.strokeColor.a > 0 || !style.strokeGradientId.empty()) {
            ApplyStroke(style, bounds);
            context->StrokePathPreserve();
        }
        context->ClearPath();
    }

    void SVGElementRenderer::PushStyle(const SVGStyle& style) {
        SVGStyle newStyle = styleStack.top();

        // Merge with new style
        if (style.fillColor.a > 0) newStyle.fillColor = style.fillColor;
        if (!style.fillGradientId.empty()) newStyle.fillGradientId = style.fillGradientId;
        if (style.strokeColor.a > 0) newStyle.strokeColor = style.strokeColor;
        if (!style.strokeGradientId.empty()) newStyle.strokeGradientId = style.strokeGradientId;
        if (style.strokeWidth > 0) newStyle.strokeWidth = style.strokeWidth;

        newStyle.opacity *= style.opacity;
        newStyle.fillOpacity *= style.fillOpacity;
        newStyle.strokeOpacity *= style.strokeOpacity;

        styleStack.push(newStyle);
    }

    void SVGElementRenderer::PopStyle() {
        if (styleStack.size() > 1) {
            styleStack.pop();
        }
    }

    void SVGElementRenderer::ApplyFill(const SVGStyle& style, const Rect2Df& bounds) {
        if (!style.fillGradientId.empty()) {
            SVGGradient* gradient = const_cast<SVGDocument&>(document).GetGradient(style.fillGradientId);
            if (gradient) {
                context->SetFillPaint(gradient->CreatePattern(context, bounds));
            }
        } else {
            Color fillColor = style.fillColor;
            fillColor.a = static_cast<uint8_t>(fillColor.a * style.fillOpacity);
            context->SetFillPaint(fillColor);
        }
    }

    void SVGElementRenderer::ApplyStroke(const SVGStyle& style, const Rect2Df& bounds) {
        if (!style.strokeGradientId.empty()) {
            SVGGradient* gradient = const_cast<SVGDocument&>(document).GetGradient(style.strokeGradientId);
            if (gradient) {
                context->SetStrokePaint(gradient->CreatePattern(context, bounds));
            }
        } else {
            Color strokeColor = style.strokeColor;
            strokeColor.a = static_cast<uint8_t>(strokeColor.a * style.strokeOpacity);
            context->SetStrokePaint(strokeColor);
        }

        context->SetStrokeWidth(style.strokeWidth);

        // Apply line cap
        switch (style.lineCap) {
            case SVGStyle::Butt:
                context->SetLineCap(LineCap::Butt);
                break;
            case SVGStyle::Round:
                context->SetLineCap(LineCap::Round);
                break;
            case SVGStyle::Square:
                context->SetLineCap(LineCap::Square);
                break;
        }

        // Apply line join
        switch (style.lineJoin) {
            case SVGStyle::Miter:
                context->SetLineJoin(LineJoin::Miter);
                break;
            case SVGStyle::RoundJoin:
                context->SetLineJoin(LineJoin::Round);
                break;
            case SVGStyle::Bevel:
                context->SetLineJoin(LineJoin::Bevel);
                break;
        }

        if (style.dashArray.size() > 0) {
            context->SetLineDash(style.dashArray, style.dashArray.size());
        }
    }

    SVGStyle SVGElementRenderer::ParseStyle(tinyxml2::XMLElement* elem) {
        SVGStyle style = styleStack.top();
        style.ParseFromAttributes(elem);
        return style;
    }

    SVGTransform SVGElementRenderer::ParseTransform(const std::string& transformStr) {
        SVGTransform transform;

        // Simple transform parser - handles basic cases
        std::regex transformRegex(R"((\w+)\s*\(([\d\s,.\-+e]+)\))");
        std::smatch match;

        if (std::regex_search(transformStr, match, transformRegex)) {
            std::string type = match[1];
            std::string params = match[2];

            // Parse parameters
            size_t pos = 0;
            transform.values = SVGPathParser::ParseNumbers(params, pos);

            // Determine transform type
            if (type == "matrix") {
                transform.type = SVGTransform::Matrix;
            } else if (type == "translate") {
                transform.type = SVGTransform::Translate;
            } else if (type == "scale") {
                transform.type = SVGTransform::Scale;
            } else if (type == "rotate") {
                transform.type = SVGTransform::Rotate;
            } else if (type == "skewX") {
                transform.type = SVGTransform::SkewX;
            } else if (type == "skewY") {
                transform.type = SVGTransform::SkewY;
            }
        }

        return transform;
    }

    std::vector<Point2Df> SVGElementRenderer::ParsePoints(const std::string& pointsStr) {
        std::vector<Point2Df> points;
        size_t pos = 0;
        std::vector<float> coords = SVGPathParser::ParseNumbers(pointsStr, pos);

        for (size_t i = 0; i + 1 < coords.size(); i += 2) {
            points.push_back({coords[i], coords[i + 1]});
        }

        return points;
    }

    Color SVGElementRenderer::ParseColor(const std::string& colorStr) {
        Color color{0, 0, 0, 255};

        if (colorStr[0] == '#') {
            unsigned int rgb = std::stoul(colorStr.substr(1), nullptr, 16);
            color.r = (rgb >> 16) & 0xFF;
            color.g = (rgb >> 8) & 0xFF;
            color.b = rgb & 0xFF;
        }
        // Add support for named colors and rgb() format as needed

        return color;
    }

    float SVGElementRenderer::ParseLength(const std::string& lengthStr, float reference) {
        if (lengthStr.empty()) return 0;

        // Simple length parser - handles px and %
        if (lengthStr.back() == '%') {
            float percentage = std::stof(lengthStr.substr(0, lengthStr.length() - 1));
            return percentage * reference / 100.0f;
        }

        // Remove unit suffix if present
        std::string numStr = lengthStr;
        if (numStr.find("px") != std::string::npos) {
            numStr = numStr.substr(0, numStr.find("px"));
        }

        return std::stof(numStr);
    }

    Rect2Df SVGElementRenderer::GetElementBounds(tinyxml2::XMLElement* elem) {
        // Simple bounds calculation - would need more sophisticated implementation
        std::string name = elem->Name();
        Rect2Df bounds{0, 0, 100, 100};

        if (name == "rect") {
            bounds.x = ParseFloatAttribute(elem, "x", 0);
            bounds.y = ParseFloatAttribute(elem, "y", 0);
            bounds.width = ParseFloatAttribute(elem, "width", 100);
            bounds.height = ParseFloatAttribute(elem, "height", 100);
        } else if (name == "circle") {
            float cx = ParseFloatAttribute(elem, "cx", 0);
            float cy = ParseFloatAttribute(elem, "cy", 0);
            float r = ParseFloatAttribute(elem, "r", 50);
            bounds = {cx - r, cy - r, 2 * r, 2 * r};
        } else if (name == "ellipse") {
            float cx = ParseFloatAttribute(elem, "cx", 0);
            float cy = ParseFloatAttribute(elem, "cy", 0);
            float rx = ParseFloatAttribute(elem, "rx", 50);
            float ry = ParseFloatAttribute(elem, "ry", 50);
            bounds = {cx - rx, cy - ry, 2 * rx, 2 * ry};
        } else if (name == "line") {
            float x1 = ParseFloatAttribute(elem, "x1", 0);
            float y1 = ParseFloatAttribute(elem, "y1", 0);
            float x2 = ParseFloatAttribute(elem, "x2", 100);
            float y2 = ParseFloatAttribute(elem, "y2", 100);
            bounds = {std::min(x1, x2), std::min(y1, y2),
                      std::abs(x2 - x1), std::abs(y2 - y1)};
        } else if (name == "polygon" || name == "polyline") {
            const char* pointsAttr = elem->Attribute("points");
            if (pointsAttr) {
                std::vector<Point2Df> points = ParsePoints(pointsAttr);
                if (!points.empty()) {
                    float minX = points[0].x, maxX = points[0].x;
                    float minY = points[0].y, maxY = points[0].y;

                    for (const auto& pt : points) {
                        minX = std::min(minX, pt.x);
                        maxX = std::max(maxX, pt.x);
                        minY = std::min(minY, pt.y);
                        maxY = std::max(maxY, pt.y);
                    }

                    bounds = {minX, minY, maxX - minX, maxY - minY};
                }
            }
        } else if (name == "path") {
            // For paths, we'd need to calculate bounds from path data
            // This is complex, so using default for now
            bounds = {0, 0, 100, 100};
        }

        return bounds;
    }

// UltraCanvasSVGElement implementation
    UltraCanvasSVGElement::UltraCanvasSVGElement(const std::string& identifier, long id, long x, long y, long w, long h = 24)
            : UltraCanvasUIElement(identifier, id, x, y, w, h),
            document(std::make_unique<SVGDocument>()) {
    }

    bool UltraCanvasSVGElement::LoadFromFile(const std::string& filepath) {
        return document->LoadFromFile(filepath);
    }

    bool UltraCanvasSVGElement::LoadFromString(const std::string& svgContent) {
        return document->LoadFromString(svgContent);
    }

    void UltraCanvasSVGElement::Render() {
        auto context = GetRenderContext();
        if (!document || !context) return;

        context->PushState();

        // Apply element transform
        Rect2Di bounds = GetBounds();

        // Apply scale
        // Handle aspect ratio
        if (preserveAspectRatio) {
            float docAspect = document->GetWidth() / document->GetHeight();
            float boundsAspect = static_cast<float>(bounds.width) / static_cast<float>(bounds.height);

            if (docAspect > boundsAspect) {
                // Document is wider - scale based on width
                float scaleFactor = static_cast<float>(bounds.width) / document->GetWidth() * scale;
                float pos_y = (bounds.height - document->GetHeight() * scaleFactor) / 2 + bounds.y;
                context->Translate(bounds.x, pos_y);
                context->Scale(scaleFactor, scaleFactor);
            } else {
                // Document is taller - scale based on height
                float scaleFactor = static_cast<float>(bounds.height) / document->GetHeight();
                float pos_x = (bounds.width - document->GetWidth() * scaleFactor) / 2 + bounds.x;
                context->Translate(pos_x, bounds.y);
                context->Scale(scaleFactor, scaleFactor);
            }
        } else {
            // Stretch to fill
            if (scale != 1.0f) {
                context->Scale(scale, scale);
            }
            context->Translate(bounds.x, bounds.y);
            float scaleX = static_cast<float>(bounds.width) / document->GetWidth();
            float scaleY = static_cast<float>(bounds.height) / document->GetHeight();
            context->Scale(scaleX, scaleY);
        }

        // Render the SVG
        SVGElementRenderer renderer(*document, context);
        renderer.Render();

        context->PopState();
    }

// UltraCanvasSVGPlugin implementation
//    UltraCanvasSVGPlugin::UltraCanvasSVGPlugin() {
//        instance = this;
//    }
//
//    UltraCanvasSVGPlugin::~UltraCanvasSVGPlugin() {
//        if (instance == this) {
//            instance = nullptr;
//        }
//    }
//
//    bool UltraCanvasSVGPlugin::Initialize() {
//        // Register SVG file extensions
//        RegisterFileExtension(".svg", "Scalable Vector Graphics");
//        RegisterFileExtension(".svgz", "Compressed SVG");
//
//        return true;
//    }
//
//    void UltraCanvasSVGPlugin::Shutdown() {
//        // Cleanup resources
//    }
//
//    std::unique_ptr<UltraCanvasSVGElement> UltraCanvasSVGPlugin::CreateSVGElement(const std::string& filepath) {
//        auto element = std::make_unique<UltraCanvasSVGElement>();
//        if (element->LoadFromFile(filepath)) {
//            return element;
//        }
//        return nullptr;
//    }

} // namespace UltraCanvas