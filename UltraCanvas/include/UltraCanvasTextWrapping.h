// include/UltraCanvasTextWrapping.h
// Text wrapping helpers for captions that are narrower than their text —
// file names under a thumbnail tile above all.
// The algorithm is deliberately independent of any render context: every
// function takes a `measure` callable returning the pixel width of a string,
// so the same code runs against a real IRenderContext in a widget and against
// a synthetic font in a unit test (Tests/TextWrapTest.cpp).
//
// What it does beyond filling lines up to their width:
//   * words are kept whole. A break is only placed *inside* a word when the
//     word has to be split — a break that would leave `breakTolerance`
//     characters or fewer on either side of it ("CoderBo" / "x") buys almost
//     no room and is never taken;
//   * a line may run `overflowSlack` pixels past its width to keep a word (or
//     a whole last line) intact, which is what the inset around a caption is
//     there for;
//   * when keeping words whole would cost part of the text, mid-word breaks
//     are allowed again — content beats typography (see Wrap);
//   * text that fits its lines completely is re-broken balanced, so the lines
//     come out near equal instead of the first taking all it can hold.
// Version: 1.0.0
// Last Modified: 2026-08-27
// Author: UltraCanvas Framework
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace TextWrapping {

    // ===== OPTIONS =====
    struct Options {
        int lineWidth      = 0;   // px available per line (<= 0: nothing wraps)
        int maxLines       = 1;   // lines the text may use
        // A word is split across two lines only when both pieces are longer
        // than this many characters; 0 breaks a word wherever it stops fitting.
        int breakTolerance = 3;
        // How far a line may exceed lineWidth to keep a word — or the whole
        // last line — in one piece. 0 keeps every line strictly inside.
        int overflowSlack  = 0;
    };

    // ===== UTF-8 HELPERS =====
    // Byte offset of every UTF-8 code point start in `s`, plus s.size() as the
    // closing boundary: cutting on one never splits a multibyte sequence.
    // Index i of the result addresses the prefix s[0, b[i]) — so i is also the
    // length of that prefix in code points — and the suffix s[b[i], end).
    inline std::vector<size_t> Utf8Boundaries(const std::string& s) {
        std::vector<size_t> b;
        b.reserve(s.size() + 1);
        for (size_t i = 0; i < s.size(); ++i) {
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) b.push_back(i);
        }
        b.push_back(s.size());
        return b;
    }

    // Code points in s[from, to).
    inline size_t Utf8Count(const std::string& s, size_t from, size_t to) {
        size_t n = 0;
        for (size_t i = from; i < to && i < s.size(); ++i) {
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
        }
        return n;
    }

    // Break the line after one of these: a name reads much better broken at
    // its own separators ("Holiday photos - Rome.jpg") than inside a word.
    inline bool IsBreakChar(char c) {
        return c == ' ' || c == '-' || c == '_' || c == '.' || c == ',' ||
               c == ';' || c == '(' || c == ')' || c == '[' || c == ']';
    }

    // Code points a wrapped text still shows (its "…" marker included, which
    // both wrap modes carry at most once, so the counts stay comparable).
    inline size_t RetainedCount(const std::vector<std::string>& lines) {
        size_t n = 0;
        for (const std::string& line : lines) n += Utf8Count(line, 0, line.size());
        return n;
    }

    // ===== SINGLE LINE =====
    // Longest prefix of `text` that fits `maxWidth` with a trailing "…".
    // Binary search over the code point boundaries (the fit is monotone in the
    // prefix length): trimming one code point at a time measured the text once
    // per removed character, which a long name in a narrow column paid for on
    // every frame.
    template <class Measure>
    std::string Ellipsize(Measure&& measure, const std::string& text, int maxWidth) {
        if (maxWidth <= 0) return "";
        if (measure(text) <= maxWidth) return text;
        std::vector<size_t> bounds = Utf8Boundaries(text);
        size_t lo = 0, hi = bounds.size() - 1;      // prefix is text[0, bounds[i])
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (measure(text.substr(0, bounds[mid]) + "…") <= maxWidth) lo = mid;
            else hi = mid - 1;
        }
        if (lo == 0) return "…";
        return text.substr(0, bounds[lo]) + "…";
    }

    // Same search without the marker: in a page preview the "…" would be most
    // of what a narrow spreadsheet column has room for.
    template <class Measure>
    std::string Truncate(Measure&& measure, const std::string& text, int maxWidth) {
        if (maxWidth <= 0) return "";
        if (measure(text) <= maxWidth) return text;
        std::vector<size_t> bounds = Utf8Boundaries(text);
        size_t lo = 0, hi = bounds.size() - 1;
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (measure(text.substr(0, bounds[mid])) <= maxWidth) lo = mid;
            else hi = mid - 1;
        }
        return text.substr(0, bounds[lo]);
    }

    // ===== GREEDY WRAP =====
    // Fills each line as far as it goes before breaking. `outTruncated` reports
    // whether anything had to be dropped: what does not fit in `maxLines` is
    // cut from the *front* of the last line, which then opens with "…", so the
    // end of a file name — its extension — stays readable.
    template <class Measure>
    std::vector<std::string> WrapGreedy(Measure&& measure, const std::string& text,
                                        const Options& opt, bool* outTruncated = nullptr) {
        if (outTruncated) *outTruncated = false;
        std::vector<std::string> lines;
        if (opt.lineWidth <= 0 || text.empty()) return lines;

        const int    lineWidth = opt.lineWidth;
        const int    maxLines  = std::max(1, opt.maxLines);
        const int    slack     = std::max(0, opt.overflowSlack);
        const size_t tolerance = opt.breakTolerance > 0
                                 ? static_cast<size_t>(opt.breakTolerance) : 0;

        std::string rest = text;
        for (int line = 0; line < maxLines && !rest.empty(); ++line) {
            std::vector<size_t> bounds = Utf8Boundaries(rest);
            const bool lastLine = (line == maxLines - 1);

            if (lastLine) {
                // A last line a hair too wide is kept whole rather than opened
                // with "…" — the slack is the free inset around the caption.
                if (measure(rest) <= lineWidth + slack) {
                    lines.push_back(rest);
                    break;
                }
                // Longest tail that fits behind a leading "…" (the shorter the
                // tail the narrower the line, so the fit is monotone in `lo`).
                size_t lo = 0, hi = bounds.size() - 1;
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    if (measure("…" + rest.substr(bounds[mid])) <= lineWidth) hi = mid;
                    else lo = mid + 1;
                }
                lines.push_back("…" + rest.substr(bounds[lo]));
                if (outTruncated) *outTruncated = true;
                break;
            }

            // Longest prefix that still fits this line.
            size_t lo = 0, hi = bounds.size() - 1;
            while (lo < hi) {
                size_t mid = (lo + hi + 1) / 2;
                if (measure(rest.substr(0, bounds[mid])) <= lineWidth) lo = mid;
                else hi = mid - 1;
            }
            size_t fit = bounds[lo];
            // Not even one code point fits: take one anyway so the loop always
            // makes progress (a caption this narrow is unreadable regardless).
            if (fit == 0) {
                fit = bounds.size() > 1 ? bounds[1] : rest.size();
                lo = bounds.size() > 1 ? 1 : lo;
            }

            size_t cut = fit;
            if (fit < rest.size() && !IsBreakChar(rest[fit])) {
                // The line ends inside a word. Where does that word end, and
                // how much of it would have to move to the next line?
                size_t wordEnd = fit;
                while (wordEnd < rest.size() && !IsBreakChar(rest[wordEnd])) ++wordEnd;
                // The separator behind the word closes the line (break chars
                // are ASCII, so stepping over one byte is safe).
                const size_t wholeWord = wordEnd < rest.size() ? wordEnd + 1 : wordEnd;
                const size_t pushedDown = Utf8Count(rest, fit, wordEnd);

                // Last separator on the line — where the word would start over
                // if it moved down as a whole.
                size_t sep = 0;
                for (size_t i = fit; i > 0; --i) {
                    if (IsBreakChar(rest[i - 1])) { sep = i; break; }
                }
                const size_t keptHere = Utf8Count(rest, sep, fit);

                // 1. A word missing only a character or three fits the slack:
                //    pull it up rather than break it. "Logo CoderBox" / "with
                //    text.png" instead of "Logo CoderBo" / "x with text.png",
                //    which gained the first line a single character.
                std::string whole = rest.substr(0, wholeWord);
                while (!whole.empty() && whole.back() == ' ') whole.pop_back();
                const bool pullUp = tolerance > 0 && slack > 0 &&
                                    pushedDown <= tolerance &&
                                    measure(whole) <= lineWidth + slack;
                // 2. A break leaving a stub of `tolerance` characters or fewer
                //    on either side is not worth making at all.
                const bool stub = tolerance > 0 &&
                                  (keptHere <= tolerance || pushedDown <= tolerance);

                if (pullUp) {
                    cut = wholeWord;
                } else if (sep > 0 && (stub || sep * 2 >= fit)) {
                    // The word moves to the next line whole. (Without the stub
                    // rule the line still backs off to a separator sitting in
                    // its back half, so a break never leaves half a line empty.)
                    cut = sep;
                } else if (stub && pushedDown <= tolerance && lo > 0) {
                    // Nothing to fall back on — the word owns the whole line —
                    // so the break moves left inside the word instead, leaving
                    // the next line a readable piece rather than one character.
                    const size_t giveBack = tolerance + 1 - pushedDown;
                    if (lo > giveBack && lo - giveBack > tolerance)
                        cut = bounds[lo - giveBack];
                }
            }

            std::string head = rest.substr(0, cut);
            rest.erase(0, cut);
            while (!head.empty() && head.back() == ' ') head.pop_back();
            while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
            if (!head.empty()) lines.push_back(head);
        }
        return lines;
    }

    // ===== BALANCED WRAP =====
    // WrapGreedy, then two corrections:
    //   * when keeping words whole cost part of the text, the text is re-broken
    //     with mid-word breaks allowed — a readable break is worth less than
    //     the characters it would drop;
    //   * text that fits its lines completely is re-broken at the smallest line
    //     width that still needs no extra line, which evens the lines out
    //     ("CoderBox" / "compiler.png" rather than the greedy "CoderBox
    //     compiler" / ".png") while the line count stays exactly the same.
    template <class Measure>
    std::vector<std::string> Wrap(Measure&& measure, const std::string& text,
                                  const Options& opt, bool* outTruncated = nullptr) {
        if (outTruncated) *outTruncated = false;
        std::vector<std::string> lines;
        if (opt.lineWidth <= 0 || text.empty()) return lines;

        Options o = opt;
        o.maxLines = std::max(1, opt.maxLines);
        const int slack = std::max(0, o.overflowSlack);

        const int totalWidth = measure(text);
        if (totalWidth <= o.lineWidth + slack) {
            lines.push_back(text);
            return lines;                       // the common case: one measure
        }
        if (o.maxLines == 1) {
            lines.push_back(Ellipsize(measure, text, o.lineWidth));
            if (outTruncated) *outTruncated = true;
            return lines;
        }

        bool truncated = false;
        lines = WrapGreedy(measure, text, o, &truncated);
        if (truncated && o.breakTolerance > 0) {
            // Keeping the words whole cost part of the text: re-break it with
            // mid-word breaks allowed and keep that instead when it fits the
            // lines, or still shows more of the text than the tidy break did.
            Options relaxed = o;
            relaxed.breakTolerance = 0;
            bool cut = false;
            std::vector<std::string> alt = WrapGreedy(measure, text, relaxed, &cut);
            if (!cut || RetainedCount(alt) > RetainedCount(lines)) {
                lines = std::move(alt);
                truncated = cut;
                o = relaxed;                    // balance in the same mode
            }
        }
        if (outTruncated) *outTruncated = truncated;

        if (!truncated && lines.size() >= 2) {
            const size_t lineCount = lines.size();
            // No re-break can make every line narrower than the average.
            Options probe = o;
            int lo = std::max(1, std::min(o.lineWidth,
                                          totalWidth / static_cast<int>(lineCount)));
            int hi = o.lineWidth;
            while (lo < hi) {
                const int mid = lo + (hi - lo) / 2;
                probe.lineWidth = mid;
                bool cut = false;
                const size_t n = WrapGreedy(measure, text, probe, &cut).size();
                if (!cut && n <= lineCount) hi = mid;
                else lo = mid + 1;
            }
            if (hi < o.lineWidth) {
                probe.lineWidth = hi;
                bool cut = false;
                std::vector<std::string> balanced = WrapGreedy(measure, text, probe, &cut);
                if (!cut && balanced.size() <= lineCount) lines = std::move(balanced);
            }
        }
        return lines;
    }

    // Lines `text` needs at `opt` (1..maxLines). Balancing never changes the
    // line count, so the cheaper greedy pass answers this.
    template <class Measure>
    int LineCount(Measure&& measure, const std::string& text, const Options& opt) {
        const int maxLines = std::max(1, opt.maxLines);
        if (opt.lineWidth <= 0 || maxLines == 1 || text.empty()) return 1;
        if (measure(text) <= opt.lineWidth + std::max(0, opt.overflowSlack)) return 1;
        const int n = static_cast<int>(WrapGreedy(measure, text, opt, nullptr).size());
        return std::min(maxLines, std::max(1, n));
    }

}  // namespace TextWrapping
}  // namespace UltraCanvas
