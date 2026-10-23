// Apps/DemoApp/UltraCanvasNotificationToastExamples.cpp
// Demonstration of UltraCanvasNotificationToast - how ULTRA OS shows a
// notification: the toast element itself, laid out on the page, and real
// toasts popped at the corner of the screen by an
// UltraCanvasNotificationToastHost (the one the ULTRA OS desktop runs on the
// UltraMessage bus; fed directly here, so the page needs no bus). What the
// toasts' buttons and close boxes do is echoed into the status line.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): a card for the
//                  element, the screen toasts and the notes, buttons and status
//                  sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
#include "Plugins/UltraMessage/UltraCanvasNotificationToast.h"
#endif

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
namespace {

struct DemoNotification {
    std::string appName;
    std::string summary;
    std::string body;
    std::string urgency = "normal";
    std::vector<UltraMessage::NotificationAction> actions;
    std::string icon;
};

DemoNotification MailSample() {
    return {"UltraMail", "New mail from Grace Hopper", "Found the moth. Literally.\nto erika@example.org", "normal",
            {{"default", "Open"}}, NormalizePath(GetResourcesDir() + "media/appicon/UltraMail.png")};
}

DemoNotification ChatSample() {
    return {"Telegram", "Ada Lovelace", "Are we still on for the engine review at 9?", "normal",
            {{"default", "Open"}, {"reply", "Reply"}}, ""};
}

DemoNotification CriticalSample() {
    return {"Power", "Battery low", "5 % left - plug in now. A critical notification stays until it is closed.",
            "critical", {}, ""};
}

NotificationToastContent ContentOf(const DemoNotification& n, const std::string& id) {
    NotificationToastContent c;
    c.notificationId = id;
    c.appName = n.appName;
    c.summary = n.summary;
    c.body = n.body;
    c.urgency = n.urgency;
    c.actions = n.actions;
    c.iconPath = n.icon;
    return c;
}

// The message the toast host would receive from the bus.
UltraMsgMessage MessageOf(const DemoNotification& n, const std::string& id) {
    UltraMessage::SystemNotification body;
    body.appName = n.appName;
    body.summary = n.summary;
    body.body = n.body;
    body.urgency = n.urgency;
    body.actions = n.actions;
    body.icon = n.icon;
    UltraMsgMessage m;
    m.envelope.id = id;
    m.envelope.topic = UltraMsgTopics::SystemNotification;
    m.body = UltraMessage::MakeSystemNotification(body);
    return m;
}

// What the page keeps alive between clicks: the host and its counters.
struct ToastDemoState {
    UltraCanvasNotificationToastHost host;
    int serial = 0;
    int progress = -1;   // the download's percentage; -1 while none runs

    std::string NextId() { return "demo-toast-" + std::to_string(++serial); }
};

// One step of the download: the same id each time, so the toast updates in
// place; a one-shot timer schedules the next.
void StepDownload(const std::shared_ptr<ToastDemoState>& state) {
    if (state->progress < 0) return;
    DemoNotification n{"Downloads", "Downloading ultracanvas-src.tar.gz",
                       std::to_string(state->progress) + " % of 48 MB", "low", {}, ""};
    if (state->progress >= 100) {
        n = {"Downloads", "Download finished", "ultracanvas-src.tar.gz (48 MB)", "normal",
             {{"default", "Open"}, {"folder", "Show in Filer"}}, ""};
        state->progress = -1;
    } else {
        state->progress += 10;
    }
    state->host.Ingest(MessageOf(n, "demo-download"));
    if (state->progress < 0) return;
    if (auto* app = UltraCanvasApplicationBase::GetCurrent())
        app->StartTimer(350, /*periodic=*/false, [state](TimerId) { StepDownload(state); });
}

} // namespace
#endif // ULTRACANVAS_HAS_MESSAGECENTER

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateNotificationToastExamples() {
#ifdef ULTRACANVAS_HAS_MESSAGECENTER
    const std::string subtitleText =
        "How ULTRA OS shows a notification. Every application's notification - a chat, new mail, "
        "a finished download - arrives on the UltraMessage bus, and the ULTRA OS desktop draws it "
        "as a toast in the top-right corner. A toast never blocks and never takes the keyboard "
        "focus; it goes by itself after a few seconds, or when it is clicked or closed.";
#else
    const std::string subtitleText =
        "This build has no UltraMessage module (ULTRACANVAS_ENABLE_ULTRAMESSAGE needs UltraDatabase), "
        "so the notification toast is not available.";
#endif
    auto container = DemoKit::CreatePage("ToastExamples", "Notification Toast", subtitleText);

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
    // ----- the element, on the page -----
    auto elementCard = DemoKit::AddCard(container, "ToastElementCaption",
                                        "The element: UltraCanvasNotificationToast");
    // Both cards report here; it sits under the buttons.
    auto status = DemoKit::CreateText("ToastStatus", "");

    // The previews wrap under each other when the window is narrow.
    auto previews = DemoKit::AddRow(elementCard, "ToastPreviews", 16);
    NotificationToastStyle previewStyle;
    previewStyle.width = 300;
    int index = 0;
    for (const DemoNotification& n : {MailSample(), ChatSample(), CriticalSample()}) {
        auto toast = CreateNotificationToast("ToastPreview" + std::to_string(index++), 0, 0, 300, 0);
        toast->SetStyle(previewStyle);
        toast->SetContent(ContentOf(n, "preview"));
        const std::string app = n.appName;
        toast->onAction = [status = status.get(), app](const std::string& action) {
            status->SetText("Preview: \"" + action + "\" on " + app + "'s toast - on the bus this is a "
                            "system.notification.action");
        };
        toast->onClose = [status = status.get(), app]() {
            status->SetText("Preview: " + app + "'s toast closed - on the bus this is a "
                            "system.notification.dismissed");
        };
        DemoKit::AddFixed(previews, toast);
    }

    // ----- real toasts at the corner of the screen -----
    auto screenCard = DemoKit::AddCard(container, "ToastScreenCaption",
                                       "On the screen: UltraCanvasNotificationToastHost");
    auto state = std::make_shared<ToastDemoState>();
    state->host.onAction = [status = status.get()](const NotificationToastContent& c, const std::string& action) {
        status->SetText("\"" + action + "\" on \"" + c.summary + "\" (" + c.appName +
                        ") - the application is told through system.notification.action");
    };
    state->host.onDismissed = [status = status.get()](const NotificationToastContent& c) {
        status->SetText("Closed \"" + c.summary + "\" (" + c.appName +
                        ") - the application is told through system.notification.dismissed");
    };

    auto buttons = DemoKit::AddRow(screenCard, "ToastButtons", 8);
    // As wide as its text, at least the width it had.
    auto addButton = [&buttons](const std::string& id, const std::string& text, float width) {
        auto button = std::make_shared<UltraCanvasButton>(id, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(width);
        limits.minHeight = CSSLayout::Dimension::Px(28);
        button->boxConstraints = limits;
        DemoKit::Add(buttons, button);
        return button;
    };
    addButton("ToastMailBtn", "New mail", 100)->onClick = [state]() {
        state->host.Ingest(MessageOf(MailSample(), state->NextId()));
    };
    addButton("ToastChatBtn", "Chat with Reply", 130)->onClick = [state]() {
        state->host.Ingest(MessageOf(ChatSample(), state->NextId()));
    };
    addButton("ToastCriticalBtn", "Critical (stays)", 130)->onClick = [state]() {
        state->host.Ingest(MessageOf(CriticalSample(), state->NextId()));
    };
    addButton("ToastDownloadBtn", "Download progress", 180)->onClick = [state]() {
        if (state->progress >= 0) return;   // one at a time
        state->progress = 0;
        StepDownload(state);
    };
    addButton("ToastManyBtn", "Six at once", 110)->onClick = [state]() {
        // Four at most: the oldest give way to the newest.
        for (int i = 1; i <= 6; ++i) {
            DemoNotification n{"Demo", "Notification " + std::to_string(i) + " of 6",
                               "Only the newest four stay on screen.", "normal", {}, ""};
            state->host.Ingest(MessageOf(n, state->NextId()));
        }
    };
    addButton("ToastClearBtn", "Clear", 80)->onClick = [state]() {
        for (const auto& toast : state->host.GetToasts()) state->host.Withdraw(toast.notificationId);
    };
    // The state lives as long as the page: the buttons above hold it.

    status->SetText("Status: (press a button, then click a toast, its buttons or its close box)");
    status->SetBackgroundColor(Color(240, 240, 240));
    status->SetPadding(4);
    status->SetMargin(4, 0, 0, 0);
    DemoKit::Add(screenCard, status);

    // ----- toast or alert -----
    auto notesCard = DemoKit::AddCard(container, "ToastNotes", "Toast or Alert?");
    DemoKit::AddText(notesCard, "ToastNotesText",
        "An Alert (the Alert / Message Box page) is for what must be answered before the user goes on: it is "
        "modal, centred on its window and keeps the keyboard focus until a button is pressed. A toast is for "
        "what the user should see but need not answer now: it sits in a corner, never takes the focus, and "
        "goes by itself - low 5 s, normal 8 s, critical stays; the pointer resting on it holds it.\n\n"
        "From an application, post a system.notification on UltraMessage:\n"
        "  UltraMessage::SystemNotification n;  n.appId = \"org.example.myapp\";  n.appName = \"MyApp\";\n"
        "  n.summary = \"Export finished\";  n.actions = {{\"default\", \"Open\"}};  n.launch = \"file=report.pdf\";\n"
        "  UltraMsg_Post(endpoint, UltraMsgTopics::SystemNotification, UltraMessage::MakeSystemNotification(n));\n"
        "The desktop's notification service shows it - on ULTRA OS the desktop's toast host, as here; on GNOME, "
        "Plasma or Windows their own - and a click comes back as system.notification.action.\n"
        "Register for the clicks once at the start, so one on a Windows toast reaches the application even after "
        "the bus is gone (UltraCanvasNotificationActivation.h):\n"
        "  UltraCanvas::RegisterNotificationActivator(\"org.example.myapp\", [](const auto& click) { Open(click.launch); });\n\n"
        "A desktop shell runs the host itself: UltraCanvasNotificationToastHost toasts; "
        "toasts.SetScreenMargins(0, 0, barWidth, 0); toasts.Connect();");
#endif
    return container;
}

} // namespace UltraCanvas
