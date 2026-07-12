// core/HTMLReader/HTMLParser.cpp
// Tolerant HTML/XHTML parser implementation.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace UltraCanvas {
namespace HTML {

namespace {

bool IsVoidElement(const std::string& tag) {
    static const char* kVoid[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    for (const char* v : kVoid) {
        if (tag == v) return true;
    }
    return false;
}

bool IsRawTextElement(const std::string& tag) {
    return tag == "script" || tag == "style";
}

bool IsBlockLevel(const std::string& tag) {
    static const char* kBlock[] = {
        "address", "article", "aside", "blockquote", "div", "dl", "dd", "dt",
        "fieldset", "figure", "figcaption", "footer", "form", "h1", "h2", "h3",
        "h4", "h5", "h6", "header", "hr", "main", "nav", "ol", "p", "pre",
        "section", "table", "ul", "li"
    };
    for (const char* b : kBlock) {
        if (tag == b) return true;
    }
    return false;
}

// Whether an open element with tag `open` is implicitly closed when a start
// tag `incoming` appears.
bool ImplicitlyCloses(const std::string& open, const std::string& incoming) {
    if (open == "p") return IsBlockLevel(incoming);
    if (open == "li") return incoming == "li";
    if (open == "dt" || open == "dd") return incoming == "dt" || incoming == "dd";
    if (open == "tr") return incoming == "tr";
    if (open == "td" || open == "th") {
        return incoming == "td" || incoming == "th" || incoming == "tr";
    }
    if (open == "option") return incoming == "option";
    return false;
}

bool IsWhitespaceOnly(const std::string& text) {
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

// ============================================================================
// ENTRY POINT
// ============================================================================

Document Parser::Parse(const std::string& html, const ParseOptions& options) {
    input = html;
    pos = 0;
    opts = options;
    errors.clear();

    // Skip UTF-8 BOM.
    if (input.size() >= 3 &&
        static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF) {
        pos = 3;
    }

    // Parse everything into a scratch root, then normalize to html/head/body.
    auto scratch = std::make_shared<Node>();
    scratch->type = NodeType::Document;

    std::vector<Node*> openStack;
    openStack.push_back(scratch.get());
    ParseNodes(scratch.get(), openStack);

    Document doc;

    // Locate or synthesize <html> and <body>.
    NodePtr htmlNode;
    for (const auto& child : scratch->children) {
        if (child->IsElement("html")) {
            htmlNode = child;
            break;
        }
    }
    if (!htmlNode) {
        htmlNode = std::make_shared<Node>();
        htmlNode->tag = "html";
        // All parsed top-level nodes become body content below.
    }

    // A real <head>/<body> may sit under <html> or (for fragments and
    // partially structured input) directly at the top level.
    NodePtr headNode, bodyNode;
    auto findParts = [&](const std::vector<NodePtr>& from) {
        for (const auto& child : from) {
            if (child->IsElement("head") && !headNode) headNode = child;
            if (child->IsElement("body") && !bodyNode) bodyNode = child;
        }
    };
    findParts(htmlNode->children);
    findParts(scratch->children);

    if (!headNode) {
        headNode = std::make_shared<Node>();
        headNode->tag = "head";
    }
    if (!bodyNode) {
        bodyNode = std::make_shared<Node>();
        bodyNode->tag = "body";
    }
    // Fragment input: any top-level content that is not part of the document
    // scaffolding moves under <body>.
    auto adopt = [&](std::vector<NodePtr>& from) {
        for (auto& n : from) {
            if (n->IsElement("html") || n->IsElement("head") ||
                n->IsElement("body")) {
                continue;
            }
            if (n->type == NodeType::Comment) continue;
            n->parent = bodyNode.get();
            bodyNode->children.push_back(n);
        }
    };
    adopt(htmlNode->children);
    adopt(scratch->children);

    // Rebuild html's children as exactly [head, body].
    htmlNode->children.clear();
    headNode->parent = htmlNode.get();
    bodyNode->parent = htmlNode.get();
    htmlNode->children.push_back(headNode);
    htmlNode->children.push_back(bodyNode);
    htmlNode->parent = nullptr;
    doc.root = htmlNode;

    // Collect title / styles / meta from the whole tree (head elements often
    // end up in odd places in tolerant parsing).
    doc.root->ForEachElement([&](Node& element) {
        if (element.tag == "title" && doc.title.empty()) {
            doc.title = Trimmed(element.TextContent());
        } else if (element.tag == "style") {
            doc.styleSheets.push_back(element.TextContent());
        } else if (element.tag == "link") {
            if (ToLower(element.GetAttribute("rel")) == "stylesheet") {
                doc.styleSheetLinks.push_back(element.GetAttribute("href"));
            }
        } else if (element.tag == "meta") {
            std::string name = ToLower(element.GetAttribute("name"));
            if (!name.empty()) {
                doc.meta[name] = element.GetAttribute("content");
            }
        }
        return true;
    });

    return doc;
}

std::string Parser::Trimmed(const std::string& text) {
    size_t start = 0, end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(start, end - start);
}

// ============================================================================
// CORE LOOP
// ============================================================================

bool Parser::Match(const char* s) const {
    size_t len = std::strlen(s);
    if (pos + len > input.size()) return false;
    for (size_t i = 0; i < len; ++i) {
        if (std::tolower(static_cast<unsigned char>(input[pos + i])) !=
            std::tolower(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

void Parser::SkipUntil(const char* s) {
    while (!AtEnd() && !Match(s)) ++pos;
    if (Match(s)) pos += std::strlen(s);
}

void Parser::ParseNodes(Node* /*parent*/, std::vector<Node*>& openStack) {
    std::string pendingText;

    auto flushText = [&]() {
        if (pendingText.empty()) return;
        Node* top = openStack.back();
        bool inPre = false;
        for (Node* n : openStack) {
            if (n->IsElement("pre")) { inPre = true; break; }
        }
        AddText(top, std::move(pendingText), inPre);
        pendingText.clear();
    };

    while (!AtEnd()) {
        if (Cur() == '<') {
            char next = Peek();
            if (next == '!') {
                flushText();
                ParseComment();      // comment / doctype / CDATA
                continue;
            }
            if (next == '?') {       // XML declaration / processing instruction
                flushText();
                SkipUntil(">");
                continue;
            }
            if (next == '/' || std::isalpha(static_cast<unsigned char>(next))) {
                flushText();
                ParseTag(openStack.back(), openStack);
                continue;
            }
            // A bare '<' in text.
            pendingText += Cur();
            ++pos;
            continue;
        }

        pendingText += Cur();
        ++pos;
    }
    flushText();
}

void Parser::ParseComment() {
    if (Match("<!--")) {
        pos += 4;
        SkipUntil("-->");
        return;
    }
    if (Match("<![CDATA[")) {
        pos += 9;
        size_t start = pos;
        while (!AtEnd() && !Match("]]>")) ++pos;
        std::string data = input.substr(start, pos - start);
        if (Match("]]>")) pos += 3;
        // CDATA becomes literal text of the current element — rare in books;
        // dropped here because openStack isn't available. Recorded as error.
        if (!IsWhitespaceOnly(data)) {
            Error("CDATA section ignored");
        }
        return;
    }
    // <!DOCTYPE ...> or any other markup declaration.
    SkipUntil(">");
}

std::string Parser::ParseTagName() {
    std::string name;
    while (!AtEnd()) {
        char c = Cur();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
            c == ':') {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ++pos;
        } else {
            break;
        }
    }
    // Strip an XML namespace prefix (epub:switch → switch is NOT wanted; but
    // xhtml files commonly use plain names — keep the local part only).
    size_t colon = name.find(':');
    if (colon != std::string::npos) name = name.substr(colon + 1);
    return name;
}

void Parser::ParseTag(Node* /*parent*/, std::vector<Node*>& openStack) {
    // Assumes input[pos] == '<'.
    ++pos;

    bool closing = false;
    if (Cur() == '/') {
        closing = true;
        ++pos;
    }

    std::string tag = ParseTagName();
    if (tag.empty()) {
        // Malformed: skip to '>'.
        SkipUntil(">");
        return;
    }

    if (closing) {
        while (!AtEnd() && Cur() != '>') ++pos;
        if (Cur() == '>') ++pos;

        // Pop up to and including the matching open element; ignore the end
        // tag entirely if nothing matches (stray </b> etc.).
        for (size_t i = openStack.size(); i-- > 1;) {
            if (openStack[i]->IsElement(tag)) {
                openStack.resize(i);
                return;
            }
        }
        return;
    }

    // Implicit closes: <p> before block, <li> before <li>, ...
    while (openStack.size() > 1 &&
           ImplicitlyCloses(openStack.back()->tag, tag)) {
        openStack.pop_back();
    }

    auto element = std::make_shared<Node>();
    element->tag = tag;
    ParseAttributes(*element);

    bool selfClosed = false;
    if (Cur() == '/') {
        selfClosed = true;
        ++pos;
    }
    if (Cur() == '>') ++pos;

    Node* top = openStack.back();
    element->parent = top;
    top->children.push_back(element);

    if (selfClosed || IsVoidElement(tag)) return;

    if (IsRawTextElement(tag)) {
        ParseRawText(*element);
        return;
    }

    openStack.push_back(element.get());
}

void Parser::ParseAttributes(Node& element) {
    while (!AtEnd()) {
        while (!AtEnd() && std::isspace(static_cast<unsigned char>(Cur()))) ++pos;
        char c = Cur();
        if (c == '>' || c == '/' || c == '\0') return;

        std::string name;
        while (!AtEnd()) {
            c = Cur();
            if (c == '=' || c == '>' || c == '/' ||
                std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ++pos;
        }
        if (name.empty()) {
            ++pos;   // stray character; don't loop forever
            continue;
        }

        while (!AtEnd() && std::isspace(static_cast<unsigned char>(Cur()))) ++pos;

        std::string value;
        if (Cur() == '=') {
            ++pos;
            while (!AtEnd() && std::isspace(static_cast<unsigned char>(Cur()))) ++pos;
            value = ParseAttributeValue();
        }

        // Drop namespace prefixes on attribute names except xml:lang-style
        // ones we don't consume anyway; keep names verbatim otherwise.
        element.attributes.emplace_back(name, DecodeEntities(value));
    }
}

std::string Parser::ParseAttributeValue() {
    char quote = Cur();
    if (quote == '"' || quote == '\'') {
        ++pos;
        std::string value;
        while (!AtEnd() && Cur() != quote) {
            value += Cur();
            ++pos;
        }
        if (Cur() == quote) ++pos;
        return value;
    }

    std::string value;
    while (!AtEnd()) {
        char c = Cur();
        if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/') break;
        value += c;
        ++pos;
    }
    return value;
}

void Parser::ParseRawText(Node& element) {
    std::string endTag = "</" + element.tag;
    size_t start = pos;
    while (!AtEnd() && !Match(endTag.c_str())) ++pos;

    std::string content = input.substr(start, pos - start);
    if (!content.empty()) {
        auto textNode = std::make_shared<Node>();
        textNode->type = NodeType::Text;
        textNode->text = content;   // raw: no entity decoding inside style/script
        textNode->parent = &element;
        element.children.push_back(textNode);
    }

    if (Match(endTag.c_str())) {
        pos += endTag.size();
        while (!AtEnd() && Cur() != '>') ++pos;
        if (Cur() == '>') ++pos;
    }
}

void Parser::AddText(Node* parent, std::string text, bool inPre) {
    if (!inPre && !opts.keepWhitespaceNodes && IsWhitespaceOnly(text)) {
        // Preserve a single separating space between inline content so
        // "<b>a</b> <i>b</i>" keeps its word break.
        bool hasInlineSibling = false;
        for (auto it = parent->children.rbegin(); it != parent->children.rend(); ++it) {
            NodeType t = (*it)->type;
            if (t == NodeType::Comment) continue;
            if (t == NodeType::Text ||
                ((*it)->IsElement() && !IsBlockLevel((*it)->tag))) {
                hasInlineSibling = true;
            }
            break;
        }
        if (!hasInlineSibling) return;
        text = " ";
    }

    auto textNode = std::make_shared<Node>();
    textNode->type = NodeType::Text;
    textNode->text = DecodeEntities(text);
    textNode->parent = parent;
    parent->children.push_back(textNode);
}

void Parser::Error(const std::string& message) {
    if (errors.size() < 100) {
        errors.push_back(message + " (at offset " + std::to_string(pos) + ")");
    }
}

} // namespace HTML
} // namespace UltraCanvas
