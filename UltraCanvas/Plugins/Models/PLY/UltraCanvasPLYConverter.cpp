// Plugins/Models/PLY/UltraCanvasPLYConverter.cpp
// Stanford PLY reading and writing. Declared in UltraCanvasPLYConverter.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/PLY/UltraCanvasPLYConverter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

// ===== TYPES =====

enum class Scalar {
    Int8, Uint8, Int16, Uint16, Int32, Uint32, Float32, Float64, Unknown
};

bool IsIntegerType(Scalar type) {
    return type != Scalar::Float32 && type != Scalar::Float64 && type != Scalar::Unknown;
}

// PLY spells its types two ways, and files in the wild use both - the short
// names came first, the sized ones were added for clarity.
Scalar ScalarFromName(const std::string& name) {
    if (name == "char"   || name == "int8")    return Scalar::Int8;
    if (name == "uchar"  || name == "uint8")   return Scalar::Uint8;
    if (name == "short"  || name == "int16")   return Scalar::Int16;
    if (name == "ushort" || name == "uint16")  return Scalar::Uint16;
    if (name == "int"    || name == "int32")   return Scalar::Int32;
    if (name == "uint"   || name == "uint32")  return Scalar::Uint32;
    if (name == "float"  || name == "float32") return Scalar::Float32;
    if (name == "double" || name == "float64") return Scalar::Float64;
    return Scalar::Unknown;
}

struct Property {
    std::string Name;
    Scalar Type = Scalar::Float32;
    bool IsList = false;
    Scalar CountType = Scalar::Uint8;   // list only
};

struct Element {
    std::string Name;
    size_t Count = 0;
    std::vector<Property> Properties;
};

enum class Encoding { Ascii, BinaryLittleEndian, BinaryBigEndian };

// ===== BINARY READING =====

// A cursor over the file's bytes that refuses to read past the end. A PLY
// header can claim a million elements a truncated file does not contain, and
// the difference between noticing and not is a crash.
class Cursor {
public:
    Cursor(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    size_t Offset() const { return at_; }
    void Seek(size_t at) { at_ = at; }
    bool Exhausted() const { return at_ >= size_; }
    bool Remaining(size_t bytes) const { return at_ + bytes <= size_; }

    bool Read(void* out, size_t bytes) {
        if (!Remaining(bytes)) { at_ = size_; return false; }
        std::memcpy(out, data_ + at_, bytes);
        at_ += bytes;
        return true;
    }

    // One line, without its terminator. PLY headers are always ASCII even in a
    // binary file, and may end \n or \r\n.
    bool ReadLine(std::string& out) {
        if (at_ >= size_) return false;
        const size_t start = at_;
        while (at_ < size_ && data_[at_] != '\n') ++at_;
        size_t end = at_;
        if (end > start && data_[end - 1] == '\r') --end;
        out.assign(reinterpret_cast<const char*>(data_ + start), end - start);
        if (at_ < size_) ++at_;   // step over the newline
        return true;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t at_ = 0;
};

template <typename T>
T ByteSwap(T value) {
    uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    for (size_t i = 0; i < sizeof(T) / 2; ++i) std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
    T out;
    std::memcpy(&out, bytes, sizeof(T));
    return out;
}

// Every scalar is widened to double on the way in. PLY's value range is small
// enough that this is lossless for everything but a full-range int64, which
// the format does not have, and it collapses eight reader paths into one.
bool ReadBinaryScalar(Cursor& cursor, Scalar type, bool swap, double& out) {
    switch (type) {
        case Scalar::Int8:   { int8_t v;   if (!cursor.Read(&v, 1)) return false; out = v; return true; }
        case Scalar::Uint8:  { uint8_t v;  if (!cursor.Read(&v, 1)) return false; out = v; return true; }
        case Scalar::Int16:  { int16_t v;  if (!cursor.Read(&v, 2)) return false; out = swap ? ByteSwap(v) : v; return true; }
        case Scalar::Uint16: { uint16_t v; if (!cursor.Read(&v, 2)) return false; out = swap ? ByteSwap(v) : v; return true; }
        case Scalar::Int32:  { int32_t v;  if (!cursor.Read(&v, 4)) return false; out = swap ? ByteSwap(v) : v; return true; }
        case Scalar::Uint32: { uint32_t v; if (!cursor.Read(&v, 4)) return false; out = swap ? ByteSwap(v) : v; return true; }
        case Scalar::Float32:{ float v;    if (!cursor.Read(&v, 4)) return false; out = swap ? ByteSwap(v) : v; return true; }
        case Scalar::Float64:{ double v;   if (!cursor.Read(&v, 8)) return false; out = swap ? ByteSwap(v) : v; return true; }
        default: return false;
    }
}

// ===== ASCII READING =====

// A tokeniser over the whole ASCII body. PLY does not promise one element per
// line - the specification says whitespace-separated - so counting values is
// the only correct way to walk it, and plenty of exporters do wrap long faces.
class AsciiTokens {
public:
    AsciiTokens(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool Next(double& out) {
        while (at_ < size_ && std::isspace(static_cast<unsigned char>(data_[at_]))) ++at_;
        if (at_ >= size_) return false;
        const size_t start = at_;
        while (at_ < size_ && !std::isspace(static_cast<unsigned char>(data_[at_]))) ++at_;
        const std::string token(reinterpret_cast<const char*>(data_ + start), at_ - start);
        out = std::strtod(token.c_str(), nullptr);
        return true;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t at_ = 0;
};

// ===== NAME RECOGNITION =====

std::string Lowered(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Where each meaningful vertex property sits, or -1.
struct VertexLayout {
    int X = -1, Y = -1, Z = -1;
    int NX = -1, NY = -1, NZ = -1;
    int U = -1, V = -1;
    int Red = -1, Green = -1, Blue = -1, Alpha = -1;
    bool HasPosition() const { return X >= 0 && Y >= 0 && Z >= 0; }
    bool HasNormal() const { return NX >= 0 && NY >= 0 && NZ >= 0; }
    bool HasUV() const { return U >= 0 && V >= 0; }
    bool HasColor() const { return Red >= 0 && Green >= 0 && Blue >= 0; }
};

VertexLayout LayoutOf(const Element& element) {
    VertexLayout layout;
    for (size_t i = 0; i < element.Properties.size(); ++i) {
        const std::string name = Lowered(element.Properties[i].Name);
        const int index = static_cast<int>(i);
        if (name == "x") layout.X = index;
        else if (name == "y") layout.Y = index;
        else if (name == "z") layout.Z = index;
        else if (name == "nx") layout.NX = index;
        else if (name == "ny") layout.NY = index;
        else if (name == "nz") layout.NZ = index;
        // Three spellings of the same thing, all of them common.
        else if (name == "s" || name == "u" || name == "texture_u" || name == "tu") layout.U = index;
        else if (name == "t" || name == "v" || name == "texture_v" || name == "tv") layout.V = index;
        else if (name == "red"   || name == "r") layout.Red = index;
        else if (name == "green" || name == "g") layout.Green = index;
        else if (name == "blue"  || name == "b") layout.Blue = index;
        else if (name == "alpha" || name == "a") layout.Alpha = index;
    }
    return layout;
}

bool IsIndexProperty(const Property& property) {
    const std::string name = Lowered(property.Name);
    return property.IsList &&
           (name == "vertex_indices" || name == "vertex_index" || name == "vertexindices");
}

} // namespace

// ===== READER =====

std::shared_ptr<ModelDocument> PLYConverter::ImportFromMemory(const std::vector<uint8_t>& data,
                                                              const ConversionOptions& options) {
    if (!ValidateData(data)) {
        options.Warn("PLY: the file does not begin with the 'ply' magic");
        return nullptr;
    }

    Cursor cursor(data.data(), data.size());

    // --- header ---
    std::string line;
    cursor.ReadLine(line);   // "ply"

    Encoding encoding = Encoding::Ascii;
    std::vector<Element> elements;
    std::string comments;
    bool sawFormat = false;
    bool headerEnded = false;

    while (cursor.ReadLine(line)) {
        std::istringstream words(line);
        std::string keyword;
        if (!(words >> keyword)) continue;

        if (keyword == "format") {
            std::string name;
            words >> name;
            if (name == "ascii") encoding = Encoding::Ascii;
            else if (name == "binary_little_endian") encoding = Encoding::BinaryLittleEndian;
            else if (name == "binary_big_endian") encoding = Encoding::BinaryBigEndian;
            else {
                options.Warn("PLY: unknown format '" + name + "'");
                return nullptr;
            }
            sawFormat = true;
        } else if (keyword == "comment" || keyword == "obj_info") {
            std::string rest;
            std::getline(words, rest);
            if (!rest.empty()) {
                if (!comments.empty()) comments += "\n";
                comments += rest.substr(rest.find_first_not_of(' ') == std::string::npos
                                               ? 0 : rest.find_first_not_of(' '));
            }
        } else if (keyword == "element") {
            Element element;
            long long count = 0;
            words >> element.Name >> count;
            element.Count = count > 0 ? static_cast<size_t>(count) : 0;
            elements.push_back(std::move(element));
        } else if (keyword == "property") {
            if (elements.empty()) continue;
            Property property;
            std::string type;
            words >> type;
            if (type == "list") {
                std::string countType, valueType;
                words >> countType >> valueType >> property.Name;
                property.IsList = true;
                property.CountType = ScalarFromName(countType);
                property.Type = ScalarFromName(valueType);
            } else {
                words >> property.Name;
                property.Type = ScalarFromName(type);
            }
            if (property.Type == Scalar::Unknown) {
                options.Warn("PLY: property '" + property.Name + "' has an unknown type and the "
                             "file cannot be walked past it");
                return nullptr;
            }
            elements.back().Properties.push_back(std::move(property));
        } else if (keyword == "end_header") {
            headerEnded = true;
            break;
        }
    }

    if (!sawFormat || !headerEnded) {
        options.Warn("PLY: the header is incomplete");
        return nullptr;
    }

    // --- body ---
    const bool binary = encoding != Encoding::Ascii;
    // Detect the host's order once rather than per value.
    const uint16_t probe = 1;
    const bool hostIsLittleEndian = *reinterpret_cast<const uint8_t*>(&probe) == 1;
    const bool swap = binary &&
                      ((encoding == Encoding::BinaryLittleEndian) != hostIsLittleEndian);

    AsciiTokens tokens(data.data() + cursor.Offset(), data.size() - cursor.Offset());

    auto readScalar = [&](Scalar type, double& out) -> bool {
        return binary ? ReadBinaryScalar(cursor, type, swap, out) : tokens.Next(out);
    };

    std::vector<Vec3d> positions;
    std::vector<Vec3f> normals;
    std::vector<float> uvs;
    std::vector<float> colors;
    // Anything the format carried that this reader has no meaning for. PLY is
    // the reason ModelDocument has these at all.
    std::vector<VertexAttribute> extras;
    std::vector<int> extraFor;              // property index -> slot in `extras`
    std::vector<uint32_t> indices;
    std::vector<uint32_t> faceStarts;
    size_t maxCorners = 0;
    size_t degenerateFaces = 0;
    size_t truncated = 0;

    for (const Element& element : elements) {
        const std::string name = Lowered(element.Name);

        if (name == "vertex") {
            const VertexLayout layout = LayoutOf(element);
            if (!layout.HasPosition()) {
                options.Warn("PLY: the vertex element has no x/y/z and cannot be read");
                return nullptr;
            }
            // A colour stored as float is already 0..1; as an integer it is
            // 0..255 and has to be scaled, or every model comes out white.
            const bool colorIsInteger = layout.HasColor() &&
                                        IsIntegerType(element.Properties[
                                                static_cast<size_t>(layout.Red)].Type);

            extraFor.assign(element.Properties.size(), -1);
            for (size_t p = 0; p < element.Properties.size(); ++p) {
                const Property& property = element.Properties[p];
                if (property.IsList) continue;   // a per-vertex list has no home here
                const int index = static_cast<int>(p);
                if (index == layout.X || index == layout.Y || index == layout.Z ||
                    index == layout.NX || index == layout.NY || index == layout.NZ ||
                    index == layout.U || index == layout.V ||
                    index == layout.Red || index == layout.Green ||
                    index == layout.Blue || index == layout.Alpha)
                    continue;
                VertexAttribute attribute;
                attribute.Semantic = AttributeSemantic::Custom;
                attribute.Name = property.Name;
                attribute.Components = 1;
                attribute.Values.reserve(element.Count);
                extraFor[p] = static_cast<int>(extras.size());
                extras.push_back(std::move(attribute));
            }

            positions.reserve(element.Count);
            if (layout.HasNormal()) normals.reserve(element.Count);
            if (layout.HasUV()) uvs.reserve(element.Count * 2);
            if (layout.HasColor()) colors.reserve(element.Count * 4);

            std::vector<double> values(element.Properties.size(), 0.0);
            for (size_t v = 0; v < element.Count; ++v) {
                bool complete = true;
                for (size_t p = 0; p < element.Properties.size(); ++p) {
                    const Property& property = element.Properties[p];
                    if (property.IsList) {
                        double count = 0.0;
                        if (!readScalar(property.CountType, count)) { complete = false; break; }
                        for (long long k = 0; k < static_cast<long long>(count); ++k) {
                            double ignored = 0.0;
                            if (!readScalar(property.Type, ignored)) { complete = false; break; }
                        }
                        values[p] = 0.0;
                    } else if (!readScalar(property.Type, values[p])) {
                        complete = false;
                    }
                    if (!complete) break;
                }
                if (!complete) { ++truncated; break; }

                positions.emplace_back(values[static_cast<size_t>(layout.X)],
                                       values[static_cast<size_t>(layout.Y)],
                                       values[static_cast<size_t>(layout.Z)]);
                if (layout.HasNormal())
                    normals.emplace_back(static_cast<float>(values[static_cast<size_t>(layout.NX)]),
                                         static_cast<float>(values[static_cast<size_t>(layout.NY)]),
                                         static_cast<float>(values[static_cast<size_t>(layout.NZ)]));
                if (layout.HasUV()) {
                    uvs.push_back(static_cast<float>(values[static_cast<size_t>(layout.U)]));
                    uvs.push_back(static_cast<float>(values[static_cast<size_t>(layout.V)]));
                }
                if (layout.HasColor()) {
                    const double scale = colorIsInteger ? 1.0 / 255.0 : 1.0;
                    colors.push_back(static_cast<float>(values[static_cast<size_t>(layout.Red)] * scale));
                    colors.push_back(static_cast<float>(values[static_cast<size_t>(layout.Green)] * scale));
                    colors.push_back(static_cast<float>(values[static_cast<size_t>(layout.Blue)] * scale));
                    colors.push_back(layout.Alpha >= 0
                            ? static_cast<float>(values[static_cast<size_t>(layout.Alpha)] * scale)
                            : 1.0f);
                }
                for (size_t p = 0; p < extraFor.size(); ++p)
                    if (extraFor[p] >= 0)
                        extras[static_cast<size_t>(extraFor[p])].Values.push_back(
                                static_cast<float>(values[p]));
            }

        } else if (name == "face") {
            int indexProperty = -1;
            for (size_t p = 0; p < element.Properties.size(); ++p)
                if (IsIndexProperty(element.Properties[p])) indexProperty = static_cast<int>(p);
            if (indexProperty < 0) {
                options.Warn("PLY: the face element carries no vertex_indices list; its faces "
                             "were skipped");
            }

            faceStarts.reserve(element.Count);
            for (size_t f = 0; f < element.Count; ++f) {
                bool complete = true;
                std::vector<uint32_t> corners;
                for (size_t p = 0; p < element.Properties.size(); ++p) {
                    const Property& property = element.Properties[p];
                    if (property.IsList) {
                        double count = 0.0;
                        if (!readScalar(property.CountType, count)) { complete = false; break; }
                        const long long total = static_cast<long long>(count);
                        for (long long k = 0; k < total; ++k) {
                            double value = 0.0;
                            if (!readScalar(property.Type, value)) { complete = false; break; }
                            if (static_cast<int>(p) == indexProperty && value >= 0.0)
                                corners.push_back(static_cast<uint32_t>(value));
                        }
                        if (!complete) break;
                    } else {
                        double ignored = 0.0;
                        if (!readScalar(property.Type, ignored)) { complete = false; break; }
                    }
                }
                if (!complete) { ++truncated; break; }
                if (corners.size() < 3) { ++degenerateFaces; continue; }

                bool inRange = true;
                for (uint32_t corner : corners)
                    if (corner >= positions.size()) inRange = false;
                if (!inRange) { ++degenerateFaces; continue; }

                maxCorners = std::max(maxCorners, corners.size());
                faceStarts.push_back(static_cast<uint32_t>(indices.size()));
                indices.insert(indices.end(), corners.begin(), corners.end());
            }

        } else {
            // Some other element - an edge list, a per-face material table.
            // Nothing here wants it, but every one of its bytes must still be
            // consumed: in a binary file a mis-sized skip does not lose this
            // element, it destroys everything after it.
            bool complete = true;
            for (size_t i = 0; i < element.Count && complete; ++i)
                for (const Property& property : element.Properties) {
                    if (property.IsList) {
                        double count = 0.0;
                        if (!readScalar(property.CountType, count)) { complete = false; break; }
                        for (long long k = 0; k < static_cast<long long>(count); ++k) {
                            double ignored = 0.0;
                            if (!readScalar(property.Type, ignored)) { complete = false; break; }
                        }
                    } else {
                        double ignored = 0.0;
                        if (!readScalar(property.Type, ignored)) { complete = false; break; }
                    }
                }
            if (element.Count > 0)
                options.Warn("PLY: element '" + element.Name + "' (" +
                             std::to_string(element.Count) + ") is not read, only stepped over");
            if (!complete) { ++truncated; break; }
        }
    }

    if (positions.empty()) {
        options.Warn("PLY: the file declares no vertices");
        return nullptr;
    }
    if (truncated)
        options.Warn("PLY: the file ends before the header's element counts are satisfied; "
                     "what was read is kept");
    if (degenerateFaces)
        options.Warn("PLY: " + std::to_string(degenerateFaces) +
                     " face(s) had fewer than three corners or an index outside the vertex "
                     "array, and were dropped");

    // --- assemble ---
    MeshPrimitive prim;
    prim.Positions = std::move(positions);
    prim.Normals = std::move(normals);

    if (indices.empty()) {
        // Vertices and no faces: a point cloud, which is what PLY is most often
        // used for outside of research meshes.
        prim.Mode = PrimitiveMode::Points;
    } else if (maxCorners <= 3) {
        prim.Mode = PrimitiveMode::Triangles;
        prim.Indices = std::move(indices);
    } else {
        prim.Mode = PrimitiveMode::Polygons;
        prim.Indices = std::move(indices);
        prim.FaceStarts = std::move(faceStarts);
        prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
    }

    if (!uvs.empty()) {
        VertexAttribute uv;
        uv.Semantic = AttributeSemantic::TexCoord;
        uv.Name = "TEXCOORD_0";
        uv.Components = 2;
        uv.Values = std::move(uvs);
        prim.Attributes.push_back(std::move(uv));
    }
    if (!colors.empty()) {
        VertexAttribute color;
        color.Semantic = AttributeSemantic::Color;
        color.Name = "COLOR_0";
        color.Components = 4;
        color.Values = std::move(colors);
        prim.Attributes.push_back(std::move(color));
    }
    for (VertexAttribute& attribute : extras)
        if (attribute.Values.size() == prim.Positions.size())
            prim.Attributes.push_back(std::move(attribute));

    if (prim.Normals.empty() && options.GenerateMissingNormals &&
        prim.Mode != PrimitiveMode::Points)
        prim.RecomputeNormals();

    ModelMesh mesh;
    mesh.Name = "ply";
    mesh.Primitives.push_back(std::move(prim));

    auto document = std::make_shared<ModelDocument>(ModelDocument::FromSingleMesh(std::move(mesh)));
    document->SourceFormat = "ply";
    document->Generator = options.Generator;
    // PLY states neither a unit nor an up axis. Y-up is the convention of the
    // tools that write it most; it is recorded as a convention, not a fact.
    document->Up = UpAxis::YUp;
    if (!comments.empty()) document->Metadata["comment"] = comments;
    document->Metadata["encoding"] = encoding == Encoding::Ascii ? "ascii"
                                   : encoding == Encoding::BinaryLittleEndian
                                             ? "binary_little_endian" : "binary_big_endian";

    if (options.WeldTolerance > 0.0) document->WeldVertices(options.WeldTolerance);
    if (options.TriangulateOnImport) document->TriangulateAll();
    if (options.ForceUpAxis.has_value()) document->ConvertUpAxis(*options.ForceUpAxis);
    return document;
}

std::shared_ptr<ModelDocument> PLYConverter::ImportFromStream(std::istream& stream,
                                                              const ConversionOptions& options) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string text = buffer.str();
    return ImportFromMemory(std::vector<uint8_t>(text.begin(), text.end()), options);
}

std::shared_ptr<ModelDocument> PLYConverter::Import(const std::string& filename,
                                                    const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("PLY: cannot open " + filename);
        return nullptr;
    }
    auto document = ImportFromStream(file, options);
    if (document) {
        const size_t slash = filename.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = base.find_last_of('.');
        document->Title = dot == std::string::npos ? base : base.substr(0, dot);
        if (!document->Meshes.empty()) document->Meshes[0].Name = document->Title;
    }
    return document;
}

// ===== WRITER =====

namespace {

// Everything the document holds, flattened into the one vertex list and one
// face list PLY has room for. PLY has no scene graph, so a document whose
// transforms are not baked would be written in the wrong place.
struct FlatMesh {
    std::vector<Vec3d> Positions;
    std::vector<Vec3f> Normals;
    std::vector<float> UVs;        // 2 per vertex
    std::vector<float> Colors;     // 4 per vertex
    std::vector<std::vector<uint32_t>> Faces;
    bool HasNormals = false, HasUVs = false, HasColors = false;
    bool AnyPoints = false;
};

FlatMesh Flatten(const ModelDocument& source, const ConversionOptions& options) {
    ModelDocument document = source;
    document.FlattenTransforms();

    FlatMesh flat;
    for (const ModelMesh& mesh : document.Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            if (prim.Positions.empty()) continue;
            const uint32_t base = static_cast<uint32_t>(flat.Positions.size());

            // Every vertex must carry the same properties, so a mesh that has
            // normals and one that does not are reconciled by padding rather
            // than by dropping the ones that do.
            if (!prim.Normals.empty()) flat.HasNormals = true;
            const VertexAttribute* uv = prim.FindAttribute(AttributeSemantic::TexCoord, 0);
            if (uv && uv->Components >= 2) flat.HasUVs = true;
            const VertexAttribute* color = prim.FindAttribute(AttributeSemantic::Color, 0);
            if (color && color->Components >= 3) flat.HasColors = true;

            for (size_t v = 0; v < prim.Positions.size(); ++v) {
                flat.Positions.push_back(prim.Positions[v]);
                flat.Normals.push_back(v < prim.Normals.size() ? prim.Normals[v]
                                                               : Vec3f(0.0f, 0.0f, 0.0f));
                if (uv && uv->Components >= 2 &&
                    v * static_cast<size_t>(uv->Components) + 1 < uv->Values.size()) {
                    flat.UVs.push_back(uv->Values[v * static_cast<size_t>(uv->Components)]);
                    flat.UVs.push_back(uv->Values[v * static_cast<size_t>(uv->Components) + 1]);
                } else {
                    flat.UVs.push_back(0.0f);
                    flat.UVs.push_back(0.0f);
                }
                for (int channel = 0; channel < 4; ++channel) {
                    float value = channel == 3 ? 1.0f : 1.0f;
                    if (color && channel < color->Components) {
                        const size_t at = v * static_cast<size_t>(color->Components) +
                                          static_cast<size_t>(channel);
                        if (at < color->Values.size()) value = color->Values[at];
                    }
                    flat.Colors.push_back(value);
                }
            }

            if (prim.Mode == PrimitiveMode::Points ||
                prim.Mode == PrimitiveMode::Lines || prim.Mode == PrimitiveMode::LineStrip ||
                prim.Mode == PrimitiveMode::LineLoop) {
                flat.AnyPoints = true;
                continue;
            }
            const size_t faces = prim.FaceCount();
            for (size_t f = 0; f < faces; ++f) {
                std::vector<uint32_t> corners = prim.Face(f);
                if (corners.size() < 3) continue;
                for (uint32_t& corner : corners) corner += base;
                flat.Faces.push_back(std::move(corners));
            }
        }

    if (flat.AnyPoints)
        options.Warn("PLY: point and line primitives have no face form; their vertices are "
                     "written and their connectivity is not");
    return flat;
}

} // namespace

bool PLYConverter::ExportToStream(const ModelDocument& document, std::ostream& stream,
                                  const ConversionOptions& options) {
    const FlatMesh flat = Flatten(document, options);
    if (flat.Positions.empty()) {
        options.Warn("PLY: the document holds no vertices; nothing to write");
        return false;
    }

    const bool binary = options.PreferBinary;
    // Positions are double in the document, and PLY can hold either. Compact
    // writes float, which is what almost every PLY in existence carries and
    // half the size; Full writes double, which is the only way a document that
    // came from CAD - where a coordinate far from the origin has already lost
    // millimetres to float - survives the trip. That is the same thing
    // NumericPrecision means for the OBJ writer: enough to round-trip exactly.
    //
    // It also gives the flag an effect in binary, where digit counts mean
    // nothing and it would otherwise be silently ignored.
    const bool doublePositions = options.Precision == NumericPrecision::Full;
    const char* positionType = doublePositions ? "double" : "float";

    // Colours go out as uchar: it is what every reader expects, and a float
    // colour channel triples the size of a property nothing reads at more
    // than eight bits.
    stream << "ply\n";
    stream << "format " << (binary ? "binary_little_endian" : "ascii") << " 1.0\n";
    stream << "comment Written by " << (options.Generator.empty() ? "UltraCanvas"
                                                                  : options.Generator) << "\n";
    if (!document.Title.empty()) stream << "comment source " << document.Title << "\n";
    stream << "element vertex " << flat.Positions.size() << "\n";
    stream << "property " << positionType << " x\n"
           << "property " << positionType << " y\n"
           << "property " << positionType << " z\n";
    if (flat.HasNormals) stream << "property float nx\nproperty float ny\nproperty float nz\n";
    if (flat.HasUVs) stream << "property float s\nproperty float t\n";
    if (flat.HasColors)
        stream << "property uchar red\nproperty uchar green\nproperty uchar blue\n"
                  "property uchar alpha\n";
    stream << "element face " << flat.Faces.size() << "\n";
    stream << "property list uchar uint vertex_indices\n";
    stream << "end_header\n";

    auto clampByte = [](float value) -> uint8_t {
        const float scaled = value * 255.0f;
        return static_cast<uint8_t>(scaled < 0.0f ? 0.0f : (scaled > 255.0f ? 255.0f : scaled));
    };

    if (binary) {
        for (size_t v = 0; v < flat.Positions.size(); ++v) {
            if (doublePositions) {
                const double xyz[3] = {flat.Positions[v].x, flat.Positions[v].y,
                                       flat.Positions[v].z};
                stream.write(reinterpret_cast<const char*>(xyz), sizeof(xyz));
            } else {
                const float xyz[3] = {static_cast<float>(flat.Positions[v].x),
                                      static_cast<float>(flat.Positions[v].y),
                                      static_cast<float>(flat.Positions[v].z)};
                stream.write(reinterpret_cast<const char*>(xyz), sizeof(xyz));
            }
            if (flat.HasNormals) {
                const float n[3] = {flat.Normals[v].x, flat.Normals[v].y, flat.Normals[v].z};
                stream.write(reinterpret_cast<const char*>(n), sizeof(n));
            }
            if (flat.HasUVs) {
                const float st[2] = {flat.UVs[v * 2], flat.UVs[v * 2 + 1]};
                stream.write(reinterpret_cast<const char*>(st), sizeof(st));
            }
            if (flat.HasColors) {
                const uint8_t rgba[4] = {clampByte(flat.Colors[v * 4]),
                                         clampByte(flat.Colors[v * 4 + 1]),
                                         clampByte(flat.Colors[v * 4 + 2]),
                                         clampByte(flat.Colors[v * 4 + 3])};
                stream.write(reinterpret_cast<const char*>(rgba), sizeof(rgba));
            }
        }
        for (const std::vector<uint32_t>& face : flat.Faces) {
            const uint8_t count = static_cast<uint8_t>(std::min<size_t>(face.size(), 255));
            stream.write(reinterpret_cast<const char*>(&count), 1);
            stream.write(reinterpret_cast<const char*>(face.data()),
                         static_cast<std::streamsize>(count * sizeof(uint32_t)));
        }
    } else {
        for (size_t v = 0; v < flat.Positions.size(); ++v) {
            // Enough digits for whichever type the header promised: 17 round-
            // trips a double, 9 a float, and the stream's default 6 is the
            // compact choice a deliverable wants.
            stream << std::setprecision(doublePositions
                                                ? std::numeric_limits<double>::max_digits10 : 6);
            stream << flat.Positions[v].x << ' ' << flat.Positions[v].y << ' '
                   << flat.Positions[v].z;
            // Normals and texture coordinates are float in the document, so
            // float's exact-round-trip digit count is all they can use.
            stream << std::setprecision(doublePositions
                                                ? std::numeric_limits<float>::max_digits10 : 6);
            if (flat.HasNormals)
                stream << ' ' << flat.Normals[v].x << ' ' << flat.Normals[v].y
                       << ' ' << flat.Normals[v].z;
            if (flat.HasUVs)
                stream << ' ' << flat.UVs[v * 2] << ' ' << flat.UVs[v * 2 + 1];
            if (flat.HasColors)
                stream << ' ' << static_cast<int>(clampByte(flat.Colors[v * 4]))
                       << ' ' << static_cast<int>(clampByte(flat.Colors[v * 4 + 1]))
                       << ' ' << static_cast<int>(clampByte(flat.Colors[v * 4 + 2]))
                       << ' ' << static_cast<int>(clampByte(flat.Colors[v * 4 + 3]));
            stream << '\n';
        }
        for (const std::vector<uint32_t>& face : flat.Faces) {
            stream << face.size();
            for (uint32_t corner : face) stream << ' ' << corner;
            stream << '\n';
        }
    }

    return static_cast<bool>(stream);
}

bool PLYConverter::Export(const ModelDocument& document, const std::string& filename,
                          const ConversionOptions& options) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("PLY: cannot write " + filename);
        return false;
    }
    return ExportToStream(document, file, options);
}

bool PLYConverter::ExportToMemory(const ModelDocument& document, std::vector<uint8_t>& outData,
                                  const ConversionOptions& options) {
    std::ostringstream stream;
    if (!ExportToStream(document, stream, options)) return false;
    const std::string text = stream.str();
    outData.assign(text.begin(), text.end());
    return true;
}

// ===== IDENTITY =====

FormatCapabilities PLYConverter::GetCapabilities() const {
    FormatCapabilities capabilities;
    capabilities.Meshes = true;
    capabilities.NGons = true;
    capabilities.PointClouds = true;
    capabilities.Normals = true;
    capabilities.TextureCoordinates = true;
    capabilities.VertexColors = true;
    // The reason PLY is worth having: a per-vertex property nothing else has a
    // field for survives as a named attribute.
    capabilities.CustomAttributes = true;
    capabilities.Metadata = true;      // comments
    // What PLY has no room for, so the report does not claim it.
    capabilities.SceneGraph = false;   // one vertex list, one face list
    capabilities.Materials = false;
    capabilities.Textures = false;
    capabilities.Units = false;
    capabilities.UpAxis = false;
    capabilities.DoublePrecision = false;   // written as float
    return capabilities;
}

bool PLYConverter::ValidateData(const std::vector<uint8_t>& data) const {
    // "ply" then a line break - enough to tell it from anything else, and not
    // so much that a file with an unusual header line is refused.
    if (data.size() < 4) return false;
    if (data[0] != 'p' || data[1] != 'l' || data[2] != 'y') return false;
    return data[3] == '\n' || data[3] == '\r';
}

bool PLYConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(8);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

} // namespace ModelConverter
} // namespace UltraCanvas
