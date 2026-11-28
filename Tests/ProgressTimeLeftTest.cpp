// Tests/ProgressTimeLeftTest.cpp
// The progress window's time left (UltraCanvasProgressDialog::
// EstimateSecondsLeft over CombinedProgress) comes from the whole run so far:
// the share of the items and of the bytes done, and the time that took. It
// used to come from the last three seconds' byte speed, and a copy of many
// small files - 44 000 of them in about ten minutes - read anywhere from a
// few seconds to two hours as the files' sizes went by.
//
//   * the sums: progress from items and bytes, each half; the one total
//     known; the ring's reading when there is none;
//   * no estimate before two seconds of work, 0 at the end, a run that began
//     part way through (a window opened again) counted from where it began;
//   * the copy of 44 000 files of very different sizes, second by second:
//     after the first 5 % the estimate is never more than 50 % off the time
//     that was really left, and never shows hours.
// Headless.
// Version: 1.0.0
// Last Modified: 2026-11-28
// Author: UltraCanvas Framework

#include "UltraCanvasProgressDialog.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance; }

void TestSums() {
    std::cout << "\n-- how far the work is --\n";
    ProgressFigures f;
    f.totalItems = 100;
    f.doneItems = 50;
    f.totalBytes = 1000;
    f.doneBytes = 100;
    Check(Near(UltraCanvasProgressDialog::CombinedProgress(f, 0.9), 0.3),
          "items and bytes count half each (50 % and 10 % make 30 %)");
    f.totalBytes = 0;
    Check(Near(UltraCanvasProgressDialog::CombinedProgress(f, 0.9), 0.5), "only the items' total: the items");
    f.totalItems = 0;
    f.totalBytes = 1000;
    Check(Near(UltraCanvasProgressDialog::CombinedProgress(f, 0.9), 0.1), "only the bytes' total: the bytes");
    f.totalBytes = 0;
    Check(Near(UltraCanvasProgressDialog::CombinedProgress(f, 0.4), 0.4), "no totals: the ring's reading");
    Check(UltraCanvasProgressDialog::CombinedProgress(f, -1.0) < 0.0, "nothing to go by: -1");
}

void TestEstimate() {
    std::cout << "\n-- the estimate --\n";
    Check(UltraCanvasProgressDialog::EstimateSecondsLeft(1.0, 0.0, 0.5) < 0.0,
          "nothing shown in the first two seconds");
    Check(Near(UltraCanvasProgressDialog::EstimateSecondsLeft(60.0, 0.0, 0.25), 180.0),
          "a quarter in a minute: three minutes left");
    Check(Near(UltraCanvasProgressDialog::EstimateSecondsLeft(60.0, 0.5, 0.75), 60.0),
          "a run that began at half way: from where it began");
    Check(UltraCanvasProgressDialog::EstimateSecondsLeft(30.0, 0.0, 1.0) == 0.0, "done: nothing left");
    Check(UltraCanvasProgressDialog::EstimateSecondsLeft(30.0, 0.0, 0.0) < 0.0, "no progress yet: no estimate");
}

// The reported copy: 44 000 files, 729 MB, about ten minutes. Each file costs
// a fixed overhead plus its size at a steady disk speed; the sizes are mostly
// tiny with a few big ones, as in a source tree with its pictures.
void TestManySmallFiles() {
    std::cout << "\n-- 44 000 files in about ten minutes --\n";
    const int count = 44000;
    std::mt19937 rng(1234);
    std::lognormal_distribution<double> sizeDist(6.5, 2.2);
    std::vector<uint64_t> sizes(count);
    uint64_t totalBytes = 0;
    for (auto& s : sizes) {
        s = static_cast<uint64_t>(std::min(sizeDist(rng), 200e6));
        totalBytes += s;
    }
    const double overhead = 0.011;   // s per file
    const double speed = 25e6;       // bytes per s
    std::vector<double> finishAt(count);
    double t = 0.0;
    for (int i = 0; i < count; ++i) {
        t += overhead + static_cast<double>(sizes[i]) / speed;
        finishAt[i] = t;
    }
    const double total = t;
    std::cout << "  (" << totalBytes / 1000000 << " MB, " << static_cast<int>(total) << " s in all)\n";

    ProgressFigures f;
    f.totalItems = count;
    f.totalBytes = totalBytes;
    int next = 0;
    double worst = 0.0, longest = 0.0;
    for (double now = 1.0; now < total; now += 1.0) {
        while (next < count && finishAt[next] <= now) {
            ++f.doneItems;
            f.doneBytes += sizes[next];
            ++next;
        }
        const double progress = UltraCanvasProgressDialog::CombinedProgress(f, -1.0);
        const double left = UltraCanvasProgressDialog::EstimateSecondsLeft(now, 0.0, progress);
        if (now < 0.05 * total || left < 0.0) continue;
        const double truth = total - now;
        if (truth < 30.0) continue;   // the last seconds: rounding, not an estimate
        worst = std::max(worst, std::fabs(left - truth) / truth);
        longest = std::max(longest, left);
    }
    std::cout << "  (worst miss " << static_cast<int>(worst * 100) << " %, longest shown "
              << static_cast<int>(longest) << " s)\n";
    Check(worst <= 0.5, "after the first 5 %, never more than 50 % off the time really left");
    Check(longest < 3600.0, "never an hour or more");
}

} // namespace

int main() {
    std::cout << "=== Progress window: time left ===\n";
    TestSums();
    TestEstimate();
    TestManySmallFiles();
    std::cout << "\n" << (g_failures == 0 ? "All time-left tests passed." : "SOME TIME-LEFT TESTS FAILED.") << "\n";
    return g_failures == 0 ? 0 : 1;
}
