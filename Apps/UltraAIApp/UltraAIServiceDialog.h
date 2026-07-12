// Apps/UltraAIApp/UltraAIServiceDialog.h
// Base class for the per-capability service dialogs. Builds the common
// shell — title bar, "Run" button, scrollable result area, "Close"
// button — and lets subclasses contribute their own input form and
// "what to do when Run is pressed" logic.
// Version: 0.1.1
// Last Modified: 2026-07-12
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasLabel.h"

#include <memory>
#include <string>

namespace UltraAIApp {

class UltraAIServiceDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    UltraAIServiceDialog(std::string serviceName, std::string description);
    ~UltraAIServiceDialog() override = default;

    // Build the standard dialog shell. Calls BuildForm() so subclasses
    // can drop their own widgets between the description and the Run row.
    void CreateServiceDialog();

protected:
    // Subclass hook: build the input form, returning the bottom Y
    // coordinate after the last widget so the Run row can be placed
    // below it. `formTop` is the y-coordinate where the form starts.
    virtual long BuildForm(long formTop) = 0;

    // Subclass hook: invoked when the user clicks "Run". Implementation
    // calls into a UltraAI mock adapter and pushes textual output via
    // AppendResult() / SetResult().
    virtual void RunCapability() = 0;

    // Helpers for subclasses.
    void SetResult(const std::string& text);
    void AppendResult(const std::string& text);
    void SetStatus(const std::string& text);

    // Standard widget factories so subclasses don't repeat themselves.
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> MakeLabel(
        const std::string& id, long x, long y, long w, long h,
        const std::string& text);

    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> MakeInput(
        const std::string& id, long x, long y, long w, long h,
        const std::string& placeholder, bool multiline = false);

public:
    // Layout constants — public so free functions in the dialog implementation
    // (e.g. kFormWidth in UltraAIDialogs.cpp) can reference them at namespace scope.
    static constexpr long kDialogWidth  = 720;
    static constexpr long kDialogHeight = 560;
    static constexpr long kMargin       = 16;
    static constexpr long kFormTop      = 80;   // below the description label

protected:
    std::string serviceName_;
    std::string description_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> resultArea_;
};

} // namespace UltraAIApp
