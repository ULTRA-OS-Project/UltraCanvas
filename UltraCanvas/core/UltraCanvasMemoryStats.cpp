// core/UltraCanvasMemoryStats.cpp
// Implementation of the scoped & categorized memory tracker.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#include "UltraCanvasMemoryStats.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <unordered_map>

#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace UltraCanvas {

namespace {
    // Demangle a typeid().name() into something a human (and a task manager)
    // can read. No-op on toolchains without the Itanium ABI demangler.
    std::string Demangle(const char* raw) {
        if (!raw) return std::string();
#if defined(__GNUG__)
        int status = 0;
        char* out = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
        if (status == 0 && out) {
            std::string result(out);
            std::free(out);
            return result;
        }
#endif
        return std::string(raw);
    }

    // Key identifying one (scope, category, type) accumulation bucket.
    struct BucketKey {
        MemoryScopeId  scope;
        MemoryCategory category;
        uint32_t       typeId;
        bool operator==(const BucketKey& o) const {
            return scope == o.scope && category == o.category && typeId == o.typeId;
        }
    };
    struct BucketKeyHash {
        std::size_t operator()(const BucketKey& k) const {
            std::size_t h = std::hash<uint64_t>{}(
                (static_cast<uint64_t>(k.scope) << 32) ^
                (static_cast<uint64_t>(k.typeId)));
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(k.category)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct Bucket {
        int64_t bytes      = 0;
        int64_t liveCount  = 0;
        int64_t totalCount = 0; // cumulative allocations (churn)
        int64_t peakBytes  = 0;
    };

    struct ScopeInfo {
        std::string name;
        bool        alive      = true;
        int64_t     hostBytes  = 0; // heap (excludes GLResource)
        int64_t     gpuBytes   = 0; // GLResource
        int64_t     peakBytes  = 0;
    };

    bool IsGpuCategory(MemoryCategory c) { return c == MemoryCategory::GLResource; }
} // namespace

// ===== Impl =====
struct UltraCanvasMemoryTracker::Impl {
    mutable std::mutex mutex;

    std::unordered_map<MemoryScopeId, ScopeInfo> scopes;
    std::unordered_map<BucketKey, Bucket, BucketKeyHash> buckets;

    // Type interning: stable small id + demangled display name per raw name.
    std::unordered_map<std::string, uint32_t> typeIds;
    std::vector<std::string> typeNames; // indexed by typeId

    MemoryScopeId nextScopeId = 1; // 0 is GlobalScope
    int64_t hostTotal = 0;
    int64_t gpuTotal  = 0;
    std::atomic<uint64_t> generation{0};

    // Subscribers, dispatched off the hot path.
    std::unordered_map<uint64_t, std::function<void()>> subscribers;
    uint64_t nextSubToken = 1;
    std::atomic<bool> dirtySinceDispatch{false};

    uint32_t InternType(const char* rawName) {
        std::string key = rawName ? rawName : "";
        auto it = typeIds.find(key);
        if (it != typeIds.end()) return it->second;
        uint32_t id = static_cast<uint32_t>(typeNames.size());
        typeNames.push_back(Demangle(rawName));
        typeIds.emplace(std::move(key), id);
        return id;
    }
};

// Function-local thread-local ambient scope stack. Lives here (not in Impl) so
// it is genuinely per-thread regardless of how many trackers exist (there is
// only one, but this keeps the semantics obvious).
static std::vector<MemoryScopeId>& AmbientStack() {
    static thread_local std::vector<MemoryScopeId> stack;
    return stack;
}

UltraCanvasMemoryTracker::UltraCanvasMemoryTracker() : impl_(std::make_unique<Impl>()) {
    // GlobalScope always exists.
    impl_->scopes[GlobalScope] = ScopeInfo{"Global", true, 0, 0, 0};
}

UltraCanvasMemoryTracker& UltraCanvasMemoryTracker::Get() {
    static UltraCanvasMemoryTracker instance;
    return instance;
}

MemoryScopeId UltraCanvasMemoryTracker::CreateScope(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    MemoryScopeId id = impl_->nextScopeId++;
    impl_->scopes[id] = ScopeInfo{name, true, 0, 0, 0};
    return id;
}

void UltraCanvasMemoryTracker::DestroyScope(MemoryScopeId id) {
    if (id == GlobalScope) return; // never destroy the catch-all
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->scopes.find(id);
    if (it == impl_->scopes.end()) return;

    if (it->second.hostBytes != 0 || it->second.gpuBytes != 0) {
        std::cerr << "[UltraCanvas][MemoryStats] Scope '" << it->second.name
                  << "' (id=" << id << ") destroyed with "
                  << it->second.hostBytes << " host + " << it->second.gpuBytes
                  << " GPU bytes still live - likely a retained reference / leak.\n";
    }

    // Reclaim its buckets and subtract from the running totals.
    for (auto bit = impl_->buckets.begin(); bit != impl_->buckets.end();) {
        if (bit->first.scope == id) {
            if (IsGpuCategory(bit->first.category)) impl_->gpuTotal -= bit->second.bytes;
            else                                    impl_->hostTotal -= bit->second.bytes;
            bit = impl_->buckets.erase(bit);
        } else {
            ++bit;
        }
    }
    impl_->scopes.erase(it);
    impl_->generation.fetch_add(1, std::memory_order_relaxed);
    impl_->dirtySinceDispatch.store(true, std::memory_order_relaxed);
}

std::string UltraCanvasMemoryTracker::ScopeName(MemoryScopeId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->scopes.find(id);
    return it != impl_->scopes.end() ? it->second.name : std::string();
}

void UltraCanvasMemoryTracker::PushScope(MemoryScopeId id) { AmbientStack().push_back(id); }
void UltraCanvasMemoryTracker::PopScope() {
    auto& s = AmbientStack();
    if (!s.empty()) s.pop_back();
}
MemoryScopeId UltraCanvasMemoryTracker::CurrentScope() const {
    auto& s = AmbientStack();
    return s.empty() ? GlobalScope : s.back();
}

void UltraCanvasMemoryTracker::Record(MemoryScopeId scope, MemoryCategory category,
                                      std::type_index /*type*/, const char* rawTypeName,
                                      int64_t byteDelta, int64_t countDelta) {
    MemoryScopeId resolved = (scope == kUseAmbientScope) ? CurrentScope() : scope;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Attribute to the resolved scope; fall back to Global if it was destroyed.
    auto sit = impl_->scopes.find(resolved);
    if (sit == impl_->scopes.end()) {
        resolved = GlobalScope;
        sit = impl_->scopes.find(resolved);
    }

    uint32_t typeId = impl_->InternType(rawTypeName);
    BucketKey key{resolved, category, typeId};
    Bucket& b = impl_->buckets[key];
    b.bytes     += byteDelta;
    b.liveCount += countDelta;
    if (byteDelta > 0) b.totalCount += (countDelta > 0 ? countDelta : 1);
    if (b.bytes > b.peakBytes) b.peakBytes = b.bytes;

    ScopeInfo& si = sit->second;
    if (IsGpuCategory(category)) {
        si.gpuBytes  += byteDelta;
        impl_->gpuTotal += byteDelta;
    } else {
        si.hostBytes += byteDelta;
        impl_->hostTotal += byteDelta;
    }
    int64_t scopeTotal = si.hostBytes + si.gpuBytes;
    if (scopeTotal > si.peakBytes) si.peakBytes = scopeTotal;

    impl_->generation.fetch_add(1, std::memory_order_relaxed);
    impl_->dirtySinceDispatch.store(true, std::memory_order_relaxed);
}

int64_t UltraCanvasMemoryTracker::BytesForScope(MemoryScopeId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->scopes.find(id);
    return it != impl_->scopes.end() ? (it->second.hostBytes + it->second.gpuBytes) : 0;
}

int64_t UltraCanvasMemoryTracker::HostTotalBytes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->hostTotal;
}

int64_t UltraCanvasMemoryTracker::GpuTotalBytes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->gpuTotal;
}

std::vector<std::pair<MemoryScopeId, std::string>> UltraCanvasMemoryTracker::ListScopes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::pair<MemoryScopeId, std::string>> out;
    out.reserve(impl_->scopes.size());
    for (const auto& [id, info] : impl_->scopes) out.emplace_back(id, info.name);
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

uint64_t UltraCanvasMemoryTracker::Generation() const {
    return impl_->generation.load(std::memory_order_relaxed);
}

MemoryReport UltraCanvasMemoryTracker::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    MemoryReport report;
    report.hostTotalBytes = impl_->hostTotal;
    report.gpuTotalBytes  = impl_->gpuTotal;
    report.generation     = impl_->generation.load(std::memory_order_relaxed);
    report.timestamp      = std::chrono::steady_clock::now();

    // Build one MemoryScopeStats per live scope, keyed by id for fast fill.
    std::unordered_map<MemoryScopeId, std::size_t> indexOf;
    report.scopes.reserve(impl_->scopes.size());
    for (const auto& [id, info] : impl_->scopes) {
        indexOf[id] = report.scopes.size();
        MemoryScopeStats s;
        s.id         = id;
        s.name       = info.name;
        s.totalBytes = info.hostBytes;
        s.gpuBytes   = info.gpuBytes;
        s.peakBytes  = info.peakBytes;
        report.scopes.push_back(std::move(s));
    }

    // Accumulate per-category rows and collect per-type rows.
    for (const auto& [key, bucket] : impl_->buckets) {
        auto iit = indexOf.find(key.scope);
        if (iit == indexOf.end()) continue;
        MemoryScopeStats& s = report.scopes[iit->second];

        // by category
        auto cit = std::find_if(s.byCategory.begin(), s.byCategory.end(),
            [&](const MemoryCategoryRow& r) { return r.category == key.category; });
        if (cit == s.byCategory.end()) {
            s.byCategory.push_back(MemoryCategoryRow{key.category, 0, 0, 0});
            cit = std::prev(s.byCategory.end());
        }
        cit->bytes     += bucket.bytes;
        cit->liveCount += bucket.liveCount;
        cit->peakBytes += bucket.peakBytes;

        // by type
        MemoryTypeRow row;
        row.typeName  = (key.typeId < impl_->typeNames.size())
                            ? impl_->typeNames[key.typeId] : std::string("<unknown>");
        row.category  = key.category;
        row.bytes     = bucket.bytes;
        row.liveCount = bucket.liveCount;
        row.peakBytes = bucket.peakBytes;
        s.byType.push_back(std::move(row));
    }

    // Sort rows by bytes desc, and scopes by total desc (Global last for
    // stable, readable output).
    for (auto& s : report.scopes) {
        std::sort(s.byCategory.begin(), s.byCategory.end(),
                  [](const MemoryCategoryRow& a, const MemoryCategoryRow& b) { return a.bytes > b.bytes; });
        std::sort(s.byType.begin(), s.byType.end(),
                  [](const MemoryTypeRow& a, const MemoryTypeRow& b) { return a.bytes > b.bytes; });
    }
    std::sort(report.scopes.begin(), report.scopes.end(),
              [](const MemoryScopeStats& a, const MemoryScopeStats& b) {
                  if (a.id == GlobalScope) return false;
                  if (b.id == GlobalScope) return true;
                  return (a.totalBytes + a.gpuBytes) > (b.totalBytes + b.gpuBytes);
              });
    return report;
}

UltraCanvasMemoryTracker::ChangeToken
UltraCanvasMemoryTracker::Subscribe(std::function<void()> onChanged) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    uint64_t token = impl_->nextSubToken++;
    impl_->subscribers.emplace(token, std::move(onChanged));
    return token;
}

void UltraCanvasMemoryTracker::Unsubscribe(ChangeToken token) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->subscribers.erase(token);
}

void UltraCanvasMemoryTracker::DispatchPendingNotifications() {
    if (!impl_->dirtySinceDispatch.exchange(false, std::memory_order_relaxed)) return;
    // Copy callbacks out so we never hold the lock while invoking user code
    // (which may allocate and re-enter Record()).
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        callbacks.reserve(impl_->subscribers.size());
        for (const auto& [token, cb] : impl_->subscribers) callbacks.push_back(cb);
    }
    for (const auto& cb : callbacks) if (cb) cb();
}

// ===== Raw buffer hooks =====
MemoryScopeId TrackRawAlloc(MemoryCategory category, int64_t bytes,
                            const char* typeName, MemoryScopeId scope) {
#if UC_ENABLE_MEMORY_STATS
    // Resolve the ambient scope to a concrete id and return it so the caller
    // can refund against the same scope later.
    MemoryScopeId resolved = (scope == kUseAmbientScope)
                                 ? UltraCanvasMemoryTracker::Get().CurrentScope() : scope;
    if (bytes <= 0) return resolved;
    UltraCanvasMemoryTracker::Get().Record(
        resolved, category, std::type_index(typeid(void)), typeName, bytes, +1);
    return resolved;
#else
    (void)category; (void)bytes; (void)typeName;
    return scope == kUseAmbientScope ? GlobalScope : scope;
#endif
}

void TrackRawFree(MemoryCategory category, int64_t bytes,
                  const char* typeName, MemoryScopeId scope) {
#if UC_ENABLE_MEMORY_STATS
    if (bytes <= 0) return;
    UltraCanvasMemoryTracker::Get().Record(
        scope, category, std::type_index(typeid(void)), typeName, -bytes, -1);
#else
    (void)category; (void)bytes; (void)typeName; (void)scope;
#endif
}

// ===== Public read API =====
namespace MemoryStats {

bool IsEnabled() {
#if UC_ENABLE_MEMORY_STATS
    return true;
#else
    return false;
#endif
}

MemoryReport Snapshot()                       { return UltraCanvasMemoryTracker::Get().Snapshot(); }
int64_t BytesForScope(MemoryScopeId id)       { return UltraCanvasMemoryTracker::Get().BytesForScope(id); }
int64_t HostTotalBytes()                      { return UltraCanvasMemoryTracker::Get().HostTotalBytes(); }
int64_t GpuTotalBytes()                       { return UltraCanvasMemoryTracker::Get().GpuTotalBytes(); }
std::vector<std::pair<MemoryScopeId, std::string>> ListScopes() { return UltraCanvasMemoryTracker::Get().ListScopes(); }
MemoryScopeId CreateScope(const std::string& name) { return UltraCanvasMemoryTracker::Get().CreateScope(name); }
void DestroyScope(MemoryScopeId id)           { UltraCanvasMemoryTracker::Get().DestroyScope(id); }
uint64_t Generation()                         { return UltraCanvasMemoryTracker::Get().Generation(); }
ChangeToken Subscribe(std::function<void()> cb){ return UltraCanvasMemoryTracker::Get().Subscribe(std::move(cb)); }
void Unsubscribe(ChangeToken token)           { UltraCanvasMemoryTracker::Get().Unsubscribe(token); }
void DispatchPendingNotifications()           { UltraCanvasMemoryTracker::Get().DispatchPendingNotifications(); }

std::string CategoryName(MemoryCategory category) {
    switch (category) {
        case MemoryCategory::UIElement:      return "UI Elements";
        case MemoryCategory::ImagePixels:    return "Image Pixels";
        case MemoryCategory::VectorGeometry: return "Vector Geometry";
        case MemoryCategory::TextBuffer:     return "Text Buffers";
        case MemoryCategory::VideoFrame:     return "Video Frames";
        case MemoryCategory::AudioBuffer:    return "Audio Buffers";
        case MemoryCategory::DocumentData:   return "Document Data";
        case MemoryCategory::GLResource:     return "GPU Resources";
        case MemoryCategory::Cache:          return "Caches";
        case MemoryCategory::Other:          return "Other";
        case MemoryCategory::Unknown:        return "Unknown";
        default:                             return "Unknown";
    }
}

std::string FormatBytes(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    bool negative = value < 0;
    if (negative) value = -value;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }

    char buf[48];
    if (unit == 0) std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
    else           std::snprintf(buf, sizeof(buf), "%s%.1f %s", negative ? "-" : "", value, units[unit]);
    return std::string(buf);
}

} // namespace MemoryStats
} // namespace UltraCanvas
