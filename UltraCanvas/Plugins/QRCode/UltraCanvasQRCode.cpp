// Pull the X11/window chain (UltraCanvasLinuxDragDrop.h uses the X11 `None` macro) BEFORE
// the QRCode header, which does `#undef None` for its QRGradientType::None / QRLogoStyle::None
// enumerators. Otherwise DragDrop.h would be parsed after None is undef'd (its Xlib re-include
// being guarded) and fail to compile.
#include "UltraCanvasFileLoader.h"

#include "Plugins/QRCode/UltraCanvasQRCode.h"
#include "third_party/qrcodegen.hpp"
#include <algorithm>
#include <cctype>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

#include <cairo/cairo.h>
#include <vips/vips8>
#ifdef ULTRACANVAS_QRCODE_HAS_DECODER
#include <zbar.h>
#endif

namespace UltraCanvas {

    namespace {

        qrcodegen::QrCode::Ecc ToNayukiEcl(QRErrorCorrection lvl) {
            switch (lvl) {
                case QRErrorCorrection::Low:      return qrcodegen::QrCode::Ecc::LOW;
                case QRErrorCorrection::Medium:   return qrcodegen::QrCode::Ecc::MEDIUM;
                case QRErrorCorrection::Quartile: return qrcodegen::QrCode::Ecc::QUARTILE;
                case QRErrorCorrection::High:     return qrcodegen::QrCode::Ecc::HIGH;
            }
            return qrcodegen::QrCode::Ecc::MEDIUM;
        }

        QRErrorCorrection FromNayukiEcl(qrcodegen::QrCode::Ecc e) {
            switch (e) {
                case qrcodegen::QrCode::Ecc::LOW:      return QRErrorCorrection::Low;
                case qrcodegen::QrCode::Ecc::MEDIUM:   return QRErrorCorrection::Medium;
                case qrcodegen::QrCode::Ecc::QUARTILE: return QRErrorCorrection::Quartile;
                case qrcodegen::QrCode::Ecc::HIGH:     return QRErrorCorrection::High;
            }
            return QRErrorCorrection::Medium;
        }

        std::vector<qrcodegen::QrSegment> BuildSegments(const std::string& text,
                                                       QREncodingMode mode) {
            switch (mode) {
                case QREncodingMode::Numeric:
                    if (!qrcodegen::QrSegment::isNumeric(text.c_str()))
                        throw std::domain_error("text is not all-numeric");
                    return { qrcodegen::QrSegment::makeNumeric(text.c_str()) };

                case QREncodingMode::Alphanumeric:
                    if (!qrcodegen::QrSegment::isAlphanumeric(text.c_str()))
                        throw std::domain_error("text is not all-alphanumeric");
                    return { qrcodegen::QrSegment::makeAlphanumeric(text.c_str()) };

                case QREncodingMode::Byte: {
                    std::vector<std::uint8_t> bytes(text.begin(), text.end());
                    return { qrcodegen::QrSegment::makeBytes(bytes) };
                }

                case QREncodingMode::Auto:
                default:
                    return qrcodegen::QrSegment::makeSegments(text.c_str());
            }
        }

        QRCodeData ToQRCodeData(const qrcodegen::QrCode& qr,
                                const std::string& content,
                                QREncodingMode mode) {
            QRCodeData out;
            out.content         = content;
            out.mode            = mode;
            out.errorCorrection = FromNayukiEcl(qr.getErrorCorrectionLevel());
            out.version         = qr.getVersion();
            out.size            = qr.getSize();
            out.mask            = qr.getMask();
            out.matrix.assign(static_cast<size_t>(out.size) * out.size, 0);
            for (int y = 0; y < out.size; ++y) {
                for (int x = 0; x < out.size; ++x) {
                    if (qr.getModule(x, y))
                        out.matrix[y * out.size + x] = 1;
                }
            }
            out.valid = true;
            return out;
        }

        std::string PercentEncode(const std::string& s) {
            static const char hex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(s.size());
            for (unsigned char c : s) {
                bool unreserved = (std::isalnum(c) ||
                                   c == '-' || c == '_' || c == '.' || c == '~');
                if (unreserved) {
                    out.push_back(static_cast<char>(c));
                } else {
                    out.push_back('%');
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                }
            }
            return out;
        }

        std::string WifiEscape(const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
                    out.push_back('\\');
                out.push_back(c);
            }
            return out;
        }
    }

    QRCodeData QRCodeUtils::GenerateQRCode(const std::string& text,
                                           QRErrorCorrection ecl) {
        QRGeneratorConfig cfg;
        cfg.errorCorrection = ecl;
        return GenerateQRCode(text, cfg);
    }

    QRCodeData QRCodeUtils::GenerateQRCode(const std::string& text,
                                           const QRGeneratorConfig& cfg) {
        QRCodeData out;
        out.content = text;
        out.mode    = cfg.encodingMode;
        if (text.empty()) {
            out.errorMessage = "content is empty";
            return out;
        }
        try {
            auto segs = BuildSegments(text, cfg.encodingMode);
            int minV = std::clamp(cfg.minVersion, 1, 40);
            int maxV = std::clamp(cfg.maxVersion, minV, 40);
            int msk  = (cfg.mask >= 0 && cfg.mask <= 7) ? cfg.mask : -1;

            qrcodegen::QrCode qr = qrcodegen::QrCode::encodeSegments(
                segs, ToNayukiEcl(cfg.errorCorrection),
                minV, maxV, msk, cfg.boostEcl);

            return ToQRCodeData(qr, text, cfg.encodingMode);
        } catch (const std::exception& e) {
            out.errorMessage = e.what();
            return out;
        }
    }

    bool QRCodeUtils::ExportToSVG(const QRCodeData& data,
                                  const std::string& filename,
                                  int moduleSize,
                                  const Color& foreground,
                                  const Color& background) {
        if (!data.valid || moduleSize <= 0) return false;

        const int quiet = 4;
        const int totalModules = data.size + 2 * quiet;
        const int px = totalModules * moduleSize;

        std::ofstream f(filename);
        if (!f) return false;

        f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        f << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
          << "viewBox=\"0 0 " << totalModules << " " << totalModules << "\" "
          << "width=\"" << px << "\" height=\"" << px << "\" "
          << "shape-rendering=\"crispEdges\">\n";
        f << "  <rect width=\"" << totalModules << "\" height=\"" << totalModules
          << "\" fill=\"" << background.ToHexString() << "\"/>\n";

        f << "  <path fill=\"" << foreground.ToHexString() << "\" d=\"";
        for (int y = 0; y < data.size; ++y) {
            int x = 0;
            while (x < data.size) {
                if (!data.IsDark(x, y)) { ++x; continue; }
                int runStart = x;
                while (x < data.size && data.IsDark(x, y)) ++x;
                int runLen = x - runStart;
                f << "M" << (runStart + quiet) << "," << (y + quiet)
                  << "h" << runLen << "v1h-" << runLen << "z";
            }
        }
        f << "\"/>\n";
        f << "</svg>\n";
        return f.good();
    }

    std::string QRCodeUtils::CreateURLContent(const std::string& url) {
        return url;
    }

    std::string QRCodeUtils::CreateEmailContent(const std::string& email,
                                                const std::string& subject,
                                                const std::string& body) {
        std::string out = "mailto:" + email;
        bool hasParam = false;
        if (!subject.empty()) {
            out += "?subject=" + PercentEncode(subject);
            hasParam = true;
        }
        if (!body.empty()) {
            out += (hasParam ? "&" : "?");
            out += "body=" + PercentEncode(body);
        }
        return out;
    }

    std::string QRCodeUtils::CreateSMSContent(const std::string& phone,
                                              const std::string& message) {
        std::string out = "SMSTO:" + phone;
        if (!message.empty()) out += ":" + message;
        return out;
    }

    std::string QRCodeUtils::CreateTelContent(const std::string& phone) {
        return "tel:" + phone;
    }

    std::string QRCodeUtils::CreateWiFiContent(const std::string& ssid,
                                               const std::string& password,
                                               const std::string& security,
                                               bool hidden) {
        std::string sec = security;
        for (auto& c : sec) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (sec != "WPA" && sec != "WEP" && sec != "NOPASS") sec = "WPA";

        std::string out = "WIFI:";
        out += "T:" + sec + ";";
        out += "S:" + WifiEscape(ssid) + ";";
        if (sec != "NOPASS") out += "P:" + WifiEscape(password) + ";";
        if (hidden)          out += "H:true;";
        out += ";";
        return out;
    }

    std::string QRCodeUtils::CreateVCardContent(const std::string& name,
                                                const std::string& phone,
                                                const std::string& email) {
        std::string out;
        out += "BEGIN:VCARD\r\n";
        out += "VERSION:3.0\r\n";
        out += "FN:" + name + "\r\n";
        if (!phone.empty()) out += "TEL:" + phone + "\r\n";
        if (!email.empty()) out += "EMAIL:" + email + "\r\n";
        out += "END:VCARD";
        return out;
    }

    std::string QRCodeUtils::CreateGeoContent(double latitude, double longitude) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "geo:%.6f,%.6f", latitude, longitude);
        return std::string(buf);
    }

    bool QRCodeUtils::IsNumeric(const std::string& text) {
        return !text.empty() && qrcodegen::QrSegment::isNumeric(text.c_str());
    }

    bool QRCodeUtils::IsAlphanumeric(const std::string& text) {
        return !text.empty() && qrcodegen::QrSegment::isAlphanumeric(text.c_str());
    }

    bool QRCodeUtils::IsDecoderAvailable() {
#ifdef ULTRACANVAS_QRCODE_HAS_DECODER
        return true;
#else
        return false;
#endif
    }

    std::vector<QRScanResult> QRCodeUtils::ScanQRCodeFile(const std::string& imagePath,
                                                          std::string* errorMessage) {
        std::vector<QRScanResult> results;
        auto setErr = [&](const std::string& s) {
            if (errorMessage) *errorMessage = s;
        };
#ifndef ULTRACANVAS_QRCODE_HAS_DECODER
        setErr("QR decoder support not built in (compile with libzbar)");
        return results;
#else
        using namespace zbar;

        // Load image with libvips
        vips::VImage img;
        try {
            img = vips::VImage::new_from_file(imagePath.c_str());
        } catch (const vips::VError& e) {
            setErr(std::string("Failed to open image: ") + e.what());
            return results;
        }

        int width = img.width();
        int height = img.height();
        int bands = img.bands();

        size_t dataSize = static_cast<size_t>(width) * height;
        std::vector<uint8_t> grayData(dataSize);

        if (bands >= 3) {
            // Manual BT.601 luminance from RGB channels (skip alpha band)
            VipsBandFormat fmt = img.format();
            if (fmt != VIPS_FORMAT_UCHAR)
                img = img.cast(VIPS_FORMAT_UCHAR);
            int stride = width * bands;
            const uint8_t* src = static_cast<const uint8_t*>(img.data());
            for (int y = 0; y < height; ++y) {
                const uint8_t* row = src + y * stride;
                for (int x = 0; x < width; ++x) {
                    int off = x * bands;
                    int r = row[off + 0];
                    int g = row[off + 1];
                    int b = row[off + 2];
                    grayData[y * width + x] = static_cast<uint8_t>(
                        (r * 77 + g * 150 + b * 29) >> 8);
                }
            }
        } else if (bands == 1) {
            if (img.format() != VIPS_FORMAT_UCHAR)
                img = img.cast(VIPS_FORMAT_UCHAR);
            memcpy(grayData.data(), img.data(), dataSize);
        } else {
            setErr("Unsupported image format (bands=" + std::to_string(bands) + ")");
            return results;
        }

        // Adaptive threshold: use average of all pixel values. QR codes are
        // roughly 50/50 black/white so the average should be ~128. We then
        // binarize so zbar gets clean black/white pixels.
        {
            unsigned long long sum = 0;
            for (size_t i = 0; i < dataSize; ++i) sum += grayData[i];
            int threshold = static_cast<int>(sum / dataSize);
            if (threshold < 64)  threshold = 128;
            if (threshold > 192) threshold = 128;
            for (size_t i = 0; i < dataSize; ++i)
                grayData[i] = (grayData[i] < threshold) ? 0 : 255;
        }

        // Create zbar scanner
        zbar_image_scanner_t* scanner = zbar_image_scanner_create();
        if (!scanner) {
            setErr("Failed to create zbar scanner");
            return results;
        }

        // Enable QR code symbology explicitly
        zbar_image_scanner_set_config(scanner, ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);

        // Create zbar image in Y800 (grayscale) format
        zbar_image_t* zimg = zbar_image_create();
        if (!zimg) {
            zbar_image_scanner_destroy(scanner);
            setErr("Failed to create zbar image");
            return results;
        }

        zbar_image_set_format(zimg, zbar_fourcc('Y', '8', '0', '0'));
        zbar_image_set_size(zimg, static_cast<unsigned>(width),
                            static_cast<unsigned>(height));
        zbar_image_set_data(zimg, grayData.data(), grayData.size(), nullptr);

        // Scan
        int nSymbols = zbar_scan_image(scanner, zimg);

        // Collect results from the scanner's symbol set
        if (nSymbols > 0) {
            const zbar_symbol_set_t* symset =
                zbar_image_scanner_get_results(scanner);
            if (symset) {
                const zbar_symbol_t* sym =
                    zbar_symbol_set_first_symbol(symset);
                while (sym) {
                    QRScanResult r;
                    r.type = zbar_get_symbol_name(
                        zbar_symbol_get_type(sym));
                    unsigned len = zbar_symbol_get_data_length(sym);
                    const char* data = zbar_symbol_get_data(sym);
                    if (data && len > 0)
                        r.data.assign(data, len);
                    unsigned nPts = zbar_symbol_get_loc_size(sym);
                    for (unsigned i = 0; i < nPts; ++i) {
                        r.polygon.push_back(Point2Df(
                            static_cast<float>(
                                zbar_symbol_get_loc_x(sym, i)),
                            static_cast<float>(
                                zbar_symbol_get_loc_y(sym, i))));
                    }
                    r.valid = true;
                    results.push_back(std::move(r));
                    sym = zbar_symbol_next(sym);
                }
            }
        } else if (nSymbols < 0) {
            setErr("zbar scan failed");
        }

        zbar_image_destroy(zimg);
        zbar_image_scanner_destroy(scanner);

        if (results.empty()) {
            setErr("No QR codes detected");
        }
        return results;
#endif
    }

    UltraCanvasQRCode::UltraCanvasQRCode(const std::string& identifier,
                                         int x, int y, int w, int h)
        : UltraCanvasUIElement(identifier, x, y, w, h) {
        backgroundColor = config_.style.backgroundColor;
    }

    void UltraCanvasQRCode::SetContent(const std::string& text) {
        if (text == content_) return;
        content_ = text;
        dirty_ = true;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetGeneratorConfig(const QRGeneratorConfig& cfg) {
        config_ = cfg;
        backgroundColor = config_.style.backgroundColor;
        dirty_ = true;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetStyleConfig(const QRStyleConfig& style) {
        config_.style = style;
        backgroundColor = style.backgroundColor;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetErrorCorrection(QRErrorCorrection level) {
        if (config_.errorCorrection == level) return;
        config_.errorCorrection = level;
        dirty_ = true;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetEncodingMode(QREncodingMode mode) {
        if (config_.encodingMode == mode) return;
        config_.encodingMode = mode;
        dirty_ = true;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetMinMaxVersion(int minVer, int maxVer) {
        minVer = std::clamp(minVer, 1, 40);
        maxVer = std::clamp(maxVer, minVer, 40);
        if (config_.minVersion == minVer && config_.maxVersion == maxVer) return;
        config_.minVersion = minVer;
        config_.maxVersion = maxVer;
        dirty_ = true;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetPointStyle(QRPointStyle s) {
        config_.style.pointStyle = s;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetFinderStyle(QRFinderStyle s) {
        config_.style.finderStyle = s;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetModuleRoundness(float r) {
        config_.style.moduleRoundness = std::clamp(r, 0.0f, 1.0f);
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetModuleScale(float s) {
        config_.style.moduleScale = std::clamp(s, 0.40f, 1.0f);
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetQuietZoneModules(int modules) {
        config_.style.quietZoneModules = std::max(0, modules);
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetForegroundColor(const Color& c) {
        config_.style.foregroundColor = c;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetBackgroundColor(const Color& c) {
        config_.style.backgroundColor = c;
        backgroundColor = c;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetFinderColor(const Color& c) {
        config_.style.finderColor = c;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetGradient(QRGradientType type,
                                        const Color& start, const Color& end) {
        config_.style.gradientType       = type;
        config_.style.gradientStartColor = start;
        config_.style.gradientEndColor   = end;
        RequestRedraw();
    }

    void UltraCanvasQRCode::ClearGradient() {
        config_.style.gradientType = QRGradientType::None;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetLogo(std::shared_ptr<UCImage> logo, float scale) {
        config_.style.logoImage = std::move(logo);
        config_.style.logoScale = std::clamp(scale, 0.0f, 0.40f);
        if (config_.style.logoStyle == QRLogoStyle::None)
            config_.style.logoStyle = QRLogoStyle::CenterWithBorder;
        RequestRedraw();
    }

    void UltraCanvasQRCode::SetLogoStyle(QRLogoStyle s) {
        config_.style.logoStyle = s;
        RequestRedraw();
    }

    void UltraCanvasQRCode::ClearLogo() {
        config_.style.logoImage.reset();
        config_.style.logoStyle = QRLogoStyle::None;
        RequestRedraw();
    }

    bool UltraCanvasQRCode::ExportToSVG(const std::string& filename, int moduleSize) {
        EnsureFresh();
        if (!data_.valid) return false;
        return QRCodeUtils::ExportToSVG(data_, filename, moduleSize,
                                        config_.style.foregroundColor,
                                        config_.style.backgroundColor);
    }

    bool UltraCanvasQRCode::ExportToImage(const std::string& filename,
                                          QRImageFormat format,
                                          int moduleSize,
                                          std::string* errorMessage) {
        auto setErr = [&](const std::string& s) {
            if (errorMessage) *errorMessage = s;
        };

        EnsureFresh();
        if (!data_.valid) { setErr("invalid QR data"); return false; }
        if (moduleSize <= 0) { setErr("moduleSize must be > 0"); return false; }

        const int quiet  = std::max(0, config_.style.quietZoneModules);
        const int totalM = data_.size + 2 * quiet;
        const int px     = totalM * moduleSize;

        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surface);
            setErr("cairo: failed to create surface");
            return false;
        }
        std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(px, px), nullptr);
        if (!ctx) {
            cairo_surface_destroy(surface);
            setErr("failed to create offscreen render context");
            return false;
        }
        cairo_t* nativeCr = static_cast<cairo_t*>(ctx->GetNativeContext());
        cairo_surface_destroy(surface);
        if (!nativeCr) {
            setErr("render context has no native handle");
            return false;
        }
        cairo_surface_t* ctxSurface = cairo_get_target(nativeCr);
        if (!ctxSurface ||
            cairo_image_surface_get_format(ctxSurface) != CAIRO_FORMAT_ARGB32) {
            setErr("offscreen surface format is not ARGB32");
            return false;
        }

        // Temporarily switch to scan-friendly style for export
        QRStyleConfig savedStyle = config_.style;
        config_.style.pointStyle      = QRPointStyle::Square;
        config_.style.moduleScale     = 1.0f;
        config_.style.moduleRoundness = 0.0f;
        config_.style.finderStyle     = QRFinderStyle::Standard;
        config_.style.gradientType    = QRGradientType::None;
        config_.style.foregroundColor = Colors::Black;
        config_.style.backgroundColor = Colors::White;

        const Rect2Di savedBounds = GetBounds();
        SetBounds(Rect2Di(0, 0, px, px));
        Render(ctx.get(), Rect2Di(0, 0, px, px));
        SetBounds(savedBounds);

        config_.style = savedStyle;

        cairo_surface_flush(ctxSurface);
        const int sw     = cairo_image_surface_get_width(ctxSurface);
        const int sh     = cairo_image_surface_get_height(ctxSurface);
        const int stride = cairo_image_surface_get_stride(ctxSurface);
        const unsigned char* src = cairo_image_surface_get_data(ctxSurface);
        if (!src) { setErr("cairo: surface has no data pointer"); return false; }

        std::vector<unsigned char> rgba(static_cast<size_t>(sw) * sh * 4);
        for (int y = 0; y < sh; ++y) {
            const unsigned char* sl = src + y * stride;
            unsigned char*       dl = rgba.data() + static_cast<size_t>(y) * sw * 4;
            for (int x = 0; x < sw; ++x) {
                unsigned char b = sl[4 * x + 0];
                unsigned char g = sl[4 * x + 1];
                unsigned char r = sl[4 * x + 2];
                unsigned char a = sl[4 * x + 3];
                if (a != 0 && a != 255) {
                    r = static_cast<unsigned char>(std::min(255, (r * 255 + a / 2) / a));
                    g = static_cast<unsigned char>(std::min(255, (g * 255 + a / 2) / a));
                    b = static_cast<unsigned char>(std::min(255, (b * 255 + a / 2) / a));
                }
                dl[4 * x + 0] = r;
                dl[4 * x + 1] = g;
                dl[4 * x + 2] = b;
                dl[4 * x + 3] = a;
            }
        }

        const char* fmtExt = nullptr;
        switch (format) {
            case QRImageFormat::PNG:  fmtExt = ".png";  break;
            case QRImageFormat::JPEG: fmtExt = ".jpg";  break;
            case QRImageFormat::QOI:  fmtExt = ".qoi";  break;
            case QRImageFormat::WebP: fmtExt = ".webp"; break;
            case QRImageFormat::AVIF: fmtExt = ".avif"; break;
        }
        std::string outPath = filename;
        {
            auto endsWith = [](const std::string& s, const char* suf) {
                size_t n = std::strlen(suf);
                if (s.size() < n) return false;
                for (size_t i = 0; i < n; ++i)
                    if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i]))
                        != std::tolower(static_cast<unsigned char>(suf[i])))
                        return false;
                return true;
            };
            if (format == QRImageFormat::JPEG &&
                (endsWith(outPath, ".jpg") || endsWith(outPath, ".jpeg"))) {
            } else if (!endsWith(outPath, fmtExt)) {
                outPath += fmtExt;
            }
        }

        try {
            if (VIPS_INIT("ultracanvas-qrcode") != 0) {
                setErr(vips_error_buffer());
                vips_error_clear();
                return false;
            }
            // `rgba` stays alive through write_to_file below, so the non-copying
            // new_from_memory is safe (new_from_memory_copy isn't in this libvips).
            vips::VImage img = vips::VImage::new_from_memory(
                rgba.data(),
                rgba.size(),
                sw, sh,
                4,
                VIPS_FORMAT_UCHAR);
            img.set("interpretation", static_cast<int>(VIPS_INTERPRETATION_sRGB));
            img.write_to_file(outPath.c_str());
            return true;
        } catch (const vips::VError& e) {
            setErr(std::string("vips: ") + e.what());
            return false;
        } catch (const std::exception& e) {
            setErr(e.what());
            return false;
        }
    }

    void UltraCanvasQRCode::Regenerate() {
        if (content_.empty()) {
            data_ = QRCodeData{};
            data_.errorMessage = "content is empty";
            return;
        }
        data_ = QRCodeUtils::GenerateQRCode(content_, config_);
    }

    void UltraCanvasQRCode::EnsureFresh() {
        if (dirty_) {
            Regenerate();
            dirty_ = false;
        }
    }

    Color UltraCanvasQRCode::ResolveFinderColor() const {
        if (config_.style.finderColor.a == 0)
            return config_.style.foregroundColor;
        return config_.style.finderColor;
    }

    void UltraCanvasQRCode::ApplyDataPaint(IRenderContext* ctx, const Rect2Df& qrRect) {
        const auto& st = config_.style;
        if (st.gradientType == QRGradientType::None) {
            ctx->SetFillPaint(st.foregroundColor);
            return;
        }
        std::vector<GradientStop> stops{
            GradientStop(0.0, st.gradientStartColor),
            GradientStop(1.0, st.gradientEndColor),
        };
        std::shared_ptr<IPaintPattern> pat;
        switch (st.gradientType) {
            case QRGradientType::Linear:
                pat = ctx->CreateLinearGradientPattern(
                    qrRect.x, qrRect.y + qrRect.height * 0.5f,
                    qrRect.x + qrRect.width, qrRect.y + qrRect.height * 0.5f,
                    stops);
                break;
            case QRGradientType::Diagonal:
                pat = ctx->CreateLinearGradientPattern(
                    qrRect.x, qrRect.y,
                    qrRect.x + qrRect.width, qrRect.y + qrRect.height,
                    stops);
                break;
            case QRGradientType::Radial: {
                float cx = qrRect.x + qrRect.width * 0.5f;
                float cy = qrRect.y + qrRect.height * 0.5f;
                float r  = qrRect.width * 0.6f;
                pat = ctx->CreateRadialGradientPattern(cx, cy, 0.0f, cx, cy, r, stops);
                break;
            }
            default: break;
        }
        if (pat) ctx->SetFillPaint(pat);
        else     ctx->SetFillPaint(st.foregroundColor);
    }

    void UltraCanvasQRCode::ApplyFinderPaint(IRenderContext* ctx) {
        ctx->SetFillPaint(ResolveFinderColor());
    }

    bool UltraCanvasQRCode::IsFinderCell(int x, int y, int size) {
        if (x < 7 && y < 7)              return true;
        if (x >= size - 7 && y < 7)      return true;
        if (x < 7 && y >= size - 7)      return true;
        return false;
    }

    void UltraCanvasQRCode::DrawModule(IRenderContext* ctx,
                                       const Rect2Df& cell,
                                       QRPointStyle style) const {
        const float scale = config_.style.moduleScale;
        const float cx = cell.x + cell.width  * 0.5f;
        const float cy = cell.y + cell.height * 0.5f;
        const float w  = cell.width  * scale;
        const float h  = cell.height * scale;
        const Rect2Df shrunk(cx - w * 0.5f, cy - h * 0.5f, w, h);

        switch (style) {
            case QRPointStyle::Square:
                ctx->FillRectangle(cell);
                return;

            case QRPointStyle::RoundedSquare: {
                float r = cell.width * 0.5f * config_.style.moduleRoundness;
                ctx->FillRoundedRectangle(cell, r);
                return;
            }

            case QRPointStyle::Circle:
                ctx->FillCircle(Point2Df(cx, cy), std::min(w, h) * 0.5f);
                return;

            case QRPointStyle::Diamond: {
                ctx->ClearPath();
                ctx->MoveTo(cx,        cy - h * 0.5f);
                ctx->LineTo(cx + w*0.5f, cy);
                ctx->LineTo(cx,        cy + h * 0.5f);
                ctx->LineTo(cx - w*0.5f, cy);
                ctx->ClosePath();
                ctx->Fill();
                return;
            }

            case QRPointStyle::Hexagon: {
                ctx->ClearPath();
                const float r = std::min(w, h) * 0.5f;
                for (int i = 0; i < 6; ++i) {
                    float a = static_cast<float>(i) * (2.0f * 3.14159265358979f / 6.0f);
                    float px = cx + r * std::cos(a);
                    float py = cy + r * std::sin(a);
                    if (i == 0) ctx->MoveTo(px, py);
                    else        ctx->LineTo(px, py);
                }
                ctx->ClosePath();
                ctx->Fill();
                return;
            }

            case QRPointStyle::VerticalBars: {
                float bw = cell.width * 0.65f;
                ctx->FillRectangle(Rect2Df(cx - bw * 0.5f, cell.y, bw, cell.height));
                return;
            }

            case QRPointStyle::HorizontalBars: {
                float bh = cell.height * 0.65f;
                ctx->FillRectangle(Rect2Df(cell.x, cy - bh * 0.5f, cell.width, bh));
                return;
            }

            case QRPointStyle::Dots:
                ctx->FillCircle(Point2Df(cx, cy), std::min(w, h) * 0.40f);
                return;
        }
    }

    void UltraCanvasQRCode::DrawFinder(IRenderContext* ctx,
                                       const Rect2Df& rect,
                                       QRFinderStyle style) const {
        const float u   = rect.width / 7.0f;
        const float bgX = rect.x + u;
        const float bgY = rect.y + u;
        const float bgW = rect.width - 2 * u;
        const float bgH = rect.height - 2 * u;
        const float ctrX = rect.x + 2 * u;
        const float ctrY = rect.y + 2 * u;
        const float ctrW = rect.width - 4 * u;
        const float ctrH = rect.height - 4 * u;
        const Color finderC = ResolveFinderColor();

        auto drawDiamond = [ctx](float x, float y, float w, float h) {
            float cx = x + w * 0.5f, cy = y + h * 0.5f;
            ctx->ClearPath();
            ctx->MoveTo(cx, y);
            ctx->LineTo(x + w, cy);
            ctx->LineTo(cx, y + h);
            ctx->LineTo(x, cy);
            ctx->ClosePath();
            ctx->Fill();
        };

        switch (style) {
            case QRFinderStyle::Standard:
                ctx->SetFillPaint(finderC);
                ctx->FillRectangle(rect);
                ctx->SetFillPaint(config_.style.backgroundColor);
                ctx->FillRectangle(Rect2Df(bgX, bgY, bgW, bgH));
                ctx->SetFillPaint(finderC);
                ctx->FillRectangle(Rect2Df(ctrX, ctrY, ctrW, ctrH));
                return;

            case QRFinderStyle::Rounded: {
                float rO = rect.width * 0.18f;
                float rI = bgW * 0.20f;
                float rC = ctrW * 0.25f;
                ctx->SetFillPaint(finderC);
                ctx->FillRoundedRectangle(rect, rO);
                ctx->SetFillPaint(config_.style.backgroundColor);
                ctx->FillRoundedRectangle(Rect2Df(bgX, bgY, bgW, bgH), rI);
                ctx->SetFillPaint(finderC);
                ctx->FillRoundedRectangle(Rect2Df(ctrX, ctrY, ctrW, ctrH), rC);
                return;
            }

            case QRFinderStyle::Circle: {
                Point2Df c(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f);
                ctx->SetFillPaint(finderC);
                ctx->FillCircle(c, rect.width * 0.5f);
                ctx->SetFillPaint(config_.style.backgroundColor);
                ctx->FillCircle(c, bgW * 0.5f);
                ctx->SetFillPaint(finderC);
                ctx->FillCircle(c, ctrW * 0.5f);
                return;
            }

            case QRFinderStyle::RoundedCircle: {
                Point2Df c(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f);
                ctx->SetFillPaint(finderC);
                ctx->FillRoundedRectangle(rect, rect.width * 0.30f);
                ctx->SetFillPaint(config_.style.backgroundColor);
                ctx->FillCircle(c, bgW * 0.5f);
                ctx->SetFillPaint(finderC);
                ctx->FillCircle(c, ctrW * 0.5f);
                return;
            }

            case QRFinderStyle::Diamond:
                ctx->SetFillPaint(finderC);
                drawDiamond(rect.x, rect.y, rect.width, rect.height);
                ctx->SetFillPaint(config_.style.backgroundColor);
                drawDiamond(bgX, bgY, bgW, bgH);
                ctx->SetFillPaint(finderC);
                drawDiamond(ctrX, ctrY, ctrW, ctrH);
                return;
        }
    }

    // Largest fraction of the QR *width* that the logo together with its
    // margin/border may occupy while the chosen error-correction level can
    // still reconstruct the obscured modules. The obscured region is a single
    // contiguous block in the centre (all data modules), so these limits are
    // kept conservatively below the theoretical Reed-Solomon recovery rates
    // (L 7%, M 15%, Q 25%, H 30%). The linear limits below correspond roughly
    // to a covered *area* of 9% / 16% / 25% / 30% of the symbol.
    static float MaxLogoCoverWidthFraction(QRErrorCorrection ecl) {
        switch (ecl) {
            case QRErrorCorrection::Low:      return 0.30f;
            case QRErrorCorrection::Medium:   return 0.40f;
            case QRErrorCorrection::Quartile: return 0.50f;
            case QRErrorCorrection::High:     return 0.55f;
        }
        return 0.40f;
    }

    void UltraCanvasQRCode::DrawLogo(IRenderContext* ctx, const Rect2Df& qrRect) const {
        const auto& st = config_.style;
        if (st.logoStyle == QRLogoStyle::None || !st.logoImage) return;

        // The logo and its margin hide QR modules in the centre. If they hide
        // more than the error correction can recover, the code stops scanning.
        // Clamp the *combined* footprint (logo + margin) to the safe budget for
        // the level that was actually encoded (data_.errorCorrection already
        // accounts for any boost applied during generation). The margin is
        // sacrificed first, then the logo itself, so the rendered code can
        // never be made unscannable from the UI controls.
        const float maxCover = qrRect.width *
                               MaxLogoCoverWidthFraction(data_.errorCorrection);

        float pad  = std::max(0.0f, st.logoBorderWidth);
        float side = qrRect.width * std::clamp(st.logoScale, 0.0f, 1.0f);

        if (side > maxCover) {            // logo alone overflows: shrink it, no margin
            side = maxCover;
            pad  = 0.0f;
        } else if (side + 2.0f * pad > maxCover) {   // trim the margin to fit
            pad = (maxCover - side) * 0.5f;
        }

        float logoX   = qrRect.x + (qrRect.width  - side) * 0.5f;
        float logoY   = qrRect.y + (qrRect.height - side) * 0.5f;
        Rect2Df logo(logoX, logoY, side, side);

        Rect2Df frame(logo.x - pad, logo.y - pad,
                      logo.width + 2 * pad, logo.height + 2 * pad);

        ctx->SetFillPaint(st.logoBorderColor);
        switch (st.logoStyle) {
            case QRLogoStyle::None:
                return;

            case QRLogoStyle::Center:
                ctx->FillRectangle(frame);
                ctx->DrawImage(*st.logoImage, logo, ImageFitMode::Contain);
                return;

            case QRLogoStyle::CenterWithBorder:
                ctx->FillRectangle(frame);
                ctx->DrawImage(*st.logoImage, logo, ImageFitMode::Contain);
                return;

            case QRLogoStyle::CenterRounded:
                ctx->FillRoundedRectangle(frame, st.logoCornerRadius);
                ctx->PushState();
                ctx->ClipRoundedRectangle(
                    logo,
                    st.logoCornerRadius, st.logoCornerRadius,
                    st.logoCornerRadius, st.logoCornerRadius);
                ctx->DrawImage(*st.logoImage, logo, ImageFitMode::Cover);
                ctx->PopState();
                return;

            case QRLogoStyle::CenterCircle: {
                Point2Df c(logo.x + logo.width * 0.5f, logo.y + logo.height * 0.5f);
                float rOuter = logo.width * 0.5f + pad;
                float rInner = logo.width * 0.5f;
                ctx->FillCircle(c, rOuter);
                ctx->PushState();
                ctx->ClearPath();
                ctx->Circle(c.x, c.y, rInner);
                ctx->ClipPath();
                ctx->DrawImage(*st.logoImage, logo, ImageFitMode::Cover);
                ctx->PopState();
                return;
            }
        }
    }

    void UltraCanvasQRCode::Render(IRenderContext* ctx, const Rect2Df& /*dirty*/) {
        EnsureFresh();

        Rect2Di localBnds = GetLocalBounds();
        Rect2Df boundsF(0.0f, 0.0f,
                        static_cast<float>(localBnds.width),
                        static_cast<float>(localBnds.height));

        ctx->SetFillPaint(config_.style.backgroundColor);
        ctx->FillRectangle(boundsF);

        if (!data_.valid) return;

        const auto& st = config_.style;
        const int quiet = std::max(0, st.quietZoneModules);
        const int totalModules = data_.size + 2 * quiet;
        if (totalModules <= 0) return;

        const float side       = std::min(boundsF.width, boundsF.height);
        const float moduleSize = side / static_cast<float>(totalModules);
        const float qrSize     = data_.size * moduleSize;
        const float originX    = (boundsF.width  - side) * 0.5f
                                 + quiet * moduleSize;
        const float originY    = (boundsF.height - side) * 0.5f
                                 + quiet * moduleSize;
        const Rect2Df qrRect(originX, originY, qrSize, qrSize);

        ApplyDataPaint(ctx, qrRect);
        for (int y = 0; y < data_.size; ++y) {
            for (int x = 0; x < data_.size; ++x) {
                if (!data_.IsDark(x, y))              continue;
                if (IsFinderCell(x, y, data_.size))   continue;
                Rect2Df cell(originX + x * moduleSize,
                             originY + y * moduleSize,
                             moduleSize, moduleSize);
                DrawModule(ctx, cell, st.pointStyle);
            }
        }

        const float fSide = 7.0f * moduleSize;
        Rect2Df tl(originX,                                originY,                                fSide, fSide);
        Rect2Df tr(originX + (data_.size - 7) * moduleSize, originY,                                fSide, fSide);
        Rect2Df bl(originX,                                originY + (data_.size - 7) * moduleSize, fSide, fSide);
        DrawFinder(ctx, tl, st.finderStyle);
        DrawFinder(ctx, tr, st.finderStyle);
        DrawFinder(ctx, bl, st.finderStyle);

        DrawLogo(ctx, qrRect);
    }

} // namespace UltraCanvas
