// Plugins/Diagrams/UltraCanvasCompositorDiagram.cpp
// Compositor / shader-style node graph editor - implementation.
// See header for the design rationale and API surface.
//
// Version: 0.1.0

#include <stdexcept>  // For std::runtime_error used (uninclude'd) by ImageCairo.h
#include "Plugins/Diagrams/UltraCanvasCompositorDiagram.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// FILE-LOCAL HELPERS
// =============================================================================

namespace {

// ----- JSON helpers (mirroring UltraCanvasNodeDiagram.cpp idiom) -------------

std::string EscapeJsonString(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    r.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    r += buf;
                } else {
                    r.push_back(c);
                }
        }
    }
    r.push_back('"');
    return r;
}

std::string ColorToJsonString(const Color& c) {
    std::ostringstream s;
    s << "[" << static_cast<int>(c.r) << "," << static_cast<int>(c.g)
      << "," << static_cast<int>(c.b) << "," << static_cast<int>(c.a) << "]";
    return s.str();
}

// Find the raw value substring for a key (returns "" if not found).
// Handles strings, arrays, objects, and bare numbers/bools/null.
std::string FindRawValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return "";
    char first = json[pos];
    size_t start = pos;
    if (first == '"') {
        ++pos;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) pos += 2;
            else ++pos;
        }
        if (pos < json.size()) ++pos;
        return json.substr(start, pos - start);
    }
    if (first == '[' || first == '{') {
        char open = first;
        char close = (first == '[') ? ']' : '}';
        int depth = 0;
        bool inStr = false;
        for (; pos < json.size(); ++pos) {
            char c = json[pos];
            if (inStr) {
                if (c == '\\' && pos + 1 < json.size()) { ++pos; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == open)  ++depth;
            if (c == close) {
                --depth;
                if (depth == 0) { ++pos; break; }
            }
        }
        return json.substr(start, pos - start);
    }
    while (pos < json.size() &&
           json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
           !std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    return json.substr(start, pos - start);
}

std::string ExtractStringValue(const std::string& json, const std::string& key) {
    std::string raw = FindRawValue(json, key);
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return "";
    std::string inner = raw.substr(1, raw.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            char n = inner[i + 1];
            switch (n) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                default:   out.push_back(n);
            }
            ++i;
        } else {
            out.push_back(inner[i]);
        }
    }
    return out;
}

double ExtractNumberValue(const std::string& json, const std::string& key, double dflt = 0.0) {
    std::string raw = FindRawValue(json, key);
    if (raw.empty()) return dflt;
    try { return std::stod(raw); } catch (...) { return dflt; }
}

bool ExtractBoolValue(const std::string& json, const std::string& key, bool dflt = false) {
    std::string raw = FindRawValue(json, key);
    if (raw == "true")  return true;
    if (raw == "false") return false;
    return dflt;
}

Color ExtractColorValue(const std::string& json, const std::string& key, const Color& fallback) {
    std::string raw = FindRawValue(json, key);
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return fallback;
    std::string inner = raw.substr(1, raw.size() - 2);
    int parts[4] = { fallback.r, fallback.g, fallback.b, fallback.a };
    int i = 0;
    size_t pos = 0;
    while (i < 4 && pos < inner.size()) {
        size_t comma = inner.find(',', pos);
        std::string tok = inner.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
        try { parts[i] = std::stoi(tok); } catch (...) {}
        if (comma == std::string::npos) break;
        pos = comma + 1;
        ++i;
    }
    auto clip = [](int v) { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
    return Color(clip(parts[0]), clip(parts[1]), clip(parts[2]), clip(parts[3]));
}

std::vector<std::string> ExtractObjectArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string raw = FindRawValue(json, key);
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return result;
    std::string inner = raw.substr(1, raw.size() - 2);
    int depth = 0;
    bool inStr = false;
    size_t start = std::string::npos;
    for (size_t i = 0; i < inner.size(); ++i) {
        char c = inner[i];
        if (inStr) {
            if (c == '\\' && i + 1 < inner.size()) { ++i; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') { if (depth == 0) start = i; ++depth; }
        else if (c == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                result.push_back(inner.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return result;
}

// ----- enum <-> string -------------------------------------------------------

const char* SocketDataTypeToString(SocketDataType t) {
    switch (t) {
        case SocketDataType::Any:      return "any";
        case SocketDataType::Image:    return "image";
        case SocketDataType::Color:    return "color";
        case SocketDataType::Vector:   return "vector";
        case SocketDataType::Scalar:   return "scalar";
        case SocketDataType::Boolean:     return "bool";
        case SocketDataType::String:   return "string";
        case SocketDataType::Geometry: return "geometry";
        case SocketDataType::Shader:   return "shader";
        case SocketDataType::Audio:    return "audio";
        case SocketDataType::Data:     return "data";
        case SocketDataType::Video:    return "video";
        case SocketDataType::Custom:   return "custom";
    }
    return "any";
}

const char* LinkStyleToCompositorString(LinkStyle s) {
    switch (s) {
        case LinkStyle::Straight:   return "straight";
        case LinkStyle::Bezier:     return "bezier";
        case LinkStyle::SmoothStep: return "smoothstep";
        case LinkStyle::Step:       return "step";
    }
    return "bezier";
}

LinkStyle LinkStyleFromCompositorString(const std::string& s) {
    if (s == "straight")   return LinkStyle::Straight;
    if (s == "smoothstep") return LinkStyle::SmoothStep;
    if (s == "step")       return LinkStyle::Step;
    return LinkStyle::Bezier;
}

// ----- color tweak -----------------------------------------------------------

Color Lighten(const Color& c, int delta) {
    auto clip = [](int v) { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
    return Color(clip(c.r + delta), clip(c.g + delta), clip(c.b + delta), c.a);
}

// ----- bezier / step path construction --------------------------------------

void BuildBezierPath(const Point2Df& src, const Point2Df& dst,
                     std::vector<Point2Df>& outPath, int samples = 24) {
    outPath.clear();
    outPath.reserve(samples + 1);
    double dx = std::max(40.0, std::min(160.0, std::abs(dst.x - src.x) * 0.5));
    Point2Df cp1(src.x + dx, src.y);
    Point2Df cp2(dst.x - dx, dst.y);
    for (int i = 0; i <= samples; ++i) {
        double t  = static_cast<double>(i) / samples;
        double u  = 1.0 - t;
        double b0 = u * u * u;
        double b1 = 3 * u * u * t;
        double b2 = 3 * u * t * t;
        double b3 = t * t * t;
        outPath.push_back(Point2Df(
            b0 * src.x + b1 * cp1.x + b2 * cp2.x + b3 * dst.x,
            b0 * src.y + b1 * cp1.y + b2 * cp2.y + b3 * dst.y));
    }
}

void BuildStepPath(const Point2Df& src, const Point2Df& dst,
                   std::vector<Point2Df>& outPath) {
    outPath.clear();
    double midX = (src.x + dst.x) * 0.5;
    outPath.push_back(src);
    outPath.push_back(Point2Df(midX, src.y));
    outPath.push_back(Point2Df(midX, dst.y));
    outPath.push_back(dst);
}

double DistancePointToSegment(const Point2Df& p, const Point2Df& a, const Point2Df& b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-6) {
        double ex = p.x - a.x;
        double ey = p.y - a.y;
        return std::sqrt(ex * ex + ey * ey);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));
    double cx = a.x + t * dx;
    double cy = a.y + t * dy;
    double ex = p.x - cx;
    double ey = p.y - cy;
    return std::sqrt(ex * ex + ey * ey);
}

// ----- short numeric formatting for in-place widget text --------------------

std::string FormatNumberShort(double v, double step) {
    int decimals = 2;
    if (step >= 1.0) decimals = 0;
    else if (step >= 0.1) decimals = 1;
    else if (step >= 0.01) decimals = 2;
    else decimals = 3;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} // anonymous namespace

// =============================================================================
// CONSTRUCTOR
// =============================================================================

UltraCanvasCompositorDiagram::UltraCanvasCompositorDiagram(
        const std::string& id, int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {

    auto seed = [this](SocketDataType t, const char* name, Color c) {
        SocketTypeInfo info;
        info.type = t;
        info.displayName = name;
        info.color = c;
        info.borderColor = Color(20, 20, 20, 255);
        info.radius = 5.0f;
        socketTypes[static_cast<int>(t)] = info;
    };
    seed(SocketDataType::Any,      "Any",      Color(200, 200, 200, 255));
    seed(SocketDataType::Image,    "Image",    Color(240, 150,  80, 255));
    seed(SocketDataType::Color,    "Color",    Color(240, 200,  80, 255));
    seed(SocketDataType::Vector,   "Vector",   Color(120, 110, 220, 255));
    seed(SocketDataType::Scalar,   "Scalar",   Color(160, 200, 220, 255));
    seed(SocketDataType::Boolean,     "Bool",     Color(150, 220, 150, 255));
    seed(SocketDataType::String,   "String",   Color(120, 200, 200, 255));
    seed(SocketDataType::Geometry, "Geometry", Color( 90, 200, 130, 255));
    seed(SocketDataType::Shader,   "Shader",   Color( 80, 180, 200, 255));
    seed(SocketDataType::Audio,    "Audio",    Color(220, 150, 150, 255));
    seed(SocketDataType::Data,     "Data",     Color(220, 220, 160, 255));
    seed(SocketDataType::Video,    "Video",    Color(220, 130, 200, 255));  // magenta
}

// =============================================================================
// SOCKET TYPE REGISTRY
// =============================================================================

void UltraCanvasCompositorDiagram::RegisterSocketType(const SocketTypeInfo& info) {
    socketTypes[static_cast<int>(info.type)] = info;
}

void UltraCanvasCompositorDiagram::RegisterCustomSocketType(
        const std::string& customTag, const SocketTypeInfo& info) {
    SocketTypeInfo copy = info;
    copy.type = SocketDataType::Custom;
    copy.customTag = customTag;
    customSocketTypes[customTag] = copy;
}

const SocketTypeInfo* UltraCanvasCompositorDiagram::GetSocketTypeInfo(
        SocketDataType type, const std::string& customTag) const {
    if (type == SocketDataType::Custom && !customTag.empty()) {
        auto it = customSocketTypes.find(customTag);
        if (it != customSocketTypes.end()) return &it->second;
    }
    auto it = socketTypes.find(static_cast<int>(type));
    if (it != socketTypes.end()) return &it->second;
    return nullptr;
}

bool UltraCanvasCompositorDiagram::AreSocketsCompatible(
        const CompositorSocketSpec& src, const CompositorSocketSpec& dst) const {
    if (compatibilityChecker) return compatibilityChecker(src, dst);
    if (src.type == SocketDataType::Any || dst.type == SocketDataType::Any) return true;
    if (src.type != dst.type) return false;
    if (src.type == SocketDataType::Custom) return src.customTag == dst.customTag;
    return true;
}

void UltraCanvasCompositorDiagram::SetSocketCompatibilityChecker(
        std::function<bool(const CompositorSocketSpec&,
                           const CompositorSocketSpec&)> checker) {
    compatibilityChecker = std::move(checker);
}

// =============================================================================
// TEMPLATE MANAGEMENT
// =============================================================================

void UltraCanvasCompositorDiagram::RegisterTemplate(const CompositorNodeTemplate& tmpl) {
    templates[tmpl.id] = tmpl;
}

void UltraCanvasCompositorDiagram::RemoveTemplate(const std::string& templateId) {
    templates.erase(templateId);
}

const CompositorNodeTemplate* UltraCanvasCompositorDiagram::GetTemplate(
        const std::string& templateId) const {
    auto it = templates.find(templateId);
    return it == templates.end() ? nullptr : &it->second;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetAllTemplateIds() const {
    std::vector<std::string> ids;
    ids.reserve(templates.size());
    for (const auto& kv : templates) ids.push_back(kv.first);
    return ids;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetTemplateIdsByCategory(
        const std::string& category) const {
    std::vector<std::string> ids;
    for (const auto& kv : templates) {
        if (kv.second.category == category) ids.push_back(kv.first);
    }
    return ids;
}

// =============================================================================
// NODE MANAGEMENT
// =============================================================================

namespace {

ParamValue SeedFromSpec(const CompositorParamSpec& spec) {
    ParamValue v;
    v.kind = spec.valueKind;
    v.number = spec.defaultNumber;
    v.boolean = spec.defaultBool;
    v.text = spec.defaultText;
    v.color = spec.defaultColor;
    v.dropdownIndex = spec.defaultDropdownIndex;
    std::memcpy(v.vec, spec.defaultVec, sizeof(v.vec));
    return v;
}

const CompositorParamSpec* FindParamSpec(const CompositorNodeTemplate& tmpl,
                                          const std::string& paramId) {
    for (const auto& p : tmpl.params) {
        if (p.id == paramId) return &p;
    }
    return nullptr;
}

} // anonymous namespace

bool UltraCanvasCompositorDiagram::AddNode(const std::string& nodeId,
                                            const std::string& templateId,
                                            double x, double y) {
    const auto* tmpl = GetTemplate(templateId);
    if (!tmpl) return false;
    if (nodes.count(nodeId)) return false;

    CompositorNode node(nodeId, templateId, x, y);
    node.width = tmpl->defaultWidth;
    for (const auto& spec : tmpl->params) {
        node.paramValues[spec.id] = SeedFromSpec(spec);
    }
    nodes[nodeId] = node;
    nodeOrder.push_back(nodeId);
    RequestRedraw();
    return true;
}

void UltraCanvasCompositorDiagram::AddNode(const CompositorNode& node) {
    if (nodes.count(node.id)) return;
    CompositorNode copy = node;
    const auto* tmpl = GetTemplate(copy.templateId);
    if (tmpl) {
        if (copy.width <= 0.0) copy.width = tmpl->defaultWidth;
        for (const auto& spec : tmpl->params) {
            if (!copy.paramValues.count(spec.id)) {
                copy.paramValues[spec.id] = SeedFromSpec(spec);
            }
        }
    }
    nodes[copy.id] = copy;
    nodeOrder.push_back(copy.id);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::RemoveNode(const std::string& nodeId) {
    if (!nodes.count(nodeId)) return;
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const CompositorLink& l) {
            bool dead = (l.sourceNodeId == nodeId || l.targetNodeId == nodeId);
            if (dead && onLinkRemoved) onLinkRemoved(l.id);
            return dead;
        }), links.end());
    nodes.erase(nodeId);
    nodeOrder.erase(std::remove(nodeOrder.begin(), nodeOrder.end(), nodeId),
                    nodeOrder.end());
    selectedNodes.erase(nodeId);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::UpdateNodePosition(const std::string& nodeId,
                                                       double x, double y) {
    if (auto* n = GetNode(nodeId)) {
        n->x = x;
        n->y = y;
        RequestRedraw();
    }
}

void UltraCanvasCompositorDiagram::SetNodeCollapsed(const std::string& nodeId, bool collapsed) {
    if (auto* n = GetNode(nodeId)) {
        n->collapsed = collapsed;
        RequestRedraw();
    }
}

void UltraCanvasCompositorDiagram::SetNodeTitleOverride(const std::string& nodeId,
                                                         const std::string& title) {
    if (auto* n = GetNode(nodeId)) {
        n->titleOverride = title;
        RequestRedraw();
    }
}

void UltraCanvasCompositorDiagram::SetNodePreviewHandle(const std::string& nodeId,
                                                         const std::string& handle) {
    if (auto* n = GetNode(nodeId)) {
        n->previewHandle = handle;
        RequestRedraw();
    }
}

CompositorNode* UltraCanvasCompositorDiagram::GetNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    return it == nodes.end() ? nullptr : &it->second;
}

const CompositorNode* UltraCanvasCompositorDiagram::GetNode(const std::string& nodeId) const {
    auto it = nodes.find(nodeId);
    return it == nodes.end() ? nullptr : &it->second;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetAllNodeIds() const {
    return nodeOrder;
}

// =============================================================================
// PARAMETER VALUES
// =============================================================================

bool UltraCanvasCompositorDiagram::SetParamNumber(const std::string& nodeId,
                                                    const std::string& paramId,
                                                    double value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    auto& v = node->paramValues[paramId];
    if (v.kind == ParamValueKind::NoValue) v.kind = ParamValueKind::Number;
    v.number = value;
    if (onParamChange) onParamChange(nodeId, paramId, v);
    RequestRedraw();
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamBool(const std::string& nodeId,
                                                  const std::string& paramId, bool value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    auto& v = node->paramValues[paramId];
    if (v.kind == ParamValueKind::NoValue) v.kind = ParamValueKind::Boolean;
    v.boolean = value;
    if (onParamChange) onParamChange(nodeId, paramId, v);
    RequestRedraw();
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamText(const std::string& nodeId,
                                                  const std::string& paramId,
                                                  const std::string& value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    auto& v = node->paramValues[paramId];
    if (v.kind == ParamValueKind::NoValue) v.kind = ParamValueKind::Text;
    v.text = value;
    if (onParamChange) onParamChange(nodeId, paramId, v);
    RequestRedraw();
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamColor(const std::string& nodeId,
                                                   const std::string& paramId,
                                                   const Color& value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    auto& v = node->paramValues[paramId];
    if (v.kind == ParamValueKind::NoValue) v.kind = ParamValueKind::Color;
    v.color = value;
    if (onParamChange) onParamChange(nodeId, paramId, v);
    RequestRedraw();
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamDropdownIndex(const std::string& nodeId,
                                                          const std::string& paramId,
                                                          int index) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    auto& v = node->paramValues[paramId];
    v.dropdownIndex = index;
    if (onParamChange) onParamChange(nodeId, paramId, v);
    RequestRedraw();
    return true;
}

const ParamValue* UltraCanvasCompositorDiagram::GetParamValue(
        const std::string& nodeId, const std::string& paramId) const {
    const auto* node = GetNode(nodeId);
    if (!node) return nullptr;
    auto it = node->paramValues.find(paramId);
    return it == node->paramValues.end() ? nullptr : &it->second;
}

// =============================================================================
// SOCKET RESOLUTION (internal)
// =============================================================================

namespace {

// Returns the socket spec for a given id within a template, possibly
// synthesized from a param row's input socket. Storage is provided by caller.
const CompositorSocketSpec* ResolveSocketSpec(
        const CompositorNodeTemplate& tmpl, const std::string& socketId,
        bool& outIsOutput, CompositorSocketSpec& storage) {
    for (const auto& s : tmpl.outputs) {
        if (s.id == socketId) { outIsOutput = true;  return &s; }
    }
    for (const auto& s : tmpl.inputs) {
        if (s.id == socketId) { outIsOutput = false; return &s; }
    }
    for (const auto& p : tmpl.params) {
        if (p.hasInputSocket && p.id == socketId) {
            storage.id = p.id;
            storage.label = p.label;
            storage.type = p.inputType;
            storage.customTag = p.inputCustomTag;
            storage.maxConnections = 1;
            storage.visibleWhenCollapsed = false;
            outIsOutput = false;
            return &storage;
        }
    }
    return nullptr;
}

} // anonymous namespace

bool UltraCanvasCompositorDiagram::ResolveSocketWorldPosition(
        const CompositorNode& node, const CompositorNodeTemplate& tmpl,
        const std::string& socketId, bool& outIsOutput, Point2Df& outPos) const {
    for (size_t i = 0; i < tmpl.outputs.size(); ++i) {
        if (tmpl.outputs[i].id == socketId) {
            outIsOutput = true;
            outPos = GetSocketWorldPosition(node, tmpl, true, static_cast<int>(i));
            return true;
        }
    }
    for (size_t i = 0; i < tmpl.inputs.size(); ++i) {
        if (tmpl.inputs[i].id == socketId) {
            outIsOutput = false;
            outPos = GetSocketWorldPosition(node, tmpl, false, static_cast<int>(i));
            return true;
        }
    }
    // Param-row sockets (synthesized).
    NodeLayout L = ComputeNodeLayout(node, tmpl);
    for (size_t i = 0; i < tmpl.params.size(); ++i) {
        const auto& p = tmpl.params[i];
        if (!p.hasInputSocket || p.id != socketId) continue;
        if (i >= L.rowBounds.size()) return false;
        const auto& r = L.rowBounds[i];
        outIsOutput = false;
        outPos = Point2Df(r.x, r.y + r.height * 0.5);
        return true;
    }
    return false;
}

// =============================================================================
// LINK MANAGEMENT
// =============================================================================

bool UltraCanvasCompositorDiagram::AddLink(
        const std::string& linkId,
        const std::string& sourceNodeId, const std::string& sourceSocketId,
        const std::string& targetNodeId, const std::string& targetSocketId) {
    CompositorLink l;
    l.id = linkId;
    l.sourceNodeId = sourceNodeId;
    l.sourceSocketId = sourceSocketId;
    l.targetNodeId = targetNodeId;
    l.targetSocketId = targetSocketId;
    return AddLink(l);
}

bool UltraCanvasCompositorDiagram::AddLink(const CompositorLink& link) {
    if (link.sourceNodeId == link.targetNodeId) return false;
    const auto* srcNode = GetNode(link.sourceNodeId);
    const auto* dstNode = GetNode(link.targetNodeId);
    if (!srcNode || !dstNode) return false;
    const auto* srcTmpl = GetTemplate(srcNode->templateId);
    const auto* dstTmpl = GetTemplate(dstNode->templateId);
    if (!srcTmpl || !dstTmpl) return false;

    CompositorSocketSpec srcStorage, dstStorage;
    bool srcIsOutput = false, dstIsOutput = false;
    const auto* srcSocket = ResolveSocketSpec(*srcTmpl, link.sourceSocketId, srcIsOutput, srcStorage);
    const auto* dstSocket = ResolveSocketSpec(*dstTmpl, link.targetSocketId, dstIsOutput, dstStorage);
    if (!srcSocket || !dstSocket) return false;
    if (!srcIsOutput || dstIsOutput) {
        if (onConnectionRejected) onConnectionRejected(*srcSocket, *dstSocket);
        return false;
    }
    if (!AreSocketsCompatible(*srcSocket, *dstSocket)) {
        if (onConnectionRejected) onConnectionRejected(*srcSocket, *dstSocket);
        return false;
    }

    auto countOnSocket = [&](const std::string& nid, const std::string& sid) {
        int c = 0;
        for (const auto& ex : links) {
            if ((ex.sourceNodeId == nid && ex.sourceSocketId == sid) ||
                (ex.targetNodeId == nid && ex.targetSocketId == sid)) ++c;
        }
        return c;
    };
    if (srcSocket->maxConnections > 0 &&
        countOnSocket(link.sourceNodeId, link.sourceSocketId) >= srcSocket->maxConnections) {
        return false;
    }
    if (dstSocket->maxConnections > 0 &&
        countOnSocket(link.targetNodeId, link.targetSocketId) >= dstSocket->maxConnections) {
        RemoveLinksOnSocket(link.targetNodeId, link.targetSocketId);
    }

    links.push_back(link);
    if (onLinkCreated) onLinkCreated(links.back());
    RequestRedraw();
    return true;
}

void UltraCanvasCompositorDiagram::RemoveLink(const std::string& linkId) {
    auto it = std::find_if(links.begin(), links.end(),
        [&](const CompositorLink& l) { return l.id == linkId; });
    if (it == links.end()) return;
    if (onLinkRemoved) onLinkRemoved(linkId);
    links.erase(it);
    selectedLinks.erase(linkId);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::RemoveLinksOnSocket(const std::string& nodeId,
                                                        const std::string& socketId) {
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const CompositorLink& l) {
            bool dead = (l.sourceNodeId == nodeId && l.sourceSocketId == socketId) ||
                        (l.targetNodeId == nodeId && l.targetSocketId == socketId);
            if (dead && onLinkRemoved) onLinkRemoved(l.id);
            return dead;
        }), links.end());
    RequestRedraw();
}

CompositorLink* UltraCanvasCompositorDiagram::GetLink(const std::string& linkId) {
    for (auto& l : links) if (l.id == linkId) return &l;
    return nullptr;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetAllLinkIds() const {
    std::vector<std::string> ids;
    ids.reserve(links.size());
    for (const auto& l : links) ids.push_back(l.id);
    return ids;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetLinksForSocket(
        const std::string& nodeId, const std::string& socketId) const {
    std::vector<std::string> ids;
    for (const auto& l : links) {
        if ((l.sourceNodeId == nodeId && l.sourceSocketId == socketId) ||
            (l.targetNodeId == nodeId && l.targetSocketId == socketId)) {
            ids.push_back(l.id);
        }
    }
    return ids;
}

// =============================================================================
// STYLING
// =============================================================================

void UltraCanvasCompositorDiagram::SetStyle(const CompositorDiagramStyle& s) {
    style = s;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetBackgroundColor(const Color& color) {
    style.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    if (spacing > 0.0) style.gridSpacing = spacing;
    RequestRedraw();
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasCompositorDiagram::SelectNode(const std::string& nodeId, bool addToSelection) {
    if (!nodes.count(nodeId)) return;
    if (!addToSelection) {
        selectedNodes.clear();
        selectedLinks.clear();
    }
    selectedNodes.insert(nodeId);
    // Raise z-order so the newly-selected node draws on top.
    auto it = std::find(nodeOrder.begin(), nodeOrder.end(), nodeId);
    if (it != nodeOrder.end()) {
        nodeOrder.erase(it);
        nodeOrder.push_back(nodeId);
    }
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SelectLink(const std::string& linkId, bool addToSelection) {
    if (!GetLink(linkId)) return;
    if (!addToSelection) {
        selectedNodes.clear();
        selectedLinks.clear();
    }
    selectedLinks.insert(linkId);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SelectAll() {
    selectedNodes.clear();
    selectedLinks.clear();
    for (const auto& id : nodeOrder) selectedNodes.insert(id);
    for (const auto& l : links) selectedLinks.insert(l.id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::DeselectAll() {
    if (selectedNodes.empty() && selectedLinks.empty()) return;
    selectedNodes.clear();
    selectedLinks.clear();
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::DeleteSelected() {
    std::vector<std::string> nodesToKill(selectedNodes.begin(), selectedNodes.end());
    std::vector<std::string> linksToKill(selectedLinks.begin(), selectedLinks.end());
    for (const auto& id : linksToKill) RemoveLink(id);
    for (const auto& id : nodesToKill) RemoveNode(id);
    NotifySelectionChange();
}

void UltraCanvasCompositorDiagram::Clear() {
    nodes.clear();
    nodeOrder.clear();
    links.clear();
    selectedNodes.clear();
    selectedLinks.clear();
    NotifySelectionChange();
    RequestRedraw();
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetSelectedNodeIds() const {
    return std::vector<std::string>(selectedNodes.begin(), selectedNodes.end());
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetSelectedLinkIds() const {
    return std::vector<std::string>(selectedLinks.begin(), selectedLinks.end());
}

bool UltraCanvasCompositorDiagram::IsNodeSelected(const std::string& nodeId) const {
    return selectedNodes.count(nodeId) > 0;
}

bool UltraCanvasCompositorDiagram::IsLinkSelected(const std::string& linkId) const {
    return selectedLinks.count(linkId) > 0;
}

// =============================================================================
// VIEWPORT
// =============================================================================

void UltraCanvasCompositorDiagram::SetZoomLevel(double zoom) {
    zoomLevel = zoom;
    ClampZoom();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetPanOffset(double x, double y) {
    panOffset.x = x;
    panOffset.y = y;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ZoomIn(double factor) {
    zoomLevel *= factor;
    ClampZoom();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ZoomOut(double factor) {
    zoomLevel /= factor;
    ClampZoom();
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::FitView(double padding) {
    if (nodes.empty()) return;
    double minX =  1e18, minY =  1e18;
    double maxX = -1e18, maxY = -1e18;
    for (const auto& kv : nodes) {
        const auto& n = kv.second;
        const auto* tmpl = GetTemplate(n.templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(n, *tmpl);
        minX = std::min(minX, L.bounds.x);
        minY = std::min(minY, L.bounds.y);
        maxX = std::max(maxX, L.bounds.x + L.bounds.width);
        maxY = std::max(maxY, L.bounds.y + L.bounds.height);
    }
    if (minX > maxX || minY > maxY) return;
    double contentW = std::max(1.0, maxX - minX);
    double contentH = std::max(1.0, maxY - minY);
    double availW = std::max(1.0, static_cast<double>(GetWidth())  - 2 * padding);
    double availH = std::max(1.0, static_cast<double>(GetHeight()) - 2 * padding);
    zoomLevel = std::min(availW / contentW, availH / contentH);
    ClampZoom();
    double cx = (minX + maxX) * 0.5;
    double cy = (minY + maxY) * 0.5;
    panOffset.x = GetWidth()  * 0.5 - cx * zoomLevel;
    panOffset.y = GetHeight() * 0.5 - cy * zoomLevel;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::CenterOn(double worldX, double worldY) {
    panOffset.x = GetWidth()  * 0.5 - worldX * zoomLevel;
    panOffset.y = GetHeight() * 0.5 - worldY * zoomLevel;
    RequestRedraw();
}

// =============================================================================
// LAYOUT COMPUTATION
// =============================================================================

UltraCanvasCompositorDiagram::NodeLayout
UltraCanvasCompositorDiagram::ComputeNodeLayout(
        const CompositorNode& node, const CompositorNodeTemplate& tmpl) const {
    NodeLayout L;
    double w = (node.width > 0.0) ? node.width : tmpl.defaultWidth;
    double rowH = style.rowHeight;
    double headerH = style.headerHeight;

    int nOut = static_cast<int>(tmpl.outputs.size());
    int nParam = static_cast<int>(tmpl.params.size());
    int nIn = static_cast<int>(tmpl.inputs.size());

    double bodyRowsH = (nOut + nParam + nIn) * rowH;
    double previewH = (tmpl.hasPreview && !node.collapsed) ? tmpl.previewHeight : 0.0;
    double previewPad = (previewH > 0.0) ? 6.0 : 0.0;
    double bodyH = node.collapsed ? 0.0 : (bodyRowsH + previewH + previewPad);
    double totalH = headerH + bodyH + 2.0;

    L.bounds       = Rect2Df(node.x, node.y, w, totalH);
    L.headerBounds = Rect2Df(node.x, node.y, w, headerH);
    L.bodyBounds   = Rect2Df(node.x, node.y + headerH, w, bodyH);

    if (node.collapsed) return L;

    L.outputSocketBounds.reserve(nOut);
    L.inputSocketBounds.reserve(nIn);
    L.rowBounds.reserve(nParam);
    L.rowWidgetBounds.reserve(nParam);

    const SocketTypeInfo* anyInfo = GetSocketTypeInfo(SocketDataType::Any);
    double defaultDotR = anyInfo ? anyInfo->radius : 5.0;
    double y = node.y + headerH;

    for (int i = 0; i < nOut; ++i) {
        const auto& sock = tmpl.outputs[i];
        const auto* info = GetSocketTypeInfo(sock.type, sock.customTag);
        double r = info ? info->radius : defaultDotR;
        double cx = node.x + w;
        double cy = y + rowH * 0.5;
        L.outputSocketBounds.push_back(Rect2Df(cx - r, cy - r, 2 * r, 2 * r));
        y += rowH;
    }

    for (int i = 0; i < nParam; ++i) {
        const auto& spec = tmpl.params[i];
        Rect2Df rowR(node.x, y, w, rowH);
        L.rowBounds.push_back(rowR);
        if (spec.widget == ParamWidgetKind::NoWidget) {
            L.rowWidgetBounds.push_back(Rect2Df(0, 0, 0, 0));
        } else {
            double widgetW = std::max(style.widgetMinWidth, w * 0.45);
            double widgetH = rowH - 6.0;
            double widgetX = node.x + w - style.rowPaddingX - widgetW;
            double widgetY = y + 3.0;
            L.rowWidgetBounds.push_back(Rect2Df(widgetX, widgetY, widgetW, widgetH));
        }
        y += rowH;
    }

    for (int i = 0; i < nIn; ++i) {
        const auto& sock = tmpl.inputs[i];
        const auto* info = GetSocketTypeInfo(sock.type, sock.customTag);
        double r = info ? info->radius : defaultDotR;
        double cx = node.x;
        double cy = y + rowH * 0.5;
        L.inputSocketBounds.push_back(Rect2Df(cx - r, cy - r, 2 * r, 2 * r));
        y += rowH;
    }

    if (tmpl.hasPreview) {
        L.previewBounds = Rect2Df(node.x + style.rowPaddingX,
                                   y + previewPad,
                                   w - 2 * style.rowPaddingX,
                                   tmpl.previewHeight);
    }
    return L;
}

Point2Df UltraCanvasCompositorDiagram::GetSocketWorldPosition(
        const CompositorNode& node, const CompositorNodeTemplate& tmpl,
        bool isOutput, int socketIndex) const {
    NodeLayout L = ComputeNodeLayout(node, tmpl);
    if (isOutput) {
        if (socketIndex < 0 || socketIndex >= static_cast<int>(L.outputSocketBounds.size()))
            return Point2Df(node.x + ((node.width > 0) ? node.width : tmpl.defaultWidth), node.y);
        const auto& r = L.outputSocketBounds[socketIndex];
        return Point2Df(r.x + r.width * 0.5, r.y + r.height * 0.5);
    } else {
        if (socketIndex < 0 || socketIndex >= static_cast<int>(L.inputSocketBounds.size()))
            return Point2Df(node.x, node.y);
        const auto& r = L.inputSocketBounds[socketIndex];
        return Point2Df(r.x + r.width * 0.5, r.y + r.height * 0.5);
    }
}

// =============================================================================
// HIT TESTING
// =============================================================================

std::string UltraCanvasCompositorDiagram::FindNodeAt(const Point2Di& screenPos) {
    Point2Df world = ScreenToWorld(screenPos);
    // Walk top-down (highest z first) so the visually-topmost node wins.
    for (auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it) {
        const auto* n = GetNode(*it);
        if (!n) continue;
        const auto* tmpl = GetTemplate(n->templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(*n, *tmpl);
        if (L.bounds.Contains(world)) return *it;
    }
    return "";
}

std::string UltraCanvasCompositorDiagram::FindLinkAt(const Point2Di& screenPos) {
    Point2Df world = ScreenToWorld(screenPos);
    // Pick threshold scales inversely with zoom so distant zoomed-out lines
    // remain easy to click.
    double pickThresh = 6.0 / std::max(0.1, zoomLevel);
    for (const auto& l : links) {
        const auto* sn = GetNode(l.sourceNodeId);
        const auto* dn = GetNode(l.targetNodeId);
        if (!sn || !dn) continue;
        const auto* st = GetTemplate(sn->templateId);
        const auto* dt = GetTemplate(dn->templateId);
        if (!st || !dt) continue;
        bool sIsOut = false, dIsOut = false;
        Point2Df sp, dp;
        if (!ResolveSocketWorldPosition(*sn, *st, l.sourceSocketId, sIsOut, sp)) continue;
        if (!ResolveSocketWorldPosition(*dn, *dt, l.targetSocketId, dIsOut, dp)) continue;
        std::vector<Point2Df> path;
        switch (l.style) {
            case LinkStyle::Straight:   path = { sp, dp }; break;
            case LinkStyle::Step:
            case LinkStyle::SmoothStep: BuildStepPath(sp, dp, path); break;
            case LinkStyle::Bezier:
            default:                    BuildBezierPath(sp, dp, path, 16); break;
        }
        for (size_t i = 1; i < path.size(); ++i) {
            if (DistancePointToSegment(world, path[i - 1], path[i]) <= pickThresh) {
                return l.id;
            }
        }
    }
    return "";
}

UltraCanvasCompositorDiagram::SocketHit
UltraCanvasCompositorDiagram::FindSocketAt(const Point2Di& screenPos) {
    SocketHit hit;
    Point2Df world = ScreenToWorld(screenPos);
    double slop = 4.0;  // world-space extra hit radius
    // Reverse z-order so a socket on a node-on-top wins over an obscured one.
    for (auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it) {
        const auto* n = GetNode(*it);
        if (!n) continue;
        const auto* tmpl = GetTemplate(n->templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(*n, *tmpl);

        // Outputs
        for (size_t i = 0; i < L.outputSocketBounds.size(); ++i) {
            const auto& r = L.outputSocketBounds[i];
            double cx = r.x + r.width * 0.5;
            double cy = r.y + r.height * 0.5;
            double dx = world.x - cx, dy = world.y - cy;
            double rr = r.width * 0.5 + slop;
            if (dx * dx + dy * dy <= rr * rr) {
                hit.nodeId = *it;
                hit.socketId = tmpl->outputs[i].id;
                hit.isOutput = true;
                return hit;
            }
        }

        // Inputs (explicit list)
        for (size_t i = 0; i < L.inputSocketBounds.size(); ++i) {
            const auto& r = L.inputSocketBounds[i];
            double cx = r.x + r.width * 0.5;
            double cy = r.y + r.height * 0.5;
            double dx = world.x - cx, dy = world.y - cy;
            double rr = r.width * 0.5 + slop;
            if (dx * dx + dy * dy <= rr * rr) {
                hit.nodeId = *it;
                hit.socketId = tmpl->inputs[i].id;
                hit.isOutput = false;
                return hit;
            }
        }

        // Param input sockets
        for (size_t i = 0; i < tmpl->params.size() && i < L.rowBounds.size(); ++i) {
            const auto& p = tmpl->params[i];
            if (!p.hasInputSocket) continue;
            const auto& rb = L.rowBounds[i];
            const auto* info = GetSocketTypeInfo(p.inputType, p.inputCustomTag);
            double radius = info ? info->radius : 5.0;
            double cx = rb.x;
            double cy = rb.y + rb.height * 0.5;
            double dx = world.x - cx, dy = world.y - cy;
            double rr = radius + slop;
            if (dx * dx + dy * dy <= rr * rr) {
                hit.nodeId = *it;
                hit.socketId = p.id;
                hit.isOutput = false;
                return hit;
            }
        }
    }
    return hit;
}

UltraCanvasCompositorDiagram::WidgetHit
UltraCanvasCompositorDiagram::FindRowWidgetAt(const Point2Di& screenPos) {
    WidgetHit hit;
    Point2Df world = ScreenToWorld(screenPos);
    for (auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it) {
        const auto* n = GetNode(*it);
        if (!n || n->collapsed) continue;
        const auto* tmpl = GetTemplate(n->templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(*n, *tmpl);
        for (size_t i = 0; i < tmpl->params.size() && i < L.rowWidgetBounds.size(); ++i) {
            const auto& wb = L.rowWidgetBounds[i];
            if (wb.width <= 0.0 || wb.height <= 0.0) continue;
            if (wb.Contains(world)) {
                hit.nodeId = *it;
                hit.paramId = tmpl->params[i].id;
                return hit;
            }
        }
    }
    return hit;
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasCompositorDiagram::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
    (void)dirtyRect;
    if (!ctx || !IsVisible()) return;

    // Background (screen space)
    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(Rect2Df(0, 0, GetWidth(), GetHeight()));

    // Apply viewport transform (world space below this).
    ctx->PushState();
    ctx->Translate(panOffset.x, panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    if (style.showGrid) RenderGrid(ctx);
    RenderLinks(ctx);
    RenderNodes(ctx);
    if (isConnecting) RenderConnectionLine(ctx);

    ctx->PopState();
}

void UltraCanvasCompositorDiagram::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(1.0 / std::max(0.01, zoomLevel));
    double spacing = style.gridSpacing;
    if (spacing < 2.0) return;

    Point2Df tl = ScreenToWorld({0, 0});
    Point2Df br = ScreenToWorld(Point2Di(GetWidth(), GetHeight()));
    double startX = std::floor(tl.x / spacing) * spacing;
    double endX   = std::ceil (br.x / spacing) * spacing;
    double startY = std::floor(tl.y / spacing) * spacing;
    double endY   = std::ceil (br.y / spacing) * spacing;

    for (double x = startX; x <= endX; x += spacing) {
        ctx->DrawLine(Point2Df(x, tl.y), Point2Df(x, br.y));
    }
    for (double y = startY; y <= endY; y += spacing) {
        ctx->DrawLine(Point2Df(tl.x, y), Point2Df(br.x, y));
    }
}

void UltraCanvasCompositorDiagram::RenderLinks(IRenderContext* ctx) {
    for (const auto& l : links) {
        const auto* sn = GetNode(l.sourceNodeId);
        const auto* dn = GetNode(l.targetNodeId);
        if (!sn || !dn) continue;
        const auto* st = GetTemplate(sn->templateId);
        const auto* dt = GetTemplate(dn->templateId);
        if (!st || !dt) continue;
        bool sIsOut = false, dIsOut = false;
        Point2Df sp, dp;
        if (!ResolveSocketWorldPosition(*sn, *st, l.sourceSocketId, sIsOut, sp)) continue;
        if (!ResolveSocketWorldPosition(*dn, *dt, l.targetSocketId, dIsOut, dp)) continue;

        // Color: explicit override > source socket type color
        Color lineColor = l.lineColorOverride;
        if (lineColor.a == 0) {
            CompositorSocketSpec stor;
            bool isOut = false;
            const auto* spec = ResolveSocketSpec(*st, l.sourceSocketId, isOut, stor);
            const SocketTypeInfo* info = nullptr;
            if (spec) info = GetSocketTypeInfo(spec->type, spec->customTag);
            lineColor = info ? info->color : Color(180, 180, 180, 255);
        }
        double lw = l.lineWidth;
        if (selectedLinks.count(l.id)) {
            lineColor = style.selectionColor;
            lw += 1.5;
        } else if (hoveredLinkId == l.id) {
            lineColor = Lighten(lineColor, 30);
        }

        std::vector<Point2Df> path;
        switch (l.style) {
            case LinkStyle::Straight:   path = { sp, dp }; break;
            case LinkStyle::Step:
            case LinkStyle::SmoothStep: BuildStepPath(sp, dp, path); break;
            case LinkStyle::Bezier:
            default:                    BuildBezierPath(sp, dp, path); break;
        }
        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(lw);
        for (size_t i = 1; i < path.size(); ++i) {
            ctx->DrawLine(path[i - 1], path[i]);
        }
    }
}

void UltraCanvasCompositorDiagram::RenderNodes(IRenderContext* ctx) {
    for (const auto& id : nodeOrder) {
        const auto* n = GetNode(id);
        if (!n) continue;
        const auto* tmpl = GetTemplate(n->templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(*n, *tmpl);
        RenderNodePanel(ctx, *n, *tmpl, L);
    }
}

void UltraCanvasCompositorDiagram::RenderNodePanel(
        IRenderContext* ctx, const CompositorNode& node,
        const CompositorNodeTemplate& tmpl, const NodeLayout& L) {
    bool selected = selectedNodes.count(node.id) > 0;
    bool hovered = (hoveredNodeId == node.id);

    Color bodyColor = tmpl.bodyColor;
    Color borderColor = selected ? style.selectionColor : tmpl.borderColor;
    double borderW = selected ? style.selectionWidth : tmpl.borderWidth;
    if (!selected && hovered) borderColor = Lighten(borderColor, 40);

    // Body fill (rounded)
    ctx->SetFillPaint(bodyColor);
    ctx->FillRoundedRectangle(L.bounds, tmpl.cornerRadius);

    // Header bar (rounded top corners). We approximate by drawing a rounded
    // rect for the full bounds clipped to header height, then a plain rect on
    // the bottom edge to square off where it meets the body. For simplicity
    // (and since FillRoundedRectangle is the only available helper) we just
    // draw a rounded rect of header height; the bottom curve looks fine for
    // a 22px header on a 200px+ tall body.
    ctx->SetFillPaint(tmpl.headerColor);
    ctx->FillRoundedRectangle(L.headerBounds, tmpl.cornerRadius);
    // Cover the bottom-curve of the header so it meets the body cleanly.
    Rect2Df headerStrip(L.headerBounds.x,
                         L.headerBounds.y + L.headerBounds.height - tmpl.cornerRadius,
                         L.headerBounds.width, tmpl.cornerRadius);
    ctx->SetFillPaint(tmpl.headerColor);
    ctx->FillRectangle(headerStrip);

    // Header title
    ctx->SetFontFamily(style.fontFamily);
    ctx->SetFontSize(style.headerFontSize);
    ctx->SetTextPaint(tmpl.headerTextColor);
    std::string title = node.titleOverride.empty() ? tmpl.title : node.titleOverride;
    Size2Di titleDim = ctx->GetTextLineDimensions(title);
    double titleX = L.headerBounds.x + style.rowPaddingX;
    double titleY = L.headerBounds.y + (L.headerBounds.height - titleDim.height) * 0.5;
    ctx->DrawText(title, Point2Df(titleX, titleY));

    // Border
    ctx->SetStrokePaint(borderColor);
    ctx->SetStrokeWidth(borderW);
    ctx->DrawRoundedRectangle(L.bounds, tmpl.cornerRadius);

    if (node.collapsed) {
        return;
    }

    ctx->SetFontSize(style.rowFontSize);

    // Outputs (right edge), label to the left of the dot.
    for (size_t i = 0; i < tmpl.outputs.size() && i < L.outputSocketBounds.size(); ++i) {
        const auto& sock = tmpl.outputs[i];
        const auto* info = GetSocketTypeInfo(sock.type, sock.customTag);
        Color dotColor   = info ? info->color : Color(200, 200, 200, 255);
        Color dotBorder  = info ? info->borderColor : Color(20, 20, 20, 255);

        const auto& r = L.outputSocketBounds[i];
        double cx = r.x + r.width * 0.5;
        double cy = r.y + r.height * 0.5;
        double rad = r.width * 0.5;

        // Label
        ctx->SetTextPaint(Color(220, 220, 220, 255));
        Size2Di labelDim = ctx->GetTextLineDimensions(sock.label);
        double labelX = cx - rad - style.socketLabelGap - labelDim.width;
        double labelY = cy - labelDim.height * 0.5;
        ctx->DrawText(sock.label, Point2Df(labelX, labelY));

        // Dot
        ctx->SetFillPaint(dotColor);
        ctx->FillCircle(Point2Df(cx, cy), rad);
        ctx->SetStrokePaint(dotBorder);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle(Point2Df(cx, cy), rad);
    }

    // Param rows: optional left socket + label + widget
    for (size_t i = 0; i < tmpl.params.size() && i < L.rowBounds.size(); ++i) {
        const auto& spec = tmpl.params[i];
        const auto& row = L.rowBounds[i];
        double rowCy = row.y + row.height * 0.5;
        double textX = row.x + style.rowPaddingX;

        if (spec.hasInputSocket) {
            const auto* info = GetSocketTypeInfo(spec.inputType, spec.inputCustomTag);
            Color dotColor  = info ? info->color : Color(200, 200, 200, 255);
            Color dotBorder = info ? info->borderColor : Color(20, 20, 20, 255);
            double radius = info ? info->radius : 5.0;
            Point2Df center(row.x, rowCy);
            ctx->SetFillPaint(dotColor);
            ctx->FillCircle(center, radius);
            ctx->SetStrokePaint(dotBorder);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawCircle(center, radius);
            textX = row.x + radius + style.socketLabelGap;
        }

        ctx->SetTextPaint(Color(220, 220, 220, 255));
        Size2Di lblDim = ctx->GetTextLineDimensions(spec.label);
        double lblY = rowCy - lblDim.height * 0.5;
        ctx->DrawText(spec.label, Point2Df(textX, lblY));

        if (i < L.rowWidgetBounds.size()) {
            const auto& wb = L.rowWidgetBounds[i];
            if (wb.width > 0.0) {
                auto vit = node.paramValues.find(spec.id);
                ParamValue empty;
                const ParamValue& val = (vit != node.paramValues.end()) ? vit->second : empty;
                RenderRowWidget(ctx, node, spec, val, wb);
            }
        }
    }

    // Inputs (left edge)
    for (size_t i = 0; i < tmpl.inputs.size() && i < L.inputSocketBounds.size(); ++i) {
        const auto& sock = tmpl.inputs[i];
        const auto* info = GetSocketTypeInfo(sock.type, sock.customTag);
        Color dotColor  = info ? info->color : Color(200, 200, 200, 255);
        Color dotBorder = info ? info->borderColor : Color(20, 20, 20, 255);

        const auto& r = L.inputSocketBounds[i];
        double cx = r.x + r.width * 0.5;
        double cy = r.y + r.height * 0.5;
        double rad = r.width * 0.5;

        ctx->SetFillPaint(dotColor);
        ctx->FillCircle(Point2Df(cx, cy), rad);
        ctx->SetStrokePaint(dotBorder);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle(Point2Df(cx, cy), rad);

        ctx->SetTextPaint(Color(220, 220, 220, 255));
        Size2Di labelDim = ctx->GetTextLineDimensions(sock.label);
        double labelX = cx + rad + style.socketLabelGap;
        double labelY = cy - labelDim.height * 0.5;
        ctx->DrawText(sock.label, Point2Df(labelX, labelY));
    }

    // Preview slot: checkerboard placeholder if no handle, else a flat-color
    // rect (the actual texture must be uploaded by the host app - this
    // component does not own raster resources).
    if (tmpl.hasPreview && L.previewBounds.width > 0.0 && L.previewBounds.height > 0.0) {
        const auto& pb = L.previewBounds;
        ctx->SetFillPaint(Color(60, 60, 60, 255));
        ctx->FillRectangle(pb);
        if (node.previewHandle.empty()) {
            // Cheap checkerboard.
            double tile = 12.0;
            Color light(120, 120, 120, 255);
            Color dark (90, 90, 90, 255);
            for (double yy = pb.y; yy < pb.y + pb.height; yy += tile) {
                for (double xx = pb.x; xx < pb.x + pb.width; xx += tile) {
                    int cellX = static_cast<int>((xx - pb.x) / tile);
                    int cellY = static_cast<int>((yy - pb.y) / tile);
                    bool light_cell = ((cellX + cellY) & 1) == 0;
                    double cellW = std::min(tile, pb.x + pb.width  - xx);
                    double cellH = std::min(tile, pb.y + pb.height - yy);
                    ctx->SetFillPaint(light_cell ? light : dark);
                    ctx->FillRectangle(Rect2Df(xx, yy, cellW, cellH));
                }
            }
        } else {
            // Host app is expected to swap in real raster rendering through a
            // future texture-binding API; for now we annotate the slot.
            ctx->SetTextPaint(Color(180, 180, 180, 255));
            Size2Di hDim = ctx->GetTextLineDimensions(node.previewHandle);
            double tx = pb.x + (pb.width  - hDim.width)  * 0.5;
            double ty = pb.y + (pb.height - hDim.height) * 0.5;
            ctx->DrawText(node.previewHandle, Point2Df(tx, ty));
        }
        ctx->SetStrokePaint(Color(20, 20, 20, 255));
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(pb);
    }
}

void UltraCanvasCompositorDiagram::RenderRowWidget(
        IRenderContext* ctx, const CompositorNode& node,
        const CompositorParamSpec& spec, const ParamValue& value,
        const Rect2Df& wb) {
    (void)node;
    Color bg(70, 70, 70, 255);
    Color fg(230, 230, 230, 255);
    Color border(30, 30, 30, 255);
    double radius = 3.0;

    switch (spec.widget) {
        case ParamWidgetKind::NoWidget:
            break;

        case ParamWidgetKind::Dropdown: {
            ctx->SetFillPaint(bg);
            ctx->FillRoundedRectangle(wb, radius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(wb, radius);
            std::string text;
            if (!spec.choices.empty()) {
                int idx = std::max(0, std::min<int>(value.dropdownIndex,
                                                     (int)spec.choices.size() - 1));
                text = spec.choices[idx];
            }
            ctx->SetTextPaint(fg);
            Size2Di td = ctx->GetTextLineDimensions(text);
            ctx->DrawText(text,
                Point2Df(wb.x + 6.0, wb.y + (wb.height - td.height) * 0.5));
            // Right-side caret triangle (down)
            double cx = wb.x + wb.width - 8.0;
            double cy = wb.y + wb.height * 0.5;
            ctx->SetStrokePaint(fg);
            ctx->SetStrokeWidth(1.4);
            ctx->DrawLine(Point2Df(cx - 4, cy - 2), Point2Df(cx,     cy + 3));
            ctx->DrawLine(Point2Df(cx,     cy + 3), Point2Df(cx + 4, cy - 2));
            break;
        }

        case ParamWidgetKind::NumberInput: {
            ctx->SetFillPaint(bg);
            ctx->FillRoundedRectangle(wb, radius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(wb, radius);
            std::string text = FormatNumberShort(value.number, spec.numStep);
            ctx->SetTextPaint(fg);
            Size2Di td = ctx->GetTextLineDimensions(text);
            double tx = wb.x + wb.width - 6.0 - td.width;
            double ty = wb.y + (wb.height - td.height) * 0.5;
            ctx->DrawText(text, Point2Df(tx, ty));
            break;
        }

        case ParamWidgetKind::Checkbox: {
            double box = std::min(wb.height, 14.0);
            double bx = wb.x + wb.width - box - 2.0;
            double by = wb.y + (wb.height - box) * 0.5;
            Rect2Df chk(bx, by, box, box);
            ctx->SetFillPaint(value.boolean ? Color(80, 160, 240, 255) : bg);
            ctx->FillRoundedRectangle(chk, 2.0);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(chk, 2.0);
            if (value.boolean) {
                ctx->SetStrokePaint(Color(255, 255, 255, 255));
                ctx->SetStrokeWidth(2.0);
                ctx->DrawLine(Point2Df(bx + 3,       by + box * 0.55),
                              Point2Df(bx + box * 0.45, by + box - 3));
                ctx->DrawLine(Point2Df(bx + box * 0.45, by + box - 3),
                              Point2Df(bx + box - 3,    by + 3));
            }
            break;
        }

        case ParamWidgetKind::ColorSwatch: {
            ctx->SetFillPaint(value.color);
            ctx->FillRoundedRectangle(wb, radius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(wb, radius);
            break;
        }

        case ParamWidgetKind::TextInput: {
            ctx->SetFillPaint(bg);
            ctx->FillRoundedRectangle(wb, radius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(wb, radius);
            ctx->SetTextPaint(fg);
            Size2Di td = ctx->GetTextLineDimensions(value.text);
            ctx->DrawText(value.text,
                Point2Df(wb.x + 6.0, wb.y + (wb.height - td.height) * 0.5));
            break;
        }

        case ParamWidgetKind::Slider: {
            ctx->SetFillPaint(bg);
            ctx->FillRoundedRectangle(wb, radius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(wb, radius);
            double range = std::max(1e-9, spec.numMax - spec.numMin);
            double t = (value.number - spec.numMin) / range;
            t = std::max(0.0, std::min(1.0, t));
            double fillW = wb.width * t;
            ctx->SetFillPaint(Color(80, 130, 200, 255));
            ctx->FillRoundedRectangle(Rect2Df(wb.x, wb.y, fillW, wb.height), radius);
            std::string text = FormatNumberShort(value.number, spec.numStep);
            ctx->SetTextPaint(fg);
            Size2Di td = ctx->GetTextLineDimensions(text);
            ctx->DrawText(text,
                Point2Df(wb.x + (wb.width - td.width) * 0.5,
                         wb.y + (wb.height - td.height) * 0.5));
            break;
        }

        case ParamWidgetKind::FileBrowser: {
            ctx->SetFillPaint(bg);
            ctx->FillRoundedRectangle(wb, radius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(wb, radius);
            std::string disp = value.text.empty() ? "(none)" : value.text;
            // Truncate visually if long
            const int maxChars = 18;
            if ((int)disp.size() > maxChars) disp = "..." + disp.substr(disp.size() - maxChars + 3);
            ctx->SetTextPaint(fg);
            Size2Di td = ctx->GetTextLineDimensions(disp);
            ctx->DrawText(disp,
                Point2Df(wb.x + 6.0, wb.y + (wb.height - td.height) * 0.5));
            break;
        }
    }
}

void UltraCanvasCompositorDiagram::RenderConnectionLine(IRenderContext* ctx) {
    if (!isConnecting) return;
    const auto* n = GetNode(connectionSourceNode);
    if (!n) return;
    const auto* tmpl = GetTemplate(n->templateId);
    if (!tmpl) return;
    bool isOut = false;
    Point2Df sp;
    if (!ResolveSocketWorldPosition(*n, *tmpl, connectionSourceSocket, isOut, sp)) return;
    Point2Df from = isOut ? sp : connectionEndPoint;
    Point2Df to   = isOut ? connectionEndPoint : sp;
    std::vector<Point2Df> path;
    BuildBezierPath(from, to, path, 18);
    ctx->SetStrokePaint(style.connectionLineColor);
    ctx->SetStrokeWidth(style.connectionLineWidth);
    for (size_t i = 1; i < path.size(); ++i) {
        ctx->DrawLine(path[i - 1], path[i]);
    }
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasCompositorDiagram::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:  return HandleMouseDown(event);
        case UCEventType::MouseUp:    return HandleMouseUp(event);
        case UCEventType::MouseMove:  return HandleMouseMove(event);
        case UCEventType::MouseWheel: return HandleMouseWheel(event);
        case UCEventType::KeyDown:    return HandleKeyDown(event);
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasCompositorDiagram::HandleMouseDown(const UCEvent& event) {
    if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
    if (!isInteractive && event.button != UCMouseButton::Middle) return false;

    Point2Di mousePos(event.pointer.x, event.pointer.y);
    lastMousePos = mousePos;
    dragStartPos = mousePos;
    isMultiSelectKeyHeld = event.shift;

    // Right-click on empty canvas dispatches the create-node hook.
    if (event.button == UCMouseButton::Right) {
        SocketHit sh = FindSocketAt(mousePos);
        if (!sh.socketId.empty()) {
            // Right-click on socket: disconnect.
            RemoveLinksOnSocket(sh.nodeId, sh.socketId);
            return true;
        }
        std::string nid = FindNodeAt(mousePos);
        std::string lid = nid.empty() ? FindLinkAt(mousePos) : "";
        if (nid.empty() && lid.empty() && onCanvasRightClick) {
            Point2Df w = ScreenToWorld(mousePos);
            onCanvasRightClick(w.x, w.y);
        }
        return true;
    }

    // Middle-click: pan.
    if (event.button == UCMouseButton::Middle) {
        isDraggingViewport = true;
        return true;
    }

    if (event.button != UCMouseButton::Left) return true;

    // 1. Socket → drag-to-connect.
    if (nodesConnectable) {
        SocketHit sh = FindSocketAt(mousePos);
        if (!sh.socketId.empty()) {
            isConnecting = true;
            connectionSourceNode = sh.nodeId;
            connectionSourceSocket = sh.socketId;
            connectionSourceIsOutput = sh.isOutput;
            connectionEndPoint = ScreenToWorld(mousePos);
            return true;
        }
    }

    // 2. Row widget hit (handled inline; commit value changes).
    WidgetHit wh = FindRowWidgetAt(mousePos);
    if (!wh.paramId.empty()) {
        auto* node = GetNode(wh.nodeId);
        const auto* tmpl = node ? GetTemplate(node->templateId) : nullptr;
        const auto* spec = tmpl ? FindParamSpec(*tmpl, wh.paramId) : nullptr;
        if (node && spec) {
            // Bring this node to front so subsequent drags route to it.
            SelectNode(wh.nodeId, event.shift);
            switch (spec->widget) {
                case ParamWidgetKind::Dropdown: {
                    if (!spec->choices.empty()) {
                        auto& v = node->paramValues[spec->id];
                        int next = v.dropdownIndex + 1;
                        if (next >= (int)spec->choices.size()) next = 0;
                        v.dropdownIndex = next;
                        if (onParamChange) onParamChange(node->id, spec->id, v);
                        RequestRedraw();
                    }
                    return true;
                }
                case ParamWidgetKind::Checkbox: {
                    auto& v = node->paramValues[spec->id];
                    v.boolean = !v.boolean;
                    if (onParamChange) onParamChange(node->id, spec->id, v);
                    RequestRedraw();
                    return true;
                }
                case ParamWidgetKind::Slider: {
                    NodeLayout L = ComputeNodeLayout(*node, *tmpl);
                    for (size_t i = 0; i < tmpl->params.size() && i < L.rowWidgetBounds.size(); ++i) {
                        if (tmpl->params[i].id != spec->id) continue;
                        const auto& wb = L.rowWidgetBounds[i];
                        Point2Df w = ScreenToWorld(mousePos);
                        double t = (w.x - wb.x) / std::max(1e-6, wb.width);
                        t = std::max(0.0, std::min(1.0, t));
                        double newVal = spec->numMin + t * (spec->numMax - spec->numMin);
                        if (spec->numStep > 0)
                            newVal = std::round(newVal / spec->numStep) * spec->numStep;
                        auto& v = node->paramValues[spec->id];
                        v.number = newVal;
                        if (onParamChange) onParamChange(node->id, spec->id, v);
                        RequestRedraw();
                        return true;
                    }
                    return true;
                }
                default:
                    // Other kinds (ColorSwatch, TextInput, FileBrowser, NumberInput)
                    // need a separate editor UX. Fire the change callback with the
                    // current value so apps can spawn a popup.
                    if (onParamChange) {
                        auto it = node->paramValues.find(spec->id);
                        if (it != node->paramValues.end()) {
                            onParamChange(node->id, spec->id, it->second);
                        }
                    }
                    return true;
            }
        }
    }

    // 3. Node hit → select + start drag.
    std::string nid = FindNodeAt(mousePos);
    if (!nid.empty()) {
        if (!event.shift) DeselectAll();
        SelectNode(nid, true);
        isDraggingNode = true;
        dragStartPositions.clear();
        for (const auto& sel : selectedNodes) {
            const auto* n = GetNode(sel);
            if (n) dragStartPositions[sel] = Point2Df(n->x, n->y);
        }
        if (onNodeClick) onNodeClick(nid);
        return true;
    }

    // 4. Link hit → select.
    std::string lid = FindLinkAt(mousePos);
    if (!lid.empty()) {
        if (!event.shift) DeselectAll();
        SelectLink(lid, true);
        if (onLinkClick) onLinkClick(lid);
        return true;
    }

    // 5. Empty canvas: deselect, start pan.
    if (!event.shift) DeselectAll();
    if (panOnDrag) isDraggingViewport = true;
    return true;
}

bool UltraCanvasCompositorDiagram::HandleMouseUp(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isConnecting) {
        // Find a socket under the release point.
        SocketHit sh = FindSocketAt(mousePos);
        bool ok = false;
        if (!sh.socketId.empty() && sh.nodeId != connectionSourceNode) {
            // Orient: src must be output, dst must be input.
            std::string srcNode, srcSock, dstNode, dstSock;
            if (connectionSourceIsOutput && !sh.isOutput) {
                srcNode = connectionSourceNode; srcSock = connectionSourceSocket;
                dstNode = sh.nodeId;             dstSock = sh.socketId;
                ok = true;
            } else if (!connectionSourceIsOutput && sh.isOutput) {
                srcNode = sh.nodeId;             srcSock = sh.socketId;
                dstNode = connectionSourceNode; dstSock = connectionSourceSocket;
                ok = true;
            }
            if (ok) {
                static unsigned int linkCounter = 0;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "link_%u", ++linkCounter);
                std::string id(buf);
                AddLink(id, srcNode, srcSock, dstNode, dstSock);
            }
        }
        isConnecting = false;
        connectionSourceNode.clear();
        connectionSourceSocket.clear();
        RequestRedraw();
        return true;
    }

    if (isDraggingNode) {
        isDraggingNode = false;
        return true;
    }
    if (isDraggingViewport) {
        isDraggingViewport = false;
        return true;
    }
    return false;
}

bool UltraCanvasCompositorDiagram::HandleMouseMove(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isConnecting) {
        connectionEndPoint = ScreenToWorld(mousePos);
        RequestRedraw();
        return true;
    }

    if (isDraggingNode) {
        Point2Df startW = ScreenToWorld(dragStartPos);
        Point2Df curW   = ScreenToWorld(mousePos);
        double dx = curW.x - startW.x;
        double dy = curW.y - startW.y;
        for (const auto& kv : dragStartPositions) {
            auto* n = GetNode(kv.first);
            if (!n) continue;
            n->x = kv.second.x + dx;
            n->y = kv.second.y + dy;
            if (onNodeDrag) onNodeDrag(kv.first, n->x, n->y);
        }
        RequestRedraw();
        return true;
    }

    if (isDraggingViewport) {
        panOffset.x += (mousePos.x - lastMousePos.x);
        panOffset.y += (mousePos.y - lastMousePos.y);
        lastMousePos = mousePos;
        RequestRedraw();
        return true;
    }

    // Update hover for visual feedback (cheap; only redraw if something changed).
    lastMousePos = mousePos;
    std::string newHoveredNode = FindNodeAt(mousePos);
    std::string newHoveredLink = newHoveredNode.empty() ? FindLinkAt(mousePos) : "";
    if (newHoveredNode != hoveredNodeId || newHoveredLink != hoveredLinkId) {
        hoveredNodeId = newHoveredNode;
        hoveredLinkId = newHoveredLink;
        RequestRedraw();
    }
    return false;
}

bool UltraCanvasCompositorDiagram::HandleMouseWheel(const UCEvent& event) {
    if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
    if (!zoomOnScroll) return false;

    Point2Di mousePos(event.pointer.x, event.pointer.y);
    Point2Df worldBefore = ScreenToWorld(mousePos);

    double factor = (event.wheelDelta > 0) ? 1.15 : (1.0 / 1.15);
    zoomLevel *= factor;
    ClampZoom();

    // Keep cursor anchored to its world point.
    Point2Df worldAfter = ScreenToWorld(mousePos);
    panOffset.x += (worldAfter.x - worldBefore.x) * zoomLevel;
    panOffset.y += (worldAfter.y - worldBefore.y) * zoomLevel;

    RequestRedraw();
    return true;
}

bool UltraCanvasCompositorDiagram::HandleKeyDown(const UCEvent& event) {
    switch (event.virtualKey) {
        case UCKeys::Delete:
        case UCKeys::Backspace:
            DeleteSelected();
            return true;
        case UCKeys::Escape:
            if (isConnecting) {
                isConnecting = false;
                connectionSourceNode.clear();
                connectionSourceSocket.clear();
                RequestRedraw();
                return true;
            }
            DeselectAll();
            return true;
        case UCKeys::A:
            if (event.ctrl) {
                SelectAll();
                return true;
            }
            break;
        default: break;
    }
    return false;
}

// =============================================================================
// UTILITY
// =============================================================================

Point2Df UltraCanvasCompositorDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    Point2Df w;
    w.x = (screenPos.x - panOffset.x) / std::max(1e-9, zoomLevel);
    w.y = (screenPos.y - panOffset.y) / std::max(1e-9, zoomLevel);
    return w;
}

Point2Di UltraCanvasCompositorDiagram::WorldToScreen(const Point2Df& worldPos) const {
    Point2Di s;
    s.x = static_cast<int>(worldPos.x * zoomLevel + panOffset.x);
    s.y = static_cast<int>(worldPos.y * zoomLevel + panOffset.y);
    return s;
}

void UltraCanvasCompositorDiagram::NotifySelectionChange() {
    if (onSelectionChange) {
        onSelectionChange(GetSelectedNodeIds(), GetSelectedLinkIds());
    }
}

void UltraCanvasCompositorDiagram::ClampZoom() {
    zoomLevel = std::max(minZoom, std::min(maxZoom, zoomLevel));
}

bool UltraCanvasCompositorDiagram::TryConnect(
        const std::string& srcNodeId, const std::string& srcSocketId,
        const std::string& dstNodeId, const std::string& dstSocketId,
        std::string& outNewLinkId) {
    static unsigned int counter = 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "link_%u", ++counter);
    outNewLinkId = buf;
    return AddLink(outNewLinkId, srcNodeId, srcSocketId, dstNodeId, dstSocketId);
}

// =============================================================================
// SERIALIZATION
// =============================================================================

std::string UltraCanvasCompositorDiagram::ToJson() const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"version\": \"0.1.0\",\n";
    out << "  \"viewport\": {\n";
    out << "    \"zoom\": " << zoomLevel << ",\n";
    out << "    \"panX\": " << panOffset.x << ",\n";
    out << "    \"panY\": " << panOffset.y << "\n";
    out << "  },\n";

    // Templates
    out << "  \"templates\": [\n";
    bool first = true;
    for (const auto& kv : templates) {
        const auto& t = kv.second;
        if (!first) out << ",\n";
        first = false;
        out << "    {\n";
        out << "      \"id\": " << EscapeJsonString(t.id) << ",\n";
        out << "      \"category\": " << EscapeJsonString(t.category) << ",\n";
        out << "      \"title\": " << EscapeJsonString(t.title) << ",\n";
        out << "      \"headerColor\": " << ColorToJsonString(t.headerColor) << ",\n";
        out << "      \"bodyColor\": " << ColorToJsonString(t.bodyColor) << ",\n";
        out << "      \"borderColor\": " << ColorToJsonString(t.borderColor) << ",\n";
        out << "      \"defaultWidth\": " << t.defaultWidth << ",\n";
        out << "      \"hasPreview\": " << (t.hasPreview ? "true" : "false") << ",\n";
        auto writeSockets = [&](const std::vector<CompositorSocketSpec>& list,
                                 const char* key) {
            out << "      \"" << key << "\": [";
            for (size_t i = 0; i < list.size(); ++i) {
                if (i) out << ", ";
                out << "{ \"id\": " << EscapeJsonString(list[i].id)
                    << ", \"label\": " << EscapeJsonString(list[i].label)
                    << ", \"type\": \"" << SocketDataTypeToString(list[i].type) << "\""
                    << ", \"customTag\": " << EscapeJsonString(list[i].customTag)
                    << ", \"maxConnections\": " << list[i].maxConnections << " }";
            }
            out << "]";
        };
        writeSockets(t.inputs,  "inputs");  out << ",\n";
        writeSockets(t.outputs, "outputs"); out << ",\n";
        out << "      \"params\": [";
        for (size_t i = 0; i < t.params.size(); ++i) {
            const auto& p = t.params[i];
            if (i) out << ", ";
            out << "{ \"id\": " << EscapeJsonString(p.id)
                << ", \"label\": " << EscapeJsonString(p.label)
                << ", \"hasInputSocket\": " << (p.hasInputSocket ? "true" : "false")
                << ", \"inputType\": \"" << SocketDataTypeToString(p.inputType) << "\""
                << ", \"widget\": \"";
            switch (p.widget) {
                case ParamWidgetKind::NoWidget:        out << "none";        break;
                case ParamWidgetKind::Dropdown:    out << "dropdown";    break;
                case ParamWidgetKind::NumberInput: out << "number";      break;
                case ParamWidgetKind::Checkbox:    out << "checkbox";    break;
                case ParamWidgetKind::ColorSwatch: out << "color_swatch";break;
                case ParamWidgetKind::TextInput:   out << "text";        break;
                case ParamWidgetKind::Slider:      out << "slider";      break;
                case ParamWidgetKind::FileBrowser: out << "file";        break;
            }
            out << "\" }";
        }
        out << "]\n";
        out << "    }";
    }
    out << "\n  ],\n";

    // Nodes
    out << "  \"nodes\": [\n";
    first = true;
    for (const auto& nid : nodeOrder) {
        auto it = nodes.find(nid);
        if (it == nodes.end()) continue;
        const auto& n = it->second;
        if (!first) out << ",\n";
        first = false;
        out << "    {\n";
        out << "      \"id\": " << EscapeJsonString(n.id) << ",\n";
        out << "      \"templateId\": " << EscapeJsonString(n.templateId) << ",\n";
        out << "      \"titleOverride\": " << EscapeJsonString(n.titleOverride) << ",\n";
        out << "      \"x\": " << n.x << ",\n";
        out << "      \"y\": " << n.y << ",\n";
        out << "      \"width\": " << n.width << ",\n";
        out << "      \"collapsed\": " << (n.collapsed ? "true" : "false") << ",\n";
        out << "      \"previewHandle\": " << EscapeJsonString(n.previewHandle) << ",\n";
        out << "      \"params\": [";
        bool firstP = true;
        for (const auto& pv : n.paramValues) {
            if (!firstP) out << ", ";
            firstP = false;
            const auto& v = pv.second;
            out << "{ \"id\": " << EscapeJsonString(pv.first);
            switch (v.kind) {
                case ParamValueKind::Number:  out << ", \"number\": "  << v.number; break;
                case ParamValueKind::Boolean: out << ", \"bool\": "    << (v.boolean ? "true" : "false"); break;
                case ParamValueKind::Text:    out << ", \"text\": "    << EscapeJsonString(v.text); break;
                case ParamValueKind::Color:   out << ", \"color\": "   << ColorToJsonString(v.color); break;
                default: break;
            }
            if (v.dropdownIndex != 0) out << ", \"dropdown\": " << v.dropdownIndex;
            out << " }";
        }
        out << "]\n";
        out << "    }";
    }
    out << "\n  ],\n";

    // Links
    out << "  \"links\": [\n";
    first = true;
    for (const auto& l : links) {
        if (!first) out << ",\n";
        first = false;
        out << "    {\n";
        out << "      \"id\": " << EscapeJsonString(l.id) << ",\n";
        out << "      \"srcNode\": " << EscapeJsonString(l.sourceNodeId) << ",\n";
        out << "      \"srcSocket\": " << EscapeJsonString(l.sourceSocketId) << ",\n";
        out << "      \"dstNode\": " << EscapeJsonString(l.targetNodeId) << ",\n";
        out << "      \"dstSocket\": " << EscapeJsonString(l.targetSocketId) << ",\n";
        out << "      \"style\": \"" << LinkStyleToCompositorString(l.style) << "\",\n";
        out << "      \"lineWidth\": " << l.lineWidth << "\n";
        out << "    }";
    }
    out << "\n  ]\n";
    out << "}\n";
    return out.str();
}

bool UltraCanvasCompositorDiagram::FromJson(const std::string& json) {
    Clear();
    // Viewport
    std::string vp = FindRawValue(json, "viewport");
    if (!vp.empty()) {
        zoomLevel  = ExtractNumberValue(vp, "zoom", 1.0);
        panOffset.x = ExtractNumberValue(vp, "panX", 0.0);
        panOffset.y = ExtractNumberValue(vp, "panY", 0.0);
        ClampZoom();
    }

    // Note: we do NOT restore registered templates from JSON because templates
    // are app-defined behaviour; the application is expected to re-register
    // them before calling FromJson. Nodes referencing unknown template ids
    // will silently fail to instantiate.

    // Nodes
    for (const auto& nobj : ExtractObjectArray(json, "nodes")) {
        CompositorNode node;
        node.id = ExtractStringValue(nobj, "id");
        node.templateId = ExtractStringValue(nobj, "templateId");
        node.titleOverride = ExtractStringValue(nobj, "titleOverride");
        node.x = ExtractNumberValue(nobj, "x");
        node.y = ExtractNumberValue(nobj, "y");
        node.width = ExtractNumberValue(nobj, "width");
        node.collapsed = ExtractBoolValue(nobj, "collapsed");
        node.previewHandle = ExtractStringValue(nobj, "previewHandle");
        for (const auto& pobj : ExtractObjectArray(nobj, "params")) {
            std::string pid = ExtractStringValue(pobj, "id");
            if (pid.empty()) continue;
            ParamValue v;
            std::string numRaw = FindRawValue(pobj, "number");
            if (!numRaw.empty()) { v.kind = ParamValueKind::Number; v.number = ExtractNumberValue(pobj, "number"); }
            std::string boolRaw = FindRawValue(pobj, "bool");
            if (!boolRaw.empty()) { v.kind = ParamValueKind::Boolean; v.boolean = ExtractBoolValue(pobj, "bool"); }
            std::string textRaw = FindRawValue(pobj, "text");
            if (!textRaw.empty()) { v.kind = ParamValueKind::Text; v.text = ExtractStringValue(pobj, "text"); }
            std::string colRaw = FindRawValue(pobj, "color");
            if (!colRaw.empty()) { v.kind = ParamValueKind::Color; v.color = ExtractColorValue(pobj, "color", Color()); }
            std::string ddRaw = FindRawValue(pobj, "dropdown");
            if (!ddRaw.empty()) v.dropdownIndex = static_cast<int>(ExtractNumberValue(pobj, "dropdown"));
            node.paramValues[pid] = v;
        }
        AddNode(node);
    }

    // Links
    for (const auto& lobj : ExtractObjectArray(json, "links")) {
        CompositorLink l;
        l.id = ExtractStringValue(lobj, "id");
        l.sourceNodeId   = ExtractStringValue(lobj, "srcNode");
        l.sourceSocketId = ExtractStringValue(lobj, "srcSocket");
        l.targetNodeId   = ExtractStringValue(lobj, "dstNode");
        l.targetSocketId = ExtractStringValue(lobj, "dstSocket");
        l.style = LinkStyleFromCompositorString(ExtractStringValue(lobj, "style"));
        l.lineWidth = ExtractNumberValue(lobj, "lineWidth", 2.0);
        AddLink(l);
    }
    RequestRedraw();
    return true;
}

} // namespace UltraCanvas
