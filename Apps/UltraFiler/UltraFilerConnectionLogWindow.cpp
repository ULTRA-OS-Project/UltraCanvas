// Apps/UltraFiler/UltraFilerConnectionLogWindow.cpp
// The connection log window. The text areas do the work - Markdown mode lays
// the error report's headings, tables and code blocks out, and being real
// text areas they scroll, select and copy without any of that written here.
// Built the way UltraCanvasMetadataDialog is.
// Version: 1.0.0
// Last Modified: 2026-10-04
// Author: UltraCanvas Framework

#include "UltraFilerConnectionLogWindow.h"
#include "UltraFilerTabStripStyle.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {

namespace {

constexpr int kPadding = 12;
constexpr float kMinButtonWidth = 88.0f;

void SizeButtonToLabel(const std::shared_ptr<UltraCanvasButton>& button) {
    // Hugs its own text so a translated caption fits, never narrower than a
    // comfortable minimum.
    button->size.width = CSSLayout::Dimension::Auto();
    CSSLayout::BoxConstraints limits =
            button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
    limits.minWidth = CSSLayout::Dimension::Px(kMinButtonWidth);
    button->boxConstraints = limits;
    button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
}

std::shared_ptr<UltraCanvasTextArea> MakeReadOnlyView(const std::string& id,
                                                      TextAreaEditingMode mode) {
    auto view = std::make_shared<UltraCanvasTextArea>(id, 0, 0, 0, 0);
    view->SetEditingMode(mode);
    view->SetReadOnly(true);
    view->SetShowLineNumbers(false);
    view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return view;
}

} // namespace

UltraFilerConnectionLogWindow::UltraFilerConnectionLogWindow() : UltraCanvasWindow() {
    config_.title = "Connection log - remote drives";
    config_.width = 760;
    config_.height = 560;
    config_.minWidth = 420;
    config_.minHeight = 260;
    config_.resizable = true;
    config_.deleteOnClose = true;

    SetPadding(kPadding);
    SetBackgroundColor(Color(250, 250, 252, 255));
    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    tabs = std::make_shared<UltraCanvasTabbedContainer>("ConnectionLogTabs");
    tabs->SetTabHeight(28);
    tabs->SetTabMinWidth(110);
    tabs->SetCloseMode(TabCloseMode::NoClose);
    tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // The report: headings, a table per failure, the steps and diagnostics
    // as code blocks. Wrapped, because a reply from a server can be long.
    errorsView = MakeReadOnlyView("ConnectionLogErrors", TextAreaEditingMode::MarkdownHybrid);
    errorsView->SetWordWrap(true);
    tabs->AddTab("Errors", errorsView);

    // The log: one line per step, in a fixed-width face so the kinds line up
    // the way they do in an FTP client, and not wrapped, so a line is a line.
    logView = MakeReadOnlyView("ConnectionLogMessages", TextAreaEditingMode::PlainText);
    logView->SetWordWrap(false);
    if (auto* app = UltraCanvasApplication::GetInstance())
        logView->SetFontFamily(app->GetDefaultMonospacedFontStyle().fontFamily);
    tabs->AddTab("Message log", logView);

    tabs->onTabChange = [this](int /*oldIndex*/, int /*newIndex*/) { RenderActiveTab(); };
    AddChild(tabs);

    auto footer = std::make_shared<UltraCanvasContainer>("ConnectionLogFooter", 0, 0, 0, 34);
    footer->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    summaryLabel = std::make_shared<UltraCanvasLabel>("ConnectionLogSummary", 0, 0, 0, 20);
    summaryLabel->SetTextColor(Color(70, 70, 76, 255));
    summaryLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    summaryLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    footer->AddChild(summaryLabel);

    copyButton = std::make_shared<UltraCanvasButton>("ConnectionLogCopy", 0, 0, 96, 30);
    copyButton->SetText("Copy");
    copyButton->SetTooltip("Copy the tab on show to the clipboard - the error report or "
                           "the whole message log");
    copyButton->onClick = [this]() {
        auto* clipboard = GetClipboard();
        if (!clipboard) return;
        clipboard->SetText(tabs->GetActiveTab() == ErrorsTab ? errorsText : logText);
    };
    SizeButtonToLabel(copyButton);
    footer->AddChild(copyButton);

    clearButton = std::make_shared<UltraCanvasButton>("ConnectionLogClear", 0, 0, 96, 30);
    clearButton->SetText("Clear");
    clearButton->SetTooltip("Forget every connection logged so far");
    clearButton->onClick = [this]() {
        if (onClear) onClear();
    };
    SizeButtonToLabel(clearButton);
    footer->AddChild(clearButton);

    closeButton = std::make_shared<UltraCanvasButton>("ConnectionLogClose", 0, 0, 96, 30);
    closeButton->SetText("Close");
    closeButton->SetStyle(ButtonStyles::PrimaryStyle());
    closeButton->onClick = [this]() { PerformClose(); };
    SizeButtonToLabel(closeButton);
    footer->AddChild(closeButton);

    AddChild(footer);
}

void UltraFilerConnectionLogWindow::Update(const std::vector<RemoteLogSession>& sessions) {
    errorsText = RenderRemoteErrorsMarkdown(sessions);
    logText = RenderRemoteLogText(sessions);

    std::size_t failures = 0, running = 0;
    for (const RemoteLogSession& s : sessions) {
        if (s.failed) ++failures;
        if (s.Running()) ++running;
    }
    tabs->SetTabTitle(ErrorsTab, failures > 0 ? "Errors (" + std::to_string(failures) + ")"
                                              : "Errors");
    std::string summary = std::to_string(sessions.size()) +
                          (sessions.size() == 1 ? " connection" : " connections");
    if (failures > 0)
        summary += ", " + std::to_string(failures) + " failed";
    if (running > 0) summary += ", " + std::to_string(running) + " running";
    summaryLabel->SetText(summary);

    RenderActiveTab();
}

void UltraFilerConnectionLogWindow::ShowTab(Tab tab) {
    if (tabs->GetActiveTab() != tab) tabs->SetActiveTab(tab);   // renders through onTabChange
    else RenderActiveTab();
}

void UltraFilerConnectionLogWindow::RenderActiveTab() {
    // Only the tab on show is laid out again; the other catches up when it
    // is brought up. The message log can be thousands of lines, and it
    // changes with every step of a running connection.
    if (tabs->GetActiveTab() == ErrorsTab) {
        if (shownErrorsText == errorsText) return;
        shownErrorsText = errorsText;
        errorsView->SetText(errorsText, false);
    } else {
        if (shownLogText == logText) return;
        shownLogText = logText;
        logView->SetText(logText, false);
        // Follows the newest line, the way a log does - once the text area
        // has laid the new text out. Scrolled at once, it measured the text
        // it held before (or none, when the tab had never been on show) and
        // scrolled the log out of sight. A timer runs after the frame that
        // lays it out; it holds the view weakly, so a window closed in
        // between is simply not scrolled.
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        std::weak_ptr<UltraCanvasTextArea> view = logView;
        app->StartTimer(60, false, [view](TimerId) {
            if (auto v = view.lock()) {
                v->MoveCursorToEnd();
                v->EnsureCursorVisible();
                v->RequestRedraw();
            }
        });
    }
}

void UltraFilerConnectionLogWindow::SetTabStripStyle(FilerTabStripStyle style) {
    if (tabs) ApplyFilerTabStripStyle(*tabs, style);
}

std::shared_ptr<UltraFilerConnectionLogWindow> ShowConnectionLogWindow(
        const std::vector<RemoteLogSession>& sessions, UltraCanvasWindowBase* parent) {
    auto window = std::make_shared<UltraFilerConnectionLogWindow>();
    window->Create();
    bool anyError = false;
    for (const RemoteLogSession& s : sessions)
        if (s.failed) { anyError = true; break; }
    window->ShowTab(anyError ? UltraFilerConnectionLogWindow::ErrorsTab
                             : UltraFilerConnectionLogWindow::MessageLogTab);
    window->Update(sessions);
    if (parent) {
        window->SetTransientParent(parent);
        window->CenterOnParent(parent);
    }
    window->Show();
    return window;
}

} // namespace UltraCanvas
