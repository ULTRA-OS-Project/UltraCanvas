// include/HTMLReader/HTMLElementBuilder.h
// Builds a native UltraCanvas element tree from an HTML::Document.
// Block-level markup becomes UltraCanvasContainer nodes with CSSLayout box
// properties; runs of inline content become UltraCanvasLabel nodes using
// Pango markup for bold/italic/links/inline color; <img> becomes
// UltraCanvasImageElement fed through a caller-supplied resource loader.
// The CSSLayout engine then does all measurement and layout natively —
// there is no separate HTML layout engine.
// Version: 1.11.0 - flex and grid containers on the CSSLayout flex / grid engines:
//                  BuildContentInto, BuildItemsInto, BuildItem, EstimateContentWidth
// Version: 1.10.0 - BuildOptions::linkTooltips (a link's href as a tooltip, or not)
// Version: 1.9.0 - merged with main's 1.3.0 (a list marker carried into the item's
//                  first block)
// Version: 1.8.0 - ApplyBoxStyle: width / height are the content's (CSS content-box)
//                  unless box-sizing: border-box or `borderBoxSizes`
// Version: 1.7.0 - ApplyBorders; collapsed table borders
// Version: 1.6.0 - the gap between images is a space measured in their font
// Version: 1.5.0 - images in a block without text share a wrapping line
// Version: 1.4.0 - background-repeat
// Version: 1.3.0 - background-position
// Version: 1.2.0 - viewport width for @media; background images; margin: auto
// Version: 1.1.0 - tables on the CSSLayout table engine; inline-block boxes
// Last Modified: 2026-10-08
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

    // The width, in CSS px, the document is shown at: @media (min-width /
    // max-width) queries are answered for it. 0 = a desktop mail pane (800).
    float viewportWidth = 0.f;

    // Resolves an <img src> or <link href> to raw bytes. For EPUB this reads
    // from the archive; return an empty vector when the resource is missing.
    // Also used to load <link rel="stylesheet"> targets.
    std::function<std::vector<uint8_t>(const std::string& href)> resourceLoader;

    // Invoked with the raw (unresolved) href of an <a> when the user clicks
    // its text. Labels carry per-byte-range link data, so a run with several
    // links activates exactly the one under the pointer.
    std::function<void(const std::string& href)> onLinkActivated;
    // The link under the pointer, as it moves onto one (its href) and off it
    // (""): text links and linked pictures alike.
    std::function<void(const std::string& href)> onLinkHovered;
    // Show a link's href as a tooltip while the pointer rests on it (text
    // links and linked pictures). An app that shows the address elsewhere -
    // a status line fed by onLinkHovered - turns it off.
    bool linkTooltips = true;
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
    // A list item's marker not yet placed, handed to the item's first block
    // child (<li><div>text</div></li>) so it starts that block's first line.
    std::string carriedMarker;

    // Per-inline-run state: the rendered plain text built alongside the Pango
    // markup (same bytes the text layout reports from hit testing) and the
    // link ranges found in it.
    std::string runPlain;
    std::vector<LabelTextLink> runLinks;
    // Images flowing in the current run, at U+FFFC placeholders of runPlain.
    std::vector<LabelInlineImage> runImages;

    // The width of a space in a style's font, in px - the gap between two
    // images a space apart. Measured on a small offscreen context made on
    // first use, cached per font.
    float SpaceWidth(const ComputedStyle& style);
    std::shared_ptr<IRenderContext> measureContext;
    std::unordered_map<std::string, float> spaceWidths;

    std::string MakeId(const std::string& hint);

    // All containers in the built tree scroll via the host's scroll view, so
    // this creates them with their own scrollbars disabled.
    std::shared_ptr<UltraCanvasContainer> MakeContainer(const std::string& hint);

    std::shared_ptr<UltraCanvasContainer> BuildBlock(Node& element);
    // A box's content by its layout mode: normal flow (BuildChildrenInto) or,
    // for display: flex / grid, its items (BuildItemsInto).
    void BuildContentInto(UltraCanvasContainer& parent, Node& element);
    void BuildChildrenInto(UltraCanvasContainer& parent, Node& element,
                           int listItemIndex = -1);
    // A flex or grid container: `parent` laid out by the CSSLayout flex or
    // grid engine, every child element an item of its own (blockified, as CSS
    // does: a <span> or <a> item is a box) and every run of text between them
    // an anonymous item; whitespace between items renders nothing.
    void BuildItemsInto(UltraCanvasContainer& parent, Node& element);
    // One flex / grid item built from an element, at its own size (no 100%
    // width) with its margins as real margins. Null when it shows nothing.
    std::shared_ptr<UltraCanvasUIElement> BuildItem(Node& element);
    // The content width of `element` estimated before layout, from the
    // viewport down through its ancestors' widths, margins, padding and
    // borders: what repeat(auto-fill, ...) is counted against.
    float EstimateContentWidth(const Node& element) const;
    // `blockNode` is the element whose content the run is: a text node whose
    // parent is some other element (an inline the builder looked through,
    // because it wraps a block) takes that element's formatting.
    std::shared_ptr<UltraCanvasLabel> BuildInlineRun(
        const std::vector<Node*>& run, const ComputedStyle& blockStyle,
        const std::string& markerPrefix, const Node* blockNode = nullptr);
    // Whether a block's inline content has text of its own (not only images
    // and whitespace): then its images flow in that text, as in a browser;
    // otherwise its images go on lines of their own - side by side on a
    // shared, wrapping line while only whitespace separates them.
    bool HasInlineText(const Node& element) const;

    // An image on a line of its own (a wrapping row the next images can
    // join), placed by the text-align it inherits.
    // `linkHref` makes it clickable (an image inside <a href>).
    std::shared_ptr<UltraCanvasUIElement> BuildImage(Node& element,
                                                     const std::string& linkHref = "");
    std::shared_ptr<UltraCanvasUIElement> BuildRule(Node& element);
    // A <table> (or display: table) on the CSSLayout table engine: one cell
    // element per <td>/<th> at its row / column / spans, columns shared by
    // every row. A table narrower than its line is placed by its align
    // attribute or the text-align it inherits, unless `inlineBox` (it then
    // sits in a line of inline content, which places it).
    std::shared_ptr<UltraCanvasContainer> BuildTable(Node& element, bool inlineBox = false);
    // An inline-block with a box of its own (background, border, padding,
    // width - a mail "button"), or an inline element holding a block or a
    // table: a shrink-to-fit box on the line, beside the text around it.
    std::shared_ptr<UltraCanvasUIElement> BuildInlineBox(Node& element);
    bool NeedsInlineBox(const Node& element) const;
    bool HasBlockDescendant(const Node& element) const;
    // Display-only render of a form control (input/textarea/button/select):
    // a styled box showing its value/label. Returns null for hidden inputs.
    std::shared_ptr<UltraCanvasContainer> BuildFormControl(Node& element);

    void AppendInlineMarkup(const Node& node, const ComputedStyle& runStyle,
                            bool preserveWhitespace, std::string& out);
    // Pango markup that turns `runStyle` text into `style` text (bold, size,
    // color, ...); `tag` adds <sub>/<sup>.
    static void StyleWrap(const ComputedStyle& style, const ComputedStyle& runStyle,
                          std::string& prefix, std::string& suffix,
                          const std::string& tag = std::string());

    // Maps the node's id (and <a name>) to the built element in `anchors`,
    // recursing into `node`'s subtree when deep is true.
    void RegisterAnchors(const Node& node,
                         const std::shared_ptr<UltraCanvasUIElement>& element,
                         bool deep = false);

    // Horizontal margins fold into padding unless `realMargins` (an inline
    // box, whose background must not reach into its margin). width / height
    // size the content, as CSS's content-box does, unless the style says
    // box-sizing: border-box or the caller sizes the box as a whole
    // (`borderBoxSizes`: tables and their cells, as browsers size them, and
    // images, which set up their own content box).
    void ApplyBoxStyle(UltraCanvasUIElement& target, const ComputedStyle& style,
                       bool fillWidth = true, bool realMargins = false,
                       bool borderBoxSizes = false);
    // The border sides and radius of a style (ApplyBoxStyle calls it; a
    // collapsed table's cells call it once their shared edges are settled).
    void ApplyBorders(UltraCanvasUIElement& target, const ComputedStyle& style);
    void ConfigureLabel(UltraCanvasLabel& label, const ComputedStyle& style,
                        bool noWrap = false);
    // background-image: the first url() layer that loads, drawn under the
    // box's content (an out-of-flow image element filling it), fitted by that
    // layer's background-size, placed by its background-position and tiled
    // by its background-repeat.
    void ApplyBackgroundImage(UltraCanvasContainer& box, const ComputedStyle& style);
    // A box narrower than its line (width / max-width) with margin-left and /
    // or margin-right auto: centred (or pushed right) in a full-width row.
    std::shared_ptr<UltraCanvasUIElement> PlaceByAutoMargins(
        std::shared_ptr<UltraCanvasUIElement> box, const ComputedStyle& style);

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
