// include/Plugins/Diagrams/UltraCanvasSequenceDiagram.h
// UML sequence diagram element
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// Renders an UltraCanvasSequenceModel as a UML 2 sequence diagram: lifeline
// heads (object boxes, actors, robustness icons, database cylinders), dashed
// lifelines, nested execution bars, the seven message arrow forms, combined
// fragments (loop / alt / opt / par / break / critical) with pentagon labels
// and operand separators, and anchored notes.
//
// The layers are deliberately separate, matching the class-diagram feature:
//
//   UltraCanvasSequenceModel     meaning   (what the lifelines and messages ARE)
//   UltraCanvasSequenceDiagram   geometry + appearance (this file)
//
// Only this file needs a window. The model — including activation nesting and
// hierarchical message numbering — is unit-tested without one
// (Tests/SequenceModelTest.cpp).
//
// Vertical order is message order: every y coordinate derives from a message
// index, so the layout is a single walk down the message list. Horizontal
// positions are solved from head widths and message label widths, measured
// with the real render context on the first paint. Marking the model dirty
// re-measures and re-lays out on the next frame.
//
// CHANGELOG 1.0.0:
//  - Initial implementation: six themes, zoom/pan/fit view, hover and
//    selection with callbacks, hierarchical or sequential message numbering,
//    optional "sd" frame and foot boxes.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Diagrams/UltraCanvasSequenceModel.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

enum class SequenceDiagramTheme {
    Default,       // neutral blue-grey heads on white — the house look
    Classic,       // reference image 1: golden head boxes, hairline arrows
    Professional,  // reference image 2: teal heads and activation bars
    Colorful,      // reference image 3: a different accent per lifeline
    Minimal,       // no fills, hairline borders
    Dark
};

// What number, if any, is written before each message label.
enum class SequenceNumbering {
    Hidden,
    Sequential,    // 1, 2, 3, ...
    Hierarchical   // 1, 1.1, 1.1.1, 1.2 — follows the call nesting
};

// =============================================================================
// STYLE
// =============================================================================

// Per-diagram appearance. Every colour is settable, so a theme is just a
// preset that fills this in.
struct SequenceDiagramStyle {
    // Canvas
    Color backgroundColor = Color(252, 252, 253, 255);

    // Lifeline heads
    Color headColor = Color(222, 232, 244, 255);
    Color headBorderColor = Color(70, 90, 115, 255);
    Color headTextColor = Color(30, 38, 48, 255);
    double headBorderWidth = 1.5;
    double headCornerRadius = 3.0;
    double headPaddingX = 12.0;
    double headPaddingY = 8.0;
    double minHeadWidth = 70.0;

    // Lifelines
    Color lifelineColor = Color(120, 130, 145, 255);
    double lifelineWidth = 1.2;
    bool showFootBoxes = false;    // repeat the head at the bottom

    // Execution (activation) bars
    Color activationColor = Color(235, 240, 246, 255);
    Color activationBorderColor = Color(70, 90, 115, 255);
    double activationWidth = 14.0;
    double activationNestingOffset = 5.0;   // horizontal shift per nesting level

    // Messages
    Color messageColor = Color(45, 55, 70, 255);
    Color messageTextColor = Color(35, 42, 52, 255);
    double messageLineWidth = 1.4;
    double arrowheadLength = 11.0;
    double arrowheadWidth = 8.0;
    double selfMessageWidth = 46.0;    // horizontal reach of the loop-back
    double gateStubLength = 52.0;      // found / lost message stub
    double gateCircleRadius = 5.0;

    // Combined fragments
    Color fragmentBorderColor = Color(90, 100, 115, 255);
    Color fragmentLabelColor = Color(228, 234, 241, 255);
    Color fragmentLabelTextColor = Color(35, 42, 52, 255);
    Color fragmentGuardTextColor = Color(60, 70, 85, 255);
    Color fragmentFillColor = Color(0, 0, 0, 0);   // transparent by default
    double fragmentBorderWidth = 1.3;
    double fragmentPadding = 8.0;      // extra width beyond the covered lifelines

    // Notes
    Color noteColor = Color(255, 249, 196, 255);
    Color noteBorderColor = Color(190, 175, 110, 255);
    Color noteTextColor = Color(60, 55, 35, 255);
    double noteMaxWidth = 180.0;
    double notePadding = 7.0;

    // Title / frame ("sd Name" pentagon in the top-left corner)
    Color frameBorderColor = Color(90, 100, 115, 255);
    Color frameLabelColor = Color(228, 234, 241, 255);
    Color frameLabelTextColor = Color(35, 42, 52, 255);
    double frameBorderWidth = 1.3;

    // Selection / hover
    Color selectionColor = Color(0, 122, 204, 255);
    Color hoverColor = Color(0, 122, 204, 80);
    double selectionWidth = 2.2;

    // Text
    std::string fontFamily = "Arial";
    double headFontSize = 13.0;
    double messageFontSize = 12.0;
    double fragmentFontSize = 11.5;
    double noteFontSize = 11.0;
    double titleFontSize = 14.0;

    // Vertical rhythm
    double topMargin = 16.0;
    double sideMargin = 24.0;
    double headGap = 24.0;         // head bottom to first message
    double rowGap = 14.0;          // between message rows
    double bottomMargin = 26.0;
    double fragmentHeaderHeight = 22.0;
    double operandGap = 20.0;      // room for a dashed operand separator

    // Horizontal rhythm
    double minLifelineGap = 60.0;  // centre-to-centre floor
    double labelPadding = 14.0;    // label width → required lifeline distance
};

// =============================================================================
// ELEMENT
// =============================================================================

class UltraCanvasSequenceDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasSequenceDiagram(const std::string& id, int x, int y, int width, int height);

    bool AcceptsFocus() const override { return true; }

    // ---- model ----------------------------------------------------------

    UltraCanvasSequenceModel& Model() { return model; }
    const UltraCanvasSequenceModel& Model() const { return model; }
    // Replaces the model wholesale; the layout is recomputed on the next paint.
    void SetModel(const UltraCanvasSequenceModel& value);
    void Clear();

    // Convenience forwarders so simple diagrams read well at the call site.
    // Each marks the diagram dirty.
    std::string AddLifeline(const std::string& name,
                            SequenceLifelineKind kind = SequenceLifelineKind::Object);
    std::string AddActor(const std::string& name);
    std::string AddDatabase(const std::string& name);
    size_t AddMessage(const std::string& from, const std::string& to,
                      const std::string& label,
                      SequenceMessageKind kind = SequenceMessageKind::Synchronous);
    size_t AddReturnMessage(const std::string& from, const std::string& to,
                            const std::string& label = std::string());
    size_t AddSelfMessage(const std::string& lifeline, const std::string& label);
    std::string AddLoop(size_t firstMessage, size_t lastMessage,
                        const std::string& guard = std::string());
    std::string AddAlt(size_t firstMessage, size_t lastMessage,
                       const std::string& firstGuard = std::string());

    // Call after mutating the model directly through Model().
    void MarkModelChanged();

    // ---- presentation ---------------------------------------------------

    void SetTheme(SequenceDiagramTheme value);
    SequenceDiagramTheme GetTheme() const { return theme; }

    SequenceDiagramStyle& Style() { return style; }
    const SequenceDiagramStyle& Style() const { return style; }
    void SetStyle(const SequenceDiagramStyle& value);

    void SetTitle(const std::string& value);
    const std::string& GetTitle() const { return model.Title(); }

    // Draws the UML "sd Title" frame around the diagram.
    void SetShowFrame(bool value);
    bool GetShowFrame() const { return showFrame; }

    void SetShowFootBoxes(bool value);
    bool GetShowFootBoxes() const { return style.showFootBoxes; }

    void SetNumbering(SequenceNumbering value);
    SequenceNumbering GetNumbering() const { return numbering; }

    // Assigns a distinct head colour to every lifeline from a built-in
    // qualitative palette (the Colorful theme's look, usable with any theme).
    void ApplyPaletteByLifeline();

    // ---- view -----------------------------------------------------------

    void SetZoomLevel(double zoom);
    double GetZoomLevel() const { return zoomLevel; }
    void SetPanOffset(double x, double y);
    Point2Dd GetPanOffset() const { return panOffset; }
    // Fits the whole drawing into the element on the next paint.
    void FitView();
    void ResetView();

    // Read-only presentation mode: zoom and pan still work, selection does not.
    void SetInteractive(bool value);
    bool IsInteractive() const { return interactive; }

    // ---- selection ------------------------------------------------------

    // -1 = nothing selected.
    int GetSelectedMessage() const { return selectedMessage; }
    const std::string& GetSelectedLifelineId() const { return selectedLifelineId; }
    void SelectMessage(int index);
    void SelectLifeline(const std::string& idOrName);
    void DeselectAll();

    // ---- element --------------------------------------------------------

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // ---- callbacks ------------------------------------------------------

    std::function<void(size_t, const SequenceMessage&)> onMessageClick;
    std::function<void(const std::string&)> onLifelineClick;
    std::function<void(const std::string&)> onFragmentClick;
    std::function<void()> onSelectionChanged;

private:
    // =========================================================================
    // CACHED GEOMETRY (world coordinates, i.e. before zoom/pan)
    // =========================================================================

    struct LifelineGeometry {
        std::string id;
        Rect2Dd headRect;        // for Create targets this sits at the message
        double centerX = 0.0;
        double topY = 0.0;       // where the dashed line starts
        double bottomY = 0.0;    // where it ends (destroy X or diagram bottom)
        bool destroyed = false;  // ends in an X
        Color headColor;
        Rect2Dd footRect;        // valid when style.showFootBoxes
    };

    struct MessageGeometry {
        size_t index = 0;
        double y = 0.0;          // arrow y
        double fromX = 0.0;      // arrow endpoints, activation-bar adjusted
        double toX = 0.0;
        double selfBottomY = 0.0;   // self messages: lower leg y
        Rect2Dd labelRect;
        std::string displayLabel;   // number prefix + label
    };

    struct ActivationGeometry {
        std::string lifelineId;
        Rect2Dd rect;
    };

    struct FragmentGeometry {
        std::string fragmentId;
        Rect2Dd rect;
        Rect2Dd labelRect;                    // pentagon tab
        std::vector<double> separatorYs;      // one per operand past the first
        std::vector<std::string> separatorGuards;
        std::string headerText;               // "loop", "alt", ...
        std::string guardText;                // "[until valid]"
    };

    struct NoteGeometry {
        size_t noteIndex = 0;
        Rect2Dd rect;
        std::vector<std::string> lines;
    };

    // =========================================================================
    // STATE
    // =========================================================================

    UltraCanvasSequenceModel model;
    SequenceDiagramTheme theme = SequenceDiagramTheme::Default;
    SequenceDiagramStyle style;
    SequenceNumbering numbering = SequenceNumbering::Hidden;
    bool showFrame = false;
    bool interactive = true;

    double zoomLevel = 1.0;
    Point2Dd panOffset = Point2Dd(0, 0);
    bool fitPending = false;

    bool layoutDirty = true;

    // Interaction state
    bool isPanning = false;
    Point2Di panStartScreen;
    Point2Dd panStartOffset;
    int hoveredMessage = -1;
    std::string hoveredLifelineId;
    int selectedMessage = -1;
    std::string selectedLifelineId;

    // Layout cache
    std::vector<LifelineGeometry> lifelineGeometry;
    std::vector<MessageGeometry> messageGeometry;
    std::vector<ActivationGeometry> activationGeometry;
    std::vector<FragmentGeometry> fragmentGeometry;
    std::vector<NoteGeometry> noteGeometry;
    Rect2Dd contentBounds;   // union of everything, for FitView

    // =========================================================================
    // LAYOUT
    // =========================================================================

    void RebuildLayout(IRenderContext* ctx);
    // Head sizes and lifeline x centres, from head and label widths.
    void SolveHorizontal(IRenderContext* ctx, std::vector<double>& centers,
                         std::vector<Rect2Dd>& headSizes);
    // One walk down the message list assigning every y. Fills noteGeometry
    // and the fragments' vertical extents on the way (both need row context).
    void WalkVertical(IRenderContext* ctx, const std::vector<double>& centers,
                      std::vector<double>& messageYs, std::vector<double>& selfBottoms,
                      double& bottomY);
    void PlaceActivations(const std::vector<double>& messageYs, double bottomY,
                          double headBottom);
    void PlaceFragmentHorizontals();
    // The x of an arrow endpoint: the lifeline centre pushed to the edge of
    // its deepest activation bar at that y.
    double AttachX(const std::string& lifelineId, double y, bool towardsRight) const;
    int LifelinePosition(const std::string& lifelineId) const;

    // =========================================================================
    // DRAWING
    // =========================================================================

    void RenderFrame(IRenderContext* ctx);
    void RenderLifelines(IRenderContext* ctx);
    void RenderHead(IRenderContext* ctx, const LifelineGeometry& geometry,
                    const SequenceLifeline& lifeline, const Rect2Dd& rect);
    void RenderActorIcon(IRenderContext* ctx, const Rect2Dd& rect, const Color& color);
    void RenderDatabaseIcon(IRenderContext* ctx, const Rect2Dd& rect,
                            const Color& fill, const Color& border);
    void RenderRobustnessIcon(IRenderContext* ctx, SequenceLifelineKind kind,
                              const Rect2Dd& rect, const Color& fill, const Color& border);
    void RenderActivations(IRenderContext* ctx);
    void RenderFragments(IRenderContext* ctx);
    void RenderMessages(IRenderContext* ctx);
    void RenderMessage(IRenderContext* ctx, const MessageGeometry& geometry,
                       const SequenceMessage& message, bool highlighted);
    void RenderArrowhead(IRenderContext* ctx, const Point2Dd& tip, bool pointsRight,
                         bool filled);
    void RenderNotes(IRenderContext* ctx);

    // =========================================================================
    // HELPERS
    // =========================================================================

    Color HeadColorFor(const SequenceLifeline& lifeline, size_t position) const;
    std::vector<std::string> WrapNoteText(IRenderContext* ctx, const std::string& text,
                                          double maxWidth) const;
    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    void ApplyFit();

    // World-space hit tests against the layout cache.
    int FindMessageAt(const Point2Dd& worldPos) const;
    const LifelineGeometry* FindLifelineAt(const Point2Dd& worldPos) const;
    const FragmentGeometry* FindFragmentLabelAt(const Point2Dd& worldPos) const;

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
};

// =============================================================================
// THEME PRESETS
// =============================================================================

namespace SequenceDiagramStyles {
    SequenceDiagramStyle CreateForTheme(SequenceDiagramTheme theme);
    std::vector<Color> GetLifelinePalette();
}

// =============================================================================
// SAMPLE DATA
// =============================================================================

namespace SequenceDiagramSamples {
    // Card game round: five object lifelines, self messages, an
    // always-active driver — reference image 1.
    UltraCanvasSequenceModel CardGame();
    // Actor-driven login flow with returns — reference image 2.
    UltraCanvasSequenceModel UserLogin();
    // OAuth-style token exchange across four services with notes —
    // reference image 3.
    UltraCanvasSequenceModel Authentication();
    // Web shop checkout with a loop and an alt fragment — reference image 4.
    UltraCanvasSequenceModel OnlineShop();
    // Object lifecycle: create, work, destroy, lost/found gates.
    UltraCanvasSequenceModel ObjectLifecycle();
    // How this framework itself turns an XEvent into a button callback —
    // UltraCanvasApplicationBase::DispatchEvent end to end.
    UltraCanvasSequenceModel UltraCanvasEventFlow();
}

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

std::shared_ptr<UltraCanvasSequenceDiagram> CreateSequenceDiagramElement(
        const std::string& id, int x, int y, int width, int height);

std::shared_ptr<UltraCanvasSequenceDiagram> CreateSequenceDiagramWithModel(
        const std::string& id, int x, int y, int width, int height,
        const UltraCanvasSequenceModel& model);

} // namespace UltraCanvas
