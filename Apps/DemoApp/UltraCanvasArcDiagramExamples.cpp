// Apps/DemoApp/UltraCanvasArcDiagramExamples.cpp
// Demo examples for UltraCanvasArcDiagram — arc diagram
// Version: 1.0.1
// Last Modified: 2026-05-14
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasArcDiagram.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include <sstream>
#include <cmath>

namespace UltraCanvas {

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 — Character co-occurrence  (image 1 — Friends network)
// Many nodes, degree-based sizing, vertical labels, semicircle arcs
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeFriendsTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("ArcFriendsTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ArcFriendsDesc", 10, 8, 900, 22);
        desc->SetText("Character co-occurrence: node size = degree, labels vertical, semicircle arcs. Click a node to see connections.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasArcDiagram>(
                "ArcFriends", 10, 34, 990, 654);

        // Characters ordered by importance (from image 1)
        struct Char { const char* id; const char* label; Color color; };
        Char chars[] = {
                {"monica",  "Monica",         Color(100,160,220,255)},
                {"rachel",  "Rachel",         Color(100,160,220,255)},
                {"chandler","Chandler",       Color( 90,120,200,255)},
                {"ross",    "Ross",           Color(120, 80,200,255)},
                {"joey",    "Joey",           Color(180,120,200,255)},
                {"phoebe",  "Phoebe",         Color(220,120,200,255)},
                {"paul",    "Paul the wine guy",Color(190,180,200,200)},
                {"mrsgel",  "Mrs Geller",     Color(200,180,180,200)},
                {"mrgel",   "Mr Geller",      Color(200,180,180,200)},
                {"chip",    "Chip",           Color(200,200,180,200)},
                {"pete",    "Pete",           Color(200,200,180,200)},
                {"emily",   "Emily",          Color(200,200,180,200)},
                {"timothy", "Timothy (Burke)",Color(200,180,200,200)},
                {"droger",  "Dr. Roger",      Color(180,200,200,200)},
                {"tag",     "Tag",            Color(180,200,200,200)},
                {"melissa", "Melissa",        Color(200,180,180,200)},
                {"gavin",   "Gavin",          Color(200,180,180,200)},
                {"janice",  "Janice",         Color(220,180,180,200)},
                {"joanna",  "Joanna",         Color(200,180,200,200)},
                {"kathy",   "Kathy",          Color(180,180,220,200)},
                {"mrbing",  "Mr Bing",        Color(180,180,220,200)},
                {"carol",   "Carol",          Color(180,220,180,200)},
                {"ben",     "Ben",            Color(180,220,180,200)},
                {"jill",    "Jill",           Color(200,210,180,200)},
                {"elizabeth","Elizabeth",     Color(200,200,200,200)},
                {"mona",    "Mona",           Color(210,190,190,200)},
                {"emma",    "Emma",           Color(190,210,190,200)},
                {"charlie", "Charlie",        Color(190,190,210,200)},
                {"janine",  "Janine",         Color(210,190,210,200)},
                {"erin",    "Erin",           Color(190,210,210,200)},
                {"alice",   "Alice",          Color(210,200,180,200)},
        };

        for (const auto& c : chars) {
            ArcNode n;
            n.id    = c.id;
            n.label = c.label;
            n.color = c.color;
            n.size  = 8.0f;
            diagram->AddNode(n);
        }

        // Core connections (main characters to each other — high weight)
        struct Edge { const char* a; const char* b; float w; };
        Edge edges[] = {
                {"monica","rachel",  8},{"monica","chandler",9},{"monica","ross",   8},
                {"monica","joey",    7},{"monica","phoebe",   7},
                {"rachel","ross",    9},{"rachel","chandler", 7},{"rachel","joey",   8},
                {"rachel","phoebe",  7},{"chandler","joey",   9},{"chandler","ross", 7},
                {"ross",  "joey",    7},{"ross",   "phoebe",  6},
                {"joey",  "phoebe",  6},
                // Secondary
                {"monica","paul",    3},{"rachel","paul",     2},
                {"monica","mrsgel",  4},{"ross","mrsgel",     4},{"chandler","mrsgel",3},
                {"monica","mrgel",   4},{"ross","mrgel",      4},
                {"rachel","chip",    3},{"monica","chip",     2},
                {"monica","pete",    3},{"rachel","emily",    2},{"ross","emily",    4},
                {"rachel","timothy", 2},{"phoebe","droger",   3},
                {"rachel","tag",     4},{"rachel","melissa",  2},
                {"ross",  "mona",    3},{"joey",  "janine",  3},
                {"rachel","gavin",   3},{"chandler","kathy",  4},
                {"ross",  "janice",  2},{"chandler","janice", 5},{"monica","janice", 2},
                {"ross",  "carol",   5},{"ross","ben",        4},
                {"rachel","joanna",  3},{"joey","mrbing",     2},
                {"ross",  "charlie", 3},{"joey","charlie",    2},
                {"phoebe","alice",   2},{"joey","erin",       2},
        };

        for (const auto& e : edges) {
            ArcEdge ae;
            ae.sourceId = e.a;
            ae.targetId = e.b;
            ae.weight   = e.w;
            ae.color    = Color(160,160,160,static_cast<uint8_t>(60 + e.w * 15));
            ae.side     = ArcEdgeType::Above;
            diagram->AddEdge(ae);
        }

        ArcDiagramStyle s;
        s.nodeSizeMode      = ArcNodeSizeMode::ByDegree;
        s.nodeBaseSize      = 5.0f;
        s.arcHeightMode     = ArcHeightMode::Semicircle;
        s.arcEncodeOpacity  = true;
        s.arcMinOpacity     = 30;
        s.arcMaxOpacity     = 180;
        s.arcMinWidth       = 0.5f;
        s.arcMaxWidth       = 3.5f;
        s.labelSide         = ArcLabelSide::Vertical;
        s.labelFontSize     = 10.0f;
        s.nodePadding       = 26.0f;
        s.marginH           = 60.0f;
        s.marginV           = 120.0f;
        s.sortBySpan        = true;
        diagram->SetStyle(s);

        diagram->onNodeClick = [statusLabel](int, const ArcNode& n) {
            statusLabel->SetText("Character: " + n.label);
        };
        diagram->onEdgeClick = [statusLabel](int, const ArcEdge& e) {
            std::ostringstream ss;
            ss << e.sourceId << " — " << e.targetId << "  scenes: " << static_cast<int>(e.weight);
            statusLabel->SetText(ss.str());
        };

        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 — Canonical reference  (image 2)
// Classic arc diagram explanation: color-coded, weighted arcs
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeCanonicalTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("ArcCanonTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ArcCanonDesc", 10, 8, 900, 22);
        desc->SetText("Canonical arc diagram: arcs colored by category, width encodes strength. Perfect semicircle geometry.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasArcDiagram>(
                "ArcCanon", 10, 34, 990, 654);

        const char* nodes[] = {"A","B","C","D","E","F","G","H"};
        Color nodeCol(80, 120, 200, 240);
        for (const char* n : nodes) {
            ArcNode an;
            an.id = n; an.label = n;
            an.color = nodeCol; an.size = 10.0f;
            diagram->AddNode(an);
        }

        // Red arcs — strong, long-range (image 2: red = category 1)
        Color red  (210, 50,  40, 200);
        Color orange(220,150,  50, 180);
        Color blue (  60,120, 200, 200);

        struct CatEdge { const char* a; const char* b; float w; Color col; };
        CatEdge catEdges[] = {
                {"A","H", 4.0f, red},    // long-range, thick
                {"B","G", 2.5f, red},
                {"A","D", 2.0f, orange}, // medium
                {"B","E", 1.5f, orange},
                {"C","D", 2.5f, blue},   // short, thick (image 2: blue = category 3)
                {"E","F", 2.0f, blue},
                {"F","H", 1.0f, orange},
                {"G","H", 1.5f, blue},
        };

        for (const auto& e : catEdges) {
            ArcEdge ae;
            ae.sourceId = e.a; ae.targetId = e.b;
            ae.weight   = e.w; ae.color    = e.col;
            ae.side     = ArcEdgeType::Above;
            diagram->AddEdge(ae);
        }

        ArcDiagramStyle s;
        s.arcHeightMode  = ArcHeightMode::Semicircle;
        s.arcMinWidth    = 1.0f;
        s.arcMaxWidth    = 6.0f;
        s.labelSide      = ArcLabelSide::Below;
        s.labelFontSize  = 13.0f;
        s.nodeBaseSize   = 10.0f;
        s.nodePadding    = 90.0f;
        s.marginH        = 80.0f;
        s.marginV        = 60.0f;
        s.showAxisArrow  = true;
        s.showLegend     = true;
        s.legendEntries  = {{"Category 1", red}, {"Category 2", orange}, {"Category 3", blue}};
        s.legendX        = 20.0f;
        s.legendY        = 30.0f;
        diagram->SetStyle(s);

        diagram->onEdgeClick = [statusLabel](int, const ArcEdge& e) {
            std::ostringstream ss;
            ss << e.sourceId << " → " << e.targetId << "  weight: " << e.weight;
            statusLabel->SetText(ss.str());
        };

        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 — Directed flow  (image 3 — Navy website directed arcs)
// Mid-arc arrowheads, weight labels at apex, self-loops, below arcs
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeDirectedTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("ArcDirTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ArcDirDesc", 10, 8, 900, 22);
        desc->SetText("Directed flow: arrowheads at arc midpoint, weight labels, self-loops, above/below encoding direction.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasArcDiagram>(
                "ArcDir", 10, 34, 990, 654);

        struct FlowNode { const char* id; const char* label; float size; Color color; };
        FlowNode fnodes[] = {
                {"home",    "Home Page",      20.0f, Color( 80,140,210,230)},
                {"fleet",   "The Fleet",      28.0f, Color( 30, 30, 30,240)},
                {"special", "Special Forces", 22.0f, Color(180, 50, 50,230)},
                {"train",   "Training",        8.0f, Color(180, 80, 80,200)},
                {"weNavy",  "We Are Navy",     8.0f, Color( 60, 60, 60,200)},
                {"marco",   "San Marco Briga.",18.0f, Color( 40,120, 40,230)},
        };
        for (const auto& fn : fnodes) {
            ArcNode an;
            an.id = fn.id; an.label = fn.label;
            an.color = fn.color; an.size = fn.size;
            diagram->AddNode(an);
        }

        Color green(80, 180, 80, 200);
        Color gray (150, 150, 150, 180);
        Color violet(160, 80, 200, 180);

        // Green: incoming flows (above)
        auto makeEdge = [](const char* s, const char* t, float w,
                           Color c, bool dir, ArcEdgeType side,
                           const std::string& lbl = "") {
            ArcEdge e;
            e.sourceId = s; e.targetId = t;
            e.weight = w; e.color = c;
            e.directed = dir; e.side = side;
            e.label = lbl;
            return e;
        };

        diagram->AddEdge(makeEdge("home",  "fleet",   10.0f, green, true,  ArcEdgeType::Above, "10"));
        diagram->AddEdge(makeEdge("fleet", "special", 13.0f, green, true,  ArcEdgeType::Above, "13"));
        diagram->AddEdge(makeEdge("home",  "marco",   10.0f, green, true,  ArcEdgeType::Above, "10"));
        diagram->AddEdge(makeEdge("fleet", "train",    5.0f, gray,  true,  ArcEdgeType::Above, "5"));
        diagram->AddEdge(makeEdge("fleet", "weNavy",   5.0f, gray,  true,  ArcEdgeType::Above, "5"));
        diagram->AddEdge(makeEdge("fleet", "marco",    5.0f, gray,  true,  ArcEdgeType::Above, "5"));

        // Self-loop (below)
        ArcEdge selfLoop = makeEdge("special","special",3.0f, violet, true, ArcEdgeType::Below, "3");
        diagram->AddEdge(selfLoop);

        ArcDiagramStyle s;
        s.arcHeightMode     = ArcHeightMode::Proportional;
        s.arcHeightScale    = 0.38f;
        s.arcMinWidth       = 1.5f;
        s.arcMaxWidth       = 7.0f;
        s.showArrows        = true;
        s.arrowPosition     = ArcArrowPosition::AtMidArc;
        s.arrowSize         = 9.0f;
        s.labelSide         = ArcLabelSide::Below;
        s.labelFontSize     = 11.0f;
        s.arcLabelFontSize  = 11.0f;
        s.nodeSizeMode      = ArcNodeSizeMode::Fixed;
        s.nodePadding       = 130.0f;
        s.marginH           = 80.0f;
        s.marginV           = 60.0f;
        diagram->SetStyle(s);

        diagram->onEdgeClick = [statusLabel](int, const ArcEdge& e) {
            std::ostringstream ss;
            ss << e.sourceId << " → " << e.targetId << "  flow: " << static_cast<int>(e.weight);
            statusLabel->SetText(ss.str());
        };

        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 4 — Genomics / RNA base pairs  (image 4)
// Parallel bundles above AND below, color legend for P-value ranges
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeGenomicsTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("ArcGenomTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ArcGenomDesc", 10, 8, 900, 22);
        desc->SetText("RNA base pairs: known (above) and novel (below) connections, colored by statistical significance (P-value).");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasArcDiagram>(
                "ArcGenom", 10, 34, 990, 654);

        // 20 genomic positions as nodes
        for (int i = 1; i <= 20; ++i) {
            ArcNode n;
            char id[8]; snprintf(id, sizeof(id), "p%d", i);
            n.id    = id;
            n.label = std::to_string(i);
            n.color = Color(100, 100, 100, 220);
            n.size  = 5.0f;
            diagram->AddNode(n);
        }

        // P-value color bands
        Color darkBlue (  8,  48, 107, 220);  // [0.1e-06]    strongest
        Color lightBlue(100, 160, 210, 180);  // (1e-06,1e-05]
        Color amber    (220, 160,  40, 180);  // (1e-05,0.0001]
        Color red      (200,  50,  40, 180);  // (0.0001,0.001]

        // Known pairs — above baseline
        struct Pair { int a; int b; Color c; };
        Pair knownPairs[] = {
                {1,18, darkBlue},{2,17, darkBlue},{3,16, darkBlue},{4,15, lightBlue},
                {5,14, lightBlue},{6,13, amber},{7,12, amber},{8,11, red},
                {9,10, red},{1,12, lightBlue},{3,14, amber},{5,16, darkBlue},
                {2,19, lightBlue},{4,18, amber},{6,15, red},
        };
        for (const auto& p : knownPairs) {
            char idA[8], idB[8];
            snprintf(idA, sizeof(idA), "p%d", p.a);
            snprintf(idB, sizeof(idB), "p%d", p.b);
            ArcEdge e;
            e.sourceId = idA; e.targetId = idB;
            e.weight = 1.0f; e.color = p.c;
            e.side = ArcEdgeType::Above;
            diagram->AddEdge(e);
        }

        // Novel pairs — below baseline
        Pair novelPairs[] = {
                {1,10, red},{2,11, red},{3,12, amber},{4,13, amber},
                {5,14, lightBlue},{6,15, lightBlue},{7,16, darkBlue},
                {8,17, darkBlue},{9,18, darkBlue},{10,19, lightBlue},
                {11,20, amber},{1,8,  red},{3,10, red},{5,12, amber},
        };
        for (const auto& p : novelPairs) {
            char idA[8], idB[8];
            snprintf(idA, sizeof(idA), "p%d", p.a);
            snprintf(idB, sizeof(idB), "p%d", p.b);
            ArcEdge e;
            e.sourceId = idA; e.targetId = idB;
            e.weight = 1.0f; e.color = p.c;
            e.side = ArcEdgeType::Below;
            diagram->AddEdge(e);
        }

        ArcDiagramStyle s;
        s.arcHeightMode    = ArcHeightMode::Semicircle;
        s.arcMinWidth      = 0.8f;
        s.arcMaxWidth      = 2.0f;
        s.bundleHeightStep = 6.0f;
        s.sortBySpan       = true;
        s.labelSide        = ArcLabelSide::Below;
        s.labelFontSize    = 10.0f;
        s.nodeBaseSize     = 5.0f;
        s.nodePadding      = 30.0f;
        s.marginH          = 40.0f;
        s.marginV          = 50.0f;
        s.showLegend       = true;
        s.legendEntries    = {
                {"Known: [0.1e-06]",      darkBlue},
                {"Known: (1e-06,1e-05]",  lightBlue},
                {"Known: (1e-05,0.0001]", amber},
                {"Known: (0.0001,0.001]", red},
        };
        s.legendX = 20.0f;
        s.legendY = 10.0f;
        diagram->SetStyle(s);

        diagram->onEdgeClick = [statusLabel](int, const ArcEdge& e) {
            std::string side = (e.side == ArcEdgeType::Above) ? "known" : "novel";
            statusLabel->SetText(e.sourceId + " ↔ " + e.targetId + "  (" + side + " base pair)");
        };

        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// ENTRY POINT
// ─────────────────────────────────────────────────────────────────────────────
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateArcDiagramExamples()
    {
        auto main = std::make_shared<UltraCanvasContainer>(
                "ArcDiagramExamples", 0, 0, 1030, 800);

        auto title = std::make_shared<UltraCanvasLabel>("ArcTitle", 20, 10, 600, 32);
        title->SetText("Arc Diagram");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(80, 50, 140));
        main->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("ArcSub", 20, 44, 900, 20);
        subtitle->SetText("Nodes on a baseline, edges as cubic Bezier arcs. Above = forward, below = back or novel. Width + opacity encode weight.");
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(90, 90, 90));
        main->AddChild(subtitle);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("ArcStatus", 680, 10, 340, 56);
        statusLabel->SetText("Click a node or arc to inspect.");
        statusLabel->SetFontSize(10);
        statusLabel->SetBackgroundColor(Color(245, 245, 245));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(6.0f);
        statusLabel->SetAlignment(TextAlignment::Center);
        main->AddChild(statusLabel);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
                "ArcTabs", 10, 72, 1010, 728);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);

        tabs->AddTab("Character co-occurrence",   MakeFriendsTab(statusLabel));
        tabs->AddTab("Canonical reference",        MakeCanonicalTab(statusLabel));
        tabs->AddTab("Directed flow (Navy)",       MakeDirectedTab(statusLabel));
        tabs->AddTab("RNA base pairs (genomics)",  MakeGenomicsTab(statusLabel));

        main->AddChild(tabs);
        return main;
    }

} // namespace UltraCanvas