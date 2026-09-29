// Apps/UltraMail/engine/UltraMailRichComposer.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailRichComposer.h"

#include "HTMLReader/HTMLRichDocumentImporter.h"
#include "UltraCanvasRichDocument.h"

#include <cstdio>
#include <map>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

bool HasContent(const std::string& text) {
    for (char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
    return false;
}

// A paragraph of plain lines, set off from the answer above it by a blank
// line's worth of space.
RichDocBlock TextParagraph(const std::vector<std::string>& lines) {
    RichDocBlock block;
    block.spaceBeforePt = 12.0f;
    for (size_t i = 0; i < lines.size(); ++i) {
        RichTextRun run;
        run.text = lines[i];
        run.lineBreakBefore = i > 0;
        block.runs.push_back(std::move(run));
    }
    return block;
}

HTMLRichImportOptions ImportOptions(const SourceMessage& src, int quoteLevel) {
    HTMLRichImportOptions options;
    options.quoteLevel = quoteLevel;
    if (src.image) {
        options.resolveImage = [&src](const std::string& url, HTMLRichImportImage& out) {
            out.data = src.image(url);
            return !out.data.empty();
        };
    }
    return options;
}

// A Content-ID for picture `index`: the same bytes give the same id, so a
// draft sent twice (the outbox retrying) says the same thing.
std::string ContentIdFor(int index, const std::vector<uint8_t>& data) {
    uint64_t hash = 1469598103934665603ULL;   // FNV-1a
    for (uint8_t byte : data) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "part%d.%016llx@ultramail", index,
                  static_cast<unsigned long long>(hash));
    return buffer;
}

} // namespace

bool MakeRichReply(Draft& draft, const SourceMessage& src) {
    if (!HasContent(src.bodyHtml)) return false;
    auto doc = std::make_shared<UCRichDocument>();
    doc->blocks.push_back(RichDocBlock{});   // where the answer is written
    const std::string who = src.fromName.empty() ? src.fromAddr : src.fromName;
    doc->blocks.push_back(TextParagraph({
        "On " + (src.date.empty() ? std::string("an earlier date") : src.date) + ", "
        + (who.empty() ? std::string("someone") : who) + " wrote:"}));
    AppendHTMLToRichDocument(*doc, src.bodyHtml, ImportOptions(src, 1));
    draft.richBody = doc;
    draft.bodyIsHtml = true;
    return true;
}

bool MakeRichForward(Draft& draft, const SourceMessage& src) {
    if (!HasContent(src.bodyHtml)) return false;
    auto doc = std::make_shared<UCRichDocument>();
    doc->blocks.push_back(RichDocBlock{});
    std::vector<std::string> header = {
        "---------- Forwarded message ----------",
        "From: " + (src.fromName.empty() ? src.fromAddr : src.fromName + " <" + src.fromAddr + ">"),
        "Date: " + src.date,
        "Subject: " + src.subject,
    };
    if (!src.to.empty()) {
        std::string to = "To: ";
        for (size_t i = 0; i < src.to.size(); ++i) to += (i ? ", " : "") + src.to[i];
        header.push_back(to);
    }
    doc->blocks.push_back(TextParagraph(header));
    AppendHTMLToRichDocument(*doc, src.bodyHtml, ImportOptions(src, 0));
    draft.richBody = doc;
    draft.bodyIsHtml = true;
    return true;
}

void RenderRichBody(Draft& draft) {
    if (!draft.richBody) return;
    const UCRichDocument& doc = *draft.richBody;

    // Only the pictures the text still shows go out: ToHTML asks for exactly
    // those.
    std::map<int, std::string> contentIds;
    RichDocumentHTMLOptions options;
    options.imageSource = [&](int index) {
        auto it = contentIds.find(index);
        if (it == contentIds.end()) {
            it = contentIds.emplace(index, ContentIdFor(index, doc.media[static_cast<size_t>(index)].data)).first;
        }
        return "cid:" + it->second;
    };
    draft.body = "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"></head><body>\n"
               + doc.ToHTML(options) + "</body></html>\n";
    draft.bodyIsHtml = true;
    draft.textBody = doc.ToPlainText();

    draft.inlineParts.clear();
    for (const auto& [index, contentId] : contentIds) {
        const RichDocMedia& media = doc.media[static_cast<size_t>(index)];
        Attachment part;
        part.filename = media.name.empty()
            ? "image" + std::to_string(index + 1) + "." + UCRichDocument::FileExtensionForMimeType(media.mimeType)
            : media.name;
        part.mediaType = media.mimeType;
        part.contentId = contentId;
        part.isInline = true;
        part.data = media.data;
        draft.inlineParts.push_back(std::move(part));
    }
}

} // namespace UltraMail
