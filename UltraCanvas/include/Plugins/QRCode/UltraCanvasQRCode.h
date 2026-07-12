#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef None
#undef None
#endif

namespace UltraCanvas {

    enum class QRErrorCorrection {
        Low      = 0,
        Medium   = 1,
        Quartile = 2,
        High     = 3
    };

    enum class QREncodingMode {
        Auto = 0,
        Numeric,
        Alphanumeric,
        Byte
    };

    enum class QRPointStyle {
        Square = 0,
        RoundedSquare,
        Circle,
        Diamond,
        Hexagon,
        VerticalBars,
        HorizontalBars,
        Dots
    };

    enum class QRFinderStyle {
        Standard = 0,
        Rounded,
        Circle,
        RoundedCircle,
        Diamond
    };

    enum class QRLogoStyle {
        None = 0,
        Center,
        CenterWithBorder,
        CenterRounded,
        CenterCircle
    };

    enum class QRGradientType {
        None = 0,
        Linear,
        Radial,
        Diagonal
    };

    struct QRStyleConfig {
        QRPointStyle  pointStyle    = QRPointStyle::Square;
        QRFinderStyle finderStyle   = QRFinderStyle::Standard;
        float moduleRoundness = 0.35f;
        float moduleScale     = 0.92f;

        Color foregroundColor = Colors::Black;
        Color backgroundColor = Colors::White;
        Color finderColor     = Color(0, 0, 0, 0);

        QRGradientType gradientType       = QRGradientType::None;
        Color          gradientStartColor = Colors::Black;
        Color          gradientEndColor   = Color(0, 102, 204);

        QRLogoStyle              logoStyle        = QRLogoStyle::None;
        std::shared_ptr<UCImage> logoImage;
        float                    logoScale        = 0.20f;
        float                    logoBorderWidth  = 4.0f;
        float                    logoCornerRadius = 6.0f;
        Color                    logoBorderColor  = Colors::White;

        int quietZoneModules = 4;
    };

    struct QRGeneratorConfig {
        QRErrorCorrection errorCorrection = QRErrorCorrection::Medium;
        QREncodingMode    encodingMode    = QREncodingMode::Auto;
        int               minVersion      = 1;
        int               maxVersion      = 40;
        int               mask            = -1;
        bool              boostEcl        = true;
        QRStyleConfig     style;
    };

    struct QRCodeData {
        std::string                content;
        QREncodingMode             mode             = QREncodingMode::Auto;
        QRErrorCorrection          errorCorrection  = QRErrorCorrection::Medium;
        int                        version          = 0;
        int                        size             = 0;
        int                        mask             = -1;
        bool                       valid            = false;
        std::string                errorMessage;
        std::vector<std::uint8_t>  matrix;

        bool IsDark(int x, int y) const {
            if (x < 0 || y < 0 || x >= size || y >= size) return false;
            return matrix[y * size + x] != 0;
        }
    };

    struct QRScanResult {
        std::string         data;
        std::string         type;
        std::vector<Point2Df> polygon;
        bool                valid = false;
    };

    enum class QRImageFormat {
        PNG = 0,
        JPEG,
        QOI,
        WebP,
        AVIF
    };

    namespace QRCodeUtils {

        QRCodeData GenerateQRCode(const std::string& text,
                                  QRErrorCorrection ecl = QRErrorCorrection::Medium);

        QRCodeData GenerateQRCode(const std::string& text,
                                  const QRGeneratorConfig& config);

        bool ExportToSVG(const QRCodeData& data,
                         const std::string& filename,
                         int moduleSize = 10,
                         const Color& foreground = Colors::Black,
                         const Color& background = Colors::White);

        std::vector<QRScanResult> ScanQRCodeFile(const std::string& imagePath,
                                                 std::string* errorMessage = nullptr);

        bool IsDecoderAvailable();

        std::string CreateURLContent(const std::string& url);
        std::string CreateEmailContent(const std::string& email,
                                       const std::string& subject = "",
                                       const std::string& body    = "");
        std::string CreateSMSContent(const std::string& phone,
                                     const std::string& message = "");
        std::string CreateTelContent(const std::string& phone);
        std::string CreateWiFiContent(const std::string& ssid,
                                      const std::string& password,
                                      const std::string& security = "WPA",
                                      bool hidden = false);
        std::string CreateVCardContent(const std::string& name,
                                       const std::string& phone,
                                       const std::string& email = "");
        std::string CreateGeoContent(double latitude, double longitude);

        bool IsNumeric(const std::string& text);
        bool IsAlphanumeric(const std::string& text);
    }

    class UltraCanvasQRCode : public UltraCanvasUIElement {
    public:
        UltraCanvasQRCode(const std::string& identifier = "QRCode",
                          int x = 0, int y = 0, int w = 200, int h = 200);
        ~UltraCanvasQRCode() override = default;

        void SetContent(const std::string& text);
        const std::string& GetContent() const { return content_; }

        void SetGeneratorConfig(const QRGeneratorConfig& cfg);
        const QRGeneratorConfig& GetGeneratorConfig() const { return config_; }

        void SetStyleConfig(const QRStyleConfig& style);
        const QRStyleConfig& GetStyleConfig() const { return config_.style; }

        void SetErrorCorrection(QRErrorCorrection level);
        void SetEncodingMode(QREncodingMode mode);
        void SetMinMaxVersion(int minVer, int maxVer);

        void SetPointStyle(QRPointStyle s);
        void SetFinderStyle(QRFinderStyle s);
        void SetModuleRoundness(float r);
        void SetModuleScale(float s);
        void SetQuietZoneModules(int modules);

        void SetForegroundColor(const Color& c);
        void SetBackgroundColor(const Color& c);
        void SetFinderColor(const Color& c);
        void SetGradient(QRGradientType type, const Color& start, const Color& end);
        void ClearGradient();

        void SetLogo(std::shared_ptr<UCImage> logo, float scale = 0.20f);
        void SetLogoStyle(QRLogoStyle s);
        void ClearLogo();

        const QRCodeData& GetQRData() const { return data_; }
        bool IsValid() const { return data_.valid; }
        int  GetVersion() const { return data_.version; }
        int  GetModuleCount() const { return data_.size; }
        const std::string& GetErrorMessage() const { return data_.errorMessage; }

        bool ExportToSVG(const std::string& filename, int moduleSize = 10);
        bool ExportToImage(const std::string& filename,
                           QRImageFormat format,
                           int moduleSize = 10,
                           std::string* errorMessage = nullptr);

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        void Regenerate();
        void EnsureFresh();

        void   ApplyDataPaint(IRenderContext* ctx, const Rect2Df& qrRect);
        void   ApplyFinderPaint(IRenderContext* ctx);
        Color  ResolveFinderColor() const;

        void   DrawModule(IRenderContext* ctx,
                          const Rect2Df& cell,
                          QRPointStyle style) const;
        void   DrawFinder(IRenderContext* ctx,
                          const Rect2Df& rect,
                          QRFinderStyle style) const;
        void   DrawLogo(IRenderContext* ctx, const Rect2Df& qrRect) const;

        static bool IsFinderCell(int x, int y, int size);

    private:
        std::string        content_;
        QRGeneratorConfig  config_;
        QRCodeData         data_;
        bool               dirty_ = true;
    };

} // namespace UltraCanvas
