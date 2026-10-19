// Apps/UltraPress/engine/UltraPressBlocks.cpp
// The built-in block types and the site validator.
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"

#include <set>

using UltraCanvas::JSONValue;

namespace UltraPress {

const BlockProperty* BlockType::FindProperty(const std::string& propertyName) const {
    for (const BlockProperty& property : properties) {
        if (property.name == propertyName) return &property;
    }
    return nullptr;
}

void BlockRegistry::Register(BlockType type) {
    for (BlockType& existing : types) {
        if (existing.name == type.name) {
            existing = std::move(type);
            return;
        }
    }
    types.push_back(std::move(type));
}

const BlockType* BlockRegistry::Find(const std::string& name) const {
    for (const BlockType& type : types) {
        if (type.name == name) return &type;
    }
    return nullptr;
}

namespace {

BlockProperty Prop(const char* name, PropertyKind kind, bool required, const char* description) {
    return BlockProperty{name, kind, required, description};
}

BlockRegistry MakeBuiltIn() {
    using K = PropertyKind;
    BlockRegistry r;
    r.Register({"heading", "Heading", "A section heading.", {
        Prop("text", K::Text, true, "The heading"),
        Prop("level", K::Number, false, "2, 3 or 4; 2 when absent"),
    }});
    r.Register({"text", "Text", "Paragraphs of plain text, with an optional heading.", {
        Prop("heading", K::Text, false, "A heading above the text"),
        Prop("text", K::LongText, true, "The paragraphs, separated by blank lines"),
    }});
    r.Register({"richtext", "Rich text", "Formatted text from the editor, kept as HTML.", {
        Prop("html", K::Html, true, "The HTML the editor wrote"),
    }});
    r.Register({"hero", "Hero", "The large opening section: a kicker, a title, a lead and buttons.", {
        Prop("kicker", K::Text, false, "The small line above the title"),
        Prop("title", K::Text, true, "The title"),
        Prop("text", K::LongText, false, "The lead"),
        Prop("buttons", K::Links, false, "Buttons: title, url or page, primary"),
        Prop("picture", K::Image, false, "A picture beside or behind the text"),
    }});
    r.Register({"picture", "Picture", "One picture with its alt text and a caption.", {
        Prop("src", K::Image, true, "The media path"),
        Prop("alt", K::Text, false, "What the picture shows, for those who cannot see it"),
        Prop("caption", K::Text, false, "A caption under it"),
    }});
    r.Register({"columns", "Columns", "Blocks side by side; each child is one column.", {
        Prop("heading", K::Text, false, "A heading above the columns"),
    }, true});
    r.Register({"card", "Card", "A titled box with text, a picture and a link; usually inside columns.", {
        Prop("title", K::Text, true, "The title"),
        Prop("text", K::LongText, false, "The text"),
        Prop("picture", K::Image, false, "A picture at the top"),
        Prop("link", K::Link, false, "Where the card leads: url or page"),
        Prop("linkTitle", K::Text, false, "The link's words; 'Read more' when absent"),
    }});
    r.Register({"features", "Feature list", "A heading and a list of short points.", {
        Prop("heading", K::Text, false, "The heading"),
        Prop("items", K::Strings, true, "The points"),
    }});
    r.Register({"cta", "Call to action", "A band asking for one action.", {
        Prop("title", K::Text, true, "The title"),
        Prop("text", K::LongText, false, "The text"),
        Prop("buttons", K::Links, false, "Buttons: title, url or page, primary"),
    }});
    r.Register({"faq", "FAQ", "Questions and answers; the HTML shadow carries them as structured data too.", {
        Prop("heading", K::Text, false, "The heading"),
        Prop("items", K::Items, true, "Objects with q and a"),
    }});
    r.Register({"quote", "Quote", "A quotation and who said it.", {
        Prop("text", K::LongText, true, "The quotation"),
        Prop("cite", K::Text, false, "Who said it"),
    }});
    r.Register({"buttons", "Buttons", "A row of buttons.", {
        Prop("items", K::Links, true, "Buttons: title, url or page, primary"),
    }});
    r.Register({"form", "Form", "A form the server tier receives; static sites need a form service.", {
        Prop("id", K::Text, true, "The form's name, the route it posts to"),
        Prop("heading", K::Text, false, "The heading"),
        Prop("text", K::LongText, false, "Text above the fields"),
        Prop("fields", K::Items, true, "Objects with name, label, type (text, email, textarea, checkbox), required"),
        Prop("submit", K::Text, false, "The button's words; 'Send' when absent"),
    }});
    r.Register({"newsletter", "Newsletter", "An e-mail address field that subscribes to the newsletter.", {
        Prop("heading", K::Text, false, "The heading"),
        Prop("text", K::LongText, false, "Why to subscribe"),
        Prop("list", K::Text, false, "The list's name; 'default' when absent"),
    }});
    r.Register({"downloads", "Downloads", "Files to download, with version and size.", {
        Prop("heading", K::Text, false, "The heading"),
        Prop("items", K::Items, true, "Objects with title, file (a media path or URL), version, size, description"),
    }});
    r.Register({"html", "HTML", "HTML written by the site owner, passed through unchanged.", {
        Prop("html", K::Html, true, "The HTML"),
    }});
    r.Register({"spacer", "Spacer", "Vertical space.", {
        Prop("height", K::Number, false, "Pixels; 32 when absent"),
    }});
    return r;
}

bool HasShape(const JSONValue& value, PropertyKind kind) {
    switch (kind) {
        case PropertyKind::Text:
        case PropertyKind::LongText:
        case PropertyKind::Html:
        case PropertyKind::Url:
        case PropertyKind::Image:
            return value.IsString();
        case PropertyKind::Boolean: return value.IsBoolean();
        case PropertyKind::Number: return value.IsNumber();
        case PropertyKind::Link: return value.IsObject();
        case PropertyKind::Links:
        case PropertyKind::Strings:
        case PropertyKind::Items:
            return value.IsArray();
    }
    return false;
}

void ValidateBlock(const Site& site, const Page& page, const Block& block, const std::string& path,
                   const BlockRegistry& registry, std::vector<ValidationIssue>& issues) {
    const BlockType* type = registry.Find(block.type);
    if (!type) {
        issues.push_back({page.slug, path, "unknown block type '" + block.type + "'"});
        return;
    }
    for (const BlockProperty& property : type->properties) {
        const JSONValue& value = block.props[property.name];
        if (value.IsNull()) {
            if (property.required) {
                issues.push_back({page.slug, path, block.type + " needs '" + property.name + "'"});
            }
            continue;
        }
        if (!HasShape(value, property.kind)) {
            issues.push_back({page.slug, path, block.type + "." + property.name + " has the wrong shape"});
            continue;
        }
        if (property.kind == PropertyKind::Link || property.kind == PropertyKind::Links) {
            const std::vector<JSONValue> links = property.kind == PropertyKind::Link
                                                     ? std::vector<JSONValue>{value}
                                                     : value.GetElements();
            for (const JSONValue& link : links) {
                const std::string slug = link["page"].GetString();
                if (!slug.empty() && !site.FindPage(slug)) {
                    issues.push_back({page.slug, path, block.type + "." + property.name +
                                                       " names a page the site does not have: '" + slug + "'"});
                }
                if (slug.empty() && link["url"].GetString().empty()) {
                    issues.push_back({page.slug, path, block.type + "." + property.name + " has a link with neither url nor page"});
                }
            }
        }
    }
    for (const auto& [name, value] : block.props.GetMembers()) {
        if (!type->FindProperty(name)) {
            issues.push_back({page.slug, path, block.type + " does not have a property '" + name + "'"});
        }
    }
    if (!block.children.empty() && !type->acceptsChildren) {
        issues.push_back({page.slug, path, block.type + " takes no children"});
    }
    for (size_t i = 0; i < block.children.size(); ++i) {
        ValidateBlock(site, page, block.children[i], path + ".children[" + std::to_string(i) + "]", registry, issues);
    }
}

void ValidateMenuItems(const Site& site, const std::string& menuName, const std::vector<MenuItem>& items,
                       std::vector<ValidationIssue>& issues) {
    for (const MenuItem& item : items) {
        if (!item.page.empty() && !site.FindPage(item.page)) {
            issues.push_back({"", "menus." + menuName, "'" + item.title + "' names a page the site does not have: '" + item.page + "'"});
        }
        if (item.page.empty() && item.url.empty() && item.children.empty()) {
            issues.push_back({"", "menus." + menuName, "'" + item.title + "' leads nowhere"});
        }
        ValidateMenuItems(site, menuName, item.children, issues);
    }
}

} // namespace

const BlockRegistry& BlockRegistry::BuiltIn() {
    static const BlockRegistry registry = MakeBuiltIn();
    return registry;
}

std::vector<ValidationIssue> ValidateSite(const Site& site, const BlockRegistry& registry) {
    std::vector<ValidationIssue> issues;
    if (site.url.empty()) issues.push_back({"", "site", "no url"});
    std::set<std::string> slugs;
    for (const Page& page : site.pages) {
        if (page.slug.empty()) issues.push_back({page.slug, "page", "a page without a slug"});
        if (!slugs.insert(page.slug).second) issues.push_back({page.slug, "page", "two pages with this slug"});
        if (page.title.empty()) issues.push_back({page.slug, "page", "no title"});
        if (page.status != "published" && page.status != "draft" && page.status != "scheduled") {
            issues.push_back({page.slug, "page", "status is not published, draft or scheduled"});
        }
        for (size_t i = 0; i < page.blocks.size(); ++i) {
            ValidateBlock(site, page, page.blocks[i], "blocks[" + std::to_string(i) + "]", registry, issues);
        }
    }
    for (const Menu& menu : site.menus) ValidateMenuItems(site, menu.name, menu.items, issues);
    return issues;
}

} // namespace UltraPress
