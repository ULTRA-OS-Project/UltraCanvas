// include/Plugins/Diagrams/UltraCanvasPacketModel.h
// Protocol data unit (PDU) field model for the packet structure diagram.
//
// Header-only and dependency-free apart from UltraCanvasCommonTypes (for
// Color): no render context, no UI element, no I/O. This is deliberate so the
// model and the layout engine can be unit-tested without a window.
//
// Everything is expressed in BITS. Bytes are a presentation choice made by the
// ruler and the label formatter, not a property of the model.
//
// These elements know nothing about networking: they consume a field table and
// optionally a byte buffer, and they perform no I/O and no protocol parsing.
// There is no dependency on Plugins/UltraNet in either direction.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

    enum class PacketFieldKind {
        Fixed,        // ordinary field of known bit length
        Flag,         // 1-bit; renders with stacked/rotated text
        Reserved,     // greyed, usually unnamed
        Padding,      // hatched
        LengthPrefix, // this field's value gives the length of another
        Variable,     // length not known statically (options)
        Payload,      // open-ended region, special shape/fill
        Checksum,     // CRC/FCS block
        Marker,       // start bit / end bit / delimiter
        Ellipsis      // "...", a repetition elision
    };

    enum class PacketBitNumbering {
        MsbFirst,     // RFC convention: bit 0 is the most significant
        LsbFirst      // register convention (WaveDrom default)
    };

    enum class PacketEndianness {
        Big,          // network byte order
        Little
    };

// =============================================================================
// FIELD
// =============================================================================

    struct PacketField {
        std::string id;
        std::string name;
        std::string description;

        uint64_t bitOffset = 0;     // absolute within its layer
        uint64_t bitLength = 0;     // 0 => variable / runs to the end

        PacketFieldKind kind = PacketFieldKind::Fixed;

        // Rendered as "name=constantText", e.g. Protocol=6 (TCP).
        std::string constantText;

        // WaveDrom-style second line under the name: "RO", "RW", "MBZ".
        std::string attrText;

        // Host-supplied value text (the P1 escape hatch: any application with
        // its own decoder can fill this without the widget parsing anything).
        std::string valueText;

        std::vector<PacketField> children;   // sub-fields, e.g. DSCP|ECN in DS

        bool  hasColorOverride = false;
        Color color;

        void* userData = nullptr;

        PacketField() = default;
        PacketField(const std::string& fieldName, uint64_t bits,
                    PacketFieldKind fieldKind = PacketFieldKind::Fixed)
                : name(fieldName), bitLength(bits), kind(fieldKind) {}

        uint64_t BitEnd() const { return bitOffset + bitLength; }
        bool IsVariableLength() const {
            return bitLength == 0 ||
                   kind == PacketFieldKind::Variable ||
                   kind == PacketFieldKind::Payload;
        }

        // Fluent helpers so template tables read cleanly.
        PacketField& SetConstantText(const std::string& t) { constantText = t; return *this; }
        PacketField& SetAttrText(const std::string& t)     { attrText = t;     return *this; }
        PacketField& SetDescription(const std::string& t)  { description = t;  return *this; }
        PacketField& SetColor(const Color& c) {
            color = c; hasColorOverride = true; return *this;
        }
        PacketField& AddChild(const std::string& childName, uint64_t bits,
                              PacketFieldKind childKind = PacketFieldKind::Fixed) {
            PacketField c(childName, bits, childKind);
            c.bitOffset = children.empty() ? bitOffset : children.back().BitEnd();
            children.push_back(c);
            return *this;
        }
        PacketField& AddChildBytes(const std::string& childName, uint64_t bytes,
                                   PacketFieldKind childKind = PacketFieldKind::Fixed) {
            return AddChild(childName, bytes * 8, childKind);
        }
    };

// =============================================================================
// LAYER
// =============================================================================

// One protocol layer, e.g. "IP Header" or "TCP". Layers stack in declaration
// order; each one's fields are offset from the start of that layer.
    struct PacketLayer {
        std::string id;
        std::string name;
        std::string bracketLabel;    // defaults to name when empty

        std::vector<PacketField> fields;

        bool  hasColor = false;
        Color color;
        bool  collapsed = false;

        PacketLayer() = default;
        PacketLayer(const std::string& layerId, const std::string& layerName)
                : id(layerId), name(layerName) {}

        // Append a field immediately after the previous one.
        PacketField& AddField(const std::string& name, uint64_t bits,
                              PacketFieldKind kind = PacketFieldKind::Fixed) {
            PacketField f(name, bits, kind);
            f.bitOffset = fields.empty() ? 0 : fields.back().BitEnd();
            fields.push_back(f);
            return fields.back();
        }

        PacketField& AddFieldBytes(const std::string& name, uint64_t bytes,
                                   PacketFieldKind kind = PacketFieldKind::Fixed) {
            return AddField(name, bytes * 8, kind);
        }

        // Append at an explicit offset (mixing the two styles is allowed, the
        // way Mermaid allows mixing "0-15:" ranges with "+16:").
        PacketField& AddFieldAt(const std::string& name, uint64_t startBit,
                                uint64_t endBitInclusive,
                                PacketFieldKind kind = PacketFieldKind::Fixed) {
            PacketField f(name, endBitInclusive - startBit + 1, kind);
            f.bitOffset = startBit;
            fields.push_back(f);
            return fields.back();
        }

        uint64_t TotalBits() const {
            uint64_t end = 0;
            for (const auto& f : fields) end = std::max(end, f.BitEnd());
            return end;
        }

        const PacketField* FindField(const std::string& nameOrId) const {
            for (const auto& f : fields) {
                if (f.id == nameOrId || f.name == nameOrId) return &f;
            }
            return nullptr;
        }
        PacketField* FindField(const std::string& nameOrId) {
            return const_cast<PacketField*>(
                    static_cast<const PacketLayer*>(this)->FindField(nameOrId));
        }
    };

// =============================================================================
// MODEL
// =============================================================================

    struct PacketModel {
        std::string title;
        std::string caption;
        std::string trailerText;      // e.g. "data begins here ..."

        std::vector<PacketLayer> layers;

        uint32_t bitsPerRow = 32;
        PacketBitNumbering numbering = PacketBitNumbering::MsbFirst;
        PacketEndianness   endianness = PacketEndianness::Big;

        PacketLayer& AddLayer(const std::string& id, const std::string& name) {
            layers.emplace_back(id, name);
            return layers.back();
        }

        uint64_t TotalBits() const {
            uint64_t total = 0;
            for (const auto& l : layers) total += l.TotalBits();
            return total;
        }

        // Bit offset at which a layer starts, counted from the start of the PDU.
        uint64_t LayerBitOffset(size_t layerIndex) const {
            uint64_t offset = 0;
            for (size_t i = 0; i < layerIndex && i < layers.size(); ++i) {
                offset += layers[i].TotalBits();
            }
            return offset;
        }

        const PacketLayer* FindLayer(const std::string& id) const {
            for (const auto& l : layers) if (l.id == id) return &l;
            return nullptr;
        }
        PacketLayer* FindLayer(const std::string& id) {
            return const_cast<PacketLayer*>(
                    static_cast<const PacketModel*>(this)->FindLayer(id));
        }

        bool IsEmpty() const {
            for (const auto& l : layers) if (!l.fields.empty()) return false;
            return true;
        }
    };

// =============================================================================
// VALIDATION
// =============================================================================

    enum class PacketValidationSeverity { Info, Warning, Error };

    struct PacketValidationIssue {
        PacketValidationSeverity severity = PacketValidationSeverity::Warning;
        std::string message;
        std::string layerId;
        std::string fieldId;      // name when the field carries no explicit id
        uint64_t    bitOffset = 0;
    };

    struct PacketValidationResult {
        std::vector<PacketValidationIssue> issues;

        bool HasErrors() const {
            for (const auto& i : issues) {
                if (i.severity == PacketValidationSeverity::Error) return true;
            }
            return false;
        }
        bool IsClean() const { return issues.empty(); }
        size_t Count(PacketValidationSeverity s) const {
            size_t n = 0;
            for (const auto& i : issues) if (i.severity == s) ++n;
            return n;
        }
    };

// V1 overlap, V2 gap, V3 total length, V5 duplicate/empty/zero-length.
// expectedTotalBits == 0 disables the V3 check.
    PacketValidationResult ValidatePacketModel(const PacketModel& model,
                                               uint64_t expectedTotalBits = 0);

// Fill detected gaps with Reserved fields, in place. Returns the number added.
    size_t FillPacketModelGaps(PacketModel& model);

} // namespace UltraCanvas
