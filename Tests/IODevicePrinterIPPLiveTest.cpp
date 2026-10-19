// Tests/IODevicePrinterIPPLiveTest.cpp
// The IPP printer backend against a real IPP Everywhere printer: CUPS's
// reference implementation, ippeveprinter, started here and printed to
// through nothing but the public IODeviceManager API.
//
// IODevicePrinterIPPTest covers everything that can be decided without a
// printer. This covers what cannot, and it is not a formality: run by hand
// against ippeveprinter while the backend was being written, it found five
// things that test could not - the mDNS plugin's names were full service
// names, not instance names; a printer busy with one job refuses the next
// rather than queueing it; decoding an image for printing crashed a program
// that had never opened a window; and text was drawn edge to edge, into the
// border a printer cannot print. So it is kept, rather than left behind in
// the scratch directory it was written in.
//
// Skipped - exit code 77, which ctest reports as skipped - where
// ippeveprinter is not installed (it is in Debian and Ubuntu's
// cups-ipp-utils) or will not start. It is a check against a real
// implementation where one is to hand, not a dependency. ippeveprinter
// (CUPS 2.4) needs two things from the machine even with -r off: a running
// DNS-SD daemon (Avahi on Linux), or it stops with "Unable to initialize
// DNS-SD", and IPv6, or it stops with "Unable to create IPv6 listener" - so a
// container without IPv6 skips here however it is set up. CI's Linux rows
// install it, start Avahi, and set ULTRACANVAS_TEST_IPP_REQUIRED, which turns
// a skip into a failure.
//
// The same printer is also reached over ipps://, where it presents the
// self-signed certificate it made itself (-K), the way real printers do: the
// device trust (UltraCanvasIODeviceTlsTrust.h) learns its key on first
// contact, prints through the pinned connection, refuses a key that differs
// from the one kept, and learns again once the old key is forgotten. The keys
// go to a file of the test's own (ULTRACANVAS_DEVICE_CERTIFICATES), never the
// user's.
// Version: 1.3.0
// Author: UltraCanvas Framework

#include <cstdlib>
#include <iostream>

#if (defined(__linux__) || defined(__APPLE__)) && defined(ULTRACANVAS_HAS_NET)

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "IODeviceManager/UltraCanvasIODevicePrinter.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterIPP.h"
#include "IODeviceManager/UltraCanvasIODeviceTlsTrust.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// What a skip returns. CI sets ULTRACANVAS_TEST_IPP_REQUIRED on the rows that
// install ippeveprinter, because there a skip is indistinguishable from a pass
// and would let the test quietly stop running - the same reason
// ULTRAFIBU_TEST_PG_REQUIRED exists for the multi-user database test.
int SkipOrFail() {
    const char* required = std::getenv("ULTRACANVAS_TEST_IPP_REQUIRED");
    if (required && *required && std::string(required) != "0") {
        std::cout << "FAILED: ULTRACANVAS_TEST_IPP_REQUIRED is set, so a skip is a failure\n";
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

// ippeveprinter lives in sbin, which is often not on a user's PATH.
std::string FindIppEvePrinter() {
    std::vector<std::string> directories;
    if (const char* path = std::getenv("PATH")) {
        std::stringstream list(path);
        std::string directory;
        while (std::getline(list, directory, ':')) directories.push_back(directory);
    }
    for (const char* extra : {"/usr/sbin", "/usr/local/sbin", "/opt/homebrew/sbin"}) {
        directories.push_back(extra);
    }
    for (const std::string& directory : directories) {
        const std::string candidate = directory + "/ippeveprinter";
        if (!directory.empty() && access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return std::string();
}

// A printer process, stopped however the test ends.
class ReferencePrinter {
public:
    ~ReferencePrinter() { Stop(); }

    bool Start(const std::string& program, const std::string& spool, const std::string& keys,
               int port, const std::string& log) {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

        const std::string portText = std::to_string(port);
        // Two-sided, keeping every job's document (-k) in the spool, taking
        // PDF and JPEG as they are and PWG raster for everything else; fast
        // (-s), so a job is done in a moment; and not advertised on the
        // network (-r off), so running the test publishes no printer. Its
        // self-signed certificate is made in a directory of the test's own
        // (-K), and it answers TLS on the same port, as ipps://.
        std::vector<std::string> args = {program, "-2", "-k", "-d", spool, "-K", keys,
                                         "-f", "image/pwg-raster,image/jpeg,application/pdf",
                                         "-s", "600", "-r", "off", "-p", portText,
                                         "UltraCanvas Live Test"};
        std::vector<char*> argv;
        for (std::string& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);

        const int spawned = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
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

std::string ReadTail(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path));
    std::stringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();
    return text.size() > 600 ? text.substr(text.size() - 600) : text;
}

// The spool file ippeveprinter kept for job `jobId`: "<id>-<name>.<ext>".
std::string WaitForSpoolFile(const std::string& spool, int jobId, const std::string& extension) {
    const std::string prefix = std::to_string(jobId) + "-";
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(UltraCanvas::PathFromUtf8(spool), error)) {
            const std::string name = PathToUtf8(entry.path().filename());
            if (name.rfind(prefix, 0) == 0 && PathToUtf8(entry.path().extension()) == extension) {
                return PathToUtf8(entry.path());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return std::string();
}

std::vector<uint8_t> ReadBytes(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    return (static_cast<uint32_t>(b[at]) << 24) | (static_cast<uint32_t>(b[at + 1]) << 16) |
           (static_cast<uint32_t>(b[at + 2]) << 8) | static_cast<uint32_t>(b[at + 3]);
}

// What arrived at the printer, read per PWG 5102.4: the page count, the
// first page's geometry, and how far from the left edge its first ink is.
struct ReceivedRaster {
    bool valid = false;
    int pages = 0;
    uint32_t width = 0, height = 0, dpi = 0, colorSpace = 0, pageWidthPoints = 0;
    int firstInkX = -1;
};

ReceivedRaster ReadPwgRaster(const std::vector<uint8_t>& data) {
    ReceivedRaster raster;
    if (data.size() < 4 || std::memcmp(data.data(), "RaS2", 4) != 0) return raster;
    size_t at = 4;
    while (at < data.size()) {
        if (at + 1796 > data.size()) return raster;
        const size_t header = at;
        at += 1796;
        const uint32_t w = U32(data, header + 372), h = U32(data, header + 376);
        const uint32_t bpp = U32(data, header + 388) / 8;
        if (raster.pages == 0) {
            raster.width = w;
            raster.height = h;
            raster.dpi = U32(data, header + 276);
            raster.colorSpace = U32(data, header + 400);
            raster.pageWidthPoints = U32(data, header + 352);
        }
        uint32_t y = 0;
        while (y < h) {
            if (at >= data.size()) return raster;
            const uint32_t repeats = data[at++] + 1u;
            uint32_t x = 0;
            while (x < w) {
                if (at >= data.size()) return raster;
                const int n = data[at++];
                const uint32_t count = n <= 127 ? static_cast<uint32_t>(n + 1) : static_cast<uint32_t>(257 - n);
                const bool literal = n > 127;
                for (uint32_t i = 0; i < count; ++i) {
                    const size_t pixel = at + (literal ? i * bpp : 0);
                    if (pixel + bpp > data.size()) return raster;
                    if (raster.pages == 0 && data[pixel] < 128 &&
                        (raster.firstInkX < 0 || static_cast<int>(x + i) < raster.firstInkX)) {
                        raster.firstInkX = static_cast<int>(x + i);
                    }
                }
                at += literal ? count * bpp : bpp;
                x += count;
            }
            if (x != w) return raster;
            y += repeats;
        }
        ++raster.pages;
    }
    raster.valid = raster.pages > 0;
    return raster;
}

void WaitUntilFinished(const PrinterDevicePtr& printer, int jobId) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (printer->GetJobStatus(jobId).IsFinished()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

}  // namespace

int main() {
    std::cout << "IPP printing against CUPS's reference printer (ippeveprinter)\n";

    const std::string program = FindIppEvePrinter();
    if (program.empty()) {
        std::cout << "SKIPPED: ippeveprinter is not installed (Debian/Ubuntu: cups-ipp-utils)\n";
        return SkipOrFail();
    }

    char pattern[] = "/tmp/uc-ipp-live-XXXXXX";
    if (!mkdtemp(pattern)) {
        std::cout << "SKIPPED: no temporary directory\n";
        return SkipOrFail();
    }
    const std::string root = pattern;
    const std::string spool = root + "/spool";
    fs::create_directories(PathFromUtf8(spool));
    const std::string keys = root + "/keys";
    fs::create_directories(PathFromUtf8(keys));
    // The device keys this test learns go to its own file, never the user's.
    const std::string trustFile = root + "/DeviceCertificates.conf";
    setenv("ULTRACANVAS_DEVICE_CERTIFICATES", trustFile.c_str(), 1);
    unsetenv("ULTRACANVAS_DEVICE_TLS_TOFU");
    const std::string log = root + "/ippeveprinter.log";

    // Documents: text long enough for several pages, and a PDF, which
    // ippeveprinter stores without rendering, so a hand-made one will do.
    const std::string textPath = root + "/letter.txt";
    {
        std::ofstream text(UltraCanvas::PathFromUtf8(textPath));
        for (int line = 1; line <= 200; ++line) text << "Line " << line << " of a letter to the printer\n";
    }
    const std::string pdfPath = root + "/three-pages.pdf";
    const std::string pdf = "%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n"
                            "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n"
                            "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 595 842]>>endobj\n"
                            "trailer<</Root 1 0 R>>\n%%EOF\n";
    { std::ofstream(UltraCanvas::PathFromUtf8(pdfPath), std::ios::binary) << pdf; }

    const int port = 20000 + static_cast<int>(getpid() % 20000);
    ReferencePrinter reference;
    if (!reference.Start(program, spool, keys, port, log)) {
        std::cout << "SKIPPED: ippeveprinter would not start\n";
        fs::remove_all(PathFromUtf8(root));
        return SkipOrFail();
    }

    const std::string uri = "ipp://localhost:" + std::to_string(port) + "/ipp/print";
    // The same printer over TLS, listed apart: see "Over ipps://" below.
    const std::string tlsUri = "ipps://localhost:" + std::to_string(port) + "/ipp/print";
    const std::string tlsAddress = "localhost:" + std::to_string(port);
    setenv("ULTRACANVAS_IPP_PRINTERS", (uri + "," + tlsUri).c_str(), 1);

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Initialize();
    manager.EnumerateDevices(IODeviceCategory::Printer);

    PrinterDevicePtr printer;
    PrinterDevicePtr tlsPrinter;
    for (const IODevicePtr& device : manager.GetDevices(IODeviceCategory::Printer)) {
        if (device->GetDeviceInfo().connectionPath == uri) {
            printer = std::dynamic_pointer_cast<PrinterDevice>(device);
        } else if (device->GetDeviceInfo().connectionPath == tlsUri) {
            tlsPrinter = std::dynamic_pointer_cast<PrinterDevice>(device);
        }
    }
    Check(printer != nullptr, "a printer named in ULTRACANVAS_IPP_PRINTERS is listed");
    if (!printer) {
        manager.Shutdown();
        fs::remove_all(PathFromUtf8(root));
        return EXIT_FAILURE;
    }
    Check(printer->GetBackendName() == "IPP", "  by the IPP backend");

    // The printer takes a moment to start listening; one that exits instead
    // means this machine cannot run it (no IPv6 loopback, a port in use),
    // which is the environment's problem, not the backend's.
    IODeviceResult connected;
    for (int attempt = 0; attempt < 75; ++attempt) {
        connected = printer->Connect();
        if (connected.success) break;
        if (!reference.Running()) {
            std::cout << "SKIPPED: ippeveprinter exited:\n" << ReadTail(log) << "\n";
            manager.Shutdown();
            fs::remove_all(PathFromUtf8(root));
            return SkipOrFail();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    Check(connected.success, "connecting asks the printer to describe itself: " + connected.message);
    if (!connected.success) {
        std::cout << ReadTail(log) << "\n";
        manager.Shutdown();
        fs::remove_all(PathFromUtf8(root));
        return EXIT_FAILURE;
    }

    // --- What it says about itself ------------------------------------------
    const IODeviceInfo info = printer->GetDeviceInfo();
    Check(info.name == "UltraCanvas Live Test",
          "its own printer-name replaces the address it was listed under");

    const IOPrinterCapabilities& caps = printer->GetCapabilities();
    bool a4 = false;
    bool letter = false;
    for (IOPaperSize size : caps.paperSizes) {
        a4 = a4 || size == IOPaperSize::A4;
        letter = letter || size == IOPaperSize::Letter;
    }
    Check(a4 && letter, "its paper sizes include A4 and Letter");
    Check(caps.supportsDuplex == IOSupport::Yes, "it prints two-sided");
    Check(caps.maxCopies > 1, "it makes copies");

    const std::vector<IOPrintRenderer> renderers = printer->GetAvailableRenderers();
    Check(renderers.size() == 1 && renderers[0] == IOPrintRenderer::IPP &&
              printer->GetRenderer() == IOPrintRenderer::IPP,
          "IPP is its one renderer, and Auto chooses it");

    Check(printer->IsReady(), "it is idle and accepting jobs");
    Check(!printer->GetSupplyLevels().empty(), "it reports its supplies");

    // --- The supply query the Windows spooler backend borrows -----------------
    // When a Windows driver keeps its levels to itself, the backend guesses
    // IPP addresses from the queue's port and asks each in turn. It moves on
    // to the next path when one is wrong, and gives up on the host when it
    // cannot connect - so those two failures must not look alike.
    {
        std::vector<IOSupplyLevel> supplies;
        const IODeviceResult asked = Internal::QueryIppSupplyLevels(uri, supplies);
        Check(asked.success && !supplies.empty(),
              "asked directly, the printer reports its supplies: " + asked.message);

        std::vector<IOSupplyLevel> none;
        const IODeviceResult wrongPath = Internal::QueryIppSupplyLevels(
            "ipp://localhost:" + std::to_string(port) + "/no/such/printer", none);
        Check(!wrongPath.success && wrongPath.code != IODeviceResultCode::ConnectionFailed,
              "a wrong path on the printer fails, but not as unreachable: " + wrongPath.message);

        const IODeviceResult nobody =
            Internal::QueryIppSupplyLevels("ipp://127.0.0.1:1/ipp/print", none);
        Check(!nobody.success && nobody.code == IODeviceResultCode::ConnectionFailed,
              "a host that takes no connection is unreachable: " + nobody.message);
    }

    // --- Text, drawn here as PWG raster ---------------------------------------
    IOPrintJob text;
    text.jobName = "letter";
    text.filePath = textPath;
    text.options.page.paperSize = IOPaperSize::A4;
    const IODeviceResult textPrinted = printer->Print(text);
    Check(textPrinted.success && textPrinted.backendCode > 0,
          "a text file prints, and the printer gives it a job id: " + textPrinted.message);
    if (textPrinted.success) {
        const std::string file = WaitForSpoolFile(spool, textPrinted.backendCode, ".pwg");
        Check(!file.empty(), "  it arrives as PWG raster");
        const ReceivedRaster raster = file.empty() ? ReceivedRaster() : ReadPwgRaster(ReadBytes(file));
        Check(raster.valid && raster.pages >= 2, "  which decodes, all " +
                                                    std::to_string(raster.pages) + " pages of it");
        Check(raster.pageWidthPoints == 595 && raster.dpi == 300,
              "  on A4, at the 300 dpi the printer offers for normal quality");
        Check(raster.colorSpace == 18, "  in grey, because this printer has no colour");
        Check(raster.firstInkX >= 40,
              "  and the text starts inside the printer's margin (first ink at x=" +
                  std::to_string(raster.firstInkX) + "), not at the sheet's edge");
        WaitUntilFinished(printer, textPrinted.backendCode);
    }

    // --- The second page only: selected here, not sent as a range ------------
    IOPrintJob secondPage = text;
    secondPage.jobName = "page two";
    secondPage.pageRange = {2};
    const IODeviceResult rangePrinted = printer->Print(secondPage);
    Check(rangePrinted.success, "page 2 alone prints: " + rangePrinted.message);
    if (rangePrinted.success) {
        const std::string file = WaitForSpoolFile(spool, rangePrinted.backendCode, ".pwg");
        const ReceivedRaster raster = file.empty() ? ReceivedRaster() : ReadPwgRaster(ReadBytes(file));
        Check(raster.valid && raster.pages == 1, "  as exactly one page");
        WaitUntilFinished(printer, rangePrinted.backendCode);
    }

    // --- A PDF, sent as it is --------------------------------------------------
    IOPrintJob document;
    document.jobName = "document";
    document.filePath = pdfPath;
    const IODeviceResult pdfPrinted = printer->Print(document);
    Check(pdfPrinted.success, "a PDF prints: " + pdfPrinted.message);
    if (pdfPrinted.success) {
        const std::string file = WaitForSpoolFile(spool, pdfPrinted.backendCode, ".pdf");
        Check(!file.empty() && ReadBytes(file) == ReadBytes(pdfPath),
              "  and arrives byte for byte as it was sent, for the printer to render");
        WaitUntilFinished(printer, pdfPrinted.backendCode);
    }

    // --- What it cannot take ---------------------------------------------------
    IOPrintJob spreadsheet;
    spreadsheet.jobName = "sheet";
    spreadsheet.data = {1, 2, 3};
    spreadsheet.mimeType = "application/vnd.ms-excel";
    const IODeviceResult refused = printer->Print(spreadsheet);
    Check(!refused.success && refused.code == IODeviceResultCode::NotSupported,
          "a spreadsheet is refused by name: " + refused.message);

    // --- Jobs --------------------------------------------------------------------
    if (textPrinted.success) {
        const IOPrintJobStatus status = printer->GetJobStatus(textPrinted.backendCode);
        Check(status.jobId == textPrinted.backendCode && status.IsFinished(),
              std::string("a printed job's status is read back: ") + IOPrintJobStateToString(status.state));
    }
    const IODeviceResult cancelMissing = printer->CancelJob(987654);
    Check(!cancelMissing.success, "cancelling a job the printer never had fails: " + cancelMissing.message);

    // --- Over ipps://, trusted on first use ------------------------------------
    // The printer's certificate is one it signed itself, which ordinary
    // verification refuses; the device trust learns its key the first time
    // and pins every later connection to it.
    Check(tlsPrinter != nullptr, "the same printer is listed again under its ipps:// address");
    Check(IODeviceTrustedCertificates().empty(), "  with no device key kept yet");
    if (tlsPrinter) {
        const IODeviceResult tlsConnected = tlsPrinter->Connect();
        Check(tlsConnected.success,
              "over ipps:// it connects, its self-signed certificate trusted on first use: " +
                  tlsConnected.message);

        std::string learnedPin;
        for (const IODeviceTrustedCertificate& kept : IODeviceTrustedCertificates()) {
            if (kept.address == tlsAddress) {
                learnedPin = kept.pin;
                Check(kept.name == "UltraCanvas Live Test",
                      "  its key is kept under the name it gives itself, '" + kept.name + "'");
            }
        }
        Check(learnedPin.rfind("sha256//", 0) == 0 && learnedPin.size() > 8,
              "  and its key was kept for " + tlsAddress + ": " + learnedPin);

        IOPrintJob tlsDocument;
        tlsDocument.jobName = "document over tls";
        tlsDocument.filePath = pdfPath;
        const IODeviceResult tlsPrinted = tlsPrinter->Print(tlsDocument);
        Check(tlsPrinted.success, "a PDF prints over the pinned connection: " + tlsPrinted.message);
        if (tlsPrinted.success) {
            const std::string file = WaitForSpoolFile(spool, tlsPrinted.backendCode, ".pdf");
            Check(!file.empty() && ReadBytes(file) == ReadBytes(pdfPath),
                  "  and arrives byte for byte");
            WaitUntilFinished(tlsPrinter, tlsPrinted.backendCode);
        }

        // Another key on file for the address - as if a different machine
        // had answered there the first time. The printer's real key must be
        // refused, and must not replace the one kept.
        const std::string otherPin = "sha256//EXZWCU5rn8MYG8MMbem9Op0MlXkL0YcPAEjXh0kPAOM=";
        {
            std::ofstream file(UltraCanvas::PathFromUtf8(trustFile), std::ios::binary | std::ios::trunc);
            file << tlsAddress << "=" << otherPin << "\n";
        }
        std::vector<IOSupplyLevel> supplies;
        const IODeviceResult refusedKey = Internal::QueryIppSupplyLevels(tlsUri, supplies);
        Check(!refusedKey.success &&
                  refusedKey.message.find("different certificate") != std::string::npos,
              "a printer whose key differs from the one kept is refused: " + refusedKey.message);
        const std::vector<IODeviceTrustedCertificate> afterRefusal = IODeviceTrustedCertificates();
        Check(afterRefusal.size() == 1 && afterRefusal.front().pin == otherPin,
              "  and the key kept is not replaced by the one it presented");

        Check(IODeviceForgetCertificate(tlsAddress), "forgetting the key kept for it");
        const IODeviceResult relearned = Internal::QueryIppSupplyLevels(tlsUri, supplies);
        const std::vector<IODeviceTrustedCertificate> afterForget = IODeviceTrustedCertificates();
        Check(relearned.success && afterForget.size() == 1 && afterForget.front().pin == learnedPin,
              "  lets the next connection learn its key again - the same key as the first time: " +
                  relearned.message);

        IODeviceForgetCertificate(tlsAddress);
        setenv("ULTRACANVAS_DEVICE_TLS_TOFU", "0", 1);
        const IODeviceResult notLearned = Internal::QueryIppSupplyLevels(tlsUri, supplies);
        Check(!notLearned.success && IODeviceTrustedCertificates().empty(),
              "with learning switched off, a printer whose key is not kept is refused: " +
                  notLearned.message);
        unsetenv("ULTRACANVAS_DEVICE_TLS_TOFU");
        tlsPrinter->Disconnect();
    }

    printer->Disconnect();
    manager.Shutdown();
    reference.Stop();
    fs::remove_all(PathFromUtf8(root));

    std::cout << "\n" << g_passed << " passed, " << g_failed << " failed\n";
    if (g_failed == 0) {
        std::cout << "All live IPP printer checks passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

#else

int main() {
    std::cout << "SKIPPED: the live IPP test runs on Linux and macOS, with UltraNet\n";
    return 77;
}

#endif
