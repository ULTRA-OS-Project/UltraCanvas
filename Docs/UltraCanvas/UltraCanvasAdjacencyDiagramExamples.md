# UltraCanvasAdjacencyDiagram Documentation

## Overview

**UltraCanvasAdjacencyDiagram** is an architectural space-planning adjacency diagram. Rooms are drawn as **area-proportional circles** colored by their functional type (Public, Private, Service, Support, Circulation, or a Custom override), adjacency requirements between rooms are drawn as solid (direct), dashed (secondary), or dotted (service) lines with optional arrowheads, and functional zones are drawn as dashed translucent bounding regions grouping related rooms (e.g. floors). Layout is fully manual — the designer iterates on spatial proximity by setting room x/y values directly.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.1

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasAdjacencyDiagram
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h"
```

## Features

- **Area-proportional rooms**: `radius = sqrt(areaSqM) * areaScale`, clamped to [`minRadius`, `maxRadius`]
- **Five functional type colors** + Custom override per-room
- **Three link types**: Direct (solid), Secondary (dashed), ServiceOnly (dotted)
- **Optional directed arrows** on links
- **Functional zones**: dashed rounded-rect bounding box around a set of rooms, with translucent fill and a zone label
- **Pan via drag**, content centering
- **Hover, click, selection** callbacks for rooms and links
- **Tooltip** showing room name, area, and floor

## Data Structures

### Enumerations

```cpp
enum class AdjacencyLinkType {
    Direct,        // Solid line — rooms share a wall
    Secondary,     // Dashed line — indirect / nearby
    ServiceOnly    // Dotted line — service / back-of-house
};

enum class RoomFunctionType {
    Public,        // Lobbies, reception, meeting rooms
    Private,       // Offices, workrooms
    Service,       // Kitchens, storage, loading
    Support,       // Toilets, plant rooms, corridors
    Circulation,   // Halls, stairs, elevators
    Custom         // Use AdjacencyRoom.color directly
};
```

### AdjacencyRoom

```cpp
struct AdjacencyRoom {
    std::string      id;
    std::string      label;
    float            areaSqM      = 20.0f;
    RoomFunctionType functionType = RoomFunctionType::Public;
    Color            color        = Color(100, 160, 220, 200); // Used when functionType == Custom
    float            x = 0.0f;
    float            y = 0.0f;
    std::string      floorId;
    std::string      note;
};
```

### AdjacencyLink

```cpp
struct AdjacencyLink {
    std::string       sourceId;
    std::string       targetId;
    AdjacencyLinkType type     = AdjacencyLinkType::Direct;
    bool              directed = false;
    float             weight   = 1.0f;
    std::string       label;
};
```

### AdjacencyZone

```cpp
struct AdjacencyZone {
    std::string              id;
    std::string              label;
    std::vector<std::string> roomIds;
    Color                    borderColor = Color(160, 160, 160, 200);
    Color                    fillColor   = Color(240, 240, 240,  60);
    float                    padding      = 24.0f;
    float                    cornerRadius = 20.0f;
};
```

### AdjacencyDiagramStyle (selected fields)

```cpp
struct AdjacencyDiagramStyle {
    // Room circles
    float areaScale       = 4.0f;     // radius = sqrt(areaSqM) * areaScale
    float minRadius       = 14.0f;
    float maxRadius       = 80.0f;
    float roomStrokeWidth = 1.5f;

    // Default zone colors
    Color colorPublic      = Color(100, 160, 220, 200);
    Color colorPrivate     = Color(210,  80,  60, 200);
    Color colorService     = Color(100, 100, 100, 200);
    Color colorSupport     = Color(200, 200, 200, 200);
    Color colorCirculation = Color( 80, 180, 140, 200);

    // Links
    float directLinkWidth    = 1.8f;
    float secondaryLinkWidth = 1.0f;
    float serviceLinkWidth   = 0.8f;
    float dashLength = 6.0f, dotLength = 2.0f, dashGap = 4.0f;
    float arrowSize  = 8.0f;

    // Zones
    float zoneBorderWidth   = 1.0f;
    float zoneLabelFontSize = 12.0f;

    // Tooltip
    bool  showTooltip       = true;
};
```

## Class Reference

### Constructor

```cpp
UltraCanvasAdjacencyDiagram(const std::string& id,
                            long x, long y, long w, long h);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasAdjacencyDiagram> CreateAdjacencyDiagram(
        const std::string& id, long uid,
        long x, long y, long width, long height);
```

### Room API

```cpp
int  AddRoom(const AdjacencyRoom& room);
int  AddRoom(const std::string& id, const std::string& label,
             float areaSqM, float x, float y,
             RoomFunctionType type = RoomFunctionType::Public);

bool RemoveRoom(const std::string& id);
void UpdateRoom(const std::string& id, const AdjacencyRoom& updated);
void MoveRoom(const std::string& id, float x, float y);

int                  GetRoomCount() const;
const AdjacencyRoom* GetRoom(int index) const;
const AdjacencyRoom* GetRoomById(const std::string& id) const;
```

### Link API

```cpp
int  AddLink(const AdjacencyLink& link);
int  AddLink(const std::string& sourceId,
             const std::string& targetId,
             AdjacencyLinkType type = AdjacencyLinkType::Direct,
             bool directed = false);

void RemoveLink(const std::string& sourceId, const std::string& targetId);
void ClearLinks();
int  GetLinkCount() const;
```

### Zone API

```cpp
void AddZone(const AdjacencyZone& zone);
void RemoveZone(const std::string& id);
void ClearZones();
int  GetZoneCount() const;
```

### Clear / View / Selection

```cpp
void Clear();

void SetEnablePan(bool enable);
bool GetEnablePan() const;
void ResetPan();
void SetPanOffset(float x, float y);
void CenterContent();   // Bounding-box centers all rooms in widget

void SetStyle(const AdjacencyDiagramStyle& s);
const AdjacencyDiagramStyle& GetStyle() const;

int  GetSelectedRoomIndex() const;
int  GetSelectedLinkIndex() const;
void ClearSelection();
```

### Callbacks

```cpp
std::function<void(int, const AdjacencyRoom&)> onRoomClick;
std::function<void(int, const AdjacencyRoom&)> onRoomHover;
std::function<void(int, const AdjacencyLink&)> onLinkClick;
```

## Usage Examples

All examples are drawn from `Apps/DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp`.

### Library branch — single-floor program

```cpp
auto diagram = std::make_shared<UltraCanvasAdjacencyDiagram>(
        "LibDiagram", 10, 34, 990, 654);

// Rooms — position in diagram-local coordinates; CenterContent() below
// shifts the bounding box of all rooms to the widget center.
diagram->AddRoom("lobby",    "Lobby / Entry",     80,    0,    0, RoomFunctionType::Circulation);
diagram->AddRoom("circ",     "Circulation\nDesk", 35,  120,  -80, RoomFunctionType::Public);
diagram->AddRoom("staff",    "Staff Work\nRoom",  60,  150,   40, RoomFunctionType::Private);
diagram->AddRoom("fiction",  "Adult Fiction",    140,  300,  -90, RoomFunctionType::Public);
diagram->AddRoom("nonfic",   "Adult\nNon-Fiction",120, 400,   20, RoomFunctionType::Public);
diagram->AddRoom("youngadl", "Young Adult",       60,  350,  110, RoomFunctionType::Public);
diagram->AddRoom("children", "Children\n(3 zones)",350,-200,  60, RoomFunctionType::Public);
diagram->AddRoom("meeting",  "Meeting Room",     180, -120,  130, RoomFunctionType::Public);
diagram->AddRoom("print",    "Print / Copy",      18,   60, -150, RoomFunctionType::Support);
diagram->AddRoom("toilets",  "Public\nRestrooms", 30,   80,  160, RoomFunctionType::Support);
diagram->AddRoom("storage",  "Storage",           20,  -20,  190, RoomFunctionType::Service);
diagram->AddRoom("lounge",   "Staff Lounge",      20,  220,   90, RoomFunctionType::Private);
```

### Adding direct, secondary, and service links

```cpp
// Direct adjacency (solid)
diagram->AddLink("lobby",    "circ",     AdjacencyLinkType::Direct);
diagram->AddLink("lobby",    "children", AdjacencyLinkType::Direct);
diagram->AddLink("circ",     "staff",    AdjacencyLinkType::Direct);
diagram->AddLink("staff",    "fiction",  AdjacencyLinkType::Direct);
diagram->AddLink("fiction",  "nonfic",   AdjacencyLinkType::Direct);
diagram->AddLink("nonfic",   "youngadl", AdjacencyLinkType::Direct);

// Secondary (dashed)
diagram->AddLink("lobby",    "meeting",  AdjacencyLinkType::Secondary);
diagram->AddLink("lobby",    "print",    AdjacencyLinkType::Secondary);
diagram->AddLink("lobby",    "toilets",  AdjacencyLinkType::Secondary);
diagram->AddLink("meeting",  "storage",  AdjacencyLinkType::Secondary);

// Service (dotted)
diagram->AddLink("staff",    "storage",  AdjacencyLinkType::ServiceOnly);
```

### A floor zone wrapping all rooms

```cpp
AdjacencyZone zone;
zone.id    = "gf";
zone.label = "Ground floor";
zone.roomIds = {"lobby","circ","staff","fiction","nonfic","youngadl",
                "children","meeting","childprog","print","toilets","storage","lounge"};
zone.borderColor = Color(120, 120, 180, 160);
zone.fillColor   = Color(230, 235, 255,  35);
zone.padding     = 30.0f;
diagram->AddZone(zone);

diagram->CenterContent();
```

### Reporting selection through callbacks

```cpp
diagram->onRoomClick = [statusLabel](int, const AdjacencyRoom& r) {
    std::ostringstream ss;
    ss << r.label << "  " << static_cast<int>(r.areaSqM) << " m²";
    statusLabel->SetText(ss.str());
};
```

### Multi-floor villa — directed inter-floor circulation

```cpp
// Ground floor rooms
diagram->AddRoom("gf_dining",   "Dining",         30, -120,  60, RoomFunctionType::Public);
diagram->AddRoom("gf_kitchen",  "Kitchen",        25,  -40, 110, RoomFunctionType::Service);
diagram->AddRoom("gf_theater",  "Home Theater",   40,  -30,  50, RoomFunctionType::Private);
diagram->AddRoom("gf_outdoor",  "Outdoor Dining", 60, -200,  70, RoomFunctionType::Public);

// 1F
diagram->AddRoom("1f_lobby",    "Lobby",         120,  140, -20, RoomFunctionType::Circulation);
diagram->AddRoom("1f_elev",     "Elevator",       10,   80, -20, RoomFunctionType::Support);

// 2F
diagram->AddRoom("2f_library",  "Library",        80,  130, -160, RoomFunctionType::Private);
diagram->AddRoom("2f_hall",     "Hall",           20,  130, -100, RoomFunctionType::Circulation);

// Directed circulation between floors (arrowheads)
diagram->AddLink("1f_lobby", "2f_hall", AdjacencyLinkType::Direct,  /*directed=*/true);
diagram->AddLink("1f_elev",  "2f_hall", AdjacencyLinkType::Direct,  /*directed=*/true);
```

### Creating zones with a small helper lambda

```cpp
auto makeZone = [](const std::string& id, const std::string& lbl,
                   std::vector<std::string> rooms,
                   Color border, Color fill) {
    AdjacencyZone z;
    z.id          = id;
    z.label       = lbl;
    z.roomIds     = std::move(rooms);
    z.borderColor = border;
    z.fillColor   = fill;
    z.padding     = 28.0f;
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
```

### Office program — public and back-of-house zones

```cpp
// Public-facing rooms
diagram->AddRoom("entry",     "Entry",        25, -200, -100, RoomFunctionType::Public);
diagram->AddRoom("lobby",     "Lobby",        50, -100, -100, RoomFunctionType::Public);
diagram->AddRoom("reception", "Reception",    30,  -60, -150, RoomFunctionType::Public);
diagram->AddRoom("conf1",     "Conference A", 40,   60, -150, RoomFunctionType::Public);
diagram->AddRoom("showroom",  "Showroom",     90,   40,  -80, RoomFunctionType::Public);

// Staff / back-of-house
diagram->AddRoom("openplan",  "Open Studio",  120,   0,    20, RoomFunctionType::Private);
diagram->AddRoom("principal", "Principal Off.",35, -80,    80, RoomFunctionType::Private);
diagram->AddRoom("server",    "Server Room",   12, 160,    50, RoomFunctionType::Service);
diagram->AddRoom("files",     "File Room",     14, 140,   100, RoomFunctionType::Support);
diagram->AddRoom("kitchen",   "Kitchenette",   15, -80,   140, RoomFunctionType::Service);
diagram->AddRoom("toilets",   "Toilets",       16,  20,   160, RoomFunctionType::Support);

AdjacencyZone pubZone;
pubZone.id = "pub"; pubZone.label = "Public";
pubZone.roomIds = {"entry","lobby","reception","conf1","conf2","showroom"};
pubZone.borderColor = Color(80, 120, 200, 160);
pubZone.fillColor   = Color(210, 225, 255, 35);
pubZone.padding     = 28.0f;
diagram->AddZone(pubZone);

AdjacencyZone staffZone;
staffZone.id = "staff"; staffZone.label = "Staff / Back-of-house";
staffZone.roomIds = {"openplan","principal","bookkeeper","server","files","kitchen","toilets"};
staffZone.borderColor = Color(160, 100,  80, 160);
staffZone.fillColor   = Color(255, 228, 215, 35);
staffZone.padding     = 28.0f;
diagram->AddZone(staffZone);
```

### Inspecting links on click

```cpp
diagram->onLinkClick = [statusLabel](int, const AdjacencyLink& l) {
    std::string typeStr =
        (l.type == AdjacencyLinkType::Secondary)   ? "secondary" :
        (l.type == AdjacencyLinkType::ServiceOnly) ? "service"   : "direct";
    statusLabel->SetText(l.sourceId + " — " + l.targetId + " (" + typeStr + ")");
};
```

## Render Order

The diagram draws in this order each frame, so room circles and labels stay
visually on top of zone bands and link lines:

1. Zones (translucent fill + dashed rounded border + zone label)
2. Links (solid / dashed / dotted, optional arrowheads)
3. Rooms (filled circles with stroke)
4. Labels (room name + optional note)
5. Tooltip (over hovered room)

## Manual Layout Notes

Adjacency diagrams intentionally have no auto-layout. The architectural
workflow is to iterate on relative proximity by editing room x/y. `CenterContent()`
re-centers the bounding box of all rooms inside the widget, which is the only
implicit positioning step. Use `MoveRoom(id, x, y)` to reposition without
needing to call `UpdateRoom` with a full struct.

## See Also

- [UltraCanvasBlockDiagramExamples](UltraCanvasBlockDiagramExamples.md) — flowchart-style block diagrams
- [UltraCanvasNodeDiagramExamples](UltraCanvasNodeDiagramExamples.md) — interactive graph editor with handles
- [UltraCanvasGourceTreeExamples](UltraCanvasGourceTreeExamples.md) — radial filesystem tree
- [UltraCanvasArcDiagramExamples](UltraCanvasArcDiagramExamples.md) — nodes on a baseline with arc edges
- [UltraCanvasTabbedContainer](UltraCanvasTabbedContainer.md) — tab control used by the demo
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
