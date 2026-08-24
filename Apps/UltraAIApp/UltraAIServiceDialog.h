// Apps/UltraAIApp/UltraAIServiceDialog.h
// Base class for the per-capability service dialogs. Builds the common
// shell — title bar, "Run" button, scrollable result area, "Close"
// button — and lets subclasses contribute their own input form and
// "what to do when Run is pressed" logic.
// Version: 0.2.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAIServiceDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    UltraAIServiceDialog(std::string serviceName, std::string description);
    ~UltraAIServiceDialog() override;

    // Build the standard dialog shell. Calls BuildForm() so subclasses
    // can drop their own widgets between the description and the Run row.
    void CreateServiceDialog();

protected:
    // Subclass hook: build the input form, returning the bottom Y
    // coordinate after the last widget so the Run row can be placed
    // below it. `formTop` is the y-coordinate where the form starts.
    virtual long BuildForm(long formTop) = 0;

    // Subclass hook: invoked when the user clicks "Run". Implementation
    // reads its widgets, then hands the provider call to RunOffThread().
    virtual void RunCapability() = 0;

    // What one run produced: the line for the status label and the body for
    // the result area.
    struct RunOutcome {
        std::string status = "Done";
        std::string result;
    };

    // Run `work` on a worker thread and deliver its outcome back on the UI
    // thread. Providers are real now — a ComfyUI or Hailuo generation takes
    // minutes, and calling one from the click handler freezes the window
    // for that whole time (no repaint, no Close button).
    //
    // `work` runs off the UI thread: it must not touch widgets. Read every
    // input before calling this and capture the values by value. A second
    // Run while one is in flight is ignored.
    void RunOffThread(std::function<RunOutcome()> work);

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

    // Provider picker shared by every service dialog: a labeled dropdown
    // seeded with "(default route)" plus the capability's registered
    // providers (List<Capability>Providers()). Stores the widget in
    // providerDropdown_ and advances y past the row.
    void AddProviderPicker(long& y, const std::vector<std::string>& providers);

    // The picker with a model field beside it, for capabilities whose
    // providers need naming a model: a cloud model id, or a checkpoint file
    // name for a local server. Creates the input, stores it in `outModel`,
    // and advances y past the row.
    void AddProviderAndModelRow(long& y,
                                const std::string& idPrefix,
                                const std::vector<std::string>& providers,
                                const std::string& modelLabel,
                                const std::string& modelPlaceholder,
                                std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& outModel);

    // A single labelled full-width input row.
    void AddLabelledInput(long& y, const std::string& id,
                          const std::string& label,
                          const std::string& placeholder,
                          std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& outInput);

    // Provider chosen in the picker; empty for "(default route)" (or when
    // the dialog never called AddProviderPicker), which lets the UltraAI
    // routing policy decide.
    std::string SelectedProviderId() const;

    // The picker's routing-policy entry.
    static constexpr const char* kDefaultRouteLabel = "(default route)";

public:
    // Layout constants — public so free functions in the dialog implementation
    // (e.g. kFormWidth in UltraAIDialogs.cpp) can reference them at namespace scope.
    static constexpr long kDialogWidth  = 720;
    // Tall enough that the dialogs with the most form rows (chat, image and
    // video generation each carry a provider+model row, a credential row and
    // their own inputs) still leave a readable result area below Run.
    static constexpr long kDialogHeight = 640;
    static constexpr long kMargin       = 16;
    static constexpr long kFormTop      = 80;   // below the description label

protected:
    std::string serviceName_;
    std::string description_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> resultArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  providerDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    runButton_;

    // One run at a time; the button says so while it lasts.
    bool running_ = false;
    // The destructor flips this so a queued delivery that outlives the
    // dialog becomes a no-op instead of a dangling call (the pattern
    // UltraCanvasAudioPlayerElement and UltraCanvasAlbum use).
    std::shared_ptr<std::atomic<bool>> uiAlive_ =
        std::make_shared<std::atomic<bool>>(true);
};

} // namespace UltraAIApp
