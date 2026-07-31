// Tests/MindMapIOTest.cpp
// Interchange tests for UltraCanvasMindMapIO: Mermaid, FreeMind/Freeplane .mm,
// OPML, indented text, CSV, XMind content.json, SVG export and format sniffing.
//
// Every format is checked for (a) a faithful round-trip where one exists and
// (b) safe behaviour on malformed input - a failed import must leave the
// caller's model untouched rather than half-destroying it.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMapIO.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

using IO = UltraCanvasMindMapIO;

// Flattens a model to "depth:text" lines in pre-order, for shape comparison.
static std::vector<std::string> Shape(const MindMapModel& model) {
    std::vector<std::string> lines;
    model.ForEachTopic([&](const MindMapTopic& topic) {
        lines.push_back(std::to_string(model.GetDepth(topic.id)) + ":" + topic.text);
    });
    return lines;
}

static std::string Join(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& line : lines) { out += line; out += "|"; }
    return out;
}

static MindMapModel SampleModel() {
    MindMapModel model;
    model.BuildFromOutline({
        {0, "Product launch"},
        {1, "Research"},
        {2, "User interviews"},
        {2, "Competitor scan"},
        {1, "Design"},
        {2, "Wireframes"},
        {1, "Build"},
    });
    return model;
}

// =============================================================================

static void TestMermaid() {
    std::printf("\n[mermaid]\n");

    const std::string source =
        "mindmap\n"
        "  root((Product launch))\n"
        "    Research\n"
        "      User interviews\n"
        "      Competitor scan\n"
        "    Design\n"
        "      Wireframes\n"
        "    Build\n";

    MindMapModel model;
    MindMapImportResult result = IO::FromMermaid(model, source);
    CHECK(result.success, "mermaid source imports");
    CHECK(result.topicsImported == 7, "all seven nodes imported");

    const MindMapTopic* root = model.GetTopic(model.GetRootId());
    CHECK(root && root->text == "Product launch",
          "((circle)) delimiters stripped from the root text");
    CHECK(root && root->childIds.size() == 3, "three main topics");
    CHECK(model.GetDepth(model.GetTopic(root->childIds[0])->childIds[0]) == 2,
          "nesting depth follows indentation");

    // The circle shape must survive as a style override.
    CHECK(root && root->styleOverride.has_value() &&
          root->styleOverride->shape == MindMapNodeShape::Circle,
          "((circle)) maps to the Circle shape");

    // Tab indentation and a 4-space source must give the same tree.
    const std::string tabbed =
        "mindmap\n"
        "\tProduct launch\n"
        "\t\tResearch\n"
        "\t\t\tUser interviews\n";
    MindMapModel tabModel;
    CHECK(IO::FromMermaid(tabModel, tabbed).success, "tab-indented source imports");
    CHECK(tabModel.GetTopicCount() == 3, "tab indentation nests correctly");
    CHECK(tabModel.GetDepth(
              tabModel.GetTopic(tabModel.GetTopic(tabModel.GetRootId())->childIds[0])
                  ->childIds[0]) == 2,
          "tab depth matches space depth");

    // Icons.
    const std::string withIcon =
        "mindmap\n"
        "  Root\n"
        "    Child\n"
        "    ::icon(fa fa-book)\n";
    MindMapModel iconModel;
    CHECK(IO::FromMermaid(iconModel, withIcon).success, "source with an icon imports");
    const MindMapTopic* iconRoot = iconModel.GetTopic(iconModel.GetRootId());
    CHECK(iconRoot && !iconRoot->childIds.empty() &&
          iconModel.GetTopic(iconRoot->childIds[0])->iconPath == "fa fa-book",
          "::icon() attaches to the preceding node");
    CHECK(iconModel.GetTopicCount() == 2, "the icon line is not itself a topic");

    // Comments and a missing header are tolerated.
    MindMapModel looseModel;
    CHECK(IO::FromMermaid(looseModel, "%% a comment\nRoot\n  Child\n").success,
          "a source without the mindmap header still imports");

    // Export round-trip.
    MindMapModel source2 = SampleModel();
    std::string exported = IO::ToMermaid(source2);
    MindMapModel reimported;
    CHECK(IO::FromMermaid(reimported, exported).success, "exported mermaid re-imports");
    CHECK(Join(Shape(source2)) == Join(Shape(reimported)),
          "mermaid export/import round-trips the tree shape");

    // Empty input fails cleanly.
    MindMapModel untouched = SampleModel();
    size_t before = untouched.GetTopicCount();
    MindMapImportResult empty = IO::FromMermaid(untouched, "mindmap\n");
    CHECK(!empty.success, "an empty mindmap block fails");
    CHECK(!empty.error.empty(), "failure carries an explanation");
    CHECK(untouched.GetTopicCount() == before,
          "a failed import leaves the existing model untouched");
}

static void TestFreeMind() {
    std::printf("\n[freemind .mm]\n");

    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<map version=\"1.0.1\">\n"
        "  <node TEXT=\"Central\">\n"
        "    <node TEXT=\"Left branch\" POSITION=\"left\" FOLDED=\"true\">\n"
        "      <node TEXT=\"Hidden child\"/>\n"
        "    </node>\n"
        "    <node TEXT=\"Right branch\" POSITION=\"right\" LINK=\"https://example.com\">\n"
        "      <icon BUILTIN=\"idea\"/>\n"
        "    </node>\n"
        "  </node>\n"
        "</map>\n";

    MindMapModel model;
    MindMapImportResult result = IO::FromFreeMind(model, xml);
    CHECK(result.success, "FreeMind document imports");
    CHECK(result.topicsImported == 4, "all four nodes imported");

    const MindMapTopic* root = model.GetTopic(model.GetRootId());
    CHECK(root && root->text == "Central", "root TEXT read");
    CHECK(root && root->childIds.size() == 2, "two branches");

    const MindMapTopic* left = model.GetTopic(root->childIds[0]);
    CHECK(left && left->side == MindMapTopicSide::Left, "POSITION=left honoured");
    CHECK(left && left->collapsed, "FOLDED=true becomes collapsed");

    const MindMapTopic* right = model.GetTopic(root->childIds[1]);
    CHECK(right && right->side == MindMapTopicSide::Right, "POSITION=right honoured");
    CHECK(right && right->link == "https://example.com", "LINK read");
    CHECK(right && right->iconPath == "idea", "BUILTIN icon read");

    // Round-trip.
    std::string exported = IO::ToFreeMind(model);
    MindMapModel reimported;
    CHECK(IO::FromFreeMind(reimported, exported).success, "exported .mm re-imports");
    CHECK(Join(Shape(model)) == Join(Shape(reimported)),
          ".mm export/import round-trips the tree shape");
    const MindMapTopic* reRoot = reimported.GetTopic(reimported.GetRootId());
    CHECK(reRoot && reimported.GetTopic(reRoot->childIds[0])->collapsed,
          "collapsed state survives the round trip");
    CHECK(reRoot && reimported.GetTopic(reRoot->childIds[0])->side == MindMapTopicSide::Left,
          "side survives the round trip");

    // Text containing XML metacharacters must survive escaping.
    MindMapModel special;
    special.SetCentralTopic("A & B <tag> \"quoted\"");
    std::string escaped = IO::ToFreeMind(special);
    MindMapModel unescaped;
    CHECK(IO::FromFreeMind(unescaped, escaped).success, "escaped text re-imports");
    CHECK(unescaped.GetTopic(unescaped.GetRootId())->text == "A & B <tag> \"quoted\"",
          "XML metacharacters round-trip intact");

    // Malformed input.
    MindMapModel untouched = SampleModel();
    size_t before = untouched.GetTopicCount();
    CHECK(!IO::FromFreeMind(untouched, "<map><node TEXT=\"unclosed\"></map>").success,
          "malformed XML is rejected");
    CHECK(untouched.GetTopicCount() == before, "rejected XML leaves the model untouched");
    CHECK(!IO::FromFreeMind(untouched, "<other/>").success,
          "a non-FreeMind document is rejected");
    CHECK(!IO::FromFreeMind(untouched, "<map/>").success,
          "a map with no root node is rejected");
}

static void TestOpml() {
    std::printf("\n[opml]\n");

    const std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<opml version=\"2.0\">\n"
        "  <head><title>Plan</title></head>\n"
        "  <body>\n"
        "    <outline text=\"Central\">\n"
        "      <outline text=\"One\" url=\"https://example.com\"/>\n"
        "      <outline text=\"Two\">\n"
        "        <outline text=\"Two A\"/>\n"
        "      </outline>\n"
        "    </outline>\n"
        "  </body>\n"
        "</opml>\n";

    MindMapModel model;
    MindMapImportResult result = IO::FromOpml(model, xml);
    CHECK(result.success, "OPML imports");
    CHECK(result.topicsImported == 4, "all four outlines imported");

    const MindMapTopic* root = model.GetTopic(model.GetRootId());
    CHECK(root && root->text == "Central", "root outline text read");
    CHECK(root && root->childIds.size() == 2, "two children");
    CHECK(model.GetTopic(root->childIds[0])->link == "https://example.com", "url read");
    CHECK(model.GetDepth(model.GetTopic(root->childIds[1])->childIds[0]) == 2,
          "nested outline depth correct");

    // Several top-level outlines: the first becomes the centre, the rest its
    // children, rather than being dropped.
    const std::string multiRoot =
        "<opml version=\"2.0\"><body>"
        "<outline text=\"First\"/><outline text=\"Second\"/><outline text=\"Third\"/>"
        "</body></opml>";
    MindMapModel multi;
    CHECK(IO::FromOpml(multi, multiRoot).success, "multi-root OPML imports");
    CHECK(multi.GetTopicCount() == 3, "no top-level outline is dropped");
    CHECK(multi.GetTopic(multi.GetRootId())->childIds.size() == 2,
          "later top-level outlines become children of the first");

    // Round-trip.
    std::string exported = IO::ToOpml(model);
    MindMapModel reimported;
    CHECK(IO::FromOpml(reimported, exported).success, "exported OPML re-imports");
    CHECK(Join(Shape(model)) == Join(Shape(reimported)),
          "OPML export/import round-trips the tree shape");

    // Malformed.
    MindMapModel untouched = SampleModel();
    size_t before = untouched.GetTopicCount();
    CHECK(!IO::FromOpml(untouched, "<opml/>").success, "OPML with no body is rejected");
    CHECK(!IO::FromOpml(untouched, "<notopml><body/></notopml>").success,
          "a non-OPML document is rejected");
    CHECK(untouched.GetTopicCount() == before, "rejected OPML leaves the model untouched");
}

static void TestIndentedText() {
    std::printf("\n[indented text]\n");

    const std::string text =
        "Product launch\n"
        "  Research\n"
        "    User interviews\n"
        "    Competitor scan\n"
        "  Design\n"
        "    Wireframes\n";

    MindMapModel model;
    CHECK(IO::FromIndentedText(model, text).success, "indented text imports");
    CHECK(model.GetTopicCount() == 6, "all six lines imported");
    CHECK(model.GetTopic(model.GetRootId())->childIds.size() == 2, "two branches");

    // Bullets are tolerated.
    MindMapModel bulleted;
    CHECK(IO::FromIndentedText(bulleted, "- Root\n  - Child\n    - Grandchild\n").success,
          "bulleted text imports");
    CHECK(bulleted.GetTopic(bulleted.GetRootId())->text == "Root",
          "the bullet marker is stripped");
    CHECK(bulleted.GetTopicCount() == 3, "bulleted nesting works");

    // Round-trip.
    std::string exported = IO::ToIndentedText(model);
    MindMapModel reimported;
    CHECK(IO::FromIndentedText(reimported, exported).success, "exported text re-imports");
    CHECK(Join(Shape(model)) == Join(Shape(reimported)),
          "indented text round-trips the tree shape");

    // A four-space source with spacesPerLevel=4.
    MindMapModel wide;
    CHECK(IO::FromIndentedText(wide, "Root\n    Child\n        Grandchild\n", 4).success,
          "four-space indentation imports with spacesPerLevel=4");
    CHECK(wide.GetTopicCount() == 3, "four-space nesting works");
    CHECK(wide.GetDepth(
              wide.GetTopic(wide.GetTopic(wide.GetRootId())->childIds[0])->childIds[0]) == 2,
          "four-space depth is correct");

    MindMapModel empty;
    CHECK(!IO::FromIndentedText(empty, "   \n\n  \n").success,
          "whitespace-only input is rejected");
}

static void TestCsv() {
    std::printf("\n[csv]\n");

    // Deliberately out of order: a child appears before its parent.
    const std::string csv =
        "id,parent,text\n"
        "c1,b1,Grandchild\n"
        "b1,root,Child\n"
        "root,,Centre\n"
        "b2,root,\"Second, with comma\"\n";

    MindMapModel model;
    MindMapImportResult result = IO::FromCsv(model, csv);
    CHECK(result.success, "CSV imports");
    CHECK(result.topicsImported == 4, "all four rows imported");
    CHECK(model.GetRootId() == "root", "the parentless row becomes the root");
    CHECK(model.GetDepth("c1") == 2, "out-of-order rows still nest correctly");

    const MindMapTopic* second = model.GetTopic("b2");
    CHECK(second && second->text == "Second, with comma",
          "a quoted field containing a comma is parsed correctly");

    // Doubled quotes.
    MindMapModel quoted;
    CHECK(IO::FromCsv(quoted, "id,parent,text\nr,,\"He said \"\"hi\"\"\"\n").success,
          "doubled quotes import");
    CHECK(quoted.GetTopic("r")->text == "He said \"hi\"",
          "doubled quotes decode to a single quote");

    // Alternative text column names.
    MindMapModel labelled;
    CHECK(IO::FromCsv(labelled, "id,parent,label\nr,,Root\na,r,Alpha\n").success,
          "a 'label' column is found when 'text' is absent");
    CHECK(labelled.GetTopic("a")->text == "Alpha", "label column value read");

    // Round-trip.
    std::string exported = IO::ToCsv(model);
    MindMapModel reimported;
    CHECK(IO::FromCsv(reimported, exported).success, "exported CSV re-imports");
    CHECK(Join(Shape(model)) == Join(Shape(reimported)),
          "CSV round-trips the tree shape");

    // Missing columns are reported, not guessed.
    MindMapModel untouched = SampleModel();
    size_t before = untouched.GetTopicCount();
    MindMapImportResult noParent = IO::FromCsv(untouched, "id,text\nr,Root\n");
    CHECK(!noParent.success, "CSV without a parent column is rejected");
    CHECK(noParent.error.find("parent") != std::string::npos,
          "the error names the missing column");
    CHECK(!IO::FromCsv(untouched, "id,parent\nr,\n").success,
          "CSV without a text column is rejected");
    CHECK(!IO::FromCsv(untouched, "id,parent,text\n").success,
          "CSV with only a header is rejected");
    CHECK(untouched.GetTopicCount() == before, "rejected CSV leaves the model untouched");
}

static void TestXMind() {
    std::printf("\n[xmind content.json]\n");

    const std::string content = R"([
      {
        "id": "sheet1",
        "title": "Sheet 1",
        "rootTopic": {
          "id": "root",
          "title": "Central",
          "children": {
            "attached": [
              { "id": "a", "title": "Alpha", "branch": "folded",
                "children": { "attached": [ { "id": "a1", "title": "Alpha one" } ] } },
              { "id": "b", "title": "Beta", "href": "https://example.com" }
            ]
          }
        }
      }
    ])";

    MindMapModel model;
    MindMapImportResult result = IO::FromXMindJson(model, content);
    CHECK(result.success, "XMind content.json imports");
    CHECK(result.topicsImported == 4, "all four topics imported");

    const MindMapTopic* root = model.GetTopic(model.GetRootId());
    CHECK(root && root->text == "Central", "rootTopic title read");
    CHECK(root && root->childIds.size() == 2, "attached children imported");

    const MindMapTopic* alpha = model.GetTopic(root->childIds[0]);
    CHECK(alpha && alpha->collapsed, "branch:folded becomes collapsed");
    CHECK(alpha && alpha->childIds.size() == 1, "nested attached children imported");

    const MindMapTopic* beta = model.GetTopic(root->childIds[1]);
    CHECK(beta && beta->link == "https://example.com", "href read");

    // A bare object (not wrapped in a sheet array) is also accepted.
    MindMapModel bare;
    CHECK(IO::FromXMindJson(bare, R"({"rootTopic":{"title":"Solo"}})").success,
          "a bare sheet object imports");
    CHECK(bare.GetTopic(bare.GetRootId())->text == "Solo", "bare sheet root read");

    // Malformed.
    MindMapModel untouched = SampleModel();
    size_t before = untouched.GetTopicCount();
    CHECK(!IO::FromXMindJson(untouched, "{ not json").success, "malformed JSON is rejected");
    CHECK(!IO::FromXMindJson(untouched, "[]").success, "an empty sheet array is rejected");
    CHECK(!IO::FromXMindJson(untouched, R"([{"title":"No root"}])").success,
          "a sheet with no rootTopic is rejected");
    CHECK(untouched.GetTopicCount() == before, "rejected JSON leaves the model untouched");

    // A missing file must fail with an explanation rather than crash.
    MindMapModel fromFile;
    MindMapImportResult missing = IO::FromXMindFile(fromFile, "/nonexistent/path.xmind");
    CHECK(!missing.success, "a missing .xmind file fails");
    CHECK(!missing.error.empty(), "the failure explains itself");
}

static void TestSvgExport() {
    std::printf("\n[svg export]\n");

    MindMapModel model = SampleModel();
    MindMapLayoutResult layout;
    UltraCanvasMindMapLayout::ComputeLayout(model, MindMapLayoutOptions(),
                                            nullptr, nullptr, layout);

    std::string svg = IO::ToSvg(model, layout);
    CHECK(svg.find("<?xml") == 0, "SVG starts with an XML declaration");
    CHECK(svg.find("<svg") != std::string::npos, "SVG has an svg element");
    CHECK(svg.find("xmlns=\"http://www.w3.org/2000/svg\"") != std::string::npos,
          "SVG declares the correct namespace");
    CHECK(svg.find("</svg>") != std::string::npos, "SVG is closed");
    CHECK(svg.find("viewBox") != std::string::npos, "SVG has a viewBox");

    // Every topic's text should appear.
    bool allPresent = true;
    model.ForEachTopic([&](const MindMapTopic& topic) {
        if (svg.find(topic.text) == std::string::npos) allPresent = false;
    });
    CHECK(allPresent, "every topic's label appears in the SVG");

    // Connectors: one path per non-root topic.
    size_t pathCount = 0;
    size_t pos = 0;
    while ((pos = svg.find("<path", pos)) != std::string::npos) { ++pathCount; ++pos; }
    CHECK(pathCount == model.GetTopicCount() - 1,
          "one connector path per non-root topic");

    // Text with XML metacharacters must be escaped.
    MindMapModel special;
    special.SetCentralTopic("A & B <tag>");
    MindMapLayoutResult specialLayout;
    UltraCanvasMindMapLayout::ComputeLayout(special, MindMapLayoutOptions(),
                                            nullptr, nullptr, specialLayout);
    std::string specialSvg = IO::ToSvg(special, specialLayout);
    CHECK(specialSvg.find("A &amp; B &lt;tag&gt;") != std::string::npos,
          "SVG escapes XML metacharacters in labels");

    // An empty map still produces a valid document rather than a broken file.
    MindMapModel empty;
    MindMapLayoutResult emptyLayout;
    std::string emptySvg = IO::ToSvg(empty, emptyLayout);
    CHECK(emptySvg.find("<svg") != std::string::npos,
          "an empty map still yields a valid SVG document");
}

static void TestFormatDetection() {
    std::printf("\n[format detection]\n");

    CHECK(IO::DetectFormat("mindmap\n  Root\n    Child\n") == IO::Format::Mermaid,
          "mermaid detected");
    CHECK(IO::DetectFormat("<?xml version=\"1.0\"?><map><node TEXT=\"x\"/></map>") ==
              IO::Format::FreeMind,
          "freemind detected");
    CHECK(IO::DetectFormat("<opml version=\"2.0\"><body/></opml>") == IO::Format::Opml,
          "opml detected");
    CHECK(IO::DetectFormat("{\"topics\":[]}") == IO::Format::Json, "json detected");
    CHECK(IO::DetectFormat("# Heading\n- item\n") == IO::Format::Markdown,
          "markdown detected");
    CHECK(IO::DetectFormat("id,parent,text\nr,,Root\n") == IO::Format::Csv, "csv detected");
    CHECK(IO::DetectFormat("Root\n  Child\n") == IO::Format::IndentedText,
          "indented text detected");
    CHECK(IO::DetectFormat("") == IO::Format::Unknown, "empty input is unknown");

    CHECK(IO::FormatFromExtension(".mm") == IO::Format::FreeMind, "extension .mm");
    CHECK(IO::FormatFromExtension("opml") == IO::Format::Opml, "extension opml without dot");
    CHECK(IO::FormatFromExtension(".MD") == IO::Format::Markdown, "extension is case-insensitive");
    CHECK(IO::FormatFromExtension(".xyz") == IO::Format::Unknown, "unknown extension");
}

static void TestCrossFormatFidelity() {
    std::printf("\n[cross-format fidelity]\n");

    // The same tree pushed through every text format must come back identical
    // in shape. This is what makes the formats actually interoperable.
    MindMapModel original = SampleModel();
    std::string expected = Join(Shape(original));

    struct Pass {
        const char* name;
        std::string (*write)(const MindMapModel&);
        MindMapImportResult (*read)(MindMapModel&, const std::string&);
    };

    // Wrappers, since the real signatures carry default arguments.
    struct Wrap {
        static std::string Mermaid(const MindMapModel& m) { return IO::ToMermaid(m); }
        static MindMapImportResult ReadMermaid(MindMapModel& m, const std::string& s) {
            return IO::FromMermaid(m, s);
        }
        static std::string FreeMind(const MindMapModel& m) { return IO::ToFreeMind(m); }
        static MindMapImportResult ReadFreeMind(MindMapModel& m, const std::string& s) {
            return IO::FromFreeMind(m, s);
        }
        static std::string Opml(const MindMapModel& m) { return IO::ToOpml(m); }
        static MindMapImportResult ReadOpml(MindMapModel& m, const std::string& s) {
            return IO::FromOpml(m, s);
        }
        static std::string Text(const MindMapModel& m) { return IO::ToIndentedText(m); }
        static MindMapImportResult ReadText(MindMapModel& m, const std::string& s) {
            return IO::FromIndentedText(m, s);
        }
        static std::string Csv(const MindMapModel& m) { return IO::ToCsv(m); }
        static MindMapImportResult ReadCsv(MindMapModel& m, const std::string& s) {
            return IO::FromCsv(m, s);
        }
    };

    const Pass passes[] = {
        {"mermaid",  &Wrap::Mermaid,  &Wrap::ReadMermaid},
        {"freemind", &Wrap::FreeMind, &Wrap::ReadFreeMind},
        {"opml",     &Wrap::Opml,     &Wrap::ReadOpml},
        {"text",     &Wrap::Text,     &Wrap::ReadText},
        {"csv",      &Wrap::Csv,      &Wrap::ReadCsv},
    };

    for (const auto& pass : passes) {
        MindMapModel reimported;
        std::string encoded = pass.write(original);
        MindMapImportResult result = pass.read(reimported, encoded);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "%s: round-trips the full tree shape", pass.name);
        CHECK(result.success && Join(Shape(reimported)) == expected, msg);
    }
}

int main() {
    std::printf("UltraCanvasMindMapIO interchange tests\n");
    std::printf("======================================\n");

    TestMermaid();
    TestFreeMind();
    TestOpml();
    TestIndentedText();
    TestCsv();
    TestXMind();
    TestSvgExport();
    TestFormatDetection();
    TestCrossFormatFidelity();

    std::printf("\n======================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
