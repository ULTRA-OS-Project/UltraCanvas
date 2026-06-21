// Apps/DemoApp/UltraCanvasCompositorDiagramExamples.cpp
// Comprehensive demo for UltraCanvasCompositorDiagram v0.4.0.
//
// Layout pattern follows UltraCanvasNodeDiagramExamples.cpp (title + subtitle
// + tabs + button bar + status bar).
//
// Tabs:
//   1. Recreate Images - 5 sub-scenarios reproducing the visual styles shown
//      in the design references (Layer Compositor, Brush Editor, Music KPI
//      tree, Portfolio tree, ORM modeler).
//   2. Subgraph / Groups - nested groups (React Flow model).
//   3. Visual Scripting - exec-pin (Trigger) sockets + boolean branches.
//   4. Feature Playground - empty canvas + full feature toolbar.
//
// Global toolbar exposes Undo / Redo / Cut / Copy / Paste / Duplicate /
// Group / Ungroup / Delete / Fit View / Save / Load + four toggles
// (Minimap, Controls, Snap, Alignment guides).

#include <stdexcept>  // ImageCairo.h uses std::runtime_error without including this
#include "Plugins/Diagrams/UltraCanvasCompositorDiagram.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasUIElement.h"
#include <memory>
#include <vector>
#include <string>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// SCENARIO BUILDERS - each registers a set of templates AND populates the
// diagram with the corresponding initial graph.
// =============================================================================

namespace {

// ----- Image 2: LAYER COMPOSITOR (the original screenshot) ------------------

void BuildLayerCompositor(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    // Layer template - the dominant card with preview at the bottom.
    CompositorNodeTemplate layer;
    layer.id = "layer";
    layer.category = "Compositor";
    layer.title = "Layer";
    layer.headerColor = Color(126, 86, 56, 255);     // warm brown
    layer.bodyColor   = Color(45, 45, 50, 245);
    layer.borderColor = Color(20, 20, 20, 255);
    layer.cornerRadius = 6.0;
    layer.defaultWidth = 220.0;
    layer.hasPreview = true;
    layer.previewHeight = 140.0;

    auto addOut = [&](const std::string& id, const std::string& label, SocketDataType t) {
        CompositorSocketSpec s; s.id = id; s.label = label; s.type = t;
        layer.outputs.push_back(s);
    };
    addOut("out",            "Output",          SocketDataType::Image);
    addOut("out_nofilters",  "Without filters", SocketDataType::Image);
    addOut("out_raw",        "Raw",             SocketDataType::Image);
    addOut("canvas_pos",     "Canvas Position", SocketDataType::Vector);
    addOut("center_pos",     "Center Position", SocketDataType::Vector);
    addOut("size",           "Size",            SocketDataType::Vector);

    auto addParam = [&](const std::string& id, const std::string& label,
                         SocketDataType inT, ParamWidgetKind w, ParamValueKind k) {
        CompositorParamSpec p; p.id = id; p.label = label;
        p.hasInputSocket = true; p.inputType = inT;
        p.widget = w; p.valueKind = k;
        layer.params.push_back(p);
    };
    addParam("opacity",    "Opacity",       SocketDataType::Scalar,  ParamWidgetKind::NumberInput, ParamValueKind::Number);
    layer.params.back().defaultNumber = 1.0;
    addParam("visible",    "Is visible",    SocketDataType::Boolean, ParamWidgetKind::Checkbox,    ParamValueKind::Boolean);
    layer.params.back().defaultBool = true;
    addParam("blend",      "Blend mode",    SocketDataType::Color,   ParamWidgetKind::Dropdown,    ParamValueKind::Unset);
    layer.params.back().choices = {"Normal", "Multiply", "Screen", "Overlay", "Soft Light", "Hard Light"};

    auto addIn = [&](const std::string& id, const std::string& label, SocketDataType t) {
        CompositorSocketSpec s; s.id = id; s.label = label; s.type = t;
        s.maxConnections = 1;
        layer.inputs.push_back(s);
    };
    addIn("bg",           "Background", SocketDataType::Image);
    addIn("mask",         "Mask",       SocketDataType::Image);

    addParam("mask_vis",   "Mask is visible", SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    layer.params.back().defaultBool = true;
    addIn("filters",      "Filters",    SocketDataType::Data);

    d.RegisterTemplate(layer);

    // Output template - small, just one input + preview
    CompositorNodeTemplate out;
    out.id = "output";
    out.category = "Compositor";
    out.title = "Output";
    out.headerColor = Color(60, 140, 80, 255);       // green
    out.bodyColor   = Color(45, 45, 50, 245);
    out.borderColor = Color(60, 200, 80, 255);
    out.borderWidth = 2.0;
    out.cornerRadius = 6.0;
    out.defaultWidth = 180.0;
    out.hasPreview = true;
    out.previewHeight = 140.0;
    {
        CompositorSocketSpec s; s.id = "bg"; s.label = "Background"; s.type = SocketDataType::Image;
        s.maxConnections = 1;
        out.inputs.push_back(s);
    }
    d.RegisterTemplate(out);

    d.AddNode("layer1", "layer", 40,  20);
    d.AddNode("layer2", "layer", 320, 50);
    d.AddNode("out1",   "output", 600, 70);

    d.AddLink("L1", "layer1", "out", "layer2", "bg");
    d.AddLink("L2", "layer2", "out", "out1",   "bg");

    d.SetHistoryRecording(true);
    d.FitView(30.0);
}

// ----- Image 1: KRITA-STYLE BRUSH EDITOR ------------------------------------

void BuildBrushEditor(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    // Ellipse: smallish shape generator with several inputs+widgets.
    CompositorNodeTemplate ellipse;
    ellipse.id = "ellipse";
    ellipse.category = "Shapes";
    ellipse.title = "Ellipse";
    ellipse.headerColor = Color(110, 50, 150, 255);  // purple
    ellipse.bodyColor   = Color(45, 50, 65, 245);
    ellipse.borderColor = Color(140, 200, 140, 255); // greenish border
    ellipse.borderWidth = 2.0;
    ellipse.cornerRadius = 6.0;
    ellipse.defaultWidth = 240.0;
    {
        CompositorSocketSpec s;
        s.id = "out"; s.label = "Output"; s.type = SocketDataType::Vector;
        ellipse.outputs.push_back(s);
    }
    auto addP = [&](CompositorNodeTemplate& t,
                     const std::string& id, const std::string& label,
                     SocketDataType inT, ParamWidgetKind w, ParamValueKind k,
                     double def = 0.0) {
        CompositorParamSpec p; p.id = id; p.label = label;
        p.hasInputSocket = true; p.inputType = inT;
        p.widget = w; p.valueKind = k;
        p.defaultNumber = def;
        t.params.push_back(p);
    };
    addP(ellipse, "pos_x",   "Position",     SocketDataType::Vector, ParamWidgetKind::NumberInput, ParamValueKind::Number, 0);
    addP(ellipse, "pos_y",   "",             SocketDataType::Vector, ParamWidgetKind::NumberInput, ParamValueKind::Number, 0);
    addP(ellipse, "rad_x",   "Radius",       SocketDataType::Vector, ParamWidgetKind::NumberInput, ParamValueKind::Number, 1);
    addP(ellipse, "rad_y",   "",             SocketDataType::Vector, ParamWidgetKind::NumberInput, ParamValueKind::Number, 1);
    addP(ellipse, "stroke",  "Stroke color", SocketDataType::Color,  ParamWidgetKind::ColorSwatch, ParamValueKind::Color);
    ellipse.params.back().defaultColor = Color(20, 20, 20, 255);
    addP(ellipse, "fill",    "Fill color",   SocketDataType::Color,  ParamWidgetKind::ColorSwatch, ParamValueKind::Color);
    ellipse.params.back().defaultColor = Color(40, 40, 40, 255);
    addP(ellipse, "swidth",  "Stroke width", SocketDataType::Scalar, ParamWidgetKind::NumberInput, ParamValueKind::Number, 1);
    d.RegisterTemplate(ellipse);

    // Brush Output: big multi-input/checkbox node.
    CompositorNodeTemplate brush;
    brush.id = "brush_output";
    brush.category = "Brush";
    brush.title = "Brush Output";
    brush.headerColor = Color(20, 110, 130, 255);    // dark teal
    brush.bodyColor   = Color(45, 50, 60, 245);
    brush.borderColor = Color(20, 20, 20, 255);
    brush.cornerRadius = 6.0;
    brush.defaultWidth = 280.0;
    brush.hasPreview = true;
    brush.previewHeight = 90.0;
    {
        CompositorParamSpec p; p.id = "name"; p.label = "Name";
        p.widget = ParamWidgetKind::TextInput; p.valueKind = ParamValueKind::Text;
        p.defaultText = "Basic";
        brush.params.push_back(p);
    }
    auto addBrushIn = [&](const std::string& id, const std::string& label, SocketDataType t) {
        CompositorSocketSpec s; s.id = id; s.label = label; s.type = t; s.maxConnections = 1;
        brush.inputs.push_back(s);
    };
    addBrushIn("shape",     "Shape",     SocketDataType::Vector);
    addBrushIn("stroke",    "Stroke",    SocketDataType::Color);
    addBrushIn("fill",      "Fill",      SocketDataType::Color);
    addBrushIn("content",   "Content",   SocketDataType::Image);
    addBrushIn("transform", "Transform", SocketDataType::Matrix);
    addP(brush, "blend",     "Blend mode",       SocketDataType::Trigger, ParamWidgetKind::Dropdown, ParamValueKind::Unset);
    brush.params.back().choices = {"Normal", "Multiply", "Screen", "Overlay"};
    addP(brush, "stampblend","Stamp Blend Mode", SocketDataType::Trigger, ParamWidgetKind::Dropdown, ParamValueKind::Unset);
    brush.params.back().choices = {"Normal", "Multiply", "Screen"};
    addP(brush, "custom_blender",    "Use Custom Stamp Blender",  SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    addP(brush, "reuse_stamps",      "Can Reuse Stamps",          SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    brush.params.back().defaultBool = true;
    addBrushIn("pressure",  "Pressure",  SocketDataType::Scalar);
    addBrushIn("spacing",   "Spacing",   SocketDataType::Scalar);
    addP(brush, "fit_size",          "Fit to stroke size",        SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    brush.params.back().defaultBool = true;
    addP(brush, "auto_position",     "Auto Position",             SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    brush.params.back().defaultBool = true;
    addP(brush, "stacking",          "Allow sample stacking",     SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    addP(brush, "always_clear",      "Always Clear",              SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    addP(brush, "snap_pixels",       "Snap to Pixels",            SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
    addBrushIn("tags",      "Tags",     SocketDataType::List);
    addBrushIn("previous",  "Previous", SocketDataType::Trigger);
    d.RegisterTemplate(brush);

    // Pointer Info: read-only reference node, outputs only.
    CompositorNodeTemplate pinfo;
    pinfo.id = "pointer_info";
    pinfo.category = "Input";
    pinfo.title = "Pointer Info";
    pinfo.headerColor = Color(70, 70, 80, 255);
    pinfo.bodyColor   = Color(45, 45, 50, 245);
    pinfo.borderColor = Color(20, 20, 20, 255);
    pinfo.cornerRadius = 6.0;
    pinfo.defaultWidth = 220.0;
    auto addPInfoOut = [&](const std::string& id, const std::string& label, SocketDataType t) {
        CompositorSocketSpec s; s.id = id; s.label = label; s.type = t;
        pinfo.outputs.push_back(s);
    };
    addPInfoOut("lbtn",     "Is Left Button Pressed",  SocketDataType::Boolean);
    addPInfoOut("rbtn",     "Is Right Button Pressed", SocketDataType::Boolean);
    addPInfoOut("pos",      "Position on Canvas",      SocketDataType::Vector);
    addPInfoOut("pressure", "Pressure",                SocketDataType::Scalar);
    addPInfoOut("twist",    "Twist",                   SocketDataType::Scalar);
    addPInfoOut("tilt",     "Tilt",                    SocketDataType::Scalar);
    addPInfoOut("movement", "Movement Direction",      SocketDataType::Vector);
    addPInfoOut("rotation", "Rotation",                SocketDataType::Scalar);
    d.RegisterTemplate(pinfo);

    // Blackboard Variable Value: dropdown + scalar output.
    CompositorNodeTemplate bb;
    bb.id = "blackboard";
    bb.category = "Variables";
    bb.title = "Blackboard Variable Value";
    bb.headerColor = Color(70, 70, 80, 255);
    bb.bodyColor   = Color(45, 45, 50, 245);
    bb.borderColor = Color(20, 20, 20, 255);
    bb.cornerRadius = 6.0;
    bb.defaultWidth = 220.0;
    {
        CompositorParamSpec p; p.id = "var"; p.label = "Variable Name";
        p.widget = ParamWidgetKind::Dropdown; p.valueKind = ParamValueKind::Unset;
        p.choices = {"SPACING", "PRESSURE_FALLOFF", "STAMP_RATE"};
        bb.params.push_back(p);
    }
    {
        CompositorSocketSpec s; s.id = "value"; s.label = "Value"; s.type = SocketDataType::Scalar;
        bb.outputs.push_back(s);
    }
    d.RegisterTemplate(bb);

    // Initial graph reproducing the brush editor layout.
    d.AddNode("e1",   "ellipse",        140, 40);
    d.AddNode("bo",   "brush_output",   500, 60);
    d.AddNode("pi",   "pointer_info",  -120, 380);
    d.AddNode("bb",   "blackboard",     220, 540);

    d.AddLink("BL1", "e1", "out",       "bo", "shape");
    d.AddLink("BL2", "pi", "pressure",  "bo", "pressure");
    d.AddLink("BL3", "bb", "value",     "bo", "spacing");

    d.SetHistoryRecording(true);
    d.FitView(40.0);
}

// ----- Image 3: MUSIC SUBSCRIPTION KPI TREE (light theme, edge labels) ------

void BuildMusicKPI(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    // Apply a light theme.
    CompositorDiagramStyle st = d.GetStyle();
    st.backgroundColor = Color(243, 247, 252, 255);
    st.gridColor       = Color(228, 232, 240, 255);
    st.showGrid = false;
    d.SetStyle(st);

    auto whiteCard = [](CompositorNodeTemplate& t,
                         const std::string& id, const std::string& title,
                         const Color& accent) {
        t.id = id;
        t.title = title;
        t.category = "KPI";
        t.headerColor = Color(255, 255, 255, 255);
        t.headerTextColor = Color(35, 45, 80, 255);
        t.bodyColor = Color(255, 255, 255, 255);
        t.borderColor = accent;
        t.borderWidth = 1.0;
        t.cornerRadius = 6.0;
        t.defaultWidth = 230.0;
    };
    auto addLine = [](CompositorNodeTemplate& t, const std::string& label) {
        CompositorParamSpec p; p.id = label + std::to_string(t.params.size());
        p.label = label;
        p.widget = ParamWidgetKind::NoWidget;
        p.hasInputSocket = false;
        t.params.push_back(p);
    };
    auto addIn = [](CompositorNodeTemplate& t, SocketDataType type) {
        CompositorSocketSpec s; s.id = "in"; s.label = ""; s.type = type;
        s.maxConnections = 0;
        t.inputs.push_back(s);
    };
    auto addOut = [](CompositorNodeTemplate& t, SocketDataType type) {
        CompositorSocketSpec s; s.id = "out"; s.label = ""; s.type = type;
        t.outputs.push_back(s);
    };

    // Project / Epic card templates (no metric values, just title + meta).
    CompositorNodeTemplate proj;
    whiteCard(proj, "kpi_project", "Project", Color(180, 195, 225, 255));
    addLine(proj, "Asana (Project)");
    addLine(proj, "6 issues  -  67% done");
    addOut(proj, SocketDataType::Data);
    d.RegisterTemplate(proj);

    CompositorNodeTemplate epic;
    whiteCard(epic, "kpi_epic", "Epic", Color(180, 195, 225, 255));
    addLine(epic, "Jira (Epic)");
    addLine(epic, "4 issues  -  50% done");
    addOut(epic, SocketDataType::Data);
    d.RegisterTemplate(epic);

    // Metric (Input)
    CompositorNodeTemplate metricIn;
    whiteCard(metricIn, "kpi_metric_in", "Metric", Color(160, 200, 230, 255));
    addLine(metricIn, "Metric (Input)  -  Sum");
    addLine(metricIn, "7d:  -    6w:  -    12m:  - ");
    addIn(metricIn, SocketDataType::Data);
    addOut(metricIn, SocketDataType::Scalar);
    d.RegisterTemplate(metricIn);

    // Metric (North Star)
    CompositorNodeTemplate northStar;
    whiteCard(northStar, "kpi_north", "North Star", Color(80, 180, 130, 255));
    northStar.borderWidth = 2.0;
    addLine(northStar, "Metric (North Star)  -  Sum");
    addLine(northStar, "7d: 4.41K mins (0.43%)");
    addLine(northStar, "6w: 26.15K mins (2.57%)");
    addLine(northStar, "12m: 198.31K mins (38.59%)");
    addIn(northStar, SocketDataType::Scalar);
    addOut(northStar, SocketDataType::Scalar);
    d.RegisterTemplate(northStar);

    // Metric (KPI)
    CompositorNodeTemplate kpiCard;
    whiteCard(kpiCard, "kpi_metric_kpi", "KPI", Color(180, 195, 225, 255));
    addLine(kpiCard, "Metric (KPI)  -  Sum");
    addLine(kpiCard, "7d:  -    6w:  -    12m:  - ");
    addIn(kpiCard, SocketDataType::Scalar);
    d.RegisterTemplate(kpiCard);

    // Helper to create a metric with title and three time-series rows.
    auto addMetric = [&](const std::string& nodeId, const std::string& templateId,
                          const std::string& title,
                          double x, double y) {
        d.AddNode(nodeId, templateId, x, y);
        auto* n = d.GetNode(nodeId);
        if (n) n->titleOverride = title;
    };

    addMetric("camp",    "kpi_project",    "New marketing campaign",  60, 60);
    addMetric("social",  "kpi_epic",       "Social notifications",    60, 220);
    addMetric("timed",   "kpi_epic",       "Time-based notifications",60, 360);
    addMetric("ai",      "kpi_epic",       "AI model for songs",      60, 500);
    addMetric("share",   "kpi_epic",       "Sharing prompts",         60, 660);

    addMetric("trial",     "kpi_metric_in",  "Premium trial users",      360, 60);
    addMetric("sessions",  "kpi_metric_in",  "Avg. sessions per week",   360, 220);
    addMetric("duration",  "kpi_metric_in",  "Average session duration", 360, 380);
    addMetric("shares",    "kpi_metric_in",  "Avg. shares per session",  360, 540);

    addMetric("northstar", "kpi_north",      "Time spent listening",     680, 320);

    addMetric("arr",       "kpi_metric_kpi", "ARR",                      1000, 100);
    addMetric("ret",       "kpi_metric_kpi", "Monthly retention",        1000, 320);
    addMetric("subs",      "kpi_metric_kpi", "Monthly premium subs",     1000, 540);

    // Links with correlation labels.
    Color GREEN = Color(80, 180, 130, 230);
    Color RED   = Color(220, 80, 80, 230);

    auto addLabeled = [&](const std::string& id,
                           const std::string& src, const std::string& dst,
                           const std::string& label, const Color& bg) {
        CompositorLink l;
        l.id = id;
        l.sourceNodeId = src; l.sourceSocketId = "out";
        l.targetNodeId = dst; l.targetSocketId = "in";
        l.label = label;
        l.labelBgColor = bg;
        l.style = LinkStyle::Bezier;
        l.lineColorOverride = Color(120, 130, 150, 200);
        l.lineWidth = 1.5;
        d.AddLink(l);
    };
    addLabeled("k1",  "camp",    "trial",     "",        GREEN);
    addLabeled("k2",  "social",  "sessions",  "",        GREEN);
    addLabeled("k3",  "timed",   "sessions",  "",        GREEN);
    addLabeled("k4",  "ai",      "duration",  "",        GREEN);
    addLabeled("k5",  "share",   "shares",    "",        GREEN);

    addLabeled("k6",  "trial",     "northstar", "0.998",  GREEN);
    addLabeled("k7",  "sessions",  "northstar", "0.998",  GREEN);
    addLabeled("k8",  "duration",  "northstar", "-0.644", RED);
    addLabeled("k9",  "shares",    "northstar", "0.999",  GREEN);

    addLabeled("k10", "northstar", "arr",   "0.388",  GREEN);
    addLabeled("k11", "northstar", "ret",   "0.999",  GREEN);
    addLabeled("k12", "northstar", "subs",  "0.998",  GREEN);

    d.SetHistoryRecording(true);
    d.FitView(20.0);
}

// ----- Image 4: PORTFOLIO/CAMPAIGN/AD HIERARCHY (light theme) ---------------

void BuildPortfolioTree(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    CompositorDiagramStyle st = d.GetStyle();
    st.backgroundColor = Color(248, 250, 252, 255);
    st.gridColor       = Color(225, 230, 240, 255);
    st.showGrid = true;
    d.SetStyle(st);

    auto plainCard = [](CompositorNodeTemplate& t, const std::string& id,
                         const std::string& title, double width) {
        t.id = id;
        t.title = title;
        t.category = "Portfolio";
        t.headerColor = Color(255, 255, 255, 255);
        t.headerTextColor = Color(35, 40, 60, 255);
        t.bodyColor = Color(255, 255, 255, 255);
        t.borderColor = Color(200, 210, 225, 255);
        t.borderWidth = 1.0;
        t.cornerRadius = 4.0;
        t.defaultWidth = width;
    };
    auto bullet = [](CompositorNodeTemplate& t, const std::string& s) {
        CompositorParamSpec p;
        p.id = "b" + std::to_string(t.params.size());
        p.label = "- " + s;
        p.widget = ParamWidgetKind::NoWidget;
        t.params.push_back(p);
    };
    auto addIn = [](CompositorNodeTemplate& t) {
        CompositorSocketSpec s; s.id = "in"; s.type = SocketDataType::Data;
        t.inputs.push_back(s);
    };
    auto addOut = [](CompositorNodeTemplate& t) {
        CompositorSocketSpec s; s.id = "out"; s.type = SocketDataType::Data;
        t.outputs.push_back(s);
    };

    CompositorNodeTemplate portfolio;
    plainCard(portfolio, "p_portfolio", "Portfolio", 220);
    bullet(portfolio, "contains campaigns");
    bullet(portfolio, "optional");
    bullet(portfolio, "may contain various ad types");
    bullet(portfolio, "may assign budget");
    addOut(portfolio);
    d.RegisterTemplate(portfolio);

    CompositorNodeTemplate campaign;
    plainCard(campaign, "p_campaign", "Campaign", 220);
    bullet(campaign, "contains ad groups");
    bullet(campaign, "may contain one ad type");
    bullet(campaign, "may assign negative targets");
    bullet(campaign, "may assign budget");
    addIn(campaign);
    addOut(campaign);
    d.RegisterTemplate(campaign);

    CompositorNodeTemplate adgroup;
    plainCard(adgroup, "p_adgroup", "Ad Group", 220);
    bullet(adgroup, "contains ads");
    bullet(adgroup, "may contain one ad type");
    bullet(adgroup, "may assign targets");
    bullet(adgroup, "may assign negative targets");
    addIn(adgroup);
    addOut(adgroup);
    d.RegisterTemplate(adgroup);

    CompositorNodeTemplate ad;
    plainCard(ad, "p_ad", "Ad", 220);
    bullet(ad, "Format depends on ad type");
    addIn(ad);
    d.RegisterTemplate(ad);

    d.AddNode("port",   "p_portfolio", 380, 30);
    d.AddNode("camp",   "p_campaign",  380, 240);
    d.AddNode("ag1",    "p_adgroup",   100, 460);
    d.AddNode("ag2",    "p_adgroup",   660, 460);
    d.AddNode("ad1",    "p_ad",        20,  720);
    d.AddNode("ad2",    "p_ad",        380, 720);
    d.AddNode("ad3",    "p_ad",        740, 720);

    Color edgeC(180, 195, 215, 220);
    auto addEdge = [&](const std::string& id,
                        const std::string& s, const std::string& t) {
        CompositorLink l;
        l.id = id;
        l.sourceNodeId = s; l.sourceSocketId = "out";
        l.targetNodeId = t; l.targetSocketId = "in";
        l.style = LinkStyle::Step;
        l.lineColorOverride = edgeC;
        l.lineWidth = 1.5;
        d.AddLink(l);
    };
    addEdge("e1", "port", "camp");
    addEdge("e2", "camp", "ag1");
    addEdge("e3", "camp", "ag2");
    addEdge("e4", "ag1",  "ad1");
    addEdge("e5", "ag2",  "ad2");
    addEdge("e6", "ag2",  "ad3");

    d.SetHistoryRecording(true);
    d.FitView(30.0);
}

// ----- Image 5: STORM-STYLE ORM MODELER --------------------------------------

void BuildOrmModeler(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    CompositorDiagramStyle st = d.GetStyle();
    st.backgroundColor = Color(38, 30, 60, 255);
    st.gridColor       = Color(50, 40, 75, 255);
    st.showGrid = true;
    d.SetStyle(st);

    auto card = [](CompositorNodeTemplate& t, const std::string& id,
                    const std::string& title) {
        t.id = id;
        t.title = title;
        t.category = "ORM";
        t.headerColor = Color(80, 80, 95, 255);
        t.headerTextColor = Color(245, 245, 250, 255);
        t.bodyColor = Color(60, 60, 75, 240);
        t.borderColor = Color(40, 40, 55, 255);
        t.cornerRadius = 4.0;
        t.defaultWidth = 220.0;
    };
    auto addField = [](CompositorNodeTemplate& t,
                        const std::string& typeTag, const std::string& name,
                        bool out, SocketDataType outType) {
        CompositorParamSpec p;
        p.id = name;
        p.label = "[" + typeTag + "]  " + name;
        p.widget = ParamWidgetKind::NoWidget;
        p.hasInputSocket = !out;
        if (!out) p.inputType = SocketDataType::String;
        t.params.push_back(p);
        if (out) {
            CompositorSocketSpec s; s.id = name; s.label = name; s.type = outType;
            t.outputs.push_back(s);
        }
    };

    CompositorNodeTemplate addr;
    card(addr, "orm_address", "Create Address");
    addField(addr, "S", "id", true, SocketDataType::String);
    addField(addr, "S", "street", false, SocketDataType::String);
    addField(addr, "S", "city",   false, SocketDataType::String);
    addField(addr, "S", "province", false, SocketDataType::String);
    addField(addr, "I", "postal", false, SocketDataType::Scalar);
    d.RegisterTemplate(addr);

    CompositorNodeTemplate contact;
    card(contact, "orm_contact", "Create Contact Details");
    addField(contact, "S[]", "email", false, SocketDataType::List);
    addField(contact, "S[]", "cell",  false, SocketDataType::List);
    addField(contact, "S[]", "tell",  false, SocketDataType::List);
    addField(contact, "S[]", "fax",   false, SocketDataType::List);
    addField(contact, "A",  "postalAddress",   false, SocketDataType::Custom);
    addField(contact, "A",  "physicalAddress", false, SocketDataType::Custom);
    addField(contact, "CD", "contactDetails",  true,  SocketDataType::Custom);
    d.RegisterTemplate(contact);

    CompositorNodeTemplate user;
    card(user, "orm_user", "Create User");
    addField(user, "S",  "name",    true,  SocketDataType::String);
    addField(user, "S",  "surname", false, SocketDataType::String);
    addField(user, "S",  "middle",  false, SocketDataType::String);
    addField(user, "D",  "dob",     false, SocketDataType::Time);
    addField(user, "OP", "orgProfile",     false, SocketDataType::Custom);
    addField(user, "CD", "contactDetails", false, SocketDataType::Custom);
    d.RegisterTemplate(user);

    CompositorNodeTemplate orgtype;
    card(orgtype, "orm_orgtype", "Create Organization Type");
    addField(orgtype, "S", "name",         true,  SocketDataType::String);
    addField(orgtype, "O", "organization", false, SocketDataType::Custom);
    d.RegisterTemplate(orgtype);

    CompositorNodeTemplate org;
    card(org, "orm_org", "Create Organization");
    addField(org, "S",  "name",           true,  SocketDataType::String);
    addField(org, "S",  "desc",           false, SocketDataType::String);
    addField(org, "S",  "vat",            false, SocketDataType::String);
    addField(org, "CD", "contactDetails", false, SocketDataType::Custom);
    d.RegisterTemplate(org);

    CompositorNodeTemplate orgprof;
    card(orgprof, "orm_orgprof", "Create Organization Profile");
    addField(orgprof, "U",  "user",  false, SocketDataType::Custom);
    addField(orgprof, "O",  "org",   false, SocketDataType::Custom);
    addField(orgprof, "O",  "type",  false, SocketDataType::Custom);
    addField(orgprof, "S",  "code",  false, SocketDataType::String);
    addField(orgprof, "OP", "profile", true,  SocketDataType::Custom);
    d.RegisterTemplate(orgprof);

    d.AddNode("addr",     "orm_address",  60,  280);
    d.AddNode("contact",  "orm_contact",  380, 130);
    d.AddNode("user",     "orm_user",     800, 130);
    d.AddNode("orgtype",  "orm_orgtype",  580, 400);
    d.AddNode("org",      "orm_org",      380, 480);
    d.AddNode("orgprof",  "orm_orgprof",  1100,300);

    auto link = [&](const std::string& id, const std::string& sn, const std::string& ss,
                     const std::string& dn, const std::string& ds) {
        CompositorLink l;
        l.id = id;
        l.sourceNodeId = sn; l.sourceSocketId = ss;
        l.targetNodeId = dn; l.targetSocketId = ds;
        l.style = LinkStyle::Bezier;
        l.lineColorOverride = Color(180, 190, 230, 255);
        l.lineWidth = 1.5;
        d.AddLink(l);
    };
    link("o1", "addr",    "id",   "contact", "postalAddress");
    link("o2", "contact", "contactDetails", "user",  "contactDetails");
    link("o3", "user",    "name", "orgprof", "user");
    link("o4", "org",     "name", "orgprof", "org");
    link("o5", "orgtype", "name", "orgprof", "type");

    d.SetHistoryRecording(true);
    d.FitView(30.0);
}

// ----- LOGIC DIAGRAM (German PLC / smart-home function blocks) --------------

void BuildLogicDiagram(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    CompositorDiagramStyle st = d.GetStyle();
    st.backgroundColor = Color(245, 245, 245, 255);
    st.gridColor       = Color(228, 228, 228, 255);
    st.showGrid = true;
    d.SetStyle(st);

    const Color FB_HEADER(190, 220, 170, 255);    // light green - regular FB
    const Color FB_ORANGE(230, 175, 100, 255);    // orange - Tacho
    const Color FB_BLUE  (175, 205, 220, 255);    // blue - Trainingsreset
    const Color FB_BODY  (255, 252, 220, 255);    // cream
    const Color FB_BORDER(150, 150, 90, 255);
    const Color FB_TEXT  (20, 25, 20, 255);
    const Color CST_BODY (225, 225, 225, 255);
    const Color CST_BORDER(170, 170, 170, 255);
    const Color IO_BODY  (255, 255, 255, 255);

    auto mkBase = [&](const std::string& id, const std::string& subtitle,
                       const Color& headerCol, double width) {
        CompositorNodeTemplate t;
        t.id = id;
        t.title = subtitle;
        t.category = "Logic";
        t.headerColor = headerCol;
        t.headerTextColor = FB_TEXT;
        t.bodyColor = FB_BODY;
        t.borderColor = FB_BORDER;
        t.borderWidth = 1.0;
        t.cornerRadius = 2.0;
        t.defaultWidth = width;
        return t;
    };
    auto addIn  = [](CompositorNodeTemplate& t, const std::string& id, const std::string& lbl) {
        CompositorSocketSpec s; s.id = id; s.label = lbl;
        s.type = SocketDataType::Scalar; s.maxConnections = 1;
        t.inputs.push_back(s);
    };
    auto addOut = [](CompositorNodeTemplate& t, const std::string& id, const std::string& lbl) {
        CompositorSocketSpec s; s.id = id; s.label = lbl;
        s.type = SocketDataType::Scalar;
        t.outputs.push_back(s);
    };

    // --- Function-block templates ---
    { auto t = mkBase("fb_pulse",   "Impulsgeber",        FB_HEADER, 130);
      addIn(t,"DisP","DisP"); addIn(t,"Inv","Inv"); addOut(t,"Q","Q");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_counter", "Zaehler",            FB_HEADER, 130);
      addIn(t,"Tr","Tr"); addIn(t,"R","R"); addOut(t,"AQ","AQ");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_analog",  "Analogspeicher",     FB_HEADER, 130);
      addIn(t,"AI","AI"); addIn(t,"Tr","Tr"); addIn(t,"R","R"); addOut(t,"AQ","AQ");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_delayed", "Verzoegerter Impuls",FB_BLUE,   150);
      addIn(t,"Tr","Tr"); addIn(t,"R","R"); addOut(t,"Q","Q");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_and",     "UND",                FB_HEADER, 110);
      addIn(t,"I1","I1"); addIn(t,"I2","I2"); addOut(t,"Q","Q");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_formula", "Formel",             FB_HEADER, 130);
      addIn(t,"AI1","AI1"); addIn(t,"AI2","AI2");
      addIn(t,"AI3","AI3"); addIn(t,"AI4","AI4");
      addOut(t,"AQ","AQ"); addOut(t,"TQ","TQ");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_div",     "Dividierer",         FB_HEADER, 130);
      addIn(t,"AI1","AI1"); addIn(t,"AI2","AI2"); addOut(t,"AQ","AQ");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_int",     "Ganzzahl",           FB_HEADER, 130);
      addIn(t,"AI1","AI1"); addIn(t,"R","R"); addOut(t,"AQ","AQ");
      d.RegisterTemplate(t); }
    { auto t = mkBase("fb_energy",  "Energiezaehler",     FB_ORANGE, 140);
      addIn(t,"Tr","Tr"); addIn(t,"C","C"); addIn(t,"V","V");
      addIn(t,"P","P"); addIn(t,"R","R");
      addOut(t,"AQ","AQ");  addOut(t,"AQp","AQp"); addOut(t,"AQa","AQa");
      addOut(t,"AQ1","AQ1"); addOut(t,"AQ2","AQ2"); addOut(t,"AQ3","AQ3");
      addOut(t,"AQ4","AQ4"); addOut(t,"AQ5","AQ5"); addOut(t,"AQ6","AQ6");
      addOut(t,"AQ7","AQ7"); addOut(t,"AQ8","AQ8"); addOut(t,"AQ9","AQ9");
      addOut(t,"T","T");    addOut(t,"Q","Q");
      d.RegisterTemplate(t); }

    // --- Constants (gray strip with single output) ---
    {
        CompositorNodeTemplate t;
        t.id = "fb_const";
        t.title = "C";
        t.category = "Logic";
        t.headerColor = CST_BODY;
        t.headerTextColor = FB_TEXT;
        t.bodyColor = CST_BODY;
        t.borderColor = CST_BORDER;
        t.cornerRadius = 2.0;
        t.defaultWidth = 200.0;
        addOut(t, "out", "");
        d.RegisterTemplate(t);
    }

    // --- Input marker (small white box, single output, letter badge in title) ---
    {
        CompositorNodeTemplate t;
        t.id = "fb_input";
        t.title = "Input";
        t.category = "Logic";
        t.headerColor = IO_BODY;
        t.headerTextColor = FB_TEXT;
        t.bodyColor = IO_BODY;
        t.borderColor = CST_BORDER;
        t.cornerRadius = 2.0;
        t.defaultWidth = 160.0;
        addOut(t, "out", "");
        d.RegisterTemplate(t);
    }

    // --- Output sink template - "Virtueller Status" cards on the right edge ---
    {
        CompositorNodeTemplate t;
        t.id = "fb_status";
        t.title = "Status";
        t.category = "Logic";
        t.headerColor = IO_BODY;
        t.headerTextColor = FB_TEXT;
        t.bodyColor = IO_BODY;
        t.borderColor = CST_BORDER;
        t.cornerRadius = 2.0;
        t.defaultWidth = 170.0;
        CompositorParamSpec sub;
        sub.id = "_sub"; sub.label = "Virtueller Status";
        sub.widget = ParamWidgetKind::NoWidget;
        t.params.push_back(sub);
        CompositorParamSpec studio;
        studio.id = "_studio"; studio.label = "Studio";
        studio.widget = ParamWidgetKind::NoWidget;
        t.params.push_back(studio);
        CompositorSocketSpec s; s.id = "AI1"; s.label = "AI1";
        s.type = SocketDataType::Scalar; s.maxConnections = 1;
        t.inputs.push_back(s);
        d.RegisterTemplate(t);
    }

    // --- "Merker" memo template (white sink with single AI input) ---
    {
        CompositorNodeTemplate t;
        t.id = "fb_memo";
        t.title = "Merker";
        t.category = "Logic";
        t.headerColor = IO_BODY;
        t.headerTextColor = FB_TEXT;
        t.bodyColor = IO_BODY;
        t.borderColor = CST_BORDER;
        t.cornerRadius = 2.0;
        t.defaultWidth = 160.0;
        CompositorSocketSpec s; s.id = "AI"; s.label = "AI";
        s.type = SocketDataType::Scalar; s.maxConnections = 1;
        t.inputs.push_back(s);
        d.RegisterTemplate(t);
    }

    // --- Helper for instantiating a node with a custom title ---
    auto place = [&](const std::string& id, const std::string& tmpl,
                      const std::string& title, double x, double y) {
        d.AddNode(id, tmpl, x, y);
        auto* n = d.GetNode(id);
        if (n) n->titleOverride = title;
    };

    // ===== Place nodes (positions approximate the reference layout) =====

    // Top row inputs/outputs
    place("inV",   "fb_input", "[V] Elite Counter",  -260,   30);
    place("logT1", "fb_memo",  "[L] Training Log",     90,   10);
    place("kmTop", "fb_memo",  "[M] Elite km",        330,   10);
    place("o916",  "fb_analog","O916",                550,   10);
    place("logT2", "fb_memo",  "[L] Training Log",    780,   10);

    // Pulse generator + counter top-mid
    place("sek",   "fb_pulse",   "Sekunden",           200, 130);
    place("zeit",  "fb_counter", "Zeit in Sekunden",   420, 130);

    // Top-right math chain
    place("o919",  "fb_div",     "O919",               770, 110);
    place("o923",  "fb_int",     "O923",               940, 110);
    place("cMin",  "fb_const",   "[C] Sek. pro Min.=60", 600, 260);

    // Left column inputs / trainingsreset
    place("tres",  "fb_delayed", "Trainingsreset",     30, 280);
    place("inI",   "fb_input",   "[I] Reed Elite",   -260, 460);

    // O901 UND gate
    place("o901",  "fb_and",     "O901",               60, 420);

    // Kalibrierung pulse generator
    place("kalib", "fb_pulse",   "Kalibrierung",      -110, 590);

    // Tageszähler counter (mid-bottom)
    place("tag",   "fb_counter", "Tageszaehler",      400, 480);
    place("cHr",   "fb_const",   "[C] Sek. pro Stunde=3600", 380, 280);

    // Tacho energy counter (the central beast)
    place("tacho", "fb_energy",  "Tacho",             280, 360);

    // Constants stack (middle column)
    place("cF1",   "fb_const",   "[C] Faktor=1000",            550, 380);
    place("cU1",   "fb_const",   "[C] Umfang m=2,099",         550, 410);
    place("cK1",   "fb_const",   "[C] Korrektur=1,040",        550, 440);
    place("cKkmh1","fb_const",   "[C] Korrektur=1,040",        550, 580);
    place("cFkmh1","fb_const",   "[C] Faktor km/h=3,600",      550, 610);
    place("cKkmh2","fb_const",   "[C] Korrektur=1,040",        550, 730);
    place("cFkmh2","fb_const",   "[C] Faktor km/h=3,600",      550, 760);

    // Right column - formulas
    place("o913",  "fb_formula", "O913",               770, 220);
    place("o898a", "fb_formula", "O898",               770, 380);
    place("o898b", "fb_formula", "O898",               770, 560);
    place("o899a", "fb_formula", "O899",               770, 720);
    place("o899b", "fb_formula", "O899",               770, 860);

    // Right far-right Ganzzahl + status sinks
    place("o922",  "fb_int",     "O922",              1000, 820);

    place("stDur", "fb_status",  "Elite Dauer",       1000, 180);
    place("stDur2","fb_status",  "Elite Durchschnitt",1000, 240);
    place("kmM",   "fb_memo",    "Elite km",          1000, 320);
    place("stKM",  "fb_status",  "Elite Kilometer",   1000, 380);
    place("stSpeed","fb_status", "Geschwindigkeit",   1000, 500);
    place("stWk",  "fb_status",  "Kilometer diese Woche", 1000, 600);
    place("stTot", "fb_status",  "Kilometer Gesamt",  1000, 880);

    // Bottom-left supporting constants and o897
    place("o897",   "fb_formula", "O897",            150, 760);
    place("cFb1",   "fb_const",   "[C] Faktor km/h=3,600", -50, 760);
    place("cUb1",   "fb_const",   "[C] Umfang m=2,099",    -50, 790);
    place("cFakt1", "fb_const",   "[C] Faktor=1000",       -50, 820);
    place("cAnz1",  "fb_const",   "[C] Anzahl Magnete=1",  -50, 850);
    place("cMit1",  "fb_const",   "[C] Mittlung=60",        90, 900);

    // ===== Links =====
    auto link = [&](const std::string& id,
                     const std::string& s, const std::string& ss,
                     const std::string& t, const std::string& ts) {
        CompositorLink l;
        l.id = id;
        l.sourceNodeId = s; l.sourceSocketId = ss;
        l.targetNodeId = t; l.targetSocketId = ts;
        l.style = LinkStyle::Step;
        l.lineColorOverride = Color(160, 30, 30, 220);   // dark red, matches reference
        l.lineWidth = 1.2;
        d.AddLink(l);
    };

    // Top row connectivity
    link("LD1",  "inV",   "out", "logT1", "AI");
    link("LD2",  "o916",  "AQ",  "logT2", "AI");
    link("LD3",  "o916",  "AQ",  "kmTop", "AI");

    // Sekunden -> Zeit in Sekunden
    link("LD4",  "sek",   "Q",   "zeit",  "Tr");
    link("LD5",  "tres",  "Q",   "zeit",  "R");

    // Zeit AQ fan-out
    link("LD6",  "zeit",  "AQ",  "o916",  "AI");
    link("LD7",  "zeit",  "AQ",  "o919",  "AI1");
    link("LD8",  "cMin",  "out", "o919",  "AI2");
    link("LD9",  "o919",  "AQ",  "o923",  "AI1");
    link("LD10", "zeit",  "AQ",  "o913",  "AI1");
    link("LD11", "cHr",   "out", "o913",  "AI2");

    // O913 -> Status outputs
    link("LD12", "o913",  "AQ",  "stDur",  "AI1");
    link("LD13", "o913",  "TQ",  "stDur2", "AI1");

    // Reed Elite -> O901 AND -> Tacho Tr
    link("LD14", "inI",   "out", "o901",  "I1");
    link("LD15", "kalib", "Q",   "o901",  "I2");
    link("LD16", "o901",  "Q",   "tacho", "Tr");

    // Tageszähler driven by Tacho
    link("LD17", "tacho", "Q",   "tag",   "Tr");
    link("LD18", "tres",  "Q",   "tag",   "R");
    link("LD19", "tag",   "AQ",  "o898a", "AI4");

    // O898 fed by constants
    link("LD20", "cF1",   "out", "o898a", "AI1");
    link("LD21", "cU1",   "out", "o898a", "AI2");
    link("LD22", "cK1",   "out", "o898a", "AI3");
    link("LD23", "o898a", "AQ",  "kmM",   "AI");
    link("LD24", "o898a", "TQ",  "stKM",  "AI1");

    // Tacho mid outputs -> O898b -> Speed
    link("LD25", "tacho", "AQ1", "o898b", "AI4");
    link("LD26", "cKkmh1","out", "o898b", "AI1");
    link("LD27", "cFkmh1","out", "o898b", "AI2");
    link("LD28", "o898b", "AQ",  "stSpeed","AI1");

    // Tacho lower -> O899 chain -> weekly km + Ganzzahl
    link("LD29", "tacho", "AQ5", "o899a", "AI4");
    link("LD30", "o899a", "AQ",  "stWk",  "AI1");
    link("LD31", "tacho", "AQ8", "o899b", "AI4");
    link("LD32", "cKkmh2","out", "o899b", "AI1");
    link("LD33", "cFkmh2","out", "o899b", "AI2");
    link("LD34", "o899b", "AQ",  "o922",  "AI1");
    link("LD35", "o922",  "AQ",  "stTot", "AI1");

    // O897 chain (bottom-left)
    link("LD36", "cFb1",  "out", "o897",  "AI1");
    link("LD37", "cUb1",  "out", "o897",  "AI2");
    link("LD38", "cFakt1","out", "o897",  "AI3");
    link("LD39", "cAnz1", "out", "o897",  "AI4");
    link("LD40", "o897",  "AQ",  "tacho", "P");
    link("LD41", "cMit1", "out", "tacho", "V");

    d.SetHistoryRecording(true);
    d.FitView(40.0);
}

// ----- MARKETING FUNNEL (donut-ring cards via customPreviewRenderer) --------

void BuildMarketingFunnel(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    CompositorDiagramStyle st = d.GetStyle();
    st.backgroundColor = Color(235, 237, 240, 255);
    st.gridColor       = Color(225, 228, 235, 255);
    st.showGrid = false;
    d.SetStyle(st);

    // Stage colors (top header bar)
    const Color STAGE_EXEC (76, 175, 80, 255);    // green
    const Color STAGE_INTG (255, 152, 0, 255);    // orange
    const Color STAGE_PREP (130, 130, 130, 255);  // gray
    const Color RING_BLUE_PURPLE(120, 100, 220, 255);

    // Donut chart preview renderer. Reads percentage/glyph/count/ring color
    // from the node's paramValues map (set per-node via SetParam* below).
    auto donutDraw = [](IRenderContext* ctx, const Rect2Df& pb,
                         const CompositorNode& node) {
        auto findP = [&node](const std::string& id) -> const ParamValue* {
            auto it = node.paramValues.find(id);
            return (it == node.paramValues.end()) ? nullptr : &it->second;
        };
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(pb);

        double pct = 0.0;
        std::string glyph = "?";
        std::string count = "0";
        Color ringCol = Color(120, 100, 220, 255);
        if (auto p = findP("pct"))   pct = p->number;
        if (auto p = findP("glyph")) glyph = p->text;
        if (auto p = findP("count")) count = p->text;
        if (auto p = findP("ringR")) ringCol.r = static_cast<uint8_t>(p->number);
        if (auto p = findP("ringG")) ringCol.g = static_cast<uint8_t>(p->number);
        if (auto p = findP("ringB")) ringCol.b = static_cast<uint8_t>(p->number);

        double cx = pb.x + pb.width  * 0.5;
        double cy = pb.y + pb.height * 0.5 - 8.0;
        double R  = std::min(pb.width, pb.height) * 0.34;
        // Background ring
        ctx->SetStrokePaint(Color(225, 228, 232, 255));
        ctx->SetStrokeWidth(8.0);
        ctx->DrawArc(cx, cy, R, 0.0, 2.0 * 3.14159265358979);
        // Filled portion (clockwise from top)
        double startA = -3.14159265358979 * 0.5;
        double endA   = startA + 2.0 * 3.14159265358979 * (pct / 100.0);
        ctx->SetStrokePaint(ringCol);
        ctx->SetStrokeWidth(8.0);
        ctx->DrawArc(cx, cy, R, startA, endA);

        // Centered icon glyph
        ctx->SetFontFamily("Sans");
        ctx->SetFontSize(22);
        ctx->SetTextPaint(Color(70, 90, 130, 255));
        Size2Di gd = ctx->GetTextLineDimensions(glyph);
        ctx->DrawText(glyph, Point2Df(cx - gd.width * 0.5, cy - gd.height * 0.5));

        // Footer: count (left) + percentage (right)
        ctx->SetFontSize(11);
        ctx->SetTextPaint(Color(60, 70, 90, 255));
        double footY = pb.y + pb.height - 18.0;
        ctx->DrawText("o " + count, Point2Df(pb.x + 8.0, footY));
        std::ostringstream pstr;
        pstr << "v " << static_cast<int>(pct) << " %";
        std::string pStr = pstr.str();
        Size2Di pd = ctx->GetTextLineDimensions(pStr);
        ctx->DrawText(pStr, Point2Df(pb.x + pb.width - 8.0 - pd.width, footY));
    };

    // Template factory: produces a "funnel card" with a colored header (stage
    // name), a single AI input on the left, a single output on the right,
    // and a preview slot owned by donutDraw.
    auto mkCard = [&](const std::string& id, const std::string& stage,
                       const Color& header) {
        CompositorNodeTemplate t;
        t.id = id;
        t.title = stage;
        t.category = "Funnel";
        t.headerColor = header;
        t.headerTextColor = Color(255, 255, 255, 255);
        t.bodyColor = Color(255, 255, 255, 255);
        t.borderColor = Color(190, 195, 205, 255);
        t.borderWidth = 1.0;
        t.cornerRadius = 8.0;
        t.defaultWidth = 130.0;
        t.hasPreview = true;
        t.previewHeight = 130.0;
        t.customPreviewRenderer = donutDraw;

        CompositorSocketSpec in;
        in.id = "in"; in.label = ""; in.type = SocketDataType::Scalar;
        in.maxConnections = 1;
        t.inputs.push_back(in);

        CompositorSocketSpec out;
        out.id = "out"; out.label = ""; out.type = SocketDataType::Scalar;
        t.outputs.push_back(out);

        // Parameter declarations - all NoWidget; values are set per-node and
        // read by the preview renderer.
        auto numParam = [&](const std::string& pid) {
            CompositorParamSpec p;
            p.id = pid; p.label = "";
            p.widget = ParamWidgetKind::NoWidget;
            p.valueKind = ParamValueKind::Number;
            t.params.push_back(p);
        };
        auto txtParam = [&](const std::string& pid) {
            CompositorParamSpec p;
            p.id = pid; p.label = "";
            p.widget = ParamWidgetKind::NoWidget;
            p.valueKind = ParamValueKind::Text;
            t.params.push_back(p);
        };
        numParam("pct");
        numParam("ringR");
        numParam("ringG");
        numParam("ringB");
        txtParam("glyph");
        txtParam("count");

        d.RegisterTemplate(t);
    };

    mkCard("funn_exec", "Execution",   STAGE_EXEC);
    mkCard("funn_intg", "Integration", STAGE_INTG);
    mkCard("funn_prep", "Preparation", STAGE_PREP);

    // Helper to place + set the per-node params.
    auto place = [&](const std::string& id, const std::string& tmpl,
                      double x, double y,
                      const std::string& glyph, const std::string& count,
                      double pct, const Color& ringCol) {
        d.AddNode(id, tmpl, x, y);
        d.SetParamNumber(id, "pct", pct);
        d.SetParamNumber(id, "ringR", ringCol.r);
        d.SetParamNumber(id, "ringG", ringCol.g);
        d.SetParamNumber(id, "ringB", ringCol.b);
        d.SetParamText  (id, "glyph", glyph);
        d.SetParamText  (id, "count", count);
    };

    Color RING_PURP_TEAL(60, 180, 200, 255);
    Color RING_PINK_PURPLE(180, 60, 180, 255);
    Color RING_BLUE_PURPLE2(120, 100, 220, 255);
    Color RING_ORANGE(250, 160, 60, 255);

    place("n_email",  "funn_exec", 60,  140, "M",  "2000", 10,  RING_PURP_TEAL);
    place("n_stats",  "funn_exec", 360,  20, "S",  "150",  73,  RING_BLUE_PURPLE2);
    place("n_mail2",  "funn_exec", 360, 260, "@",  "100",  40,  RING_PURP_TEAL);
    place("n_intg",   "funn_intg", 660,  20, "U",  "110",  54,  RING_ORANGE);
    place("n_sms",    "funn_exec", 660, 260, "T",  "30",   20,  RING_PINK_PURPLE);
    place("n_prep",   "funn_prep", 960, 140, "C",  "30",   0,   RING_ORANGE);

    auto linkLabeled = [&](const std::string& id,
                            const std::string& s, const std::string& t,
                            const std::string& label) {
        CompositorLink l;
        l.id = id;
        l.sourceNodeId = s; l.sourceSocketId = "out";
        l.targetNodeId = t; l.targetSocketId = "in";
        l.style = LinkStyle::Step;
        l.lineColorOverride = Color(160, 165, 175, 255);
        l.lineWidth = 1.2;
        l.label = label;
        l.labelBgColor = Color(255, 255, 255, 240);
        l.labelTextColor = Color(80, 90, 110, 255);
        d.AddLink(l);
    };
    linkLabeled("f1", "n_email", "n_stats", "Accepted: 200");
    linkLabeled("f2", "n_email", "n_mail2", "Opened: 500");
    linkLabeled("f3", "n_stats", "n_intg",  "Submitted: 110");
    linkLabeled("f4", "n_mail2", "n_sms",   "Accepted: 40");
    linkLabeled("f5", "n_intg",  "n_prep",  "Visited: 60");
    linkLabeled("f6", "n_sms",   "n_prep",  "Accepted: 6");

    d.SetHistoryRecording(true);
    d.FitView(40.0);
}

// =============================================================================
// TAB 2: SUBGRAPH / GROUPS
// =============================================================================

void BuildSubgraphScene(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    d.RegisterDefaultGroupTemplate("group", "Group");

    CompositorNodeTemplate node;
    node.id = "process";
    node.title = "Process";
    node.headerColor = Color(60, 100, 160, 255);
    node.bodyColor   = Color(45, 50, 60, 240);
    node.borderColor = Color(20, 20, 20, 255);
    node.defaultWidth = 160.0;
    {
        CompositorSocketSpec s;
        s.id = "in"; s.label = "in"; s.type = SocketDataType::Image; s.maxConnections = 1;
        node.inputs.push_back(s);
    }
    {
        CompositorSocketSpec s;
        s.id = "out"; s.label = "out"; s.type = SocketDataType::Image;
        node.outputs.push_back(s);
    }
    d.RegisterTemplate(node);

    // Three top-level nodes that we'll group.
    d.AddNode("src",  "process", 100, 100);
    d.AddNode("blur", "process", 320, 100);
    d.AddNode("tone", "process", 540, 100);

    d.AddNode("sink", "process", 1000, 300);

    d.AddLink("g1", "src",  "out", "blur", "in");
    d.AddLink("g2", "blur", "out", "tone", "in");

    // Form a group around src/blur/tone.
    d.Group("preprocess", "group", {"src", "blur", "tone"});
    auto* g = d.GetNode("preprocess");
    if (g) g->titleOverride = "Preprocess";

    // Bottom group with two more nodes.
    d.AddNode("scale",  "process", 100, 450);
    d.AddNode("output", "process", 320, 450);
    d.AddLink("g3", "scale", "out", "output", "in");
    d.Group("postprocess", "group", {"scale", "output"});
    auto* g2 = d.GetNode("postprocess");
    if (g2) g2->titleOverride = "Postprocess";

    // Link from a child in the first group to a child in the second.
    d.AddLink("g4", "tone", "out", "scale", "in");
    // And to the standalone sink.
    d.AddLink("g5", "output", "out", "sink", "in");

    d.SetHistoryRecording(true);
    d.FitView(40.0);
}

// =============================================================================
// TAB 3: VISUAL SCRIPTING
// =============================================================================

void BuildVisualScripting(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    auto common = [](CompositorNodeTemplate& t, const std::string& id,
                      const std::string& title, const Color& header) {
        t.id = id;
        t.title = title;
        t.category = "Scripting";
        t.headerColor = header;
        t.bodyColor = Color(45, 45, 55, 245);
        t.borderColor = Color(20, 20, 20, 255);
        t.cornerRadius = 4.0;
        t.defaultWidth = 200.0;
    };
    auto exec  = [](CompositorSocketSpec& s) {
        s.type = SocketDataType::Trigger;  // White exec pin
    };

    CompositorNodeTemplate onStart;
    common(onStart, "on_start", "OnStart", Color(120, 50, 180, 255));
    {
        CompositorSocketSpec s; s.id = "exec"; s.label = "exec"; exec(s);
        onStart.outputs.push_back(s);
    }
    d.RegisterTemplate(onStart);

    CompositorNodeTemplate branch;
    common(branch, "branch", "Branch", Color(160, 80, 60, 255));
    {
        CompositorSocketSpec s; s.id = "exec_in"; s.label = "exec"; exec(s); s.maxConnections = 1;
        branch.inputs.push_back(s);
    }
    {
        CompositorParamSpec p; p.id = "cond"; p.label = "Condition";
        p.hasInputSocket = true; p.inputType = SocketDataType::Boolean;
        p.widget = ParamWidgetKind::Checkbox; p.valueKind = ParamValueKind::Boolean;
        branch.params.push_back(p);
    }
    {
        CompositorSocketSpec s; s.id = "true";  s.label = "True";  exec(s);
        branch.outputs.push_back(s);
        s.id = "false"; s.label = "False"; exec(s);
        branch.outputs.push_back(s);
    }
    d.RegisterTemplate(branch);

    CompositorNodeTemplate compare;
    common(compare, "compare", "Compare", Color(60, 140, 80, 255));
    {
        CompositorSocketSpec s; s.id = "a"; s.label = "A"; s.type = SocketDataType::Scalar; s.maxConnections = 1;
        compare.inputs.push_back(s);
        s.id = "b"; s.label = "B";
        compare.inputs.push_back(s);
    }
    {
        CompositorParamSpec p; p.id = "op"; p.label = "Op";
        p.widget = ParamWidgetKind::Dropdown; p.valueKind = ParamValueKind::Unset;
        p.choices = {">", ">=", "==", "<=", "<", "!="};
        p.hasInputSocket = false;
        compare.params.push_back(p);
    }
    {
        CompositorSocketSpec s; s.id = "result"; s.label = "result";
        s.type = SocketDataType::Boolean;
        compare.outputs.push_back(s);
    }
    d.RegisterTemplate(compare);

    CompositorNodeTemplate print;
    common(print, "print", "Print", Color(60, 60, 60, 255));
    {
        CompositorSocketSpec s; s.id = "exec_in"; s.label = "exec"; exec(s); s.maxConnections = 1;
        print.inputs.push_back(s);
    }
    {
        CompositorParamSpec p; p.id = "msg"; p.label = "Message";
        p.hasInputSocket = true; p.inputType = SocketDataType::String;
        p.widget = ParamWidgetKind::TextInput; p.valueKind = ParamValueKind::Text;
        p.defaultText = "Hello";
        print.params.push_back(p);
    }
    {
        CompositorSocketSpec s; s.id = "exec_out"; s.label = "exec"; exec(s);
        print.outputs.push_back(s);
    }
    d.RegisterTemplate(print);

    // A small scalar source so we can feed Compare without a popup editor.
    CompositorNodeTemplate scalar;
    common(scalar, "scalar", "Scalar", Color(60, 120, 160, 255));
    {
        CompositorParamSpec p; p.id = "value"; p.label = "Value";
        p.widget = ParamWidgetKind::Slider; p.valueKind = ParamValueKind::Number;
        p.numMin = 0; p.numMax = 100; p.numStep = 1; p.defaultNumber = 50;
        scalar.params.push_back(p);
    }
    {
        CompositorSocketSpec s; s.id = "out"; s.label = "value"; s.type = SocketDataType::Scalar;
        scalar.outputs.push_back(s);
    }
    d.RegisterTemplate(scalar);

    d.AddNode("start",   "on_start", 60, 220);
    d.AddNode("sA",      "scalar",   60, 80);
    d.AddNode("sB",      "scalar",   60, 400);
    d.AddNode("cmp",     "compare",  340, 220);
    d.AddNode("branch1", "branch",   620, 220);
    d.AddNode("printT",  "print",    900, 100);
    d.AddNode("printF",  "print",    900, 320);

    d.AddLink("v1", "sA",   "out",      "cmp",     "a");
    d.AddLink("v2", "sB",   "out",      "cmp",     "b");
    d.AddLink("v3", "cmp",  "result",   "branch1", "cond");
    d.AddLink("v4", "start","exec",     "branch1", "exec_in");
    d.AddLink("v5", "branch1","true",   "printT",  "exec_in");
    d.AddLink("v6", "branch1","false",  "printF",  "exec_in");
    // Set messages.
    d.SetParamText("printT", "msg", "A is greater");
    d.SetParamText("printF", "msg", "A is not greater");

    d.SetHistoryRecording(true);
    d.FitView(40.0);
}

// =============================================================================
// TAB 4: FEATURE PLAYGROUND
// =============================================================================

void BuildPlayground(UltraCanvasCompositorDiagram& d) {
    d.Clear();
    d.ClearHistory();
    d.SetHistoryRecording(false);

    // Three quick templates so Tab-palette has variety.
    auto mk = [&](const std::string& id, const std::string& title,
                   const Color& header, SocketDataType inOutType) {
        CompositorNodeTemplate t;
        t.id = id;
        t.title = title;
        t.category = "Demo";
        t.headerColor = header;
        t.bodyColor = Color(45, 45, 55, 245);
        t.borderColor = Color(20, 20, 20, 255);
        t.defaultWidth = 160.0;
        {
            CompositorSocketSpec s; s.id = "in"; s.label = "in"; s.type = inOutType; s.maxConnections = 1;
            t.inputs.push_back(s);
        }
        {
            CompositorSocketSpec s; s.id = "out"; s.label = "out"; s.type = inOutType;
            t.outputs.push_back(s);
        }
        {
            CompositorParamSpec p; p.id = "v"; p.label = "Strength";
            p.widget = ParamWidgetKind::Slider; p.valueKind = ParamValueKind::Number;
            p.numMin = 0; p.numMax = 1; p.numStep = 0.05; p.defaultNumber = 0.5;
            t.params.push_back(p);
        }
        d.RegisterTemplate(t);
    };
    mk("pg_src",  "Source",  Color(80, 130, 200, 255), SocketDataType::Image);
    mk("pg_blur", "Blur",    Color(120, 80, 200, 255), SocketDataType::Image);
    mk("pg_tint", "Tint",    Color(200, 100, 80, 255), SocketDataType::Image);
    mk("pg_out",  "Output",  Color(60, 160, 100, 255), SocketDataType::Image);
    d.RegisterDefaultGroupTemplate("group", "Group");

    // Drop a couple of nodes so the canvas isn't empty.
    d.AddNode("p_src",  "pg_src",  100, 200);
    d.AddNode("p_blur", "pg_blur", 360, 200);
    d.AddNode("p_out",  "pg_out",  640, 200);
    d.AddLink("p1", "p_src",  "out", "p_blur", "in");
    d.AddLink("p2", "p_blur", "out", "p_out",  "in");

    d.SetHistoryRecording(true);
    d.FitView(40.0);
}

// =============================================================================
// SUB-SCENARIO SELECTOR (small button bar inside tab 1)
// =============================================================================

struct ImageTabState {
    std::shared_ptr<UltraCanvasCompositorDiagram> diagram;
    std::shared_ptr<UltraCanvasLabel> statusLabel;
};

std::shared_ptr<UltraCanvasButton> MakeSubButton(
        const std::string& id, int x, int y, int w, int h,
        const std::string& text, std::function<void()> onClick) {
    auto b = std::make_shared<UltraCanvasButton>(id, x, y, w, h);
    b->SetText(text);
    b->SetBackgroundColor(Color(50, 60, 80, 255));
    b->onClick = std::move(onClick);
    return b;
}

} // anonymous namespace

// =============================================================================
// PUBLIC ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateCompositorDiagramExamples() {
    const int CONTAINER_W = 1280;
    const int CONTAINER_H = 820;
    const int PAD = 20;
    const int TITLE_H = 32;
    const int SUBTITLE_H = 36;
    const int TABS_H = 600;
    const int BTNBAR_H = 36;
    const int STATUS_H = 26;

    auto root = std::make_shared<UltraCanvasContainer>(
        "compDemoRoot", 0, 0, CONTAINER_W, CONTAINER_H);
    root->SetBackgroundColor(Color(248, 248, 250, 255));
    // Root is a CSS flex column: title / subtitle / tabs (grows) / button bar / status.
    root->SetPadding(PAD);
    root->layout.SetFlexColumn().SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Add a flex child with the given grow factor, stretched on the cross axis.
    auto addFlex = [](const std::shared_ptr<UltraCanvasContainer>& parent,
                      const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    };

    auto title = std::make_shared<UltraCanvasLabel>(
        "compTitle", 0, 0, 0, TITLE_H);
    title->SetText("Compositor Diagram");
    title->SetFontSize(20);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 60, 130, 255));
    addFlex(root, title, 0);

    auto subtitle = std::make_shared<UltraCanvasLabel>(
        "compSubtitle", 0, 0, 0, SUBTITLE_H);
    subtitle->SetText(
        "Comprehensive showcase: typed sockets, embedded widgets, groups, "
        "history, edge labels. Use the toolbar to toggle features, undo/redo, "
        "copy/paste. Tab key opens the node palette (host-rendered).");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(110, 110, 120, 255));
    subtitle->SetWrap(TextWrap::WrapWord);
    addFlex(root, subtitle, 0);

    // ---- TabbedContainer (grows to fill the middle) ----
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "compTabs", 0, 0, CONTAINER_W - 2 * PAD, TABS_H);
    tabs->SetTabStyle(TabStyle::Rounded);

    const int INNER_W = CONTAINER_W - 2 * PAD - 8;
    const int INNER_H = TABS_H - 40;

    auto statusLabel = std::make_shared<UltraCanvasLabel>(
        "compStatus", 0, 0, 0, STATUS_H);
    statusLabel->SetText("Ready - Wheel zoom, drag to pan, shift+drag for selection box. "
                         "Tab in canvas opens palette.");
    statusLabel->SetFontSize(10);
    statusLabel->SetTextColor(Color(90, 90, 100, 255));

    // ---- Tab 1: Recreate Images ----
    const int SUBBAR_H = 32;
    auto tab1 = std::make_shared<UltraCanvasContainer>(
        "compTab1", 0, 0, INNER_W, INNER_H);
    tab1->SetBackgroundColor(Color(255, 255, 255, 255));
    tab1->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto diag1 = CreateCompositorDiagram("compDiag1", 0, 0, INNER_W, INNER_H - SUBBAR_H);
    BuildLayerCompositor(*diag1);
    auto state1 = std::make_shared<ImageTabState>();
    state1->diagram = diag1;
    state1->statusLabel = statusLabel;

    // Sub-scenario button row (flex row at the top of the tab).
    auto subBar1 = std::make_shared<UltraCanvasContainer>("compTab1Sub", 0, 0, 0, SUBBAR_H);
    subBar1->layout.SetFlexRow().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    subBar1->size.height = CSSLayout::Dimension::Px(SUBBAR_H);
    auto subBtn = [&](const std::string& id, const std::string& text,
                       std::function<void(UltraCanvasCompositorDiagram&)> populate) {
        const int W = 130;
        auto b = MakeSubButton(id, 0, 0, W, SUBBAR_H - 4, text,
            [state1, populate, text]() {
                if (state1->diagram) {
                    populate(*state1->diagram);
                    state1->diagram->RequestRedraw();
                }
                if (state1->statusLabel) {
                    state1->statusLabel->SetText("Sub-scenario: " + text);
                }
            });
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        subBar1->AddChild(b);
    };
    subBtn("sub_layer", "Layer Compositor", BuildLayerCompositor);
    subBtn("sub_brush", "Brush Editor",     BuildBrushEditor);
    subBtn("sub_kpi",   "Music KPI",        BuildMusicKPI);
    subBtn("sub_port",  "Portfolio Tree",   BuildPortfolioTree);
    subBtn("sub_orm",   "ORM Modeler",      BuildOrmModeler);
    subBtn("sub_logic", "Logic Diagram",    BuildLogicDiagram);
    subBtn("sub_funn",  "Marketing Funnel", BuildMarketingFunnel);

    addFlex(tab1, subBar1, 0);
    addFlex(tab1, diag1, 1);

    // ---- Tab 2: Subgraph / Groups ----
    auto tab2 = std::make_shared<UltraCanvasContainer>(
        "compTab2", 0, 0, INNER_W, INNER_H);
    tab2->SetBackgroundColor(Color(255, 255, 255, 255));
    tab2->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    auto diag2 = CreateCompositorDiagram("compDiag2", 0, 0, INNER_W, INNER_H);
    BuildSubgraphScene(*diag2);
    addFlex(tab2, diag2, 1);

    // ---- Tab 3: Visual Scripting ----
    auto tab3 = std::make_shared<UltraCanvasContainer>(
        "compTab3", 0, 0, INNER_W, INNER_H);
    tab3->SetBackgroundColor(Color(255, 255, 255, 255));
    tab3->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    auto diag3 = CreateCompositorDiagram("compDiag3", 0, 0, INNER_W, INNER_H);
    BuildVisualScripting(*diag3);
    addFlex(tab3, diag3, 1);

    // ---- Tab 4: Feature Playground ----
    auto tab4 = std::make_shared<UltraCanvasContainer>(
        "compTab4", 0, 0, INNER_W, INNER_H);
    tab4->SetBackgroundColor(Color(255, 255, 255, 255));
    tab4->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    auto diag4 = CreateCompositorDiagram("compDiag4", 0, 0, INNER_W, INNER_H);
    BuildPlayground(*diag4);
    diag4->SetMinimapVisible(true);
    diag4->SetControlsVisible(true);
    diag4->SetSnapToGrid(true);
    diag4->SetAlignmentGuidesEnabled(true);
    addFlex(tab4, diag4, 1);

    tabs->AddTab("Recreate Images", tab1);
    tabs->AddTab("Subgraph / Groups", tab2);
    tabs->AddTab("Visual Scripting", tab3);
    tabs->AddTab("Feature Playground", tab4);
    tabs->SetActiveTab(0);

    addFlex(root, tabs, 1);

    // ---- Active diagram tracker ----
    auto activeDiagram = std::make_shared<std::shared_ptr<UltraCanvasCompositorDiagram>>(diag1);
    std::vector<std::shared_ptr<UltraCanvasCompositorDiagram>> diags = {diag1, diag2, diag3, diag4};

    tabs->onTabChange = [activeDiagram, diags, statusLabel](int /*oldIdx*/, int newIdx) {
        if (newIdx >= 0 && newIdx < (int)diags.size()) {
            *activeDiagram = diags[newIdx];
            if (statusLabel) {
                const char* names[] = {
                    "Recreate Images", "Subgraph / Groups",
                    "Visual Scripting", "Feature Playground"
                };
                statusLabel->SetText(std::string("Tab: ") + names[newIdx]);
            }
        }
    };

    // ---- Button bar (global actions + toggles) — a flex row ----
    auto buttonBar = std::make_shared<UltraCanvasContainer>("compBtnBar", 0, 0, 0, BTNBAR_H);
    buttonBar->layout.SetFlexRow().SetFlexGap(4)
              .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
              .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonBar->size.height = CSSLayout::Dimension::Px(BTNBAR_H);
    const int B_W = 70;
    const int TOG_W = 90;
    auto addBtn = [&](const std::string& id, const std::string& text, int w,
                       Color bg, std::function<void()> click) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, w, BTNBAR_H - 4);
        b->SetText(text);
        b->SetBackgroundColor(bg);
        b->onClick = std::move(click);
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        buttonBar->AddChild(b);
    };
    Color act(80, 140, 220, 255);    // action buttons
    Color tog(80, 110, 80, 255);     // toggles
    Color destr(190, 90, 90, 255);   // destructive

    addBtn("bUndo", "Undo", B_W, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->Undo();
    });
    addBtn("bRedo", "Redo", B_W, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->Redo();
    });
    addBtn("bCut",  "Cut", B_W, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->Cut();
    });
    addBtn("bCopy", "Copy", B_W, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->Copy();
    });
    addBtn("bPaste","Paste",B_W, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->Paste();
    });
    addBtn("bDup",  "Duplicate", B_W + 16, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->Duplicate();
    });
    addBtn("bGroup", "Group", B_W, act, [activeDiagram]() {
        if (!*activeDiagram) return;
        auto sel = (*activeDiagram)->GetSelectedNodeIds();
        if (sel.empty()) return;
        // Try the registered group template ids (different tabs use different ones).
        std::string tmplId;
        for (const auto& tid : {"group"}) {
            if ((*activeDiagram)->GetTemplate(tid)) { tmplId = tid; break; }
        }
        if (tmplId.empty()) {
            (*activeDiagram)->RegisterDefaultGroupTemplate("group", "Group");
            tmplId = "group";
        }
        (*activeDiagram)->Group("", tmplId, sel);
    });
    addBtn("bUngroup", "Ungroup", B_W + 6, act, [activeDiagram]() {
        if (!*activeDiagram) return;
        auto sel = (*activeDiagram)->GetSelectedNodeIds();
        for (const auto& id : sel) (*activeDiagram)->Ungroup(id);
    });
    addBtn("bDel", "Delete", B_W, destr, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->DeleteSelected();
    });
    addBtn("bFit", "Fit View", B_W + 4, act, [activeDiagram]() {
        if (*activeDiagram) (*activeDiagram)->FitView(40.0);
    });

    // Toggles
    addBtn("tMini", "Minimap", TOG_W, tog, [activeDiagram, statusLabel]() {
        if (!*activeDiagram) return;
        auto cfg = (*activeDiagram)->GetMinimapConfig();
        (*activeDiagram)->SetMinimapVisible(!cfg.visible);
        if (statusLabel) statusLabel->SetText(std::string("Minimap: ") +
            (!cfg.visible ? "on" : "off"));
    });
    addBtn("tCtrl", "Controls", TOG_W, tog, [activeDiagram, statusLabel]() {
        if (!*activeDiagram) return;
        auto cfg = (*activeDiagram)->GetControlsConfig();
        (*activeDiagram)->SetControlsVisible(!cfg.visible);
        if (statusLabel) statusLabel->SetText(std::string("Controls: ") +
            (!cfg.visible ? "on" : "off"));
    });
    addBtn("tSnap", "Snap", B_W, tog, [activeDiagram, statusLabel]() {
        if (!*activeDiagram) return;
        bool now = !(*activeDiagram)->IsSnapToGridEnabled();
        (*activeDiagram)->SetSnapToGrid(now);
        if (statusLabel) statusLabel->SetText(std::string("Snap: ") + (now ? "on" : "off"));
    });
    addBtn("tAlign", "Align Lines", TOG_W + 12, tog, [activeDiagram, statusLabel]() {
        if (!*activeDiagram) return;
        bool now = !(*activeDiagram)->IsAlignmentGuidesEnabled();
        (*activeDiagram)->SetAlignmentGuidesEnabled(now);
        if (statusLabel) statusLabel->SetText(std::string("Alignment guides: ") + (now ? "on" : "off"));
    });

    addFlex(root, buttonBar, 0);

    // ---- Per-diagram event wiring (status updates on selection / history) ----
    for (auto& d : diags) {
        std::weak_ptr<UltraCanvasLabel> sl = statusLabel;
        d->onSelectionChange = [sl](const std::vector<std::string>& nodes,
                                     const std::vector<std::string>& links) {
            if (auto s = sl.lock()) {
                std::ostringstream os;
                os << "Selection: " << nodes.size() << " node(s), "
                   << links.size() << " link(s)";
                s->SetText(os.str());
            }
        };
        d->onHistoryChange = [sl, d]() {
            if (auto s = sl.lock()) {
                std::ostringstream os;
                os << "History: undo " << d->GetUndoStackSize()
                   << " | redo " << d->GetRedoStackSize();
                s->SetText(os.str());
            }
        };
        d->onConnectionRejected = [sl](const CompositorSocketSpec& src,
                                         const CompositorSocketSpec& dst) {
            if (auto s = sl.lock()) {
                s->SetText("Connection rejected: socket types incompatible (" +
                           src.label + " -> " + dst.label + ")");
            }
        };
        d->onPaletteRequested = [sl, d](double wx, double wy) {
            if (auto s = sl.lock()) {
                std::ostringstream os;
                auto ids = d->SearchTemplates("");
                os << "Palette requested at (" << (int)wx << ", " << (int)wy
                   << ") - " << ids.size() << " templates available";
                s->SetText(os.str());
            }
        };
    }

    addFlex(root, statusLabel, 0);
    return root;
}

} // namespace UltraCanvas
