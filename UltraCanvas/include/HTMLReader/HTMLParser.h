// include/HTMLReader/HTMLParser.h
// Tolerant HTML/XHTML parser producing an HTML::Document.
// Handles real-world eBook markup: unclosed <p>/<li>, void elements,
// self-closing XHTML syntax, comments, CDATA, doctype, entities, and
// raw-text elements (<style>, <script>). Framework-independent.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "HTMLReader/HTMLDocument.h"

namespace UltraCanvas {
namespace HTML {

struct ParseOptions {
    // Keep whitespace-only text nodes. Off by default: they are meaningless
    // between block elements and the builder collapses inline whitespace
    // anyway ( <pre> content is always preserved regardless of this flag).
    bool keepWhitespaceNodes = false;
};

class Parser {
public:
    Document Parse(const std::string& html, const ParseOptions& options = {});

    const std::vector<std::string>& Errors() const { return errors; }

private:
    std::string input;
    size_t pos = 0;
    ParseOptions opts;
    std::vector<std::string> errors;

    char Cur() const { return pos < input.size() ? input[pos] : '\0'; }
    char Peek(size_t offset = 1) const {
        return pos + offset < input.size() ? input[pos + offset] : '\0';
    }
    bool AtEnd() const { return pos >= input.size(); }
    bool Match(const char* s) const;
    void SkipUntil(const char* s);

    void ParseNodes(Node* parent, std::vector<Node*>& openStack);
    void ParseTag(Node* parent, std::vector<Node*>& openStack);
    void ParseComment();
    std::string ParseTagName();
    void ParseAttributes(Node& element);
    std::string ParseAttributeValue();
    void ParseRawText(Node& element); // <style>/<script> content up to end tag

    void AddText(Node* parent, std::string text, bool inPre);

    static std::string Trimmed(const std::string& text);

    void Error(const std::string& message);
};

} // namespace HTML
} // namespace UltraCanvas
