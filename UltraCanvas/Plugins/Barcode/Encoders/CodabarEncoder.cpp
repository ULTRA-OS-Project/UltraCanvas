// Plugins/Barcode/Encoders/CodabarEncoder.cpp
// Codabar (NW-7 / Rationalized Codabar) encoder.
//
// Character set: 0-9 plus "-$:/.+" plus 4 start/stop chars A B C D
// (lowercase a/b/c/d also accepted). Each character is 7 elements (4 bars +
// 3 spaces alternating, starting with bar). Some elements are wide (W),
// others narrow (N); ratio used is 2:1. Inter-character gap = 1 narrow
// space.
//
// Patterns sourced from Zint's CodaTable (BSD-3), trailing inter-char
// element stripped — we add the gap explicitly between characters.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"

namespace UltraCanvas {

    namespace {
        constexpr const char* kCharset = "0123456789-$:/.+ABCD";

        // 20 entries, 7-width patterns each. '1' = narrow, '2' = wide.
        constexpr const char* kPatterns[20] = {
            "1111122","1111221","1112112","2211111","1121121",
            "2111121","1211112","1211211","1221111","2112111",
            "1112211","1122111","2111212","2121112","2121211",
            "1121212","1122212","1221212","1212112","1211212",
        };

        int IndexOf(char c) {
            // Allow lowercase start/stop.
            if (c >= 'a' && c <= 'd') c = (char)(c - 32);
            for (int i = 0; i < 20; ++i) if (kCharset[i] == c) return i;
            return -1;
        }

        bool IsStartStop(char c) {
            return c == 'A' || c == 'B' || c == 'C' || c == 'D';
        }

        void EmitChar(BarcodePattern& out, int idx) {
            BarcodeInternal::AppendWidths(out, kPatterns[idx], BarcodeModuleKind::Bar);
        }

        class CodabarEncoder : public IBarcodeEncoder {
        public:
            explicit CodabarEncoder(const BarcodeEncoderOptions& opt) : options(opt) {}
            BarcodeSymbology GetSymbology() const override { return BarcodeSymbology::Codabar; }
            const char* GetName() const override { return "Codabar"; }

            BarcodePattern Encode(const std::string& data) const override {
                if (data.size() < 3) {
                    return BarcodeInternal::MakeError(
                        "Codabar: need start + data + stop (e.g. \"A12345B\")");
                }
                std::string norm = data;
                // Uppercase start/stop only.
                if (norm.front() >= 'a' && norm.front() <= 'd') norm.front() -= 32;
                if (norm.back()  >= 'a' && norm.back()  <= 'd') norm.back()  -= 32;

                if (!IsStartStop(norm.front()) || !IsStartStop(norm.back())) {
                    return BarcodeInternal::MakeError(
                        "Codabar: start/stop chars must be one of A B C D");
                }

                BarcodePattern p;
                for (size_t i = 0; i < norm.size(); ++i) {
                    char c = norm[i];
                    // Middle chars must not be start/stop.
                    if (i > 0 && i + 1 < norm.size() && IsStartStop(c)) {
                        return BarcodeInternal::MakeError(
                            "Codabar: A/B/C/D only allowed at start and end");
                    }
                    int idx = IndexOf(c);
                    if (idx < 0) {
                        return BarcodeInternal::MakeError(
                            std::string("Codabar: invalid char '") + c + "'");
                    }
                    if (i > 0) {
                        // Inter-char narrow space.
                        BarcodeInternal::AppendRun(p, BarcodeModuleKind::Space, 1);
                    }
                    EmitChar(p, idx);
                }
                if (options.emitHumanReadable) p.humanReadable = norm;
                return p;
            }
        private:
            BarcodeEncoderOptions options;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateCodabarEncoder(const BarcodeEncoderOptions& opt) {
        return std::make_unique<CodabarEncoder>(opt);
    }

} // namespace UltraCanvas
