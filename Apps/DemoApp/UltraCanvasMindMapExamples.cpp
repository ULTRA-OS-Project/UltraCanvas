// Apps/DemoApp/UltraCanvasMindMapExamples.cpp
// Demo examples for UltraCanvasMindMap — mind map diagram
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the tabs in a
//                  card that fills the area, every tab a column whose
//                  description wraps above the map, buttons as wide as their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasMindMap.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include <sstream>
#include <cmath>

namespace UltraCanvas {

namespace {

// A tab: its description over the map, which takes the rest of the height
// the tab gives it.
std::shared_ptr<UltraCanvasContainer> MakeMapTab(const std::string& id, const std::string& descId,
                                                 const std::string& text) {
    auto tab = DemoKit::CreateColumn(id, 6);
    tab->SetPadding(8, 10, 10, 10);
    DemoKit::AddText(tab, descId, text)->SetFontSize(11);
    return tab;
}

// The map was built at a size for its first fit; in the tab it drops that
// and grows from a zero basis into whatever height is left.
void AddMap(const std::shared_ptr<UltraCanvasContainer>& tab,
            const std::shared_ptr<UltraCanvasMindMap>& map) {
    map->size.width = CSSLayout::Dimension::Auto();
    map->size.height = CSSLayout::Dimension::Auto();
    DemoKit::Add(tab, map, 1);
    map->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);
}

// A gallery button as wide as its label, at least the width it had.
std::shared_ptr<UltraCanvasButton> MakeGalleryButton(const std::string& id, const std::string& label,
                                                     float minWidth) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 24, label);
    CSSLayout::BoxConstraints limits;
    limits.minWidth = CSSLayout::Dimension::Px(minWidth);
    button->boxConstraints = limits;
    return button;
}

// Wires the shared status read-out every tab uses.
void AttachStatus(const std::shared_ptr<UltraCanvasMindMap>& map,
                  const std::shared_ptr<UltraCanvasLabel>& status) {
    map->onTopicClick = [map = map.get(), status](const std::string& id) {
        const MindMapTopic* topic = map->GetTopic(id);
        if (!topic) return;
        std::string text = TrF("\"{0}\"  ·  depth {1}", topic->text, topic->depth);
        int descendants = map->Model().CountDescendants(id);
        if (descendants > 0) text += "  ·  " + TrN("{n} descendant", "{n} descendants", descendants);
        status->SetText(text);
    };
    map->onCollapseChanged = [map = map.get(), status](const std::string& id, bool collapsed) {
        const MindMapTopic* topic = map->GetTopic(id);
        const std::string what = topic ? "\"" + topic->text + "\"" : id;
        status->SetText(collapsed ? TrF("Collapsed {0}", what) : TrF("Expanded {0}", what));
    };
    map->onTopicTextChanged = [status](const std::string&, const std::string& previous) {
        status->SetText(TrF("Renamed (was \"{0}\")", previous));
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 — Classic balanced map, editable (reference image 4)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeEditableTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = MakeMapTab("MindMapEditableTab", "MindMapEditableDesc",
            Tr("Balanced structure with one branch overridden to an org chart. "
            "Double-click or F2 to rename, Enter for a sibling, Tab for a child, "
            "Delete to remove, drag onto another topic to reparent, Ctrl+drag to "
            "empty canvas to create, Ctrl+C/X/V to copy, Ctrl+Z to undo."));

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapEditable", 0, 0, 990, 654);
    map->SetTheme(MindMapTheme::Colorful);
    map->SetStructure(MindMapStructure::Balanced);
    map->SetBalancePolicy(MindMapBalancePolicy::MinimiseSubtreeWeight);
    map->SetControlsVisible(true);
    map->SetMinimapVisible(true);

    map->BuildFromOutline({
        {0, Tr("Enforce fire safety regulations")},
        {1, Tr("Inspection")},
        {2, Tr("Periodic inspection")},
        {2, Tr("Inspection by complaints")},
        {2, Tr("Special inspection")},
        {3, Tr("Prior to a big event")},
        {1, Tr("Personnel")},
        {2, Tr("Manage inspector works")},
        {2, Tr("Conduct inspections")},
        {2, Tr("Assist inspectors")},
        {2, Tr("Draft inspection form")},
        {1, Tr("Risk")},
        {2, Tr("Types of risk")},
        {3, Tr("Low risk")},
        {3, Tr("High risk")},
        {2, Tr("Business nature")},
        {2, Tr("Property type")},
        {1, Tr("Inspection report")},
        {2, Tr("Keep in department")},
        {2, Tr("Send to archive")},
        {2, Tr("May include photos")},
    });

    // One branch drawn as an org chart while the rest of the map stays a mind
    // map - the XMind "combine structures" behaviour (L8).
    const MindMapTopic* editableRoot = map->GetTopic(map->GetRootId());
    if (editableRoot && editableRoot->childIds.size() >= 4) {
        if (MindMapTopic* branch = map->GetTopic(editableRoot->childIds[3])) {
            branch->structureOverride = MindMapStructure::OrgChartDown;
        }
    }
    map->Style().layoutAnimationMs = 200.0;

    AttachStatus(map, status);
    AddMap(tab, map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 — Logic chart with numbered badges and an icon (reference image 8)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeLogicChartTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = MakeMapTab("MindMapLogicTab", "MindMapLogicDesc",
            Tr("LogicRight structure: the whole tree grows to one side. Main topics carry "
            "automatic ordinal badges in their branch hue; leaves use the underline "
            "shape with no box."));

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapLogic", 0, 0, 990, 654);
    map->SetTheme(MindMapTheme::Default);
    map->SetStructure(MindMapStructure::LogicRight);
    map->SetNumbering(MindMapNumbering::Badge);
    map->SetControlsVisible(true);

    map->BuildFromOutline({
        {0, Tr("Mind Mapping")},
        {1, Tr("Analysis")},
        {2, Tr("A structured approach to brainstorming")},
        {2, Tr("Define what you know and do not know")},
        {1, Tr("Objectives")},
        {2, Tr("Define the goal clearly")},
        {2, Tr("Identify success criteria")},
        {1, Tr("Strategy")},
        {2, Tr("Brainstorm different strategies")},
        {2, Tr("Evaluate pros and cons of each")},
        {1, Tr("Action")},
        {2, Tr("Break the strategy into steps")},
        {2, Tr("Identify required resources")},
        {2, Tr("Assign responsibilities")},
        {2, Tr("Set timelines")},
        {1, Tr("Revision")},
        {2, Tr("Review and refine the scheme")},
        {2, Tr("Make adjustments as needed")},
        {2, Tr("Update responsibilities and timelines")},
        {1, Tr("Solution")},
        {2, Tr("Implement the plan")},
        {2, Tr("Track progress against the goal")},
        {2, Tr("Celebrate success")},
        {2, Tr("Continuously improve")},
    });

    // Leaves as plain underlined text, the software mind-map convention.
    MindMapTopicStyle leaf = map->GetLevelStyle(2);
    leaf.shape = MindMapNodeShape::Underline;
    leaf.maxWidth = 300.0;
    map->SetLevelStyle(2, leaf);
    map->SetLevelStyle(3, leaf);

    // A legend, parked off to the side as a floating topic.
    std::string legend = map->AddFloatingTopic(Tr("Mind mapping · brainstorming"), 620.0, -300.0);
    MindMapTopicStyle legendStyle = map->GetLevelStyle(1);
    legendStyle.fillColor = Color(232, 245, 233, 255);
    legendStyle.borderColor = Color(120, 180, 130, 255);
    legendStyle.textColor = Color(45, 90, 55, 255);
    map->SetTopicStyle(legend, legendStyle);

    AttachStatus(map, status);
    AddMap(tab, map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 — Radial map (reference image 10)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeRadialTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = MakeMapTab("MindMapRadialTab", "MindMapRadialDesc",
            Tr("Radial structure: branches distributed over a full circle, one ring per "
            "generation, each wedge sized by its subtree's leaf count. Circular nodes "
            "with straight spokes and dot terminators."));

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapRadial", 0, 0, 990, 654);
    map->SetTheme(MindMapTheme::Colorful);
    map->SetStructure(MindMapStructure::Radial);
    map->SetConnectorStyle(MindMapConnectorStyle::Straight);
    map->Style().endDecoration = MindMapEndDecoration::Dot;
    map->Style().connectorColorMode = MindMapConnectorColorMode::InheritFromBranch;
    map->SetControlsVisible(true);

    map->BuildFromOutline({
        {0, Tr("Mind Map Diagrams")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
        {1, Tr("Simple Text")}, {2, Tr("Detail")},
    });

    // Uniform circles: a min size larger than any label makes every node match.
    for (int level = 0; level <= 2; ++level) {
        MindMapTopicStyle levelStyle = map->GetLevelStyle(level);
        levelStyle.shape = MindMapNodeShape::Circle;
        levelStyle.minWidth = (level == 0) ? 150.0 : 96.0;
        levelStyle.minHeight = (level == 0) ? 150.0 : 96.0;
        levelStyle.maxWidth = 110.0;
        map->SetLevelStyle(level, levelStyle);
    }

    MindMapLayoutOptions options = map->GetLayoutOptions();
    options.radialRingGap = 190.0;
    map->SetLayoutOptions(options);

    AttachStatus(map, status);
    AddMap(tab, map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 4 — Cross-branch relationships (reference image 6)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeRelationshipsTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = MakeMapTab("MindMapRelTab", "MindMapRelDesc",
            Tr("Non-hierarchical relationships: dashed labelled arcs linking topics that "
            "belong to different branches, drawn independently of the branch strokes. "
            "Rounded-elbow connectors."));

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapRel", 0, 0, 990, 654);
    map->SetTheme(MindMapTheme::Pastel);
    map->SetStructure(MindMapStructure::Balanced);
    map->SetConnectorStyle(MindMapConnectorStyle::RoundedElbow);
    map->SetControlsVisible(true);

    std::string root = map->SetCentralTopic(Tr("Mind Map"));
    struct Branch { std::string label; std::string leaves[3]; };
    const Branch branches[] = {
        {Tr("Subtopic A"), {Tr("Item A1"), Tr("Item A2"), Tr("Item A3")}},
        {Tr("Subtopic B"), {Tr("Item B1"), Tr("Item B2"), Tr("Item B3")}},
        {Tr("Subtopic C"), {Tr("Item C1"), Tr("Item C2"), Tr("Item C3")}},
        {Tr("Subtopic D"), {Tr("Item D1"), Tr("Item D2"), Tr("Item D3")}},
    };

    std::vector<std::vector<std::string>> leafIds;
    for (const auto& branch : branches) {
        std::string branchId = map->AddTopic(root, branch.label);
        std::vector<std::string> ids;
        for (const std::string& leaf : branch.leaves) {
            ids.push_back(map->AddTopic(branchId, leaf));
        }
        leafIds.push_back(ids);
    }

    // The two cross-branch "Connection" arcs from the reference design.
    if (leafIds.size() >= 4) {
        map->AddRelationship("connection_1", leafIds[0][2], leafIds[2][0], Tr("Connection"));
        map->AddRelationship("connection_2", leafIds[1][2], leafIds[3][0], Tr("Connection"));
    }

    AttachStatus(map, status);
    AddMap(tab, map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 5 — Themes and structures gallery
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeGalleryTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = MakeMapTab("MindMapGalleryTab", "MindMapGalleryDesc",
            Tr("The same map under every theme, structure and connector style. "
            "Use the buttons to switch; the layout is recomputed and re-fitted each time."));

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapGallery", 0, 0, 990, 592);
    map->SetControlsVisible(true);
    map->BuildFromOutline({
        {0, Tr("Product launch")},
        {1, Tr("Research")},
        {2, Tr("User interviews")},
        {2, Tr("Competitor scan")},
        {1, Tr("Design")},
        {2, Tr("Wireframes")},
        {2, Tr("Prototype")},
        {1, Tr("Build")},
        {2, Tr("Backend")},
        {2, Tr("Frontend")},
        {1, Tr("Launch")},
        {2, Tr("Marketing")},
        {2, Tr("Support")},
    });
    AttachStatus(map, status);

    struct ThemeButton { const char* key; std::string label; MindMapTheme theme; };
    const ThemeButton themes[] = {
        {"Default", Tr("Default"),      MindMapTheme::Default},
        {"Professional", Tr("Professional"), MindMapTheme::Professional},
        {"Colorful", Tr("Colorful"),     MindMapTheme::Colorful},
        {"Pastel", Tr("Pastel"),       MindMapTheme::Pastel},
        {"Dark", Tr("Dark"),         MindMapTheme::Dark},
        {"Hand drawn", Tr("Hand drawn"),   MindMapTheme::HandDrawn},
    };
    auto themeRow = DemoKit::AddRow(tab, "MindMapThemeRow", 4);
    for (const auto& entry : themes) {
        auto button = MakeGalleryButton(std::string("MindMapTheme_") + entry.key, entry.label, 108);
        MindMapTheme value = entry.theme;
        std::string label = entry.label;
        button->onClick = [map, status, value, label]() {
            map->SetTheme(value);
            map->FitView();
            status->SetText(TrF("Theme: {0}", label));
        };
        DemoKit::Add(themeRow, button);
    }

    struct StructureButton { const char* key; std::string label; MindMapStructure structure; };
    const StructureButton structures[] = {
        {"Balanced", Tr("Balanced"),    MindMapStructure::Balanced},
        {"Logic right", Tr("Logic right"), MindMapStructure::LogicRight},
        {"Logic left", Tr("Logic left"),  MindMapStructure::LogicLeft},
        {"Org chart", Tr("Org chart"),   MindMapStructure::OrgChartDown},
        {"Radial", Tr("Radial"),      MindMapStructure::Radial},
    };
    // Structures and connectors share a line while it is wide enough.
    auto styleRow = DemoKit::AddRow(tab, "MindMapStyleRow", 24);
    auto structureRow = DemoKit::CreateRow("MindMapStructRow", 4);
    DemoKit::Add(styleRow, structureRow);
    for (const auto& entry : structures) {
        auto button = MakeGalleryButton(std::string("MindMapStruct_") + entry.key, entry.label, 108);
        MindMapStructure value = entry.structure;
        std::string label = entry.label;
        button->onClick = [map, status, value, label]() {
            map->SetStructure(value);
            map->FitView();
            status->SetText(TrF("Structure: {0}", label));
        };
        DemoKit::Add(structureRow, button);
    }

    struct ConnectorButton { const char* key; std::string label; MindMapConnectorStyle connector; };
    const ConnectorButton connectors[] = {
        {"Curve", Tr("Curve"),    MindMapConnectorStyle::Curve},
        {"Straight", Tr("Straight"), MindMapConnectorStyle::Straight},
        {"Elbow", Tr("Elbow"),    MindMapConnectorStyle::Elbow},
        {"Rounded", Tr("Rounded"),  MindMapConnectorStyle::RoundedElbow},
        {"Tapered", Tr("Tapered"),  MindMapConnectorStyle::TaperedBranch},
    };
    auto connectorRow = DemoKit::CreateRow("MindMapConnRow", 4);
    DemoKit::Add(styleRow, connectorRow);
    for (const auto& entry : connectors) {
        auto button = MakeGalleryButton(std::string("MindMapConn_") + entry.key, entry.label, 82);
        MindMapConnectorStyle value = entry.connector;
        std::string label = entry.label;
        button->onClick = [map, status, value, label]() {
            map->SetConnectorStyle(value);
            status->SetText(TrF("Connector: {0}", label));
        };
        DemoKit::Add(connectorRow, button);
    }

    AddMap(tab, map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 6 — Presentation features (reference images 2, 3, 5 and 9)
// Aligned columns and rows, connector badges, markers, notes, links,
// shadows and a background layer.
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakePresentationTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = MakeMapTab("MindMapPresTab", "MindMapPresDesc",
            Tr("Infographic layout: level columns and side rows aligned, numbered badges "
            "on the connectors, drop shadows, a dotted background layer, and note / "
            "link / marker indicators. Click a link glyph to fire the callback."));

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapPres", 0, 0, 990, 654);
    map->SetTheme(MindMapTheme::Colorful);
    map->SetStructure(MindMapStructure::Balanced);
    map->SetBalancePolicy(MindMapBalancePolicy::SplitInHalf);
    map->SetNumbering(MindMapNumbering::Prefix);
    map->SetControlsVisible(true);

    std::string root = map->SetCentralTopic(Tr("MIND MAP"));
    const std::string titles[] = {Tr("TITLE 01"), Tr("TITLE 02"), Tr("TITLE 03"),
                                  Tr("TITLE 04"), Tr("TITLE 05"), Tr("TITLE 06")};
    for (const std::string& title : titles) {
        std::string branch = map->AddTopic(root, title);
        map->AddTopic(branch, "Lorem ipsum dolor sit amet, consectetur adipiscing elit.");
    }

    // Aligned columns and rows are what give the infographic its grid feel.
    MindMapLayoutOptions options = map->GetLayoutOptions();
    options.alignLevelColumns = true;
    options.alignSideRows = true;
    options.siblingGap = 22.0;
    map->SetLayoutOptions(options);

    // Pill main topics, plain text leaves.
    MindMapTopicStyle main = map->GetLevelStyle(1);
    main.shape = MindMapNodeShape::Pill;
    main.minWidth = 130.0;
    map->SetLevelStyle(1, main);

    MindMapTopicStyle leaf = map->GetLevelStyle(2);
    leaf.shape = MindMapNodeShape::NoShape;
    leaf.maxWidth = 210.0;
    leaf.textColor = Color(90, 90, 100, 255);
    map->SetLevelStyle(2, leaf);

    MindMapStyle& s = map->Style();
    s.showConnectorBadges = true;
    s.connectorBadgeT = 0.55;
    s.showShadows = true;
    s.dimUnrelatedOnHover = true;
    // A faint dot grid behind the map (S9).
    s.backgroundRenderer = [](IRenderContext* ctx, const Rect2Dd& visible) {
        const double spacing = 40.0;
        ctx->SetFillPaint(Color(228, 228, 236, 255));
        double startX = std::floor(visible.x / spacing) * spacing;
        double startY = std::floor(visible.y / spacing) * spacing;
        for (double x = startX; x <= visible.x + visible.width; x += spacing) {
            for (double y = startY; y <= visible.y + visible.height; y += spacing) {
                ctx->FillCircle({x, y}, 1.6);
            }
        }
    };

    // Indicators on the first branch.
    const MindMapTopic* rootTopic = map->GetTopic(root);
    if (rootTopic && rootTopic->childIds.size() >= 3) {
        map->SetTopicNote(rootTopic->childIds[0], Tr("A longer note attached to this topic."));
        map->SetTopicLink(rootTopic->childIds[1], "https://example.com");
        map->SetTopicMarkers(rootTopic->childIds[2],
                             {"media/icons/check.png", "media/icons/clock-five.svg"});
    }

    // Callout cards with leader lines, and a label on one branch connector.
    if (rootTopic && !rootTopic->childIds.empty()) {
        map->AddCallout(rootTopic->childIds[0],
                        Tr("A callout card, tied back to its topic by a leader line."),
                        -520.0, -260.0);
        map->SetTopicConnectorLabel(rootTopic->childIds[0], Tr("primary"));
    }

    // Re-layout tweens instead of snapping, so collapse and structure changes
    // are followable.
    s.layoutAnimationMs = 220.0;

    map->onTopicLinkActivated = [status](const std::string&, const std::string& link) {
        status->SetText(TrF("Link activated: {0}", link));
    };

    AttachStatus(map, status);
    AddMap(tab, map);
    return tab;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ENTRY POINT
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateMindMapExamples() {
    auto main = DemoKit::CreatePage("MindMapExamples", Tr("Mind Map"),
            Tr("Rooted topic tree on a pan/zoom canvas. Balanced, logic, org chart and "
            "radial structures; per-branch colour cascade; collapse, in-place "
            "editing and drag-to-reparent."));
    // Every map pans and zooms itself: the page is the area's height and the
    // tabs take what the texts leave.
    main->SetFillsArea(true);

    auto card = DemoKit::CreateCard("MindMapCard");
    card->SetPadding(4);
    DemoKit::Add(main, card, 1);
    card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

    // The read-out every tab reports to, above the tabs.
    auto statusLabel = DemoKit::CreateText("MindMapStatus", Tr("Click a topic to inspect it."));
    statusLabel->SetFontSize(10);
    statusLabel->SetBackgroundColor(Color(245, 245, 245));
    statusLabel->SetBorders(1.0f);
    statusLabel->SetPadding(6.0f);
    statusLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    DemoKit::Add(card, statusLabel);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("MindMapTabs");
    tabs->SetTabPosition(TabPosition::Top);
    tabs->SetTabStyle(TabStyle::Modern);
    tabs->SetTabMaxWidth(300);

    tabs->AddTab(Tr("Editable map"),        MakeEditableTab(statusLabel));
    tabs->AddTab(Tr("Logic chart"),         MakeLogicChartTab(statusLabel));
    tabs->AddTab(Tr("Radial"),              MakeRadialTab(statusLabel));
    tabs->AddTab(Tr("Relationships"),       MakeRelationshipsTab(statusLabel));
    tabs->AddTab(Tr("Presentation"),        MakePresentationTab(statusLabel));
    tabs->AddTab(Tr("Themes & structures"), MakeGalleryTab(statusLabel));

    DemoKit::Add(card, tabs, 1);
    tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);
    return main;
}

} // namespace UltraCanvas
