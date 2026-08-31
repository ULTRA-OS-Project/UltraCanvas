// Apps/EmailCleaner/ui/EmailCleanerDetailView.cpp
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerDetailView.h"

#include "UltraCanvasButton.h"

#include "EmailCleanerAnalytics.h"

#include <string>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {

constexpr float kHeadingH  = 24.0f;
constexpr float kSummaryH  = 22.0f;
constexpr float kEvidenceH = 108.0f;
constexpr float kRowH      = 40.0f;
constexpr int   kMaxRows   = 200;

Color ToCanvasColor(const MapColor& color) {
    return Color(color.r, color.g, color.b, 255);
}

} // namespace

// ---- MessageRow ------------------------------------------------------------

MessageRow::MessageRow(const std::string& id, float x, float y, float w, float h,
                       const AnalyzedMessage& message,
                       const std::function<void(const AnalyzedMessage&)>& onAttachments)
    : UltraCanvasContainer(id, x, y, w, h) {
    // A colour chip carries the category, so a list of a hundred rows can be
    // skimmed without reading the label on each.
    auto chip = CreateLabel(id + ".chip", 4, 6, 10, h - 12, "");
    chip->SetBackgroundColor(ToCanvasColor(Analytics::CategoryColor(message.category)));
    AddChild(chip);

    AddChild(CreateLabel(id + ".date", 20, 2, 110, 18, FormatDate(message.date)));

    // The attachment button takes the right end of the row, so the subject
    // stops short of it rather than running underneath.
    const bool showAttachments = onAttachments && message.attachmentCount > 0;
    const float subjectW = w - 144 - (showAttachments ? 130.0f : 0.0f);

    std::string headline = message.subject.empty() ? "(no subject)" : message.subject;
    AddChild(CreateLabel(id + ".subject", 136, 2, subjectW, 18, headline));

    if (showAttachments) {
        // Just "Attachments": the row's second line already says how many and
        // how big, and a count in the label only made it too long to read.
        auto button = CreateButton(id + ".att", w - 126, 6, 120, h - 12, "Attachments");
        const AnalyzedMessage copy = message;
        button->onClick = [onAttachments, copy]() { onAttachments(copy); };
        AddChild(button);
    }

    std::string sub = CategoryLabel(message.category);
    if (message.score > 0.0)
        sub += " · score " + std::to_string(static_cast<int>(message.score + 0.5));
    if (message.attachmentCount > 0) {
        sub += " · " + std::to_string(message.attachmentCount) + " attachment" +
               (message.attachmentCount == 1 ? "" : "s") +
               " (" + FormatBytes(message.attachmentBytes) + ")";
    }
    sub += " · " + message.senderAddr;
    AddChild(CreateLabel(id + ".sub", 20, 20, w - 28, 18, sub));
}

// ---- DetailView ------------------------------------------------------------

std::shared_ptr<UltraCanvasContainer> DetailView::Build(float x, float y,
                                                        float width, float height) {
    width_ = width;
    container_ = CreateContainer("ecDetailView", x, y, width, height);

    heading_ = CreateLabel("ecDetailHeading", 8, 6, width - 16, kHeadingH, "All senders");
    heading_->SetFontSize(15.0f);
    container_->AddChild(heading_);

    summary_ = CreateLabel("ecDetailSummary", 8, 6 + kHeadingH, width - 16, kSummaryH,
                           "No messages analysed yet");
    container_->AddChild(summary_);

    const float evidenceY = 6 + kHeadingH + kSummaryH + 4;
    evidence_ = CreateContainer("ecDetailEvidence", 8, evidenceY, width - 16, kEvidenceH);
    container_->AddChild(evidence_);

    const float messagesY = evidenceY + kEvidenceH + 4;
    messages_ = CreateScrollableContainer("ecDetailMessages", 8, messagesY,
                                          width - 16, height - messagesY - 8);
    container_->AddChild(messages_);
    return container_;
}

void DetailView::Refresh(const MessageFilter& filter, const std::string& title) {
    filter_ = filter;
    if (heading_) heading_->SetText(title.empty() ? "All senders" : title);

    if (store_ && summary_) {
        StoreOverview overview;
        if (store_->GetOverview(filter_, overview))
            summary_->SetText(Analytics::DescribeOverview(overview));
    }
    RebuildEvidence();
    RebuildMessages();
}

void DetailView::RebuildEvidence() {
    if (!store_ || !evidence_) return;
    evidence_->ClearChildren();

    float y = 0.0f;
    evidence_->AddChild(CreateLabel("ecEvidenceTitle", 0, y, 360, 18,
                                    "Why these were flagged"));
    y += 20.0f;

    std::vector<KeywordHit> hits;
    if (store_->GetTopKeywords(filter_, 6, hits) && !hits.empty()) {
        for (size_t i = 0; i < hits.size(); ++i) {
            const KeywordHit& hit = hits[i];
            const std::string id = "ecEvidence" + std::to_string(i);
            auto chip = CreateLabel(id + ".chip", 0, y + 3, 10, 12, "");
            chip->SetBackgroundColor(ToCanvasColor(Analytics::CategoryColor(hit.category)));
            evidence_->AddChild(chip);
            // The store's keyword rollup reports how many messages a term
            // fired in, which is the number worth showing here.
            const std::string text = "\"" + hit.term + "\" — " +
                                     CategoryLabel(hit.category) + ", in " +
                                     std::to_string(static_cast<int>(hit.weight)) +
                                     " message" +
                                     (static_cast<int>(hit.weight) == 1 ? "" : "s");
            evidence_->AddChild(CreateLabel(id + ".text", 16, y, width_ - 40, 16, text));
            y += 18.0f;
            if (y > kEvidenceH - 18.0f) break;
        }
    } else {
        evidence_->AddChild(CreateLabel("ecEvidenceNone", 0, y, width_ - 32, 16,
                                        "Nothing matched a keyword rule here."));
        y += 18.0f;
    }

    // Attachment types are the other half of "what does this sender send me".
    std::vector<AttachmentTypeTotal> attachments;
    if (store_->GetAttachmentTypeTotals(filter_, attachments) && !attachments.empty()) {
        std::string line = "Attachments: ";
        for (size_t i = 0; i < attachments.size() && i < 4; ++i) {
            if (i) line += ", ";
            line += attachments[i].mediaType + " x" + std::to_string(attachments[i].count) +
                    " (" + FormatBytes(attachments[i].totalBytes) + ")";
            if (attachments[i].riskyCount > 0)
                line += " ⚠ " + std::to_string(attachments[i].riskyCount) + " risky";
        }
        evidence_->AddChild(CreateLabel("ecEvidenceAtt", 0, y, width_ - 32, 16, line));
    }
}

void DetailView::RebuildMessages() {
    if (!store_ || !messages_) return;
    messages_->ClearChildren();

    MessageFilter filter = filter_;
    if (filter.limit == 0 || filter.limit > kMaxRows) filter.limit = kMaxRows;

    std::vector<AnalyzedMessage> list;
    if (!store_->ListMessages(filter, list)) {
        messages_->AddChild(CreateLabel("ecMessagesError", 0, 0, width_ - 40, 20,
                                        "Could not read the analysis database."));
        return;
    }
    if (list.empty()) {
        messages_->AddChild(CreateLabel("ecMessagesEmpty", 0, 0, width_ - 40, 20,
                                        "No messages match the current filter."));
        return;
    }

    float y = 0.0f;
    int index = 0;
    for (const AnalyzedMessage& message : list) {
        auto row = std::make_shared<MessageRow>(
            "ecMessage" + std::to_string(index++), 0, y, width_ - 40, kRowH, message,
            onOpenAttachments);
        messages_->AddChild(row);
        y += kRowH + 2.0f;
    }
}

} // namespace EmailCleaner
