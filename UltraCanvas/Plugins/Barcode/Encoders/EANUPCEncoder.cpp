// Plugins/Barcode/Encoders/EANUPCEncoder.cpp
// EAN-13, EAN-8, UPC-A, UPC-E and ISBN-13 encoder.
// Spec: ISO/IEC 15420 (EAN/UPC).
//
// EAN-13: 95 modules total (3 + 6*7 + 5 + 6*7 + 3). The leading digit is
// not drawn as bars — it's implied by the L/G parity pattern of the first
// six digits.
// EAN-8: 67 modules (3 + 4*7 + 5 + 4*7 + 3). All digits are drawn directly.
// UPC-A: 95 modules; an EAN-13 with implicit leading 0, but the first and
// last digits sit outside the guard bars in the HRI.
// UPC-E: 51 modules (3 + 6*7 + 6). 6 data digits between guards encode a
// compressed 12-digit GTIN, with parity from the number system + check.
// ISBN-13: an EAN-13 with 978/979 prefix, HRI shows formatted ISBN above.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"
#include <array>
#include <cstring>

namespace UltraCanvas {

    namespace {
        // 7-module patterns. L = odd parity, G = even parity (reflected R),
        // R = even-numbered (right-hand) pattern.
        constexpr const char* kL[10] = {
            "0001101","0011001","0010011","0111101","0100011",
            "0110001","0101111","0111011","0110111","0001011",
        };
        constexpr const char* kG[10] = {
            "0100111","0110011","0011011","0100001","0011101",
            "0111001","0000101","0010001","0001001","0010111",
        };
        constexpr const char* kR[10] = {
            "1110010","1100110","1101100","1000010","1011100",
            "1001110","1010000","1000100","1001000","1110100",
        };

        // EAN-13 first-digit -> 6-char L/G pattern for the left half.
        // 'L' = use kL,  'G' = use kG.
        constexpr const char* kEAN13Parity[10] = {
            "LLLLLL", // 0
            "LLGLGG",
            "LLGGLG",
            "LLGGGL",
            "LGLLGG",
            "LGGLLG",
            "LGGGLL",
            "LGLGLG",
            "LGLGGL",
            "LGGLGL", // 9
        };

        // UPC-E parity tables for number system digit 0.
        // Each char selects E (even/G) or O (odd/L) for the corresponding digit.
        constexpr const char* kUPCEParityNS0[10] = {
            "EEEOOO", // check = 0
            "EEOEOO",
            "EEOOEO",
            "EEOOOE",
            "EOEEOO",
            "EOOEEO",
            "EOOOEE",
            "EOEOEO",
            "EOEOOE",
            "EOOEOE", // 9
        };

        int ComputeEANCheck(const std::string& digits, int len) {
            // Weighted Mod 10: rightmost weight is 3, then 1, alternating.
            // For EAN-13 we compute over the first 12 digits to produce the 13th.
            // For EAN-8: first 7 digits -> 8th.
            // For UPC-A: first 11 digits -> 12th (same as EAN-13 with leading 0).
            int sum = 0;
            for (int i = 0; i < len; ++i) {
                int d = digits[i] - '0';
                // From the right (excluding check pos): odd indices weight 3.
                // i (0-based) and length len: pos-from-right = (len - i)
                int posFromRight = len - i;
                int w = (posFromRight % 2 == 0) ? 1 : 3;
                sum += d * w;
            }
            return (10 - (sum % 10)) % 10;
        }

        void AppendGuard(BarcodePattern& out, const char* pattern) {
            // Append a guard pattern, marking bars as GuardBar.
            int start = (int)out.modules.size();
            BarcodeInternal::AppendBits(out, pattern);
            BarcodeInternal::MarkGuard(out, start, (int)std::strlen(pattern));
        }

        void EmitDigit(BarcodePattern& out, char digit, const char* const* table) {
            BarcodeInternal::AppendBits(out, table[digit - '0']);
        }

        // ===== EAN-13 =====
        BarcodePattern EncodeEAN13(std::string digits, bool isISBN) {
            // Strip dashes/spaces if present.
            std::string clean;
            clean.reserve(digits.size());
            for (char c : digits) {
                if (c == '-' || c == ' ' || c == ' ') continue;
                clean.push_back(c);
            }
            digits = clean;

            // Accept 12 or 13 digits; if 12 we compute the check digit.
            if (digits.size() == 12 && BarcodeInternal::IsAllDigits(digits)) {
                int chk = ComputeEANCheck(digits, 12);
                digits.push_back('0' + chk);
            }
            if (digits.size() != 13 || !BarcodeInternal::IsAllDigits(digits)) {
                return BarcodeInternal::MakeError(
                    isISBN ? "ISBN-13: need 12 or 13 digits (978/979 prefix)"
                           : "EAN-13: need 12 or 13 digits");
            }
            // Validate check.
            int expectedChk = ComputeEANCheck(digits, 12);
            if (digits[12] != ('0' + expectedChk)) {
                return BarcodeInternal::MakeError(
                    isISBN ? "ISBN-13: check digit mismatch"
                           : "EAN-13: check digit mismatch");
            }
            if (isISBN) {
                if (digits.substr(0, 3) != "978" && digits.substr(0, 3) != "979") {
                    return BarcodeInternal::MakeError("ISBN-13: prefix must be 978 or 979");
                }
            }

            BarcodePattern p;
            // Left guard: 101
            AppendGuard(p, "101");
            // Left half: 6 digits with parity by first-digit lookup
            const char* parity = kEAN13Parity[digits[0] - '0'];
            int leftStartModule = (int)p.modules.size();
            for (int i = 0; i < 6; ++i) {
                char c = digits[1 + i];
                EmitDigit(p, c, parity[i] == 'L' ? kL : kG);
            }
            int leftEndModule = (int)p.modules.size();
            // Center guard: 01010
            AppendGuard(p, "01010");
            int rightStartModule = (int)p.modules.size();
            // Right half: 6 digits using R table
            for (int i = 0; i < 6; ++i) {
                char c = digits[7 + i];
                EmitDigit(p, c, kR);
            }
            int rightEndModule = (int)p.modules.size();
            // Right guard: 101
            AppendGuard(p, "101");

            // Human-readable layout: leading digit before left quiet zone (we
            // signal that via negative segment), 6 digits under left half,
            // 6 under right half.
            p.humanReadable = digits;
            BarcodePattern::HRISegment seg;
            // Leading digit appears to the LEFT of the left guard. We encode
            // this as a segment with moduleStart < 0 (the widget handles it).
            seg.moduleStart = -1;
            seg.moduleWidth = 0;
            seg.text = std::string(1, digits[0]);
            p.hriSegments.push_back(seg);

            seg.moduleStart = leftStartModule;
            seg.moduleWidth = leftEndModule - leftStartModule;
            seg.text = digits.substr(1, 6);
            p.hriSegments.push_back(seg);

            seg.moduleStart = rightStartModule;
            seg.moduleWidth = rightEndModule - rightStartModule;
            seg.text = digits.substr(7, 6);
            p.hriSegments.push_back(seg);

            if (isISBN) {
                // Format ISBN as "ISBN N-NN-NNNNNN-N" (simplified) above bars.
                std::string isbn = "ISBN " + digits.substr(0, 3) + "-" +
                                   std::string(1, digits[3]) + "-" +
                                   digits.substr(4, 4) + "-" +
                                   digits.substr(8, 4) + "-" +
                                   std::string(1, digits[12]);
                BarcodePattern::HRISegment above;
                above.moduleStart = 0;
                above.moduleWidth = (int)p.modules.size();
                above.text = isbn;
                above.aboveBars = true;
                p.hriSegments.push_back(above);
                p.humanReadable = isbn;
            }
            return p;
        }

        // ===== EAN-8 =====
        BarcodePattern EncodeEAN8(std::string digits) {
            std::string clean;
            for (char c : digits) if (c >= '0' && c <= '9') clean.push_back(c);
            digits = clean;

            if (digits.size() == 7) {
                int chk = ComputeEANCheck(digits, 7);
                digits.push_back('0' + chk);
            }
            if (digits.size() != 8 || !BarcodeInternal::IsAllDigits(digits)) {
                return BarcodeInternal::MakeError("EAN-8: need 7 or 8 digits");
            }
            int expectedChk = ComputeEANCheck(digits, 7);
            if (digits[7] != ('0' + expectedChk)) {
                return BarcodeInternal::MakeError("EAN-8: check digit mismatch");
            }

            BarcodePattern p;
            AppendGuard(p, "101");
            int leftStart = (int)p.modules.size();
            for (int i = 0; i < 4; ++i) EmitDigit(p, digits[i], kL);
            int leftEnd = (int)p.modules.size();
            AppendGuard(p, "01010");
            int rightStart = (int)p.modules.size();
            for (int i = 0; i < 4; ++i) EmitDigit(p, digits[4 + i], kR);
            int rightEnd = (int)p.modules.size();
            AppendGuard(p, "101");

            p.humanReadable = digits;
            BarcodePattern::HRISegment seg;
            seg.moduleStart = leftStart;
            seg.moduleWidth = leftEnd - leftStart;
            seg.text = digits.substr(0, 4);
            p.hriSegments.push_back(seg);

            seg.moduleStart = rightStart;
            seg.moduleWidth = rightEnd - rightStart;
            seg.text = digits.substr(4, 4);
            p.hriSegments.push_back(seg);
            return p;
        }

        // ===== UPC-A =====
        BarcodePattern EncodeUPCA(std::string digits) {
            std::string clean;
            for (char c : digits) if (c >= '0' && c <= '9') clean.push_back(c);
            digits = clean;

            if (digits.size() == 11) {
                int chk = ComputeEANCheck(digits, 11);
                digits.push_back('0' + chk);
            }
            if (digits.size() != 12 || !BarcodeInternal::IsAllDigits(digits)) {
                return BarcodeInternal::MakeError("UPC-A: need 11 or 12 digits");
            }
            int expectedChk = ComputeEANCheck(digits, 11);
            if (digits[11] != ('0' + expectedChk)) {
                return BarcodeInternal::MakeError("UPC-A: check digit mismatch");
            }

            // UPC-A == EAN-13 with leading 0. Generate as EAN-13 then relabel HRI.
            BarcodePattern p = EncodeEAN13("0" + digits, false);
            if (!p.valid) return p;
            // Rewrite HRI: UPC-A shows first digit + last digit outside the
            // guards (smaller), middle 5+5 between.
            p.hriSegments.clear();
            // First module of the bar zone (left guard) = position 0.
            // Left guard = 3 modules, then 6 digits * 7 = 42 modules.
            // So leftStartModule = 3, leftEndModule = 3 + 42 = 45.
            // Center guard = 5 modules, then 6 digits.
            // rightStartModule = 50, rightEndModule = 92, right guard 3 -> 95.

            BarcodePattern::HRISegment seg;
            // First UPC-A digit (digits[0]) appears LEFT of guards.
            seg.moduleStart = -1; seg.moduleWidth = 0;
            seg.text = std::string(1, digits[0]);
            p.hriSegments.push_back(seg);

            // Middle 5 digits under positions 3..3+7..3+7+7+7+7+7 (digits[1..5])
            // The first left-half digit (encoded position) is digits[0] = '0'
            // since we passed "0" + digits as EAN-13. So digit positions 2..6
            // of the 6-left-half are digits[1..5]. They map to module range
            // 3 + 7 = 10 (start) to 3 + 42 = 45 (end), but only 5 chars wide.
            seg.moduleStart = 10;
            seg.moduleWidth = 35;  // 5 digits * 7
            seg.text = digits.substr(1, 5);
            p.hriSegments.push_back(seg);

            // Right half: digits[6..10] under modules 50..85 (first 5 of 6 right digits)
            seg.moduleStart = 50;
            seg.moduleWidth = 35;
            seg.text = digits.substr(6, 5);
            p.hriSegments.push_back(seg);

            // Last UPC-A digit appears RIGHT of guards.
            seg.moduleStart = (int)p.modules.size() + 1;
            seg.moduleWidth = 0;
            seg.text = std::string(1, digits[11]);
            p.hriSegments.push_back(seg);

            p.humanReadable = digits;
            return p;
        }

        // ===== UPC-E =====
        BarcodePattern EncodeUPCE(std::string digits) {
            std::string clean;
            for (char c : digits) if (c >= '0' && c <= '9') clean.push_back(c);
            digits = clean;

            // Accept 6-digit (raw data, we assume NS=0 + computed check),
            // 7-digit (NS + 6 data, compute check), or 8-digit (full NS + data + check).
            if (digits.size() == 6) {
                // Assume number system 0, compute check via UPC-A expansion below.
                digits = "0" + digits;
            }
            if (digits.size() != 7 && digits.size() != 8) {
                return BarcodeInternal::MakeError("UPC-E: need 6, 7, or 8 digits");
            }
            if (!BarcodeInternal::IsAllDigits(digits)) {
                return BarcodeInternal::MakeError("UPC-E: only digits allowed");
            }
            char ns = digits[0];
            if (ns != '0' && ns != '1') {
                return BarcodeInternal::MakeError("UPC-E: number system must be 0 or 1");
            }
            std::string data6 = digits.substr(1, 6);

            // Expand to UPC-A to compute / verify check digit.
            // UPC-E -> UPC-A reverse expansion rules:
            //   data6 = d1 d2 d3 d4 d5 d6
            //   if d6 in 0..2: UPC-A = ns d1 d2 d6 0 0 0 0 d3 d4 d5 C
            //   if d6 == 3:   UPC-A = ns d1 d2 d3 0 0 0 0 0 d4 d5 C
            //   if d6 == 4:   UPC-A = ns d1 d2 d3 d4 0 0 0 0 0 d5 C
            //   if d6 in 5..9:UPC-A = ns d1 d2 d3 d4 d5 0 0 0 0 d6 C
            std::string upca = std::string(1, ns);
            char d[6] = { data6[0], data6[1], data6[2], data6[3], data6[4], data6[5] };
            if (d[5] >= '0' && d[5] <= '2') {
                upca += d[0]; upca += d[1]; upca += d[5];
                upca += "0000";
                upca += d[2]; upca += d[3]; upca += d[4];
            } else if (d[5] == '3') {
                upca += d[0]; upca += d[1]; upca += d[2];
                upca += "00000";
                upca += d[3]; upca += d[4];
            } else if (d[5] == '4') {
                upca += d[0]; upca += d[1]; upca += d[2]; upca += d[3];
                upca += "00000";
                upca += d[4];
            } else {
                upca += d[0]; upca += d[1]; upca += d[2]; upca += d[3]; upca += d[4];
                upca += "0000";
                upca += d[5];
            }
            // upca is now 11 chars; compute check.
            int check = ComputeEANCheck(upca, 11);

            if (digits.size() == 8) {
                if (digits[7] != ('0' + check)) {
                    return BarcodeInternal::MakeError("UPC-E: check digit mismatch");
                }
            } else {
                digits.push_back('0' + check);
            }
            // Final 8-digit HRI: ns data6 check
            std::string hri = std::string(1, ns) + data6 + std::string(1, '0' + check);

            // Pick parity pattern.
            const char* parity = kUPCEParityNS0[check];
            // For NS=1, invert parity (E<->O).

            BarcodePattern p;
            AppendGuard(p, "101");
            int dataStart = (int)p.modules.size();
            for (int i = 0; i < 6; ++i) {
                char par = parity[i];
                if (ns == '1') par = (par == 'E') ? 'O' : 'E';
                const char* const* table = (par == 'O') ? kL : kG;
                EmitDigit(p, data6[i], table);
            }
            int dataEnd = (int)p.modules.size();
            // Right guard for UPC-E: 010101 (6 modules)
            AppendGuard(p, "010101");

            // HRI: ns to the left, data6 under bars, check to the right.
            BarcodePattern::HRISegment seg;
            seg.moduleStart = -1; seg.moduleWidth = 0;
            seg.text = std::string(1, ns);
            p.hriSegments.push_back(seg);

            seg.moduleStart = dataStart; seg.moduleWidth = dataEnd - dataStart;
            seg.text = data6;
            p.hriSegments.push_back(seg);

            seg.moduleStart = (int)p.modules.size() + 1; seg.moduleWidth = 0;
            seg.text = std::string(1, '0' + check);
            p.hriSegments.push_back(seg);

            p.humanReadable = hri;
            return p;
        }

        class EANUPCEncoder : public IBarcodeEncoder {
        public:
            EANUPCEncoder(const BarcodeEncoderOptions& opt, BarcodeSymbology m)
                : options(opt), mode(m) {}

            BarcodeSymbology GetSymbology() const override { return mode; }
            const char* GetName() const override {
                switch (mode) {
                    case BarcodeSymbology::EAN13: return "EAN-13";
                    case BarcodeSymbology::EAN8:  return "EAN-8";
                    case BarcodeSymbology::UPCA:  return "UPC-A";
                    case BarcodeSymbology::UPCE:  return "UPC-E";
                    case BarcodeSymbology::ISBN:  return "ISBN-13";
                    default: return "EAN/UPC";
                }
            }

            BarcodePattern Encode(const std::string& data) const override {
                switch (mode) {
                    case BarcodeSymbology::EAN13: return EncodeEAN13(data, false);
                    case BarcodeSymbology::ISBN:  return EncodeEAN13(data, true);
                    case BarcodeSymbology::EAN8:  return EncodeEAN8(data);
                    case BarcodeSymbology::UPCA:  return EncodeUPCA(data);
                    case BarcodeSymbology::UPCE:  return EncodeUPCE(data);
                    default:
                        return BarcodeInternal::MakeError("Bad EAN/UPC mode");
                }
            }

        private:
            BarcodeEncoderOptions options;
            BarcodeSymbology mode;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateEANUPCEncoder(const BarcodeEncoderOptions& opt,
                                                         BarcodeSymbology mode) {
        return std::make_unique<EANUPCEncoder>(opt, mode);
    }

} // namespace UltraCanvas
