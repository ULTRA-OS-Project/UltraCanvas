// Apps/UltraMail/ui/UltraMailSingleInstance.cpp
// One UltraMail per user: the lock, and the hand-off to the copy already
// running. See UltraMailSingleInstance.h.
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailSingleInstance.h"
#include "UltraMailTrace.h"

#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageUltraCanvas.h"

#include <chrono>
#include <thread>
#include <vector>
#endif

namespace UltraMail {

#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
namespace {

// Its own app id: the feed publisher's endpoint is "org.ultraos.ultramail",
// and it does not answer "show".
constexpr const char* kAppId = "org.ultraos.ultramail.instance";
constexpr const char* kShowTopic = "org.ultraos.ultramail.show";
// The running copy answers from its UI thread, before it acts.
constexpr int kAnswerTimeoutMs = 3000;
// A copy started a moment earlier may still be opening its mailbox and not
// listen yet; this start keeps asking for this long.
constexpr int kStartingGraceMs = 10000;
constexpr int kRetryPauseMs = 250;

void RegisterShowTopic() {
    static const bool registered = [] {
        UltraMsgTopicSchema schema;
        schema.topic = kShowTopic;
        schema.description = "Bring the running UltraMail's window forward";
        return static_cast<bool>(UltraMsg_RegisterSchema(schema));
    }();
    (void)registered;
}

UltraMsgHandle Connect(bool hostBus) {
    RegisterShowTopic();
    UltraMsgConnectOptions options;
    options.appId = kAppId;
    options.displayName = "UltraMail";
    options.startBrokerIfAbsent = hostBus;
    UltraMsgResult error;
    const UltraMsgHandle endpoint = UltraMsg_Connect(options, &error);
    if (endpoint == UltraMsgInvalidHandle)
        Trace::Line("Single instance: no UltraMessage bus (" + error.message + ")");
    return endpoint;
}

} // namespace

bool SingleInstance::HandOff() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartingGraceMs);
    do {
        // The running copy hosts the bus once it listens, so this start never
        // hosts one of its own.
        if (endpoint_ == 0) {
            const UltraMsgHandle endpoint = Connect(false);
            if (endpoint != UltraMsgInvalidHandle) endpoint_ = endpoint;
        }
        if (endpoint_ != 0) {
            std::vector<std::string> instances;
            if (UltraMsg_ResolveApp(endpoint_, kAppId, instances)) {
                UltraMsgEndpointInfo self;
                UltraMsg_GetEndpointInfo(endpoint_, self);
                for (const std::string& instance : instances) {
                    if (instance == self.instanceId) continue;
                    UltraMsgMessage reply;
                    if (UltraMsg_Request(endpoint_, instance, kShowTopic,
                                         UltraCanvas::JSONValue::MakeObject(), kAnswerTimeoutMs, reply))
                        return true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryPauseMs));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void SingleInstance::Listen(ShowHandler handler) {
    if (subscription_ != 0 || !lock_.IsHeld()) return;
    if (endpoint_ == 0) {
        // Hosts the bus when nothing else does, so the next start finds it.
        const UltraMsgHandle endpoint = Connect(true);
        if (endpoint == UltraMsgInvalidHandle) return;
        endpoint_ = endpoint;
    }
    handler_ = std::move(handler);
    if (!UltraMsg_HasUIDispatcher()) UltraMsg_UseUltraCanvasApplication();
    const UltraMsgHandle endpoint = endpoint_;
    const UltraMsgHandle subscription = UltraMsg_Subscribe(endpoint, kShowTopic,
        [this, endpoint](const UltraMsgMessage& message) {
            // Answer first: the other start is waiting to quit.
            UltraMsg_Reply(endpoint, message, UltraCanvas::JSONValue::MakeObject());
            Trace::Line("Single instance: another start asked for the window; bringing it forward");
            if (handler_) handler_();
        });
    if (subscription != UltraMsgInvalidHandle) subscription_ = subscription;
}

void SingleInstance::Close() {
    if (subscription_ != 0) UltraMsg_Unsubscribe(subscription_);
    subscription_ = 0;
    if (endpoint_ != 0) UltraMsg_Disconnect(endpoint_);
    endpoint_ = 0;
    handler_ = nullptr;
}
#else
bool SingleInstance::HandOff() { return false; }
void SingleInstance::Listen(ShowHandler) {}
void SingleInstance::Close() {}
#endif

SingleInstance::Start SingleInstance::Claim(const std::string& dataDir) {
    std::string error;
    switch (lock_.Acquire(dataDir + "/instance.lock", &error)) {
        case UltraCanvas::InstanceLockResult::Acquired:
            return Start::First;
        case UltraCanvas::InstanceLockResult::Unsupported:
            return Start::First;
        case UltraCanvas::InstanceLockResult::Failed:
            // Better two copies than none: an unwritable lock file must not
            // stop the mail from opening. The database says so in its own
            // words if the folder is unusable.
            Trace::Line("Single instance: the lock could not be taken (" + error +
                        "); starting without it");
            return Start::First;
        case UltraCanvas::InstanceLockResult::HeldElsewhere:
            break;
    }
    Trace::Line("Single instance: UltraMail is already running; asking it to come forward");
    const bool handedOff = HandOff();
    Close();
    return handedOff ? Start::HandedOff : Start::AlreadyRunning;
}

SingleInstance::~SingleInstance() {
    Close();
}

} // namespace UltraMail
