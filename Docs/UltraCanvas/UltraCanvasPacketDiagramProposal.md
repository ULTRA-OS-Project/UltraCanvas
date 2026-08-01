# UltraCanvas Packet Diagrams — Research & Feature Proposal

Status: **Phase 0 and Phase 1 implemented** — see
[`UltraCanvasPacketDiagram.md`](UltraCanvasPacketDiagram.md) for the API
documentation and `Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp` for the
demo. This document remains the research write-up and the roadmap for
Phases 2 and 3.

Delivered: the shared `UltraCanvasChartLegend` (§10.1 G1–G9, G11); the
dependency-free `UltraCanvasPacketModel` / `UltraCanvasPacketLayout` /
`UltraCanvasPacketParser` / `UltraCanvasPacketTemplates` units; the
`UltraCanvasPacketDiagram` element with the word-grid, proportional and lane
layouts, row-wrapping fields with continuation edges, layers and sub-fields,
flag stacking, highlight sets, banding, bit and byte rulers with stacked-digit
labels, offset and word gutters, dimension rails, twin braces, the monochrome
theme, tooltips and click callbacks; the Mermaid-compatible parser with ASCII
and Mermaid emitters; the validators; eight RFC-pinned protocol templates;
`Tests/PacketLayoutTest.cpp` (80+ assertions, registered with CTest); the
six-tab demo; and the API documentation.

Still open from Phase 1: JSON load/save via `UltraCanvasJSON` (X3) and
on-diagram validation badges (V6's visual half). Everything else outstanding
is Phase 2 or 3 as scheduled in §11.

Author: UltraCanvas Framework
Last Modified: 2026-07-31

> **Scope note.** "Packet diagram" turned out to be an umbrella term covering
> several structurally unrelated diagram types. This document therefore
> proposes a **family** of elements sharing one data model, not a single
> widget. §1 defines the taxonomy; §2 maps every reference image onto it; §3
> shows which families the framework already covers; §§5–9 give the per-family
> feature lists.
>
> **Review status: all open questions resolved (2026-07-31).** The decisions
> are recorded in §12 and are already reflected in the architecture, the
> feature lists and the delivery order. Notably: the ladder element ships as
> the framework's general-purpose `UltraCanvasSequenceDiagram` with a packet
> preset, a **shared legend component** is pulled out as its own deliverable,
> and the proposed UltraNet coupling was **rejected** — see §12.6.

---

## 1. The packet diagram family

The eight reference images do not describe one diagram — they describe four
different questions, and the answers have almost nothing in common
geometrically. The taxonomy below is the core finding of this research.

| # | Variant | Answers the question | Geometry | Status |
|---|---|---|---|---|
| **A** | **Packet Structure** (header / bitfield / format diagram) | *What do the bytes look like?* | Bit-proportional cell grid | **New element needed** |
| **B** | **Packet Switching / Routing** | *How do packets get across the network?* | Topology graph + tokens in transit | **New element on top of `UltraCanvasNodeDiagram`** |
| **C1** | **Packet Processing Flow** (netfilter/iptables, NIC ingress, kernel stack) | *What happens to a packet inside a box?* | Flowchart with decisions | **Covered** by `UltraCanvasFlowChart` (+ swimlanes) |
| **C2** | **Packet Flow Graph / Ladder** (Wireshark *Statistics → Flow Graph*) | *Who sent what, when?* | Lifelines + time-ordered arrows | **New `UltraCanvasSequenceDiagram`** — general-purpose, with a packet preset |
| **D** | **Packet Data** (hex dump + dissection tree) | *What are the actual values?* | Monospace byte pane + tree | **New companion view** |
| **E** | **Encapsulation / OSI layering** | *Which protocol wraps which?* | Nested PDU bands | **Mode of A** |
| **F** | **Packet Timing / bitstream** (SDIO, SPI, CAN) | *What does the bus do bit by bit?* | Lanes over time | **Mode of A** |
| **G** | **Fragmentation / reassembly** | *How is one datagram split into MTU pieces?* | Parent PDU → offset-indexed children | **Mode of A** |
| **H** | **Packet statistics** (throughput, RTT, TCP time-sequence, I/O graph) | *How is the flow performing?* | XY charts | **Already covered** by the existing chart elements — explicitly out of scope |
| **I** | **Protocol state machine** (TCP states) | *What state is the endpoint in?* | State graph | **Already covered** by `UltraCanvasNodeDiagram` / `UltraCanvasFlowChart` |
| **J** | **Queueing / buffer occupancy** at a router | *Where does congestion happen?* | Queues at graph nodes | **Mode of B** |

The three families that need real work are **A** (structure), **B**
(switching) and **C2** (flow graph). **D** is a companion pane that makes A and
C2 far more useful. Everything else is a mode of one of those, or already
exists in the framework.

Two supporting deliverables fall out of the same work and are scoped in §10:
a **shared legend component** (§10.1) and **swimlanes on
`UltraCanvasFlowChart`** (§10.2) — both are framework-wide wins that the packet
work merely forces into the open.

The variants are recognised as distinct in the literature: packet *flow*
diagrams show data movement and processing sequences, packet *switching*
diagrams illustrate topology and routing paths, and packet *structure*
diagrams detail the internal format of an individual packet.

### 1.1 The canonical structure form: the RFC word grid

For family A, the dominant convention comes from IETF RFCs (791/793 and
everything since): a grid **32 bits wide**, with a *bit ruler* across the top,
fields laid left-to-right MSB-first in network byte order, and any field wider
than the remaining space in a row **wrapping onto the next row**. A
variable-length trailer is drawn as an open or ragged box.

The convention is entrenched enough that the IETF has an expired draft,
*Describing Protocol Data Units with Augmented Packet Header Diagrams*
(draft-mcquistin-augmented-ascii-diagrams), motivated by the fact that "packet
diagram formats vary within and between RFC documents, making it difficult to
build tools to generate parsers from specifications". A machine-readable packet
model is therefore not a nicety — it is what the standards community has been
asking for.

### 1.2 Four layout sub-families inside family A

| Sub-layout | Width means | Typical use | Images |
|---|---|---|---|
| **Word grid** (RFC style) | Bits, wrapped every *N* (usually 32) | IP/TCP/UDP headers, RFC figures | 1, 2, 4, 6 |
| **Proportional linear** | Bytes, one continuous strip | Embedded/wireless frames | 3 |
| **Encapsulation stack** | Nesting depth | "TCP segment inside an IP packet" | 4 (top band) |
| **Lane / bitstream** | Time, one row per signal | SDIO, SPI, I²C, CAN, USB | 5 |

### 1.3 Existing tooling — the state of the art

| Tool | What it contributes |
|---|---|
| **Mermaid `packet`** (v11.0+) | Text syntax `0-15: "Source Port"` plus auto-advancing `+16:`; wraps at a configurable bits-per-row; ships TCP and UDP examples. The simplest possible authoring grammar — worth adopting near-verbatim. |
| **WaveDrom `bitfield`** | Register renderer with `name`/`bits`/`attr`/`type` per field and options `lanes` (default 2), `bits` (default 32), `vspace`, `hspace`, `fontsize`, `compact`, `hflip`, `vflip`, `bigendian`, `trim`, `offset`. Establishes multi-lane layout, the attribute row, and the endianness/flip switches. |
| **Protocol** (luismg, ASCII generator) | RFC-like ASCII header diagrams from a compact field list, with an optional bit-count ruler. Establishes the ASCII export target. |
| **draft-mcquistin augmented diagrams** | Formalises the diagram as a *parseable specification*. Establishes the import/validation ambitions. |
| **Wireshark packet-diagram pane** | Renders the diagram for the selected packet from the real dissected bytes. Establishes the "bind actual data" requirement (family D ↔ A sync). |
| **Wireshark *Statistics → Flow Graph*** | Column-per-endpoint ladder showing time, frame size, sequence number and TCP ports; selecting an arrow highlights the packet in the main window. This *is* family C2, and its feature set is the target. |
| **EventHelix VisualEther** | Converts pcap into sequence diagrams — the C2 import path from real captures. |
| **Netfilter packet-flow diagram** (Wikimedia/Thermalcircle) | The canonical C1 figure: five hook points, tables and chains as a decision flowchart. |

Sources consulted:
[Mermaid — Packet Diagram](https://mermaid.js.org/syntax/packet.html),
[Mermaid packet.md source](https://github.com/mermaid-js/mermaid/blob/develop/packages/mermaid/src/docs/syntax/packet.md),
[WaveDrom bitfield](https://github.com/wavedrom/bitfield),
[WaveDrom bit-field guide](https://observablehq.com/@drom/wavedrom-bit-field-guide),
[Protocol — an ASCII header generator](https://www.luismg.com/protocol/),
[McQuistin, Band, Jacob & Perkins, *Describing Protocol Data Units with Augmented Packet Header Diagrams*](https://datatracker.ietf.org/doc/html/draft-mcquistin-augmented-ascii-diagrams),
[Wireshark issue 20820 — packet diagram pane](https://gitlab.com/wireshark/wireshark/-/issues/20820),
[Flow Graph in Wireshark — GeeksforGeeks](https://www.geeksforgeeks.org/ethical-hacking/flow-graph-in-wireshark/),
[EventHelix VisualEther](https://www.eventhelix.com/visualether/),
[Netfilter packet flow (Wikimedia Commons)](https://commons.wikimedia.org/wiki/File:Netfilter-packet-flow.svg),
[Nftables packet flow and Netfilter hooks in detail](https://thermalcircle.de/doku.php?id=blog%3Alinux%3Anftables_packet_flow_netfilter_hooks_detail).

---

## 2. What the eight reference images demand

### Family A — Packet Structure

#### Image 1 — "TCP/IP Packet": stacked headers, flag bits, highlighted fields
A 32-bit word grid holding **two headers plus payload**: IP header, TCP header,
"TCP Data". Left-hand **layer brackets** label the two regions. Rows have
uneven splits (Version | IHL | Type of Service | Total Length). Four cells are
**highlighted green** (Source/Destination Address, Source/Destination Port) — a
semantic emphasis set, not a layer colour. The flags row shows **six one-bit
cells** (U A P R S F) with the letters stacked vertically because the cell is
far too narrow for horizontal text. One field carries an inline constant:
`Protocol=6 (TCP)`.

> Requires: multi-layer packets in one diagram, side brackets, highlight sets
> independent of layer colour, vertical/stacked text fallback, per-field
> constant annotation, title.

#### Image 2 — IPv4 header: dimension rails, row banding, open payload
A **top dimension rail** — double-headed arrow labelled "4 bytes (32 bits)" —
and a **left vertical brace** labelled "Header / 24 bytes". Rows alternate
light/dark blue banding. `DF` and `MF` sit as narrow cells inline with Fragment
Offset. The "Data" region is a **large open box with a soft gradient and a
curved boundary**, signalling "variable length, continues beyond the figure".

> Requires: dimension rails with arrowheads on any edge, labelled side braces,
> alternating row banding, a payload region with its own shape/fill treatment.

#### Image 3 — 802.15.4-style frame: proportional widths + exploded detail
A **single proportional strip** — Length / Header / Payload — with byte-count
annotations *above* (`1 byte`, `21 bytes`, `96 bytes`). Below it, a **second
strip expands the Header** into sub-fields with byte counts *below* (`2 bytes`,
`1 byte`, `2 bytes`, `8 bytes`, `8 bytes`), joined by **diagonal leader lines**
from the parent cell's edges. The expansion is drawn at a *different scale*
from its parent — that is the point of a drill-down.

> Requires: proportional (non-wrapping) layout, hierarchical fields with an
> **exploded detail band**, leader lines, dimension annotations above *and*
> below, per-band independent scaling, a "length prefix" field kind.

#### Image 4 — Networkustad IPv4: encapsulation stack + byte column headers
Two figures stacked. On top, an **encapsulation view**: `Segment Header | Data`
nested inside `IPv4 Header | Data`, colour-coded to show which bytes of the
outer PDU the inner one occupies. Below, the IPv4 word grid with **"Byte 1 …
Byte 4" column headers** instead of a bit ruler, and cells **sub-divided within
one column** (Version | IHL; DS split into DSCP | ECN on a second header row).

> Requires: encapsulation render mode, byte-granular column headers, two-level
> cell headers, per-layer fill colours.

#### Image 5 — SDIO data packet: bit lanes, per-byte colouring, markers
A **lane layout**: one row for the narrow bus (DAT0) and four for the wide bus
(DAT0–DAT3), with a **left signal-name gutter**. Along each lane run
**individual bit cells labelled b7…b0**, coloured by source byte (1st/2nd/3rd/
*n*th Byte, with a matching **legend**). Bracketed by **Start bit** and **End
bit** markers, a **CRC** block per lane, a top bracket annotating `n Byte
Data`, an **ellipsis for repetition**, and a figure caption. On the wide bus
each byte is *striped across the four lanes* — a genuine index remapping.

> Requires: multi-lane layout with a signal gutter, per-bit cells with labels,
> per-byte colour coding, start/end/CRC marker kinds, repetition regions,
> bracket annotations, source-byte legend, caption, bit-to-lane interleave.

#### Image 6 (new) — O'Reilly IPv4: word gutter, stacked ruler digits, twin braces
The classic monochrome RFC figure. The **bit ruler** carries tick marks at
0, 4, 8, 12, 16, 20, 24, **31** — note the last tick is 31, not 32 — with
**two-digit labels written vertically**, one digit per line (`1`/`2` for 12).
A **left gutter numbers the words 1–6** with its own vertical double-headed
"Words" rail, while a **right-hand brace** simultaneously marks "Header". The
final row is a full-width `data begins here …`.

> Requires: a word/row index gutter (independent of the byte-offset gutter),
> stacked multi-digit ruler labels with tick marks and non-uniform tick
> positions, **simultaneous left and right braces**, and a monochrome/print
> theme. Everything else it needs is already in the image 1–5 list.

### Family B — Packet Switching

#### Image 7 (new) — "Packet Switching": topology with packets in transit
Sender and Receiver hosts **outside** a bounded region containing the network;
**routers drawn as green dots** with a legend ("● Routers"); the network's
links drawn as a mesh. Packets are **small numbered squares (1–4) sitting on
the links**, each with a **direction arrow**, at different points along
different edges — i.e. the four packets of one message are **taking different
routes**. At the sender they queue as an ordered train (`4 3 2 1`); at the
receiver they arrive and are **reassembled** back into order.

> Requires: node roles (host / router / switch) with distinct glyphs, a
> **bounded network region** container with a title, packet tokens positioned
> at parameter *t* along a specific link, per-packet independent routes,
> ordered packet trains at the endpoints, reassembly at the destination, a
> legend, and — critically — this is a **static figure**, so good still-frame
> output matters more than animation.

#### Image 8 (new) — Britannica: circuit switching vs packet switching
**Two scenarios stacked in one figure**, each with its own caption. The circuit
case shows a **single highlighted end-to-end path** (red) reserved through the
switches. The packet case shows numbered packets **1–4 diverging onto different
routes** through the same topology and re-ordering on arrival. Device glyphs
distinguish hosts from switches.

> Requires: **multi-scenario comparison panels** with per-panel captions,
> **path highlighting** (a route as an emphasised polyline over the topology),
> per-packet route assignment, and out-of-order arrival / reassembly display.

---

## 3. How this fits the existing UltraCanvas code

The good news from the code survey: **two of the four families are already
covered**, and the third gets its hardest half for free.

| Family | Existing coverage | What is missing |
|---|---|---|
| **A — Structure** | Nothing | The whole element |
| **B — Switching** | `UltraCanvasNodeDiagram` v2.0.6 — force-directed and manual layout, `NodeDiagramLink` with `LinkStyle::{Straight,Bezier,SmoothStep,Step}`, arrowheads, zoom/pan, `WorldToScreen`/`ScreenToWorld`, minimap, hit testing, JSON save/load. Topology is **done**. | Packet tokens on links, route model, region containers, scenario panels, animation driver |
| **C1 — Processing flow** | `UltraCanvasFlowChart` — `Process`, `Decision`, `Database`, `Cloud`, `Actor` shapes, orthogonal routing, themes | Swimlanes (grep finds none anywhere in the repo) — a small addition, useful far beyond packets |
| **C2 — Flow graph / ladder** | Nothing — there is **no sequence/ladder diagram element in the framework at all** | The whole element, shipped as the general-purpose `UltraCanvasSequenceDiagram` |
| **D — Packet data** | Nothing packet-specific | Hex pane + dissection tree |
| **H — Statistics** | The full chart suite (`UltraCanvasFinancialChart`, line/area/heatmap …) | Nothing — out of scope by design |
| *(cross-cutting)* | 10 private `RenderLegend`/`DrawLegend` implementations across the chart plugins, behind 5 incompatible position enums (`CircularLegendPosition`, `DumbbellLegendPosition`, `FunnelLegendPosition`, `LegendPosition`, `PolarLegendPosition`) plus a stringly-typed sixth in `UltraCanvasPopulationChart` | One shared legend component (§10.1) |

Shared machinery to reuse rather than duplicate:

| Existing piece | Reuse for |
|---|---|
| `UltraCanvasUIElement` | Base for all three new elements — none of them has axes, data bounds or series, so `UltraCanvasChartElementBase` is the wrong parent (§11.1) |
| `IRenderContext` | Everything the renderers need exists: `FillRectangle`/`DrawRectangle`, `FillRoundedRectangle`, `MoveTo`/`LineTo`/`BezierCurveTo` + `Fill`/`Stroke` (curved payload edge, ladder arrows), `SetLineDash`, `Rotate` + `PushState`/`PopState` (vertical text in 1-bit cells, stacked ruler digits), `ClipRect`, `SetFillPaint(IPaintPattern)` (image-2 gradient), `SetAlpha` |
| `UltraCanvasApplication::StartTimer(ms, periodic, cb)` | The animation driver for family B — timers already fire on the main thread, so no new threading |
| `Plugins/Charts/UltraCanvasColormap.h` | Layer, byte and per-stream palettes — 19 ramps, `SampleColormap`, `QuantizeNorm`; image 5's per-byte colouring is a quantised ramp lookup |
| `Plugins/Charts/UltraCanvasLabelPlacement.h` | Collision-aware dimension labels, leader-line callouts, ladder arrow labels, legend entries |
| `Plugins/Charts/UltraCanvasConnectionRenderer.cpp` | Leader lines from a parent cell to its exploded detail band (image 3) |
| `Plugins/Charts/UltraCanvasHexLayout.h` | The precedent to follow: header-only, dependency-free, unit-testable geometry with no UI includes |
| `UltraCanvasTooltipManager` | Hover read-out in all four views |
| `UltraCanvasJSON` | Load/save of specs, topologies and traces (never expose yyjson — house rule) |
| `UltraCanvas/CMakeLists.txt:389` (`Plugins/Diagrams/…`) | Where new `.cpp` files register — the source list is explicit, not globbed |

### 3.1 What these elements must *not* depend on

**No dependency on UltraNet, in either direction.** A packet diagram is a
drawing widget: it consumes a `PacketModel` (field table) and, optionally, a
`const uint8_t*`. It performs no I/O, opens no sockets and knows no protocol
semantics. Equally, `Plugins/UltraNet/` must not gain a dependency on the
diagram model in order to publish field tables — that would make a networking
plugin link against a UI plugin to describe its own headers.

The two plugin trees stay independent, and an application that wants both
simply wires them together in app code:

```cpp
// Application code — the only place the two modules meet.
UltraNetResult r = UltraNetRtpReceive(handle, buffer, sizeof(buffer));
packetDiagram->SetModel(UltraCanvas::PacketTemplates::RTP());   // static field table
packetDiagram->SetPacketBytes(buffer, r.bytesTransferred);      // the widget just draws
```

The protocol templates (§5.11) are therefore **plain static data** living in
`Plugins/Diagrams/UltraCanvasPacketTemplates.cpp` — field names, offsets and
lengths, no parsing code. See §12.6 for why the originally proposed coupling
was rejected and what replaces it.

---

## 4. Proposed architecture

### 4.1 One model, four views

The unifying idea: a captured packet should be describable **once** and shown
in every view, with selection synchronised between them.

```
                    ┌──────────────────────────────┐
                    │   UltraCanvasPacketModel     │  field/layer model  (no UI)
                    │   UltraCanvasPacketTrace     │  packets, endpoints, routes, times
                    └──────────────┬───────────────┘
        ┌──────────────┬───────────┴────────┬──────────────────┐
        ▼              ▼                    ▼                  ▼
  PacketDiagram   SequenceDiagram     PacketSwitchingDiagram  PacketBytesView
   (family A)     + packet preset       (family B)             (family D)
   structure        (family C2)        topology+tokens         hex + tree
                    ladder/time      : UltraCanvasNodeDiagram
        └──────────────┴────────────────────┴──────────────────┘
                       PacketSelectionBus (id ↔ id ↔ byte range)
```

Click a packet arrow in the ladder → its structure appears in A and its bytes
highlight in D → its route lights up in B. That cross-view sync is the feature
no existing tool ships in one embeddable widget, and it costs little once the
model is shared.

### 4.2 Files

All four elements derive from `UltraCanvasUIElement` and live in
`Plugins/Diagrams/` (§12.1).

```
# dependency-free model/algorithm units (no UI includes, unit-testable)
include/Plugins/Diagrams/UltraCanvasPacketModel.h            # field/layer model
include/Plugins/Diagrams/UltraCanvasPacketLayout.h           # offsets -> cell rects
include/Plugins/Diagrams/UltraCanvasPacketParser.h           # text spec -> model
include/Plugins/Diagrams/UltraCanvasPacketTrace.h            # packets/endpoints/routes
include/Plugins/Diagrams/UltraCanvasPacketTemplates.h        # static protocol field tables

# elements
include/Plugins/Diagrams/UltraCanvasPacketDiagram.h          # family A  (+E/F/G modes)
include/Plugins/Diagrams/UltraCanvasSequenceDiagram.h        # family C2 (general purpose)
include/Plugins/Diagrams/UltraCanvasPacketSwitchingDiagram.h # family B  : UltraCanvasNodeDiagram
include/Plugins/Diagrams/UltraCanvasPacketBytesView.h        # family D
Plugins/Diagrams/UltraCanvasPacket*.cpp
Plugins/Diagrams/UltraCanvasSequenceDiagram.cpp

# shared, framework-wide (§10.1)
include/Plugins/Charts/UltraCanvasChartLegend.h
Plugins/Charts/UltraCanvasChartLegend.cpp
```

### 4.3 The structure model (family A)

Everything is expressed in **bits**, because bits are the only unit covering
all of images 1–6; bytes are a presentation choice made by the ruler and the
label formatter.

```cpp
enum class PacketFieldKind {
    Fixed,        // ordinary field of known bit length
    Flag,         // 1-bit; renders with stacked/rotated text
    Reserved,     // greyed, usually unnamed
    Padding,      // hatched
    LengthPrefix, // this field's value gives the length of another (image 3)
    Variable,     // length not known statically (options)
    Payload,      // open-ended region, special shape/fill (images 1, 2)
    Checksum,     // CRC/FCS block (image 5)
    Marker,       // start bit / end bit / delimiter (image 5)
    Ellipsis      // "…", a repetition elision
};

struct PacketField {
    std::string      id, name, description;
    uint64_t         bitOffset = 0;      // absolute, within its layer
    uint64_t         bitLength = 0;      // 0 => variable / to end
    PacketFieldKind  kind = PacketFieldKind::Fixed;
    std::string      constantText;       // "6 (TCP)"  -> "Protocol=6 (TCP)"
    std::string      attrText;           // WaveDrom-style second line: "RO"/"RW"
    std::vector<PacketField> children;   // sub-fields (DSCP|ECN inside DS)
    bool             hasColorOverride = false;
    Color            color;
    void*            userData = nullptr;
};

struct PacketLayer {           // one protocol layer, e.g. "IP Header", "TCP"
    std::string id, name, bracketLabel;
    std::vector<PacketField> fields;
    Color   color;
    bool    collapsed = false;
};

struct PacketModel {
    std::string title, caption;
    std::vector<PacketLayer> layers;
    uint32_t bitsPerRow = 32;
    PacketBitNumbering numbering = PacketBitNumbering::MsbFirst;
    PacketEndianness   endianness = PacketEndianness::Big;   // network order
};
```

### 4.4 The trace model (families B, C2, D)

```cpp
struct PacketEndpoint { std::string id, label; PacketNodeRole role; };  // Host/Router/Switch/…
struct PacketRoute    { std::string packetId; std::vector<std::string> nodeIds; };

struct TracePacket {
    std::string id, label;              // "1", "SYN", "GET /index.html"
    std::string sourceId, destId;
    double      timestamp = 0.0;        // seconds, for the ladder
    uint64_t    sizeBytes = 0;
    std::string protocol, streamId;
    PacketDeliveryState state;          // Delivered / Lost / Retransmitted / Duplicate
    std::shared_ptr<PacketModel> structure;   // optional: its own field layout
    std::vector<uint8_t> bytes;               // optional: the real bytes
};

struct PacketTrace {
    std::vector<PacketEndpoint> endpoints;
    std::vector<TracePacket>    packets;
    std::vector<PacketRoute>    routes;       // family B only
};
```

### 4.5 The structure layout engine

Pure geometry, no `IRenderContext`, fully unit-testable — this is where the
real work is and where bugs would otherwise hide:

* **Row splitting.** A field crossing a row boundary is emitted as *several*
  `PacketCell`s, each flagged `continuesLeft`/`continuesRight` so the renderer
  suppresses the shared border and dashes the cut edge. Getting this wrong is
  the most visible packet-diagram bug: a 64-bit Sequence Number over a 32-bit
  grid must read as **one** field, not two.
* **Four layout strategies** behind one interface — `WordGrid`,
  `ProportionalLinear`, `Encapsulation`, `Lanes`.
* **Proportional scaling with clamping.** Image 3 puts a 1-byte field next to a
  96-byte field; a strict 1:96 ratio makes the Length cell unreadable. Needs
  `MinCellWidth` plus a `LengthScale` (`Linear`/`Sqrt`/`Log`/`Clamped`) and a
  visible broken-scale marker when clamping applies.
* **Bit-to-lane interleave** for wide buses: `Sequential`, `Striped(n)`, custom.
* **Text fitting decided in layout, not paint**: full → abbreviated → rotated
  90° → stacked glyphs → tooltip-only, measured once and cached per cell.
* Output is a plain `PacketLayoutResult { cells, rulerTicks, brackets, rails,
  leaders }` — testable against known headers (feed it RFC 791: assert 20
  bytes, 14 fields, 5 rows, `Source Address` at bits 96–127).

### 4.6 The switching element (family B)

`UltraCanvasPacketSwitchingDiagram` **derives from** `UltraCanvasNodeDiagram`
rather than reimplementing a graph or forwarding dozens of methods (§12.2).
Because a packet figure is not a flow editor, the inherited authoring surface
is suppressed in the constructor and the editing entry points are re-declared
`private` so they cannot be reached through the derived type:

```cpp
class UltraCanvasPacketSwitchingDiagram : public UltraCanvasNodeDiagram {
public:
    UltraCanvasPacketSwitchingDiagram(...) {
        SetInteractionLocked(true);      // no drag-to-connect, no node dragging
        SetShowControls(false);          // no editor overlay
        SetSnapToGrid(false);
    }
    // topology is declared, not drawn by hand:
    void AddNode(const std::string& id, const std::string& label, PacketNodeRole role);
    void AddLink(const std::string& fromId, const std::string& toId);
private:
    // hidden: handle/port editing, selection boxes, delete-key handling
    using UltraCanvasNodeDiagram::AddHandle;
    using UltraCanvasNodeDiagram::SetHandlesVisible;
    /* … */
};
```

Read-only inherited behaviour that packet figures *do* want — force-directed
layout, `FitView`, zoom/pan, `WorldToScreen`, minimap, JSON load — stays public.

One small upstream addition to `UltraCanvasNodeDiagram` is required:

```cpp
// New public API on UltraCanvasNodeDiagram — sample a point along a routed link.
Point2Dd GetLinkPointAt(const std::string& linkId, double t) const;  // t in [0,1]
Point2Dd GetLinkTangentAt(const std::string& linkId, double t) const;
```

That one function is what lets a packet token sit at 40% along a Bezier link
with its arrow aligned to the local tangent, for every `LinkStyle`. Everything
else — layout, zoom, pan, hit testing, JSON — is inherited.

**The refactor this implies** (approved, §12.3). Today each `LinkStyle` builds
its geometry inline inside the render path. The change extracts that into a
pure function and leaves rendering as a consumer:

```cpp
// Private, in UltraCanvasNodeDiagram — one place that knows link geometry.
struct LinkPath { std::vector<Point2Dd> points; bool isBezier; /* control pts */ };
LinkPath BuildLinkPath(const NodeDiagramLink& link) const;   // per LinkStyle
```

`RenderLink` then draws `BuildLinkPath(...)`, and `GetLinkPointAt` walks the
same path by arc length. Properties of the change worth stating up front:

* **Purely additive in public API** — two new `const` methods; no existing
  signature changes, so it is a **minor** version bump (2.1.0), not a breaking
  one. This matters because `UltraCanvasNodeDiagram`'s changelog shows it is
  actively used and has been through several behavioural fixes.
* **Behaviour-preserving by construction** — the extracted function must return
  the geometry the renderer already produced. The regression risk is arrow
  placement on `SmoothStep`/`Step` links, which the changelog (2.3.1, 2.0.0)
  shows has been fixed before; a golden-geometry unit test per `LinkStyle`
  pins it.
* **Independently useful** — link labels, hit testing and any future link
  decoration (bandwidth ticks, flow arrows) all currently re-derive geometry
  the renderer already computed. One source of truth removes that class of bug.

Animation is driven by `UltraCanvasApplication::StartTimer`, but note that
**both reference images are static figures**: the element must produce a good
still frame from a fixed `SetSimulationTime(t)`, with playback as a layer on
top rather than a prerequisite.

### 4.7 The parser

`UltraCanvasPacketParser` accepts a Mermaid-compatible spec, because that
grammar is already the lingua franca:

```
packet
title TCP Header
0-15: "Source Port"
16-31: "Destination Port"
+32: "Sequence Number"
106: "URG"
```

UltraCanvas extensions (layers, kinds, nesting, colours) are trailing
modifiers, so plain Mermaid input still parses. The same unit gains an ASCII
**exporter** (the RFC `+-+-+` form) and, in P3, an ASCII *importer* — parsing
an RFC figure back into a model is the highest-value trick this element could
do for a networking codebase.

---

## 5. Family A — Packet Structure: feature list

**P1** = core, ship first; **P2** = completes the reference images;
**P3** = polish / advanced.

### 5.1 Data model & construction
| # | Feature | Phase |
|---|---|---|
| M1 | Bit-granular field model: name, offset, length, description | P1 |
| M2 | Auto-advancing append (`AddField(name, bits)`) alongside explicit offsets | P1 |
| M3 | Field kinds: Fixed, Flag, Reserved, Padding, Variable, Payload, Checksum, Marker, LengthPrefix, Ellipsis | P1 |
| M4 | Multiple layers in one diagram, each with name/colour/bracket (image 1) | P1 |
| M5 | Nested sub-fields, arbitrary depth (DS → DSCP + ECN; image 4) | P1 |
| M6 | Per-field constant/expected value text (`Protocol=6 (TCP)`; image 1) | P1 |
| M7 | Per-field attribute line (WaveDrom `attr`: RO/RW/MBZ) | P2 |
| M8 | `LengthPrefix` linkage — a field declaring another's length (image 3) | P2 |
| M9 | Repeating groups / arrays with a count or "n" ellipsis (image 5) | P2 |
| M10 | Conditional / optional fields (present only if a flag is set), drawn dashed | P3 |
| M11 | Enumerated value tables per field (tooltips, detail pane) | P3 |
| M12 | Per-field id + user data pointer for host-app linkage | P1 |

### 5.2 Layout modes
| # | Feature | Phase |
|---|---|---|
| L1 | **WordGrid** — RFC style, wraps every *N* bits (8/16/32/64/custom, default 32) | P1 |
| L2 | Fields wrapping across rows as one logical field (continuation edges, no repeated label) | P1 |
| L3 | **ProportionalLinear** — one continuous strip, width ∝ size (image 3) | P1 |
| L4 | **Lanes** — one row per signal with a left signal-name gutter (image 5) | P2 |
| L5 | **Encapsulation** — nested PDU bands (image 4, top; variant E) | P2 |
| L6 | Min cell width + scale modes (Linear/Sqrt/Log/Clamped) + broken-scale marker | P2 |
| L7 | Bit-to-lane interleave (Sequential / Striped(n) / custom) for wide buses | P2 |
| L8 | Exploded detail band: expand a field into a second, independently scaled strip (image 3) | P2 |
| L9 | Auto-fit rows/cell height, or fixed metrics with scrolling | P1 |
| L10 | Vertical (top-to-bottom) orientation | P3 |
| L11 | LSB-first numbering, little-endian order, `hflip`/`vflip` equivalents | P2 |
| L12 | Row-range windowing for very large PDUs | P3 |
| L13 | **Fragmentation mode** (variant G): one parent PDU rendered as offset-indexed MTU fragments with a reassembly bracket | P3 |

### 5.3 Cell rendering & typography
| # | Feature | Phase |
|---|---|---|
| R1 | Rectangular cells, configurable border width/colour/radius | P1 |
| R2 | Text fit cascade: full → abbreviated → rotated 90° → stacked glyphs → tooltip-only (image 1 flags) | P1 |
| R3 | Two-line cell text (name + value, or name + attr) | P1 |
| R4 | Sub-field cells inside the parent cell, second header row (image 4) | P1 |
| R5 | Payload styling: gradient fill, curved or ragged edge, open right side (images 1, 2) | P2 |
| R6 | Dashed / hatched treatment for Reserved, Padding, optional fields | P2 |
| R7 | Marker glyphs for start bit / end bit / delimiters (image 5) | P2 |
| R8 | Per-bit cells with `b7…b0` labels when zoom allows (image 5) | P2 |
| R9 | Ellipsis cell (`…`) for elided repetition, with a count label | P2 |
| R10 | Alternating row banding (image 2) | P1 |
| R11 | Drop shadow / bevel option for presentation decks | P3 |
| R12 | Per-field icon or badge slot | P3 |

### 5.4 Rulers, dimensions & annotations
| # | Feature | Phase |
|---|---|---|
| A1 | Bit ruler across the top, tick every 1/4/8 bits, configurable | P1 |
| A2 | Byte-column headers (`Byte 1…Byte 4`) as an alternative (image 4) | P1 |
| A3 | Left offset gutter per row: bit, byte, or hex offset | P1 |
| A4 | **Word/row index gutter** with its own rail label (`Words 1…6`; image 6) — independent of A3 | P1 |
| A5 | **Stacked multi-digit ruler labels** with tick marks and non-uniform tick positions (0,4,…,31; image 6) | P1 |
| A6 | Dimension rail: double-headed arrow + label on any edge (image 2) | P1 |
| A7 | Per-field size annotations above and/or below the strip (image 3) | P1 |
| A8 | Side braces spanning a row range with a label — **left and right simultaneously** (images 1, 2, 6) | P1 |
| A9 | Top brackets spanning a cell range with a count (`n Byte Data`; image 5) | P2 |
| A10 | Leader lines from a parent cell to its exploded detail band (image 3) | P2 |
| A11 | Free callout notes anchored to a field, collision-aware | P2 |
| A12 | Diagram title and figure caption (images 1, 5) | P1 |
| A13 | Size label unit/format control (bits / bytes / auto, custom formatter) | P1 |
| A14 | Total-size footer (`Total: 20 bytes (160 bits)`) | P2 |

### 5.5 Colour, theming & legend
| # | Feature | Phase |
|---|---|---|
| S1 | Per-field explicit colour override | P1 |
| S2 | Per-layer colour applied to its fields (images 1, 4) | P1 |
| S3 | Colour-by-kind palette (payload vs reserved vs checksum) | P1 |
| S4 | Colour-by-source-byte from a quantised colormap (image 5) | P2 |
| S5 | Highlight set — emphasise any subset of fields independently of layer colour (image 1) | P1 |
| S6 | Full theme surface (background, fill, border, text, ruler, banding, gutter) with light, dark and **monochrome/print** presets (image 6) | P1 |
| S7 | Reuse of the 19 `UltraCanvasColormap` ramps for automatic colouring | P1 |
| S8 | Legend: swatch + label for layers, kinds or source bytes; four edge positions (image 5) — **consumes the shared `UltraCanvasChartLegend` (§10.1), does not reimplement it** | P2 |
| S9 | Automatic readable text colour (contrast against cell fill) | P1 |
| S10 | Hatch/pattern fills for accessibility and monochrome print | P3 |
| S11 | Per-field opacity + "dim all but the selection" focus mode | P2 |

### 5.6 Live byte binding & decoding
| # | Feature | Phase |
|---|---|---|
| B1 | `SetPacketBytes(const uint8_t*, size_t)` — bind real captured bytes | P2 |
| B2 | Extract and display each field's actual value (dec/hex/bin, per-field format) | P2 |
| B3 | Value formatters: IPv4/IPv6 address, MAC, timestamp, enum name, flag list, custom callback | P2 |
| B4 | Resolve `Variable`/`LengthPrefix` lengths from bound data and re-lay out | P2 |
| B5 | Two-way highlight sync with the family D bytes view | P2 |
| B6 | Checksum verification indicator on `Checksum` fields | P3 |
| B7 | Diff mode: two byte buffers on one diagram, changed fields marked | P3 |
| B8 | Streaming update from a capture source without rebuilding the model | P3 |

### 5.7 Interaction
| # | Feature | Phase |
|---|---|---|
| I1 | Hover highlight + tooltip: name, bit offset, byte offset, length, description, value | P1 |
| I2 | Click callback `(fieldId, layerId, bitOffset, bitLength)` and selection state | P1 |
| I3 | Keyboard navigation between fields with a focus ring — accessibility | P2 |
| I4 | Expand/collapse sub-fields and whole layers | P2 |
| I5 | Drill-down into an exploded detail band with a back path (image 3) | P2 |
| I6 | Zoom + pan, ruler tick density adapting; double-click to reset | P2 |
| I7 | Copy field value / spec line / whole diagram as ASCII | P2 |
| I8 | Editing mode: drag a boundary to resize, drag to reorder, inline rename | P3 |
| I9 | Search / jump to a field by name or offset | P3 |
| I10 | Context menu (copy value, copy offset, hide field, set colour) | P3 |

### 5.8 Import / export / interop
| # | Feature | Phase |
|---|---|---|
| X1 | Mermaid-compatible `packet` text parser (`0-15:` and `+16:` forms) | P1 |
| X2 | UltraCanvas grammar extensions: layers, kinds, colours, nesting, lanes | P1 |
| X3 | JSON load/save via `UltraCanvasJSON` | P1 |
| X4 | ASCII/RFC `+-+-+` export with optional bit ruler | P2 |
| X5 | WaveDrom-bitfield JSON import (`name`/`bits`/`attr`/`type`) | P2 |
| X6 | SVG / PNG / PDF export at a chosen scale | P2 |
| X7 | ASCII/RFC diagram **import** — parse an RFC figure back into a model | P3 |
| X8 | Kaitai Struct `.ksy` import (fixed-size seq subset) | P3 |
| X9 | Mermaid `packet` **export**, for round-tripping into Markdown docs | P2 |
| X10 | C/C++ struct generation from the model, and the reverse | P3 |

### 5.9 Validation & diagnostics
| # | Feature | Phase |
|---|---|---|
| V1 | Overlap detection — two fields claiming the same bits | P1 |
| V2 | Gap detection — unclaimed ranges, optionally auto-filled as Reserved | P1 |
| V3 | Total-length check against a declared expected size | P1 |
| V4 | Row-boundary sanity warnings (misalignment that usually means a spec typo) | P2 |
| V5 | Duplicate id / empty name / zero-length diagnostics | P1 |
| V6 | Structured `PacketValidationResult` (severity, message, field id) + optional on-diagram warning badges | P1 |

### 5.10 Engineering
| # | Feature | Phase |
|---|---|---|
| E1 | Model + layout headers free of UI dependencies, unit-tested against IPv4/TCP/UDP/802.15.4 | P1 |
| E2 | Layout cache keyed on a model/size generation counter | P1 |
| E3 | Factory helper `CreatePacketDiagram(...)` per house convention | P1 |
| E4 | `Docs/UltraCanvas/UltraCanvasPacketDiagram.md` + `…Examples.md` in house style | P1 |
| E5 | `Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp` reproducing images 1–6, registered in the demo tree | P1 |
| E6 | `Tests/` coverage for layout, parser and validators | P1 |
| E7 | Registration in `UltraCanvas/CMakeLists.txt` and `Masterfile_modules.md` | P1 |

### 5.11 Built-in protocol template library
| # | Feature | Phase |
|---|---|---|
| P1t | `PacketTemplates::IPv4()`, `IPv6()`, `TCP()`, `UDP()`, `ICMP()`, `ARP()`, `Ethernet()` | P1 |
| P2t | `DNS()`, `DHCP()`, `TLSRecord()`, `HTTP2Frame()`, `QUICLongHeader()`, `WebSocketFrame()` | P2 |
| P3t | `RTP()`, `RTCP()` — plain field tables written against RFC 3550, with **no dependency on `Plugins/UltraNet/` in either direction** (§3.1, §12.6) | P2 |
| P6t | RFC-pinning unit tests per template (e.g. RTP: version @ 0–1, PT @ 9–15, SSRC @ 64–95) — the drift protection that replaces the rejected coupling | P1 |
| P4t | Bus/embedded: `SDIOData()`, `CANFrame()`, `Modbus()`, `MQTT()`, `IEEE802_15_4()`, `USBPacket()` | P3 |
| P5t | `Stack({Ethernet(), IPv4(), TCP()})` composition producing an image-1 style layered diagram | P2 |

---

## 6. Family B — Packet Switching: feature list

Built on `UltraCanvasNodeDiagram`; only the packet layer is new.

### 6.1 Topology & scene
| # | Feature | Phase |
|---|---|---|
| PS1 | Node roles with distinct glyphs: Host, Router, Switch, Firewall, Server, Cloud, Sender/Receiver (images 7, 8) | P1 |
| PS2 | Reuse of the full `UltraCanvasNodeDiagram` layout/zoom/pan/minimap/JSON stack | P1 |
| PS3 | **Bounded network region** container — a labelled box or cloud grouping a node subset (image 7) | P1 |
| PS4 | Link attributes: bandwidth, latency, cost, capacity — mapped to width, dash, colour or label | P2 |
| PS5 | `GetLinkPointAt(linkId, t)` / `GetLinkTangentAt` added upstream to `UltraCanvasNodeDiagram` | P1 |
| PS6 | Legend (`● Routers`) via the shared `UltraCanvasChartLegend` (§10.1), diagram title, per-scenario caption | P1 |
| PS7 | **Multi-scenario comparison panels** — two or more topologies stacked or side by side, each captioned (image 8) | P2 |

### 6.2 Packets & routing
| # | Feature | Phase |
|---|---|---|
| PS8 | Packet tokens: numbered/labelled squares placed at `(linkId, t)` with a direction arrow on the local tangent (image 7) | P1 |
| PS9 | Per-packet **independent routes** through the topology (images 7, 8) | P1 |
| PS10 | Automatic route computation (shortest path / ECMP / explicit list / round-robin over disjoint paths) | P2 |
| PS11 | Ordered packet **train** at an endpoint — the `4 3 2 1` queue at the sender (image 7) | P1 |
| PS12 | **Reassembly display** at the destination: arrival order vs sequence order, with a reorder buffer (images 7, 8) | P2 |
| PS13 | **Path highlighting** — a route drawn as an emphasised polyline for the circuit-switching case (image 8) | P1 |
| PS14 | Packet states: Delivered, Lost, Dropped, Retransmitted, Duplicate — with distinct glyphs | P2 |
| PS15 | Colour by packet, by stream, or by protocol, from a colormap | P1 |
| PS16 | Queue / buffer occupancy at a node, with drop markers (variant J) | P3 |
| PS17 | Broadcast / multicast / flooding fan-out | P3 |

### 6.3 Time & animation
| # | Feature | Phase |
|---|---|---|
| PS18 | **Static snapshot**: `SetSimulationTime(t)` produces a complete still figure — both reference images are static, so this is the primary mode, not a fallback | P1 |
| PS19 | Playback via `UltraCanvasApplication::StartTimer`: play / pause / step / speed, loop | P2 |
| PS20 | Timeline scrubber overlay with the current time read-out | P2 |
| PS21 | Store-and-forward vs cut-through timing models (whole-packet hop delay vs pipelined) | P3 |
| PS22 | Per-link propagation delay and per-node processing delay affecting token positions | P2 |
| PS23 | Trails / motion blur behind a moving packet | P3 |
| PS24 | Frame-sequence export for animated GIF / video | P3 |

### 6.4 Interaction & data
| # | Feature | Phase |
|---|---|---|
| PS25 | Hover tooltip on a packet (id, size, protocol, hop, elapsed) and on a link (utilisation) | P1 |
| PS26 | Click a packet → callback + selection, broadcast on the selection bus to families A/C2/D | P1 |
| PS27 | Click a node → highlight every route traversing it | P2 |
| PS28 | Load topology + routes from JSON; build a trace programmatically | P1 |
| PS29 | Statistics overlay: delivered/lost counts, average hops, end-to-end latency | P3 |
| PS30 | SVG/PNG export | P2 |

---

## 7. Family C2 — `UltraCanvasSequenceDiagram`: feature list

Shipped as the framework's **general-purpose sequence diagram** with a packet
preset, not as a packet-only widget (§12.7). The framework has no sequence
diagram at all today, and the ladder geometry is identical to UML's:
participants ≡ endpoints, messages ≡ packets, time flows down in both. Naming
it for the general case costs nothing and doubles the payoff.

The split is: **PF1–PF17 are the generic engine**, and the packet character
comes entirely from a preset that supplies label formatting, colour-by-stream
and the trace importer.

```cpp
auto seq = UltraCanvas::CreateSequenceDiagram("flow", 300, 20, 20, 640, 480);
seq->ApplyPreset(UltraCanvas::SequencePreset::PacketFlow);   // Wireshark-style
// or  SequencePreset::UML  — participants, messages, activations, notes
```

| # | Feature | Phase |
|---|---|---|
| PF1 | Vertical lifelines, one per participant/endpoint, with header boxes and subtitles (address, role) | P1 |
| PF2 | Time flowing downward; arrows between lifelines, one per packet | P1 |
| PF3 | Arrow labels: protocol, ports, flags, sequence number, frame size — Wireshark's flow-graph column set | P1 |
| PF4 | Time axis modes: absolute, relative-to-first, delta-from-previous | P1 |
| PF5 | Time scaling: uniform rows (readable) vs true-to-scale (shows gaps), with gap compression | P2 |
| PF6 | Slanted arrows conveying propagation delay (send time ≠ receive time) | P2 |
| PF7 | Self-arrows (retransmission, loopback, internal events) | P2 |
| PF8 | Lost/dropped packets: arrow stopping short with an ✗ marker | P2 |
| PF9 | Activation bars / spans on a lifeline | P2 |
| PF10 | Grouping brackets spanning several packets ("3-way handshake", "slow start") | P2 |
| PF11 | Colour by stream, protocol or direction, from a colormap | P1 |
| PF12 | Filtering: by stream, protocol, endpoint pair, time window | P1 |
| PF13 | Virtualised rendering + scrolling for captures of 10⁵+ packets | P2 |
| PF14 | Click an arrow → callback + selection bus (opens the packet in families A and D) | P1 |
| PF15 | Hover tooltip with the full packet summary | P1 |
| PF16 | Endpoint reordering / hiding, and automatic ordering by traffic volume | P2 |
| PF17 | Comment/annotation column beside the ladder | P2 |
| PF18 | Import from a `PacketTrace`, CSV, or a host-supplied pcap reader callback | P1 |
| PF19 | Export to Mermaid `sequenceDiagram` and PlantUML | P2 |
| PF20 | ASCII export (the Wireshark text flow-graph form) | P2 |
| PF21 | SVG/PNG export | P2 |
| **PF22** | **`SequencePreset::PacketFlow`** — Wireshark-style labels, colour-by-stream, time columns, `PacketTrace` importer | P1 |
| **PF23** | **`SequencePreset::UML`** — participants, sync/async/return message arrows, notes | P2 |
| PF24 | UML fragments: `alt` / `opt` / `loop` / `par` boxes with guard labels (reuses PF10's bracket machinery) | P2 |
| PF25 | Participant creation/destruction markers (UML `create`/`destroy`) | P3 |
| PF26 | Mermaid `sequenceDiagram` **import**, so existing docs render natively | P3 |

---

## 8. Family D — Packet Data (bytes view): feature list

| # | Feature | Phase |
|---|---|---|
| PD1 | Hex dump pane: offset column, hex bytes, ASCII gutter, configurable bytes-per-row | P2 |
| PD2 | Dissection tree: collapsible protocol layers → fields → values | P2 |
| PD3 | **Two-way highlight sync**: select a field → its bytes highlight, and select bytes → the field highlights (works against families A and C2 too) | P2 |
| PD4 | Byte range selection, copy as hex / C array / base64 / escaped string | P2 |
| PD5 | Search: hex pattern, ASCII string, goto offset | P2 |
| PD6 | Virtualised rendering for multi-megabyte buffers | P2 |
| PD7 | Per-layer background tinting of the hex pane matching the structure colours | P2 |
| PD8 | Diff of two buffers with changed bytes marked | P3 |
| PD9 | Editable hex (write back into the bound buffer) | P3 |

---

## 9. Proposed API sketch

### Family A — layered TCP/IP packet (image 1)

```cpp
#include "Plugins/Diagrams/UltraCanvasPacketDiagram.h"

auto pkt = UltraCanvas::CreatePacketDiagram("packet1", 100, 20, 20, 720, 520);

pkt->SetTitle("TCP/IP Packet");
pkt->SetLayoutMode(UltraCanvas::PacketLayoutMode::WordGrid);
pkt->SetBitsPerRow(32);
pkt->SetRulerMode(UltraCanvas::PacketRulerMode::BitRuler);

auto& ip = pkt->AddLayer("ip", "IP Header");
ip.AddField("Version", 4);
ip.AddField("IHL", 4);
ip.AddField("Type of Service", 8);
ip.AddField("Total Length", 16);
ip.AddField("Identification", 16);
ip.AddField("Flags", 3);
ip.AddField("Fragment Offset", 13);
ip.AddField("Time to Live", 8);
ip.AddField("Protocol", 8).SetConstantText("6 (TCP)");
ip.AddField("Header Checksum", 16);
ip.AddField("Source Address", 32);
ip.AddField("Destination Address", 32);
ip.AddField("Options", 24, UltraCanvas::PacketFieldKind::Variable);
ip.AddField("Padding", 8, UltraCanvas::PacketFieldKind::Padding);

auto& tcp = pkt->AddLayer("tcp", "TCP");
tcp.AddField("Source Port", 16);
tcp.AddField("Destination Port", 16);
tcp.AddField("Sequence Number", 32);          // wraps cleanly across the row
tcp.AddField("Acknowledgement Number", 32);
tcp.AddField("Data Offset", 4);
tcp.AddField("Reserved", 6, UltraCanvas::PacketFieldKind::Reserved);
for (const char* f : {"U","A","P","R","S","F"})
    tcp.AddField(f, 1, UltraCanvas::PacketFieldKind::Flag);   // stacked text
tcp.AddField("Window", 16);
tcp.AddField("Checksum", 16, UltraCanvas::PacketFieldKind::Checksum);
tcp.AddField("Urgent Pointer", 16);
tcp.AddField("TCP Data", 0, UltraCanvas::PacketFieldKind::Payload);

pkt->SetLayerBrackets(UltraCanvas::PacketEdge::Left);
pkt->SetHighlightFields({"Source Address", "Destination Address",
                         "Source Port", "Destination Port"},
                        UltraCanvas::Color(160, 205, 160, 255));

pkt->SetOnFieldClick([](const UltraCanvas::PacketFieldRef& f) {
    std::cout << f.name << " @ bit " << f.bitOffset << " (" << f.bitLength << ")\n";
});

container->AddChild(pkt);
```

### Family A — O'Reilly monochrome style (image 6)

```cpp
auto oreilly = UltraCanvas::CreatePacketDiagram("ipv4mono", 104, 20, 20, 620, 300);
oreilly->SetModel(UltraCanvas::PacketTemplates::IPv4());
oreilly->SetTheme(UltraCanvas::PacketTheme::Monochrome);
oreilly->SetRulerTicks({0, 4, 8, 12, 16, 20, 24, 31});      // note: 31, not 32
oreilly->SetRulerLabelStyle(UltraCanvas::PacketRulerLabelStyle::StackedDigits);
oreilly->SetRulerTitle("Bits");
oreilly->SetRowIndexGutter(UltraCanvas::PacketRowIndex::WordNumber, "Words");
oreilly->AddBrace(UltraCanvas::PacketEdge::Right, "ip", "Header");   // right AND left
oreilly->SetTrailerText("data begins here …");
```

### Family A — proportional strip with exploded detail (image 3)

```cpp
auto frame = UltraCanvas::CreatePacketDiagram("frame", 102, 20, 20, 700, 300);
frame->SetLayoutMode(UltraCanvas::PacketLayoutMode::ProportionalLinear);
frame->SetSizeUnit(UltraCanvas::PacketSizeUnit::Bytes);
frame->SetLengthScale(UltraCanvas::PacketLengthScale::Clamped, /*minCellPx*/ 48.0f);

auto& f = frame->AddLayer("frame", "");
f.AddFieldBytes("Length:\n[header + payload]", 1, UltraCanvas::PacketFieldKind::LengthPrefix);
auto& hdr = f.AddFieldBytes("Header", 21);
f.AddFieldBytes("Payload", 96, UltraCanvas::PacketFieldKind::Payload);

hdr.AddChildBytes("IEEE header", 2);
hdr.AddChildBytes("Seq. number", 1);
hdr.AddChildBytes("Destination PAN", 2);
hdr.AddChildBytes("Destination address", 8);
hdr.AddChildBytes("Source address", 8);

frame->SetSizeAnnotations(UltraCanvas::PacketEdge::Top);
frame->ExpandField("Header", UltraCanvas::PacketEdge::Bottom);   // detail band + leaders
frame->SetDetailSizeAnnotations(UltraCanvas::PacketEdge::Bottom);
```

### Family A — SDIO wide-bus lane view (image 5)

```cpp
auto sdio = UltraCanvas::CreatePacketDiagram("sdio", 103, 20, 20, 820, 320);
sdio->SetLayoutMode(UltraCanvas::PacketLayoutMode::Lanes);
sdio->SetLanes({"DAT3", "DAT2", "DAT1", "DAT0"});
sdio->SetBitLaneMapping(UltraCanvas::PacketBitLaneMapping::Striped);
sdio->SetShowBitLabels(true);                       // b7 … b0 in each cell
sdio->SetColorMode(UltraCanvas::PacketColorMode::BySourceByte);
sdio->AddMarker(UltraCanvas::PacketEdge::Left,  "Start bit");
sdio->AddMarker(UltraCanvas::PacketEdge::Right, "End bit");
sdio->AddDataBytes(/*count*/ 4, /*ellipsisAfter*/ 3);
sdio->AddChecksumPerLane("CRC", 16);
sdio->AddBracket(UltraCanvas::PacketEdge::Top, "data", "n Byte Data");
sdio->SetCaption("Figure 3-7: Data Packet Format — Usual Data");
```

### Family B — packet switching (image 7)

```cpp
#include "Plugins/Diagrams/UltraCanvasPacketSwitchingDiagram.h"

auto sw = UltraCanvas::CreatePacketSwitchingDiagram("switching", 200, 20, 20, 760, 420);
sw->SetTitle("Packet Switching");

sw->AddNode("s1", "Sender",   UltraCanvas::PacketNodeRole::Host);
sw->AddNode("s2", "Sender",   UltraCanvas::PacketNodeRole::Host);
sw->AddNode("r1", "",         UltraCanvas::PacketNodeRole::Router);
/* … r2 … r6 … */
sw->AddNode("d1", "Receiver", UltraCanvas::PacketNodeRole::Host);

sw->AddLink("s1", "r1");
sw->AddLink("r1", "r3");
/* … the mesh … */

sw->AddRegion("core", "", {"r1","r2","r3","r4","r5","r6"});   // the bounded box
sw->SetLegend({{UltraCanvas::PacketNodeRole::Router, "Routers"}});

// four packets of one message, each on its own route
sw->AddPacket("1", {"s1","r1","r3","r6","d1"});
sw->AddPacket("2", {"s1","r1","r2","r5","d1"});
sw->AddPacket("3", {"s1","r2","r4","r6","d1"});
sw->AddPacket("4", {"s1","r1","r4","r5","d1"});

sw->SetSimulationTime(0.45);        // static snapshot — the reference is a still figure
sw->SetShowSenderQueue(true);       // the "4 3 2 1" train
sw->SetShowReassembly(true);        // ordered arrival at the receiver

sw->SetOnPacketClick([](const UltraCanvas::TracePacketRef& p) { /* … */ });
```

### Family B — circuit vs packet comparison (image 8)

```cpp
auto cmp = UltraCanvas::CreatePacketSwitchingDiagram("compare", 201, 20, 20, 760, 520);
cmp->SetTitle("Switching networks");
cmp->SetPanelLayout(UltraCanvas::PacketPanelLayout::Stacked, 2);

auto& circuit = cmp->Panel(0);
circuit.SetCaption("circuit switching");
circuit.LoadTopology(topologyJson);
circuit.HighlightPath({"a","sw1","sw2","sw3","b"},         // the reserved circuit
                      UltraCanvas::Colors::Red, /*width*/ 4.0f);

auto& packet = cmp->Panel(1);
packet.SetCaption("packet switching");
packet.LoadTopology(topologyJson);
packet.AddPacket("1", {"a","sw1","sw3","b"});
packet.AddPacket("2", {"a","sw2","sw3","b"});
packet.AddPacket("3", {"a","sw1","sw4","b"});
packet.AddPacket("4", {"a","sw2","sw4","b"});
packet.SetShowReassembly(true);
```

### Family C2 — flow graph / ladder (`UltraCanvasSequenceDiagram`)

```cpp
#include "Plugins/Diagrams/UltraCanvasSequenceDiagram.h"

auto flow = UltraCanvas::CreateSequenceDiagram("flow", 300, 20, 20, 640, 480);
flow->ApplyPreset(UltraCanvas::SequencePreset::PacketFlow);
flow->SetTitle("TCP three-way handshake");
flow->AddLifeline("client", "10.0.0.5");
flow->AddLifeline("server", "93.184.216.34:80");

flow->AddPacket({.id="1", .sourceId="client", .destId="server",
                 .timestamp=0.000, .sizeBytes=74, .protocol="TCP",
                 .label="SYN  Seq=0 Win=64240"});
flow->AddPacket({.id="2", .sourceId="server", .destId="client",
                 .timestamp=0.021, .sizeBytes=74, .protocol="TCP",
                 .label="SYN, ACK  Seq=0 Ack=1"});
flow->AddPacket({.id="3", .sourceId="client", .destId="server",
                 .timestamp=0.021, .sizeBytes=66, .protocol="TCP",
                 .label="ACK  Seq=1 Ack=1"});

flow->SetTimeMode(UltraCanvas::SequenceTimeMode::RelativeToFirst);
flow->AddGroupBracket("1", "3", "3-way handshake");
flow->SetColorMode(UltraCanvas::SequenceColorMode::ByStream);
flow->SetOnMessageClick([&](const UltraCanvas::TracePacketRef& p) {
    structureView->SetModel(p.structure);      // cross-view sync
    bytesView->SetBytes(p.bytes);
});
```

The same element with the UML preset — no packet vocabulary involved:

```cpp
auto uml = UltraCanvas::CreateSequenceDiagram("uml", 301, 20, 20, 640, 480);
uml->ApplyPreset(UltraCanvas::SequencePreset::UML);
uml->AddLifeline("user", "User");
uml->AddLifeline("api",  "API Gateway");
uml->AddMessage("user", "api", "POST /login", UltraCanvas::SequenceArrow::Sync);
uml->AddMessage("api", "user", "200 OK",      UltraCanvas::SequenceArrow::Return);
uml->AddFragment(UltraCanvas::SequenceFragment::Alt, "credentials valid", /*from*/1, /*to*/2);
```

### The shared legend, used by any element (§10.1)

```cpp
#include "Plugins/Charts/UltraCanvasChartLegend.h"

UltraCanvas::ChartLegend legend;
legend.SetPosition(UltraCanvas::ChartLegendPosition::BottomCenter);
legend.SetTitle("level");
legend.AddEntry({.label = "Routers", .color = green,
                 .swatch = UltraCanvas::LegendSwatch::Circle});

// during the host element's paint pass:
Rect2Dd consumed = legend.Measure(ctx, availableRect);   // host shrinks its plot area
legend.Render(ctx, consumed);
```

### Text spec (Mermaid-compatible) instead of code

```cpp
pkt->LoadFromText(R"(
packet
title UDP Packet
+16: "Source Port"
+16: "Destination Port"
32-47: "Length"
48-63: "Checksum"
64-95: "Data (variable length)" @payload
)");
```

---

## 10. Supporting deliverables

Two pieces of shared framework work fall out of this proposal. Neither is
packet-specific; the packet elements are simply the fifteenth and sixteenth
callers that make the duplication impossible to ignore.

### 10.1 `UltraCanvasChartLegend` — the shared legend component

**Approved as its own deliverable (§12.8).** The survey found **10 private
`RenderLegend` / `DrawLegend` implementations** across the chart plugins,
reached through **5 mutually incompatible position enums** —
`CircularLegendPosition`, `DumbbellLegendPosition`, `FunnelLegendPosition`,
`LegendPosition`, `PolarLegendPosition` — plus a stringly-typed sixth
(`std::string legendPosition` in `UltraCanvasPopulationChart`). Every one of
them re-solves swatch sizing, text measurement, wrapping and edge placement,
and they disagree on defaults.

Proposed unit: `include/Plugins/Charts/UltraCanvasChartLegend.h` +
`Plugins/Charts/UltraCanvasChartLegend.cpp` — a **renderable helper, not a
`UIElement`**, so any element can host it inside its own paint pass without a
child-widget relationship.

| # | Feature | Phase |
|---|---|---|
| G1 | One `ChartLegendPosition` enum: Top/Bottom/Left/Right × Start/Center/End, plus the four inset corners | P1 |
| G2 | Entry model: swatch + label (+ optional secondary value text), swatch shapes Square/Circle/Line/Dash/Marker | P1 |
| G3 | Discrete band entries with interval text (`[0.00, 0.02]`, `> a`) — the contour/hexbin requirement | P1 |
| G4 | Continuous colour bar mode with ticks and a formatter — folds in `UltraCanvasHeatmapChartElement::RenderColorBar` | P2 |
| G5 | Layout: measure-then-place, returning the space consumed so the host can shrink its plot area | P1 |
| G6 | Flow across multiple rows/columns with wrapping and a max-entries + "…and N more" overflow | P1 |
| G7 | Legend title, per-entry enable/disable, ordering | P1 |
| G8 | Optional interactivity: hit-test an entry, click to toggle series visibility, hover to highlight | P2 |
| G9 | Theming: background, border, text, spacing, font — light/dark/monochrome presets | P1 |
| G10 | Migrate the 10 existing call sites, delete the private implementations, alias the old enums as deprecated typedefs so no chart API breaks | P2 |
| G11 | Unit tests for measurement and wrapping (no render context needed) | P1 |

Sequencing note: build G1–G7 and G9 with the packet work (families A and B
consume it immediately), then do the migration in G10 as a separate, purely
mechanical change so any visual regression is isolated from new-feature churn.

### 10.2 Swimlanes on `UltraCanvasFlowChart`

Variant C1 (the netfilter/iptables packet-flow figure) is otherwise already
covered, but that figure is fundamentally lane-organised — chains grouped by
table, hooks grouped by traversal stage. A grep finds no swimlane support
anywhere in the repo.

| # | Feature | Phase |
|---|---|---|
| SL1 | Horizontal and vertical lane bands with header labels | P3 |
| SL2 | Node assignment to a lane; lane-aware layout keeps nodes inside their band | P3 |
| SL3 | Per-lane background tint and separators | P3 |
| SL4 | Nested lanes (pools containing lanes, BPMN style) | P3 |
| SL5 | A `FlowChartTemplates::NetfilterPacketFlow()` preset reproducing the canonical figure | P3 |

---

## 11. Suggested delivery order

0. **Phase 0 — the shared legend.** `UltraCanvasChartLegend` G1–G7, G9, G11
   (§10.1). Small, self-contained, no dependency on any packet work, and every
   later phase consumes it instead of growing an eleventh private
   implementation. Migration of the existing 10 call sites (G10) is deliberately
   *not* here — it comes in Phase 2, once the component has been proven.
1. **Phase 1 — Family A core.** `UltraCanvasPacketModel` +
   `UltraCanvasPacketLayout` (word grid, proportional linear, row-wrapping with
   continuation edges, text-fit cascade) unit-tested against IPv4/TCP/UDP; the
   element with layers, sub-fields, flags, highlights, banding, bit ruler,
   byte-column headers, offset **and word** gutters, stacked ruler digits,
   dimension rails, twin side braces, title/caption, monochrome theme,
   tooltips, click callbacks; the Mermaid parser; JSON; validators; the first
   template batch **plus its RFC-pinning tests (§12.6)**; docs and demo.
   **Reproduces images 1, 2, 6 and the lower half of 4.**
2. **Phase 2 — Family A completeness + Family B + legend migration.** Lane
   layout with striped bit-to-lane mapping, encapsulation mode, exploded detail
   bands, payload styling, per-bit cells, markers, byte binding and decoding,
   zoom/pan, ASCII/Mermaid/SVG export (**completes images 3, 4, 5**); the
   `BuildLinkPath` extraction and `GetLinkPointAt` on `UltraCanvasNodeDiagram`
   (2.1.0) with golden-geometry tests, then the switching element deriving from
   it with roles, regions, routes, tokens, path highlighting, sender queue,
   reassembly, comparison panels and static snapshots (**completes images 7 and
   8**); and separately, the mechanical legend migration (G10).
3. **Phase 3 — `UltraCanvasSequenceDiagram` + Family D.** The sequence element
   with both presets (PacketFlow and UML) and the hex/dissection companion,
   wired to the shared selection bus; then switching animation and playback,
   family A's ASCII import, editing mode, diff mode, fragmentation layout, and
   swimlanes on `UltraCanvasFlowChart` (§10.2) for the C1 netfilter figure.

---

## 12. Resolved decisions

All questions raised in review are settled. This section is the record; the
rest of the document already reflects these outcomes.

### 12.1 Base class — `UltraCanvasUIElement`, in `Plugins/Diagrams/` ✔
All four elements derive from `UltraCanvasUIElement`, not
`UltraCanvasChartElementBase`. None of them has data bounds, a coordinate
transform, series or axes, so the chart base offers nothing to inherit and
several things to fight. All live in `Plugins/Diagrams/` alongside
`UltraCanvasBlockDiagram` and `UltraCanvasNodeDiagram`. The one shared piece
that *does* belong under `Plugins/Charts/` is the legend (§10.1), because its
other ten callers are charts.

### 12.2 Family B — derive from `UltraCanvasNodeDiagram`, hide the editing API ✔
`UltraCanvasPacketSwitchingDiagram : public UltraCanvasNodeDiagram`, with the
flow-editor surface suppressed in the constructor and re-declared `private`
(§4.6). Composition would have meant forwarding dozens of methods to reach
layout, zoom, pan, minimap and JSON — all of which a packet figure genuinely
wants.

### 12.3 `GetLinkPointAt` — approved, as a minor version bump ✔
Link geometry is extracted from the render path into a private `BuildLinkPath`
and consumed by both the renderer and the new samplers (§4.6). Purely additive
public API → `UltraCanvasNodeDiagram` **2.1.0**. Golden-geometry unit tests per
`LinkStyle` guard the extraction, with `SmoothStep`/`Step` arrow placement
called out as the known-fragile case from the component's own changelog.

### 12.4 Element count — four ✔
Family A keeps its four layouts (word grid, proportional, encapsulation, lanes)
plus the encapsulation/timing/fragmentation modes in **one** element: the
model, colouring, annotations, legend, tooltips, validation and export are
identical across them, and splitting would duplicate ~80% of the code.
Families B, C2 and D are genuinely separate elements.

### 12.5 Text-spec grammar — Mermaid-compatible baseline ✔
Mermaid's `packet` syntax verbatim (`0-15:` ranges and `+16:` auto-advance),
with UltraCanvas features as trailing `@modifiers` that Mermaid ignores. A
plain Mermaid diagram must parse unchanged; an UltraCanvas diagram must
degrade to a valid Mermaid one when the modifiers are stripped. Round-tripping
into Markdown docs is worth more than syntactic elegance.

### 12.6 Byte binding & template ownership — **UltraNet coupling rejected** ✘
The earlier draft justified two design points with "so UltraNet can drive it"
and "define `PacketTemplates::RTP()` next to the `Plugins/UltraNet/rtp` code
that already parses those headers". Both justifications were wrong and are
withdrawn.

*Why it was wrong.* Inspecting `Plugins/UltraNet/rtp/RtpPlugin.cpp` shows the
header knowledge lives in an anonymous-namespace struct with hand-rolled bit
shifts (`out.version = (buf[0] >> 6) & 0x03;`) — a private implementation
detail of a 325-line socket plugin, not a publishable field table. Making it
the source of truth for the diagram would require either exporting that struct
(so a **networking** plugin gains a dependency on a **UI** plugin's model
header, backwards through the layering) or having the diagram include UltraNet
(so a drawing widget depends on a socket library). Both trade a real,
permanent coupling for protection against drift in a 12-byte header that has
been frozen since RFC 3550 in 1996.

*What replaces it.*

| Concern | Resolution |
|---|---|
| Module dependency | **None, in either direction** (§3.1). `Plugins/Diagrams` and `Plugins/UltraNet` stay independent; applications wire them together. |
| Template ownership | Plain static field tables in `Plugins/Diagrams/UltraCanvasPacketTemplates.cpp`. No parsing code, no protocol behaviour — just names, offsets and lengths. |
| Drift protection | A **unit test**, not a dependency: assert `PacketTemplates::RTP()` places version at bits 0–1, payload type at 9–15, sequence number at 16–31, timestamp at 32–63, SSRC at 64–95. If a template is edited wrongly the test fails, and it pins the template to the RFC rather than to another module's private code. |
| Byte binding rationale | The `SetFieldValueText(id, text)` escape hatch stays (P1) and automatic extraction stays (P2) — but the reason is **generic**: any host with its own decoder (a pcap reader, a firmware log parser, an app-specific protocol) can supply values without the widget implementing decoding. UltraNet is one such host, not the motivating case. |

The net effect on the design is small — the same two features ship — but the
architecture is now honest about what the diagram elements are: **drawing
widgets that know nothing about networking.**

### 12.7 Family C2 — ship as `UltraCanvasSequenceDiagram` ✔
The ladder is a general sequence diagram with packet-flavoured labels, and the
framework has no sequence diagram at all. It ships under the general name with
`SequencePreset::PacketFlow` and `SequencePreset::UML` (§7). Same code, wider
payoff.

### 12.8 Shared legend — build it ✔
`UltraCanvasChartLegend` is promoted from a question to a deliverable (§10.1),
with the migration of the 10 existing implementations sequenced as a separate
mechanical change after the new component is proven by the packet elements.
