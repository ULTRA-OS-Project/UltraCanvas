// Tests/ModelFbxTest.cpp
// The FBX reader.
//
// The sample is the same E-45 aircraft every other model suite reads, and it
// pins two cross-format facts: its scene is titled `E-45_GLSL`, exactly as the
// COLLADA export names its visual scene, and its ArmatureAction runs for
// 0.8333333 s, exactly the duration `Tests/ModelColladaTest.cpp` asserts. Its
// geometry - 2934 positions in 1681 polygons - is the Alembic export's, to the
// vertex.
//
// The synthetic half carries most of the weight, and it has to build binary FBX
// by hand to do it: nothing about the container, the deflate-compressed arrays,
// the eight layer mapping combinations, the transform chain's pivots, or the
// malformed-file refusals is reachable from one Blender export. A container
// nothing exercises is a liability.
//
// argv[1] is the .fbx. Without it only the synthetic cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/FBX/UltraCanvasFbxConverter.h"

#include <cmath>
#include <functional>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <zlib.h>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// ===== A BINARY FBX WRITER =====
// Small, and only as capable as the tests need: enough to build a scene the
// reader must agree with, including a compressed array.

class Builder {
public:
    Builder() {
        const char magic[] = "Kaydara FBX Binary  ";
        bytes_.insert(bytes_.end(), magic, magic + 20);
        bytes_.push_back(0x00);
        bytes_.push_back(0x1a);
        bytes_.push_back(0x00);
        PutU32(7400);
    }

    void Begin(const std::string& name) {
        if (!stack_.empty()) stack_.back().HasChildren = true;
        Frame frame;
        frame.Header = bytes_.size();
        PutU32(0);          // EndOffset, patched in End
        PutU32(0);          // NumProperties, patched
        PutU32(0);          // PropertyListLen, patched
        bytes_.push_back(static_cast<uint8_t>(name.size()));
        bytes_.insert(bytes_.end(), name.begin(), name.end());
        frame.PropertyStart = bytes_.size();
        stack_.push_back(frame);
    }

    void End() {
        Frame frame = stack_.back();
        stack_.pop_back();
        if (frame.HasChildren) for (int i = 0; i < 13; ++i) bytes_.push_back(0);
        Patch(frame.Header, static_cast<uint32_t>(bytes_.size()));
        Patch(frame.Header + 4, frame.Properties);
        Patch(frame.Header + 8, static_cast<uint32_t>(frame.PropertyEnd - frame.PropertyStart));
    }

    void Int32(int32_t value) { Count(); bytes_.push_back('I'); PutU32(static_cast<uint32_t>(value)); Mark(); }
    void Int64(int64_t value) {
        Count(); bytes_.push_back('L');
        for (int i = 0; i < 8; ++i) bytes_.push_back(static_cast<uint8_t>((static_cast<uint64_t>(value) >> (i * 8)) & 0xff));
        Mark();
    }
    void Double(double value) {
        Count(); bytes_.push_back('D');
        uint64_t bits = 0; std::memcpy(&bits, &value, 8);
        for (int i = 0; i < 8; ++i) bytes_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xff));
        Mark();
    }
    void Text(const std::string& value) {
        Count(); bytes_.push_back('S'); PutU32(static_cast<uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end()); Mark();
    }
    // An object's Name|Class pair, which the format writes with a 0x00 0x01
    // separator inside one string.
    void ObjectName(const std::string& name, const std::string& klass) {
        std::string joined = name;
        joined.push_back('\0');
        joined.push_back('\1');
        joined += klass;
        Text(joined);
    }
    void Doubles(const std::vector<double>& values, bool compressed = false) {
        std::vector<uint8_t> raw(values.size() * 8);
        for (size_t i = 0; i < values.size(); ++i) std::memcpy(raw.data() + i * 8, &values[i], 8);
        Array('d', static_cast<uint32_t>(values.size()), raw, compressed);
    }
    void Int32s(const std::vector<int32_t>& values, bool compressed = false) {
        std::vector<uint8_t> raw(values.size() * 4);
        for (size_t i = 0; i < values.size(); ++i) std::memcpy(raw.data() + i * 4, &values[i], 4);
        Array('i', static_cast<uint32_t>(values.size()), raw, compressed);
    }
    void Int64s(const std::vector<int64_t>& values) {
        std::vector<uint8_t> raw(values.size() * 8);
        for (size_t i = 0; i < values.size(); ++i) std::memcpy(raw.data() + i * 8, &values[i], 8);
        Array('l', static_cast<uint32_t>(values.size()), raw, false);
    }
    void Floats(const std::vector<float>& values) {
        std::vector<uint8_t> raw(values.size() * 4);
        for (size_t i = 0; i < values.size(); ++i) std::memcpy(raw.data() + i * 4, &values[i], 4);
        Array('f', static_cast<uint32_t>(values.size()), raw, false);
    }

    // A Properties70 P record: name, type, subtype, flags, then the values.
    void Property(const std::string& name, const std::string& type,
                  const std::vector<double>& values) {
        Begin("P");
        Text(name); Text(type); Text(""); Text("");
        for (double value : values) Double(value);
        End();
    }
    void PropertyText(const std::string& name, const std::string& type, const std::string& value) {
        Begin("P");
        Text(name); Text(type); Text(""); Text(""); Text(value);
        End();
    }

    void Connect(const std::string& kind, int64_t child, int64_t parent,
                 const std::string& property = "") {
        Begin("C");
        Text(kind); Int64(child); Int64(parent);
        if (!property.empty()) Text(property);
        End();
    }

    std::vector<uint8_t> Finish() {
        // The root list's own terminating null record.
        for (int i = 0; i < 13; ++i) bytes_.push_back(0);
        return bytes_;
    }
    std::vector<uint8_t>& Raw() { return bytes_; }

private:
    struct Frame {
        size_t Header = 0;
        uint32_t Properties = 0;
        size_t PropertyStart = 0;
        size_t PropertyEnd = 0;
        bool HasChildren = false;
    };

    void Count() { ++stack_.back().Properties; }
    void Mark() { stack_.back().PropertyEnd = bytes_.size(); }
    void PutU32(uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
    }
    void Patch(size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes_[at + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (i * 8)) & 0xff);
    }
    void Array(char code, uint32_t length, const std::vector<uint8_t>& raw, bool compressed) {
        Count();
        bytes_.push_back(static_cast<uint8_t>(code));
        PutU32(length);
        if (!compressed) {
            PutU32(0);
            PutU32(static_cast<uint32_t>(raw.size()));
            bytes_.insert(bytes_.end(), raw.begin(), raw.end());
        } else {
            uLongf bound = compressBound(static_cast<uLong>(raw.size()));
            std::vector<uint8_t> packed(bound);
            compress(packed.data(), &bound, raw.data(), static_cast<uLong>(raw.size()));
            packed.resize(bound);
            PutU32(1);
            PutU32(static_cast<uint32_t>(packed.size()));
            bytes_.insert(bytes_.end(), packed.begin(), packed.end());
        }
        Mark();
    }

    std::vector<uint8_t> bytes_;
    std::vector<Frame> stack_;
};

static std::shared_ptr<ModelDocument> Read(const std::vector<uint8_t>& data,
                                           std::vector<std::string>* warnings = nullptr) {
    FbxConverter converter;
    ConversionOptions options;
    if (warnings)
        options.WarningCallback = [warnings](const std::string& w) { warnings->push_back(w); };
    return converter.ImportFromMemory(data, options);
}

// A scene with one model and one quad, with the caller's extra work folded in.
// `model` adds Properties70 entries to the Model; `layers` adds layer elements
// to the Geometry.
static std::vector<uint8_t> Scene(const std::function<void(Builder&)>& model,
                                  const std::function<void(Builder&)>& layers,
                                  bool compressed = false) {
    Builder b;
    b.Begin("Objects");
      b.Begin("Geometry");
        b.Int64(100); b.ObjectName("Quad", "Geometry"); b.Text("Mesh");
        b.Begin("Vertices");
          b.Doubles({0,0,0,  1,0,0,  1,1,0,  0,1,0}, compressed);
        b.End();
        b.Begin("PolygonVertexIndex");
          b.Int32s({0, 1, 2, -4}, compressed);   // ~(-4) == 3, and ends the polygon
        b.End();
        if (layers) layers(b);
      b.End();
      b.Begin("Model");
        b.Int64(200); b.ObjectName("Box", "Model"); b.Text("Mesh");
        b.Begin("Properties70");
          if (model) model(b);
        b.End();
      b.End();
    b.End();
    b.Begin("Connections");
      b.Connect("OO", 200, 0);
      b.Connect("OO", 100, 200);
    b.End();
    return b.Finish();
}

static void TestValidation() {
    std::printf("Validation\n");
    FbxConverter converter;
    Check(!converter.ValidateData({}), "empty data is rejected");
    Check(!converter.ValidateData(std::vector<uint8_t>{'x','o','f',' ','0','3','0','3'}),
          "a DirectX .x file is rejected");
    Check(converter.ValidateData(Scene(nullptr, nullptr)), "a binary FBX is accepted");

    // ASCII has no "Kaydara FBX Binary" magic to match on, so validation has
    // to recognise it from its comment header and first record instead.
    const std::string ascii = "; FBX 6.1.0 project file\n; ----------------------\n\n"
                              "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n"
                              "\tFBXVersion: 6100\n}\n";
    const std::vector<uint8_t> asciiBytes(ascii.begin(), ascii.end());
    Check(converter.ValidateData(asciiBytes), "an ASCII FBX is accepted too");

    std::vector<std::string> warnings;
    // It parses; it just says nothing. The complaint should be about the
    // absent content, not about the encoding, which the reader handles.
    Check(Read(asciiBytes, &warnings) == nullptr, "a header with no objects reads as nothing");
    bool aboutContent = false;
    for (const std::string& w : warnings)
        if (w.find("no models") != std::string::npos) aboutContent = true;
    Check(aboutContent, "and the warning is about the missing models, not the encoding");

    // An ASCII file is a byte vector, not a C string: a number running to the
    // last byte must stop at the buffer's end rather than at a NUL that is not
    // there.
    warnings.clear();
    const std::string cut = "; FBX 6.1.0 project file\n"
                            "FBXHeaderExtension:  {\n\tFBXVersion: 6100\n}\n"
                            "Objects:  {\n\tModel: \"Model::Box\", \"Mesh\" {\n"
                            "\t\tVertices: 0,0,0,1,0,0,0,1,0";
    Check(Read(std::vector<uint8_t>(cut.begin(), cut.end()), &warnings) == nullptr,
          "an ASCII file cut mid-record reads as nothing");
    Check(!warnings.empty(), "rather than running off the end of the buffer");

    warnings.clear();
    std::vector<uint8_t> truncated = Scene(nullptr, nullptr);
    truncated.resize(truncated.size() / 2);
    Check(Read(truncated, &warnings) == nullptr, "a truncated file reads as nothing");
    Check(!warnings.empty(), "rather than crashing");
}

static void TestContainer() {
    std::printf("Container\n");

    // The same scene twice: once with raw arrays, once with deflate-compressed
    // ones. A container that reads only one of those reads half the FBX files
    // in the world.
    auto plain = Read(Scene(nullptr, nullptr, false));
    auto packed = Read(Scene(nullptr, nullptr, true));
    Check(plain != nullptr && packed != nullptr, "raw and deflate-compressed arrays both parse");
    if (plain && packed) {
        const Bounds3D a = plain->ComputeBounds();
        const Bounds3D b = packed->ComputeBounds();
        Check(Near(a.Max.x, b.Max.x, 1e-12) && Near(a.Max.y, b.Max.y, 1e-12) &&
              plain->TotalVertexCount() == packed->TotalVertexCount(),
              "and give identical geometry - compression is the container's business alone");
    }

    // An array whose byte count disagrees with its length is corrupt, and must
    // be refused rather than believed.
    {
        Builder b;
        b.Begin("Objects");
          b.Begin("Geometry");
            b.Int64(1); b.ObjectName("G", "Geometry"); b.Text("Mesh");
            b.Begin("Vertices");
              b.Doubles({0, 0, 0});
            b.End();
          b.End();
        b.End();
        std::vector<uint8_t> data = b.Finish();
        // Reach into the Vertices array header and inflate its declared length.
        for (size_t i = 0; i + 12 < data.size(); ++i) {
            if (data[i] == 'd' && data[i + 1] == 3 && data[i + 2] == 0) {
                data[i + 1] = 0x7f;   // now claims 32515 doubles in 24 bytes
                break;
            }
        }
        std::vector<std::string> warnings;
        Check(Read(data, &warnings) == nullptr && !warnings.empty(),
              "an array whose length disagrees with its byte count is refused, not allocated");
    }

    // A property count larger than the file can hold is the other shape a
    // corrupt length takes.
    {
        Builder b;
        b.Begin("Objects");
          b.Int32(1);
        b.End();
        std::vector<uint8_t> data = b.Finish();
        // Objects' NumProperties sits 4 bytes into its record, which starts at 27.
        data[27 + 4] = 0xff; data[27 + 5] = 0xff; data[27 + 6] = 0xff; data[27 + 7] = 0x7f;
        std::vector<std::string> warnings;
        Check(Read(data, &warnings) == nullptr && !warnings.empty(),
              "a record claiming more properties than the file holds is refused");
    }
}

// The property most likely to be got wrong: FBX's transform is not TRS, and
// pivots are what Maya and 3ds Max files are full of.
static void TestTransformChain() {
    std::printf("Transform chain\n");

    auto plain = Read(Scene([](Builder& b) {
        b.Property("Lcl Translation", "Lcl Translation", {3, 4, 5});
        b.Property("Lcl Scaling", "Lcl Scaling", {2, 2, 2});
    }, nullptr));
    Check(plain != nullptr, "a plain T and S parse");
    if (plain) {
        Check(Near(plain->Nodes[0].Translation.x, 3.0, 1e-12) &&
              Near(plain->Nodes[0].Scale.z, 2.0, 1e-12),
              "and come back as exactly the fields that were written");
        const Bounds3D bounds = plain->ComputeBounds();
        Check(Near(bounds.Max.x, 5.0, 1e-9) && Near(bounds.Max.y, 6.0, 1e-9),
              "with the quad scaled then translated");
    }

    // Lcl Rotation is Euler degrees. 90 about X must carry +Y onto +Z.
    auto turned = Read(Scene([](Builder& b) {
        b.Property("Lcl Rotation", "Lcl Rotation", {90, 0, 0});
    }, nullptr));
    Check(turned != nullptr, "an Euler rotation parses");
    if (turned) {
        const Vec3d up = turned->Nodes[0].Rotation.Rotate(Vec3d(0.0, 1.0, 0.0));
        Check(Near(up.z, 1.0, 1e-9), "and 90 degrees about X carries +Y onto +Z");
    }

    // RotationOrder is honoured, not assumed. XYZ and ZYX disagree for any two
    // non-commuting rotations, so reading the field wrong is visible here.
    auto xyz = Read(Scene([](Builder& b) {
        b.Property("Lcl Rotation", "Lcl Rotation", {90, 90, 0});
    }, nullptr));
    auto zyx = Read(Scene([](Builder& b) {
        b.Property("RotationOrder", "enum", {5});
        b.Property("Lcl Rotation", "Lcl Rotation", {90, 90, 0});
    }, nullptr));
    Check(xyz && zyx, "both rotation orders parse");
    if (xyz && zyx) {
        const Vec3d a = xyz->Nodes[0].Rotation.Rotate(Vec3d(1.0, 0.0, 0.0));
        const Vec3d b = zyx->Nodes[0].Rotation.Rotate(Vec3d(1.0, 0.0, 0.0));
        Check(!(Near(a.x, b.x, 1e-6) && Near(a.y, b.y, 1e-6) && Near(a.z, b.z, 1e-6)),
              "and RotationOrder changes the result, so the field is being read");
    }

    // A rotation pivot moves what the rotation turns about. A reader that
    // copies Lcl Rotation into a TRS slot puts this geometry somewhere else.
    auto pivoted = Read(Scene([](Builder& b) {
        b.Property("RotationPivot", "Vector3D", {1, 0, 0});
        b.Property("Lcl Rotation", "Lcl Rotation", {0, 0, 180});
    }, nullptr));
    Check(pivoted != nullptr, "a rotation pivot parses");
    if (pivoted) {
        // Half a turn about (1,0,0) maps the quad's x range [0,1] to [1,2].
        const Bounds3D bounds = pivoted->ComputeBounds();
        Check(Near(bounds.Min.x, 1.0, 1e-6) && Near(bounds.Max.x, 2.0, 1e-6),
              "and the rotation happens about it, not about the origin");
    }

    // A geometric transform places the mesh without being inherited, which one
    // transform per node cannot say - so it becomes a child node.
    auto geometric = Read(Scene([](Builder& b) {
        b.Property("GeometricTranslation", "Vector3D", {10, 0, 0});
    }, nullptr));
    Check(geometric != nullptr, "a geometric transform parses");
    if (geometric) {
        Check(geometric->Nodes.size() == 2 && geometric->Nodes[1].Parent == 0 &&
              geometric->Nodes[1].Mesh == 0 && geometric->Nodes[0].Mesh < 0,
              "as a child node carrying the mesh, since it is not inherited");
        Check(Near(geometric->ComputeBounds().Min.x, 10.0, 1e-9), "placed where it says");
    }
}

static void TestLayers() {
    std::printf("Vertex layers\n");

    // ByPolygonVertex + Direct: one normal per corner, in corner order.
    auto perCorner = Read(Scene(nullptr, [](Builder& b) {
        b.Begin("LayerElementNormal");
          b.Int32(0);
          b.Begin("MappingInformationType"); b.Text("ByPolygonVertex"); b.End();
          b.Begin("ReferenceInformationType"); b.Text("Direct"); b.End();
          b.Begin("Normals"); b.Doubles({0,0,1, 0,0,1, 0,1,0, 0,1,0}); b.End();
        b.End();
    }));
    Check(perCorner != nullptr, "ByPolygonVertex + Direct normals parse");
    if (perCorner) {
        const MeshPrimitive& prim = perCorner->Meshes[0].Primitives[0];
        Check(prim.VertexCount() == 4 && prim.Normals.size() == 4, "one normal per corner");
        Check(Near(prim.Normals[0].z, 1.0, 1e-9) && Near(prim.Normals[2].y, 1.0, 1e-9),
              "each landing on the corner it was written for");
    }

    // ByVertice + IndexToDirect: shared normals reached through an index.
    auto perVertex = Read(Scene(nullptr, [](Builder& b) {
        b.Begin("LayerElementNormal");
          b.Int32(0);
          b.Begin("MappingInformationType"); b.Text("ByVertice"); b.End();
          b.Begin("ReferenceInformationType"); b.Text("IndexToDirect"); b.End();
          b.Begin("Normals"); b.Doubles({1,0,0, 0,1,0}); b.End();
          b.Begin("NormalsIndex"); b.Int32s({0, 0, 1, 1}); b.End();
        b.End();
    }));
    Check(perVertex != nullptr, "ByVertice + IndexToDirect normals parse");
    if (perVertex) {
        const MeshPrimitive& prim = perVertex->Meshes[0].Primitives[0];
        Check(Near(prim.Normals[0].x, 1.0, 1e-9) && Near(prim.Normals[3].y, 1.0, 1e-9),
              "resolving the index per vertex rather than per corner");
    }

    // AllSame: one UV for the whole mesh.
    auto allSame = Read(Scene(nullptr, [](Builder& b) {
        b.Begin("LayerElementUV");
          b.Int32(0);
          b.Begin("MappingInformationType"); b.Text("AllSame"); b.End();
          b.Begin("ReferenceInformationType"); b.Text("Direct"); b.End();
          b.Begin("UV"); b.Doubles({0.25, 0.75}); b.End();
        b.End();
    }));
    Check(allSame != nullptr, "an AllSame UV layer parses");
    if (allSame) {
        const VertexAttribute* uv =
                allSame->Meshes[0].Primitives[0].FindAttribute(AttributeSemantic::TexCoord, 0);
        Check(uv && uv->Count() == 4 && Near(uv->Values[0], 0.25, 1e-9) &&
              Near(uv->Values[7], 0.75, 1e-9),
              "and every vertex gets it");
    }

    // A polygon is ended by a negative index, which is the bitwise complement
    // of the real one. Reading it as a negation loses the last corner of
    // every face.
    auto quad = Read(Scene(nullptr, nullptr));
    Check(quad != nullptr, "the polygon terminator parses");
    if (quad) {
        const MeshPrimitive& prim = quad->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Polygons && prim.FaceCount() == 1 &&
              prim.Face(0).size() == 4,
              "as a quad, not a triangle - the terminator is a complement, not a negation");
        Check(Near(prim.Positions[3].y, 1.0, 1e-12),
              "with its fourth corner at the vertex ~(-4) names");
    }
}

static void TestMaterials() {
    std::printf("Materials\n");

    Builder b;
    b.Begin("Objects");
      b.Begin("Geometry");
        b.Int64(100); b.ObjectName("Quad", "Geometry"); b.Text("Mesh");
        b.Begin("Vertices"); b.Doubles({0,0,0, 1,0,0, 1,1,0, 0,1,0}); b.End();
        b.Begin("PolygonVertexIndex"); b.Int32s({0, 1, 2, -4}); b.End();
        b.Begin("LayerElementMaterial");
          b.Int32(0);
          b.Begin("MappingInformationType"); b.Text("AllSame"); b.End();
          b.Begin("ReferenceInformationType"); b.Text("IndexToDirect"); b.End();
          b.Begin("Materials"); b.Int32s({0}); b.End();
        b.End();
      b.End();
      b.Begin("Model");
        b.Int64(200); b.ObjectName("Box", "Model"); b.Text("Mesh");
        b.Begin("Properties70"); b.End();
      b.End();
      b.Begin("Material");
        b.Int64(300); b.ObjectName("Paint", "Material"); b.Text("");
        b.Begin("ShadingModel"); b.Text("Phong"); b.End();
        b.Begin("Properties70");
          b.Property("DiffuseColor", "Color", {1.0, 0.5, 0.25});
          b.Property("DiffuseFactor", "Number", {0.5});
          b.Property("SpecularColor", "Color", {1.0, 1.0, 1.0});
          b.Property("SpecularFactor", "Number", {0.25});
          b.Property("EmissiveColor", "Color", {1.0, 1.0, 1.0});
          b.Property("EmissiveFactor", "Number", {0.0});
          b.Property("ShininessExponent", "Number", {48.0});
          b.Property("TransparencyFactor", "Number", {0.25});
        b.End();
      b.End();
      b.Begin("Texture");
        b.Int64(400); b.ObjectName("Skin", "Texture"); b.Text("");
        b.Begin("FileName"); b.Text("C:\\elsewhere\\hull.png"); b.End();
        b.Begin("RelativeFilename"); b.Text("textures\\hull.png"); b.End();
      b.End();
    b.End();
    b.Begin("Connections");
      b.Connect("OO", 200, 0);
      b.Connect("OO", 100, 200);
      b.Connect("OO", 300, 200);
      b.Connect("OP", 400, 300, "DiffuseColor");
    b.End();

    auto document = Read(b.Finish());
    Check(document != nullptr, "a material with a texture parses");
    if (!document) return;
    Check(document->Materials.size() == 1, "one material");
    const ModelMaterial& material = document->Materials[0];
    Check(material.Name == "Paint", "named as the object is");
    Check(material.Phong.has_value(), "stated in fixed-function terms");
    if (material.Phong) {
        // Colour times factor is what the format means, and the reason a model
        // does not come out twice as bright as it was authored.
        Check(Near(material.Phong->Diffuse.x, 0.5, 1e-6) &&
              Near(material.Phong->Diffuse.y, 0.25, 1e-6),
              "DiffuseColor times DiffuseFactor is the diffuse");
        Check(Near(material.Phong->Specular.x, 0.25, 1e-6),
              "and SpecularColor times SpecularFactor the specular");
        Check(Near(material.Phong->Shininess, 48.0, 1e-6),
              "ShininessExponent is the specular exponent outright");
    }
    // EmissiveColor is white here and EmissiveFactor zero - which is exactly
    // what exporters write, and what makes an unmultiplied reader glow.
    Check(Near(material.EmissiveFactor.x, 0.0, 1e-6),
          "EmissiveColor times a zero EmissiveFactor does not glow");
    Check(Near(material.BaseColorFactor.w, 0.75, 1e-6) && material.Alpha == AlphaMode::Blend,
          "TransparencyFactor becomes opacity, and the material blends");
    Check(document->Images.size() == 1 && document->Images[0].Uri == "textures/hull.png",
          "RelativeFilename is the texture path, with its backslashes turned round");
    Check(material.BaseColorTexture.IsSet(), "and it reaches the base colour slot");
    Check(document->Meshes[0].Primitives[0].Material == 0,
          "the mesh's AllSame material assignment resolves through the model's own list");
}

static void TestAnimation() {
    std::printf("Animation\n");

    // FBX counts time in 46186158000 ticks per second. A key at exactly one
    // second is the cleanest way to assert that constant is right.
    const int64_t oneSecond = 46186158000LL;

    Builder b;
    b.Begin("Objects");
      b.Begin("Geometry");
        b.Int64(100); b.ObjectName("Quad", "Geometry"); b.Text("Mesh");
        b.Begin("Vertices"); b.Doubles({0,0,0, 1,0,0, 1,1,0, 0,1,0}); b.End();
        b.Begin("PolygonVertexIndex"); b.Int32s({0, 1, 2, -4}); b.End();
      b.End();
      b.Begin("Model");
        b.Int64(200); b.ObjectName("Box", "Model"); b.Text("Mesh");
        b.Begin("Properties70"); b.End();
      b.End();
      b.Begin("AnimationStack");
        b.Int64(300); b.ObjectName("Take 001", "AnimStack"); b.Text("");
      b.End();
      b.Begin("AnimationLayer");
        b.Int64(400); b.ObjectName("BaseLayer", "AnimLayer"); b.Text("");
      b.End();
      b.Begin("AnimationCurveNode");
        b.Int64(500); b.ObjectName("T", "AnimCurveNode"); b.Text("");
        b.Begin("Properties70");
          b.Property("d|X", "Number", {0.0});
          b.Property("d|Y", "Number", {7.0});     // no curve: the default stands
          b.Property("d|Z", "Number", {0.0});
        b.End();
      b.End();
      b.Begin("AnimationCurve");
        b.Int64(600); b.ObjectName("", "AnimCurve"); b.Text("");
        b.Begin("KeyTime"); b.Int64s({0, oneSecond}); b.End();
        b.Begin("KeyValueFloat"); b.Floats({0.0f, 10.0f}); b.End();
      b.End();
    b.End();
    b.Begin("Connections");
      b.Connect("OO", 200, 0);
      b.Connect("OO", 100, 200);
      b.Connect("OO", 400, 300);
      b.Connect("OO", 500, 400);
      b.Connect("OP", 500, 200, "Lcl Translation");
      b.Connect("OP", 600, 500, "d|X");
    b.End();

    std::vector<std::string> warnings;
    auto document = Read(b.Finish(), &warnings);
    Check(document != nullptr, "a routed animation parses");
    if (!document) return;
    Check(document->Animations.size() == 1 && document->Animations[0].Channels.size() == 1,
          "one stack becomes one animation with one channel");
    if (document->Animations.empty() || document->Animations[0].Channels.empty()) return;

    const ModelAnimation& track = document->Animations[0];
    Check(track.Name == "Take 001", "named as the AnimationStack is");
    Check(track.Channels[0].Path == AnimationPath::Translation &&
          track.Channels[0].TargetNode == 0,
          "targeting the model the OP connection names, on the property it names");
    Check(Near(track.Duration(), 1.0f, 1e-5),
          "and a key at 46186158000 ticks is one second, not some other number");

    const AnimationSampler& sampler = track.Samplers[0];
    Check(sampler.Values.size() == 6, "two keys of three floats");
    if (sampler.Values.size() == 6) {
        Check(Near(sampler.Values[0], 0.0, 1e-6) && Near(sampler.Values[3], 10.0, 1e-6),
              "the X curve's own values");
        // Y has no curve at all, so the curve node's default is what the
        // channel must carry - not zero.
        Check(Near(sampler.Values[1], 7.0, 1e-6) && Near(sampler.Values[4], 7.0, 1e-6),
              "and an axis with no curve holds the curve node's default, not zero");
    }
}

// FBX 6.x is not the same format as 7.x, and it is the one that is usually
// ASCII: no object ids, geometry inside the Model, Properties60 one field
// shorter than Properties70, textures bound to the model, animation in Takes.
static void TestLegacyAscii() {
    std::printf("ASCII, and the 6.x object model\n");

    const std::string text =
            "; FBX 6.1.0 project file\n"
            "FBXHeaderExtension:  {\n"
            "\tFBXHeaderVersion: 1003\n"
            "\tFBXVersion: 6100\n"
            "}\n"
            "Creator: \"Blender version 2.78\"\n"
            "Objects:  {\n"
            "\tModel: \"Model::Camera Switcher\", \"CameraSwitcher\" {\n"
            "\t\tVersion: 232\n"
            "\t}\n"
            "\tModel: \"Model::Producer Perspective\", \"Camera\" {\n"
            "\t\tVersion: 232\n"
            "\t}\n"
            "\tModel: \"Model::Quad\", \"Mesh\" {\n"
            "\t\tVersion: 232\n"
            "\t\tProperties60:  {\n"
            "\t\t\tProperty: \"Lcl Translation\", \"Lcl Translation\", \"A+\",3,4,5\n"
            "\t\t\tProperty: \"Lcl Scaling\", \"Lcl Scaling\", \"A+\",2,2,2\n"
            "\t\t}\n"
            "\t\tShading: Y\n"
            "\t\tVertices: 0,0,0,1,0,0,\n"
            "\t\t\t1,1,0,0,1,0\n"
            "\t\tPolygonVertexIndex: 0,1,2,-4\n"
            "\t\tGeometryVersion: 124\n"
            "\t\tLayerElementMaterial: 0 {\n"
            "\t\t\tMappingInformationType: \"AllSame\"\n"
            "\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
            "\t\t\tMaterials: 0\n"
            "\t\t}\n"
            "\t}\n"
            "\tMaterial: \"Material::Paint\", \"\" {\n"
            "\t\tShadingModel: \"lambert\"\n"
            "\t\tProperties60:  {\n"
            "\t\t\tProperty: \"DiffuseColor\", \"ColorRGB\", \"\",1.0,0.5,0.25\n"
            "\t\t\tProperty: \"DiffuseFactor\", \"double\", \"\",0.5\n"
            "\t\t\tProperty: \"Opacity\", \"double\", \"\",0.5\n"
            "\t\t}\n"
            "\t}\n"
            "\tTexture: \"Texture::Skin\", \"TextureVideoClip\" {\n"
            "\t\tRelativeFilename: \"textures\\hull.png\"\n"
            "\t}\n"
            "\tGlobalSettings:  {\n"
            "\t\tProperties60:  {\n"
            "\t\t\tProperty: \"UpAxis\", \"int\", \"\",2\n"
            "\t\t\tProperty: \"UnitScaleFactor\", \"double\", \"\",100\n"
            "\t\t}\n"
            "\t}\n"
            "}\n"
            "Connections:  {\n"
            "\tConnect: \"OO\", \"Model::Quad\", \"Model::Scene\"\n"
            "\tConnect: \"OO\", \"Material::Paint\", \"Model::Quad\"\n"
            "\tConnect: \"OO\", \"Texture::Skin\", \"Model::Quad\"\n"
            "}\n"
            "Takes:  {\n"
            "\tCurrent: \"Default Take\"\n"
            "\tTake: \"Default Take\" {\n"
            "\t\tModel: \"Model::Quad\" {\n"
            "\t\t\tChannel: \"Transform\" {\n"
            "\t\t\t\tChannel: \"T\" {\n"
            "\t\t\t\t\tChannel: \"X\" {\n"
            "\t\t\t\t\t\tDefault: 0\n"
            "\t\t\t\t\t\tKeyVer: 4005\n"
            "\t\t\t\t\t\tKeyCount: 2\n"
            "\t\t\t\t\t\tKey: \n"
            "\t\t\t\t\t\t\t0,0,L,\n"
            "\t\t\t\t\t\t\t46186158000,10,L\n"
            "\t\t\t\t\t\tColor: 1,0,0\n"
            "\t\t\t\t\t}\n"
            "\t\t\t\t\tChannel: \"Y\" {\n"
            "\t\t\t\t\t\tDefault: 7\n"
            "\t\t\t\t\t\tKeyCount: 0\n"
            "\t\t\t\t\t}\n"
            "\t\t\t\t\tLayerType: 1\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n"
            "\t\t}\n"
            "\t}\n"
            "}\n";

    std::vector<std::string> warnings;
    auto document = Read(std::vector<uint8_t>(text.begin(), text.end()), &warnings);
    Check(document != nullptr, "an FBX 6.1 ASCII file parses");
    if (!document) return;
    Check(document->Metadata["fbx.version"] == "6100" &&
          document->Metadata["fbx.encoding"] == "ascii",
          "and reports the generation and encoding it was read from");

    // The Camera Switcher and the Producer cameras are exporter boilerplate,
    // and their absence is stated rather than silent.
    Check(document->Nodes.size() == 1 && document->Nodes[0].Name == "Quad",
          "only the real model becomes a node");
    Check(document->Metadata.count("fbx.producerModels") == 1,
          "and the boilerplate camera models it skipped are counted in metadata");

    // Properties60 records are one field shorter than Properties70's. Reading
    // them at the 7.x offset returns the flags string instead of the number.
    Check(Near(document->Nodes[0].Translation.x, 3.0, 1e-12) &&
          Near(document->Nodes[0].Scale.y, 2.0, 1e-12),
          "Properties60 values are read at their own offset, not Properties70's");

    // Geometry is inside the Model here, not a separate object, and the
    // numbers are one property each rather than one array.
    Check(document->Meshes.size() == 1, "the Model's own Vertices become a mesh");
    if (!document->Meshes.empty()) {
        const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
        Check(prim.VertexCount() == 4 && prim.FaceCount() == 1 && prim.Face(0).size() == 4,
              "a quad, with its numbers read one property at a time");
        Check(prim.Material == 0, "bound to the material connected to its model");
    }
    Check(Near(document->ComputeBounds().Max.x, 5.0, 1e-9),
          "and the transform applies, so the chain is the same one 7.x uses");

    // A texture connects to the model, not to a material property.
    Check(document->Images.size() == 1 && document->Images[0].Uri == "textures/hull.png",
          "the texture is read");
    Check(document->Materials.size() == 1 && document->Materials[0].BaseColorTexture.IsSet(),
          "and reaches the material, which in 6.x is only inferable from sharing a model");
    Check(Near(document->Materials[0].BaseColorFactor.w, 0.5, 1e-6),
          "Opacity is read where 7.x would say TransparencyFactor");

    // GlobalSettings lives inside Objects in 6.x.
    Check(document->Up == UpAxis::ZUp,
          "GlobalSettings is found inside Objects, and its Z-up is honoured");
    Check(Near(document->UnitScaleToMeters, 1.0, 1e-12) &&
          document->SourceUnit == ModelUnit::Meter,
          "and a UnitScaleFactor of 100 centimetres is a metre");

    // Animation is a Takes block, not stacks and curve nodes.
    Check(document->Animations.size() == 1 && document->Animations[0].Channels.size() == 1,
          "a Take becomes one animation with one channel");
    if (!document->Animations.empty() && !document->Animations[0].Channels.empty()) {
        const ModelAnimation& track = document->Animations[0];
        Check(track.Name == "Default Take", "named as the Take is");
        Check(Near(track.Duration(), 1.0f, 1e-5),
              "with the same ticks per second the binary generation uses");
        const AnimationSampler& sampler = track.Samplers[0];
        Check(sampler.Values.size() == 6 && Near(sampler.Values[3], 10.0, 1e-6),
              "the X channel's keys");
        Check(Near(sampler.Values[1], 7.0, 1e-6) && Near(sampler.Values[4], 7.0, 1e-6),
              "and an axis with no keys holds its Default, not zero");
    }
}

static void TestLegacySample(const char* path) {
    std::printf("Sample (6.1 ASCII): %s\n", path);
    FbxConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = converter.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    Check(doc->Metadata["fbx.version"] == "6100" && doc->Metadata["fbx.encoding"] == "ascii",
          "read as FBX 6.1, ASCII");
    Check(doc->Generator.rfind("Blender", 0) == 0, "the Creator record reaches Generator");
    Check(doc->Nodes.size() == 6 && doc->Meshes.size() == 2,
          "six nodes - the two meshes and the four-bone armature, with the eight "
          "boilerplate camera models skipped");
    Check(doc->Metadata.count("fbx.producerModels") == 1, "which metadata records");

    // This export is NOT the one the 7.4 binary sample is. It has 8110 faces
    // and 11749 positions - the OBJ and X3D counts - where the binary has 1681
    // and 2934. Two exports of one scene, in the same format, on opposite sides
    // of the mirror-modifier split.
    Check(doc->TotalFaceCount() == 8110,
          "8110 faces - the OBJ and X3D count, not the 1681 of the 7.4 binary sample");
    Check(doc->TotalVertexCount() == 32440,
          "32440 corners once the layers are resolved, exactly as the X3D reader produces");

    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.9732, 1e-3),
          "and it is symmetric about X: this export had its mirror modifier applied");

    Check(doc->Materials.size() == 2 && doc->Images.size() == 2,
          "both materials, each with the texture that shares its model");
    bool textured = true;
    for (const ModelMaterial& material : doc->Materials)
        if (!material.BaseColorTexture.IsSet()) textured = false;
    Check(textured, "which is the only way 6.x states that binding");

    Check(doc->Animations.size() == 2, "both Takes are read");
    const ModelAnimation* armature = nullptr;
    for (const ModelAnimation& animation : doc->Animations)
        if (animation.Name == "ArmatureAction") armature = &animation;
    Check(armature != nullptr, "including the one named ArmatureAction");
    if (armature)
        Check(Near(armature->Duration(), 0.8333333f, 1e-4),
              "running 0.8333333 s - the same duration the .dae and the 7.4 binary carry");

    bool skinning = false;
    for (const std::string& w : warnings)
        if (w.find("skin deformers") != std::string::npos) skinning = true;
    Check(skinning, "and the skin deformers this generation writes are reported, not read");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    FbxConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = converter.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    // The title is in the file, not the path: FBX names its document.
    Check(doc->Title == "E-45_GLSL",
          "the Document object titles the scene - the same name the .dae's visual scene has");
    Check(doc->Generator.rfind("Blender", 0) == 0, "the Creator record reaches Generator");
    Check(doc->Metadata["fbx.version"] == "7400", "read as FBX 7.4");

    Check(doc->Up == UpAxis::YUp, "GlobalSettings says Y-up");
    Check(doc->SourceUnit == ModelUnit::Centimeter && Near(doc->UnitScaleToMeters, 0.01, 1e-12),
          "and a UnitScaleFactor of 1 means centimetres, which is FBX's own unit");

    Check(doc->Nodes.size() == 7 && doc->Meshes.size() == 2,
          "two meshes under the seven-node Armature / root / Ship / Glass chain");
    Check(doc->Nodes[0].Name == "Armature" && doc->Nodes[1].Name == "root" &&
          doc->Nodes[2].Name == "Ship" && doc->Nodes[2].Parent == 1,
          "with the model names and nesting the connections describe");

    // 1920 + 1014 positions in 937 + 744 polygons: the Alembic export's
    // geometry exactly, which is what says these two came out of one scene.
    Check(doc->TotalFaceCount() == 1681,
          "1681 polygons - the same count the .abc export of this scene holds");
    size_t triangles = 0;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t f = 0; f < prim.FaceCount(); ++f) triangles += prim.Face(f).size() - 2;
    Check(triangles == 3297, "3297 triangles once fanned");

    Check(doc->Materials.size() == 2, "both materials are read");
    Check(doc->Images.size() == 6, "and all six textures, not just the ones with a document slot");
    bool transparent = false;
    for (const ModelMaterial& material : doc->Materials)
        if (material.BaseColorFactor.w < 0.9f && material.Alpha == AlphaMode::Blend)
            transparent = true;
    Check(transparent, "the canopy glass is read as transparent");

    bool vertexColours = false;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            if (prim.FindAttribute(AttributeSemantic::Color, 0)) vertexColours = true;
    Check(vertexColours, "and the hull's vertex colour layer with it");

    // Two takes, each driving four nodes on translation, rotation and scaling.
    Check(doc->Animations.size() == 2, "both animation stacks are read");
    const ModelAnimation* armature = nullptr;
    for (const ModelAnimation& animation : doc->Animations)
        if (animation.Name.find("ArmatureAction") != std::string::npos) armature = &animation;
    Check(armature != nullptr, "including the one named ArmatureAction");
    if (armature) {
        Check(armature->Channels.size() == 12,
              "as twelve channels - four nodes by translation, rotation and scaling");
        Check(Near(armature->Duration(), 0.8333333f, 1e-4),
              "running 0.8333333 s, which is exactly the duration the .dae export carries");
        bool rotation = false;
        for (const AnimationChannel& channel : armature->Channels)
            if (channel.Path == AnimationPath::Rotation) {
                rotation = true;
                Check(armature->Samplers[static_cast<size_t>(channel.Sampler)].Values.size() %
                              4 == 0,
                      "and its Euler rotation keys arrive as quaternions");
                break;
            }
        Check(rotation, "with a rotation channel among them");
    }

    // The one thing the file does that the document cannot hold, reported
    // rather than hidden: three textures on one material slot.
    bool layered = false;
    for (const std::string& w : warnings)
        if (w.find("more than one texture") != std::string::npos) layered = true;
    Check(layered, "the three textures stacked on the hull's DiffuseColor slot are reported");

    // World bounds, checked against an independent walk of the same chain.
    // This is the assertion that the whole transform chain is right.
    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -97.3152, 1e-3) && Near(bounds.Max.x, 55.9136, 1e-3) &&
          Near(bounds.Min.y, -135.5636, 1e-3) && Near(bounds.Max.y, 285.5452, 1e-3) &&
          Near(bounds.Min.z, -294.6116, 1e-3) && Near(bounds.Max.z, 321.1289, 1e-3),
          "world bounds match an independent walk of the model chain");
    Check(bounds.Max.y > 100.0,
          "and they are in centimetres, because the armature carries the 100x that "
          "UnitScaleFactor implies");
}

int main(int argc, char** argv) {
    TestValidation();
    TestContainer();
    TestTransformChain();
    TestLayers();
    TestMaterials();
    TestAnimation();
    TestLegacyAscii();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass a binary .fbx path to run it)\n");
    if (argc > 2) TestLegacySample(argv[2]);
    else std::printf("Sample (6.1 ASCII): skipped (pass a second .fbx path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
