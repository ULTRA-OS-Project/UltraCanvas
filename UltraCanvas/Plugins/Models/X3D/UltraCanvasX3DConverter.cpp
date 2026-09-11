// Plugins/Models/X3D/UltraCanvasX3DConverter.cpp
// The X3D reader.
//
// Four things about X3D shape this code:
//
//   * DEF/USE is the whole instancing mechanism, and it applies to *every*
//     node, not just geometry: a Coordinate can be shared between two
//     IndexedFaceSets, an Appearance between two Shapes, and a whole Transform
//     subtree can be reused. A pre-pass collects every DEF in the file, and
//     Resolve() turns a USE back into the element it names, so the rest of the
//     reader never has to know which of the two it is looking at.
//   * <Transform> is not a TRS triple. The spec composes it as
//     T * C * R * SR * S * -SR * -C, and a file that sets `center` or
//     `scaleOrientation` means something the three-slot form cannot say, so
//     the matrix is built and then decomposed rather than copied field by
//     field.
//   * The index streams of an <IndexedFaceSet> are parallel, -1-separated and
//     independent. texCoordIndex, normalIndex and colorIndex may each be
//     present or absent, and normals and colours may be per-face rather than
//     per-vertex. A corner is the tuple of whichever streams exist, and unique
//     tuples become document vertices - the same resolution the OBJ and
//     COLLADA readers do, for the same reason.
//   * Animation is not stored on the node. A TimeSensor emits fractions, a
//     ROUTE carries them to an interpolator, and a second ROUTE carries the
//     interpolator's output to one field of one Transform. Following those two
//     hops is what turns a pile of unrelated nodes into channels.
//
// XML is parsed with tinyxml2, as the COLLADA, SVG, DOCX and XLSX code
// already does.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/X3D/UltraCanvasX3DConverter.h"

#include "tinyxml2.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ===== FIELD PARSING =====
//
// X3D field values are attribute text. Multi-valued fields (MFInt32, MFVec3f,
// MFColor) separate their items with whitespace, commas, or both - the commas
// are decoration the spec permits and every exporter uses differently - so
// every numeric parser here treats a comma as whitespace and reads a flat run
// of numbers. The field's own arity then says how to group them.

const char* Attribute(const XMLElement* element, const char* name) {
    const char* value = element ? element->Attribute(name) : nullptr;
    return value ? value : "";
}

bool HasAttribute(const XMLElement* element, const char* name) {
    return element && element->Attribute(name) != nullptr;
}

std::vector<double> ParseNumbers(const char* text) {
    std::vector<double> values;
    if (!text) return values;
    std::string cleaned(text);
    for (char& c : cleaned)
        if (c == ',') c = ' ';
    std::istringstream stream(cleaned);
    double value = 0.0;
    while (stream >> value) values.push_back(value);
    return values;
}

std::vector<double> ParseNumbers(const XMLElement* element, const char* name) {
    return ParseNumbers(element ? element->Attribute(name) : nullptr);
}

// MFInt32. Read as long long first: an index stream is the one field where a
// value out of int range means a corrupt file rather than a big number, and
// clamping it quietly would index memory that is not there.
std::vector<int> ParseIndices(const XMLElement* element, const char* name) {
    std::vector<int> values;
    const char* text = element ? element->Attribute(name) : nullptr;
    if (!text) return values;
    std::string cleaned(text);
    for (char& c : cleaned)
        if (c == ',') c = ' ';
    std::istringstream stream(cleaned);
    long long value = 0;
    while (stream >> value) {
        if (value < -1 || value > 0x7fffffffLL) value = -1;
        values.push_back(static_cast<int>(value));
    }
    return values;
}

bool ParseBool(const XMLElement* element, const char* name, bool fallback) {
    const char* text = element ? element->Attribute(name) : nullptr;
    if (!text) return fallback;
    std::string value(text);
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return fallback;
}

double ParseScalar(const XMLElement* element, const char* name, double fallback) {
    const std::vector<double> values = ParseNumbers(element, name);
    return values.empty() ? fallback : values[0];
}

Vec3d ParseVec3(const XMLElement* element, const char* name, const Vec3d& fallback) {
    const std::vector<double> values = ParseNumbers(element, name);
    if (values.size() < 3) return fallback;
    return Vec3d(values[0], values[1], values[2]);
}

Vec3f ParseColor(const XMLElement* element, const char* name, const Vec3f& fallback) {
    const std::vector<double> values = ParseNumbers(element, name);
    if (values.size() < 3) return fallback;
    return Vec3f(static_cast<float>(values[0]), static_cast<float>(values[1]),
                 static_cast<float>(values[2]));
}

// SFRotation: an axis and an angle in radians. A zero-length axis or a zero
// angle is the identity, which files write constantly ("0 0 0 0").
Quatd ParseRotation(const XMLElement* element, const char* name) {
    const std::vector<double> values = ParseNumbers(element, name);
    if (values.size() < 4) return Quatd::Identity();
    const Vec3d axis(values[0], values[1], values[2]);
    if (axis.Length() < 1e-12 || std::fabs(values[3]) < 1e-12) return Quatd::Identity();
    return Quatd::FromAxisAngle(axis, values[3]);
}

// MFString: a run of quoted strings, single or double quoted, with the other
// kind of quote usable inside. Anything unquoted is taken as one whole string,
// which is what an SFString attribute looks like.
std::vector<std::string> ParseStrings(const XMLElement* element, const char* name) {
    std::vector<std::string> items;
    const char* text = element ? element->Attribute(name) : nullptr;
    if (!text) return items;

    const std::string value(text);
    size_t cursor = 0;
    bool sawQuote = false;
    while (cursor < value.size()) {
        while (cursor < value.size() &&
               std::isspace(static_cast<unsigned char>(value[cursor]))) ++cursor;
        if (cursor >= value.size()) break;
        if (value[cursor] == '"' || value[cursor] == '\'') {
            sawQuote = true;
            const char quote = value[cursor++];
            std::string item;
            while (cursor < value.size() && value[cursor] != quote) {
                // X3D escapes an embedded quote with a backslash.
                if (value[cursor] == '\\' && cursor + 1 < value.size()) ++cursor;
                item.push_back(value[cursor++]);
            }
            if (cursor < value.size()) ++cursor;
            items.push_back(std::move(item));
        } else {
            break;
        }
    }
    if (!sawQuote && !value.empty()) items.push_back(value);
    return items;
}

// The rotation that carries `from` onto `to`. Used to point a light or a
// viewpoint: the document states direction as node orientation, X3D states it
// as a vector on the light itself.
Quatd RotationBetween(const Vec3d& from, const Vec3d& to) {
    const double fromLength = from.Length();
    const double toLength = to.Length();
    if (fromLength < 1e-12 || toLength < 1e-12) return Quatd::Identity();
    const Vec3d a(from.x / fromLength, from.y / fromLength, from.z / fromLength);
    const Vec3d b(to.x / toLength, to.y / toLength, to.z / toLength);

    const double dot = a.Dot(b);
    if (dot > 1.0 - 1e-12) return Quatd::Identity();
    if (dot < -1.0 + 1e-12) {
        // Opposite vectors: any perpendicular axis turns one into the other.
        const Vec3d seed = std::fabs(a.x) < 0.9 ? Vec3d(1.0, 0.0, 0.0) : Vec3d(0.0, 1.0, 0.0);
        return Quatd::FromAxisAngle(a.Cross(seed), kPi);
    }
    return Quatd::FromAxisAngle(a.Cross(b), std::acos(dot));
}

std::string MimeTypeForUri(const std::string& uri) {
    const size_t dot = uri.rfind('.');
    if (dot == std::string::npos) return {};
    std::string extension = uri.substr(dot + 1);
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "bmp") return "image/bmp";
    if (extension == "tif" || extension == "tiff") return "image/tiff";
    if (extension == "webp") return "image/webp";
    return {};
}

// The vertex streams every geometry node hangs off itself, resolved out of its
// child nodes. Which of them are filled says what the geometry carries.
struct GeometryStreams {
    std::vector<Vec3d> Points;
    std::vector<float> Normals;      // 3 per entry
    std::vector<float> TexCoords;    // TexComponents per entry
    std::vector<float> Colors;       // ColorComponents per entry
    int TexComponents = 2;
    int ColorComponents = 3;

    size_t NormalCount() const { return Normals.size() / 3; }
    size_t TexCoordCount() const {
        return TexComponents > 0 ? TexCoords.size() / static_cast<size_t>(TexComponents) : 0;
    }
    size_t ColorCount() const {
        return ColorComponents > 0 ? Colors.size() / static_cast<size_t>(ColorComponents) : 0;
    }
};

// ===== READER =====

class Reader {
public:
    explicit Reader(const ConversionOptions& options) : options_(options) {}

    std::shared_ptr<ModelDocument> Run(XMLDocument& xml) {
        const XMLElement* root = xml.RootElement();
        if (!root) {
            options_.Warn("X3D: the file has no root element");
            return nullptr;
        }
        if (std::strcmp(root->Name(), "X3D") != 0) {
            options_.Warn(std::string("X3D: the root element is <") + root->Name() +
                          ">, not <X3D>");
            return nullptr;
        }

        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "x3d";
        // X3D is right-handed and Y-up by definition; there is no field that
        // could say otherwise, which is why UpAxis is reported false in the
        // capabilities even though the value below is certain.
        document_->Up = UpAxis::YUp;
        document_->Chirality = Handedness::RightHanded;
        if (HasAttribute(root, "version"))
            document_->Metadata["x3d.version"] = Attribute(root, "version");
        if (HasAttribute(root, "profile"))
            document_->Metadata["x3d.profile"] = Attribute(root, "profile");

        ReadHead(root->FirstChildElement("head"));

        const XMLElement* scene = root->FirstChildElement("Scene");
        if (!scene) {
            options_.Warn("X3D: no <Scene>; there is nothing to read");
            return nullptr;
        }

        // Every DEF in the file, before anything is read. The spec requires a
        // DEF to precede its USE, but collecting up front costs one pass and
        // makes a file that breaks that rule read correctly anyway.
        CollectDefinitions(scene);

        for (const XMLElement* child = scene->FirstChildElement(); child;
             child = child->NextSiblingElement())
            ReadChild(child, -1);

        ReadRoutes(scene);

        if (document_->Meshes.empty() && document_->Nodes.empty()) {
            options_.Warn("X3D: no geometry found");
            return nullptr;
        }
        Finish();
        return document_;
    }

private:
    // ===== DEF / USE =====

    void CollectDefinitions(const XMLElement* element) {
        for (const XMLElement* child = element->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const char* def = child->Attribute("DEF");
            // First DEF of a name wins, which is what a browser does: a later
            // duplicate is a file error, not a redefinition.
            if (def && *def && definitions_.find(def) == definitions_.end())
                definitions_[def] = child;
            CollectDefinitions(child);
        }
    }

    // A USE node stands for the element its name was DEF'd on. Resolved()
    // holds the element to actually read - null when the name resolves to
    // nothing or would expand into itself - and unwinds the cycle guard when
    // it goes out of scope, so the many early returns below cannot leak it.
    class Resolved {
    public:
        Resolved(Reader& reader, const XMLElement* element) : reader_(reader) {
            if (!element) return;
            const char* use = element->Attribute("USE");
            if (!use || !*use) { target_ = element; return; }

            auto found = reader.definitions_.find(use);
            if (found == reader.definitions_.end()) {
                reader.options_.Warn(std::string("X3D: USE=\"") + use +
                                     "\" names nothing DEF'd in this file");
                return;
            }
            // A USE inside the subtree it names would expand forever. The spec
            // forbids it; a generated file can still contain it.
            if (!reader.expanding_.insert(found->second).second) {
                reader.options_.Warn(std::string("X3D: USE=\"") + use +
                                     "\" refers to a node it sits inside; "
                                     "the reference is dropped");
                return;
            }
            target_ = found->second;
            guarded_ = found->second;
        }
        ~Resolved() { if (guarded_) reader_.expanding_.erase(guarded_); }

        Resolved(const Resolved&) = delete;
        Resolved& operator=(const Resolved&) = delete;

        const XMLElement* Get() const { return target_; }
        explicit operator bool() const { return target_ != nullptr; }
        const XMLElement* operator->() const { return target_; }

    private:
        Reader& reader_;
        const XMLElement* target_ = nullptr;
        const XMLElement* guarded_ = nullptr;
    };
    friend class Resolved;

    static std::string DefName(const XMLElement* element) {
        const char* def = element ? element->Attribute("DEF") : nullptr;
        if (def && *def) return def;
        const char* use = element ? element->Attribute("USE") : nullptr;
        return use ? use : std::string();
    }

    // ===== HEAD =====

    void ReadHead(const XMLElement* head) {
        if (!head) return;

        for (const XMLElement* meta = head->FirstChildElement("meta"); meta;
             meta = meta->NextSiblingElement("meta")) {
            const std::string name = Attribute(meta, "name");
            const std::string content = Attribute(meta, "content");
            if (name.empty() || content.empty()) continue;

            if (name == "generator") document_->Generator = content;
            else if (name == "title") document_->Title = content;
            else if (name == "creator" || name == "author") document_->Author = content;
            else if (name == "rights" || name == "copyright") document_->Copyright = content;
            else document_->Metadata["x3d.meta." + name] = content;
        }

        // <unit> arrived in X3D 3.3 and is the only place the format states a
        // physical size. conversionFactor is metres per stored unit, exactly
        // what UnitScaleToMeters means. Without it X3D is metres by
        // definition, so that is what a file that says nothing gets.
        bool statedUnit = false;
        for (const XMLElement* unit = head->FirstChildElement("unit"); unit;
             unit = unit->NextSiblingElement("unit")) {
            if (std::string(Attribute(unit, "category")) != "length") continue;
            const double factor = ParseScalar(unit, "conversionFactor", 0.0);
            if (factor <= 0.0) continue;
            document_->UnitScaleToMeters = factor;
            document_->SourceUnit = UnitFromMetres(factor);
            statedUnit = true;
        }
        if (!statedUnit) {
            document_->SourceUnit = ModelUnit::Meter;
            document_->UnitScaleToMeters = 1.0;
        }
    }

    // Match the declared metres-per-unit to a named unit, so a consumer can
    // print "mm" rather than "0.001 m". Anything unrecognised keeps the scale
    // and stays Unspecified rather than being forced into the nearest name.
    static ModelUnit UnitFromMetres(double metres) {
        struct Candidate { ModelUnit Unit; double Metres; };
        static const Candidate candidates[] = {
                {ModelUnit::Micrometer, 1e-6}, {ModelUnit::Millimeter, 1e-3},
                {ModelUnit::Centimeter, 1e-2}, {ModelUnit::Decimeter, 1e-1},
                {ModelUnit::Meter, 1.0},       {ModelUnit::Kilometer, 1000.0},
                {ModelUnit::Inch, 0.0254},     {ModelUnit::Foot, 0.3048},
                {ModelUnit::Yard, 0.9144},     {ModelUnit::Mile, 1609.344},
                {ModelUnit::Mil, 0.0000254}};
        for (const Candidate& candidate : candidates)
            if (std::fabs(metres - candidate.Metres) <= candidate.Metres * 1e-6)
                return candidate.Unit;
        return ModelUnit::Unspecified;
    }

    // ===== SCENE WALK =====

    void ReadChild(const XMLElement* element, int parent) {
        Resolved resolved(*this, element);
        if (!resolved) return;
        const XMLElement* node = resolved.Get();
        const std::string kind = node->Name();

        if (kind == "Transform") {
            ReadTransform(node, parent);
        } else if (kind == "Group" || kind == "StaticGroup" || kind == "Anchor" ||
                   kind == "Collision" || kind == "Billboard" || kind == "CADAssembly" ||
                   kind == "CADPart" || kind == "CADLayer" || kind == "CADFace") {
            // Grouping nodes with no transform of their own. They still become
            // document nodes: the names are how a file labels its assembly
            // structure, and dropping them would flatten it away.
            const int index = MakeNode(node, parent);
            ReadGroupChildren(node, index);
        } else if (kind == "Switch") {
            ReadSwitch(node, parent);
        } else if (kind == "LOD") {
            ReadLOD(node, parent);
        } else if (kind == "Shape") {
            ReadShape(node, parent);
        } else if (kind == "DirectionalLight" || kind == "PointLight" || kind == "SpotLight") {
            ReadLight(node, kind, parent);
        } else if (kind == "Viewpoint" || kind == "OrthoViewpoint") {
            ReadViewpoint(node, kind, parent);
        } else if (kind == "Background" || kind == "TextureBackground") {
            ReadBackground(node);
        } else if (kind == "WorldInfo") {
            ReadWorldInfo(node);
        } else if (kind == "Inline") {
            const std::vector<std::string> urls = ParseStrings(node, "url");
            options_.Warn(std::string("X3D: <Inline> is not followed; the scene at '") +
                          (urls.empty() ? std::string("(no url)") : urls.front()) +
                          "' is not read");
        } else if (kind == "ProtoInstance") {
            WarnOnce("proto", std::string("X3D: <ProtoInstance ") + Attribute(node, "name") +
                             "> is not expanded; prototypes are not read");
        } else if (IsIgnorableNode(kind)) {
            // Sensors, interpolators, ROUTEs, prototype declarations, script
            // and metadata nodes. Nothing to place - the interpolators and
            // ROUTEs are picked up by ReadRoutes() instead.
        } else {
            WarnOnce("node." + kind, "X3D: <" + kind + "> is not read");
        }
    }

    // Nodes that carry no geometry and are either handled elsewhere or carry
    // nothing the document holds. Listed rather than defaulted so that a node
    // this reader has genuinely never heard of still produces a warning.
    static bool IsIgnorableNode(const std::string& kind) {
        static const std::set<std::string> ignorable = {
                "ROUTE", "NavigationInfo", "Fog", "LocalFog", "ProtoDeclare",
                "ExternProtoDeclare", "IMPORT", "EXPORT", "Script", "TimeSensor",
                "PositionInterpolator", "PositionInterpolator2D", "OrientationInterpolator",
                "ScalarInterpolator", "ColorInterpolator", "CoordinateInterpolator",
                "NormalInterpolator", "TouchSensor", "PlaneSensor", "SphereSensor",
                "CylinderSensor", "ProximitySensor", "VisibilitySensor", "KeySensor",
                "StringSensor", "LoadSensor", "MetadataString", "MetadataDouble",
                "MetadataFloat", "MetadataInteger", "MetadataSet", "MetadataBoolean",
                "IS", "connect", "field", "fieldValue", "component", "unit", "meta"};
        return ignorable.count(kind) != 0;
    }

    int MakeNode(const XMLElement* element, int parent) {
        ModelNode node;
        node.Name = DefName(element);
        if (node.Name.empty()) node.Name = element->Name();
        return AddNamedNode(std::move(node), element, parent);
    }

    void ReadGroupChildren(const XMLElement* element, int parent) {
        for (const XMLElement* child = element->FirstChildElement(); child;
             child = child->NextSiblingElement())
            ReadChild(child, parent);
    }

    // The spec composes a Transform as
    //     T * C * R * SR * S * SR^-1 * C^-1
    // and a file that sets `center` or `scaleOrientation` means something the
    // three-slot TRS form cannot say. So the matrix is built and then
    // decomposed: the common case (both unset) decomposes back to exactly the
    // fields that were written, and the uncommon case keeps its meaning
    // instead of being silently discarded.
    void ReadTransform(const XMLElement* element, int parent) {
        const Vec3d translation = ParseVec3(element, "translation", Vec3d(0.0, 0.0, 0.0));
        const Vec3d scale = ParseVec3(element, "scale", Vec3d(1.0, 1.0, 1.0));
        const Vec3d center = ParseVec3(element, "center", Vec3d(0.0, 0.0, 0.0));
        const Quatd rotation = ParseRotation(element, "rotation");
        const Quatd scaleOrientation = ParseRotation(element, "scaleOrientation");

        ModelNode node;
        node.Name = DefName(element);
        if (node.Name.empty()) node.Name = "Transform";

        const bool centered = center.Length() > 1e-15;
        const bool orientedScale =
                std::fabs(scaleOrientation.w) < 1.0 - 1e-15;

        if (!centered && !orientedScale) {
            node.Translation = translation;
            node.Rotation = rotation;
            node.Scale = scale;
        } else {
            Matrix4x4 matrix = Matrix4x4::Translation(translation);
            if (centered) matrix = matrix * Matrix4x4::Translation(center);
            matrix = matrix * Matrix4x4::FromQuaternion(rotation);
            if (orientedScale) matrix = matrix * Matrix4x4::FromQuaternion(scaleOrientation);
            matrix = matrix * Matrix4x4::Scaling(scale);
            if (orientedScale) {
                const Quatd inverse(-scaleOrientation.x, -scaleOrientation.y,
                                    -scaleOrientation.z, scaleOrientation.w);
                matrix = matrix * Matrix4x4::FromQuaternion(inverse);
            }
            if (centered) matrix = matrix * Matrix4x4::Translation(center * -1.0);

            Vec3d outTranslation, outScale;
            Quatd outRotation;
            if (matrix.DecomposeTRS(outTranslation, outRotation, outScale)) {
                node.Translation = outTranslation;
                node.Rotation = outRotation;
                node.Scale = outScale;
            } else {
                // scaleOrientation with a non-uniform scale is exactly the
                // shear TRS cannot hold; the matrix keeps it.
                node.Matrix = matrix;
            }
        }

        const int index = AddNamedNode(std::move(node), element, parent);
        ReadGroupChildren(element, index);
    }

    // whichChoice indexes the *children*, and -1 (the default) draws none.
    void ReadSwitch(const XMLElement* element, int parent) {
        const int choice = static_cast<int>(ParseScalar(element, "whichChoice", -1.0));
        if (choice < 0) {
            WarnOnce("switch", "X3D: a <Switch> has whichChoice = -1; none of its children "
                               "are read, which is what a browser would draw");
            return;
        }
        int position = 0;
        for (const XMLElement* child = element->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            if (IsIgnorableNode(child->Name())) continue;
            if (position++ != choice) continue;
            ReadChild(child, parent);
            return;
        }
        options_.Warn("X3D: a <Switch> selects child " + std::to_string(choice) +
                      ", which it does not have");
    }

    // The highest-detail level is the first child. A document has no LOD
    // concept, so the alternatives are dropped rather than all drawn at once.
    void ReadLOD(const XMLElement* element, int parent) {
        for (const XMLElement* child = element->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            if (IsIgnorableNode(child->Name())) continue;
            WarnOnce("lod", "X3D: <LOD> is read at its first (highest-detail) level; "
                            "the lower ones are dropped");
            ReadChild(child, parent);
            return;
        }
    }

    void ReadWorldInfo(const XMLElement* element) {
        const char* title = element->Attribute("title");
        if (title && *title && document_->Title.empty()) document_->Title = title;
        const std::vector<std::string> info = ParseStrings(element, "info");
        for (size_t i = 0; i < info.size(); ++i)
            document_->Metadata["x3d.worldInfo." + std::to_string(i)] = info[i];
    }

    // A Background is not a light or a mesh, so it has nowhere to go in the
    // document - but it is the scene's stated backdrop, and a viewer that
    // wants to reproduce the file needs it. Kept as metadata rather than
    // dropped.
    void ReadBackground(const XMLElement* element) {
        for (const char* field : {"skyColor", "groundColor", "skyAngle", "groundAngle"})
            if (HasAttribute(element, field))
                document_->Metadata[std::string("x3d.background.") + field] =
                        Attribute(element, field);
        for (const char* field : {"frontUrl", "backUrl", "leftUrl", "rightUrl",
                                  "topUrl", "bottomUrl"})
            if (HasAttribute(element, field))
                document_->Metadata[std::string("x3d.background.") + field] =
                        Attribute(element, field);
    }

    // ===== APPEARANCE =====

    // Returns an index into ModelDocument::Materials, or -1 when the Shape had
    // no <Appearance> at all - which X3D defines as unlit white, and the
    // document spells as "no material".
    //
    // `doubleSided` comes from the geometry's `solid` field rather than from
    // the appearance, because that is where X3D puts it. It is part of the
    // cache key: the same Appearance used by a solid and a non-solid geometry
    // is two materials in the document, which is the only way to keep both.
    int ReadAppearance(const XMLElement* appearanceElement, bool doubleSided) {
        Resolved appearance(*this, appearanceElement);
        if (!appearance) return -1;

        const std::string key = DefName(appearance.Get()) + (doubleSided ? "|2" : "|1");
        if (!DefName(appearance.Get()).empty()) {
            auto cached = materialCache_.find(key);
            if (cached != materialCache_.end()) return cached->second;
        }

        ModelMaterial material;
        material.Name = DefName(appearance.Get());
        material.DoubleSided = doubleSided;

        PhongParams phong;
        float opacity = 1.0f;
        bool sawMaterial = false;

        for (const XMLElement* child = appearance->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            Resolved node(*this, child);
            if (!node) continue;
            const std::string kind = node->Name();

            if (kind == "Material" || kind == "TwoSidedMaterial") {
                sawMaterial = true;
                if (material.Name.empty()) material.Name = DefName(node.Get());
                ReadMaterialFields(node.Get(), phong, material, opacity);
                if (kind == "TwoSidedMaterial") {
                    material.DoubleSided = true;
                    if (ParseBool(node.Get(), "separateBackColor", false))
                        WarnOnce("twoSided",
                                 "X3D: <TwoSidedMaterial separateBackColor='true'> - the back "
                                 "colours are dropped; the document holds one material per face");
                }
            } else if (kind == "ImageTexture") {
                phong.DiffuseTexture = ReadImageTexture(node.Get());
            } else if (kind == "TextureTransform") {
                ReadTextureTransform(node.Get(), phong.DiffuseTexture);
            } else if (kind == "MultiTexture" || kind == "MultiTextureTransform") {
                // The first layer is the one a single-texture document can
                // hold; the rest are a blending stack it has no field for.
                WarnOnce("multiTexture",
                         "X3D: <" + kind + "> - only the first texture layer is read");
                for (const XMLElement* layer = node->FirstChildElement(); layer;
                     layer = layer->NextSiblingElement()) {
                    Resolved inner(*this, layer);
                    if (!inner) continue;
                    if (std::string(inner->Name()) == "ImageTexture") {
                        phong.DiffuseTexture = ReadImageTexture(inner.Get());
                        break;
                    }
                    if (std::string(inner->Name()) == "TextureTransform") {
                        ReadTextureTransform(inner.Get(), phong.DiffuseTexture);
                        break;
                    }
                }
            } else if (kind == "PixelTexture") {
                WarnOnce("pixelTexture", "X3D: <PixelTexture> holds its image as inline integers; "
                                         "it is not decoded");
            } else if (kind == "MovieTexture") {
                WarnOnce("movieTexture", "X3D: <MovieTexture> is not read; the document has no "
                                         "animated texture");
            } else if (kind == "ComposedCubeMapTexture" || kind == "GeneratedCubeMapTexture" ||
                       kind == "ImageCubeMapTexture") {
                WarnOnce("cubeMap", "X3D: <" + kind + "> is not read");
            } else if (kind == "LineProperties" || kind == "FillProperties") {
                // Line width, hatching and stipple - drawing style, not a
                // material the document can hold. Recorded, not dropped.
                for (const tinyxml2::XMLAttribute* attribute = node->FirstAttribute();
                     attribute; attribute = attribute->Next())
                    material.Extras["x3d." + kind + "." + attribute->Name()] = attribute->Value();
            } else if (kind == "ComposedShader" || kind == "PackagedShader" ||
                       kind == "ProgramShader") {
                WarnOnce("shader", "X3D: <" + kind + "> is not read; the material keeps its "
                                   "fixed-function fields only");
            } else if (!IsIgnorableNode(kind)) {
                WarnOnce("appearance." + kind, "X3D: <" + kind + "> inside <Appearance> "
                                               "is not read");
            }
        }

        if (!sawMaterial) {
            // An Appearance with only a texture is a common way to write an
            // unlit textured surface. X3D's own default Material is what a
            // browser applies, so that is what the document gets.
            phong.Diffuse = Vec3f(0.8f, 0.8f, 0.8f);
            phong.Ambient = Vec3f(0.16f, 0.16f, 0.16f);
            phong.Shininess = 0.2f * 128.0f;
        }

        material.Phong = phong;
        material.BaseColorFactor = Vec4f(phong.Diffuse.x, phong.Diffuse.y, phong.Diffuse.z,
                                         opacity);
        if (opacity < 1.0f) material.Alpha = AlphaMode::Blend;
        // Fills the PBR block from Phong, carrying the diffuse texture over as
        // the base colour texture and keeping the alpha set just above.
        material.DeriveMissingModel();

        const int index = document_->AddMaterial(std::move(material));
        if (!DefName(appearance.Get()).empty()) materialCache_[key] = index;
        return index;
    }

    void ReadMaterialFields(const XMLElement* element, PhongParams& phong,
                            ModelMaterial& material, float& opacity) {
        phong.Diffuse = ParseColor(element, "diffuseColor", Vec3f(0.8f, 0.8f, 0.8f));
        phong.Specular = ParseColor(element, "specularColor", Vec3f(0.0f, 0.0f, 0.0f));
        material.EmissiveFactor = ParseColor(element, "emissiveColor", Vec3f(0.0f, 0.0f, 0.0f));

        // X3D states ambient as an *intensity* multiplying the diffuse colour,
        // where MTL and COLLADA state an ambient colour outright. The product
        // is that colour.
        const float ambient = static_cast<float>(ParseScalar(element, "ambientIntensity", 0.2));
        phong.Ambient = Vec3f(phong.Diffuse.x * ambient, phong.Diffuse.y * ambient,
                              phong.Diffuse.z * ambient);

        // X3D shininess is 0..1 and is defined as the OpenGL specular exponent
        // divided by 128. PhongParams::Shininess is MTL's Ns, which is that
        // exponent, so the conversion is the multiply back.
        phong.Shininess = static_cast<float>(ParseScalar(element, "shininess", 0.2)) * 128.0f;

        const float transparency =
                static_cast<float>(ParseScalar(element, "transparency", 0.0));
        opacity = std::min(1.0f, std::max(0.0f, 1.0f - transparency));
    }

    // url is an MFString *fallback list*: the first entry that resolves is the
    // one a browser uses. Nothing here touches the filesystem, so the first is
    // taken and the alternates are kept in metadata rather than thrown away -
    // they are usually the only record of where the texture came from.
    TextureRef ReadImageTexture(const XMLElement* element) {
        TextureRef texture;
        const std::string name = DefName(element);

        if (!name.empty()) {
            auto cached = imageCache_.find(name);
            if (cached != imageCache_.end()) {
                texture.Image = cached->second;
                ApplyTextureWrap(element, texture);
                return texture;
            }
        }

        const std::vector<std::string> urls = ParseStrings(element, "url");
        if (urls.empty()) {
            options_.Warn("X3D: <ImageTexture> has no url");
            return texture;
        }

        ModelImage image;
        image.Name = name.empty() ? urls.front() : name;
        image.Uri = urls.front();
        image.MimeType = MimeTypeForUri(image.Uri);
        texture.Image = static_cast<int>(document_->Images.size());
        document_->Images.push_back(std::move(image));
        if (!name.empty()) imageCache_[name] = texture.Image;

        if (urls.size() > 1) {
            std::string alternates;
            for (size_t i = 1; i < urls.size(); ++i)
                alternates += (alternates.empty() ? "" : " | ") + urls[i];
            document_->Metadata["x3d.imageTexture." +
                                (name.empty() ? std::to_string(texture.Image) : name) +
                                ".alternateUrls"] = alternates;
        }

        ApplyTextureWrap(element, texture);
        return texture;
    }

    // repeatS / repeatT default true, which is the document's default sampler,
    // so a sampler is only created when the file actually clamps.
    void ApplyTextureWrap(const XMLElement* element, TextureRef& texture) {
        const bool repeatS = ParseBool(element, "repeatS", true);
        const bool repeatT = ParseBool(element, "repeatT", true);
        if (repeatS && repeatT) return;

        ModelSampler sampler;
        sampler.WrapU = repeatS ? TextureWrap::Repeat : TextureWrap::ClampToEdge;
        sampler.WrapV = repeatT ? TextureWrap::Repeat : TextureWrap::ClampToEdge;
        texture.Sampler = static_cast<int>(document_->Samplers.size());
        document_->Samplers.push_back(sampler);
    }

    void ReadTextureTransform(const XMLElement* element, TextureRef& texture) {
        const std::vector<double> translation = ParseNumbers(element, "translation");
        const std::vector<double> scale = ParseNumbers(element, "scale");
        const std::vector<double> center = ParseNumbers(element, "center");

        if (translation.size() >= 2) {
            texture.OffsetU = static_cast<float>(translation[0]);
            texture.OffsetV = static_cast<float>(translation[1]);
        }
        if (scale.size() >= 2) {
            texture.ScaleU = static_cast<float>(scale[0]);
            texture.ScaleV = static_cast<float>(scale[1]);
        }
        texture.RotationRadians = static_cast<float>(ParseScalar(element, "rotation", 0.0));

        // KHR_texture_transform, which TextureRef mirrors, rotates and scales
        // about the origin. X3D's `center` moves that pivot, and there is no
        // field for it.
        if (center.size() >= 2 &&
            (std::fabs(center[0]) > 1e-9 || std::fabs(center[1]) > 1e-9))
            WarnOnce("textureCenter",
                     "X3D: <TextureTransform center=...> - the pivot is dropped; scale and "
                     "rotation are applied about the texture origin");
    }

    // ===== SHAPE =====

    void ReadShape(const XMLElement* shape, int parent) {
        const XMLElement* appearanceElement = nullptr;
        const XMLElement* geometryElement = nullptr;
        for (const XMLElement* child = shape->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const std::string kind = child->Name();
            if (kind == "Appearance") appearanceElement = child;
            else if (!IsIgnorableNode(kind) && !geometryElement) geometryElement = child;
        }

        Resolved geometry(*this, geometryElement);
        if (!geometry) {
            if (geometryElement == nullptr)
                options_.Warn("X3D: a <Shape> carries no geometry node");
            return;
        }
        const std::string kind = geometry->Name();

        // `solid` lives on the geometry, but back-face culling is a material
        // property in the document. solid="false" means "draw both sides".
        const bool solid = ParseBool(geometry.Get(), "solid", true);
        const int material = ReadAppearance(appearanceElement, !solid);

        // One geometry DEF'd and USE'd twice under the same material is one
        // mesh with two nodes - real instancing, which is the whole point of
        // DEF/USE. Under a different material it has to be a second mesh,
        // because the material sits on the primitive.
        const std::string geometryName = DefName(geometry.Get());
        const std::string cacheKey = geometryName + "#" + std::to_string(material);
        int meshIndex = -1;
        if (!geometryName.empty()) {
            auto cached = meshCache_.find(cacheKey);
            if (cached != meshCache_.end()) meshIndex = cached->second;
        }

        // Geometry nodes are usually anonymous - a Blender export DEFs the
        // Coordinate but not the IndexedFaceSet - so the name falls back
        // through the Shape's DEF to the group that encloses it, which is
        // where an exporter puts the object's real name. "IndexedFaceSet"
        // is the last resort, not the first.
        std::string name = geometryName;
        if (name.empty()) name = DefName(shape);
        if (name.empty() && parent >= 0)
            name = document_->Nodes[static_cast<size_t>(parent)].Name;
        if (name.empty()) name = kind;

        double creaseAngle = -1.0;
        if (meshIndex < 0) {
            MeshPrimitive prim;
            if (!BuildGeometry(geometry.Get(), kind, prim)) return;
            prim.Material = material;
            prim.Name = name;

            ModelMesh mesh;
            mesh.Name = name;
            mesh.Primitives.push_back(std::move(prim));
            meshIndex = document_->AddMesh(std::move(mesh));
            if (!geometryName.empty()) meshCache_[cacheKey] = meshIndex;
        }
        if (HasAttribute(geometry.Get(), "creaseAngle"))
            creaseAngle = ParseScalar(geometry.Get(), "creaseAngle", 0.0);

        ModelNode node;
        node.Name = DefName(shape).empty() ? name : DefName(shape);
        node.Mesh = meshIndex;

        // creaseAngle is the file's own smoothing rule and the document has no
        // field for it, so it is recorded rather than lost. It is only acted
        // on when the file carried no normals at all - see Finish().
        if (creaseAngle >= 0.0) {
            node.Extras["x3d.creaseAngle"] = FormatNumber(creaseAngle);
            if (creaseAngle < kPi - 1e-6) sawSharpCrease_ = true;
        }
        AddNamedNode(std::move(node), shape, parent);
    }

    static std::string FormatNumber(double value) {
        std::ostringstream out;
        out.precision(9);
        out << value;
        return out.str();
    }

    // ===== GEOMETRY =====

    bool BuildGeometry(const XMLElement* geometry, const std::string& kind,
                       MeshPrimitive& prim) {
        if (kind == "IndexedFaceSet") return BuildIndexedFaceSet(geometry, prim);
        if (kind == "IndexedTriangleSet") return BuildIndexedSet(geometry, prim, 3);
        if (kind == "IndexedQuadSet") return BuildIndexedSet(geometry, prim, 4);
        if (kind == "TriangleSet") return BuildFlatSet(geometry, prim, 3);
        if (kind == "QuadSet") return BuildFlatSet(geometry, prim, 4);
        if (kind == "IndexedTriangleFanSet") return BuildStrips(geometry, prim, true, true);
        if (kind == "IndexedTriangleStripSet") return BuildStrips(geometry, prim, false, true);
        if (kind == "TriangleFanSet") return BuildStrips(geometry, prim, true, false);
        if (kind == "TriangleStripSet") return BuildStrips(geometry, prim, false, false);
        if (kind == "IndexedLineSet") return BuildIndexedLineSet(geometry, prim);
        if (kind == "LineSet") return BuildLineSet(geometry, prim);
        if (kind == "PointSet") return BuildPointSet(geometry, prim);
        if (kind == "Box") return BuildBox(geometry, prim);
        if (kind == "Sphere") return BuildSphere(geometry, prim);
        if (kind == "Cylinder") return BuildCylinder(geometry, prim);
        if (kind == "Cone") return BuildCone(geometry, prim);

        WarnOnce("geometry." + kind, "X3D: <" + kind + "> geometry is not read");
        return false;
    }

    // The coord / normal / texCoord / color children of any geometry node.
    // Matched by node type rather than by containerField: every exporter
    // writes the type, and containerField only disambiguates nodes that can
    // sit in more than one slot, which none of these can.
    GeometryStreams ReadStreams(const XMLElement* geometry) {
        GeometryStreams streams;
        for (const XMLElement* child = geometry->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            Resolved node(*this, child);
            if (!node) continue;
            const std::string kind = node->Name();

            if (kind == "Coordinate" || kind == "CoordinateDouble") {
                const std::vector<double> values = ParseNumbers(node.Get(), "point");
                streams.Points.reserve(values.size() / 3);
                for (size_t i = 0; i + 2 < values.size(); i += 3)
                    streams.Points.emplace_back(values[i], values[i + 1], values[i + 2]);
            } else if (kind == "Normal") {
                for (double value : ParseNumbers(node.Get(), "vector"))
                    streams.Normals.push_back(static_cast<float>(value));
            } else if (kind == "TextureCoordinate") {
                streams.TexComponents = 2;
                for (double value : ParseNumbers(node.Get(), "point"))
                    streams.TexCoords.push_back(static_cast<float>(value));
            } else if (kind == "TextureCoordinate3D") {
                streams.TexComponents = 3;
                for (double value : ParseNumbers(node.Get(), "point"))
                    streams.TexCoords.push_back(static_cast<float>(value));
            } else if (kind == "Color") {
                streams.ColorComponents = 3;
                for (double value : ParseNumbers(node.Get(), "color"))
                    streams.Colors.push_back(static_cast<float>(value));
            } else if (kind == "ColorRGBA") {
                streams.ColorComponents = 4;
                for (double value : ParseNumbers(node.Get(), "color"))
                    streams.Colors.push_back(static_cast<float>(value));
            } else if (kind == "MultiTextureCoordinate") {
                WarnOnce("multiTexCoord",
                         "X3D: <MultiTextureCoordinate> - only the first set is read");
                for (const XMLElement* inner = node->FirstChildElement(); inner;
                     inner = inner->NextSiblingElement()) {
                    Resolved first(*this, inner);
                    if (!first) continue;
                    streams.TexComponents = 2;
                    for (double value : ParseNumbers(first.Get(), "point"))
                        streams.TexCoords.push_back(static_cast<float>(value));
                    break;
                }
            } else if (kind == "TextureCoordinateGenerator") {
                WarnOnce("texCoordGen", "X3D: <TextureCoordinateGenerator> is not evaluated; "
                                        "the surface arrives without texture coordinates");
            } else if (kind == "FogCoordinate" || kind == "Normal2D") {
                // Nothing the document holds, and nothing a renderer misses.
            } else if (!IsIgnorableNode(kind)) {
                WarnOnce("stream." + kind, "X3D: <" + kind + "> inside a geometry node "
                                           "is not read");
            }
        }
        return streams;
    }

    static void AddTexCoordAttribute(MeshPrimitive& prim, std::vector<float> values,
                                     int components) {
        if (values.empty()) return;
        VertexAttribute uv;
        uv.Semantic = AttributeSemantic::TexCoord;
        uv.Name = "TEXCOORD_0";
        uv.Components = components;
        uv.Values = std::move(values);
        prim.Attributes.push_back(std::move(uv));
    }

    static void AddColorAttribute(MeshPrimitive& prim, std::vector<float> values,
                                  int components) {
        if (values.empty()) return;
        VertexAttribute color;
        color.Semantic = AttributeSemantic::Color;
        color.Name = "COLOR_0";
        color.Components = components;
        color.Values = std::move(values);
        prim.Attributes.push_back(std::move(color));
    }

    // <IndexedFaceSet> - the n-gon workhorse, and the only geometry node whose
    // index streams may disagree with each other.
    //
    // coordIndex is a flat run of vertex indices with -1 ending each face.
    // texCoordIndex, normalIndex and colorIndex, when present, are parallel to
    // it *by position*, -1s included - so corner k of the file is entry k of
    // every stream. The exceptions are normals and colours declared per-face,
    // where the stream has one entry per face instead and the face counter
    // indexes it.
    //
    // A corner is therefore the tuple of whichever streams exist, and unique
    // tuples become document vertices. That is the same resolution the OBJ and
    // COLLADA readers perform, for the same reason: a vertex with two texture
    // coordinates is two vertices everywhere downstream.
    bool BuildIndexedFaceSet(const XMLElement* geometry, MeshPrimitive& prim) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) {
            options_.Warn("X3D: <IndexedFaceSet> has no <Coordinate>");
            return false;
        }

        const std::vector<int> coordIndex = ParseIndices(geometry, "coordIndex");
        if (coordIndex.empty()) {
            options_.Warn("X3D: <IndexedFaceSet> has no coordIndex");
            return false;
        }
        const std::vector<int> texCoordIndex = ParseIndices(geometry, "texCoordIndex");
        const std::vector<int> normalIndex = ParseIndices(geometry, "normalIndex");
        const std::vector<int> colorIndex = ParseIndices(geometry, "colorIndex");

        const bool ccw = ParseBool(geometry, "ccw", true);
        const bool normalPerVertex = ParseBool(geometry, "normalPerVertex", true);
        const bool colorPerVertex = ParseBool(geometry, "colorPerVertex", true);

        const bool haveNormals = !streams.Normals.empty();
        const bool haveTexCoords = !streams.TexCoords.empty();
        const bool haveColors = !streams.Colors.empty();

        // (position, texCoord, normal, colour) -> document vertex.
        std::map<std::array<int, 4>, uint32_t> vertexCache;
        std::vector<float> texcoords, colors;
        std::vector<uint32_t> corners;
        size_t maxCorners = 0;
        size_t faceIndex = 0;
        bool warnedRange = false;

        auto pick = [](const std::vector<int>& stream, size_t at, int fallback) {
            if (stream.empty()) return fallback;
            return at < stream.size() ? stream[at] : -1;
        };

        for (size_t i = 0; i <= coordIndex.size(); ++i) {
            if (i < coordIndex.size() && coordIndex[i] >= 0) { corners.push_back(
                    static_cast<uint32_t>(i)); continue; }
            if (corners.empty()) continue;

            // A face is closed either by a -1 or by the end of the stream; the
            // trailing -1 is conventional but not required.
            if (corners.size() >= 3) {
                if (!ccw) std::reverse(corners.begin(), corners.end());

                const size_t faceStart = prim.Indices.size();
                bool ok = true;
                for (uint32_t position : corners) {
                    const int positionIndex = coordIndex[position];
                    if (positionIndex < 0 ||
                        static_cast<size_t>(positionIndex) >= streams.Points.size()) {
                        ok = false;
                        break;
                    }
                    std::array<int, 4> key{positionIndex, -1, -1, -1};
                    if (haveTexCoords)
                        key[1] = pick(texCoordIndex, position, positionIndex);
                    if (haveNormals)
                        key[2] = normalPerVertex ? pick(normalIndex, position, positionIndex)
                                                 : pick(normalIndex, faceIndex,
                                                        static_cast<int>(faceIndex));
                    if (haveColors)
                        key[3] = colorPerVertex ? pick(colorIndex, position, positionIndex)
                                                : pick(colorIndex, faceIndex,
                                                       static_cast<int>(faceIndex));

                    auto found = vertexCache.find(key);
                    if (found != vertexCache.end()) {
                        prim.Indices.push_back(found->second);
                        continue;
                    }
                    const uint32_t fresh = static_cast<uint32_t>(prim.Positions.size());
                    vertexCache.emplace(key, fresh);
                    prim.Indices.push_back(fresh);

                    prim.Positions.push_back(streams.Points[static_cast<size_t>(positionIndex)]);
                    if (haveNormals) AppendTriple(streams.Normals, key[2], prim.Normals);
                    if (haveTexCoords)
                        AppendComponents(streams.TexCoords, key[1], streams.TexComponents,
                                         texcoords);
                    if (haveColors)
                        AppendComponents(streams.Colors, key[3], streams.ColorComponents, colors);
                }

                if (!ok) {
                    // Roll the partial face back rather than leave a truncated
                    // one behind. The vertices it added stay - unused vertices
                    // are harmless, a half-face is not.
                    prim.Indices.resize(faceStart);
                    if (!warnedRange) {
                        options_.Warn("X3D: <IndexedFaceSet> coordIndex refers past the end of "
                                      "its <Coordinate>; those faces are dropped");
                        warnedRange = true;
                    }
                } else {
                    prim.FaceStarts.push_back(static_cast<uint32_t>(faceStart));
                    maxCorners = std::max(maxCorners, corners.size());
                }
            }
            ++faceIndex;
            corners.clear();
        }

        if (prim.Positions.empty() || prim.FaceStarts.empty()) {
            options_.Warn("X3D: <IndexedFaceSet> produced no faces");
            return false;
        }

        if (maxCorners <= 3) {
            prim.Mode = PrimitiveMode::Triangles;
            prim.FaceStarts.clear();
        } else {
            prim.Mode = PrimitiveMode::Polygons;
            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
        }

        AddTexCoordAttribute(prim, std::move(texcoords), streams.TexComponents);
        AddColorAttribute(prim, std::move(colors), streams.ColorComponents);
        return true;
    }

    static void AppendTriple(const std::vector<float>& source, int index,
                             std::vector<Vec3f>& out) {
        const size_t base = static_cast<size_t>(std::max(0, index)) * 3;
        if (index < 0 || base + 2 >= source.size()) { out.emplace_back(0.0f, 0.0f, 1.0f); return; }
        out.emplace_back(source[base], source[base + 1], source[base + 2]);
    }

    static void AppendComponents(const std::vector<float>& source, int index, int components,
                                 std::vector<float>& out) {
        const size_t base = static_cast<size_t>(std::max(0, index)) *
                            static_cast<size_t>(components);
        for (int c = 0; c < components; ++c) {
            const size_t at = base + static_cast<size_t>(c);
            out.push_back(index >= 0 && at < source.size() ? source[at] : 0.0f);
        }
    }

    // The *Set nodes other than IndexedFaceSet share one index (or none) for
    // every stream, so their vertices transfer straight across and only the
    // topology differs. This copies the streams; the callers below build the
    // faces.
    void CopyStreams(const GeometryStreams& streams, MeshPrimitive& prim) {
        prim.Positions = streams.Points;

        if (streams.NormalCount() >= streams.Points.size()) {
            prim.Normals.reserve(streams.Points.size());
            for (size_t i = 0; i < streams.Points.size(); ++i)
                prim.Normals.emplace_back(streams.Normals[i * 3], streams.Normals[i * 3 + 1],
                                          streams.Normals[i * 3 + 2]);
        } else if (!streams.Normals.empty()) {
            WarnOnce("shortNormals", "X3D: a geometry node has fewer normals than vertices; "
                                     "they are dropped and regenerated");
        }

        if (streams.TexCoordCount() >= streams.Points.size()) {
            std::vector<float> uv(streams.TexCoords.begin(),
                                  streams.TexCoords.begin() +
                                          static_cast<std::ptrdiff_t>(
                                                  streams.Points.size() *
                                                  static_cast<size_t>(streams.TexComponents)));
            AddTexCoordAttribute(prim, std::move(uv), streams.TexComponents);
        } else if (!streams.TexCoords.empty()) {
            WarnOnce("shortTexCoords", "X3D: a geometry node has fewer texture coordinates "
                                       "than vertices; they are dropped");
        }

        if (streams.ColorCount() >= streams.Points.size()) {
            std::vector<float> rgb(streams.Colors.begin(),
                                   streams.Colors.begin() +
                                           static_cast<std::ptrdiff_t>(
                                                   streams.Points.size() *
                                                   static_cast<size_t>(streams.ColorComponents)));
            AddColorAttribute(prim, std::move(rgb), streams.ColorComponents);
        } else if (!streams.Colors.empty()) {
            WarnOnce("shortColors", "X3D: a geometry node has fewer colours than vertices; "
                                    "they are dropped");
        }
    }

    bool InRange(const MeshPrimitive& prim, int index) const {
        return index >= 0 && static_cast<size_t>(index) < prim.Positions.size();
    }

    // <IndexedTriangleSet> and <IndexedQuadSet>: a flat index run with a fixed
    // number of corners per face and no separators.
    bool BuildIndexedSet(const XMLElement* geometry, MeshPrimitive& prim, int cornersPerFace) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) return false;
        CopyStreams(streams, prim);

        const std::vector<int> index = ParseIndices(geometry, "index");
        const bool ccw = ParseBool(geometry, "ccw", true);
        const size_t step = static_cast<size_t>(cornersPerFace);

        for (size_t i = 0; i + step <= index.size(); i += step) {
            std::vector<int> face(index.begin() + static_cast<std::ptrdiff_t>(i),
                                  index.begin() + static_cast<std::ptrdiff_t>(i + step));
            if (std::any_of(face.begin(), face.end(),
                            [&](int v) { return !InRange(prim, v); })) continue;
            if (!ccw) std::reverse(face.begin(), face.end());
            if (cornersPerFace > 3)
                prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
            for (int v : face) prim.Indices.push_back(static_cast<uint32_t>(v));
        }
        return FinishFixedFaces(prim, cornersPerFace);
    }

    // <TriangleSet> and <QuadSet>: no index at all, vertices consumed in order.
    bool BuildFlatSet(const XMLElement* geometry, MeshPrimitive& prim, int cornersPerFace) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) return false;
        CopyStreams(streams, prim);

        const bool ccw = ParseBool(geometry, "ccw", true);
        const size_t step = static_cast<size_t>(cornersPerFace);
        for (size_t i = 0; i + step <= prim.Positions.size(); i += step) {
            if (cornersPerFace > 3)
                prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
            for (size_t c = 0; c < step; ++c) {
                const size_t at = ccw ? i + c : i + step - 1 - c;
                prim.Indices.push_back(static_cast<uint32_t>(at));
            }
        }
        return FinishFixedFaces(prim, cornersPerFace);
    }

    static bool FinishFixedFaces(MeshPrimitive& prim, int cornersPerFace) {
        if (prim.Indices.empty()) return false;
        if (cornersPerFace <= 3) {
            prim.Mode = PrimitiveMode::Triangles;
            prim.FaceStarts.clear();
        } else {
            prim.Mode = PrimitiveMode::Polygons;
            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
        }
        return true;
    }

    // The fan and strip sets, indexed or not. Both are expanded to independent
    // triangles: the document has TriangleFan and TriangleStrip modes, but one
    // primitive holds one topology and these nodes carry *several* fans or
    // strips, which only separate triangles can express in one batch.
    bool BuildStrips(const XMLElement* geometry, MeshPrimitive& prim, bool fans, bool indexed) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) return false;
        CopyStreams(streams, prim);

        const bool ccw = ParseBool(geometry, "ccw", true);

        // Each run, as vertex indices into the streams.
        std::vector<std::vector<int>> runs;
        if (indexed) {
            std::vector<int> run;
            for (int value : ParseIndices(geometry, "index")) {
                if (value < 0) {
                    if (!run.empty()) runs.push_back(std::move(run));
                    run.clear();
                } else {
                    run.push_back(value);
                }
            }
            if (!run.empty()) runs.push_back(std::move(run));
        } else {
            size_t cursor = 0;
            for (int count : ParseIndices(geometry, fans ? "fanCount" : "stripCount")) {
                if (count < 3) { cursor += static_cast<size_t>(std::max(0, count)); continue; }
                std::vector<int> run;
                for (int c = 0; c < count && cursor < prim.Positions.size(); ++c)
                    run.push_back(static_cast<int>(cursor++));
                runs.push_back(std::move(run));
            }
        }

        for (const std::vector<int>& run : runs) {
            if (run.size() < 3) continue;
            for (size_t i = 0; i + 2 < run.size(); ++i) {
                int a = 0, b = 0, c = 0;
                if (fans) {
                    a = run[0]; b = run[i + 1]; c = run[i + 2];
                } else {
                    // A strip alternates winding; swapping the first two
                    // vertices of every odd triangle restores it.
                    const bool even = (i % 2) == 0;
                    a = run[even ? i : i + 1];
                    b = run[even ? i + 1 : i];
                    c = run[i + 2];
                }
                if (!InRange(prim, a) || !InRange(prim, b) || !InRange(prim, c)) continue;
                if (!ccw) std::swap(b, c);
                prim.Indices.push_back(static_cast<uint32_t>(a));
                prim.Indices.push_back(static_cast<uint32_t>(b));
                prim.Indices.push_back(static_cast<uint32_t>(c));
            }
        }
        if (prim.Indices.empty()) return false;
        prim.Mode = PrimitiveMode::Triangles;
        return true;
    }

    // <IndexedLineSet>: -1-separated polylines, expanded to independent
    // segments because that is the only line topology the document has.
    bool BuildIndexedLineSet(const XMLElement* geometry, MeshPrimitive& prim) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) return false;
        CopyStreams(streams, prim);

        std::vector<int> polyline;
        auto flush = [&]() {
            for (size_t i = 0; i + 1 < polyline.size(); ++i) {
                if (!InRange(prim, polyline[i]) || !InRange(prim, polyline[i + 1])) continue;
                prim.Indices.push_back(static_cast<uint32_t>(polyline[i]));
                prim.Indices.push_back(static_cast<uint32_t>(polyline[i + 1]));
            }
            polyline.clear();
        };
        for (int value : ParseIndices(geometry, "coordIndex")) {
            if (value < 0) flush();
            else polyline.push_back(value);
        }
        flush();

        if (prim.Indices.empty()) return false;
        prim.Mode = PrimitiveMode::Lines;
        return true;
    }

    // <LineSet>: vertexCount says how many of the coordinates each polyline
    // takes, consumed in order.
    bool BuildLineSet(const XMLElement* geometry, MeshPrimitive& prim) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) return false;
        CopyStreams(streams, prim);

        size_t cursor = 0;
        for (int count : ParseIndices(geometry, "vertexCount")) {
            if (count < 2) { cursor += static_cast<size_t>(std::max(0, count)); continue; }
            for (int i = 0; i + 1 < count && cursor + 1 < prim.Positions.size(); ++i, ++cursor) {
                prim.Indices.push_back(static_cast<uint32_t>(cursor));
                prim.Indices.push_back(static_cast<uint32_t>(cursor + 1));
            }
            ++cursor;
        }
        if (prim.Indices.empty()) return false;
        prim.Mode = PrimitiveMode::Lines;
        return true;
    }

    bool BuildPointSet(const XMLElement* geometry, MeshPrimitive& prim) {
        const GeometryStreams streams = ReadStreams(geometry);
        if (streams.Points.empty()) return false;
        CopyStreams(streams, prim);
        prim.Mode = PrimitiveMode::Points;
        return true;
    }

    // ===== GEOMETRIC PRIMITIVES =====
    //
    // Box, Sphere, Cylinder and Cone are real geometry in X3D, not a
    // convenience wrapper - a hand-written scene is often nothing else - so
    // they are tessellated here rather than skipped. The spec fixes their
    // dimensions and their default texture mapping but deliberately leaves the
    // subdivision to the browser; these counts are what the reference
    // implementations use, and a caller who wants finer can re-read nothing
    // because the shape is exact and this is the approximation.
    static constexpr int kSegments = 24;   // around the axis
    static constexpr int kRings = 12;      // pole to pole, Sphere only

    struct Builder {
        MeshPrimitive& Prim;
        std::vector<float> TexCoords;

        explicit Builder(MeshPrimitive& prim) : Prim(prim) {}

        uint32_t Vertex(const Vec3d& position, const Vec3f& normal, float u, float v) {
            const uint32_t index = static_cast<uint32_t>(Prim.Positions.size());
            Prim.Positions.push_back(position);
            Prim.Normals.push_back(normal);
            TexCoords.push_back(u);
            TexCoords.push_back(v);
            return index;
        }
        void Face(std::initializer_list<uint32_t> corners) {
            Prim.FaceStarts.push_back(static_cast<uint32_t>(Prim.Indices.size()));
            for (uint32_t corner : corners) Prim.Indices.push_back(corner);
        }
        void FaceOf(const std::vector<uint32_t>& corners) {
            Prim.FaceStarts.push_back(static_cast<uint32_t>(Prim.Indices.size()));
            for (uint32_t corner : corners) Prim.Indices.push_back(corner);
        }
        bool Finish(PrimitiveMode mode) {
            if (Prim.Indices.empty()) return false;
            Prim.Mode = mode;
            if (mode == PrimitiveMode::Polygons)
                Prim.FaceStarts.push_back(static_cast<uint32_t>(Prim.Indices.size()));
            else
                Prim.FaceStarts.clear();
            AddTexCoordAttribute(Prim, std::move(TexCoords), 2);
            return true;
        }
    };

    bool BuildBox(const XMLElement* geometry, MeshPrimitive& prim) {
        const Vec3d size = ParseVec3(geometry, "size", Vec3d(2.0, 2.0, 2.0));
        const double x = size.x * 0.5, y = size.y * 0.5, z = size.z * 0.5;

        struct Facet { Vec3f Normal; Vec3d Corner[4]; };
        const Facet facets[6] = {
                {{0, 0, 1},  {{-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}}},      // +Z
                {{0, 0, -1}, {{x, -y, -z}, {-x, -y, -z}, {-x, y, -z}, {x, y, -z}}},  // -Z
                {{1, 0, 0},  {{x, -y, z}, {x, -y, -z}, {x, y, -z}, {x, y, z}}},      // +X
                {{-1, 0, 0}, {{-x, -y, -z}, {-x, -y, z}, {-x, y, z}, {-x, y, -z}}},  // -X
                {{0, 1, 0},  {{-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}}},      // +Y
                {{0, -1, 0}, {{-x, -y, -z}, {x, -y, -z}, {x, -y, z}, {-x, -y, z}}}}; // -Y

        Builder builder(prim);
        static const float u[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        static const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        for (const Facet& facet : facets) {
            std::vector<uint32_t> corners;
            for (int i = 0; i < 4; ++i)
                corners.push_back(builder.Vertex(facet.Corner[i], facet.Normal, u[i], v[i]));
            builder.FaceOf(corners);
        }
        return builder.Finish(PrimitiveMode::Polygons);
    }

    bool BuildSphere(const XMLElement* geometry, MeshPrimitive& prim) {
        const double radius = ParseScalar(geometry, "radius", 1.0);
        Builder builder(prim);

        // The seam is duplicated (kSegments + 1 columns) so the texture wraps
        // to 1.0 instead of back to 0.0 across the last quad.
        for (int ring = 0; ring <= kRings; ++ring) {
            const double theta = kPi * ring / kRings;
            const double ringRadius = std::sin(theta);
            const double height = std::cos(theta);
            for (int segment = 0; segment <= kSegments; ++segment) {
                const double phi = 2.0 * kPi * segment / kSegments;
                const Vec3d direction(ringRadius * std::sin(phi), height,
                                      ringRadius * std::cos(phi));
                builder.Vertex(direction * radius,
                               Vec3f(static_cast<float>(direction.x),
                                     static_cast<float>(direction.y),
                                     static_cast<float>(direction.z)),
                               static_cast<float>(segment) / kSegments,
                               1.0f - static_cast<float>(ring) / kRings);
            }
        }

        const int stride = kSegments + 1;
        for (int ring = 0; ring < kRings; ++ring) {
            for (int segment = 0; segment < kSegments; ++segment) {
                const uint32_t v00 = static_cast<uint32_t>(ring * stride + segment);
                const uint32_t v01 = v00 + 1;
                const uint32_t v10 = static_cast<uint32_t>((ring + 1) * stride + segment);
                const uint32_t v11 = v10 + 1;
                // The pole rings collapse to a point, so their quads are
                // triangles rather than a quad with two identical corners.
                if (ring == 0) builder.Face({v00, v10, v11});
                else if (ring == kRings - 1) builder.Face({v00, v10, v01});
                else builder.Face({v00, v10, v11, v01});
            }
        }
        return builder.Finish(PrimitiveMode::Polygons);
    }

    bool BuildCylinder(const XMLElement* geometry, MeshPrimitive& prim) {
        const double radius = ParseScalar(geometry, "radius", 1.0);
        const double half = ParseScalar(geometry, "height", 2.0) * 0.5;
        const bool side = ParseBool(geometry, "side", true);
        const bool top = ParseBool(geometry, "top", true);
        const bool bottom = ParseBool(geometry, "bottom", true);

        Builder builder(prim);
        if (side) {
            for (int segment = 0; segment < kSegments; ++segment) {
                const double phi0 = 2.0 * kPi * segment / kSegments;
                const double phi1 = 2.0 * kPi * (segment + 1) / kSegments;
                const uint32_t b0 = SideVertex(builder, radius, -half, phi0,
                                               static_cast<float>(segment) / kSegments, 0.0f);
                const uint32_t b1 = SideVertex(builder, radius, -half, phi1,
                                               static_cast<float>(segment + 1) / kSegments, 0.0f);
                const uint32_t t1 = SideVertex(builder, radius, half, phi1,
                                               static_cast<float>(segment + 1) / kSegments, 1.0f);
                const uint32_t t0 = SideVertex(builder, radius, half, phi0,
                                               static_cast<float>(segment) / kSegments, 1.0f);
                builder.Face({b0, b1, t1, t0});
            }
        }
        // The caps are single n-gons rather than fans: the document holds
        // n-gons, and a 24-sided disc is one face in every format that does.
        if (top) builder.FaceOf(CapVertices(builder, radius, half, true));
        if (bottom) builder.FaceOf(CapVertices(builder, radius, -half, false));
        return builder.Finish(PrimitiveMode::Polygons);
    }

    bool BuildCone(const XMLElement* geometry, MeshPrimitive& prim) {
        const double radius = ParseScalar(geometry, "bottomRadius", 1.0);
        const double half = ParseScalar(geometry, "height", 2.0) * 0.5;
        const bool side = ParseBool(geometry, "side", true);
        const bool bottom = ParseBool(geometry, "bottom", true);

        Builder builder(prim);
        if (side) {
            // The slant, so the side normal leans out by the cone's own angle
            // rather than standing straight out from the axis.
            const double slant = std::sqrt(radius * radius + 4.0 * half * half);
            const double normalY = slant > 1e-12 ? radius / slant : 1.0;
            const double normalR = slant > 1e-12 ? 2.0 * half / slant : 0.0;

            for (int segment = 0; segment < kSegments; ++segment) {
                const double phi0 = 2.0 * kPi * segment / kSegments;
                const double phi1 = 2.0 * kPi * (segment + 1) / kSegments;
                const double phiMid = 0.5 * (phi0 + phi1);
                const uint32_t apex = builder.Vertex(
                        Vec3d(0.0, half, 0.0),
                        Vec3f(static_cast<float>(normalR * std::sin(phiMid)),
                              static_cast<float>(normalY),
                              static_cast<float>(normalR * std::cos(phiMid))),
                        (static_cast<float>(segment) + 0.5f) / kSegments, 1.0f);
                const uint32_t b0 = ConeVertex(builder, radius, -half, phi0, normalR, normalY,
                                               static_cast<float>(segment) / kSegments);
                const uint32_t b1 = ConeVertex(builder, radius, -half, phi1, normalR, normalY,
                                               static_cast<float>(segment + 1) / kSegments);
                builder.Face({apex, b0, b1});
            }
        }
        if (bottom) builder.FaceOf(CapVertices(builder, radius, -half, false));
        return builder.Finish(PrimitiveMode::Polygons);
    }

    static uint32_t SideVertex(Builder& builder, double radius, double height, double phi,
                               float u, float v) {
        return builder.Vertex(Vec3d(radius * std::sin(phi), height, radius * std::cos(phi)),
                              Vec3f(static_cast<float>(std::sin(phi)), 0.0f,
                                    static_cast<float>(std::cos(phi))),
                              u, v);
    }

    static uint32_t ConeVertex(Builder& builder, double radius, double height, double phi,
                               double normalR, double normalY, float u) {
        return builder.Vertex(Vec3d(radius * std::sin(phi), height, radius * std::cos(phi)),
                              Vec3f(static_cast<float>(normalR * std::sin(phi)),
                                    static_cast<float>(normalY),
                                    static_cast<float>(normalR * std::cos(phi))),
                              u, 0.0f);
    }

    // One n-gon disc. Wound counter-clockwise seen from outside, which is
    // increasing phi for the top cap and decreasing for the bottom.
    static std::vector<uint32_t> CapVertices(Builder& builder, double radius, double height,
                                             bool facingUp) {
        std::vector<uint32_t> corners;
        const Vec3f normal(0.0f, facingUp ? 1.0f : -1.0f, 0.0f);
        for (int i = 0; i < kSegments; ++i) {
            const int segment = facingUp ? i : kSegments - 1 - i;
            const double phi = 2.0 * kPi * segment / kSegments;
            const double sinPhi = std::sin(phi), cosPhi = std::cos(phi);
            corners.push_back(builder.Vertex(Vec3d(radius * sinPhi, height, radius * cosPhi),
                                             normal,
                                             static_cast<float>(0.5 + 0.5 * sinPhi),
                                             static_cast<float>(0.5 + 0.5 * cosPhi)));
        }
        return corners;
    }

    // ===== LIGHTS AND VIEWPOINTS =====

    // X3D states a light's direction and position on the light node; the
    // document states them as the transform of the node that carries it, which
    // is what glTF, USD and every renderer expect. So a light becomes a node
    // whose rotation carries -Z onto the stated direction.
    void ReadLight(const XMLElement* element, const std::string& kind, int parent) {
        ModelLight light;
        light.Name = DefName(element);
        if (light.Name.empty()) light.Name = kind;
        light.Color = ParseColor(element, "color", Vec3f(1.0f, 1.0f, 1.0f));
        light.Intensity = static_cast<float>(ParseScalar(element, "intensity", 1.0));
        if (!ParseBool(element, "on", true)) light.Intensity = 0.0f;

        ModelNode node;
        node.Name = light.Name;

        if (kind == "DirectionalLight") {
            light.Type = LightType::Directional;
            node.Rotation = RotationBetween(Vec3d(0.0, 0.0, -1.0),
                                            ParseVec3(element, "direction",
                                                      Vec3d(0.0, 0.0, -1.0)));
        } else if (kind == "PointLight") {
            light.Type = LightType::Point;
            light.Range = static_cast<float>(ParseScalar(element, "radius", 100.0));
            node.Translation = ParseVec3(element, "location", Vec3d(0.0, 0.0, 0.0));
        } else {
            light.Type = LightType::Spot;
            light.Range = static_cast<float>(ParseScalar(element, "radius", 100.0));
            light.OuterConeRadians =
                    static_cast<float>(ParseScalar(element, "cutOffAngle", kPi / 4.0));
            // beamWidth is where falloff starts. X3D allows it to exceed
            // cutOffAngle, meaning "no falloff at all"; the document's inner
            // cone cannot exceed the outer, so it clamps.
            light.InnerConeRadians =
                    static_cast<float>(ParseScalar(element, "beamWidth", kPi / 2.0));
            light.InnerConeRadians = std::min(light.InnerConeRadians, light.OuterConeRadians);
            node.Translation = ParseVec3(element, "location", Vec3d(0.0, 0.0, 0.0));
            node.Rotation = RotationBetween(Vec3d(0.0, 0.0, -1.0),
                                            ParseVec3(element, "direction",
                                                      Vec3d(0.0, 0.0, -1.0)));
        }

        // X3D lights carry an ambient term the document has no field for. It
        // is small and almost always zero, but it is not nothing.
        const double ambient = ParseScalar(element, "ambientIntensity", 0.0);
        if (ambient > 1e-9)
            document_->Metadata["x3d.light." + light.Name + ".ambientIntensity"] =
                    FormatNumber(ambient);
        if (ParseBool(element, "global", false))
            document_->Metadata["x3d.light." + light.Name + ".global"] = "true";

        node.Light = static_cast<int>(document_->Lights.size());
        document_->Lights.push_back(std::move(light));
        AddNamedNode(std::move(node), element, parent);
    }

    void ReadViewpoint(const XMLElement* element, const std::string& kind, int parent) {
        ModelCamera camera;
        camera.Name = Attribute(element, "description");
        if (camera.Name.empty()) camera.Name = DefName(element);
        if (camera.Name.empty()) camera.Name = kind;

        if (kind == "OrthoViewpoint") {
            camera.Type = CameraType::Orthographic;
            std::vector<double> extent = ParseNumbers(element, "fieldOfView");
            if (extent.size() < 4) extent = {-1.0, -1.0, 1.0, 1.0};
            camera.XMag = static_cast<float>((extent[2] - extent[0]) * 0.5);
            camera.YMag = static_cast<float>((extent[3] - extent[1]) * 0.5);
        } else {
            camera.Type = CameraType::Perspective;
            // X3D's fieldOfView is the *lesser* of the horizontal and vertical
            // angles; YFovRadians is the vertical one. They agree for every
            // viewport at least as wide as it is tall, which is all of them in
            // practice, and the document has no field for the other reading.
            camera.YFovRadians = static_cast<float>(ParseScalar(element, "fieldOfView",
                                                                kPi / 4.0));
        }

        ModelNode node;
        node.Name = camera.Name;
        node.Translation = ParseVec3(element, "position", Vec3d(0.0, 0.0, 10.0));
        node.Rotation = ParseRotation(element, "orientation");
        node.Camera = static_cast<int>(document_->Cameras.size());
        document_->Cameras.push_back(std::move(camera));
        AddNamedNode(std::move(node), element, parent);
    }

    // Nodes are registered under their DEF name so a ROUTE can find them
    // later. A name reused by DEF and then USE'd names several document nodes;
    // the first - the one the DEF actually created - is the one a route
    // targets, which is what a browser does too.
    int AddNamedNode(ModelNode node, const XMLElement* element, int parent) {
        const std::string name = DefName(element);
        const int index = document_->AddNode(std::move(node), parent);
        if (!name.empty() && nodeIndexByName_.find(name) == nodeIndexByName_.end())
            nodeIndexByName_[name] = index;
        return index;
    }

    // ===== ANIMATION =====
    //
    // X3D holds no keyframes on the node. A TimeSensor emits a fraction, a
    // ROUTE carries it to an interpolator, the interpolator turns it into a
    // value, and a second ROUTE carries that value to one field of one node.
    // Following both hops turns those four unrelated nodes into exactly one
    // AnimationChannel and its sampler.

    struct Route {
        std::string FromNode, FromField, ToNode, ToField;
    };

    void CollectRoutes(const XMLElement* element, std::vector<Route>& routes) {
        for (const XMLElement* child = element->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            if (std::string(child->Name()) == "ROUTE") {
                Route route;
                route.FromNode = Attribute(child, "fromNode");
                route.FromField = Attribute(child, "fromField");
                route.ToNode = Attribute(child, "toNode");
                route.ToField = Attribute(child, "toField");
                if (!route.FromNode.empty() && !route.ToNode.empty())
                    routes.push_back(std::move(route));
            }
            CollectRoutes(child, routes);
        }
    }

    // Field names appear with and without the set_ / _changed decoration
    // depending on the exporter and the X3D version; both spell the same field.
    static std::string BareField(std::string field) {
        if (field.rfind("set_", 0) == 0) field.erase(0, 4);
        const std::string suffix = "_changed";
        if (field.size() > suffix.size() &&
            field.compare(field.size() - suffix.size(), suffix.size(), suffix) == 0)
            field.erase(field.size() - suffix.size());
        return field;
    }

    void ReadRoutes(const XMLElement* scene) {
        std::vector<Route> routes;
        CollectRoutes(scene, routes);
        if (routes.empty()) return;

        ModelAnimation animation;
        animation.Name = "X3D";

        for (const Route& route : routes) {
            if (BareField(route.FromField) != "value") continue;

            auto source = definitions_.find(route.FromNode);
            if (source == definitions_.end()) continue;
            const std::string interpolator = source->second->Name();
            if (interpolator.find("Interpolator") == std::string::npos) continue;

            auto target = nodeIndexByName_.find(route.ToNode);
            if (target == nodeIndexByName_.end()) {
                WarnOnce("route." + route.ToNode,
                         "X3D: a ROUTE drives '" + route.ToNode +
                                 "', which is not a node this reader placed");
                continue;
            }

            AnimationPath path;
            const std::string field = BareField(route.ToField);
            if (field == "translation") path = AnimationPath::Translation;
            else if (field == "rotation") path = AnimationPath::Rotation;
            else if (field == "scale") path = AnimationPath::Scale;
            else {
                WarnOnce("routeField." + field,
                         "X3D: a ROUTE drives the field '" + field +
                                 "', which the document has no animation path for");
                continue;
            }

            // The TimeSensor upstream of this interpolator sets the timescale:
            // interpolator keys are 0..1 fractions of one cycle.
            double cycle = 1.0;
            for (const Route& upstream : routes) {
                if (upstream.ToNode != route.FromNode) continue;
                if (BareField(upstream.ToField) != "fraction") continue;
                auto sensor = definitions_.find(upstream.FromNode);
                if (sensor == definitions_.end()) continue;
                cycle = ParseScalar(sensor->second, "cycleInterval", 1.0);
                break;
            }

            AnimationSampler sampler;
            if (!BuildSampler(source->second, interpolator, path, cycle, sampler)) continue;

            AnimationChannel channel;
            channel.TargetNode = target->second;
            channel.Path = path;
            channel.Sampler = static_cast<int>(animation.Samplers.size());
            animation.Samplers.push_back(std::move(sampler));
            animation.Channels.push_back(channel);
        }

        if (!animation.Channels.empty()) document_->Animations.push_back(std::move(animation));
    }

    bool BuildSampler(const XMLElement* element, const std::string& interpolator,
                      AnimationPath path, double cycle, AnimationSampler& sampler) {
        const std::vector<double> keys = ParseNumbers(element, "key");
        const std::vector<double> values = ParseNumbers(element, "keyValue");
        if (keys.empty() || values.empty()) return false;

        const bool orientation = interpolator == "OrientationInterpolator";
        if (!orientation && interpolator != "PositionInterpolator") {
            WarnOnce("interpolator." + interpolator,
                     "X3D: <" + interpolator + "> is not read; only position and orientation "
                     "interpolators map to an animation channel");
            return false;
        }
        if (orientation && path != AnimationPath::Rotation) return false;
        if (!orientation && path == AnimationPath::Rotation) return false;

        const size_t arity = orientation ? 4u : 3u;
        const size_t count = std::min(keys.size(), values.size() / arity);
        if (count == 0) return false;

        for (size_t i = 0; i < count; ++i) {
            sampler.Times.push_back(static_cast<float>(keys[i] * cycle));
            const size_t base = i * arity;
            if (orientation) {
                const Vec3d axis(values[base], values[base + 1], values[base + 2]);
                const Quatd q = axis.Length() < 1e-12
                                        ? Quatd::Identity()
                                        : Quatd::FromAxisAngle(axis, values[base + 3]);
                sampler.Values.push_back(static_cast<float>(q.x));
                sampler.Values.push_back(static_cast<float>(q.y));
                sampler.Values.push_back(static_cast<float>(q.z));
                sampler.Values.push_back(static_cast<float>(q.w));
            } else {
                for (size_t c = 0; c < 3; ++c)
                    sampler.Values.push_back(static_cast<float>(values[base + c]));
            }
        }
        sampler.Interpolate = Interpolation::Linear;
        return true;
    }

    // ===== FINISHING =====

    void Finish() {
        bool generated = false;
        if (options_.GenerateMissingNormals) {
            for (auto& mesh : document_->Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty() && prim.Mode != PrimitiveMode::Lines &&
                        prim.Mode != PrimitiveMode::Points) {
                        prim.RecomputeNormals();
                        generated = true;
                    }
        }
        // Normals arriving through a per-corner index stream can be shorter
        // than the vertex list when a stream is malformed; pad rather than
        // leave the arrays out of step.
        for (auto& mesh : document_->Meshes)
            for (auto& prim : mesh.Primitives)
                if (!prim.Normals.empty() && prim.Normals.size() != prim.Positions.size())
                    prim.Normals.resize(prim.Positions.size(), Vec3f(0.0f, 0.0f, 1.0f));

        // creaseAngle only matters where the file left the normals to the
        // browser. Below pi it means "crease edges sharper than this", and
        // RecomputeNormals() averages across every edge instead - so the model
        // arrives smoother than the file asked for. The angle itself is kept
        // on the node, so a consumer that implements creasing can still do it.
        if (generated && sawSharpCrease_)
            options_.Warn("X3D: creaseAngle is below pi and the file carries no normals; "
                          "generated normals average across every edge, so edges the file "
                          "wanted creased arrive smooth (the angle is kept as the node's "
                          "x3d.creaseAngle extra)");

        if (options_.WeldTolerance > 0.0) document_->WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) document_->TriangulateAll();
        if (options_.ForceUpAxis.has_value()) document_->ConvertUpAxis(*options_.ForceUpAxis);
    }

    void WarnOnce(const std::string& key, const std::string& message) {
        if (!warned_.insert(key).second) return;
        options_.Warn(message);
    }

    const ConversionOptions& options_;
    std::shared_ptr<ModelDocument> document_;

    std::map<std::string, const XMLElement*> definitions_;   // DEF name -> element
    std::set<const XMLElement*> expanding_;                  // USE cycle guard
    std::map<std::string, int> nodeIndexByName_;             // DEF name -> node index
    std::map<std::string, int> materialCache_;               // Appearance DEF + sidedness
    std::map<std::string, int> imageCache_;                  // ImageTexture DEF
    std::map<std::string, int> meshCache_;                   // geometry DEF + material
    std::set<std::string> warned_;

    bool sawSharpCrease_ = false;
};

bool LooksLikeX3D(const std::string& head) {
    // The DOCTYPE, the root element and the schema location - any one of them
    // identifies the XML encoding. A .x3d that is really VRML classic syntax
    // has none of them and is deliberately rejected rather than half-read.
    return head.find("<X3D") != std::string::npos ||
           head.find("DTD X3D") != std::string::npos ||
           head.find("x3d.xsd") != std::string::npos ||
           head.find("x3d-3.0.xsd") != std::string::npos;
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities X3DConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;              // IndexedFaceSet is the n-gon workhorse
    caps.Lines = true;              // IndexedLineSet, LineSet
    caps.PointClouds = true;        // PointSet
    caps.SceneGraph = true;
    caps.Instancing = true;         // DEF/USE on geometry and on whole subtrees
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.VertexColors = true;       // Color and ColorRGBA
    caps.Normals = true;
    caps.Animations = true;         // TimeSensor + interpolator + ROUTE
    caps.Cameras = true;            // Viewpoint, OrthoViewpoint
    caps.Lights = true;             // Directional, Point, Spot
    caps.Units = true;              // <unit category="length"> (X3D 3.3+)
    caps.Metadata = true;           // <head><meta>
    caps.DoublePrecision = true;    // coordinates are decimal text, read into double
    // Deliberately false: no PBR materials (X3D 3's Material is fixed-function
    // and the X3D 4 PhysicalMaterial is not read), no embedded textures
    // (PixelTexture is not decoded), no tangents, no skinning (H-Anim is not
    // read), no morph targets (CoordinateInterpolator is not read), and no
    // up-axis field - X3D is Y-up by definition, so there is nothing to state.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> X3DConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    XMLDocument xml;
    if (xml.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
        options.Warn(std::string("X3D: cannot parse ") + filename + " — " + xml.ErrorStr());
        return nullptr;
    }
    Reader reader(options);
    auto document = reader.Run(xml);
    if (document && document->Title.empty()) {
        const size_t slash = filename.find_last_of("/\\");
        std::string stem = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos && dot > 0) stem.erase(dot);
        document->Title = stem;
    }
    return document;
}

std::shared_ptr<ModelStorage::ModelDocument> X3DConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    XMLDocument xml;
    if (xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) !=
        tinyxml2::XML_SUCCESS) {
        options.Warn(std::string("X3D: cannot parse the data — ") + xml.ErrorStr());
        return nullptr;
    }
    Reader reader(options);
    return reader.Run(xml);
}

std::shared_ptr<ModelStorage::ModelDocument> X3DConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return ImportFromMemory(std::vector<uint8_t>(text.begin(), text.end()), options);
}

bool X3DConverter::ValidateData(const std::vector<uint8_t>& data) const {
    const size_t limit = std::min<size_t>(data.size(), 4096);
    return LooksLikeX3D(
            std::string(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit)));
}

bool X3DConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<char> head(4096);
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    return LooksLikeX3D(std::string(head.data(),
                                    static_cast<size_t>(std::max<std::streamsize>(0, file.gcount()))));
}

} // namespace ModelConverter
} // namespace UltraCanvas
