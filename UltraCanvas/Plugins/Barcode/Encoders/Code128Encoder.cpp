// Plugins/Barcode/Encoders/Code128Encoder.cpp
// Code 128 + GS1-128 encoder. Spec: ISO/IEC 15417.
//
// Each codeword (except stop) is 11 modules — 3 bars + 3 spaces summing to
// 11. Stop is 13 modules (4 bars + 3 spaces, total 13). Three subsets A/B/C
// switch with dedicated code values; auto-mode picks the most efficient mix.
//
// GS1-128 inserts FNC1 (codeword 102) at the start; the user can include
// additional FNC1 separators by embedding the sentinel "~F" anywhere in the
// data.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"
#include <array>
#include <cstring>

namespace UltraCanvas {

    namespace {
        // 107 patterns. Each is a width-string interpreted as alternating
        // bar/space (starting with bar), summing to 11 modules (13 for stop).
        constexpr const char* kPatterns[107] = {
            "212222","222122","222221","121223","121322","131222","122213","122312",
            "132212","221213","221312","231212","112232","122132","122231","113222",
            "123122","123221","223211","221132","221231","213212","223112","312131",
            "311222","321122","321221","312212","322112","322211","212123","212321",
            "232121","111323","131123","131321","112313","132113","132311","211313",
            "231113","231311","112133","112331","132131","113123","113321","133121",
            "313121","211331","231131","213113","213311","213131","311123","311321",
            "331121","312113","312311","332111","314111","221411","431111","111224",
            "111422","121124","121421","141122","141221","112214","112412","122114",
            "122411","142112","142211","241211","221114","413111","241112","134111",
            "111242","121142","121241","114212","124112","124211","411212","421112",
            "421211","212141","214121","412121","111143","111341","131141","114113",
            "114311","411113","411311","113141","114131","311141","411131","211412",
            "211214","211232",
            "2331112", // 106 = STOP (4 bars + 3 spaces, 13 modules)
        };

        constexpr int FNC1 = 102;
        constexpr int FNC2 = 97;
        constexpr int FNC3 = 96;
        constexpr int FNC4_A = 101;  // Value of FNC4 in subset A
        constexpr int FNC4_B = 100;  // Value of FNC4 in subset B
        constexpr int SHIFT = 98;
        constexpr int CODE_C = 99;
        constexpr int CODE_B_FROM_A = 100;
        constexpr int CODE_A_FROM_B = 101;
        constexpr int CODE_B_FROM_C = 100;
        constexpr int CODE_A_FROM_C = 101;
        constexpr int START_A = 103;
        constexpr int START_B = 104;
        constexpr int START_C = 105;
        constexpr int STOP    = 106;

        enum class Subset { A, B, C };

        // The encoded value of a printable character in subset A.
        // Subset A: ASCII 0..95.  Codeword == byte value mod 96, with control
        // codes (0..31) mapped to codewords 64..95.
        int ValueInA(unsigned char c) {
            if (c <= 95) {
                // ASCII 32..95 -> codewords 0..63
                // ASCII 0..31  -> codewords 64..95
                return (c >= 32) ? (c - 32) : (c + 64);
            }
            return -1;
        }
        int ValueInB(unsigned char c) {
            if (c >= 32 && c <= 127) return c - 32;  // 32..127 -> 0..95
            return -1;
        }

        bool IsDigit(char c) { return c >= '0' && c <= '9'; }

        // Count consecutive digits starting at pos.
        int DigitRun(const std::string& s, size_t pos) {
            int n = 0;
            while (pos + n < s.size() && IsDigit(s[pos + n])) ++n;
            return n;
        }

        // Greedy auto-encode. Returns the list of codewords (not yet including
        // checksum or stop). Uses "~F" as the FNC1 sentinel.
        bool AutoEncode(const std::string& data, bool gs1, std::vector<int>& out, std::string& err) {
            const size_t n = data.size();
            size_t i = 0;
            Subset mode;

            // Helper: peek for FNC1 sentinel at position p.
            auto sentinelAt = [&](size_t p) -> bool {
                return p + 1 < n && data[p] == '~' && data[p + 1] == 'F';
            };

            // Determine start mode.
            int digitsAtStart = 0;
            while (digitsAtStart < (int)n && IsDigit(data[digitsAtStart])) ++digitsAtStart;
            if (digitsAtStart == (int)n && (n % 2) == 0 && n >= 2) {
                out.push_back(START_C); mode = Subset::C;
            } else if (digitsAtStart >= 4) {
                out.push_back(START_C); mode = Subset::C;
            } else {
                // Pick A if first non-digit is a control char, else B.
                bool needA = false;
                for (size_t k = 0; k < n; ++k) {
                    unsigned char c = (unsigned char)data[k];
                    if (sentinelAt(k)) { ++k; continue; }
                    if (c < 32) { needA = true; break; }
                    if (c >= 32 && c <= 127) break; // B-compatible char first
                }
                out.push_back(needA ? START_A : START_B);
                mode = needA ? Subset::A : Subset::B;
            }

            if (gs1) out.push_back(FNC1);

            while (i < n) {
                // FNC1 sentinel "~F"
                if (sentinelAt(i)) {
                    out.push_back(FNC1);
                    i += 2;
                    continue;
                }

                if (mode == Subset::C) {
                    if (i + 1 < n && IsDigit(data[i]) && IsDigit(data[i + 1])) {
                        int pair = (data[i] - '0') * 10 + (data[i + 1] - '0');
                        out.push_back(pair);
                        i += 2;
                        continue;
                    }
                    // Need to leave C: choose A or B for next char.
                    unsigned char c = (unsigned char)(i < n ? data[i] : 0);
                    if (c < 32 || sentinelAt(i)) {
                        out.push_back(CODE_A_FROM_C); mode = Subset::A;
                    } else {
                        out.push_back(CODE_B_FROM_C); mode = Subset::B;
                    }
                } else {
                    // Currently A or B. Consider switching to C if we have a
                    // long digit run.
                    int run = DigitRun(data, i);
                    bool atEnd = (i + run == n);
                    // Cost of staying: `run` codewords.
                    // Cost of switching to C: 1 (Code C) + run/2 codewords.
                    if (run >= 6 || (atEnd && run >= 4 && (run % 2) == 0) ||
                        (i == 0 && run >= 4)) {
                        // Switch to C. If run is odd, encode the first digit in
                        // current mode, then switch.
                        if (run % 2 == 1) {
                            // Emit one digit in current mode.
                            unsigned char d = (unsigned char)data[i];
                            int v = (mode == Subset::A) ? ValueInA(d) : ValueInB(d);
                            out.push_back(v);
                            ++i; --run;
                        }
                        out.push_back(CODE_C); mode = Subset::C;
                        continue;
                    }

                    // Otherwise emit current char in A or B, switching if needed.
                    unsigned char c = (unsigned char)data[i];
                    int va = ValueInA(c);
                    int vb = ValueInB(c);

                    // Special: char encodable only in A vs only in B
                    bool inA = (va >= 0);
                    bool inB = (vb >= 0);
                    if (!inA && !inB) {
                        err = std::string("Code 128: unencodable byte 0x") +
                              std::to_string((int)c);
                        return false;
                    }

                    if (mode == Subset::A && !inA) {
                        // Need B. Use shift if just one char, else code switch.
                        // Cheap heuristic: look at next char; if also B-only, switch.
                        bool nextAlsoB = (i + 1 < n) && (ValueInA((unsigned char)data[i + 1]) < 0);
                        if (nextAlsoB) {
                            out.push_back(CODE_B_FROM_A); mode = Subset::B;
                        } else {
                            out.push_back(SHIFT);
                            out.push_back(vb);
                            ++i; continue;
                        }
                    } else if (mode == Subset::B && !inB) {
                        bool nextAlsoA = (i + 1 < n) && (ValueInB((unsigned char)data[i + 1]) < 0);
                        if (nextAlsoA) {
                            out.push_back(CODE_A_FROM_B); mode = Subset::A;
                        } else {
                            out.push_back(SHIFT);
                            out.push_back(va);
                            ++i; continue;
                        }
                    }

                    int v = (mode == Subset::A) ? ValueInA(c) : ValueInB(c);
                    out.push_back(v);
                    ++i;
                }
            }
            return true;
        }

        bool ForceEncode(const std::string& data, Subset forced, bool gs1,
                          std::vector<int>& out, std::string& err) {
            const size_t n = data.size();
            switch (forced) {
                case Subset::A: out.push_back(START_A); break;
                case Subset::B: out.push_back(START_B); break;
                case Subset::C: out.push_back(START_C); break;
            }
            if (gs1) out.push_back(FNC1);

            if (forced == Subset::C) {
                if (n % 2 != 0) {
                    err = "Code 128-C: input length must be even";
                    return false;
                }
                for (size_t i = 0; i < n;) {
                    if (i + 1 < n && data[i] == '~' && data[i + 1] == 'F') {
                        out.push_back(FNC1); i += 2; continue;
                    }
                    if (!IsDigit(data[i]) || !IsDigit(data[i + 1])) {
                        err = "Code 128-C: non-digit char";
                        return false;
                    }
                    out.push_back((data[i] - '0') * 10 + (data[i + 1] - '0'));
                    i += 2;
                }
            } else {
                for (size_t i = 0; i < n; ++i) {
                    if (i + 1 < n && data[i] == '~' && data[i + 1] == 'F') {
                        out.push_back(FNC1); ++i; continue;
                    }
                    unsigned char c = (unsigned char)data[i];
                    int v = (forced == Subset::A) ? ValueInA(c) : ValueInB(c);
                    if (v < 0) {
                        err = std::string("Code 128: char 0x") + std::to_string((int)c) +
                              " not encodable in subset " +
                              (forced == Subset::A ? "A" : "B");
                        return false;
                    }
                    out.push_back(v);
                }
            }
            return true;
        }

        class Code128Encoder : public IBarcodeEncoder {
        public:
            Code128Encoder(const BarcodeEncoderOptions& opt, BarcodeSymbology m)
                : options(opt), mode(m) {}

            BarcodeSymbology GetSymbology() const override { return mode; }
            const char* GetName() const override {
                switch (mode) {
                    case BarcodeSymbology::Code128A: return "Code 128-A";
                    case BarcodeSymbology::Code128B: return "Code 128-B";
                    case BarcodeSymbology::Code128C: return "Code 128-C";
                    case BarcodeSymbology::GS1_128:  return "GS1-128";
                    default: return "Code 128";
                }
            }

            BarcodePattern Encode(const std::string& data) const override {
                if (data.empty()) {
                    return BarcodeInternal::MakeError("Code 128: empty input");
                }

                std::vector<int> codewords;
                codewords.reserve(data.size() + 4);
                std::string err;
                bool gs1 = (mode == BarcodeSymbology::GS1_128);

                bool ok = false;
                switch (mode) {
                    case BarcodeSymbology::Code128A:
                        ok = ForceEncode(data, Subset::A, false, codewords, err); break;
                    case BarcodeSymbology::Code128B:
                        ok = ForceEncode(data, Subset::B, false, codewords, err); break;
                    case BarcodeSymbology::Code128C:
                        ok = ForceEncode(data, Subset::C, false, codewords, err); break;
                    case BarcodeSymbology::GS1_128:
                    case BarcodeSymbology::Code128:
                    default:
                        ok = AutoEncode(data, gs1, codewords, err); break;
                }

                if (!ok) return BarcodeInternal::MakeError(err);

                // Checksum: (start + sum(value[i]*i for i>=1)) mod 103.
                // Start codeword has weight 1, then each subsequent has weight = position (1-based after start).
                int sum = codewords[0];
                for (size_t i = 1; i < codewords.size(); ++i) {
                    sum += static_cast<int>(i) * codewords[i];
                }
                int check = sum % 103;
                codewords.push_back(check);
                codewords.push_back(STOP);

                BarcodePattern p;
                for (int cw : codewords) {
                    BarcodeInternal::AppendWidths(p, kPatterns[cw], BarcodeModuleKind::Bar);
                }

                if (options.emitHumanReadable) {
                    if (gs1) {
                        // Strip "~F" sentinels and show as GS1 with ( ) around AIs
                        // if the user formatted that way; otherwise show plain.
                        std::string hri;
                        hri.reserve(data.size());
                        for (size_t i = 0; i < data.size(); ++i) {
                            if (i + 1 < data.size() && data[i] == '~' && data[i + 1] == 'F') {
                                ++i; continue;
                            }
                            hri.push_back(data[i]);
                        }
                        p.humanReadable = hri;
                    } else {
                        // Strip sentinels here too.
                        std::string hri;
                        hri.reserve(data.size());
                        for (size_t i = 0; i < data.size(); ++i) {
                            if (i + 1 < data.size() && data[i] == '~' && data[i + 1] == 'F') {
                                ++i; continue;
                            }
                            hri.push_back(data[i]);
                        }
                        p.humanReadable = hri;
                    }
                }
                return p;
            }

        private:
            BarcodeEncoderOptions options;
            BarcodeSymbology mode;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateCode128Encoder(const BarcodeEncoderOptions& opt,
                                                          BarcodeSymbology mode) {
        return std::make_unique<Code128Encoder>(opt, mode);
    }

} // namespace UltraCanvas
