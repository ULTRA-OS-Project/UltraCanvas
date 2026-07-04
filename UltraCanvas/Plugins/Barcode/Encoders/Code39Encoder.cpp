// Plugins/Barcode/Encoders/Code39Encoder.cpp
// Code 39 (a.k.a. "3 of 9") encoder, plus Code 39 Extended (Full ASCII).
//
// Spec: ISO/IEC 16388. 43-character set: 0-9, A-Z, space, -, ., $, /, +, %.
// Each character = 9 modules (5 bars + 4 spaces), of which exactly 3 are
// "wide". The wide:narrow ratio used here is 2:1 (so each character spans
// 12 modules: 6 narrow + 3 wide@2 = 12). Inter-character gap = 1 narrow
// space. Start/stop = '*'. Optional Mod 43 checksum.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"
#include <array>
#include <cstring>

namespace UltraCanvas {

    namespace {
        // Code 39 character set in index order. Indices 0..42 are the mod-43
        // data set; index 43 is the '*' start/stop pattern (never weighted in
        // the checksum, but indexable so EmitChar can find its width pattern).
        constexpr const char* kCode39Charset =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%*";

        // Width patterns (9 widths: B S B S B S B S B), '1' = narrow, '2' = wide.
        // Order MUST match kCode39Charset above, plus '*' as the last entry.
        constexpr const char* kCode39Patterns[] = {
            "111221211", // 0
            "211211112", // 1
            "112211112", // 2
            "212211111", // 3
            "111221112", // 4
            "211221111", // 5
            "112221111", // 6
            "111211212", // 7
            "211211211", // 8
            "112211211", // 9
            "211112112", // A
            "112112112", // B
            "212112111", // C
            "111122112", // D
            "211122111", // E
            "112122111", // F
            "111112212", // G
            "211112211", // H
            "112112211", // I
            "111122211", // J
            "211111122", // K
            "112111122", // L
            "212111121", // M
            "111121122", // N
            "211121121", // O
            "112121121", // P
            "111111222", // Q
            "211111221", // R
            "112111221", // S
            "111121221", // T
            "221111112", // U
            "122111112", // V
            "222111111", // W
            "121121112", // X
            "221121111", // Y
            "122121111", // Z
            "121111212", // -
            "221111211", // .
            "122111211", // (space)
            "121212111", // $
            "121211121", // /
            "121112121", // +
            "111212121", // %
            "121121211", // *  (start/stop)
        };

        int IndexOf(char c) {
            const char* p = std::strchr(kCode39Charset, c);
            if (!p) return -1;
            return static_cast<int>(p - kCode39Charset);
        }

        // Full ASCII (Code 39 Extended) encoding map.
        // Each ASCII char (0-127) maps to one or two Code 39 chars.
        // Source: AIM USS-39 spec, Appendix B.
        const char* ExtendedExpand(unsigned char c) {
            static const char* table[128] = {
                "%U","$A","$B","$C","$D","$E","$F","$G","$H","$I","$J","$K","$L","$M","$N","$O",
                "$P","$Q","$R","$S","$T","$U","$V","$W","$X","$Y","$Z","%A","%B","%C","%D","%E",
                " " ,"/A","/B","/C","/D","/E","/F","/G","/H","/I","/J","/K","/L","-" ,"." ,"/O",
                "0" ,"1" ,"2" ,"3" ,"4" ,"5" ,"6" ,"7" ,"8" ,"9" ,"/Z","%F","%G","%H","%I","%J",
                "%V","A" ,"B" ,"C" ,"D" ,"E" ,"F" ,"G" ,"H" ,"I" ,"J" ,"K" ,"L" ,"M" ,"N" ,"O" ,
                "P" ,"Q" ,"R" ,"S" ,"T" ,"U" ,"V" ,"W" ,"X" ,"Y" ,"Z" ,"%K","%L","%M","%N","%O",
                "%W","+A","+B","+C","+D","+E","+F","+G","+H","+I","+J","+K","+L","+M","+N","+O",
                "+P","+Q","+R","+S","+T","+U","+V","+W","+X","+Y","+Z","%P","%Q","%R","%S","%T",
            };
            return (c < 128) ? table[c] : nullptr;
        }

        void EmitChar(BarcodePattern& out, int idx) {
            const char* widths = kCode39Patterns[idx];
            // Wide:narrow = 2:1 — each char is 12 modules + 1-narrow separator below.
            BarcodeInternal::AppendWidths(out, widths, BarcodeModuleKind::Bar);
        }

        void EmitInterCharGap(BarcodePattern& out) {
            // One narrow space.
            BarcodeInternal::AppendRun(out, BarcodeModuleKind::Space, 1);
        }

        class Code39Encoder : public IBarcodeEncoder {
        public:
            Code39Encoder(const BarcodeEncoderOptions& opt, bool ext)
                : options(opt), extended(ext) {}

            BarcodeSymbology GetSymbology() const override {
                return extended ? BarcodeSymbology::Code39Extended : BarcodeSymbology::Code39;
            }
            const char* GetName() const override {
                return extended ? "Code 39 Extended" : "Code 39";
            }

            BarcodePattern Encode(const std::string& data) const override {
                BarcodePattern p;
                if (data.empty()) {
                    return BarcodeInternal::MakeError("Code 39: empty input");
                }

                // Build the actual character list (after extended expansion).
                std::string expanded;
                expanded.reserve(data.size() * 2);
                std::string hri; hri.reserve(data.size());
                if (extended) {
                    for (unsigned char c : data) {
                        const char* exp = ExtendedExpand(c);
                        if (!exp) {
                            return BarcodeInternal::MakeError(
                                "Code 39 Extended: non-ASCII byte 0x"
                                + std::to_string(static_cast<int>(c)));
                        }
                        expanded.append(exp);
                        hri.push_back(static_cast<char>(c));
                    }
                } else {
                    for (char c : data) {
                        char up = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
                        if (IndexOf(up) < 0 || up == '*') {
                            return BarcodeInternal::MakeError(
                                std::string("Code 39: invalid char '") + c + "'");
                        }
                        expanded.push_back(up);
                    }
                    hri = expanded;
                }

                // Optional Mod 43 checksum.
                if (options.includeChecksum) {
                    int sum = 0;
                    for (char c : expanded) sum += IndexOf(c);
                    int checkIdx = sum % 43;
                    expanded.push_back(kCode39Charset[checkIdx]);
                }

                // Emit start (*), data, stop (*), with inter-char gaps.
                const int starIdx = IndexOf('*');
                EmitChar(p, starIdx);
                for (char c : expanded) {
                    EmitInterCharGap(p);
                    EmitChar(p, IndexOf(c));
                }
                EmitInterCharGap(p);
                EmitChar(p, starIdx);

                if (options.emitHumanReadable) {
                    p.humanReadable = "*" + hri + "*";
                }
                return p;
            }

        private:
            BarcodeEncoderOptions options;
            bool extended;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateCode39Encoder(const BarcodeEncoderOptions& opt,
                                                         bool extended) {
        return std::make_unique<Code39Encoder>(opt, extended);
    }

} // namespace UltraCanvas
