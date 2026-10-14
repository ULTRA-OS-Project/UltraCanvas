// Apps/UltraPress/render/UltraPressRender.cpp
// The block renderer: the content model as UltraCanvas elements. The
// construction follows UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp -
// full-width bands whose centred column is Block-laid-out so wrapped text
// measures, wrapping Flex rows for columns and buttons - with the texts,
// colours and menus read from the site instead of tables in the source.
// Version: 0.7.0 - text bindings for editing in place
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressRender.h"

#include "HTMLReader/HTMLElementBuilder.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

using namespace UltraCanvas;

namespace UltraPress {

// ----- Colours -------------------------------------------------------------------------

bool ParseHexColor(const std::string& text, Color& out) {
    if (text.size() != 7 && text.size() != 9) return false;
    if (text[0] != '#') return false;
    unsigned values[4] = {0, 0, 0, 255};
    for (size_t i = 1; i + 1 < text.size(); i += 2) {
        unsigned value = 0;
        for (size_t j = i; j < i + 2; ++j) {
            const char c = text[j];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        values[(i - 1) / 2] = value;
    }
    out = Color(static_cast<uint8_t>(values[0]), static_cast<uint8_t>(values[1]),
                static_cast<uint8_t>(values[2]), static_cast<uint8_t>(values[3]));
    return true;
}

Palette PaletteFromTheme(const Theme& theme) {
    Palette p;
    p.accent = Color(169, 7, 7, 255);
    p.accentHover = Color(130, 5, 5, 255);
    p.ink = Color(32, 32, 32, 255);
    p.inkSoft = Color(90, 90, 98, 255);
    p.paper = Color(255, 255, 255, 255);
    p.paperSoft = Color(239, 239, 239, 255);
    p.rule = Color(220, 220, 224, 255);
    p.night = Color(13, 12, 34, 255);
    p.nightSoft = Color(24, 25, 63, 255);
    p.onDark = Color(208, 208, 220, 255);
    p.tile = Color(226, 228, 236, 255);
    if (!theme.fontFamily.empty()) p.font = theme.fontFamily;
    for (const auto& [name, value] : theme.colors) {
        Color color;
        if (!ParseHexColor(value, color)) continue;
        if (name == "accent") {
            p.accent = color;
            p.accentHover = Color(static_cast<uint8_t>(color.r * 3 / 4), static_cast<uint8_t>(color.g * 3 / 4),
                                  static_cast<uint8_t>(color.b * 3 / 4), color.a);
        } else if (name == "accentHover") p.accentHover = color;
        else if (name == "ink") p.ink = color;
        else if (name == "inkSoft") p.inkSoft = color;
        else if (name == "paper") p.paper = color;
        else if (name == "paperSoft") p.paperSoft = color;
        else if (name == "rule") p.rule = color;
        else if (name == "night") p.night = color;
        else if (name == "nightSoft") p.nightSoft = color;
        else if (name == "onDark") p.onDark = color;
        else if (name == "tile") p.tile = color;
    }
    return p;
}

int CountElements(const std::shared_ptr<UltraCanvasUIElement>& root) {
    if (!root) return 0;
    int count = 1;
    if (auto container = std::dynamic_pointer_cast<UltraCanvasContainer>(root)) {
        for (const auto& child : container->GetChildren()) count += CountElements(child);
    }
    return count;
}

// ----- The renderer -----------------------------------------------------------------------

PageRenderer::PageRenderer(const Site& siteRef, RenderOptions renderOptions, const BlockRegistry& blockRegistry)
    : site(siteRef), options(std::move(renderOptions)), registry(blockRegistry), palette(PaletteFromTheme(siteRef.theme)) {}

void PageRenderer::Activate(const std::string& href) const {
    if (href.empty()) return;
    if (options.onLink) {
        options.onLink(href);
        return;
    }
    if (href.find("://") != std::string::npos || href.rfind("mailto:", 0) == 0 || href.rfind("tel:", 0) == 0) OpenURL(href);
}

PageRenderer::Band PageRenderer::MakeBand(const std::string& id, const Color& background, float paddingV) const {
    Band band;
    band.outer = CreateContainer(id);
    band.outer->SetBackgroundColor(background);
    band.outer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    band.outer->SetPadding(paddingV, 32);
    band.outer->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    band.inner = CreateContainer(id + "Inner");
    band.inner->layout.SetDisplay(CSSLayout::DisplayType::Block);
    band.inner->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    CSSLayout::BoxConstraints limits;
    limits.maxWidth = CSSLayout::Dimension::Px(options.contentWidth);
    band.inner->boxConstraints = limits;
    band.inner->layoutItem.SetFlexShrink(1);
    band.outer->AddChild(band.inner);
    return band;
}

PageRenderer::Container PageRenderer::Row(const std::string& id, float gap, bool wrap) const {
    auto row = CreateContainer(id);
    row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    row->layout.SetFlexRow().SetFlexWrap(wrap ? CSSLayout::FlexWrap::Wrap : CSSLayout::FlexWrap::NoWrap)
        .SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    return row;
}

PageRenderer::Container PageRenderer::Column(const std::string& id, float minWidth) const {
    auto column = CreateContainer(id);
    column->layout.SetDisplay(CSSLayout::DisplayType::Block);
    column->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(minWidth));
    return column;
}

PageRenderer::Container PageRenderer::Card(const std::string& id, float minWidth, const Color& background, bool framed) const {
    auto card = CreateContainer(id);
    card->layout.SetDisplay(CSSLayout::DisplayType::Block);
    card->SetBackgroundColor(background);
    card->SetPadding(26);
    if (framed) card->SetBorders(1, palette.rule, 10);
    else card->SetBorderRadius(10);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(minWidth));
    return card;
}

PageRenderer::Container PageRenderer::Gap(float height) const {
    auto spacer = CreateContainer("");
    spacer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
    return spacer;
}

PageRenderer::Element PageRenderer::Text(const std::string& id, const std::string& text, float size, const Color& color,
                                         bool bold, bool centered) const {
    auto label = CreateLabel(id, text);
    label->SetFont(palette.font, size, bold ? FontWeight::Bold : FontWeight::Normal);
    label->SetTextColor(color);
    label->SetWrap(TextWrap::WrapWord);
    label->SetAlignment(centered ? TextAlignment::Center : TextAlignment::Left, VerticalAlignment::Top);
    label->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    return label;
}

PageRenderer::Element PageRenderer::Paragraphs(const std::string& id, const std::string& longText, float size,
                                               const Color& color, bool centered, RenderedPage& page,
                                               const TextLocation& where) {
    auto column = CreateContainer(id);
    column->layout.SetDisplay(CSSLayout::DisplayType::Block);
    column->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    int index = 0;
    for (const std::string& paragraph : SplitParagraphs(longText)) {
        if (index) column->AddChild(Gap(10));
        TextLocation one = where;
        one.paragraph = index;
        column->AddChild(Bound(id + "P" + std::to_string(index), paragraph, size, color, false, centered, page, one, true));
        ++index;
    }
    return column;
}

PageRenderer::Element PageRenderer::Bound(const std::string& id, const std::string& text, float size, const Color& color,
                                          bool bold, bool centered, RenderedPage& page, const TextLocation& where,
                                          bool multiline, const std::string& shown) {
    auto label = std::static_pointer_cast<UltraCanvasLabel>(Text(id, shown.empty() ? text : shown, size, color, bold, centered));
    if (!options.editable) return label;
    auto slot = CreateContainer(id + "Slot");
    slot->layout.SetDisplay(CSSLayout::DisplayType::Block);
    slot->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    slot->AddChild(label);
    TextBinding binding;
    binding.blockPath = blockPath;
    binding.where = where;
    binding.text = text;
    binding.multiline = multiline;
    binding.size = size;
    binding.slot = slot;
    binding.label = label;
    page.bindings.push_back(std::move(binding));
    const int index = static_cast<int>(page.bindings.size()) - 1;
    // The label holds the callback; it captures the editor's function and
    // the index, never the renderer (gone after Render) nor the page.
    label->onClick = [callback = options.onEditText, index]() {
        if (callback) callback(index);
    };
    return slot;
}

PageRenderer::Element PageRenderer::Kicker(const std::string& id, const std::string& text, RenderedPage& page,
                                           const TextLocation& where) {
    std::string upper = text;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return Bound(id, text, 13, palette.accent, true, true, page, where, false, upper);
}

std::shared_ptr<UltraCanvasButton> PageRenderer::Flat(const std::string& id, const std::string& text, float size,
                                                      const Color& color, const Color& hover) const {
    auto button = CreateButton(id, 0, 0, 0, 34, text);
    button->SetFont(palette.font, size);
    button->SetColors(Colors::Transparent, Color(hover.r, hover.g, hover.b, 28));
    button->SetTextColors(color, hover);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(6);
    button->SetPadding(0, 9);
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

std::shared_ptr<UltraCanvasButton> PageRenderer::Action(const std::string& id, const std::string& text,
                                                        const std::string& href, bool primary, bool onDark) const {
    auto button = CreateButton(id, 0, 0, 0, 44, text);
    button->SetFont(palette.font, 15, FontWeight::Bold);
    button->SetCornerRadius(6);
    button->SetPadding(0, 22);
    if (primary) {
        button->SetColors(palette.accent, palette.accentHover);
        button->SetTextColors(palette.paper);
        button->SetBorder(0, Colors::Transparent);
    } else if (onDark) {
        button->SetColors(Colors::Transparent, Color(255, 255, 255, 30));
        button->SetTextColors(palette.paper);
        button->SetBorder(1.5f, palette.paper);
    } else {
        button->SetColors(palette.paper, palette.paperSoft);
        button->SetTextColors(palette.accent);
        button->SetBorder(1.5f, palette.accent);
    }
    button->SetMouseCursor(UCMouseCursor::Hand);
    if (!href.empty()) button->onClick = [this, href]() { Activate(href); };
    return button;
}

PageRenderer::Element PageRenderer::Links(const std::string& id, const JSONValue& links, bool onDark, bool centered) const {
    auto row = Row(id, 14);
    row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center)
        .SetFlexJustifyContent(centered ? CSSLayout::JustifyContent::Center : CSSLayout::JustifyContent::Start);
    int index = 0;
    for (const JSONValue& link : links.GetElements()) {
        const std::string href = LinkHref(site, link);
        const std::string title = link["title"].GetString();
        if (href.empty() || title.empty()) continue;
        row->AddChild(Action(id + std::to_string(index++), title, href, link["primary"].GetBoolean(false), onDark));
    }
    return row;
}

PageRenderer::Element PageRenderer::Picture(const std::string& id, const std::string& path, const std::string& caption,
                                            float w, float h, RenderedPage& page) const {
    std::string file = path;
    if (!options.mediaRoot.empty() && path.find("://") == std::string::npos && !path.empty() && path[0] != '/') {
        file = options.mediaRoot + "/" + path;
    }
    std::error_code ec;
    if (!path.empty() && std::filesystem::exists(PathFromUtf8(file), ec)) {
        auto image = CreateImageFromFile(id, 0, 0, w, h, file);
        image->SetFitMode(ImageFitMode::Contain);
        image->SetBorderRadius(10);
        image->layoutItem.SetFlexShrink(0);
        return image;
    }
    if (!path.empty()) page.warnings.push_back("picture not found: " + path);
    auto tile = CreateContainer(id, 0, 0, w, h);
    tile->SetBackgroundColor(palette.tile);
    tile->SetBorderRadius(10);
    tile->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    tile->layoutItem.SetFlexShrink(0);
    auto label = CreateLabel(id + "Caption", caption.empty() ? path : caption);
    label->SetFont(palette.font, 15, FontWeight::Bold);
    label->SetTextColor(palette.inkSoft);
    tile->AddChild(label);
    return tile;
}

void PageRenderer::AddMenu(const Menu* menu, const std::string& id, Container into, bool onDark, RenderedPage& page) const {
    if (!menu) return;
    for (size_t i = 0; i < menu->items.size(); ++i) {
        const MenuItem& item = menu->items[i];
        auto button = std::static_pointer_cast<UltraCanvasButton>(
            Flat(id + std::to_string(i), item.title, 14, onDark ? palette.onDark : palette.ink, palette.accent));
        button->layoutItem.SetFlexShrink(0);
        if (!item.children.empty()) {
            auto popup = std::make_shared<UltraCanvasMenu>(id + "Menu" + std::to_string(i), 0, 0, 240, 0);
            popup->SetMenuType(MenuType::PopupMenu);
            for (const MenuItem& child : item.children) {
                std::string href = child.url;
                if (href.empty()) {
                    if (const UltraPress::Page* target = site.FindPage(child.page)) href = PagePath(*target);
                }
                popup->AddItem(MenuItemData::Action(child.title, [this, href]() { Activate(href); }));
            }
            page.menus.push_back(popup);
            // The button holds the callback, so it is captured raw; the popup is
            // kept alive by the page and the capture.
            button->onClick = [popup, anchor = button.get()]() {
                if (auto* window = anchor->GetWindow()) {
                    const Rect2Df b = anchor->GetBoundsInWindow();
                    popup->OpenMenuAnchored(Rect2Di(static_cast<int>(b.x), static_cast<int>(b.y),
                                                    static_cast<int>(b.width), static_cast<int>(b.height)),
                                            *window, PopupElementSettings(), 4);
                }
            };
        } else {
            std::string href = item.url;
            if (href.empty()) {
                if (const UltraPress::Page* target = site.FindPage(item.page)) href = PagePath(*target);
            }
            button->onClick = [this, href]() { Activate(href); };
        }
        into->AddChild(button);
    }
}

PageRenderer::Element PageRenderer::RenderBlock(const Block& block, RenderedPage& page, const std::string& id) {
    const std::string& type = block.type;
    if (!registry.Find(type)) {
        page.warnings.push_back("unknown block type: " + type);
        return nullptr;
    }
    const bool onDark = type == "hero" || type == "cta";
    const Color& ink = onDark ? palette.paper : palette.ink;
    const Color& inkSoft = onDark ? palette.onDark : palette.inkSoft;

    if (type == "heading") {
        const int level = static_cast<int>(block.Number("level", 2));
        return Bound(id, block.Text("text"), level >= 4 ? 20.f : level == 3 ? 26.f : 34.f, palette.ink, true, false, page, {"text"}, false);
    }
    if (type == "text") {
        auto column = Column(id);
        if (!block.Text("heading").empty()) {
            column->AddChild(Bound(id + "Heading", block.Text("heading"), 30, palette.ink, true, false, page, {"heading"}, false));
            column->AddChild(Gap(14));
        }
        column->AddChild(Paragraphs(id + "Text", block.Text("text"), 16, palette.inkSoft, false, page, {"text"}));
        return column;
    }
    if (type == "richtext" || type == "html") {
        HTML::BuildOptions buildOptions;
        buildOptions.viewportWidth = options.contentWidth;
        buildOptions.onLinkActivated = [this](const std::string& href) { Activate(href); };
        HTML::ElementBuilder builder;
        HTML::BuildResult built = builder.Build(block.Text("html"), buildOptions);
        for (const std::string& warning : built.warnings) page.warnings.push_back(type + ": " + warning);
        if (!built.root) return Column(id);
        built.root->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
        return built.root;
    }
    if (type == "hero") {
        auto column = Column(id);
        if (!block.Text("kicker").empty()) {
            column->AddChild(Kicker(id + "Kicker", block.Text("kicker"), page, {"kicker"}));
            column->AddChild(Gap(16));
        }
        column->AddChild(Bound(id + "Title", block.Text("title"), 40, ink, true, true, page, {"title"}, false));
        if (!block.Text("text").empty()) {
            column->AddChild(Gap(20));
            auto lead = Paragraphs(id + "Lead", block.Text("text"), 19, inkSoft, true, page, {"text"});
            lead->SetPadding(0, 90);
            lead->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
            column->AddChild(lead);
        }
        if (!block.Text("picture").empty()) {
            column->AddChild(Gap(24));
            auto row = Row(id + "PictureRow", 0);
            row->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
            row->AddChild(Picture(id + "Picture", block.Text("picture"), block.Text("title"), 640, 360, page));
            column->AddChild(row);
        }
        if (block.Items("buttons").GetSize() > 0) {
            column->AddChild(Gap(32));
            column->AddChild(Links(id + "Buttons", block.Items("buttons"), true, true));
        }
        return column;
    }
    if (type == "picture") {
        auto column = Column(id);
        auto row = Row(id + "Row", 0);
        row->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
        row->AddChild(Picture(id + "Image", block.Text("src"), block.Text("alt"), 720, 405, page));
        column->AddChild(row);
        if (!block.Text("caption").empty()) {
            column->AddChild(Gap(8));
            column->AddChild(Bound(id + "Caption", block.Text("caption"), 14, palette.inkSoft, false, true, page, {"caption"}, false));
        }
        return column;
    }
    if (type == "columns") {
        auto column = Column(id);
        if (!block.Text("heading").empty()) {
            column->AddChild(Bound(id + "Heading", block.Text("heading"), 30, palette.ink, true, true, page, {"heading"}, false));
            column->AddChild(Gap(24));
        }
        auto row = Row(id + "Row", 24);
        const std::string parentPath = blockPath;
        for (size_t i = 0; i < block.children.size(); ++i) {
            auto cell = Column(id + "Cell" + std::to_string(i), 260);
            blockPath = parentPath + ".children[" + std::to_string(i) + "]";
            if (auto child = RenderBlock(block.children[i], page, id + "C" + std::to_string(i))) cell->AddChild(child);
            row->AddChild(cell);
        }
        blockPath = parentPath;
        column->AddChild(row);
        return column;
    }
    if (type == "card") {
        auto card = Card(id, 260, palette.paper, true);
        if (!block.Text("picture").empty()) {
            card->AddChild(Picture(id + "Picture", block.Text("picture"), block.Text("title"), 300, 170, page));
            card->AddChild(Gap(14));
        }
        card->AddChild(Bound(id + "Title", block.Text("title"), 20, palette.ink, true, false, page, {"title"}, false));
        if (!block.Text("text").empty()) {
            card->AddChild(Gap(8));
            card->AddChild(Paragraphs(id + "Text", block.Text("text"), 15, palette.inkSoft, false, page, {"text"}));
        }
        const std::string href = LinkHref(site, block.props["link"]);
        if (!href.empty()) {
            card->AddChild(Gap(14));
            auto link = Flat(id + "Link", block.Text("linkTitle", "Read more"), 15, palette.accent, palette.accentHover);
            link->SetPadding(0, 0);
            std::static_pointer_cast<UltraCanvasButton>(link)->SetTextAlign(TextAlignment::Left);
            link->onClick = [this, href]() { Activate(href); };
            card->AddChild(link);
        }
        return card;
    }
    if (type == "features") {
        auto column = Column(id);
        if (!block.Text("heading").empty()) {
            column->AddChild(Bound(id + "Heading", block.Text("heading"), 30, palette.ink, true, false, page, {"heading"}, false));
            column->AddChild(Gap(16));
        }
        int index = 0;
        for (const JSONValue& item : block.Items("items").GetElements()) {
            auto row = Row(id + "Item" + std::to_string(index), 10, false);
            auto bullet = Text(id + "Bullet" + std::to_string(index), "\xE2\x9C\x93", 16, palette.accent, true);
            bullet->SetElementSize(CSSLayout::Dimension::Px(22), CSSLayout::Dimension::Auto());
            bullet->layoutItem.SetFlexShrink(0);
            row->AddChild(bullet);
            auto text = Bound(id + "Text" + std::to_string(index), item.GetString(), 16, palette.ink, false, false, page,
                              TextLocation{"items", index, "", -1}, false);
            text->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
            row->AddChild(text);
            column->AddChild(row);
            column->AddChild(Gap(8));
            ++index;
        }
        return column;
    }
    if (type == "cta") {
        auto column = Column(id);
        column->AddChild(Bound(id + "Title", block.Text("title"), 32, ink, true, true, page, {"title"}, false));
        if (!block.Text("text").empty()) {
            column->AddChild(Gap(14));
            column->AddChild(Paragraphs(id + "Text", block.Text("text"), 17, inkSoft, true, page, {"text"}));
        }
        if (block.Items("buttons").GetSize() > 0) {
            column->AddChild(Gap(26));
            column->AddChild(Links(id + "Buttons", block.Items("buttons"), true, true));
        }
        return column;
    }
    if (type == "faq") {
        auto column = Column(id);
        if (!block.Text("heading").empty()) {
            column->AddChild(Bound(id + "Heading", block.Text("heading"), 34, palette.ink, true, true, page, {"heading"}, false));
            column->AddChild(Gap(28));
        }
        int index = 0;
        for (const JSONValue& item : block.Items("items").GetElements()) {
            const std::string q = item["q"].GetString();
            if (q.empty()) continue;
            const std::string entryId = id + "Entry" + std::to_string(index);
            auto entry = CreateContainer(entryId);
            entry->layout.SetDisplay(CSSLayout::DisplayType::Block);
            entry->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
            entry->SetBackgroundColor(palette.paper);
            entry->SetBorders(1, palette.rule, 10);
            entry->SetPadding(6, 18);
            entry->SetMargin(0, 0, 12, 0);
            auto question = std::static_pointer_cast<UltraCanvasButton>(
                Flat(entryId + "Question", (index == 0 ? "-  " : "+  ") + q, 18, palette.ink, palette.accent));
            question->SetFont(palette.font, 18, FontWeight::Bold);
            question->SetTextAlign(TextAlignment::Left);
            question->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(44));
            entry->AddChild(question);
            auto answer = Paragraphs(entryId + "Answer", item["a"].GetString(), 15, palette.inkSoft, false, page,
                                     TextLocation{"items", index, "a", -1});
            answer->SetPadding(6, 10, 14, 10);
            answer->box.boxSizing = CSSLayout::BoxSizing::BorderBox;
            answer->SetVisible(index == 0);
            entry->AddChild(answer);
            // The question holds the callback, so it is captured raw.
            question->onClick = [answer, question = question.get(), q]() {
                const bool open = !answer->IsVisible();
                answer->SetVisible(open);
                question->SetText((open ? "-  " : "+  ") + q);
            };
            column->AddChild(entry);
            ++index;
        }
        return column;
    }
    if (type == "quote") {
        auto card = Card(id, 0, palette.paperSoft, false);
        card->AddChild(Paragraphs(id + "Text", block.Text("text"), 22, palette.ink, false, page, {"text"}));
        if (!block.Text("cite").empty()) {
            card->AddChild(Gap(10));
            card->AddChild(Bound(id + "Cite", block.Text("cite"), 15, palette.inkSoft, true, false, page, {"cite"}, false,
                                 "\xE2\x80\x94 " + block.Text("cite")));
        }
        return card;
    }
    if (type == "buttons") {
        return Links(id, block.Items("items"), false, false);
    }
    if (type == "form") {
        auto card = Card(id, 420, palette.paperSoft, false);
        if (!block.Text("heading").empty()) {
            card->AddChild(Bound(id + "Heading", block.Text("heading"), 26, palette.ink, true, false, page, {"heading"}, false));
            card->AddChild(Gap(10));
        }
        if (!block.Text("text").empty()) {
            card->AddChild(Paragraphs(id + "Text", block.Text("text"), 15, palette.inkSoft, false, page, {"text"}));
            card->AddChild(Gap(14));
        }
        struct Field { std::string name; std::function<std::string()> value; };
        auto fields = std::make_shared<std::vector<Field>>();
        int index = 0;
        for (const JSONValue& field : block.Items("fields").GetElements()) {
            const std::string name = field["name"].GetString();
            if (name.empty()) continue;
            const std::string fieldId = id + "Field" + std::to_string(index++);
            const std::string label = field["label"].GetString(name);
            const std::string fieldType = field["type"].GetString("text");
            if (fieldType == "checkbox") {
                auto box = UltraCanvasCheckbox::CreateCheckbox(fieldId, 0, 0, 0, 28, label);
                box->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(28));
                card->AddChild(box);
                fields->push_back({name, [box = box.get()]() { return std::string(box->IsChecked() ? "yes" : "no"); }});
            } else {
                card->AddChild(Text(fieldId + "Caption", label, 14, palette.ink, true));
                card->AddChild(Gap(6));
                if (fieldType == "textarea") {
                    auto area = std::make_shared<UltraCanvasTextArea>(fieldId, 0, 0, 0, 120);
                    area->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(120));
                    area->SetPlaceholder(label);
                    area->SetFontSize(15);
                    card->AddChild(area);
                    fields->push_back({name, [area = area.get()]() { return area->GetText(); }});
                } else {
                    auto input = CreateTextInput(fieldId, 0, 0, 0, 40);
                    input->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(40));
                    input->SetPlaceholder(label);
                    input->SetFontSize(15);
                    card->AddChild(input);
                    fields->push_back({name, [input = input.get()]() { return input->GetText(); }});
                }
            }
            card->AddChild(Gap(12));
        }
        auto submit = Action(id + "Submit", block.Text("submit", "Send"), "", true, false);
        const std::string formId = block.Text("id");
        // The button holds the callback; the fields' getters hold raw
        // pointers to inputs the same card owns, which outlives every click.
        submit->onClick = [this, formId, fields]() {
            std::vector<std::pair<std::string, std::string>> values;
            for (const Field& field : *fields) values.emplace_back(field.name, field.value());
            if (options.onFormSubmit) options.onFormSubmit(formId, values);
        };
        card->AddChild(submit);
        return card;
    }
    if (type == "newsletter") {
        auto card = Card(id, 420, palette.paperSoft, false);
        if (!block.Text("heading").empty()) {
            card->AddChild(Bound(id + "Heading", block.Text("heading"), 26, palette.ink, true, false, page, {"heading"}, false));
            card->AddChild(Gap(10));
        }
        if (!block.Text("text").empty()) {
            card->AddChild(Paragraphs(id + "Text", block.Text("text"), 15, palette.inkSoft, false, page, {"text"}));
            card->AddChild(Gap(14));
        }
        auto email = CreateTextInput(id + "Email", 0, 0, 0, 42);
        email->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(42));
        email->SetPlaceholder("E-mail");
        email->SetFontSize(15);
        card->AddChild(email);
        card->AddChild(Gap(12));
        auto submit = Action(id + "Submit", "Subscribe", "", true, false);
        const std::string list = block.Text("list", "default");
        submit->onClick = [this, list, email = email.get()]() {
            if (options.onNewsletterSubscribe) options.onNewsletterSubscribe(list, email->GetText());
        };
        card->AddChild(submit);
        return card;
    }
    if (type == "downloads") {
        auto column = Column(id);
        if (!block.Text("heading").empty()) {
            column->AddChild(Bound(id + "Heading", block.Text("heading"), 30, palette.ink, true, false, page, {"heading"}, false));
            column->AddChild(Gap(16));
        }
        int index = 0;
        for (const JSONValue& item : block.Items("items").GetElements()) {
            const std::string file = item["file"].GetString();
            if (file.empty()) continue;
            const std::string itemId = id + "Item" + std::to_string(index++);
            auto entry = Card(itemId, 0, palette.paper, true);
            entry->SetPadding(14, 18);
            auto link = Flat(itemId + "Link", item["title"].GetString(file), 17, palette.accent, palette.accentHover);
            link->SetPadding(0, 0);
            std::static_pointer_cast<UltraCanvasButton>(link)->SetTextAlign(TextAlignment::Left);
            const std::string href = file.find("://") != std::string::npos ? file : "/" + file;
            link->onClick = [this, href]() { Activate(href); };
            entry->AddChild(link);
            std::string details;
            if (!item["version"].GetString().empty()) details += "version " + item["version"].GetString();
            if (!item["size"].GetString().empty()) details += (details.empty() ? "" : ", ") + item["size"].GetString();
            if (!details.empty()) entry->AddChild(Text(itemId + "Details", details, 14, palette.inkSoft));
            if (!item["description"].GetString().empty()) {
                entry->AddChild(Gap(6));
                entry->AddChild(Bound(itemId + "Description", item["description"].GetString(), 15, palette.ink, false, false, page,
                                      TextLocation{"items", index - 1, "description", -1}, false));
            }
            column->AddChild(entry);
            column->AddChild(Gap(10));
        }
        return column;
    }
    if (type == "spacer") {
        return Gap(static_cast<float>(block.Number("height", 32)));
    }
    return nullptr;
}

RenderedPage PageRenderer::Render(const UltraPress::Page& page) {
    RenderedPage rendered;
    auto root = CreateContainer("Page" + page.slug);
    root->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    root->SetBackgroundColor(palette.paper);
    root->layout.SetDisplay(CSSLayout::DisplayType::Block);

    // Header: the site's name and the main menu, on the night colour.
    Band header = MakeBand("Header", palette.night, 10);
    header.inner->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(14);
    auto brand = std::static_pointer_cast<UltraCanvasButton>(Flat("Brand", site.name, 17, palette.paper, palette.accent));
    brand->SetFont(palette.font, 17, FontWeight::Bold);
    brand->layoutItem.SetFlexShrink(0);
    brand->onClick = [this]() { Activate("/"); };
    header.inner->AddChild(brand);
    auto nav = Row("Nav", 2);
    nav->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    nav->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    nav->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    AddMenu(site.FindMenu("main"), "Nav", nav, true, rendered);
    header.inner->AddChild(nav);
    root->AddChild(header.outer);

    // The blocks, each in its band: heroes and calls to action on the dark
    // colour, FAQs and quotes on the soft paper, the rest on paper.
    bool soft = false;
    for (size_t i = 0; i < page.blocks.size(); ++i) {
        const Block& block = page.blocks[i];
        const std::string id = "B" + std::to_string(i);
        blockPath = "blocks[" + std::to_string(i) + "]";
        const bool dark = block.type == "hero" || block.type == "cta";
        Color background = palette.paper;
        float padding = 56;
        if (block.type == "hero") { background = palette.nightSoft; padding = 84; }
        else if (block.type == "cta") { background = palette.night; padding = 64; }
        else if (block.type == "faq" || block.type == "quote" || block.type == "newsletter") { background = palette.paperSoft; }
        else if (block.type == "spacer") { padding = 0; }
        else { background = soft ? palette.paperSoft : palette.paper; }
        if (!dark && block.type != "spacer") soft = !soft;
        Band band = MakeBand(id + "Band", background, padding);
        if (auto element = RenderBlock(block, rendered, id)) band.inner->AddChild(element);
        root->AddChild(band.outer);
    }

    // Footer: the footer menu and the owner.
    Band footer = MakeBand("Footer", palette.night, 40);
    auto footerRow = Row("FooterRow", 24);
    footerRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    AddMenu(site.FindMenu("footer"), "FooterNav", footerRow, true, rendered);
    footer.inner->AddChild(footerRow);
    footer.inner->AddChild(Gap(16));
    footer.inner->AddChild(Text("FooterOwner", site.owner.empty() ? site.name : site.owner, 14, palette.onDark));
    root->AddChild(footer.outer);

    rendered.root = root;
    rendered.elementCount = CountElements(root);
    return rendered;
}

} // namespace UltraPress
