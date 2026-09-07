// Apps/UltraAIApp/UltraAIChatDialog.cpp
// Version: 0.1.0

#include "UltraAIChatDialog.h"
#include "UltraAISettingsDialog.h"

#include "UltraAI.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

namespace {

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct MimeInfo { std::string mime; bool isImage; };

// Best-effort MIME/type guess from a filename extension. The image flag drives
// whether an attachment becomes an ImagePart or a FilePart.
MimeInfo GuessMime(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    const std::string ext = dot == std::string::npos ? "" : ToLower(filename.substr(dot + 1));

    if (ext == "png")                 return {"image/png",  true};
    if (ext == "jpg" || ext == "jpeg")return {"image/jpeg", true};
    if (ext == "gif")                 return {"image/gif",  true};
    if (ext == "webp")                return {"image/webp", true};
    if (ext == "bmp")                 return {"image/bmp",  true};

    if (ext == "pdf")  return {"application/pdf", false};
    if (ext == "txt")  return {"text/plain", false};
    if (ext == "md" || ext == "markdown") return {"text/markdown", false};
    if (ext == "json") return {"application/json", false};
    if (ext == "csv")  return {"text/csv", false};
    return {"application/octet-stream", false};
}

constexpr const char* kGreeting =
    "**UltraAI Chat**\n\nPick an endpoint above and type a message. Replies "
    "stream in live; attach images or files with the 📎 button. \"New chat\" "
    "clears the conversation.\n\n---\n\n";

} // namespace

UltraAIChatDialog::UltraAIChatDialog() = default;

UltraAIChatDialog::~UltraAIChatDialog() {
    uiAlive_->store(false);
    if (stream_) stream_->Cancel();
}

void UltraAIChatDialog::CreateChatDialog() {
    using UltraCanvas::CSSLayout::Dimension;
    using UltraCanvas::CSSLayout::AlignItems;

    DialogConfig cfg;
    cfg.title      = "UltraAI — Chat";
    cfg.width      = kW;
    cfg.height     = kH;
    cfg.dialogType = DialogType::Custom;        // drop the icon/message chrome
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = true;                       // let the user grow the window
    CreateDialog(cfg);

    // Root flex column.
    layout.SetFlexColumn().SetFlexGap(8);
    SetPadding(16);

    // ===== Endpoint row: picker (grows) + Settings + New chat =====
    auto topRow = std::make_shared<UltraCanvasContainer>("chat-toprow");
    topRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    topRow->size.height = Dimension::Px(30);
    topRow->layoutItem.SetFlexShrink(0);

    endpointPicker_ = std::make_shared<UltraCanvasDropdown>("chat-endpoint", 0, 0, 0, 28);
    endpointPicker_->size.height = Dimension::Px(28);
    endpointPicker_->layoutItem.SetFlexGrow(1);
    topRow->AddChild(endpointPicker_);

    auto settingsBtn = std::make_shared<UltraCanvasButton>("chat-settings", 0, 0, 130, 28);
    settingsBtn->SetText("⚙  Settings");
    settingsBtn->size.width = Dimension::Px(130);
    settingsBtn->layoutItem.SetFlexShrink(0);
    settingsBtn->onClick = [this]() { OnOpenSettings(); };
    topRow->AddChild(settingsBtn);

    auto newChatBtn = std::make_shared<UltraCanvasButton>("chat-new", 0, 0, 120, 28);
    newChatBtn->SetText("New chat");
    newChatBtn->size.width = Dimension::Px(120);
    newChatBtn->layoutItem.SetFlexShrink(0);
    newChatBtn->onClick = [this]() { OnNewChat(); };
    topRow->AddChild(newChatBtn);
    AddChild(topRow);

    // ===== Transcript (fills all slack) =====
    transcript_ = std::make_shared<UltraCanvasTextArea>("chat-transcript", 0, 0, 0, 0);
    transcript_->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    transcript_->SetReadOnly(true);
    transcript_->SetWordWrap(true);
    transcript_->SetText(kGreeting);
    transcript_->layoutItem.SetFlexGrow(1);
    AddChild(transcript_);

    // ===== Attachment chips =====
    chipsLabel_ = std::make_shared<UltraCanvasLabel>("chat-chips", 0, 0, 0, 18, "");
    chipsLabel_->size.height = Dimension::Px(18);
    chipsLabel_->layoutItem.SetFlexShrink(0);
    AddChild(chipsLabel_);

    // ===== Input =====
    input_ = std::make_shared<UltraCanvasTextInput>("chat-input", 0, 0, 0, 90);
    input_->SetInputType(TextInputType::Multiline);
    input_->SetPlaceholder("Type a message...");
    input_->size.height = Dimension::Px(90);
    input_->layoutItem.SetFlexShrink(0);
    AddChild(input_);

    // ===== Pinned action row =====
    auto actionRow = std::make_shared<UltraCanvasContainer>("chat-actions");
    actionRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    actionRow->size.height = Dimension::Px(34);
    actionRow->layoutItem.SetFlexShrink(0);

    auto attachBtn = std::make_shared<UltraCanvasButton>("chat-attach", 0, 0, 130, 30);
    attachBtn->SetText("📎  Attach");
    attachBtn->size.width = Dimension::Px(130);
    attachBtn->layoutItem.SetFlexShrink(0);
    attachBtn->onClick = [this]() { OnAttach(); };
    actionRow->AddChild(attachBtn);

    statusLabel_ = std::make_shared<UltraCanvasLabel>("chat-status", 0, 0, 0, 20, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    actionRow->AddChild(statusLabel_);

    sendButton_ = std::make_shared<UltraCanvasButton>("chat-send", 0, 0, 120, 30);
    sendButton_->SetText("Send");
    sendButton_->size.width = Dimension::Px(120);
    sendButton_->layoutItem.SetFlexShrink(0);
    sendButton_->onClick = [this]() { OnSend(); };
    actionRow->AddChild(sendButton_);

    auto closeBtn = std::make_shared<UltraCanvasButton>("chat-close", 0, 0, 90, 30);
    closeBtn->SetText("Close");
    closeBtn->size.width = Dimension::Px(90);
    closeBtn->layoutItem.SetFlexShrink(0);
    closeBtn->onClick = [this]() { CloseDialog(DialogResult::Close); };
    actionRow->AddChild(closeBtn);
    AddChild(actionRow);

    RebuildEndpointPicker();
}

void UltraAIChatDialog::RebuildEndpointPicker() {
    chatEndpoints_ = EndpointStore::Instance().ForCapability(AICapability::Chat);
    if (!endpointPicker_) return;

    endpointPicker_->ClearItems();
    if (chatEndpoints_.empty()) {
        endpointPicker_->AddItem("(no chat endpoint — open Settings)");
        endpointPicker_->SetSelectedIndex(0, false);
        SetStatus("No chat endpoint configured. Click Settings to add one.");
        return;
    }
    for (const auto& e : chatEndpoints_) {
        endpointPicker_->AddItem((e.name.empty() ? e.id : e.name) +
                                 "   (" + e.providerId + ")");
    }
    endpointPicker_->SetSelectedIndex(0, false);
    SetStatus("");
}

void UltraAIChatDialog::OnSend() {
    if (sending_) return;
    if (chatEndpoints_.empty()) {
        SetStatus("No chat endpoint configured. Click Settings to add one.");
        return;
    }

    int idx = endpointPicker_ ? endpointPicker_->GetSelectedIndex() : 0;
    if (idx < 0 || idx >= static_cast<int>(chatEndpoints_.size())) idx = 0;
    const Endpoint endpoint = chatEndpoints_[static_cast<size_t>(idx)];

    const std::string text = input_ ? input_->GetText() : "";
    if (text.empty() && pending_.empty()) {
        SetStatus("Type a message or attach a file first.");
        return;
    }

    // Build the user message (multimodal when attachments are present).
    Message um;
    um.role = Role::User;
    if (pending_.empty()) {
        um.text = text;
    } else {
        if (!text.empty()) um.parts.push_back(TextPart{text});
        for (const auto& a : pending_) {
            if (a.isImage) {
                ImagePart ip;
                ip.bytes = a.bytes;
                ip.mimeType = a.mimeType;
                um.parts.push_back(std::move(ip));
            } else {
                FilePart fp;
                fp.bytes = a.bytes;
                fp.mimeType = a.mimeType;
                fp.filename = a.filename;
                um.parts.push_back(std::move(fp));
            }
        }
    }
    messages_.push_back(um);

    // Echo the user turn into the transcript.
    std::ostringstream turn;
    turn << "**You**\n\n" << (text.empty() ? "*(no text)*" : text) << "\n";
    if (!pending_.empty()) {
        turn << "\n";
        for (const auto& a : pending_) turn << "📎 " << a.filename << "  ";
        turn << "\n";
    }
    turn << "\n**Assistant**\n\n";
    AppendTranscript(turn.str());

    // Reset the composer.
    if (input_) input_->SetText("");
    pending_.clear();
    RefreshAttachmentChips();
    assistantAccum_.clear();

    sending_ = true;
    if (sendButton_) sendButton_->SetText("Sending...");
    SetStatus("Contacting " + endpoint.providerId + "...");

    TextLLMConfig chatCfg = ToTextLLMConfig(endpoint);
    ChatRequest req;
    req.messages = messages_;
    req.model = endpoint.defaultModel;   // empty -> adapter default

    std::thread worker([this, alive = uiAlive_, chatCfg, req]() {
        auto post = [alive](std::function<void()> fn) {
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            auto guarded = [alive, fn = std::move(fn)]() {
                if (alive->load()) fn();
            };
            if (app) app->PostToUIThread(std::move(guarded));
            else guarded();
        };

        Error createError;
        auto llm = CreateTextLLM(chatCfg, &createError);
        if (!llm) {
            std::string msg = createError.message.empty()
                ? "Could not create the provider."
                : createError.message;
            post([this, msg]() { OnStreamFinished(msg); });
            return;
        }

        auto cb = [this, post](const StreamEvent& ev) {
            switch (ev.kind) {
                case StreamEventKind::TextDelta: {
                    std::string d = ev.textDelta;
                    post([this, d]() { OnStreamDelta(d); });
                    break;
                }
                case StreamEventKind::Error: {
                    std::string m = ev.error.message.empty()
                        ? "Stream error." : ev.error.message;
                    post([this, m]() { OnStreamFinished(m); });
                    break;
                }
                case StreamEventKind::Done:
                    post([this]() { OnStreamFinished(""); });
                    break;
                default:
                    break;  // ToolCall*/UsageUpdate — not surfaced yet
            }
        };

        auto handle = llm->ChatStream(req, cb);
        stream_ = handle;
        // Keep `llm` alive until the stream terminates, whether the adapter
        // ran the callback inline or on its own thread.
        while (handle && !handle->IsDone()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    worker.detach();
}

void UltraAIChatDialog::OnStreamDelta(const std::string& delta) {
    assistantAccum_ += delta;
    if (transcript_) transcript_->AppendText(delta);   // AppendText auto-scrolls
    SetStatus("");
}

void UltraAIChatDialog::OnStreamFinished(const std::string& errorMessage) {
    if (!errorMessage.empty()) {
        AppendTranscript("\n\n_" + errorMessage + "_");
    }
    if (!assistantAccum_.empty()) {
        Message am;
        am.role = Role::Assistant;
        am.text = assistantAccum_;
        messages_.push_back(std::move(am));
    }
    AppendTranscript("\n\n---\n\n");
    assistantAccum_.clear();

    sending_ = false;
    if (sendButton_) sendButton_->SetText("Send");
    SetStatus(errorMessage.empty() ? "" : "Error: " + errorMessage);
    stream_.reset();
}

void UltraAIChatDialog::OnAttach() {
    FileDialogOptions opts;
    opts.SetTitle("Attach a file")
        .SetParentWindow(this)
        .AddFilter("Images", std::vector<std::string>{"png", "jpg", "jpeg", "gif", "webp", "bmp"})
        .AddFilter("Documents", std::vector<std::string>{"pdf", "txt", "md", "json", "csv"})
        .AddFilter("All files", "*");

    UltraCanvasFileLoader::OpenFileDialog(
        opts, [this, alive = uiAlive_](DialogResult result, const std::string& path) {
            if (!alive->load()) return;
            if (result != DialogResult::OK || path.empty()) return;

            auto res = UltraCanvasFileLoader::LoadFile(path);
            if (!res.success) {
                SetStatus("Could not read " + BaseName(path) +
                          (res.error.empty() ? "" : ": " + res.error));
                return;
            }
            Attachment a;
            a.filename = BaseName(path);
            const auto mi = GuessMime(a.filename);
            a.mimeType = mi.mime;
            a.isImage  = mi.isImage;
            a.bytes    = std::move(res.bytes);
            pending_.push_back(std::move(a));
            RefreshAttachmentChips();
            SetStatus("Attached " + BaseName(path) + " — send to include it.");
        });
}

void UltraAIChatDialog::OnNewChat() {
    if (sending_) { SetStatus("Wait for the current reply to finish."); return; }
    messages_.clear();
    pending_.clear();
    assistantAccum_.clear();
    if (transcript_) transcript_->SetText(kGreeting);
    RefreshAttachmentChips();
    SetStatus("New conversation.");
}

void UltraAIChatDialog::OnOpenSettings() {
    auto dlg = std::make_shared<UltraAISettingsDialog>();
    dlg->CreateSettingsDialog();
    dlg->ShowModal(this);
    // Reflect any endpoint changes made while the dialog was open.
    RebuildEndpointPicker();
}

void UltraAIChatDialog::RefreshAttachmentChips() {
    if (!chipsLabel_) return;
    if (pending_.empty()) { chipsLabel_->SetText(""); return; }
    std::string s = "Attachments:  ";
    for (const auto& a : pending_) s += "📎 " + a.filename + "   ";
    chipsLabel_->SetText(s);
}

void UltraAIChatDialog::AppendTranscript(const std::string& markdown) {
    if (transcript_) transcript_->AppendText(markdown);
}

void UltraAIChatDialog::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

} // namespace UltraAIApp
