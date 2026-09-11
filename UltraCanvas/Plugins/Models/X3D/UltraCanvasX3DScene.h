// Plugins/Models/X3D/UltraCanvasX3DScene.h
// An X3D scene as a tree of typed nodes, independent of which of the
// standard's encodings it arrived in.
//
// X3D is one node set with three encodings: XML (.x3d), Classic VRML (.x3dv)
// and a binary one. VRML97 (.wrl) is that same classic syntax, one revision
// earlier. They differ in spelling and in nothing else:
//
//     <Transform translation='0 1 0'>        Transform {
//       <Shape>                                translation 0 1 0
//         <Box size='2 2 2'/>                  children [ Shape {
//       </Shape>                                 geometry Box { size 2 2 2 }
//     </Transform>                             } ]
//                                            }
//
// Both are the same three nodes with the same two fields. So this layer is the
// seam: each encoding has its own reader that produces this tree, and
// UltraCanvasX3DConverter.cpp reads the tree without knowing which one ran.
// That is the same container/semantics split the STEP, Alembic, FBX and .x
// readers use, and it is what stops the node set being implemented twice.
//
// The two encodings state the *same* structure differently in one place worth
// naming. XML nests a node inside its parent element and names the field it
// fills with a `containerField` attribute, defaulting by type. Classic VRML
// writes the field name first and the node second, and gathers multiple nodes
// in brackets:
//
//     <IndexedFaceSet><Coordinate point='...'/></IndexedFaceSet>
//     IndexedFaceSet { coord Coordinate { point [ ... ] } }
//
// Either way this tree holds one `IndexedFaceSet` with one `Coordinate` child,
// and `ContainerField` carries "coord" when the file said it. The reader finds
// a geometry's streams by node type, which both encodings agree on, so the
// field name is recorded rather than relied upon.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_X3D_SCENE_H
#define ULTRACANVAS_X3D_SCENE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace X3D {

// One node. `Name` is the node type - "Transform", "IndexedFaceSet" - and
// `Fields` holds its field values as text, which is the form the XML encoding
// stores natively and the one the classic encoding is rendered into.
struct Node {
    std::string Name;
    std::string ContainerField;   // the parent field this fills; may be empty
    std::vector<std::pair<std::string, std::string>> Fields;
    std::vector<Node> Children;

    // The field's text, or nullptr when the node does not state it. A field
    // that is absent and a field that is present but empty are different: the
    // first takes the spec's default, the second states emptiness.
    const char* Attribute(const std::string& name) const {
        for (const auto& field : Fields)
            if (field.first == name) return field.second.c_str();
        return nullptr;
    }

    bool Has(const std::string& name) const { return Attribute(name) != nullptr; }

    void Set(std::string name, std::string value) {
        for (auto& field : Fields) {
            if (field.first == name) {
                field.second = std::move(value);
                return;
            }
        }
        Fields.emplace_back(std::move(name), std::move(value));
    }

    // The first child of that node type, or null.
    const Node* Find(const std::string& type) const {
        for (const Node& child : Children)
            if (child.Name == type) return &child;
        return nullptr;
    }
};

enum class Encoding { Xml, ClassicVrml };

struct Scene {
    Encoding How = Encoding::Xml;
    // As the file spells it: "3.3" for X3D, "2.0" for a VRML97 header.
    std::string Version;
    std::string Profile;
    bool Vrml97 = false;          // a #VRML header rather than an X3D one
    Node Root;                    // the X3D node; its children are head/Scene
};

// ===== RECOGNITION =====
//
// Both encodings announce themselves in their first line, and neither can be
// mistaken for the other: XML opens with a declaration or an <X3D element, and
// the classic encoding opens with a `#VRML` or `#X3D` comment that the spec
// makes mandatory.

bool LooksLikeX3DXml(const std::string& head);
bool LooksLikeClassicVrml(const std::string& head);

// ===== THE CLASSIC ENCODING =====
//
// Reads `#VRML V2.0 utf8` and `#X3D V3.0 utf8` files into the tree above.
// `warn` may be null. On false, `error` says what stopped it.
bool ParseClassicVrml(const std::vector<uint8_t>& data, Scene& out, std::string& error,
                      const std::function<void(const std::string&)>& warn = nullptr);

} // namespace X3D
} // namespace UltraCanvas

#endif // ULTRACANVAS_X3D_SCENE_H
