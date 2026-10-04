// Tests/UltraNet/test_ftp_log.cpp
// The session log of UltraNet's FTP calls (UltraNetFtp.h onLog /
// UltraNet_SetThreadFtpLog, core/UltraNet/UltraNetFtpLog.h) and what a failed
// call reports.
//
// Before the log, a listing that hung after PASV said "Timeout was reached"
// and nothing else - not which address, not which reply, not which step - and
// a refused sign-in was asked three times over (MLSD, LIST, NLST), each
// asking for the password again. Checked here two ways: the transcript on its
// own, fed recorded libcurl lines; and real calls against a scripted FTP
// server on loopback, small enough to run in CI (POSIX only - it is a socket
// loop on a thread).
#include "test_framework.h"

#include <UltraNet/UltraNetFtp.h>

#include "../../UltraCanvas/core/UltraNet/UltraNetFtpLog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// Linux and the BSDs take MSG_NOSIGNAL per send; macOS has no such flag and
// gets SO_NOSIGPIPE on the socket instead (NoSigPipe below), so a client that
// hangs up mid-reply ends a session rather than the test binary.
#if !defined(_WIN32) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

using namespace ultranet_internal::ftplog;

namespace {

struct Collected {
    std::vector<UltraNetFtpLogLine> lines;
    UltraNetFtpLogCallback Sink() {
        return [this](const UltraNetFtpLogLine& l) { lines.push_back(l); };
    }
    bool Has(UltraNetFtpLogKind kind, const std::string& text) const {
        for (const auto& l : lines)
            if (l.kind == kind && l.text == text) return true;
        return false;
    }
    bool HasStarting(UltraNetFtpLogKind kind, const std::string& prefix) const {
        for (const auto& l : lines)
            if (l.kind == kind && l.text.rfind(prefix, 0) == 0) return true;
        return false;
    }
    bool Mentions(const std::string& needle) const {
        for (const auto& l : lines)
            if (l.text.find(needle) != std::string::npos) return true;
        return false;
    }
    const UltraNetFtpLogLine* Last(UltraNetFtpLogKind kind) const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->kind == kind) return &*it;
        return nullptr;
    }
};

} // namespace

// ===== The pure helpers =====

TEST(ftp_log_redacts_the_password) {
    REQUIRE_EQ(RedactCommand("PASS hunter2"), std::string("PASS ********"));
    REQUIRE_EQ(RedactCommand("pass hunter2"), std::string("pass ********"));
    REQUIRE_EQ(RedactCommand("ACCT secret"), std::string("ACCT ********"));
    REQUIRE_EQ(RedactCommand("PASV"), std::string("PASV"));
    REQUIRE_EQ(RedactCommand("USER erika"), std::string("USER erika"));
}

TEST(ftp_log_reads_reply_codes) {
    REQUIRE_EQ(ReplyCode("227 Entering Passive Mode (1,2,3,4,5,6)"), 227);
    REQUIRE_EQ(ReplyCode("230-Welcome to the server"), 230);
    REQUIRE_EQ(ReplyCode("530"), 530);
    REQUIRE_EQ(ReplyCode(" continuation of a reply"), 0);
    REQUIRE_EQ(ReplyCode("2270 not a code"), 0);
    REQUIRE_EQ(ReplyCode("ab"), 0);
}

TEST(ftp_log_names_the_host_of_a_url) {
    REQUIRE_EQ(HostOfUrl("ftp://user:pw@files.example.org:2121/pub/"),
               std::string("files.example.org"));
    REQUIRE_EQ(HostOfUrl("sftp://[2001:db8::1]:22/home/"), std::string("2001:db8::1"));
    REQUIRE_EQ(HostOfUrl("ftp://127.0.0.1/"), std::string("127.0.0.1"));
}

TEST(ftp_log_words_a_timeout_as_inactivity) {
    REQUIRE_EQ(FailureMessage("Operation too slow. Less than 1 bytes/sec transferred the "
                              "last 30 seconds", 30, 227,
                              "227 Entering Passive Mode (1,2,3,4,5,6)"),
               std::string("Connection timed out after 30 seconds of inactivity"));
    REQUIRE_EQ(FailureMessage("server response timeout", 20, 0, ""),
               std::string("Connection timed out after 20 seconds of inactivity"));
    // Without a limit of its own the detail stays libcurl's.
    REQUIRE_EQ(FailureMessage("server response timeout", 0, 0, ""),
               std::string("server response timeout"));
}

TEST(ftp_log_quotes_the_servers_refusal) {
    REQUIRE_EQ(FailureMessage("RETR response: 550", 30, 550, "550 Permission denied"),
               std::string("RETR response: 550 - the server said \"550 Permission denied\""));
    // A success reply is not a reason, and a reply already quoted is not
    // quoted twice.
    REQUIRE_EQ(FailureMessage("Couldn't connect", 30, 220, "220 Welcome"),
               std::string("Couldn't connect"));
    REQUIRE_EQ(FailureMessage("Access denied: 530 Login incorrect", 30, 530, "530 Login incorrect"),
               std::string("Access denied: 530 Login incorrect"));
}

// ===== The transcript, fed what libcurl 8 says =====

TEST(ftp_log_transcript_reads_like_an_ftp_client) {
    Collected c;
    Transcript t(c.Sink(), /*sftp=*/false);
    t.Begin("ftp://user@ftp.example.com/pub/");
    t.Feed(Channel::Text, "  Trying 203.0.113.7:21...\n");
    t.Feed(Channel::Text, "Connected to ftp.example.com (203.0.113.7) port 21\n");
    t.Feed(Channel::Received, "220-Welcome\r\n");
    t.Feed(Channel::Received, "220 FTP ready\r\n");
    t.Feed(Channel::Sent, "USER erika\r\n");
    t.Feed(Channel::Received, "331 Password required\r\n");
    t.Feed(Channel::Sent, "PASS correct horse\r\n");
    t.Feed(Channel::Received, "230 Logged in\r\n");
    t.Feed(Channel::Sent, "PASV\r\n");
    t.Feed(Channel::Received, "227 Entering Passive Mode (203,0,113,7,246,253)\r\n");
    t.Feed(Channel::Text, "Maxdownload = -1\n");   // libcurl's bookkeeping: left out
    t.Feed(Channel::Text, "Uses proxy env variable no_proxy == 'localhost,127.0.0.1'\n");
    t.Feed(Channel::Text, "Connected 2nd connection to 203.0.113.7 port 63229\n");
    t.Feed(Channel::Sent, "MLSD\r\n");
    t.Feed(Channel::Received, "150 Here comes the listing\r\n");
    t.Feed(Channel::Received, "226 Transfer complete\r\n");

    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Resolving address of ftp.example.com"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Connecting to 203.0.113.7:21..."));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step,
                  "Connection established, waiting for welcome message..."));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Logged in"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Data connection established"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Retrieving directory listing..."));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Directory listing successful"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Command, "PASS ********"));
    REQUIRE(!c.Mentions("correct horse"));
    REQUIRE(!c.Mentions("Maxdownload"));
    REQUIRE(!c.Mentions("no_proxy"));
    // The code rides along with the reply; a continuation line has none.
    bool sawCode = false, sawContinuation = false;
    for (const auto& l : c.lines) {
        if (l.kind != UltraNetFtpLogKind::Response) continue;
        if (l.text == "227 Entering Passive Mode (203,0,113,7,246,253)")
            sawCode = l.replyCode == 227;
        if (l.text == "220-Welcome") sawContinuation = l.replyCode == 220;
    }
    REQUIRE(sawCode);
    REQUIRE(sawContinuation);
    REQUIRE_EQ(t.LastReplyCode(), 226);
    // "Logged in" is said once, after the reply that logged in.
    REQUIRE_EQ(std::count_if(c.lines.begin(), c.lines.end(), [](const UltraNetFtpLogLine& l) {
                   return l.text == "Logged in";
               }), static_cast<std::ptrdiff_t>(1));
}

TEST(ftp_log_transcript_says_when_tls_was_refused) {
    Collected c;
    Transcript t(c.Sink(), false);
    t.Begin("ftp://h/");
    t.Feed(Channel::Sent, "AUTH SSL\r\n");
    t.Feed(Channel::Received, "500 AUTH not understood\r\n");
    t.Feed(Channel::Sent, "AUTH TLS\r\n");
    t.Feed(Channel::Received, "500 AUTH not understood\r\n");
    t.Feed(Channel::Sent, "USER erika\r\n");
    REQUIRE(c.Has(UltraNetFtpLogKind::Step,
                  "Insecure server, it does not support FTP over TLS"));
}

TEST(ftp_log_transcript_follows_replies_with_nobody_listening) {
    // The failure message quotes the last reply whether or not anyone reads
    // the log.
    Transcript t(UltraNetFtpLogCallback{}, false);
    t.Begin("ftp://h/");
    t.Feed(Channel::Received, "550 Permission denied\r\n");
    REQUIRE_EQ(t.LastReplyCode(), 550);
    REQUIRE_EQ(t.LastReply(), std::string("550 Permission denied"));
}

// ===== Real calls, against a scripted server =====

#if !defined(_WIN32)
namespace {

// Just enough of an FTP server to drive libcurl through a listing: a greeting,
// sign-in, PWD, EPSV refused (so libcurl uses PASV), TYPE, and MLSD / LIST /
// NLST over a passive data connection. Each control connection is served in
// turn on one thread; libcurl opens one per call, so a listing that falls back
// shows up as a second session.
class ScriptedFtpServer {
public:
    struct Script {
        bool refuseLogin = false;
        bool refuseMlsd = false;
        std::string mlsdBody;
        std::string listBody;
        // After 150, hold the data connection open and send nothing.
        bool stallData = false;
        // Never answer the listing command at all.
        bool silentListing = false;
    };

    ~ScriptedFtpServer() { Stop(); }

    bool Start(const Script& script) {
        script_ = script;
        listenFd_ = OpenListener(&port_);
        if (listenFd_ < 0) return false;
        thread_ = std::thread([this]() { Serve(); });
        return true;
    }

    void Stop() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
        if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    }

    int Port() const { return port_; }
    std::string Url() const { return "ftp://127.0.0.1:" + std::to_string(port_) + "/"; }

    std::vector<std::string> Commands() {
        std::lock_guard<std::mutex> lk(mutex_);
        return commands_;
    }
    int Count(const std::string& verb) {
        int n = 0;
        for (const std::string& c : Commands())
            if (c.substr(0, c.find(' ')) == verb) ++n;
        return n;
    }

private:
    static int OpenListener(int* port) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
            ::listen(fd, 4) != 0) {
            ::close(fd);
            return -1;
        }
        socklen_t len = sizeof addr;
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
        *port = ntohs(addr.sin_port);
        return fd;
    }

    // Waits up to `ms` for `fd` to be readable, giving up early on Stop().
    bool WaitReadable(int fd, int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!stop_.load()) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 50) > 0) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
        }
        return false;
    }

    static void NoSigPipe(int fd) {
#ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
        (void)fd;
#endif
    }

    int AcceptOn(int fd, int ms) {
        if (!WaitReadable(fd, ms)) return -1;
        const int accepted = ::accept(fd, nullptr, nullptr);
        if (accepted >= 0) NoSigPipe(accepted);
        return accepted;
    }

    static void Send(int fd, const std::string& text) {
        const std::string line = text + "\r\n";
        (void)::send(fd, line.data(), line.size(), MSG_NOSIGNAL);
    }

    bool ReadLine(int fd, std::string& line) {
        line.clear();
        char c = 0;
        while (true) {
            if (!WaitReadable(fd, 5000)) return false;
            const ssize_t n = ::recv(fd, &c, 1, 0);
            if (n <= 0) return false;
            if (c == '\n') break;
            if (c != '\r') line.push_back(c);
        }
        return true;
    }

    // Accepts the data connection and sends `body` over it.
    void SendData(int& dataListen, int control, const std::string& body) {
        const int data = dataListen >= 0 ? AcceptOn(dataListen, 3000) : -1;
        if (data < 0) {
            Send(control, "425 Can't open data connection");
            return;
        }
        Send(control, "150 Here comes the listing");
        if (script_.stallData) {
            // Open, and silent: what a firewall that passed PASV and dropped
            // the data looks like from the client.
            WaitReadable(data, 8000);
            ::close(data);
            return;
        }
        (void)::send(data, body.data(), body.size(), MSG_NOSIGNAL);
        ::close(data);
        ::close(dataListen);
        dataListen = -1;
        Send(control, "226 Transfer complete");
    }

    void Session(int fd) {
        int dataListen = -1;
        Send(fd, "220 Scripted FTP server ready");
        std::string line;
        while (!stop_.load() && ReadLine(fd, line)) {
            {
                std::lock_guard<std::mutex> lk(mutex_);
                commands_.push_back(line);
            }
            const std::string verb = Upper(line.substr(0, line.find(' ')));
            if (verb == "USER") {
                Send(fd, "331 Password required");
            } else if (verb == "PASS") {
                Send(fd, script_.refuseLogin ? "530 Login incorrect." : "230 Logged in");
            } else if (verb == "PWD") {
                Send(fd, "257 \"/\" is the current directory");
            } else if (verb == "CWD") {
                Send(fd, "250 Directory changed");
            } else if (verb == "TYPE") {
                Send(fd, "200 Type set");
            } else if (verb == "EPSV") {
                Send(fd, "500 EPSV not understood");
            } else if (verb == "PASV") {
                int dataPort = 0;
                if (dataListen >= 0) ::close(dataListen);
                dataListen = OpenListener(&dataPort);
                Send(fd, "227 Entering Passive Mode (127,0,0,1," +
                         std::to_string(dataPort / 256) + "," +
                         std::to_string(dataPort % 256) + ")");
            } else if (verb == "MLSD" || verb == "LIST" || verb == "NLST") {
                if (script_.silentListing) {
                    WaitReadable(fd, 8000);   // say nothing until hung up on
                    break;
                }
                if (verb == "MLSD" && script_.refuseMlsd) {
                    Send(fd, "500 Unknown command");
                    continue;
                }
                SendData(dataListen, fd, verb == "MLSD" ? script_.mlsdBody : script_.listBody);
            } else if (verb == "QUIT") {
                Send(fd, "221 Goodbye");
                break;
            } else {
                Send(fd, "502 Command not implemented");
            }
        }
        if (dataListen >= 0) ::close(dataListen);
        ::close(fd);
    }

    void Serve() {
        while (!stop_.load()) {
            const int fd = AcceptOn(listenFd_, 200);
            if (fd >= 0) Session(fd);
        }
    }

    Script script_;
    int listenFd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<std::string> commands_;
};

// A short inactivity limit only where a test waits for it to run out; the
// rest get room for a slow CI runner.
UltraNetFtpOptions TestOptions(Collected& c, int inactivityMs = 5000) {
    UltraNetFtpOptions opt;
    opt.credentials.username = "tester";
    opt.credentials.password = "s3cret-pw";
    opt.connectTimeoutMs = 3000;
    opt.inactivityTimeoutMs = inactivityMs;
    opt.onLog = c.Sink();
    return opt;
}

} // namespace

TEST(ftp_log_a_refused_sign_in_is_asked_once_and_logged) {
    ScriptedFtpServer server;
    ScriptedFtpServer::Script script;
    script.refuseLogin = true;
    if (!server.Start(script)) SKIP("cannot listen on loopback");

    Collected c;
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult r = UltraNet_FtpListDirectory(server.Url(), entries, TestOptions(c));
    server.Stop();

    REQUIRE(!r.success);
    REQUIRE_EQ(r.code, UltraNetResultCode::AuthenticationFailed);
    REQUIRE(r.message.find("530") != std::string::npos);
    REQUIRE(!r.diagnostics.empty());
    REQUIRE(r.diagnostics.find("Last server reply: 530 Login incorrect.") != std::string::npos);
    // Asked once: a refused password is refused in every listing format.
    REQUIRE_EQ(server.Count("USER"), 1);
    REQUIRE_EQ(server.Count("PASS"), 1);
    // The steps, the password kept out, the reply with its code, the error
    // with libcurl's number (67: login denied).
    REQUIRE(c.Has(UltraNetFtpLogKind::Step,
                  "Connection established, waiting for welcome message..."));
    REQUIRE(c.Has(UltraNetFtpLogKind::Command, "USER tester"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Command, "PASS ********"));
    REQUIRE(!c.Mentions("s3cret-pw"));
    const UltraNetFtpLogLine* reply = c.Last(UltraNetFtpLogKind::Response);
    REQUIRE(reply != nullptr);
    REQUIRE_EQ(reply->replyCode, 530);
    const UltraNetFtpLogLine* error = c.Last(UltraNetFtpLogKind::Error);
    REQUIRE(error != nullptr);
    REQUIRE_EQ(error->transportCode, 67);
    REQUIRE_EQ(error->resultCode, UltraNetResultCode::AuthenticationFailed);
    REQUIRE_EQ(error->text, r.message);
    REQUIRE(c.lines.back().kind == UltraNetFtpLogKind::Error);
}

TEST(ftp_log_an_empty_folder_is_listed_once) {
    ScriptedFtpServer server;
    ScriptedFtpServer::Script script;   // MLSD answers with nothing at all
    if (!server.Start(script)) SKIP("cannot listen on loopback");

    Collected c;
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult r = UltraNet_FtpListDirectory(server.Url(), entries, TestOptions(c));
    server.Stop();

    REQUIRE(r.success);
    REQUIRE(entries.empty());
    // Empty is the answer, not a format to try again in.
    REQUIRE_EQ(server.Count("MLSD"), 1);
    REQUIRE_EQ(server.Count("LIST"), 0);
    REQUIRE_EQ(server.Count("NLST"), 0);
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Logged in"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Retrieving directory listing..."));
    REQUIRE(c.Has(UltraNetFtpLogKind::Step, "Directory listing successful"));
    REQUIRE(c.Last(UltraNetFtpLogKind::Error) == nullptr);
}

TEST(ftp_log_a_refused_mlsd_falls_back_to_list_and_says_so) {
    ScriptedFtpServer server;
    ScriptedFtpServer::Script script;
    script.refuseMlsd = true;
    script.listBody = "-rw-r--r-- 1 erika users 5 Jan  3  2024 notes.txt\r\n";
    if (!server.Start(script)) SKIP("cannot listen on loopback");

    Collected c;
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult r = UltraNet_FtpListDirectory(server.Url(), entries, TestOptions(c));
    server.Stop();

    REQUIRE(r.success);
    REQUIRE_EQ(entries.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(entries[0].name, std::string("notes.txt"));
    REQUIRE_EQ(server.Count("MLSD"), 1);
    REQUIRE_EQ(server.Count("LIST"), 1);
    REQUIRE(c.HasStarting(UltraNetFtpLogKind::Step, "The server refused MLSD"));
    REQUIRE(c.Last(UltraNetFtpLogKind::Error) == nullptr);
}

TEST(ftp_log_a_stalled_data_connection_times_out_as_inactivity) {
    ScriptedFtpServer server;
    ScriptedFtpServer::Script script;
    script.stallData = true;
    if (!server.Start(script)) SKIP("cannot listen on loopback");

    Collected c;
    std::vector<UltraNetFtpEntry> entries;
    const auto started = std::chrono::steady_clock::now();
    const UltraNetResult r =
            UltraNet_FtpListDirectory(server.Url(), entries, TestOptions(c, 1000));
    const auto took = std::chrono::steady_clock::now() - started;
    server.Stop();

    REQUIRE(!r.success);
    REQUIRE_EQ(r.message, std::string("Connection timed out after 1 second of inactivity"));
    // Once, not once per listing format.
    REQUIRE_EQ(server.Count("MLSD"), 1);
    REQUIRE_EQ(server.Count("LIST"), 0);
    REQUIRE(took < std::chrono::seconds(6));
    const UltraNetFtpLogLine* error = c.Last(UltraNetFtpLogKind::Error);
    REQUIRE(error != nullptr);
    REQUIRE_EQ(error->transportCode, 28);   // CURLE_OPERATION_TIMEDOUT
}

TEST(ftp_log_a_server_that_never_answers_the_listing_times_out) {
    ScriptedFtpServer server;
    ScriptedFtpServer::Script script;
    script.silentListing = true;
    if (!server.Start(script)) SKIP("cannot listen on loopback");

    Collected c;
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult r =
            UltraNet_FtpListDirectory(server.Url(), entries, TestOptions(c, 1000));
    server.Stop();

    REQUIRE(!r.success);
    REQUIRE_EQ(r.message, std::string("Connection timed out after 1 second of inactivity"));
    REQUIRE_EQ(server.Count("MLSD"), 1);
    // The log ends where the server went quiet: the passive-mode reply, the
    // listing command, then the error.
    REQUIRE(c.HasStarting(UltraNetFtpLogKind::Response, "227 Entering Passive Mode"));
    REQUIRE(c.Has(UltraNetFtpLogKind::Command, "MLSD"));
    REQUIRE(c.lines.back().kind == UltraNetFtpLogKind::Error);
}

TEST(ftp_log_the_thread_sink_hears_calls_without_an_onlog) {
    ScriptedFtpServer server;
    ScriptedFtpServer::Script script;
    script.refuseLogin = true;
    if (!server.Start(script)) SKIP("cannot listen on loopback");

    Collected c;
    UltraNetFtpLogCallback previous = UltraNet_SetThreadFtpLog(c.Sink());
    Collected other;
    UltraNetFtpOptions opt = TestOptions(other);
    opt.onLog = nullptr;
    std::vector<UltraNetFtpEntry> entries;
    (void)UltraNet_FtpListDirectory(server.Url(), entries, opt);

    // Another thread's calls are not this sink's business.
    std::thread elsewhere([&]() {
        std::vector<UltraNetFtpEntry> e;
        (void)UltraNet_FtpListDirectory(server.Url(), e, opt);
    });
    elsewhere.join();
    UltraNet_SetThreadFtpLog(std::move(previous));
    server.Stop();

    REQUIRE(c.Has(UltraNetFtpLogKind::Command, "USER tester"));
    REQUIRE_EQ(std::count_if(c.lines.begin(), c.lines.end(), [](const UltraNetFtpLogLine& l) {
                   return l.kind == UltraNetFtpLogKind::Error;
               }), static_cast<std::ptrdiff_t>(1));
    REQUIRE(other.lines.empty());
}
#endif // !_WIN32
