// Tests/XARFidelityTest.cpp
// Checks that the repo's XAR samples resolve to what Xara itself draws.
//
// This guards the class of defect no structural check catches: the file
// parses, every node is present, the geometry is in the right place — and the
// picture is still wrong. Each sample carries Xara's own rendering of itself
// as an embedded preview bitmap, and the expectations below are read off that
// rendering and off the file's own bytes.
//
// Colours. Xara's Shade colours store two SIGNED parameters relative to a
// parent colour, not absolute colour coordinates, so recomputing the colour
// from them collapsed Apple5's soft gradients into hard dark bands and
// drained Midget's bodywork. The expectations are the RGB triples the files
// carry at the head of each colour record — Xara's own colour-managed screen
// values. They cover the cases that broke: Shade colours over HSV and over
// CMYK parents, a Shade with negative parameters, and plain CMYK (whose
// screen appearance comes from Xara's separation tables and is not
// reproducible from a (1-ink)(1-K) formula).
//
// Winding rule. Xara fills by the even-odd rule. Every letter outline on
// Midget's number plate winds all of its subpaths the same way, so under
// non-zero the counters of "B" and "9" filled solid.
//
// Text kerns. TAG_TEXT_KERN's two INT32s are not a coordinate pair; reading
// them as one lifted "AB Designs - February 1992" 4pt off the baseline it
// shares with "Produced by".
//
// Usage: XARFidelityTest [samples-dir]
// Exit code is the number of failed expectations.
// Version: 1.1.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/XAR/UltraCanvasXARPlugin.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int gFailures = 0;

void Check(bool ok, const char* label, const std::string& detail) {
    if (!ok) ++gFailures;
    std::printf("  %s %-34s %s\n", ok ? "ok  " : "FAIL", label, detail.c_str());
}

// ===== COLOURS =====

struct ColourExpectation {
    int32_t ref;                // colour record's sequence number in the file
    uint8_t r, g, b;
    const char* what;           // colour model / type, for the failure message
};

void CheckColours(XARDocument& doc, const std::vector<ColourExpectation>& expected) {
    for (const auto& e : expected) {
        const Color c = doc.ResolveColorRef(e.ref);
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "expected (%3u,%3u,%3u)  got (%3u,%3u,%3u)  %s",
                      e.r, e.g, e.b, c.r, c.g, c.b, e.what);
        char label[32];
        std::snprintf(label, sizeof(label), "colour %d", e.ref);
        Check(c.r == e.r && c.g == e.g && c.b == e.b, label, detail);
    }
}

// ===== TREE WALK =====

void Collect(const XARNodePtr& node, std::vector<XARNode*>& out) {
    if (!node) return;
    out.push_back(node.get());
    for (const auto& child : node->children) Collect(child, out);
}

std::vector<XARNode*> AllNodes(XARDocument& doc) {
    std::vector<XARNode*> nodes;
    Collect(doc.GetRoot(), nodes);
    return nodes;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : XAR_SAMPLES_DIR;

    // ===== Apple5.xar =====
    {
        const std::string path = dir + "/Apple5.xar";
        std::printf("%s\n", path.c_str());
        XARDocument doc;
        if (!doc.LoadFromFile(path)) {
            Check(false, "load", path);
        } else {
            CheckColours(doc, {
                {  112,  47, 161,  49, "HSV normal (shade parent)" },
                {  113,   6, 145,   8, "HSV shade of 112" },
                {  291,  33,  27,   3, "RGB normal" },
                { 3332,   9, 106,   6, "HSV shade, negative parameters" },
                { 3818, 255, 249, 176, "CMYK normal" },
                { 4591, 221, 255, 158, "HSV shade, brightest highlight" },
            });
        }
    }

    // ===== Midget.xar =====
    {
        const std::string path = dir + "/Midget.xar";
        std::printf("%s\n", path.c_str());
        XARDocument doc;
        if (!doc.LoadFromFile(path)) {
            Check(false, "load", path);
            std::printf("\nFAIL (%d failed expectation(s))\n", gFailures);
            return gFailures;
        }
        CheckColours(doc, {
            {   62, 109, 110, 113, "CMYK normal (shade parent)" },
            {  519, 239, 238, 239, "CMYK shade of 62" },
            {  151, 225, 166, 172, "HSV normal (shade parent)" },
            { 1563, 234, 172, 178, "HSV shade of 151" },
            { 2319, 198, 138,  47, "CMYK shade of 62" },
        });

        const std::vector<XARNode*> nodes = AllNodes(doc);

        // Winding rule. Every TAG_WINDINGRULE record in this file carries
        // byte 0, and the drawing needs the even-odd rule throughout, so no
        // node may come out non-zero.
        size_t paths = 0, nonZero = 0;
        for (XARNode* n : nodes) {
            if (n->type != XARNodeType::Path) continue;
            ++paths;
            if (n->windingRule == XARWindingRule::NonZero) ++nonZero;
        }
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "%zu of %zu path nodes resolved to non-zero, expected 0",
                      nonZero, paths);
        Check(paths > 0 && nonZero == 0, "winding rule", detail);

        // Text kerns. The file has seven, and the credit line's is 55
        // millipoints — a hair of letter spacing. The other field of that
        // record holds 4006, which as a vertical offset would be a visible
        // 4pt step mid-line.
        std::vector<int32_t> kerns;
        for (XARNode* n : nodes) {
            if (n->type != XARNodeType::TextKern) continue;
            kerns.push_back(static_cast<XARTextKernNode*>(n)->kernMP);
        }
        std::vector<int32_t> sorted = kerns;
        std::sort(sorted.begin(), sorted.end());
        const std::vector<int32_t> expected = { -1, 1, 2, 3, 55, 73, 74 };
        std::string got;
        for (int32_t k : sorted) got += (got.empty() ? "" : ", ") + std::to_string(k);
        Check(sorted == expected, "text kerns",
              "expected {-1, 1, 2, 3, 55, 73, 74} millipoints, got {" + got + "}");
    }

    std::printf("\n%s (%d failed expectation(s))\n",
                gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures;
}
