// include/Plugins/Diagrams/UltraCanvasMindMapIO.h
// Mind map interchange: Mermaid, FreeMind/Freeplane .mm, OPML, indented text,
// CSV, XMind and SVG export
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// CHANGELOG 1.0.0 (initial - MindMap proposal Phase 2, X3-X8):
//  - Mermaid `mindmap` import and export, including the shape delimiters,
//    ::icon() and :::class syntax.
//  - FreeMind / Freeplane .mm import and export (tinyxml2).
//  - OPML import and export (tinyxml2).
//  - Plain indented text and parent/child CSV import.
//  - XMind (.xmind) read support - zip container plus content.json.
//  - SVG export of a laid-out map at arbitrary scale.
//
// Every entry point operates on MindMapModel and needs no UI stack, so the
// whole module is unit-tested headlessly (Tests/MindMapIOTest.cpp). The three
// format families the framework already vendors are reused: UltraCanvasJSON,
// tinyxml2 and UCZipPackageReader. No new third-party dependency.

#pragma once

#include "Plugins/Diagrams/UltraCanvasMindMapLayout.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// Result of an import. On failure `error` explains why and the model is left
// untouched, so a failed import can never half-destroy an open map.
struct MindMapImportResult {
    bool success = false;
    std::string error;
    size_t topicsImported = 0;

    explicit operator bool() const { return success; }
};

// Styling needed to draw an SVG. Supplied by the caller (usually the element)
// so this module stays independent of MindMapStyle and the render context.
struct MindMapSvgOptions {
    double scale = 1.0;
    double padding = 40.0;
    std::string fontFamily = "Arial";
    Color backgroundColor = Color(255, 255, 255, 255);
    Color connectorColor = Color(150, 150, 160, 255);
    double connectorWidth = 2.0;
    // Called for each topic; lets the caller apply its own level/branch styling.
    std::function<MindMapTopicStyle(const MindMapTopic&)> styleOf;
};

class UltraCanvasMindMapIO {
public:
    // =========================================================================
    // MERMAID (X3)
    // =========================================================================

    // Parses a Mermaid `mindmap` block. Understands indentation nesting, the
    // shape delimiters ((circle)), [square], (rounded), {{hexagon}},
    // ))cloud((, >bang], plus ::icon(...) and :::class lines.
    static MindMapImportResult FromMermaid(MindMapModel& model, const std::string& source);
    static std::string ToMermaid(const MindMapModel& model);

    // =========================================================================
    // FREEMIND / FREEPLANE .mm (X4)
    // =========================================================================

    static MindMapImportResult FromFreeMind(MindMapModel& model, const std::string& xml);
    static std::string ToFreeMind(const MindMapModel& model);

    // =========================================================================
    // OPML (X5)
    // =========================================================================

    static MindMapImportResult FromOpml(MindMapModel& model, const std::string& xml);
    static std::string ToOpml(const MindMapModel& model, const std::string& title = "Mind Map");

    // =========================================================================
    // PLAIN TEXT & CSV (X6)
    // =========================================================================

    // Indented text: tabs, or `spacesPerLevel` spaces, per nesting level.
    static MindMapImportResult FromIndentedText(MindMapModel& model, const std::string& text,
                                                int spacesPerLevel = 2);
    static std::string ToIndentedText(const MindMapModel& model, int spacesPerLevel = 2);

    // CSV with a header row. Requires an id column and a parent column; the
    // text column defaults to "text"/"label"/"title" when unnamed. Rows may
    // appear in any order - parents are resolved after the whole file is read.
    static MindMapImportResult FromCsv(MindMapModel& model, const std::string& csv,
                                       const std::string& idColumn = "id",
                                       const std::string& parentColumn = "parent",
                                       const std::string& textColumn = std::string());
    static std::string ToCsv(const MindMapModel& model);

    // =========================================================================
    // XMIND (X8)
    // =========================================================================

    // Reads a .xmind file: a zip container whose content.json holds the sheet
    // and its root topic. Older archives store content.xml instead, which is
    // reported as an unsupported-version error rather than silently ignored.
    static MindMapImportResult FromXMindFile(MindMapModel& model, const std::string& filePath);
    // Same, for an already-extracted content.json payload.
    static MindMapImportResult FromXMindJson(MindMapModel& model, const std::string& json);

    // =========================================================================
    // SVG EXPORT (X7)
    // =========================================================================

    // Renders the CURRENT layout (call ComputeLayout first) to standalone SVG.
    static std::string ToSvg(const MindMapModel& model,
                             const MindMapLayoutResult& layout,
                             const MindMapSvgOptions& options = MindMapSvgOptions());
    static bool WriteSvgFile(const std::string& filePath,
                             const MindMapModel& model,
                             const MindMapLayoutResult& layout,
                             const MindMapSvgOptions& options = MindMapSvgOptions());

    // =========================================================================
    // FORMAT DETECTION
    // =========================================================================

    enum class Format {
        Unknown,
        Mermaid,
        FreeMind,
        Opml,
        Markdown,
        IndentedText,
        Csv,
        Json
    };

    // Sniffs a text payload. Never reads the filesystem.
    static Format DetectFormat(const std::string& text);
    // Maps a file extension (with or without the dot) to a format.
    static Format FormatFromExtension(const std::string& extension);
};

} // namespace UltraCanvas
