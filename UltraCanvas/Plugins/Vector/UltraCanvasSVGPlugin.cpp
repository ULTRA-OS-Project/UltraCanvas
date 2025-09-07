// UltraCanvasSVGPlugin.cpp
// Lightweight SVG plugin implementation leveraging existing UltraCanvas infrastructure
// Version: 1.0.0
// Last Modified: 2025-07-17
// Author: UltraCanvas Framework

#include "UltraCanvasSVGPlugin.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

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
                return defaultValue;
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

std::vector<Point2D> SimpleSVGParser::ParsePoints(const std::string& pointsStr) {
    std::vector<Point2D> points;
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

std::vector<Point2D> SimpleSVGParser::ParsePath(const std::string& pathStr) {
    std::vector<Point2D> points;
    if (pathStr.empty()) return points;
    
    // Simplified path parsing - handles basic MoveTo and LineTo commands
    std::istringstream iss(pathStr);
    std::string token;
    Point2D current(0, 0);
    
    while (iss >> token) {
        if (token == "M" || token == "m") {
            // MoveTo
            float x, y;
            if (iss >> x >> y) {
                if (token == "m") {
                    current.x += x; current.y += y; // Relative
                } else {
                    current.x = x; current.y = y; // Absolute
                }
                points.push_back(current);
            }
        } else if (token == "L" || token == "l") {
            // LineTo
            float x, y;
            if (iss >> x >> y) {
                if (token == "l") {
                    current.x += x; current.y += y; // Relative
                } else {
                    current.x = x; current.y = y; // Absolute
                }
                points.push_back(current);
            }
        } else if (token == "Z" || token == "z") {
            // ClosePath - connect back to start
            if (!points.empty()) {
                points.push_back(points[0]);
            }
        }
        // Note: This is a basic implementation. Full SVG path parsing would handle
        // curves (C, Q, A), horizontal/vertical lines (H, V), etc.
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
    
    auto element = std::make_shared<SVGElement>(tagName);
    element->attributes = ParseAttributes();
    
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
void SVGElementRenderer::ApplyStyles(const SVGElement& element) {
    ctx->PushState();
    
    // Parse and apply fill
    std::string fill = element.attributes.Get("fill", "black");
    if (fill != "none") {
        Color fillColor = SimpleSVGParser::ParseColor(fill);
       ctx->SetFillColor(fillColor);
    }
    
    // Parse and apply stroke
    std::string stroke = element.attributes.Get("stroke", "none");
    if (stroke != "none") {
        Color strokeColor = SimpleSVGParser::ParseColor(stroke);
        float strokeWidth = element.attributes.GetFloat("stroke-width", 1.0f);
        
        ctx->SetStrokeColor(strokeColor);
        ctx->SetStrokeWidth(strokeWidth);
    }
    
    // Apply opacity
    float opacity = element.attributes.GetFloat("opacity", 1.0f);
    if (opacity < 1.0f) {
        SetGlobalAlpha(opacity);
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
    
    Rect2D rect(x, y, width, height);
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas functions
    if (element.attributes.Get("fill", "black") != "none") {
        if (rx > 0 || ry > 0) {
            ctx->FillRoundedRectangle(rect, std::max(rx, ry));
        } else {
            ctx->FillRectangle(rect);
        }
    }
    
    if (element.attributes.Get("stroke", "none") != "none") {
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
    
    Point2D center(cx, cy);
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas functions
    if (element.attributes.Get("fill", "black") != "none") {
        FillCircle(center, r);
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
    
    Rect2D ellipseRect(cx - rx, cy - ry, rx * 2, ry * 2);
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas functions
    if (element.attributes.Get("fill", "black") != "none") {
        FillEllipse(ellipseRect);
    }
    
    if (element.attributes.Get("stroke", "none") != "none") {
        DrawEllipse(ellipseRect);
    }
}

void SVGElementRenderer::RenderLine(const SVGElement& element) {
    float x1 = element.attributes.GetFloat("x1", 0.0f);
    float y1 = element.attributes.GetFloat("y1", 0.0f);
    float x2 = element.attributes.GetFloat("x2", 0.0f);
    float y2 = element.attributes.GetFloat("y2", 0.0f);
    
    Point2D start(x1, y1);
    Point2D end(x2, y2);
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas function
    ctx->DrawLine(start, end);
}

void SVGElementRenderer::RenderPolyline(const SVGElement& element) {
    std::string pointsStr = element.attributes.Get("points");
    if (pointsStr.empty()) return;
    
    std::vector<Point2D> points = SimpleSVGParser::ParsePoints(pointsStr);
    if (points.size() < 2) return;
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas drawing - draw as connected lines
    for (size_t i = 0; i < points.size() - 1; ++i) {
        ctx->DrawLine(points[i], points[i + 1]);
    }
}

void SVGElementRenderer::RenderPolygon(const SVGElement& element) {
    std::string pointsStr = element.attributes.Get("points");
    if (pointsStr.empty()) return;
    
    std::vector<Point2D> points = SimpleSVGParser::ParsePoints(pointsStr);
    if (points.size() < 3) return;
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas polygon function from UltraCanvasDrawingSurface
    if (element.attributes.Get("fill", "black") != "none") {
        // Use the existing DrawPolygon with fill from UltraCanvasDrawingSurface
        // For now, simulate with individual triangles from center
        Point2D center(0, 0);
        for (const Point2D& p : points) {
            center.x += p.x; center.y += p.y;
        }
        center.x /= points.size(); center.y /= points.size();
        
        for (size_t i = 0; i < points.size(); ++i) {
            size_t next = (i + 1) % points.size();
            // Create triangle: center -> point[i] -> point[next]
            std::vector<Point2D> triangle = {center, points[i], points[next]};
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

void SVGElementRenderer::RenderPath(const SVGElement& element) {
    std::string pathStr = element.attributes.Get("d");
    if (pathStr.empty()) return;
    
    std::vector<Point2D> points = SimpleSVGParser::ParsePath(pathStr);
    if (points.size() < 2) return;
    
    ApplyStyles(element);
    
    // Render as connected lines (simplified path rendering)
    for (size_t i = 0; i < points.size() - 1; ++i) {
        ctx->DrawLine(points[i], points[i + 1]);
    }
}

void SVGElementRenderer::RenderText(const SVGElement& element) {
    float x = element.attributes.GetFloat("x", 0.0f);
    float y = element.attributes.GetFloat("y", 0.0f);
    
    if (element.textContent.empty()) return;
    
    ApplyStyles(element);
    
    // Use existing UltraCanvas text function
    Point2D position(x, y);
    DrawText(element.textContent, position);
}

void SVGElementRenderer::RenderGroup(const SVGElement& element) {
    ctx->PushState();
    
    ApplyStyles(element);
    
    // Render all children
    for (const auto& child : element.children) {
        if (child) {
            RenderElement(*child);
        }
    }
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

void SVGElementRenderer::RenderDocument(const Rect2D& viewport) {
    ctx->PushState();
    
    if (!document.root) return;
    
    // Apply viewport transformation if needed
    if (document.hasViewBox) {
        // Calculate scale to fit viewBox in viewport
        float scaleX = viewport.width / document.viewBox.width;
        float scaleY = viewport.height / document.viewBox.height;
        float scale = std::min(scaleX, scaleY);
        
        // Apply transformation
        PushMatrix();
        Translate(viewport.x, viewport.y);
        Scale(scale, scale);
        Translate(-document.viewBox.x, -document.viewBox.y);
    }
    
    RenderElement(*document.root);
    
    if (document.hasViewBox) {
        PopMatrix();
    }
}

// ===== SVG UI ELEMENT IMPLEMENTATION =====
UltraCanvasSVGElement::UltraCanvasSVGElement(const std::string& identifier, long id, long x, long y, long w, long h)
    : UltraCanvasElement(identifier, id, x, y, w, h) {
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
    
    delete renderer;
    renderer = new SVGElementRenderer(*document);
    
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
    ctx->PushState();
    
    if (!renderer || !document) return;
    
    Rect2D viewport(0, 0, GetWidth(), GetHeight());
    
    if (scaleFactor != 1.0f) {
        PushMatrix();
        Scale(scaleFactor, scaleFactor);
    }
    
    renderer->RenderDocument(viewport);
    
    if (scaleFactor != 1.0f) {
        PopMatrix();
    }
}

void UltraCanvasSVGElement::UpdateSizeFromSVG() {
    if (!document) return;
    
    if (document->width > 0 && document->height > 0) {
        SetWidth(static_cast<long>(document->width));
        SetHeight(static_cast<long>(document->height));
    } else if (document->hasViewBox) {
        SetWidth(static_cast<long>(document->viewBox.width));
        SetHeight(static_cast<long>(document->viewBox.height));
    }
}

// ===== SVG PLUGIN IMPLEMENTATION =====
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

void UltraCanvasSVGPlugin::Render(const Rect2D& bounds) {
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

} // namespace UltraCanvas
