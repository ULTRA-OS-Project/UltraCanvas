// Apps/DemoApp/UltraCanvasMessageCenterExamples.cpp
// Demonstration of UltraCanvasMessageCenter: the UltraMessage feed as one
// element. The page hosts its own broker on a private bus path with an
// in-memory journal, so it never touches the user's message centre, seeds a
// few chats, mails and notifications through a second endpoint, and lets the
// visitor post more with a button — the element updates live.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasDemo.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
#  include "Plugins/UltraMessage/UltraCanvasMessageCenter.h"
#  include "UltraMessage/UltraMessageEndpoint.h"
#  include <chrono>
#  include <cstdlib>
#  include <memory>
#  include <string>
#endif

namespace UltraCanvas {

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
namespace {

std::string DemoBusPath() {
#ifdef _WIN32
    return "\\\\.\\pipe\\UltraCanvasDemo-MessageCenter";
#else
    const char* tmp = std::getenv("TMPDIR");
    std::string base = tmp && *tmp ? tmp : "/tmp";
    return base + "/ultracanvas-demo-messagecenter/bus.sock";
#endif
}

// A second application on the demo bus that produces the sample traffic.
struct DemoSource {
    UltraMsgHandle endpoint = UltraMsgInvalidHandle;
    int counter = 0;

    bool Connect() {
        if (endpoint != UltraMsgInvalidHandle) return true;
        UltraMsgConnectOptions options;
        options.appId = "org.ultraos.demo.messagesource";
        options.displayName = "Demo source";
        options.busPath = DemoBusPath();
        options.journalPath = ":memory:";
        options.deliverOnUIThread = false;
        endpoint = UltraMsg_Connect(options);
        return endpoint != UltraMsgInvalidHandle;
    }
    ~DemoSource() {
        if (endpoint != UltraMsgInvalidHandle) UltraMsg_Disconnect(endpoint);
    }

    void Chat(const std::string& service, const std::string& conversation, const std::string& sender,
              const std::string& text, bool group = false) {
        UltraMessage::MessagingMessage m;
        m.service = service;
        m.account = "me";
        m.conversationId = conversation;
        m.conversationTitle = conversation;
        m.isGroup = group;
        m.sender = {sender, sender, ""};
        m.text = text;
        UltraMsgSendOptions options;
        options.conversation = UltraMessage::ConversationKey(m);
        UltraMsg_Post(endpoint, UltraMsgTopics::MessagingMessage, UltraMessage::MakeMessagingMessage(m), options);
    }
    void Mail(const std::string& account, const std::string& fromName, const std::string& fromAddr,
              const std::string& subject, const std::string& snippet) {
        UltraMessage::MailMessage m;
        m.account = account;
        m.folder = "INBOX";
        m.from = {fromName, fromAddr};
        m.subject = subject;
        m.snippet = snippet;
        UltraMsg_Post(endpoint, UltraMsgTopics::MailMessage, UltraMessage::MakeMailMessage(m));
    }
    void Notification(const std::string& appName, const std::string& appId, const std::string& summary,
                      const std::string& body, const std::string& urgency = "normal",
                      std::vector<UltraMessage::NotificationAction> actions = {}) {
        UltraMessage::SystemNotification n;
        n.appName = appName;
        n.appId = appId;
        n.summary = summary;
        n.body = body;
        n.urgency = urgency;
        n.actions = std::move(actions);
        UltraMsgSendOptions options;
        if (urgency == "critical") options.flags |= UltraMsgFlag_Urgent;
        UltraMsg_Post(endpoint, UltraMsgTopics::SystemNotification, UltraMessage::MakeSystemNotification(n), options);
    }

    void Seed() {
        Chat("telegram", "Ada Lovelace", "Ada Lovelace", "Are we still on for the engine review at 9?");
        Chat("telegram", "Analytical Engine", "Charles Babbage", "The mill is finished; the store needs another week.", true);
        Chat("signal", "Grace Hopper", "Grace Hopper", "Found the moth. Literally.");
        Mail("erika@example.org", "Konrad Zuse", "konrad@example.org", "Z3 relay order",
             "The relays arrive Thursday; can we start assembly on Friday?");
        Mail("erika@example.org", "RISC OS Open", "news@riscosopen.org", "Newsletter: September",
             "This month: the new Filer, UltraCanvas on the Pi 5, and a bundle of bug fixes.");
        Notification("Downloads", "org.ultraos.filer", "Download finished", "ultracanvas-src.tar.gz (48 MB)", "low",
                     {{"open", "Open"}, {"folder", "Show in Filer"}});
        Notification("Battery", "org.ultraos.power", "Battery low", "12 % remaining — plug in soon", "critical");
    }

    void PostAnother() {
        ++counter;
        switch (counter % 3) {
            case 0: Chat("telegram", "Ada Lovelace", "Ada Lovelace", "Message " + std::to_string(counter) + " from the demo"); break;
            case 1: Mail("erika@example.org", "Demo", "demo@example.org", "Sample mail " + std::to_string(counter),
                         "Posted from the DemoApp page."); break;
            default: Notification("Demo", "org.ultraos.demo", "Sample notification " + std::to_string(counter),
                                  "Posted from the DemoApp page.", "normal", {{"ok", "OK"}}); break;
        }
    }
};

} // namespace
#endif // ULTRACANVAS_HAS_MESSAGECENTER

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMessageCenterExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("MessageCenterExamples", 0, 0, 1000, 640);
    container->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    container->SetPadding(10, 20);

    auto title = CreateLabel("MCTitle", 0, 0, 0, 30);
    title->SetText("Message Centre");
    title->SetFontSize(18);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);
    title->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto subtitle = CreateLabel("MCSubtitle", 0, 0, 0, 40);
    subtitle->SetWrap(TextWrap::WrapWord);
    subtitle->SetFontSize(12);
    container->AddChild(subtitle);
    subtitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

#ifdef ULTRACANVAS_HAS_MESSAGECENTER
    subtitle->SetText("One element on the UltraMessage feed: chats, mail and notifications, grouped by source, "
                      "filtered by section and service, searchable, with the notification's own actions. "
                      "This page hosts a private broker with an in-memory journal and seeds it; "
                      "\"Post another\" adds a message live.");

    auto bar = CreateContainer("MCBar", 0, 0, 0, 0);
    bar->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto post = CreateButton("MCPost", 0, 0, 130, 28, "Post another");
    post->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->AddChild(post);
    auto gap = CreateContainer("MCGap", 0, 0, 10, 1);
    gap->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->AddChild(gap);
    auto readout = CreateLabel("MCReadout", 0, 0, 600, 28);
    readout->SetFontSize(12);
    readout->SetText("Unread: 0");
    bar->AddChild(readout);
    container->AddChild(bar);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto center = CreateMessageCenter("MCCenter", 0, 0, 0, 0);
    center->onUnreadCountChanged = [readout = readout.get()](int unread) {
        readout->SetText("Unread: " + std::to_string(unread));
    };
    center->onOpen = [readout = readout.get()](const MessageCenterEntry& entry) {
        readout->SetText("Open: " + entry.title + " — " + entry.snippet);
    };
    container->AddChild(center);
    center->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // The demo's own bus: the element hosts the broker, the source posts to it.
    UltraMsgConnectOptions options = UltraCanvasMessageCenter::DefaultConnectOptions();
    options.busPath = DemoBusPath();
    options.journalPath = ":memory:";
    auto source = std::make_shared<DemoSource>();
    if (center->Connect(options) && source->Connect()) {
        source->Seed();
        center->Refresh();
    }
    post->onClick = [source, center = center.get()]() {
        if (source->Connect()) source->PostAnother();
        else center->GetStatusLabel()->SetText("The demo source could not connect to the bus.");
    };
    // The source lives as long as the page: captured by the button above.
#else
    subtitle->SetText("This build has no UltraMessage module (ULTRACANVAS_ENABLE_ULTRAMESSAGE needs UltraDatabase), "
                      "so the message centre element is not available.");
#endif
    return container;
}

} // namespace UltraCanvas
