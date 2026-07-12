// Apps/DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp
// Demo examples for UltraCanvasAdjacencyDiagram — architectural space-planning
// Version: 1.1.1
// Last Modified: 2026-06-04
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include <sstream>

namespace UltraCanvas {

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 — Library branch  (image 1 — DCPL adjacency diagram)
// Public / staff / support functional zones across one floor
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeLibraryTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("AdjLibTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("AdjLibDesc", 10, 8, 900, 22);
        desc->SetText("Library branch adjacency: circles sized by floor area. Solid = direct adjacency, dashed = secondary access.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasAdjacencyDiagram>(
                "LibDiagram", 10, 34, 990, 654);

        // Rooms — position in diagram local coordinates; CenterContent() below
        // shifts the bounding box of all rooms to the widget center.
        diagram->AddRoom("lobby",    "Lobby / Entry",      80,   0,    0,  RoomFunctionType::Circulation);
        diagram->AddRoom("circ",     "Circulation\nDesk",  35,  120,  -80, RoomFunctionType::Public);
        diagram->AddRoom("staff",    "Staff Work\nRoom",   60,  150,   40, RoomFunctionType::Private);
        diagram->AddRoom("fiction",  "Adult Fiction",     140,  300,  -90, RoomFunctionType::Public);
        diagram->AddRoom("nonfic",   "Adult\nNon-Fiction", 120, 400,   20, RoomFunctionType::Public);
        diagram->AddRoom("youngadl", "Young Adult",        60,  350,  110, RoomFunctionType::Public);
        diagram->AddRoom("children", "Children\n(3 zones)",350, -200,  60, RoomFunctionType::Public);
        diagram->AddRoom("meeting",  "Meeting Room",      180, -120,  130, RoomFunctionType::Public);
        diagram->AddRoom("childprog","Children's\nProgram", 40, -260, 140, RoomFunctionType::Public);
        diagram->AddRoom("print",    "Print / Copy",       18,   60, -150, RoomFunctionType::Support);
        diagram->AddRoom("toilets",  "Public\nRestrooms",  30,   80,  160, RoomFunctionType::Support);
        diagram->AddRoom("storage",  "Storage",            20,  -20,  190, RoomFunctionType::Service);
        diagram->AddRoom("lounge",   "Staff Lounge",       20,  220,   90, RoomFunctionType::Private);

        // Direct adjacency links
        diagram->AddLink("lobby",   "circ",     AdjacencyLinkType::Direct);
        diagram->AddLink("lobby",   "children", AdjacencyLinkType::Direct);
        diagram->AddLink("circ",    "staff",    AdjacencyLinkType::Direct);
        diagram->AddLink("staff",   "fiction",  AdjacencyLinkType::Direct);
        diagram->AddLink("staff",   "lounge",   AdjacencyLinkType::Direct);
        diagram->AddLink("fiction",  "nonfic",   AdjacencyLinkType::Direct);
        diagram->AddLink("nonfic",  "youngadl", AdjacencyLinkType::Direct);

        // Secondary (dashed)
        diagram->AddLink("lobby",   "meeting",  AdjacencyLinkType::Secondary);
        diagram->AddLink("lobby",   "print",    AdjacencyLinkType::Secondary);
        diagram->AddLink("lobby",   "toilets",  AdjacencyLinkType::Secondary);
        diagram->AddLink("children","childprog",AdjacencyLinkType::Secondary);
        diagram->AddLink("meeting", "storage",  AdjacencyLinkType::Secondary);

        // Service
        diagram->AddLink("staff",   "storage",  AdjacencyLinkType::ServiceOnly);

        // Single ground-floor zone
        AdjacencyZone zone;
        zone.id    = "gf";
        zone.label = "Ground floor";
        zone.roomIds = {"lobby","circ","staff","fiction","nonfic","youngadl",
                        "children","meeting","childprog","print","toilets","storage","lounge"};
        zone.borderColor = Color(120, 120, 180, 160);
        zone.fillColor   = Color(230, 235, 255,  35);
        zone.padding     = 30.0f;
        diagram->AddZone(zone);

        diagram->onRoomClick = [statusLabel](int, const AdjacencyRoom& r) {
            std::ostringstream ss;
            ss << r.label << "  " << static_cast<int>(r.areaSqM) << " m²";
            statusLabel->SetText(ss.str());
        };

        diagram->CenterContent();
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 — Multi-floor residence  (image 2 — multi-floor villa)
// Three floors (GF, 1F, 2F) with zone bounding boxes per floor
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeResidenceTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("AdjResTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("AdjResDesc", 10, 8, 900, 22);
        desc->SetText("Multi-floor residence: three floor zones. Arrows show directed circulation routes between floors.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasAdjacencyDiagram>(
                "ResDiagram", 10, 34, 990, 654);

        // ── Ground floor ──
        diagram->AddRoom("gf_dining",    "Dining",          30,  -120,  60, RoomFunctionType::Public);
        diagram->AddRoom("gf_kitchen",   "Kitchen",         25,  -40,  110, RoomFunctionType::Service);
        diagram->AddRoom("gf_opencook",  "Open Kitchen",    20,  -80,  110, RoomFunctionType::Service);
        diagram->AddRoom("gf_theater",   "Home Theater",    40,  -30,   50, RoomFunctionType::Private);
        diagram->AddRoom("gf_outdoor",   "Outdoor Dining",  60, -200,   70, RoomFunctionType::Public);

        // ── 1F ──
        diagram->AddRoom("1f_lobby",     "Lobby",          120,  140,  -20, RoomFunctionType::Circulation);
        diagram->AddRoom("1f_elev",      "Elevator",        10,   80,  -20, RoomFunctionType::Support);
        diagram->AddRoom("1f_service",   "Service",         10,  200,  -20, RoomFunctionType::Service);

        // ── 2F ──
        diagram->AddRoom("2f_library",   "Library",         80,  130, -160, RoomFunctionType::Private);
        diagram->AddRoom("2f_lounge",    "Lounge",          60,  240, -160, RoomFunctionType::Public);
        diagram->AddRoom("2f_meeting",   "Meeting Rm",      35,  200, -100, RoomFunctionType::Public);
        diagram->AddRoom("2f_hall",      "Hall",            20,  130, -100, RoomFunctionType::Circulation);

        // Links
        diagram->AddLink("gf_dining",    "gf_kitchen",   AdjacencyLinkType::Direct);
        diagram->AddLink("gf_kitchen",   "gf_opencook",  AdjacencyLinkType::Direct);
        diagram->AddLink("gf_dining",    "gf_theater",   AdjacencyLinkType::Secondary);
        diagram->AddLink("gf_outdoor",   "gf_dining",    AdjacencyLinkType::Direct);
        diagram->AddLink("1f_lobby",     "1f_elev",      AdjacencyLinkType::Direct);
        diagram->AddLink("1f_lobby",     "2f_hall",      AdjacencyLinkType::Direct,   true);
        diagram->AddLink("1f_elev",      "2f_hall",      AdjacencyLinkType::Direct,   true);
        diagram->AddLink("2f_hall",      "2f_library",   AdjacencyLinkType::Direct);
        diagram->AddLink("2f_hall",      "2f_lounge",    AdjacencyLinkType::Direct);
        diagram->AddLink("2f_lounge",    "2f_meeting",   AdjacencyLinkType::Secondary);
        diagram->AddLink("gf_kitchen",   "1f_service",   AdjacencyLinkType::ServiceOnly);

        // Floor zones
        auto makeZone = [](const std::string& id, const std::string& lbl,
                           std::vector<std::string> rooms,
                           Color border, Color fill) {
            AdjacencyZone z;
            z.id = id; z.label = lbl; z.roomIds = std::move(rooms);
            z.borderColor = border; z.fillColor = fill;
            z.padding = 28.0f;
            return z;
        };

        diagram->AddZone(makeZone("gf", "GF",
                                  {"gf_dining","gf_kitchen","gf_opencook","gf_theater","gf_outdoor"},
                                  Color(100, 160, 100, 160), Color(200, 240, 200, 35)));

        diagram->AddZone(makeZone("1f", "1F",
                                  {"1f_lobby","1f_elev","1f_service"},
                                  Color(100, 130, 200, 160), Color(200, 215, 245, 35)));

        diagram->AddZone(makeZone("2f", "2F",
                                  {"2f_library","2f_lounge","2f_meeting","2f_hall"},
                                  Color(160, 100, 180, 160), Color(230, 210, 245, 35)));

        diagram->onRoomClick = [statusLabel](int, const AdjacencyRoom& r) {
            std::ostringstream ss;
            ss << r.label << "  " << static_cast<int>(r.areaSqM) << " m²";
            statusLabel->SetText(ss.str());
        };

        diagram->CenterContent();
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 — Office / studio  (Director / Exterior group bands)
// Matches image 1 DCPL with row group bands on the left margin
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeOfficeTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel)
    {
        auto tab = std::make_shared<UltraCanvasContainer>("AdjOfficeTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("AdjOfficeDesc", 10, 8, 900, 22);
        desc->SetText("Design studio program: public-facing vs. back-of-house zones. Drag rooms to adjust the layout.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = std::make_shared<UltraCanvasAdjacencyDiagram>(
                "OfficeDiagram", 10, 34, 990, 654);

        // Public zone
        diagram->AddRoom("entry",     "Entry",          25,  -200, -100, RoomFunctionType::Public);
        diagram->AddRoom("lobby",     "Lobby",          50,  -100, -100, RoomFunctionType::Public);
        diagram->AddRoom("reception", "Reception",      30,  -60,  -150, RoomFunctionType::Public);
        diagram->AddRoom("conf1",     "Conference A",   40,   60,  -150, RoomFunctionType::Public);
        diagram->AddRoom("conf2",     "Conference B",   40,  140,  -100, RoomFunctionType::Public);
        diagram->AddRoom("showroom",  "Showroom",       90,   40,   -80, RoomFunctionType::Public);

        // Staff zone
        diagram->AddRoom("openplan",  "Open Studio",   120,    0,    20, RoomFunctionType::Private);
        diagram->AddRoom("principal", "Principal Off.", 35,  -80,    80, RoomFunctionType::Private);
        diagram->AddRoom("bookkeeper","Bookkeeper",     18,   80,    90, RoomFunctionType::Private);
        diagram->AddRoom("server",    "Server Room",    12,  160,    50, RoomFunctionType::Service);
        diagram->AddRoom("files",     "File Room",      14,  140,   100, RoomFunctionType::Support);
        diagram->AddRoom("kitchen",   "Kitchenette",    15,  -80,   140, RoomFunctionType::Service);
        diagram->AddRoom("toilets",   "Toilets",        16,   20,   160, RoomFunctionType::Support);

        // Adjacency
        diagram->AddLink("entry",     "lobby",     AdjacencyLinkType::Direct);
        diagram->AddLink("lobby",     "reception", AdjacencyLinkType::Direct);
        diagram->AddLink("lobby",     "showroom",  AdjacencyLinkType::Direct);
        diagram->AddLink("reception", "conf1",     AdjacencyLinkType::Direct);
        diagram->AddLink("conf1",     "conf2",     AdjacencyLinkType::Secondary);
        diagram->AddLink("lobby",     "openplan",  AdjacencyLinkType::Secondary);
        diagram->AddLink("openplan",  "principal", AdjacencyLinkType::Direct);
        diagram->AddLink("openplan",  "bookkeeper",AdjacencyLinkType::Secondary);
        diagram->AddLink("openplan",  "kitchen",   AdjacencyLinkType::Secondary);
        diagram->AddLink("openplan",  "toilets",   AdjacencyLinkType::Secondary);
        diagram->AddLink("server",    "files",     AdjacencyLinkType::ServiceOnly);

        AdjacencyZone pubZone;
        pubZone.id = "pub"; pubZone.label = "Public";
        pubZone.roomIds = {"entry","lobby","reception","conf1","conf2","showroom"};
        pubZone.borderColor = Color(80, 120, 200, 160);
        pubZone.fillColor   = Color(210, 225, 255,  35);
        pubZone.padding = 28.0f;
        diagram->AddZone(pubZone);

        AdjacencyZone staffZone;
        staffZone.id = "staff"; staffZone.label = "Staff / Back-of-house";
        staffZone.roomIds = {"openplan","principal","bookkeeper","server","files","kitchen","toilets"};
        staffZone.borderColor = Color(160, 100,  80, 160);
        staffZone.fillColor   = Color(255, 228, 215,  35);
        staffZone.padding = 28.0f;
        diagram->AddZone(staffZone);

        diagram->onRoomClick = [statusLabel](int, const AdjacencyRoom& r) {
            std::ostringstream ss;
            ss << r.label << "  " << static_cast<int>(r.areaSqM) << " m²";
            statusLabel->SetText(ss.str());
        };
        diagram->onLinkClick = [statusLabel](int, const AdjacencyLink& l) {
            std::string typeStr =
                    (l.type == AdjacencyLinkType::Secondary)  ? "secondary"  :
                    (l.type == AdjacencyLinkType::ServiceOnly) ? "service"    : "direct";
            statusLabel->SetText(l.sourceId + " — " + l.targetId + " (" + typeStr + ")");
        };

        diagram->CenterContent();
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// ENTRY POINT
// ─────────────────────────────────────────────────────────────────────────────
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateAdjacencyDiagramExamples()
    {
        // No fixed size: the host display area stretches `main` (it applies
        // flex-grow + align-self:stretch), so the Fr content column grows with
        // the window and the subtitle re-wraps on resize.
        auto main = std::make_shared<UltraCanvasContainer>("AdjDiagExamples");
        main->SetPadding(5,5,0,5);

        // 3-row × 2-column grid: title/subtitle in the left column, status box in
        // the right column spanning both top rows, tabs filling the bottom row.
        // Fixed pixel heights on the label rows (Auto rows measure unwrapped text
        // and would clip the subtitle's second wrapped line).
        main->layout.SetGrid()
                .SetGridColumns({
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fr,
                                                 .value = CSSLayout::Dimension::Fr(1)},
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                                 .value = CSSLayout::Dimension::Px(350)}})
                .SetGridRows({
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                                 .value = CSSLayout::Dimension::Px(34)},  // title row
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                                 .value = CSSLayout::Dimension::Px(44)},  // subtitle (room for 2 lines)
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fr,
                                                 .value = CSSLayout::Dimension::Fr(1)}})   // tabs fill
                .SetGridGap(6);

        auto title = std::make_shared<UltraCanvasLabel>("AdjDiagTitle");
        title->SetText("Architectural Adjacency Diagram");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 100, 60));
        main->AddChild(title);
        title->layoutItem.SetGridRowColSimplified(0, 0);

        auto subtitle = std::make_shared<UltraCanvasLabel>("AdjDiagSub");
        subtitle->SetText("Rooms as area-proportional circles, colored by function. Solid = direct adjacency, dashed = secondary, dotted = service");
        subtitle->SetFontSize(11);
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetTextColor(Color(90, 90, 90));
        main->AddChild(subtitle);
        subtitle->layoutItem.SetGridRowColSimplified(1, 0);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("AdjDiagStatus");
        statusLabel->SetText("Click a room to inspect.\nDrag rooms to reposition — pan with empty-space drag.");
        statusLabel->SetFontSize(10);
        statusLabel->SetBackgroundColor(Color(245, 245, 245));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(6.0f);
        statusLabel->SetAlignment(TextAlignment::Center);
        main->AddChild(statusLabel);
        statusLabel->layoutItem.SetGridRowColSimplified(0, 1, 2, 1);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("AdjDiagTabs");
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);

        tabs->AddTab("Library branch",    MakeLibraryTab(statusLabel));
        tabs->AddTab("Multi-floor villa", MakeResidenceTab(statusLabel));
        tabs->AddTab("Design studio",     MakeOfficeTab(statusLabel));

        main->AddChild(tabs);
        tabs->layoutItem.SetGridRowColSimplified(2, 0, 1, 2);
        return main;
    }

} // namespace UltraCanvas