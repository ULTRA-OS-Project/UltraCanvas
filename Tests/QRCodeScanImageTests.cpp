// Tests/QRCodeScanImageTests.cpp
// Unit tests for QRCodeUtils::ScanQRCodeImage — decoding a QR code from pixels
// already in memory.
//
// The property under test is a round trip: generate a QR code, paint it into a
// buffer in each supported pixel layout, and read the same string back. That
// matters more than it sounds, because the in-memory path exists specifically
// so camera frames never reach the disk
// (Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md §2.2c) — if it
// silently decoded differently from the file path, the camera flow would fail
// in ways no file-based test would catch.
//
// Nothing here touches the filesystem, which is the point.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "Plugins/QRCode/UltraCanvasQRCode.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

// A realistic payload: this is exactly what an enrolment QR carries.
static const char* kUri =
    "otpauth://totp/Example:alice@example.com"
    "?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=Example";

namespace {

struct Bitmap {
    std::vector<std::uint8_t> pixels;
    int width  = 0;
    int height = 0;
    int stride = 0;
};

// Paints a generated QR matrix into a pixel buffer.
//
// `quiet` is the mandatory quiet zone: the QR spec requires four modules of
// blank margin, and a decoder is entitled to refuse a code drawn without it.
// `extraStride` pads each row so the tests can prove stride handling with a
// buffer whose rows are not tightly packed — the camera delivers exactly that.
Bitmap Paint(const QRCodeData& qr, int scale, int quiet,
             QRCodeUtils::QRPixelFormat format, int extraStride = 0) {
    int bpp = 1;
    switch (format) {
        case QRCodeUtils::QRPixelFormat::Grayscale8: bpp = 1; break;
        case QRCodeUtils::QRPixelFormat::RGB24:      bpp = 3; break;
        case QRCodeUtils::QRPixelFormat::RGBA32:
        case QRCodeUtils::QRPixelFormat::BGRA32:     bpp = 4; break;
    }

    Bitmap bmp;
    bmp.width  = (qr.size + 2 * quiet) * scale;
    bmp.height = bmp.width;
    bmp.stride = bmp.width * bpp + extraStride;
    // 0xA5 in the padding: if a stride bug made the decoder read it as image
    // data, the noise shows up as a decode failure rather than passing by luck.
    bmp.pixels.assign(static_cast<size_t>(bmp.stride) * bmp.height, 0xA5);

    for (int y = 0; y < bmp.height; ++y) {
        std::uint8_t* row = bmp.pixels.data() +
                            static_cast<size_t>(y) * bmp.stride;
        for (int x = 0; x < bmp.width; ++x) {
            const int mx = x / scale - quiet;
            const int my = y / scale - quiet;
            const bool dark = qr.IsDark(mx, my);
            const std::uint8_t v = dark ? 0 : 255;
            std::uint8_t* p = row + x * bpp;
            switch (format) {
                case QRCodeUtils::QRPixelFormat::Grayscale8:
                    p[0] = v;
                    break;
                case QRCodeUtils::QRPixelFormat::RGB24:
                    p[0] = p[1] = p[2] = v;
                    break;
                case QRCodeUtils::QRPixelFormat::RGBA32:
                case QRCodeUtils::QRPixelFormat::BGRA32:
                    // Grey is symmetric under RGB/BGR, so the same fill serves
                    // both; the channel order is exercised by the colour test.
                    p[0] = p[1] = p[2] = v;
                    p[3] = 255;
                    break;
            }
        }
    }
    return bmp;
}

} // namespace

// ===========================================================================

static void TestRoundTripEachFormat() {
    std::printf("Round trip in every pixel layout\n");

    QRCodeData qr = QRCodeUtils::GenerateQRCode(kUri);
    Check(qr.valid, "QR generated");
    if (!qr.valid) return;

    const struct { QRCodeUtils::QRPixelFormat fmt; const char* name; } kFormats[] = {
        {QRCodeUtils::QRPixelFormat::Grayscale8, "Grayscale8"},
        {QRCodeUtils::QRPixelFormat::RGB24,      "RGB24"},
        {QRCodeUtils::QRPixelFormat::RGBA32,     "RGBA32"},
        {QRCodeUtils::QRPixelFormat::BGRA32,     "BGRA32"},
    };

    for (const auto& f : kFormats) {
        Bitmap bmp = Paint(qr, 6, 4, f.fmt);
        std::string err;
        auto results = QRCodeUtils::ScanQRCodeImage(
            bmp.pixels.data(), bmp.width, bmp.height, bmp.stride, f.fmt, &err);
        Check(!results.empty(),
              std::string(f.name) + ": a symbol was decoded (" + err + ")");
        if (!results.empty()) {
            Check(results[0].data == kUri,
                  std::string(f.name) + ": the exact URI came back");
        }
    }
}

// The camera hands over rows with padding between them. Reading that padding as
// image data is the classic stride bug, so it is tested explicitly.
static void TestPaddedStride() {
    std::printf("Padded row stride\n");

    QRCodeData qr = QRCodeUtils::GenerateQRCode(kUri);
    Check(qr.valid, "QR generated");
    if (!qr.valid) return;

    Bitmap bmp = Paint(qr, 6, 4, QRCodeUtils::QRPixelFormat::RGBA32,
                       /*extraStride=*/37);
    std::string err;
    auto results = QRCodeUtils::ScanQRCodeImage(
        bmp.pixels.data(), bmp.width, bmp.height, bmp.stride,
        QRCodeUtils::QRPixelFormat::RGBA32, &err);
    Check(!results.empty(), "decoded despite padded rows (" + err + ")");
    if (!results.empty()) Check(results[0].data == kUri, "URI intact");
}

// A stride of 0 means "tightly packed" — the common case for a buffer built by
// hand rather than by a capture device.
static void TestZeroStrideMeansPacked() {
    std::printf("Zero stride means tightly packed\n");

    QRCodeData qr = QRCodeUtils::GenerateQRCode(kUri);
    if (!qr.valid) { Check(false, "QR generated"); return; }

    Bitmap bmp = Paint(qr, 6, 4, QRCodeUtils::QRPixelFormat::Grayscale8);
    std::string err;
    auto results = QRCodeUtils::ScanQRCodeImage(
        bmp.pixels.data(), bmp.width, bmp.height, /*stride=*/0,
        QRCodeUtils::QRPixelFormat::Grayscale8, &err);
    Check(!results.empty(), "decoded with stride 0 (" + err + ")");
    if (!results.empty()) Check(results[0].data == kUri, "URI intact");
}

// Hostile or malformed arguments must be refused rather than read past the end
// of the buffer. These are the cases a camera backend could plausibly produce
// after a mode change.
static void TestRejectsBadArguments() {
    std::printf("Bad arguments are refused\n");

    QRCodeData qr = QRCodeUtils::GenerateQRCode(kUri);
    if (!qr.valid) { Check(false, "QR generated"); return; }
    Bitmap bmp = Paint(qr, 6, 4, QRCodeUtils::QRPixelFormat::RGBA32);

    std::string err;
    Check(QRCodeUtils::ScanQRCodeImage(nullptr, bmp.width, bmp.height,
                                       bmp.stride,
                                       QRCodeUtils::QRPixelFormat::RGBA32,
                                       &err).empty(),
          "a null buffer is refused");

    Check(QRCodeUtils::ScanQRCodeImage(bmp.pixels.data(), 0, bmp.height,
                                       bmp.stride,
                                       QRCodeUtils::QRPixelFormat::RGBA32,
                                       &err).empty(),
          "zero width is refused");

    Check(QRCodeUtils::ScanQRCodeImage(bmp.pixels.data(), bmp.width, -1,
                                       bmp.stride,
                                       QRCodeUtils::QRPixelFormat::RGBA32,
                                       &err).empty(),
          "negative height is refused");

    // A stride shorter than one row would walk off the end of the allocation.
    err.clear();
    Check(QRCodeUtils::ScanQRCodeImage(bmp.pixels.data(), bmp.width, bmp.height,
                                       bmp.width * 4 - 1,
                                       QRCodeUtils::QRPixelFormat::RGBA32,
                                       &err).empty(),
          "a too-small stride is refused");
    Check(err.find("Stride") != std::string::npos,
          "and says so rather than failing vaguely");
}

// A frame of noise must not produce a symbol. Without this, "decoded nothing"
// and "decoded everything" would look the same in the tests above.
static void TestBlankImageDecodesNothing() {
    std::printf("An image with no QR decodes nothing\n");

    const int w = 200, h = 200;
    std::vector<std::uint8_t> gray(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < gray.size(); ++i) {
        gray[i] = static_cast<std::uint8_t>((i * 37) % 256);
    }

    std::string err;
    auto results = QRCodeUtils::ScanQRCodeImage(
        gray.data(), w, h, 0, QRCodeUtils::QRPixelFormat::Grayscale8, &err);
    Check(results.empty(), "no symbol found in noise");
    Check(!err.empty(), "and an error message explains why");
}

// The overload the camera flow actually calls.
static void TestVideoFrameOverload() {
    std::printf("UCVideoFrame overload\n");

    QRCodeData qr = QRCodeUtils::GenerateQRCode(kUri);
    if (!qr.valid) { Check(false, "QR generated"); return; }
    Bitmap bmp = Paint(qr, 6, 4, QRCodeUtils::QRPixelFormat::RGBA32);

    auto frame = UCVideoFrame::FromRGBA(bmp.pixels.data(), bmp.width,
                                        bmp.height, bmp.stride);
    Check(frame && frame->IsValid(), "frame built");
    if (frame && frame->IsValid()) {
        std::string err;
        auto results = QRCodeUtils::ScanQRCodeImage(*frame, &err);
        Check(!results.empty(), "decoded from a video frame (" + err + ")");
        if (!results.empty()) Check(results[0].data == kUri, "URI intact");
    }

    // An empty frame must be reported, not dereferenced.
    UCVideoFrame empty;
    std::string err2;
    Check(QRCodeUtils::ScanQRCodeImage(empty, &err2).empty(),
          "an invalid frame is refused");
    Check(!err2.empty(), "with an error message");
}

int main() {
    std::printf("QR in-memory scan tests — decoder: %s\n\n",
                QRCodeUtils::IsDecoderAvailable() ? "libzbar" : "none");
    if (!QRCodeUtils::IsDecoderAvailable()) {
        // Not a failure: the plugin is designed to build without zbar. But the
        // scan path cannot be tested, and saying so beats a silent pass.
        std::printf("SKIP: built without a QR decoder; nothing to test.\n");
        return 0;
    }

    TestRoundTripEachFormat();
    TestPaddedStride();
    TestZeroStrideMeansPacked();
    TestRejectsBadArguments();
    TestBlankImageDecodesNothing();
    TestVideoFrameOverload();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
