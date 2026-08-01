// Tests/PacketLayoutTest.cpp
// Unit tests for the packet structure model, layout engine and validators.
//
// Exercises the real PacketModel / PacketLayout / PacketTemplates code with no
// UI stack: the layout engine is pure geometry, so every assertion here is
// about numbers, not pixels.
//
// The template tests are the RFC-pinning tests that replace the rejected
// UltraNet coupling: they pin each built-in template to its specification
// rather than to another module's private parsing code.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPacketLayout.h"
#include "Plugins/Diagrams/UltraCanvasPacketTemplates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

// Assert a field sits at exactly [startBit, endBit] inclusive.
static void CheckField(const PacketLayer& layer, const std::string& name,
                       uint64_t startBit, uint64_t endBitInclusive) {
    const PacketField* f = layer.FindField(name);
    if (!f) {
        std::printf("  FAIL: field '%s' not found\n", name.c_str());
        ++g_failures;
        return;
    }
    const uint64_t expectedLen = endBitInclusive - startBit + 1;
    if (f->bitOffset != startBit || f->bitLength != expectedLen) {
        std::printf("  FAIL: %s at bits %llu-%llu (expected %llu-%llu)\n",
                    name.c_str(),
                    (unsigned long long)f->bitOffset,
                    (unsigned long long)(f->bitOffset + f->bitLength - 1),
                    (unsigned long long)startBit,
                    (unsigned long long)endBitInclusive);
        ++g_failures;
    } else {
        std::printf("  ok:   %s @ bits %llu-%llu\n", name.c_str(),
                    (unsigned long long)startBit,
                    (unsigned long long)endBitInclusive);
    }
}

// =============================================================================
// MODEL CONSTRUCTION
// =============================================================================

static void TestAutoAdvancingOffsets() {
    std::printf("\n=== Auto-advancing offsets ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("hdr", "Header");
    l.AddField("A", 4);
    l.AddField("B", 4);
    l.AddField("C", 16);

    CHECK(l.fields[0].bitOffset == 0,  "first field starts at bit 0");
    CHECK(l.fields[1].bitOffset == 4,  "second field follows the first");
    CHECK(l.fields[2].bitOffset == 8,  "third field follows the second");
    CHECK(l.TotalBits() == 24,         "layer total is 24 bits");
}

static void TestExplicitRanges() {
    std::printf("\n=== Explicit ranges (Mermaid '0-15:' form) ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("udp", "UDP");
    l.AddFieldAt("Source Port", 0, 15);
    l.AddFieldAt("Destination Port", 16, 31);

    CHECK(l.fields[0].bitLength == 16, "0-15 is 16 bits wide");
    CHECK(l.fields[1].bitOffset == 16, "16-31 starts at bit 16");
    CHECK(l.TotalBits() == 32,         "two 16-bit ports make one 32-bit word");
}

static void TestMultiLayerOffsets() {
    std::printf("\n=== Multi-layer PDU offsets ===\n");

    PacketModel m;
    PacketLayer& ip = m.AddLayer("ip", "IP Header");
    ip.AddField("Version", 4);
    ip.AddField("IHL", 4);
    ip.AddField("Rest", 152);          // 160 bits total = 20 bytes

    PacketLayer& tcp = m.AddLayer("tcp", "TCP");
    tcp.AddField("Source Port", 16);

    CHECK(m.LayerBitOffset(0) == 0,    "first layer starts at bit 0");
    CHECK(m.LayerBitOffset(1) == 160,  "TCP starts after the 20-byte IP header");
    CHECK(m.TotalBits() == 176,        "total is IP + the TCP field so far");
}

// =============================================================================
// WORD GRID LAYOUT - THE ROW-SPLITTING CONTRACT
// =============================================================================

static PacketLayoutOptions GridOptions() {
    PacketLayoutOptions opt;
    opt.mode = PacketLayoutMode::WordGrid;
    opt.bitsPerRow = 32;
    opt.rowHeight = 30.0;
    return opt;
}

static void TestRowSplitting() {
    std::printf("\n=== Row splitting: a 64-bit field over a 32-bit grid ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("tcp", "TCP");
    l.AddField("Source Port", 16);
    l.AddField("Destination Port", 16);
    l.AddField("Sequence Number", 64);      // spans two full rows

    const Rect2Dd area(0, 0, 320, 400);     // 320px / 32 bits = 10px per bit
    PacketLayoutResult r = ComputePacketLayout(m, GridOptions(), area);

    CHECK(r.valid, "layout is valid");
    CHECK(std::fabs(r.bitWidth - 10.0) < 1e-9, "bit width is 10px");

    // The 64-bit field must produce exactly 2 cells, not 1 and not 4.
    std::vector<size_t> seq = r.CellsOfField(0, 2);
    CHECK(seq.size() == 2, "64-bit field over a 32-bit grid produces 2 cells");

    if (seq.size() == 2) {
        const PacketCell& a = r.cells[seq[0]];
        const PacketCell& b = r.cells[seq[1]];

        CHECK(a.cellCountInField == 2 && b.cellCountInField == 2,
              "both fragments report the field's total fragment count");
        CHECK(a.IsLabelCell() && !b.IsLabelCell(),
              "only the first fragment carries the label");

        // The cut edges are what make it read as ONE field.
        CHECK(!a.continuesLeft && a.continuesRight,
              "first fragment is cut on its right edge only");
        CHECK(b.continuesLeft && !b.continuesRight,
              "second fragment is cut on its left edge only");

        CHECK(a.row == 1 && b.row == 2, "fragments land on rows 1 and 2");
        CHECK(std::fabs(a.bounds.width - 320.0) < 1e-9,
              "first fragment spans the full row");
        CHECK(a.bitLength == 32 && b.bitLength == 32,
              "each fragment carries 32 bits");
        CHECK(a.bitOffset == 32 && b.bitOffset == 64,
              "fragment offsets are absolute within the PDU");
    }
}

static void TestUnalignedSplit() {
    std::printf("\n=== Row splitting: an unaligned field ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("x", "X");
    l.AddField("Head", 24);       // bits 0-23
    l.AddField("Straddler", 16);  // bits 24-39: 8 bits on row 0, 8 on row 1

    const Rect2Dd area(0, 0, 320, 400);
    PacketLayoutResult r = ComputePacketLayout(m, GridOptions(), area);

    std::vector<size_t> cells = r.CellsOfField(0, 1);
    CHECK(cells.size() == 2, "a field straddling a row boundary splits in two");
    if (cells.size() == 2) {
        CHECK(r.cells[cells[0]].bitLength == 8,  "first fragment takes the row remainder");
        CHECK(r.cells[cells[1]].bitLength == 8,  "second fragment takes the rest");
        CHECK(r.cells[cells[0]].row == 0 && r.cells[cells[1]].row == 1,
              "fragments are on consecutive rows");
        CHECK(std::fabs(r.cells[cells[1]].bounds.x - area.x) < 1e-9,
              "the continuation starts at the left edge of the grid");
    }
}

static void TestNoSpuriousSplit() {
    std::printf("\n=== No spurious splitting for row-aligned fields ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("x", "X");
    l.AddField("Exactly One Row", 32);

    const Rect2Dd area(0, 0, 320, 400);
    PacketLayoutResult r = ComputePacketLayout(m, GridOptions(), area);

    std::vector<size_t> cells = r.CellsOfField(0, 0);
    CHECK(cells.size() == 1, "a field of exactly one row is a single cell");
    if (!cells.empty()) {
        CHECK(!r.cells[cells[0]].continuesLeft && !r.cells[cells[0]].continuesRight,
              "a whole-row field has no cut edges");
    }
}

static void TestFlagBitsAndSubFields() {
    std::printf("\n=== Single-bit flags and sub-fields ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("tcp", "TCP");
    l.AddField("Data Offset", 4);
    l.AddField("Reserved", 6, PacketFieldKind::Reserved);
    for (const char* f : {"U", "A", "P", "R", "S", "F"}) {
        l.AddField(f, 1, PacketFieldKind::Flag);
    }
    l.AddField("Window", 16);

    const Rect2Dd area(0, 0, 320, 400);
    PacketLayoutResult r = ComputePacketLayout(m, GridOptions(), area);

    CHECK(r.rowCount == 1, "the flags row fits in exactly one 32-bit word");

    // Each flag is one bit => exactly one bit-width wide.
    size_t flagCells = 0;
    for (const auto& c : r.cells) {
        if (c.kind == PacketFieldKind::Flag) {
            ++flagCells;
            if (std::fabs(c.bounds.width - r.bitWidth) > 1e-9) {
                std::printf("  FAIL: flag cell width %.3f != bit width %.3f\n",
                            c.bounds.width, r.bitWidth);
                ++g_failures;
            }
        }
    }
    CHECK(flagCells == 6, "six single-bit flag cells are emitted");

    // Sub-fields: DS split into DSCP + ECN (image 4).
    PacketModel m2;
    PacketLayer& l2 = m2.AddLayer("ip", "IP");
    PacketField& ds = l2.AddField("Differentiated Service", 8);
    ds.AddChild("DSCP", 6);
    ds.AddChild("ECN", 2);

    PacketLayoutResult r2 = ComputePacketLayout(m2, GridOptions(), area);
    size_t subCells = 0;
    for (const auto& c : r2.cells) if (c.depth == 1) ++subCells;
    CHECK(subCells == 2, "a field with two children emits two sub-cells");
}

static void TestHitTesting() {
    std::printf("\n=== Hit testing ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("udp", "UDP");
    l.AddField("Source Port", 16);
    l.AddField("Destination Port", 16);

    const Rect2Dd area(0, 0, 320, 400);
    PacketLayoutResult r = ComputePacketLayout(m, GridOptions(), area);

    const PacketCell* left  = r.FindCellAt(Point2Dd(10, 10));
    const PacketCell* right = r.FindCellAt(Point2Dd(200, 10));
    const PacketCell* miss  = r.FindCellAt(Point2Dd(10, 3000));

    CHECK(left && left->name == "Source Port",       "hit on the left half finds Source Port");
    CHECK(right && right->name == "Destination Port","hit on the right half finds Destination Port");
    CHECK(miss == nullptr,                            "a point outside the grid hits nothing");
}

// =============================================================================
// PROPORTIONAL LAYOUT
// =============================================================================

static void TestProportionalClamping() {
    std::printf("\n=== Proportional layout with clamping (image 3) ===\n");

    // 1 byte / 21 bytes / 96 bytes - a strict 1:21:96 ratio makes the first
    // cell unreadable, which is what Clamped mode exists to prevent.
    PacketModel m;
    PacketLayer& l = m.AddLayer("f", "");
    l.AddFieldBytes("Length", 1);
    l.AddFieldBytes("Header", 21);
    l.AddFieldBytes("Payload", 96, PacketFieldKind::Payload);

    PacketLayoutOptions opt;
    opt.mode = PacketLayoutMode::ProportionalLinear;
    opt.minCellWidth = 48.0;
    opt.lengthScale = PacketLengthScale::Clamped;

    const Rect2Dd area(0, 0, 700, 200);
    PacketLayoutResult r = ComputePacketLayout(m, opt, area);

    CHECK(r.cells.size() == 3, "three cells on one strip");
    if (r.cells.size() == 3) {
        CHECK(r.cells[0].bounds.width >= opt.minCellWidth,
              "the 1-byte field is at least minCellWidth wide");
        CHECK(r.cells[2].bounds.width > r.cells[1].bounds.width,
              "the 96-byte payload is still wider than the 21-byte header");
        CHECK(r.cells[1].bounds.width > r.cells[0].bounds.width,
              "ordering by size is preserved");

        const double total = r.cells[0].bounds.width + r.cells[1].bounds.width +
                             r.cells[2].bounds.width;
        CHECK(std::fabs(total - area.width) < 1.0,
              "the cells exactly fill the strip width");

        CHECK(std::fabs(r.cells[1].bounds.x -
                        (r.cells[0].bounds.x + r.cells[0].bounds.width)) < 1e-9,
              "cells are laid end to end with no gaps");
    }

    // Linear mode must NOT clamp - it is the honest-proportion mode.
    opt.lengthScale = PacketLengthScale::Linear;
    PacketLayoutResult rl = ComputePacketLayout(m, opt, area);
    CHECK(rl.cells[0].bounds.width < r.cells[0].bounds.width,
          "Linear mode gives the 1-byte field less width than Clamped");
}

static void TestLengthWeights() {
    std::printf("\n=== Length scale weights ===\n");
    CHECK(PacketLengthWeight(64, PacketLengthScale::Linear) == 64.0,
          "linear weight is the bit count");
    CHECK(std::fabs(PacketLengthWeight(64, PacketLengthScale::Sqrt) - 8.0) < 1e-9,
          "sqrt weight of 64 bits is 8");
    CHECK(PacketLengthWeight(0, PacketLengthScale::Linear) == 0.0,
          "a zero-length field has zero weight");
}

// =============================================================================
// LANES
// =============================================================================

static void TestLaneLayout() {
    std::printf("\n=== Lane layout (image 5: SDIO wide bus) ===\n");

    PacketModel m;
    PacketLayer& l = m.AddLayer("d", "");
    l.AddField("Data", 64);           // 64 bits over 4 lanes = 16 bits per lane

    PacketLayoutOptions opt;
    opt.mode = PacketLayoutMode::Lanes;
    opt.laneCount = 4;
    opt.rowHeight = 24.0;

    const Rect2Dd area(0, 0, 400, 200);
    PacketLayoutResult r = ComputePacketLayout(m, opt, area);

    CHECK(r.rowCount == 4, "four lanes are laid out");
    CHECK(r.cells.size() == 4, "a 64-bit field over 4 lanes splits into 4 cells");

    if (r.cells.size() == 4) {
        for (size_t i = 0; i < 4; ++i) {
            if (r.cells[i].row != i) {
                std::printf("  FAIL: cell %zu on lane %u, expected %zu\n",
                            i, r.cells[i].row, i);
                ++g_failures;
            }
        }
        std::printf("  ok:   each fragment lands on its own lane\n");
        CHECK(r.cells[0].continuesRight && r.cells[3].continuesLeft,
              "lane fragments carry continuation edges like row fragments");
    }
}

// =============================================================================
// VALIDATION
// =============================================================================

static void TestValidation() {
    std::printf("\n=== Validation ===\n");

    // Clean model.
    PacketModel good;
    PacketLayer& gl = good.AddLayer("x", "X");
    gl.AddField("A", 16);
    gl.AddField("B", 16);
    PacketValidationResult rg = ValidatePacketModel(good);
    CHECK(rg.IsClean(), "a contiguous model produces no issues");

    // Overlap (V1).
    PacketModel overlap;
    PacketLayer& ol = overlap.AddLayer("x", "X");
    ol.AddFieldAt("A", 0, 15);
    ol.AddFieldAt("B", 8, 23);       // overlaps A by 8 bits
    PacketValidationResult ro = ValidatePacketModel(overlap);
    CHECK(ro.HasErrors(), "an overlap is reported as an error");
    CHECK(ro.Count(PacketValidationSeverity::Error) == 1, "exactly one overlap error");

    // Gap (V2).
    PacketModel gap;
    PacketLayer& gpl = gap.AddLayer("x", "X");
    gpl.AddFieldAt("A", 0, 7);
    gpl.AddFieldAt("B", 16, 23);     // 8-bit hole
    PacketValidationResult rgap = ValidatePacketModel(gap);
    CHECK(rgap.Count(PacketValidationSeverity::Warning) >= 1,
          "a gap is reported as a warning");
    CHECK(!rgap.HasErrors(), "a gap is not an error");

    // Gap filling.
    size_t added = FillPacketModelGaps(gap);
    CHECK(added == 1, "one Reserved field is inserted to fill the hole");
    PacketValidationResult rfilled = ValidatePacketModel(gap);
    CHECK(rfilled.Count(PacketValidationSeverity::Warning) == 0,
          "the gap warning is gone after filling");

    // Total length (V3).
    PacketValidationResult rlen = ValidatePacketModel(good, 160);
    CHECK(rlen.HasErrors(), "a wrong expected total length is an error");
    PacketValidationResult rlen2 = ValidatePacketModel(good, 32);
    CHECK(!rlen2.HasErrors(), "the correct expected total length passes");
}

// =============================================================================
// RFC-PINNING TEMPLATE TESTS
// =============================================================================
// These replace the rejected UltraNet coupling: each template is pinned to its
// specification, not to another module's private parsing code.

static void TestIPv4Template() {
    std::printf("\n=== Template: IPv4 (RFC 791) ===\n");

    PacketModel m = PacketTemplates::IPv4();
    CHECK(m.layers.size() == 1, "IPv4 is one layer");
    const PacketLayer& l = m.layers[0];

    CHECK(l.TotalBits() == 160, "the IPv4 header is 160 bits (20 bytes)");

    CheckField(l, "Version", 0, 3);
    CheckField(l, "IHL", 4, 7);
    CheckField(l, "Total Length", 16, 31);
    CheckField(l, "Identification", 32, 47);
    CheckField(l, "Time to Live", 64, 71);
    CheckField(l, "Protocol", 72, 79);
    CheckField(l, "Header Checksum", 80, 95);
    CheckField(l, "Source Address", 96, 127);
    CheckField(l, "Destination Address", 128, 159);

    PacketValidationResult v = ValidatePacketModel(m, 160);
    CHECK(!v.HasErrors(), "the IPv4 template validates against its own length");
}

static void TestTCPTemplate() {
    std::printf("\n=== Template: TCP (RFC 793) ===\n");

    PacketModel m = PacketTemplates::TCP();
    const PacketLayer& l = m.layers[0];

    CHECK(l.TotalBits() == 160, "the TCP header is 160 bits (20 bytes)");

    CheckField(l, "Source Port", 0, 15);
    CheckField(l, "Destination Port", 16, 31);
    CheckField(l, "Sequence Number", 32, 63);
    CheckField(l, "Acknowledgement Number", 64, 95);
    CheckField(l, "Data Offset", 96, 99);
    CheckField(l, "Window", 112, 127);
    CheckField(l, "Checksum", 128, 143);
    CheckField(l, "Urgent Pointer", 144, 159);

    // The six flag bits sit between Reserved and Window.
    CheckField(l, "U", 106, 106);
    CheckField(l, "F", 111, 111);

    PacketValidationResult v = ValidatePacketModel(m, 160);
    CHECK(!v.HasErrors(), "the TCP template validates against its own length");
}

static void TestUDPTemplate() {
    std::printf("\n=== Template: UDP (RFC 768) ===\n");

    PacketModel m = PacketTemplates::UDP();
    const PacketLayer& l = m.layers[0];

    CHECK(l.TotalBits() == 64, "the UDP header is 64 bits (8 bytes)");
    CheckField(l, "Source Port", 0, 15);
    CheckField(l, "Destination Port", 16, 31);
    CheckField(l, "Length", 32, 47);
    CheckField(l, "Checksum", 48, 63);
}

static void TestRTPTemplate() {
    std::printf("\n=== Template: RTP (RFC 3550 s5.1) ===\n");

    // This is the test that replaces the proposed dependency on
    // Plugins/UltraNet/rtp: it pins the template to the RFC directly.
    PacketModel m = PacketTemplates::RTP();
    const PacketLayer& l = m.layers[0];

    CHECK(l.TotalBits() == 96, "the RTP header is 96 bits (12 bytes)");

    CheckField(l, "Version", 0, 1);
    CheckField(l, "P", 2, 2);
    CheckField(l, "X", 3, 3);
    CheckField(l, "CSRC Count", 4, 7);
    CheckField(l, "M", 8, 8);
    CheckField(l, "Payload Type", 9, 15);
    CheckField(l, "Sequence Number", 16, 31);
    CheckField(l, "Timestamp", 32, 63);
    CheckField(l, "SSRC", 64, 95);
}

static void TestEthernetTemplate() {
    std::printf("\n=== Template: Ethernet II ===\n");

    PacketModel m = PacketTemplates::Ethernet();
    const PacketLayer& l = m.layers[0];

    CHECK(l.TotalBits() == 112, "the Ethernet II header is 112 bits (14 bytes)");
    CheckField(l, "Destination MAC", 0, 47);
    CheckField(l, "Source MAC", 48, 95);
    CheckField(l, "EtherType", 96, 111);
}

static void TestAllTemplatesValidate() {
    std::printf("\n=== All templates validate cleanly ===\n");

    struct Named { const char* name; PacketModel model; };
    std::vector<Named> all = {
            {"IPv4",     PacketTemplates::IPv4()},
            {"IPv6",     PacketTemplates::IPv6()},
            {"TCP",      PacketTemplates::TCP()},
            {"UDP",      PacketTemplates::UDP()},
            {"ICMP",     PacketTemplates::ICMP()},
            {"ARP",      PacketTemplates::ARP()},
            {"Ethernet", PacketTemplates::Ethernet()},
            {"RTP",      PacketTemplates::RTP()},
    };

    for (auto& t : all) {
        PacketValidationResult v = ValidatePacketModel(t.model);
        if (v.HasErrors()) {
            std::printf("  FAIL: template %s has validation errors:\n", t.name);
            for (const auto& i : v.issues) {
                if (i.severity == PacketValidationSeverity::Error) {
                    std::printf("        %s\n", i.message.c_str());
                }
            }
            ++g_failures;
        } else {
            std::printf("  ok:   %s validates (%llu bits)\n", t.name,
                        (unsigned long long)t.model.TotalBits());
        }
    }
}

static void TestStackComposition() {
    std::printf("\n=== Template composition: Stack() ===\n");

    PacketModel stacked = PacketTemplates::Stack({
            PacketTemplates::Ethernet(),
            PacketTemplates::IPv4(),
            PacketTemplates::TCP()
    });

    CHECK(stacked.layers.size() == 3, "three stacked layers");
    CHECK(stacked.TotalBits() == 112 + 160 + 160,
          "stacked total is the sum of the parts");
    CHECK(stacked.LayerBitOffset(1) == 112, "IPv4 starts after the Ethernet header");
    CHECK(stacked.LayerBitOffset(2) == 272, "TCP starts after Ethernet + IPv4");

    // The layered figure of image 1 must lay out without overlaps.
    PacketLayoutResult r = ComputePacketLayout(stacked, GridOptions(),
                                               Rect2Dd(0, 0, 320, 800));
    CHECK(r.valid && !r.cells.empty(), "the stacked model lays out");
    CHECK(r.layerSpans.size() == 3, "each layer gets a bracket span");
}

// =============================================================================
// EDGE CASES
// =============================================================================

static void TestEdgeCases() {
    std::printf("\n=== Edge cases ===\n");

    // Empty model.
    PacketModel empty;
    PacketLayoutResult re = ComputePacketLayout(empty, GridOptions(),
                                                Rect2Dd(0, 0, 320, 200));
    CHECK(re.valid && re.cells.empty(), "an empty model lays out to zero cells");

    // Zero-size area.
    PacketModel m = PacketTemplates::UDP();
    PacketLayoutResult rz = ComputePacketLayout(m, GridOptions(),
                                                Rect2Dd(0, 0, 0, 0));
    CHECK(rz.valid && rz.cells.empty(), "a zero-size area produces no cells");

    // Open-ended payload still gets drawn height.
    PacketModel p;
    PacketLayer& pl = p.AddLayer("x", "X");
    pl.AddField("Header", 32);
    pl.AddField("Data", 0, PacketFieldKind::Payload);
    PacketLayoutResult rp = ComputePacketLayout(p, GridOptions(),
                                                Rect2Dd(0, 0, 320, 400));
    CHECK(rp.rowCount >= 2, "an open-ended payload reserves at least one row");
    bool foundPayload = false;
    for (const auto& c : rp.cells) {
        if (c.kind == PacketFieldKind::Payload && c.bounds.height > 0) foundPayload = true;
    }
    CHECK(foundPayload, "the payload cell has a drawable height");

    // Bits-per-row variations.
    PacketModel w;
    PacketLayer& wl = w.AddLayer("x", "X");
    wl.AddField("A", 64);
    PacketLayoutOptions o16 = GridOptions();
    o16.bitsPerRow = 16;
    PacketLayoutResult r16 = ComputePacketLayout(w, o16, Rect2Dd(0, 0, 320, 400));
    CHECK(r16.CellsOfField(0, 0).size() == 4,
          "a 64-bit field over a 16-bit grid produces 4 cells");
}

// =============================================================================

int main() {
    std::printf("PacketLayoutTest - packet model, layout engine and templates\n");
    std::printf("============================================================\n");

    TestAutoAdvancingOffsets();
    TestExplicitRanges();
    TestMultiLayerOffsets();

    TestRowSplitting();
    TestUnalignedSplit();
    TestNoSpuriousSplit();
    TestFlagBitsAndSubFields();
    TestHitTesting();

    TestProportionalClamping();
    TestLengthWeights();
    TestLaneLayout();

    TestValidation();

    TestIPv4Template();
    TestTCPTemplate();
    TestUDPTemplate();
    TestRTPTemplate();
    TestEthernetTemplate();
    TestAllTemplatesValidate();
    TestStackComposition();

    TestEdgeCases();

    std::printf("\n============================================================\n");
    if (g_failures == 0) {
        std::printf("All packet layout tests PASSED\n");
        return 0;
    }
    std::printf("%d packet layout test(s) FAILED\n", g_failures);
    return 1;
}
