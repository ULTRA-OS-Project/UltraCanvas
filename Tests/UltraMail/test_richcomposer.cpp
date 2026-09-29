// Tests/UltraMail/test_richcomposer.cpp
// Replying to and forwarding an HTML message with its formatting: the draft's
// rich body (quote, attribution, pictures), what is sent (HTML with cid:
// pictures, a plain-text version) and the outbox keeping all of it until the
// message is sent. Headless.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailComposer.h"
#include "UltraMailOutbox.h"
#include "UltraMailRichComposer.h"
#include "UltraMailSender.h"

#include "UltraCanvasRichDocument.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetMime.h>
#include <UltraNet/UltraNetPlugins.h>

#include <string>
#include <vector>

using namespace UltraMail;
using UltraCanvas::RichBlockType;
using UltraCanvas::UCRichDocument;

namespace {

// A PNG's first bytes: enough to be recognised and sized.
std::vector<uint8_t> Png(uint32_t w, uint32_t h) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (uint32_t v : {w, h})
        for (int shift : {24, 16, 8, 0}) png.push_back(static_cast<uint8_t>(v >> shift));
    png.insert(png.end(), {8, 2, 0, 0, 0, 0, 0, 0, 0});
    return png;
}

SourceMessage HtmlSource() {
    SourceMessage s;
    s.messageId = "<news@shop.example>";
    s.fromName = "Shop"; s.fromAddr = "news@shop.example";
    s.to = {"erika@example.com"};
    s.subject = "Your order";
    s.date = "Tue, 14 Jan 2026 14:02:00 +0000";
    s.body = "Thanks for your order";
    s.bodyHtml = "<html><body><h1>Thanks</h1><p>Your <b>order</b> "
                 "<img src=\"cid:logo@shop\" width=\"20\"> is on its way.</p>"
                 "<ul><li>Tea</li><li>Cups</li></ul>"
                 "<img src=\"https://tracker.example/open.gif\" alt=\"\"></body></html>";
    s.image = [](const std::string& src) -> std::vector<uint8_t> {
        if (src == "cid:logo@shop") return Png(40, 40);
        return {};   // the remote picture was never loaded
    };
    return s;
}

class FakeSmtp : public IMailProtocolPlugin {
public:
    UltraNetMailMessage last;
    std::string GetName() const override { return "FakeSMTP"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"smtp", "smtps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage& m, const UltraNetMailOptions&) override {
        last = m;
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
};

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(richreply_quotes_the_html_with_its_formatting) {
    const SourceMessage src = HtmlSource();
    Draft d = Composer::Reply(src, "Erika", "erika@example.com", false);
    REQUIRE(MakeRichReply(d, src));
    REQUIRE(d.richBody != nullptr);
    REQUIRE(d.bodyIsHtml);
    const UCRichDocument& doc = *d.richBody;
    // An empty line to write in, the attribution, then the quote.
    REQUIRE(doc.blocks.size() >= 5);
    REQUIRE(doc.blocks[0].runs.empty());
    REQUIRE_EQ(doc.blocks[0].quoteLevel, 0);
    REQUIRE_EQ(UCRichDocument::ConcatenateRunText(doc.blocks[1].runs),
               std::string("On Tue, 14 Jan 2026 14:02:00 +0000, Shop wrote:"));
    REQUIRE(doc.blocks[2].type == RichBlockType::Heading);
    REQUIRE_EQ(doc.blocks[2].quoteLevel, 1);
    bool bold = false, picture = false, list = false;
    for (const auto& block : doc.blocks) {
        for (const auto& run : block.runs) {
            if (run.text == "order" && run.bold) bold = true;
            if (run.IsInlineImage()) picture = true;
        }
        if (block.type == RichBlockType::ListItem && block.quoteLevel == 1) list = true;
    }
    REQUIRE(bold && picture && list);
    REQUIRE_EQ(doc.media.size(), (size_t)1);   // the unloaded remote picture is left out

    // A plain-text message keeps the plain reply.
    SourceMessage plain = HtmlSource();
    plain.bodyHtml.clear();
    Draft p = Composer::Reply(plain, "Erika", "erika@example.com", false);
    REQUIRE(!MakeRichReply(p, plain));
    REQUIRE(p.richBody == nullptr);
    REQUIRE(Has(p.body, "> Thanks for your order"));
}

TEST(richforward_keeps_the_original_unquoted) {
    const SourceMessage src = HtmlSource();
    Draft d = Composer::Forward(src, "Erika", "erika@example.com");
    REQUIRE(MakeRichForward(d, src));
    const UCRichDocument& doc = *d.richBody;
    const std::string header = UCRichDocument::ConcatenateRunText(doc.blocks[1].runs);
    REQUIRE(Has(header, "---------- Forwarded message ----------"));
    REQUIRE(Has(header, "From: Shop <news@shop.example>"));
    REQUIRE(Has(header, "Subject: Your order"));
    for (const auto& block : doc.blocks) REQUIRE_EQ(block.quoteLevel, 0);
}

TEST(richbody_renders_html_text_and_cid_parts) {
    const SourceMessage src = HtmlSource();
    Draft d = Composer::Reply(src, "Erika", "erika@example.com", false);
    MakeRichReply(d, src);
    // The answer, typed into the empty first line.
    UltraCanvas::RichTextRun answer;
    answer.text = "Great, thank you!";
    d.richBody->blocks[0].runs.push_back(answer);

    RenderRichBody(d);
    REQUIRE(d.bodyIsHtml);
    REQUIRE(Has(d.body, "<p>Great, thank you!</p>"));
    REQUIRE(Has(d.body, "<blockquote type=\"cite\""));
    REQUIRE(Has(d.body, "<b>order</b>"));
    REQUIRE_EQ(d.inlineParts.size(), (size_t)1);
    REQUIRE(d.inlineParts[0].isInline);
    REQUIRE(Has(d.body, "src=\"cid:" + d.inlineParts[0].contentId + "\""));
    REQUIRE_EQ(d.inlineParts[0].mediaType, std::string("image/png"));
    REQUIRE(Has(d.textBody, "Great, thank you!"));
    REQUIRE(Has(d.textBody, "> Thanks"));
    // Rendering again gives the same Content-ID (the outbox may retry).
    const std::string id = d.inlineParts[0].contentId;
    RenderRichBody(d);
    REQUIRE_EQ(d.inlineParts[0].contentId, id);

    // Sent: HTML, the text alternative and the picture as a related part.
    FakeSmtp smtp;
    MailSender sender(smtp);
    UltraNetMailOptions options;
    REQUIRE(sender.Send(d, "smtps://smtp.example.com:465/", options).success);
    REQUIRE(Has(smtp.last.contentType, "text/html"));
    REQUIRE(Has(smtp.last.alternativeText, "Great, thank you!"));
    REQUIRE_EQ(smtp.last.inlineParts.size(), (size_t)1);
    REQUIRE_EQ(smtp.last.inlineParts[0].contentId, id);
}

TEST(outbox_keeps_html_alternative_and_inline_parts) {
    const SourceMessage src = HtmlSource();
    Draft d = Composer::Reply(src, "Erika", "erika@example.com", false);
    MakeRichReply(d, src);
    RenderRichBody(d);
    Attachment file; file.filename = "invoice.pdf"; file.mediaType = "application/pdf"; file.data = {'%'};
    d.attachments.push_back(file);

    OutboxStore store;
    REQUIRE(store.Open("outbox-rich", ":memory:").success);
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "smtps://x/", d, id).success);
    std::vector<OutboxItem> pending;
    REQUIRE(store.ListPending(pending).success);
    REQUIRE_EQ(pending.size(), (size_t)1);
    const Draft& back = pending[0].draft;
    REQUIRE(back.bodyIsHtml);
    REQUIRE_EQ(back.textBody, d.textBody);
    REQUIRE_EQ(back.attachments.size(), (size_t)1);
    REQUIRE_EQ(back.attachments[0].filename, std::string("invoice.pdf"));
    REQUIRE_EQ(back.inlineParts.size(), (size_t)1);
    REQUIRE_EQ(back.inlineParts[0].contentId, d.inlineParts[0].contentId);
    REQUIRE(back.inlineParts[0].data == d.inlineParts[0].data);
}
