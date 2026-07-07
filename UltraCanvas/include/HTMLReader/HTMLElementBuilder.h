// include/HTMLReader/HTMLElementBuilder.h
// Builds a native UltraCanvas element tree from an HTML::Document.
// Block-level markup becomes UltraCanvasContainer nodes with CSSLayout box
// properties; runs of inline content become UltraCanvasLabel nodes using
// Pango markup for bold/italic/links/inline color; <img> becomes
// UltraCanvasImageElement fed through a caller-supplied resource loader.
// The CSSLayout engine then does all measurement and layout natively —
// there is no separate HTML layout engine.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/HTMLStyleResolver.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {
namespace HTML {

struct BuildOptions {
    // Base typography / colors, reading-mode overrides.
    ResolverOptions style;

    // Extra stylesheet applied after the document's own (reader settings —
    // font scale, margins — win over author CSS at equal specificity).
    std::string userCss;

    bool enableImages = true;

    // Resolves an <img src> or <link href> to raw bytes. For EPUB this reads
    // from the archive; return an empty vector when the resource is missing.
    // Also used to load <link rel="stylesheet"> targets.
    std::function<std::vector<uint8_t>(const std::string& href)> resourceLoader;

    // Invoked with the raw (unresolved) href of an <a> when the user clicks
    // its text. Labels carry per-byte-range link data, so a run with several
    // links activates exactly the one under the pointer.
    std::function<void(const std::string& href)> onLinkActivated;
};

struct BuildResult {
    std::shared_ptr<UltraCanvasContainer> root;
    std::string title;
    std::vector<std::string> warnings;
    int elementCount = 0;   // native elements created

    // id/name attribute → the native element built for (or containing) it.
    // Viewers use this to scroll to #fragment link targets.
    std::unordered_map<std::string, std::shared_ptr<UltraCanvasUIElement>> anchors;
};

class ElementBuilder {
public:
    // Parse + resolve + build in one step.
    BuildResult Build(const std::string& html, const BuildOptions& options = {});

    // Build from an already parsed document (stylesheets inside the document
    // are applied automatically; linked stylesheets are fetched through
    // options.resourceLoader).
    BuildResult BuildDocument(Document& document, const BuildOptions& options = {});

private:
    BuildOptions opts;
    StyleResolver resolver;
    std::vector<std::string> warnings;
    int elementCount = 0;
    int nextId = 0;
    std::unordered_map<std::string, std::shared_ptr<UltraCanvasUIElement>> anchors;

    // Per-inline-run state: the rendered plain text built alongside the Pango
    // markup (same bytes the text layout reports from hit testing) and the
    // link ranges found in it.
    std::string runPlain;
    std::vector<LabelTextLink> runLinks;

    std::string MakeId(const std::string& hint);

    // All containers in the built tree scroll via the host's scroll view, so
    // this creates them with their own scrollbars disabled.
    std::shared_ptr<UltraCanvasContainer> MakeContainer(const std::string& hint);

    std::shared_ptr<UltraCanvasContainer> BuildBlock(Node& element);
    void BuildChildrenInto(UltraCanvasContainer& parent, Node& element,
                           int listItemIndex = -1);
    std::shared_ptr<UltraCanvasLabel> BuildInlineRun(
        const std::vector<Node*>& run, const ComputedStyle& blockStyle,
        const std::string& markerPrefix);
    std::shared_ptr<UltraCanvasUIElement> BuildImage(Node& element);
    std::shared_ptr<UltraCanvasUIElement> BuildRule(Node& element);
    std::shared_ptr<UltraCanvasContainer> BuildTable(Node& element);

    void AppendInlineMarkup(const Node& node, const ComputedStyle& runStyle,
                            bool preserveWhitespace, std::string& out);

    // Maps the node's id (and <a name>) to the built element in `anchors`,
    // recursing into `node`'s subtree when deep is true.
    void RegisterAnchors(const Node& node,
                         const std::shared_ptr<UltraCanvasUIElement>& element,
                         bool deep = false);

    void ApplyBoxStyle(UltraCanvasUIElement& target, const ComputedStyle& style,
                       bool fillWidth = true);
    void ConfigureLabel(UltraCanvasLabel& label, const ComputedStyle& style);

    static std::string MarkerText(ListMarker marker, int index);
};

// One-call convenience mirroring the old ConvertHTMLToElements API.
inline std::shared_ptr<UltraCanvasContainer> BuildElementsFromHTML(
    const std::string& html, const BuildOptions& options = {}) {
    ElementBuilder builder;
    return builder.Build(html, options).root;
}

} // namespace HTML
} // namespace UltraCanvas
