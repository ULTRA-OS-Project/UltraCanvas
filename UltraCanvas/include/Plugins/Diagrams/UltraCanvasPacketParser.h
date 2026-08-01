// include/Plugins/Diagrams/UltraCanvasPacketParser.h
// Text spec -> PacketModel, and PacketModel -> text.
//
// The baseline grammar is Mermaid's `packet` syntax verbatim, so a diagram
// copied out of Markdown parses unchanged:
//
//     packet
//     title UDP Packet
//     +16: "Source Port"
//     +16: "Destination Port"
//     32-47: "Length"
//     48-63: "Checksum"
//
// UltraCanvas extensions are trailing @modifiers, which Mermaid ignores, so
// stripping them always leaves a valid Mermaid diagram:
//
//     64-95: "Data" @payload
//     0-3:   "Version" @const=4
//     layer ip "IP Header" @color=#C6DBEF
//
// Dependency-free apart from the model: no render context, no file I/O.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Diagrams/UltraCanvasPacketModel.h"
#include <string>
#include <vector>

namespace UltraCanvas {

    struct PacketParseError {
        size_t      line = 0;       // 1-based
        std::string message;
        std::string text;           // the offending line, verbatim
    };

    struct PacketParseResult {
        PacketModel model;
        std::vector<PacketParseError> errors;
        bool Ok() const { return errors.empty(); }
    };

// Parse a Mermaid-compatible packet spec.
//
// Recognised statements:
//   packet | packet-beta          diagram header (optional)
//   title <text>                  diagram title
//   caption <text>                figure caption            (UltraCanvas)
//   layer <id> "<name>"           start a new layer         (UltraCanvas)
//   <start>-<end>: "<label>"      explicit inclusive bit range
//   <bit>: "<label>"              single bit
//   +<count>: "<label>"           auto-advancing bit count
//   %% comment                    ignored
//
// Trailing modifiers (all UltraCanvas extensions, all optional):
//   @payload @reserved @padding @flag @checksum @marker @variable @ellipsis
//   @const=<text>   @attr=<text>   @desc=<text>   @color=#RRGGBB
    PacketParseResult ParsePacketText(const std::string& text);

// Emit a Mermaid-compatible spec. When includeExtensions is false the output
// is plain Mermaid; when true, @modifiers carry the UltraCanvas detail.
    std::string EmitPacketText(const PacketModel& model, bool includeExtensions = true);

// Emit the RFC-style ASCII diagram (the `+-+-+` form used in RFC figures).
    std::string EmitPacketAscii(const PacketModel& model, bool showBitRuler = true);

} // namespace UltraCanvas
