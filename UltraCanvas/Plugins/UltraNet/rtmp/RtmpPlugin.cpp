// UltraCanvas/Plugins/UltraNet/rtmp/RtmpPlugin.cpp
// RTMP plug-in: plays a stream (IStreamingProtocolPlugin) with an RTMP client
// of its own for rtmp:// - the libcurl UltraNet is built with has no RTMP
// (libcurl dropped librtmp), so the plug-in that relied on it could never
// connect.
//
// OpenStream(url) - rtmp://host[:port]/app[/instance]/playpath[?query]:
//   - a TCP connection made by UltraNet (UltraNet_TcpOpen), through the
//     proxy setting (a SOCKS proxy, or an HTTP proxy as a CONNECT tunnel),
//     whose socket the plug-in then owns;
//   - the simple handshake (C0/C1/C2 - S0/S1/S2), which every server
//     answers;
//   - the chunk stream: SetChunkSize 4096 out; in, any chunk size the
//     server sets, every header form (0-3), extended timestamps; Window
//     Acknowledgement Size honoured with Acknowledgements; a ping answered;
//   - AMF0 commands: connect (app, tcUrl), createStream, play (with a
//     buffer length), waiting for NetStream.Play.Start or the reason it
//     was refused (_error, NetStream.Play.StreamNotFound / Failed).
//
// ReadFrame(handle, outFrame):
//   - the stream as FLV, as librtmp gave it: the FLV header first, then one
//     tag per audio, video or data message (an aggregate message split
//     into its tags, their times put on the message's), in order. Waits up
//     to five seconds for one; an empty frame when none came. A stream the
//     server ended reports ConnectionReset. For frame-accurate demuxing
//     hand the bytes to FFmpeg / GStreamer, as with the RTSP plug-in.
//
// rtmps, rtmpe and rtmpt (TLS, Adobe's encryption, HTTP tunnelling) are
// offered, and served through libcurl, only where the libcurl loaded has
// RTMP; a URL without credentials support beyond what its query carries.
//
// Version: 0.2.0 - an RTMP client of its own for rtmp://, through the proxy
//                  setting; libcurl only for the other schemes where it has RTMP
// Version: 0.1.2 - every entry point is guarded (UltraNetPluginGuard.h): an
//                  exception becomes a PluginError result or an invalid
//                  handle, never ends the process
// Version: 0.1.1
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetSocket.h>   // UltraNet_TcpOpen / UltraNet_SocketDetach
#include "UltraNetPluginGuard.h"
#include "UltraNetPluginHostShim.h"

#include <curl/curl.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// ---- The socket --------------------------------------------------------------

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void CloseSocket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void CloseSocket(Socket s) { ::close(s); }
#endif

// Whether `s` has bytes to read within `ms`.
bool Readable(Socket s, int ms) {
#if defined(_WIN32)
    WSAPOLLFD p{};
    p.fd = s;
    p.events = POLLRDNORM;
    return WSAPoll(&p, 1, ms) > 0;
#else
    pollfd p{};
    p.fd = s;
    p.events = POLLIN;
    return ::poll(&p, 1, ms) > 0;
#endif
}

bool SendAll(Socket s, const std::vector<uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32)
        const int n = ::send(s, reinterpret_cast<const char*>(data.data()) + sent,
                             static_cast<int>(data.size() - sent), 0);
#elif defined(MSG_NOSIGNAL)
        const ssize_t n = ::send(s, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else   // macOS: the socket carries SO_NOSIGPIPE instead (Play)
        const ssize_t n = ::send(s, data.data() + sent, data.size() - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// ---- Bytes -------------------------------------------------------------------

uint32_t Be(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

void PutBe(std::vector<uint8_t>& out, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

// ---- AMF0 --------------------------------------------------------------------

void AmfNumber(std::vector<uint8_t>& out, double d) {
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof bits);
    out.push_back(0x00);
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
}

void AmfBoolean(std::vector<uint8_t>& out, bool b) {
    out.push_back(0x01);
    out.push_back(b ? 1 : 0);
}

void AmfRawString(std::vector<uint8_t>& out, const std::string& s) {
    PutBe(out, static_cast<uint32_t>(s.size()), 2);
    out.insert(out.end(), s.begin(), s.end());
}

void AmfString(std::vector<uint8_t>& out, const std::string& s) {
    out.push_back(0x02);
    AmfRawString(out, s);
}

void AmfNull(std::vector<uint8_t>& out) { out.push_back(0x05); }

void AmfObjectEnd(std::vector<uint8_t>& out) {
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x09);
}

struct AmfValue {
    enum class Kind { Number, Boolean, String, Object, Null, Other } kind = Kind::Null;
    double number = 0;
    bool boolean = false;
    std::string string;
    // An object's properties: names[i] is values[i]'s.
    std::vector<std::string> names;
    std::vector<AmfValue> values;

    std::string Prop(const std::string& name) const {
        for (std::size_t i = 0; i < names.size() && i < values.size(); ++i)
            if (names[i] == name && values[i].kind == Kind::String) return values[i].string;
        return {};
    }
};

bool AmfRead(const uint8_t*& p, const uint8_t* end, AmfValue& out, int depth = 0);

bool AmfReadProps(const uint8_t*& p, const uint8_t* end, AmfValue& out, int depth) {
    while (end - p >= 3) {
        const uint32_t len = Be(p, 2);
        if (len == 0 && p[2] == 0x09) {
            p += 3;
            return true;
        }
        p += 2;
        if (static_cast<uint32_t>(end - p) < len) return false;
        std::string name(reinterpret_cast<const char*>(p), len);
        p += len;
        AmfValue value;
        if (!AmfRead(p, end, value, depth + 1)) return false;
        out.names.push_back(std::move(name));
        out.values.push_back(std::move(value));
    }
    return false;
}

bool AmfRead(const uint8_t*& p, const uint8_t* end, AmfValue& out, int depth) {
    if (p >= end || depth > 16) return false;
    const uint8_t marker = *p++;
    switch (marker) {
        case 0x00: {   // number
            if (end - p < 8) return false;
            uint64_t bits = 0;
            for (int i = 0; i < 8; ++i) bits = (bits << 8) | p[i];
            p += 8;
            std::memcpy(&out.number, &bits, sizeof bits);
            out.kind = AmfValue::Kind::Number;
            return true;
        }
        case 0x01:     // boolean
            if (p >= end) return false;
            out.boolean = *p++ != 0;
            out.kind = AmfValue::Kind::Boolean;
            return true;
        case 0x02:     // string
        case 0x0C: {   // long string
            const int n = marker == 0x02 ? 2 : 4;
            if (end - p < n) return false;
            const uint32_t len = Be(p, n);
            p += n;
            if (static_cast<uint32_t>(end - p) < len) return false;
            out.string.assign(reinterpret_cast<const char*>(p), len);
            p += len;
            out.kind = AmfValue::Kind::String;
            return true;
        }
        case 0x03:     // object
            out.kind = AmfValue::Kind::Object;
            return AmfReadProps(p, end, out, depth);
        case 0x08:     // ECMA array: a count, then as an object
            if (end - p < 4) return false;
            p += 4;
            out.kind = AmfValue::Kind::Object;
            return AmfReadProps(p, end, out, depth);
        case 0x05:     // null
        case 0x06:     // undefined
            out.kind = AmfValue::Kind::Null;
            return true;
        case 0x0A: {   // strict array
            if (end - p < 4) return false;
            const uint32_t count = Be(p, 4);
            p += 4;
            for (uint32_t i = 0; i < count; ++i) {
                AmfValue item;
                if (!AmfRead(p, end, item, depth + 1)) return false;
            }
            out.kind = AmfValue::Kind::Other;
            return true;
        }
        case 0x0B:     // date
            if (end - p < 10) return false;
            p += 10;
            out.kind = AmfValue::Kind::Other;
            return true;
        default:
            return false;
    }
}

// ---- The address -------------------------------------------------------------

struct RtmpAddress {
    std::string host;
    int port = 1935;
    std::string app;        // "live", or "app/instance"
    std::string playPath;   // "stream?key=..."
    std::string tcUrl;      // rtmp://host[:port]/app
};

bool ParseRtmpUrl(const std::string& url, RtmpAddress& out, std::string& error) {
    const std::string prefix = "rtmp://";
    if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) {
        error = "not an rtmp:// address: " + url;
        return false;
    }
    std::string rest = url.substr(prefix.size());
    const std::size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    const std::string path = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    std::string portText;
    if (!authority.empty() && authority[0] == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string::npos) { error = "a bad address: " + url; return false; }
        out.host = authority.substr(1, close - 1);
        if (close + 1 < authority.size() && authority[close + 1] == ':') portText = authority.substr(close + 2);
    } else {
        const std::size_t colon = authority.find(':');
        out.host = authority.substr(0, colon);
        if (colon != std::string::npos) portText = authority.substr(colon + 1);
    }
    if (!portText.empty()) {
        int port = 0;
        for (const char c : portText) {
            if (c < '0' || c > '9') { error = "a bad port: " + url; return false; }
            port = port * 10 + (c - '0');
            if (port > 65535) { error = "a bad port: " + url; return false; }
        }
        out.port = port;
    }
    // The application is the path up to its last part; the play path is
    // that part, its query included (a stream key, a token).
    const std::size_t query = path.find('?');
    const std::string plain = query == std::string::npos ? path : path.substr(0, query);
    const std::size_t last = plain.rfind('/');
    if (out.host.empty() || last == std::string::npos || last == 0 || last + 1 >= plain.size()) {
        error = "the address names no application and stream (rtmp://host/app/stream): " + url;
        return false;
    }
    out.app = plain.substr(0, last);
    out.playPath = path.substr(last + 1);
    const std::string hostPart = out.host.find(':') != std::string::npos ? "[" + out.host + "]" : out.host;
    out.tcUrl = prefix + hostPart + (portText.empty() ? std::string() : ":" + portText) + "/" + out.app;
    return true;
}

// ---- A connection ------------------------------------------------------------

struct Message {
    uint8_t type = 0;
    uint32_t timestamp = 0;
    uint32_t streamId = 0;
    std::vector<uint8_t> payload;
};

struct ChunkStream {
    uint32_t timestamp = 0;
    uint32_t delta = 0;
    uint32_t length = 0;
    uint8_t type = 0;
    uint32_t streamId = 0;
    bool extended = false;
    std::vector<uint8_t> payload;   // the message being put together
};

constexpr uint32_t kOutChunkSize = 4096;
constexpr uint32_t kOurWindow = 2500000;

struct Connection {
    Socket socket = kNoSocket;
    std::vector<uint8_t> in;
    std::size_t inPos = 0;
    uint32_t inChunkSize = 128;
    std::map<uint32_t, ChunkStream> chunkStreams;
    uint64_t received = 0;
    uint64_t acknowledged = 0;
    uint32_t window = kOurWindow;
    bool ended = false;

    ~Connection() {
        if (socket != kNoSocket) CloseSocket(socket);
    }

    // Reads what arrives within `ms` into the buffer. False when the
    // connection has closed or failed.
    bool Fill(int ms) {
        if (!Readable(socket, ms)) return true;   // nothing yet
        uint8_t buf[16384];
#if defined(_WIN32)
        const int n = ::recv(socket, reinterpret_cast<char*>(buf), static_cast<int>(sizeof buf), 0);
#else
        const ssize_t n = ::recv(socket, buf, sizeof buf, 0);
#endif
        if (n <= 0) {
            ended = true;
            return false;
        }
        if (inPos > 0 && inPos == in.size()) {
            in.clear();
            inPos = 0;
        }
        in.insert(in.end(), buf, buf + n);
        received += static_cast<uint64_t>(n);
        // The server stops sending when its window is not acknowledged.
        if (received - acknowledged >= window) {
            std::vector<uint8_t> ack;
            PutBe(ack, static_cast<uint32_t>(received & 0xFFFFFFFFu), 4);
            SendMessage(2, 3, 0, 0, ack);
            acknowledged = received;
        }
        return true;
    }

    // Exactly `n` bytes, within `ms`.
    bool ReadExactly(std::vector<uint8_t>& out, std::size_t n, int ms) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (in.size() - inPos < n) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0 || !Fill(static_cast<int>(left))) return false;
        }
        out.assign(in.begin() + static_cast<std::ptrdiff_t>(inPos),
                   in.begin() + static_cast<std::ptrdiff_t>(inPos + n));
        inPos += n;
        return true;
    }

    bool SendMessage(uint32_t csid, uint8_t type, uint32_t streamId, uint32_t timestamp,
                     const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> out;
        const uint32_t ts = timestamp >= 0xFFFFFF ? 0xFFFFFF : timestamp;
        std::size_t pos = 0;
        bool first = true;
        do {
            out.push_back(static_cast<uint8_t>((first ? 0x00 : 0xC0) | (csid & 0x3F)));
            if (first) {
                PutBe(out, ts, 3);
                PutBe(out, static_cast<uint32_t>(payload.size()), 3);
                out.push_back(type);
                for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((streamId >> (8 * i)) & 0xFF));
            }
            if (ts == 0xFFFFFF) PutBe(out, timestamp, 4);
            const std::size_t take = std::min<std::size_t>(kOutChunkSize, payload.size() - pos);
            out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(pos),
                       payload.begin() + static_cast<std::ptrdiff_t>(pos + take));
            pos += take;
            first = false;
        } while (pos < payload.size());
        return SendAll(socket, out);
    }

    enum class Step { Message, Progress, NeedMore, Broken };

    // One chunk from the buffer: a whole message when it completed one. The
    // chunk stream's state changes only once the whole chunk is there.
    Step ParseChunk(Message& out) {
        const std::size_t avail = in.size() - inPos;
        const uint8_t* p = in.data() + inPos;
        if (avail < 1) return Step::NeedMore;
        const uint8_t fmt = p[0] >> 6;
        uint32_t csid = p[0] & 0x3F;
        std::size_t used = 1;
        if (csid == 0) {
            if (avail < 2) return Step::NeedMore;
            csid = 64 + p[1];
            used = 2;
        } else if (csid == 1) {
            if (avail < 3) return Step::NeedMore;
            csid = 64 + p[1] + 256u * p[2];
            used = 3;
        }
        const std::size_t headerLen = fmt == 0 ? 11 : fmt == 1 ? 7 : fmt == 2 ? 3 : 0;
        if (avail < used + headerLen) return Step::NeedMore;
        ChunkStream& cs = chunkStreams[csid];
        const uint8_t* h = p + used;
        uint32_t tsField = 0, length = cs.length, streamId = cs.streamId;
        uint8_t type = cs.type;
        if (fmt <= 2) tsField = Be(h, 3);
        if (fmt <= 1) {
            length = Be(h + 3, 3);
            type = h[6];
        }
        if (fmt == 0)
            streamId = static_cast<uint32_t>(h[7]) | (static_cast<uint32_t>(h[8]) << 8) |
                       (static_cast<uint32_t>(h[9]) << 16) | (static_cast<uint32_t>(h[10]) << 24);
        used += headerLen;
        const bool extended = fmt <= 2 ? tsField == 0xFFFFFF : cs.extended;
        uint32_t ts = tsField;
        if (extended) {
            if (avail < used + 4) return Step::NeedMore;
            ts = Be(p + used, 4);
            used += 4;
        }
        if (length > 16 * 1024 * 1024) return Step::Broken;   // not a stream anyone sends
        const bool starts = cs.payload.empty();
        const std::size_t have = starts ? 0 : cs.payload.size();
        if (have > length) return Step::Broken;
        const std::size_t take = std::min<std::size_t>(inChunkSize, length - have);
        if (avail < used + take) return Step::NeedMore;

        // The whole chunk is here: take it.
        if (fmt == 0) {
            cs.timestamp = ts;
            cs.delta = 0;
        } else if (fmt == 1 || fmt == 2) {
            cs.delta = ts;
            cs.timestamp += ts;
        } else if (starts) {
            cs.timestamp += cs.delta;
        }
        if (fmt <= 2) cs.extended = extended;
        cs.length = length;
        cs.type = type;
        cs.streamId = streamId;
        if (starts) cs.payload.reserve(length);
        cs.payload.insert(cs.payload.end(), p + used, p + used + take);
        inPos += used + take;
        if (cs.payload.size() < cs.length) return Step::Progress;
        out.type = cs.type;
        out.timestamp = cs.timestamp;
        out.streamId = cs.streamId;
        out.payload = std::move(cs.payload);
        cs.payload.clear();
        return Step::Message;
    }

    // The next message within `ms`, protocol control messages handled on the
    // way. False with `ended` set when the connection closed, or on a timeout.
    bool NextMessage(Message& out, int ms, std::string& error) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            const Step step = ParseChunk(out);
            if (step == Step::Broken) {
                error = "the server sent a message larger than any stream";
                ended = true;
                return false;
            }
            if (step == Step::Message) {
                if (Control(out)) continue;
                return true;
            }
            if (step == Step::Progress) continue;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return false;
            if (!Fill(static_cast<int>(std::min<long long>(left, 1000)))) {
                error = "the server closed the connection";
                return false;
            }
        }
    }

    // A protocol control or user control message, handled; true when it was one.
    bool Control(const Message& m) {
        const auto& d = m.payload;
        switch (m.type) {
            case 1:   // Set Chunk Size
                if (d.size() >= 4) inChunkSize = std::max<uint32_t>(1, Be(d.data(), 4) & 0x7FFFFFFF);
                return true;
            case 2:   // Abort
                if (d.size() >= 4) chunkStreams[Be(d.data(), 4)].payload.clear();
                return true;
            case 3:   // Acknowledgement
                return true;
            case 4:   // User Control
                if (d.size() >= 2) {
                    const uint32_t event = Be(d.data(), 2);
                    if (event == 6 && d.size() >= 6) {   // Ping Request: answer it
                        std::vector<uint8_t> pong;
                        PutBe(pong, 7, 2);
                        pong.insert(pong.end(), d.begin() + 2, d.begin() + 6);
                        SendMessage(2, 4, 0, 0, pong);
                    } else if (event == 1) {             // Stream EOF
                        ended = true;
                    }
                }
                return true;
            case 5:   // Window Acknowledgement Size
                if (d.size() >= 4) window = std::max<uint32_t>(1, Be(d.data(), 4));
                return true;
            case 6: { // Set Peer Bandwidth: say ours
                std::vector<uint8_t> size;
                PutBe(size, kOurWindow, 4);
                SendMessage(2, 5, 0, 0, size);
                return true;
            }
            default:
                return false;
        }
    }
};

// A command message's name, transaction and arguments; false for another.
bool ReadCommand(const Message& m, std::string& name, double& transaction, std::vector<AmfValue>& args) {
    if (m.type != 20 && m.type != 17) return false;
    const uint8_t* p = m.payload.data();
    const uint8_t* end = p + m.payload.size();
    if (m.type == 17 && p < end) ++p;   // AMF3 command: an AMF0 body after one byte
    AmfValue v;
    if (!AmfRead(p, end, v) || v.kind != AmfValue::Kind::String) return false;
    name = v.string;
    AmfValue t;
    if (!AmfRead(p, end, t)) return false;
    transaction = t.kind == AmfValue::Kind::Number ? t.number : 0;
    args.clear();
    while (p < end) {
        AmfValue a;
        if (!AmfRead(p, end, a)) break;
        args.push_back(std::move(a));
    }
    return true;
}

// What the server said in an _error or onStatus: its code and description.
std::string StatusText(const std::vector<AmfValue>& args) {
    for (const AmfValue& a : args) {
        if (a.kind != AmfValue::Kind::Object) continue;
        const std::string code = a.Prop("code");
        const std::string description = a.Prop("description");
        if (!code.empty() || !description.empty())
            return description.empty() ? code : code + ": " + description;
    }
    return "no reason given";
}

// ---- FLV ---------------------------------------------------------------------

void AppendFlvTag(std::vector<uint8_t>& out, uint8_t type, uint32_t timestamp, const uint8_t* data, std::size_t size) {
    out.push_back(type);
    PutBe(out, static_cast<uint32_t>(size), 3);
    PutBe(out, timestamp & 0xFFFFFF, 3);
    out.push_back(static_cast<uint8_t>((timestamp >> 24) & 0xFF));
    PutBe(out, 0, 3);
    out.insert(out.end(), data, data + size);
    PutBe(out, static_cast<uint32_t>(11 + size), 4);
}

// A media message as FLV tags: audio, video and data as one tag each, an
// aggregate as the tags it holds, their times moved onto the message's.
// False for any other message.
// A data message's body as an FLV script tag holds it: without the
// "@setDataFrame" a publisher's metadata arrives wrapped in, so the tag starts
// with onMetaData, as a player expects.
void AppendDataTag(std::vector<uint8_t>& out, uint32_t timestamp, const uint8_t* data, std::size_t size) {
    static const uint8_t kWrapper[] = {0x02, 0x00, 0x0D, '@', 's', 'e', 't', 'D', 'a', 't', 'a',
                                       'F', 'r', 'a', 'm', 'e'};
    if (size > sizeof kWrapper && std::memcmp(data, kWrapper, sizeof kWrapper) == 0) {
        data += sizeof kWrapper;
        size -= sizeof kWrapper;
    }
    AppendFlvTag(out, 18, timestamp, data, size);
}

bool ToFlv(const Message& m, std::vector<uint8_t>& out) {
    if (m.type == 8 || m.type == 9) {
        AppendFlvTag(out, m.type, m.timestamp, m.payload.data(), m.payload.size());
        return true;
    }
    if (m.type == 18) {
        AppendDataTag(out, m.timestamp, m.payload.data(), m.payload.size());
        return true;
    }
    if (m.type == 15 && !m.payload.empty()) {   // AMF3 data: an AMF0 body after one byte
        AppendDataTag(out, m.timestamp, m.payload.data() + 1, m.payload.size() - 1);
        return true;
    }
    if (m.type != 22) return false;
    const uint8_t* p = m.payload.data();
    const uint8_t* end = p + m.payload.size();
    bool first = true;
    uint32_t base = 0;
    while (end - p >= 11) {
        const uint8_t type = p[0];
        const uint32_t size = Be(p + 1, 3);
        const uint32_t ts = Be(p + 4, 3) | (static_cast<uint32_t>(p[7]) << 24);
        if (static_cast<std::size_t>(end - p) < 11 + size + 4) break;
        if (first) {
            base = ts;
            first = false;
        }
        AppendFlvTag(out, type, m.timestamp + (ts - base), p + 11, size);
        p += 11 + size + 4;
    }
    return !first;
}

// ---- Sessions ----------------------------------------------------------------

struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    std::mutex mutex;   // one reader at a time
    std::unique_ptr<Connection> rtmp;
    std::deque<std::vector<uint8_t>> frames;
    bool headerGiven = false;
    CURL* easy = nullptr;   // the libcurl route (rtmps, rtmpt, ...)

    ~Session() {
        if (easy) curl_easy_cleanup(easy);
    }
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

UltraNetHandle Register(std::shared_ptr<Session> s) {
    const UltraNetHandle h = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
    s->handle = h;
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    g_sessions[h] = std::move(s);
    return h;
}

bool LibcurlHasRtmp() {
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    if (!v || !v->protocols) return false;
    for (const char* const* p = v->protocols; *p; ++p)
        if (std::strcmp(*p, "rtmp") == 0) return true;
    return false;
}

// Connects, shakes hands and starts playing; an error message on failure.
std::unique_ptr<Connection> Play(const std::string& url, const UltraNetStreamOptions& options,
                                 std::deque<std::vector<uint8_t>>& frames, std::string& error) {
    RtmpAddress where;
    if (!ParseRtmpUrl(url, where, error)) return nullptr;
    const int timeout = options.connectTimeoutMs > 0 ? options.connectTimeoutMs : 10000;

    // The connection, through the proxy setting.
    UltraNetSocketOptions tcp;
    tcp.connectTimeoutMs = timeout;
    tcp.proxy = UltraNet_ResolveProxy(url, UltraNetProxyConfig());
    UltraNetHandle handle = UltraNetInvalidHandle;
    const UltraNetResult opened = UltraNet_TcpOpen(where.host, where.port, tcp, handle);
    if (!opened) {
        error = opened.message;
        return nullptr;
    }
    auto c = std::make_unique<Connection>();
    c->socket = static_cast<Socket>(UltraNet_SocketDetach(handle));
    if (c->socket == kNoSocket) {
        error = "the connection could not be taken over";
        return nullptr;
    }
#if defined(SO_NOSIGPIPE)
    {
        const int on = 1;
        setsockopt(c->socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif

    // The handshake: C0 and C1, S0 S1 S2 back, C2 (S1 echoed).
    std::vector<uint8_t> c01(1 + 1536, 0);
    c01[0] = 0x03;
    std::mt19937 random(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (std::size_t i = 9; i < c01.size(); ++i) c01[i] = static_cast<uint8_t>(random() & 0xFF);
    if (!SendAll(c->socket, c01)) { error = "the handshake could not be sent"; return nullptr; }
    std::vector<uint8_t> s0, s1, s2;
    if (!c->ReadExactly(s0, 1, timeout) || !c->ReadExactly(s1, 1536, timeout)) {
        error = "no answer to the handshake - is it an RTMP server?";
        return nullptr;
    }
    if (s0[0] != 0x03) { error = "the server speaks RTMP version " + std::to_string(s0[0]); return nullptr; }
    if (!SendAll(c->socket, s1) || !c->ReadExactly(s2, 1536, timeout)) {
        error = "the handshake was not finished";
        return nullptr;
    }

    // Larger chunks out; then connect.
    std::vector<uint8_t> size;
    PutBe(size, kOutChunkSize, 4);
    c->SendMessage(2, 1, 0, 0, size);

    std::vector<uint8_t> connect;
    AmfString(connect, "connect");
    AmfNumber(connect, 1);
    connect.push_back(0x03);
    AmfRawString(connect, "app");          AmfString(connect, where.app);
    AmfRawString(connect, "flashVer");     AmfString(connect, "LNX 9,0,124,2");
    AmfRawString(connect, "tcUrl");        AmfString(connect, where.tcUrl);
    AmfRawString(connect, "fpad");         AmfBoolean(connect, false);
    AmfRawString(connect, "capabilities"); AmfNumber(connect, 15);
    AmfRawString(connect, "audioCodecs");  AmfNumber(connect, 3191);
    AmfRawString(connect, "videoCodecs");  AmfNumber(connect, 252);
    AmfRawString(connect, "videoFunction"); AmfNumber(connect, 1);
    AmfObjectEnd(connect);
    if (!c->SendMessage(3, 20, 0, 0, connect)) { error = "connect could not be sent"; return nullptr; }

    // Waits for the answer to `transaction`, queueing media on the way.
    auto await = [&](double transaction, std::vector<AmfValue>& args, const char* what) -> bool {
        Message m;
        std::string name;
        double t = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        for (;;) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0 || !c->NextMessage(m, static_cast<int>(left), error)) {
                if (error.empty()) error = std::string("no answer to ") + what;
                return false;
            }
            std::vector<uint8_t> flv;
            if (ToFlv(m, flv)) { frames.push_back(std::move(flv)); continue; }
            if (!ReadCommand(m, name, t, args)) continue;
            if (t != transaction) continue;
            if (name == "_result") return true;
            if (name == "_error") {
                error = std::string(what) + " was refused: " + StatusText(args);
                return false;
            }
        }
    };
    std::vector<AmfValue> args;
    if (!await(1, args, "connect")) return nullptr;

    std::vector<uint8_t> create;
    AmfString(create, "createStream");
    AmfNumber(create, 2);
    AmfNull(create);
    if (!c->SendMessage(3, 20, 0, 0, create)) { error = "createStream could not be sent"; return nullptr; }
    if (!await(2, args, "createStream")) return nullptr;
    uint32_t streamId = 1;
    for (const AmfValue& a : args)
        if (a.kind == AmfValue::Kind::Number) streamId = static_cast<uint32_t>(a.number);

    // Three seconds of buffer, then play.
    std::vector<uint8_t> buffer;
    PutBe(buffer, 3, 2);
    PutBe(buffer, streamId, 4);
    PutBe(buffer, 3000, 4);
    c->SendMessage(2, 4, 0, 0, buffer);
    std::vector<uint8_t> play;
    AmfString(play, "play");
    AmfNumber(play, 0);
    AmfNull(play);
    AmfString(play, where.playPath);
    AmfNumber(play, -2);
    if (!c->SendMessage(8, 20, streamId, 0, play)) { error = "play could not be sent"; return nullptr; }

    // NetStream.Play.Start, or why not.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    for (;;) {
        Message m;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0 || !c->NextMessage(m, static_cast<int>(left), error)) {
            if (error.empty()) error = "the server did not start playing " + where.playPath;
            return nullptr;
        }
        std::vector<uint8_t> flv;
        if (ToFlv(m, flv)) {
            frames.push_back(std::move(flv));
            return c;   // media is flowing
        }
        std::string name;
        double t = 0;
        if (!ReadCommand(m, name, t, args) || name != "onStatus") continue;
        std::string code;
        for (const AmfValue& a : args)
            if (a.kind == AmfValue::Kind::Object) code = a.Prop("code");
        if (code == "NetStream.Play.Start" || code == "NetStream.Play.Reset") return c;
        if (code.find("Failed") != std::string::npos || code.find("NotFound") != std::string::npos ||
            code.find("Rejected") != std::string::npos) {
            error = "play was refused: " + StatusText(args);
            return nullptr;
        }
    }
}

class RtmpPlugin : public IStreamingProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-RTMP"; }
    std::string GetVersion() const override { return "0.2.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        // rtmp:// is played here; the others only through a libcurl with RTMP.
        if (LibcurlHasRtmp()) return {"rtmp", "rtmps", "rtmpe", "rtmpt", "rtmpte", "rtmpts"};
        return {"rtmp"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return ultranet_plugin::Guarded("Initialize", [&]() -> UltraNetResult {
            return UltraNetResult::Ok();
        });
    }
    void Shutdown() override {
        ultranet_plugin::GuardedVoid([&]() {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions.clear();
        });
    }

    UltraNetHandle OpenStream(const std::string& url,
                              const UltraNetStreamOptions& options) override {
        return ultranet_plugin::GuardedHandle([&]() -> UltraNetHandle {
            if (url.empty()) return UltraNetInvalidHandle;
            auto s = std::make_shared<Session>();
            if (url.compare(0, 7, "rtmp://") == 0) {
                std::string error;
                s->rtmp = Play(url, options, s->frames, error);
                if (!s->rtmp) return UltraNetInvalidHandle;   // the interface carries no reason
                return Register(std::move(s));
            }
            // TLS, encryption and HTTP tunnelling: libcurl's librtmp, where
            // the libcurl loaded has it.
            if (!LibcurlHasRtmp()) return UltraNetInvalidHandle;
            s->easy = curl_easy_init();
            if (!s->easy) return UltraNetInvalidHandle;
            curl_easy_setopt(s->easy, CURLOPT_URL, url.c_str());
            curl_easy_setopt(s->easy, CURLOPT_CONNECT_ONLY, 1L);
            curl_easy_setopt(s->easy, CURLOPT_NOSIGNAL, 1L);
            if (options.connectTimeoutMs > 0)
                curl_easy_setopt(s->easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options.connectTimeoutMs));
            if (curl_easy_perform(s->easy) != CURLE_OK) return UltraNetInvalidHandle;
            return Register(std::move(s));
        });
    }

    UltraNetResult ReadFrame(UltraNetHandle handle,
                             std::vector<uint8_t>& outFrame) override {
        return ultranet_plugin::Guarded("ReadFrame", [&]() -> UltraNetResult {
            outFrame.clear();
            auto s = Find(handle);
            if (!s) return UltraNetResult::Error(UltraNetResultCode::InvalidHandle, "no such RTMP session");
            std::lock_guard<std::mutex> lk(s->mutex);

            if (s->easy) {
                std::vector<uint8_t> buf(8 * 1024);
                std::size_t got = 0;
                const CURLcode rc = curl_easy_recv(s->easy, buf.data(), buf.size(), &got);
                if (rc != CURLE_OK && rc != CURLE_AGAIN)
                    return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed, curl_easy_strerror(rc));
                buf.resize(got);
                outFrame = std::move(buf);
                return UltraNetResult::Ok();
            }

            Connection& c = *s->rtmp;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            std::string error;
            while (s->frames.empty() && !c.ended) {
                Message m;
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (left <= 0) break;
                if (!c.NextMessage(m, static_cast<int>(left), error)) break;
                std::vector<uint8_t> flv;
                if (ToFlv(m, flv)) s->frames.push_back(std::move(flv));
            }
            if (s->frames.empty()) {
                if (c.ended)
                    return UltraNetResult::Error(UltraNetResultCode::ConnectionReset,
                                                 error.empty() ? "the server ended the stream" : error);
                return UltraNetResult::Ok();   // nothing within the wait
            }
            if (!s->headerGiven) {
                // FLV, version 1, audio and video, a nine-byte header, and
                // the size of the tag before the first: none.
                static const uint8_t header[] = {'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0};
                outFrame.assign(header, header + sizeof header);
                s->headerGiven = true;
            }
            outFrame.insert(outFrame.end(), s->frames.front().begin(), s->frames.front().end());
            s->frames.pop_front();
            return UltraNetResult::Ok();
        });
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    // The core functions this plug-in calls come through `host`
    // (UltraNetPluginHostShim); an older host cannot serve them.
    if (!UltraNetPlugin_AttachHost(host)) return;
    host->RegisterPlugin(std::make_shared<RtmpPlugin>());
}
