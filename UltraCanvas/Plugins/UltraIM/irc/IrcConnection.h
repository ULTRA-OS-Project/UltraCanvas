// Plugins/UltraIM/irc/IrcConnection.h
// One connection to an IRC server through UltraNet: TCP, TLS on top when
// asked (UltraNet_TlsWrap / UltraNet_TlsHandshake, the certificate and the
// host name always verified), and the stream cut into lines.
//
// Used from one thread only (the provider's): TLS cannot read and write
// from two threads at once, so the provider reads with a short timeout and
// writes between reads. UltraNet's reads answer a timeout and a closed or
// broken connection alike (an error, or no bytes - by backend), so a read
// that comes back empty at once, several times in a row, is taken as the
// connection gone; one that waited the whole timeout as quiet.
// On POSIX, SIGPIPE is held back on the thread that uses the connection: a
// write to a connection the server closed fails instead of ending the program.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetTls.h"

#include <string>
#include <vector>

namespace UltraIMIrc {

class IrcConnection {
public:
    enum class ReadStatus {
        Lines,      // one or more lines (or a part of one) came
        Idle,       // nothing within the timeout
        Closed      // the connection is gone
    };

    IrcConnection() = default;
    ~IrcConnection() { Close(); }
    IrcConnection(const IrcConnection&) = delete;
    IrcConnection& operator=(const IrcConnection&) = delete;

    // Connects (and, with tls, shakes hands, verifying the server's
    // certificate for host). False with the reason in error;
    // certificateRefused when the certificate did not verify.
    bool Open(const std::string& host, int port, bool tls, const UltraNetTlsOptions& tlsOptions, int timeoutMs,
              std::string& error, bool& certificateRefused);
    // Waits up to timeoutMs for bytes; the complete lines that came are
    // appended to lines (without CR LF).
    ReadStatus Read(int timeoutMs, std::vector<std::string>& lines);
    // One line (CR LF added). False when the connection is gone.
    bool Send(const std::string& line);
    void Close();
    bool IsOpen() const { return handle_ != UltraNetInvalidHandle; }
    bool IsTls() const { return tls_; }

    // Holds SIGPIPE back on the calling thread (POSIX; nothing elsewhere).
    static void BlockSigPipeOnThisThread();

private:
    UltraNetHandle handle_ = UltraNetInvalidHandle;
    bool tls_ = false;
    std::string buffer_;
    int quickFailures_ = 0;
};

} // namespace UltraIMIrc
