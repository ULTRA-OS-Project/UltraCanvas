// Plugins/Barcode/Encoders/MSIEncoder.cpp
// MSI Plessey encoder. Numeric only.
//
// Each digit = 8 elements (4 bars + 4 spaces), pattern table sourced from
// Zint (BSD-3). Bit encoding: each of the 4 bits per digit (MSB first) is
// represented by a bar/space pair — a "0" bit is narrow-bar + wide-space
// ("12"), a "1" bit is wide-bar + narrow-space ("21"). Start = "21", Stop =
// "121". Wide:narrow ratio = 2:1.
//
// Check-digit variants supported: Mod 10 (IBM/Luhn-like), Mod 11 (IBM
// weights), Mod 10+Mod 10, Mod 11+Mod 10.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"
#include <string>

namespace UltraCanvas {

    namespace {
        constexpr const char* kMSI[10] = {
            "12121212","12121221","12122112","12122121","12211212",
            "12211221","12212112","12212121","21121212","21121221",
        };

        int MSIMod10(const std::string& s) {
            // Concatenate alternate digits starting from the rightmost,
            // multiply by 2, sum digits of result, add the non-selected
            // digits, then take (10 - sum%10) % 10.
            std::string odd, even; // odd = selected (rightmost first), even = not selected
            for (int i = (int)s.size() - 1, k = 0; i >= 0; --i, ++k) {
                if (k % 2 == 0) odd.push_back(s[i]); else even.push_back(s[i]);
            }
            // odd is now the rightmost selected digits in order from right.
            // We need to form the number by reading them left-to-right of the
            // original — which means reversing 'odd'.
            std::string num(odd.rbegin(), odd.rend());
            // Multiply num (string) by 2.
            int carry = 0;
            std::string mul;
            for (int i = (int)num.size() - 1; i >= 0; --i) {
                int v = (num[i] - '0') * 2 + carry;
                mul.push_back('0' + (v % 10));
                carry = v / 10;
            }
            if (carry) mul.push_back('0' + carry);
            // Sum the digits of mul.
            int sum = 0;
            for (char c : mul) sum += c - '0';
            // Add even-positioned digits.
            for (char c : even) sum += c - '0';
            return (10 - (sum % 10)) % 10;
        }

        int MSIMod11(const std::string& s) {
            // IBM weights: 2,3,4,5,6,7 cycling from rightmost.
            int sum = 0, w = 2;
            for (int i = (int)s.size() - 1; i >= 0; --i) {
                sum += (s[i] - '0') * w;
                w = (w == 7) ? 2 : (w + 1);
            }
            int chk = (11 - (sum % 11)) % 11;
            return (chk >= 10) ? 0 : chk; // convention: collapse 10/11 to 0
        }

        class MSIEncoder : public IBarcodeEncoder {
        public:
            explicit MSIEncoder(const BarcodeEncoderOptions& opt) : options(opt) {}
            BarcodeSymbology GetSymbology() const override { return BarcodeSymbology::MSIPlessey; }
            const char* GetName() const override { return "MSI Plessey"; }

            BarcodePattern Encode(const std::string& data) const override {
                std::string clean;
                for (char c : data) if (c >= '0' && c <= '9') clean.push_back(c);
                if (clean.empty()) return BarcodeInternal::MakeError("MSI: digits only");

                switch (options.msiCheckDigit) {
                    case MSICheckDigit::NoCheck: break;
                    case MSICheckDigit::Mod10:
                        clean.push_back('0' + MSIMod10(clean));
                        break;
                    case MSICheckDigit::Mod11:
                        clean.push_back('0' + MSIMod11(clean));
                        break;
                    case MSICheckDigit::Mod10Mod10: {
                        clean.push_back('0' + MSIMod10(clean));
                        clean.push_back('0' + MSIMod10(clean));
                        break;
                    }
                    case MSICheckDigit::Mod11Mod10: {
                        clean.push_back('0' + MSIMod11(clean));
                        clean.push_back('0' + MSIMod10(clean));
                        break;
                    }
                }

                BarcodePattern p;
                // Start: 21 (W bar, n space)
                BarcodeInternal::AppendWidths(p, "21", BarcodeModuleKind::Bar);
                for (char c : clean) {
                    BarcodeInternal::AppendWidths(p, kMSI[c - '0'], BarcodeModuleKind::Bar);
                }
                // Stop: 121 (n bar, W space, n bar)
                BarcodeInternal::AppendWidths(p, "121", BarcodeModuleKind::Bar);

                if (options.emitHumanReadable) p.humanReadable = clean;
                return p;
            }
        private:
            BarcodeEncoderOptions options;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateMSIEncoder(const BarcodeEncoderOptions& opt) {
        return std::make_unique<MSIEncoder>(opt);
    }

} // namespace UltraCanvas
