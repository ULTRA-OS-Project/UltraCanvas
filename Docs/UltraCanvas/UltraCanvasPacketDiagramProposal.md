# UltraCanvasPacketDiagram — Research & Feature Proposal

Status: **Proposal — not yet implemented.** This document is the research
write-up and the roadmap for a comprehensive packet / protocol-data-unit
diagram element for UltraCanvas.

Author: UltraCanvas Framework
Last Modified: 2026-07-31

---

## 1. What a packet diagram is

A **packet diagram** is a byte- and bit-accurate map of a *protocol data unit*
(PDU): it draws every field of a header or frame as a box whose **width is
proportional to the field's size in bits**, positioned at its exact **offset**
from the start of the packet. It answers three questions at a glance — *what
fields exist*, *how wide is each one*, *where does it sit* — which is precisely
what a prose description of a protocol cannot do.

It is **not** a network topology diagram and **not** a sequence diagram. Those
show *who talks to whom* and *in what order*; a packet diagram shows *what the
bytes on the wire look like*.

### 1.1 The canonical form: the RFC word grid

The dominant convention comes from IETF RFCs (RFC 791/793 and everything
since): a grid **32 bits wide**, with a *bit ruler* `0 1 2 ... 31` across the
top, fields laid left-to-right MSB-first in network byte order, and any field
wider than the remaining space in a row **wrapping onto the next row**. A
variable-length trailer (options, payload) is drawn as an open or ragged box.

This convention is so entrenched that the IETF has an expired draft,
*Describing Protocol Data Units with Augmented Packet Header Diagrams*
(draft-mcquistin-augmented-ascii-diagrams), whose whole motivation is that
"packet diagram formats vary within and between RFC documents, making it
difficult to build tools to generate parsers from specifications". A
machine-readable packet model is therefore not a nicety — it is the thing the
standards community has been asking for.

### 1.2 The four layout families found in the wild

The RFC word grid is only one of four distinct layouts, and a *comprehensive*
element must handle all four — the five reference images cover all of them:

| Family | Width means | Typical use | Reference image |
|---|---|---|---|
| **Word grid** (RFC style) | Bits, wrapped every *N* (usually 32) | IP/TCP/UDP headers, RFC figures | 1, 2, 4 |
| **Proportional linear** | Bytes, one continuous strip | Embedded/wireless frames, "1 byte / 21 bytes / 96 bytes" | 3 |
| **Encapsulation stack** | Nesting depth | Teaching material: "TCP segment inside an IP packet" | 4 (top band) |
| **Lane / bitstream** | Time, one row per physical signal | Bus protocols: SDIO, SPI, I²C, CAN, USB | 5 |

### 1.3 Existing tooling — what the state of the art already does

| Tool | What it contributes to the requirements |
|---|---|
| **Mermaid `packet`** (v11.0+) | Text syntax `0-15: "Source Port"` plus auto-advancing `+16: "Field"`; wraps at a configurable bits-per-row; ships TCP and UDP examples. The simplest possible authoring grammar — worth adopting near-verbatim. |
| **WaveDrom `bitfield`** | Register/bit-field renderer with `name` / `bits` / `attr` / `type` per field and render options `lanes` (default 2), `bits` (default 32), `vspace`, `hspace`, `fontsize`, `compact`, `hflip`, `vflip`, `bigendian`, `trim`, `offset`. Establishes the multi-lane layout, the attribute row under each field, and the endianness/flip switches. |
| **Protocol** (luismg, ASCII generator) | Renders RFC-like ASCII header diagrams from a compact field list, with an optional bit-count ruler on top. Establishes the ASCII/text export target. |
| **draft-mcquistin augmented diagrams** | Formalises the diagram as a *parseable specification* — structured text alongside the picture so a parser can be generated. Establishes the import/validation ambitions. |
| **Wireshark packet-diagram pane** | Renders the live diagram for the currently selected packet, driven by the real dissected bytes. Establishes the "bind actual data and decode" requirement. |

Sources consulted:
[Mermaid — Packet Diagram](https://mermaid.js.org/syntax/packet.html),
[Mermaid packet.md source](https://github.com/mermaid-js/mermaid/blob/develop/packages/mermaid/src/docs/syntax/packet.md),
[WaveDrom bitfield](https://github.com/wavedrom/bitfield),
[WaveDrom bit-field guide](https://observablehq.com/@drom/wavedrom-bit-field-guide),
[Protocol — an ASCII header generator for network protocols](https://www.luismg.com/protocol/),
[McQuistin, Band, Jacob & Perkins, *Describing Protocol Data Units with Augmented Packet Header Diagrams*](https://datatracker.ietf.org/doc/html/draft-mcquistin-augmented-ascii-diagrams),
[Wireshark issue 20820 — packet diagram pane](https://gitlab.com/wireshark/wireshark/-/issues/20820).

---

## 2. What the five reference images demand

Each uploaded image maps to concrete capabilities. Together they define the
scope, and they are deliberately *not* five variations on one picture — they
are four different layout families plus a nesting mechanism.

### Image 1 — "TCP/IP Packet": stacked headers, flag bits, highlighted fields
A 32-bit word grid holding **two headers plus payload** in one figure: the IP
header, then the TCP header, then "TCP Data". Left-hand **layer brackets**
label the two regions ("IP Header" spanning its rows, "TCP" spanning its).
Rows have uneven splits (Version | IHL | Type of Service | Total Length).
Four cells are **highlighted green** (Source Address, Destination Address,
Source Port, Destination Port) — a semantic emphasis set, not a layer colour.
The TCP flags row shows **six one-bit cells** (U A P R S F) with the letters
stacked vertically because the cell is far too narrow for horizontal text. One
field carries an inline **constant annotation**: `Protocol=6 (TCP)`.

> Requires: multi-layer packets in one diagram, layer brackets on the side,
> highlight sets independent of layer colour, vertical/stacked text fallback in
> narrow cells, per-field constant/value annotation, chart title.

### Image 2 — IPv4 header: dimension rails, row banding, open payload
A **top dimension rail** — a double-headed arrow labelled "4 bytes (32 bits)"
spanning the full width — and a **left vertical brace** labelled "Header / 24
bytes" spanning the header rows. Rows alternate light/dark blue banding. The
flags row packs `DF` and `MF` as narrow cells inline with Fragment Offset. The
"Data" region below the header is drawn as a **large open box with a soft
gradient and a curved top-left boundary**, signalling "variable length,
continues beyond the figure".

> Requires: dimension rails with arrowheads on any edge, side braces with
> labels, alternating row banding, a payload region with its own shape/fill
> treatment (gradient, curved or ragged edge) distinct from fixed fields.

### Image 3 — 802.15.4-style frame: proportional widths + exploded detail
A **single proportional strip** — Length / Header / Payload — with byte-count
annotations *above* the strip (`1 byte`, `21 bytes`, `96 bytes`) sitting over
brace-style extents. Below it, a **second strip expands the Header** into its
sub-fields (IEEE header, Seq. number, Destination PAN, Destination address,
Source address) with byte counts *below* that strip (`2 bytes`, `1 byte`,
`2 bytes`, `8 bytes`, `8 bytes`). The two strips are connected by **diagonal
leader lines** running from the edges of the parent Header cell to the ends of
the expansion strip. Note the expansion is drawn at a *different scale* from
its parent — that is the whole point of the drill-down.

> Requires: proportional (non-wrapping) layout, hierarchical fields with an
> **exploded detail band**, leader lines from parent cell to child band,
> dimension annotations above *and* below, per-band independent scaling, and a
> "length prefix" field kind (`Length: [header + payload]`).

### Image 4 — Networkustad IPv4: encapsulation stack + byte column headers
Two figures stacked. On top, an **encapsulation view**: `Segment Header | Data`
nested inside `IPv4 Header | Data`, colour-coded to show which bytes of the
outer PDU the inner PDU occupies. Below, the IPv4 word grid with **"Byte 1 /
Byte 2 / Byte 3 / Byte 4" column headers** instead of a bit ruler, and cells
that are **sub-divided within one column** (Version | Internet Header Length;
Differentiated Service (DS) split into DSCP | ECN as a second header row).

> Requires: an encapsulation/nesting render mode, byte-granular column headers
> as an alternative to the bit ruler, two-level cell headers (a parent field
> whose sub-fields are drawn inside it), and per-layer fill colours.

### Image 5 — SDIO data packet: bit lanes, per-byte colouring, start/end markers
The hardest one. A **lane layout**: one row for the narrow bus (DAT0 only) and
four rows for the wide bus (DAT0–DAT3), with a **left signal-name gutter**
(DAT3/DAT2/DAT1/DAT0). Along each lane run **individual bit cells labelled
b7…b0**, coloured by which source byte they came from (1st Byte / 2nd Byte /
3rd Byte / *n*th Byte, with a matching **legend**). The stream is bracketed by
a **Start bit** and **End bit** marker at either end, has a **CRC** block per
lane, a top bracket annotating `n Byte Data`, an **ellipsis for repetition**
(`…`), and a figure caption ("Figure 3-7: Data Packet Format — Usual Data").
On the wide bus each byte is *striped across the four lanes* — bit *i* of the
byte goes to a different lane — which is a genuine index remapping, not just a
different drawing.

> Requires: multi-lane layout with a signal gutter, per-bit cells with bit
> labels, per-byte colour coding driven by a byte index, start/end/CRC marker
> field kinds, repetition/ellipsis regions, bracket annotations with counts,
> a source-byte legend, figure caption, and a bit-to-lane interleave mapping
> for wide-bus formats.

---

## 3. How this fits the existing UltraCanvas code

The framework already has the supporting machinery. The packet element should
**reuse, not duplicate**:

| Existing piece | Reuse for |
|---|---|
| `UltraCanvasUIElement` | Base class — this is a diagram element, not a chart; it has no axes, no data bounds and no series, so `UltraCanvasChartElementBase` is the wrong parent (see §8.1) |
| `IRenderContext` | Everything the renderer needs already exists: `FillRectangle`/`DrawRectangle`, `FillRoundedRectangle`, `MoveTo`/`LineTo`/`BezierCurveTo` + `Fill`/`Stroke` for the curved payload edge, `SetLineDash` (dashed continuation edges), `Rotate` + `PushState`/`PopState` (vertical text in 1-bit cells), `ClipRect` (text ellipsis in narrow cells), `SetFillPaint(IPaintPattern)` (the image-2 payload gradient), `SetAlpha` |
| `include/Plugins/Charts/UltraCanvasColormap.h` | Layer/byte palettes — 19 built-in ramps, `SampleColormap`, `QuantizeNorm`; the per-byte colouring of image 5 is a quantised ramp lookup, no new colour code needed |
| `include/Plugins/Charts/UltraCanvasLabelPlacement.h` | Collision-aware placement of dimension labels, leader-line callouts and legend entries |
| `Plugins/Charts/UltraCanvasConnectionRenderer.cpp` | The leader lines from a parent cell to its exploded detail band (image 3) |
| `UltraCanvasBlockDiagram` | Precedent for a hit-tested, selectable, hoverable diagram element with a node map and callbacks |
| `include/Plugins/Charts/UltraCanvasHexLayout.h` | The precedent to follow for the layout math: a **header-only, dependency-free, unit-testable** geometry unit with no UI includes |
| `UltraCanvasTooltipManager` | Hover read-out (offset, length, value) |
| `UltraCanvasJSON` | Load/save of packet specs (never expose yyjson — house rule) |
| `UltraCanvas/Plugins/UltraNet/rtp`, `.../rtsp` | Existing RTP/RTSP packet knowledge in-tree — the built-in template library (§5.11) can start from these and stay in sync with what UltraNet actually parses |
| `UltraCanvas/CMakeLists.txt:389` (`Plugins/Diagrams/…`) | Where the new `.cpp` files get registered — the source list is explicit, not globbed |

---

## 4. Proposed architecture

Four new units, following the Hexbin/Contour precedent — pure model+layout
headers that are unit-testable without a window, plus a thin UI element and a
separate parser:

```
include/Plugins/Diagrams/UltraCanvasPacketModel.h    # field/layer model      (no UI deps)
include/Plugins/Diagrams/UltraCanvasPacketLayout.h   # offsets -> cell rects  (no UI deps)
include/Plugins/Diagrams/UltraCanvasPacketParser.h   # text spec -> model     (no UI deps)
include/Plugins/Diagrams/UltraCanvasPacketDiagram.h  # the element (UIElement)
Plugins/Diagrams/UltraCanvasPacketLayout.cpp
Plugins/Diagrams/UltraCanvasPacketParser.cpp
Plugins/Diagrams/UltraCanvasPacketDiagram.cpp
```

### 4.1 The model

Everything is expressed in **bits**, because bits are the only unit that covers
all five images; bytes are a presentation choice made by the ruler and the
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

### 4.2 The layout engine (`UltraCanvasPacketLayout.h`)

Pure geometry, no `IRenderContext`, no `Color`, fully unit-testable — this is
where the real work is and where the bugs would otherwise hide:

* **Row splitting.** A field crossing a row boundary is emitted as *several*
  `PacketCell`s, each flagged `continuesLeft` / `continuesRight` so the renderer
  can suppress the shared border and dash the cut edge. Getting this wrong is
  the single most visible packet-diagram bug (a 64-bit Sequence Number over a
  32-bit grid must read as one field, not two).
* **Four layout strategies** behind one interface: `WordGrid`,
  `ProportionalLinear`, `Encapsulation`, `Lanes` — each turning
  `(bitOffset, bitLength)` into cell rectangles inside a given area.
* **Proportional scaling with clamping.** Image 3 has a 1-byte field next to a
  96-byte field: a strictly proportional 1:96 ratio makes the Length cell
  unreadable. Needs a `MinCellWidth` and an optional `LengthScale`
  (`Linear` / `Sqrt` / `Log` / `Clamped`) with a visible "broken scale" marker
  when clamping kicks in.
* **Bit-to-lane interleave** for the wide-bus case (image 5): a pluggable
  `BitLaneMapping` (`Sequential`, `Striped(nLanes)`, custom callback).
* **Text fitting decisions** made in layout, not paint: horizontal → abbreviated
  → rotated 90° → stacked one glyph per line → tooltip-only. The element asks
  the render context for text extents once and caches the decision per cell.
* Output is a plain `PacketLayoutResult { cells, rulerTicks, brackets, rails,
  leaders }` — no drawing calls — so it can be tested against known headers
  (feed it RFC 791, assert 20 bytes, 14 fields, 5 rows, `Source Address` at
  bits 96–127).

### 4.3 The element

`UltraCanvasPacketDiagramElement : public UltraCanvasUIElement` owns the model,
the layout result, a cache invalidated by a generation counter, hit-testing
(cell under cursor via the layout rects), hover/selection state, and the
render passes: banding → cells → sub-cells → borders → text → rulers → brackets
→ rails → leaders → legend → caption.

### 4.4 The parser

`UltraCanvasPacketParser` accepts a Mermaid-compatible text spec, because that
grammar is already the lingua franca and costs almost nothing to support:

```
packet
title TCP Header
0-15: "Source Port"
16-31: "Destination Port"
+32: "Sequence Number"
106: "URG"
```

with UltraCanvas extensions for what Mermaid cannot express (layers, kinds,
nesting, colours) added as trailing modifiers so plain Mermaid input still
parses. The same unit gains an ASCII **exporter** (the RFC `+-+-+` form) and,
in P3, an ASCII *importer* — parsing an RFC figure back into a model is the
single highest-value trick this element could do for a networking codebase.

---

## 5. Proposed feature list

Grouped, with a suggested delivery phase. **P1** = core, ship first;
**P2** = completes the reference images; **P3** = polish / advanced.

### 5.1 Data model & construction
| # | Feature | Phase |
|---|---|---|
| M1 | Bit-granular field model: name, offset, length, description | P1 |
| M2 | Auto-advancing append (`AddField(name, bits)`) alongside explicit offsets | P1 |
| M3 | Field kinds: Fixed, Flag, Reserved, Padding, Variable, Payload, Checksum, Marker, LengthPrefix, Ellipsis | P1 |
| M4 | Multiple layers in one diagram, each with its own name/colour/bracket (image 1) | P1 |
| M5 | Nested sub-fields, arbitrary depth (DS → DSCP + ECN; image 4) | P1 |
| M6 | Per-field constant/expected value text (`Protocol=6 (TCP)`; image 1) | P1 |
| M7 | Per-field attribute line (WaveDrom `attr`: RO/RW/MBZ) | P2 |
| M8 | `LengthPrefix` linkage — a field that declares the length of another (image 3) | P2 |
| M9 | Repeating groups / arrays of fields with a count or "n" ellipsis (image 5) | P2 |
| M10 | Conditional / optional fields (present only if a flag is set), rendered dashed | P3 |
| M11 | Enumerated value tables per field (for tooltips and the detail pane) | P3 |
| M12 | Arbitrary user data pointer + per-field id for host-app linkage | P1 |

### 5.2 Layout modes
| # | Feature | Phase |
|---|---|---|
| L1 | **WordGrid** — RFC style, wraps every *N* bits (default 32), configurable (8/16/32/64/custom) | P1 |
| L2 | Fields wrapping across row boundaries as one logical field (continuation edges, no repeated label) | P1 |
| L3 | **ProportionalLinear** — one continuous strip, width ∝ size (image 3) | P1 |
| L4 | **Lanes** — one row per signal, with a left signal-name gutter (image 5) | P2 |
| L5 | **Encapsulation** — nested PDU bands showing one protocol inside another (image 4, top) | P2 |
| L6 | Min cell width + scale modes (Linear/Sqrt/Log/Clamped) with a broken-scale marker | P2 |
| L7 | Bit-to-lane interleave mapping (Sequential / Striped(n) / custom) for wide buses | P2 |
| L8 | Exploded detail band: expand one field into a second, independently scaled strip (image 3) | P2 |
| L9 | Auto-fit: choose row count / cell height to fill the element, or fixed cell metrics with scrolling | P1 |
| L10 | Vertical (top-to-bottom) orientation as an alternative to horizontal | P3 |
| L11 | LSB-first bit numbering and little-endian byte order, with `hflip`/`vflip` equivalents | P2 |
| L12 | Page/row-range windowing for very large PDUs (render rows *a*…*b*) | P3 |

### 5.3 Cell rendering & typography
| # | Feature | Phase |
|---|---|---|
| R1 | Rectangular cells with configurable border width/colour/radius | P1 |
| R2 | Text fit cascade: full → abbreviated → rotated 90° → stacked glyphs → tooltip-only (image 1 flags) | P1 |
| R3 | Two-line cell text (name + value, or name + attr) | P1 |
| R4 | Sub-field cells drawn inside the parent cell, second header row (image 4) | P1 |
| R5 | Payload region styling: gradient fill, curved or ragged/torn edge, open right side (images 1, 2) | P2 |
| R6 | Dashed / hatched treatment for Reserved, Padding and optional fields | P2 |
| R7 | Marker glyphs for start bit / end bit / delimiters (image 5) | P2 |
| R8 | Per-bit cells with `b7…b0` labels when the zoom level allows (image 5) | P2 |
| R9 | Ellipsis cell (`…`) for elided repetition, with a count label | P2 |
| R10 | Alternating row banding (image 2) | P1 |
| R11 | Drop shadow / 3D bevel option for presentation decks | P3 |
| R12 | Per-field icon or badge slot | P3 |

### 5.4 Rulers, dimensions & annotations
| # | Feature | Phase |
|---|---|---|
| A1 | Bit ruler across the top (`0…31`), tick every 1/4/8 bits, configurable | P1 |
| A2 | Byte-column headers (`Byte 1…Byte 4`) as an alternative to the bit ruler (image 4) | P1 |
| A3 | Left offset gutter per row (bit offset, byte offset, or hex offset) | P1 |
| A4 | Dimension rail: double-headed arrow + label on any edge (`4 bytes (32 bits)`; image 2) | P1 |
| A5 | Per-field size annotations above and/or below the strip (`1 byte`, `96 bytes`; image 3) | P1 |
| A6 | Side braces / brackets spanning a row range with a label (`Header 24 bytes`, `IP Header`; images 1, 2) | P1 |
| A7 | Top brackets spanning a cell range with a count (`n Byte Data`; image 5) | P2 |
| A8 | Leader lines connecting a parent cell to its exploded detail band (image 3) | P2 |
| A9 | Free callout notes anchored to a field, with collision-aware placement | P2 |
| A10 | Diagram title and figure caption (images 1, 5) | P1 |
| A11 | Size label unit/format control (bits / bytes / auto, custom formatter callback) | P1 |
| A12 | Total-size footer (`Total: 20 bytes (160 bits)`) | P2 |

### 5.5 Colour, theming & legend
| # | Feature | Phase |
|---|---|---|
| S1 | Per-field explicit colour override | P1 |
| S2 | Per-layer colour, applied to all its fields (images 1, 4) | P1 |
| S3 | Colour-by-kind palette (payload vs reserved vs checksum) | P1 |
| S4 | Colour-by-source-byte from a quantised colormap (image 5) | P2 |
| S5 | Highlight set — mark any subset of fields as emphasised, independent of layer colour (image 1's green cells) | P1 |
| S6 | Full theme surface: background, cell fill, border, text, ruler, banding, gutter — all settable; light and dark presets | P1 |
| S7 | Reuse of the 19 `UltraCanvasColormap` ramps for automatic per-field/per-layer colouring | P1 |
| S8 | Legend: swatch + label, for layers, kinds, or source bytes; position top/bottom/left/right (image 5) | P2 |
| S9 | Automatic readable text colour (contrast against the cell fill) | P1 |
| S10 | Hatch/pattern fills for accessibility and monochrome print | P3 |
| S11 | Per-field opacity, and a "dim everything except the selection" focus mode | P2 |

### 5.6 Live byte binding & decoding
| # | Feature | Phase |
|---|---|---|
| B1 | `SetPacketBytes(const uint8_t*, size_t)` — bind real captured bytes to the model | P2 |
| B2 | Extract and display each field's actual value in its cell (dec / hex / bin, per-field format) | P2 |
| B3 | Field value formatters: IPv4/IPv6 address, MAC, timestamp, enum name, flag list, custom callback | P2 |
| B4 | Resolve `Variable` / `LengthPrefix` lengths from the bound data, re-laying out accordingly | P2 |
| B5 | Companion hex-dump view with two-way highlight sync (click a field → the bytes light up, and back) | P3 |
| B6 | Checksum verification indicator on `Checksum` fields | P3 |
| B7 | Diff mode: two byte buffers on one diagram, changed fields marked | P3 |
| B8 | Live/streaming update from a capture source without rebuilding the model | P3 |

### 5.7 Interaction
| # | Feature | Phase |
|---|---|---|
| I1 | Hover highlight + tooltip: name, bit offset, byte offset, length, description, value | P1 |
| I2 | Click callback `(fieldId, layerId, bitOffset, bitLength)` and selection state | P1 |
| I3 | Keyboard navigation between fields (arrows / Tab) with focus ring — accessibility | P2 |
| I4 | Expand/collapse a field's sub-fields; expand/collapse a whole layer | P2 |
| I5 | Drill-down: click a field to open its exploded detail band (image 3), with a back path | P2 |
| I6 | Zoom + pan, with the bit ruler adapting its tick density; double-click to reset | P2 |
| I7 | Copy: field value, field spec line, whole diagram as ASCII, to the clipboard | P2 |
| I8 | Editing mode: drag a field boundary to resize, drag to reorder, inline rename | P3 |
| I9 | Search / jump to a field by name or offset | P3 |
| I10 | Context menu (copy value, copy offset, hide field, set colour) | P3 |

### 5.8 Import / export / interop
| # | Feature | Phase |
|---|---|---|
| X1 | Mermaid-compatible `packet` text parser (`0-15:` ranges and `+16:` auto-advance) | P1 |
| X2 | UltraCanvas extensions to that grammar: layers, kinds, colours, nesting, lanes | P1 |
| X3 | JSON load/save of the model via `UltraCanvasJSON` | P1 |
| X4 | ASCII/RFC `+-+-+` export with an optional bit ruler (the Protocol-tool output form) | P2 |
| X5 | WaveDrom-bitfield JSON import (`name`/`bits`/`attr`/`type`) | P2 |
| X6 | SVG / PNG / PDF export at a chosen scale, via the existing export path | P2 |
| X7 | ASCII/RFC diagram **import** — parse an RFC figure back into a model | P3 |
| X8 | Kaitai Struct `.ksy` import (subset: fixed-size seq fields) | P3 |
| X9 | Mermaid `packet` **export**, so a diagram can round-trip into docs | P2 |
| X10 | C/C++ struct generation from the model (`#pragma pack` + bitfields), and the reverse | P3 |

### 5.9 Validation & diagnostics
| # | Feature | Phase |
|---|---|---|
| V1 | Overlap detection — two fields claiming the same bits | P1 |
| V2 | Gap detection — unclaimed bit ranges, optionally auto-filled as Reserved | P1 |
| V3 | Total-length check against a declared expected size | P1 |
| V4 | Row-boundary sanity: warn when a field is misaligned in a way that usually indicates a spec typo | P2 |
| V5 | Duplicate id / empty name / zero-length diagnostics | P1 |
| V6 | Structured `PacketValidationResult` (severity, message, field id) surfaced to the host app, plus optional on-diagram warning badges | P1 |

### 5.10 Engineering
| # | Feature | Phase |
|---|---|---|
| E1 | `UltraCanvasPacketModel.h` / `UltraCanvasPacketLayout.h` free of UI dependencies, unit-tested against known headers (IPv4, TCP, UDP, 802.15.4) | P1 |
| E2 | Layout cache keyed on a model/size generation counter | P1 |
| E3 | Factory helper `CreatePacketDiagram(...)` matching house convention | P1 |
| E4 | `Docs/UltraCanvas/UltraCanvasPacketDiagram.md` in the house style, plus `…Examples.md` | P1 |
| E5 | `Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp` reproducing all five reference images, registered in the demo tree | P1 |
| E6 | `Tests/` coverage for the layout engine, the parser and the validators | P1 |
| E7 | Registration in `UltraCanvas/CMakeLists.txt` and `Masterfile_modules.md`; `llms.txt` regenerated | P1 |

### 5.11 Built-in protocol template library
| # | Feature | Phase |
|---|---|---|
| P1t | `PacketTemplates::IPv4()`, `IPv6()`, `TCP()`, `UDP()`, `ICMP()`, `ARP()`, `Ethernet()` | P1 |
| P2t | `DNS()`, `DHCP()`, `TLSRecord()`, `HTTP2Frame()`, `QUICLongHeader()`, `WebSocketFrame()` | P2 |
| P3t | `RTP()`, `RTCP()` — built from the existing `Plugins/UltraNet/rtp` field knowledge so the diagram and the parser cannot drift | P2 |
| P4t | Bus/embedded: `SDIOData()`, `CANFrame()`, `Modbus()`, `MQTT()`, `IEEE802_15_4()`, `USBPacket()` | P3 |
| P5t | Template composition helper: `Stack({Ethernet(), IPv4(), TCP()})` producing an image-1 style layered diagram | P2 |

---

## 6. Proposed API sketch

### Image 1 — layered TCP/IP packet with flags and highlights

```cpp
#include "Plugins/Diagrams/UltraCanvasPacketDiagram.h"

auto pkt = UltraCanvas::CreatePacketDiagram("packet1", 100, 20, 20, 720, 520);

pkt->SetTitle("TCP/IP Packet");
pkt->SetLayoutMode(UltraCanvas::PacketLayoutMode::WordGrid);
pkt->SetBitsPerRow(32);
pkt->SetRulerMode(UltraCanvas::PacketRulerMode::BitRuler);

// --- layer 1: the IP header (auto-advancing offsets) ---
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

// --- layer 2: the TCP header ---
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

// --- side brackets + emphasis set ---
pkt->SetLayerBrackets(true);                                  // "IP Header" / "TCP"
pkt->SetHighlightFields({"Source Address", "Destination Address",
                         "Source Port", "Destination Port"},
                        UltraCanvas::Color(160, 205, 160, 255));

pkt->SetOnFieldClick([](const UltraCanvas::PacketFieldRef& f) {
    std::cout << f.name << " @ bit " << f.bitOffset << " (" << f.bitLength << ")\n";
});

container->AddChild(pkt);
```

### Image 2 — IPv4 header with dimension rails and an open payload

```cpp
auto ipv4 = UltraCanvas::CreatePacketDiagram("ipv4", 101, 20, 20, 620, 460);
ipv4->SetModel(UltraCanvas::PacketTemplates::IPv4());

ipv4->SetRowBanding(true, UltraCanvas::Color(214, 231, 245, 255),
                          UltraCanvas::Color(186, 213, 235, 255));
ipv4->AddDimensionRail(UltraCanvas::PacketEdge::Top, 0, 31, "4 bytes (32 bits)");
ipv4->AddBrace(UltraCanvas::PacketEdge::Left, "ip", "Header\n24 bytes");
ipv4->SetPayloadStyle(UltraCanvas::PacketPayloadStyle::GradientCurved);
```

### Image 3 — proportional strip with an exploded detail band

```cpp
auto frame = UltraCanvas::CreatePacketDiagram("frame", 102, 20, 20, 700, 300);
frame->SetLayoutMode(UltraCanvas::PacketLayoutMode::ProportionalLinear);
frame->SetSizeUnit(UltraCanvas::PacketSizeUnit::Bytes);
frame->SetLengthScale(UltraCanvas::PacketLengthScale::Clamped, /*minCellPx*/ 48.0f);

auto& f = frame->AddLayer("frame", "");
f.AddFieldBytes("Length:\n[header + payload]", 1, UltraCanvas::PacketFieldKind::LengthPrefix);
auto& hdr = f.AddFieldBytes("Header", 21);
f.AddFieldBytes("Payload", 96, UltraCanvas::PacketFieldKind::Payload);

// sub-fields drive the exploded band
hdr.AddChildBytes("IEEE header", 2);
hdr.AddChildBytes("Seq. number", 1);
hdr.AddChildBytes("Destination PAN", 2);
hdr.AddChildBytes("Destination address", 8);
hdr.AddChildBytes("Source address", 8);

frame->SetSizeAnnotations(UltraCanvas::PacketEdge::Top);       // 1 byte / 21 bytes / 96 bytes
frame->ExpandField("Header", UltraCanvas::PacketEdge::Bottom); // detail band + leader lines
frame->SetDetailSizeAnnotations(UltraCanvas::PacketEdge::Bottom);
```

### Image 5 — SDIO wide-bus lane view

```cpp
auto sdio = UltraCanvas::CreatePacketDiagram("sdio", 103, 20, 20, 820, 320);
sdio->SetLayoutMode(UltraCanvas::PacketLayoutMode::Lanes);
sdio->SetLanes({"DAT3", "DAT2", "DAT1", "DAT0"});
sdio->SetBitLaneMapping(UltraCanvas::PacketBitLaneMapping::Striped);
sdio->SetShowBitLabels(true);                       // b7 … b0 inside each cell
sdio->SetColorMode(UltraCanvas::PacketColorMode::BySourceByte);
sdio->SetColormap(UltraCanvas::HeatmapColormap::Set3);

sdio->AddMarker(UltraCanvas::PacketEdge::Left,  "Start bit");
sdio->AddMarker(UltraCanvas::PacketEdge::Right, "End bit");
sdio->AddDataBytes(/*count*/ 4, /*ellipsisAfter*/ 3);   // 1st, 2nd, 3rd, …, nth
sdio->AddChecksumPerLane("CRC", 16);
sdio->AddBracket(UltraCanvas::PacketEdge::Top, "data", "n Byte Data");
sdio->SetLegendMode(UltraCanvas::PacketLegendMode::SourceBytes);
sdio->SetCaption("Figure 3-7: Data Packet Format — Usual Data");
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

### Bound bytes (P2)

```cpp
pkt->SetPacketBytes(capturedFrame.data(), capturedFrame.size());
pkt->SetShowFieldValues(true);
pkt->SetFieldFormatter("Source Address", UltraCanvas::PacketValueFormat::IPv4);
```

---

## 7. Suggested delivery order

1. **Phase 1 — core word grid.** `UltraCanvasPacketModel` +
   `UltraCanvasPacketLayout` (word grid, proportional linear, row-wrapping
   fields with continuation edges, text-fit cascade) with unit tests against
   IPv4/TCP/UDP; the element with layers, sub-fields, flags, highlights,
   banding, bit ruler, byte-column headers, offset gutter, dimension rails,
   side braces, title/caption, tooltips and click callbacks; the Mermaid-
   compatible parser; JSON load/save; the validators; the first template batch;
   docs and a demo. **This alone reproduces images 1, 2 and the lower half of 4.**
2. **Phase 2 — the remaining layouts and live data.** Lane layout with the
   striped bit-to-lane mapping, encapsulation mode, exploded detail bands with
   leader lines, payload gradient/ragged styling, per-bit cells, markers,
   ellipsis, legend, byte binding and value decoding, zoom/pan, ASCII and
   Mermaid export, SVG/PNG export, WaveDrom import. **This completes images 3,
   4 and 5.**
3. **Phase 3 — advanced.** ASCII/RFC diagram import, hex-dump companion view
   with two-way highlight, diff mode, editing/authoring mode, conditional
   fields, Kaitai import, C struct generation, streaming updates.

---

## 8. Open questions for review

1. **Base class.** Recommendation: derive from `UltraCanvasUIElement`, not
   `UltraCanvasChartElementBase`. A packet diagram has no data bounds, no
   coordinate transform, no series and no axes — it would inherit almost
   nothing and fight the parts it did inherit. It belongs in
   `Plugins/Diagrams/`, alongside `UltraCanvasBlockDiagram`, not in
   `Plugins/Charts/`. Confirm.
2. **One element or two?** The lane/bitstream view (image 5) is arguably a
   different widget — it is closer to a timing diagram than to a header map.
   Recommendation: **one element, four layout strategies**, because the field
   model, colouring, annotations, legend, tooltips and export are identical and
   the only real difference is cell placement. Splitting it would duplicate
   ~80% of the code. Confirm before Phase 2.
3. **Byte binding scope.** Should `SetPacketBytes` do full decoding (B2–B4,
   including resolving variable lengths from the data), or should the element
   stay purely presentational and let the host app supply pre-computed field
   values? Recommendation: **support both** — a `SetFieldValueText(id, text)`
   escape hatch in P1, automatic extraction in P2 — so UltraNet can drive it
   without the diagram element needing protocol knowledge.
4. **Template library ownership.** Should `PacketTemplates::RTP()` live with
   the diagram element or with the UltraNet RTP plugin? Recommendation: define
   the templates next to the protocol implementations that already parse those
   headers (`Plugins/UltraNet/…`) and have the diagram element merely consume a
   `PacketModel`, so the picture and the parser cannot drift apart.
5. **Text-spec grammar.** Adopt Mermaid's `packet` syntax verbatim as the
   baseline (maximum interoperability, zero learning cost) and add UltraCanvas
   features as trailing `@modifiers` that Mermaid ignores — or design a cleaner
   native grammar? Recommendation: **Mermaid-compatible baseline**, since
   round-tripping into Markdown docs is worth more than syntactic elegance.
6. **Shared legend widget.** The legend needed here (swatch + label, four edge
   positions) is the same one the contour, hexbin, heatmap and Mekko charts
   want. Should this be the moment to factor out a reusable chart/diagram
   legend component rather than adding a fifth private implementation?
