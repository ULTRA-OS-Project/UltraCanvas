// Apps/UltraDBAdmin/ui/DbAdminWindowParts.h
// What the window's two sources share (DbAdminWindow.cpp, the window;
// DbAdminWindowObjects.cpp, its tabs of open objects, the tree's operations,
// Search and the SQL log; DbAdminWindowSchema.cpp, Relations, Routines and
// Server): its sizes, its tab order and the small layout
// helpers its pages are built with. Internal to the window.
// Version: 0.2.0 - the Relations, Routines and Server tabs
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminFormat.h"
#include "DbAdminTheme.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTextArea.h"

#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace UltraDBAdmin {
namespace window {

using namespace UltraCanvas;

inline constexpr float kWindowWidth   = 1180.0f;
inline constexpr float kWindowHeight  = 740.0f;
inline constexpr float kPagePadding   = 10.0f;
inline constexpr float kTabPadding    = 10.0f;
inline constexpr float kSectionGap    = 8.0f;
inline constexpr float kControlHeight = kButtonHeight;
inline constexpr float kBarHeight     = 34.0f;
inline constexpr float kHeaderHeight  = 48.0f;
inline constexpr int   kTreeWidth     = 280;
inline constexpr int   kTreeMinWidth  = 180;
inline constexpr int   kStatementWidth = 380;
inline constexpr size_t kPageSize     = 200;

// Tab order, as added below.
inline constexpr int kBrowseTab    = 0;
inline constexpr int kStructureTab = 1;
inline constexpr int kSqlTab       = 2;
inline constexpr int kSearchTab    = 3;
inline constexpr int kRelationsTab = 4;
inline constexpr int kRoutinesTab  = 5;
inline constexpr int kServerTab    = 6;
inline constexpr size_t kRelationsLimit = 120;   // tables in one diagram
inline constexpr double kRelationsFitPadding = 60.0;   // room for a self-reference's loop and its label

inline constexpr int kLogHeight      = 150;   // the SQL log under the tabs, to begin with
inline constexpr int kObjectStripHeight = 36;
inline constexpr unsigned kLogRefreshMs = 300;
inline constexpr unsigned kCellFollowMs = 100;

inline constexpr const char* kPlaceholderText = "…";



// ----- layout helpers, as UltraNetMonitor has them -----

// A pane's single child fills it edge to edge: basis zero and grow one, so
// the pane's size decides and the child holds no size of its own against it.
inline void FillWith(const std::shared_ptr<UltraCanvasContainer>& container,
              const std::shared_ptr<UltraCanvasUIElement>& element, float grow = 1.0f) {
    container->AddChild(element);
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetFlexGrow(grow);
    element->layoutItem.SetFlexShrink(1);
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

inline void MakePlainColumn(const std::shared_ptr<UltraCanvasContainer>& container) {
    container->layout.SetFlexColumn()
                     .SetFlexGap(kSectionGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    container->SetContainerStyle(plain);
}

inline std::shared_ptr<UltraCanvasContainer> MakeToolbar(const std::string& id, float height = kBarHeight) {
    auto bar = CreateContainer(id, 0, 0, 0, height);
    bar->layout.SetFlexRow()
               .SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
    ContainerStyle plainBar;
    plainBar.autoShowScrollbars = false;
    bar->SetContainerStyle(plainBar);
    return bar;
}

inline void PinToolbar(const std::shared_ptr<UltraCanvasUIElement>& bar) {
    bar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bar->layoutItem.SetFlexGrow(0);
    bar->layoutItem.SetFlexShrink(0);
}

// Takes what the controls leave, and gives it back first (ellipsized).
inline std::shared_ptr<UltraCanvasLabel> MakeBarStatus(const std::string& id, const std::string& text = "") {
    auto label = CreateLabel(id, 0, 0, 0, 24, text);
    label->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    label->layoutItem.SetFlexGrow(1);
    label->layoutItem.SetFlexShrink(1);
    label->SetTextColor(StatusColor());
    label->SetAlignment(TextAlignment::Left);
    return label;
}

// A tab's page: no size of its own - the tabbed container sizes it.
inline std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id) {
    auto page = CreateContainer(id, 0, 0, 0, 0);
    MakePlainColumn(page);
    page->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    page->SetPadding(kTabPadding);
    return page;
}

// The splitters are the gaps between the cards: the ground, a grip on it.
inline SplitPaneStyle PaneStyle() {
    SplitPaneStyle style;
    style.splitterThickness = 8;
    style.splitterHitMargin = 3;
    style.handle.shape      = SplitterHandleShape::RoundedSquare;
    style.handle.crossSize  = 8;
    style.handle.axisLength = 40;
    return SplitPaneStyle::FromTheme(UltraCanvasTheme::Current(), style);
}

// The tree's icon for a node of the given kind.
inline std::string ObjectIcon(UltraDbObjectKind kind) {
    switch (kind) {
        case UltraDbObjectKind::Table:            return IconPath("node-table");
        case UltraDbObjectKind::View:             return IconPath("node-view");
        case UltraDbObjectKind::MaterializedView: return IconPath("node-materialized-view");
        case UltraDbObjectKind::ForeignTable:     return IconPath("node-foreign-table");
        case UltraDbObjectKind::Sequence:         return IconPath("node-sequence");
    }
    return IconPath("node-table");
}

inline std::shared_ptr<UltraCanvasListView> MakeList(const std::string& id, const std::shared_ptr<IListModel>& model) {
    auto view = std::make_shared<UltraCanvasListView>(id, -1, -1, 400, 200);
    view->SetModel(model);
    view->SetShowHeader(true);
    view->SetShowItemTooltips(true);
    StyleList(*view);
    return view;
}

inline const char* KindWord(UltraDbObjectKind kind) {
    switch (kind) {
        case UltraDbObjectKind::Table:            return "table";
        case UltraDbObjectKind::View:             return "view";
        case UltraDbObjectKind::MaterializedView: return "materialized view";
        case UltraDbObjectKind::ForeignTable:     return "foreign table";
        case UltraDbObjectKind::Sequence:         return "sequence";
    }
    return "object";
}

// "4 512" rather than "4512": a row count is read at a glance.
inline std::string Grouped(int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += " ";   // thin space
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

inline std::string Seconds(double seconds) {
    // locale-ok: a duration for the reader, in the desktop's own decimal mark.
    char text[32];
    std::snprintf(text, sizeof text, seconds < 10.0 ? "%.2f s" : "%.1f s", seconds);
    return text;
}

// A message for a one-line label: a server's error can run to three lines
// ("... Is the server running on that host?"); the tooltip has all of it.
inline std::string OneLine(const std::string& text) {
    return CellText(UltraDbValue(text), 240);
}

// Where byte `offset` of `text` is in the editor's terms: its line, and the
// code points before it on that line.
inline LineColumnIndex LineColumnAt(const std::string& text, size_t offset) {
    LineColumnIndex at{0, 0};
    for (size_t i = 0; i < offset && i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') { ++at.lineIndex; at.columnIndex = 0; }
        else if ((c & 0xC0) != 0x80) ++at.columnIndex;
    }
    return at;
}

// The first line of a script, for the history dropdown.
inline std::string FirstLine(const std::string& script) {
    return CellText(UltraDbValue(script), 70);
}

} // namespace window
} // namespace UltraDBAdmin
