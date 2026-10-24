// Apps/EmailCleaner/ui/EmailCleanerContactsView.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerContactsView.h"

#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {

constexpr float kHeadingH = 24.0f;
constexpr float kLineH    = 20.0f;
constexpr float kToolbarH = 32.0f;
constexpr float kRowH     = 42.0f;

const Color kKeepColor(46, 125, 50, 255);
const Color kDeleteColor(198, 40, 40, 255);
const Color kReviewColor(120, 120, 120, 255);
const Color kQuietColor(96, 96, 96, 255);
const Color kTextColor(32, 32, 32, 255);
const Color kSelectedRowColor(253, 228, 228, 255);
const Color kPlainRowColor(255, 255, 255, 0);

Color TagColor(ContactVerdict verdict) {
    switch (verdict) {
        case ContactVerdict::Keep:   return kKeepColor;
        case ContactVerdict::Delete: return kDeleteColor;
        case ContactVerdict::Review: return kReviewColor;
    }
    return kReviewColor;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> ContactsView::Build(float x, float y,
                                                          float width, float height) {
    width_ = width;
    container_ = CreateContainer("ecContactsView", x, y, width, height);

    auto heading = CreateLabel("ecContactsHeading", 8, 6, width - 16, kHeadingH,
                               "Contacts - who you write to");
    heading->SetFontSize(15.0f);
    container_->AddChild(heading);

    summary_ = CreateLabel("ecContactsSummary", 8, 6 + kHeadingH, width - 16, kLineH,
                           "Reading the address book…");
    container_->AddChild(summary_);

    auto note = CreateLabel("ecContactsNote", 8, 6 + kHeadingH + kLineH, width - 16, kLineH,
        "Sent counts come from the Sent folder of every account; load the mail first. "
        "Selected contacts are marked red and are deleted from UltraMail's address book.");
    note->SetTextColor(kQuietColor);
    container_->AddChild(note);

    const float toolbarY = 6 + kHeadingH + 2 * kLineH + 4;
    float cursor = 8.0f;
    auto addButton = [&](const std::string& id, const std::string& text, float w) {
        auto button = CreateButton(id, cursor, toolbarY, w, 28, text);
        container_->AddChild(button);
        cursor += w + 8.0f;
        return button;
    };
    addButton("ecContactsSelect", "Select never written to", 190)->onClick =
        [this]() { SelectDeleteRows(true); };
    addButton("ecContactsClear", "Clear selection", 130)->onClick =
        [this]() { SelectDeleteRows(false); };
    addButton("ecContactsReload", "Reload", 90)->onClick =
        [this]() { if (onReload) onReload(); };
    deleteButton_ = addButton("ecContactsDelete", "Delete selected…", 150);
    deleteButton_->onClick = [this]() {
        if (!onDelete) return;
        std::vector<ContactReview> selected;
        for (const Row& row : rows_)
            if (row.select && row.select->IsChecked()) selected.push_back(row.review);
        if (!selected.empty()) onDelete(selected);
    };

    selection_ = CreateLabel("ecContactsSelection", cursor, toolbarY + 4,
                             width - cursor - 8, kLineH, "");
    selection_->SetTextColor(kDeleteColor);
    container_->AddChild(selection_);

    const float listY = toolbarY + kToolbarH + 6;
    list_ = CreateScrollableContainer("ecContactsList", 8, listY, width - 16,
                                      height - listY - 8);
    container_->AddChild(list_);
    return container_;
}

void ContactsView::SetReviews(std::vector<ContactReview> reviews) {
    rows_.clear();
    rows_.reserve(reviews.size());
    for (ContactReview& review : reviews) {
        Row row;
        row.review = std::move(review);
        rows_.push_back(std::move(row));
    }
    if (summary_) {
        std::vector<ContactReview> all;
        all.reserve(rows_.size());
        for (const Row& row : rows_) all.push_back(row.review);
        summary_->SetText(DescribeReview(Summarize(all)));
    }
    RebuildRows();
    SelectDeleteRows(true);
}

void ContactsView::SetUnavailable(const std::string& reason) {
    rows_.clear();
    if (summary_) summary_->SetText(reason);
    RebuildRows();
    UpdateSelectionText();
}

std::vector<int64_t> ContactsView::SelectedIds() const {
    std::vector<int64_t> ids;
    for (const Row& row : rows_)
        if (row.select && row.select->IsChecked()) ids.push_back(row.review.contactId);
    return ids;
}

void ContactsView::RebuildRows() {
    if (!list_) return;
    list_->ClearChildren();

    const float rowW = width_ - 40;
    // The counts sit at the right end: sent first, the figure the list is
    // ordered by, then received as the second.
    const float sentX     = rowW - 300;
    const float receivedX = rowW - 150;
    const float textW     = sentX - 112;

    float y = 0.0f;
    for (size_t i = 0; i < rows_.size(); ++i) {
        Row& row = rows_[i];
        const ContactReview& review = row.review;
        const std::string id = "ecContact" + std::to_string(i);

        row.container = CreateContainer(id, 0, y, rowW, kRowH);

        row.select = std::make_shared<UltraCanvasCheckbox>(id + ".sel", 4, 10, 22, 22, "");
        row.select->onStateChanged = [this, i](CheckedState, CheckedState) {
            if (i >= rows_.size()) return;
            StyleRow(rows_[i]);
            if (!bulkChange_) UpdateSelectionText();
        };
        row.container->AddChild(row.select);

        auto tag = CreateLabel(id + ".tag", 32, 11, 68, 20, TagLabel(review.verdict));
        tag->SetBackgroundColor(TagColor(review.verdict));
        tag->SetTextColor(Color(255, 255, 255, 255));
        tag->SetAlignment(TextAlignment::Center);
        row.container->AddChild(tag);

        std::string title = review.Name();
        if (!review.organization.empty()) title += " · " + review.organization;
        row.name = CreateLabel(id + ".name", 108, 2, textW, 19, title);
        row.name->SetFontWeight(FontWeight::Bold);
        row.container->AddChild(row.name);

        std::string sub;
        for (size_t a = 0; a < review.addresses.size() && a < 2; ++a)
            sub += (a ? ", " : "") + review.addresses[a];
        if (review.addresses.size() > 2)
            sub += " +" + std::to_string(review.addresses.size() - 2);
        sub += (sub.empty() ? "" : " · ") + review.Reason();
        auto subLabel = CreateLabel(id + ".sub", 108, 21, textW, 18, sub);
        subLabel->SetTextColor(kQuietColor);
        row.container->AddChild(subLabel);

        row.container->AddChild(CreateLabel(id + ".sent", sentX, 2, 140, 19,
            "Sent " + std::to_string(review.sent)));
        row.container->AddChild(CreateLabel(id + ".lastSent", sentX, 21, 140, 18,
            review.lastSent > 0 ? "last " + FormatDate(review.lastSent) : "never"));

        auto received = CreateLabel(id + ".recv", receivedX, 2, 140, 19,
            "Received " + std::to_string(review.received));
        received->SetTextColor(kQuietColor);
        row.container->AddChild(received);
        if (review.lastReceived > 0) {
            auto lastReceived = CreateLabel(id + ".lastRecv", receivedX, 21, 140, 18,
                "last " + FormatDate(review.lastReceived));
            lastReceived->SetTextColor(kQuietColor);
            row.container->AddChild(lastReceived);
        }

        StyleRow(row);
        list_->AddChild(row.container);
        y += kRowH + 2.0f;
    }

    if (rows_.empty()) {
        list_->AddChild(CreateLabel("ecContactsEmpty", 0, 0, width_ - 40, 20,
                                    "No contacts to show."));
    }
}

void ContactsView::StyleRow(Row& row) {
    const bool selected = row.select && row.select->IsChecked();
    if (row.container)
        row.container->SetBackgroundColor(selected ? kSelectedRowColor : kPlainRowColor);
    if (row.name) row.name->SetTextColor(selected ? kDeleteColor : kTextColor);
}

void ContactsView::SelectDeleteRows(bool select) {
    bulkChange_ = true;
    for (Row& row : rows_) {
        if (!row.select) continue;
        row.select->SetChecked(select && row.review.verdict == ContactVerdict::Delete);
        StyleRow(row);
    }
    bulkChange_ = false;
    UpdateSelectionText();
}

void ContactsView::UpdateSelectionText() {
    int selected = 0;
    for (const Row& row : rows_)
        if (row.select && row.select->IsChecked()) ++selected;
    if (selection_) {
        selection_->SetText(selected == 0
            ? std::string("Nothing selected")
            : std::to_string(selected) + (selected == 1 ? " contact" : " contacts") +
              " selected for deletion");
    }
    if (deleteButton_) deleteButton_->SetDisabled(selected == 0);
}

} // namespace EmailCleaner
