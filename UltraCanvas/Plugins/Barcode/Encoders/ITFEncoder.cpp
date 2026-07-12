// Plugins/Barcode/Encoders/ITFEncoder.cpp
// Interleaved 2 of 5 (ITF) + ITF-14 encoder.
//
// Each digit uses a 5-bit width pattern (2 wide, 3 narrow). Pairs of digits
// are interleaved: digit-1 supplies 5 bar widths, digit-2 supplies 5 space
// widths. Start = nnnn (2 narrow bar/space pairs). Stop = Wnn (wide bar +
// narrow space + narrow bar). Wide:narrow ratio used: 2:1.
//
// ITF-14 is the same encoding for exactly 14 digits, plus a "bearer bar"
// frame around the symbol (drawn by the widget when pattern.drawBearerBars
// is set).
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"

namespace UltraCanvas {

    namespace {
        // '0' = narrow, '1' = wide. 5 widths per digit.
        constexpr const char* kITF[10] = {
            "00110","10001","01001","11000","00101",
            "10100","01100","00011","10010","01010",
        };

        constexpr int NARROW = 1;
        constexpr int WIDE   = 2;

        int ComputeITFCheck(const std::string& digits) {
            // Same algorithm as EAN: alternating 3 and 1 weights from right.
            int len = (int)digits.size();
            int sum = 0;
            for (int i = 0; i < len; ++i) {
                int d = digits[i] - '0';
                int posFromRight = len - i;
                int w = (posFromRight % 2 == 0) ? 1 : 3;
                sum += d * w;
            }
            return (10 - (sum % 10)) % 10;
        }

        void EmitPair(BarcodePattern& out, char d1, char d2) {
            const char* bars   = kITF[d1 - '0'];
            const char* spaces = kITF[d2 - '0'];
            for (int i = 0; i < 5; ++i) {
                int barW = (bars[i] == '1') ? WIDE : NARROW;
                int spW  = (spaces[i] == '1') ? WIDE : NARROW;
                BarcodeInternal::AppendRun(out, BarcodeModuleKind::Bar, barW);
                BarcodeInternal::AppendRun(out, BarcodeModuleKind::Space, spW);
            }
        }

        BarcodePattern EncodeImpl(std::string digits, bool isITF14,
                                   const BarcodeEncoderOptions& options) {
            // Strip whitespace and dashes.
            std::string clean;
            for (char c : digits) if (c >= '0' && c <= '9') clean.push_back(c);
            digits = clean;

            if (!BarcodeInternal::IsAllDigits(digits) || digits.empty()) {
                return BarcodeInternal::MakeError("ITF: digits only");
            }

            if (isITF14) {
                if (digits.size() == 13) {
                    digits.push_back('0' + ComputeITFCheck(digits));
                }
                if (digits.size() != 14) {
                    return BarcodeInternal::MakeError("ITF-14: need 13 or 14 digits");
                }
                int chk = ComputeITFCheck(digits.substr(0, 13));
                if (digits[13] != ('0' + chk)) {
                    return BarcodeInternal::MakeError("ITF-14: check digit mismatch");
                }
            } else {
                if (options.includeChecksum) {
                    // Build with check digit appended; ensure even total length.
                    if ((digits.size() % 2) == 0) {
                        // Odd data length is required so total with check is even.
                        // Prepend a 0.
                        digits = "0" + digits;
                    }
                    digits.push_back('0' + ComputeITFCheck(digits));
                } else if ((digits.size() % 2) != 0) {
                    // Pad to even length with leading zero (standard practice).
                    digits = "0" + digits;
                }
            }

            BarcodePattern p;
            // Start: narrow-bar narrow-space narrow-bar narrow-space.
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   NARROW);
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   NARROW);
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);

            // Data pairs.
            for (size_t i = 0; i + 1 < digits.size(); i += 2) {
                EmitPair(p, digits[i], digits[i + 1]);
            }

            // Stop: wide-bar narrow-space narrow-bar.
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   WIDE);
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, NARROW);
            BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar,   NARROW);

            p.humanReadable = digits;
            p.drawBearerBars = isITF14;
            return p;
        }

        class ITFEncoder : public IBarcodeEncoder {
        public:
            ITFEncoder(const BarcodeEncoderOptions& opt, BarcodeSymbology m)
                : options(opt), mode(m) {}
            BarcodeSymbology GetSymbology() const override { return mode; }
            const char* GetName() const override {
                return mode == BarcodeSymbology::ITF14 ? "ITF-14" : "ITF";
            }
            BarcodePattern Encode(const std::string& data) const override {
                return EncodeImpl(data, mode == BarcodeSymbology::ITF14, options);
            }
        private:
            BarcodeEncoderOptions options;
            BarcodeSymbology mode;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateITFEncoder(const BarcodeEncoderOptions& opt,
                                                      BarcodeSymbology mode) {
        return std::make_unique<ITFEncoder>(opt, mode);
    }

} // namespace UltraCanvas
