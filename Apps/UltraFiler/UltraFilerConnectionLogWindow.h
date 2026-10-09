// Apps/UltraFiler/UltraFilerConnectionLogWindow.h
// The connection log window, opened from the round network button in the
// bottom-left corner of a folder display on a remote drive - FTP / SFTP or a
// cloud drive: what the remote drives did in this session, in two tabs.
//
//   Errors       the failed connections as a Markdown report, newest first -
//                the message, the error class, the codes (an FTP server's
//                last reply or a cloud service's HTTP status, libcurl's
//                error), the likely cause, the last steps and the
//                diagnostics chain (RenderRemoteErrorsMarkdown).
//   Message log  every step of every connection, the way an FTP client's
//                message log shows them - a cloud drive's requests and the
//                service's answers the same way - following the newest line
//                (RenderRemoteLogText).
//
// Both are read-only UltraCanvasTextAreas, so they scroll, select and copy;
// Copy puts the tab on show on the clipboard whole - what to paste into a
// mail to whoever runs the server. Not modal: it stays open beside the main
// window and follows the log while connections run.
//
// The window keeps a copy of what it shows rather than a pointer into the
// log, so it never outlives what it reads.
// Version: 1.0.2
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerConnectionLog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraFilerConnectionLogWindow : public UltraCanvasWindow {
public:
    enum Tab { ErrorsTab = 0, MessageLogTab = 1 };

    UltraFilerConnectionLogWindow();

    // Shows `sessions` - the whole log. Cheap to call often: a tab whose text
    // did not change is not touched, and the tab not on show is rendered
    // when it is brought up.
    void Update(const std::vector<RemoteLogSession>& sessions);

    void ShowTab(Tab tab);

    // Clear was pressed: the owner empties the log and calls Update again.
    std::function<void()> onClear;

private:
    void RenderActiveTab();

    std::shared_ptr<UltraCanvasTabbedContainer> tabs;
    std::shared_ptr<UltraCanvasTextArea> errorsView;
    std::shared_ptr<UltraCanvasTextArea> logView;
    std::shared_ptr<UltraCanvasLabel> summaryLabel;
    std::shared_ptr<UltraCanvasButton> copyButton;
    std::shared_ptr<UltraCanvasButton> clearButton;
    std::shared_ptr<UltraCanvasButton> closeButton;

    // What each tab should show, and what it shows now.
    std::string errorsText, shownErrorsText;
    std::string logText, shownLogText;
};

// Creates, shows and returns the window, on the Errors tab when there is an
// error to read and on the message log otherwise, parented to `parent` so
// the window manager keeps it above the window that opened it.
std::shared_ptr<UltraFilerConnectionLogWindow> ShowConnectionLogWindow(
        const std::vector<RemoteLogSession>& sessions, UltraCanvasWindowBase* parent);

} // namespace UltraCanvas
