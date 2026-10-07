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
// Skipped - exit code 77 - without Python 3 (ctest passes the interpreter
// and the scanner script as arguments) or the openssl command, or when the
// scanner will not start. CI's Linux rows set ULTRACANVAS_TEST_ESCL_REQUIRED,
// which turns a skip into a failure. The keys go to a file of the test's own
// (ULTRACANVAS_DEVICE_CERTIFICATES), never the user's.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include <cstdlib>
#include <iostream>

#if (defined(__linux__) || defined(__APPLE__)) && defined(ULTRACANVAS_HAS_NET)

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "IODeviceManager/UltraCanvasIODeviceScanner.h"
#include "IODeviceManager/UltraCanvasIODeviceTlsTrust.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetTls.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

constexpr int kSkipped = 77;

// What a skip returns. CI sets ULTRACANVAS_TEST_ESCL_REQUIRED on the rows
// that have Python and openssl, because there a skip is indistinguishable
// from a pass and would let the test quietly stop running.
int SkipOrFail() {
    const char* required = std::getenv("ULTRACANVAS_TEST_ESCL_REQUIRED");
    if (required && *required && std::string(required) != "0") {
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

std::string FindOnPath(const std::string& program) {
    if (const char* path = std::getenv("PATH")) {
        std::stringstream list(path);
        std::string directory;
        while (std::getline(list, directory, ':')) {
            const std::string candidate = directory + "/" + program;
            if (!directory.empty() && access(candidate.c_str(), X_OK) == 0) return candidate;
        }
    }
    return std::string();
}

// Starts `args`, its output going to `log`; the process id, or -1.
pid_t Spawn(std::vector<std::string> args, const std::string& log) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int spawned = posix_spawn(&pid, args[0].c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    return spawned == 0 ? pid : -1;
}

// Runs `args` to the end; true when it exits with 0.
bool Run(const std::vector<std::string>& args, const std::string& log) {
    const pid_t pid = Spawn(args, log);
    if (pid <= 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
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
    while (std::getline(text, line)) lines.push_back(line);
    return lines;
}

std::string Tail(const std::string& path) {
    const std::string text = ReadText(path);
    return text.size() > 800 ? text.substr(text.size() - 800) : text;
}

// A self-signed certificate and its key, as a scanner makes for itself -
// named for its model, not the address it is reached at, as theirs often
// are; false when openssl could not make one.
bool MakeCertificate(const std::string& openssl, const std::string& certificate,
                     const std::string& key, const std::string& log) {
    return Run({openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                "-subj", "/CN=Acme MegaScan 42", "-keyout", key, "-out", certificate},
               log);
}

std::string PinOfCertificateFile(const std::string& path) {
    const std::string pem = ReadText(path);
    return UltraNet_PublicKeyPinOf(std::vector<uint8_t>(pem.begin(), pem.end()));
}

// The scanner process, stopped however the test ends.
class TlsScanner {
public:
    ~TlsScanner() { Stop(); }

    // Starts it with `certificate` on `port` (0: any free port, which
    // Port() then gives) and waits until it is listening.
    bool Start(const std::string& python, const std::string& script, const std::string& certificate,
               const std::string& key, const std::string& requests, int listenOn,
               const std::string& log) {
        const std::string ready = requests + ".port";
        std::error_code ignored;
        fs::remove(PathFromUtf8(ready), ignored);
        pid = Spawn({python, script, "--cert", certificate, "--key", key, "--ready", ready,
                     "--log", requests, "--port", std::to_string(listenOn), "--pages", "3"},
                    log);
        if (pid <= 0) return false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::string written = ReadText(ready);
            if (!written.empty()) {
                port = std::atoi(written.c_str());
                return port > 0;
            }
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) {
                pid = -1;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    int Port() const { return port; }

    void Stop() {
        if (pid <= 0) return;
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
        pid = -1;
    }

private:
    pid_t pid = -1;
    int port = 0;
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

    if (argc < 3) {
        std::cout << "SKIPPED: run by ctest, which passes Python 3 and the scanner script\n";
        return SkipOrFail();
    }
    const std::string python = argv[1];
    const std::string script = argv[2];
    const std::string openssl = FindOnPath("openssl");
    if (openssl.empty()) {
        std::cout << "SKIPPED: the openssl command is not installed\n";
        return SkipOrFail();
    }

    char pattern[] = "/tmp/uc-escl-live-XXXXXX";
    if (!mkdtemp(pattern)) {
        std::cout << "SKIPPED: no temporary directory\n";
        return SkipOrFail();
    }
    const std::string root = pattern;
    const std::string log = root + "/scanner.log";
    const std::string firstCertificate = root + "/first.pem";
    const std::string firstKey = root + "/first.key";
    const std::string secondCertificate = root + "/second.pem";
    const std::string secondKey = root + "/second.key";
    if (!MakeCertificate(openssl, firstCertificate, firstKey, log) ||
        !MakeCertificate(openssl, secondCertificate, secondKey, log)) {
        std::cout << "SKIPPED: openssl could not make a certificate:\n" << Tail(log) << "\n";
        fs::remove_all(PathFromUtf8(root));
        return SkipOrFail();
    }
    const std::string firstPin = PinOfCertificateFile(firstCertificate);
    const std::string secondPin = PinOfCertificateFile(secondCertificate);

    // The device keys this test learns go to its own file, never the user's.
    const std::string trustFile = root + "/DeviceCertificates.conf";
    setenv("ULTRACANVAS_DEVICE_CERTIFICATES", trustFile.c_str(), 1);
    unsetenv("ULTRACANVAS_DEVICE_TLS_TOFU");

    const std::string firstRequests = root + "/first-requests.log";
    TlsScanner scanner;
    if (!scanner.Start(python, script, firstCertificate, firstKey, firstRequests, 0, log)) {
        std::cout << "SKIPPED: the scanner would not start:\n" << Tail(log) << "\n";
        fs::remove_all(PathFromUtf8(root));
        return SkipOrFail();
    }
    const int port = scanner.Port();
    const std::string address = "127.0.0.1:" + std::to_string(port);
    const std::string url = "https://" + address + "/eSCL";
    setenv("ULTRACANVAS_ESCL_SCANNERS", url.c_str(), 1);

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
        Check(false, "the scanner restarts on the same port with another key:\n" + Tail(log));
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
        setenv("ULTRACANVAS_DEVICE_TLS_TOFU", "0", 1);
        const IODeviceResult notLearned = device->Connect();
        Check(!notLearned.success && IODeviceTrustedCertificates().empty(),
              "with learning switched off, a scanner whose key is not kept is refused: " +
                  notLearned.message);
        unsetenv("ULTRACANVAS_DEVICE_TLS_TOFU");
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
    std::cout << "SKIPPED: the live eSCL test runs on Linux and macOS, with UltraNet\n";
    return 77;
}

#endif
