// UltraCanvas/core/UltraMessage/UltraMessageTransport.h
// The local stream transport beneath the broker and the endpoints (§7.1): a
// Unix domain socket on Linux / macOS / BSD, a named pipe on Windows, the same
// framed JSON on both. Blocking calls that a Close() from another thread
// unblocks; one Connection may be written from several threads and read from
// one.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessageInternal.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraMessage {
namespace Internal {

class Connection {
public:
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Serialises and writes one frame; serialised across threads.
    bool SendFrame(const JSONValue& frame, std::string& error);
    // Writes bytes EncodeFrame produced (a frame encoded once, sent to many).
    bool SendEncoded(const std::string& bytes, std::string& error);
    // Blocks for the next frame. Returns false once the peer closed, on a
    // protocol error, or after Close().
    bool ReceiveFrame(JSONValue& out, std::string& error);
    // Unblocks a ReceiveFrame in progress and releases the stream.
    void Close();
    bool IsOpen() const { return !closed_.load(); }

    // The peer's process id as the operating system reports it, 0 when the
    // platform cannot say.
    int PeerProcessId() const { return peerPid_; }

private:
    friend class Listener;
    friend std::shared_ptr<Connection> ConnectToBus(const std::string&, int, std::string&);
    Connection() = default;
    bool ReadSome(uint8_t* buffer, size_t capacity, size_t& received, std::string& error);
    bool WriteAll(const char* data, size_t length, std::string& error);
    void QueryPeer();

    std::mutex sendMutex_;
    std::atomic<bool> closed_{false};
    FrameDecoder decoder_;
    std::vector<JSONValue> pendingFrames_;   // decoded ahead of the reader's next call
    int peerPid_ = 0;
#ifdef _WIN32
    void* pipe_ = nullptr;        // HANDLE
    void* stopEvent_ = nullptr;   // HANDLE, signalled by Close()
#else
    int fd_ = -1;
    int wakeRead_ = -1;           // self-pipe: Close() writes, ReceiveFrame polls
    int wakeWrite_ = -1;
#endif
};

using ConnectionPtr = std::shared_ptr<Connection>;

class Listener {
public:
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // Takes the bus. `alreadyInUse` is set when another broker holds it (the
    // caller should connect instead); other failures set `error`.
    static std::unique_ptr<Listener> Create(const std::string& busPath, std::string& error,
                                            bool& alreadyInUse);
    // Blocks for the next connection; nullptr after Close().
    ConnectionPtr Accept();
    void Close();
    const std::string& Path() const { return path_; }

private:
    Listener() = default;
    std::string path_;
    std::atomic<bool> closed_{false};
#ifdef _WIN32
    void* pendingPipe_ = nullptr; // HANDLE: the instance waiting for a client
    void* stopEvent_ = nullptr;
    bool CreateInstance(bool first, std::string& error, bool& alreadyInUse);
#else
    int fd_ = -1;
    int lockFd_ = -1;
    int wakeRead_ = -1;
    int wakeWrite_ = -1;
#endif
};

// Connects to the broker on `busPath`. nullptr with `error` set when nothing
// listens there (the caller may then host a broker itself).
ConnectionPtr ConnectToBus(const std::string& busPath, int timeoutMs, std::string& error);

} // namespace Internal
} // namespace UltraMessage
