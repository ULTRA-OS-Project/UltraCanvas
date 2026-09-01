// Apps/EmailCleaner/ui/EmailCleanerDetailView.h
// The detail behind a block on the map: who the sender is, the numbers that
// made their block that size, the terms that got their mail classified the way
// it was, what they attach, and the messages themselves.
//
// It answers the question the map raises — "why is this block so big, and is
// it something I want?" — without leaving the app.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include "EmailCleanerStore.h"

#include <memory>
#include <string>

namespace EmailCleaner {

// One message row: sender-scoped, so it shows date, category and subject.
class MessageRow : public UltraCanvas::UltraCanvasContainer {
public:
    MessageRow(const std::string& id, float x, float y, float w, float h,
               const AnalyzedMessage& message);
};

class DetailView {
public:
    void SetStore(const AnalysisStore* store) { store_ = store; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Show whatever the filter selects. `title` names it in the heading.
    void Refresh(const MessageFilter& filter, const std::string& title);

private:
    void RebuildEvidence();
    void RebuildMessages();

    const AnalysisStore* store_ = nullptr;
    MessageFilter        filter_;
    float                width_ = 0.0f;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> container_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     heading_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     summary_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> evidence_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> messages_;
};

} // namespace EmailCleaner
