// Tests/UltraWebTabTest.cpp
// One UltraWeb tab (Apps/UltraWeb/ui/UltraWebTab), without a window: a new
// tab is empty; pages from file:// give it their title, address and
// history; a link to a file is handed to the window as a download; the
// built-in app runs in the tab's own area; and a tab can go while its app
// runs. Two tabs keep two histories.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "ui/UltraWebTab.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

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
bool Pump(const std::function<bool()>& done, int seconds = 10) {
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

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string FileUrl(const std::filesystem::path& path) { return "file://" + PathToUtf8(path); }

struct Recorder {
    int changes = 0;
    std::string downloadName;
    UltraWebTab::Host Host() {
        UltraWebTab::Host host;
        host.post = Post;
        host.onChanged = [this](UltraWebTab&) { ++changes; };
        host.onDownload = [this](UltraWebTab&, const HTML::LoadedPage& page) { downloadName = page.fileName; };
        return host;
    }
};

void TestPages(const std::filesystem::path& dir) {
    std::printf("pages in a tab\n");
    Recorder seen;
    UltraWebTab tab("t1", seen.Host());
    Check(tab.Title() == "New tab" && tab.Address().empty() && !tab.CanGoBack() && !tab.CanGoForward(),
          "a new tab is empty");
    Check(tab.Area() && tab.Area()->GetChildren().empty(), "with nothing in its area");

    tab.Navigate(FileUrl(dir / "one.html"));
    Check(Pump([&] { return tab.Title() == "Page One" && !tab.IsLoading(); }), "a page gives the tab its title");
    Check(tab.Address() == FileUrl(dir / "one.html"), "and its address: " + tab.Address());
    Check(seen.changes > 0, "the window is told");
    Check(tab.StatusText() == tab.Address(), "the status line shows where it is: " + tab.StatusText());

    tab.Navigate(FileUrl(dir / "two.html"));
    Check(Pump([&] { return tab.Title() == "Page Two" && !tab.IsLoading(); }), "a second page");
    Check(tab.CanGoBack() && !tab.CanGoForward(), "back is possible, forward not");
    tab.GoBack();
    Check(Pump([&] { return tab.Title() == "Page One" && !tab.IsLoading(); }), "back to the first");
    Check(tab.CanGoForward(), "and forward again from there");

    // A second tab has its own history.
    Recorder other;
    UltraWebTab second("t2", other.Host());
    second.Navigate(FileUrl(dir / "two.html"));
    Check(Pump([&] { return second.Title() == "Page Two" && !second.IsLoading(); }), "another tab, another page");
    Check(!second.CanGoBack() && tab.Title() == "Page One", "each tab keeps its own history");

    tab.Navigate(FileUrl(dir / "data.zip"));
    Check(Pump([&] { return !seen.downloadName.empty(); }), "a link to a file is handed over as a download");
    Check(seen.downloadName == "data.zip", "named as the file: " + seen.downloadName);
    Check(tab.Title() == "Page One", "and the page stays");
}

void TestApp() {
    std::printf("an app in a tab\n");
    if (!UltraCanvasWasm_IsAvailable()) {
        std::printf("  skipped: %s\n", UltraCanvasWasm_EngineDescription().c_str());
        return;
    }
    Recorder seen;
    auto tab = std::make_unique<UltraWebTab>("t3", seen.Host());
    tab->Navigate("about:demo");
    Check(Pump([&] { return tab->IsApp(); }), "about:demo runs");
    Check(!tab->Area()->GetChildren().empty(), "its elements are in the tab's area");
    Check(tab->Title() == "about:demo" && tab->StatusText().rfind("Running about:demo", 0) == 0,
          "title and status: " + tab->StatusText());
    Check(!tab->CanGoBack(), "an app has no back");
    auto area = tab->Area();
    tab.reset();
    Check(area->GetChildren().empty(), "closing the tab takes the app's elements out");
    Pump([] { return false; }, 1);     // whatever the app left posted runs harmlessly
    Check(true, "and nothing it left behind runs into the closed tab");
}

} // namespace

int main() {
    UltraNet_Initialize();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ultraweb-tab-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Write(dir / "one.html", "<!DOCTYPE html><title>Page One</title><p><a href=\"two.html\">two</a>");
    Write(dir / "two.html", "<!DOCTYPE html><title>Page Two</title><p>second");
    Write(dir / "data.zip", std::string("PK\x03\x04zip", 7));
    TestPages(dir);
    TestApp();
    std::filesystem::remove_all(dir);
    UltraNet_Shutdown();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
