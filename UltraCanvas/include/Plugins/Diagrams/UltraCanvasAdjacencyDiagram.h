// UltraCanvasAdjacencyDiagram.h
// Architectural space-planning adjacency diagram
// Rooms as area-proportional circles, edges as solid/dashed adjacency links,
// functional zones as dashed bounding regions.
// Version: 1.0.2
// Last Modified: 2026-07-13
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== ENUMERATIONS =====

    /// Type of physical adjacency between two rooms
    enum class AdjacencyLinkType {
        Direct,         ///< Solid line — rooms must be directly adjacent / share a wall
        Secondary,      ///< Dashed line — rooms should be nearby / indirect access
        ServiceOnly     ///< Dotted line — service/back-of-house connection only
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
    };

    /// A directed or undirected adjacency requirement between two rooms
    struct AdjacencyLink {
        std::string         sourceId;                               ///< Source room ID
        std::string         targetId;                               ///< Target room ID
        AdjacencyLinkType   type        = AdjacencyLinkType::Direct;
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

        // ===== STYLE =====

        void SetStyle(const AdjacencyDiagramStyle& s)  { style = s; RequestRedraw(); }
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

        // ===== RENDER & EVENTS =====

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== DATA =====
        std::vector<AdjacencyRoom>  rooms;
        std::vector<AdjacencyLink>  links;
        std::vector<AdjacencyZone>  zones;

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
    };

// ===== FACTORY FUNCTION =====

    std::shared_ptr<UltraCanvasAdjacencyDiagram> CreateAdjacencyDiagram(
            const std::string& id, long uid,
            float x, float y, float width, float height);

} // namespace UltraCanvas