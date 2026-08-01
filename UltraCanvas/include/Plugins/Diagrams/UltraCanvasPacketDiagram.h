// include/Plugins/Diagrams/UltraCanvasPacketDiagram.h
// Packet structure diagram element: a bit-accurate map of a protocol data unit.
//
// Draws a PacketModel in one of four layouts - RFC word grid, proportional
// linear strip, encapsulation bands, or signal lanes - with bit rulers, offset
// and word gutters, dimension rails, side braces, layer brackets, highlight
// sets, sub-fields and tooltips.
//
// This is a DRAWING WIDGET. It performs no I/O, opens no sockets and knows no
// protocol semantics; it consumes a field table and optionally a byte buffer.
// There is no dependency on Plugins/UltraNet in either direction.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Diagrams/UltraCanvasPacketModel.h"
#include "Plugins/Diagrams/UltraCanvasPacketLayout.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

    enum class PacketRulerMode {
        NoRuler,
        BitRuler,       // 0 1 2 ... 31 across the top
        ByteColumns     // "Byte 1 | Byte 2 | Byte 3 | Byte 4"
    };

// Image 6 writes two-digit ruler labels vertically, one digit per line.
    enum class PacketRulerLabelStyle {
        Horizontal,
        StackedDigits
    };

    enum class PacketRowIndexMode {
        NoIndex,
        ByteOffset,     // 0, 4, 8 ...
        BitOffset,      // 0, 32, 64 ...
        HexOffset,      // 0x00, 0x04 ...
        WordNumber      // 1, 2, 3 ... (image 6's "Words" gutter)
    };

    enum class PacketEdge { Top, Bottom, Left, Right };

    enum class PacketColorMode {
        ByLayer,        // each protocol layer gets its own tint
        ByKind,         // payload / reserved / checksum palettes
        BySourceByte,   // quantised ramp over the byte index (image 5)
        Uniform
    };

    enum class PacketPayloadStyle {
        Flat,
        Gradient,
        GradientCurved, // image 2: soft gradient with a curved top-left corner
        Ragged          // torn right edge: "continues beyond the figure"
    };

    enum class PacketTheme { Light, Dark, Monochrome };

// =============================================================================
// ANNOTATIONS
// =============================================================================

// A double-headed dimension arrow with a label (image 2's "4 bytes (32 bits)").
    struct PacketDimensionRail {
        PacketEdge edge = PacketEdge::Top;
        uint64_t   startBit = 0;
        uint64_t   endBit = 0;       // inclusive
        std::string label;
    };

// A brace spanning a layer, on either side. Image 6 has both at once.
    struct PacketBrace {
        PacketEdge  edge = PacketEdge::Left;
        std::string layerId;         // empty = the whole diagram
        std::string label;
    };

// =============================================================================
// CALLBACK PAYLOAD
// =============================================================================

    struct PacketFieldRef {
        std::string layerId;
        std::string fieldId;
        std::string name;
        uint64_t    bitOffset = 0;
        uint64_t    bitLength = 0;
        size_t      layerIndex = 0;
        size_t      fieldIndex = 0;

        uint64_t ByteOffset() const { return bitOffset / 8; }
        bool IsValid() const { return bitLength > 0 || !name.empty(); }
    };

// =============================================================================
// THE ELEMENT
// =============================================================================

    class UltraCanvasPacketDiagram : public UltraCanvasUIElement {
    public:
        UltraCanvasPacketDiagram(const std::string& id, float x, float y,
                                 float width, float height);
        ~UltraCanvasPacketDiagram() override = default;

        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== MODEL =====
        void SetModel(const PacketModel& model);
        const PacketModel& GetModel() const { return model; }
        PacketModel& GetMutableModel() { Invalidate(); return model; }

        PacketLayer& AddLayer(const std::string& id, const std::string& name);
        void ClearModel();

        // Load a Mermaid-compatible text spec. Returns false on parse errors;
        // GetParseErrors() then describes them.
        bool LoadFromText(const std::string& text);
        const std::vector<std::string>& GetParseErrors() const { return parseErrors; }

        // ===== LAYOUT =====
        void SetLayoutMode(PacketLayoutMode mode);
        PacketLayoutMode GetLayoutMode() const { return options.mode; }

        void SetBitsPerRow(uint32_t bits);
        uint32_t GetBitsPerRow() const { return options.bitsPerRow; }

        void SetRowHeight(double h);
        void SetMinCellWidth(double w);
        void SetLengthScale(PacketLengthScale scale, double minCellPx = 0.0);
        void SetSizeUnit(PacketSizeUnit unit);
        void SetLanes(const std::vector<std::string>& laneNames);

        // ===== RULERS & GUTTERS =====
        void SetRulerMode(PacketRulerMode mode);
        void SetRulerLabelStyle(PacketRulerLabelStyle style);
        void SetRulerTitle(const std::string& t);
        // Explicit tick positions. Image 6 ticks at 0,4,8,...,31 - note 31, not 32.
        void SetRulerTicks(const std::vector<uint32_t>& bits);
        void SetOffsetGutter(PacketRowIndexMode mode, const std::string& title = "");
        void SetRowIndexGutter(PacketRowIndexMode mode, const std::string& title = "");

        // ===== ANNOTATIONS =====
        void AddDimensionRail(PacketEdge edge, uint64_t startBit, uint64_t endBit,
                              const std::string& label);
        void AddBrace(PacketEdge edge, const std::string& layerId,
                      const std::string& label);
        void SetLayerBrackets(PacketEdge edge);      // auto-brace every layer
        void ClearAnnotations();

        void SetTitle(const std::string& t);
        void SetCaption(const std::string& t);
        void SetTrailerText(const std::string& t);   // "data begins here ..."

        // ===== COLOUR & THEME =====
        void SetTheme(PacketTheme theme);
        void SetColorMode(PacketColorMode mode);
        void SetPayloadStyle(PacketPayloadStyle style);
        void SetRowBanding(bool enabled, const Color& a = Color(214, 231, 245, 255),
                           const Color& b = Color(186, 213, 235, 255));
        void SetHighlightFields(const std::vector<std::string>& names,
                                const Color& color);
        void ClearHighlights();

        // ===== LEGEND (the shared component) =====
        ChartLegend& GetLegend() { Invalidate(); return legend; }
        const ChartLegend& GetLegend() const { return legend; }
        void SetShowLegend(bool show);

        // ===== VALUES =====
        // P1 escape hatch: any host with its own decoder supplies values without
        // the widget parsing anything.
        void SetFieldValueText(const std::string& fieldNameOrId,
                               const std::string& text);
        void SetShowFieldValues(bool show);

        // ===== VALIDATION =====
        PacketValidationResult Validate(uint64_t expectedTotalBits = 0) const;
        void SetShowValidationBadges(bool show);

        // ===== INTERACTION =====
        void SetOnFieldClick(std::function<void(const PacketFieldRef&)> cb) {
            onFieldClick = std::move(cb);
        }
        void SetOnFieldHover(std::function<void(const PacketFieldRef&)> cb) {
            onFieldHover = std::move(cb);
        }
        void SetSelectedField(const std::string& fieldNameOrId);
        void ClearSelection();
        PacketFieldRef GetSelectedField() const;

        void SetShowTooltips(bool show) { showTooltips = show; }

        void Invalidate() { layoutValid = false; RequestRedraw(); }

    private:
        PacketModel model;
        PacketLayoutOptions options;
        PacketLayoutResult layout;
        bool layoutValid = false;
        Rect2Dd lastLayoutArea;

        // Rulers & gutters
        PacketRulerMode rulerMode = PacketRulerMode::BitRuler;
        PacketRulerLabelStyle rulerLabelStyle = PacketRulerLabelStyle::Horizontal;
        std::string rulerTitle;
        std::vector<uint32_t> rulerTicks;         // empty = automatic
        PacketRowIndexMode offsetGutterMode = PacketRowIndexMode::NoIndex;
        PacketRowIndexMode wordGutterMode = PacketRowIndexMode::NoIndex;
        std::string offsetGutterTitle;
        std::string wordGutterTitle;

        // Annotations
        std::vector<PacketDimensionRail> rails;
        std::vector<PacketBrace> braces;
        std::vector<std::string> laneNames;

        // Style
        PacketTheme theme = PacketTheme::Light;
        PacketColorMode colorMode = PacketColorMode::ByLayer;
        PacketPayloadStyle payloadStyle = PacketPayloadStyle::Gradient;
        PacketSizeUnit sizeUnit = PacketSizeUnit::Auto;
        bool  rowBanding = false;
        Color bandColorA = Color(214, 231, 245, 255);
        Color bandColorB = Color(186, 213, 235, 255);

        Color bgColor = Colors::White;
        Color cellFill = Color(232, 240, 248, 255);
        Color cellBorder = Color(70, 80, 95, 255);
        Color textColor = Color(25, 30, 40, 255);
        Color rulerColor = Color(60, 65, 75, 255);
        Color gutterColor = Color(90, 95, 105, 255);
        Color reservedColor = Color(210, 210, 210, 255);
        Color highlightColor = Color(160, 205, 160, 255);

        std::vector<std::string> highlightNames;
        std::string title;
        std::string caption;

        float fontSize = 11.0f;
        float titleFontSize = 14.0f;
        std::string fontFamily = "Arial";

        // Legend
        ChartLegend legend;
        bool showLegend = false;

        // Values & validation
        bool showFieldValues = false;
        bool showValidationBadges = false;

        // Interaction
        bool showTooltips = true;
        size_t hoveredCell = SIZE_MAX;
        size_t selectedCell = SIZE_MAX;
        std::vector<std::string> parseErrors;
        std::function<void(const PacketFieldRef&)> onFieldClick;
        std::function<void(const PacketFieldRef&)> onFieldHover;

        // ----- internals -----
        void EnsureLayout(IRenderContext* ctx);
        Rect2Dd ContentArea() const;
        double MeasureRulerHeight(IRenderContext* ctx) const;
        double MeasureGutterWidth(IRenderContext* ctx, PacketRowIndexMode mode) const;
        double MeasureBraceWidth(IRenderContext* ctx, PacketEdge edge) const;

        void RenderBackground(IRenderContext* ctx);
        void RenderBanding(IRenderContext* ctx);
        void RenderCells(IRenderContext* ctx);
        void RenderCell(IRenderContext* ctx, const PacketCell& cell, size_t index);
        void RenderCellText(IRenderContext* ctx, const PacketCell& cell);
        void RenderRuler(IRenderContext* ctx);
        void RenderGutters(IRenderContext* ctx);
        void RenderBraces(IRenderContext* ctx);
        void RenderRails(IRenderContext* ctx);
        void RenderTitleAndCaption(IRenderContext* ctx);

        Color CellColor(const PacketCell& cell) const;
        bool  IsHighlighted(const PacketCell& cell) const;
        std::string CellLabel(const PacketCell& cell) const;
        PacketFieldRef MakeFieldRef(const PacketCell& cell) const;
        const PacketField* FieldOfCell(const PacketCell& cell) const;

        void DrawArrowLine(IRenderContext* ctx, const Point2Dd& from,
                           const Point2Dd& to, bool arrowStart, bool arrowEnd);
        void DrawStackedDigits(IRenderContext* ctx, const std::string& digits,
                               double cx, double topY);
        void DrawVerticalText(IRenderContext* ctx, const std::string& text,
                              const Rect2Dd& cell);
        void ApplyTheme();
    };

// =============================================================================
// FACTORY
// =============================================================================

    std::shared_ptr<UltraCanvasPacketDiagram> CreatePacketDiagram(
            const std::string& id, float x, float y, float width, float height);

} // namespace UltraCanvas
