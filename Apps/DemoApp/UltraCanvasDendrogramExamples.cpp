// Apps/DemoApp/UltraCanvasDendrogramExamples.cpp
// Demo — UltraCanvasDendrogram showcase, 6 styles
// Version: 1.4.0
// Last Modified: 2026-04-07
// Author: UltraCanvas Framework
//
// Controls:
//   Scroll wheel       — zoom in/out toward cursor
//   Middle-drag / Alt+Left-drag — pan
//   Left-click node    — select
//   Shift/Ctrl+click   — multi-select
//   Double-click node  — collapse / expand subtree
//   Ctrl+drag node     — move node manually

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasDendrogram.h"
#include "Plugins/Diagrams/UltraCanvasDendrogramLayout.h"
#include <unordered_map>
#include <string>
#include <vector>

using namespace UltraCanvas;

// =============================================================================
// TREE BUILDER
// =============================================================================

struct NodeSpec {
    std::string id, label, parentId, groupId;
    double mergeDistance = 0.0;
    float  confidence    = 1.0f;
    float  branchValue   = -1.0f;
};

static std::shared_ptr<DendrogramDataVector> BuildTree(
    const std::vector<NodeSpec>& specs,
    const std::vector<DendrogramGroup>& groups = {})
{
    std::unordered_map<std::string, std::vector<std::string>> childMap;
    std::string rootId;
    for (const auto& s : specs) {
        if (s.parentId.empty()) rootId = s.id;
        else childMap[s.parentId].push_back(s.id);
    }
    auto data = std::make_shared<DendrogramDataVector>();
    for (const auto& s : specs) {
        DendrogramNode n;
        n.id            = s.id;
        n.label         = s.label;
        n.mergeDistance = s.mergeDistance;
        n.groupId       = s.groupId;
        n.confidence    = s.confidence;
        n.branchValue   = s.branchValue;
        n.tooltip = s.label;
        if (s.mergeDistance > 0.0)
            n.tooltip += "\nMerge dist: " + std::to_string(s.mergeDistance).substr(0,5);
        else if (s.parentId.empty())
            n.tooltip += " (root)";
        else
            n.tooltip += " (leaf)";
        auto it = childMap.find(s.id);
        if (it != childMap.end()) n.childIds = it->second;
        data->AddNode(n);
    }
    data->SetRootId(rootId);
    for (const auto& g : groups) data->AddGroup(g);
    return data;
}

// =============================================================================
// CONTROL BAR  (Reset / +zoom / -zoom / Expand / Collapse)
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeControlBar(
    long& uid, int w, std::shared_ptr<UltraCanvasDendrogram> d, const std::string& info)
{
    auto bar = std::make_shared<UltraCanvasContainer>("ctrl_"+d->GetIdentifier(), 0, 0, w, 36);
    bar->SetBackgroundColor(Color(247,247,250,255));

    auto mkBtn = [&](const std::string& sfx, int x, int bw, const std::string& lbl,
                     std::function<void()> cb) {
        auto btn = std::make_shared<UltraCanvasButton>(sfx+"_"+d->GetIdentifier(), x, 4, bw, 28, lbl);
        btn->onClick = std::move(cb);
        bar->AddChild(btn);
    };

    mkBtn("rst",   4,  88, "Reset View",  [d]{ d->ResetView(); });
    mkBtn("zi",   96,  36, "+",           [d]{ d->SetZoom(d->GetZoom()*1.3f); });
    mkBtn("zo",  136,  36, "−",           [d]{ d->SetZoom(d->GetZoom()/1.3f); });
    mkBtn("exp", 178,  90, "Expand All",  [d]{ d->ExpandAll(); });
    mkBtn("col", 272,  98, "Collapse All",[d]{ d->CollapseAll(); });
    mkBtn("rpos",374, 120, "Reset Pos",    [d]{ d->ResetNodePositions(); });

    auto lbl = std::make_shared<UltraCanvasLabel>("inf_"+d->GetIdentifier(),
                                                   504, 10, w-508, 20, info);
    lbl->SetTextColor(Color(100,100,110,255));
    lbl->SetFontSize(11.0f);
    bar->AddChild(lbl);
    return bar;
}

// =============================================================================
// DEMO 1 — Top-Down hierarchical clustering
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeDemo1(long& uid, int w, int h)
{
    std::vector<NodeSpec> specs = {
        {"root",  "All Species",    "",     "",       1.00},
        {"cA",    "Mammalia",       "root", "",       0.85},
        {"cA1",   "Primates",       "cA",   "grpA",  0.55},
        {"A1a",   "Homo sapiens",   "cA1",  "grpA",  0.0, 0.98f},
        {"A1b",   "Pan troglodytes","cA1",  "grpA",  0.0, 0.96f},
        {"A1c",   "Gorilla",        "cA1",  "grpA",  0.0, 0.94f},
        {"A1d",   "Macaca",         "cA1",  "grpA",  0.0, 0.90f},
        {"cA2",   "Rodentia",       "cA",   "grpA2", 0.45},
        {"A2a",   "Mus musculus",   "cA2",  "grpA2", 0.0, 0.95f},
        {"A2b",   "Rattus",         "cA2",  "grpA2", 0.0, 0.93f},
        {"A2c",   "Cavia porcellus","cA2",  "grpA2", 0.0, 0.88f},
        {"cA3",   "Carnivora",      "cA",   "grpA3", 0.42},
        {"A3a",   "Felis catus",    "cA3",  "grpA3", 0.0, 0.90f},
        {"A3b",   "Canis lupus",    "cA3",  "grpA3", 0.0, 0.88f},
        {"cB",    "Aves",           "root", "",       0.78},
        {"cB1",   "Passeriformes",  "cB",   "grpB",  0.50},
        {"B1a",   "Passer domestic","cB1",  "grpB",  0.0, 0.95f},
        {"B1b",   "Turdus merula",  "cB1",  "grpB",  0.0, 0.92f},
        {"B1c",   "Fringilla",      "cB1",  "grpB",  0.0, 0.88f},
        {"cB2",   "Falconiformes",  "cB",   "grpB2", 0.44},
        {"B2a",   "Falco peregrinus","cB2", "grpB2", 0.0, 0.93f},
        {"B2b",   "Aquila chrysaetos","cB2","grpB2", 0.0, 0.90f},
        {"cC",    "Reptilia",       "root", "",       0.70},
        {"cC1",   "Squamata",       "cC",   "grpC",  0.48},
        {"C1a",   "Python regius",  "cC1",  "grpC",  0.0, 0.88f},
        {"C1b",   "Lacerta agilis", "cC1",  "grpC",  0.0, 0.84f},
        {"cC2",   "Crocodylia",     "cC",   "grpC2", 0.40},
        {"C2a",   "Crocodylus",     "cC2",  "grpC2", 0.0, 0.82f},
        {"C2b",   "Alligator",      "cC2",  "grpC2", 0.0, 0.80f},
    };

    std::vector<DendrogramGroup> groups = {
        {"grpA",  "Primates",      {"A1a","A1b","A1c","A1d"},
         Color(30,120,210,255),Color(20,100,190,255),Color(30,120,210,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"grpA2", "Rodentia",      {"A2a","A2b","A2c"},
         Color(80,170,230,255),Color(60,150,210,255),Color(80,170,230,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"grpA3", "Carnivora",     {"A3a","A3b"},
         Color(130,200,240,255),Color(110,180,220,255),Color(130,200,240,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"grpB",  "Passeriformes", {"B1a","B1b","B1c"},
         Color(220,100,40,255), Color(200,80,20,255), Color(220,100,40,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"grpB2", "Falconiformes", {"B2a","B2b"},
         Color(240,160,60,255), Color(220,140,40,255),Color(240,160,60,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"grpC",  "Squamata",      {"C1a","C1b"},
         Color(40,170,90,255),  Color(20,150,70,255), Color(40,170,90,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"grpC2", "Crocodylia",    {"C2a","C2b"},
         Color(90,210,120,255), Color(70,190,100,255),Color(90,210,120,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
    };

    auto data  = BuildTree(specs, groups);
    auto outer = std::make_shared<UltraCanvasContainer>("o1",0,0,w,h);
    auto d     = std::make_shared<UltraCanvasDendrogram>("D1",0,36,w,h-36);
    d->SetDataSource(data);
    d->SetOrientation(DendrogramOrientation::TopDown);
    d->SetScaleMode(DendrogramScaleMode::Proportional);

    DendrogramStyle s;
    s.linkStyle          = DendrogramLinkStyle::Rectangular;
    s.branchColorMode    = BranchColorMode::ByGroup;
    s.defaultBranchColor = Color(80,80,90,255);
    s.defaultBranchWidth = 2.0f;
    s.confidenceMode     = ConfidenceDisplayMode::LineThickness;
    s.branchWidthMin = 1.0f; s.branchWidthMax = 3.5f;
    s.leafLabelFontSize  = 13.0f;  // Bigger — easier to read
    s.colorLeafLabels    = true;
    s.showDistanceAxis   = true;
    s.axisTickCount      = 5;
    s.axisFontSize       = 11.0f;
    s.showGrid           = true;
    s.showGroupLabels    = true;
    s.sidebarBracketWidth = 6;
    s.scaleNodesByDepth  = true;
    s.nodeRadiusDepthScale = 1.2f;
    s.internalNodeRadius = 4.0f;
    s.leafNodeRadius     = 3.0f;
    // Show clade names beside internal node dots, clear of branch lines
    s.showInternalNodeLabels    = true;
    s.showLabelsOnClick         = true;  // Show labels on click to avoid branch overlap
    s.internalNodeLabelFontSize = 11.0f;
    s.internalNodeLabelColor    = Color(50, 50, 60, 255);
    s.internalNodeLabelOffset   = 7;
    // White background behind all labels for readability
    s.showLabelBackground   = true;
    s.labelBackgroundColor  = Color(255, 255, 255, 210);
    s.labelBackgroundBorder = Color(200, 200, 210, 100);
    s.labelBackgroundPadding = 3;
    // Use larger leaf spacing — layout engine clamps to fit anyway
    s.leafSpacing        = 32.0f;
    s.marginTop    = 24;
    s.marginBottom = 100; // Room for 45° rotated labels
    s.marginLeft   = 60;  // Room for distance axis
    s.marginRight  = 24;

    d->SetStyle(s);
    d->onLeafClicked         = [](const std::string& id){ };
    d->onInternalNodeClicked = [](const std::string& id){ };

    outer->AddChild(MakeControlBar(uid,w,d,
        "Top-Down | Proportional | 29 taxa | Colored clades | Double-click = collapse"));
    outer->AddChild(d);
    return outer;
}

// =============================================================================
// DEMO 2 — Left-Right curved
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeDemo2(long& uid, int w, int h)
{
    std::vector<NodeSpec> specs = {
        {"flare",     "flare",              "",           "",  1.00},
        {"animate",   "animate",            "flare",      "",  0.82},
        {"Easing",    "Easing",             "animate",    "",  0.0, 1.00f},
        {"FuncSeq",   "FunctionSequence",   "animate",    "",  0.0, 0.95f},
        {"Parallel",  "Parallel",           "animate",    "",  0.0, 0.90f},
        {"Transition","Transition",         "animate",    "",  0.0, 0.92f},
        {"Tween",     "Tween",              "animate",    "",  0.0, 0.88f},
        {"analytics", "analytics",          "flare",      "",  0.76},
        {"cluster",   "cluster",            "analytics",  "",  0.56},
        {"AgglC",     "AgglomerativeCluster","cluster",   "",  0.0, 0.95f},
        {"CommStr",   "CommunityStructure", "cluster",    "",  0.0, 0.90f},
        {"HierC",     "HierarchicalCluster","cluster",    "",  0.0, 0.88f},
        {"graph",     "graph",              "analytics",  "",  0.52},
        {"BetCent",   "BetweennessCentrality","graph",    "",  0.0, 0.92f},
        {"LinkDist",  "LinkDistortion",     "graph",      "",  0.0, 0.88f},
        {"SpanTree",  "SpanningTree",       "graph",      "",  0.0, 0.85f},
    };

    auto data  = BuildTree(specs,{});
    auto outer = std::make_shared<UltraCanvasContainer>("o2",0,0,w,h);
    auto d     = std::make_shared<UltraCanvasDendrogram>("D2",0,36,w,h-36);
    d->SetDataSource(data);
    d->SetOrientation(DendrogramOrientation::LeftRight);
    d->SetScaleMode(DendrogramScaleMode::Cladogram);

    DendrogramStyle s;
    s.linkStyle          = DendrogramLinkStyle::Curved;
    s.branchColorMode    = BranchColorMode::Uniform;
    s.defaultBranchColor = Color(130,130,140,220);
    s.defaultBranchWidth = 1.0f;
    s.leafLabelFontSize  = 13.0f;
    s.colorLeafLabels    = false;
    s.defaultLeafColor   = Color(35,35,45,255);
    s.showInternalNodes  = true;
    s.scaleNodesByDepth  = true;
    s.nodeRadiusDepthScale = 2.0f;
    s.nodeRadiusMax      = 12.0f;
    s.internalNodeRadius = 3.0f;
    s.internalNodeColor  = Color(40,40,50,255);
    s.leafNodeRadius     = 3.0f;
    s.showDistanceAxis   = false;
    s.showGroupLabels    = false;
    s.leafSpacing        = 35.0f;
    s.marginTop    = 20;
    s.marginBottom = 20;
    s.marginLeft   = 20;
    s.marginRight  = 20;
    d->SetStyle(s);

    outer->AddChild(MakeControlBar(uid,w,d,
        "Left-Right | Cladogram | Curved S-curves | DblClick=collapse | Ctrl+drag=move"));
    outer->AddChild(d);
    return outer;
}

// =============================================================================
// DEMO 3 — Radial Tree of Life
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeDemo3(long& uid, int w, int h)
{
    std::vector<NodeSpec> specs = {
        {"LUCA",   "LUCA",            "",        "",          1.00},
        {"Bact",   "Bacteria",        "LUCA",    "bacteria",  0.90},
        {"Firm",   "Firmicutes",      "Bact",    "bacteria",  0.70},
        {"Staph",  "Staphylococcus",  "Firm",    "bacteria",  0.0, 0.95f},
        {"Bacil",  "Bacillus",        "Firm",    "bacteria",  0.0, 0.92f},
        {"Prot",   "Proteobacteria",  "Bact",    "bacteria",  0.60},
        {"Esch",   "Escherichia",     "Prot",    "bacteria",  0.0, 0.95f},
        {"Salm",   "Salmonella",      "Prot",    "bacteria",  0.0, 0.90f},
        {"Arch",   "Archaea",         "LUCA",    "archaea",   0.88},
        {"Cren",   "Crenarchaeota",   "Arch",    "archaea",   0.65},
        {"Sulfo",  "Sulfolobus",      "Cren",    "archaea",   0.0, 0.90f},
        {"Eury",   "Euryarchaeota",   "Arch",    "archaea",   0.62},
        {"Methan", "Methanobacter",   "Eury",    "archaea",   0.0, 0.92f},
        {"Euka",   "Eukaryota",       "LUCA",    "eukaryota", 0.85},
        {"Opist",  "Opisthokonta",    "Euka",    "eukaryota", 0.65},
        {"Fungi",  "Fungi",           "Opist",   "eukaryota", 0.45},
        {"Sacc",   "Saccharomyces",   "Fungi",   "eukaryota", 0.0, 0.95f},
        {"Anim",   "Animalia",        "Opist",   "eukaryota", 0.40},
        {"HomoS",  "Homo sapiens",    "Anim",    "eukaryota", 0.0, 0.98f},
        {"Plant",  "Plantae",         "Euka",    "eukaryota", 0.60},
        {"Arabi",  "Arabidopsis",     "Plant",   "eukaryota", 0.0, 0.95f},
    };
    std::vector<DendrogramGroup> groups = {
        {"bacteria",  "Bacteria",
         {"Staph","Bacil","Esch","Salm"},
         Color(180,50,120,255),Color(160,30,100,255),Color(180,50,120,255),
         GroupLabelPlacement::ArcText,GroupFillMode::SectorFill,0.22f},
        {"archaea",   "Archaea",
         {"Sulfo","Methan"},
         Color(180,155,30,255),Color(160,135,10,255),Color(200,175,30,255),
         GroupLabelPlacement::ArcText,GroupFillMode::SectorFill,0.22f},
        {"eukaryota", "Eukaryota",
         {"Sacc","HomoS","Arabi"},
         Color(30,150,130,255),Color(20,130,110,255),Color(30,150,130,255),
         GroupLabelPlacement::ArcText,GroupFillMode::SectorFill,0.22f},
    };

    auto data  = BuildTree(specs,groups);
    auto outer = std::make_shared<UltraCanvasContainer>("o3",0,0,w,h);
    auto d     = std::make_shared<UltraCanvasDendrogram>("D3",0,36,w,h-36);
    d->SetDataSource(data);
    d->SetOrientation(DendrogramOrientation::Radial);
    d->SetScaleMode(DendrogramScaleMode::Proportional);

    DendrogramStyle s;
    s.linkStyle           = DendrogramLinkStyle::Curved;
    s.branchColorMode     = BranchColorMode::ByGroup;
    s.defaultBranchWidth  = 1.5f;
    s.leafLabelFontSize   = 12.0f;
    s.colorLeafLabels     = true;
    s.colorLeafNodesByGroup = true;
    s.showInternalNodes   = true;
    s.internalNodeRadius  = 3.0f;
    s.internalNodeColor   = Color(50,50,60,255);
    s.scaleNodesByDepth   = true;
    s.nodeRadiusDepthScale = 1.0f;
    s.showDistanceAxis    = false;
    s.showGroupLabels     = true;
    s.arcLabelRadiusMul   = 1.14f;
    s.leafSpacing         = 22.0f;
    s.marginTop    = 70; s.marginBottom = 70;
    s.marginLeft   = 70; s.marginRight  = 70;
    d->SetStyle(s);

    outer->AddChild(MakeControlBar(uid,w,d,
        "Radial | Tree of Life | Angular arcs | DblClick=collapse | Ctrl+drag=move"));
    outer->AddChild(d);
    return outer;
}

// =============================================================================
// DEMO 4 — Left-Right proportional phylogenetic
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeDemo4(long& uid, int w, int h)
{
    std::vector<NodeSpec> specs = {
        {"root",    "Amniota",           "",        "",            1.00},
        {"Saurop",  "Sauropsida",        "root",    "sauropsida",  0.90},
        {"Aves",    "Aves",              "Saurop",  "aves",        0.50},
        {"Gallus",  "Gallus gallus",     "Aves",    "aves",        0.0, 0.95f},
        {"Reptil",  "Reptilia",          "Saurop",  "reptilia",    0.70},
        {"Croc",    "Crocodylia",        "Reptil",  "reptilia",    0.40},
        {"Alligat", "Alligator sinensis","Croc",    "reptilia",    0.0, 0.85f},
        {"Squam",   "Squamata",          "Reptil",  "reptilia",    0.35},
        {"Mamm",    "Mammalia",          "root",    "mammalia",    0.85},
        {"Monotr",  "Monotremata",       "Mamm",    "monotremata", 0.75},
        {"Ornith",  "Ornithorhynchus",   "Monotr",  "monotremata", 0.0, 0.90f},
        {"Theria",  "Theria",            "Mamm",    "mammalia",    0.70},
        {"Placent", "Placentalia",       "Theria",  "mammalia",    0.55},
        {"Primates","Primates",          "Placent", "primates",    0.38},
        {"HomoSap", "Homo sapiens",      "Primates","primates",    0.0, 0.98f},
    };
    std::vector<DendrogramGroup> groups = {
        {"aves",        "Aves",        {"Gallus"},
         Color(200,50,50,255), Color(180,30,30,255), Color(200,50,50,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"reptilia",    "Reptilia",    {"Alligat","Squam"},
         Color(230,120,30,255),Color(210,100,10,255),Color(230,120,30,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"monotremata", "Monotremata", {"Ornith"},
         Color(200,200,30,255),Color(180,180,10,255),Color(200,200,30,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
        {"primates",    "Primates",    {"HomoSap"},
         Color(100,60,200,255),Color(80,40,180,255), Color(100,60,200,40),
         GroupLabelPlacement::SidebarBracket,GroupFillMode::BandFill,0.12f},
    };

    auto data  = BuildTree(specs,groups);
    auto outer = std::make_shared<UltraCanvasContainer>("o4",0,0,w,h);
    auto d     = std::make_shared<UltraCanvasDendrogram>("D4",0,36,w,h-36);
    d->SetDataSource(data);
    d->SetOrientation(DendrogramOrientation::LeftRight);
    d->SetScaleMode(DendrogramScaleMode::Proportional);

    DendrogramStyle s;
    // Use curved links for a more organic, phylogenetic look
    s.linkStyle          = DendrogramLinkStyle::Curved;
    // Rich color palette for groups - scientific publication quality
    s.branchColorMode    = BranchColorMode::ByGroup;
    s.defaultBranchColor = Color(90,90,100,255);
    s.defaultBranchWidth = 1.4f;
    s.confidenceMode     = ConfidenceDisplayMode::LineThickness;
    s.branchWidthMin = 0.8f; s.branchWidthMax = 2.8f;
    
    // Leaf labels - cleaner and more readable
    s.leafLabelFontSize  = 12.0f;
    s.colorLeafLabels    = true;
    
    // Distance axis - more refined
    s.showDistanceAxis   = true;
    s.axisTickCount      = 5;
    s.axisFontSize       = 10.0f;
    s.axisColor          = Color(120,120,130,180);
    
    // Grid - subtle, doesn't distract
    s.showGrid           = true;
    s.gridColor          = Color(200,200,210,60);
    
    // Group labels - cleaner presentation
    s.showGroupLabels    = true;
    s.sidebarBracketWidth = 5;
    
    // Node styling - more elegant
    s.internalNodeRadius = 3.0f;
    s.internalNodeColor   = Color(70,70,80,255);
    s.leafNodeRadius      = 2.5f;
    s.showInternalNodes   = true;
    s.showLeafNodes       = true;
    
    // Leaf spacing - better fit
    s.leafSpacing        = 24.0f;
    
    // Internal node labels - smarter positioning
    s.showInternalNodeLabels    = true;
    s.showLabelsOnClick         = true;
    s.internalNodeLabelFontSize = 10.0f;
    s.internalNodeLabelColor    = Color(45, 45, 55, 255);
    s.internalNodeLabelOffset   = 8;
    
    // Margins - balanced layout
    s.marginTop    = 30;
    s.marginBottom = 60;
    s.marginLeft   = 30;
    s.marginRight  = 40;
    d->SetStyle(s);

    outer->AddChild(MakeControlBar(uid,w,d,
        "Phylogenetic | Curved branches | Group coloring | Click branch to show clade name"));
    outer->AddChild(d);
    return outer;
}

// =============================================================================
// DEMO 5 — Radial circular sectors
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeDemo5(long& uid, int w, int h)
{
    std::vector<NodeSpec> specs = {
        {"root",    "Root",              "",       "",         1.00},
        {"Anc",     "Ancient",           "root",   "ancient",  0.93},
        {"Chow",    "Chow-chow",         "Anc",    "ancient",  0.0, 0.95f},
        {"Akita",   "Akita",             "Anc",    "ancient",  0.0, 0.92f},
        {"Basenji", "Basenji",           "Anc",    "ancient",  0.0, 0.88f},
        {"Mod",     "Modern",            "root",   "modern",   0.85},
        {"Labrad",  "Labrador",          "Mod",    "modern",   0.0, 0.95f},
        {"GoldRet", "Golden Retriever",  "Mod",    "modern",   0.0, 0.93f},
        {"Beagle",  "Beagle",            "Mod",    "modern",   0.0, 0.90f},
        {"MastT",   "Mastiff-Terrier",   "root",   "mastiff",  0.80},
        {"Bulldog", "Bulldog",           "MastT",  "mastiff",  0.0, 0.88f},
        {"Boxer",   "Boxer",             "MastT",  "mastiff",  0.0, 0.85f},
    };
    std::vector<DendrogramGroup> groups = {
        {"ancient", "Ancient",
         {"Chow","Akita","Basenji"},
         Color(200,170,30,255),Color(180,150,10,255),Color(200,170,30,255),
         GroupLabelPlacement::ArcText,GroupFillMode::SectorFill,0.28f},
        {"modern",  "Modern",
         {"Labrad","GoldRet","Beagle"},
         Color(200,60,60,255), Color(180,40,40,255), Color(200,60,60,255),
         GroupLabelPlacement::ArcText,GroupFillMode::SectorFill,0.28f},
        {"mastiff", "Mastiff-Terrier",
         {"Bulldog","Boxer"},
         Color(30,100,200,255),Color(20,80,180,255), Color(30,100,200,255),
         GroupLabelPlacement::ArcText,GroupFillMode::SectorFill,0.28f},
    };

    auto data  = BuildTree(specs,groups);
    auto outer = std::make_shared<UltraCanvasContainer>("o5",0,0,w,h);
    auto d     = std::make_shared<UltraCanvasDendrogram>("D5",0,36,w,h-36);
    d->SetDataSource(data);
    d->SetOrientation(DendrogramOrientation::Radial);
    d->SetScaleMode(DendrogramScaleMode::Proportional);

    DendrogramStyle s;
    s.linkStyle           = DendrogramLinkStyle::Rectangular;
    s.branchColorMode     = BranchColorMode::ByGroup;
    s.defaultBranchWidth  = 1.2f;
    s.confidenceMode      = ConfidenceDisplayMode::NodeDot;
    s.confidenceHighThreshold = 0.80f;
    s.leafLabelFontSize   = 12.0f;
    s.colorLeafLabels     = true;
    s.colorLeafNodesByGroup = true;
    s.showInternalNodes   = true;
    s.internalNodeRadius  = 4.0f;
    s.internalNodeColor   = Color(20,20,25,255);
    s.showDistanceAxis    = false;
    s.showGroupLabels     = true;
    s.arcLabelRadiusMul   = 1.20f;
    s.leafSpacing         = 22.0f;
    s.marginTop    = 65; s.marginBottom = 65;
    s.marginLeft   = 65; s.marginRight  = 65;
    d->SetStyle(s);

    outer->AddChild(MakeControlBar(uid,w,d,
        "Circular | Sectors | DblClick=collapse | Ctrl+drag=move"));
    outer->AddChild(d);
    return outer;
}

// =============================================================================
// DEMO 6 — Rate gradient
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> MakeDemo6(long& uid, int w, int h)
{
    std::vector<NodeSpec> specs = {
        {"root", "Root",          "",     "", 1.0},
        {"cA",   "Clade A",       "root", "", 0.86},
        {"cA1",  "Clade A1",      "cA",   "", 0.62},
        {"A1a",  "Sp. A1a",       "cA1",  "", 0.0, 1.0f, 0.22f},
        {"A1b",  "Sp. A1b",       "cA1",  "", 0.0, 1.0f, 0.30f},
        {"cB",   "Clade B",       "root", "", 0.80},
        {"cB1",  "Clade B1",      "cB",   "", 0.58},
        {"B1a",  "Sp. B1a",       "cB1",  "", 0.0, 1.0f, 0.78f},
        {"B1b",  "Sp. B1b",       "cB1",  "", 0.0, 1.0f, 0.82f},
        {"cC",   "Clade C",       "root", "", 0.72},
        {"cC1",  "Clade C1",      "cC",   "", 0.50},
        {"C1a",  "Sp. C1a",       "cC1",  "", 0.0, 1.0f, 0.85f},
    };

    auto data  = BuildTree(specs,{});
    auto outer = std::make_shared<UltraCanvasContainer>("o6",0,0,w,h);
    auto d     = std::make_shared<UltraCanvasDendrogram>("D6",0,36,w,h-36);
    d->SetDataSource(data);
    d->SetOrientation(DendrogramOrientation::LeftRight);
    d->SetScaleMode(DendrogramScaleMode::Proportional);

    DendrogramStyle s;
    s.linkStyle          = DendrogramLinkStyle::Rectangular;
    s.branchColorMode    = BranchColorMode::ByValue;
    s.gradientLow        = Color(24, 95,165,255);
    s.gradientHigh       = Color(163,45, 45,255);
    s.defaultBranchWidth = 2.2f;
    s.leafLabelFontSize  = 13.0f;
    s.colorLeafLabels    = true;
    s.showInternalNodes  = true;
    s.internalNodeRadius = 4.0f;
    s.scaleNodesByDepth  = true;
    s.nodeRadiusDepthScale = 0.8f;
    s.showDistanceAxis   = true;
    s.axisTickCount      = 5;
    s.axisFontSize       = 11.0f;
    s.showGrid           = true;
    s.showGroupLabels    = false;
    s.leafSpacing        = 22.0f;
    s.marginTop    = 20;
    s.marginBottom = 55;
    s.marginLeft   = 20;
    s.marginRight  = 20;
    d->SetStyle(s);

    outer->AddChild(MakeControlBar(uid,w,d,
        "Rate Gradient | Blue→Red | DblClick=collapse | Ctrl+drag=move"));
    outer->AddChild(d);
    return outer;
}

// =============================================================================
// MAIN ENTRY POINT FOR DEMO APP
// =============================================================================

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDendrogramExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DendrogramExamples", 0, 0, 1220, 860);
        container->SetBackgroundColor(Color(255,255,255,255));
        
        const int headerH = 62;
        
        auto titleLbl = std::make_shared<UltraCanvasLabel>("DendrogramTitle", 1172, 34, "Dendrogram Analysis Suite");
        titleLbl->SetFontSize(22.0f);
        titleLbl->SetFontWeight(FontWeight::Bold);
        titleLbl->SetTextColor(Color(20,20,30,255));
        container->AddChild(titleLbl);
        
        auto subLbl = std::make_shared<UltraCanvasLabel>("DendrogramSub", 1172, 20, 
            "Interactive visualization of hierarchical data - Scroll to zoom, drag to pan, double-click to collapse");
        subLbl->SetFontSize(12.0f);
        subLbl->SetTextColor(Color(100,100,110,255));
        container->AddChild(subLbl);
        
        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("DendrogramTabs", 0, headerH, 1220, 860-headerH);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        
        long uid = 2000L;
        int tw = 1220;
        int th = 860 - headerH - 36;
        
        tabs->AddTab("Top-Down (E)",           MakeDemo1(uid,tw,th));
        tabs->AddTab("Left-Right Curved (F)", MakeDemo2(uid,tw,th));
        tabs->AddTab("Radial Tree of Life (G)", MakeDemo3(uid,tw,th));
        tabs->AddTab("Phylogenetic (H)",      MakeDemo4(uid,tw,th));
        tabs->AddTab("Circular Sectors (L)",  MakeDemo5(uid,tw,th));
        tabs->AddTab("Rate Gradient",         MakeDemo6(uid,tw,th));
        
        container->AddChild(tabs);
        
        return container;
    }
}
