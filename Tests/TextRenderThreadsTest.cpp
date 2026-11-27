// Tests/TextRenderThreadsTest.cpp
// Text drawn on several threads at once - the UI thread and the Filer's
// thumbnail workers (a document's first page, a slide, a drawing) - must not
// crash. UltraFiler did on Windows: an access violation in cairo's glyph
// lookup under pango_renderer_draw_layout_line, while thumbnails were made.
// Every render context in the process shared one cache of Pango layouts, so a
// layout made on a worker, with that thread's Pango context and fonts, was
// drawn by the UI thread while the worker used it too, and the other way
// round; Pango objects are not to be shared between threads.
//
// The main thread and four workers each draw the same words, in the same
// fonts, into offscreen contexts of their own, for a few seconds, measuring
// and drawing as a tile and a page do. Passes by finishing. Headless.
//
// Then eight threads make and destroy contexts and do nothing else. Every
// context adds itself to one list of live contexts and takes itself off
// again; the list had no lock, so two threads grew and shrank it at once and
// the heap was corrupted - this test aborted in malloc now and then, on CI
// too ("malloc(): unaligned tcache chunk detected"). Unlocked, this part
// aborted in about every second run of six seconds.
// Version: 1.1.0 - contexts made and destroyed on eight threads at once
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework

#include "UltraCanvasRenderContext.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace {

const char* const kWords[] = {
    "Release", "reviews", "docs", "export", "Xara", "Image", "code source",
    "Quarterly Review", "October 2026", "Fixture Heading", "word-shapes.docx",
    "The quick brown fox jumps over the lazy dog", "Ünïcödé ÄÖÜ ß",
};
const char* const kFamilies[] = {"Sans", "Serif", "Monospace"};

// Draws every word in every family at a few sizes, measuring each first, as a
// tile's caption and a page's paragraph do. False when no context could be made.
bool DrawRound(int round) {
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(320, 240), nullptr);
    if (!ctx) return false;
    for (const char* family : kFamilies) {
        for (int size = 9; size <= 15; size += 3) {
            ctx->SetFontFace(family, (round & 1) ? FontWeight::Bold : FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(size);
            for (const char* word : kWords) {
                const Size2Di dims = ctx->GetTextLineDimensions(word);
                ctx->DrawText(word, Point2Dd(4.5 + (round % 7), 4.0 + dims.height));
            }
        }
    }
    return true;
}

} // namespace

int main() {
    std::cout << "TextRenderThreadsTest\n";
    if (!DrawRound(0)) {
        std::cout << "SKIP: no offscreen render context in this build\n";
        return 0;
    }

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    std::atomic<long> rounds{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t] {
            int round = t;
            while (std::chrono::steady_clock::now() < until) {
                if (!DrawRound(round++)) { failed = true; return; }
                ++rounds;
            }
        });
    }
    // The main thread draws too, as the UI thread does while the workers run.
    int round = 100;
    while (std::chrono::steady_clock::now() < until) {
        if (!DrawRound(round++)) failed = true;
        ++rounds;
    }
    for (std::thread& w : workers) w.join();

    std::cout << "  " << rounds.load() << " rounds of text drawn on 5 threads at once\n";
    if (failed) {
        std::cout << "FAIL: a render context could not be made\n";
        return 1;
    }

    // Contexts made and destroyed, nothing drawn: the list of live contexts
    // is all these threads share.
    const auto churnUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::atomic<long> made{0};
    std::vector<std::thread> churners;
    for (int t = 0; t < 8; ++t) {
        churners.emplace_back([&] {
            while (std::chrono::steady_clock::now() < churnUntil) {
                std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(16, 16), nullptr);
                if (!ctx) { failed = true; return; }
                ++made;
            }
        });
    }
    for (std::thread& c : churners) c.join();
    std::cout << "  " << made.load() << " contexts made and destroyed on 8 threads at once\n";
    if (failed) {
        std::cout << "FAIL: a render context could not be made\n";
        return 1;
    }
    std::cout << "All passed\n";
    return 0;
}
