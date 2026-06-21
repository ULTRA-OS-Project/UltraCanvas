// include/UltraCanvasMemoryStats.h
// Scoped & categorized memory accounting for UltraCanvas.
//
// Every tracked allocation is attributed to a (scope, category, C++ type)
// triple:
//   * scope    - WHO owns the memory (e.g. a browser tab). Apps create one
//                scope per logical owner via CreateScope().
//   * category - WHAT KIND of data it is (image pixels, text, geometry ...).
//   * type     - the concrete C++ type, for fine-grained drill-down.
//
// There are three ways memory gets attributed:
//   A. Ambient scope  - a thread-local "current scope" set by MemoryScopeGuard.
//                       Everything allocated underneath inherits it.
//   B. Explicit tag   - UltraCanvasUIElementFactory::CreateTagged(tag, ...).
//   C. Raw buffers    - UC_TRACK_ALLOC / UC_TRACK_FREE for the big pixel /
//                       video / document buffers that dominate real usage.
//
// The read side (UltraCanvas::MemoryStats::) is a small, stable, public API
// that application code - a task manager, an "About > Memory" panel, tests -
// can query at runtime. It compiles and returns empty/zero results even when
// tracking is disabled, so callers never need #ifdefs.
//
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

// Compile-time master switch. Default ON so the public stats API carries real
// data out of the box (a task manager needs it). Define UC_ENABLE_MEMORY_STATS=0
// to collapse the element factory back to plain make_shared and turn the
// UC_TRACK_* macros into no-ops for a zero-overhead release build.
#ifndef UC_ENABLE_MEMORY_STATS
#define UC_ENABLE_MEMORY_STATS 1
#endif

namespace UltraCanvas {

// ===== CATEGORIES (what kind of data) =====
    enum class MemoryCategory : uint8_t {
        Unknown = 0,
        UIElement,        // the widget objects themselves
        ImagePixels,      // decoded bitmaps / UCImage backing stores
        VectorGeometry,   // SVG/CDR/XAR path & shape storage
        TextBuffer,       // text area / spreadsheet string data
        VideoFrame,       // decoded video frames
        AudioBuffer,      // PCM / sample buffers
        DocumentData,     // PDF / document page data
        GLResource,       // GPU textures, FBOs, VBOs (reported on a separate axis)
        Cache,            // glyph / image / render caches
        Other,
        Count_            // sentinel - keep last
    };

// ===== SCOPES (who owns the memory) =====
    using MemoryScopeId = uint32_t;

    // The always-present catch-all scope for anything not under a guard.
    constexpr MemoryScopeId GlobalScope = 0;
    // Sentinel meaning "resolve against the thread-local ambient scope".
    constexpr MemoryScopeId kUseAmbientScope = 0xFFFFFFFFu;

    // Attribution tag threaded through the element factory / raw hooks.
    struct MemoryTag {
        MemoryCategory category = MemoryCategory::UIElement;
        MemoryScopeId  scope    = kUseAmbientScope; // default: inherit ambient
    };

// ===== REPORT DATA TYPES (the public contract) =====
//   Flat, pointer-free, copyable - safe to hand to UI code, serialize to
//   JSON, or carry across a process boundary later.
    struct MemoryTypeRow {
        std::string    typeName;     // demangled, e.g. "UltraCanvasImageElement"
        MemoryCategory category = MemoryCategory::Unknown;
        int64_t        bytes     = 0;
        int64_t        liveCount = 0;
        int64_t        peakBytes = 0;
    };

    struct MemoryCategoryRow {
        MemoryCategory category  = MemoryCategory::Unknown;
        int64_t        bytes     = 0;
        int64_t        liveCount = 0;
        int64_t        peakBytes = 0;
    };

    struct MemoryScopeStats {
        MemoryScopeId                  id = GlobalScope;
        std::string                    name;
        int64_t                        totalBytes = 0; // host heap (excludes GLResource)
        int64_t                        gpuBytes   = 0; // GLResource within this scope
        int64_t                        peakBytes  = 0;
        std::vector<MemoryCategoryRow> byCategory; // sorted by bytes desc
        std::vector<MemoryTypeRow>     byType;     // sorted by bytes desc
    };

    struct MemoryReport {
        int64_t                       hostTotalBytes = 0; // sum of all scopes (heap)
        int64_t                       gpuTotalBytes  = 0; // sum of all scopes (GPU)
        uint64_t                      generation     = 0; // value of Generation() at capture
        std::vector<MemoryScopeStats> scopes;             // one per live scope
        std::chrono::steady_clock::time_point timestamp{};
    };

// ===== THE TRACKER (internal aggregation point) =====
// Application code should prefer the MemoryStats:: namespace below; the tracker
// is exposed because the inline TrackedAllocator template has to call Record().
    class UltraCanvasMemoryTracker {
    public:
        static UltraCanvasMemoryTracker& Get();

        // --- Scope lifecycle ---
        MemoryScopeId CreateScope(const std::string& name);
        void          DestroyScope(MemoryScopeId id); // warns if memory still live
        std::string   ScopeName(MemoryScopeId id) const;

        // --- Ambient scope stack (used by MemoryScopeGuard) ---
        void          PushScope(MemoryScopeId id);
        void          PopScope();
        MemoryScopeId CurrentScope() const;          // back of stack, or GlobalScope

        // --- Recording (called by TrackedAllocator and the UC_TRACK_* hooks) ---
        // byteDelta > 0 on allocation, < 0 on free. countDelta is +1 / -1.
        // A `scope` of kUseAmbientScope is resolved against the ambient stack.
        void Record(MemoryScopeId scope, MemoryCategory category,
                    std::type_index type, const char* rawTypeName,
                    int64_t byteDelta, int64_t countDelta);

        // --- Reporting (public read side) ---
        MemoryReport Snapshot() const;
        int64_t      BytesForScope(MemoryScopeId id) const;
        int64_t      HostTotalBytes() const;
        int64_t      GpuTotalBytes() const;
        std::vector<std::pair<MemoryScopeId, std::string>> ListScopes() const;

        // Cheap monotonically-increasing change counter. A task manager polls
        // this on its refresh timer and only rebuilds when it moves.
        uint64_t Generation() const;

        // --- Change subscription (dispatched off the hot path) ---
        using ChangeToken = uint64_t;
        ChangeToken Subscribe(std::function<void()> onChanged);
        void        Unsubscribe(ChangeToken token);
        // Fire pending change callbacks. Call from the UI loop; never called
        // on the allocation hot path, so observers can be slow safely.
        void        DispatchPendingNotifications();

    private:
        UltraCanvasMemoryTracker();
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

// ===== RAII AMBIENT SCOPE =====
// Push a scope for the duration of a block. The browser sets the active tab
// once per build/render pass; everything allocated underneath is attributed to
// it without threading a parameter through every CreateXxx() call.
    class MemoryScopeGuard {
    public:
        explicit MemoryScopeGuard(MemoryScopeId id) {
#if UC_ENABLE_MEMORY_STATS
            UltraCanvasMemoryTracker::Get().PushScope(id);
            active_ = true;
#else
            (void)id;
#endif
        }
        ~MemoryScopeGuard() {
#if UC_ENABLE_MEMORY_STATS
            if (active_) UltraCanvasMemoryTracker::Get().PopScope();
#endif
        }
        MemoryScopeGuard(const MemoryScopeGuard&) = delete;
        MemoryScopeGuard& operator=(const MemoryScopeGuard&) = delete;
    private:
        bool active_ = false;
    };

// ===== TRACKED ALLOCATOR =====
// Stateful STL allocator used with std::allocate_shared so the tracker captures
// the REAL allocation (object + shared control block in one block) and the
// concrete element type. The tag/type are preserved across the rebind that
// allocate_shared performs for the control block.
    template<class T>
    class TrackedAllocator {
    public:
        using value_type = T;

        explicit TrackedAllocator(MemoryTag tag = {},
                                  std::type_index ti = std::type_index(typeid(T)),
                                  const char* rawName = typeid(T).name()) noexcept
            : tag_(tag), typeIndex_(ti), rawName_(rawName) {}

        template<class U>
        TrackedAllocator(const TrackedAllocator<U>& o) noexcept
            : tag_(o.tag_), typeIndex_(o.typeIndex_), rawName_(o.rawName_) {}

        T* allocate(std::size_t n) {
            T* p = static_cast<T*>(::operator new(n * sizeof(T)));
#if UC_ENABLE_MEMORY_STATS
            UltraCanvasMemoryTracker::Get().Record(
                tag_.scope, tag_.category, typeIndex_, rawName_,
                static_cast<int64_t>(n * sizeof(T)), +1);
#endif
            return p;
        }

        void deallocate(T* p, std::size_t n) noexcept {
#if UC_ENABLE_MEMORY_STATS
            UltraCanvasMemoryTracker::Get().Record(
                tag_.scope, tag_.category, typeIndex_, rawName_,
                -static_cast<int64_t>(n * sizeof(T)), -1);
#endif
            ::operator delete(p);
        }

        template<class U> struct rebind { using other = TrackedAllocator<U>; };

        // Expose state to the converting constructor of other instantiations.
        template<class U> friend class TrackedAllocator;

        MemoryTag       tag_;
        std::type_index typeIndex_;
        const char*     rawName_;
    };

    template<class A, class B>
    bool operator==(const TrackedAllocator<A>&, const TrackedAllocator<B>&) noexcept { return true; }
    template<class A, class B>
    bool operator!=(const TrackedAllocator<A>&, const TrackedAllocator<B>&) noexcept { return false; }

// ===== RAW BUFFER HOOKS =====
// For the big buffers that do NOT go through the element factory: pixel data,
// decoded video frames, vector geometry, PDF page data, audio buffers, and the
// bundled C libraries. These attribute to the ambient scope by default.
//
// TrackRawAlloc resolves the ambient scope eagerly and RETURNS the concrete id;
// pass that same id back to TrackRawFree so the free is attributed to the scope
// that allocated, even if it runs later under a different (or no) ambient scope.
    MemoryScopeId TrackRawAlloc(MemoryCategory category, int64_t bytes,
                                const char* typeName, MemoryScopeId scope = kUseAmbientScope);
    void TrackRawFree(MemoryCategory category, int64_t bytes,
                      const char* typeName, MemoryScopeId scope = kUseAmbientScope);

// RAII charge for a sized buffer: records on construction, refunds on
// destruction against the SAME scope. The safe way to instrument an owned
// buffer whose lifetime crosses scope boundaries.
    class TrackedBuffer {
    public:
        TrackedBuffer() = default;
        TrackedBuffer(MemoryCategory category, int64_t bytes, const char* typeName,
                      MemoryScopeId scope = kUseAmbientScope)
            : category_(category), bytes_(bytes), typeName_(typeName) {
            scope_ = TrackRawAlloc(category_, bytes_, typeName_, scope);
        }
        ~TrackedBuffer() { Release(); }

        TrackedBuffer(TrackedBuffer&& o) noexcept { *this = std::move(o); }
        TrackedBuffer& operator=(TrackedBuffer&& o) noexcept {
            if (this != &o) {
                Release();
                category_ = o.category_; bytes_ = o.bytes_;
                typeName_ = o.typeName_; scope_ = o.scope_;
                o.bytes_ = 0;
            }
            return *this;
        }
        TrackedBuffer(const TrackedBuffer&) = delete;
        TrackedBuffer& operator=(const TrackedBuffer&) = delete;

        void Release() {
            if (bytes_ > 0) { TrackRawFree(category_, bytes_, typeName_, scope_); bytes_ = 0; }
        }

    private:
        MemoryCategory category_ = MemoryCategory::Other;
        int64_t        bytes_    = 0;
        const char*    typeName_ = "";
        MemoryScopeId  scope_    = GlobalScope;
    };

#if UC_ENABLE_MEMORY_STATS
    #define UC_TRACK_ALLOC(category, bytes, typeName) \
        ::UltraCanvas::TrackRawAlloc((category), static_cast<int64_t>(bytes), (typeName))
    #define UC_TRACK_FREE(category, bytes, typeName) \
        ::UltraCanvas::TrackRawFree((category), static_cast<int64_t>(bytes), (typeName))
#else
    #define UC_TRACK_ALLOC(category, bytes, typeName) ((void)0)
    #define UC_TRACK_FREE(category, bytes, typeName)  ((void)0)
#endif

// ===== PUBLIC READ API =====
// Stable, header-exposed surface for application "task manager" UIs. Safe to
// call from the UI thread while other threads allocate. Works (returning
// empty/zero) regardless of UC_ENABLE_MEMORY_STATS, so callers need no #ifdefs.
    namespace MemoryStats {
        // True when allocations are actually being tracked (compile-time switch).
        bool IsEnabled();

        // Whole-system snapshot - what a task manager polls on a timer.
        MemoryReport Snapshot();

        // Allocation-free fast paths for a single badge / row.
        int64_t BytesForScope(MemoryScopeId id);
        int64_t HostTotalBytes();
        int64_t GpuTotalBytes();

        // Enumerate live scopes (e.g. one per tab) for building the table.
        std::vector<std::pair<MemoryScopeId, std::string>> ListScopes();

        // Scope lifecycle (the app creates one per logical owner / tab).
        MemoryScopeId CreateScope(const std::string& name);
        void          DestroyScope(MemoryScopeId id);

        // Cheap change-detection: poll this and only rebuild your UI when it
        // changes (it bumps on every material allocation/free).
        uint64_t Generation();

        // Optional push notifications. Callbacks fire from
        // DispatchPendingNotifications(), which you call from your UI loop -
        // never on the allocation hot path.
        using ChangeToken = uint64_t;
        ChangeToken Subscribe(std::function<void()> onChanged);
        void        Unsubscribe(ChangeToken token);
        void        DispatchPendingNotifications();

        // Display helpers.
        std::string CategoryName(MemoryCategory category);
        std::string FormatBytes(int64_t bytes);
    } // namespace MemoryStats

} // namespace UltraCanvas
