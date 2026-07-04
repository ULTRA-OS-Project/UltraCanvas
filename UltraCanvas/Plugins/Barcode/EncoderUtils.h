// Plugins/Barcode/EncoderUtils.h
// Shared helpers used by individual symbology encoders. Not part of the
// public API — only included by encoder TUs in this directory.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#pragma once

#include "Plugins/Barcode/UltraCanvasBarcodeEncoders.h"
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
    namespace BarcodeInternal {

        // Append a sequence of bar/space modules to the pattern. `pattern` is
        // a string of '1' (bar) and '0' (space) characters. Convenient for
        // copy-pasting symbology tables directly out of the standards.
        inline void AppendBits(BarcodePattern& out, const char* bits) {
            for (const char* p = bits; *p; ++p) {
                out.modules.push_back(*p == '1' ? BarcodeModuleKind::Bar
                                                : BarcodeModuleKind::Space);
            }
        }

        // Append `count` modules of one kind.
        inline void AppendRun(BarcodePattern& out, BarcodeModuleKind kind, int count) {
            out.modules.insert(out.modules.end(), count, kind);
        }

        // Append a wide/narrow run-length pattern. `widths` is an array of
        // narrow/wide counts; `kinds` alternates bar/space starting from
        // `startKind`. Example: "wnwnn" with startKind=Bar means
        // wide-bar, narrow-space, wide-bar, narrow-space, narrow-bar.
        // narrowModules and wideModules are the unit counts (typically 1, 2 or 1, 3).
        inline void AppendWideNarrow(BarcodePattern& out,
                                     const char* pattern,
                                     BarcodeModuleKind startKind,
                                     int narrowModules, int wideModules) {
            BarcodeModuleKind kind = startKind;
            for (const char* p = pattern; *p; ++p) {
                int run = (*p == 'w' || *p == 'W') ? wideModules : narrowModules;
                AppendRun(out, kind, run);
                kind = (kind == BarcodeModuleKind::Bar)
                           ? BarcodeModuleKind::Space
                           : BarcodeModuleKind::Bar;
            }
        }

        // Append a width-table pattern where each digit in `widths` is the
        // raw unit-count for one alternating bar/space run.
        // Example: "2113" starting with Bar = 2-bar 1-space 1-bar 3-space.
        inline void AppendWidths(BarcodePattern& out,
                                  const char* widths,
                                  BarcodeModuleKind startKind) {
            BarcodeModuleKind kind = startKind;
            for (const char* p = widths; *p; ++p) {
                int n = *p - '0';
                if (n <= 0) n = 1;
                AppendRun(out, kind, n);
                kind = (kind == BarcodeModuleKind::Bar)
                           ? BarcodeModuleKind::Space
                           : BarcodeModuleKind::Bar;
            }
        }

        // Promote a contiguous range of bars to guard bars (extends past the
        // main bar zone in the renderer). Used by EAN/UPC.
        inline void MarkGuard(BarcodePattern& out, int startIndex, int count) {
            int end = startIndex + count;
            if (end > static_cast<int>(out.modules.size())) {
                end = static_cast<int>(out.modules.size());
            }
            for (int i = startIndex; i < end; ++i) {
                if (out.modules[i] == BarcodeModuleKind::Bar) {
                    out.modules[i] = BarcodeModuleKind::GuardBar;
                }
            }
        }

        inline BarcodePattern MakeError(const std::string& msg) {
            BarcodePattern p;
            p.valid = false;
            p.errorMessage = msg;
            return p;
        }

        inline bool IsAllDigits(const std::string& s) {
            if (s.empty()) return false;
            for (char c : s) if (c < '0' || c > '9') return false;
            return true;
        }

    } // namespace BarcodeInternal
} // namespace UltraCanvas
