// Tests/IODeviceScannerESCLLiveTest.cpp
// The eSCL scanner backend against a scanner that answers only over HTTPS,
// with a certificate it signed itself - as real scanners on `_uscans._tcp`
// do - scanned from through nothing but the public IODeviceManager API.
//
// IODeviceScannerESCLTest covers what can be decided without a network. This
// covers what cannot: reaching the scanner at all, which ordinary certificate
// verification refuses, and so the device trust (UltraCanvasIODeviceTlsTrust.h)
// end to end. There is no reference eSCL scanner to run the way ippeveprinter
// is run for IODevicePrinterIPPLiveTest, so IODeviceScannerESCLLiveScanner.py
// is one: the four eSCL calls, over TLS, logging every request it receives.
// Its certificates are made here with the openssl command, two of them, so a
// key that really changes - the scanner reset or replaced - is tried as well
// as one trusted on first use:
//
//   - first contact learns the scanner's key, and the only thing sent before
//     it is known is a bare HEAD / - nothing of the user's;
//   - the key kept is the one in the scanner's certificate, kept under the
//     name it gives itself (its make and model, as it is listed only by URL);
//   - a feeder run, a flatbed page and a grey page come over the pinned
//     connection;
//   - the scanner restarted with another key is refused before any request
//     reaches it, and the key kept is not replaced;
//   - forgetting the key lets the next connection learn the new one;
//   - with learning switched off, a scanner whose key is not kept is refused.
//
// Skipped - exit code 77 - without Python 3 or the openssl command, which
// ctest passes as arguments with the scanner script (CMake finds them), or
// when the scanner will not start. CI sets ULTRACANVAS_TEST_ESCL_REQUIRED,
// which turns a skip into a failure, on every row that runs it: Linux, where
// libcurl does TLS with OpenSSL; macOS, with Apple's system libcurl and its
// TLS; Windows, with libcurl on Schannel - three implementations of the
// pinning and the certificate capture the trust stands on. The keys go to a
// file of the test's own (ULTRACANVAS_DEVICE_CERTIFICATES), never the user's,
// and only the eSCL backend is run (ULTRACANVAS_DEVICE_BACKENDS): SANE, or
// whatever else searches for scanners here, takes seconds and finds nothing
// this test is about.
// Version: 1.1.1
// Author: UltraCanvas Framework

#include <cstdlib>
#include <iostream>

#if defined(ULTRACANVAS_HAS_NET)

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "IODeviceManager/UltraCanvasIODeviceScanner.h"
#include "IODeviceManager/UltraCanvasIODeviceTlsTrust.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetTls.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

constexpr int kSkipped = 77;

// What a skip returns. CI sets ULTRACANVAS_TEST_ESCL_REQUIRED on the rows
// that have Python and openssl, because there a skip is indistinguishable
// from a pass and would let the test quietly stop running.
int SkipOrFail() {
    const std::string required = GetEnvUtf8("ULTRACANVAS_TEST_ESCL_REQUIRED");
    if (!required.empty() && required != "0") {
        std::cout << "FAILED: ULTRACANVAS_TEST_ESCL_REQUIRED is set, so a skip is a failure\n";
        return EXIT_FAILURE;
    }
    return kSkipped;
}

int g_passed = 0;
int g_failed = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (condition) ++g_passed; else ++g_failed;
}

void SetEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void UnsetEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// ----------------------------------------------------------------------------
// Child processes: the scanner, and openssl. Their output goes to `log`.
// ----------------------------------------------------------------------------

#if defined(_WIN32)

// One argument as CommandLineToArgvW reads it back.
std::wstring QuoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        quoted.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        quoted += c;
    }
    quoted.append(backslashes * 2, L'\\');
    return quoted + L"\"";
}

class ChildProcess {
public:
    ~ChildProcess() { Stop(); }

    bool Start(const std::vector<std::string>& args, const std::string& log) {
        // PathFromUtf8 is the UTF-8 to UTF-16 conversion here, for the
        // arguments that are not paths as for those that are.
        std::wstring commandLine;
        for (const std::string& arg : args) {
            if (!commandLine.empty()) commandLine += L' ';
            commandLine += QuoteArgument(PathFromUtf8(arg).wstring());
        }
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE output = CreateFileW(PathFromUtf8(log).c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) return false;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = output;
        startup.hStdError = output;
        PROCESS_INFORMATION started{};
        const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started);
        CloseHandle(output);
        if (!ok) return false;
        CloseHandle(started.hThread);
        process = started.hProcess;
        return true;
    }

    bool Running() const {
        return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }

    // Waits for it to end; true when it exited with 0.
    bool Wait() {
        if (!process) return false;
        WaitForSingleObject(process, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        process = nullptr;
        return code == 0;
    }

    void Stop() {
        if (!process) return;
        TerminateProcess(process, 1);
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
        process = nullptr;
    }

private:
    HANDLE process = nullptr;
};

#else

class ChildProcess {
public:
    ~ChildProcess() { Stop(); }

    bool Start(std::vector<std::string> args, const std::string& log) {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(),
                                         O_WRONLY | O_CREAT | O_APPEND, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        std::vector<char*> argv;
        for (std::string& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        const int spawned =
            posix_spawn(&pid, args[0].c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawned != 0) pid = -1;
        return pid > 0;
    }

    bool Running() {
        if (pid <= 0) return false;
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            pid = -1;
            return false;
        }
        return true;
    }

    bool Wait() {
        if (pid <= 0) return false;
        int status = 0;
        waitpid(pid, &status, 0);
        pid = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    void Stop() {
        if (pid <= 0) return;
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
        pid = -1;
    }

private:
    pid_t pid = -1;
};

#endif

// Runs `args` to the end; true when it exits with 0.
bool Run(const std::vector<std::string>& args, const std::string& log) {
    ChildProcess child;
    return child.Start(args, log) && child.Wait();
}

std::string ReadText(const std::string& path) {
    std::ifstream file(PathFromUtf8(path), std::ios::binary);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::stringstream text(ReadText(path));
    std::string line;
    while (std::getline(text, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string Tail(const std::string& path) {
    const std::string text = ReadText(path);
    return text.size() > 800 ? text.substr(text.size() - 800) : text;
}

// A directory of the test's own under the system's temporary one.
std::string MakeTemporaryDirectory() {
    std::error_code error;
    const fs::path base = fs::temp_directory_path(error);
    if (error) return std::string();
    std::random_device random;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const fs::path candidate = base / ("uc-escl-live-" + std::to_string(random()));
        if (fs::create_directory(candidate, error) && !error) return PathToUtf8(candidate);
    }
    return std::string();
}

// A self-signed certificate and its key, as a scanner makes for itself -
// named for its model, not the address it is reached at, as theirs often
// are; false when openssl could not make one. The request's settings come
// from a file of the test's own, so the openssl.cnf a platform ships - or,
// for some Windows builds of openssl, does not - is not relied on.
bool MakeCertificate(const std::string& openssl, const std::string& directory,
                     const std::string& certificate, const std::string& key,
                     const std::string& log) {
    const std::string config = directory + "/certificate.cnf";
    {
        std::ofstream out(PathFromUtf8(config), std::ios::binary | std::ios::trunc);
        out << "[req]\ndistinguished_name = name\nprompt = no\n"
               "[name]\nCN = Acme MegaScan 42\n";
    }
    return Run({openssl, "req", "-x509", "-config", config, "-newkey", "rsa:2048", "-nodes",
                "-days", "2", "-keyout", key, "-out", certificate},
               log);
}

std::string PinOfCertificateFile(const std::string& path) {
    const std::string pem = ReadText(path);
    return UltraNet_PublicKeyPinOf(std::vector<uint8_t>(pem.begin(), pem.end()));
}

// The scanner process, stopped however the test ends.
class TlsScanner {
public:
    // Starts it with `certificate` on `port` (0: any free port, which
    // Port() then gives) and waits until it is listening.
    bool Start(const std::string& python, const std::string& script, const std::string& certificate,
               const std::string& key, const std::string& requests, int listenOn,
               const std::string& log) {
        const std::string ready = requests + ".port";
        std::error_code ignored;
        fs::remove(PathFromUtf8(ready), ignored);
        // -u: unbuffered, so whatever it says before failing reaches the log.
        if (!process.Start({python, "-u", script, "--cert", certificate, "--key", key, "--ready",
                            ready, "--log", requests, "--port", std::to_string(listenOn),
                            "--pages", "3"},
                           log)) {
            failure = "it could not be started";
            return false;
        }
        // Up to a minute: a CI runner starting Python cold can be slow, and
        // a scanner that is merely slow must not read as one that failed.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline) {
            const std::string written = ReadText(ready);
            if (!written.empty()) {
                port = std::atoi(written.c_str());
                if (port > 0) return true;
                failure = "it wrote no port number";
                return false;
            }
            if (!process.Running()) {
                failure = "it exited";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        failure = "it was still running but not listening after 60 seconds";
        return false;
    }

    // Why Start() failed.
    const std::string& Failure() const { return failure; }

    int Port() const { return port; }

    void Stop() { process.Stop(); }

private:
    ChildProcess process;
    int port = 0;
    std::string failure;
};

// What is kept for `address` now; an empty pin when nothing is.
IODeviceTrustedCertificate KeptFor(const std::string& address) {
    for (const IODeviceTrustedCertificate& certificate : IODeviceTrustedCertificates()) {
        if (certificate.address == address) return certificate;
    }
    return IODeviceTrustedCertificate{address, "", ""};
}

}  // namespace

int main(int argc, char** argv) {
    std::cout << "eSCL scanning over HTTPS, from a scanner with a self-signed certificate\n";

    if (argc < 4) {
        std::cout << "SKIPPED: run by ctest, which passes Python 3, the scanner script and "
                     "openssl - missing when CMake found no Python 3 or no openssl\n";
        return SkipOrFail();
    }
    const std::string python = argv[1];
    const std::string script = argv[2];
    const std::string openssl = argv[3];

    const std::string root = MakeTemporaryDirectory();
    if (root.empty()) {
        std::cout << "SKIPPED: no temporary directory\n";
        return SkipOrFail();
    }
    const std::string log = root + "/scanner.log";
    const std::string firstCertificate = root + "/first.pem";
    const std::string firstKey = root + "/first.key";
    const std::string secondCertificate = root + "/second.pem";
    const std::string secondKey = root + "/second.key";
    if (!MakeCertificate(openssl, root, firstCertificate, firstKey, log) ||
        !MakeCertificate(openssl, root, secondCertificate, secondKey, log)) {
        std::cout << "SKIPPED: openssl could not make a certificate:\n" << Tail(log) << "\n";
        fs::remove_all(PathFromUtf8(root));
        return SkipOrFail();
    }
    const std::string firstPin = PinOfCertificateFile(firstCertificate);
    const std::string secondPin = PinOfCertificateFile(secondCertificate);

    // The device keys this test learns go to its own file, never the user's.
    const std::string trustFile = root + "/DeviceCertificates.conf";
    SetEnv("ULTRACANVAS_DEVICE_CERTIFICATES", trustFile);
    UnsetEnv("ULTRACANVAS_DEVICE_TLS_TOFU");
    // The eSCL backend alone: see the top of the file.
    SetEnv("ULTRACANVAS_DEVICE_BACKENDS", "eSCL");

    const std::string firstRequests = root + "/first-requests.log";
    TlsScanner scanner;
    if (!scanner.Start(python, script, firstCertificate, firstKey, firstRequests, 0, log)) {
        std::cout << "SKIPPED: the scanner would not start - " << scanner.Failure() << ":\n"
                  << Tail(log) << "\n";
        fs::remove_all(PathFromUtf8(root));
        return SkipOrFail();
    }
    const int port = scanner.Port();
    const std::string address = "127.0.0.1:" + std::to_string(port);
    const std::string url = "https://" + address + "/eSCL";
    SetEnv("ULTRACANVAS_ESCL_SCANNERS", url);

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Initialize();
    manager.EnumerateDevices(IODeviceCategory::Scanner);

    ScannerDevicePtr device;
    for (const IODevicePtr& listed : manager.GetDevices(IODeviceCategory::Scanner)) {
        if (listed->GetDeviceInfo().connectionPath == url) {
            device = std::dynamic_pointer_cast<ScannerDevice>(listed);
        }
    }
    Check(device != nullptr, "a scanner named in ULTRACANVAS_ESCL_SCANNERS is listed");
    if (!device) {
        manager.Shutdown();
        fs::remove_all(PathFromUtf8(root));
        return EXIT_FAILURE;
    }
    Check(device->GetDeviceInfo().backend == "eSCL", "  by the eSCL backend");
    Check(device->GetDeviceInfo().name == url, "  under its address, all it is known by yet");
    Check(IODeviceTrustedCertificates().empty(), "  with no device key kept yet");

    // --- First contact: the key is learned --------------------------------------
    const IODeviceResult connected = device->Connect();
    Check(connected.success,
          "it connects over https, its self-signed certificate trusted on first use: " +
              connected.message);
    if (!connected.success) {
        std::cout << Tail(log) << "\n";
        manager.Shutdown();
        fs::remove_all(PathFromUtf8(root));
        return EXIT_FAILURE;
    }

    const std::vector<std::string> firstContact = ReadLines(firstRequests);
    Check(!firstContact.empty() && firstContact.front() == "HEAD /",
          "  before its key was known, all it was sent was a bare HEAD / (first request: '" +
              (firstContact.empty() ? std::string() : firstContact.front()) + "')");
    Check(firstContact.size() >= 2 && firstContact[1] == "GET /eSCL/ScannerCapabilities",
          "  and then, pinned to that key, the request it was asked for");

    IODeviceTrustedCertificate kept = KeptFor(address);
    Check(kept.pin == firstPin,
          "  the key kept for " + address + " is the one in its certificate: " +
              (kept.pin.empty() ? std::string("(none)") : kept.pin));

    // --- What it says about itself ----------------------------------------------
    const IODeviceInfo info = device->GetDeviceInfo();
    Check(info.name == "Acme MegaScan 42",
          "its make and model replace the address it was listed under: '" + info.name + "'");
    Check(info.serialNumber == "SN-0001", "  and its serial number is filled in");
    kept = KeptFor(address);
    Check(kept.name == "Acme MegaScan 42",
          "  and its key is kept under that name: '" + kept.name + "'");

    const ScanCapabilities& caps = device->GetCapabilities();
    Check(caps.sources.size() == 3, "it offers the flatbed and both sides of its feeder");
    Check(caps.resolutions.size() == 4, "  at four resolutions");

    // --- Scanning over the pinned connection ------------------------------------
    ScanConfiguration config;
    config.source = ScanSource::ADF;
    config.colorMode = ScanColorMode::Color;
    config.resolutionDpi = 300;
    Check(device->SetConfiguration(config).success, "a feeder configuration is accepted");

    int pages = 0;
    bool pagesWhole = true;
    const IODeviceResult run = device->ScanPages([&](const ScannedImage& page) {
        ++pages;
        pagesWhole = pagesWhole && page.IsValid() && page.width == 64 && page.height == 88 &&
                     page.channels == 3;
        return true;
    });
    Check(run.success && run.code == IODeviceResultCode::Success,
          "a feeder run over the pinned connection completes, the empty feeder ending it: " +
              run.message);
    Check(pages == 3 && pagesWhole, "  with the three pages the scanner had, each 64 x 88 in colour");

    config.source = ScanSource::Flatbed;
    config.colorMode = ScanColorMode::Grayscale;
    device->SetConfiguration(config);
    ScannedImage grey;
    const IODeviceResult flatbed = device->Scan(grey);
    Check(flatbed.success && grey.IsValid() && grey.channels == 1,
          "a grey flatbed page comes back as one channel: " + flatbed.message);

    bool feederJob = false;
    bool greyPlatenJob = false;
    for (const std::string& line : ReadLines(firstRequests)) {
        feederJob = feederJob || line == "POST /eSCL/ScanJobs source=Feeder color=RGB24";
        greyPlatenJob = greyPlatenJob || line == "POST /eSCL/ScanJobs source=Platen color=Grayscale8";
    }
    Check(feederJob && greyPlatenJob, "  the scanner was asked for exactly those two jobs");
    device->Disconnect();

    // --- The scanner comes back with another key --------------------------------
    // Reset, replaced - or something else answering at its address. Only the
    // user can tell which, so it is refused, and the key kept is not touched.
    scanner.Stop();
    const std::string secondRequests = root + "/second-requests.log";
    if (!scanner.Start(python, script, secondCertificate, secondKey, secondRequests, port, log)) {
        Check(false, "the scanner restarts on the same port with another key - " +
                         scanner.Failure() + ":\n" + Tail(log));
    } else {
        const IODeviceResult refused = device->Connect();
        Check(!refused.success && refused.message.find("different certificate") != std::string::npos,
              "with another key, it is refused: " + refused.message);
        Check(refused.message.find("UOS-Settings") != std::string::npos,
              "  and the message says where the old key is forgotten");
        Check(ReadLines(secondRequests).empty(),
              "  before a single request reached it, not even the HEAD of first contact");
        kept = KeptFor(address);
        Check(kept.pin == firstPin, "  and the key kept is still the first one");

        Check(IODeviceForgetCertificate(url), "forgetting the key kept for it");
        const IODeviceResult relearned = device->Connect();
        kept = KeptFor(address);
        Check(relearned.success && kept.pin == secondPin && kept.name == "Acme MegaScan 42",
              "  lets the next connection learn its new key, kept under its name: " +
                  relearned.message);
        device->Disconnect();

        IODeviceForgetCertificate(url);
        SetEnv("ULTRACANVAS_DEVICE_TLS_TOFU", "0");
        const IODeviceResult notLearned = device->Connect();
        Check(!notLearned.success && IODeviceTrustedCertificates().empty(),
              "with learning switched off, a scanner whose key is not kept is refused: " +
                  notLearned.message);
        UnsetEnv("ULTRACANVAS_DEVICE_TLS_TOFU");
    }

    device->Disconnect();
    manager.Shutdown();
    scanner.Stop();
    fs::remove_all(PathFromUtf8(root));

    std::cout << "\n" << g_passed << " passed, " << g_failed << " failed\n";
    if (g_failed == 0) {
        std::cout << "All live eSCL scanner checks passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

#else

int main() {
    std::cout << "SKIPPED: the live eSCL test needs UltraNet\n";
    return 77;
}

#endif
