// UltraCanvasAdjacencyDiagram.h
// Architectural space-planning adjacency diagram
// Rooms as area-proportional circles, edges as solid/dashed adjacency links,
// functional zones as dashed bounding regions.
// Version: 1.1.0
// Last Modified: 2026-07-13
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>
#include <memory>

namespace UltraCanvas {

// ===== ENUMERATIONS =====

    /// Type of physical adjacency between two rooms
    enum class AdjacencyLinkType {
        Direct,         ///< Solid line — rooms must be directly adjacent / share a wall
        Secondary,      ///< Dashed line — rooms should be nearby / indirect access
        ServiceOnly     ///< Dotted line — service/back-of-house connection only
    };

    /// How strongly an adjacency is wanted.
    ///
    /// This is a different axis from AdjacencyLinkType, which says what KIND of
    /// connection it is (a shared wall, an indirect route, a service run). A
    /// service link can itself be mandatory or merely preferred, so the two
    /// overlap on Must/Direct and Should/Secondary but do not coincide.
    /// Priority is what a matrix view plots; the bubble view ignores it.
    enum class AdjacencyPriority {
        Must,       ///< Red — these spaces must be adjacent
        Should,     ///< Blue — these spaces should be adjacent / preferred
        Maybe       ///< Green — desirable if the plan allows
    };

    /// Which view the element draws
    enum class AdjacencyView {
        Bubble,     ///< Area-proportional room circles joined by typed links
        Matrix      ///< The same data as a half matrix of spaces against themselves
    };

    /// How the half matrix is drawn
    enum class AdjacencyMatrixStyle {
        Staircase,  ///< Axis-aligned grid, upper-right triangle populated
        Rotated45   ///< Classic architectural triangle: horizontal labels on
                    ///< the left, cells as 45° diamonds where the diagonals of
                    ///< each pair of rooms cross. The traditional hand-drawn
                    ///< look; Staircase stays denser for very large programmes.
    };

    /// Which functional category a room belongs to
    enum class RoomFunctionType {
        Public,         ///< Public-facing spaces (lobbies, reception, meeting rooms)
        Private,        ///< Private / staff spaces (offices, workrooms)
        Service,        ///< Service / back-of-house (kitchens, storage, loading)
        Support,        ///< Support / utility (toilets, plant rooms, corridors)
        Circulation,    ///< Circulation (halls, stairs, elevators)
        Custom          ///< User-defined color via room.color
    };

// ===== DATA STRUCTURES =====

    /// A single room / space node in the adjacency diagram
    struct AdjacencyRoom {
        std::string         id;                                     ///< Unique identifier
        std::string         label;                                  ///< Display name (shown inside circle)
        float               areaSqM         = 20.0f;               ///< Floor area in square metres — drives circle radius
        RoomFunctionType    functionType    = RoomFunctionType::Public;
        Color               color           = Color(100, 160, 220, 200); ///< Override color (used when functionType == Custom)
        float               x               = 0.0f;                ///< Centre X in diagram local coordinates
        float               y               = 0.0f;                ///< Centre Y in diagram local coordinates
        std::string         floorId;                               ///< Optional floor/level identifier
        std::string         note;                                  ///< Optional small annotation below label

        /// Free-form per-room values, parallel to the diagram's attribute
        /// column headers. Rendered as a table gutter beside the matrix row
        /// labels; ignored by the bubble view.
        std::vector<std::string> attributes;
    };

    /// A directed or undirected adjacency requirement between two rooms
    struct AdjacencyLink {
        std::string         sourceId;                               ///< Source room ID
        std::string         targetId;                               ///< Target room ID
        AdjacencyLinkType   type        = AdjacencyLinkType::Direct;

        /// How badly this adjacency is wanted — an axis of its own, additive to
        /// `type`. Defaults to Must so existing callers keep their meaning: a
        /// matrix view of data that never expressed a priority shows every link
        /// at full strength, which is the honest reading.
        AdjacencyPriority   priority    = AdjacencyPriority::Must;

        bool                directed    = false;                    ///< Show arrowhead at target
        float               weight      = 1.0f;                    ///< Line thickness multiplier
        std::string         label;                                 ///< Optional link label
    };

    /// A named functional zone that groups rooms on one floor/cluster
    /// Rendered as a dashed rounded rectangle bounding its member rooms
    struct AdjacencyZone {
        std::string         id;                                     ///< Unique identifier
        std::string         label;                                  ///< Zone/floor label (e.g. "2F", "GF", "B1")
        std::vector<std::string> roomIds;                          ///< IDs of member rooms
        Color               borderColor = Color(160, 160, 160, 200); ///< Dashed border color
        Color               fillColor   = Color(240, 240, 240, 60);  ///< Background fill (translucent)
        float               padding     = 24.0f;                   ///< Extra space around member rooms
        float               cornerRadius = 20.0f;                  ///< Corner rounding of bounding rect
    };

    /// Visual styling for the adjacency diagram
    struct AdjacencyDiagramStyle {
        // Room circles
        float   areaScale               = 4.0f;     ///< radius = sqrt(areaSqM) * areaScale
        float   minRadius               = 14.0f;    ///< Minimum room circle radius
        float   maxRadius               = 80.0f;    ///< Maximum room circle radius
        float   roomStrokeWidth         = 1.5f;     ///< Room circle outline width
        Color   roomStrokeColor         = Color(255, 255, 255, 180); ///< Room outline color
        Color   roomHoverStroke         = Color(255, 220, 0,   255); ///< Stroke on hover
        Color   roomSelectedStroke      = Color(255, 120, 0,   255); ///< Stroke when selected

        // Default zone colors by function type
        Color   colorPublic             = Color(100, 160, 220, 200); ///< Blue — public
        Color   colorPrivate            = Color(210,  80,  60, 200); ///< Red/orange — private
        Color   colorService            = Color(100, 100, 100, 200); ///< Gray — service
        Color   colorSupport            = Color(200, 200, 200, 200); ///< Light gray — support
        Color   colorCirculation        = Color( 80, 180, 140, 200); ///< Teal — circulation

        // Room labels
        float   labelFontSize           = 11.0f;    ///< Room name font size
        float   noteFontSize            = 9.0f;     ///< Room note font size
        Color   labelColor              = Color(255, 255, 255, 240); ///< Label text color on dark fills
        Color   labelColorDark          = Color( 30,  30,  30, 240); ///< Label text on light fills

        // Label halo — a contrasting outline drawn behind label/zone text so it
        // stays readable when a label is wider than its circle and spills onto
        // the diagram background or a similarly-colored zone fill.
        bool    labelHalo               = true;     ///< Draw a contrasting halo behind text
        float   labelHaloWidth          = 2.0f;     ///< Halo thickness in pixels
        Color   labelHaloLight          = Color(255, 255, 255, 220); ///< Halo behind dark text
        Color   labelHaloDark           = Color(  0,   0,   0, 180); ///< Halo behind light text

        // Links
        float   directLinkWidth         = 1.8f;     ///< Direct adjacency line width
        float   secondaryLinkWidth      = 1.0f;     ///< Secondary link line width
        float   serviceLinkWidth        = 0.8f;     ///< Service link line width
        Color   directLinkColor         = Color( 60,  60,  60, 200); ///< Direct link color
        Color   secondaryLinkColor      = Color(120, 120, 120, 160); ///< Secondary link color
        Color   serviceLinkColor        = Color(180, 180, 180, 140); ///< Service link color
        float   dashLength              = 6.0f;     ///< Dash segment length (secondary)
        float   dotLength               = 2.0f;     ///< Dot segment length (service)
        float   dashGap                 = 4.0f;     ///< Gap between dash/dot segments
        float   arrowSize               = 8.0f;     ///< Arrowhead size for directed links

        // Zone bounding boxes
        float   zoneBorderWidth         = 1.0f;     ///< Zone dashed border width
        float   zoneLabelFontSize       = 12.0f;    ///< Zone label font size (shown at top-left of zone)
        Color   zoneLabelColor          = Color( 80,  80,  80, 220);

        // Tooltip
        bool    showTooltip             = true;
        float   tooltipFontSize         = 11.0f;
        Color   tooltipBackground       = Color( 50,  50,  50, 230);
        Color   tooltipText             = Color(255, 255, 255, 255);

        // ===== MATRIX VIEW =====
        float   matrixMinCellSize       = 15.0f;   ///< Cells shrink to fit down to this
        float   matrixMaxCellSize       = 42.0f;
        float   matrixGridLineWidth     = 1.0f;
        Color   matrixGridLineColor     = Color(205, 211, 219, 255);
        Color   matrixCellBackground    = Color(255, 255, 255, 255);
        Color   matrixBlockedCell       = Color(243, 245, 248, 255); ///< The unused lower triangle
        float   matrixMarkSize          = 0.46f;   ///< Fraction of the cell's short side
        float   matrixRowLabelFontSize  = 11.0f;
        float   matrixColLabelFontSize  = 10.0f;
        float   matrixMaxRowLabelWidth  = 210.0f;
        float   matrixMaxHeaderHeight   = 170.0f;
        float   matrixAttributeColWidth = 42.0f;   ///< Per attribute gutter column
        Color   matrixLabelColor        = Color( 45,  50,  58, 255);
        Color   matrixHoverBand         = Color( 70, 130, 200,  40);
        Color   matrixSelectedCell      = Color( 70, 130, 200,  70);

        // Priority mark colors, shared by the matrix marks and the legend.
        Color   colorMust               = Color(214,  45,  45, 255);
        Color   colorShould             = Color( 40,  85, 190, 255);
        Color   colorMaybe              = Color( 45, 160,  85, 255);
    };

// ===== MAIN CLASS =====

    /**
     * @brief Architectural space-planning adjacency diagram
     *
     * Renders rooms as area-proportional circles colored by functional type,
     * adjacency requirements as solid/dashed/dotted lines between rooms,
     * and functional zones as dashed bounding regions (floors, clusters).
     *
     * Layout is fully manual — the caller positions each room by setting
     * its x/y coordinates. This matches the architectural workflow where
     * a designer iterates on spatial proximity rather than auto-placement.
     *
     * Features:
     * - Room size proportional to floor area (radius = √area × scale)
     * - Five built-in functional type colors + custom override
     * - Three link types: direct (solid), secondary (dashed), service (dotted)
     * - Directed links with arrowheads
     * - Zone bounding boxes with translucent fill and dashed border
     * - Zone and floor labels
     * - Hover and click callbacks for rooms and links
     * - Tooltip showing room name, area, and floor
     * - Pan support via drag (when enablePan = true)
     */
    class UltraCanvasAdjacencyDiagram : public UltraCanvasUIElement {
    public:
        // ===== CONSTRUCTION =====

        UltraCanvasAdjacencyDiagram(const std::string& id,
                                    float x, float y, float w, float h);

        // ===== ROOM API =====

        /// Add a room. Duplicate IDs are rejected. Returns insertion index.
        int  AddRoom(const AdjacencyRoom& room);

        /// Convenience — add by ID, label, area and position
        int  AddRoom(const std::string& id, const std::string& label,
                     float areaSqM, float x, float y,
                     RoomFunctionType type = RoomFunctionType::Public);

        /// Remove a room and all its connected links. Returns false if not found.
        bool RemoveRoom(const std::string& id);

        /// Update room in-place (preserves ID)
        void UpdateRoom(const std::string& id, const AdjacencyRoom& updated);

        /// Move room to new diagram coordinates
        void MoveRoom(const std::string& id, float x, float y);

        /// Returns room count
        int  GetRoomCount() const { return static_cast<int>(rooms.size()); }

        /// Returns room by index, nullptr if out of range
        const AdjacencyRoom* GetRoom(int index) const;

        /// Returns room by ID, nullptr if not found
        const AdjacencyRoom* GetRoomById(const std::string& id) const;

        // ===== LINK API =====

        /// Add an adjacency link. Returns link index.
        int  AddLink(const AdjacencyLink& link);

        /// Convenience — add link by IDs and type
        int  AddLink(const std::string& sourceId,
                     const std::string& targetId,
                     AdjacencyLinkType type = AdjacencyLinkType::Direct,
                     bool directed = false);

        /// Convenience — add link by IDs and priority. Use this when the
        /// programme is expressed as must/should/maybe rather than as a kind
        /// of connection; `type` follows from the priority for the bubble view.
        int  AddLink(const std::string& sourceId,
                     const std::string& targetId,
                     AdjacencyPriority priority);

        /// Set the priority of every link between two rooms (both directions).
        /// Returns false when no such link exists.
        bool SetLinkPriority(const std::string& sourceId,
                             const std::string& targetId,
                             AdjacencyPriority priority);

        /// Priority of the link between two rooms, or nullptr when unlinked.
        const AdjacencyLink* FindLink(const std::string& sourceId,
                                      const std::string& targetId) const;

        /// Remove all links between two rooms (both directions)
        void RemoveLink(const std::string& sourceId, const std::string& targetId);

        /// Remove all links
        void ClearLinks();

        /// Returns link count
        int  GetLinkCount() const { return static_cast<int>(links.size()); }

        // ===== ZONE API =====

        /// Add a functional zone / floor group
        void AddZone(const AdjacencyZone& zone);

        /// Remove zone by ID
        void RemoveZone(const std::string& id);

        /// Remove all zones
        void ClearZones();

        /// Returns zone count
        int  GetZoneCount() const { return static_cast<int>(zones.size()); }

        // ===== CLEAR ALL =====

        void Clear();

        // ===== PAN & ZOOM =====

        /// Enable/disable mouse drag panning
        void SetEnablePan(bool enable)          { enablePan = enable; }
        bool GetEnablePan() const               { return enablePan; }

        /// Reset pan offset to zero
        void ResetPan()                         { panOffsetX = panOffsetY = 0.0f; RequestRedraw(); }

        /// Get/set current pan offset
        void SetPanOffset(float x, float y)     { panOffsetX = x; panOffsetY = y; RequestRedraw(); }

        /// Center all rooms within the widget bounds (computes bbox of room
        /// circles and adjusts pan offset). Safe to call after AddRoom calls.
        /// No-op if there are no rooms.
        void CenterContent();

        // ===== VIEW =====

        /// Switch between the bubble diagram and the matrix. Both draw the same
        /// rooms and links; neither converts or copies the data.
        void SetView(AdjacencyView v);
        AdjacencyView GetView() const           { return view; }

        /// Staircase (default) or the classic rotated diamond triangle.
        void SetMatrixStyle(AdjacencyMatrixStyle s);
        AdjacencyMatrixStyle GetMatrixStyle() const { return matrixStyle; }

        /// Explicit row/column order for the matrix, by room id. Ids not in the
        /// list are appended in insertion order; unknown ids are ignored. Pass
        /// an empty vector to return to insertion order, which is the default —
        /// the bubble view's x/y positions give no ordering, and the order a
        /// caller typed their rooms in is usually the meaningful one.
        void SetMatrixOrder(const std::vector<std::string>& roomIds);

        /// Draw the self-intersection cells (a room against itself). Off by
        /// default: a room is trivially adjacent to itself and the diagonal is
        /// noise.
        void SetShowMatrixDiagonal(bool on);
        bool GetShowMatrixDiagonal() const      { return showMatrixDiagonal; }

        /// Headers for the attribute gutter drawn left of the matrix row
        /// labels. Values come from AdjacencyRoom::attributes, positionally.
        void SetAttributeColumns(const std::vector<std::string>& headers);
        const std::vector<std::string>& GetAttributeColumns() const { return attributeColumns; }

        // ===== LEGEND =====

        /// Show a key. In the matrix view it explains the priority marks; in
        /// the bubble view it explains the room function-type colours, which
        /// were previously unlabelled.
        void SetShowLegend(bool on);
        bool GetShowLegend() const              { return showLegend; }

        void SetLegendPosition(ChartLegendPosition position);

        /// Human-readable name of a priority ("Must be adjacent", ...). Useful
        /// for status bars and tooltips built by the caller.
        static const char* PriorityLabel(AdjacencyPriority priority);

        // ===== STYLE =====

        void SetStyle(const AdjacencyDiagramStyle& s)  { style = s; RebuildLegend(); RequestRedraw(); }
        const AdjacencyDiagramStyle& GetStyle() const   { return style; }

        // ===== SELECTION =====

        int  GetSelectedRoomIndex() const       { return selectedRoomIdx; }
        int  GetSelectedLinkIndex() const       { return selectedLinkIdx; }
        void ClearSelection()                   { selectedRoomIdx = selectedLinkIdx = -1; RequestRedraw(); }

        // ===== CALLBACKS =====

        /// Fired when a room circle is clicked
        std::function<void(int, const AdjacencyRoom&)> onRoomClick;

        /// Fired when mouse enters a room circle
        std::function<void(int, const AdjacencyRoom&)> onRoomHover;

        /// Fired when an adjacency link is clicked
        std::function<void(int, const AdjacencyLink&)> onLinkClick;

        /// Fired when a matrix cell is clicked, populated or not. The bubble
        /// view has no equivalent — there is nothing to click where two rooms
        /// are unlinked.
        std::function<void(const std::string& rowRoomId,
                           const std::string& colRoomId,
                           const AdjacencyLink* link)> onMatrixCellClick;

        // ===== RENDER & EVENTS =====

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== DATA =====
        std::vector<AdjacencyRoom>  rooms;
        std::vector<AdjacencyLink>  links;
        std::vector<AdjacencyZone>  zones;

        // ===== VIEW STATE =====
        AdjacencyView           view            = AdjacencyView::Bubble;
        AdjacencyMatrixStyle    matrixStyle     = AdjacencyMatrixStyle::Staircase;
        bool                    showMatrixDiagonal = false;
        std::vector<std::string> matrixOrder;           // room ids, may be empty
        std::vector<std::string> attributeColumns;
        bool                    showLegend      = false;
        std::unique_ptr<ChartLegend> legend;

        // Matrix layout cache, element-local coordinates
        struct MatrixLayout {
            bool    valid       = false;
            Rect2Dd content;                 // after the legend takes its bite
            Rect2Dd legendArea;              // before it does
            Rect2Dd grid;
            Rect2Dd rowLabels;
            Rect2Dd colLabels;
            Rect2Dd attributes;              // gutter left of the row labels
            double  cellSize    = 0.0;
            int     count       = 0;         // rows == columns
            bool    rotateHeaders = true;
            std::vector<int> order;          // room indices, matrix order
        };
        MatrixLayout matrixLayout;

        int  hoveredMatrixRow   = -1;
        int  hoveredMatrixCol   = -1;
        int  selectedMatrixRow  = -1;
        int  selectedMatrixCol  = -1;

        // ===== STATE =====
        AdjacencyDiagramStyle   style;
        bool    enablePan       = true;
        float   panOffsetX      = 0.0f;
        float   panOffsetY      = 0.0f;
        bool    isPanning       = false;
        float   panStartX       = 0.0f;
        float   panStartY       = 0.0f;
        float   panStartOffX    = 0.0f;
        float   panStartOffY    = 0.0f;

        // Hover / selection
        int     hoveredRoomIdx  = -1;
        int     hoveredLinkIdx  = -1;
        int     selectedRoomIdx = -1;
        int     selectedLinkIdx = -1;

        // Tooltip
        bool        showingTooltip  = false;
        float       tooltipX        = 0.0f;
        float       tooltipY        = 0.0f;
        std::string tooltipText;

        // ===== INTERNAL HELPERS =====
        int     LookupRoom(const std::string& id) const;
        int     LookupZone(const std::string& id) const;
        float   RoomRadius(const AdjacencyRoom& room) const;
        Color   RoomColor(const AdjacencyRoom& room) const;
        bool    IsLightColor(const Color& c) const;

        // Draw text with a contrasting halo behind it (keeps labels readable
        // when they overflow their circle onto the diagram background).
        void    DrawTextWithHalo(IRenderContext* ctx, const std::string& text,
                                 float x, float y, const Color& textColor) const;

        // Diagram → screen coordinate conversion (applies pan offset)
        void    DiagramToScreen(float dx, float dy, float& sx, float& sy) const;
        void    ScreenToDiagram(float sx, float sy, float& dx, float& dy) const;

        // Rendering passes (draw order: zones → links → rooms → labels → tooltip)
        void    DrawZones(IRenderContext* ctx) const;
        void    DrawLinks(IRenderContext* ctx) const;
        void    DrawRooms(IRenderContext* ctx) const;
        void    DrawLabels(IRenderContext* ctx) const;
        void    DrawTooltip(IRenderContext* ctx) const;

        // Link rendering helpers
        void    DrawLink(IRenderContext* ctx, const AdjacencyLink& link,
                         int srcIdx, int tgtIdx, bool hovered, bool selected) const;
        void    DrawDashedLine(IRenderContext* ctx,
                               float x1, float y1, float x2, float y2,
                               float dashLen, float gapLen) const;
        void    DrawArrowhead(IRenderContext* ctx,
                              float tipX, float tipY, float angle,
                              const Color& col) const;

        // Zone bounding box computation
        Rect2Dd ComputeZoneBounds(const AdjacencyZone& zone) const;

        // Hit testing
        int     HitTestRoom(float localX, float localY) const;
        int     HitTestLink(float localX, float localY) const;

        // ===== MATRIX VIEW (UltraCanvasAdjacencyMatrix.cpp) =====
        void    InvalidateMatrixLayout()        { matrixLayout.valid = false; }
        void    UpdateMatrixLayout(IRenderContext* ctx);
        void    RenderMatrix(IRenderContext* ctx);
        void    DrawMatrixGrid(IRenderContext* ctx) const;
        void    DrawMatrixLabels(IRenderContext* ctx) const;
        void    DrawMatrixAttributes(IRenderContext* ctx) const;
        void    DrawMatrixMarks(IRenderContext* ctx) const;
        void    DrawMatrixHighlight(IRenderContext* ctx) const;

        // Room indices in matrix order, honouring SetMatrixOrder.
        std::vector<int> BuildMatrixOrder() const;

        // Cell under a point; returns false when outside the populated region.
        bool    HitTestMatrixCell(float localX, float localY,
                                  int& outRow, int& outCol) const;

        // Whether (row, col) is inside the drawn upper triangle.
        bool    IsMatrixCellVisible(int row, int col) const;

        // Strongest priority linking two rooms, or nullptr when unlinked.
        const AdjacencyLink* MatrixLinkAt(int row, int col) const;

        Color   PriorityColor(AdjacencyPriority priority) const;

        void    RebuildLegend();
        std::string EllipsizeToWidth(IRenderContext* ctx, const std::string& text,
                                     double maxWidth) const;
    };

// ===== FACTORY FUNCTION =====

    std::shared_ptr<UltraCanvasAdjacencyDiagram> CreateAdjacencyDiagram(
            const std::string& id, long uid,
            float x, float y, float width, float height);

} // namespace UltraCanvas