// Apps/UltraAIApp/UltraAIDialogs.h
// One per-capability service dialog class for each of UltraAI's ten
// interfaces. Each subclass owns its input form and the logic that runs
// the provider picked in the dialog (or the default route) on "Run".
// Version: 0.2.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAIServiceDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasLabel.h"

#include <memory>

namespace UltraAIApp {

#define ULTRAAI_DECLARE_DIALOG(NameType)                                \
class NameType : public UltraAIServiceDialog {                          \
public:                                                                 \
    NameType();                                                         \
protected:                                                              \
    long BuildForm(long formTop) override;                              \
    void RunCapability() override;                                      \
private:                                                                \
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input1_;        \
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;        \
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input3_;        \
};

// Every dialog carries a provider picker (the base class helper, fed by
// the capability's List<X>Providers). Chat additionally has an optional
// model / GGUF-path field and an API-key field that stores into UltraVault
// as ai.<provider>.api_key instead of living in the widget.
class ChatDialog : public UltraAIServiceDialog {
public:
    ChatDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input1_;   // system
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // user
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  modelInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  keyInput_;
};

ULTRAAI_DECLARE_DIALOG(EmbeddingsDialog)
ULTRAAI_DECLARE_DIALOG(SpeechToTextDialog)
ULTRAAI_DECLARE_DIALOG(TextToSpeechDialog)
ULTRAAI_DECLARE_DIALOG(ImageGenDialog)
ULTRAAI_DECLARE_DIALOG(VisionDialog)
ULTRAAI_DECLARE_DIALOG(TranslatorDialog)
ULTRAAI_DECLARE_DIALOG(VideoGenDialog)
ULTRAAI_DECLARE_DIALOG(MusicGenDialog)
ULTRAAI_DECLARE_DIALOG(CodeAssistDialog)

#undef ULTRAAI_DECLARE_DIALOG

} // namespace UltraAIApp
