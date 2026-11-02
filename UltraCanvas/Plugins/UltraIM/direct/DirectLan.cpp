// Plugins/UltraIM/direct/DirectLan.cpp
// ChatCube Direct - the local network without a relay
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectLan.h"

#include "UltraCrypt/UltraCryptCore.h"
#include "UltraNet/UltraNetPlugins.h"
#include "UltraNet/UltraNetSocket.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace UltraIMDirect {

using UltraCanvas::JSONValue;

namespace {

const char* const kServiceType = "_chatcube._tcp";
constexpr size_t  kMaxFrame = 1024 * 1024;
constexpr int     kPollMs = 500;              // how often a blocked thread looks up
constexpr int64_t kHandshakeMs = 10000;       // a connection not proven by then is closed
constexpr int64_t kPingMs = 15000;
constexpr int64_t kSilenceMs = 45000;         // nothing heard for this long: the peer is gone
constexpr int64_t kForgetNearbyMs = 20000;    // not announced for this long: no longer nearby
constexpr int64_t kRedialMs = 5000;           // a failed or lost dial is retried after this

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string RandomNonce() {
    std::vector<uint8_t> bytes;
    UltraCrypt_RandomBytes(bytes, 16);
    return Base64(bytes);
}

std::string TxtValue(const std::vector<std::string>& txt, const std::string& key) {
    const std::string prefix = key + "=";
    for (const std::string& record : txt) {
        if (record.rfind(prefix, 0) == 0) return record.substr(prefix.size());
    }
    return std::string();
}

} // namespace

// One TCP connection. Only its reader thread closes the socket: a socket
// closed under a thread blocked in it could hand that thread the next
// socket to take its number.
struct Connection {
    UltraNetHandle socket = UltraNetInvalidHandle;
    bool outgoing = false;
    std::string address;                    // host:port we dialled (outgoing)
    std::string myNonce = RandomNonce();
    std::string peerId;                     // from its hello
    std::vector<uint8_t> peerKey;
    std::string peerName;
    std::string peerNonce;
    bool authed = false;
    std::atomic<bool> closed{false};
    int64_t startedMs = NowMs();
    std::atomic<int64_t> lastHeardMs{NowMs()};
    int64_t lastPingMs = 0;
    std::mutex sendMutex;
    std::thread reader;
};

struct LanTransport::Core {
    LanTransport* owner = nullptr;          // its callbacks
    const Identity* identity = nullptr;
    std::string displayName;

    mutable std::mutex mutex;
    std::condition_variable wake;
    bool running = false;
    UltraNetHandle listener = UltraNetInvalidHandle;
    int port = 0;
    int discoveryIntervalMs = 0;
    IServiceDiscoveryPlugin* discovery = nullptr;
    std::shared_ptr<IUltraNetPlugin> discoveryPlugin;   // keeps `discovery` alive
    UltraNetHandle advertisement = UltraNetInvalidHandle;

    std::vector<std::shared_ptr<Connection>> connections;
    std::map<std::string, std::shared_ptr<Connection>> active;   // peer id -> its connection
    std::set<std::string> connecting;                            // "id:<peer>" or "addr:<host:port>"
    std::map<std::string, int64_t> lastDialMs;                   // the same keys
    std::set<std::string> wanted;
    std::set<std::string> addresses;                             // host:port, dialled while nobody is reached there
    std::map<std::string, std::string> addressPeer;              // host:port -> the peer it proved to be
    std::map<std::string, std::pair<LanPeer, int64_t>> nearby;   // peer id -> (peer, last seen)

    std::thread acceptThread;
    std::thread discoveryThread;

    // ---- framing ----------------------------------------------------------------

    static bool WriteFrame(Connection& c, const JSONValue& frame) {
        const std::string text = UltraCanvas::JSON::Serialize(frame);
        std::vector<uint8_t> bytes(4 + text.size());
        const uint32_t n = static_cast<uint32_t>(text.size());
        bytes[0] = static_cast<uint8_t>(n >> 24);
        bytes[1] = static_cast<uint8_t>(n >> 16);
        bytes[2] = static_cast<uint8_t>(n >> 8);
        bytes[3] = static_cast<uint8_t>(n);
        std::copy(text.begin(), text.end(), bytes.begin() + 4);
        std::lock_guard<std::mutex> lock(c.sendMutex);
        if (c.closed) return false;                 // checked under the lock the reader closes with
        size_t at = 0;
        while (at < bytes.size()) {
            std::vector<uint8_t> rest(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
            int sent = 0;
            if (!UltraNet_TcpSend(c.socket, rest, sent).success || sent <= 0) {
                c.closed = true;
                return false;
            }
            at += static_cast<size_t>(sent);
        }
        return true;
    }

    // ---- connections ------------------------------------------------------------

    // Takes over a connected socket: starts its reader and says hello.
    void Adopt(UltraNetHandle socket, bool outgoing, const std::string& address) {
        auto c = std::make_shared<Connection>();
        c->socket = socket;
        c->outgoing = outgoing;
        c->address = address;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!running) {
                UltraNet_SocketClose(socket);
                return;
            }
            connections.push_back(c);
            c->reader = std::thread([this, c]() { ReadLoop(c); });
        }
        JSONValue hello = JSONValue::MakeObject();
        hello.Set("t", JSONValue("hello"));
        hello.Set("v", JSONValue(static_cast<int64_t>(1)));
        hello.Set("id", JSONValue(identity->userId));
        hello.Set("pk", JSONValue(Base64(identity->signPublic)));
        hello.Set("name", JSONValue(displayName));
        hello.Set("nonce", JSONValue(c->myNonce));
        WriteFrame(*c, hello);
    }

    void ReadLoop(std::shared_ptr<Connection> c) {
        std::vector<uint8_t> pending;
        std::vector<uint8_t> chunk;
        int fastFailures = 0;
        while (!c->closed) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!running) break;
            }
            const int64_t now = NowMs();
            if (!c->authed && now - c->startedMs > kHandshakeMs) break;
            if (now - c->lastHeardMs > kSilenceMs) break;
            chunk.clear();
            UltraNetResult r = UltraNet_TcpReceive(c->socket, chunk, 65536, kPollMs);
            if (r.success && chunk.empty()) break;              // the other side closed
            if (!r.success) {
                // A timeout takes the whole poll; a broken socket fails at once.
                if (NowMs() - now < kPollMs / 4) {
                    if (++fastFailures >= 3) break;
                } else {
                    fastFailures = 0;
                }
                continue;
            }
            fastFailures = 0;
            pending.insert(pending.end(), chunk.begin(), chunk.end());
            while (pending.size() >= 4) {
                const size_t n = (static_cast<size_t>(pending[0]) << 24) | (static_cast<size_t>(pending[1]) << 16) |
                                 (static_cast<size_t>(pending[2]) << 8) | static_cast<size_t>(pending[3]);
                if (n > kMaxFrame) {
                    c->closed = true;
                    break;
                }
                if (pending.size() < 4 + n) break;
                const std::string text(pending.begin() + 4, pending.begin() + 4 + static_cast<std::ptrdiff_t>(n));
                pending.erase(pending.begin(), pending.begin() + 4 + static_cast<std::ptrdiff_t>(n));
                c->lastHeardMs = NowMs();
                UltraCanvas::JSONParseResult parsed;
                JSONValue frame = UltraCanvas::JSON::Parse(text, &parsed);
                if (!parsed.success || !HandleFrame(c, frame)) {
                    c->closed = true;
                    break;
                }
            }
        }
        {
            // Not under a send in progress: a closed socket's number can come
            // back as the next socket's.
            std::lock_guard<std::mutex> sendLock(c->sendMutex);
            c->closed = true;
            UltraNet_SocketClose(c->socket);
        }
        bool wasActive = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = active.find(c->peerId);
            if (it != active.end() && it->second == c) {
                active.erase(it);
                wasActive = true;
            }
        }
        // Still in `connections`, so Stop waits for this callback too.
        if (wasActive && owner->onPeerDown) owner->onPeerDown(c->peerId);
        {
            std::lock_guard<std::mutex> lock(mutex);
            connections.erase(std::remove(connections.begin(), connections.end(), c), connections.end());
            // The thread cannot join itself, and Stop waits on `connections`
            // instead, so it goes its own way.
            if (c->reader.joinable()) c->reader.detach();
        }
        wake.notify_all();
    }

    // false: the connection is closed.
    bool HandleFrame(const std::shared_ptr<Connection>& c, const JSONValue& frame) {
        const std::string t = frame["t"].GetString();
        if (t == "hello") {
            if (!c->peerId.empty()) return false;                  // a second hello
            const std::vector<uint8_t> key = FromBase64(frame["pk"].GetString());
            const std::string id = frame["id"].GetString();
            if (key.size() != 32 || UserIdFor(key) != id || id == identity->userId) return false;
            c->peerId = id;
            c->peerKey = key;
            c->peerName = frame["name"].GetString();
            c->peerNonce = frame["nonce"].GetString();
            if (c->peerNonce.size() < 16) return false;
            JSONValue auth = JSONValue::MakeObject();
            auth.Set("t", JSONValue("auth"));
            auth.Set("sig", JSONValue(Sign(*identity, LanAuthMessage(c->peerNonce, identity->userId, id))));
            return WriteFrame(*c, auth);
        }
        if (t == "auth") {
            if (c->peerId.empty() || c->authed) return false;
            if (!Verify(c->peerKey, LanAuthMessage(c->myNonce, c->peerId, identity->userId), frame["sig"].GetString()))
                return false;
            c->authed = true;
            return Promote(c);
        }
        if (!c->authed) return false;                               // nothing else before the handshake
        if (t == "ping") {
            JSONValue pong = JSONValue::MakeObject();
            pong.Set("t", JSONValue("pong"));
            return WriteFrame(*c, pong);
        }
        if (t == "pong") return true;
        if (owner->onFrame) owner->onFrame(c->peerId, frame);
        return true;
    }

    // A proven connection becomes the peer's. Two at once (both dialled):
    // both ends keep the one the smaller user id opened.
    bool Promote(const std::shared_ptr<Connection>& c) {
        const std::string& me = identity->userId;
        const std::string smaller = std::min(me, c->peerId);
        auto initiator = [&](const Connection& x) { return x.outgoing ? me : x.peerId; };
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = active.find(c->peerId);
            if (it != active.end() && it->second != c && !it->second->closed) {
                const bool keepNew = initiator(*c) == smaller || initiator(*it->second) != smaller;
                if (!keepNew) return false;                         // this one goes
                it->second->closed = true;                          // the old one goes, quietly
                it->second = c;
            } else {
                active[c->peerId] = c;
                notify = true;
            }
            if (!c->address.empty()) addressPeer[c->address] = c->peerId;   // dialled again only while it is gone
        }
        if (notify && owner->onPeerUp) owner->onPeerUp(c->peerId, c->peerKey, c->peerName);
        return true;
    }

    // ---- threads ----------------------------------------------------------------

    void AcceptLoop() {
        for (;;) {
            UltraNetHandle l;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!running) break;
                l = listener;
            }
            UltraNetEndpoint remote;
            UltraNetHandle socket = UltraNet_TcpAccept(l, remote, kPollMs);
            if (socket != UltraNetInvalidHandle) Adopt(socket, false, std::string());
        }
    }

    // Dials on a thread of its own: a connect has no timeout, and Stop must
    // not wait for one. It hands its socket over only while running.
    void Dial(const std::string& host, int port, const std::string& key) {
        std::weak_ptr<Core> weak = self.lock();
        std::thread([weak, host, port, key]() {
            UltraNetSocketOptions options;
            options.noDelay = true;
            UltraNetHandle socket = UltraNet_TcpConnect(host, port, options);
            auto core = weak.lock();
            if (!core) {
                if (socket != UltraNetInvalidHandle) UltraNet_SocketClose(socket);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(core->mutex);
                core->connecting.erase(key);
            }
            if (socket == UltraNetInvalidHandle) return;
            core->Adopt(socket, true, key.rfind("addr:", 0) == 0 ? key.substr(5) : std::string());
        }).detach();
    }

    void Browse() {
        std::vector<UltraNetDirectoryEntry> entries;
        UltraNetDirectoryQuery query;
        query.baseDn = kServiceType;
        query.timeLimitSeconds = 2;
        if (!discovery || !discovery->Search("mdns://", query, entries).success) return;
        const int64_t now = NowMs();
        bool changed = false;
        std::lock_guard<std::mutex> lock(mutex);
        for (const UltraNetDirectoryEntry& entry : entries) {
            auto txt = entry.attributes.find("txt");
            auto port = entry.attributes.find("port");
            if (txt == entry.attributes.end() || port == entry.attributes.end() || port->second.empty()) continue;
            LanPeer peer;
            peer.userId = TxtValue(txt->second, "id");
            peer.displayName = TxtValue(txt->second, "name");
            peer.port = std::atoi(port->second[0].c_str());
            auto ip = entry.attributes.find("ip");
            auto host = entry.attributes.find("host");
            if (ip != entry.attributes.end() && !ip->second.empty()) peer.host = ip->second[0];
            else if (host != entry.attributes.end() && !host->second.empty()) peer.host = host->second[0];
            if (peer.userId.size() != 32 || peer.userId == identity->userId || peer.host.empty() || peer.port <= 0)
                continue;
            auto it = nearby.find(peer.userId);
            if (it == nearby.end() || it->second.first.displayName != peer.displayName) changed = true;
            nearby[peer.userId] = {peer, now};
        }
        for (auto it = nearby.begin(); it != nearby.end();) {
            if (now - it->second.second > kForgetNearbyMs) {
                it = nearby.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed) nearbyChanged = true;
    }
    bool nearbyChanged = false;
    std::weak_ptr<Core> self;

    void DiscoveryLoop() {
        int64_t lastBrowseMs = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!running) break;
                wake.wait_for(lock, std::chrono::milliseconds(kPollMs * 2));
                if (!running) break;
            }
            const int64_t now = NowMs();
            if (discovery && discoveryIntervalMs > 0 && now - lastBrowseMs >= discoveryIntervalMs) {
                lastBrowseMs = now;
                Browse();
            }
            bool notifyNearby = false;
            std::vector<std::pair<std::string, std::pair<std::string, int>>> dials;   // key, (host, port)
            std::vector<std::shared_ptr<Connection>> toPing;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!running) break;
                notifyNearby = nearbyChanged;
                nearbyChanged = false;
                // Both ends may dial each other; Promote keeps one connection.
                auto due = [&](const std::string& key) {
                    if (connecting.count(key)) return false;
                    auto last = lastDialMs.find(key);
                    if (last != lastDialMs.end() && now - last->second < kRedialMs) return false;
                    connecting.insert(key);
                    lastDialMs[key] = now;
                    return true;
                };
                for (const std::string& id : wanted) {
                    if (active.count(id)) continue;
                    auto it = nearby.find(id);
                    const std::string key = "id:" + id;
                    if (it == nearby.end() || !due(key)) continue;
                    dials.push_back({key, {it->second.first.host, it->second.first.port}});
                }
                for (const std::string& address : addresses) {
                    const size_t colon = address.rfind(':');
                    const std::string key = "addr:" + address;
                    auto known = addressPeer.find(address);
                    if (known != addressPeer.end() && active.count(known->second)) continue;   // reached
                    if (colon == std::string::npos || !due(key)) continue;
                    dials.push_back({key, {address.substr(0, colon), std::atoi(address.c_str() + colon + 1)}});
                }
                for (auto& entry : active) {
                    Connection& c = *entry.second;
                    if (now - c.lastPingMs >= kPingMs) {
                        c.lastPingMs = now;
                        toPing.push_back(entry.second);
                    }
                }
            }
            for (const auto& d : dials) Dial(d.second.first, d.second.second, d.first);
            JSONValue ping = JSONValue::MakeObject();
            ping.Set("t", JSONValue("ping"));
            for (const auto& c : toPing) WriteFrame(*c, ping);
            if (notifyNearby && owner->onNearbyChanged) owner->onNearbyChanged();
        }
    }
};

// ============================================================================
// LanTransport
// ============================================================================

LanTransport::LanTransport() : core_(std::make_shared<Core>()) {
    core_->owner = this;
    core_->self = core_;
}

LanTransport::~LanTransport() {
    Stop();
}

bool LanTransport::Start(const Identity* identity, const std::string& displayName, bool advertise,
                         int discoveryIntervalMs, std::string& error) {
    Stop();
    if (!identity || !identity->IsValid()) {
        error = "No identity";
        return false;
    }
    Core& c = *core_;
    UltraNetSocketOptions options;
    options.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(0, options);
    if (listener == UltraNetInvalidHandle) {
        error = "Cannot listen for local connections";
        return false;
    }
    UltraNetEndpoint local;
    UltraNet_SocketLocalEndpoint(listener, local);

    UltraNet_RefreshPlugins();
    std::shared_ptr<IUltraNetPlugin> plugin = UltraNet_GetPlugin("mdns");
    auto* discovery = dynamic_cast<IServiceDiscoveryPlugin*>(plugin.get());
    UltraNetHandle advertisement = UltraNetInvalidHandle;
    if (discovery && advertise) {
        UltraNetServiceAdvertisement ad;
        ad.serviceType = kServiceType;
        ad.instanceName = displayName.empty() ? "ChatCube" : displayName;
        ad.port = local.port;
        ad.txt = {"v=1", "id=" + identity->userId, "name=" + displayName.substr(0, 200)};
        if (!discovery->Advertise(ad, advertisement).success) advertisement = UltraNetInvalidHandle;
    }
    {
        std::lock_guard<std::mutex> lock(c.mutex);
        c.identity = identity;
        c.displayName = displayName;
        c.listener = listener;
        c.port = local.port;
        c.discoveryIntervalMs = discoveryIntervalMs;
        c.discovery = discovery;
        c.discoveryPlugin = plugin;
        c.advertisement = advertisement;
        c.running = true;
    }
    c.acceptThread = std::thread([&c]() { c.AcceptLoop(); });
    c.discoveryThread = std::thread([&c]() { c.DiscoveryLoop(); });
    return true;
}

void LanTransport::Stop() {
    Core& c = *core_;
    std::vector<std::shared_ptr<Connection>> connections;
    UltraNetHandle advertisement = UltraNetInvalidHandle;
    IServiceDiscoveryPlugin* discovery = nullptr;
    {
        std::lock_guard<std::mutex> lock(c.mutex);
        if (!c.running) return;
        c.running = false;
        connections = c.connections;
        advertisement = c.advertisement;
        discovery = c.discovery;
        c.advertisement = UltraNetInvalidHandle;
    }
    c.wake.notify_all();
    if (c.acceptThread.joinable()) c.acceptThread.join();
    if (c.discoveryThread.joinable()) c.discoveryThread.join();
    UltraNet_SocketClose(c.listener);
    // Readers see `running` within kPollMs; wait until each has finished.
    for (const auto& conn : connections) conn->closed = true;
    for (;;) {
        std::unique_lock<std::mutex> lock(c.mutex);
        if (c.connections.empty()) break;
        c.wake.wait_for(lock, std::chrono::milliseconds(50));
    }
    if (discovery && advertisement != UltraNetInvalidHandle) discovery->StopAdvertising(advertisement);
    std::lock_guard<std::mutex> lock(c.mutex);
    c.active.clear();
    c.connecting.clear();
    c.lastDialMs.clear();
    c.nearby.clear();
    c.listener = UltraNetInvalidHandle;
    c.discovery = nullptr;
    c.discoveryPlugin.reset();
}

bool LanTransport::IsRunning() const {
    std::lock_guard<std::mutex> lock(core_->mutex);
    return core_->running;
}

int LanTransport::GetPort() const {
    std::lock_guard<std::mutex> lock(core_->mutex);
    return core_->port;
}

bool LanTransport::HasDiscovery() const {
    std::lock_guard<std::mutex> lock(core_->mutex);
    return core_->discovery != nullptr;
}

bool LanTransport::IsConnected(const std::string& peerId) const {
    std::lock_guard<std::mutex> lock(core_->mutex);
    auto it = core_->active.find(peerId);
    return it != core_->active.end() && !it->second->closed;
}

bool LanTransport::Send(const std::string& peerId, const JSONValue& frame) {
    std::shared_ptr<Connection> c;
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        auto it = core_->active.find(peerId);
        if (it == core_->active.end()) return false;
        c = it->second;
    }
    return Core::WriteFrame(*c, frame);
}

void LanTransport::SetWanted(const std::set<std::string>& userIds) {
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        core_->wanted = userIds;
    }
    core_->wake.notify_all();
}

void LanTransport::AddWanted(const std::string& userId) {
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        core_->wanted.insert(userId);
    }
    core_->wake.notify_all();
}

void LanTransport::AddAddress(const std::string& host, int port) {
    if (host.empty() || port <= 0 || port > 65535) return;
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        core_->addresses.insert(host + ":" + std::to_string(port));
    }
    core_->wake.notify_all();
}

std::vector<LanPeer> LanTransport::Nearby() {
    std::lock_guard<std::mutex> lock(core_->mutex);
    std::vector<LanPeer> out;
    for (const auto& entry : core_->nearby) out.push_back(entry.second.first);
    return out;
}

} // namespace UltraIMDirect
