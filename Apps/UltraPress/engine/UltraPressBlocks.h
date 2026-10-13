// Apps/UltraPress/engine/UltraPressBlocks.h
// The block registry: every block type a page may hold, with the properties
// it declares, so an editor can offer them, a validator can check a page and
// a renderer can refuse what it does not know. The predefined blocks of the
// brief are the sections of the ultraos.eu front page generalised
// (Docs/Research/UltraPressDesignProposal.md §5.1, §7.3).
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressModel.h"

#include <string>
#include <vector>

namespace UltraPress {

enum class PropertyKind {
    Text,       // one line
    LongText,   // paragraphs, separated by blank lines
    Html,       // HTML the editor wrote (UCRichDocument::ToHTML) or the owner pasted; passed through
    Url,        // an absolute URL, or a site path
    Image,      // a media path, relative to the site folder
    Boolean,
    Number,
    Link,       // {"url": ...} or {"page": "<slug>"}
    Links,      // an array of Link objects with a "title", and "primary" for the main one
    Strings,    // an array of strings
    Items,      // an array of objects whose fields the description names
};

struct BlockProperty {
    std::string name;
    PropertyKind kind = PropertyKind::Text;
    bool required = false;
    std::string description;
};

struct BlockType {
    std::string name;          // "hero", "faq", ...: what Block::type holds
    std::string title;         // what the editor's palette shows
    std::string description;
    std::vector<BlockProperty> properties;
    bool acceptsChildren = false;

    const BlockProperty* FindProperty(const std::string& name) const;
};

class BlockRegistry {
public:
    // The predefined blocks: heading, text, richtext, hero, picture, columns,
    // card, features, cta, faq, quote, buttons, form, newsletter, downloads,
    // html, spacer.
    static const BlockRegistry& BuiltIn();

    void Register(BlockType type);                  // replaces one of the same name
    const BlockType* Find(const std::string& name) const;
    const std::vector<BlockType>& All() const { return types; }

private:
    std::vector<BlockType> types;
};

struct ValidationIssue {
    std::string page;      // slug
    std::string path;      // "blocks[2].children[0]"
    std::string message;
};

// Unknown block types, missing required properties, properties of the
// wrong shape, children on a block that takes none, a menu item naming no
// page, two pages with one slug, a page without a title.
std::vector<ValidationIssue> ValidateSite(const Site& site,
                                          const BlockRegistry& registry = BlockRegistry::BuiltIn());

} // namespace UltraPress
