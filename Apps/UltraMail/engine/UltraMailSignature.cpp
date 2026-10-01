// Apps/UltraMail/engine/UltraMailSignature.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSignature.h"

#include "HTMLReader/HTMLRichDocumentImporter.h"
#include "UltraCanvasRichDocument.h"

#include <map>
#include <sstream>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr const char* kDelimiter = "-- ";   // RFC 3676 4.3: dash dash space
constexpr float kSignatureGapPt = 12.0f;    // above the signature, as above a quote

bool IsBlank(const std::string& line) {
    for (char c : line)
        if (c != ' ' && c != '\t' && c != '\r') return false;
    return true;
}

// The signature's lines: CRLF read as LF, blank lines at either end and
// trailing spaces dropped - but a line that is only "-- " keeps its space,
// since that is the separator.
std::vector<std::string> SignatureLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != kDelimiter)
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        lines.push_back(line);
    }
    while (!lines.empty() && IsBlank(lines.front())) lines.erase(lines.begin());
    while (!lines.empty() && IsBlank(lines.back())) lines.pop_back();
    return lines;
}

// Whether the user wrote the separator themselves ("--" or "-- ").
bool StartsWithDelimiter(const std::vector<std::string>& lines) {
    if (lines.empty()) return false;
    std::string first = lines.front();
    while (!first.empty() && first.back() == ' ') first.pop_back();
    return first == "--";
}

// The lines that go into the message: the separator, then the signature.
std::vector<std::string> DelimitedLines(const std::string& text) {
    std::vector<std::string> lines = SignatureLines(text);
    if (StartsWithDelimiter(lines)) lines.front() = kDelimiter;
    else lines.insert(lines.begin(), kDelimiter);
    return lines;
}

// A plain-text body: the signature below the first line - the one written on
// - when the body starts with the empty lines a reply or forward leaves for
// the answer, else at the end of what is there.
void AddToPlainBody(std::string& body, const std::vector<std::string>& lines) {
    std::string signature;
    for (size_t i = 0; i < lines.size(); ++i) signature += (i ? "\n" : "") + lines[i];

    if (body.empty() || body.front() == '\n' || body.front() == '\r') {
        size_t rest = 0;
        while (rest < body.size() && (body[rest] == '\n' || body[rest] == '\r')) ++rest;
        const std::string after = body.substr(rest);
        body = "\n\n" + signature + (after.empty() ? std::string("\n") : "\n\n" + after);
        return;
    }
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
    body += "\n\n" + signature + "\n";
}

// Where the signature's blocks go in a rich body: after the first block (the
// line the answer is written on; Composer / MakeRichReply always start with
// it), which is added when the document has none.
size_t SignatureInsertPoint(UCRichDocument& doc) {
    if (doc.blocks.empty()) doc.blocks.push_back(RichDocBlock{});
    return 1;
}

void RemapRunMedia(std::vector<RichTextRun>& runs, const std::map<int, int>& remap) {
    for (RichTextRun& run : runs) {
        if (run.mediaIndex < 0) continue;
        auto it = remap.find(run.mediaIndex);
        run.mediaIndex = it == remap.end() ? -1 : it->second;
    }
}

// Moves `from`'s blocks into `into` at `at`, its pictures into `into`'s media
// (an identical picture is stored once) with every reference renumbered.
void SpliceDocument(UCRichDocument& into, UCRichDocument from, size_t at) {
    std::map<int, int> remap;
    for (size_t i = 0; i < from.media.size(); ++i) {
        RichDocMedia& m = from.media[i];
        remap[static_cast<int>(i)] = into.AddMedia(m.name, m.mimeType, std::move(m.data));
    }
    for (RichDocBlock& block : from.blocks) {
        if (block.mediaIndex >= 0) {
            auto it = remap.find(block.mediaIndex);
            block.mediaIndex = it == remap.end() ? -1 : it->second;
        }
        RemapRunMedia(block.runs, remap);
        for (RichTableRow& row : block.tableRows)
            for (RichTableCell& cell : row.cells) RemapRunMedia(cell.runs, remap);
    }
    into.blocks.insert(into.blocks.begin() + static_cast<std::ptrdiff_t>(at),
                       std::make_move_iterator(from.blocks.begin()),
                       std::make_move_iterator(from.blocks.end()));
}

// One paragraph whose lines are separated by line breaks.
RichDocBlock LinesParagraph(const std::vector<std::string>& lines) {
    RichDocBlock block;
    for (size_t i = 0; i < lines.size(); ++i) {
        RichTextRun run;
        run.text = lines[i];
        run.lineBreakBefore = i > 0;
        block.runs.push_back(std::move(run));
    }
    return block;
}

bool RunsShowSomething(const std::vector<RichTextRun>& runs) {
    for (const RichTextRun& run : runs)
        if (run.IsInlineImage() || !IsBlank(run.text)) return true;
    return false;
}

bool DocumentShowsSomething(const UCRichDocument& doc) {
    for (const RichDocBlock& block : doc.blocks) {
        switch (block.type) {
            case RichBlockType::Image:
            case RichBlockType::HorizontalRule:
                return true;
            case RichBlockType::Table:
                for (const RichTableRow& row : block.tableRows)
                    for (const RichTableCell& cell : row.cells)
                        if (RunsShowSomething(cell.runs)) return true;
                break;
            default:
                if (RunsShowSomething(block.runs)) return true;
                break;
        }
    }
    return false;
}

// The first line of text, at most `maxChars` characters (UTF-8 aware).
std::string FirstLine(const std::string& text, size_t maxChars) {
    for (const std::string& line : SignatureLines(text)) {
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.erase(trimmed.begin());
        if (trimmed.empty() || trimmed == "--" || trimmed == kDelimiter) continue;
        size_t chars = 0, i = 0;
        while (i < trimmed.size()) {
            if (chars == maxChars) return trimmed.substr(0, i) + "\xE2\x80\xA6";   // …
            const unsigned char c = static_cast<unsigned char>(trimmed[i]);
            i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
            ++chars;
        }
        return trimmed;
    }
    return {};
}

} // namespace

std::shared_ptr<UCRichDocument> PlainBodyToRichDocument(const std::string& body) {
    auto doc = std::make_shared<UCRichDocument>();
    std::istringstream is(body);
    std::string line;
    bool first = true;
    bool open = false;          // the last block takes the next line
    int openLevel = 0;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // "> > text" and ">> text": one level per '>', one space after each.
        int level = 0;
        size_t i = 0;
        while (i < line.size() && line[i] == '>') {
            ++level;
            ++i;
            if (i < line.size() && line[i] == ' ') ++i;
        }
        const std::string text = line.substr(i);
        if (first) {
            RichDocBlock block;
            block.quoteLevel = level;
            if (!text.empty()) block.runs.push_back(RichTextRun{text});
            doc->blocks.push_back(std::move(block));
            first = false;
            open = false;       // the writing line stands alone
            continue;
        }
        if (IsBlank(text)) { open = false; continue; }
        if (open && level == openLevel) {
            RichTextRun run{text};
            run.lineBreakBefore = true;
            doc->blocks.back().runs.push_back(std::move(run));
            continue;
        }
        RichDocBlock block;
        block.quoteLevel = level;
        block.runs.push_back(RichTextRun{text});
        doc->blocks.push_back(std::move(block));
        open = true;
        openLevel = level;
    }
    if (doc->blocks.empty()) doc->blocks.push_back(RichDocBlock{});
    return doc;
}

std::shared_ptr<UCRichDocument> SignatureDocument(const std::string& html) {
    auto doc = std::make_shared<UCRichDocument>();
    if (!IsBlank(html)) AppendHTMLToRichDocument(*doc, html);
    if (doc->blocks.empty()) doc->blocks.push_back(RichDocBlock{});
    return doc;
}

std::string SignatureHtmlFrom(const UCRichDocument& document) {
    if (!DocumentShowsSomething(document)) return {};
    return document.ToHTML();
}

bool ApplySignature(Draft& draft, const Signature& signature, DraftPurpose purpose) {
    if (!signature.IsActive()) return false;
    if (purpose == DraftPurpose::ReplyOrForward && !signature.onReplies) return false;

    if (signature.kind == SignatureKind::Text) {
        const std::vector<std::string> lines = DelimitedLines(signature.text);
        if (!draft.richBody) {
            AddToPlainBody(draft.body, lines);
            return true;
        }
        UCRichDocument& doc = *draft.richBody;
        const size_t at = SignatureInsertPoint(doc);
        RichDocBlock block = LinesParagraph(lines);
        block.spaceBeforePt = kSignatureGapPt;
        doc.blocks.insert(doc.blocks.begin() + static_cast<std::ptrdiff_t>(at), std::move(block));
        return true;
    }

    // HTML: the draft becomes a formatted one if it is not already.
    UCRichDocument signatureDoc = ImportHTMLToRichDocument(signature.html);
    if (signatureDoc.blocks.empty()) return false;
    if (signatureDoc.blocks.front().spaceBeforePt < kSignatureGapPt)
        signatureDoc.blocks.front().spaceBeforePt = kSignatureGapPt;
    if (!draft.richBody) {
        draft.richBody = PlainBodyToRichDocument(draft.body);
        draft.bodyIsHtml = true;
    }
    UCRichDocument& doc = *draft.richBody;
    SpliceDocument(doc, std::move(signatureDoc), SignatureInsertPoint(doc));
    return true;
}

std::string DescribeSignature(const Signature& signature) {
    if (!signature.IsActive()) return "None";
    constexpr size_t kMaxChars = 40;
    if (signature.kind == SignatureKind::Text) {
        const std::string first = FirstLine(signature.text, kMaxChars);
        return first.empty() ? std::string("Plain text") : "Plain text \xC2\xB7 " + first;
    }
    const std::string first = FirstLine(SignatureDocument(signature.html)->ToPlainText(), kMaxChars);
    return first.empty() ? std::string("HTML") : "HTML \xC2\xB7 " + first;
}

} // namespace UltraMail
