// Apps/DemoApp/UltraCanvasPacketDiagramExamples.cpp
// Packet structure diagram demonstration. One tab per reference form: the
// layered TCP/IP figure with flag bits and highlighted address fields, the
// IPv4 header with dimension rails and an open payload, the classic
// monochrome RFC figure with a word gutter and stacked ruler digits, a
// proportional byte strip, a byte-column view, and the Mermaid-compatible
// text spec driving the same element.
//
// Each tab's second description line states what the mouse does there.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasPacketDiagram.h"
#include "Plugins/Diagrams/UltraCanvasPacketTemplates.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    static void AddPacketFlexChild(const std::shared_ptr<UltraCanvasContainer>& parent,
                                   const std::shared_ptr<UltraCanvasUIElement>& child,
                                   float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    }

    static std::shared_ptr<UltraCanvasLabel> MakePacketInfoLine(const std::string& id,
                                                                const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 46);
        label->SetText(text);
        label->SetFontSize(10);
        label->SetWrap(TextWrap::WrapWord);
        label->SetTextColor(Color(90, 90, 100, 255));
        return label;
    }

    static std::shared_ptr<UltraCanvasContainer> MakePacketTab(const std::string& id,
                                                               int w, int h) {
        auto tab = std::make_shared<UltraCanvasContainer>(id, 0, 0, w, h);
        tab->SetBackgroundColor(Color(252, 252, 254, 255));
        tab->SetPadding(10);
        tab->layout.SetFlexColumn().SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        return tab;
    }

// =============================================================================

    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreatePacketDiagramExamples() {
        const int CONTAINER_W = 1180;
        const int CONTAINER_H = 800;
        const int PAD = 16;

        auto root = std::make_shared<UltraCanvasContainer>("PacketRoot", 0, 0,
                                                           CONTAINER_W, CONTAINER_H);
        root->SetBackgroundColor(Color(250, 250, 252, 255));
        root->SetPadding(PAD);
        root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto title = std::make_shared<UltraCanvasLabel>("PkTitle", 0, 0, 0, 30);
        title->SetText("Packet Diagrams - Protocol Header & Frame Structure");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 50, 120, 255));
        AddPacketFlexChild(root, title, 0);

        auto desc = std::make_shared<UltraCanvasLabel>("PkDesc", 0, 0, 0, 34);
        desc->SetText("A packet diagram is a bit-accurate map of a protocol data unit: every "
                      "field is a box whose width is proportional to its size in bits, at its "
                      "exact offset. Fields wider than a row wrap and still read as one field. "
                      "Hover any cell for its offset and length; click to select it.");
        desc->SetFontSize(11);
        desc->SetWrap(TextWrap::WrapWord);
        desc->SetTextColor(Color(90, 90, 100, 255));
        AddPacketFlexChild(root, desc, 0);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("PacketTabs", 0, 0,
                                                                 CONTAINER_W - 2 * PAD, 690);
        tabs->SetTabStyle(TabStyle::Rounded);
        tabs->SetTabHeight(30);
        tabs->SetTabCornerRadius(8.0f);

        const int INNER_W = CONTAINER_W - 2 * PAD - 8;
        const int INNER_H = 650;

        // =====================================================================
        // Tab 1: layered TCP/IP packet - the full stack in one figure
        // =====================================================================
        {
            auto tab = MakePacketTab("PkTab1", INNER_W, INNER_H);

            auto pkt = CreatePacketDiagram("PkTcpIp", 0, 0, INNER_W - 24, INNER_H - 70);
            pkt->SetModel(PacketTemplates::Stack({
                    PacketTemplates::IPv4(),
                    PacketTemplates::TCP()
            }));
            pkt->SetTitle("TCP/IP Packet");
            pkt->SetLayoutMode(PacketLayoutMode::WordGrid);
            pkt->SetBitsPerRow(32);
            pkt->SetRulerMode(PacketRulerMode::BitRuler);
            pkt->SetColorMode(PacketColorMode::ByLayer);
            pkt->SetLayerBrackets(PacketEdge::Left);

            // A semantic emphasis set, independent of the layer tint.
            pkt->SetHighlightFields({"Source Address", "Destination Address",
                                     "Source Port", "Destination Port"},
                                    Color(160, 205, 160, 255));

            // The inline constant annotation: "Protocol=6 (TCP)".
            if (PacketLayer* ip = pkt->GetMutableModel().FindLayer("ipv4")) {
                if (PacketField* proto = ip->FindField("Protocol")) {
                    proto->SetConstantText("6 (TCP)");
                }
            }

            auto info = MakePacketInfoLine("PkTab1Info",
                    "Two headers plus payload in one figure. The six single-bit TCP flags "
                    "(U A P R S F) stack their letters vertically because the cell is one bit "
                    "wide; the 32-bit Sequence and Acknowledgement numbers each fill a whole "
                    "row. Left brackets label the layers. Mouse: hover for offsets, click to select.");

            AddPacketFlexChild(tab, pkt, 1);
            AddPacketFlexChild(tab, info, 0);
            tabs->AddTab("TCP/IP Stack", tab);
        }

        // =====================================================================
        // Tab 2: IPv4 with dimension rails, banding and an open payload
        // =====================================================================
        {
            auto tab = MakePacketTab("PkTab2", INNER_W, INNER_H);

            auto pkt = CreatePacketDiagram("PkIpv4", 0, 0, INNER_W - 24, INNER_H - 70);
            PacketModel m = PacketTemplates::IPv4();
            m.AddLayer("payload", "").AddField("Data", 0, PacketFieldKind::Payload);
            pkt->SetModel(m);
            pkt->SetTitle("IPv4 Header");
            pkt->SetRulerMode(PacketRulerMode::BitRuler);
            pkt->SetRowBanding(true, Color(214, 231, 245, 255), Color(186, 213, 235, 255));
            pkt->SetColorMode(PacketColorMode::Uniform);
            pkt->SetPayloadStyle(PacketPayloadStyle::GradientCurved);
            pkt->AddDimensionRail(PacketEdge::Top, 0, 31, "4 bytes (32 bits)");
            pkt->AddBrace(PacketEdge::Left, "ipv4", "Header 20 bytes");

            auto info = MakePacketInfoLine("PkTab2Info",
                    "A top dimension rail measures the row, a left brace measures the header, "
                    "and rows alternate light/dark. The Data region is open-ended: it carries "
                    "a curved corner meaning 'variable length, continues beyond the figure'. "
                    "Mouse: hover any field for its bit and byte offset.");

            AddPacketFlexChild(tab, pkt, 1);
            AddPacketFlexChild(tab, info, 0);
            tabs->AddTab("Rails + Payload", tab);
        }

        // =====================================================================
        // Tab 3: the classic monochrome RFC figure
        // =====================================================================
        {
            auto tab = MakePacketTab("PkTab3", INNER_W, INNER_H);

            auto pkt = CreatePacketDiagram("PkRfc", 0, 0, INNER_W - 24, INNER_H - 70);
            pkt->SetModel(PacketTemplates::IPv4());
            pkt->SetTheme(PacketTheme::Monochrome);
            pkt->SetColorMode(PacketColorMode::Uniform);
            pkt->SetRulerMode(PacketRulerMode::BitRuler);
            pkt->SetRulerLabelStyle(PacketRulerLabelStyle::StackedDigits);
            pkt->SetRulerTitle("Bits");
            // Note the last tick is 31, not 32 - the RFC convention.
            pkt->SetRulerTicks({0, 4, 8, 12, 16, 20, 24, 31});
            pkt->SetRowIndexGutter(PacketRowIndexMode::WordNumber, "Words");
            pkt->AddBrace(PacketEdge::Right, "ipv4", "Header");
            pkt->SetTrailerText("data begins here ...");

            auto info = MakePacketInfoLine("PkTab3Info",
                    "The O'Reilly/RFC presentation: monochrome, a 'Words' gutter numbering the "
                    "rows, ruler labels with their digits stacked vertically over tick marks, "
                    "and a brace on the RIGHT this time - braces work on either side, or both "
                    "at once. Mouse: hover for offsets.");

            AddPacketFlexChild(tab, pkt, 1);
            AddPacketFlexChild(tab, info, 0);
            tabs->AddTab("RFC Monochrome", tab);
        }

        // =====================================================================
        // Tab 4: proportional byte strip
        // =====================================================================
        {
            auto tab = MakePacketTab("PkTab4", INNER_W, INNER_H);

            auto pkt = CreatePacketDiagram("PkStrip", 0, 0, INNER_W - 24, 160);
            PacketModel m;
            m.title = "802.15.4-style Frame";
            PacketLayer& f = m.AddLayer("frame", "");
            f.AddFieldBytes("Length", 1, PacketFieldKind::LengthPrefix)
                    .SetDescription("Length of header + payload");
            f.AddFieldBytes("Header", 21);
            f.AddFieldBytes("Payload", 96, PacketFieldKind::Payload);

            pkt->SetModel(m);
            pkt->SetLayoutMode(PacketLayoutMode::ProportionalLinear);
            pkt->SetSizeUnit(PacketSizeUnit::Bytes);
            pkt->SetLengthScale(PacketLengthScale::Clamped, 90.0);
            pkt->SetRulerMode(PacketRulerMode::NoRuler);
            pkt->SetColorMode(PacketColorMode::ByKind);

            auto info = MakePacketInfoLine("PkTab4Info",
                    "Here width means BYTES, not bits, and the strip does not wrap. A strict "
                    "1:21:96 proportion would make the 1-byte Length cell unreadable, so "
                    "Clamped scaling gives every cell a floor and distributes the rest by size "
                    "- the ordering by size is still honest. (The exploded detail band that "
                    "expands Header into its sub-fields is Phase 2.) Mouse: hover for byte offsets.");

            AddPacketFlexChild(tab, pkt, 0);
            AddPacketFlexChild(tab, info, 0);
            tabs->AddTab("Proportional Strip", tab);
        }

        // =====================================================================
        // Tab 5: byte columns and sub-fields
        // =====================================================================
        {
            auto tab = MakePacketTab("PkTab5", INNER_W, INNER_H);

            auto pkt = CreatePacketDiagram("PkBytes", 0, 0, INNER_W - 24, INNER_H - 70);
            pkt->SetModel(PacketTemplates::IPv4());
            pkt->SetRulerMode(PacketRulerMode::ByteColumns);
            pkt->SetColorMode(PacketColorMode::ByLayer);
            pkt->SetOffsetGutter(PacketRowIndexMode::HexOffset);
            pkt->SetShowFieldValues(true);

            // The P1 escape hatch: a host with its own decoder supplies values
            // without the widget parsing anything.
            pkt->SetFieldValueText("Version", "4");
            pkt->SetFieldValueText("IHL", "5");
            pkt->SetFieldValueText("Total Length", "1500");
            pkt->SetFieldValueText("Protocol", "6");
            pkt->SetFieldValueText("Source Address", "192.168.1.10");
            pkt->SetFieldValueText("Destination Address", "93.184.216.34");

            auto info = MakePacketInfoLine("PkTab5Info",
                    "Byte columns instead of a bit ruler, a hex offset gutter, and sub-fields "
                    "drawn inside their parent (Type of Service splits into DSCP and ECN, Flags "
                    "into R/DF/MF). Field values come from the host application - the widget "
                    "decodes nothing itself. Mouse: hover to see offset, length and value.");

            AddPacketFlexChild(tab, pkt, 1);
            AddPacketFlexChild(tab, info, 0);
            tabs->AddTab("Byte Columns + Values", tab);
        }

        // =====================================================================
        // Tab 6: the Mermaid-compatible text spec
        // =====================================================================
        {
            auto tab = MakePacketTab("PkTab6", INNER_W, INNER_H);

            auto pkt = CreatePacketDiagram("PkText", 0, 0, INNER_W - 24, INNER_H - 90);
            // Custom raw-string delimiter: the spec text itself contains )"
            // inside "Data (variable length)".
            pkt->LoadFromText(R"PKT(packet
title UDP Packet
+16: "Source Port"
+16: "Destination Port"
32-47: "Length"
48-63: "Checksum"
64-95: "Data (variable length)" @payload
)PKT");
            pkt->SetRulerMode(PacketRulerMode::BitRuler);
            pkt->SetColorMode(PacketColorMode::ByKind);
            pkt->SetPayloadStyle(PacketPayloadStyle::Gradient);

            auto info = MakePacketInfoLine("PkTab6Info",
                    "This diagram was built from a text spec, not from code. The grammar is "
                    "Mermaid's `packet` syntax verbatim - both the '+16:' auto-advancing form "
                    "and explicit '32-47:' ranges - so a diagram copied out of Markdown parses "
                    "unchanged. UltraCanvas extras ride along as trailing @modifiers "
                    "(here @payload) which Mermaid ignores, so stripping them always leaves a "
                    "valid Mermaid diagram.");

            AddPacketFlexChild(tab, pkt, 1);
            AddPacketFlexChild(tab, info, 0);
            tabs->AddTab("Text Spec (Mermaid)", tab);
        }

        AddPacketFlexChild(root, tabs, 1);
        return root;
    }

} // namespace UltraCanvas
