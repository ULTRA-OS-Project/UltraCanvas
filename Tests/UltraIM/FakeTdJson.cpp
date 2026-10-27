// Tests/UltraIM/FakeTdJson.cpp
// A stand-in for libtdjson for TelegramProviderTests: TDLib's C interface
// (td_create_client_id, td_send, td_receive, td_execute) over a small
// Telegram kept in memory - accounts, sign-in with code and two-step
// password, registration, private chats and basic groups, messages sent,
// edited, deleted and read, typing, history, the session kept per database
// directory - answering with TDLib's JSON objects (td_api.tl names), so that
// several providers in one process can talk to each other through it.
// A test-only request through td_execute: fakeAddAccount. For trying an
// application on it, FAKE_TDJSON_ACCOUNTS seeds accounts
// ("phone:First:username:password;..."), and an account called "echobot"
// answers every message with "echo: <text>". The code is always 12345.
// The number +1 555 9999 is never answered (a network that is down), for the
// provider's step timeout.
// Version: 0.1.2 - the code to the app first, by SMS on resendAuthenticationCode (as Telegram does)
// Version: 0.1.1 - a phone number whose code never comes
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#define FAKE_TD_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_TD_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using UltraCanvas::JSONValue;

namespace {

const char* const kCode = "12345";

struct Account {
    int64_t     id = 0;
    std::string phone;          // digits only
    std::string firstName;
    std::string lastName;
    std::string username;
    std::string password;       // "" = no two-step verification
};

struct Message {
    int64_t     id = 0;
    int64_t     sender = 0;
    int64_t     date = 0;
    int64_t     editDate = 0;
    int64_t     replyTo = 0;
    std::string text;
    std::string service;        // "" = text; else the content's @type
    std::string title;
};

struct ChatRec {
    int64_t     groupId = 0;    // 0 = private
    std::string title;
    std::vector<int64_t> members;
    std::vector<Message> messages;          // oldest first
    std::map<int64_t, int64_t> lastRead;    // member -> read up to
};

struct Client {
    int         id = 0;
    bool        started = false;
    bool        closed = false;
    bool        codeBySms = false;       // after resendAuthenticationCode
    std::string state;
    std::string databaseDir;
    std::string databaseKey;
    std::string phone;
    int64_t     user = 0;
    std::set<int64_t> knownUsers;
    std::set<int64_t> announced;            // chat ids (in its view) it has had updateNewChat for
    bool        allLoaded = false;
};

struct Server {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> out;
    std::map<int64_t, Account> accounts;
    std::map<std::string, std::pair<std::string, int64_t>> sessions;    // database dir -> key, user
    std::map<std::string, ChatRec> chats;   // "p:<low>:<high>" or "g:<id>"
    std::map<int, Client> clients;
    int nextClient = 1;
    int64_t nextUser = 1000;
    int64_t nextMessage = 1;
    int64_t nextTemp = 1;
    int64_t nextGroup = 500;
    int64_t clock = 1700000000;
};

void Seed(Server& server);

Server& S() {
    static Server server;
    static bool seeded = (Seed(server), true);
    (void)seeded;
    return server;
}

JSONValue Obj(const char* type) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("@type", JSONValue(type));
    return o;
}

std::string Digits(const std::string& s) {
    std::string d;
    for (char c : s) {
        if (c >= '0' && c <= '9') d += c;
    }
    return d;
}

void Seed(Server& server) {
    const char* env = std::getenv("FAKE_TDJSON_ACCOUNTS");
    std::string list = env ? env : "";
    size_t at = 0;
    while (at < list.size()) {
        size_t end = list.find(';', at);
        if (end == std::string::npos) end = list.size();
        std::vector<std::string> f;
        size_t from = at;
        for (size_t i = at; i <= end; ++i) {
            if (i == end || list[i] == ':') {
                f.push_back(list.substr(from, i - from));
                from = i + 1;
            }
        }
        if (f.size() >= 2) {
            Account a;
            a.id = server.nextUser++;
            a.phone = Digits(f[0]);
            a.firstName = f[1];
            a.username = f.size() > 2 ? f[2] : std::string();
            a.password = f.size() > 3 ? f[3] : std::string();
            server.accounts[a.id] = a;
        }
        at = end + 1;
    }
}

// ---- server mutex held ----------------------------------------------------------

void Push(Client& c, JSONValue o) {
    if (c.closed) return;
    o.Set("@client_id", JSONValue(c.id));
    S().out.push_back(UltraCanvas::JSON::Serialize(o));
    S().ready.notify_all();
}

void Answer(Client& c, const std::string& extra, JSONValue o) {
    if (!extra.empty()) o.Set("@extra", JSONValue(extra));
    Push(c, std::move(o));
}

JSONValue Error(int code, const std::string& message) {
    JSONValue e = Obj("error");
    e.Set("code", JSONValue(code));
    e.Set("message", JSONValue(message));
    return e;
}

void State(Client& c, const char* state, JSONValue extra = JSONValue()) {
    c.state = state;
    JSONValue s = extra.IsObject() ? extra : JSONValue::MakeObject();
    s.Set("@type", JSONValue(state));
    JSONValue u = Obj("updateAuthorizationState");
    u.Set("authorization_state", s);
    Push(c, u);
}

std::vector<Client*> ClientsOf(int64_t user) {
    std::vector<Client*> out;
    for (auto& entry : S().clients) {
        if (entry.second.user == user && !entry.second.closed && entry.second.state == "authorizationStateReady")
            out.push_back(&entry.second);
    }
    return out;
}

JSONValue UserObject(const Account& a) {
    JSONValue u = Obj("user");
    u.Set("id", JSONValue(a.id));
    u.Set("first_name", JSONValue(a.firstName));
    u.Set("last_name", JSONValue(a.lastName));
    JSONValue names = Obj("usernames");
    JSONValue active = JSONValue::MakeArray();
    if (!a.username.empty()) active.Append(JSONValue(a.username));
    names.Set("active_usernames", active);
    names.Set("disabled_usernames", JSONValue::MakeArray());
    names.Set("editable_username", JSONValue(a.username));
    u.Set("usernames", names);
    u.Set("phone_number", JSONValue(a.phone));
    JSONValue status = Obj(ClientsOf(a.id).empty() ? "userStatusOffline" : "userStatusOnline");
    if (ClientsOf(a.id).empty()) status.Set("was_online", JSONValue(S().clock));
    else status.Set("expires", JSONValue(S().clock + 300));
    u.Set("status", status);
    u.Set("type", Obj("userTypeRegular"));
    return u;
}

void KnowUser(Client& c, int64_t user) {
    if (c.knownUsers.count(user) || !S().accounts.count(user)) return;
    c.knownUsers.insert(user);
    JSONValue u = Obj("updateUser");
    u.Set("user", UserObject(S().accounts[user]));
    Push(c, u);
}

std::string PrivateKey(int64_t a, int64_t b) {
    return "p:" + std::to_string(std::min(a, b)) + ":" + std::to_string(std::max(a, b));
}

// The chat's id as `viewer` sees it: the other person's id, or -group.
int64_t ViewId(const ChatRec& chat, int64_t viewer) {
    if (chat.groupId) return -chat.groupId;
    for (int64_t m : chat.members) {
        if (m != viewer) return m;
    }
    return viewer;
}

ChatRec* ChatOf(int64_t viewer, int64_t chatId) {
    if (chatId < 0) {
        auto it = S().chats.find("g:" + std::to_string(-chatId));
        if (it == S().chats.end()) return nullptr;
        for (int64_t m : it->second.members) {
            if (m == viewer) return &it->second;
        }
        return nullptr;
    }
    if (!S().accounts.count(chatId)) return nullptr;
    ChatRec& chat = S().chats[PrivateKey(viewer, chatId)];
    if (chat.members.empty()) chat.members = viewer == chatId ? std::vector<int64_t>{viewer}
                                                              : std::vector<int64_t>{viewer, chatId};
    return &chat;
}

JSONValue MessageObject(const ChatRec& chat, const Message& m, int64_t viewer, bool pending = false) {
    JSONValue o = Obj("message");
    o.Set("id", JSONValue(m.id));
    JSONValue sender = Obj("messageSenderUser");
    sender.Set("user_id", JSONValue(m.sender));
    o.Set("sender_id", sender);
    o.Set("chat_id", JSONValue(ViewId(chat, viewer)));
    if (pending) o.Set("sending_state", Obj("messageSendingStatePending"));
    o.Set("is_outgoing", JSONValue(m.sender == viewer));
    o.Set("date", JSONValue(m.date));
    o.Set("edit_date", JSONValue(m.editDate));
    if (m.replyTo) {
        JSONValue reply = Obj("messageReplyToMessage");
        reply.Set("chat_id", JSONValue(ViewId(chat, viewer)));
        reply.Set("message_id", JSONValue(m.replyTo));
        o.Set("reply_to", reply);
    }
    JSONValue content;
    if (m.service.empty()) {
        content = Obj("messageText");
        JSONValue text = Obj("formattedText");
        text.Set("text", JSONValue(m.text));
        text.Set("entities", JSONValue::MakeArray());
        content.Set("text", text);
    } else {
        content = Obj(m.service.c_str());
        content.Set("title", JSONValue(m.title));
        JSONValue ids = JSONValue::MakeArray();
        for (int64_t u : chat.members) {
            if (u != m.sender) ids.Append(JSONValue(u));
        }
        content.Set("member_user_ids", ids);
    }
    o.Set("content", content);
    return o;
}

JSONValue Positions(const ChatRec& chat) {
    JSONValue list = JSONValue::MakeArray();
    if (chat.messages.empty()) return list;
    JSONValue p = Obj("chatPosition");
    p.Set("list", Obj("chatListMain"));
    // int64: a string, as TDLib sends it.
    p.Set("order", JSONValue(std::to_string(chat.messages.back().date * 100000 + chat.messages.back().id % 100000)));
    p.Set("is_pinned", JSONValue(false));
    list.Append(p);
    return list;
}

JSONValue ChatObject(const ChatRec& chat, int64_t viewer) {
    JSONValue o = Obj("chat");
    o.Set("id", JSONValue(ViewId(chat, viewer)));
    if (chat.groupId) {
        JSONValue t = Obj("chatTypeBasicGroup");
        t.Set("basic_group_id", JSONValue(chat.groupId));
        o.Set("type", t);
        o.Set("title", JSONValue(chat.title));
    } else {
        JSONValue t = Obj("chatTypePrivate");
        const int64_t peer = ViewId(chat, viewer);
        t.Set("user_id", JSONValue(peer));
        o.Set("type", t);
        const Account& a = S().accounts[peer];
        o.Set("title", JSONValue(a.firstName + (a.lastName.empty() ? "" : " " + a.lastName)));
    }
    o.Set("positions", JSONValue::MakeArray());     // they follow in updateChatLastMessage
    int unread = 0;
    const int64_t read = chat.lastRead.count(viewer) ? chat.lastRead.at(viewer) : 0;
    int64_t othersRead = 0;
    for (const auto& r : chat.lastRead) {
        if (r.first != viewer) othersRead = std::max(othersRead, r.second);
    }
    for (const auto& m : chat.messages) {
        if (m.sender != viewer && m.id > read) ++unread;
    }
    o.Set("unread_count", JSONValue(unread));
    o.Set("last_read_inbox_message_id", JSONValue(read));
    o.Set("last_read_outbox_message_id", JSONValue(othersRead));
    return o;
}

void Announce(Client& c, const ChatRec& chat) {
    const int64_t view = ViewId(chat, c.user);
    for (int64_t m : chat.members) KnowUser(c, m);
    if (!c.announced.count(view)) {
        c.announced.insert(view);
        JSONValue u = Obj("updateNewChat");
        u.Set("chat", ChatObject(chat, c.user));
        Push(c, u);
    }
    if (!chat.messages.empty()) {
        JSONValue u = Obj("updateChatLastMessage");
        u.Set("chat_id", JSONValue(view));
        u.Set("last_message", MessageObject(chat, chat.messages.back(), c.user));
        u.Set("positions", Positions(chat));
        Push(c, u);
    }
}

void PresenceChanged(int64_t user) {
    for (auto& entry : S().clients) {
        Client& c = entry.second;
        if (c.closed || c.user == user || !c.knownUsers.count(user) || c.state != "authorizationStateReady") continue;
        JSONValue u = Obj("updateUserStatus");
        u.Set("user_id", JSONValue(user));
        u.Set("status", UserObject(S().accounts[user])["status"]);
        Push(c, u);
    }
}

void SignedIn(Client& c, int64_t user) {
    c.user = user;
    S().sessions[c.databaseDir] = {c.databaseKey, user};
    KnowUser(c, user);
    State(c, "authorizationStateReady");
    PresenceChanged(user);
}

void Deliver(ChatRec& chat, const Message& m, int64_t exceptClient) {
    for (int64_t member : chat.members) {
        for (Client* other : ClientsOf(member)) {
            if (other->id == exceptClient) continue;
            Announce(*other, chat);
            JSONValue u = Obj("updateNewMessage");
            u.Set("message", MessageObject(chat, m, member));
            Push(*other, u);
        }
    }
}

// The code info Telegram sends: to the app the number is signed in on first,
// with SMS as the next way; by SMS once asked for, with no way after that.
void CodeSent(Client& c) {
    JSONValue info = Obj("authenticationCodeInfo");
    info.Set("phone_number", JSONValue("+" + c.phone));
    JSONValue how = Obj(c.codeBySms ? "authenticationCodeTypeSms" : "authenticationCodeTypeTelegramMessage");
    how.Set("length", JSONValue(5));
    info.Set("type", how);
    if (!c.codeBySms) {
        JSONValue next = Obj("authenticationCodeTypeSms");
        next.Set("length", JSONValue(5));
        info.Set("next_type", next);
    }
    JSONValue extraState = JSONValue::MakeObject();
    extraState.Set("code_info", info);
    State(c, "authorizationStateWaitCode", extraState);
}

// ---- requests ----------------------------------------------------------------------

void Handle(Client& c, const JSONValue& r) {
    const std::string type = r["@type"].GetString();
    const std::string extra = r["@extra"].GetString();
    if (!c.started) {
        c.started = true;
        State(c, "authorizationStateWaitTdlibParameters");
    }
    auto ok = [&]() { Answer(c, extra, Obj("ok")); };
    auto fail = [&](int code, const std::string& m) { Answer(c, extra, Error(code, m)); };
    const bool ready = c.state == "authorizationStateReady";

    if (type == "getOption") {
        JSONValue v = Obj("optionValueString");
        v.Set("value", JSONValue("fake"));
        return Answer(c, extra, v);
    }
    if (type == "close") {
        ok();
        const int64_t user = c.user;
        State(c, "authorizationStateClosing");
        State(c, "authorizationStateClosed");
        c.closed = true;
        if (user) PresenceChanged(user);
        return;
    }
    if (type == "setTdlibParameters") {
        if (c.state != "authorizationStateWaitTdlibParameters") return fail(400, "Unexpected setTdlibParameters");
        if (r["api_id"].GetInteger(0) <= 0 || r["api_hash"].GetString().empty())
            return fail(400, "Valid api_id must be provided");
        c.databaseDir = r["database_directory"].GetString();
        c.databaseKey = r["database_encryption_key"].GetString();
        std::error_code ec;     // TDLib makes its directory
        std::filesystem::create_directories(UltraCanvas::PathFromUtf8(c.databaseDir), ec);
        auto session = S().sessions.find(c.databaseDir);
        if (session != S().sessions.end() && session->second.first != c.databaseKey)
            return fail(401, "Wrong database encryption key");
        ok();
        if (session != S().sessions.end()) return SignedIn(c, session->second.second);
        return State(c, "authorizationStateWaitPhoneNumber");
    }
    if (type == "setAuthenticationPhoneNumber") {
        if (c.state != "authorizationStateWaitPhoneNumber") return fail(400, "Unexpected setAuthenticationPhoneNumber");
        c.phone = Digits(r["phone_number"].GetString());
        if (c.phone.size() < 5) return fail(400, "PHONE_NUMBER_INVALID");
        if (c.phone == "15559999") return;      // no answer, ever: the network is down
        ok();
        c.codeBySms = false;
        return CodeSent(c);
    }
    if (type == "resendAuthenticationCode") {
        if (c.state != "authorizationStateWaitCode") return fail(400, "Unexpected resendAuthenticationCode");
        if (c.codeBySms) return fail(400, "SEND_CODE_UNAVAILABLE");    // no next way left
        ok();
        c.codeBySms = true;
        return CodeSent(c);
    }
    if (type == "checkAuthenticationCode") {
        if (c.state != "authorizationStateWaitCode") return fail(400, "Unexpected checkAuthenticationCode");
        if (r["code"].GetString() != kCode) return fail(400, "PHONE_CODE_INVALID");
        ok();
        for (auto& a : S().accounts) {
            if (a.second.phone != c.phone) continue;
            if (!a.second.password.empty()) {
                JSONValue s = JSONValue::MakeObject();
                s.Set("password_hint", JSONValue("the usual"));
                s.Set("has_recovery_email_address", JSONValue(false));
                return State(c, "authorizationStateWaitPassword", s);
            }
            return SignedIn(c, a.first);
        }
        return State(c, "authorizationStateWaitRegistration");
    }
    if (type == "checkAuthenticationPassword") {
        if (c.state != "authorizationStateWaitPassword") return fail(400, "Unexpected checkAuthenticationPassword");
        for (auto& a : S().accounts) {
            if (a.second.phone != c.phone) continue;
            if (a.second.password != r["password"].GetString()) return fail(400, "PASSWORD_HASH_INVALID");
            ok();
            return SignedIn(c, a.first);
        }
        return fail(400, "No such account");
    }
    if (type == "registerUser") {
        if (c.state != "authorizationStateWaitRegistration") return fail(400, "Unexpected registerUser");
        Account a;
        a.id = S().nextUser++;
        a.phone = c.phone;
        a.firstName = r["first_name"].GetString();
        a.lastName = r["last_name"].GetString();
        S().accounts[a.id] = a;
        ok();
        return SignedIn(c, a.id);
    }
    if (!ready) return fail(401, "Unauthorized");

    if (type == "getMe") return Answer(c, extra, UserObject(S().accounts[c.user]));
    if (type == "loadChats") {
        if (c.allLoaded) return fail(404, "Not Found");
        for (auto& entry : S().chats) {
            ChatRec& chat = entry.second;
            if (std::find(chat.members.begin(), chat.members.end(), c.user) == chat.members.end()) continue;
            if (chat.messages.empty()) continue;
            Announce(c, chat);
        }
        c.allLoaded = true;
        return ok();
    }
    if (type == "getChat") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        Announce(c, *chat);
        return Answer(c, extra, ChatObject(*chat, c.user));
    }
    if (type == "searchPublicChat") {
        for (auto& a : S().accounts) {
            if (a.second.username != r["username"].GetString()) continue;
            ChatRec* chat = ChatOf(c.user, a.first);
            Announce(c, *chat);
            return Answer(c, extra, ChatObject(*chat, c.user));
        }
        return fail(400, "USERNAME_NOT_OCCUPIED");
    }
    if (type == "searchUserByPhoneNumber") {
        const std::string phone = Digits(r["phone_number"].GetString());
        for (auto& a : S().accounts) {
            if (a.second.phone != phone) continue;
            KnowUser(c, a.first);
            return Answer(c, extra, UserObject(a.second));
        }
        return fail(404, "User not found");
    }
    if (type == "createPrivateChat") {
        ChatRec* chat = ChatOf(c.user, r["user_id"].GetInteger(0));
        if (!chat) return fail(400, "User not found");
        Announce(c, *chat);
        return Answer(c, extra, ChatObject(*chat, c.user));
    }
    if (type == "createNewBasicGroupChat") {
        ChatRec group;
        group.groupId = S().nextGroup++;
        group.title = r["title"].GetString();
        group.members.push_back(c.user);
        for (const auto& u : r["user_ids"].GetElements()) {
            if (!S().accounts.count(u.GetInteger(0))) return fail(400, "USER_ID_INVALID");
            group.members.push_back(u.GetInteger(0));
        }
        Message created;
        created.id = (S().nextMessage++) << 20;
        created.sender = c.user;
        created.date = ++S().clock;
        created.service = "messageBasicGroupChatCreate";
        created.title = group.title;
        group.messages.push_back(created);
        ChatRec& stored = S().chats["g:" + std::to_string(group.groupId)] = group;
        Deliver(stored, created, -1);
        JSONValue answer = Obj("createdBasicGroupChat");
        answer.Set("chat_id", JSONValue(-stored.groupId));
        JSONValue failed = Obj("failedToAddMembers");
        failed.Set("failed_to_add_members", JSONValue::MakeArray());
        answer.Set("failed_to_add_members", failed);
        return Answer(c, extra, answer);
    }
    if (type == "sendMessage") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        const JSONValue& content = r["input_message_content"];
        if (content["@type"].GetString() != "inputMessageText") return fail(400, "Unsupported content");
        Message m;
        m.sender = c.user;
        m.date = ++S().clock;
        m.text = content["text"]["text"].GetString();
        m.replyTo = r["reply_to"]["message_id"].GetInteger(0);
        // First the message as it goes out (a temporary id), then as sent.
        Message pending = m;
        pending.id = (S().nextTemp++) + 1;
        JSONValue update = Obj("updateNewMessage");
        update.Set("message", MessageObject(*chat, pending, c.user, true));
        Push(c, update);
        Answer(c, extra, MessageObject(*chat, pending, c.user, true));
        m.id = (S().nextMessage++) << 20;
        chat->messages.push_back(m);
        JSONValue done = Obj("updateMessageSendSucceeded");
        done.Set("message", MessageObject(*chat, m, c.user));
        done.Set("old_message_id", JSONValue(pending.id));
        Push(c, done);
        Announce(c, *chat);
        Deliver(*chat, m, c.id);
        // The echo bot answers at once.
        for (int64_t member : chat->members) {
            if (member == c.user || S().accounts[member].username != "echobot") continue;
            Message answer;
            answer.id = (S().nextMessage++) << 20;
            answer.sender = member;
            answer.date = ++S().clock;
            answer.text = "echo: " + m.text;
            answer.replyTo = m.id;
            chat->messages.push_back(answer);
            Deliver(*chat, answer, -1);
        }
        return;
    }
    if (type == "editMessageText") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        for (auto& m : chat->messages) {
            if (m.id != r["message_id"].GetInteger(0)) continue;
            if (m.sender != c.user) return fail(400, "Message can't be edited");
            m.text = r["input_message_content"]["text"]["text"].GetString();
            m.editDate = ++S().clock;
            for (int64_t member : chat->members) {
                for (Client* other : ClientsOf(member)) {
                    JSONValue u = Obj("updateMessageContent");
                    u.Set("chat_id", JSONValue(ViewId(*chat, member)));
                    u.Set("message_id", JSONValue(m.id));
                    u.Set("new_content", MessageObject(*chat, m, member)["content"]);
                    Push(*other, u);
                }
            }
            return Answer(c, extra, MessageObject(*chat, m, c.user));
        }
        return fail(400, "Message not found");
    }
    if (type == "deleteMessages") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        std::set<int64_t> ids;
        for (const auto& id : r["message_ids"].GetElements()) ids.insert(id.GetInteger(0));
        chat->messages.erase(std::remove_if(chat->messages.begin(), chat->messages.end(),
                                            [&](const Message& m) { return ids.count(m.id) != 0; }),
                             chat->messages.end());
        ok();
        for (int64_t member : chat->members) {
            if (member != c.user && !r["revoke"].GetBoolean(false)) continue;
            for (Client* other : ClientsOf(member)) {
                JSONValue u = Obj("updateDeleteMessages");
                u.Set("chat_id", JSONValue(ViewId(*chat, member)));
                JSONValue list = JSONValue::MakeArray();
                for (int64_t id : ids) list.Append(JSONValue(id));
                u.Set("message_ids", list);
                u.Set("is_permanent", JSONValue(true));
                u.Set("from_cache", JSONValue(false));
                Push(*other, u);
            }
        }
        return;
    }
    if (type == "viewMessages") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        int64_t upTo = chat->lastRead[c.user];
        for (const auto& id : r["message_ids"].GetElements()) upTo = std::max(upTo, id.GetInteger(0));
        chat->lastRead[c.user] = upTo;
        ok();
        for (int64_t member : chat->members) {
            for (Client* other : ClientsOf(member)) {
                if (member == c.user) {
                    JSONValue u = Obj("updateChatReadInbox");
                    u.Set("chat_id", JSONValue(ViewId(*chat, member)));
                    u.Set("last_read_inbox_message_id", JSONValue(upTo));
                    u.Set("unread_count", JSONValue(0));
                    Push(*other, u);
                } else {
                    JSONValue u = Obj("updateChatReadOutbox");
                    u.Set("chat_id", JSONValue(ViewId(*chat, member)));
                    u.Set("last_read_outbox_message_id", JSONValue(upTo));
                    Push(*other, u);
                }
            }
        }
        return;
    }
    if (type == "sendChatAction") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        ok();
        for (int64_t member : chat->members) {
            if (member == c.user) continue;
            for (Client* other : ClientsOf(member)) {
                JSONValue u = Obj("updateChatAction");
                u.Set("chat_id", JSONValue(ViewId(*chat, member)));
                JSONValue sender = Obj("messageSenderUser");
                sender.Set("user_id", JSONValue(c.user));
                u.Set("sender_id", sender);
                u.Set("action", r["action"]);
                Push(*other, u);
            }
        }
        return;
    }
    if (type == "getChatHistory") {
        ChatRec* chat = ChatOf(c.user, r["chat_id"].GetInteger(0));
        if (!chat) return fail(400, "Chat not found");
        const int64_t from = r["from_message_id"].GetInteger(0);
        // Like TDLib from its database: a short first page.
        const int64_t limit = std::min<int64_t>(r["limit"].GetInteger(20), from == 0 ? 10 : 100);
        JSONValue list = JSONValue::MakeArray();
        for (auto it = chat->messages.rbegin(); it != chat->messages.rend() && list.GetSize() < static_cast<size_t>(limit);
             ++it) {
            if (from && it->id > from) continue;
            list.Append(MessageObject(*chat, *it, c.user));
        }
        JSONValue answer = Obj("messages");
        answer.Set("total_count", JSONValue(static_cast<int64_t>(chat->messages.size())));
        answer.Set("messages", list);
        return Answer(c, extra, answer);
    }
    return fail(400, "Unknown method " + type);
}

std::string g_executeAnswer;

} // namespace

FAKE_TD_EXPORT int td_create_client_id() {
    std::lock_guard<std::mutex> lock(S().mutex);
    Client c;
    c.id = S().nextClient++;
    S().clients[c.id] = c;
    return c.id;
}

FAKE_TD_EXPORT void td_send(int clientId, const char* request) {
    UltraCanvas::JSONParseResult parsed;
    const JSONValue r = UltraCanvas::JSON::Parse(request ? request : "", &parsed);
    std::lock_guard<std::mutex> lock(S().mutex);
    auto it = S().clients.find(clientId);
    if (it == S().clients.end() || it->second.closed) return;
    Handle(it->second, r);
}

FAKE_TD_EXPORT const char* td_receive(double timeout) {
    static thread_local std::string last;
    std::unique_lock<std::mutex> lock(S().mutex);
    if (!S().ready.wait_for(lock, std::chrono::duration<double>(timeout), []() { return !S().out.empty(); }))
        return nullptr;
    last = S().out.front();
    S().out.pop_front();
    return last.c_str();
}

FAKE_TD_EXPORT const char* td_execute(const char* request) {
    UltraCanvas::JSONParseResult parsed;
    const JSONValue r = UltraCanvas::JSON::Parse(request ? request : "", &parsed);
    std::lock_guard<std::mutex> lock(S().mutex);
    const std::string type = r["@type"].GetString();
    JSONValue answer = Obj("ok");
    if (type == "fakeAddAccount") {
        Account a;
        a.id = S().nextUser++;
        a.phone = Digits(r["phone"].GetString());
        a.firstName = r["first_name"].GetString();
        a.lastName = r["last_name"].GetString();
        a.username = r["username"].GetString();
        a.password = r["password"].GetString();
        S().accounts[a.id] = a;
        answer = Obj("user");
        answer.Set("id", JSONValue(a.id));
    } else if (type == "getOption") {
        answer = Obj("optionValueString");
        answer.Set("value", JSONValue("fake"));
    }
    g_executeAnswer = UltraCanvas::JSON::Serialize(answer);
    return g_executeAnswer.c_str();
}
