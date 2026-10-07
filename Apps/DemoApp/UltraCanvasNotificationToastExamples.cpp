// Apps/DemoApp/UltraCanvasNotificationToastExamples.cpp
// Demonstration of UltraCanvasNotificationToast - how ULTRA OS shows a
// notification: the toast element itself, laid out on the page, and real
// toasts popped at the corner of the screen by an
// UltraCanvasNotificationToastHost (the one the ULTRA OS desktop runs on the
// UltraMessage bus; fed directly here, so the page needs no bus). What the
// toasts' buttons and close boxes do is echoed into the status line.
// Version: 1.0.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasDemo.h"
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

std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id, const std::string& text) {
    auto label = CreateLabel(id, 0, 0, 0, 24);
    label->SetText(text);
    label->SetFontSize(13);
    label->SetFontWeight(FontWeight::Bold);
    label->SetMargin(10, 0, 4, 0);
    label->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return label;
}

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
    auto container = std::make_shared<UltraCanvasContainer>("ToastExamples", 0, 0, 1000, 700);
    container->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Start);
    container->SetPadding(10, 20);

    auto title = CreateLabel("ToastTitle", 0, 0, 0, 30);
    title->SetText("Notification Toast");
    title->SetFontSize(18);
    title->SetFontWeight(FontWeight::Bold);
    title->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    container->AddChild(title);

    auto subtitle = CreateLabel("ToastSubtitle", 0, 0, 940, 0);
    subtitle->SetWrap(TextWrap::WrapWord);
    subtitle->SetFontSize(12);
    subtitle->size.height = CSSLayout::Dimension::Auto();
    subtitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(subtitle);

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
    subtitle->SetText("How ULTRA OS shows a notification. Every application's notification - a chat, new mail, "
                      "a finished download - arrives on the UltraMessage bus, and the ULTRA OS desktop draws it "
                      "as a toast in the top-right corner. A toast never blocks and never takes the keyboard "
                      "focus; it goes by itself after a few seconds, or when it is clicked or closed.");

    // ----- the element, on the page -----
    container->AddChild(Caption("ToastElementCaption", "The element: UltraCanvasNotificationToast"));
    auto status = CreateLabel("ToastStatus", 0, 0, 940, 26);

    auto previews = CreateContainer("ToastPreviews", 0, 0, 0, 0);
    previews->layout.SetFlexRow().SetFlexGap(16).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    previews->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    NotificationToastStyle previewStyle;
    previewStyle.width = 300;
    int index = 0;
    for (const DemoNotification& n : {MailSample(), ChatSample(), CriticalSample()}) {
        auto toast = CreateNotificationToast("ToastPreview" + std::to_string(index++), 0, 0, 300, 0);
        toast->SetStyle(previewStyle);
        toast->SetContent(ContentOf(n, "preview"));
        toast->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        const std::string app = n.appName;
        toast->onAction = [status = status.get(), app](const std::string& action) {
            status->SetText("Preview: \"" + action + "\" on " + app + "'s toast - on the bus this is a "
                            "system.notification.action");
        };
        toast->onClose = [status = status.get(), app]() {
            status->SetText("Preview: " + app + "'s toast closed - on the bus this is a "
                            "system.notification.dismissed");
        };
        previews->AddChild(toast);
    }
    container->AddChild(previews);

    // ----- real toasts at the corner of the screen -----
    container->AddChild(Caption("ToastScreenCaption", "On the screen: UltraCanvasNotificationToastHost"));
    auto state = std::make_shared<ToastDemoState>();
    state->host.onAction = [status = status.get()](const NotificationToastContent& c, const std::string& action) {
        status->SetText("\"" + action + "\" on \"" + c.summary + "\" (" + c.appName +
                        ") - the application is told through system.notification.action");
    };
    state->host.onDismissed = [status = status.get()](const NotificationToastContent& c) {
        status->SetText("Closed \"" + c.summary + "\" (" + c.appName +
                        ") - the application is told through system.notification.dismissed");
    };

    auto buttons = CreateContainer("ToastButtons", 0, 0, 0, 0);
    buttons->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto addButton = [&buttons](const std::string& id, const std::string& text, float width) {
        auto button = CreateButton(id, 0, 0, width, 28, text);
        button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        buttons->AddChild(button);
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
    container->AddChild(buttons);
    // The state lives as long as the page: the buttons above hold it.

    status->SetText("Status: (press a button, then click a toast, its buttons or its close box)");
    status->SetFontSize(12);
    status->SetBackgroundColor(Color(240, 240, 240));
    status->SetPadding(4);
    status->SetMargin(10, 0, 0, 0);
    status->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(status);

    // ----- toast or alert -----
    auto notes = CreateLabel("ToastNotes", 0, 0, 940, 0);
    notes->SetWrap(TextWrap::WrapWord);
    notes->size.height = CSSLayout::Dimension::Auto();
    notes->SetText(
        "Toast or Alert?\n"
        "An Alert (the Alert / Message Box page) is for what must be answered before the user goes on: it is "
        "modal, centred on its window and keeps the keyboard focus until a button is pressed. A toast is for "
        "what the user should see but need not answer now: it sits in a corner, never takes the focus, and "
        "goes by itself - low 5 s, normal 8 s, critical stays; the pointer resting on it holds it.\n\n"
        "From an application, post a system.notification on UltraMessage:\n"
        "  UltraMessage::SystemNotification n;  n.appName = \"MyApp\";\n"
        "  n.summary = \"Export finished\";  n.actions = {{\"default\", \"Open\"}};\n"
        "  UltraMsg_Post(endpoint, UltraMsgTopics::SystemNotification, UltraMessage::MakeSystemNotification(n));\n"
        "The desktop's notification service shows it - on ULTRA OS the desktop's toast host, as here; on GNOME, "
        "Plasma or Windows their own - and a click comes back as system.notification.action.\n\n"
        "A desktop shell runs the host itself: UltraCanvasNotificationToastHost toasts; "
        "toasts.SetScreenMargins(0, 0, barWidth, 0); toasts.Connect();");
    notes->SetFontSize(11);
    notes->SetBackgroundColor(Color(255, 255, 240));
    notes->SetPadding(8);
    notes->SetMargin(12, 0, 0, 0);
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(notes);
#else
    subtitle->SetText("This build has no UltraMessage module (ULTRACANVAS_ENABLE_ULTRAMESSAGE needs UltraDatabase), "
                      "so the notification toast is not available.");
#endif
    return container;
}

} // namespace UltraCanvas
