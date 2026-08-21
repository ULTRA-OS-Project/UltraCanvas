// Apps/UltraAIApp/UltraAIDialogs.cpp
// Implementation of the ten per-capability service dialogs. Each one
// builds its own input form (headed by the shared provider picker) and
// runs the selected — or default-routed — UltraAI provider on "Run".
// Version: 0.2.0
// Last Modified: 2026-08-21

#include "UltraAIDialogs.h"

#include "UltraAI.h"
#include "UltraCanvasDropdown.h"
#ifdef ULTRAAI_HAS_ULTRAVAULT
#include <UltraVault/UltraVault.h>
#endif

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

namespace {

constexpr long kLabelHeight = 18;
constexpr long kRowHeight   = 28;
constexpr long kRowGap      = 6;
constexpr long kFormWidth   = UltraAIServiceDialog::kDialogWidth -
                              UltraAIServiceDialog::kMargin * 2;

// Helper: format byte count compactly.
std::string FormatBytes(size_t bytes) {
    std::ostringstream os;
    if (bytes < 1024)            os << bytes << " B";
    else if (bytes < 1024 * 1024) os << (bytes / 1024.0) << " KiB";
    else                          os << (bytes / (1024.0 * 1024.0)) << " MiB";
    return os.str();
}

// Helper: split a multi-line input into one entry per non-empty line.
std::vector<std::string> SplitLines(const std::string& in) {
    std::vector<std::string> out;
    std::string current;
    for (char c : in) {
        if (c == '\n') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string ErrorLine(const Error& e) {
    if (e.IsOk()) return "";
    return "ERROR: " + e.message + "\n";
}

} // namespace

// ===================================================================
// ChatDialog (ITextLLM)
// ===================================================================

ChatDialog::ChatDialog()
    : UltraAIServiceDialog("Chat (LLM)",
        "Send a prompt to the LLM and receive a single-turn reply. The "
        "provider list comes from the UltraAI registry; \"(default route)\" "
        "follows the routing policy. Cloud API keys are stored in UltraVault, "
        "never in this window.") {}

namespace {
bool ProviderNeedsApiKey(const std::string& provider) {
    return !provider.empty() && provider != "mock" && provider != "llama-cpp";
}
} // namespace

long ChatDialog::BuildForm(long y) {
    AddDialogElement(MakeLabel("prov-lbl", kMargin, y, 200, kLabelHeight,
                               "Provider"));
    AddDialogElement(MakeLabel("model-lbl", kMargin + 220, y,
                               kFormWidth - 220, kLabelHeight,
                               "Model (or GGUF path for llama-cpp; optional)"));
    y += kLabelHeight + 2;
    providerDropdown_ = CreateDropdown("chat-provider",
                                       kMargin, y, 200, kRowHeight);
    providerDropdown_->AddItem(kDefaultRouteLabel);
    for (const auto& id : ListTextLLMProviders()) {
        providerDropdown_->AddItem(id);
    }
    providerDropdown_->SetSelectedIndex(0);
    AddDialogElement(providerDropdown_);
    modelInput_ = MakeInput("chat-model", kMargin + 220, y,
                            kFormWidth - 220, kRowHeight,
                            "provider default");
    AddDialogElement(modelInput_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("key-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "API key (cloud providers; saved to UltraVault "
                               "as ai.<provider>.api_key and cleared here)"));
    y += kLabelHeight + 2;
    keyInput_ = MakeInput("chat-key", kMargin, y, kFormWidth, kRowHeight,
                          "leave empty to use the stored key");
    AddDialogElement(keyInput_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("sys-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "System prompt (optional)"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("chat-sys", kMargin, y, kFormWidth, kRowHeight,
                        "You are a concise assistant...");
    AddDialogElement(input1_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("usr-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "User message"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("chat-usr", kMargin, y, kFormWidth, 60,
                        "Type your prompt...", true);
    AddDialogElement(input2_);
    y += 60 + kRowGap;
    return y;
}

void ChatDialog::RunCapability() {
    SetStatus("Running...");
    SetResult("");

    const std::string provider = SelectedProviderId();

    TextLLMConfig cfg;
    cfg.providerId = provider;
    if (modelInput_ && !modelInput_->GetText().empty()) {
        cfg.defaultModel = modelInput_->GetText();
    }

    if (ProviderNeedsApiKey(provider)) {
#ifdef ULTRAAI_HAS_ULTRAVAULT
        const std::string vaultRef = "ai." + provider + ".api_key";
        if (keyInput_ && !keyInput_->GetText().empty()) {
            if (!UltraVault::IsAvailable()) UltraVault::Initialize();
            std::string typed = keyInput_->GetText();
            auto stored = UltraVault::Put(
                vaultRef, UltraVault::SecretValue::FromString(typed));
            keyInput_->SetText("");   // the key lives in the vault now
            if (!stored.IsOk()) {
                SetStatus("Could not store the key: " + stored.message);
                return;
            }
        }
        cfg.apiKeyVaultRef = vaultRef;
#else
        // No vault in this build: the key stays in the widget for the call.
        if (keyInput_) cfg.apiKey = keyInput_->GetText();
#endif
    }

    Error createError;
    auto llm = CreateTextLLM(cfg, &createError);
    if (!llm) {
        SetStatus("Failed to create TextLLM: " + createError.message);
        return;
    }

    ChatRequest req;
    if (input1_ && !input1_->GetText().empty()) {
        Message sys; sys.role = Role::System; sys.text = input1_->GetText();
        req.messages.push_back(std::move(sys));
    }
    Message usr; usr.role = Role::User;
    usr.text = input2_ ? input2_->GetText() : "";
    req.messages.push_back(std::move(usr));

    auto resp = llm->Chat(req);
    std::ostringstream os;
    os << ErrorLine(resp.error) << resp.text
       << "\n\n(provider=" << llm->GetCapabilities().providerId
       << "  model=" << resp.model
       << "  in=" << resp.usage.inputTokens
       << "  out=" << resp.usage.outputTokens << ")";
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// EmbeddingsDialog (IEmbeddings)
// ===================================================================

EmbeddingsDialog::EmbeddingsDialog()
    : UltraAIServiceDialog("Embeddings",
        "Compute vector embeddings for one or more texts (one per line). "
        "\"(default route)\" follows the routing policy across the "
        "registered providers.") {}

long EmbeddingsDialog::BuildForm(long y) {
    AddProviderPicker(y, ListEmbeddingsProviders());

    AddDialogElement(MakeLabel("emb-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "Inputs (one per line)"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("emb-in", kMargin, y, kFormWidth, 120,
                        "apple\nfruit\ncar", true);
    AddDialogElement(input1_);
    y += 120 + kRowGap;

    AddDialogElement(MakeLabel("dim-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "Dimensions (optional; provider default)"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("emb-dim", kMargin, y, 120, kRowHeight, "auto");
    AddDialogElement(input2_);
    y += kRowHeight + kRowGap;
    return y;
}

void EmbeddingsDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto emb = CreateEmbeddings({.providerId = SelectedProviderId()},
                                &createError);
    if (!emb) {
        SetStatus("Failed to create Embeddings: " + createError.message);
        return;
    }

    EmbeddingRequest req;
    req.input = SplitLines(input1_ ? input1_->GetText() : "");
    if (req.input.empty()) {
        SetStatus("Provide at least one input line"); return;
    }
    if (input2_ && !input2_->GetText().empty()) {
        try { req.dimensions = std::stoi(input2_->GetText()); } catch (...) {}
    }

    auto resp = emb->Embed(req);
    std::ostringstream os;
    os << ErrorLine(resp.error)
       << "(provider=" << emb->GetCapabilities().providerId << ")\n";
    for (size_t i = 0; i < resp.embeddings.size(); ++i) {
        const auto& v = resp.embeddings[i].values;
        os << "[" << i << "] dim=" << v.size() << " { ";
        for (size_t k = 0; k < v.size() && k < 8; ++k) {
            os << v[k] << (k + 1 == std::min<size_t>(8, v.size()) ? "" : ", ");
        }
        os << (v.size() > 8 ? " ..." : "") << " }\n";
    }
    if (resp.embeddings.size() >= 2) {
        double s = IEmbeddings::CosineSimilarity(resp.embeddings[0],
                                                 resp.embeddings[1]);
        os << "\ncos(0, 1) = " << s;
    }
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// SpeechToTextDialog (ISpeechToText)
// ===================================================================

SpeechToTextDialog::SpeechToTextDialog()
    : UltraAIServiceDialog("Speech to Text",
        "Transcribe audio. The mock adapter doesn't read real audio "
        "files — instead it consumes the byte count and returns a "
        "placeholder transcript.") {}

long SpeechToTextDialog::BuildForm(long y) {
    AddProviderPicker(y, ListSpeechToTextProviders());

    AddDialogElement(MakeLabel("stt-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "Mock audio byte length"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("stt-bytes", kMargin, y, 200, kRowHeight, "4096");
    AddDialogElement(input1_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("stt-lang-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Language (BCP-47, empty = auto)"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("stt-lang", kMargin, y, 200, kRowHeight, "en");
    AddDialogElement(input2_);
    y += kRowHeight + kRowGap;
    return y;
}

void SpeechToTextDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto stt = CreateSpeechToText({.providerId = SelectedProviderId()},
                                  &createError);
    if (!stt) {
        SetStatus("Failed to create STT: " + createError.message);
        return;
    }

    TranscribeRequest req;
    size_t bytes = 4096;
    if (input1_ && !input1_->GetText().empty()) {
        try { bytes = static_cast<size_t>(std::stoul(input1_->GetText())); }
        catch (...) {}
    }
    req.audio.bytes = std::vector<uint8_t>(bytes, 0x00);
    req.audio.mimeType = "audio/wav";
    if (input2_) req.language = input2_->GetText();

    auto resp = stt->Transcribe(req);
    std::ostringstream os;
    os << ErrorLine(resp.error)
       << "transcript: " << resp.text << "\n"
       << "language   : " << resp.detectedLanguage << "\n"
       << "duration   : " << resp.durationSec << " s\n"
       << "segments   : " << resp.segments.size();
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// TextToSpeechDialog (ITextToSpeech)
// ===================================================================

TextToSpeechDialog::TextToSpeechDialog()
    : UltraAIServiceDialog("Text to Speech",
        "Synthesize speech from text. The mock returns placeholder "
        "audio bytes (one byte per character).") {}

long TextToSpeechDialog::BuildForm(long y) {
    AddProviderPicker(y, ListTextToSpeechProviders());

    AddDialogElement(MakeLabel("tts-lbl", kMargin, y, kFormWidth, kLabelHeight,
                               "Text to speak"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("tts-text", kMargin, y, kFormWidth, 80,
                        "Hello world from UltraAI.", true);
    AddDialogElement(input1_);
    y += 80 + kRowGap;

    AddDialogElement(MakeLabel("tts-voice-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Voice id (try mock-aria | mock-leo | mock-greta)"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("tts-voice", kMargin, y, 240, kRowHeight, "mock-aria");
    AddDialogElement(input2_);
    y += kRowHeight + kRowGap;
    return y;
}

void TextToSpeechDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto tts = CreateTextToSpeech({.providerId = SelectedProviderId()},
                                  &createError);
    if (!tts) {
        SetStatus("Failed to create TTS: " + createError.message);
        return;
    }

    SpeakRequest req;
    req.text    = input1_ ? input1_->GetText() : "";
    req.voiceId = input2_ ? input2_->GetText() : "mock-aria";
    req.format  = TtsAudioFormat::Mp3;

    auto resp = tts->Speak(req);
    std::ostringstream os;
    os << ErrorLine(resp.error)
       << "audio bytes : " << FormatBytes(resp.audio.bytes.size()) << "\n"
       << "mime        : " << resp.audio.mimeType << "\n"
       << "duration    : " << resp.durationSec << " s";

    // Show available voices for the user's reference.
    os << "\n\nAvailable voices:";
    for (const auto& v : tts->ListVoices()) {
        os << "\n  " << v.id << "  (" << v.displayName
           << ", " << v.language << ")";
    }
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// ImageGenDialog (IImageGen)
// ===================================================================

ImageGenDialog::ImageGenDialog()
    : UltraAIServiceDialog("Image Generation",
        "Generate one or more images from a prompt. The mock returns "
        "placeholder PNG bytes (header + width/height/index tag).") {}

long ImageGenDialog::BuildForm(long y) {
    AddProviderPicker(y, ListImageGenProviders());

    AddDialogElement(MakeLabel("ig-prompt-lbl", kMargin, y,
                               kFormWidth, kLabelHeight, "Prompt"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("ig-prompt", kMargin, y, kFormWidth, 60,
                        "a serene mountain lake at sunset", true);
    AddDialogElement(input1_);
    y += 60 + kRowGap;

    AddDialogElement(MakeLabel("ig-size-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Size (WxH) and count, e.g. 1024x1024"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("ig-size", kMargin, y, 200, kRowHeight, "512x512");
    AddDialogElement(input2_);
    input3_ = MakeInput("ig-count", kMargin + 220, y, 80, kRowHeight, "1");
    AddDialogElement(input3_);
    y += kRowHeight + kRowGap;
    return y;
}

void ImageGenDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto ig = CreateImageGen({.providerId = SelectedProviderId()},
                             &createError);
    if (!ig) {
        SetStatus("Failed to create ImageGen: " + createError.message);
        return;
    }

    ImageGenRequest req;
    req.prompt = input1_ ? input1_->GetText() : "";
    if (input2_) {
        const auto s = input2_->GetText();
        auto x = s.find('x');
        if (x != std::string::npos) {
            try {
                req.width  = std::stoi(s.substr(0, x));
                req.height = std::stoi(s.substr(x + 1));
            } catch (...) {}
        }
    }
    if (input3_ && !input3_->GetText().empty()) {
        try { req.count = std::stoi(input3_->GetText()); } catch (...) {}
    }

    auto resp = ig->Generate(req);
    std::ostringstream os;
    os << ErrorLine(resp.error)
       << "model    : " << resp.model << "\n"
       << "images   : " << resp.images.size() << "\n";
    for (size_t i = 0; i < resp.images.size(); ++i) {
        const auto& g = resp.images[i];
        os << "  [" << i << "] " << FormatBytes(g.image.bytes.size())
           << "  mime=" << g.image.mimeType
           << "  seed=" << g.seed << "\n";
    }
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// VisionDialog (IVisionAnalyzer)
// ===================================================================

VisionDialog::VisionDialog()
    : UltraAIServiceDialog("Vision Analysis",
        "Run caption + tags + objects + OCR + VQA against a (placeholder) "
        "image. The mock fills exactly the fields you request.") {}

long VisionDialog::BuildForm(long y) {
    AddProviderPicker(y, ListVisionAnalyzerProviders());

    AddDialogElement(MakeLabel("v-bytes-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Mock image byte length"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("v-bytes", kMargin, y, 200, kRowHeight, "2048");
    AddDialogElement(input1_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("v-q-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Visual question (optional)"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("v-q", kMargin, y, kFormWidth, kRowHeight,
                        "What is in this image?");
    AddDialogElement(input2_);
    y += kRowHeight + kRowGap;
    return y;
}

void VisionDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto v = CreateVisionAnalyzer({.providerId = SelectedProviderId()},
                                  &createError);
    if (!v) {
        SetStatus("Failed to create Vision: " + createError.message);
        return;
    }

    VisionAnalyzeRequest req;
    size_t bytes = 2048;
    if (input1_ && !input1_->GetText().empty()) {
        try { bytes = std::stoul(input1_->GetText()); } catch (...) {}
    }
    req.image.bytes = std::vector<uint8_t>(bytes, 0x55);
    req.image.mimeType = "image/png";
    req.prompt = input2_ ? input2_->GetText() : "";
    req.tasks = {
        VisionTask::Caption, VisionTask::Tags,
        VisionTask::ObjectDetection, VisionTask::Ocr,
        VisionTask::VisualQuestion
    };

    auto resp = v->Analyze(req);
    std::ostringstream os;
    os << ErrorLine(resp.error)
       << "caption  : " << resp.caption << "\n"
       << "ocr text : " << resp.ocrText << "\n"
       << "tags     :";
    for (const auto& t : resp.tags) os << " " << t.label << "(" << t.confidence << ")";
    os << "\nobjects  : " << resp.objects.size();
    if (!resp.vqaAnswer.empty())
        os << "\nVQA      : " << resp.vqaAnswer;
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// TranslatorDialog (ITranslator)
// ===================================================================

TranslatorDialog::TranslatorDialog()
    : UltraAIServiceDialog("Translation",
        "Translate one line of text per line of input. Source language "
        "is auto-detected when left empty.") {}

long TranslatorDialog::BuildForm(long y) {
    AddProviderPicker(y, ListTranslatorProviders());

    AddDialogElement(MakeLabel("tr-text-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Texts (one per line)"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("tr-text", kMargin, y, kFormWidth, 80,
                        "ich bin der schnelle fuchs", true);
    AddDialogElement(input1_);
    y += 80 + kRowGap;

    AddDialogElement(MakeLabel("tr-tgt-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Target language (BCP-47)"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("tr-tgt", kMargin, y, 120, kRowHeight, "fr");
    AddDialogElement(input2_);
    y += kRowHeight + kRowGap;
    return y;
}

void TranslatorDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto tr = CreateTranslator({.providerId = SelectedProviderId()},
                               &createError);
    if (!tr) {
        SetStatus("Failed to create Translator: " + createError.message);
        return;
    }

    TranslateRequest req;
    req.texts = SplitLines(input1_ ? input1_->GetText() : "");
    req.targetLanguage = input2_ ? input2_->GetText() : "en";
    if (req.texts.empty()) {
        SetStatus("Provide at least one input line"); return;
    }

    auto resp = tr->Translate(req);
    std::ostringstream os;
    os << ErrorLine(resp.error);
    for (size_t i = 0; i < resp.results.size(); ++i) {
        const auto& r = resp.results[i];
        os << "[" << r.detectedSourceLanguage << "->" << req.targetLanguage
           << "]  " << r.text << "\n";
    }
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// VideoGenDialog (IVideoGen)
// ===================================================================

VideoGenDialog::VideoGenDialog()
    : UltraAIServiceDialog("Video Generation",
        "Generate a short video from a prompt. The mock returns "
        "placeholder MP4 bytes and a thumbnail PNG.") {}

long VideoGenDialog::BuildForm(long y) {
    AddProviderPicker(y, ListVideoGenProviders());

    AddDialogElement(MakeLabel("vg-prompt-lbl", kMargin, y,
                               kFormWidth, kLabelHeight, "Prompt"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("vg-prompt", kMargin, y, kFormWidth, 60,
                        "ocean waves rolling at sunset", true);
    AddDialogElement(input1_);
    y += 60 + kRowGap;

    AddDialogElement(MakeLabel("vg-size-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Size (WxH) and duration (sec)"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("vg-size", kMargin, y, 200, kRowHeight, "1024x576");
    AddDialogElement(input2_);
    input3_ = MakeInput("vg-dur",  kMargin + 220, y, 80, kRowHeight, "4");
    AddDialogElement(input3_);
    y += kRowHeight + kRowGap;
    return y;
}

void VideoGenDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto vg = CreateVideoGen({.providerId = SelectedProviderId()},
                             &createError);
    if (!vg) {
        SetStatus("Failed to create VideoGen: " + createError.message);
        return;
    }

    VideoGenRequest req;
    req.prompt = input1_ ? input1_->GetText() : "";
    if (input2_) {
        const auto s = input2_->GetText();
        auto x = s.find('x');
        if (x != std::string::npos) {
            try {
                req.width  = std::stoi(s.substr(0, x));
                req.height = std::stoi(s.substr(x + 1));
            } catch (...) {}
        }
    }
    if (input3_ && !input3_->GetText().empty()) {
        try { req.durationSec = std::stod(input3_->GetText()); } catch (...) {}
    }

    auto resp = vg->Generate(req);
    std::ostringstream os;
    os << ErrorLine(resp.error);
    for (size_t i = 0; i < resp.videos.size(); ++i) {
        const auto& v = resp.videos[i];
        os << "video      : " << v.width << "x" << v.height
           << " @ " << v.fps << "fps, " << v.durationSec << "s\n"
           << "video bytes: " << FormatBytes(v.video.bytes.size()) << "\n"
           << "thumb bytes: " << FormatBytes(v.thumbnail.bytes.size()) << "\n";
    }
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// MusicGenDialog (IMusicGen)
// ===================================================================

MusicGenDialog::MusicGenDialog()
    : UltraAIServiceDialog("Music Generation",
        "Generate a music track from a prompt. The mock returns "
        "placeholder MP3 bytes; song mode includes (optionally "
        "auto-generated) lyrics.") {}

long MusicGenDialog::BuildForm(long y) {
    AddProviderPicker(y, ListMusicGenProviders());

    AddDialogElement(MakeLabel("mg-prompt-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Prompt (style, mood, instruments)"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("mg-prompt", kMargin, y, kFormWidth, 60,
                        "uplifting jazz piano trio", true);
    AddDialogElement(input1_);
    y += 60 + kRowGap;

    AddDialogElement(MakeLabel("mg-meta-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Duration (sec) and BPM"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("mg-dur", kMargin, y, 120, kRowHeight, "8");
    AddDialogElement(input2_);
    input3_ = MakeInput("mg-bpm", kMargin + 140, y, 120, kRowHeight, "120");
    AddDialogElement(input3_);
    y += kRowHeight + kRowGap;
    return y;
}

void MusicGenDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto mg = CreateMusicGen({.providerId = SelectedProviderId()},
                             &createError);
    if (!mg) {
        SetStatus("Failed to create MusicGen: " + createError.message);
        return;
    }

    MusicGenRequest req;
    req.prompt = input1_ ? input1_->GetText() : "";
    req.mode   = MusicGenMode::Instrumental;
    if (input2_ && !input2_->GetText().empty()) {
        try { req.durationSec = std::stod(input2_->GetText()); } catch (...) {}
    }
    if (input3_ && !input3_->GetText().empty()) {
        try { req.bpm = std::stoi(input3_->GetText()); } catch (...) {}
    }

    auto resp = mg->Generate(req);
    std::ostringstream os;
    os << ErrorLine(resp.error);
    for (size_t i = 0; i < resp.tracks.size(); ++i) {
        const auto& t = resp.tracks[i];
        os << "track " << i << " : " << FormatBytes(t.audio.bytes.size())
           << "  " << t.audio.mimeType
           << "  " << t.durationSec << "s"
           << "  bpm=" << t.bpm
           << "  key=" << t.key << "\n";
    }
    SetResult(os.str());
    SetStatus("Done");
}

// ===================================================================
// CodeAssistDialog (ICodeAssist)
// ===================================================================

CodeAssistDialog::CodeAssistDialog()
    : UltraAIServiceDialog("Code Assist",
        "Generate, explain, refactor or complete code. The mock returns "
        "shape-correct stubs so you can verify the wiring.") {}

long CodeAssistDialog::BuildForm(long y) {
    AddProviderPicker(y, ListCodeAssistProviders());

    AddDialogElement(MakeLabel("ca-instr-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Instruction (used for Generate)"));
    y += kLabelHeight + 2;
    input1_ = MakeInput("ca-instr", kMargin, y, kFormWidth, kRowHeight,
                        "implement fizzbuzz");
    AddDialogElement(input1_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("ca-lang-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Language"));
    y += kLabelHeight + 2;
    input2_ = MakeInput("ca-lang", kMargin, y, 200, kRowHeight, "python");
    AddDialogElement(input2_);
    y += kRowHeight + kRowGap;

    AddDialogElement(MakeLabel("ca-code-lbl", kMargin, y,
                               kFormWidth, kLabelHeight,
                               "Code snippet (used by Explain / Refactor / DetectBugs)"));
    y += kLabelHeight + 2;
    input3_ = MakeInput("ca-code", kMargin, y, kFormWidth, 80,
                        "if x = 1: pass", true);
    AddDialogElement(input3_);
    y += 80 + kRowGap;
    return y;
}

void CodeAssistDialog::RunCapability() {
    SetStatus("Running...");

    Error createError;
    auto ca = CreateCodeAssist({.providerId = SelectedProviderId()},
                               &createError);
    if (!ca) {
        SetStatus("Failed to create CodeAssist: " + createError.message);
        return;
    }

    const std::string instr  = input1_ ? input1_->GetText() : "";
    const std::string lang   = input2_ ? input2_->GetText() : "python";
    const std::string snippet = input3_ ? input3_->GetText() : "";

    auto runOne = [&](CodeTask task, const char* label) -> std::string {
        CodeAssistRequest req;
        req.task        = task;
        req.language    = lang;
        req.instruction = instr;
        req.codeSnippet = snippet;
        auto resp = ca->Run(req);
        std::ostringstream os;
        os << "=== " << label << " ===\n"
           << ErrorLine(resp.error)
           << resp.code;
        if (!resp.explanation.empty())
            os << "\n-- explanation --\n" << resp.explanation;
        if (!resp.diagnostics.empty()) {
            os << "\n-- diagnostics --";
            for (const auto& d : resp.diagnostics) {
                os << "\n  [" << d.line << ":" << d.column << "] "
                   << d.message;
            }
        }
        return os.str();
    };

    std::ostringstream os;
    os << runOne(CodeTask::Generate,   "Generate")   << "\n\n"
       << runOne(CodeTask::Explain,    "Explain")    << "\n\n"
       << runOne(CodeTask::DetectBugs, "DetectBugs");
    SetResult(os.str());
    SetStatus("Done");
}

} // namespace UltraAIApp
