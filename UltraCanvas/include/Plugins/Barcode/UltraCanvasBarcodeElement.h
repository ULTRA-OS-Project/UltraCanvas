// include/Plugins/Barcode/UltraCanvasBarcodeElement.h
// UltraCanvas barcode widget — encodes data to a chosen 1D symbology and
// renders bars + human-readable interpretation via the existing Cairo
// render context. Encoders are pure-C++20, no external dependencies.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#pragma once

#include "Plugins/Barcode/UltraCanvasBarcodeEncoders.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    // ===== HRI POSITION =====
    // Suffixed because X11/Xlib.h defines Above/Below as preprocessor macros.
    enum class BarcodeTextPosition {
        HiddenText,
        TextBelow,
        TextAbove,
        TextBothSides,
    };

    // ===== ROTATION =====
    // The renderer composes the bars in normal orientation and rotates the
    // whole thing as a final step.
    enum class BarcodeRotation {
        Rotate0,
        Rotate90,
        Rotate180,
        Rotate270,
    };

    // ===== STYLE BLOCK =====
    struct BarcodeStyle {
        // Geometry
        float moduleWidth = 2.0f;        // X-dimension in logical pixels
        float barHeight = 60.0f;         // Height of the bar zone
        float quietZoneModules = 10.0f;  // Whitespace on each side, in modules
        float guardBarExtraHeight = 5.0f;// EAN/UPC guard bars extend by this
        float bearerBarThickness = 4.0f; // ITF-14 bearer bars

        // Colors
        Color foreground = Colors::Black;
        Color background = Colors::White;
        Color errorColor = Color(180, 30, 30, 255);

        // Human-readable text
        BarcodeTextPosition textPosition = BarcodeTextPosition::TextBelow;
        float textFontSize = 12.0f;
        float textPadding = 4.0f;        // Gap between bars and text
        std::string textFontFamily = ""; // Empty = current default
        FontWeight textFontWeight = FontWeight::Normal;
        Color textColor = Colors::Black;
        bool textMonospaceLayout = true; // Spread digits under their module group

        // Layout
        BarcodeRotation rotation = BarcodeRotation::Rotate0;
        bool autoFitToBounds = true;     // Auto-tune moduleWidth/barHeight to bounds
        bool maintainAspect = true;      // Keep printable aspect when fitting

        static BarcodeStyle DefaultStyle() { return BarcodeStyle{}; }
        static BarcodeStyle CompactStyle();
        static BarcodeStyle PrintStyle();   // 0.33mm @ 300dpi-ish defaults
        static BarcodeStyle MonochromeStyle();
    };

    // ===== BARCODE WIDGET =====
    class UltraCanvasBarcodeElement : public UltraCanvasUIElement {
    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasBarcodeElement(const std::string& identifier,
                                  long x, long y, long w, long h);

        explicit UltraCanvasBarcodeElement(const std::string& identifier = "Barcode",
                                           long w = 240, long h = 100);

        ~UltraCanvasBarcodeElement() override = default;

        // ===== DATA & SYMBOLOGY =====
        void SetSymbology(BarcodeSymbology symbology);
        BarcodeSymbology GetSymbology() const { return symbology; }

        void SetData(const std::string& data);
        const std::string& GetData() const { return data; }

        void SetEncoderOptions(const BarcodeEncoderOptions& options);
        const BarcodeEncoderOptions& GetEncoderOptions() const { return options; }

        // Convenience: equivalent to SetSymbology + SetData.
        void Set(BarcodeSymbology sym, const std::string& d);

        // ===== STYLE =====
        void SetStyle(const BarcodeStyle& style);
        const BarcodeStyle& GetStyle() const { return style; }

        void SetModuleWidth(float w);
        void SetBarHeight(float h);
        void SetQuietZoneModules(float modules);
        void SetForegroundColor(const Color& c);
        void SetBackgroundColor(const Color& c);
        void SetTextPosition(BarcodeTextPosition pos);
        void SetTextFontSize(float size);
        void SetTextFontFamily(const std::string& family);
        void SetTextColor(const Color& c);
        void SetRotation(BarcodeRotation r);
        void SetAutoFitToBounds(bool autoFit);

        // ===== STATUS =====
        bool IsValid() const { return pattern.valid; }
        const std::string& GetError() const { return pattern.errorMessage; }
        const BarcodePattern& GetPattern() const { return pattern; }

        // Re-encode now (data + symbology + options). Normally automatic.
        void Reencode();

        // ===== PREFERRED SIZE =====
        // Returns the natural size needed to render at the configured
        // moduleWidth + barHeight (no scaling). Useful for layout planning.
        int GetPreferredWidth() override;
        int GetPreferredHeight() override;

        Size2Di GetNaturalSize() const;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;

        // ===== EVENTS =====
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(const std::string& /*error*/)> onError;
        std::function<void()> onEncoded;
        std::function<void()> onClick;

    protected:
        // ===== STATE =====
        BarcodeSymbology symbology = BarcodeSymbology::Code128;
        std::string data;
        BarcodeEncoderOptions options;
        BarcodeStyle style;

        BarcodePattern pattern;
        bool encodeDirty = true;

        // ===== INTERNAL =====
        void EnsureEncoded();

        // Final geometry after auto-fit, in element-local coordinates.
        struct LayoutMetrics {
            float moduleWidth = 1.0f;
            float barHeight = 0.0f;
            float guardExtra = 0.0f;
            float bearerThickness = 0.0f;
            float quietPx = 0.0f;
            float textHeightAbove = 0.0f;
            float textHeightBelow = 0.0f;
            float originX = 0.0f;        // Left edge of left quiet zone
            float originY = 0.0f;        // Top edge of "above" text (or bars)
            float barsTopY = 0.0f;
            float totalWidth = 0.0f;
            float totalHeight = 0.0f;
        };

        LayoutMetrics ComputeMetrics(IRenderContext* ctx, float availW, float availH) const;

        void RenderUnrotated(IRenderContext* ctx, const LayoutMetrics& m);
        void RenderError(IRenderContext* ctx, const Rect2Di& local);
    };

    // ===== FACTORIES =====
    std::shared_ptr<UltraCanvasBarcodeElement> CreateBarcode(
        const std::string& identifier, long x, long y, long w, long h,
        BarcodeSymbology symbology, const std::string& data);

    std::shared_ptr<UltraCanvasBarcodeElement> CreateBarcode(
        const std::string& identifier, long w, long h,
        BarcodeSymbology symbology, const std::string& data);

    // ===== BUILDER =====
    class BarcodeBuilder {
    public:
        BarcodeBuilder(const std::string& identifier, long x, long y, long w = 240, long h = 100);

        BarcodeBuilder& SetSymbology(BarcodeSymbology s);
        BarcodeBuilder& SetData(const std::string& d);
        BarcodeBuilder& SetStyle(const BarcodeStyle& s);
        BarcodeBuilder& SetModuleWidth(float w);
        BarcodeBuilder& SetBarHeight(float h);
        BarcodeBuilder& SetQuietZoneModules(float m);
        BarcodeBuilder& SetForegroundColor(const Color& c);
        BarcodeBuilder& SetBackgroundColor(const Color& c);
        BarcodeBuilder& SetTextPosition(BarcodeTextPosition p);
        BarcodeBuilder& SetTextFontSize(float s);
        BarcodeBuilder& SetTextFontFamily(const std::string& f);
        BarcodeBuilder& SetRotation(BarcodeRotation r);
        BarcodeBuilder& SetAutoFitToBounds(bool b);
        BarcodeBuilder& SetIncludeChecksum(bool b);
        BarcodeBuilder& SetMSICheckDigit(MSICheckDigit m);
        BarcodeBuilder& OnError(std::function<void(const std::string&)> cb);
        BarcodeBuilder& OnEncoded(std::function<void()> cb);
        BarcodeBuilder& OnClick(std::function<void()> cb);

        std::shared_ptr<UltraCanvasBarcodeElement> Build() { return widget; }

    private:
        std::shared_ptr<UltraCanvasBarcodeElement> widget;
    };

    inline BarcodeBuilder CreateBarcodeBuilder(const std::string& identifier,
                                                long x, long y, long w = 240, long h = 100) {
        return BarcodeBuilder(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
