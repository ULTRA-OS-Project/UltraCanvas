// include/HTMLReader/HTMLDocument.h
// Lightweight HTML/XHTML DOM used by the HTMLReader module.
// This layer is framework-independent (std C++ only) so it can be unit-tested
// without linking the UltraCanvas library. The DOM is consumed by
// HTMLStyleResolver (CSS cascade) and HTMLElementBuilder (native element trees).
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace UltraCanvas {
namespace HTML {

enum class NodeType {
    Document,
    Element,
    Text,
    Comment
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

// One DOM node. A single struct (rather than an Element/Text class hierarchy)
// keeps the parser and consumers simple; `type` says which fields are valid.
struct Node {
    NodeType type = NodeType::Element;

    // Element fields. Tag and attribute names are stored lowercase.
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attributes;

    // Text / Comment content (entity-decoded for Text).
    std::string text;

    std::vector<NodePtr> children;
    Node* parent = nullptr;

    bool IsElement() const { return type == NodeType::Element; }
    bool IsElement(const std::string& tagName) const {
        return type == NodeType::Element && tag == tagName;
    }
    bool IsText() const { return type == NodeType::Text; }

    bool HasAttribute(const std::string& name) const;
    // Returns "" when absent.
    std::string GetAttribute(const std::string& name) const;
    void SetAttribute(const std::string& name, const std::string& value);

    std::string GetId() const { return GetAttribute("id"); }
    std::vector<std::string> ClassList() const;
    bool HasClass(const std::string& className) const;

    // Concatenated text of this node and all descendants.
    std::string TextContent() const;

    // First descendant element with the given tag (depth-first), or nullptr.
    Node* FindFirst(const std::string& tagName);

    // Depth-first visit of every descendant element (including this one if it
    // is an element). Return false from the callback to stop the walk.
    void ForEachElement(const std::function<bool(Node&)>& fn);
};

struct Document {
    // Root <html> element. The parser guarantees an html/body structure even
    // for fragments, so Body() is always available after a successful parse.
    NodePtr root;

    std::string title;

    // Contents of <style> blocks, in document order.
    std::vector<std::string> styleSheets;
    // hrefs of <link rel="stylesheet">, in document order. The caller resolves
    // and loads these (e.g. from an EPUB archive) and feeds the text to the
    // style resolver.
    std::vector<std::string> styleSheetLinks;

    std::unordered_map<std::string, std::string> meta;

    Node* Head() const;
    Node* Body() const;
    Node* GetElementById(const std::string& id) const;

    int CountElements() const;
};

// Decode HTML entities (&amp;, &#233;, &#x2019;, ...) into UTF-8.
std::string DecodeEntities(const std::string& text);

// Strip all tags from an HTML string and collapse whitespace; entity-decoded.
// Convenience for search/indexing paths that do not need a DOM.
std::string ExtractPlainText(const std::string& html);

} // namespace HTML
} // namespace UltraCanvas
