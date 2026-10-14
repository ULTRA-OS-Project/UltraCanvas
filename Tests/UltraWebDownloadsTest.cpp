// Tests/UltraWebDownloadsTest.cpp
// UltraWeb's downloads (Apps/UltraWeb/host/UltraWebDownloads): bytes saved
// through a part file, a local file copied, sizes shown dot-decimal, and an
// http download streamed to disk from a loopback server - whole, a 404 that
// leaves no file, a cancelled one that leaves no part file either, and one
// whose server stops sending, which ends as a stall rather than hanging.
// Then the downloads list (host/UltraWebDownloadList) and its panel
// (ui/UltraWebDownloadsPanel): states, sizes, the buttons each row offers
// and what they do.
// The http part needs POSIX sockets and is skipped on Windows.
// Version: 1.2.0 - a stalled download ends
// Version: 1.2.0 - speed, time left, long names
// Version: 1.1.0 - the downloads list and panel
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "host/UltraWebDownloadList.h"
#include "host/UltraWebDownloads.h"
#include "ui/UltraWebDownloadsPanel.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0      // macOS: SIGPIPE is ignored in main instead
#endif
#endif

using namespace UltraWeb;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

// The "UI thread": tasks posted from anywhere, run by Pump on this thread.
std::mutex g_lock;
std::condition_variable g_wake;
std::deque<std::function<void()>> g_tasks;

void Post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_tasks.push_back(std::move(task));
    }
    g_wake.notify_all();
}

// Runs posted tasks until `done` holds or `seconds` pass.
bool Pump(const std::function<bool()>& done, int seconds = 20) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> guard(g_lock);
            if (!g_wake.wait_until(guard, until, [] { return !g_tasks.empty(); })) return done();
            task = std::move(g_tasks.front());
            g_tasks.pop_front();
        }
        task();
    }
    return true;
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestSaveBytesAndCopy(const std::filesystem::path& dir) {
    std::printf("saving bytes, copying a local file\n");
    const std::string path = PathToUtf8(dir / "saved \xC3\xA9.bin");
    std::string error;
    const std::vector<uint8_t> bytes = { 'a', 0, 'b', 0xFF };
    Check(SaveBytes(path, bytes, error), "SaveBytes");
    Check(ReadAll(PathFromUtf8(path)) == std::string("a\0b\xFF", 4), "the bytes, NULs and all, under a UTF-8 name");
    Check(!std::filesystem::exists(PathFromUtf8(path + ".part")), "no part file left");
    Check(SaveBytes(path, { 'z' }, error) && ReadAll(PathFromUtf8(path)) == "z", "a second save replaces the file");
    Check(!SaveBytes(PathToUtf8(dir / "no-such-dir" / "x"), bytes, error) && !error.empty(), "a bad folder fails, saying why");

    const std::string source = PathToUtf8(dir / "source.txt");
    { std::ofstream(PathFromUtf8(source), std::ios::binary) << "local file"; }
    const std::string copy = PathToUtf8(dir / "copy.txt");
    bool finished = false, ok = false;
    StartDownload("file://" + source, copy, Post, nullptr, [&](bool good, const std::string&) { finished = true; ok = good; });
    Pump([&] { return finished; });
    Check(ok && ReadAll(PathFromUtf8(copy)) == "local file", "a file:// download copies the file");

    Check(ByteSize(12) == "12 bytes" && ByteSize(1) == "1 byte", "bytes");
    Check(ByteSize(2048) == "2 KB", "kilobytes");
    Check(ByteSize(int64_t(1536) * 1024) == "1.5 MB", "megabytes, dot-decimal");
    Check(ByteSize(int64_t(3) * 1024 * 1024 * 1024) == "3.0 GB", "gigabytes");
}

void TestList() {
    std::printf("the downloads list\n");
    DownloadList list;
    std::vector<std::pair<int, bool>> changes;
    list.onChanged = [&](int id, bool progressOnly) { changes.emplace_back(id, progressOnly); };

    const int saved = list.AddSaved("http://x/a.zip", "a.zip", "/d/a.zip", 2048);
    const int big = list.Add("http://x/big.iso", "big.iso", "/d/big.iso");
    Check(list.Entries().size() == 2 && list.Entries()[0].id == big, "newest first");
    Check(list.RunningCount() == 1, "one running");
    Check(DownloadList::Describe(*list.Find(saved)) == "Saved - 2 KB", "a saved one: " + DownloadList::Describe(*list.Find(saved)));
    Check(DownloadList::Describe(*list.Find(big)) == "Starting ...", "nothing yet");

    list.Progress(big, int64_t(5) * 1024 * 1024, int64_t(20) * 1024 * 1024);
    Check(DownloadList::Describe(*list.Find(big)) == "5.0 MB of 20.0 MB (25%)", "progress: " + DownloadList::Describe(*list.Find(big)));
    Check(!changes.empty() && changes.back() == std::make_pair(big, true), "progress is reported as progress only");
    list.Progress(big, 3 * 1024, -1);
    Check(DownloadList::Describe(*list.Find(big)) == "3 KB so far", "no total: " + DownloadList::Describe(*list.Find(big)));

    Check(!list.Remove(big), "a running download cannot be removed");
    list.Finish(big, true, "");
    Check(list.Find(big)->state == DownloadEntry::State::Saved && list.RunningCount() == 0, "finished: saved");
    Check(DownloadList::Describe(*list.Find(big)) == "Saved - 3 KB", "its size is what came: " + DownloadList::Describe(*list.Find(big)));
    list.Progress(big, 1, 1);
    Check(list.Find(big)->received == 3 * 1024, "late progress is ignored");

    const int failed = list.Add("http://x/gone", "gone", "/d/gone");
    list.Finish(failed, false, "the server answered 404");
    Check(DownloadList::Describe(*list.Find(failed)) == "Failed: the server answered 404", "failed, saying why");
    const int stopped = list.Add("http://x/s", "s", "/d/s");
    list.Finish(stopped, false, "cancelled");
    Check(list.Find(stopped)->state == DownloadEntry::State::Cancelled && DownloadList::Describe(*list.Find(stopped)) == "Cancelled",
          "cancelled is not failed");

    const int running = list.Add("http://x/r", "r", "/d/r");
    Check(list.Remove(saved) && !list.Find(saved), "a finished one is removed");
    Check(!list.Remove(12345), "an unknown id is not");
    changes.clear();
    list.ClearFinished();
    Check(list.Entries().size() == 1 && list.Entries()[0].id == running, "clear finished keeps the running one");
    Check(changes.size() == 1 && changes[0].first == -1, "and says the list changed once");
    changes.clear();
    list.ClearFinished();
    Check(changes.empty(), "clearing nothing says nothing");
}

void TestSpeedAndNames() {
    std::printf("speed, time left, long names\n");
    DownloadList list;
    int64_t now = 1000;
    list.clock = [&] { return now; };
    const int id = list.Add("http://x/big.iso", "big.iso", "/d/big.iso");
    const int64_t mb = 1024 * 1024;
    now += 500;
    list.Progress(id, mb / 2, 20 * mb);
    Check(list.Find(id)->bytesPerSecond == 0 && DownloadList::Describe(*list.Find(id)) == "512 KB of 20.0 MB (2%)",
          "no speed before a second has passed: " + DownloadList::Describe(*list.Find(id)));
    now += 500;
    list.Progress(id, 2 * mb, 20 * mb);
    Check(list.Find(id)->bytesPerSecond == 2 * mb, "2 MB in the first second: 2 MB/s");
    Check(DownloadList::Describe(*list.Find(id)) == "2.0 MB of 20.0 MB (10%) - 2.0 MB/s, 9 s left",
          "speed and time left: " + DownloadList::Describe(*list.Find(id)));
    now += 1000;
    list.Progress(id, 3 * mb, 20 * mb);
    Check(list.Find(id)->bytesPerSecond == (2 * 2 * mb + mb) / 3, "a slow second moves the speed a third of the way");

    Check(DownloadList::TimeLeft(10, 0).empty(), "no speed, no time");
    Check(DownloadList::TimeLeft(1, 1000) == "1 s left", "under a second shows 1 s");
    Check(DownloadList::TimeLeft(59 * 1000, 1000) == "59 s left", "seconds");
    Check(DownloadList::TimeLeft(61 * 1000, 1000) == "2 min left", "minutes, rounded up");
    Check(DownloadList::TimeLeft(3600 * 1000, 1000) == "1 h left", "an hour");
    Check(DownloadList::TimeLeft(3900 * 1000, 1000) == "1 h 5 min left", "hours and minutes");

    const int unsized = list.Add("http://x/s", "s", "/d/s");
    now += 1000;
    list.Progress(unsized, mb, -1);
    Check(DownloadList::Describe(*list.Find(unsized)) == "1.0 MB so far - 1.0 MB/s", "no size: speed, no time left");

    Check(DownloadList::Ellipsize("short.zip", 36) == "short.zip", "a short name stays");
    const std::string longName = "ultracanvas-nightly-build-linux-x86_64-2026-10-14-snapshot.tar.gz";
    const std::string cut = DownloadList::Ellipsize(longName, 36);
    Check(cut.find("\xE2\x80\xA6") != std::string::npos, "a long name gets an ellipsis: " + cut);
    Check(cut.rfind("ultracanvas-nightly", 0) == 0 && cut.size() >= 7 && cut.compare(cut.size() - 7, 7, ".tar.gz") == 0,
          "it keeps the start and the extension");
    size_t chars = 0;
    for (unsigned char c : cut) if ((c & 0xC0) != 0x80) ++chars;
    Check(chars == 36, "36 characters, the ellipsis one of them (" + std::to_string(chars) + ")");
    const std::string utf8 = std::string("\xC3\xA9t\xC3\xA9-") + std::string(60, 'x') + ".pdf";
    const std::string cutUtf8 = DownloadList::Ellipsize(utf8, 20);
    Check(cutUtf8.rfind("\xC3\xA9t\xC3\xA9", 0) == 0 && cutUtf8.compare(cutUtf8.size() - 4, 4, ".pdf") == 0,
          "never splits a character: " + cutUtf8);
}

void TestPanel() {
    std::printf("the downloads panel\n");
    DownloadList list;
    UltraWebDownloadsPanel panel(list, Post);
    list.onChanged = [&](int id, bool progressOnly) { panel.Update(id, progressOnly); };
    auto drain = [] { Pump([] { std::lock_guard<std::mutex> guard(g_lock); return g_tasks.empty(); }, 2); };

    Check(panel.ClearButton()->IsDisabled(), "nothing to clear at first");
    const int saved = list.AddSaved("http://x/a.zip", "a.zip", "/d/a.zip", 10);
    const int running = list.Add("http://x/b.iso", "b.iso", "/d/b.iso");
    Check(!panel.RowFor(running), "rows are rebuilt on a later turn");
    drain();
    const auto* runRow = panel.RowFor(running);
    const auto* savedRow = panel.RowFor(saved);
    Check(runRow && savedRow, "a row for each");
    if (!runRow || !savedRow) return;
    Check(runRow->name->GetText() == "b.iso" && runRow->state->GetText() == "Starting ...", "name and state");
    Check(runRow->cancel && !runRow->open && !runRow->remove, "a running one can be cancelled, nothing else");
    Check(savedRow->open && savedRow->showFolder && savedRow->remove && !savedRow->cancel, "a saved one: open, show folder, remove");
    Check(!panel.ClearButton()->IsDisabled(), "something to clear");

    list.Progress(running, 512 * 1024, 1024 * 1024);
    Check(panel.RowFor(running) == runRow && runRow->state->GetText() == "512 KB of 1.0 MB (50%)",
          "progress rewrites the state in place: " + runRow->state->GetText());

    int cancelled = 0;
    panel.onCancel = [&](int id) { cancelled = id; };
    runRow->cancel->onClick();
    Check(cancelled == running, "Cancel asks the window to cancel that download");
    std::string opened, folder;
    panel.onOpen = [&](const DownloadEntry& e) { opened = e.path; };
    panel.onShowFolder = [&](const DownloadEntry& e) { folder = e.path; };
    savedRow->open->onClick();
    savedRow->showFolder->onClick();
    Check(opened == "/d/a.zip" && folder == "/d/a.zip", "Open and Show folder name the file");

    list.Finish(running, false, "cancelled");
    drain();
    runRow = panel.RowFor(running);
    Check(runRow && runRow->state->GetText() == "Cancelled" && !runRow->cancel && runRow->remove, "cancelled: only remove");

    auto removeButton = panel.RowFor(saved)->remove;
    removeButton->onClick();
    Check(!list.Find(saved) && panel.RowFor(saved), "Remove drops it from the list; the row goes on a later turn");
    drain();
    Check(!panel.RowFor(saved), "and then the row");
    panel.ClearButton()->onClick();
    drain();
    Check(list.Entries().empty() && !panel.RowFor(running), "Clear finished empties it");
    Check(panel.ClearButton()->IsDisabled(), "and nothing is left to clear");
    bool closed = false;
    panel.onClose = [&] { closed = true; };
    panel.CloseButton()->onClick();
    Check(closed, "Close");

    const std::string longName = "a-really-long-file-name-for-a-download-2026-10-14-final.iso";
    const int named = list.AddSaved("http://x/l", longName, "/d/l", 1);
    drain();
    const auto* namedRow = panel.RowFor(named);
    Check(namedRow && namedRow->name->GetText() != longName && namedRow->name->GetTooltip() == longName,
          "a long name is cut in the row, whole in its tooltip");
}

#ifndef _WIN32
// A loopback HTTP/1.0 server: /file answers `body` (slowly when asked, or
// only its first piece and then nothing when `stall` is set), anything
// else 404.
struct Server {
    int listener = -1;
    int port = 0;
    std::string body;
    std::atomic<bool> slow{ false };
    std::atomic<bool> stall{ false };
    std::atomic<bool> stop{ false };
    std::thread thread;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        int one = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        socklen_t len = sizeof(addr);
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        ::listen(listener, 8);
        thread = std::thread([this] { Run(); });
        return true;
    }
    void Run() {
        while (!stop) {
            const int client = ::accept(listener, nullptr, nullptr);
            if (client < 0) break;
            char request[2048];
            const ssize_t got = ::recv(client, request, sizeof(request) - 1, 0);
            const std::string line = got > 0 ? std::string(request, static_cast<size_t>(got)) : std::string();
            if (line.rfind("GET /file ", 0) == 0) {
                const std::string head = "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                                         std::to_string(body.size()) + "\r\n\r\n";
                ::send(client, head.data(), head.size(), MSG_NOSIGNAL);
                for (size_t at = 0; at < body.size() && !stop;) {
                    const size_t n = std::min<size_t>(64 * 1024, body.size() - at);
                    if (::send(client, body.data() + at, n, MSG_NOSIGNAL) <= 0) break;
                    at += n;
                    if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (stall) {
                        // The connection stays open and silent until the test ends.
                        while (!stop) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        break;
                    }
                }
            } else {
                const std::string head = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                ::send(client, head.data(), head.size(), MSG_NOSIGNAL);
            }
            ::close(client);
        }
    }
    void Stop() {
        stop = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        if (thread.joinable()) thread.join();
    }
};

void TestHttp(const std::filesystem::path& dir) {
    std::printf("http downloads streamed to disk\n");
    Server server;
    server.body.resize(3 * 1024 * 1024 + 17);
    for (size_t i = 0; i < server.body.size(); ++i) server.body[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    if (!server.Start()) { Check(false, "a loopback server"); return; }
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port);

    const std::string path = PathToUtf8(dir / "big.bin");
    bool finished = false, ok = false;
    int progress = 0;
    std::string error;
    auto download = StartDownload(base + "/file", path, Post,
                                  [&](int64_t, int64_t) { ++progress; },
                                  [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    Check(Pump([&] { return finished; }), "it finishes");
    Check(ok, "whole (" + error + ")");
    Check(ReadAll(PathFromUtf8(path)) == server.body, "every byte, in order (3 MB)");
    Check(!std::filesystem::exists(PathFromUtf8(path + ".part")), "the part file became the file");

    finished = false;
    const std::string missing = PathToUtf8(dir / "missing.bin");
    StartDownload(base + "/nothing", missing, Post, nullptr,
                  [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    Pump([&] { return finished; });
    Check(!ok && error.find("404") != std::string::npos, "a 404 fails, saying so");
    Check(!std::filesystem::exists(PathFromUtf8(missing)) && !std::filesystem::exists(PathFromUtf8(missing + ".part")),
          "and leaves no file");

    server.slow = true;
    finished = false;
    const std::string cancelled = PathToUtf8(dir / "cancelled.bin");
    auto slow = StartDownload(base + "/file", cancelled, Post, nullptr,
                              [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    slow->Cancel();
    Pump([&] { return finished; });
    Check(finished && !ok, "a cancelled download ends, not saved");
    Check(!std::filesystem::exists(PathFromUtf8(cancelled)) && !std::filesystem::exists(PathFromUtf8(cancelled + ".part")),
          "and leaves no part file");

    // A server that goes quiet: the download has no time limit of its own
    // (a 30 s one used to end every large file), so it is the stall limit
    // that ends it - shortened here so the test does not wait 30 s.
    server.slow = false;
    server.stall = true;
    SetDownloadStallTimeout(1000);
    finished = false;
    const std::string stalled = PathToUtf8(dir / "stalled.bin");
    const auto began = std::chrono::steady_clock::now();
    StartDownload(base + "/file", stalled, Post, nullptr,
                  [&](bool good, const std::string& why) { finished = true; ok = good; error = why; });
    Pump([&] { return finished; }, 15);
    const auto took = std::chrono::steady_clock::now() - began;
    Check(finished && !ok, "a download whose server stops sending ends (" + error + ")");
    Check(took < std::chrono::seconds(10), "within the stall limit, not a whole-transfer one");
    Check(!std::filesystem::exists(PathFromUtf8(stalled)) && !std::filesystem::exists(PathFromUtf8(stalled + ".part")),
          "and leaves no part file");
    SetDownloadStallTimeout(0);
    server.Stop();
}
#endif

} // namespace

int main() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);   // a client that hangs up must not end the test
#endif
    UltraNet_Initialize();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ultraweb-downloads-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    TestSaveBytesAndCopy(dir);
    TestList();
    TestSpeedAndNames();
    TestPanel();
#ifndef _WIN32
    TestHttp(dir);
#else
    std::printf("http downloads: skipped (POSIX sockets)\n");
#endif
    std::filesystem::remove_all(dir);
    UltraNet_Shutdown();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
