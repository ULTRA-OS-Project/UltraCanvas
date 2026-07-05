// Apps/UltraMail/ui/UltraMailComposeWindow.h
// The compose surface: To / Cc / Subject fields, an editable body, and Send /
// Cancel. Built from a Draft (blank, reply-prefilled or forward-prefilled) and
// hands an updated Draft back through onSend.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before engine headers (X11 macro ordering).
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"

#include "UltraMailComposer.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraMail {

class ComposeView {
public:
    void SetDraft(Draft draft) { draft_ = std::move(draft); }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Raised when Send is clicked, with the edited draft.
    std::function<void(const Draft&)> onSend;
    // Raised when Cancel is clicked.
    std::function<void()> onCancel;

private:
    Draft CollectDraft() const;

    Draft draft_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> to_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> cc_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> subject_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  body_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
};

} // namespace UltraMail
