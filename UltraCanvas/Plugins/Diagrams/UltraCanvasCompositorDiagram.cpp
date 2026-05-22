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
        case SocketDataType::Time:     return "time";
        case SocketDataType::Matrix:   return "matrix";
        case SocketDataType::Trigger:  return "trigger";
        case SocketDataType::List:     return "list";
        case SocketDataType::Path:     return "path";
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
    seed(SocketDataType::Time,     "Time",     Color(180, 180, 200, 255));  // silver-blue
    seed(SocketDataType::Matrix,   "Matrix",   Color(140,  80, 180, 255));  // dark purple
    seed(SocketDataType::Trigger,  "Trigger",  Color(230, 230, 230, 255));  // bright white (exec-pin convention)
    seed(SocketDataType::List,     "List",     Color(180, 230, 180, 255));  // light green
    seed(SocketDataType::Path,     "Path",     Color(180, 150, 110, 255));  // tan / brown
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
    ApplyAddNode(node);

    if (historyRecording) {
        PushHistory({
            [this, nodeId]()    { ApplyRemoveNode(nodeId); },
            [this, node]()      { ApplyAddNode(node); },
            "Add node"
        });
    }
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
    ApplyAddNode(copy);

    if (historyRecording) {
        std::string nodeId = copy.id;
        PushHistory({
            [this, nodeId]() { ApplyRemoveNode(nodeId); },
            [this, copy]()   { ApplyAddNode(copy); },
            "Add node"
        });
    }
}

void UltraCanvasCompositorDiagram::RemoveNode(const std::string& nodeId) {
    auto nit = nodes.find(nodeId);
    if (nit == nodes.end()) return;

    // Collect this node + all descendants (BFS). React Flow's convention is
    // that removing a parent removes the whole subtree.
    std::vector<std::string> victims;
    std::vector<std::string> frontier{nodeId};
    while (!frontier.empty()) {
        std::string cur = frontier.back();
        frontier.pop_back();
        victims.push_back(cur);
        for (const auto& id : nodeOrder) {
            auto it = nodes.find(id);
            if (it == nodes.end()) continue;
            if (it->second.parentId == cur) frontier.push_back(id);
        }
    }
    std::set<std::string> victimSet(victims.begin(), victims.end());

    // Capture all victims for undo.
    std::vector<CompositorNode> savedNodes;
    for (const auto& v : victims) {
        auto it = nodes.find(v);
        if (it != nodes.end()) savedNodes.push_back(it->second);
    }
    std::vector<CompositorLink> savedLinks;
    for (const auto& l : links) {
        if (victimSet.count(l.sourceNodeId) || victimSet.count(l.targetNodeId)) {
            savedLinks.push_back(l);
        }
    }

    // Mutate.
    for (const auto& l : savedLinks) {
        if (onLinkRemoved) onLinkRemoved(l.id);
    }
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const CompositorLink& l) {
            return victimSet.count(l.sourceNodeId) || victimSet.count(l.targetNodeId);
        }), links.end());
    for (const auto& v : victims) ApplyRemoveNode(v);

    if (historyRecording) {
        PushHistory({
            [this, savedNodes, savedLinks]() {
                for (const auto& n : savedNodes) ApplyAddNode(n);
                for (const auto& l : savedLinks) ApplyAddLink(l);
            },
            [this, victims, savedLinks]() {
                for (const auto& l : savedLinks) ApplyRemoveLink(l.id);
                for (const auto& v : victims) ApplyRemoveNode(v);
            },
            "Remove node"
        });
    }
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

// Internal helper: write a new ParamValue, push history capturing old/new.
// Kept as a member function so the file-local SetParam* methods stay tidy.
namespace {
ParamValue CapturePrev(const CompositorNode* node, const std::string& paramId) {
    if (!node) return {};
    auto it = node->paramValues.find(paramId);
    return (it == node->paramValues.end()) ? ParamValue{} : it->second;
}
} // anonymous namespace

bool UltraCanvasCompositorDiagram::SetParamNumber(const std::string& nodeId,
                                                    const std::string& paramId,
                                                    double value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    ParamValue oldVal = CapturePrev(node, paramId);
    ParamValue newVal = oldVal;
    if (newVal.kind == ParamValueKind::Unset) newVal.kind = ParamValueKind::Number;
    newVal.number = value;
    ApplySetParamValue(nodeId, paramId, newVal);
    if (historyRecording) {
        PushHistory({
            [this, nodeId, paramId, oldVal]() { ApplySetParamValue(nodeId, paramId, oldVal); },
            [this, nodeId, paramId, newVal]() { ApplySetParamValue(nodeId, paramId, newVal); },
            "Change parameter"
        });
    }
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamBool(const std::string& nodeId,
                                                  const std::string& paramId, bool value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    ParamValue oldVal = CapturePrev(node, paramId);
    ParamValue newVal = oldVal;
    if (newVal.kind == ParamValueKind::Unset) newVal.kind = ParamValueKind::Boolean;
    newVal.boolean = value;
    ApplySetParamValue(nodeId, paramId, newVal);
    if (historyRecording) {
        PushHistory({
            [this, nodeId, paramId, oldVal]() { ApplySetParamValue(nodeId, paramId, oldVal); },
            [this, nodeId, paramId, newVal]() { ApplySetParamValue(nodeId, paramId, newVal); },
            "Toggle parameter"
        });
    }
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamText(const std::string& nodeId,
                                                  const std::string& paramId,
                                                  const std::string& value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    ParamValue oldVal = CapturePrev(node, paramId);
    ParamValue newVal = oldVal;
    if (newVal.kind == ParamValueKind::Unset) newVal.kind = ParamValueKind::Text;
    newVal.text = value;
    ApplySetParamValue(nodeId, paramId, newVal);
    if (historyRecording) {
        PushHistory({
            [this, nodeId, paramId, oldVal]() { ApplySetParamValue(nodeId, paramId, oldVal); },
            [this, nodeId, paramId, newVal]() { ApplySetParamValue(nodeId, paramId, newVal); },
            "Change text"
        });
    }
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamColor(const std::string& nodeId,
                                                   const std::string& paramId,
                                                   const Color& value) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    ParamValue oldVal = CapturePrev(node, paramId);
    ParamValue newVal = oldVal;
    if (newVal.kind == ParamValueKind::Unset) newVal.kind = ParamValueKind::Color;
    newVal.color = value;
    ApplySetParamValue(nodeId, paramId, newVal);
    if (historyRecording) {
        PushHistory({
            [this, nodeId, paramId, oldVal]() { ApplySetParamValue(nodeId, paramId, oldVal); },
            [this, nodeId, paramId, newVal]() { ApplySetParamValue(nodeId, paramId, newVal); },
            "Change color"
        });
    }
    return true;
}

bool UltraCanvasCompositorDiagram::SetParamDropdownIndex(const std::string& nodeId,
                                                          const std::string& paramId,
                                                          int index) {
    auto* node = GetNode(nodeId);
    if (!node) return false;
    ParamValue oldVal = CapturePrev(node, paramId);
    ParamValue newVal = oldVal;
    newVal.dropdownIndex = index;
    ApplySetParamValue(nodeId, paramId, newVal);
    if (historyRecording) {
        PushHistory({
            [this, nodeId, paramId, oldVal]() { ApplySetParamValue(nodeId, paramId, oldVal); },
            [this, nodeId, paramId, newVal]() { ApplySetParamValue(nodeId, paramId, newVal); },
            "Select option"
        });
    }
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
    // Replace existing connections on a saturated input socket. Capture for
    // undo so the history entry can re-add them on undo.
    std::vector<CompositorLink> displacedLinks;
    if (dstSocket->maxConnections > 0 &&
        countOnSocket(link.targetNodeId, link.targetSocketId) >= dstSocket->maxConnections) {
        for (const auto& l : links) {
            if (l.targetNodeId == link.targetNodeId && l.targetSocketId == link.targetSocketId) {
                displacedLinks.push_back(l);
            }
        }
        for (const auto& l : displacedLinks) {
            if (onLinkRemoved) onLinkRemoved(l.id);
        }
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const CompositorLink& l) {
                return l.targetNodeId == link.targetNodeId &&
                       l.targetSocketId == link.targetSocketId;
            }), links.end());
    }

    ApplyAddLink(link);
    if (onLinkCreated) onLinkCreated(links.back());

    if (historyRecording) {
        std::string linkId = link.id;
        PushHistory({
            // undo: remove the new link, restore displaced ones
            [this, linkId, displacedLinks]() {
                ApplyRemoveLink(linkId);
                for (const auto& l : displacedLinks) ApplyAddLink(l);
            },
            // redo: remove displaced again, add the new one
            [this, link, displacedLinks]() {
                for (const auto& l : displacedLinks) ApplyRemoveLink(l.id);
                ApplyAddLink(link);
            },
            "Add link"
        });
    }
    return true;
}

void UltraCanvasCompositorDiagram::RemoveLink(const std::string& linkId) {
    auto it = std::find_if(links.begin(), links.end(),
        [&](const CompositorLink& l) { return l.id == linkId; });
    if (it == links.end()) return;
    CompositorLink savedLink = *it;
    if (onLinkRemoved) onLinkRemoved(linkId);
    ApplyRemoveLink(linkId);
    if (historyRecording) {
        PushHistory({
            [this, savedLink]() { ApplyAddLink(savedLink); },
            [this, linkId]()    { ApplyRemoveLink(linkId); },
            "Remove link"
        });
    }
}

void UltraCanvasCompositorDiagram::RemoveLinksOnSocket(const std::string& nodeId,
                                                        const std::string& socketId) {
    // Capture all affected links before erasing for a single composite history entry.
    std::vector<CompositorLink> saved;
    for (const auto& l : links) {
        if ((l.sourceNodeId == nodeId && l.sourceSocketId == socketId) ||
            (l.targetNodeId == nodeId && l.targetSocketId == socketId)) {
            saved.push_back(l);
        }
    }
    if (saved.empty()) return;
    for (const auto& l : saved) {
        if (onLinkRemoved) onLinkRemoved(l.id);
    }
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const CompositorLink& l) {
            return (l.sourceNodeId == nodeId && l.sourceSocketId == socketId) ||
                   (l.targetNodeId == nodeId && l.targetSocketId == socketId);
        }), links.end());
    RequestRedraw();

    if (historyRecording) {
        PushHistory({
            [this, saved]() { for (const auto& l : saved) ApplyAddLink(l); },
            [this, saved]() { for (const auto& l : saved) ApplyRemoveLink(l.id); },
            "Disconnect socket"
        });
    }
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
// SNAP-TO-GRID
// =============================================================================

void UltraCanvasCompositorDiagram::SetSnapToGrid(bool enabled) {
    snapGrid.enabled = enabled;
}

void UltraCanvasCompositorDiagram::SetSnapGrid(double snapX, double snapY) {
    if (snapX > 0.0) snapGrid.snapX = snapX;
    if (snapY > 0.0) snapGrid.snapY = snapY;
}

Point2Df UltraCanvasCompositorDiagram::SnapWorldPoint(const Point2Df& p) const {
    if (!snapGrid.enabled) return p;
    Point2Df out;
    out.x = std::round(p.x / snapGrid.snapX) * snapGrid.snapX;
    out.y = std::round(p.y / snapGrid.snapY) * snapGrid.snapY;
    return out;
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
    Point2Df world = GetNodeWorldPos(node.id);  // walks parent chain
    double nx = world.x;
    double ny = world.y;
    double w = (node.width > 0.0) ? node.width : tmpl.defaultWidth;
    double rowH = style.rowHeight;
    double headerH = style.headerHeight;

    int nOut = static_cast<int>(tmpl.outputs.size());
    int nParam = static_cast<int>(tmpl.params.size());
    int nIn = static_cast<int>(tmpl.inputs.size());

    double bodyRowsH = (nOut + nParam + nIn) * rowH;
    double previewH = (tmpl.hasPreview && !node.collapsed && !tmpl.isGroup) ? tmpl.previewHeight : 0.0;
    double previewPad = (previewH > 0.0) ? 6.0 : 0.0;
    double bodyH = node.collapsed ? 0.0 : (bodyRowsH + previewH + previewPad);
    if (tmpl.isGroup) {
        // Groups have a tall body that auto-fits to children below.
        bodyH = std::max(bodyH, tmpl.groupMinHeight - headerH);
    }
    double totalH = headerH + bodyH + 2.0;

    L.bounds       = Rect2Df(nx, ny, w, totalH);
    L.headerBounds = Rect2Df(nx, ny, w, headerH);
    L.bodyBounds   = Rect2Df(nx, ny + headerH, w, bodyH);

    if (node.collapsed) return L;

    L.outputSocketBounds.reserve(nOut);
    L.inputSocketBounds.reserve(nIn);
    L.rowBounds.reserve(nParam);
    L.rowWidgetBounds.reserve(nParam);

    const SocketTypeInfo* anyInfo = GetSocketTypeInfo(SocketDataType::Any);
    double defaultDotR = anyInfo ? anyInfo->radius : 5.0;
    double y = ny + headerH;

    for (int i = 0; i < nOut; ++i) {
        const auto& sock = tmpl.outputs[i];
        const auto* info = GetSocketTypeInfo(sock.type, sock.customTag);
        double r = info ? info->radius : defaultDotR;
        double cx = nx + w;
        double cy = y + rowH * 0.5;
        L.outputSocketBounds.push_back(Rect2Df(cx - r, cy - r, 2 * r, 2 * r));
        y += rowH;
    }

    for (int i = 0; i < nParam; ++i) {
        const auto& spec = tmpl.params[i];
        Rect2Df rowR(nx, y, w, rowH);
        L.rowBounds.push_back(rowR);
        if (spec.widget == ParamWidgetKind::NoWidget) {
            L.rowWidgetBounds.push_back(Rect2Df(0, 0, 0, 0));
        } else {
            double widgetW = std::max(style.widgetMinWidth, w * 0.45);
            double widgetH = rowH - 6.0;
            double widgetX = nx + w - style.rowPaddingX - widgetW;
            double widgetY = y + 3.0;
            L.rowWidgetBounds.push_back(Rect2Df(widgetX, widgetY, widgetW, widgetH));
        }
        y += rowH;
    }

    for (int i = 0; i < nIn; ++i) {
        const auto& sock = tmpl.inputs[i];
        const auto* info = GetSocketTypeInfo(sock.type, sock.customTag);
        double r = info ? info->radius : defaultDotR;
        double cx = nx;
        double cy = y + rowH * 0.5;
        L.inputSocketBounds.push_back(Rect2Df(cx - r, cy - r, 2 * r, 2 * r));
        y += rowH;
    }

    if (tmpl.hasPreview && !tmpl.isGroup) {
        L.previewBounds = Rect2Df(nx + style.rowPaddingX,
                                   y + previewPad,
                                   w - 2 * style.rowPaddingX,
                                   tmpl.previewHeight);
    }

    // Group nodes auto-fit their bounds to enclose direct children + padding.
    // We do this after the row layout so the header/body inherit the expanded
    // size automatically.
    if (tmpl.isGroup) {
        double pad = tmpl.groupPadding;
        double minXb = L.bounds.x;
        double minYb = L.bounds.y;
        double maxXb = L.bounds.x + std::max(L.bounds.width,  tmpl.groupMinWidth);
        double maxYb = L.bounds.y + std::max(L.bounds.height, tmpl.groupMinHeight);
        for (const auto& kv : nodes) {
            if (kv.second.parentId != node.id) continue;
            const auto* ct = GetTemplate(kv.second.templateId);
            if (!ct) continue;
            NodeLayout cL = ComputeNodeLayout(kv.second, *ct);
            minXb = std::min(minXb, cL.bounds.x - pad);
            minYb = std::min(minYb, cL.bounds.y - pad - headerH);  // reserve space above for the group header
            maxXb = std::max(maxXb, cL.bounds.x + cL.bounds.width  + pad);
            maxYb = std::max(maxYb, cL.bounds.y + cL.bounds.height + pad);
        }
        L.bounds       = Rect2Df(minXb, minYb, maxXb - minXb, maxYb - minYb);
        L.headerBounds = Rect2Df(L.bounds.x, L.bounds.y, L.bounds.width, headerH);
        L.bodyBounds   = Rect2Df(L.bounds.x, L.bounds.y + headerH,
                                  L.bounds.width, L.bounds.height - headerH);
    }
    return L;
}

Point2Df UltraCanvasCompositorDiagram::GetSocketWorldPosition(
        const CompositorNode& node, const CompositorNodeTemplate& tmpl,
        bool isOutput, int socketIndex) const {
    NodeLayout L = ComputeNodeLayout(node, tmpl);
    Point2Df nworld = GetNodeWorldPos(node.id);
    if (isOutput) {
        if (socketIndex < 0 || socketIndex >= static_cast<int>(L.outputSocketBounds.size()))
            return Point2Df(nworld.x + ((node.width > 0) ? node.width : tmpl.defaultWidth), nworld.y);
        const auto& r = L.outputSocketBounds[socketIndex];
        return Point2Df(r.x + r.width * 0.5, r.y + r.height * 0.5);
    } else {
        if (socketIndex < 0 || socketIndex >= static_cast<int>(L.inputSocketBounds.size()))
            return Point2Df(nworld.x, nworld.y);
        const auto& r = L.inputSocketBounds[socketIndex];
        return Point2Df(r.x + r.width * 0.5, r.y + r.height * 0.5);
    }
}

// =============================================================================
// HIT TESTING
// =============================================================================

std::string UltraCanvasCompositorDiagram::FindNodeAt(const Point2Di& screenPos) {
    Point2Df world = ScreenToWorld(screenPos);
    // Walk REVERSE render order so children (which render after their parent)
    // win clicks over their parent's body. The visually-topmost node wins.
    auto order = BuildRenderOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
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
    // Reverse render-order so a socket on a node-on-top (incl. children inside
    // a group) wins over an obscured one below.
    auto order = BuildRenderOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
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
    auto order = BuildRenderOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
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
    if (isConnecting)    RenderConnectionLine(ctx);
    if (isSelectingBox)  RenderSelectionBox(ctx);
    if (isDraggingNode && alignmentGuidesEnabled) RenderAlignmentGuides(ctx);

    ctx->PopState();

    // Overlays render in screen space.
    if (minimapConfig.visible)  RenderMinimap(ctx);
    if (controlsConfig.visible) RenderControls(ctx);
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

        // Optional edge label at the midpoint - a small rounded "pill" with text.
        if (!l.label.empty() && path.size() >= 2) {
            Point2Df mid = path[path.size() / 2];
            ctx->SetFontFamily(style.fontFamily);
            ctx->SetFontSize(style.socketLabelFontSize);
            Size2Di td = ctx->GetTextLineDimensions(l.label);
            double padX = 6.0;
            double padY = 3.0;
            Rect2Df pill(mid.x - td.width * 0.5 - padX,
                          mid.y - td.height * 0.5 - padY,
                          td.width + 2 * padX,
                          td.height + 2 * padY);
            ctx->SetFillPaint(l.labelBgColor);
            ctx->FillRoundedRectangle(pill, pill.height * 0.5);
            ctx->SetTextPaint(l.labelTextColor);
            ctx->DrawText(l.label,
                Point2Df(pill.x + padX, pill.y + padY));
        }
    }
}

void UltraCanvasCompositorDiagram::RenderNodes(IRenderContext* ctx) {
    // Render in tree order (roots first, then children) so nested children
    // appear visually on top of their group containers.
    auto order = BuildRenderOrder();
    for (const auto& id : order) {
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

    // Group nodes: the body is intentionally just a translucent container so
    // children show through. Skip param-row, socket, and preview drawing
    // entirely (group templates can still declare sockets if the author wants
    // interface sockets, in which case we'd draw them - but the React Flow
    // default group is contents-only, so we ship that as the convention).
    if (tmpl.isGroup) {
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
        if (tmpl.customPreviewRenderer) {
            tmpl.customPreviewRenderer(ctx, pb, node);
        } else {
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
        }   // end of "default renderer" else branch
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

    Point2Di mousePos(event.pointer.x, event.pointer.y);
    lastMousePos = mousePos;
    dragStartPos = mousePos;
    isMultiSelectKeyHeld = event.shift;

    // 0a. Controls overlay button click - always reachable even when
    //     interaction is locked, otherwise the lock button can't be released.
    if (controlsConfig.visible && event.button == UCMouseButton::Left) {
        int btnIdx = FindControlButtonAt(mousePos);
        if (btnIdx >= 0) {
            int idx = btnIdx;
            if (controlsConfig.showZoom) {
                if (idx == 0) { ZoomIn();  return true; }
                if (idx == 1) { ZoomOut(); return true; }
                idx -= 2;
            }
            if (controlsConfig.showFit) {
                if (idx == 0) { FitView(); return true; }
                idx -= 1;
            }
            if (controlsConfig.showLock) {
                if (idx == 0) { SetInteractive(!isInteractive); return true; }
            }
            return true;
        }
    }

    if (!isInteractive && event.button != UCMouseButton::Middle) return false;

    // 0b. Minimap click → recenter viewport (or start minimap drag).
    if (minimapConfig.visible && minimapConfig.pannable &&
        event.button == UCMouseButton::Left && PointInMinimap(mousePos)) {
        isDraggingMinimap = true;
        HandleMinimapDrag(mousePos);
        return true;
    }

    // Right-click: dispatch to the most-specific hook (socket > node > link >
    // canvas). The socket fallback (disconnect all) only triggers when no
    // onSocketRightClick hook is set, so apps that want custom menus retain
    // full control of socket UX.
    if (event.button == UCMouseButton::Right) {
        Point2Df w = ScreenToWorld(mousePos);
        SocketHit sh = FindSocketAt(mousePos);
        if (!sh.socketId.empty()) {
            if (onSocketRightClick) {
                onSocketRightClick(sh.nodeId, sh.socketId, sh.isOutput, w.x, w.y);
            } else {
                RemoveLinksOnSocket(sh.nodeId, sh.socketId);
            }
            return true;
        }
        std::string nid = FindNodeAt(mousePos);
        if (!nid.empty()) {
            if (onNodeRightClick) onNodeRightClick(nid, w.x, w.y);
            return true;
        }
        std::string lid = FindLinkAt(mousePos);
        if (!lid.empty()) {
            if (onLinkRightClick) onLinkRightClick(lid, w.x, w.y);
            return true;
        }
        if (onCanvasRightClick) onCanvasRightClick(w.x, w.y);
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
            // Edge reconnection: if the user clicked a single-connection socket
            // that already has an edge attached, pick the edge up by its near
            // endpoint and let them re-route it. The original edge is removed
            // (with history suppressed) and a connect-drag starts from the
            // OTHER endpoint.
            bool reconnectStarted = false;
            if (edgeReconnectionEnabled) {
                // Find an existing link on this socket.
                CompositorLink* attached = nullptr;
                for (auto& l : links) {
                    if ((l.sourceNodeId == sh.nodeId && l.sourceSocketId == sh.socketId) ||
                        (l.targetNodeId == sh.nodeId && l.targetSocketId == sh.socketId)) {
                        attached = &l;
                        break;
                    }
                }
                if (attached) {
                    // Only reconnect-pick the side of a single-connection socket
                    // (typical: inputs). On unlimited-fanout sockets (typical:
                    // outputs), preserve the fresh-connection UX.
                    const auto* node = GetNode(sh.nodeId);
                    const auto* tmpl = node ? GetTemplate(node->templateId) : nullptr;
                    bool singleConn = false;
                    if (tmpl) {
                        CompositorSocketSpec stor;
                        bool dummyIsOut = false;
                        const auto* spec = ResolveSocketSpec(*tmpl, sh.socketId, dummyIsOut, stor);
                        if (spec && spec->maxConnections == 1) singleConn = true;
                    }
                    if (singleConn) {
                        reconnectSavedLink = *attached;
                        // Drag from the OTHER endpoint.
                        bool otherIsOutput;
                        std::string otherNode, otherSocket;
                        if (attached->sourceNodeId == sh.nodeId &&
                            attached->sourceSocketId == sh.socketId) {
                            otherNode = attached->targetNodeId;
                            otherSocket = attached->targetSocketId;
                            otherIsOutput = false;
                        } else {
                            otherNode = attached->sourceNodeId;
                            otherSocket = attached->sourceSocketId;
                            otherIsOutput = true;
                        }
                        bool prevRec = historyRecording;
                        historyRecording = false;
                        RemoveLink(attached->id);
                        historyRecording = prevRec;
                        isReconnectingEdge = true;
                        isConnecting = true;
                        connectionSourceNode = otherNode;
                        connectionSourceSocket = otherSocket;
                        connectionSourceIsOutput = otherIsOutput;
                        connectionEndPoint = ScreenToWorld(mousePos);
                        reconnectStarted = true;
                    }
                }
            }
            if (!reconnectStarted) {
                isConnecting = true;
                connectionSourceNode = sh.nodeId;
                connectionSourceSocket = sh.socketId;
                connectionSourceIsOutput = sh.isOutput;
                connectionEndPoint = ScreenToWorld(mousePos);
            }
            return true;
        }
    }

    // 2. Row widget hit. Mutations go through SetParam* so they record into
    // the undo history. Widgets needing a popup editor (ColorSwatch, TextInput,
    // FileBrowser, NumberInput) just fire onParamChange with the current value
    // - the host app is expected to open the editor.
    WidgetHit wh = FindRowWidgetAt(mousePos);
    if (!wh.paramId.empty()) {
        auto* node = GetNode(wh.nodeId);
        const auto* tmpl = node ? GetTemplate(node->templateId) : nullptr;
        const auto* spec = tmpl ? FindParamSpec(*tmpl, wh.paramId) : nullptr;
        if (node && spec) {
            SelectNode(wh.nodeId, event.shift);
            switch (spec->widget) {
                case ParamWidgetKind::Dropdown: {
                    if (!spec->choices.empty()) {
                        auto it = node->paramValues.find(spec->id);
                        int cur = (it == node->paramValues.end()) ? 0 : it->second.dropdownIndex;
                        int next = cur + 1;
                        if (next >= (int)spec->choices.size()) next = 0;
                        SetParamDropdownIndex(node->id, spec->id, next);
                    }
                    return true;
                }
                case ParamWidgetKind::Checkbox: {
                    auto it = node->paramValues.find(spec->id);
                    bool cur = (it != node->paramValues.end()) && it->second.boolean;
                    SetParamBool(node->id, spec->id, !cur);
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
                        SetParamNumber(node->id, spec->id, newVal);
                        return true;
                    }
                    return true;
                }
                default:
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
        dragLeaderId = nid;
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

    // 5. Empty canvas:
    //   - shift+drag (or pan-on-drag disabled): start rubber-band selection
    //   - otherwise: start pan
    Point2Df world = ScreenToWorld(mousePos);
    if (event.shift || !panOnDrag) {
        isSelectingBox = true;
        selectionBoxStart = world;
        selectionBoxEnd   = world;
        if (event.shift) {
            // Preserve existing selection so the rubber-band extends it.
            selectionBoxStartNodes = selectedNodes;
            selectionBoxStartLinks = selectedLinks;
        } else {
            selectionBoxStartNodes.clear();
            selectionBoxStartLinks.clear();
            DeselectAll();
        }
    } else {
        DeselectAll();
        isDraggingViewport = true;
    }
    return true;
}

bool UltraCanvasCompositorDiagram::HandleMouseUp(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isConnecting) {
        SocketHit sh = FindSocketAt(mousePos);
        bool ok = false;
        std::string srcNode, srcSock, dstNode, dstSock;
        if (!sh.socketId.empty() && sh.nodeId != connectionSourceNode) {
            if (connectionSourceIsOutput && !sh.isOutput) {
                srcNode = connectionSourceNode; srcSock = connectionSourceSocket;
                dstNode = sh.nodeId;             dstSock = sh.socketId;
                ok = true;
            } else if (!connectionSourceIsOutput && sh.isOutput) {
                srcNode = sh.nodeId;             srcSock = sh.socketId;
                dstNode = connectionSourceNode; dstSock = connectionSourceSocket;
                ok = true;
            }
        }

        if (isReconnectingEdge) {
            // Reconnection flow: we already removed the original link. Bundle
            // "remove original + add new" or "remove original + restore" into
            // one composite undo entry so user gets a single-step undo.
            CompositorLink original = reconnectSavedLink;
            CompositorLink newLink;
            bool addedNew = false;
            if (ok) {
                newLink = original;  // preserve style/lineWidth/etc.
                newLink.id = GenerateUniqueLinkId();
                newLink.sourceNodeId = srcNode;
                newLink.sourceSocketId = srcSock;
                newLink.targetNodeId = dstNode;
                newLink.targetSocketId = dstSock;
                bool prevRec = historyRecording;
                historyRecording = false;
                if (AddLink(newLink)) addedNew = true;
                historyRecording = prevRec;
            }
            // Push a single composite history entry.
            if (historyRecording) {
                if (addedNew) {
                    std::string newId = newLink.id;
                    PushHistory({
                        // undo: remove new, restore original
                        [this, newId, original]() {
                            ApplyRemoveLink(newId);
                            ApplyAddLink(original);
                        },
                        // redo: remove original, add new
                        [this, original, newLink]() {
                            ApplyRemoveLink(original.id);
                            ApplyAddLink(newLink);
                        },
                        "Reconnect edge"
                    });
                } else {
                    // Drop on empty / incompatible: the original is gone. One
                    // undoable "remove" step.
                    PushHistory({
                        [this, original]() { ApplyAddLink(original); },
                        [this, original]() { ApplyRemoveLink(original.id); },
                        "Remove edge (drop)"
                    });
                }
            }
            isReconnectingEdge = false;
        } else if (ok) {
            // Fresh connect-drag.
            AddLink(GenerateUniqueLinkId(), srcNode, srcSock, dstNode, dstSock);
        }
        isConnecting = false;
        connectionSourceNode.clear();
        connectionSourceSocket.clear();
        RequestRedraw();
        return true;
    }

    if (isDraggingMinimap) {
        isDraggingMinimap = false;
        return true;
    }

    if (isDraggingNode) {
        // Push a MoveNodes history entry if anything actually moved.
        std::map<std::string, Point2Df> finalPositions;
        bool moved = false;
        for (const auto& kv : dragStartPositions) {
            const auto* n = GetNode(kv.first);
            if (!n) continue;
            finalPositions[kv.first] = Point2Df(n->x, n->y);
            if (n->x != kv.second.x || n->y != kv.second.y) moved = true;
        }
        if (moved && historyRecording) {
            auto starts = dragStartPositions;
            PushHistory({
                [this, starts]()         { ApplyMoveNodes(starts); },
                [this, finalPositions]() { ApplyMoveNodes(finalPositions); },
                "Move nodes"
            });
        }
        isDraggingNode = false;
        dragStartPositions.clear();
        dragLeaderId.clear();
        currentAlignmentGuides.clear();
        RequestRedraw();
        return true;
    }
    if (isSelectingBox) {
        isSelectingBox = false;
        selectionBoxStartNodes.clear();
        selectionBoxStartLinks.clear();
        RequestRedraw();
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

    if (isDraggingMinimap) {
        HandleMinimapDrag(mousePos);
        return true;
    }

    if (isDraggingNode) {
        Point2Df startW = ScreenToWorld(dragStartPos);
        Point2Df curW   = ScreenToWorld(mousePos);
        double dx = curW.x - startW.x;
        double dy = curW.y - startW.y;

        // Compute the leader's proposed position, apply grid snap, then
        // alignment snap on top. The same dx/dy is applied to all dragged nodes
        // so the group moves rigidly.
        Point2Df leaderProposed(0, 0);
        bool haveLeader = false;
        if (!dragLeaderId.empty()) {
            auto lit = dragStartPositions.find(dragLeaderId);
            if (lit != dragStartPositions.end()) {
                leaderProposed = Point2Df(lit->second.x + dx, lit->second.y + dy);
                haveLeader = true;
            }
        }
        if (haveLeader) {
            Point2Df gridSnapped = SnapWorldPoint(leaderProposed);
            Point2Df alignSnapped = alignmentGuidesEnabled
                ? ComputeAlignmentSnap(dragLeaderId, gridSnapped)
                : gridSnapped;
            dx = alignSnapped.x - dragStartPositions[dragLeaderId].x;
            dy = alignSnapped.y - dragStartPositions[dragLeaderId].y;
        }

        for (const auto& kv : dragStartPositions) {
            auto* n = GetNode(kv.first);
            if (!n) continue;
            Point2Df raw(kv.second.x + dx, kv.second.y + dy);
            // Only apply grid snap on non-leader nodes when there's no leader
            // (paranoia path; in practice haveLeader is always true).
            Point2Df final = haveLeader ? raw : SnapWorldPoint(raw);
            n->x = final.x;
            n->y = final.y;
            if (onNodeDrag) onNodeDrag(kv.first, n->x, n->y);
        }
        RequestRedraw();
        return true;
    }

    if (isSelectingBox) {
        selectionBoxEnd = ScreenToWorld(mousePos);
        // Recompute selection: enclosed nodes + intra-selection links, union'd
        // with the pre-drag selection.
        double x0 = std::min(selectionBoxStart.x, selectionBoxEnd.x);
        double y0 = std::min(selectionBoxStart.y, selectionBoxEnd.y);
        double x1 = std::max(selectionBoxStart.x, selectionBoxEnd.x);
        double y1 = std::max(selectionBoxStart.y, selectionBoxEnd.y);
        Rect2Df box(x0, y0, x1 - x0, y1 - y0);

        std::set<std::string> newSelectedNodes = selectionBoxStartNodes;
        for (const auto& id : nodeOrder) {
            const auto* n = GetNode(id);
            if (!n) continue;
            const auto* tmpl = GetTemplate(n->templateId);
            if (!tmpl) continue;
            NodeLayout L = ComputeNodeLayout(*n, *tmpl);
            // Fully-enclosed: node bounds entirely inside box. This is the
            // React Flow / Blender convention - half-enclosed nodes are NOT
            // selected, avoiding accidental selection while panning over them.
            if (L.bounds.x >= box.x && L.bounds.y >= box.y &&
                L.bounds.x + L.bounds.width  <= box.x + box.width &&
                L.bounds.y + L.bounds.height <= box.y + box.height) {
                newSelectedNodes.insert(id);
            }
        }
        std::set<std::string> newSelectedLinks = selectionBoxStartLinks;
        for (const auto& l : links) {
            if (newSelectedNodes.count(l.sourceNodeId) &&
                newSelectedNodes.count(l.targetNodeId)) {
                newSelectedLinks.insert(l.id);
            }
        }
        if (newSelectedNodes != selectedNodes || newSelectedLinks != selectedLinks) {
            selectedNodes = std::move(newSelectedNodes);
            selectedLinks = std::move(newSelectedLinks);
            NotifySelectionChange();
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

    // Update hover for visual feedback.
    lastMousePos = mousePos;
    int newHoveredCtrl = controlsConfig.visible ? FindControlButtonAt(mousePos) : -1;
    std::string newHoveredNode;
    std::string newHoveredLink;
    if (newHoveredCtrl < 0 && !PointInMinimap(mousePos)) {
        newHoveredNode = FindNodeAt(mousePos);
        newHoveredLink = newHoveredNode.empty() ? FindLinkAt(mousePos) : "";
    }
    if (newHoveredNode != hoveredNodeId || newHoveredLink != hoveredLinkId ||
        newHoveredCtrl != hoveredControlButton) {
        hoveredNodeId = newHoveredNode;
        hoveredLinkId = newHoveredLink;
        hoveredControlButton = newHoveredCtrl;
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
                // If we were reconnecting an edge, Esc restores the original.
                if (isReconnectingEdge) {
                    bool prevRec = historyRecording;
                    historyRecording = false;
                    AddLink(reconnectSavedLink);
                    historyRecording = prevRec;
                    isReconnectingEdge = false;
                }
                isConnecting = false;
                connectionSourceNode.clear();
                connectionSourceSocket.clear();
                RequestRedraw();
                return true;
            }
            if (isSelectingBox) {
                isSelectingBox = false;
                selectedNodes = selectionBoxStartNodes;
                selectedLinks = selectionBoxStartLinks;
                selectionBoxStartNodes.clear();
                selectionBoxStartLinks.clear();
                NotifySelectionChange();
                RequestRedraw();
                return true;
            }
            DeselectAll();
            return true;
        case UCKeys::Tab:
            if (onPaletteRequested) {
                Point2Df w = ScreenToWorld(lastMousePos);
                onPaletteRequested(w.x, w.y);
                return true;
            }
            break;
        case UCKeys::A:
            if (event.ctrl) { SelectAll();    return true; }
            break;
        case UCKeys::Z:
            if (event.ctrl && event.shift) { Redo(); return true; }
            if (event.ctrl)                { Undo(); return true; }
            break;
        case UCKeys::Y:
            if (event.ctrl) { Redo();         return true; }
            break;
        case UCKeys::C:
            if (event.ctrl) { Copy();         return true; }
            break;
        case UCKeys::X:
            if (event.ctrl) { Cut();          return true; }
            break;
        case UCKeys::V:
            if (event.ctrl) { Paste();        return true; }
            break;
        case UCKeys::D:
            if (event.ctrl) { Duplicate();    return true; }
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
    // Programmatic load: suppress per-op history, wipe the existing history,
    // then resume recording so subsequent user edits ARE undoable.
    bool prevRec = historyRecording;
    historyRecording = false;
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

    historyRecording = prevRec;
    undoStack.clear();
    redoStack.clear();
    NotifyHistoryChange();
    RequestRedraw();
    return true;
}

// =============================================================================
// APPLY-* PRIMITIVES (raw mutators - used by undo/redo lambdas, no history)
// =============================================================================

void UltraCanvasCompositorDiagram::ApplyAddNode(const CompositorNode& node) {
    if (nodes.count(node.id)) return;
    nodes[node.id] = node;
    nodeOrder.push_back(node.id);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ApplyRemoveNode(const std::string& nodeId) {
    if (!nodes.count(nodeId)) return;
    nodes.erase(nodeId);
    nodeOrder.erase(std::remove(nodeOrder.begin(), nodeOrder.end(), nodeId),
                    nodeOrder.end());
    selectedNodes.erase(nodeId);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ApplyAddLink(const CompositorLink& link) {
    links.push_back(link);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ApplyRemoveLink(const std::string& linkId) {
    auto it = std::find_if(links.begin(), links.end(),
        [&](const CompositorLink& l) { return l.id == linkId; });
    if (it == links.end()) return;
    links.erase(it);
    selectedLinks.erase(linkId);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ApplySetParamValue(
        const std::string& nodeId, const std::string& paramId,
        const ParamValue& value) {
    auto* node = GetNode(nodeId);
    if (!node) return;
    node->paramValues[paramId] = value;
    if (onParamChange) onParamChange(nodeId, paramId, value);
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::ApplyMoveNodes(
        const std::map<std::string, Point2Df>& positions) {
    for (const auto& kv : positions) {
        auto* n = GetNode(kv.first);
        if (n) { n->x = kv.second.x; n->y = kv.second.y; }
    }
    RequestRedraw();
}

// =============================================================================
// HISTORY
// =============================================================================

void UltraCanvasCompositorDiagram::PushHistory(HistoryEntry entry) {
    if (!historyRecording) return;
    undoStack.push_back(std::move(entry));
    if (undoStack.size() > historyLimit) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
    NotifyHistoryChange();
}

void UltraCanvasCompositorDiagram::Undo() {
    if (undoStack.empty()) return;
    HistoryEntry e = std::move(undoStack.back());
    undoStack.pop_back();
    bool prev = historyRecording;
    historyRecording = false;
    if (e.undo) e.undo();
    historyRecording = prev;
    redoStack.push_back(std::move(e));
    NotifyHistoryChange();
}

void UltraCanvasCompositorDiagram::Redo() {
    if (redoStack.empty()) return;
    HistoryEntry e = std::move(redoStack.back());
    redoStack.pop_back();
    bool prev = historyRecording;
    historyRecording = false;
    if (e.redo) e.redo();
    historyRecording = prev;
    undoStack.push_back(std::move(e));
    NotifyHistoryChange();
}

void UltraCanvasCompositorDiagram::ClearHistory() {
    undoStack.clear();
    redoStack.clear();
    NotifyHistoryChange();
}

void UltraCanvasCompositorDiagram::NotifyHistoryChange() {
    if (onHistoryChange) onHistoryChange();
}

// =============================================================================
// SELECTION-BOX RENDERING
// =============================================================================

void UltraCanvasCompositorDiagram::RenderSelectionBox(IRenderContext* ctx) {
    if (!isSelectingBox) return;
    double x0 = std::min(selectionBoxStart.x, selectionBoxEnd.x);
    double y0 = std::min(selectionBoxStart.y, selectionBoxEnd.y);
    double x1 = std::max(selectionBoxStart.x, selectionBoxEnd.x);
    double y1 = std::max(selectionBoxStart.y, selectionBoxEnd.y);
    Rect2Df r(x0, y0, x1 - x0, y1 - y0);
    ctx->SetFillPaint(style.selectionBoxFill);
    ctx->FillRectangle(r);
    ctx->SetStrokePaint(style.selectionBoxStroke);
    ctx->SetStrokeWidth(1.0 / std::max(0.01, zoomLevel));
    ctx->DrawRectangle(r);
}

// =============================================================================
// JSON HELPERS - per-node / per-link write & parse (reused by Copy/Paste)
// =============================================================================

void UltraCanvasCompositorDiagram::WriteNodeJson(std::ostream& out,
                                                   const CompositorNode& n) const {
    out << "    {\n";
    out << "      \"id\": " << EscapeJsonString(n.id) << ",\n";
    out << "      \"templateId\": " << EscapeJsonString(n.templateId) << ",\n";
    out << "      \"titleOverride\": " << EscapeJsonString(n.titleOverride) << ",\n";
    out << "      \"parentId\": " << EscapeJsonString(n.parentId) << ",\n";
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

void UltraCanvasCompositorDiagram::WriteLinkJson(std::ostream& out,
                                                   const CompositorLink& l) const {
    out << "    {\n";
    out << "      \"id\": " << EscapeJsonString(l.id) << ",\n";
    out << "      \"srcNode\": " << EscapeJsonString(l.sourceNodeId) << ",\n";
    out << "      \"srcSocket\": " << EscapeJsonString(l.sourceSocketId) << ",\n";
    out << "      \"dstNode\": " << EscapeJsonString(l.targetNodeId) << ",\n";
    out << "      \"dstSocket\": " << EscapeJsonString(l.targetSocketId) << ",\n";
    out << "      \"style\": \"" << LinkStyleToCompositorString(l.style) << "\",\n";
    out << "      \"lineWidth\": " << l.lineWidth << ",\n";
    out << "      \"label\": " << EscapeJsonString(l.label) << "\n";
    out << "    }";
}

bool UltraCanvasCompositorDiagram::ParseNodeFromObject(const std::string& obj,
                                                        CompositorNode& out) {
    out.id = ExtractStringValue(obj, "id");
    if (out.id.empty()) return false;
    out.templateId = ExtractStringValue(obj, "templateId");
    out.titleOverride = ExtractStringValue(obj, "titleOverride");
    out.parentId = ExtractStringValue(obj, "parentId");
    out.x = ExtractNumberValue(obj, "x");
    out.y = ExtractNumberValue(obj, "y");
    out.width = ExtractNumberValue(obj, "width");
    out.collapsed = ExtractBoolValue(obj, "collapsed");
    out.previewHandle = ExtractStringValue(obj, "previewHandle");
    for (const auto& pobj : ExtractObjectArray(obj, "params")) {
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
        out.paramValues[pid] = v;
    }
    return true;
}

bool UltraCanvasCompositorDiagram::ParseLinkFromObject(const std::string& obj,
                                                        CompositorLink& out) {
    out.id = ExtractStringValue(obj, "id");
    out.sourceNodeId   = ExtractStringValue(obj, "srcNode");
    out.sourceSocketId = ExtractStringValue(obj, "srcSocket");
    out.targetNodeId   = ExtractStringValue(obj, "dstNode");
    out.targetSocketId = ExtractStringValue(obj, "dstSocket");
    out.style = LinkStyleFromCompositorString(ExtractStringValue(obj, "style"));
    out.lineWidth = ExtractNumberValue(obj, "lineWidth", 2.0);
    out.label = ExtractStringValue(obj, "label");
    return !out.sourceNodeId.empty() && !out.targetNodeId.empty();
}

// =============================================================================
// CLIPBOARD (COPY / CUT / PASTE / DUPLICATE)
// =============================================================================

std::string UltraCanvasCompositorDiagram::GenerateUniqueNodeId(
        const std::string& base) const {
    std::string trimmed = base;
    // Strip trailing _copyN suffix so repeated pastes don't grow indefinitely.
    auto pos = trimmed.rfind("_copy");
    if (pos != std::string::npos) trimmed.erase(pos);
    for (int n = 1; n < 100000; ++n) {
        std::string candidate = trimmed + "_copy" + std::to_string(n);
        if (!nodes.count(candidate)) return candidate;
    }
    return trimmed + "_copy_" + std::to_string(std::rand());
}

std::string UltraCanvasCompositorDiagram::GenerateUniqueLinkId() const {
    for (int i = 0; i < 100000; ++i) {
        ++linkIdCounter;
        std::string candidate = "link_" + std::to_string(linkIdCounter);
        bool taken = false;
        for (const auto& l : links) if (l.id == candidate) { taken = true; break; }
        if (!taken) return candidate;
    }
    return "link_" + std::to_string(std::rand());
}

void UltraCanvasCompositorDiagram::Copy() {
    if (selectedNodes.empty()) {
        clipboard.clear();
        return;
    }
    std::ostringstream out;
    out << "{\n  \"nodes\": [\n";
    bool first = true;
    for (const auto& id : nodeOrder) {
        if (!selectedNodes.count(id)) continue;
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        if (!first) out << ",\n";
        first = false;
        WriteNodeJson(out, it->second);
    }
    out << "\n  ],\n  \"links\": [\n";
    first = true;
    for (const auto& l : links) {
        // Only links where BOTH endpoints are in the selection (intra-selection).
        if (!selectedNodes.count(l.sourceNodeId)) continue;
        if (!selectedNodes.count(l.targetNodeId)) continue;
        if (!first) out << ",\n";
        first = false;
        WriteLinkJson(out, l);
    }
    out << "\n  ]\n}\n";
    clipboard = out.str();
}

void UltraCanvasCompositorDiagram::Cut() {
    Copy();
    DeleteSelected();
}

void UltraCanvasCompositorDiagram::Paste(double offsetX, double offsetY) {
    if (clipboard.empty()) return;

    std::map<std::string, std::string> idRemap;
    std::vector<CompositorNode> toAdd;
    std::vector<CompositorLink> toLink;

    for (const auto& nobj : ExtractObjectArray(clipboard, "nodes")) {
        CompositorNode n;
        if (!ParseNodeFromObject(nobj, n)) continue;
        std::string origId = n.id;
        n.id = GenerateUniqueNodeId(origId);
        idRemap[origId] = n.id;
        n.x += offsetX;
        n.y += offsetY;
        toAdd.push_back(n);
    }
    // Second pass: remap parentId. Orphan children (parent not in the paste
    // selection) get promoted to roots so the paste produces a valid tree.
    for (auto& n : toAdd) {
        if (n.parentId.empty()) continue;
        auto itp = idRemap.find(n.parentId);
        if (itp != idRemap.end()) {
            n.parentId = itp->second;
        } else {
            // Orphan - promote to root. Its x/y were relative to a parent
            // that's not in the paste, so adopt the world position the
            // original had (best-effort: use the rel position + offset and
            // hope the user re-parents as needed; alternative would be to
            // compute the source clipboard's world position pre-paste).
            n.parentId.clear();
        }
    }
    for (const auto& lobj : ExtractObjectArray(clipboard, "links")) {
        CompositorLink l;
        if (!ParseLinkFromObject(lobj, l)) continue;
        auto its = idRemap.find(l.sourceNodeId);
        auto itt = idRemap.find(l.targetNodeId);
        if (its == idRemap.end() || itt == idRemap.end()) continue;
        l.id = GenerateUniqueLinkId();
        l.sourceNodeId = its->second;
        l.targetNodeId = itt->second;
        toLink.push_back(l);
    }
    if (toAdd.empty()) return;

    // Apply as one composite history step (suppress per-op recording).
    bool prevRec = historyRecording;
    historyRecording = false;
    for (const auto& n : toAdd) AddNode(n);
    for (const auto& l : toLink) AddLink(l);
    historyRecording = prevRec;

    // Select the new nodes.
    selectedNodes.clear();
    selectedLinks.clear();
    for (const auto& n : toAdd) selectedNodes.insert(n.id);
    NotifySelectionChange();

    if (historyRecording) {
        std::vector<std::string> addedNodeIds;
        std::vector<std::string> addedLinkIds;
        for (const auto& n : toAdd) addedNodeIds.push_back(n.id);
        for (const auto& l : toLink) addedLinkIds.push_back(l.id);
        PushHistory({
            // undo
            [this, addedNodeIds, addedLinkIds]() {
                for (const auto& lid : addedLinkIds) ApplyRemoveLink(lid);
                for (const auto& nid : addedNodeIds) ApplyRemoveNode(nid);
            },
            // redo
            [this, toAdd, toLink]() {
                for (const auto& n : toAdd) ApplyAddNode(n);
                for (const auto& l : toLink) ApplyAddLink(l);
            },
            "Paste"
        });
    }
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::Duplicate(double offsetX, double offsetY) {
    if (selectedNodes.empty()) return;
    // Save & restore clipboard so Duplicate doesn't trample user's clipboard.
    std::string savedClipboard = clipboard;
    Copy();
    Paste(offsetX, offsetY);
    clipboard = savedClipboard;
}

// =============================================================================
// MINIMAP OVERLAY
// =============================================================================

void UltraCanvasCompositorDiagram::SetMinimapVisible(bool visible) {
    minimapConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetMinimapPosition(NodeDiagramPanelPosition pos) {
    minimapConfig.position = pos;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetMinimapConfig(const CompositorMinimapConfig& cfg) {
    minimapConfig = cfg;
    RequestRedraw();
}

Rect2Df UltraCanvasCompositorDiagram::GetMinimapRectScreen() const {
    double w = minimapConfig.width;
    double h = minimapConfig.height;
    double pad = minimapConfig.padding;
    double bw = static_cast<double>(GetWidth());
    double bh = static_cast<double>(GetHeight());
    double mx = pad, my = pad;
    switch (minimapConfig.position) {
        case NodeDiagramPanelPosition::TopLeft:     mx = pad;             my = pad;             break;
        case NodeDiagramPanelPosition::TopRight:    mx = bw - w - pad;    my = pad;             break;
        case NodeDiagramPanelPosition::BottomLeft:  mx = pad;             my = bh - h - pad;    break;
        case NodeDiagramPanelPosition::BottomRight: mx = bw - w - pad;    my = bh - h - pad;    break;
    }
    return Rect2Df(mx, my, w, h);
}

bool UltraCanvasCompositorDiagram::PointInMinimap(const Point2Di& screenPos) const {
    if (!minimapConfig.visible) return false;
    Rect2Df r = GetMinimapRectScreen();
    return screenPos.x >= r.x && screenPos.x <= r.x + r.width &&
           screenPos.y >= r.y && screenPos.y <= r.y + r.height;
}

void UltraCanvasCompositorDiagram::RenderMinimap(IRenderContext* ctx) {
    Rect2Df mr = GetMinimapRectScreen();
    ctx->SetFillPaint(minimapConfig.backgroundColor);
    ctx->FillRoundedRectangle(mr, 4.0);
    ctx->SetStrokePaint(minimapConfig.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(mr, 4.0);

    if (nodes.empty()) return;

    // World bbox of all node layouts (use computed bounds, not raw width/0).
    double minX =  1e18, minY =  1e18;
    double maxX = -1e18, maxY = -1e18;
    for (const auto& kv : nodes) {
        const auto* tmpl = GetTemplate(kv.second.templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(kv.second, *tmpl);
        minX = std::min(minX, L.bounds.x);
        minY = std::min(minY, L.bounds.y);
        maxX = std::max(maxX, L.bounds.x + L.bounds.width);
        maxY = std::max(maxY, L.bounds.y + L.bounds.height);
    }
    if (minX > maxX || minY > maxY) return;
    double margin = 20.0;
    minX -= margin; minY -= margin; maxX += margin; maxY += margin;

    double worldW = maxX - minX;
    double worldH = maxY - minY;
    if (worldW <= 0 || worldH <= 0) return;

    double innerPad = 6.0;
    double availW = mr.width  - 2 * innerPad;
    double availH = mr.height - 2 * innerPad;
    double scale = std::min(availW / worldW, availH / worldH);
    double drawnW = worldW * scale;
    double drawnH = worldH * scale;
    double ox = mr.x + innerPad + (availW - drawnW) * 0.5;
    double oy = mr.y + innerPad + (availH - drawnH) * 0.5;

    auto worldToMini = [&](double wx, double wy) -> Point2Df {
        return Point2Df(ox + (wx - minX) * scale, oy + (wy - minY) * scale);
    };

    ctx->SetFillPaint(minimapConfig.nodeColor);
    for (const auto& kv : nodes) {
        const auto* tmpl = GetTemplate(kv.second.templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(kv.second, *tmpl);
        Point2Df tl = worldToMini(L.bounds.x, L.bounds.y);
        double w = L.bounds.width  * scale;
        double h = L.bounds.height * scale;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        ctx->FillRectangle(Rect2Df(tl.x, tl.y, w, h));
    }

    // Viewport rectangle
    Point2Df tlW = ScreenToWorld({0, 0});
    Point2Df brW = ScreenToWorld(Point2Di(GetWidth(), GetHeight()));
    Point2Df vpTL = worldToMini(tlW.x, tlW.y);
    Point2Df vpBR = worldToMini(brW.x, brW.y);
    double vpW = vpBR.x - vpTL.x;
    double vpH = vpBR.y - vpTL.y;
    ctx->SetFillPaint(minimapConfig.viewportFill);
    ctx->FillRectangle(Rect2Df(vpTL.x, vpTL.y, vpW, vpH));
    ctx->SetStrokePaint(minimapConfig.viewportStroke);
    ctx->SetStrokeWidth(1.5);
    ctx->DrawRectangle(Rect2Df(vpTL.x, vpTL.y, vpW, vpH));
}

void UltraCanvasCompositorDiagram::HandleMinimapDrag(const Point2Di& screenPos) {
    // Map the click point inside the minimap rect to a world coord and center
    // the viewport on it.
    Rect2Df mr = GetMinimapRectScreen();
    if (nodes.empty()) return;
    double minX =  1e18, minY =  1e18;
    double maxX = -1e18, maxY = -1e18;
    for (const auto& kv : nodes) {
        const auto* tmpl = GetTemplate(kv.second.templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(kv.second, *tmpl);
        minX = std::min(minX, L.bounds.x);
        minY = std::min(minY, L.bounds.y);
        maxX = std::max(maxX, L.bounds.x + L.bounds.width);
        maxY = std::max(maxY, L.bounds.y + L.bounds.height);
    }
    if (minX > maxX || minY > maxY) return;
    double margin = 20.0;
    minX -= margin; minY -= margin; maxX += margin; maxY += margin;
    double worldW = maxX - minX, worldH = maxY - minY;
    double innerPad = 6.0;
    double availW = mr.width - 2 * innerPad, availH = mr.height - 2 * innerPad;
    double scale = std::min(availW / worldW, availH / worldH);
    if (scale <= 0) return;
    double drawnW = worldW * scale, drawnH = worldH * scale;
    double ox = mr.x + innerPad + (availW - drawnW) * 0.5;
    double oy = mr.y + innerPad + (availH - drawnH) * 0.5;
    double wx = minX + (screenPos.x - ox) / scale;
    double wy = minY + (screenPos.y - oy) / scale;
    CenterOn(wx, wy);
}

// =============================================================================
// CONTROLS OVERLAY
// =============================================================================

void UltraCanvasCompositorDiagram::SetControlsVisible(bool visible) {
    controlsConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetControlsPosition(NodeDiagramPanelPosition pos) {
    controlsConfig.position = pos;
    RequestRedraw();
}

void UltraCanvasCompositorDiagram::SetControlsConfig(const CompositorControlsConfig& cfg) {
    controlsConfig = cfg;
    RequestRedraw();
}

int UltraCanvasCompositorDiagram::FindControlButtonAt(const Point2Di& screenPos) {
    if (!controlsConfig.visible) return -1;
    int btnCount = 0;
    if (controlsConfig.showZoom) btnCount += 2;
    if (controlsConfig.showFit)  btnCount += 1;
    if (controlsConfig.showLock) btnCount += 1;
    if (btnCount == 0) return -1;
    double bs = controlsConfig.buttonSize;
    double gap = controlsConfig.gap;
    double totalH = btnCount * bs + (btnCount - 1) * gap;
    double panelW = bs + 2 * controlsConfig.padding;
    double panelH = totalH + 2 * controlsConfig.padding;
    double pad = controlsConfig.padding;
    double bw = static_cast<double>(GetWidth());
    double bh = static_cast<double>(GetHeight());
    double px = 0, py = 0;
    switch (controlsConfig.position) {
        case NodeDiagramPanelPosition::TopLeft:     px = pad;             py = pad;             break;
        case NodeDiagramPanelPosition::TopRight:    px = bw - panelW-pad; py = pad;             break;
        case NodeDiagramPanelPosition::BottomLeft:  px = pad;             py = bh - panelH-pad; break;
        case NodeDiagramPanelPosition::BottomRight: px = bw - panelW-pad; py = bh - panelH-pad; break;
    }
    double bx = px + controlsConfig.padding;
    double by = py + controlsConfig.padding;
    for (int i = 0; i < btnCount; ++i) {
        double btnY = by + i * (bs + gap);
        if (screenPos.x >= bx && screenPos.x <= bx + bs &&
            screenPos.y >= btnY && screenPos.y <= btnY + bs) {
            return i;
        }
    }
    return -1;
}

void UltraCanvasCompositorDiagram::RenderControls(IRenderContext* ctx) {
    int btnCount = 0;
    if (controlsConfig.showZoom) btnCount += 2;
    if (controlsConfig.showFit)  btnCount += 1;
    if (controlsConfig.showLock) btnCount += 1;
    if (btnCount == 0) return;

    double bs = controlsConfig.buttonSize;
    double gap = controlsConfig.gap;
    double totalH = btnCount * bs + (btnCount - 1) * gap;
    double panelW = bs + 2 * controlsConfig.padding;
    double panelH = totalH + 2 * controlsConfig.padding;
    double pad = controlsConfig.padding;
    double bw = static_cast<double>(GetWidth());
    double bh = static_cast<double>(GetHeight());
    double px = 0, py = 0;
    switch (controlsConfig.position) {
        case NodeDiagramPanelPosition::TopLeft:     px = pad;             py = pad;             break;
        case NodeDiagramPanelPosition::TopRight:    px = bw - panelW-pad; py = pad;             break;
        case NodeDiagramPanelPosition::BottomLeft:  px = pad;             py = bh - panelH-pad; break;
        case NodeDiagramPanelPosition::BottomRight: px = bw - panelW-pad; py = bh - panelH-pad; break;
    }

    ctx->SetFillPaint(controlsConfig.backgroundColor);
    ctx->FillRoundedRectangle(Rect2Df(px, py, panelW, panelH), 4.0);
    ctx->SetStrokePaint(controlsConfig.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(Rect2Df(px, py, panelW, panelH), 4.0);

    double bx = px + controlsConfig.padding;
    double by = py + controlsConfig.padding;
    int btnIndex = 0;

    auto drawBtnBg = [&](int idx) -> std::pair<double, double> {
        double btnY = by + idx * (bs + gap);
        if (hoveredControlButton == idx) {
            ctx->SetFillPaint(controlsConfig.hoverColor);
            ctx->FillRoundedRectangle(Rect2Df(bx, btnY, bs, bs), 3.0);
        }
        return { bx + bs * 0.5, btnY + bs * 0.5 };
    };

    if (controlsConfig.showZoom) {
        auto [cx1, cy1] = drawBtnBg(btnIndex++);
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetStrokeWidth(2.0);
        double a = bs * 0.25;
        ctx->DrawLine({cx1 - a, cy1}, {cx1 + a, cy1});
        ctx->DrawLine({cx1, cy1 - a}, {cx1, cy1 + a});
        auto [cx2, cy2] = drawBtnBg(btnIndex++);
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawLine({cx2 - a, cy2}, {cx2 + a, cy2});
    }
    if (controlsConfig.showFit) {
        auto [cx, cy] = drawBtnBg(btnIndex++);
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetStrokeWidth(1.8);
        double hs = bs * 0.28;
        double br = hs * 0.45;
        // TL/TR/BL/BR brackets
        ctx->DrawLine({cx - hs, cy - hs}, {cx - hs + br, cy - hs});
        ctx->DrawLine({cx - hs, cy - hs}, {cx - hs, cy - hs + br});
        ctx->DrawLine({cx + hs - br, cy - hs}, {cx + hs, cy - hs});
        ctx->DrawLine({cx + hs, cy - hs}, {cx + hs, cy - hs + br});
        ctx->DrawLine({cx - hs, cy + hs - br}, {cx - hs, cy + hs});
        ctx->DrawLine({cx - hs, cy + hs}, {cx - hs + br, cy + hs});
        ctx->DrawLine({cx + hs, cy + hs - br}, {cx + hs, cy + hs});
        ctx->DrawLine({cx + hs - br, cy + hs}, {cx + hs, cy + hs});
    }
    if (controlsConfig.showLock) {
        auto [cx, cy] = drawBtnBg(btnIndex++);
        ctx->SetFillPaint(controlsConfig.iconColor);
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetStrokeWidth(1.8);
        double bodyW = 9.0, bodyH = 7.0, bodyY = cy + 1.0;
        ctx->FillRectangle(Rect2Df(cx - bodyW * 0.5, bodyY, bodyW, bodyH));
        double shW = 7.0, shH = 5.5, shTop = bodyY - shH;
        if (isInteractive) {
            // Open shackle = unlocked = interactive
            ctx->DrawLine({cx - shW * 0.5, bodyY}, {cx - shW * 0.5, shTop});
            ctx->DrawLine({cx - shW * 0.5, shTop}, {cx + shW * 0.6, shTop});
            ctx->DrawLine({cx + shW * 0.6, shTop}, {cx + shW * 0.6, shTop + 2.5});
        } else {
            ctx->DrawLine({cx - shW * 0.5, bodyY}, {cx - shW * 0.5, shTop});
            ctx->DrawLine({cx - shW * 0.5, shTop}, {cx + shW * 0.5, shTop});
            ctx->DrawLine({cx + shW * 0.5, shTop}, {cx + shW * 0.5, bodyY});
        }
    }
}

// =============================================================================
// ALIGNMENT GUIDES
// =============================================================================

Point2Df UltraCanvasCompositorDiagram::ComputeAlignmentSnap(
        const std::string& leaderId, const Point2Df& proposedTopLeft) {
    currentAlignmentGuides.clear();
    if (!alignmentGuidesEnabled || leaderId.empty()) return proposedTopLeft;
    const auto* leader = GetNode(leaderId);
    if (!leader) return proposedTopLeft;
    const auto* leaderTmpl = GetTemplate(leader->templateId);
    if (!leaderTmpl) return proposedTopLeft;

    // Use the proposed top-left + the leader's computed size for candidate edges.
    double lw = (leader->width > 0.0) ? leader->width : leaderTmpl->defaultWidth;
    NodeLayout leaderL = ComputeNodeLayout(*leader, *leaderTmpl);
    double lh = leaderL.bounds.height;

    struct Candidate { double leaderPos; double snapTo; double otherStart; double otherEnd; };
    std::vector<Candidate> vCands;  // vertical lines (constant X)
    std::vector<Candidate> hCands;  // horizontal lines (constant Y)

    // Leader candidate Xs: left, center, right
    double lx0 = proposedTopLeft.x;
    double lx1 = proposedTopLeft.x + lw;
    double lxc = proposedTopLeft.x + lw * 0.5;
    double ly0 = proposedTopLeft.y;
    double ly1 = proposedTopLeft.y + lh;
    double lyc = proposedTopLeft.y + lh * 0.5;

    // World-space threshold (the user-set threshold is in pixels).
    double thr = alignmentGuideThreshold / std::max(0.01, zoomLevel);

    for (const auto& kv : nodes) {
        if (kv.first == leaderId) continue;
        if (dragStartPositions.count(kv.first)) continue;  // skip co-dragged
        const auto* tmpl = GetTemplate(kv.second.templateId);
        if (!tmpl) continue;
        NodeLayout L = ComputeNodeLayout(kv.second, *tmpl);
        double ox0 = L.bounds.x;
        double ox1 = L.bounds.x + L.bounds.width;
        double oxc = L.bounds.x + L.bounds.width * 0.5;
        double oy0 = L.bounds.y;
        double oy1 = L.bounds.y + L.bounds.height;
        double oyc = L.bounds.y + L.bounds.height * 0.5;

        auto tryV = [&](double lp, double op) {
            if (std::abs(lp - op) <= thr) {
                vCands.push_back({lp, op, std::min(ly0, oy0), std::max(ly1, oy1)});
            }
        };
        auto tryH = [&](double lp, double op) {
            if (std::abs(lp - op) <= thr) {
                hCands.push_back({lp, op, std::min(lx0, ox0), std::max(lx1, ox1)});
            }
        };
        // Vertical alignments (X coordinates)
        tryV(lx0, ox0); tryV(lx0, oxc); tryV(lx0, ox1);
        tryV(lxc, ox0); tryV(lxc, oxc); tryV(lxc, ox1);
        tryV(lx1, ox0); tryV(lx1, oxc); tryV(lx1, ox1);
        // Horizontal alignments (Y coordinates)
        tryH(ly0, oy0); tryH(ly0, oyc); tryH(ly0, oy1);
        tryH(lyc, oy0); tryH(lyc, oyc); tryH(lyc, oy1);
        tryH(ly1, oy0); tryH(ly1, oyc); tryH(ly1, oy1);
    }

    // Pick closest in each axis and snap.
    Point2Df out = proposedTopLeft;
    if (!vCands.empty()) {
        auto best = std::min_element(vCands.begin(), vCands.end(),
            [](const Candidate& a, const Candidate& b) {
                return std::abs(a.leaderPos - a.snapTo) < std::abs(b.leaderPos - b.snapTo);
            });
        out.x = proposedTopLeft.x + (best->snapTo - best->leaderPos);
        currentAlignmentGuides.push_back({true, best->snapTo, best->otherStart, best->otherEnd});
    }
    if (!hCands.empty()) {
        auto best = std::min_element(hCands.begin(), hCands.end(),
            [](const Candidate& a, const Candidate& b) {
                return std::abs(a.leaderPos - a.snapTo) < std::abs(b.leaderPos - b.snapTo);
            });
        out.y = proposedTopLeft.y + (best->snapTo - best->leaderPos);
        currentAlignmentGuides.push_back({false, best->snapTo, best->otherStart, best->otherEnd});
    }
    return out;
}

void UltraCanvasCompositorDiagram::RenderAlignmentGuides(IRenderContext* ctx) {
    if (currentAlignmentGuides.empty()) return;
    ctx->SetStrokePaint(Color(255, 80, 200, 200));
    ctx->SetStrokeWidth(1.0 / std::max(0.01, zoomLevel));
    for (const auto& g : currentAlignmentGuides) {
        if (g.vertical) {
            ctx->DrawLine(Point2Df(g.position, g.start), Point2Df(g.position, g.end));
        } else {
            ctx->DrawLine(Point2Df(g.start, g.position), Point2Df(g.end, g.position));
        }
    }
}

// =============================================================================
// SUBGRAPHS / NODE GROUPS
// =============================================================================

Point2Df UltraCanvasCompositorDiagram::GetNodeWorldPos(
        const std::string& nodeId) const {
    // Walk parent chain with a depth cap as defense against corrupted data.
    const int MaxDepth = 32;
    Point2Df out(0, 0);
    std::string cur = nodeId;
    for (int i = 0; i < MaxDepth && !cur.empty(); ++i) {
        auto it = nodes.find(cur);
        if (it == nodes.end()) break;
        out.x += it->second.x;
        out.y += it->second.y;
        cur = it->second.parentId;
    }
    return out;
}

std::string UltraCanvasCompositorDiagram::GetNodeParent(
        const std::string& nodeId) const {
    auto it = nodes.find(nodeId);
    return it == nodes.end() ? "" : it->second.parentId;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetChildrenOf(
        const std::string& nodeId) const {
    std::vector<std::string> out;
    // Walk nodeOrder so children come back in stable z-order.
    for (const auto& id : nodeOrder) {
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        if (it->second.parentId == nodeId) out.push_back(id);
    }
    return out;
}

std::vector<std::string> UltraCanvasCompositorDiagram::GetRootNodeIds() const {
    std::vector<std::string> out;
    for (const auto& id : nodeOrder) {
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        if (it->second.parentId.empty()) out.push_back(id);
    }
    return out;
}

void UltraCanvasCompositorDiagram::ApplySetNodeParent(
        const std::string& nodeId, const std::string& newParentId,
        double relX, double relY) {
    auto* n = GetNode(nodeId);
    if (!n) return;
    std::string oldParent = n->parentId;
    n->parentId = newParentId;
    n->x = relX;
    n->y = relY;
    if (onNodeParentChanged) onNodeParentChanged(nodeId, oldParent, newParentId);
    RequestRedraw();
}

bool UltraCanvasCompositorDiagram::SetNodeParent(
        const std::string& nodeId, const std::string& newParentId) {
    auto* n = GetNode(nodeId);
    if (!n) return false;
    if (n->parentId == newParentId) return true;

    // Cycle check: walk up from newParentId; if we hit nodeId, reject.
    if (!newParentId.empty()) {
        const int MaxDepth = 64;
        std::string cur = newParentId;
        for (int i = 0; i < MaxDepth && !cur.empty(); ++i) {
            if (cur == nodeId) return false;  // would create a cycle
            auto it = nodes.find(cur);
            if (it == nodes.end()) break;
            cur = it->second.parentId;
        }
    }

    // Preserve world position: newRel = currentWorld - newParentWorld.
    Point2Df curWorld = GetNodeWorldPos(nodeId);
    Point2Df newParentWorld(0, 0);
    if (!newParentId.empty()) newParentWorld = GetNodeWorldPos(newParentId);
    double newRelX = curWorld.x - newParentWorld.x;
    double newRelY = curWorld.y - newParentWorld.y;

    // Capture for undo.
    std::string oldParent = n->parentId;
    double oldRelX = n->x;
    double oldRelY = n->y;

    ApplySetNodeParent(nodeId, newParentId, newRelX, newRelY);

    if (historyRecording) {
        PushHistory({
            [this, nodeId, oldParent, oldRelX, oldRelY]() {
                ApplySetNodeParent(nodeId, oldParent, oldRelX, oldRelY);
            },
            [this, nodeId, newParentId, newRelX, newRelY]() {
                ApplySetNodeParent(nodeId, newParentId, newRelX, newRelY);
            },
            "Change parent"
        });
    }
    return true;
}

std::string UltraCanvasCompositorDiagram::Group(
        const std::string& groupId, const std::string& groupTemplateId,
        const std::vector<std::string>& nodeIds) {
    const auto* tmpl = GetTemplate(groupTemplateId);
    if (!tmpl || !tmpl->isGroup) return "";
    if (nodeIds.empty()) return "";

    // Compute bbox of the to-be-grouped nodes in world coords.
    double minX =  1e18, minY =  1e18;
    double maxX = -1e18, maxY = -1e18;
    for (const auto& nid : nodeIds) {
        const auto* n = GetNode(nid);
        if (!n) continue;
        const auto* nt = GetTemplate(n->templateId);
        if (!nt) continue;
        NodeLayout L = ComputeNodeLayout(*n, *nt);
        minX = std::min(minX, L.bounds.x);
        minY = std::min(minY, L.bounds.y);
        maxX = std::max(maxX, L.bounds.x + L.bounds.width);
        maxY = std::max(maxY, L.bounds.y + L.bounds.height);
    }
    if (minX > maxX || minY > maxY) return "";

    // Position the group's world top-left a bit above-left of the children
    // so its header sits above them; the auto-fit will expand the rest.
    double headerH = style.headerHeight;
    double pad = tmpl->groupPadding;
    double gx = minX - pad;
    double gy = minY - pad - headerH;

    std::string realId = groupId.empty() ? GenerateUniqueNodeId("group") : groupId;
    if (nodes.count(realId)) return "";  // collision

    // Do the whole operation as one composite history step.
    bool prevRec = historyRecording;
    historyRecording = false;
    if (!AddNode(realId, groupTemplateId, gx, gy)) {
        historyRecording = prevRec;
        return "";
    }
    std::vector<std::tuple<std::string, std::string, double, double>> oldStates;
    for (const auto& nid : nodeIds) {
        auto* n = GetNode(nid);
        if (!n) continue;
        if (nid == realId) continue;
        oldStates.push_back({nid, n->parentId, n->x, n->y});
        SetNodeParent(nid, realId);
    }
    historyRecording = prevRec;

    if (historyRecording) {
        std::string capturedGroupId = realId;
        std::string capturedTmplId  = groupTemplateId;
        double capturedGx = gx;
        double capturedGy = gy;
        PushHistory({
            // undo: ungroup (restore each child's old parent + rel position),
            // then remove the group node.
            [this, capturedGroupId, oldStates]() {
                for (const auto& s : oldStates) {
                    ApplySetNodeParent(std::get<0>(s), std::get<1>(s),
                                        std::get<2>(s), std::get<3>(s));
                }
                ApplyRemoveNode(capturedGroupId);
            },
            // redo: re-add group, re-set each child's parent (recomputing
            // relative pos).
            [this, capturedGroupId, capturedTmplId, capturedGx, capturedGy, nodeIds]() {
                CompositorNode g(capturedGroupId, capturedTmplId, capturedGx, capturedGy);
                ApplyAddNode(g);
                bool prevRec2 = historyRecording;
                historyRecording = false;
                for (const auto& nid : nodeIds) SetNodeParent(nid, capturedGroupId);
                historyRecording = prevRec2;
            },
            "Group nodes"
        });
    }
    return realId;
}

void UltraCanvasCompositorDiagram::Ungroup(const std::string& groupId) {
    auto* group = GetNode(groupId);
    if (!group) return;
    auto children = GetChildrenOf(groupId);
    if (children.empty()) {
        // Just remove the empty group.
        RemoveNode(groupId);
        return;
    }
    std::vector<std::tuple<std::string, std::string, double, double>> oldStates;
    for (const auto& cid : children) {
        const auto* c = GetNode(cid);
        if (!c) continue;
        oldStates.push_back({cid, c->parentId, c->x, c->y});
    }
    std::string capturedTmplId = group->templateId;
    double capturedGx = group->x;  // group's relative pos (its parent's rel)
    double capturedGy = group->y;
    std::string groupParent = group->parentId;

    bool prevRec = historyRecording;
    historyRecording = false;
    // Promote children to be siblings of the group (same parent as the group).
    for (const auto& cid : children) SetNodeParent(cid, groupParent);
    RemoveNode(groupId);
    historyRecording = prevRec;

    if (historyRecording) {
        PushHistory({
            // undo: re-add group with same id/template/pos/parent, restore children
            [this, groupId, capturedTmplId, capturedGx, capturedGy,
             groupParent, oldStates]() {
                CompositorNode g(groupId, capturedTmplId, capturedGx, capturedGy);
                g.parentId = groupParent;
                ApplyAddNode(g);
                for (const auto& s : oldStates) {
                    ApplySetNodeParent(std::get<0>(s), std::get<1>(s),
                                        std::get<2>(s), std::get<3>(s));
                }
            },
            // redo: promote children to siblings, remove group
            [this, groupId, children, groupParent]() {
                bool prevRec2 = historyRecording;
                historyRecording = false;
                for (const auto& cid : children) SetNodeParent(cid, groupParent);
                historyRecording = prevRec2;
                ApplyRemoveNode(groupId);
            },
            "Ungroup"
        });
    }
}

void UltraCanvasCompositorDiagram::RegisterDefaultGroupTemplate(
        const std::string& templateId, const std::string& title) {
    CompositorNodeTemplate t;
    t.id = templateId;
    t.category = "Layout";
    t.title = title;
    t.iconName = "group";
    t.headerColor       = Color(80, 80, 80, 255);
    t.headerTextColor   = Color(255, 255, 255, 255);
    t.bodyColor         = Color(60, 60, 60, 120);  // translucent so children show
    t.borderColor       = Color(140, 140, 140, 255);
    t.borderWidth       = 1.5;
    t.cornerRadius      = 6.0;
    t.isGroup = true;
    t.groupPadding = 16.0;
    t.groupMinWidth  = 200.0;
    t.groupMinHeight = 120.0;
    RegisterTemplate(t);
}

std::vector<std::string> UltraCanvasCompositorDiagram::BuildRenderOrder() const {
    std::vector<std::string> out;
    out.reserve(nodes.size());
    // DFS from each root in nodeOrder, emitting parent then children.
    std::function<void(const std::string&)> emit =
        [&](const std::string& nid) {
            out.push_back(nid);
            // Emit children in their own z-order.
            for (const auto& other : nodeOrder) {
                auto it = nodes.find(other);
                if (it == nodes.end()) continue;
                if (it->second.parentId == nid) emit(other);
            }
        };
    for (const auto& id : nodeOrder) {
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        if (it->second.parentId.empty()) emit(id);
    }
    // Orphan children (parent missing) become roots so they don't disappear.
    if (out.size() < nodes.size()) {
        std::set<std::string> seen(out.begin(), out.end());
        for (const auto& id : nodeOrder) {
            if (!seen.count(id) && nodes.count(id)) emit(id);
        }
    }
    return out;
}

// =============================================================================
// NODE PALETTE HELPER
// =============================================================================

std::vector<std::string> UltraCanvasCompositorDiagram::SearchTemplates(
        const std::string& query) const {
    std::vector<std::string> result;
    if (query.empty()) {
        for (const auto& kv : templates) result.push_back(kv.first);
        std::sort(result.begin(), result.end());
        return result;
    }
    std::string q;
    q.reserve(query.size());
    for (char c : query) q.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    struct Hit { std::string id; int score; };
    std::vector<Hit> hits;
    for (const auto& kv : templates) {
        const auto& t = kv.second;
        auto lower = [](std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        std::string tid    = lower(t.id);
        std::string ttitle = lower(t.title);
        std::string tcat   = lower(t.category);
        int score = -1;
        if (tid.rfind(q, 0) == 0)         score = 1000;  // id starts with query
        else if (ttitle.rfind(q, 0) == 0) score = 900;   // title starts with query
        else if (ttitle.find(q) != std::string::npos) score = 500;
        else if (tcat.find(q)   != std::string::npos) score = 300;
        else if (tid.find(q)    != std::string::npos) score = 200;
        if (score > 0) hits.push_back({kv.first, score});
    }
    std::sort(hits.begin(), hits.end(),
        [](const Hit& a, const Hit& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.id < b.id;
        });
    result.reserve(hits.size());
    for (const auto& h : hits) result.push_back(h.id);
    return result;
}

} // namespace UltraCanvas
