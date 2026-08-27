// include/Plugins/Charts/UltraCanvasKanbanBoard.h
// Kanban board element: workflow columns with WIP limits, swimlanes, cards
// with priorities/due dates/assignees/tags, drag & drop, a built-in card
// editor, move history for flow metrics, text-definition (Mermaid kanban)
// and JSON loading, and a preset-based design/palette system.
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasSmoothScroll.h"
#include "UltraCanvasGanttChart.h"   // GanttDate (shared calendar date type)
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// CARD & COLUMN MODEL
// =============================================================================

    enum class KanbanPriority {
        NoPriority,
        Low,
        Normal,
        High,
        Urgent
    };

    struct KanbanColumn {
        int id = -1;                  // Unique id (assigned by the data source)
        std::string title;
        int wipLimit = 0;             // 0 = no limit
        Color customColor = Colors::Transparent; // Transparent = palette color
        std::string footerCaption;    // Optional caption strip text ("Q1", ...)
    };

    struct KanbanLane {               // Swimlane (optional)
        int id = -1;
        std::string title;
        bool collapsed = false;
    };

    struct KanbanCard {
        int id = -1;                  // Unique id (assigned by the data source)
        int columnId = -1;
        int laneId = -1;              // -1 = no lane / default lane
        std::string title;
        std::string description;
        std::string assignee;
        std::vector<std::string> tags;
        KanbanPriority priority = KanbanPriority::NoPriority;
        GanttDate dueDate;
        bool hasDueDate = false;
        Color customColor = Colors::Transparent; // Transparent = style decides
        bool blocked = false;
        std::string blockedReason;
        GanttDate enteredColumn;      // Stamped on add/move (aging WIP)
    };

    // One workflow state change; appended by AddCard and MoveCard. This is
    // the raw material for lead/cycle time and the cumulative flow diagram.
    struct KanbanMoveRecord {
        int cardId = -1;
        int fromColumnId = -1;        // -1 = card created
        int toColumnId = -1;          // -1 = card removed
        GanttDate date;
    };

// =============================================================================
// DATA SOURCE
// =============================================================================

    class KanbanDataSource : public IChartDataSource {
    private:
        std::vector<KanbanColumn> columns;            // Display order
        std::vector<KanbanLane> lanes;                // Display order
        std::vector<KanbanCard> cards;                // Insertion order
        std::map<int, std::vector<int>> cardOrder;    // columnId -> ordered card ids
        std::vector<KanbanMoveRecord> history;
        int nextColumnId = 1;
        int nextLaneId = 1;
        int nextCardId = 1;
        uint64_t version = 1;                         // Bumped on every mutation
        GanttDate currentDate;                        // Stamps history entries
        int commitmentColumnId = -1;                  // -1 = auto (second column)
        int deliveryColumnId = -1;                    // -1 = auto (last column)

    public:
        // ===== COLUMNS =====

        int AddColumn(const std::string& title, int wipLimit = 0,
                      const Color& color = Colors::Transparent);
        bool RemoveColumn(int columnId);              // Removes its cards too
        bool RenameColumn(int columnId, const std::string& title);
        bool SetColumnWipLimit(int columnId, int wipLimit);
        bool SetColumnColor(int columnId, const Color& color);
        bool SetColumnFooterCaption(int columnId, const std::string& caption);
        bool MoveColumn(int columnId, int newIndex);
        KanbanColumn* FindColumn(int columnId);
        const KanbanColumn* FindColumn(int columnId) const;
        const std::vector<KanbanColumn>& GetColumns() const { return columns; }
        int ColumnIndexOf(int columnId) const;        // -1 = unknown

        // ===== SWIMLANES (optional) =====

        int AddLane(const std::string& title);
        bool RemoveLane(int laneId);                  // Its cards go to laneId -1
        bool RenameLane(int laneId, const std::string& title);
        KanbanLane* FindLane(int laneId);
        const KanbanLane* FindLane(int laneId) const;
        const std::vector<KanbanLane>& GetLanes() const { return lanes; }

        // ===== CARDS =====

        // Adds a card at the end of `columnId` and returns its id (-1 for an
        // unknown column). Records a creation history entry.
        int AddCard(int columnId, const std::string& title, int laneId = -1);
        int AddCard(const KanbanCard& card);          // id field is ignored
        bool RemoveCard(int cardId);
        void Clear();

        KanbanCard* FindCard(int cardId);
        const KanbanCard* FindCard(int cardId) const;

        bool SetCardTitle(int cardId, const std::string& title);
        bool SetCardDescription(int cardId, const std::string& text);
        bool SetCardAssignee(int cardId, const std::string& assignee);
        bool SetCardPriority(int cardId, KanbanPriority priority);
        bool SetCardDueDate(int cardId, const GanttDate& date);
        bool ClearCardDueDate(int cardId);
        bool SetCardColor(int cardId, const Color& color);
        bool SetCardBlocked(int cardId, bool blocked,
                            const std::string& reason = "");
        bool SetCardLane(int cardId, int laneId);
        bool SetCardTags(int cardId, const std::vector<std::string>& tags);
        bool AddCardTag(int cardId, const std::string& tag);

        // Moves a card to `toColumnId` at `index` (-1 = append). The single
        // mutation path for workflow state: records a history entry and
        // restamps KanbanCard::enteredColumn. Reordering within the same
        // column does not create a history entry.
        bool MoveCard(int cardId, int toColumnId, int index = -1);

        // Ordered card ids of one column; laneId -2 = all lanes.
        std::vector<int> GetCardsInColumn(int columnId, int laneId = -2) const;
        size_t GetCardCount() const { return cards.size(); }
        int CardCountInColumn(int columnId) const;
        bool IsWipExceeded(int columnId) const;

        // ===== HISTORY & FLOW METRICS =====

        // Date stamped onto subsequent history entries / enteredColumn.
        // Defaults to serial 0; hosts tracking real flow should set it.
        void SetCurrentDate(const GanttDate& date) { currentDate = date; }
        GanttDate GetCurrentDate() const { return currentDate; }
        const std::vector<KanbanMoveRecord>& GetHistory() const { return history; }

        // Boundaries lead/cycle time measure between. Auto: commitment =
        // second column (first when only one exists), delivery = last.
        void SetCommitmentColumn(int columnId) { commitmentColumnId = columnId; }
        void SetDeliveryColumn(int columnId) { deliveryColumnId = columnId; }
        int EffectiveCommitmentColumn() const;
        int EffectiveDeliveryColumn() const;

        // Cards per column at end of `date`, replayed from the history —
        // the cumulative flow diagram input. Result is columnId -> count.
        std::map<int, int> GetColumnCountsAt(const GanttDate& date) const;

        // Days from entering the commitment column to entering the delivery
        // column (lead), or from leaving the commitment column (cycle);
        // -1 when the card never passed the relevant boundaries.
        long CardLeadTimeDays(int cardId) const;
        long CardCycleTimeDays(int cardId) const;

        // ===== TEXT DEFINITIONS =====

        // Replaces the whole board from a Mermaid `kanban` text definition:
        //
        //   kanban
        //     todo[To Do]@{ wip: 3 }
        //       item1[Fix the cart size bug]@{ assigned: 'iris', priority: 'High' }
        //       [Anonymous card text]
        //     done[Done]
        //
        // Supported card metadata keys: assigned, priority ('Very High',
        // 'High', 'Normal', 'Low', 'Very Low'), due (YYYY-MM-DD), ticket
        // and label (both become tags). Column metadata: wip, color (#rrggbb).
        // Returns false and fills `error` on malformed input.
        bool LoadFromText(const std::string& text, std::string* error = nullptr);

        // JSON persistence (schema documented in UltraCanvasKanbanBoard.md).
        std::string ToJSON(bool pretty = true) const;
        bool LoadFromJSONText(const std::string& jsonText,
                              std::string* error = nullptr);
        bool SaveToJSONFile(const std::string& filePath) const;
        bool LoadFromJSONFile(const std::string& filePath,
                              std::string* error = nullptr);

        // Monotonic change counter so the element can invalidate caches.
        uint64_t GetVersion() const { return version; }

        // ===== IChartDataSource =====
        size_t GetPointCount() const override { return cards.size(); }
        ChartDataPoint GetPoint(size_t index) override;
        bool SupportsStreaming() const override { return false; }
        void LoadFromCSV(const std::string& filePath) override {}
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {}

    private:
        void Touch() { ++version; }
        void RecordMove(int cardId, int fromColumnId, int toColumnId);
        std::vector<int>& OrderOf(int columnId);
    };

// =============================================================================
// STYLE SYSTEM
// =============================================================================

    enum class KanbanHeaderShape {
        Bar,          // Slim colored bar above the column (sticky-note board)
        Pill,         // Rounded pill filled with the column color
        PillWithDot,  // Pill plus a connector line and detached circle
        TextRow       // Plain text over a full-width separator line
    };

    enum class KanbanColumnBackground {
        Panel,        // Full-height solid panel in the column color
        Track,        // Full-height panel in a neutral track color
        Separators,   // No panel; (dashed) separator lines between columns
        Plain         // Nothing behind the cards
    };

    enum class KanbanCardShape {
        Rounded,      // Rounded rectangle
        StickyNote,   // Square note, slight corner radius
        DogEar,       // White card with a folded bottom-left corner
        Pill          // Compact single-line pill
    };

    enum class KanbanCardLayout {
        Stack,        // Strict vertical list
        Stagger,      // Cards alternate a left/right offset
        Grid          // Fixed-size cards on a uniform grid
    };

    // Where a card's face color comes from.
    enum class KanbanCardColorMode {
        ByCard,       // Card's customColor, palette cycle when unset
        Fixed,        // style.cardBackground for every card
        ByColumn,     // The owning column's color
        ByPriority,   // Priority color mapping
        ByLane        // The owning lane's palette color
    };

    enum class KanbanPriorityMarker {
        Hidden,
        Dot,          // Small colored circle in the card corner
        Chip          // Colored chip with the priority name
    };

    // Complete visual description of a Kanban board. Design presets fill
    // this in; every field can be adjusted afterwards.
    struct KanbanBoardStyle {
        // ----- Board layout metrics -----
        float boardPadding = 14.0f;
        float columnGap = 12.0f;
        float columnWidth = 0.0f;          // 0 = divide available width evenly
        float minColumnWidth = 150.0f;     // Lower bound for the even split
        float headerHeight = 30.0f;
        float headerGap = 10.0f;           // Space between header and body
        float footerHeight = 0.0f;         // Caption strip height (0 = hidden)
        float titleHeight = 0.0f;          // Board title band (0 = hidden)
        float legendHeight = 0.0f;         // Priority legend band (0 = hidden)
        float laneHeaderWidth = 0.0f;      // Swimlane tab width (0 = hidden)

        // ----- Card layout metrics -----
        KanbanCardLayout cardLayout = KanbanCardLayout::Stack;
        float cardGap = 10.0f;
        float cardPadding = 10.0f;
        float cardCornerRadius = 6.0f;
        float cardMinHeight = 0.0f;        // Grid/Pill layouts use this
        float staggerOffset = 26.0f;       // Horizontal shift for Stagger
        float cardSidePadding = 10.0f;     // Card inset from column edges

        // ----- Board chrome -----
        Color background = Color(255, 255, 255);
        Color boardPanelColor = Colors::Transparent; // Rounded panel behind all
        float boardPanelRadius = 12.0f;              // columns (Schematic look)
        bool showTitle = false;            // Draws the element's chartTitle
        Color titleColor = Color(45, 45, 55);
        float titleFontSize = 22.0f;
        bool showPriorityLegend = false;   // Overdue/High/... dot legend
        Color legendTextColor = Color(70, 70, 80);

        // ----- Column headers -----
        KanbanHeaderShape headerShape = KanbanHeaderShape::Bar;
        Color headerTextColor = Colors::White;
        bool uppercaseHeaders = false;
        float headerFontSize = 13.0f;
        bool showCardCount = false;        // "(4)" after the column title
        bool showWipBadge = true;          // "2/3" chip when a limit is set
        Color wipBadgeColor = Color(255, 255, 255, 70);
        Color wipExceededColor = Color(217, 48, 37);

        // ----- Column bodies -----
        KanbanColumnBackground columnBackground = KanbanColumnBackground::Track;
        Color columnTrackColor = Color(243, 244, 246);
        float columnPanelAlpha = 1.0f;     // Panel fill alpha (Panel mode)
        float columnCornerRadius = 8.0f;
        Color separatorColor = Color(210, 212, 218);
        bool separatorDashed = true;

        // ----- Swimlanes -----
        Color laneHeaderColor = Color(52, 62, 80);
        Color laneHeaderTextColor = Colors::White;
        Color laneSeparatorColor = Color(224, 226, 232);
        float laneMinHeight = 90.0f;
        float laneFontSize = 12.0f;

        // ----- Cards -----
        KanbanCardShape cardShape = KanbanCardShape::Rounded;
        KanbanCardColorMode cardColorMode = KanbanCardColorMode::ByCard;
        Color cardBackground = Colors::White;          // Fixed mode face color
        Color cardTitleColor = Colors::Transparent;    // Transparent = auto
        Color cardTextColor = Color(90, 95, 105);
        Color cardBorderColor = Colors::Transparent;   // Transparent = none
        float cardBorderWidth = 0.0f;
        bool cardShadow = false;
        Color cardShadowColor = Color(20, 25, 40, 40);
        float cardShadowOffset = 3.0f;
        float cardTitleFontSize = 12.5f;
        float cardTextFontSize = 11.0f;
        float metaFontSize = 10.0f;        // Due date / assignee / tag text

        // ----- Card content -----
        bool showCardDescription = true;
        int descriptionMaxLines = 3;
        bool showDueDate = true;
        std::string dueDateLabel = "Due Date:";  // Empty = no label line
        Color dueLabelColor = Colors::Transparent; // Transparent = column color
        Color overdueColor = Color(217, 48, 37);   // Needs SetToday()
        bool showAssignee = true;
        bool assigneeAsAvatar = true;      // Initials circle vs plain text
        bool showTags = true;
        KanbanPriorityMarker priorityMarker = KanbanPriorityMarker::Dot;
        float priorityDotRadius = 5.5f;

        // ----- Footer caption strip (image 5's Q1..Q4 band) -----
        Color footerColor = Color(45, 48, 56);
        Color footerTextColor = Color(225, 227, 232);

        // ----- Selection / hover / drag -----
        Color selectionColor = Color(0, 120, 215);     // Selected card outline
        Color hoverTint = Color(0, 0, 0, 10);          // Overlay on hovered card
        Color dropIndicatorColor = Color(0, 120, 215); // Insertion line
        float dragGhostAlpha = 0.75f;

        // ----- Editing chrome (visible when the element is editable) -----
        Color addButtonColor = Color(120, 126, 138);
        Color editorPanelColor = Colors::White;
        Color editorFieldColor = Color(244, 245, 248);
        Color editorTextColor = Color(40, 44, 52);
        Color editorAccentColor = Color(0, 120, 215);

        // ----- Fonts / misc -----
        std::string fontFamily;            // Empty = system default
        GanttDateFormat dateFormat = GanttDateFormat::ISO;

        // ----- Palette -----
        std::vector<Color> palette;        // Column color cycle; empty = default
        std::vector<Color> cardPalette;    // ByCard cycle; empty = pastel default
    };

// =============================================================================
// PALETTES & DESIGN PRESETS
// =============================================================================

    enum class KanbanPalette {
        Vibrant,         // Blue / purple / teal / red / green headers
        Pastel,          // Soft sticky-note colors
        CorporateBlue,   // Single blue family
        Ocean,           // Teal-blue family
        Sunset,          // Orange-red-pink family
        Slate,           // Muted professional tones
        Mono             // Grayscale
    };

    namespace KanbanPalettes {
        // Column color cycles for each named palette.
        const std::vector<Color>& Get(KanbanPalette palette);
        // Pastel card face colors (sticky-note boards).
        const std::vector<Color>& PastelCards();
        // Marker/chip colors for priorities (NoPriority..Urgent).
        const std::vector<Color>& PriorityColors();
    }

    // Ready-made designs modeled on common real-world Kanban board styles.
    enum class KanbanDesign {
        Professional,  // Project-tool look: neutral tracks, full-field cards,
                       // WIP badges, avatars, tag chips
        StickyNotes,   // Light board, slim colored headers, pastel offset
                       // sticky notes (reference image 1)
        Poster,        // Solid colored column panels, pill+dot headers,
                       // dog-ear cards, title + priority legend (image 2)
        Schematic,     // Gray rounded panel, dashed separators, compact
                       // color-block cards without text details (image 3)
        Timeline,      // Status-colored pill cards, swimlane tabs, footer
                       // caption strip (image 5)
        Dark           // Dark variant of Professional
    };

    namespace KanbanBoardStyles {
        KanbanBoardStyle CreateProfessional();
        KanbanBoardStyle CreateStickyNotes();
        KanbanBoardStyle CreatePoster();
        KanbanBoardStyle CreateSchematic();
        KanbanBoardStyle CreateTimeline();
        KanbanBoardStyle CreateDark();
        KanbanBoardStyle Create(KanbanDesign design);
    }

// =============================================================================
// KANBAN BOARD ELEMENT
// =============================================================================

    class UltraCanvasKanbanBoardElement : public UltraCanvasChartElementBase {
    public:
        UltraCanvasKanbanBoardElement(const std::string& id, int x, int y,
                                      int width, int height);

        // ===== DATA =====
        void SetKanbanDataSource(std::shared_ptr<KanbanDataSource> ds);
        std::shared_ptr<KanbanDataSource> GetKanbanDataSource() const {
            return std::dynamic_pointer_cast<KanbanDataSource>(dataSource);
        }

        // ===== STYLE =====
        void SetStyle(const KanbanBoardStyle& newStyle);
        const KanbanBoardStyle& GetStyle() const { return style; }
        // Mutate the style in place, then call StyleChanged().
        KanbanBoardStyle& EditStyle() { return style; }
        void StyleChanged();

        // Replace the whole style with a design preset (data untouched).
        void ApplyDesign(KanbanDesign design);
        // Swap only the column color cycle.
        void SetPalette(KanbanPalette palette);
        void SetCustomPalette(const std::vector<Color>& colors);

        // Reference date for overdue highlighting and editor defaults.
        void SetToday(const GanttDate& date);
        GanttDate GetToday() const { return today; }

        // ===== EDITING =====
        // Enables the built-in editor: drag & drop reordering, "+ Add card"
        // per column, "+ Add column", double-click card editor overlay,
        // double-click column rename. Off by default.
        void SetEditable(bool editable);
        bool IsEditable() const { return editable; }

        // Opens the card editor overlay programmatically (-1 aborts).
        void OpenCardEditor(int cardId);
        void CloseCardEditor(bool save);

        // ===== SELECTION / VIEW =====
        int GetSelectedCardId() const { return selectedCardId; }
        void SelectCard(int cardId);             // -1 clears
        void ScrollToCard(int cardId);

        // ===== CALLBACKS =====
        std::function<void(int cardId, int fromColumnId, int toColumnId)> onCardMoved;
        std::function<void(int cardId)> onCardAdded;      // Via the editor UI
        std::function<void(int cardId)> onCardRemoved;    // Via the editor UI
        std::function<void(int cardId)> onCardClick;
        std::function<void(int cardId)> onCardDoubleClick; // Editor consumed it
                                                           // when editable
        std::function<void(int cardId)> onSelectionChanged; // -1 = cleared
        std::function<void(int columnId)> onWipExceeded;
        std::function<void()> onBoardChanged;             // Any editor mutation

        // Editable boards can veto a drop (e.g. strict WIP enforcement).
        std::function<bool(int cardId, int toColumnId)> canDropCard;

        // ===== FRAMEWORK OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }

    private:
        // ----- Resolved per-frame layout -----
        struct ColumnLayout {
            int columnId = -1;
            Rect2Dd headerRect;
            Rect2Dd bodyRect;              // Card area (scrolls vertically)
            Rect2Dd footerRect;            // Caption strip (may be empty)
            Rect2Dd addButtonRect;         // "+ Add card" (editable only)
            float contentHeight = 0.0f;    // Cards + gaps, before scrolling
        };
        struct LaneLayout {
            int laneId = -1;               // -1 = implicit default lane
            Rect2Dd headerRect;            // Left tab (may be empty)
            double top = 0.0, height = 0.0;
        };
        struct CardLayout {
            int cardId = -1;
            int columnIndex = -1;
            Rect2Dd rect;                  // Screen rect after scrolling
        };
        struct Layout {
            Rect2Dd titleRect, legendRect, boardRect;
            Rect2Dd addColumnRect;         // "+" chip (editable only)
            std::vector<ColumnLayout> columns;
            std::vector<LaneLayout> lanes;
            std::vector<CardLayout> cards; // Visible cards only
        };

        // ----- Inline editor overlay -----
        struct EditorField {
            std::string label;
            std::string text;
            Rect2Dd rect;
            bool multiline = false;
        };
        struct EditorState {
            bool open = false;
            int cardId = -1;
            bool newCard = false;      // Cancel removes the card again
            // Field order: Title, Description, Assignee, Due date, Tags
            std::vector<EditorField> fields;
            int focusedField = 0;
            KanbanPriority priority = KanbanPriority::NoPriority;
            Rect2Dd panelRect, priorityRect, saveRect, cancelRect, deleteRect;
        };

        KanbanBoardStyle style;
        Layout layout;
        GanttDate today;
        bool todaySet = false;
        bool editable = false;

        float scrollX = 0.0f;
        // Wheel scrolling glides to its target (see UltraCanvasSmoothScroll.h).
        // The board scrolls sideways and each column scrolls on its own, but
        // only one column can be under the wheel at a time, so a single animator
        // is re-bound to whichever column is being wheeled — re-binding cancels
        // the previous column's glide, which is exactly what moving on to
        // another column should do.
        // Largest horizontal scroll the columns allow, refreshed by the layout
        // pass so the wheel's glide knows its range.
        float maxScrollX = 0.0f;
        UltraCanvasSmoothScroll scrollAnimX;
        UltraCanvasSmoothScroll columnScrollAnim;
        int columnScrollAnimKey = 0;   // column id columnScrollAnim is bound to
        std::map<int, float> columnScroll;   // columnId -> scrollY
        int selectedCardId = -1;
        int hoveredCardId = -1;

        // Drag & drop state
        int pressedCardId = -1;
        Point2Di pressAnchor;
        Point2Dd pressOffset;                // Grab point inside the card
        bool draggingCard = false;
        Point2Di dragPos;
        int dropColumnIndex = -1;
        int dropLaneId = -1;
        int dropInsertIndex = -1;

        // Inline column rename
        int renamingColumnId = -1;
        std::string renameBuffer;

        EditorState editor;

        // ----- Layout helpers -----
        void ComputeLayout(IRenderContext* ctx);
        double CardHeight(IRenderContext* ctx, const KanbanCard& card,
                          double cardWidth) const;
        const CardLayout* CardAt(const Point2Di& pos) const;
        int ColumnIndexAt(const Point2Di& pos) const;
        int LaneIdAt(const Point2Di& pos) const;
        void UpdateDropTarget(const Point2Di& pos);
        void ClampScroll();
    // Glides the vertical scroll of one column (id -1 = the swim-lane board).
    void AnimateColumnScrollBy(int columnId, float delta);
        float MaxScrollX() const { return maxScrollX; }
        float MaxColumnScroll(int columnId) const;

        // ----- Color helpers -----
        Color ColumnColor(size_t columnIndex, const KanbanColumn& col) const;
        Color CardFaceColor(const KanbanCard& card, size_t cardOrdinal,
                            size_t columnIndex) const;
        Color CardTitleColor(const KanbanCard& card, size_t columnIndex) const;
        std::string FormatDate(const GanttDate& date) const;

        // ----- Rendering -----
        void RenderTitleAndLegend(IRenderContext* ctx);
        void RenderColumns(IRenderContext* ctx);
        void RenderLanes(IRenderContext* ctx);
        void RenderColumnHeader(IRenderContext* ctx, size_t columnIndex,
                                const ColumnLayout& cl);
        void RenderCards(IRenderContext* ctx);
        void RenderCard(IRenderContext* ctx, const KanbanCard& card,
                        const Rect2Dd& rect, size_t cardOrdinal,
                        size_t columnIndex, bool ghost);
        void RenderCardShape(IRenderContext* ctx, const Rect2Dd& rect,
                             const Color& face);
        void RenderDropIndicator(IRenderContext* ctx);
        void RenderEditingChrome(IRenderContext* ctx);
        void RenderEditorOverlay(IRenderContext* ctx);

        // ----- Interaction -----
        bool HandleWheel(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleDoubleClick(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        bool HandleTextInput(const UCEvent& event);
        bool EditorMouseDown(const UCEvent& event);
        void CommitRename(bool save);
        void StartAddCard(int columnId);
        void StartAddColumn();
        std::string BuildCardTooltip(const KanbanCard& card) const;
        void NotifyWipIfExceeded(int columnId);
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<UltraCanvasKanbanBoardElement> CreateKanbanBoardElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasKanbanBoardElement>(id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasKanbanBoardElement> CreateKanbanBoardWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<KanbanDataSource> data,
            KanbanDesign design = KanbanDesign::Professional,
            const std::string& title = "") {
        auto board = CreateKanbanBoardElement(id, x, y, width, height);
        board->ApplyDesign(design);
        board->SetKanbanDataSource(data);
        if (!title.empty()) {
            board->SetChartTitle(title);
        }
        return board;
    }

    // Builds the board from a Mermaid `kanban` text definition (see
    // KanbanDataSource::LoadFromText). Returns nullptr on a parse error and
    // fills `error` when given.
    inline std::shared_ptr<UltraCanvasKanbanBoardElement> CreateKanbanBoardFromText(
            const std::string& id, int x, int y, int width, int height,
            const std::string& textDefinition,
            KanbanDesign design = KanbanDesign::Professional,
            std::string* error = nullptr) {
        auto data = std::make_shared<KanbanDataSource>();
        if (!data->LoadFromText(textDefinition, error)) return nullptr;
        return CreateKanbanBoardWithData(id, x, y, width, height, data, design);
    }

} // namespace UltraCanvas
