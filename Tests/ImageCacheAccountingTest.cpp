// Tests/ImageCacheAccountingTest.cpp
// UCCache (UltraCanvasUtils.h) keeps a running total of the bytes it holds and
// evicts against it. This test is about that total staying true, because when
// it does not the cache does not merely mis-report — it stops working.
//
// `currentCacheSize` is a size_t. Subtracting more than was added wraps it to
// an enormous number, every later insert then finds itself over budget, and
// the eviction loop empties the whole cache to make room for one entry. From
// the outside that looks like a cache that is permanently full: it holds one
// item, every image is decoded again on every use, and the Filer's thumbnails
// take so long to come back that they look as if they never do.
//
// Two ways the total used to come apart, both covered below:
//
//   1. A payload that GROWS after it is stored. UCImageRaster::GetDataSize()
//      counts a lazily decoded animation and UCSvgDocument::GetMemoryBytes()
//      counts pages rasterized on demand, so an entry asked for its size at
//      eviction time answers with MORE than it was ever charged. That is the
//      underflow. The cache now asks once, when the entry is stored.
//
//   2. The same key stored TWICE. Four thumbnail workers can miss on one
//      picture at the same moment and all decode it; each one then stores its
//      result. Each insert used to add its bytes while only one entry existed
//      to give them back.
//
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasUtils.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// A payload whose reported size can change after it has been cached — an
// image that decodes its animation later, an SVG that rasterizes a page.
struct GrowingPayload {
    size_t bytes = 0;
    explicit GrowingPayload(size_t initial) : bytes(initial) {}
};

struct GrowingEntry {
    std::shared_ptr<GrowingPayload> payload;
    std::chrono::steady_clock::time_point lastAccess;
    size_t GetEntrySize() { return payload->bytes; }
};

using GrowingCache = UCCache<GrowingPayload, GrowingEntry>;

std::shared_ptr<GrowingPayload> Payload(size_t bytes) {
    return std::make_shared<GrowingPayload>(bytes);
}

// ===== 1. A PAYLOAD THAT GROWS AFTER IT WAS STORED =====
void TestGrowthCannotUnderflow() {
    std::cout << "\nA payload that grows after it is cached:\n";

    GrowingCache cache(10000);
    auto small = Payload(1000);
    cache.AddToCache("grower", small);
    Check(cache.GetCurrentCacheSize() == 1000,
          "stored at the size it reported when it was stored");

    // The animation decodes; the payload is now far bigger than the whole
    // budget. Nothing tells the cache, and nothing should have to.
    small->bytes = 9'000'000;

    Check(cache.RemoveFromCache("grower"), "the grown entry is removed");
    Check(cache.GetCurrentCacheSize() == 0,
          "removing it gives back exactly what it was charged");
    Check(cache.GetEntryCount() == 0, "and the entry is gone");

    // The proof that the total is still usable: the cache still caches.
    cache.AddToCache("after", Payload(500));
    Check(cache.GetEntryCount() == 1 && cache.GetCurrentCacheSize() == 500,
          "the cache still holds what is put in it afterwards");
}

void TestGrowthDoesNotEmptyTheCacheOnEviction() {
    std::cout << "\nA grown entry evicted to make room for another:\n";

    GrowingCache cache(3000);
    auto grower = Payload(1000);
    cache.AddToCache("a", grower);
    // Touched in order, so "a" is the least recently used.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    cache.AddToCache("b", Payload(1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    grower->bytes = 50'000'000;     // "a" decodes its animation

    // Over budget: "a" goes. Before the fix this returned 50 MB against a
    // 1 kB charge, the total wrapped, and every entry after it was evicted
    // on arrival.
    cache.AddToCache("c", Payload(1500));

    Check(cache.GetCurrentCacheSize() <= 3000,
          "the total stays inside the budget it was given");
    Check(cache.GetFromCache("c") != nullptr,
          "the entry that caused the eviction is still cached");
    Check(cache.GetFromCache("b") != nullptr,
          "and so is the one that was not the oldest");
    Check(cache.GetFromCache("a") == nullptr, "the oldest is the one that went");

    // The real regression: one eviction must not leave the cache unable to
    // hold anything ever again.
    for (int i = 0; i < 20; ++i) {
        cache.AddToCache("fill" + std::to_string(i), Payload(200));
    }
    Check(cache.GetEntryCount() > 1,
          "the cache still holds more than a single entry afterwards");
    Check(cache.GetCurrentCacheSize() <= 3000,
          "and is still inside its budget");
}

// ===== 2. THE SAME KEY STORED TWICE =====
void TestOverwriteIsNotCountedTwice() {
    std::cout << "\nThe same key stored more than once:\n";

    GrowingCache cache(1'000'000);
    cache.AddToCache("same", Payload(400));
    cache.AddToCache("same", Payload(400));
    cache.AddToCache("same", Payload(400));

    Check(cache.GetEntryCount() == 1, "one key is one entry");
    Check(cache.GetCurrentCacheSize() == 400,
          "charged once, not once per store");

    // A replacement of a different size settles at the new size, not the sum.
    cache.AddToCache("same", Payload(1200));
    Check(cache.GetCurrentCacheSize() == 1200,
          "replacing an entry charges the replacement, not both");

    cache.RemoveFromCache("same");
    Check(cache.GetCurrentCacheSize() == 0,
          "and removing it leaves nothing behind");
}

void TestRepeatedOverwritesDoNotDrift() {
    std::cout << "\nA key rewritten many times (four workers, one picture):\n";

    GrowingCache cache(100'000);
    for (int i = 0; i < 500; ++i) {
        cache.AddToCache("hot", Payload(1000));
    }
    Check(cache.GetEntryCount() == 1, "still one entry after 500 stores");
    Check(cache.GetCurrentCacheSize() == 1000,
          "and still charged for exactly one");

    // The old code drifted up by 1000 per store: 500 stores put the total at
    // 500 kB against a 100 kB budget, and every insert after that emptied the
    // cache. The check that matters is that the cache still caches.
    cache.AddToCache("other", Payload(1000));
    Check(cache.GetFromCache("hot") != nullptr && cache.GetFromCache("other") != nullptr,
          "a second key is still cached alongside the first");
}

// ===== EVERY PATH OUT OF AN ENTRY GIVES ITS BYTES BACK =====
void TestEveryRemovalPathBalances() {
    std::cout << "\nEvery way an entry can leave:\n";

    GrowingCache cache(1'000'000);
    cache.AddToCache("pic.png?64", Payload(300));
    cache.AddToCache("pic.png?128", Payload(500));
    cache.AddToCache("other.png?64", Payload(700));
    Check(cache.GetCurrentCacheSize() == 1500, "three entries, summed");

    Check(cache.RemoveFromCacheByPrefix("pic.png?") == 2,
          "the prefix removal takes both sizes of the one picture");
    Check(cache.GetCurrentCacheSize() == 700,
          "and gives back exactly their bytes");
    Check(cache.GetEntryCount() == 1, "leaving the unrelated entry alone");

    Check(!cache.RemoveFromCache("not there"),
          "removing a key that is not there reports so");
    Check(cache.GetCurrentCacheSize() == 700, "and changes nothing");

    cache.ClearCache();
    Check(cache.GetCurrentCacheSize() == 0 && cache.GetEntryCount() == 0,
          "clearing empties both the map and the total");
}

void TestBudgetIsHonoured() {
    std::cout << "\nThe budget itself:\n";

    GrowingCache cache(1000);
    for (int i = 0; i < 10; ++i) {
        cache.AddToCache("e" + std::to_string(i), Payload(300));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(cache.GetCurrentCacheSize() <= 1000, "never over budget");
    Check(cache.GetEntryCount() == 3, "which is three 300-byte entries");

    // An entry larger than the whole budget is still stored — the caller
    // asked for it and there is nothing else to serve — but it must not
    // leave the total permanently poisoned.
    cache.AddToCache("huge", Payload(5000));
    Check(cache.GetEntryCount() == 1, "an over-budget entry displaces the rest");
    cache.RemoveFromCache("huge");
    Check(cache.GetCurrentCacheSize() == 0,
          "and gives all of its bytes back when it goes");
}

// ===== CONCURRENT STORES OF THE SAME KEY =====
// What four thumbnail workers actually do to one picture. The assertion is
// only that the total is consistent with what is held afterwards - which
// thread won is neither knowable nor interesting.
void TestConcurrentStoresStayBalanced() {
    std::cout << "\nFour threads storing the same keys at once:\n";

    GrowingCache cache(10'000'000);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&cache]() {
            for (int i = 0; i < 200; ++i) {
                cache.AddToCache("shared" + std::to_string(i % 10), Payload(1000));
            }
        });
    }
    for (std::thread& thread : threads) thread.join();

    Check(cache.GetEntryCount() == 10, "ten keys, whoever stored them");
    Check(cache.GetCurrentCacheSize() == 10 * 1000,
          "the total is what those ten entries are worth, not what was stored");
}

} // namespace

int main() {
    std::cout << "UCCache byte accounting\n";
    std::cout << "=======================\n";

    TestGrowthCannotUnderflow();
    TestGrowthDoesNotEmptyTheCacheOnEviction();
    TestOverwriteIsNotCountedTwice();
    TestRepeatedOverwritesDoNotDrift();
    TestEveryRemovalPathBalances();
    TestBudgetIsHonoured();
    TestConcurrentStoresStayBalanced();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}
