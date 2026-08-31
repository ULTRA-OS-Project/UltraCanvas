// Apps/EmailCleaner/ui/EmailCleanerDetailView.h
// The detail behind a block on the map: who the sender is, the numbers that
// made their block that size, the terms that got their mail classified the way
// it was, what they attach, and the messages themselves.
//
// It answers the question the map raises — "why is this block so big, and is
// it something I want?" — without leaving the app.
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include "EmailCleanerStore.h"

#include <functional>
#include <memory>
#include <string>

namespace EmailCleaner {

// One message row: sender-scoped, so it shows date, category and subject.
class MessageRow : public UltraCanvas::UltraCanvasContainer {
public:
    // `onAttachments`, when set and the message has any, puts a button on the
    // row that asks to see them.
    MessageRow(const std::string& id, float x, float y, float w, float h,
               const AnalyzedMessage& message,
               const std::function<void(const AnalyzedMessage&)>& onAttachments = {});
};

class DetailView {
public:
    void SetStore(const AnalysisStore* store) { store_ = store; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Show whatever the filter selects. `title` names it in the heading.
    void Refresh(const MessageFilter& filter, const std::string& title);

    // Raised when a message's attachments are asked for. The app owns the
    // opening: the bytes live in the mail cache, not in the analysis database.
    std::function<void(const AnalyzedMessage&)> onOpenAttachments;

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
