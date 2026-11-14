// Apps/UltraNotifications/ui/UltraNotificationsWindow.cpp
// UltraNotifications' main window. See the header.
// Version: 0.1.1 - the look from the theme (UltraCanvasTheme roles): the window, the
//                  footer and the message centre's colours follow a dark look
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNotificationsWindow.h"

#include "Plugins/UltraMessage/UltraCanvasMessageCenter.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <string>

#ifndef ULTRANOTIFICATIONS_VERSION
#error "ULTRANOTIFICATIONS_VERSION must come from Docs/UltraNotifications/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace UltraNotifications {

namespace {
// The desktop notice the bell reads (UltraCanvasDesktopShell::PublishNotice).
const char* const kNoticeName = "UltraNotifications";
const char* const kAppId = "org.ultraos.ultranotifications";
} // namespace

UltraNotificationsWindow::UltraNotificationsWindow(UltraCanvasApplication& app) : app_(app) {}

UltraNotificationsWindow::~UltraNotificationsWindow() {
    if (themeHandler_) UltraCanvasTheme::RemoveChangeHandler(themeHandler_);
    // The count goes with the program: a bell must not show what nobody
    // keeps counting.
    UltraCanvasDesktopShell::RemoveNotice(kNoticeName);
    if (center_) {
        center_->onUnreadCountChanged = nullptr;
        center_->Disconnect();
    }
}

bool UltraNotificationsWindow::Create() {
    WindowConfig config;
    config.title = std::string("UltraNotifications ") + ULTRANOTIFICATIONS_VERSION;
    config.width = 1000;
    config.height = 680;
    config.minWidth = 640;
    config.minHeight = 400;
    config.backgroundColor = UltraCanvasTheme::Current().roles.ground;
    window_ = CreateWindow(config);
    if (!window_) return false;
    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    center_ = CreateMessageCenter("notifications.center");
    center_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    center_->onOpen = [this](const MessageCenterEntry& entry) { Open(entry); };
    center_->onUnreadCountChanged = [this](int unread) { UnreadChanged(unread); };
    window->AddChild(center_);

    // Quit, while closing only hides the window (SetKeepRunning).
    footer_ = std::make_shared<UltraCanvasContainer>("notifications.footer");
    footer_->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    footer_->layoutItem.SetFlexShrink(0.0f);
    footer_->SetPadding(6, 12, 6, 12);
    footer_->AddStretchSpacer(1.0f);
    quit_ = CreateButton("notifications.quit", 0, 0, 90, 28, Tr("Quit"));
    quit_->SetTooltip(Tr("Stop UltraNotifications running in the background (Ctrl+Q)"));
    quit_->onClick = [this]() { Quit(); };
    footer_->AddChild(quit_);
    footer_->SetVisible(keepRunning_);
    window->AddChild(footer_);
    ApplyLook();
    themeHandler_ = UltraCanvasTheme::AddChangeHandler([this](const UltraCanvasTheme&) { ApplyLook(); });

    window->InstallEventFilter("ultranotifications-keys", [this](const UCEvent& event) { return OnKey(event); },
                               {UCEventType::KeyDown});
    window->onWindowClosing = [this]() {
        if (keepRunning_) {
            // In the background: the window goes, the count stays.
            window_->Hide();
            shown_ = false;
            return false;
        }
        app_.RequestExit();
        return true;
    };

    UltraMsgConnectOptions options = UltraCanvasMessageCenter::DefaultConnectOptions();
    options.appId = kAppId;
    options.displayName = "UltraNotifications";
    // Not connected (no bus could be reached or started): the centre says
    // why in its status line, and the window still opens.
    center_->Connect(options);
    return true;
}

void UltraNotificationsWindow::Show() {
    if (!window_) return;
    window_->Show();
    shown_ = true;
}

void UltraNotificationsWindow::Present() {
    if (!window_) return;
    if (!shown_) {
        center_->Refresh();
        Show();
    }
    if (window_->IsMinimized()) window_->Restore();
    window_->RaiseAndFocus();
}

void UltraNotificationsWindow::SetKeepRunning(bool keep) {
    keepRunning_ = keep;
    if (footer_) footer_->SetVisible(keep);
}

void UltraNotificationsWindow::Open(const MessageCenterEntry& entry) {
    // The centre has marked the row read already. A mail opens the mail
    // program, a chat the messenger, a notification does what it offers.
    switch (entry.section) {
        case MessageCenterSection::Mail:
            UltraCanvasDesktopShell::LaunchProgram("UltraMail");
            return;
        case MessageCenterSection::Chats:
            if (entry.service == "chatcube" || entry.service == "org.ultraos.chatcube") {
                UltraCanvasDesktopShell::LaunchProgram("ChatCube");
                return;
            }
            break;
        default:
            break;
    }
    if (!entry.actions.empty()) center_->InvokeAction(entry.message.envelope.id, entry.actions.front().id);
}

void UltraNotificationsWindow::UnreadChanged(int unread) {
    if (unread == unread_) return;
    unread_ = unread;
    std::string title = std::string("UltraNotifications ") + ULTRANOTIFICATIONS_VERSION;
    if (unread > 0) title = "(" + std::to_string(unread) + ") " + title;
    if (window_) window_->SetWindowTitle(title);
    if (unread > 0) {
        UltraCanvasDesktopShell::PublishNotice(kNoticeName, unread,
                                               unread == 1 ? Tr("1 unread notification")
                                                           : TrF("{0} unread notifications", unread));
    } else {
        UltraCanvasDesktopShell::PublishNotice(kNoticeName, 0, Tr("No unread notifications"));
    }
}

bool UltraNotificationsWindow::OnKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown || !event.ctrl) return false;
    const bool q = event.character == 'q' || event.character == 'Q' || event.virtualKey == UCKeys::Q;
    if (!q) return false;
    Quit();
    return true;
}

// The message centre's own colours are a light set unless given: the theme's
// roles over its shape (sizes, what it shows) - the panel for the list and
// the detail, the well for the sources, the soft accent for the chosen row,
// the hover fill, ink and muted ink, the accent and danger for the marks.
void UltraNotificationsWindow::ApplyLook() {
    const UltraCanvasTheme& theme = UltraCanvasTheme::Current();
    const ThemeRoles& r = theme.roles;
    if (window_) window_->SetBackgroundColor(r.ground);
    if (footer_) {
        footer_->SetBackgroundColor(r.ground);
        footer_->SetBorderTop(1, r.line);
    }
    if (center_) {
        MessageCenterStyle style = center_->GetStyle();
        style.background = r.panel;
        style.sidebar = r.well;
        style.rowSelected = r.accentSoft;
        style.rowHover = r.hover;
        style.textPrimary = r.ink;
        style.textSecondary = r.inkMuted;
        style.unreadMark = r.accent;
        style.urgentMark = r.danger;
        center_->SetStyle(style);
        // The chosen source sits on the soft accent: ink on it, not the
        // text for the full accent the tree takes from the theme.
        if (auto sources = center_->GetSourcesTree()) sources->SetSelectionTextColor(r.ink);
    }
    if (window_) window_->RequestRedraw();
}

void UltraNotificationsWindow::Quit() {
    app_.RequestExit();
}

} // namespace UltraNotifications
