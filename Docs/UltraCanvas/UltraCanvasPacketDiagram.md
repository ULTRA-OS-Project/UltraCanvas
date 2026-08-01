# UltraCanvasPacketDiagram

Bit-accurate protocol data unit (PDU) diagrams: every field is a box whose
width is proportional to its size in bits, drawn at its exact offset.

Status: **Phase 1 implemented.** See
[`UltraCanvasPacketDiagramProposal.md`](UltraCanvasPacketDiagramProposal.md)
for the research write-up, the full family taxonomy and the roadmap for
Phases 2 and 3.

Author: UltraCanvas Framework
Last Modified: 2026-07-31

---

## What it is

A packet diagram answers three questions at a glance: *what fields exist*,
*how wide is each one*, and *where does it sit*. It is the figure every RFC
uses to define a header.

This element is a **drawing widget**. It performs no I/O, opens no sockets and
knows no protocol semantics — it consumes a `PacketModel` (a field table) and,
optionally, a byte buffer. There is **no dependency on `Plugins/UltraNet` in
either direction**: a drawing widget must not link a socket library, and a
networking plugin must not link a UI model. An application that wants both
wires them together itself.

```
Header: Plugins/Diagrams/UltraCanvasPacketDiagram.h
Impl:   Plugins/Diagrams/UltraCanvasPacketDiagram.cpp
Model:  Plugins/Diagrams/UltraCanvasPacketModel.h      (dependency-free)
Layout: Plugins/Diagrams/UltraCanvasPacketLayout.h     (dependency-free)
Parser: Plugins/Diagrams/UltraCanvasPacketParser.h     (dependency-free)
Tables: Plugins/Diagrams/UltraCanvasPacketTemplates.h  (static data only)
Demo:   Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp
Tests:  Tests/PacketLayoutTest.cpp
```

---

## Quick start

```cpp
#include "Plugins/Diagrams/UltraCanvasPacketDiagram.h"
#include "Plugins/Diagrams/UltraCanvasPacketTemplates.h"

auto pkt = UltraCanvas::CreatePacketDiagram("packet1", 20, 20, 720, 420);
pkt->SetModel(UltraCanvas::PacketTemplates::IPv4());
pkt->SetRulerMode(UltraCanvas::PacketRulerMode::BitRuler);
container->AddChild(pkt);
```

Building a model by hand — fields auto-advance, so you give sizes, not offsets:

```cpp
auto pkt = UltraCanvas::CreatePacketDiagram("udp", 20, 20, 620, 200);
pkt->SetTitle("UDP Header");

auto& udp = pkt->AddLayer("udp", "UDP");
udp.AddField("Source Port", 16);
udp.AddField("Destination Port", 16);
udp.AddField("Length", 16).SetDescription("Header + data, in bytes");
udp.AddField("Checksum", 16, UltraCanvas::PacketFieldKind::Checksum);
udp.AddField("Data", 0, UltraCanvas::PacketFieldKind::Payload);   // 0 = open-ended
```

Or from a text spec (see [Text specs](#text-specs-mermaid-compatible)):

```cpp
pkt->LoadFromText("packet\n+16: \"Source Port\"\n+16: \"Destination Port\"\n");
```

---

## The model

Everything is expressed in **bits**. Bytes are a presentation choice made by
the ruler and the label formatter, not a property of the model.

### Fields

```cpp
PacketField& f = layer.AddField("Total Length", 16);   // bits
layer.AddFieldBytes("Payload", 96);                    // bytes -> bits
layer.AddFieldAt("Source Port", 0, 15);                // explicit inclusive range
```

`AddField` and `AddFieldBytes` append immediately after the previous field.
`AddFieldAt` places one at an explicit range; mixing the two styles is allowed,
exactly as Mermaid allows mixing `+16:` with `0-15:`.

Fluent setters return the field so tables read cleanly:

```cpp
layer.AddField("Protocol", 8)
     .SetConstantText("6 (TCP)")        // renders as "Protocol=6 (TCP)"
     .SetDescription("Next-layer protocol number")
     .SetColor(UltraCanvas::Color(200, 220, 240, 255));
```

### Field kinds

| Kind | Rendering |
|---|---|
| `Fixed` | Ordinary field |
| `Flag` | 1-bit; letters stack vertically when the cell is too narrow |
| `Reserved` | Greyed |
| `Padding` | Greyed |
| `Payload` | Open-ended region; honours `SetPayloadStyle` |
| `Variable` | Length not known statically |
| `LengthPrefix` | A field whose value gives another's length |
| `Checksum` | CRC/FCS block |
| `Marker` | Start bit / end bit / delimiter |
| `Ellipsis` | `…`, an elided repetition |

A `bitLength` of `0` means **open-ended**: the field starts on a fresh row and
is given `payloadRowCount` rows of drawn height.

### Sub-fields

A field can carry children, drawn inside the parent's lower half while the
parent's own label moves to the upper half:

```cpp
PacketField& ds = layer.AddField("Type of Service", 8);
ds.AddChild("DSCP", 6);
ds.AddChild("ECN", 2);
```

### Layers

Layers stack in declaration order; each field's offset is relative to its
layer, and the element resolves absolute offsets for tooltips and callbacks.

```cpp
auto& ip  = pkt->AddLayer("ip",  "IP Header");
auto& tcp = pkt->AddLayer("tcp", "TCP");
pkt->SetLayerBrackets(UltraCanvas::PacketEdge::Left);   // brace per layer
```

`PacketTemplates::Stack({...})` composes single-layer templates into one
layered figure.

---

## Layout modes

```cpp
pkt->SetLayoutMode(UltraCanvas::PacketLayoutMode::WordGrid);
```

| Mode | Width means | Use |
|---|---|---|
| `WordGrid` | Bits, wrapped every `bitsPerRow` (default 32) | RFC headers |
| `ProportionalLinear` | Bits/bytes on one continuous strip | Embedded frames |
| `Lanes` | One row per signal | Bus protocols |
| `Encapsulation` | *Phase 2* — currently falls back to `WordGrid` | Nested PDUs |

### Fields that wrap

A field wider than the remaining space in a row is split into several cells
that still read as **one** field: only the first fragment carries the label,
and the cut edges are drawn dashed and hairline instead of as full borders.

A 32-bit Sequence Number on a 32-bit grid is one cell. A 64-bit field is two
cells; over a 16-bit grid it is four. This is the contract
`Tests/PacketLayoutTest.cpp` pins hardest, because getting it wrong is the
most visible packet-diagram bug.

### Proportional scaling

A 1-byte field beside a 96-byte field is unreadable at true proportion, so:

```cpp
pkt->SetLengthScale(UltraCanvas::PacketLengthScale::Clamped, /*minCellPx*/ 48.0);
```

| Scale | Behaviour |
|---|---|
| `Linear` | True proportion — honest, can be unreadable |
| `Sqrt` | Compressed |
| `Log` | Heavily compressed |
| `Clamped` | Every cell gets `minCellWidth`, the remainder is distributed by size (default) |

Clamped still preserves ordering by size.

---

## Rulers and gutters

```cpp
pkt->SetRulerMode(UltraCanvas::PacketRulerMode::BitRuler);     // 0 4 8 ... 31
pkt->SetRulerMode(UltraCanvas::PacketRulerMode::ByteColumns);  // Byte 1 ... Byte 4
pkt->SetRulerMode(UltraCanvas::PacketRulerMode::NoRuler);
```

The classic RFC presentation writes two-digit tick labels vertically and ticks
at 0, 4, 8, …, **31** — note 31, not 32:

```cpp
pkt->SetRulerTitle("Bits");
pkt->SetRulerLabelStyle(UltraCanvas::PacketRulerLabelStyle::StackedDigits);
pkt->SetRulerTicks({0, 4, 8, 12, 16, 20, 24, 31});
```

Two independent left gutters are available, and they can be used together:

```cpp
pkt->SetRowIndexGutter(UltraCanvas::PacketRowIndexMode::WordNumber, "Words");
pkt->SetOffsetGutter(UltraCanvas::PacketRowIndexMode::HexOffset);
```

| Mode | Shows |
|---|---|
| `WordNumber` | 1, 2, 3 … |
| `ByteOffset` | 0, 4, 8 … |
| `BitOffset` | 0, 32, 64 … |
| `HexOffset` | 0x00, 0x04 … |
| `NoIndex` | Nothing |

---

## Annotations

```cpp
// Double-headed dimension arrow with a label.
pkt->AddDimensionRail(UltraCanvas::PacketEdge::Top, 0, 31, "4 bytes (32 bits)");

// Braces work on either side - or both at once.
pkt->AddBrace(UltraCanvas::PacketEdge::Left,  "ip", "Header 20 bytes");
pkt->AddBrace(UltraCanvas::PacketEdge::Right, "ip", "Header");

pkt->SetTitle("TCP/IP Packet");
pkt->SetCaption("Figure 1: the layered PDU");
pkt->SetTrailerText("data begins here ...");
```

Rails stack outward when several share an edge; the label clears a gap in the
arrow so it stays legible.

---

## Colour and theme

```cpp
pkt->SetTheme(UltraCanvas::PacketTheme::Light);       // Light | Dark | Monochrome
pkt->SetColorMode(UltraCanvas::PacketColorMode::ByLayer);
pkt->SetRowBanding(true);
pkt->SetPayloadStyle(UltraCanvas::PacketPayloadStyle::GradientCurved);
```

| Colour mode | Source of the fill |
|---|---|
| `ByLayer` | Each protocol layer's own tint (default) |
| `ByKind` | Payload / checksum / flag / marker palettes |
| `BySourceByte` | Quantised ramp over the byte index |
| `Uniform` | One fill for everything |

Highlights are a **separate** semantic layer, independent of the colour mode —
the emphasis set in the reference TCP/IP figure is exactly this:

```cpp
pkt->SetHighlightFields({"Source Address", "Destination Address",
                         "Source Port", "Destination Port"},
                        UltraCanvas::Color(160, 205, 160, 255));
```

Precedence: highlight → per-field override → kind (Reserved/Padding) →
colour mode.

---

## Field values

The element decodes nothing. Any host with its own decoder supplies values:

```cpp
pkt->SetShowFieldValues(true);
pkt->SetFieldValueText("Source Address", "192.168.1.10");
pkt->SetFieldValueText("Protocol", "6");
```

Automatic extraction from a bound byte buffer (`SetPacketBytes`) is Phase 2.

---

## Interaction

```cpp
pkt->SetOnFieldClick([](const UltraCanvas::PacketFieldRef& f) {
    std::printf("%s @ bit %llu (%llu bits), byte %llu\n",
                f.name.c_str(),
                (unsigned long long)f.bitOffset,
                (unsigned long long)f.bitLength,
                (unsigned long long)f.ByteOffset());
});
pkt->SetOnFieldHover(...);
pkt->SetSelectedField("Source Address");
```

`PacketFieldRef` always reports the offset and length of the **whole logical
field**, never of the fragment that happened to be under the cursor.

Hovering shows a tooltip with the name, bit and byte offset, length,
description and value. Disable with `SetShowTooltips(false)`.

---

## Validation

```cpp
UltraCanvas::PacketValidationResult v = pkt->Validate(/*expectedTotalBits*/ 160);
for (const auto& issue : v.issues) {
    std::printf("[%d] %s\n", (int)issue.severity, issue.message.c_str());
}
```

| Check | Severity |
|---|---|
| Two fields claiming the same bits | Error |
| Unclaimed bit range (gap) | Warning |
| Total length ≠ expected | Error |
| Zero-length non-variable field | Error |
| Duplicate field id | Error |
| Unnamed non-reserved field | Warning |

`FillPacketModelGaps(model)` inserts `Reserved` fields into any holes and
returns how many it added.

---

## Text specs (Mermaid-compatible)

The baseline grammar is Mermaid's `packet` syntax **verbatim**, so a diagram
copied out of Markdown parses unchanged:

```
packet
title UDP Packet
+16: "Source Port"
+16: "Destination Port"
32-47: "Length"
48-63: "Checksum"
```

UltraCanvas extensions are trailing `@modifiers`, which Mermaid ignores —
stripping them always leaves a valid Mermaid diagram:

| Modifier | Effect |
|---|---|
| `@payload` `@reserved` `@padding` `@flag` `@checksum` `@marker` `@variable` `@ellipsis` `@length` | Sets the field kind |
| `@const=<text>` | Constant annotation |
| `@attr=<text>` | Attribute line |
| `@desc=<text>` | Tooltip description |
| `@id=<text>` | Explicit field id |
| `@color=#RRGGBB` | Per-field colour |
| `layer <id> "<name>"` | Starts a new layer |
| `caption <text>` | Figure caption |

```cpp
if (!pkt->LoadFromText(spec)) {
    for (const auto& e : pkt->GetParseErrors()) std::printf("%s\n", e.c_str());
}
```

> **C++ gotcha:** a spec containing `)"` — for example
> `"Data (variable length)"` — closes a plain `R"( … )"` raw string early. Use
> a custom delimiter: `R"PKT( … )PKT"`.

Emitters:

```cpp
std::string mermaid = UltraCanvas::EmitPacketText(pkt->GetModel(), /*ext*/ false);
std::string ascii   = UltraCanvas::EmitPacketAscii(pkt->GetModel());
```

---

## Built-in templates

Plain static field tables — no parsing code, no protocol behaviour. Each is
pinned to its specification by `Tests/PacketLayoutTest.cpp` (for example RTP:
version at bits 0–1, payload type at 9–15, SSRC at 64–95). That test is the
drift protection.

| Template | Spec | Size |
|---|---|---|
| `PacketTemplates::Ethernet()` | Ethernet II | 14 bytes |
| `PacketTemplates::IPv4()` | RFC 791 | 20 bytes |
| `PacketTemplates::IPv6()` | RFC 8200 | 40 bytes |
| `PacketTemplates::TCP()` | RFC 793 | 20 bytes |
| `PacketTemplates::UDP()` | RFC 768 | 8 bytes |
| `PacketTemplates::ICMP()` | RFC 792 | 8 bytes |
| `PacketTemplates::ARP()` | RFC 826 | 28 bytes |
| `PacketTemplates::RTP()` | RFC 3550 §5.1 | 12 bytes |

```cpp
pkt->SetModel(UltraCanvas::PacketTemplates::Stack({
        UltraCanvas::PacketTemplates::Ethernet(),
        UltraCanvas::PacketTemplates::IPv4(),
        UltraCanvas::PacketTemplates::TCP()
}));
```

---

## Legend

The element uses the shared `UltraCanvasChartLegend`, not a private
implementation:

```cpp
pkt->SetShowLegend(true);
auto& legend = pkt->GetLegend();
legend.SetTitle("Layers");
legend.AddEntry({"IP Header", ipColor});
legend.AddEntry({"TCP", tcpColor});
legend.SetPosition(UltraCanvas::ChartLegendPosition::BottomCenter);
```

---

## Using the layout engine directly

`ComputePacketLayout` is a pure function with no UI dependency, so tools and
tests can use it without a window:

```cpp
#include "Plugins/Diagrams/UltraCanvasPacketLayout.h"

UltraCanvas::PacketLayoutOptions opt;
opt.mode = UltraCanvas::PacketLayoutMode::WordGrid;
opt.bitsPerRow = 32;
opt.rowHeight = 30.0;

auto layout = UltraCanvas::ComputePacketLayout(model, opt, Rect2Dd(0, 0, 320, 400));
for (const auto& cell : layout.cells) {
    // cell.bounds, cell.bitOffset, cell.bitLength,
    // cell.continuesLeft / continuesRight, cell.IsLabelCell()
}
```

---

## Not yet implemented (Phase 2+)

Lane rendering details (per-bit `b7…b0` labels, signal markers, bit-to-lane
striping), the encapsulation mode, exploded detail bands with leader lines,
byte binding with automatic decoding, zoom/pan, SVG/ASCII import, and the
per-band legend variants. See the proposal document for the full roadmap.

---

## See also

- [`UltraCanvasPacketDiagramProposal.md`](UltraCanvasPacketDiagramProposal.md) — research, taxonomy and roadmap
- [`UltraCanvasNodeDiagram.md`](UltraCanvasNodeDiagramExamples.md) — the host for the packet-switching variant (Phase 2)
- `Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp` — six worked examples
