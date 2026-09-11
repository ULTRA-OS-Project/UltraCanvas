// Plugins/Models/COLLADA/UltraCanvasColladaConverter.cpp
// The COLLADA reader.
//
// Three things about COLLADA shape this code:
//
//   * A node's transform is an ordered *sequence* of <translate>, <rotate>,
//     <scale> and <matrix> elements, composed in document order — not a TRS
//     triple. Reading them into fixed slots gives the wrong pose for any file
//     that writes them in another order or writes several of one kind, which
//     Blender does (three <rotate> elements, one per axis).
//   * Geometry indices are per-corner and per-stream, like OBJ's: <p> holds
//     one index per input per corner, and a corner is the tuple. Unique tuples
//     become document vertices.
//   * Animation usually targets the whole matrix rather than TRS. Those
//     keyframes are decomposed into translation, rotation and scale channels,
//     because that is what the document (and every renderer) interpolates.
//
// XML is parsed with tinyxml2, as the SVG, DOCX and XLSX code already does.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/COLLADA/UltraCanvasColladaConverter.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ===== XML AND TEXT HELPERS =====

const char* Attribute(const XMLElement* element, const char* name) {
    const char* value = element ? element->Attribute(name) : nullptr;
    return value ? value : "";
}

// COLLADA references are "#id"; a bare id also appears in a few places.
std::string ResolveReference(const char* reference) {
    if (!reference) return {};
    return reference[0] == '#' ? std::string(reference + 1) : std::string(reference);
}

std::vector<double> ParseNumbers(const XMLElement* element) {
    std::vector<double> values;
    if (!element || !element->GetText()) return values;
    std::istringstream stream(element->GetText());
    double value = 0.0;
    while (stream >> value) values.push_back(value);
    return values;
}

std::vector<int> ParseIntegers(const XMLElement* element) {
    std::vector<int> values;
    if (!element || !element->GetText()) return values;
    std::istringstream stream(element->GetText());
    int value = 0;
    while (stream >> value) values.push_back(value);
    return values;
}

std::vector<std::string> ParseWords(const XMLElement* element) {
    std::vector<std::string> words;
    if (!element || !element->GetText()) return words;
    std::istringstream stream(element->GetText());
    std::string word;
    while (stream >> word) words.push_back(word);
    return words;
}

// A <source> resolved to its values and the accessor's stride.
struct SourceData {
    std::vector<double> Values;
    int Stride = 1;

    size_t Count() const { return Stride > 0 ? Values.size() / static_cast<size_t>(Stride) : 0; }
    double At(size_t element, int component) const {
        const size_t index = element * static_cast<size_t>(Stride) + static_cast<size_t>(component);
        return index < Values.size() ? Values[index] : 0.0;
    }
};

// One <input> of a primitive element.
struct PrimitiveInput {
    std::string Semantic;
    std::string Source;
    int Offset = 0;
    int Set = 0;
};

// ===== READER =====

class Reader {
public:
    explicit Reader(const ConversionOptions& options) : options_(options) {}

    std::shared_ptr<ModelDocument> Run(XMLDocument& xml) {
        const XMLElement* root = xml.RootElement();
        if (!root || std::strcmp(root->Name(), "COLLADA") != 0) {
            options_.Warn("COLLADA: the root element is not <COLLADA>");
            return nullptr;
        }

        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "dae";
        document_->Metadata["collada.version"] = Attribute(root, "version");

        ReadAsset(root->FirstChildElement("asset"));
        ReadImages(root->FirstChildElement("library_images"));
        ReadEffects(root->FirstChildElement("library_effects"));
        ReadMaterials(root->FirstChildElement("library_materials"));
        ReadGeometries(root->FirstChildElement("library_geometries"));
        ReadVisualScenes(root);
        ReadAnimations(root->FirstChildElement("library_animations"));
        WarnAboutUnread(root);

        if (document_->Meshes.empty()) {
            options_.Warn("COLLADA: no geometry found");
            return nullptr;
        }
        Finish();
        return document_;
    }

private:
    // ===== ASSET =====

    void ReadAsset(const XMLElement* asset) {
        if (!asset) {
            options_.Warn("COLLADA: no <asset>; unit and up axis are unknown");
            document_->Up = UpAxis::YUp;
            return;
        }

        if (const XMLElement* contributor = asset->FirstChildElement("contributor")) {
            if (const XMLElement* author = contributor->FirstChildElement("author"))
                if (author->GetText()) document_->Author = author->GetText();
            if (const XMLElement* tool = contributor->FirstChildElement("authoring_tool"))
                if (tool->GetText()) document_->Generator = tool->GetText();
        }
        for (const char* name : {"created", "modified"})
            if (const XMLElement* element = asset->FirstChildElement(name))
                if (element->GetText())
                    document_->Metadata[std::string("collada.") + name] = element->GetText();

        // <unit meter="x"> is metres per drawing unit — exactly what
        // UnitScaleToMeters means, so it transfers directly. The name is a
        // hint; the number is authoritative.
        if (const XMLElement* unit = asset->FirstChildElement("unit")) {
            const double metres = unit->DoubleAttribute("meter", 0.0);
            if (metres > 0.0) {
                document_->UnitScaleToMeters = metres;
                document_->SourceUnit = UnitFromMetres(metres);
            }
        }
        if (document_->SourceUnit == ModelUnit::Unspecified &&
            options_.AssumeUnit != ModelUnit::Unspecified) {
            document_->SourceUnit = options_.AssumeUnit;
            document_->UnitScaleToMeters = MetersPerUnit(options_.AssumeUnit);
        }

        document_->Up = UpAxis::YUp;
        if (const XMLElement* up = asset->FirstChildElement("up_axis")) {
            const std::string value = up->GetText() ? up->GetText() : "";
            if (value == "Z_UP") document_->Up = UpAxis::ZUp;
            else if (value == "X_UP")
                options_.Warn("COLLADA: up_axis is X_UP, which the document cannot express; "
                              "the model is left as written and will appear rotated");
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

    // ===== IMAGES, EFFECTS, MATERIALS =====

    void ReadImages(const XMLElement* library) {
        if (!library) return;
        for (const XMLElement* image = library->FirstChildElement("image"); image;
             image = image->NextSiblingElement("image")) {
            const XMLElement* from = image->FirstChildElement("init_from");
            std::string uri = from && from->GetText() ? from->GetText() : "";
            // COLLADA 1.5 wraps the path in <ref>.
            if (uri.empty() && from)
                if (const XMLElement* ref = from->FirstChildElement("ref"))
                    if (ref->GetText()) uri = ref->GetText();
            if (uri.empty()) continue;

            ModelImage entry;
            entry.Name = Attribute(image, "id");
            entry.Uri = uri;
            imageIndexById_[entry.Name] = static_cast<int>(document_->Images.size());
            document_->Images.push_back(std::move(entry));
        }
    }

    // A texture reference names a sampler2D newparam, which names a surface
    // newparam, which names the image. Some exporters short-circuit the chain
    // and name the image directly, so both are accepted.
    int ResolveTextureImage(const XMLElement* effect, const std::string& samplerSid) {
        std::string current = samplerSid;
        for (int hop = 0; hop < 4 && !current.empty(); ++hop) {
            auto direct = imageIndexById_.find(current);
            if (direct != imageIndexById_.end()) return direct->second;

            const XMLElement* profile = effect->FirstChildElement("profile_COMMON");
            if (!profile) return -1;
            std::string next;
            for (const XMLElement* param = profile->FirstChildElement("newparam"); param;
                 param = param->NextSiblingElement("newparam")) {
                if (Attribute(param, "sid") != current) continue;
                if (const XMLElement* sampler = param->FirstChildElement("sampler2D"))
                    if (const XMLElement* source = sampler->FirstChildElement("source"))
                        if (source->GetText()) next = source->GetText();
                if (next.empty())
                    if (const XMLElement* surface = param->FirstChildElement("surface"))
                        if (const XMLElement* from = surface->FirstChildElement("init_from"))
                            if (from->GetText()) next = from->GetText();
                break;
            }
            if (next.empty() || next == current) return -1;
            current = next;
        }
        return -1;
    }

    // A <diffuse>-style slot: either a <color> or a <texture>.
    bool ReadColorOrTexture(const XMLElement* effect, const XMLElement* slot,
                            Vec3f& color, float& alpha, TextureRef& texture) {
        if (!slot) return false;
        if (const XMLElement* colorElement = slot->FirstChildElement("color")) {
            const std::vector<double> values = ParseNumbers(colorElement);
            if (values.size() >= 3)
                color = Vec3f(static_cast<float>(values[0]), static_cast<float>(values[1]),
                              static_cast<float>(values[2]));
            if (values.size() >= 4) alpha = static_cast<float>(values[3]);
            return true;
        }
        if (const XMLElement* textureElement = slot->FirstChildElement("texture")) {
            texture.Image = ResolveTextureImage(effect, Attribute(textureElement, "texture"));
            if (texture.Image < 0)
                options_.Warn(std::string("COLLADA: texture '") +
                              Attribute(textureElement, "texture") +
                              "' does not resolve to an image in <library_images>");
            return true;
        }
        return false;
    }

    static float ReadFloatChild(const XMLElement* parent, float fallback) {
        if (!parent) return fallback;
        const XMLElement* value = parent->FirstChildElement("float");
        if (!value || !value->GetText()) return fallback;
        return static_cast<float>(std::atof(value->GetText()));
    }

    void ReadEffects(const XMLElement* library) {
        if (!library) return;
        for (const XMLElement* effect = library->FirstChildElement("effect"); effect;
             effect = effect->NextSiblingElement("effect")) {
            const XMLElement* profile = effect->FirstChildElement("profile_COMMON");
            if (!profile) continue;
            const XMLElement* technique = profile->FirstChildElement("technique");
            if (!technique) continue;

            // phong, blinn, lambert or constant — all the same slots, minus
            // the ones a simpler model does not have.
            const XMLElement* shading = nullptr;
            for (const char* name : {"phong", "blinn", "lambert", "constant"})
                if ((shading = technique->FirstChildElement(name))) break;
            if (!shading) continue;

            ModelMaterial material;
            PhongParams phong;
            float alpha = 1.0f;
            float ignored = 1.0f;

            ReadColorOrTexture(effect, shading->FirstChildElement("ambient"),
                               phong.Ambient, ignored, phong.AmbientTexture);
            ReadColorOrTexture(effect, shading->FirstChildElement("diffuse"),
                               phong.Diffuse, alpha, phong.DiffuseTexture);
            ReadColorOrTexture(effect, shading->FirstChildElement("specular"),
                               phong.Specular, ignored, phong.SpecularTexture);
            Vec3f emission(0.0f, 0.0f, 0.0f);
            TextureRef emissionTexture;
            ReadColorOrTexture(effect, shading->FirstChildElement("emission"),
                               emission, ignored, emissionTexture);
            material.EmissiveFactor = emission;
            material.EmissiveTexture = emissionTexture;

            phong.Shininess = ReadFloatChild(shading->FirstChildElement("shininess"), 0.0f);
            material.IndexOfRefraction =
                    ReadFloatChild(shading->FirstChildElement("index_of_refraction"), 1.0f);

            // Transparency is two elements that must be read together, and the
            // opaque mode inverts the sense: A_ONE (the default) means the
            // transparent colour's alpha is opacity, RGB_ZERO means its
            // luminance is transparency.
            const XMLElement* transparent = shading->FirstChildElement("transparent");
            const float transparency =
                    ReadFloatChild(shading->FirstChildElement("transparency"), 1.0f);
            float opacity = 1.0f;
            if (transparent) {
                const std::string opaque = Attribute(transparent, "opaque");
                Vec3f color(0.0f, 0.0f, 0.0f);
                float colorAlpha = 1.0f;
                TextureRef unused;
                ReadColorOrTexture(effect, transparent, color, colorAlpha, unused);
                if (opaque == "RGB_ZERO" || opaque == "RGB_ONE") {
                    const float luminance = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
                    opacity = 1.0f - luminance * transparency;
                } else {
                    opacity = colorAlpha * transparency;
                }
            }
            if (opacity < 0.0f) opacity = 0.0f;
            if (opacity > 1.0f) opacity = 1.0f;

            material.Phong = phong;
            material.BaseColorFactor = Vec4f(phong.Diffuse.x, phong.Diffuse.y, phong.Diffuse.z,
                                             opacity);
            if (opacity < 1.0f) material.Alpha = AlphaMode::Blend;
            material.DeriveMissingModel();
            effectMaterials_[Attribute(effect, "id")] = std::move(material);
        }
    }

    void ReadMaterials(const XMLElement* library) {
        if (!library) return;
        for (const XMLElement* material = library->FirstChildElement("material"); material;
             material = material->NextSiblingElement("material")) {
            const std::string id = Attribute(material, "id");
            const XMLElement* instance = material->FirstChildElement("instance_effect");
            const std::string effectId = ResolveReference(Attribute(instance, "url"));

            ModelMaterial entry;
            auto found = effectMaterials_.find(effectId);
            if (found != effectMaterials_.end()) entry = found->second;
            else entry.DeriveMissingModel();

            const char* name = material->Attribute("name");
            entry.Name = name ? name : id;
            materialIndexById_[id] = document_->AddMaterial(std::move(entry));
        }
    }

    // ===== GEOMETRY =====

    void ReadGeometries(const XMLElement* library) {
        if (!library) return;
        for (const XMLElement* geometry = library->FirstChildElement("geometry"); geometry;
             geometry = geometry->NextSiblingElement("geometry")) {
            const XMLElement* mesh = geometry->FirstChildElement("mesh");
            if (!mesh) {
                if (geometry->FirstChildElement("spline") || geometry->FirstChildElement("convex_mesh"))
                    options_.Warn(std::string("COLLADA: geometry '") + Attribute(geometry, "id") +
                                  "' is a spline or convex mesh, which is not read");
                continue;
            }
            ReadMesh(geometry, mesh);
        }
    }

    void ReadMesh(const XMLElement* geometry, const XMLElement* mesh) {
        // Sources, then the <vertices> indirection: a VERTEX input names a
        // <vertices> element, which names the real POSITION source.
        std::map<std::string, SourceData> sources;
        for (const XMLElement* source = mesh->FirstChildElement("source"); source;
             source = source->NextSiblingElement("source")) {
            SourceData data;
            data.Values = ParseNumbers(source->FirstChildElement("float_array"));
            if (const XMLElement* common = source->FirstChildElement("technique_common"))
                if (const XMLElement* accessor = common->FirstChildElement("accessor"))
                    data.Stride = std::max(1, accessor->IntAttribute("stride", 1));
            sources[Attribute(source, "id")] = std::move(data);
        }

        std::map<std::string, std::string> verticesToSource;
        for (const XMLElement* vertices = mesh->FirstChildElement("vertices"); vertices;
             vertices = vertices->NextSiblingElement("vertices")) {
            for (const XMLElement* input = vertices->FirstChildElement("input"); input;
                 input = input->NextSiblingElement("input")) {
                if (std::string(Attribute(input, "semantic")) == "POSITION")
                    verticesToSource[Attribute(vertices, "id")] =
                            ResolveReference(Attribute(input, "source"));
            }
        }

        ModelMesh modelMesh;
        const char* name = geometry->Attribute("name");
        modelMesh.Name = name ? name : Attribute(geometry, "id");

        // The material each primitive names by symbol. A symbol is bound to a
        // real material at the instance, so it is kept per primitive rather
        // than resolved here — the same geometry can be instanced twice with
        // different materials.
        std::vector<std::string> symbols;
        for (const XMLElement* child = mesh->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const std::string kind = child->Name();
            if (kind == "polylist" || kind == "triangles" || kind == "polygons" ||
                kind == "tristrips" || kind == "trifans" || kind == "lines" ||
                kind == "linestrips") {
                MeshPrimitive prim;
                if (ReadPrimitive(child, kind, sources, verticesToSource, prim)) {
                    prim.Name = modelMesh.Name;
                    modelMesh.Primitives.push_back(std::move(prim));
                    symbols.push_back(Attribute(child, "material"));
                }
            }
        }

        if (modelMesh.Primitives.empty()) return;
        const int meshIndex = document_->AddMesh(std::move(modelMesh));
        meshIndexById_[Attribute(geometry, "id")] = meshIndex;
        symbolsByMesh_[meshIndex] = std::move(symbols);
    }

    bool ReadPrimitive(const XMLElement* element, const std::string& kind,
                       const std::map<std::string, SourceData>& sources,
                       const std::map<std::string, std::string>& verticesToSource,
                       MeshPrimitive& prim) {
        if (kind == "tristrips" || kind == "trifans" || kind == "linestrips") {
            options_.Warn("COLLADA: <" + kind + "> is not read; export triangles or polygons");
            return false;
        }

        std::vector<PrimitiveInput> inputs;
        int stride = 0;
        for (const XMLElement* input = element->FirstChildElement("input"); input;
             input = input->NextSiblingElement("input")) {
            PrimitiveInput entry;
            entry.Semantic = Attribute(input, "semantic");
            entry.Source = ResolveReference(Attribute(input, "source"));
            entry.Offset = input->IntAttribute("offset", 0);
            entry.Set = input->IntAttribute("set", 0);
            if (entry.Semantic == "VERTEX") {
                auto redirect = verticesToSource.find(entry.Source);
                if (redirect != verticesToSource.end()) entry.Source = redirect->second;
                entry.Semantic = "POSITION";
            }
            stride = std::max(stride, entry.Offset + 1);
            inputs.push_back(std::move(entry));
        }
        if (inputs.empty() || stride == 0) return false;

        // The corner indices, and how many corners each face has.
        std::vector<int> indices;
        std::vector<int> faceSizes;
        if (kind == "polygons") {
            // Each <p> is one face. <ph> (a face with holes) is not read.
            for (const XMLElement* p = element->FirstChildElement("p"); p;
                 p = p->NextSiblingElement("p")) {
                const std::vector<int> face = ParseIntegers(p);
                if (static_cast<int>(face.size()) < stride * 3) continue;
                faceSizes.push_back(static_cast<int>(face.size()) / stride);
                indices.insert(indices.end(), face.begin(), face.end());
            }
            if (element->FirstChildElement("ph"))
                options_.Warn("COLLADA: <polygons> with holes (<ph>) are not read");
        } else {
            indices = ParseIntegers(element->FirstChildElement("p"));
            if (kind == "polylist") {
                for (int count : ParseIntegers(element->FirstChildElement("vcount")))
                    faceSizes.push_back(count);
            } else {
                const int perFace = kind == "lines" ? 2 : 3;
                const int corners = stride > 0 ? static_cast<int>(indices.size()) / stride : 0;
                for (int i = 0; i + perFace <= corners; i += perFace) faceSizes.push_back(perFace);
            }
        }
        if (indices.empty() || faceSizes.empty()) return false;

        // Resolve each corner tuple into one document vertex, exactly as the
        // OBJ reader does — COLLADA has the same per-stream indexing.
        std::map<std::vector<int>, uint32_t> vertexCache;
        std::vector<float> texcoords, colors;
        int texcoordComponents = 2, colorComponents = 3;
        size_t cursor = 0;
        size_t maxCorners = 0;

        for (int faceSize : faceSizes) {
            if (faceSize < 1) continue;
            const size_t needed = static_cast<size_t>(faceSize) * static_cast<size_t>(stride);
            if (cursor + needed > indices.size()) {
                options_.Warn("COLLADA: <" + kind + "> index list is shorter than its face counts "
                              "declare; the remaining faces are dropped");
                break;
            }

            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
            for (int corner = 0; corner < faceSize; ++corner) {
                std::vector<int> key(indices.begin() + static_cast<std::ptrdiff_t>(cursor),
                                     indices.begin() + static_cast<std::ptrdiff_t>(cursor) + stride);
                cursor += static_cast<size_t>(stride);

                auto found = vertexCache.find(key);
                if (found != vertexCache.end()) {
                    prim.Indices.push_back(found->second);
                    continue;
                }
                const uint32_t fresh = static_cast<uint32_t>(prim.Positions.size());
                vertexCache.emplace(key, fresh);
                prim.Indices.push_back(fresh);

                for (const PrimitiveInput& input : inputs) {
                    auto source = sources.find(input.Source);
                    if (source == sources.end()) continue;
                    const size_t element_ = static_cast<size_t>(
                            key[static_cast<size_t>(input.Offset)]);
                    const SourceData& data = source->second;

                    if (input.Semantic == "POSITION") {
                        prim.Positions.emplace_back(data.At(element_, 0), data.At(element_, 1),
                                                    data.At(element_, 2));
                    } else if (input.Semantic == "NORMAL") {
                        prim.Normals.emplace_back(static_cast<float>(data.At(element_, 0)),
                                                  static_cast<float>(data.At(element_, 1)),
                                                  static_cast<float>(data.At(element_, 2)));
                    } else if (input.Semantic == "TEXCOORD") {
                        texcoordComponents = std::min(data.Stride, 3);
                        for (int c = 0; c < texcoordComponents; ++c)
                            texcoords.push_back(static_cast<float>(data.At(element_, c)));
                    } else if (input.Semantic == "COLOR") {
                        colorComponents = std::min(data.Stride, 4);
                        for (int c = 0; c < colorComponents; ++c)
                            colors.push_back(static_cast<float>(data.At(element_, c)));
                    }
                }
            }
            maxCorners = std::max(maxCorners, static_cast<size_t>(faceSize));
        }

        if (prim.Positions.empty()) return false;

        if (kind == "lines") {
            prim.Mode = PrimitiveMode::Lines;
            prim.FaceStarts.clear();
        } else if (maxCorners <= 3) {
            prim.Mode = PrimitiveMode::Triangles;
            prim.FaceStarts.clear();
        } else {
            prim.Mode = PrimitiveMode::Polygons;
            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
        }

        if (!texcoords.empty()) {
            VertexAttribute uv;
            uv.Semantic = AttributeSemantic::TexCoord;
            uv.Name = "TEXCOORD_0";
            uv.Components = texcoordComponents;
            uv.Values = std::move(texcoords);
            prim.Attributes.push_back(std::move(uv));
        }
        if (!colors.empty()) {
            VertexAttribute color;
            color.Semantic = AttributeSemantic::Color;
            color.Name = "COLOR_0";
            color.Components = colorComponents;
            color.Values = std::move(colors);
            prim.Attributes.push_back(std::move(color));
        }

        prim.Material = -1;   // bound by symbol at the instance
        return true;
    }

    // ===== SCENE =====

    void ReadVisualScenes(const XMLElement* root) {
        const XMLElement* library = root->FirstChildElement("library_visual_scenes");
        if (!library) {
            // No scene: place every mesh at the origin so the geometry is not
            // lost. A .dae without a visual scene is unusual but legal.
            options_.Warn("COLLADA: no <library_visual_scenes>; meshes are placed at the origin");
            for (size_t i = 0; i < document_->Meshes.size(); ++i) {
                ModelNode node;
                node.Name = document_->Meshes[i].Name;
                node.Mesh = static_cast<int>(i);
                document_->AddNode(std::move(node));
            }
            ApplyPendingSymbols();
            return;
        }

        std::string wanted;
        if (const XMLElement* scene = root->FirstChildElement("scene"))
            if (const XMLElement* instance = scene->FirstChildElement("instance_visual_scene"))
                wanted = ResolveReference(Attribute(instance, "url"));

        const XMLElement* chosen = nullptr;
        for (const XMLElement* scene = library->FirstChildElement("visual_scene"); scene;
             scene = scene->NextSiblingElement("visual_scene")) {
            if (!chosen) chosen = scene;
            if (!wanted.empty() && Attribute(scene, "id") == wanted) { chosen = scene; break; }
        }
        if (!chosen) return;

        if (const char* name = chosen->Attribute("name"))
            if (document_->Title.empty()) document_->Title = name;

        for (const XMLElement* node = chosen->FirstChildElement("node"); node;
             node = node->NextSiblingElement("node"))
            ReadNode(node, -1);

        ApplyPendingSymbols();
    }

    // A node's transform is the ordered product of its transform elements, so
    // they are read in document order rather than by name.
    static Matrix4x4 ReadNodeTransform(const XMLElement* node) {
        Matrix4x4 result = Matrix4x4::Identity();
        for (const XMLElement* child = node->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const std::string kind = child->Name();
            const std::vector<double> values = ParseNumbers(child);

            if (kind == "matrix" && values.size() >= 16) {
                // COLLADA writes row-major; the document stores column-major.
                Matrix4x4 m;
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        m.m[column * 4 + row] = values[static_cast<size_t>(row * 4 + column)];
                result = result * m;
            } else if (kind == "translate" && values.size() >= 3) {
                result = result * Matrix4x4::Translation(Vec3d(values[0], values[1], values[2]));
            } else if (kind == "scale" && values.size() >= 3) {
                result = result * Matrix4x4::Scaling(Vec3d(values[0], values[1], values[2]));
            } else if (kind == "rotate" && values.size() >= 4) {
                // Axis and angle, the angle in degrees.
                const Vec3d axis(values[0], values[1], values[2]);
                if (axis.Length() > 1e-12 && std::fabs(values[3]) > 1e-12)
                    result = result * Matrix4x4::FromQuaternion(
                            Quatd::FromAxisAngle(axis, values[3] * kPi / 180.0));
            }
        }
        return result;
    }

    void ReadNode(const XMLElement* element, int parent) {
        ModelNode node;
        const char* name = element->Attribute("name");
        node.Name = name ? name : Attribute(element, "id");

        const Matrix4x4 transform = ReadNodeTransform(element);
        Vec3d translation, scale;
        Quatd rotation;
        if (transform.DecomposeTRS(translation, rotation, scale)) {
            node.Translation = translation;
            node.Rotation = rotation;
            node.Scale = scale;
        } else if (!transform.IsIdentity(1e-15)) {
            node.Matrix = transform;
        }

        const XMLElement* instance = element->FirstChildElement("instance_geometry");
        std::string geometryId = instance ? ResolveReference(Attribute(instance, "url")) : "";
        if (!geometryId.empty()) {
            auto found = meshIndexById_.find(geometryId);
            if (found != meshIndexById_.end()) node.Mesh = found->second;
        }

        const int index = document_->AddNode(std::move(node), parent);
        nodeIndexById_[Attribute(element, "id")] = index;
        const char* sid = element->Attribute("sid");
        if (sid) nodeIndexBySid_[sid] = index;

        if (instance) BindMaterials(instance, index);

        if (element->FirstChildElement("instance_controller") && !warnedController_) {
            options_.Warn("COLLADA: <instance_controller> found — skinning and morph controllers "
                          "are not read, so a skinned mesh arrives in its bind pose");
            warnedController_ = true;
        }
        if (element->FirstChildElement("instance_node") && !warnedInstanceNode_) {
            options_.Warn("COLLADA: <instance_node> found — library_nodes instancing is not read");
            warnedInstanceNode_ = true;
        }

        for (const XMLElement* child = element->FirstChildElement("node"); child;
             child = child->NextSiblingElement("node"))
            ReadNode(child, index);
    }

    void BindMaterials(const XMLElement* instance, int nodeIndex) {
        const XMLElement* bind = instance->FirstChildElement("bind_material");
        if (!bind) return;
        const XMLElement* common = bind->FirstChildElement("technique_common");
        if (!common) return;

        const int meshIndex = document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh;
        if (meshIndex < 0 || meshIndex >= static_cast<int>(document_->Meshes.size())) return;

        auto symbols = symbolsByMesh_.find(meshIndex);
        if (symbols == symbolsByMesh_.end()) return;

        for (const XMLElement* material = common->FirstChildElement("instance_material"); material;
             material = material->NextSiblingElement("instance_material")) {
            const std::string symbol = Attribute(material, "symbol");
            const std::string target = ResolveReference(Attribute(material, "target"));
            auto found = materialIndexById_.find(target);
            if (found == materialIndexById_.end()) continue;

            auto& primitives = document_->Meshes[static_cast<size_t>(meshIndex)].Primitives;
            for (size_t i = 0; i < primitives.size(); ++i) {
                if (i >= symbols->second.size()) break;
                if (symbols->second[i] == symbol) primitives[i].Material = found->second;
            }
        }
    }

    // Any primitive still unbound after the scene walk takes the material its
    // own symbol names directly — what a file with no <bind_material> means,
    // since a symbol and a material id are usually the same string.
    void ApplyPendingSymbols() {
        for (auto& entry : symbolsByMesh_) {
            auto& primitives = document_->Meshes[static_cast<size_t>(entry.first)].Primitives;
            for (size_t i = 0; i < primitives.size() && i < entry.second.size(); ++i) {
                if (primitives[i].Material >= 0 || entry.second[i].empty()) continue;
                auto found = materialIndexById_.find(entry.second[i]);
                if (found != materialIndexById_.end()) primitives[i].Material = found->second;
            }
        }
    }

    // ===== ANIMATION =====

    void ReadAnimations(const XMLElement* library) {
        if (!library) return;
        ModelAnimation animation;
        animation.Name = "animation";
        CollectAnimations(library, animation);
        if (!animation.Channels.empty()) document_->Animations.push_back(std::move(animation));
    }

    void CollectAnimations(const XMLElement* parent, ModelAnimation& animation) {
        for (const XMLElement* element = parent->FirstChildElement("animation"); element;
             element = element->NextSiblingElement("animation")) {
            ReadAnimation(element, animation);
            CollectAnimations(element, animation);   // COLLADA nests them
        }
    }

    void ReadAnimation(const XMLElement* element, ModelAnimation& animation) {
        std::map<std::string, SourceData> sources;
        std::map<std::string, std::vector<std::string>> nameSources;
        for (const XMLElement* source = element->FirstChildElement("source"); source;
             source = source->NextSiblingElement("source")) {
            SourceData data;
            data.Values = ParseNumbers(source->FirstChildElement("float_array"));
            if (const XMLElement* common = source->FirstChildElement("technique_common"))
                if (const XMLElement* accessor = common->FirstChildElement("accessor"))
                    data.Stride = std::max(1, accessor->IntAttribute("stride", 1));
            if (!data.Values.empty()) sources[Attribute(source, "id")] = std::move(data);
            const std::vector<std::string> names = ParseWords(source->FirstChildElement("Name_array"));
            if (!names.empty()) nameSources[Attribute(source, "id")] = names;
        }

        std::map<std::string, std::map<std::string, std::string>> samplerInputs;
        for (const XMLElement* sampler = element->FirstChildElement("sampler"); sampler;
             sampler = sampler->NextSiblingElement("sampler")) {
            std::map<std::string, std::string> bySemantic;
            for (const XMLElement* input = sampler->FirstChildElement("input"); input;
                 input = input->NextSiblingElement("input"))
                bySemantic[Attribute(input, "semantic")] = ResolveReference(Attribute(input, "source"));
            samplerInputs[Attribute(sampler, "id")] = std::move(bySemantic);
        }

        for (const XMLElement* channel = element->FirstChildElement("channel"); channel;
             channel = channel->NextSiblingElement("channel")) {
            const std::string samplerId = ResolveReference(Attribute(channel, "source"));
            const std::string target = Attribute(channel, "target");
            auto sampler = samplerInputs.find(samplerId);
            if (sampler == samplerInputs.end()) continue;

            const size_t slash = target.find('/');
            if (slash == std::string::npos) continue;
            const std::string nodeName = target.substr(0, slash);
            const std::string member = target.substr(slash + 1);

            int nodeIndex = -1;
            auto byId = nodeIndexById_.find(nodeName);
            if (byId != nodeIndexById_.end()) nodeIndex = byId->second;
            else {
                auto bySid = nodeIndexBySid_.find(nodeName);
                if (bySid != nodeIndexBySid_.end()) nodeIndex = bySid->second;
            }
            if (nodeIndex < 0) continue;

            auto timesIt = sources.find(sampler->second["INPUT"]);
            auto valuesIt = sources.find(sampler->second["OUTPUT"]);
            if (timesIt == sources.end() || valuesIt == sources.end()) continue;

            Interpolation interpolation = Interpolation::Linear;
            auto interpolationIt = nameSources.find(sampler->second["INTERPOLATION"]);
            if (interpolationIt != nameSources.end() && !interpolationIt->second.empty()) {
                const std::string& first = interpolationIt->second.front();
                if (first == "STEP") interpolation = Interpolation::Step;
                else if (first == "BEZIER" || first == "HERMITE") {
                    interpolation = Interpolation::Linear;
                    if (!warnedBezier_) {
                        options_.Warn("COLLADA: BEZIER/HERMITE animation interpolation is read as "
                                      "LINEAR; the tangents are not carried");
                        warnedBezier_ = true;
                    }
                }
            }

            const std::vector<double>& times = timesIt->second.Values;
            const SourceData& values = valuesIt->second;

            // The common case by far: the channel drives the whole 4x4. The
            // document animates translation, rotation and scale, so each
            // keyframe matrix is decomposed into the three.
            if (values.Stride == 16 || member == "transform" || member == "matrix") {
                EmitMatrixChannels(nodeIndex, times, values, interpolation, animation);
                continue;
            }
            EmitComponentChannel(nodeIndex, member, times, values, interpolation, animation);
        }
    }

    void EmitMatrixChannels(int nodeIndex, const std::vector<double>& times,
                            const SourceData& values, Interpolation interpolation,
                            ModelAnimation& animation) {
        const size_t keys = std::min(times.size(), values.Values.size() / 16);
        if (keys == 0) return;

        AnimationSampler translationSampler, rotationSampler, scaleSampler;
        translationSampler.Interpolate = interpolation;
        rotationSampler.Interpolate = interpolation;
        scaleSampler.Interpolate = interpolation;

        bool warnedShear = false;
        for (size_t key = 0; key < keys; ++key) {
            Matrix4x4 matrix;
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    matrix.m[column * 4 + row] =
                            values.Values[key * 16 + static_cast<size_t>(row * 4 + column)];

            Vec3d translation, scale;
            Quatd rotation;
            if (!matrix.DecomposeTRS(translation, rotation, scale) && !warnedShear) {
                options_.Warn("COLLADA: an animated matrix carries shear, which translation, "
                              "rotation and scale cannot express; the shear is dropped");
                warnedShear = true;
            }

            const float time = static_cast<float>(times[key]);
            translationSampler.Times.push_back(time);
            rotationSampler.Times.push_back(time);
            scaleSampler.Times.push_back(time);
            for (double value : {translation.x, translation.y, translation.z})
                translationSampler.Values.push_back(static_cast<float>(value));
            for (double value : {rotation.x, rotation.y, rotation.z, rotation.w})
                rotationSampler.Values.push_back(static_cast<float>(value));
            for (double value : {scale.x, scale.y, scale.z})
                scaleSampler.Values.push_back(static_cast<float>(value));
        }

        const std::pair<AnimationPath, AnimationSampler*> channels[] = {
                {AnimationPath::Translation, &translationSampler},
                {AnimationPath::Rotation, &rotationSampler},
                {AnimationPath::Scale, &scaleSampler}};
        for (const auto& entry : channels) {
            AnimationChannel channel;
            channel.TargetNode = nodeIndex;
            channel.Path = entry.first;
            channel.Sampler = static_cast<int>(animation.Samplers.size());
            animation.Samplers.push_back(std::move(*entry.second));
            animation.Channels.push_back(channel);
        }
    }

    void EmitComponentChannel(int nodeIndex, const std::string& member,
                              const std::vector<double>& times, const SourceData& values,
                              Interpolation interpolation, ModelAnimation& animation) {
        AnimationPath path;
        if (member.rfind("location", 0) == 0 || member.rfind("translate", 0) == 0)
            path = AnimationPath::Translation;
        else if (member.rfind("scale", 0) == 0)
            path = AnimationPath::Scale;
        else if (member.rfind("rotation", 0) == 0 || member.rfind("rotate", 0) == 0) {
            // A per-axis angle track, which the document has no form for: it
            // stores whole rotations as quaternions. Combining several axis
            // tracks into one quaternion track needs all of them together.
            if (!warnedAxisRotation_) {
                options_.Warn("COLLADA: per-axis rotation tracks (rotationX/Y/Z) are not read; "
                              "the document animates whole rotations, and only matrix-valued "
                              "channels can be decomposed into them");
                warnedAxisRotation_ = true;
            }
            return;
        } else {
            return;
        }

        const int components = path == AnimationPath::Rotation ? 4 : 3;
        if (values.Stride < components) return;
        const size_t keys = std::min(times.size(), values.Count());
        if (keys == 0) return;

        AnimationSampler sampler;
        sampler.Interpolate = interpolation;
        for (size_t key = 0; key < keys; ++key) {
            sampler.Times.push_back(static_cast<float>(times[key]));
            for (int c = 0; c < components; ++c)
                sampler.Values.push_back(static_cast<float>(values.At(key, c)));
        }

        AnimationChannel channel;
        channel.TargetNode = nodeIndex;
        channel.Path = path;
        channel.Sampler = static_cast<int>(animation.Samplers.size());
        animation.Samplers.push_back(std::move(sampler));
        animation.Channels.push_back(channel);
    }

    // ===== FINISHING =====

    void WarnAboutUnread(const XMLElement* root) {
        struct Library { const char* Element; const char* Description; };
        static const Library libraries[] = {
                {"library_controllers", "skin and morph controllers"},
                {"library_cameras", "cameras"},
                {"library_lights", "lights"},
                {"library_physics_models", "physics models"}};
        for (const Library& library : libraries) {
            const XMLElement* element = root->FirstChildElement(library.Element);
            if (element && element->FirstChildElement())
                options_.Warn(std::string("COLLADA: <") + library.Element + "> is not read (" +
                              library.Description + ")");
        }
    }

    void Finish() {
        if (options_.GenerateMissingNormals) {
            for (auto& mesh : document_->Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty() && prim.Mode != PrimitiveMode::Lines &&
                        prim.Mode != PrimitiveMode::Points)
                        prim.RecomputeNormals();
        }
        // Normals arriving through a per-corner index stream can be shorter
        // than the vertex list when a source is malformed; pad rather than
        // leave the arrays out of step.
        for (auto& mesh : document_->Meshes)
            for (auto& prim : mesh.Primitives)
                if (!prim.Normals.empty() && prim.Normals.size() != prim.Positions.size())
                    prim.Normals.resize(prim.Positions.size(), Vec3f(0.0f, 0.0f, 1.0f));

        if (options_.WeldTolerance > 0.0) document_->WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) document_->TriangulateAll();
        if (options_.ForceUpAxis.has_value()) document_->ConvertUpAxis(*options_.ForceUpAxis);
    }

    const ConversionOptions& options_;
    std::shared_ptr<ModelDocument> document_;

    std::map<std::string, ModelMaterial> effectMaterials_;
    std::map<std::string, int> materialIndexById_;
    std::map<std::string, int> imageIndexById_;
    std::map<std::string, int> meshIndexById_;
    std::map<std::string, int> nodeIndexById_;
    std::map<std::string, int> nodeIndexBySid_;
    std::map<int, std::vector<std::string>> symbolsByMesh_;

    bool warnedController_ = false;
    bool warnedInstanceNode_ = false;
    bool warnedBezier_ = false;
    bool warnedAxisRotation_ = false;
};

bool LooksLikeCollada(const std::string& head) {
    return head.find("<COLLADA") != std::string::npos ||
           head.find("COLLADASchema") != std::string::npos;
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities ColladaConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;              // <polylist> vcount and <polygons>
    caps.Lines = true;              // <lines>
    caps.SceneGraph = true;
    caps.Instancing = true;         // one geometry instanced by several nodes
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.VertexColors = true;
    caps.Normals = true;
    caps.Animations = true;         // matrix and TRS channels
    caps.Units = true;              // <unit meter=>
    caps.UpAxis = true;             // <up_axis>
    caps.Metadata = true;
    caps.DoublePrecision = true;    // coordinates are decimal text, read into double
    // Deliberately false: no PBR materials (profile_COMMON is fixed-function),
    // no skinning or morph targets (library_controllers is not read), no
    // cameras or lights, no tangents, no embedded textures.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> ColladaConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    XMLDocument xml;
    if (xml.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
        options.Warn(std::string("COLLADA: cannot parse ") + filename + " — " + xml.ErrorStr());
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

std::shared_ptr<ModelStorage::ModelDocument> ColladaConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    XMLDocument xml;
    if (xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) !=
        tinyxml2::XML_SUCCESS) {
        options.Warn(std::string("COLLADA: cannot parse the data — ") + xml.ErrorStr());
        return nullptr;
    }
    Reader reader(options);
    return reader.Run(xml);
}

std::shared_ptr<ModelStorage::ModelDocument> ColladaConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return ImportFromMemory(std::vector<uint8_t>(text.begin(), text.end()), options);
}

bool ColladaConverter::ValidateData(const std::vector<uint8_t>& data) const {
    const size_t limit = std::min<size_t>(data.size(), 4096);
    return LooksLikeCollada(
            std::string(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit)));
}

bool ColladaConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<char> head(4096);
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    return LooksLikeCollada(std::string(head.data(),
                                        static_cast<size_t>(std::max<std::streamsize>(0, file.gcount()))));
}

} // namespace ModelConverter
} // namespace UltraCanvas
