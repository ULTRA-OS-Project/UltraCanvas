// Apps/UltraPress/render/UltraPressRender.h
// The block renderer on real UltraCanvas elements: a page of the content
// model becomes a tree of containers, labels, buttons, pictures and inputs
// laid out by the CSS layout engine - the ultraos.eu front page demo's
// construction, driven by data. It is the editor's live preview on the
// desktop, and under Emscripten the published page itself; the two share
// the Cairo + Pango pipeline, so the preview is the page
// (Docs/Research/UltraPressDesignProposal.md §5.2).
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressBlocks.h"
#include "UltraPressModel.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasUIElement.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraPress {

struct RenderOptions {
    // The site folder: a block's picture path is relative to it.
    std::string mediaRoot;
    float contentWidth = 1140.f;
    // A link was activated: a site path ("/about/", from a page link) or an
    // absolute URL. Without a callback, URLs open in the system browser and
    // site paths do nothing.
    std::function<void(const std::string& href)> onLink;
    // A form block was submitted: its id and the fields as (name, value).
    std::function<void(const std::string& formId, const std::vector<std::pair<std::string, std::string>>& fields)> onFormSubmit;
    // A newsletter block was submitted: the list's name and the address.
    std::function<void(const std::string& list, const std::string& email)> onNewsletterSubscribe;
};

// The theme's colours as the renderer uses them, with the ultraos.eu
// defaults for a theme that names fewer.
struct Palette {
    UltraCanvas::Color accent, accentHover, ink, inkSoft, paper, paperSoft, rule, night, nightSoft, onDark, tile;
    std::string font = "Ubuntu";
};
Palette PaletteFromTheme(const Theme& theme);
// "#rrggbb" or "#rrggbbaa" to a colour; false when it is neither.
bool ParseHexColor(const std::string& text, UltraCanvas::Color& out);

struct RenderedPage {
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root;      // Block layout, 100% wide: header, blocks, footer
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasMenu>> menus;   // the header's drop-downs, alive with the page
    std::vector<std::string> warnings;                              // an unknown block, a missing picture
    int elementCount = 0;
};

class PageRenderer {
public:
    explicit PageRenderer(const Site& site, RenderOptions options = {},
                          const BlockRegistry& registry = BlockRegistry::BuiltIn());

    // The whole page: the site's header with the main menu, the blocks as
    // bands, the footer with its menu.
    RenderedPage Render(const Page& page);
    // One block on its own, as the page would hold it (for tests and the editor).
    std::shared_ptr<UltraCanvas::UltraCanvasUIElement> RenderBlock(const Block& block, RenderedPage& page,
                                                                   const std::string& id);
    const Palette& Colors() const { return palette; }

private:
    using Container = std::shared_ptr<UltraCanvas::UltraCanvasContainer>;
    using Element = std::shared_ptr<UltraCanvas::UltraCanvasUIElement>;
    struct Band { Container outer, inner; };

    Band MakeBand(const std::string& id, const UltraCanvas::Color& background, float paddingV) const;
    Container Row(const std::string& id, float gap, bool wrap = true) const;
    Container Column(const std::string& id, float minWidth = 0) const;
    Container Card(const std::string& id, float minWidth, const UltraCanvas::Color& background, bool framed) const;
    Container Gap(float height) const;
    Element Text(const std::string& id, const std::string& text, float size, const UltraCanvas::Color& color,
                 bool bold = false, bool centered = false) const;
    Element Paragraphs(const std::string& id, const std::string& longText, float size, const UltraCanvas::Color& color,
                       bool centered = false) const;
    Element Kicker(const std::string& id, const std::string& text, bool centered = true) const;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> Flat(const std::string& id, const std::string& text, float size,
                                                         const UltraCanvas::Color& color, const UltraCanvas::Color& hover) const;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> Action(const std::string& id, const std::string& text,
                                                           const std::string& href, bool primary, bool onDark) const;
    Element Links(const std::string& id, const UltraCanvas::JSONValue& links, bool onDark, bool centered) const;
    Element Picture(const std::string& id, const std::string& path, const std::string& caption, float w, float h,
                    RenderedPage& page) const;
    void Activate(const std::string& href) const;
    void AddMenu(const Menu* menu, const std::string& id, Container into, bool onDark, RenderedPage& page) const;

    const Site& site;
    RenderOptions options;
    const BlockRegistry& registry;
    Palette palette;
};

// How many elements a tree holds, root included.
int CountElements(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& root);

} // namespace UltraPress
