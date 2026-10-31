// Apps/UltraDesktop/ui/UltraDesktopDevicePanel.cpp
// The device panel. See the header.
// Version: 0.2.0 - an entry's own button, headings, the message line
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopDevicePanel.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {
constexpr int kPanelWidth = 340;
constexpr int kPanelHeight = 380;
constexpr int kInnerWidth = kPanelWidth - 28;
const Color kText(242, 242, 242);
const Color kMuted(160, 160, 160);
const Color kRow(44, 44, 44);
const Color kRowHighlighted(30, 64, 110);
const Color kError(255, 140, 120);
} // namespace

UltraDesktopDevicePanel::UltraDesktopDevicePanel(std::string title, std::function<DevicePanelContent()> read,
                                                 std::vector<DevicePanelAction> actions)
    : UltraDesktopPopupPanel(title, kPanelWidth, kPanelHeight),
      title_(std::move(title)),
      read_(std::move(read)),
      actions_(std::move(actions)) {}

void UltraDesktopDevicePanel::Build(UltraCanvasWindow& window) {
    window.layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window.SetPadding(12, 14, 12, 14);

    auto title = CreateLabel("dev.title", 0, 0, kInnerWidth, 22, title_);
    title->SetFontSize(11.5f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(kText);
    title->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(title);

    summary_ = CreateLabel("dev.summary", 0, 0, kInnerWidth, 34, "Reading...");
    summary_->SetFontSize(9.5f);
    summary_->SetTextColor(kMuted);
    summary_->SetWrap(TextWrap::WrapWord);
    summary_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(summary_);

    list_ = CreateScrollableContainer("dev.list", 0, 0, kInnerWidth, 200, true, false);
    list_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    window.AddChild(list_);

    message_ = CreateLabel("dev.message", 0, 0, kInnerWidth, 34, "");
    message_->SetFontSize(9.0f);
    message_->SetTextColor(kMuted);
    message_->SetWrap(TextWrap::WrapWord);
    message_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(message_);

    if (!actions_.empty()) {
        auto footer = std::make_shared<UltraCanvasContainer>("dev.footer");
        footer->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexShrink(0.0f);
        int index = 0;
        for (const DevicePanelAction& action : actions_) {
            const int width = 24 + static_cast<int>(action.label.size()) * 7;
            auto button = CreateButton("dev.action" + std::to_string(index++), 0, 0, width, 26, action.label);
            button->layoutItem.SetFlexShrink(0.0f);
            if (!action.tooltip.empty()) button->SetTooltip(action.tooltip);
            const std::function<void()> run = action.run;
            button->onClick = [this, run]() {
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                    app->PostToUIThread([this, run]() {
                        if (run) run();
                        Close();
                    });
                }
            };
            footer->AddChild(button);
        }
        window.AddChild(footer);
    }

    busy_ = false;
    Read();
}

void UltraDesktopDevicePanel::Unbuild() {
    summary_.reset();
    list_.reset();
    message_.reset();
}

void UltraDesktopDevicePanel::Read() {
    if (!read_) return;
    work_.Run<DevicePanelContent>(read_, [this](DevicePanelContent content) {
        if (IsOpen()) Apply(content);
    });
}

void UltraDesktopDevicePanel::SetMessage(const std::string& text, bool error) {
    if (!message_) return;
    message_->SetText(text);
    message_->SetTextColor(error ? kError : kMuted);
}

void UltraDesktopDevicePanel::RunEntryAction(const DevicePanelEntry& entry) {
    if (busy_ || !entry.action) return;
    busy_ = true;
    SetMessage(entry.busyText, false);
    std::function<std::string()> action = entry.action;
    std::function<DevicePanelContent()> read = read_;
    const std::string doneText = entry.doneText;
    work_.Run<std::pair<std::string, DevicePanelContent>>(
        [action, read]() {
            std::string error = action();
            return std::make_pair(error, read ? read() : DevicePanelContent{});
        },
        [this, doneText](std::pair<std::string, DevicePanelContent> result) {
            busy_ = false;
            if (!IsOpen()) return;
            Apply(result.second);
            SetMessage(result.first.empty() ? doneText : result.first, !result.first.empty());
        });
}

void UltraDesktopDevicePanel::Apply(const DevicePanelContent& content) {
    if (!summary_ || !list_) return;
    summary_->SetText(content.summary);
    list_->ClearChildren();
    int index = 0;
    for (const DevicePanelEntry& entry : content.entries) {
        const std::string id = "dev.entry" + std::to_string(index++);
        if (entry.heading) {
            auto heading = CreateLabel(id, 0, 0, kInnerWidth - 20, 18, entry.title);
            heading->SetTextColor(kMuted);
            heading->SetFontSize(9.0f);
            heading->SetFontWeight(FontWeight::Bold);
            heading->layoutItem.SetFlexShrink(0.0f);
            list_->AddChild(heading);
            continue;
        }
        const bool twoLines = !entry.detail.empty();
        const int height = twoLines ? 42 : 28;
        auto row = CreateContainer(id, 0, 0, 0, height);
        row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
        row->layoutItem.SetFlexShrink(0.0f);
        row->SetBackgroundColor(entry.highlighted ? kRowHighlighted : kRow);
        row->SetBorders(0, Colors::Transparent, 6);
        row->SetPadding(4, 8);
        auto texts = std::make_shared<UltraCanvasContainer>(id + ".texts");
        texts->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        texts->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
        auto name = CreateLabel(id + ".title", 0, 0, kInnerWidth - 36, 18, entry.title);
        name->SetTextColor(kText);
        name->SetFontSize(10.0f);
        texts->AddChild(name);
        if (twoLines) {
            auto detail = CreateLabel(id + ".detail", 0, 0, kInnerWidth - 36, 16, entry.detail);
            detail->SetTextColor(kMuted);
            detail->SetFontSize(8.5f);
            texts->AddChild(detail);
        }
        row->AddChild(texts);
        if (entry.action && !entry.actionLabel.empty()) {
            const int width = 24 + static_cast<int>(entry.actionLabel.size()) * 7;
            auto button = CreateButton(id + ".action", 0, 0, width, 26, entry.actionLabel);
            button->layoutItem.SetFlexShrink(0.0f);
            button->SetDisabled(busy_);
            const DevicePanelEntry copy = entry;
            button->onClick = [this, copy]() {
                // After this click's handler: the run rebuilds the rows.
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                    app->PostToUIThread([this, copy]() { RunEntryAction(copy); });
                }
            };
            row->AddChild(button);
        }
        list_->AddChild(row);
    }
    if (content.entries.empty() && !content.empty.empty()) {
        auto none = CreateLabel("dev.none", 0, 0, kInnerWidth - 20, 40, content.empty);
        none->SetTextColor(kMuted);
        none->SetFontSize(9.5f);
        none->SetWrap(TextWrap::WrapWord);
        list_->AddChild(none);
    }
    list_->InvalidateLayout();
    list_->RequestRedraw();
}

} // namespace UltraDesktop
