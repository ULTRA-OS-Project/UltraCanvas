// include/Plugins/Diagrams/UltraCanvasParliamentDiagram.h
// Parliament (hemicycle) seat diagram: parties with seat counts laid out as
// seats in concentric semicircular rows, a full circle, opposing Westminster
// benches or a plain grid, with a majority marker, total label and legend
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// PARLIAMENT DIAGRAM ENUMERATIONS
// =============================================================================

    // How the seats are arranged. All layouts render the same party list;
    // they only change where each seat goes.
    enum class ParliamentLayout {
        Hemicycle,      // Concentric arcs, parties left to right (Wikipedia
                        // style). The arc span is configurable: 180 degrees by
                        // default, wider for horseshoe chambers.
        Circle,         // Concentric full rings, parties clockwise from the top
        Westminster,    // Two blocks of benches facing each other across an
                        // aisle: governing parties on the left, the rest on the
                        // right, a speaker's chair at the head of the aisle
        Grid            // Rectangular waffle of seats, parties left to right,
                        // top to bottom
    };

    // Marker drawn for each seat
    enum class ParliamentSeatShape {
        Circle,
        Square,
        RoundedSquare
    };

    // Where the party legend goes
    enum class ParliamentLegendPosition {
        Bottom,         // Wrapped rows under the chamber (default)
        Right,          // One column to the right of the chamber
        Hidden
    };

// =============================================================================
// PARLIAMENT DIAGRAM DATA STRUCTURES
// =============================================================================

    struct ParliamentParty {
        std::string name;           // "Social Democrats"
        std::string abbreviation;   // "SPD" - used in the legend when set
        int seats = 0;
        Color color = Color(128, 128, 128, 255);
        bool government = false;    // Part of the governing side / coalition
        bool vacant = false;        // Drawn hollow (vacant seats, the Speaker)

        ParliamentParty() = default;
        ParliamentParty(const std::string& partyName, int seatCount, const Color& partyColor,
                        bool isGovernment = false)
            : name(partyName), seats(seatCount), color(partyColor), government(isGovernment) {}
        ParliamentParty(const std::string& partyName, const std::string& shortName, int seatCount,
                        const Color& partyColor, bool isGovernment = false)
            : name(partyName), abbreviation(shortName), seats(seatCount), color(partyColor),
              government(isGovernment) {}

        const std::string& LegendName() const { return abbreviation.empty() ? name : abbreviation; }
    };

    // One placed seat, in the element's local coordinates. Available through
    // GetSeats() after the first render.
    struct ParliamentSeat {
        Point2Dd center;
        double radius = 0.0;
        int row = 0;                // 0 = innermost arc / front bench / top row
        size_t party = SIZE_MAX;    // Index into GetParties()
        size_t ordinal = 0;         // Position in the party fill order (0 = first seat)
    };

// =============================================================================
// PARLIAMENT DIAGRAM ELEMENT CLASS
// =============================================================================

    class UltraCanvasParliamentDiagram : public UltraCanvasChartElementBase {
    private:
        // ===== DATA =====
        std::vector<ParliamentParty> parties;

        // ===== GEOMETRY OPTIONS =====
        ParliamentLayout layout = ParliamentLayout::Hemicycle;
        ParliamentSeatShape seatShape = ParliamentSeatShape::Circle;
        double arcSpanDegrees = 180.0;   // Hemicycle only; 90..360
        int rowCount = 0;                // 0 = automatic
        double innerRadiusRatio = 0.0;   // Hole radius / outer radius; 0 = per-layout default
        double seatGap = 0.18;           // Gap between seats as a fraction of the seat pitch
        int gridColumns = 0;             // Grid layout; 0 = automatic

        // ===== VISUAL OPTIONS =====
        bool darkTheme = false;
        bool showTotalLabel = true;
        bool showMajorityMarker = true;
        bool highlightGovernment = false;   // Fade the non-governing seats
        bool dimOnHover = true;             // Fade the other parties while one is hovered/selected
        bool showSpeakerChair = true;       // Westminster layout
        ParliamentLegendPosition legendPosition = ParliamentLegendPosition::Bottom;
        std::string totalCaption = "seats";
        std::string majorityCaption = "Majority";

        // ===== FONT SETTINGS =====
        float titleFontSize = 16.0f;
        float totalFontSize = 26.0f;
        float legendFontSize = 11.0f;
        float majorityFontSize = 10.0f;

        // ===== SELECTION & INTERACTION =====
        size_t hoveredParty = SIZE_MAX;
        size_t hoveredSeat = SIZE_MAX;
        size_t selectedParty = SIZE_MAX;

        // ===== LAYOUT CACHE (local coordinates) =====
        struct LegendEntry {
            size_t party = SIZE_MAX;
            Rect2Dd rect;
        };
        struct DiagramLayout {
            Rect2Dd chamberArea;                // Where the seats live
            std::vector<ParliamentSeat> seats;  // One per seat, in fill order
            std::vector<LegendEntry> legend;    // Hit rects of the legend entries
            Point2Dd center;                    // Arc centre (Hemicycle / Circle)
            double innerRadius = 0.0;           // Arc layouts
            double outerRadius = 0.0;
            double majorityAngle = 0.0;         // Radians; arc layouts
            bool hasMajorityAngle = false;
            Point2Dd totalLabelCenter;
            Rect2Dd speakerRect;                // Westminster
            int rowsUsed = 0;
            double seatRadius = 0.0;
            int width = 0;                      // Element size the cache was built for
            int height = 0;
            bool hadTitle = false;              // Whether a title band was reserved
            bool valid = false;
        };
        DiagramLayout cache;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasParliamentDiagram(const std::string& id, int x, int y, int w, int h);

        // ===== PARTY MANAGEMENT =====
        // Parties are drawn in insertion order: left to right in the hemicycle,
        // clockwise in the circle, front bench first in the Westminster blocks.
        void AddParty(const ParliamentParty& party);
        void AddParty(const std::string& name, int seats, const Color& color, bool government = false);
        void SetParties(const std::vector<ParliamentParty>& list);
        const std::vector<ParliamentParty>& GetParties() const { return parties; }
        const ParliamentParty& GetParty(size_t index) const { return parties[index]; }
        size_t GetPartyCount() const { return parties.size(); }
        void RemoveParty(size_t index);
        void ClearParties();

        void SetPartySeats(size_t index, int seats);
        void SetPartyColor(size_t index, const Color& color);
        void SetPartyGovernment(size_t index, bool government);
        void SetPartyVacant(size_t index, bool vacant);

        int GetTotalSeats() const;
        int GetGovernmentSeats() const;
        // Smallest number of seats that is more than half of the chamber
        int GetMajorityThreshold() const { return GetTotalSeats() / 2 + 1; }
        bool GovernmentHasMajority() const { return GetTotalSeats() > 0 && GetGovernmentSeats() >= GetMajorityThreshold(); }

        // ===== LAYOUT =====
        void SetLayout(ParliamentLayout l);
        ParliamentLayout GetLayout() const { return layout; }

        // Hemicycle arc in degrees: 180 is the classic half circle, 220-270 a
        // horseshoe, 360 the same as the Circle layout. Clamped to 90..360.
        void SetArcSpan(double degrees);
        double GetArcSpan() const { return arcSpanDegrees; }

        // Number of arcs (Hemicycle / Circle), benches per side (Westminster)
        // or rows (Grid). 0 picks the smallest count that fits every seat.
        void SetRowCount(int rows);
        int GetRowCount() const { return rowCount; }
        // The count actually used by the last render
        int GetRowsUsed() const { return cache.rowsUsed; }

        // Radius of the empty centre as a fraction of the outer radius, for the
        // arc layouts. 0 restores the per-layout default (0.42 hemicycle,
        // 0.55 circle).
        void SetInnerRadiusRatio(double ratio);
        double GetInnerRadiusRatio() const { return innerRadiusRatio; }

        void SetSeatGap(double fraction);
        double GetSeatGap() const { return seatGap; }

        void SetGridColumns(int columns);
        int GetGridColumns() const { return gridColumns; }

        // ===== APPEARANCE =====
        void SetSeatShape(ParliamentSeatShape shape);
        ParliamentSeatShape GetSeatShape() const { return seatShape; }

        void SetDarkTheme(bool dark);
        bool GetDarkTheme() const { return darkTheme; }

        void SetShowTotalLabel(bool show);
        bool GetShowTotalLabel() const { return showTotalLabel; }
        void SetTotalCaption(const std::string& caption);

        void SetShowMajorityMarker(bool show);
        bool GetShowMajorityMarker() const { return showMajorityMarker; }
        void SetMajorityCaption(const std::string& caption);

        void SetHighlightGovernment(bool highlight);
        bool GetHighlightGovernment() const { return highlightGovernment; }

        void SetDimOnHover(bool dim);
        bool GetDimOnHover() const { return dimOnHover; }

        void SetShowSpeakerChair(bool show);
        bool GetShowSpeakerChair() const { return showSpeakerChair; }

        void SetLegendPosition(ParliamentLegendPosition position);
        ParliamentLegendPosition GetLegendPosition() const { return legendPosition; }

        void SetTitleFontSize(float size);
        void SetTotalFontSize(float size);
        void SetLegendFontSize(float size);

        // ===== SELECTION =====
        // Hover and click selection are controlled by the base class:
        // SetEnableSelection(bool) / SetEnableTooltips(bool)
        size_t GetSelectedParty() const { return selectedParty; }
        void SetSelectedParty(size_t index);
        void ClearSelection();
        size_t GetHoveredParty() const { return hoveredParty; }

        // Seats placed by the last render (empty before the first one)
        const std::vector<ParliamentSeat>& GetSeats() const { return cache.seats; }

        // ===== CALLBACKS =====
        std::function<void(size_t, const ParliamentParty&)> onPartyHover;
        std::function<void(size_t, const ParliamentParty&)> onPartyClick;
        std::function<void(size_t, const ParliamentSeat&)> onSeatClick;
        std::function<void()> onSelectionChange;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== THEME HELPERS =====
        Color BackgroundFill() const;
        Color TextColor() const;
        Color MutedTextColor() const;
        Color SeatFillColor(size_t seatIndex) const;
        bool IsPartyActive(size_t party) const;   // Not faded by hover / selection
        double EffectiveInnerRatio() const;

        // ===== LAYOUT =====
        void InvalidateLayout() { cache.valid = false; }
        void UpdateLayout(IRenderContext* ctx);
        void LayoutArc(const Rect2Dd& area, double spanRadians);
        void LayoutWestminster(const Rect2Dd& area);
        void LayoutGrid(const Rect2Dd& area);
        void AssignPartiesInOrder(std::vector<ParliamentSeat>& seats) const;
        double MeasureLegend(IRenderContext* ctx, const Rect2Dd& bounds,
                             std::vector<LegendEntry>& entries, bool vertical) const;
        std::string LegendText(const ParliamentParty& party) const;

        size_t FindSeatAt(const Point2Di& pos) const;
        size_t FindLegendEntryAt(const Point2Di& pos) const;

        // ===== EVENT HELPERS =====
        bool HandleClick(const UCEvent& event);
        std::string BuildPartyTooltip(size_t party) const;
        void SetHoveredParty(size_t party, size_t seat);

        // ===== DRAWING =====
        void DrawSeats(IRenderContext* ctx);
        void DrawSeatShape(IRenderContext* ctx, const Point2Dd& center, double radius, bool fill);
        void DrawMajorityMarker(IRenderContext* ctx);
        void DrawTotalLabel(IRenderContext* ctx);
        void DrawLegend(IRenderContext* ctx);
        void DrawSpeakerChair(IRenderContext* ctx);
        void DrawTitle(IRenderContext* ctx);
        void DrawEmptyChamber(IRenderContext* ctx);
    };

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace ParliamentDiagramSamples {
        // German Bundestag after the 2021 federal election (736 seats):
        // SPD / Greens / FDP traffic-light coalition marked as government.
        std::vector<ParliamentParty> Bundestag2021();
        // European Parliament groups after the 2024 election (720 seats)
        std::vector<ParliamentParty> EuropeanParliament2024();
        // UK House of Commons after the 2024 general election (650 seats):
        // Labour marked as government, suits the Westminster layout.
        std::vector<ParliamentParty> HouseOfCommons2024();
    }

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

    std::shared_ptr<UltraCanvasParliamentDiagram> CreateParliamentDiagram(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasParliamentDiagram> CreateParliamentDiagram(
            const std::string& id, int x, int y, int width, int height,
            ParliamentLayout layout);

} // namespace UltraCanvas
