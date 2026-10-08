// Apps/UltraClipboard/SingleInstance.cpp
// The hand-off to a running UltraClipboard. See SingleInstance.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "SingleInstance.h"

#ifdef ULTRACLIPBOARD_HAVE_ULTRAMESSAGE
#include "UltraCanvasDebug.h"
#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageUltraCanvas.h"

#include <vector>
#endif

namespace UltraClipboard {

#ifdef ULTRACLIPBOARD_HAVE_ULTRAMESSAGE
namespace {

constexpr const char* kAppId = "org.ultraos.ultraclipboard";
constexpr const char* kShowTopic = "org.ultraos.ultraclipboard.show";
// The running instance answers from its UI thread, before it acts; a window
// busy for longer than this is taken as not running, and this start opens
// its own.
constexpr int kAnswerTimeoutMs = 3000;

void RegisterShowTopic() {
    static const bool registered = [] {
        UltraMsgTopicSchema schema;
        schema.topic = kShowTopic;
        schema.description = "Bring UltraClipboard forward, with a search or an entry's edit dialog";
        schema.fields = {{"search", "string", false}, {"editId", "integer", false}};
        return static_cast<bool>(UltraMsg_RegisterSchema(schema));
    }();
    (void)registered;
}

} // namespace

bool SingleInstance::HandOff(const Request& request) {
    RegisterShowTopic();
    UltraMsgConnectOptions options;
    options.appId = kAppId;
    options.displayName = "UltraClipboard";
    options.startBrokerIfAbsent = true;   // so the next start finds this one
    UltraMsgResult error;
    const UltraMsgHandle endpoint = UltraMsg_Connect(options, &error);
    if (endpoint == UltraMsgInvalidHandle) {
        debugOutput << "UltraClipboard: no UltraMessage bus (" << error.message
                    << "); this start does not look for a running one" << std::endl;
        return false;
    }
    endpoint_ = endpoint;

    UltraMsgEndpointInfo self;
    std::vector<std::string> instances;
    if (!UltraMsg_GetEndpointInfo(endpoint, self) || !UltraMsg_ResolveApp(endpoint, kAppId, instances)) {
        return false;
    }
    UltraCanvas::JSONValue body = UltraCanvas::JSONValue::MakeObject();
    if (!request.search.empty()) body.Set("search", request.search);
    if (request.editId != 0) body.Set("editId", request.editId);
    for (const std::string& instance : instances) {
        if (instance == self.instanceId) continue;
        UltraMsgMessage reply;
        if (UltraMsg_Request(endpoint, instance, kShowTopic, body, kAnswerTimeoutMs, reply)) return true;
    }
    return false;
}

void SingleInstance::Listen(Handler handler) {
    if (endpoint_ == 0 || subscription_ != 0) return;
    handler_ = std::move(handler);
    if (!UltraMsg_HasUIDispatcher()) UltraMsg_UseUltraCanvasApplication();
    const UltraMsgHandle endpoint = endpoint_;
    const UltraMsgHandle subscription = UltraMsg_Subscribe(endpoint, kShowTopic,
        [this, endpoint](const UltraMsgMessage& message) {
            Request request;
            request.search = message.body.Get("search").GetString();
            request.editId = message.body.Get("editId").GetInteger();
            // Answer first: the other start is waiting to quit.
            UltraMsg_Reply(endpoint, message, UltraCanvas::JSONValue::MakeObject());
            if (handler_) handler_(request);
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
bool SingleInstance::HandOff(const Request&) { return false; }
void SingleInstance::Listen(Handler) {}
void SingleInstance::Close() {}
#endif

SingleInstance::~SingleInstance() {
    Close();
}

} // namespace UltraClipboard
