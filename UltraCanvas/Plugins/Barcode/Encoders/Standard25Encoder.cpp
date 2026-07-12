// Plugins/Barcode/Encoders/Standard25Encoder.cpp
// Standard 2 of 5 (Industrial 2 of 5 / Code 25) encoder.
//
// Each digit = 5 bars + 4 narrow inter-bar spaces. Bar widths follow the
// same 5-bit table as ITF (2 wide, 3 narrow). Inter-digit space is narrow.
// Start = W n W n N n (wide-bar narrow-space wide-bar narrow-space narrow-bar narrow-space).
// Stop  = W n N n W n (wide-bar narrow-space narrow-bar narrow-space wide-bar narrow-space).
// Wide:narrow ratio used: 2:1.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"

namespace UltraCanvas {

    namespace {
        constexpr const char* kWidths[10] = {
            "00110","10001","01001","11000","00101",
            "10100","01100","00011","10010","01010",
        };
        constexpr int NARROW = 1;
        constexpr int WIDE   = 2;

        void EmitDigit(BarcodePattern& out, char d) {
            const char* w = kWidths[d - '0'];
            for (int i = 0; i < 5; ++i) {
                BarcodeInternal::AppendRun(out, BarcodeModuleKind::Bar,
                                            w[i] == '1' ? WIDE : NARROW);
                if (i < 4) {
                    BarcodeInternal::AppendRun(out, BarcodeModuleKind::Space, NARROW);
                }
            }
        }

        int Mod10Check(const std::string& s) {
            int sum = 0;
            int len = (int)s.size();
            for (int i = 0; i < len; ++i) {
                int posFromRight = len - i;
                int w = (posFromRight % 2 == 0) ? 1 : 3;
                sum += (s[i] - '0') * w;
            }
            return (10 - (sum % 10)) % 10;
        }

        class Std25Encoder : public IBarcodeEncoder {
        public:
            explicit Std25Encoder(const BarcodeEncoderOptions& opt) : options(opt) {}
            BarcodeSymbology GetSymbology() const override { return BarcodeSymbology::Standard25; }
            const char* GetName() const override { return "Standard 2 of 5"; }
            BarcodePattern Encode(const std::string& data) const override {
                std::string clean;
                for (char c : data) if (c >= '0' && c <= '9') clean.push_back(c);
                if (clean.empty()) {
                    return BarcodeInternal::MakeError("Standard 2 of 5: digits only");
                }
                if (options.includeChecksum) clean.push_back('0' + Mod10Check(clean));

                BarcodePattern p;
                // Start: W n W n N n
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   WIDE);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   WIDE);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);

                for (size_t i = 0; i < clean.size(); ++i) {
                    EmitDigit(p, clean[i]);
                    if (i + 1 < clean.size()) {
                        BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
                    }
                }
                // Stop: n W n N n W n -> we ensure a separating space before stop.
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   WIDE);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   WIDE);

                if (options.emitHumanReadable) p.humanReadable = clean;
                return p;
            }
        private:
            BarcodeEncoderOptions options;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateStandard25Encoder(const BarcodeEncoderOptions& opt) {
        return std::make_unique<Std25Encoder>(opt);
    }

} // namespace UltraCanvas
