// Tests/ModelBlendTest.cpp
// The .blend reader.
//
// A .blend is the one format in this matrix that is not an interchange file at
// all - it is Blender's heap, written out, with an embedded SDNA block
// describing every struct in the build that wrote it. That shapes the suite in
// two ways.
//
// First, the synthetic half **builds .blend files by hand**, SDNA and all,
// because the decisions that matter are not reachable from one export: that a
// field is found at whatever offset *this file's* SDNA gives it rather than a
// remembered one, that a big-endian file reads, that both of the two mesh
// layouts Blender has shipped arrive at the same geometry, and that a
// malformed block index is refused rather than followed off the end of the
// buffer. The builder is why a field can be *moved* in a test and the reader
// still has to find it - which is the whole claim the SDNA makes.
//
// Second, the sample half asserts the thing this reader must never quietly
// stop saying: what a .blend holds is the cage *before* its modifiers. The
// E-45 stores 1147 vertices behind Mirror, Subsurf and EdgeSplit, while the
// same model exported to OBJ is 11749, and its hull stops dead at x = 0
// because the mirror is not applied. All three are asserted. Reading the cage
// is right - it is what the artist modelled - but a reader that stopped
// warning about it would be handing callers a tenth of an aircraft in silence.
//
// argv[1] is the .blend. Without it only the synthetic cases run.
//
// Version: 2.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/Blend/UltraCanvasBlendConverter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

static bool Contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// ===== A .BLEND, BUILT BY HAND =====
//
// Enough of the format to exercise the reader: the 12-byte header, a block
// index, and an SDNA describing whatever structs the caller declares. Field
// order and field *presence* are the caller's to choose, which is what lets a
// test move a field and assert the reader still finds it.

class Builder {
public:
    Builder(int pointerSize = 8, bool bigEndian = false, const char* version = "278")
        : pointerSize_(pointerSize), bigEndian_(bigEndian), version_(version) {
        // The primitives Blender's SDNA always carries.
        AddType("char", 1);
        AddType("uchar", 1);
        AddType("short", 2);
        AddType("ushort", 2);
        AddType("int", 4);
        AddType("float", 4);
        AddType("double", 8);
        AddType("int64_t", 8);
        AddType("void", 0);
    }

    // A field is declared exactly as the SDNA declares it: "*mvert" is a
    // pointer, "co[3]" is three floats.
    using Fields = std::vector<std::pair<std::string, std::string>>;

    int DefineStruct(const std::string& name, const Fields& fields) {
        uint16_t size = 0;
        std::vector<std::pair<uint16_t, uint16_t>> encoded;
        for (const auto& field : fields) {
            encoded.emplace_back(AddType(field.first, 0), AddName(field.second));
            size = static_cast<uint16_t>(size + FieldSize(field.first, field.second));
        }
        const uint16_t typeIndex = AddType(name, size);
        lengths_[typeIndex] = size;
        structs_.push_back({typeIndex, encoded});
        structSize_[name] = size;
        structIndex_[name] = static_cast<int>(structs_.size()) - 1;
        return structIndex_[name];
    }

    size_t StructSize(const std::string& name) const {
        auto found = structSize_.find(name);
        return found == structSize_.end() ? 0 : found->second;
    }

    void AddBlock(const char* code, const std::string& structName, uint32_t count,
                  uint64_t address, const std::vector<uint8_t>& body) {
        Block block;
        std::memset(block.Code, 0, 4);
        std::memcpy(block.Code, code, std::min<size_t>(4, std::strlen(code)));
        block.StructIndex = structIndex_.count(structName) ? structIndex_.at(structName) : 0;
        block.Count = count;
        block.Address = address;
        block.Body = body;
        blocks_.push_back(std::move(block));
    }

    // ----- writing values into a body, with the builder's endianness -----

    void PutInt(std::vector<uint8_t>& out, int64_t value, size_t width) const {
        uint64_t raw = static_cast<uint64_t>(value);
        std::vector<uint8_t> bytes(width);
        for (size_t i = 0; i < width; ++i) bytes[i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xff);
        if (bigEndian_) std::reverse(bytes.begin(), bytes.end());
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    void PutFloat(std::vector<uint8_t>& out, float value) const {
        uint32_t raw = 0;
        std::memcpy(&raw, &value, 4);
        PutInt(out, raw, 4);
    }
    void PutPointer(std::vector<uint8_t>& out, uint64_t address) const {
        PutInt(out, static_cast<int64_t>(address), static_cast<size_t>(pointerSize_));
    }
    void PutBytes(std::vector<uint8_t>& out, const std::string& text, size_t width) const {
        for (size_t i = 0; i < width; ++i)
            out.push_back(i < text.size() ? static_cast<uint8_t>(text[i]) : 0);
    }
    void Pad(std::vector<uint8_t>& out, size_t bytes) const { out.insert(out.end(), bytes, 0); }

    std::vector<uint8_t> Finish() {
        std::vector<uint8_t> file;
        const std::string magic = "BLENDER";
        file.insert(file.end(), magic.begin(), magic.end());
        file.push_back(pointerSize_ == 8 ? '-' : '_');
        file.push_back(bigEndian_ ? 'V' : 'v');
        for (int i = 0; i < 3; ++i) file.push_back(static_cast<uint8_t>(version_[i]));

        for (const Block& block : blocks_) WriteBlock(file, block);

        Block dna;
        std::memcpy(dna.Code, "DNA1", 4);
        dna.Body = EncodeSdna();
        dna.Count = 1;
        dna.Address = 0xD1A0000;
        WriteBlock(file, dna);

        Block end;
        std::memcpy(end.Code, "ENDB", 4);
        WriteBlock(file, end);
        return file;
    }

private:
    struct Block {
        char Code[4] = {0, 0, 0, 0};
        int StructIndex = 0;
        uint32_t Count = 0;
        uint64_t Address = 0;
        std::vector<uint8_t> Body;
    };

    uint16_t AddType(const std::string& name, uint16_t length) {
        for (size_t i = 0; i < types_.size(); ++i)
            if (types_[i] == name) return static_cast<uint16_t>(i);
        types_.push_back(name);
        lengths_.push_back(length);
        return static_cast<uint16_t>(types_.size() - 1);
    }
    uint16_t AddName(const std::string& name) {
        for (size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == name) return static_cast<uint16_t>(i);
        names_.push_back(name);
        return static_cast<uint16_t>(names_.size() - 1);
    }

    size_t FieldSize(const std::string& type, const std::string& name) const {
        size_t base = 0;
        if (!name.empty() && name[0] == '*') {
            base = static_cast<size_t>(pointerSize_);
        } else {
            for (size_t i = 0; i < types_.size(); ++i)
                if (types_[i] == type) base = lengths_[i];
        }
        size_t multiplier = 1;
        for (size_t i = 0; i + 1 < name.size(); ++i) {
            if (name[i] != '[') continue;
            const size_t close = name.find(']', i);
            if (close == std::string::npos) break;
            multiplier *= static_cast<size_t>(std::atoi(name.c_str() + i + 1));
            i = close;
        }
        return base * multiplier;
    }

    void PutU32(std::vector<uint8_t>& out, uint32_t value) const {
        PutInt(out, value, 4);
    }
    void PutU16(std::vector<uint8_t>& out, uint16_t value) const {
        PutInt(out, value, 2);
    }

    void WriteBlock(std::vector<uint8_t>& file, const Block& block) const {
        file.insert(file.end(), block.Code, block.Code + 4);
        PutU32(file, static_cast<uint32_t>(block.Body.size()));
        PutPointer(file, block.Address);
        PutU32(file, static_cast<uint32_t>(block.StructIndex));
        PutU32(file, block.Count);
        file.insert(file.end(), block.Body.begin(), block.Body.end());
    }

    void Align4(std::vector<uint8_t>& out) const {
        while (out.size() % 4) out.push_back(0);
    }

    std::vector<uint8_t> EncodeSdna() const {
        std::vector<uint8_t> out;
        auto tag = [&out](const char* text) { out.insert(out.end(), text, text + 4); };

        tag("SDNA");
        tag("NAME");
        PutU32(out, static_cast<uint32_t>(names_.size()));
        for (const std::string& name : names_) {
            out.insert(out.end(), name.begin(), name.end());
            out.push_back(0);
        }
        Align4(out);

        tag("TYPE");
        PutU32(out, static_cast<uint32_t>(types_.size()));
        for (const std::string& type : types_) {
            out.insert(out.end(), type.begin(), type.end());
            out.push_back(0);
        }
        Align4(out);

        tag("TLEN");
        for (uint16_t length : lengths_) PutU16(out, length);
        Align4(out);

        tag("STRC");
        PutU32(out, static_cast<uint32_t>(structs_.size()));
        for (const auto& entry : structs_) {
            PutU16(out, entry.first);
            PutU16(out, static_cast<uint16_t>(entry.second.size()));
            for (const auto& field : entry.second) {
                PutU16(out, field.first);
                PutU16(out, field.second);
            }
        }
        return out;
    }

    int pointerSize_;
    bool bigEndian_;
    std::string version_;
    std::vector<std::string> types_;
    std::vector<uint16_t> lengths_;
    std::vector<std::string> names_;
    std::vector<std::pair<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>>> structs_;
    std::vector<Block> blocks_;
    std::map<std::string, size_t> structSize_;
    std::map<std::string, int> structIndex_;
};

// ===== A ONE-QUAD SCENE =====
//
// The smallest file that exercises the whole path: an Object placing a Mesh,
// the mesh a single quad with UVs and vertex colours, and one material.
// `arrange` lets a caller reorder the Mesh struct's fields, which is the point
// of the exercise.

struct SceneOptions {
    bool bigEndian = false;
    int pointerSize = 8;
    const char* version = "278";
    bool extraMeshField = false;   // insert a field before the ones that matter
    bool attributeLayout = false;  // the 3.6+ CustomData layers instead of MVert
    bool withColour = true;
    float scale = 1.0f;
};

static std::vector<uint8_t> QuadScene(const SceneOptions& options = SceneOptions()) {
    Builder b(options.pointerSize, options.bigEndian, options.version);

    b.DefineStruct("ID", {{"char", "name[66]"}, {"short", "pad"}, {"void", "*next"}});
    b.DefineStruct("MVert", {{"float", "co[3]"}, {"short", "no[3]"}, {"char", "flag"},
                             {"char", "bweight"}});
    b.DefineStruct("MPoly", {{"int", "loopstart"}, {"int", "totloop"}, {"short", "mat_nr"},
                             {"char", "flag"}, {"char", "pad"}});
    b.DefineStruct("MLoop", {{"int", "v"}, {"int", "e"}});
    b.DefineStruct("MLoopUV", {{"float", "uv[2]"}, {"int", "flag"}});
    b.DefineStruct("MLoopCol", {{"char", "r"}, {"char", "g"}, {"char", "b"}, {"char", "a"}});
    b.DefineStruct("CustomDataLayer", {{"int", "type"}, {"int", "offset"}, {"int", "flag"},
                                       {"char", "name[64]"}, {"void", "*data"}});
    b.DefineStruct("CustomData", {{"CustomDataLayer", "*layers"}, {"int", "totlayer"},
                                  {"int", "maxlayer"}});
    b.DefineStruct("Material", {{"ID", "id"}, {"float", "r"}, {"float", "g"}, {"float", "b"},
                                {"float", "specr"}, {"float", "specg"}, {"float", "specb"},
                                {"float", "alpha"}, {"float", "spec"}, {"float", "emit"},
                                {"short", "har"}});

    Builder::Fields meshFields;
    // A field the reader has never heard of, placed *before* everything it
    // reads. Nothing may shift as a result: that is what the SDNA is for.
    if (options.extraMeshField) meshFields.push_back({"double", "someFutureField[4]"});
    meshFields.push_back({"ID", "id"});
    meshFields.push_back({"Material", "**mat"});
    meshFields.push_back({"MVert", "*mvert"});
    meshFields.push_back({"MPoly", "*mpoly"});
    meshFields.push_back({"MLoop", "*mloop"});
    meshFields.push_back({"MLoopUV", "*mloopuv"});
    meshFields.push_back({"MLoopCol", "*mloopcol"});
    meshFields.push_back({"CustomData", "vdata"});
    meshFields.push_back({"CustomData", "ldata"});
    meshFields.push_back({"CustomData", "pdata"});
    meshFields.push_back({"int", "totvert"});
    meshFields.push_back({"int", "totpoly"});
    meshFields.push_back({"int", "totloop"});
    meshFields.push_back({"short", "totcol"});
    meshFields.push_back({"int", "*poly_offset_indices"});
    b.DefineStruct("Mesh", meshFields);

    b.DefineStruct("Object", {{"ID", "id"}, {"Object", "*parent"}, {"void", "*data"},
                              {"short", "type"}, {"float", "obmat[4][4]"}});

    // --- addresses ---
    const uint64_t kVerts = 0x1000, kPolys = 0x2000, kLoops = 0x3000, kUVs = 0x4000;
    const uint64_t kCols = 0x5000, kMatSlots = 0x6000, kMaterial = 0x7000;
    const uint64_t kMesh = 0x8000, kObject = 0x9000;
    const uint64_t kVertLayers = 0xA000, kLoopLayers = 0xB000;
    const uint64_t kPositionData = 0xC000, kCornerData = 0xD000, kPolyOffsets = 0xE000;

    // --- the quad ---
    const float points[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};

    std::vector<uint8_t> verts;
    for (const auto& point : points) {
        for (int i = 0; i < 3; ++i) b.PutFloat(verts, point[i] * options.scale);
        for (int i = 0; i < 3; ++i) b.PutInt(verts, 0, 2);
        b.Pad(verts, 2);
    }
    b.AddBlock("DATA", "MVert", 4, kVerts, verts);

    std::vector<uint8_t> polys;
    b.PutInt(polys, 0, 4);      // loopstart
    b.PutInt(polys, 4, 4);      // totloop
    b.PutInt(polys, 0, 2);      // mat_nr
    b.Pad(polys, 2);
    b.AddBlock("DATA", "MPoly", 1, kPolys, polys);

    std::vector<uint8_t> loops;
    for (int i = 0; i < 4; ++i) {
        b.PutInt(loops, i, 4);
        b.PutInt(loops, i, 4);
    }
    b.AddBlock("DATA", "MLoop", 4, kLoops, loops);

    std::vector<uint8_t> uvs;
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const auto& pair : uv) {
        b.PutFloat(uvs, pair[0]);
        b.PutFloat(uvs, pair[1]);
        b.PutInt(uvs, 0, 4);
    }
    b.AddBlock("DATA", "MLoopUV", 4, kUVs, uvs);

    std::vector<uint8_t> cols;
    for (int i = 0; i < 4; ++i) {
        b.PutInt(cols, 255, 1);
        b.PutInt(cols, 0, 1);
        b.PutInt(cols, 0, 1);
        b.PutInt(cols, 255, 1);
    }
    b.AddBlock("DATA", "MLoopCol", 4, kCols, cols);

    // --- the 3.6+ layout: the same quad as named attribute layers ---
    std::vector<uint8_t> positionData;
    for (const auto& point : points)
        for (int i = 0; i < 3; ++i) b.PutFloat(positionData, point[i] * options.scale);
    b.AddBlock("DATA", "float", 12, kPositionData, positionData);

    std::vector<uint8_t> cornerData;
    for (int i = 0; i < 4; ++i) b.PutInt(cornerData, i, 4);
    b.AddBlock("DATA", "int", 4, kCornerData, cornerData);

    // 4.0 replaced MPoly with one offsets array of faces + 1 entries, so face
    // i runs [offset[i], offset[i + 1]). One quad is {0, 4}.
    std::vector<uint8_t> polyOffsets;
    b.PutInt(polyOffsets, 0, 4);
    b.PutInt(polyOffsets, 4, 4);
    b.AddBlock("DATA", "int", 2, kPolyOffsets, polyOffsets);

    auto layer = [&b](std::vector<uint8_t>& out, const std::string& name, uint64_t data) {
        b.PutInt(out, 0, 4);                  // type
        b.PutInt(out, 0, 4);                  // offset
        b.PutInt(out, 0, 4);                  // flag
        b.PutBytes(out, name, 64);
        b.PutPointer(out, data);
    };
    std::vector<uint8_t> vertLayers, loopLayers;
    layer(vertLayers, "position", kPositionData);
    layer(loopLayers, ".corner_vert", kCornerData);
    b.AddBlock("DATA", "CustomDataLayer", 1, kVertLayers, vertLayers);
    b.AddBlock("DATA", "CustomDataLayer", 1, kLoopLayers, loopLayers);

    // --- the material, and the slot pointing at it ---
    std::vector<uint8_t> material;
    b.PutBytes(material, "MAPaint", 66);
    b.PutInt(material, 0, 2);
    b.PutPointer(material, 0);
    for (float value : {0.2f, 0.4f, 0.8f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.0f})
        b.PutFloat(material, value);
    b.PutInt(material, 50, 2);
    b.AddBlock("MA", "Material", 1, kMaterial, material);

    std::vector<uint8_t> slots;
    b.PutPointer(slots, kMaterial);
    b.AddBlock("DATA", "void", 1, kMatSlots, slots);

    // --- the mesh ---
    std::vector<uint8_t> mesh;
    if (options.extraMeshField) b.Pad(mesh, 32);
    b.PutBytes(mesh, "MEQuad", 66);
    b.PutInt(mesh, 0, 2);
    b.PutPointer(mesh, 0);
    b.PutPointer(mesh, kMatSlots);
    b.PutPointer(mesh, options.attributeLayout ? 0 : kVerts);
    b.PutPointer(mesh, options.attributeLayout ? 0 : kPolys);
    b.PutPointer(mesh, options.attributeLayout ? 0 : kLoops);
    b.PutPointer(mesh, options.attributeLayout ? 0 : kUVs);
    b.PutPointer(mesh, options.attributeLayout || !options.withColour ? 0 : kCols);
    // vdata
    b.PutPointer(mesh, options.attributeLayout ? kVertLayers : 0);
    b.PutInt(mesh, options.attributeLayout ? 1 : 0, 4);
    b.PutInt(mesh, 1, 4);
    // ldata
    b.PutPointer(mesh, options.attributeLayout ? kLoopLayers : 0);
    b.PutInt(mesh, options.attributeLayout ? 1 : 0, 4);
    b.PutInt(mesh, 1, 4);
    // pdata
    b.PutPointer(mesh, 0);
    b.PutInt(mesh, 0, 4);
    b.PutInt(mesh, 0, 4);
    b.PutInt(mesh, 4, 4);     // totvert
    b.PutInt(mesh, 1, 4);     // totpoly
    b.PutInt(mesh, 4, 4);     // totloop
    b.PutInt(mesh, 1, 2);     // totcol
    b.PutPointer(mesh, options.attributeLayout ? kPolyOffsets : 0);
    b.AddBlock("ME", "Mesh", 1, kMesh, mesh);

    // --- the object placing it ---
    std::vector<uint8_t> object;
    b.PutBytes(object, "OBQuadObject", 66);
    b.PutInt(object, 0, 2);
    b.PutPointer(object, 0);
    b.PutPointer(object, 0);        // parent
    b.PutPointer(object, kMesh);    // data
    b.PutInt(object, 1, 2);         // type = mesh
    const float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
    for (float value : matrix) b.PutFloat(object, value);
    b.AddBlock("OB", "Object", 1, kObject, object);

    return b.Finish();
}

static std::shared_ptr<ModelDocument> Read(const std::vector<uint8_t>& data,
                                           std::vector<std::string>* warnings = nullptr) {
    BlendConverter converter;
    ConversionOptions options;
    if (warnings)
        options.WarningCallback = [warnings](const std::string& w) { warnings->push_back(w); };
    return converter.ImportFromMemory(data, options);
}

// ===== TESTS =====

static void TestRecognition() {
    std::printf("Recognition\n");
    BlendConverter conv;
    Check(!conv.ValidateData({}), "empty data is not a .blend");
    Check(!conv.ValidateData(std::vector<uint8_t>{'v', ' ', '0', ' ', '0', ' ', '0'}),
          "nor is arbitrary text");
    Check(conv.ValidateData(QuadScene()), "an uncompressed .blend is recognised by its magic");
    Check(conv.ValidateData(std::vector<uint8_t>{0x1f, 0x8b, 0x08, 0x00}),
          "and a gzip wrapper is accepted, since a compressed .blend is one");

    const FormatCapabilities caps = conv.GetCapabilities();
    Check(caps.Meshes && caps.SceneGraph && caps.Materials && caps.NGons,
          "the capability report claims the geometry this reader produces");
    Check(!caps.Animations && !caps.Skinning && !caps.Textures,
          "and claims nothing it does not read - actions, deform, node-tree textures");
    Check(conv.CanImport() && !conv.CanExport(), "read-only, as declared");
}

static void TestQuad() {
    std::printf("A mesh, read through the SDNA\n");

    std::vector<std::string> warnings;
    auto doc = Read(QuadScene(), &warnings);
    Check(doc != nullptr, "a hand-built .blend reads");
    if (!doc) return;

    Check(doc->Meshes.size() == 1 && doc->Nodes.size() == 1, "one mesh under one node");
    Check(doc->TotalFaceCount() == 1, "one face");
    const MeshPrimitive& prim = doc->Meshes[0].Primitives[0];
    Check(prim.Mode == PrimitiveMode::Polygons && prim.Face(0).size() == 4,
          "and it is a quad, not a pair of triangles - MPoly states its own corner count");
    Check(doc->Nodes[0].Name == "QuadObject" && doc->Meshes[0].Name == "Quad",
          "the object and the mesh datablock keep their own names, minus Blender's prefix");

    Check(Near(doc->Nodes[0].Translation.x, 10.0, 1e-6) &&
          Near(doc->Nodes[0].Translation.y, 20.0, 1e-6) &&
          Near(doc->Nodes[0].Translation.z, 30.0, 1e-6),
          "obmat's last row is the translation - the sixteen floats copy across untransposed");

    Check(prim.FindAttribute(AttributeSemantic::TexCoord, 0) != nullptr,
          "the per-corner UVs are read");
    Check(prim.FindAttribute(AttributeSemantic::Color, 0) != nullptr,
          "and the per-corner colours");
    Check(!prim.Normals.empty(), "normals are generated, since Blender's stored ones are a cache");
    Check(doc->Materials.size() == 1 && doc->Materials[0].Name == "Paint",
          "the material slot resolves through its pointer");
    Check(Near(doc->Materials[0].BaseColorFactor.x, 0.2, 1e-5) &&
          Near(doc->Materials[0].BaseColorFactor.z, 0.8, 1e-5),
          "with the colour the material holds");
    Check(doc->Up == UpAxis::ZUp, "and the document is Z-up, which Blender is by definition");
    Check(warnings.empty(), "a file with no modifiers warns about nothing");
}

static void TestSdnaDrivesEverything() {
    std::printf("The SDNA is the authority, not a remembered layout\n");

    // The same scene with an unknown 32-byte field inserted at the *front* of
    // the Mesh struct. Every offset after it moves. A reader that remembered
    // where totvert lives reads garbage; one that asks the SDNA does not.
    SceneOptions moved;
    moved.extraMeshField = true;
    auto shifted = Read(QuadScene(moved));
    auto plain = Read(QuadScene());
    Check(shifted != nullptr && plain != nullptr, "both layouts read");
    if (shifted && plain) {
        Check(shifted->TotalFaceCount() == plain->TotalFaceCount() &&
              shifted->TotalVertexCount() == plain->TotalVertexCount(),
              "a field inserted ahead of every field the reader wants changes nothing");
        const Bounds3D a = shifted->ComputeBounds();
        const Bounds3D b = plain->ComputeBounds();
        Check(Near(a.Min.x, b.Min.x, 1e-9) && Near(a.Max.y, b.Max.y, 1e-9),
              "and the geometry lands in exactly the same place");
    }

    // 32-bit pointers, which every .blend written by a 32-bit build has.
    SceneOptions narrow;
    narrow.pointerSize = 4;
    auto small = Read(QuadScene(narrow));
    Check(small != nullptr && small->TotalFaceCount() == 1,
          "a 32-bit .blend reads, with every pointer four bytes wide");

    // Big-endian, which the PowerPC-era files are. Nothing here can be tested
    // against a real file any more, which is exactly why it is built.
    SceneOptions swapped;
    swapped.bigEndian = true;
    auto big = Read(QuadScene(swapped));
    Check(big != nullptr, "a big-endian .blend reads");
    if (big) {
        Check(big->TotalFaceCount() == 1 && Near(big->Nodes[0].Translation.y, 20.0, 1e-6),
              "with its integers, floats and pointers all byte-swapped");
    }
}

static void TestAttributeLayout() {
    std::printf("Both mesh layouts Blender has shipped\n");

    // From 3.6 the vertices are a "position" layer and the corners a
    // ".corner_vert" layer, with no MVert or MLoop at all. Same quad.
    SceneOptions modern;
    modern.attributeLayout = true;
    modern.version = "400";
    std::vector<std::string> warnings;
    auto attributes = Read(QuadScene(modern), &warnings);
    auto classic = Read(QuadScene());
    Check(attributes != nullptr, "a mesh stored as CustomData attribute layers reads");
    if (attributes && classic) {
        Check(attributes->TotalFaceCount() == classic->TotalFaceCount(),
              "giving the same faces as the MVert/MPoly/MLoop layout");
        const Bounds3D a = attributes->ComputeBounds();
        const Bounds3D b = classic->ComputeBounds();
        Check(Near(a.Min.x, b.Min.x, 1e-9) && Near(a.Max.x, b.Max.x, 1e-9) &&
              Near(a.Min.y, b.Min.y, 1e-9) && Near(a.Max.y, b.Max.y, 1e-9),
              "and the same geometry, to the last bit - two spellings of one mesh");
    }
}

static void TestRefusals() {
    std::printf("Refusals\n");
    BlendConverter conv;
    std::vector<std::string> warnings;

    // zstd is what Blender 3.0+ writes by default, and this reader has only
    // zlib. Saying which compression, and what to do instead, beats failing.
    const std::vector<uint8_t> zstd{0x28, 0xb5, 0x2f, 0xfd, 0x00, 0x00, 0x00, 0x00};
    Check(Read(zstd, &warnings) == nullptr, "a zstd .blend reads as nothing");
    bool saidZstd = false;
    for (const std::string& w : warnings)
        if (w.find("zstd") != std::string::npos) saidZstd = true;
    Check(saidZstd, "and the warning names zstd rather than failing obscurely");

    warnings.clear();
    Check(Read(std::vector<uint8_t>{'n', 'o', 't', 'a', 'b', 'l', 'e', 'n', 'd'}, &warnings) ==
                  nullptr,
          "non-Blender data reads as nothing");
    Check(!warnings.empty(), "and says so");

    // A block claiming more bytes than the file holds must stop the walk, not
    // be followed off the end of the buffer.
    warnings.clear();
    std::vector<uint8_t> truncated = QuadScene();
    truncated.resize(truncated.size() / 2);
    Check(Read(truncated, &warnings) == nullptr, "a file cut in half reads as nothing");
    Check(!warnings.empty(), "rather than running off the end of the block index");

    // The SDNA is the only thing that makes the rest readable; without it
    // there is nothing to do but say so.
    warnings.clear();
    std::vector<uint8_t> noDna = QuadScene();
    for (size_t i = 0; i + 4 < noDna.size(); ++i)
        if (std::memcmp(noDna.data() + i, "SDNA", 4) == 0) {
            std::memcpy(noDna.data() + i, "XXXX", 4);
            break;
        }
    Check(Read(noDna, &warnings) == nullptr, "a file whose SDNA will not parse reads as nothing");
    bool saidSdna = false;
    for (const std::string& w : warnings)
        if (w.find("SDNA") != std::string::npos) saidSdna = true;
    Check(saidSdna, "and names the SDNA as the reason");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    BlendConverter conv;
    Check(conv.ValidateFile(path), "the file is recognised as a .blend");

    // The inspector half, which a file browser uses without decoding geometry.
    const BlendFileInfo info = BlendConverter::Inspect(path);
    Check(info.Valid, "the header, block index and SDNA all parse");
    if (!info.Valid) {
        std::printf("  (%s)\n", info.Error.c_str());
        return;
    }
    Check(info.Version == "2.78", "the header version is read");
    Check(info.PointerSize == 8 && !info.BigEndian, "pointer size and endianness are read");
    Check(info.Compression == "gzip", "the gzip wrapper is inflated");
    Check(info.BlockCount > 2000, "the whole block index is walked");
    Check(info.ObjectNames.size() == 5, "all five objects are named");
    Check(Contains(info.ObjectNames, "Armature") && Contains(info.ObjectNames, "Cube.021"),
          "including the armature and the hull");
    Check(info.MeshNames.size() == 2 && Contains(info.MeshNames, "Cube.048"),
          "and both mesh datablocks");
    Check(info.MaterialNames.size() == 3 && Contains(info.MaterialNames, "ship"),
          "and all three materials");
    Check(Contains(info.Modifiers, "Mirror") && Contains(info.Modifiers, "Subsurf") &&
          Contains(info.Modifiers, "EdgeSplit"),
          "Mirror, Subsurf and EdgeSplit are all found, by SDNA struct name");
    Check(info.StoredVertexCount == 1147, "the stored cage is 1147 vertices");
    Check(info.StoredPolygonCount == 1030, "and 1030 polygons");

    // The geometry half.
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    auto doc = conv.Import(path, options);
    Check(doc != nullptr, "and the cage imports as geometry");
    if (!doc) return;

    Check(doc->SourceFormat == "blend" && doc->Up == UpAxis::ZUp, "read as Z-up Blender data");
    Check(doc->Generator == "Blender 2.78", "with the version off the header");
    // The same scene name the .dae's visual scene and the 7.4 binary .fbx
    // carry, which is what says all three came out of this file.
    Check(doc->Title == "E-45_GLSL",
          "titled E-45_GLSL - the name the .dae and the .fbx exports also carry");

    Check(doc->Nodes.size() == 5, "five nodes: the armature, both hulls, and the two lamps");
    Check(doc->Meshes.size() == 2, "two meshes");
    Check(doc->TotalFaceCount() == 1030,
          "1030 faces - exactly the polygon count the inspector reports");

    // Per-corner UVs split corners that share a vertex but not a UV, the same
    // resolution every other reader here performs.
    Check(doc->TotalVertexCount() == 1567,
          "1567 vertices: 1147 cage vertices split where their corner UVs differ");

    bool haveUv = false, haveColour = false;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            if (prim.FindAttribute(AttributeSemantic::TexCoord, 0)) haveUv = true;
            if (prim.FindAttribute(AttributeSemantic::Color, 0)) haveColour = true;
        }
    Check(haveUv && haveColour, "with the UV layer and the hull's vertex colour layer");
    Check(doc->Materials.size() == 2, "both materials that a mesh actually uses");

    // The parenting: both hulls hang off the armature, which is the chain the
    // .x3d and .dae exports write out as Transforms.
    int underArmature = 0;
    for (const ModelNode& node : doc->Nodes)
        if (node.Parent >= 0 && doc->Nodes[static_cast<size_t>(node.Parent)].Name == "Armature")
            ++underArmature;
    Check(underArmature == 2, "both hulls are parented to the armature");

    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.0000, 1e-3) &&
          Near(bounds.Min.y, -3.2113, 1e-3) && Near(bounds.Max.y, 2.9584, 1e-3) &&
          Near(bounds.Min.z, -1.3556, 1e-3) && Near(bounds.Max.z, 2.8555, 1e-3),
          "world bounds match an independent walk of every object's obmat");

    // The assertion this reader exists to keep honest. The hull stops dead at
    // x = 0 because the Mirror modifier is *not* applied - half the aircraft
    // is not in the file. Every other export of this scene is symmetric or
    // spans both sides; this one is the cage.
    Check(Near(bounds.Max.x, 0.0, 1e-6),
          "and the hull stops dead at x = 0: the mirror is unapplied, so this is half an aircraft");
    Check(doc->Metadata.count("blend.unappliedModifiers") == 1,
          "which the document records rather than leaving to be discovered");

    bool warned = false;
    for (const std::string& w : warnings)
        if (w.find("Mirror") != std::string::npos && w.find("cage") != std::string::npos)
            warned = true;
    Check(warned, "and a warning names the modifiers and says the cage is what was read");
}

int main(int argc, char** argv) {
    TestRecognition();
    TestQuad();
    TestSdnaDrivesEverything();
    TestAttributeLayout();
    TestRefusals();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass a .blend path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
