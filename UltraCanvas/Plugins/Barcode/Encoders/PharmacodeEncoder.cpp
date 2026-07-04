// Plugins/Barcode/Encoders/PharmacodeEncoder.cpp
// Pharmacode (Laetus) one-track encoder.
//
// Encodes integers 3..131070 as a sequence of narrow + wide bars separated
// by narrow spaces. Algorithm: while n > 0, if n is odd append "wide",
// n=(n-1)/2; else append "narrow", n=(n-2)/2. Reverse the sequence (we
// built LSB first) and emit, inserting a 1-unit space between each pair of
// adjacent bars. Conventional ratios: narrow bar = 1 unit, wide bar = 3
// units, inter-bar space = 1 unit. No human-readable text in the standard.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"
#include <string>

namespace UltraCanvas {

    namespace {
        constexpr int NARROW_BAR = 1;
        constexpr int WIDE_BAR   = 3;
        constexpr int SPACE      = 1;

        class PharmacodeEncoder : public IBarcodeEncoder {
        public:
            explicit PharmacodeEncoder(const BarcodeEncoderOptions& opt) : options(opt) {}
            BarcodeSymbology GetSymbology() const override { return BarcodeSymbology::Pharmacode; }
            const char* GetName() const override { return "Pharmacode"; }

            BarcodePattern Encode(const std::string& data) const override {
                // Parse to integer; accept digit-only strings.
                std::string clean;
                for (char c : data) if (c >= '0' && c <= '9') clean.push_back(c);
                if (clean.empty()) {
                    return BarcodeInternal::MakeError("Pharmacode: numeric input required");
                }
                // Parse with overflow detection (cap at 200000 just in case).
                long long n = 0;
                for (char c : clean) {
                    n = n * 10 + (c - '0');
                    if (n > 200000) return BarcodeInternal::MakeError("Pharmacode: value too large");
                }
                if (n < 3 || n > 131070) {
                    return BarcodeInternal::MakeError("Pharmacode: value must be 3..131070");
                }

                std::string seq; // 'W' = wide, 'N' = narrow, LSB first.
                while (n > 0) {
                    if (n & 1) { seq.push_back('W'); n = (n - 1) / 2; }
                    else        { seq.push_back('N'); n = (n - 2) / 2; }
                }
                // Reverse to print MSB first.
                std::string ordered(seq.rbegin(), seq.rend());

                BarcodePattern p;
                for (size_t i = 0; i < ordered.size(); ++i) {
                    int w = (ordered[i] == 'W') ? WIDE_BAR : NARROW_BAR;
                    BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar, w);
                    if (i + 1 < ordered.size()) {
                        BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, SPACE);
                    }
                }
                // Pharmacode is conventionally drawn without HRI.
                if (options.emitHumanReadable) p.humanReadable = clean;
                return p;
            }
        private:
            BarcodeEncoderOptions options;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreatePharmacodeEncoder(const BarcodeEncoderOptions& opt) {
        return std::make_unique<PharmacodeEncoder>(opt);
    }

} // namespace UltraCanvas
