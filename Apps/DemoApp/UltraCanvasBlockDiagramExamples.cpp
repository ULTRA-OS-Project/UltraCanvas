// Apps/DemoApp/UltraCanvasBlockDiagramExamples.cpp
// Interactive block diagram demo - HVAC System (Flat 2D, Zoned Layout)
// Version: 2.2.2
// Last Modified: 2026-05-01
//
// Changelog 2.2.2:
//  - Reset button now also restores every node to its initial layout
//    position, not just the zoom and pan. Initial positions are captured
//    once during construction and copied into the button's lambda.
//
// Changelog 2.2.1:
//  - Air Mix Door moved up 10px (y: 470 -> 460) so it no longer touches
//    the bottom edge of the diagram canvas at default zoom.
//
// Changelog 2.2.0:
//  - Switched from Isometric3D to Flat render style for clean arrow termination
//  - Resolved overlapping nodes (air_mix and blower_ctrl were both at 320,340)
//  - Reorganized into three color-coded zones: Controls (gray), Refrigerant
//    Cycle (cool blue), Airflow / Heating (warm orange)
//  - Refrigerant cycle laid out as a closed rectangular loop on the upper half
//  - Airflow laid out as a linear left-to-right path on the lower half
//  - All connections use Orthogonal style; no crossings under or over blocks
//  - Increased canvas margins and inter-block spacing (min 40px gap)
//  - Replaced literal "\n" labels with single-line text where layout allows
 
#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasBlockDiagram.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"
 
namespace UltraCanvas {
 
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBlockDiagramExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("DiagramEditor", 0, 0, 900, 700);
    container->SetBackgroundColor(Color(245, 247, 250, 255));
 
    // -------------------------------------------------------------------------
    // Title and description
    // -------------------------------------------------------------------------
    auto title = std::make_shared<UltraCanvasLabel>("Title", 50, 20, 800, 35);
    title->SetText("HVAC System Block Diagram");
    title->SetFont("Arial", 22.0f, FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    container->AddChild(title);
 
    auto desc = std::make_shared<UltraCanvasLabel>("Desc", 50, 58, 800, 22);
    desc->SetText("Automotive climate control: refrigerant cycle (top), airflow path (bottom), control modules (left).");
    desc->SetFontSize(13);
    desc->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(desc);
 
    // -------------------------------------------------------------------------
    // Diagram canvas - flat 2D, no isometric depth
    // -------------------------------------------------------------------------
    auto diagram = CreateBlockDiagram("mainDiagram", 50, 95, 800, 520);
    diagram->SetBackgroundColor(Color(252, 252, 254, 255));
    diagram->SetGridVisible(true, 20.0f);
    diagram->SetGridColor(Color(238, 240, 244, 255));
    diagram->SetSnapToGrid(true);
 
    // FLAT 2D - eliminates the depth offset that caused arrowheads to land
    // on the front face of 3D blocks instead of the actual block edge.
    diagram->SetRenderStyle(UltraCanvasBlockDiagram::RenderStyle::Flat);
 
    // -------------------------------------------------------------------------
    // Zone color palette (soft, professional)
    // -------------------------------------------------------------------------
    // Control modules - neutral gray
    Color ctrlFill   = Color(225, 228, 234, 255);
    Color ctrlBorder = Color(120, 130, 145, 255);
    Color ctrlConn   = Color(120, 130, 145, 255);
 
    // Refrigerant / cooling cycle - cool blue
    Color coolFill   = Color(189, 215, 238, 255);
    Color coolBorder = Color( 70, 120, 170, 255);
    Color coolConn   = Color( 70, 120, 170, 255);
 
    // Airflow / heating - warm orange
    Color airFill    = Color(252, 220, 188, 255);
    Color airBorder  = Color(200, 110,  50, 255);
    Color airConn    = Color(200, 110,  50, 255);
 
    Color textColor  = Color( 25,  25,  35, 255);
    float connWidth  = 2.0f;
 
    // -------------------------------------------------------------------------
    // Layout grid (20px snap)
    //
    //   x:   40        200       360       520       660
    //        |---------|---------|---------|---------|
    //  y=80  [DriveClu] [Compres] -------> [Conden ]
    //                       ^                  |
    //  y=180 [CompCtrl]----+                   v
    //                                      [ExpVal]
    //                       +--------------------+
    //  y=280                |  [Evapor]<---------+
    //                       v
    //  y=380 [BlowCtrl] [Blower ] -> [Heater ] -> [ModeDoor]
    //                                    |           ^
    //  y=460                          [AirMix]------+
    // -------------------------------------------------------------------------
 
    // ===== Refrigerant cycle (closed loop, top) ==============================
    diagram->AddNode("compressor", BlockShape::Rectangle, "Compressor", 200, 80, 120, 60);
    diagram->SetNodeColor("compressor", coolFill, coolBorder);
    diagram->SetNodeTextColor("compressor", textColor);
 
    diagram->AddNode("condenser", BlockShape::Rectangle, "Condenser", 540, 80, 120, 60);
    diagram->SetNodeColor("condenser", coolFill, coolBorder);
    diagram->SetNodeTextColor("condenser", textColor);
 
    diagram->AddNode("expansion", BlockShape::Rectangle, "Expansion Valve", 540, 200, 120, 60);
    diagram->SetNodeColor("expansion", coolFill, coolBorder);
    diagram->SetNodeTextColor("expansion", textColor);
 
    diagram->AddNode("evaporator", BlockShape::Rectangle, "Evaporator", 200, 200, 120, 60);
    diagram->SetNodeColor("evaporator", coolFill, coolBorder);
    diagram->SetNodeTextColor("evaporator", textColor);
 
    // ===== Control modules (left column, gray) ===============================
    diagram->AddNode("drive_clutch", BlockShape::Rectangle, "Drive Clutch", 40, 80, 120, 60);
    diagram->SetNodeColor("drive_clutch", ctrlFill, ctrlBorder);
    diagram->SetNodeTextColor("drive_clutch", textColor);
 
    diagram->AddNode("comp_control", BlockShape::Rectangle, "Comp Control", 40, 200, 120, 60);
    diagram->SetNodeColor("comp_control", ctrlFill, ctrlBorder);
    diagram->SetNodeTextColor("comp_control", textColor);
 
    diagram->AddNode("blower_ctrl", BlockShape::Rectangle, "Blower Ctrl", 40, 380, 120, 60);
    diagram->SetNodeColor("blower_ctrl", ctrlFill, ctrlBorder);
    diagram->SetNodeTextColor("blower_ctrl", textColor);
 
    // ===== Airflow / heating row (bottom) ====================================
    diagram->AddNode("blower", BlockShape::Rectangle, "Blower", 200, 380, 120, 60);
    diagram->SetNodeColor("blower", airFill, airBorder);
    diagram->SetNodeTextColor("blower", textColor);
 
    // Evaporator already exists in the cycle; airflow re-enters it from below.
    // We route Blower -> Evaporator -> (cycle handles cooling) -> Heater Core.
 
    diagram->AddNode("heater", BlockShape::Rectangle, "Heater Core", 380, 380, 120, 60);
    diagram->SetNodeColor("heater", airFill, airBorder);
    diagram->SetNodeTextColor("heater", textColor);
 
    diagram->AddNode("air_mix", BlockShape::Rectangle, "Air Mix Door", 380, 460, 120, 50);
    diagram->SetNodeColor("air_mix", airFill, airBorder);
    diagram->SetNodeTextColor("air_mix", textColor);
 
    diagram->AddNode("mode_door", BlockShape::Rectangle, "Mode Door", 560, 380, 120, 60);
    diagram->SetNodeColor("mode_door", airFill, airBorder);
    diagram->SetNodeTextColor("mode_door", textColor);
 
    // -------------------------------------------------------------------------
    // CONNECTIONS
    // -------------------------------------------------------------------------
 
    // ----- Control signals into the refrigerant loop -----
    // Drive Clutch -> Compressor (mechanical drive)
    diagram->AddConnection("ctrl_drive", "drive_clutch", "compressor", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ctrl_drive", ctrlConn);
    diagram->SetConnectionWidth("ctrl_drive", connWidth);
 
    // Comp Control -> Compressor (electrical signal, dashed)
    diagram->AddConnection("ctrl_comp", "comp_control", "compressor", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ctrl_comp", ctrlConn);
    diagram->SetConnectionWidth("ctrl_comp", connWidth);
    diagram->SetConnectionLineStyle("ctrl_comp", LineStyle::Dashed);
 
    // ----- Refrigerant cycle (closed loop: clockwise) -----
    diagram->AddConnection("ref1", "compressor", "condenser", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ref1", coolConn);
    diagram->SetConnectionWidth("ref1", connWidth);
 
    diagram->AddConnection("ref2", "condenser", "expansion", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ref2", coolConn);
    diagram->SetConnectionWidth("ref2", connWidth);
 
    diagram->AddConnection("ref3", "expansion", "evaporator", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ref3", coolConn);
    diagram->SetConnectionWidth("ref3", connWidth);
 
    diagram->AddConnection("ref4", "evaporator", "compressor", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ref4", coolConn);
    diagram->SetConnectionWidth("ref4", connWidth);
 
    // ----- Blower control signal (dashed) -----
    diagram->AddConnection("ctrl_blower", "blower_ctrl", "blower", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("ctrl_blower", ctrlConn);
    diagram->SetConnectionWidth("ctrl_blower", connWidth);
    diagram->SetConnectionLineStyle("ctrl_blower", LineStyle::Dashed);
 
    // ----- Airflow path (left to right) -----
    // Blower pushes air up into the evaporator (cooling), then across to heater.
    diagram->AddConnection("air1", "blower", "evaporator", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("air1", airConn);
    diagram->SetConnectionWidth("air1", connWidth);
 
    diagram->AddConnection("air2", "evaporator", "heater", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("air2", airConn);
    diagram->SetConnectionWidth("air2", connWidth);
 
    diagram->AddConnection("air3", "heater", "mode_door", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("air3", airConn);
    diagram->SetConnectionWidth("air3", connWidth);
 
    // Air Mix Door bypasses the heater and joins the mode door
    diagram->AddConnection("air4", "blower", "air_mix", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("air4", airConn);
    diagram->SetConnectionWidth("air4", connWidth);
 
    diagram->AddConnection("air5", "air_mix", "mode_door", ConnectionStyle::Orthogonal);
    diagram->SetConnectionColor("air5", airConn);
    diagram->SetConnectionWidth("air5", connWidth);
 
    container->AddChild(diagram);
 
    // -------------------------------------------------------------------------
    // Snapshot of initial node positions, used by the Reset button below to
    // restore the layout after the user has dragged nodes around. Captured
    // by value into the lambda so it lives as long as the button does.
    // -------------------------------------------------------------------------
    struct InitialNodePos { std::string id; float x; float y; };
    std::vector<InitialNodePos> initialLayout = {
        {"compressor",   200,  80},
        {"condenser",    540,  80},
        {"expansion",    540, 200},
        {"evaporator",   200, 200},
        {"drive_clutch",  40,  80},
        {"comp_control",  40, 200},
        {"blower_ctrl",   40, 380},
        {"blower",       200, 380},
        {"heater",       380, 380},
        {"air_mix",      380, 460},
        {"mode_door",    560, 380},
    };
 
    // -------------------------------------------------------------------------
    // Toolbar
    // -------------------------------------------------------------------------
    int btnY = 635;
    int btnX = 50;
 
    auto btnSelect = std::make_shared<UltraCanvasButton>("btnSelect", btnX, btnY, 90, 32);
    btnSelect->SetText("Select");
    btnSelect->SetOnClick([diagram]() {
        diagram->SetEditMode(UltraCanvasBlockDiagram::EditMode::Select);
    });
    container->AddChild(btnSelect);
    btnX += 100;
 
    auto btnConnect = std::make_shared<UltraCanvasButton>("btnConnect", btnX, btnY, 90, 32);
    btnConnect->SetText("Connect");
    btnConnect->SetOnClick([diagram]() {
        diagram->SetEditMode(UltraCanvasBlockDiagram::EditMode::CreateConnection);
    });
    container->AddChild(btnConnect);
    btnX += 100;
 
    auto btnZoomIn = std::make_shared<UltraCanvasButton>("btnZoomIn", btnX, btnY, 70, 32);
    btnZoomIn->SetText("Zoom +");
    btnZoomIn->SetOnClick([diagram]() {
        diagram->SetZoomLevel(diagram->GetZoomLevel() * 1.2f);
    });
    container->AddChild(btnZoomIn);
    btnX += 80;
 
    auto btnZoomOut = std::make_shared<UltraCanvasButton>("btnZoomOut", btnX, btnY, 70, 32);
    btnZoomOut->SetText("Zoom -");
    btnZoomOut->SetOnClick([diagram]() {
        diagram->SetZoomLevel(diagram->GetZoomLevel() * 0.8f);
    });
    container->AddChild(btnZoomOut);
    btnX += 80;
 
    auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", btnX, btnY, 90, 32);
    btnReset->SetText("Reset");
    btnReset->SetOnClick([diagram, initialLayout]() {
        diagram->SetZoomLevel(1.0f);
        diagram->SetPanOffset(0, 0);
        for (const auto& p : initialLayout) {
            diagram->UpdateNodePosition(p.id, p.x, p.y);
        }
        diagram->DeselectAll();
    });
    container->AddChild(btnReset);
 
    auto chkGrid = std::make_shared<UltraCanvasCheckbox>("chkGrid", 50, 675, 100, 22);
    chkGrid->SetText("Show Grid");
    chkGrid->SetChecked(true);
    chkGrid->onStateChanged = [diagram](CheckedState oldState, CheckedState newState) {
        bool visible = (newState == CheckedState::Checked);
        diagram->SetGridVisible(visible, 20.0f);
    };
    container->AddChild(chkGrid);
 
    return container;
}
 
} // namespace UltraCanvas