// Plugins/Barcode/Encoders/Code93Encoder.cpp
// Code 93 encoder. Spec: AIM USS-93.
//
// Each character is 9 modules: 3 bars + 3 spaces (6 alternating elements
// starting with a bar). The 47-char set: 0-9, A-Z, -, ., space, $, /, +, %,
// plus 4 shift chars ($), (%), (/), (+) for full ASCII. Start/stop is a
// dedicated 47th pattern. Two check chars (C and K) are mandatory.
// A single narrow bar terminator follows the stop pattern.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "../EncoderUtils.h"
#include <array>

namespace UltraCanvas {

    namespace {
        // Index 0..42 = directly usable chars (same data set as Code 39 less '*').
        // Index 43..46 = shift sequence anchors ($), (%), (/), (+).
        // Index 47    = start/stop pattern (drawn but not weighted in checks).
        constexpr const char* kCharset =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";  // 43 entries

        // 48 entries (43 data + 4 shift + 1 start/stop). 6 widths each.
        constexpr const char* kPatterns[48] = {
            "131112","111213","111312","111411","121113","121212","121311","111114",
            "131211","141111","211113","211212","211311","221112","221211","231111",
            "112113","112212","112311","122112","132111","111123","111222","111321",
            "121122","131121","212112","212211","211122","211221","221121","222111",
            "112122","112221","122121","123111","121131","311112","311211","321111",
            "112131","113121","211131","121221","311121","122211","312111","111141",
        };

        constexpr int START_STOP_INDEX = 47;
        constexpr int SHIFT_DOLLAR     = 43;
        constexpr int SHIFT_PERCENT    = 44;
        constexpr int SHIFT_SLASH      = 45;
        constexpr int SHIFT_PLUS       = 46;

        int DirectIndex(char c) {
            // Returns 0..42 for direct chars in kCharset, -1 otherwise.
            for (int i = 0; i < 43; ++i) {
                if (kCharset[i] == c) return i;
            }
            return -1;
        }

        // Map an ASCII byte to a sequence of Code 93 character indices. Two-char
        // sequences are used for control codes, lowercase, and a handful of
        // punctuation. Reference: USS-93 Appendix B.
        bool ExpandAscii(unsigned char c, std::vector<int>& out) {
            if (c >= 128) return false;

            // Directly representable?
            if (c >= '0' && c <= '9') { out.push_back(c - '0'); return true; }
            if (c >= 'A' && c <= 'Z') { out.push_back(10 + (c - 'A')); return true; }
            switch (c) {
                case '-':  out.push_back(36); return true;
                case '.':  out.push_back(37); return true;
                case ' ':  out.push_back(38); return true;
                case '$':  out.push_back(39); return true;
                case '/':  out.push_back(40); return true;
                case '+':  out.push_back(41); return true;
                case '%':  out.push_back(42); return true;
                default: break;
            }

            // Shift sequences. The second char is a Code 93 char index (0..42).
            // Layout mirrors the standard table:
            //   ($) + A..Z  -> NUL..SUB (0x00..0x1A)  except a few special cases
            //   (%) + A..Z  -> ESC..DEL (some)
            //   (/) + A..Z  -> ! .. , (mostly punctuation)
            //   (+) + A..Z  -> lowercase a..z
            if (c >= 'a' && c <= 'z') {
                out.push_back(SHIFT_PLUS);
                out.push_back(10 + (c - 'a'));
                return true;
            }

            // Control chars 0x00..0x1A: ($) + A..Z
            if (c <= 0x1A) {
                if (c == 0x00) { out.push_back(SHIFT_PERCENT); out.push_back(30 /*U*/); return true; }
                out.push_back(SHIFT_DOLLAR);
                out.push_back(10 + (c - 1));  // ($)A = 0x01, ..., ($)Z = 0x1A
                return true;
            }

            // 0x1B..0x1F: (%) + A..E
            if (c >= 0x1B && c <= 0x1F) {
                out.push_back(SHIFT_PERCENT);
                out.push_back(10 + (c - 0x1B));
                return true;
            }

            // Punctuation via (/) shifts.
            switch (c) {
                case '!': out.push_back(SHIFT_SLASH); out.push_back(10 + 0); return true;  // (/)A
                case '"': out.push_back(SHIFT_SLASH); out.push_back(10 + 1); return true;
                case '#': out.push_back(SHIFT_SLASH); out.push_back(10 + 2); return true;
                case '&': out.push_back(SHIFT_SLASH); out.push_back(10 + 5); return true;
                case '\'':out.push_back(SHIFT_SLASH); out.push_back(10 + 6); return true;
                case '(': out.push_back(SHIFT_SLASH); out.push_back(10 + 7); return true;
                case ')': out.push_back(SHIFT_SLASH); out.push_back(10 + 8); return true;
                case '*': out.push_back(SHIFT_SLASH); out.push_back(10 + 9); return true;
                case ',': out.push_back(SHIFT_SLASH); out.push_back(10 + 11); return true;
                case ':': out.push_back(SHIFT_SLASH); out.push_back(10 + 25); return true; // (/)Z
                case ';': out.push_back(SHIFT_PERCENT); out.push_back(10 + 5 /*F*/); return true;
                case '<': out.push_back(SHIFT_PERCENT); out.push_back(10 + 6); return true;
                case '=': out.push_back(SHIFT_PERCENT); out.push_back(10 + 7); return true;
                case '>': out.push_back(SHIFT_PERCENT); out.push_back(10 + 8); return true;
                case '?': out.push_back(SHIFT_PERCENT); out.push_back(10 + 9); return true;
                case '@': out.push_back(SHIFT_PERCENT); out.push_back(10 + 21 /*V*/); return true;
                case '[': out.push_back(SHIFT_PERCENT); out.push_back(10 + 10 /*K*/); return true;
                case '\\':out.push_back(SHIFT_PERCENT); out.push_back(10 + 11); return true;
                case ']': out.push_back(SHIFT_PERCENT); out.push_back(10 + 12); return true;
                case '^': out.push_back(SHIFT_PERCENT); out.push_back(10 + 13); return true;
                case '_': out.push_back(SHIFT_PERCENT); out.push_back(10 + 14); return true;
                case '`': out.push_back(SHIFT_PERCENT); out.push_back(10 + 22 /*W*/); return true;
                case '{': out.push_back(SHIFT_PERCENT); out.push_back(10 + 15 /*P*/); return true;
                case '|': out.push_back(SHIFT_PERCENT); out.push_back(10 + 16); return true;
                case '}': out.push_back(SHIFT_PERCENT); out.push_back(10 + 17); return true;
                case '~': out.push_back(SHIFT_PERCENT); out.push_back(10 + 18); return true;
                case 0x7F:out.push_back(SHIFT_PERCENT); out.push_back(10 + 19); return true; // (%)T
                default: break;
            }
            return false;
        }

        void EmitChar(BarcodePattern& out, int idx) {
            BarcodeInternal::AppendWidths(out, kPatterns[idx], BarcodeModuleKind::Bar);
        }

        class Code93Encoder : public IBarcodeEncoder {
        public:
            explicit Code93Encoder(const BarcodeEncoderOptions& opt) : options(opt) {}

            BarcodeSymbology GetSymbology() const override { return BarcodeSymbology::Code93; }
            const char* GetName() const override { return "Code 93"; }

            BarcodePattern Encode(const std::string& data) const override {
                if (data.empty()) {
                    return BarcodeInternal::MakeError("Code 93: empty input");
                }

                // Expand input into char-index stream. We accept either a pure
                // Code 93 alphabet input OR fall back to full-ASCII expansion
                // when we encounter a char outside the base alphabet.
                std::vector<int> indices;
                indices.reserve(data.size() * 2);

                bool needsExtended = false;
                for (char c : data) {
                    int idx = DirectIndex(c);
                    if (idx >= 0) {
                        indices.push_back(idx);
                    } else {
                        needsExtended = true;
                        if (!ExpandAscii(static_cast<unsigned char>(c), indices)) {
                            return BarcodeInternal::MakeError(
                                std::string("Code 93: unencodable byte 0x") +
                                std::to_string(static_cast<int>(static_cast<unsigned char>(c))));
                        }
                    }
                }

                // Check digit C (weights 1..20 then wrap, right-to-left).
                {
                    int sum = 0, weight = 1;
                    for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i) {
                        sum += indices[i] * weight;
                        weight = (weight % 20) + 1;
                    }
                    indices.push_back(sum % 47);
                }
                // Check digit K (weights 1..15 then wrap).
                {
                    int sum = 0, weight = 1;
                    for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i) {
                        sum += indices[i] * weight;
                        weight = (weight % 15) + 1;
                    }
                    indices.push_back(sum % 47);
                }

                BarcodePattern p;
                EmitChar(p, START_STOP_INDEX);
                for (int idx : indices) EmitChar(p, idx);
                EmitChar(p, START_STOP_INDEX);
                // Terminator bar: a single narrow bar.
                BarcodeInternal::AppendRun(p, BarcodeModuleKind::Bar, 1);

                if (options.emitHumanReadable) {
                    p.humanReadable = needsExtended ? data : data;
                }
                return p;
            }

        private:
            BarcodeEncoderOptions options;
        };
    } // namespace

    std::unique_ptr<IBarcodeEncoder> CreateCode93Encoder(const BarcodeEncoderOptions& opt) {
        return std::make_unique<Code93Encoder>(opt);
    }

} // namespace UltraCanvas
