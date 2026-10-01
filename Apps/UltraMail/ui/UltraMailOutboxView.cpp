// Apps/UltraMail/ui/UltraMailOutboxView.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOutboxView.h"

#include "UltraCanvasListSelection.h"
#include "UltraMailTheme.h"

#include <cctype>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
}

// Why it is still here, and where it is kept.
std::string StatusOf(const OutboxItem& item, bool sending, bool held) {
    std::string status;
    if (held)                       status = "Open for correcting - not sent until it is sent again";
    else if (sending)               status = "Sending…";
    else if (item.attempts == 0)    status = "Waiting to be sent";
    else if (!item.lastError.empty()) {
        status = item.lastError;   // as a sentence: capital first letter
        status[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(status[0])));
    }
    else                            status = "Not sent yet";
    if (item.HasDraftCopy()) status += " · copy in " + item.draftsFolder;
    return status;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> OutboxView::Build() {
    root_ = CreateContainer("outboxView", 0, 0, 0, 0);
    root_->SetPadding(Theme::kPagePadding);
    root_->SetBackgroundColor(Theme::kCardBackground);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(Theme::kGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    summary_ = Theme::MakeLine("outboxSummary", "", Theme::kControlHeight,
                               Theme::kSizeBody, Theme::kTextSecondary);
    summary_->SetWrap(TextWrap::WrapWord);
    root_->AddChild(summary_);

    model_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    model_->SetColumns({
        ListColumnDef("To", 150),
        ListColumnDef("Subject", 170),
        ListColumnDef("From", 140),
        ListColumnDef("Tries", 44, TextAlignment::Right),
        ListColumnDef("Status", 220),
    });
    list_ = std::make_shared<UltraCanvasListView>("outboxList");
    list_->SetModel(model_);
    list_->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    ListViewStyle style;
    style.backgroundColor          = Theme::kCardBackground;
    style.showHeader               = true;
    style.headerBackgroundColor    = Theme::kSidebar;
    style.headerTextColor          = Theme::kTextSecondary;
    style.headerFontSize           = Theme::kSizeBody;
    style.selectionBackgroundColor = Theme::kRowSelected;
    style.hoverBackgroundColor     = Theme::kRowHover;
    list_->SetStyle(style);
    // The app's text size and colour, dark on the light selection too.
    auto rows = std::make_shared<UltraCanvasDefaultListDelegate>();
    rows->SetFontSize(Theme::kSizeBody);
    rows->SetTextColor(Theme::kTextPrimary);
    rows->SetSelectedTextColor(Theme::kTextPrimary);
    list_->SetDelegate(rows);
    list_->SetColumnsResizable(true);
    list_->SetBorders(1.0f, Theme::kCardBorder, Theme::kControlRadius);
    list_->onSelectionChanged = [this](const std::vector<int>&) { UpdateButtons(); };
    // Double-click: open it to correct it (Edit).
    list_->onItemDoubleClicked = [this](int) {
        if (const int64_t id = SelectedId(); id && !sending_ && onEdit) onEdit(id);
    };
    // The whole error, which the column may cut short.
    list_->tooltipProvider = [this](int row, int) -> std::string {
        if (row < 0 || row >= static_cast<int>(items_.size())) return {};
        const OutboxItem& item = items_[static_cast<size_t>(row)];
        return item.lastError.empty() ? std::string() : "Last attempt: " + item.lastError;
    };
    root_->AddChild(list_);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto buttons = CreateContainer("outboxButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttons->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto make = [&buttons](const std::string& id, const std::string& text, float width,
                           const std::string& tooltip, bool primary) {
        auto b = CreateButton(id, 0, 0, width, Theme::kControlHeight, text);
        Theme::FitToLabel(b, width);
        if (primary) Theme::StylePrimary(b); else Theme::StyleSecondary(b);
        b->SetTooltip(tooltip);
        buttons->AddChild(b);
        return b;
    };
    sendNow_ = make("outboxSendNow", "Send now", 90,
                    "Try to send every waiting message now", true);
    sendNow_->onClick = [this]() { if (onSendNow) onSendNow(); };
    edit_ = make("outboxEdit", "Edit\xE2\x80\xA6", 70,
                 "Open the message to correct it; it leaves the outbox when you send it again",
                 false);
    edit_->onClick = [this]() { if (const int64_t id = SelectedId(); id && onEdit) onEdit(id); };
    delete_ = make("outboxDelete", "Delete", 70,
                   "Do not send this message; its copy in Drafts is deleted too", false);
    Theme::StyleDanger(delete_);
    delete_->onClick = [this]() { if (const int64_t id = SelectedId(); id && onDelete) onDelete(id); };
    buttons->AddStretchSpacer(1);
    auto close = make("outboxClose", "Close", 80, "", false);
    close->onClick = [this]() { if (onClose) onClose(); };
    {
        ContainerStyle cs;
        cs.autoShowScrollbars = false;
        buttons->SetContainerStyle(cs);
    }
    root_->AddChild(buttons);
    buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    SetItems(items_, sending_, held_);
    return root_;
}

void OutboxView::Resize(float width, float height) {
    if (root_) root_->SetElementSize(Size2Df(width, height));
}

void OutboxView::Release() {
    list_.reset();
    model_.reset();
    summary_.reset();
    sendNow_.reset();
    edit_.reset();
    delete_.reset();
    root_.reset();
}

void OutboxView::SetItems(const std::vector<OutboxItem>& items, bool sending,
                          const std::set<int64_t>& held) {
    const int64_t selected = SelectedId();
    items_ = items;
    sending_ = sending;
    held_ = held;
    if (!model_ || !list_) return;

    model_->Clear();
    int selectRow = -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        const OutboxItem& item = items_[i];
        const Draft& d = item.draft;
        const std::string to = Join(!d.to.empty() ? d.to : !d.cc.empty() ? d.cc : d.bcc);
        model_->AddItem(MultiColumnListItem({
            to.empty() ? std::string("(no recipient)") : to,
            d.subject.empty() ? std::string("(no subject)") : d.subject,
            d.fromAddr,
            std::to_string(item.attempts),
            StatusOf(item, sending, held_.count(item.id) != 0),
        }));
        if (item.id == selected) selectRow = static_cast<int>(i);
    }
    list_->ResetSelection();
    if (selectRow >= 0) list_->GetSelection()->Select(selectRow);
    else if (!items_.empty()) list_->GetSelection()->Select(0);
    list_->RequestRedraw();

    if (summary_) {
        const size_t n = items_.size();
        summary_->SetText(n == 0
            ? std::string("The outbox is empty: every message has been sent.")
            : (n == 1 ? std::string("1 message is") : std::to_string(n) + " messages are")
              + " waiting to be sent. UltraMail tries again by itself - sooner when the "
                "connection is back. A message the server keeps refusing can be "
                "corrected (Edit) or deleted here.");
    }
    UpdateButtons();
}

int64_t OutboxView::SelectedId() const {
    if (!list_ || !list_->GetSelection()) return 0;
    const int row = list_->GetSelection()->GetCurrentRow();
    if (row < 0 || row >= static_cast<int>(items_.size())) return 0;
    return items_[static_cast<size_t>(row)].id;
}

void OutboxView::UpdateButtons() {
    const bool any = SelectedId() != 0;
    if (sendNow_) sendNow_->SetDisabled(items_.empty() || sending_);
    if (edit_)    edit_->SetDisabled(!any || sending_);
    if (delete_)  delete_->SetDisabled(!any || sending_);
}

} // namespace UltraMail
