// include/UltraCanvasFocusHistory.h
// MRU (most-recently-used) list of weak references, used by the application
// to remember window focus order for the "jump to last window" feature.
// Version: 1.0.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_FOCUS_HISTORY_H
#define ULTRACANVAS_FOCUS_HISTORY_H

#include <algorithm>
#include <memory>
#include <vector>

namespace UltraCanvas {

    // Header-only, framework-independent (unit-testable without a display).
    // Entries are weak_ptr so a destroyed object simply expires out of the
    // history instead of dangling; expired entries are pruned lazily.
    template<typename T>
    class UCWeakMRUList {
    public:
        // Move `item` to the front (most recent). Inserts it if not present.
        void Touch(const std::shared_ptr<T>& item) {
            if (!item) return;
            Remove(item.get());
            items.insert(items.begin(), item);
            if (items.size() > maxSize) {
                items.resize(maxSize);
            }
        }

        // Drop `item` from the history (e.g. when a window is destroyed).
        void Remove(const T* item) {
            std::erase_if(items, [item](const std::weak_ptr<T>& w) {
                auto locked = w.lock();
                return !locked || locked.get() == item;
            });
        }

        // Most recent live entry that is not `skip` and satisfies `pred`.
        // Returns nullptr when the history holds no such entry.
        template<typename Pred>
        std::shared_ptr<T> FindMostRecent(const T* skip, Pred pred) const {
            for (const auto& w : items) {
                auto locked = w.lock();
                if (!locked || locked.get() == skip) continue;
                if (pred(locked)) {
                    return locked;
                }
            }
            return nullptr;
        }

        // Number of live entries.
        size_t Size() const {
            size_t n = 0;
            for (const auto& w : items) {
                if (!w.expired()) ++n;
            }
            return n;
        }

        void Clear() { items.clear(); }

    private:
        std::vector<std::weak_ptr<T>> items;
        static constexpr size_t maxSize = 32;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_FOCUS_HISTORY_H
