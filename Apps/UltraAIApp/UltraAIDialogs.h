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
#include "UltraCanvasTextArea.h"
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
// the capability's List<X>Providers). Image and video generation additionally
// have an optional model field and an API-key field that stores into UltraVault
// as ai.<provider>.api_key instead of living in the widget — the providers
// behind them include cloud services (Anthropic, OpenAI, MiniMax) and local
// ones (llama-cpp, qwen, comfyui) whose "model" means a checkpoint file name.
//
// NOTE: the Chat (LLM) capability no longer lives here — it has its own
// multi-turn, streaming, attachment-capable dialog in UltraAIChatDialog.h,
// driven by the endpoints configured in the Settings dialog.
// Embeddings: a multi-line list of input strings (one per line) plus an
// optional dimensions field.
class EmbeddingsDialog : public UltraAIServiceDialog {
public:
    EmbeddingsDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input1_;   // input strings (multi-line)
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // dimensions
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input3_;
};
ULTRAAI_DECLARE_DIALOG(SpeechToTextDialog)
// Speech synthesis: text and voice id, plus the model and a credential —
// the cloud providers behind this capability need both, and their voice ids
// come from the provider rather than a fixed list.
class TextToSpeechDialog : public UltraAIServiceDialog {
public:
    TextToSpeechDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input1_;   // text (multi-line)
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // voice id
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  modelInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  keyInput_;
};
// Image generation: prompt, size and count, plus the model (a checkpoint
// file name for ComfyUI), a credential for cloud providers, and an optional
// ComfyUI workflow file whose contents are passed as the "workflow" option.
class ImageGenDialog : public UltraAIServiceDialog {
public:
    ImageGenDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input1_;   // prompt (multi-line)
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // size
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input3_;   // count
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  modelInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  keyInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  workflowInput_;
};
ULTRAAI_DECLARE_DIALOG(VisionDialog)
// Translation: a multi-line source text plus a target-language field.
class TranslatorDialog : public UltraAIServiceDialog {
public:
    TranslatorDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input1_;   // source text (multi-line)
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // target language
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input3_;
};
// Video generation: prompt, size and duration, plus the model (a checkpoint
// file name for ComfyUI) and a credential for cloud providers.
class VideoGenDialog : public UltraAIServiceDialog {
public:
    VideoGenDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input1_;   // prompt (multi-line)
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // size
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input3_;   // duration
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  modelInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  keyInput_;
};
// Music generation: a multi-line prompt plus duration and BPM.
class MusicGenDialog : public UltraAIServiceDialog {
public:
    MusicGenDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input1_;   // prompt (multi-line)
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // duration
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input3_;   // bpm
};
// Code assist: an instruction, a language, and a multi-line code field.
class CodeAssistDialog : public UltraAIServiceDialog {
public:
    CodeAssistDialog();
protected:
    long BuildForm(long formTop) override;
    void RunCapability() override;
private:
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input1_;   // instruction
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  input2_;   // language
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>   input3_;   // code (multi-line)
};

#undef ULTRAAI_DECLARE_DIALOG

} // namespace UltraAIApp
