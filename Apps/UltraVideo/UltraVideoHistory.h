// Apps/UltraVideo/UltraVideoHistory.h
// The undo history (Docs/Research/UltraVideoDesignProposal.md §5.7): every
// edit holds the document before and after it, with a name for the app bar's
// tooltip; the clean mark follows the last save, so "Saved" in the app bar
// is true exactly when the history stands where it was saved. Edits that
// carry the same merge key one after another - the values a slider passes
// through while it is dragged - fold into one entry, closed by EndMerge.
// Version: 0.5.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoDocument.h"

#include <cstddef>
#include <string>
#include <vector>

namespace UltraVideo {

class History {
public:
    // With a `mergeKey`, an edit right after one with the same key (nothing
    // undone, saved or closed in between) widens that entry instead of
    // adding one: it keeps the first edit's `before` and takes this `after`
    void Push(const std::string& label, const Document& before, const Document& after,
              const std::string& mergeKey = "");
    void EndMerge() { openKey.clear(); }      // the next edit starts an entry of its own
    bool CanUndo() const { return index > 0; }
    bool CanRedo() const { return index < entries.size(); }
    std::string UndoLabel() const { return CanUndo() ? entries[index - 1].label : ""; }
    std::string RedoLabel() const { return CanRedo() ? entries[index].label : ""; }
    bool Undo(Document& document);
    bool Redo(Document& document);
    void MarkClean() { cleanIndex = index; }
    bool IsClean() const { return cleanIndex == index; }
    void Clear();

private:
    struct Entry {
        std::string label;
        Document before;
        Document after;
    };
    std::vector<Entry> entries;
    size_t index = 0;          // entries before it are done
    size_t cleanIndex = 0;
    std::string openKey;       // the merge key of the last entry while it may still grow
};

} // namespace UltraVideo
