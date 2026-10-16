// Apps/UltraVideo/UltraVideoHistory.cpp
// The undo history (UltraVideoHistory.h).
// Version: 0.5.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#include "UltraVideoHistory.h"

namespace UltraVideo {

namespace {
constexpr size_t kMaxEntries = 200;

// The view state is not an edit: keep the document's current view through undo
void KeepView(Document& target, const Document& current) {
    target.view = current.view;
    target.path = current.path;
}
}

void History::Push(const std::string& label, const Document& before, const Document& after,
                   const std::string& mergeKey) {
    if (!mergeKey.empty() && mergeKey == openKey && index > 0 && index == entries.size() && cleanIndex != index) {
        entries.back().after = after;
        return;
    }
    openKey = mergeKey;
    entries.resize(index);
    entries.push_back(Entry{label, before, after});
    if (entries.size() > kMaxEntries) {
        entries.erase(entries.begin());
        if (cleanIndex > 0) --cleanIndex; else cleanIndex = static_cast<size_t>(-1);
    }
    index = entries.size();
}

bool History::Undo(Document& document) {
    if (!CanUndo()) return false;
    openKey.clear();
    --index;
    Document restored = entries[index].before;
    KeepView(restored, document);
    document = std::move(restored);
    return true;
}

bool History::Redo(Document& document) {
    if (!CanRedo()) return false;
    openKey.clear();
    Document restored = entries[index].after;
    KeepView(restored, document);
    document = std::move(restored);
    ++index;
    return true;
}

void History::Clear() {
    entries.clear();
    index = 0;
    cleanIndex = 0;
    openKey.clear();
}

} // namespace UltraVideo
