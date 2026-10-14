// Apps/UltraPress/ui/UltraPressWindow.cpp
// The editor window: lists, preview and the property pane from the registry.
// Version: 0.3.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressWindow.h"

#include "UltraPressBlocks.h"
#include "UltraPressPublish.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <cstdlib>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const Color kNight(13, 12, 34, 255);
const Color kPaper(255, 255, 255, 255);
const Color kPaperSoft(243, 243, 246, 255);
const Color kOnDark(208, 208, 220, 255);
const Color kInk(32, 32, 32, 255);
const Color kInkSoft(90, 90, 98, 255);
const Color kAccent(169, 7, 7, 255);
const Color kRule(220, 220, 224, 255);
const char* kFont = "Ubuntu";

std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id, const std::string& text, const Color& color, float size = 12,
                                          bool bold = true) {
    auto label = CreateLabel(id, text);
    label->SetFont(kFont, size, bold ? FontWeight::Bold : FontWeight::Normal);
    label->SetTextColor(color);
    label->SetWrap(TextWrap::WrapWord);
    label->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    return label;
}

std::shared_ptr<UltraCanvasButton> SideButton(const std::string& id, const std::string& text, bool active) {
    auto button = CreateButton(id, 0, 0, 0, 30, text);
    button->SetFont(kFont, 14, active ? FontWeight::Bold : FontWeight::Normal);
    button->SetColors(active ? Color(255, 255, 255, 36) : Colors::Transparent, Color(255, 255, 255, 24));
    button->SetTextColors(active ? kPaper : kOnDark, kPaper);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(6);
    button->SetTextAlign(TextAlignment::Left);
    button->SetPadding(0, 8);
    button->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(30));
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

std::shared_ptr<UltraCanvasButton> SmallButton(const std::string& id, const std::string& text) {
    auto button = CreateButton(id, 0, 0, 28, 26, text);
    button->SetFont(kFont, 13);
    button->SetColors(Colors::Transparent, Color(255, 255, 255, 30));
    button->SetTextColors(kOnDark, kPaper);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(4);
    button->SetPadding(0, 4);
    button->SetElementSize(CSSLayout::Dimension::Px(28), CSSLayout::Dimension::Px(26));
    button->layoutItem.SetFlexShrink(0);
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

std::shared_ptr<UltraCanvasContainer> Gap(float height) {
    auto spacer = CreateContainer("");
    spacer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
    return spacer;
}

// A default value for a property of a new block, so the page shows something.
JSONValue DefaultFor(const BlockType& type, const BlockProperty& property) {
    switch (property.kind) {
        case PropertyKind::Text:
            return property.name == "title" || property.name == "text" || property.name == "heading"
                       ? JSONValue(type.title) : JSONValue("");
        case PropertyKind::LongText: return JSONValue("Text of the " + type.title + " block.");
        case PropertyKind::Html: return JSONValue("<p>Text of the " + type.title + " block.</p>");
        case PropertyKind::Url:
        case PropertyKind::Image: return JSONValue("");
        case PropertyKind::Boolean: return JSONValue(false);
        case PropertyKind::Number: return JSONValue(property.name == "level" ? 2 : 32);
        case PropertyKind::Link: return JSONValue::MakeObject().Set("page", "home");
        case PropertyKind::Links: return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "Learn more").Set("page", "home").Set("primary", true));
        case PropertyKind::Strings: return JSONValue::MakeArray().Append("The first point").Append("The second point");
        case PropertyKind::Items:
            if (type.name == "faq") return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("q", "A question?").Set("a", "The answer."));
            if (type.name == "form") return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("name", "email").Set("label", "E-mail").Set("type", "email").Set("required", true));
            if (type.name == "downloads") return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "A file").Set("file", "media/file.zip"));
            return JSONValue::MakeArray();
    }
    return JSONValue();
}

} // namespace

EditorWindow::EditorWindow(Site siteModel, std::string siteDirectory)
    : site(std::move(siteModel)), siteDir(std::move(siteDirectory)) {}

Page* EditorWindow::CurrentPageMutable() {
    for (Page& page : site.pages) {
        if (page.slug == current) return &page;
    }
    return nullptr;
}

void EditorWindow::Later(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
    else task();
}

void EditorWindow::SetStatus(const std::string& text) {
    if (status) status->SetText(text);
}

void EditorWindow::Build(const std::shared_ptr<UltraCanvasWindow>& window) {
    auto frame = CreateContainer("Frame");
    frame->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    frame->layout.SetFlexRow();

    side = CreateScrollableContainer("Side", 0, 0, 0, 0, true, false);
    side->SetBackgroundColor(kNight);
    side->SetElementSize(CSSLayout::Dimension::Px(270), CSSLayout::Dimension::Pct(100));
    side->layout.SetDisplay(CSSLayout::DisplayType::Block);
    side->SetPadding(16);
    side->layoutItem.SetFlexShrink(0);
    frame->AddChild(side);

    pageArea = CreateScrollableContainer("PageArea", 0, 0, 0, 0, true, false);
    pageArea->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Pct(100));
    pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    {
        ContainerStyle style = pageArea->GetContainerStyle();
        style.autoShowHorizontalScrollbar = false;
        pageArea->SetContainerStyle(style);
    }
    pageArea->SetBackgroundColor(kPaper);
    pageArea->layout.SetDisplay(CSSLayout::DisplayType::Block);
    frame->AddChild(pageArea);

    properties = CreateScrollableContainer("Properties", 0, 0, 0, 0, true, false);
    properties->SetBackgroundColor(kPaperSoft);
    properties->SetElementSize(CSSLayout::Dimension::Px(340), CSSLayout::Dimension::Pct(100));
    properties->layout.SetDisplay(CSSLayout::DisplayType::Block);
    properties->SetPadding(16);
    properties->layoutItem.SetFlexShrink(0);
    frame->AddChild(properties);

    window->AddChild(frame);
    const std::string first = site.FindPage("home") ? "home" : site.pages.empty() ? "" : site.pages.front().slug;
    ShowPage(first);
}

void EditorWindow::ShowPage(const std::string& slug) {
    current = slug;
    selected = -1;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
}

void EditorWindow::SelectBlock(int index) {
    const Page* page = CurrentPageMutable();
    selected = page && index >= 0 && index < static_cast<int>(page->blocks.size()) ? index : -1;
    RebuildSide();
    RebuildProperties();
}

void EditorWindow::AddBlock(const std::string& type) {
    Page* page = CurrentPageMutable();
    const BlockType* blockType = BlockRegistry::BuiltIn().Find(type);
    if (!page || !blockType) return;
    Block block{type};
    for (const BlockProperty& property : blockType->properties) {
        if (property.required || property.kind == PropertyKind::LongText || property.name == "heading" || property.name == "title") {
            block.Set(property.name, DefaultFor(*blockType, property));
        }
    }
    const int at = selected >= 0 ? selected + 1 : static_cast<int>(page->blocks.size());
    page->blocks.insert(page->blocks.begin() + at, block);
    dirty = true;
    selected = at;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
    SetStatus("Added " + blockType->title);
}

void EditorWindow::MoveBlock(int index, int delta) {
    Page* page = CurrentPageMutable();
    if (!page) return;
    const int target = index + delta;
    if (index < 0 || index >= static_cast<int>(page->blocks.size()) || target < 0 || target >= static_cast<int>(page->blocks.size())) return;
    std::swap(page->blocks[index], page->blocks[target]);
    selected = target;
    dirty = true;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
}

void EditorWindow::RemoveBlock(int index) {
    Page* page = CurrentPageMutable();
    if (!page || index < 0 || index >= static_cast<int>(page->blocks.size())) return;
    page->blocks.erase(page->blocks.begin() + index);
    if (selected >= static_cast<int>(page->blocks.size())) selected = static_cast<int>(page->blocks.size()) - 1;
    dirty = true;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
}

bool EditorWindow::Save() {
    std::vector<std::string> errors;
    const bool ok = SaveSite(siteDir, site, errors);
    for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
    if (ok) dirty = false;
    SetStatus(ok ? "Saved" : "Not saved: " + (errors.empty() ? std::string("unknown error") : errors.front()));
    return ok;
}

void EditorWindow::RenderPreview() {
    if (!pageArea) return;
    pageArea->ClearChildren();
    const Page* page = CurrentPageMutable();
    if (!page) return;
    RenderOptions options;
    options.mediaRoot = siteDir;
    options.onLink = [this](const std::string& href) {
        if (!href.empty() && href[0] == '/') {
            const std::string target = href == "/" ? "home" : href.substr(1, href.size() - 2);
            Later([this, target]() { ShowPage(target); });
        } else {
            OpenURL(href);
        }
    };
    options.onFormSubmit = [this](const std::string& id, const std::vector<std::pair<std::string, std::string>>& fields) {
        SetStatus("Form " + id + ": " + std::to_string(fields.size()) + " fields (preview; no server tier yet)");
    };
    options.onNewsletterSubscribe = [this](const std::string& list, const std::string&) {
        SetStatus("Newsletter " + list + " (preview; no server tier yet)");
    };
    PageRenderer renderer(site, options);
    shown = renderer.Render(*page);
    pageArea->AddChild(shown.root);
}

void EditorWindow::RebuildSide() {
    if (!side) return;
    side->ClearChildren();
    auto title = Caption("SideTitle", site.name, kPaper, 18);
    side->AddChild(title);
    status = Caption("SideStatus", dirty ? "Unsaved changes" : "", kOnDark, 12, false);
    side->AddChild(status);
    side->AddChild(Gap(10));
    auto save = CreateButton("Save", 0, 0, 0, 32, "Save site");
    save->SetFont(kFont, 14, FontWeight::Bold);
    save->SetColors(kAccent, Color(130, 5, 5, 255));
    save->SetTextColors(kPaper);
    save->SetBorder(0, Colors::Transparent);
    save->SetCornerRadius(6);
    save->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
    save->onClick = [this]() { Save(); };
    side->AddChild(save);

    side->AddChild(Gap(18));
    side->AddChild(Caption("PagesTitle", "PAGES", kAccent));
    side->AddChild(Gap(6));
    for (const Page* page : SortedPages(site, PageOrder::Stored)) {
        auto button = SideButton("Page" + page->slug, (page->pinned ? "\xE2\x98\x85 " : "") + page->title +
                                                           (page->IsPublished() ? "" : " (" + page->status + ")"),
                                 page->slug == current);
        const std::string slug = page->slug;
        button->onClick = [this, slug]() { Later([this, slug]() { ShowPage(slug); }); };
        side->AddChild(button);
    }

    const Page* page = CurrentPageMutable();
    if (!page) return;
    side->AddChild(Gap(18));
    side->AddChild(Caption("BlocksTitle", "BLOCKS OF " + page->title, kAccent));
    side->AddChild(Gap(6));
    for (size_t i = 0; i < page->blocks.size(); ++i) {
        const Block& block = page->blocks[i];
        const BlockType* type = BlockRegistry::BuiltIn().Find(block.type);
        std::string label = type ? type->title : block.type;
        std::string detail = block.Text("title", block.Text("heading", block.Text("text")));
        if (detail.size() > 28) detail = detail.substr(0, 27) + "\xE2\x80\xA6";
        if (!detail.empty()) label += ": " + detail;
        auto row = CreateContainer("BlockRow" + std::to_string(i));
        row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
        row->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(2);
        auto button = SideButton("Block" + std::to_string(i), label, static_cast<int>(i) == selected);
        button->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(30));
        button->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
        const int index = static_cast<int>(i);
        button->onClick = [this, index]() { Later([this, index]() { SelectBlock(index); }); };
        row->AddChild(button);
        auto up = SmallButton("Up" + std::to_string(i), "\xE2\x96\xB2");
        up->onClick = [this, index]() { Later([this, index]() { MoveBlock(index, -1); }); };
        row->AddChild(up);
        auto down = SmallButton("Down" + std::to_string(i), "\xE2\x96\xBC");
        down->onClick = [this, index]() { Later([this, index]() { MoveBlock(index, 1); }); };
        row->AddChild(down);
        auto remove = SmallButton("Remove" + std::to_string(i), "\xE2\x9C\x95");
        remove->onClick = [this, index]() { Later([this, index]() { RemoveBlock(index); }); };
        row->AddChild(remove);
        side->AddChild(row);
    }
    side->AddChild(Gap(8));
    auto add = CreateDropdown("AddBlock", 0, 0, 0, 30);
    add->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(30));
    add->AddItem("+ Add a block\xE2\x80\xA6", "");
    for (const BlockType& type : BlockRegistry::BuiltIn().All()) add->AddItem(type.title, type.name);
    add->SetSelectedIndex(0, false);
    add->onSelectionChanged = [this](int, const DropdownItem& item) {
        const std::string type = item.value;
        if (type.empty()) return;
        Later([this, type]() { AddBlock(type); });
    };
    side->AddChild(add);
}

void EditorWindow::RebuildProperties() {
    if (!properties) return;
    properties->ClearChildren();
    Page* page = CurrentPageMutable();
    if (!page) return;
    auto field = [&](const std::string& id, const std::string& caption) {
        properties->AddChild(Caption(id + "Caption", caption, kInk, 13));
        properties->AddChild(Gap(4));
    };
    auto input = [&](const std::string& id, const std::string& value, std::function<void(const std::string&)> onChange) {
        auto edit = CreateTextInput(id, 0, 0, 0, 32);
        edit->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
        edit->SetFontSize(14);
        edit->SetText(value);
        edit->onTextChanged = std::move(onChange);
        properties->AddChild(edit);
        properties->AddChild(Gap(10));
    };
    auto area = [&](const std::string& id, const std::string& value, float height, std::function<void(const std::string&)> onChange) {
        auto edit = std::make_shared<UltraCanvasTextArea>(id, 0, 0, 0, height);
        edit->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
        edit->SetFontSize(14);
        edit->SetWordWrap(true);
        edit->SetText(value, false);
        edit->SetOnTextChanged(std::move(onChange));
        properties->AddChild(edit);
        properties->AddChild(Gap(10));
    };

    if (selected < 0 || selected >= static_cast<int>(page->blocks.size())) {
        properties->AddChild(Caption("PropsTitle", "PAGE", kAccent));
        properties->AddChild(Gap(10));
        field("PageTitle", "Title");
        input("PageTitle", page->title, [this](const std::string& text) {
            if (Page* p = CurrentPageMutable()) { p->title = text; dirty = true; }
            Later([this]() { RenderPreview(); });
        });
        field("PageDescription", "Description (search engines and link previews)");
        area("PageDescription", page->seo.description, 80, [this](const std::string& text) {
            if (Page* p = CurrentPageMutable()) { p->seo.description = text; dirty = true; }
        });
        field("PageStatus", "Status");
        auto statusPick = CreateDropdown("PageStatus", 0, 0, 0, 30);
        statusPick->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(30));
        const char* kStatuses[] = {"published", "draft", "scheduled"};
        for (int i = 0; i < 3; ++i) {
            statusPick->AddItem(kStatuses[i], kStatuses[i]);
            if (page->status == kStatuses[i]) statusPick->SetSelectedIndex(i, false);
        }
        statusPick->onSelectionChanged = [this](int, const DropdownItem& item) {
            if (Page* p = CurrentPageMutable()) { p->status = item.value; dirty = true; }
            Later([this]() { RebuildSide(); });
        };
        properties->AddChild(statusPick);
        properties->AddChild(Gap(10));
        auto pinned = UltraCanvasCheckbox::CreateCheckbox("PagePinned", 0, 0, 0, 28, "Pinned (first in the page list)", page->pinned);
        pinned->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(28));
        pinned->onStateChanged = [this, box = pinned.get()](CheckedState, CheckedState) {
            if (Page* p = CurrentPageMutable()) { p->pinned = box->IsChecked(); dirty = true; }
            Later([this]() { RebuildSide(); });
        };
        properties->AddChild(pinned);
        properties->AddChild(Gap(16));
        properties->AddChild(Caption("PropsHint", "Pick a block on the left to edit its properties.", kInkSoft, 13, false));
        return;
    }

    Block& block = page->blocks[selected];
    const BlockType* type = BlockRegistry::BuiltIn().Find(block.type);
    properties->AddChild(Caption("PropsTitle", type ? type->title : block.type, kAccent, 16));
    if (type) properties->AddChild(Caption("PropsDescription", type->description, kInkSoft, 12, false));
    properties->AddChild(Gap(12));
    if (!type) {
        properties->AddChild(Caption("PropsUnknown", "This block type is not in the registry; the page skips it.", kInkSoft, 13, false));
        return;
    }
    const int index = selected;
    auto setProperty = [this, index](const std::string& name, JSONValue value) {
        Page* p = CurrentPageMutable();
        if (!p || index >= static_cast<int>(p->blocks.size())) return;
        p->blocks[index].Set(name, std::move(value));
        dirty = true;
        Later([this]() { RenderPreview(); });
    };
    for (const BlockProperty& property : type->properties) {
        const std::string id = "Prop" + property.name;
        const JSONValue& value = block.props[property.name];
        std::string caption = property.name + (property.required ? " *" : "");
        if (!property.description.empty()) caption += " - " + property.description;
        field(id, caption);
        const std::string name = property.name;
        switch (property.kind) {
            case PropertyKind::Text:
            case PropertyKind::Url:
            case PropertyKind::Image:
                input(id, value.GetString(), [setProperty, name](const std::string& text) { setProperty(name, text); });
                break;
            case PropertyKind::LongText:
                area(id, value.GetString(), 120, [setProperty, name](const std::string& text) { setProperty(name, text); });
                break;
            case PropertyKind::Html:
                area(id, value.GetString(), 160, [setProperty, name](const std::string& text) { setProperty(name, text); });
                break;
            case PropertyKind::Boolean: {
                auto box = UltraCanvasCheckbox::CreateCheckbox(id, 0, 0, 0, 28, property.name, value.GetBoolean(false));
                box->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(28));
                box->onStateChanged = [setProperty, name, raw = box.get()](CheckedState, CheckedState) { setProperty(name, raw->IsChecked()); };
                properties->AddChild(box);
                properties->AddChild(Gap(10));
                break;
            }
            case PropertyKind::Number:
                input(id, std::to_string(static_cast<long long>(value.GetNumber(0))), [setProperty, name](const std::string& text) {
                    char* end = nullptr;
                    const long long number = std::strtoll(text.c_str(), &end, 10);   // locale-ok: an integer property, digits only
                    if (end && *end == '\0' && !text.empty()) setProperty(name, static_cast<int64_t>(number));
                });
                break;
            case PropertyKind::Link:
            case PropertyKind::Links:
            case PropertyKind::Strings:
            case PropertyKind::Items: {
                JSONSerializeOptions pretty;
                pretty.pretty = true;
                pretty.indentWidth = 2;
                const std::string json = value.IsNull() ? std::string(property.kind == PropertyKind::Link ? "{}" : "[]")
                                                        : JSON::Serialize(value, pretty);
                area(id, json, 150, [this, setProperty, name, kind = property.kind](const std::string& text) {
                    JSONParseResult result;
                    JSONValue parsed = JSON::Parse(text, &result);
                    const bool shape = kind == PropertyKind::Link ? parsed.IsObject() : parsed.IsArray();
                    if (!result.success || !shape) {
                        SetStatus(name + ": not valid JSON yet" + (result.success ? "" : " (" + result.errorMessage + ")"));
                        return;
                    }
                    SetStatus("");
                    setProperty(name, parsed);
                });
                break;
            }
        }
    }
}

} // namespace UltraPress
