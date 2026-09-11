# UltraCanvasParliamentDiagram Documentation

**Version:** 1.0.0
**Last Modified:** 2026-09-10
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasParliamentDiagram` renders the seat chart of a legislature: every
seat is one marker, coloured by party, and the parties sit next to each other
in the order they were added. It is the "parliament diagram" of election
reports and Wikipedia infoboxes — a hemicycle of concentric arcs — and the same
data can be shown as a wider horseshoe, a full circle, two blocks of benches
facing each other across an aisle (Westminster) or a plain grid.

The element draws a dashed **majority marker** at the seat that splits the
chamber in half, the **total seat count** in the empty centre, and a **party
legend** with seat counts. Parties can be flagged as part of the government,
which places them on the government benches in the Westminster layout and can
fade everyone else. Hovering a seat or a legend entry highlights that party
and shows a tooltip; clicking selects it.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasParliamentDiagram.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasParliamentDiagram.cpp`
**Base Class:** `UltraCanvasChartElementBase`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasParliamentDiagram
```

## Layouts

`ParliamentLayout` selects where the seats go. All layouts render the same
party list, so the presentation can be switched at runtime.

| Layout | Description |
|---|---|
| `ParliamentLayout::Hemicycle` | Concentric arcs, parties left to right (default). `SetArcSpan()` widens the 180° half circle into a horseshoe of up to 360°. |
| `ParliamentLayout::Circle` | Concentric full rings, parties clockwise from the top. |
| `ParliamentLayout::Westminster` | Two blocks of benches facing each other: parties flagged `government` fill the left block from the front bench next to the aisle, the rest fill the right block. A Speaker's chair sits at the head of the aisle. |
| `ParliamentLayout::Grid` | A rectangular waffle of seats in party order, left to right and top to bottom. |

In the arc layouts the seats of each arc are in proportion to its length, so
an outer arc holds more seats than an inner one, and the seats are handed to
the parties by sweeping the angle from one end of the chamber to the other
(inner arc first where seats line up). That is what produces the familiar
wedge shape of each party. The number of arcs is chosen automatically as the
smallest count that fits every seat, or fixed with `SetRowCount()`.

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasParliamentDiagram.h"
```

## Data Model

```cpp
struct ParliamentParty {
    std::string name;           // "Social Democratic Party"
    std::string abbreviation;   // "SPD" - used in the legend when set
    int seats = 0;
    Color color;
    bool government = false;    // Governing side / coalition member
    bool vacant = false;        // Drawn hollow (vacant seats, the Speaker)

    ParliamentParty(const std::string& name, int seats, const Color& color,
                    bool government = false);
    ParliamentParty(const std::string& name, const std::string& abbreviation,
                    int seats, const Color& color, bool government = false);
};

// One placed seat, in the element's local coordinates
struct ParliamentSeat {
    Point2Dd center;
    double radius;
    int row;          // 0 = innermost arc / front bench / top row
    size_t party;     // Index into GetParties()
    size_t ordinal;   // Position in the fill order
};
```

The party order is the drawing order: left to right in the hemicycle,
clockwise in the circle, front bench first in the Westminster blocks. Sort the
parties along the political spectrum before adding them for the conventional
left-to-right picture.

## Class Reference

### Constructor

```cpp
UltraCanvasParliamentDiagram(const std::string& id, int x, int y, int w, int h);
```

Selection and tooltips are enabled by default. The default is the 180°
hemicycle with round seats, automatic row count, the majority marker, the total
label and a legend under the chamber.

### Party Management

```cpp
void AddParty(const ParliamentParty& party);
void AddParty(const std::string& name, int seats, const Color& color,
              bool government = false);
void SetParties(const std::vector<ParliamentParty>& list);   // Replaces all, clears selection
const std::vector<ParliamentParty>& GetParties() const;
const ParliamentParty& GetParty(size_t index) const;
size_t GetPartyCount() const;
void RemoveParty(size_t index);
void ClearParties();

void SetPartySeats(size_t index, int seats);
void SetPartyColor(size_t index, const Color& color);
void SetPartyGovernment(size_t index, bool government);
void SetPartyVacant(size_t index, bool vacant);

int GetTotalSeats() const;
int GetGovernmentSeats() const;
int GetMajorityThreshold() const;      // Total / 2 + 1
bool GovernmentHasMajority() const;
```

### Layout

```cpp
void SetLayout(ParliamentLayout layout);
ParliamentLayout GetLayout() const;

void SetArcSpan(double degrees);       // Hemicycle: 90..360, default 180
void SetRowCount(int rows);            // Arcs / benches per side / grid rows; 0 = automatic
int GetRowsUsed() const;               // The count the last render used
void SetInnerRadiusRatio(double ratio);// Hole radius / outer radius; 0 = per-layout default
void SetSeatGap(double fraction);      // Gap between seats as a fraction of the pitch (default 0.18)
void SetGridColumns(int columns);      // Grid layout; 0 = automatic
```

The diagram title comes from the base class: `SetChartTitle("...")`.

### Appearance

```cpp
void SetSeatShape(ParliamentSeatShape shape);   // Circle (default), Square, RoundedSquare
void SetDarkTheme(bool dark);                   // Also switches the element background

void SetShowTotalLabel(bool show);              // Seat count in the centre (default: on)
void SetTotalCaption(const std::string& text);  // Word under the number, default "seats"
void SetShowMajorityMarker(bool show);          // Dashed line at the half-way seat (default: on)
void SetMajorityCaption(const std::string& text); // Label prefix, default "Majority"
void SetHighlightGovernment(bool highlight);    // Fade the seats of non-governing parties
void SetDimOnHover(bool dim);                   // Fade the other parties while one is hovered / selected (default: on)
void SetShowSpeakerChair(bool show);            // Westminster layout (default: on)
void SetLegendPosition(ParliamentLegendPosition position); // Bottom (default), Right, Hidden

void SetTitleFontSize(float size);
void SetTotalFontSize(float size);
void SetLegendFontSize(float size);
```

The majority marker is drawn in the arc layouts; in the Westminster and grid
layouts the threshold is available from `GetMajorityThreshold()` for the host
page to show.

### Selection & Interaction

```cpp
// Inherited from UltraCanvasChartElementBase:
void SetEnableSelection(bool enable);   // Default: on
void SetEnableTooltips(bool enable);    // Default: on

size_t GetSelectedParty() const;        // SIZE_MAX = none
void SetSelectedParty(size_t index);
void ClearSelection();
size_t GetHoveredParty() const;

const std::vector<ParliamentSeat>& GetSeats() const;   // Placed by the last render
```

Clicking a seat or a legend entry toggles the selection of its party; clicking
empty space clears it. While a party is hovered or selected the other parties
are faded so its seats stand out. The tooltip shows the party name, its seat
count and share of the chamber, and whether it is on the government side.

### Event Callbacks

```cpp
std::function<void(size_t, const ParliamentParty&)> onPartyHover;
std::function<void(size_t, const ParliamentParty&)> onPartyClick;
std::function<void(size_t, const ParliamentSeat&)> onSeatClick;   // Seat index, before onPartyClick
std::function<void()> onSelectionChange;                          // After any click-driven change
```

### Factory Functions

```cpp
std::shared_ptr<UltraCanvasParliamentDiagram> CreateParliamentDiagram(
        const std::string& id, int x, int y, int width, int height);

std::shared_ptr<UltraCanvasParliamentDiagram> CreateParliamentDiagram(
        const std::string& id, int x, int y, int width, int height,
        ParliamentLayout layout);
```

### Sample Data

```cpp
namespace ParliamentDiagramSamples {
    std::vector<ParliamentParty> Bundestag2021();           // 736 seats, SPD/Greens/FDP as government
    std::vector<ParliamentParty> EuropeanParliament2024();  // 720 seats, nine groups
    std::vector<ParliamentParty> HouseOfCommons2024();      // 650 seats, Labour as government, Speaker vacant
}
```

## Usage Examples

### Basic Hemicycle

```cpp
#include "Plugins/Diagrams/UltraCanvasParliamentDiagram.h"

auto chamber = CreateParliamentDiagram("chamber", 20, 20, 620, 460);
chamber->SetChartTitle("National Assembly, 2026");
chamber->AddParty(ParliamentParty("Left Alliance", "LA", 42, Color(190, 30, 110, 255)));
chamber->AddParty(ParliamentParty("Social Democrats", "SD", 138, Color(226, 0, 26, 255), true));
chamber->AddParty(ParliamentParty("Greens", "Grn", 61, Color(70, 150, 40, 255), true));
chamber->AddParty(ParliamentParty("Liberals", "Lib", 47, Color(255, 204, 0, 255)));
chamber->AddParty(ParliamentParty("Conservatives", "Con", 152, Color(50, 50, 50, 255)));
chamber->AddParty(ParliamentParty("National Front", "NF", 60, Color(0, 150, 220, 255)));
parentContainer->AddChild(chamber);
```

### Horseshoe and Circle

```cpp
chamber->SetArcSpan(240.0);                    // Wide horseshoe
chamber->SetInnerRadiusRatio(0.5);             // Bigger hole for the total label

auto ring = CreateParliamentDiagram("ring", 0, 0, 500, 500, ParliamentLayout::Circle);
ring->SetParties(ParliamentDiagramSamples::EuropeanParliament2024());
ring->SetLegendPosition(ParliamentLegendPosition::Right);
```

### Westminster Benches

```cpp
auto commons = CreateParliamentDiagram("commons", 0, 0, 640, 480, ParliamentLayout::Westminster);
commons->SetParties(ParliamentDiagramSamples::HouseOfCommons2024());
commons->SetSeatShape(ParliamentSeatShape::RoundedSquare);
commons->SetRowCount(5);                       // Five benches per side
```

Parties with `government == true` fill the left block; everyone else sits
opposite. `SetPartyGovernment()` moves a party across the aisle.

### Coalition Builder

```cpp
chamber->SetHighlightGovernment(true);         // Non-coalition seats faded
chamber->SetEnableSelection(false);            // Clicks only toggle membership
chamber->onPartyClick = [ptr = chamber.get()](size_t index, const ParliamentParty& party) {
    ptr->SetPartyGovernment(index, !party.government);
    std::cout << ptr->GetGovernmentSeats() << " of " << ptr->GetMajorityThreshold()
              << (ptr->GovernmentHasMajority() ? " - majority" : " - short") << std::endl;
};
```

### Hover and Selection Callbacks

```cpp
chamber->onPartyHover = [](size_t, const ParliamentParty& party) {
    std::cout << party.name << ": " << party.seats << " seats\n";
};
chamber->onSelectionChange = [ptr = chamber.get()]() {
    size_t sel = ptr->GetSelectedParty();
    if (sel == SIZE_MAX) std::cout << "Selection cleared\n";
};
```

## Demo Application

The demo page lives in `Apps/DemoApp/UltraCanvasParliamentDiagramExamples.cpp`
(`Diagrams > Parliament Diagram`) and shows four tabs:

1. **Bundestag** — the 2021 German Bundestag as a classic hemicycle.
2. **European Parliament** — the 2024 groups in a 200° horseshoe with the
   legend on the right.
3. **House of Commons** — the 2024 Commons on Westminster benches with
   rounded seats and a hollow Speaker's seat.
4. **Coalition Builder** — the Bundestag parties with no government set;
   clicking parties adds them to a coalition, the seats outside it fade, and
   the status bar reports how far the coalition is from a majority.

The sidebar switches every tab between the layouts (hemicycle, horseshoe,
circle, Westminster, grid), seat shapes, row counts, legend positions, the
majority marker, government highlighting and the light/dark theme. The
statistics panel is driven by `onPartyHover` and the seat-counting helpers.

## Notes & Best Practices

- **Order matters:** parties are drawn in insertion order, so add them along
  the political spectrum for the conventional left-to-right hemicycle.
- **Row count:** leave it automatic unless a chamber's real number of benches
  matters; a fixed count that is too small packs the seats closer along the
  arcs rather than dropping any.
- **Vacant seats and the Speaker:** add them as a party with `vacant = true`;
  they are drawn hollow in the chamber and in the legend and still count
  towards the total.
- **Government flag:** it drives the Westminster benches, the government
  highlight, `GetGovernmentSeats()` and the "Government" line of the tooltip;
  it has no effect on where a party sits in the arc layouts.
- **Seat positions:** `GetSeats()` holds the placed seats after the first
  render, for overlays or custom hit testing in the host page.
- **Redraws** are requested automatically by every setter; no manual
  invalidation is needed.
