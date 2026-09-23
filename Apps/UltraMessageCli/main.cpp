// Apps/UltraMessageCli/main.cpp
// ultramsg — the UltraMessage command line (Phase 1): post a message, follow
// the bus, query the journal, list endpoints, show the broker. What
// `osascript` is to Apple Events and `busctl` is to D-Bus, for shell scripts
// and two-process tests.
//
//   ultramsg post <topic> [json-body] [--to <app>] [--conversation <id>] [--persistent]
//   ultramsg tail [pattern]              follow live messages (default: everything)
//   ultramsg query [--topic <pattern>] [--unread] [--text <s>] [--limit <n>] [--json]
//   ultramsg conversations
//   ultramsg endpoints
//   ultramsg info
//   ultramsg mark-read <id>... | dismiss <id>... | delete <id>...
//   ultramsg export <path> [--topic <pattern>]
//
// Options everywhere: --bus <path>  --journal <path>  --app <id>  --no-broker
// (fail when no broker runs instead of hosting one).
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraMessage/UltraMessage.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using UltraCanvas::JSONValue;

namespace {

volatile std::sig_atomic_t gStop = 0;
void OnSignal(int) { gStop = 1; }

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::string bus;
    std::string journal;
    std::string app = "org.ultraos.ultramsg";
    std::string to = "*";
    std::string conversation;
    std::string topic;
    std::string text;
    std::string service;
    int limit = 50;
    bool persistent = false;
    bool unread = false;
    bool json = false;
    bool noBroker = false;
};

bool Parse(int argc, char** argv, Args& args) {
    if (argc < 2) return false;
    args.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (a == "--bus") { if (!value(args.bus)) return false; }
        else if (a == "--journal") { if (!value(args.journal)) return false; }
        else if (a == "--app") { if (!value(args.app)) return false; }
        else if (a == "--to") { if (!value(args.to)) return false; }
        else if (a == "--conversation") { if (!value(args.conversation)) return false; }
        else if (a == "--topic") { if (!value(args.topic)) return false; }
        else if (a == "--text") { if (!value(args.text)) return false; }
        else if (a == "--service") { if (!value(args.service)) return false; }
        else if (a == "--limit") { std::string v; if (!value(v)) return false; args.limit = std::atoi(v.c_str()); }
        else if (a == "--persistent") args.persistent = true;
        else if (a == "--unread") args.unread = true;
        else if (a == "--json") args.json = true;
        else if (a == "--no-broker") args.noBroker = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
        else args.positional.push_back(a);
    }
    return true;
}

void Usage() {
    std::fputs(
        "usage: ultramsg <command> [options]\n"
        "  post <topic> [json-body] [--to <app>] [--conversation <id>] [--persistent]\n"
        "  tail [pattern]\n"
        "  query [--topic <pattern>] [--unread] [--text <s>] [--service <s>] [--limit <n>] [--json]\n"
        "  conversations\n"
        "  endpoints\n"
        "  info\n"
        "  adapters [enable <name> | disable <name>]\n"
        "  mark-read <id>...  |  dismiss <id>...  |  delete <id>...\n"
        "  export <path> [--topic <pattern>]\n"
        "options: --bus <path> --journal <path> --app <id> --no-broker\n",
        stderr);
}

std::string Compact(const JSONValue& value) {
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = false;
    return UltraCanvas::JSON::Serialize(value, options);
}

std::string Pretty(const JSONValue& value) {
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    return UltraCanvas::JSON::Serialize(value, options);
}

std::string Summary(const UltraMsgMessage& m) {
    const UltraMsgEnvelope& e = m.envelope;
    std::string line = e.id + "  " + e.topic + "  from " + e.from.appId;
    if (!e.conversation.empty()) line += "  [" + e.conversation + "]";
    if (m.read) line += "  (read)";
    if (m.body.IsObject()) {
        for (const char* key : {"text", "subject", "summary"}) {
            if (const JSONValue* v = m.body.Find(key); v && v->IsString() && !v->GetString().empty()) {
                line += "  " + v->GetString();
                break;
            }
        }
    }
    return line;
}

JSONValue MessageJson(const UltraMsgMessage& m) {
    JSONValue j = JSONValue::MakeObject();
    j.Set("id", m.envelope.id);
    j.Set("topic", m.envelope.topic);
    j.Set("kind", UltraMsg_KindName(m.envelope.kind));
    j.Set("from", m.envelope.from.appId);
    j.Set("instance", m.envelope.from.instanceId);
    j.Set("to", m.envelope.to);
    j.Set("conversation", m.envelope.conversation);
    j.Set("time", m.envelope.timestampMs);
    j.Set("read", m.read);
    j.Set("dismissed", m.dismissed);
    j.Set("body", m.body);
    return j;
}

int Fail(const UltraMsgResult& r, const char* what) {
    std::fprintf(stderr, "%s: %s (%s)\n", what, r.message.c_str(), UltraMsg_ResultCodeName(r.code));
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!Parse(argc, argv, args) || args.command == "help" || args.command == "--help") {
        Usage();
        return args.command == "help" || args.command == "--help" ? 0 : 2;
    }

    UltraMsgConnectOptions options;
    options.appId = args.app;
    options.displayName = "ultramsg";
    options.busPath = args.bus;
    options.journalPath = args.journal;
    options.startBrokerIfAbsent = !args.noBroker;
    options.deliverOnUIThread = false;   // a tool: callbacks straight from the transport thread
    UltraMsgResult error;
    UltraMsgHandle endpoint = UltraMsg_Connect(options, &error);
    if (endpoint == UltraMsgInvalidHandle) return Fail(error, "connect");

    int rc = 0;
    const std::string& cmd = args.command;

    if (cmd == "post") {
        if (args.positional.empty()) { Usage(); rc = 2; }
        else {
            JSONValue body = JSONValue::MakeObject();
            if (args.positional.size() > 1) {
                UltraCanvas::JSONParseResult parsed;
                body = UltraCanvas::JSON::Parse(args.positional[1], &parsed);
                if (!parsed.success) {
                    std::fprintf(stderr, "body is not JSON: %s\n", parsed.errorMessage.c_str());
                    UltraMsg_Disconnect(endpoint);
                    return 2;
                }
            }
            UltraMsgSendOptions send;
            send.to = args.to;
            send.conversation = args.conversation;
            if (args.persistent) send.flags |= UltraMsgFlag_Persistent;
            std::string id;
            UltraMsgResult r = UltraMsg_Post(endpoint, args.positional[0], body, send, &id);
            if (!r) rc = Fail(r, "post");
            else std::printf("%s\n", id.c_str());
            // Give the frame a moment to leave before the socket closes.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } else if (cmd == "tail") {
        const std::string pattern = args.positional.empty() ? "#" : args.positional[0];
        std::signal(SIGINT, OnSignal);
        std::signal(SIGTERM, OnSignal);
        const bool json = args.json;
        UltraMsgSubscribeOptions sub;
        sub.onWorkerThread = true;
        UltraMsgHandle subscription = UltraMsg_Subscribe(
            endpoint, pattern,
            [json](const UltraMsgMessage& m) {
                std::printf("%s\n", json ? Compact(MessageJson(m)).c_str() : Summary(m).c_str());
                std::fflush(stdout);
            },
            sub, &error);
        if (subscription == UltraMsgInvalidHandle) rc = Fail(error, "subscribe");
        else {
            UltraMsgBrokerInfo info;
            UltraMsg_GetBrokerInfo(endpoint, info);
            std::fprintf(stderr, "following %s on %s — Ctrl-C to stop\n", pattern.c_str(),
                         info.busPath.c_str());
            while (!gStop && UltraMsg_IsConnected(endpoint))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            UltraMsg_Unsubscribe(subscription);
        }
    } else if (cmd == "query" || cmd == "export") {
        UltraMsgQuery query;
        if (!args.topic.empty()) query.topics = {args.topic};
        query.unreadOnly = args.unread;
        query.textContains = args.text;
        query.service = args.service;
        query.limit = args.limit;
        if (cmd == "export") {
            if (args.positional.empty()) { Usage(); rc = 2; }
            else {
                int64_t count = 0;
                query.limit = 0;
                UltraMsgResult r = UltraMsg_Export(endpoint, query, args.positional[0], &count);
                if (!r) rc = Fail(r, "export");
                else std::printf("%lld messages written to %s\n", static_cast<long long>(count),
                                 args.positional[0].c_str());
            }
        } else {
            std::vector<UltraMsgMessage> messages;
            UltraMsgResult r = UltraMsg_Query(endpoint, query, messages);
            if (!r) rc = Fail(r, "query");
            else if (args.json) {
                JSONValue list = JSONValue::MakeArray();
                for (const auto& m : messages) list.Append(MessageJson(m));
                std::printf("%s\n", Pretty(list).c_str());
            } else {
                for (const auto& m : messages) std::printf("%s\n", Summary(m).c_str());
                if (messages.empty()) std::printf("(no messages)\n");
            }
        }
    } else if (cmd == "conversations") {
        UltraMsgQuery query;
        query.service = args.service;
        query.limit = args.limit;
        std::vector<UltraMsgConversation> conversations;
        UltraMsgResult r = UltraMsg_ListConversations(endpoint, query, conversations);
        if (!r) rc = Fail(r, "conversations");
        else {
            for (const auto& c : conversations)
                std::printf("%s  %s  %s  %d unread / %d\n", c.id.c_str(), c.service.c_str(),
                            c.title.c_str(), c.unreadCount, c.messageCount);
            if (conversations.empty()) std::printf("(no conversations)\n");
        }
    } else if (cmd == "endpoints") {
        std::vector<UltraMsgEndpointInfo> endpoints;
        UltraMsgResult r = UltraMsg_ListEndpoints(endpoint, endpoints);
        if (!r) rc = Fail(r, "endpoints");
        else {
            for (const auto& e : endpoints)
                std::printf("%s  %s  pid %d%s  %s\n", e.appId.c_str(), e.instanceId.c_str(), e.processId,
                            e.verified ? "" : " (unverified)", e.displayName.c_str());
        }
    } else if (cmd == "info") {
        UltraMsgBrokerInfo info;
        UltraMsgResult r = UltraMsg_GetBrokerInfo(endpoint, info);
        if (!r) rc = Fail(r, "info");
        else {
            std::printf("bus:       %s\n", info.busPath.c_str());
            std::printf("journal:   %s\n", info.journalPath.empty() ? "(none)" : info.journalPath.c_str());
            std::printf("host pid:  %d%s\n", info.hostProcessId, info.inProcess ? " (this process)" : "");
            std::printf("endpoints: %d\n", info.endpointCount);
            std::printf("version:   %s\n", info.version.c_str());
        }
    } else if (cmd == "adapters") {
        if (args.positional.empty()) {
            std::vector<UltraMsgAdapterInfo> adapters;
            UltraMsgResult r = UltraMsg_ListAdapters(endpoint, adapters);
            if (!r) rc = Fail(r, "adapters");
            else if (adapters.empty()) std::printf("no adapters in this broker's build\n");
            for (const auto& a : adapters) {
                std::string status = UltraMsg_AdapterStatusName(a.state.status);
                if (!a.state.mode.empty()) status += " (" + a.state.mode + ")";
                std::printf("%-28s %-8s %-22s %s\n", a.name.c_str(), a.enabled ? "on" : "off", status.c_str(),
                            a.state.message.c_str());
                if (!a.state.remedy.empty()) std::printf("%-28s          remedy: %s\n", "", a.state.remedy.c_str());
            }
        } else if ((args.positional[0] == "enable" || args.positional[0] == "disable") && args.positional.size() == 2) {
            UltraMsgResult r = UltraMsg_EnableAdapter(endpoint, args.positional[1], args.positional[0] == "enable");
            if (!r) rc = Fail(r, "adapters");
            else {
                UltraMsgAdapterState state;
                if (UltraMsg_GetAdapterState(endpoint, args.positional[1], state))
                    std::printf("%s: %s%s%s\n", args.positional[1].c_str(), UltraMsg_AdapterStatusName(state.status),
                                state.message.empty() ? "" : " - ", state.message.c_str());
            }
        } else {
            Usage();
            rc = 2;
        }
    } else if (cmd == "mark-read" || cmd == "dismiss" || cmd == "delete") {
        if (args.positional.empty()) { Usage(); rc = 2; }
        else {
            UltraMsgResult r = cmd == "mark-read" ? UltraMsg_MarkRead(endpoint, args.positional)
                             : cmd == "dismiss"   ? UltraMsg_Dismiss(endpoint, args.positional)
                                                  : UltraMsg_Delete(endpoint, args.positional);
            if (!r) rc = Fail(r, cmd.c_str());
        }
    } else {
        Usage();
        rc = 2;
    }

    UltraMsg_Disconnect(endpoint);
    return rc;
}
