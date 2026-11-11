// Apps/UltraAIApp/UltraAIChatDialog.cpp
// Version: 0.3.0 - the playground panel
// Version: 0.1.0

#include "UltraAIChatDialog.h"

#include "UltraAILocalModelDialog.h"
#include "UltraAISettingsDialog.h"
#include "UltraAISettingsWidgets.h"

#include "UltraAI.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasTextUtils.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <thread>
#include <optional>
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

// "12,345" style token counts are for a person: grouped, in the locale's way
// is a nicety we leave out; plain digits read fine at these sizes.
std::string Tokens(int32_t n) { return std::to_string(n); }

std::string ShortenForTranscript(const std::string& text, size_t limit = 400) {
    if (text.size() <= limit) return text;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "…";
}

// Inline code in the markdown transcript: a backtick inside would end it.
std::string InlineCode(const std::string& text) {
    std::string clean;
    for (char c : text) clean += (c == '`') ? '\'' : (c == '\n' ? ' ' : c);
    return "`" + clean + "`";
}

constexpr const char* kToolsPlaceholder =
    R"([{"name": "get_weather", "description": "Weather for a city", )"
    R"("parameters": {"type": "object", "properties": {"city": {"type": "string"}}}}])";

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
    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    SetPadding(16);

    // ===== Endpoint row: picker (grows) + Settings + New chat =====
    auto topRow = std::make_shared<UltraCanvasContainer>("chat-toprow");
    topRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
    topRow->size.height = Dimension::Px(30);
    topRow->layoutItem.SetFlexShrink(0);

    endpointPicker_ = std::make_shared<UltraCanvasDropdown>("chat-endpoint", 0, 0, 0, 28);
    endpointPicker_->size.height = Dimension::Px(28);
    endpointPicker_->layoutItem.SetFlexGrow(1);
    endpointPicker_->onSelectionChanged = [this](int, const DropdownItem&) { RebuildModelPicker(); };
    topRow->AddChild(endpointPicker_);

    playgroundButton_ = std::make_shared<UltraCanvasButton>("chat-playground", 0, 0, 130, 28);
    playgroundButton_->SetText(Tr("Hide settings"));
    playgroundButton_->size.width = Dimension::Px(130);
    playgroundButton_->layoutItem.SetFlexShrink(0);
    playgroundButton_->onClick = [this]() { TogglePlayground(); };
    topRow->AddChild(playgroundButton_);

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

    // ===== Playground settings =====
    CreatePlaygroundPanel();

    // ===== Transcript (fills all slack) =====
    transcript_ = std::make_shared<UltraCanvasTextArea>("chat-transcript", 0, 0, 0, 0);
    transcript_->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    transcript_->SetReadOnly(true);
    transcript_->SetCaretVisible(false);   // read-only transcript: no blinking caret, still selectable
    transcript_->SetWordWrap(true);
    transcript_->SetText(kGreeting);
    transcript_->SetBorders(1.0f, Color(200, 200, 200, 255));
    transcript_->layoutItem.SetFlexGrow(1);
    AddChild(transcript_);

    // ===== Attachment chips =====
    chipsLabel_ = std::make_shared<UltraCanvasLabel>("chat-chips", 0, 0, 0, 18, "");
    chipsLabel_->size.height = Dimension::Px(18);
    chipsLabel_->layoutItem.SetFlexShrink(0);
    AddChild(chipsLabel_);

    // ===== Input =====
    input_ = std::make_shared<UltraCanvasTextArea>("chat-input", 0, 0, 0, 90);
    input_->SetEditingMode(TextAreaEditingMode::PlainText);   // plain text, not markdown
    input_->SetWordWrap(true);
    input_->SetPlaceholder("Type a message...  (Enter sends, Shift+Enter for a new line)");
    input_->size.height = Dimension::Px(90);
    input_->SetBorders(1.0f, Color(200, 200, 200, 255));
    input_->layoutItem.SetFlexShrink(0);
    // Enter sends, Shift+Enter starts a new line - the text area hands the key
    // to onBeforeKeyDown before it would insert the line break.
    input_->onBeforeKeyDown = [this](const UCEvent& e) {
        const bool enter = e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter;
        if (!enter || e.shift) return false;
        OnSend();   // ignores the key while a reply is still on its way
        return true;
    };
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

void UltraAIChatDialog::RebuildEndpointPicker(const std::string& selectId) {
    chatEndpoints_ = EndpointStore::Instance().ForCapability(AICapability::Chat);
    if (!endpointPicker_) return;

    endpointPicker_->ClearItems();
    if (chatEndpoints_.empty()) {
        endpointPicker_->AddItem("(no chat endpoint — open Settings)");
        endpointPicker_->SetSelectedIndex(0, false);
        SetStatus("No chat endpoint configured. Click Settings to add one.");
        return;
    }
    // Keep the caller's endpoint selected (by its stable id) across a rebuild;
    // fall back to the first when it's empty or no longer in the list.
    int selectIndex = 0;
    for (size_t i = 0; i < chatEndpoints_.size(); ++i) {
        const auto& e = chatEndpoints_[i];
        endpointPicker_->AddItem((e.name.empty() ? e.id : e.name) +
                                 "   (" + e.providerId + ")");
        if (!selectId.empty() && e.id == selectId) selectIndex = static_cast<int>(i);
    }
    endpointPicker_->SetSelectedIndex(selectIndex, false);
    SetStatus("");
    RebuildModelPicker();
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
        SetStatus(pendingToolCalls_.empty() ? "Type a message or attach a file first."
                                            : Tr("Type the tool's result first."));
        return;
    }
    {
        // A bad number or tools JSON is reported before anything changes.
        ChatRequest probe;
        std::string error;
        if (!ApplyPlayground(probe, endpoint, error)) {
            SetStatus(error);
            return;
        }
    }

    // The model asked for tools: this message is their result.
    if (!pendingToolCalls_.empty()) {
        for (const auto& call : pendingToolCalls_) {
            Message tm;
            tm.role = Role::Tool;
            tm.toolCallId = call.id;
            tm.name = call.name;
            tm.text = text;
            messages_.push_back(std::move(tm));
        }
        pendingToolCalls_.clear();
        AppendTranscript("**" + Tr("Tool result") + "**\n\n" + text + "\n\n**Assistant**\n\n");
        if (input_) input_->SetText("");
        assistantAccum_.clear();
        StartStream(endpoint);
        return;
    }

    // A user turn left at the end of the history was never answered: the
    // stream failed before it produced a reply, or the "Get a local model"
    // dialog was dismissed. Replace it instead of appending after it, or the
    // provider sees two user messages in a row - a chat template that
    // demands alternating roles (Gemma on LocalAI, say) refuses the whole
    // request with "Conversation roles must alternate user/assistant".
    if (!messages_.empty() && messages_.back().role == Role::User) {
        messages_.pop_back();
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

    StartStream(endpoint);
}

void UltraAIChatDialog::StartStream(const Endpoint& endpoint) {
    if (sending_) return;
    // Name the endpoint the user picked (e.g. "ai.ultraos.eu"), not its
    // provider id — the server being contacted is the endpoint, not "openai".
    const std::string epLabel = endpoint.name.empty() ? endpoint.id : endpoint.name;
    TextLLMConfig chatCfg = ToTextLLMConfig(endpoint);
    ChatRequest req;
    req.messages = messages_;
    req.model = endpoint.defaultModel;   // empty -> adapter default
    std::string playgroundError;
    ApplyPlayground(req, endpoint, playgroundError);   // checked in OnSend
    lastModel_ = req.model;
    streamToolCalls_.clear();
    lastUsage_ = TokenUsage{};

    sending_ = true;
    if (sendButton_) sendButton_->SetText("Sending...");
    SetStatus("Contacting " + epLabel + "...");

    std::thread worker([this, alive = uiAlive_, chatCfg, req, endpoint]() {
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
            if (createError.providerCode == "needs-install") {
                post([this, endpoint, msg]() { OnNeedsLocalModel(endpoint, msg); });
                return;
            }
            post([this, msg]() { OnStreamFinished(msg); });
            return;
        }

        auto cb = [this, post, endpoint](const StreamEvent& ev) {
            switch (ev.kind) {
                case StreamEventKind::TextDelta: {
                    std::string d = ev.textDelta;
                    post([this, d]() { OnStreamDelta(d); });
                    break;
                }
                case StreamEventKind::Error: {
                    std::string m = ev.error.message.empty()
                        ? "Stream error." : ev.error.message;
                    if (ev.error.providerCode == "needs-install") {
                        post([this, endpoint, m]() { OnNeedsLocalModel(endpoint, m); });
                        break;
                    }
                    post([this, m]() { OnStreamFinished(m); });
                    break;
                }
                case StreamEventKind::ToolCallEnd: {
                    ToolCall call = ev.toolCallDelta;
                    post([this, call]() { OnStreamToolCall(call); });
                    break;
                }
                case StreamEventKind::UsageUpdate: {
                    TokenUsage u = ev.usage;
                    post([this, u]() { OnStreamUsage(u); });
                    break;
                }
                case StreamEventKind::Done: {
                    TokenUsage u = ev.usage;
                    post([this, u]() {
                        if (u.inputTokens || u.outputTokens) OnStreamUsage(u);
                        OnStreamFinished("");
                    });
                    break;
                }
                default:
                    break;  // ToolCallDelta: the finished call arrives as ToolCallEnd
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

void UltraAIChatDialog::OnNeedsLocalModel(const Endpoint& endpoint, const std::string& message) {
    // The turn is not over: the transcript keeps its "Assistant" heading and
    // the conversation its last user message, so a resend continues it.
    sending_ = false;
    if (sendButton_) sendButton_->SetText("Send");
    SetStatus(message);
    stream_.reset();
    UltraAILocalModelDialog::Show(this, "Chat", "textllm", endpoint.defaultModel,
        [this, alive = uiAlive_, endpoint](const std::string& modelId) {
            if (!alive->load()) return;
            Endpoint resolved = endpoint;
            if (resolved.defaultModel.empty()) resolved.defaultModel = modelId;
            SetStatus("Model installed; sending...");
            StartStream(resolved);
        });
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
    if (!assistantAccum_.empty() || !streamToolCalls_.empty()) {
        Message am;
        am.role = Role::Assistant;
        am.text = assistantAccum_;
        am.toolCalls = streamToolCalls_;
        messages_.push_back(std::move(am));
        if (!streamToolCalls_.empty()) {
            // A playground has no code behind the tools: the user answers.
            std::string calls;
            for (const auto& c : streamToolCalls_) {
                calls += "\n\n🔧 **" + c.name + "** " +
                         InlineCode(ShortenForTranscript(c.argumentsJson.empty() ? "{}" : c.argumentsJson));
            }
            AppendTranscript(calls);
            pendingToolCalls_ = streamToolCalls_;
        }
    } else if (!errorMessage.empty() && !messages_.empty() &&
               messages_.back().role == Role::Tool) {
        // The tool results were sent but nothing came back: take them out
        // and ask for them again, so the conversation stays one a provider
        // accepts (every tool call answered exactly once).
        while (!messages_.empty() && messages_.back().role == Role::Tool) messages_.pop_back();
        if (!messages_.empty()) pendingToolCalls_ = messages_.back().toolCalls;
    } else if (!errorMessage.empty() && !messages_.empty() &&
               messages_.back().role == Role::User) {
        // The request failed before the model said anything, so the turn
        // never happened: take the user message back out of the history.
        // Left in, the next send would follow it with a second user message
        // and a provider whose chat template requires alternating roles
        // rejects the conversation (seen with LiteLLM + LocalAI: both the
        // failed and the new question arrived as consecutive "user" turns).
        // The transcript keeps the echo and the error so the user sees what
        // was tried; a partial reply above is kept as the assistant turn.
        messages_.pop_back();
    }
    AppendTranscript("\n\n---\n\n");
    assistantAccum_.clear();
    streamToolCalls_.clear();

    sending_ = false;
    if (sendButton_) sendButton_->SetText("Send");
    if (!errorMessage.empty()) SetStatus("Error: " + errorMessage);
    else ShowUsage();
    stream_.reset();
}

void UltraAIChatDialog::OnStreamToolCall(const ToolCall& call) {
    streamToolCalls_.push_back(call);
}

void UltraAIChatDialog::OnStreamUsage(const TokenUsage& usage) {
    lastUsage_ = usage;
}

void UltraAIChatDialog::ShowUsage() {
    chatUsage_.inputTokens  += lastUsage_.inputTokens;
    chatUsage_.outputTokens += lastUsage_.outputTokens;
    std::string text = TrF("{0} tokens in, {1} out  ·  this chat: {2} in, {3} out",
                           Tokens(lastUsage_.inputTokens), Tokens(lastUsage_.outputTokens),
                           Tokens(chatUsage_.inputTokens), Tokens(chatUsage_.outputTokens));
    if (!lastModel_.empty()) text = lastModel_ + "  ·  " + text;
    if (!pendingToolCalls_.empty()) {
        text = TrF("The model asked for {0}: type the result and send it.  ", 
                   pendingToolCalls_.size() == 1 ? pendingToolCalls_[0].name
                                                 : Tr("several tools")) + text;
    }
    SetStatus(text);
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
    pendingToolCalls_.clear();
    streamToolCalls_.clear();
    chatUsage_ = TokenUsage{};
    if (transcript_) transcript_->SetText(kGreeting);
    RefreshAttachmentChips();
    SetStatus("New conversation.");
}

void UltraAIChatDialog::OnOpenSettings() {
    // Open the settings dialog on the endpoint currently selected in the chat
    // picker so it can be edited directly, rather than the default "New endpoint".
    std::string selectId;
    if (endpointPicker_) {
        const int idx = endpointPicker_->GetSelectedIndex();
        if (idx >= 0 && idx < static_cast<int>(chatEndpoints_.size())) {
            selectId = chatEndpoints_[static_cast<size_t>(idx)].id;
        }
    }

    auto dlg = std::make_shared<UltraAISettingsDialog>();
    dlg->CreateSettingsDialog(selectId);
    dlg->ShowModal(this);
    // Reflect any endpoint changes made while the dialog was open, keeping the
    // endpoint the user had selected rather than snapping back to the first.
    RebuildEndpointPicker(selectId);
}

// ===== Playground =====

void UltraAIChatDialog::CreatePlaygroundPanel() {
    using UltraCanvas::CSSLayout::Dimension;
    using UltraCanvas::CSSLayout::AlignItems;
    using SettingsWidgets::MakeLabel;

    playgroundPanel_ = std::make_shared<UltraCanvasContainer>("chat-playground-panel");
    playgroundPanel_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(AlignItems::Stretch);
    playgroundPanel_->layoutItem.SetFlexShrink(0);

    auto row = [](const std::string& id) {
        auto r = std::make_shared<UltraCanvasContainer>(id);
        r->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Center);
        r->size.height = Dimension::Px(26);
        r->layoutItem.SetFlexShrink(0);
        return r;
    };
    auto number = [](const std::string& id, int width, const std::string& placeholder) {
        auto input = std::make_shared<UltraCanvasTextInput>(id, 0, 0, width, 24);
        input->SetPlaceholder(placeholder);
        input->size.width = Dimension::Px(width);
        input->layoutItem.SetFlexShrink(0);
        return input;
    };

    // Model.
    auto modelRow = row("chat-pg-model");
    modelRow->AddChild(MakeLabel("chat-pg-model-label", Tr("Model")));
    modelPicker_ = std::make_shared<UltraCanvasDropdown>("chat-pg-model-picker", 0, 0, 0, 24);
    modelPicker_->size.height = Dimension::Px(24);
    modelPicker_->layoutItem.SetFlexGrow(1);
    modelRow->AddChild(modelPicker_);
    modelRow->AddChild(MakeLabel("chat-pg-model-or", Tr("or")));
    customModel_ = number("chat-pg-model-custom", 220, Tr("another model id"));
    modelRow->AddChild(customModel_);
    playgroundPanel_->AddChild(modelRow);

    // Sampling.
    auto samplingRow = row("chat-pg-sampling");
    samplingRow->AddChild(MakeLabel("chat-pg-temp-label", Tr("Temperature")));
    temperature_ = number("chat-pg-temp", 70, Tr("default"));
    samplingRow->AddChild(temperature_);
    samplingRow->AddChild(MakeLabel("chat-pg-topp-label", Tr("Top p")));
    topP_ = number("chat-pg-topp", 70, Tr("default"));
    samplingRow->AddChild(topP_);
    samplingRow->AddChild(MakeLabel("chat-pg-max-label", Tr("Most output tokens")));
    maxTokens_ = number("chat-pg-max", 90, Tr("default"));
    samplingRow->AddChild(maxTokens_);
    playgroundPanel_->AddChild(samplingRow);

    // System prompt and tools, side by side.
    auto textRow = std::make_shared<UltraCanvasContainer>("chat-pg-texts");
    textRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(AlignItems::Stretch);
    textRow->size.height = Dimension::Px(92);
    textRow->layoutItem.SetFlexShrink(0);
    auto column = [](const std::string& id, const std::string& label,
                     std::shared_ptr<UltraCanvasTextArea>& area, const std::string& placeholder) {
        auto col = std::make_shared<UltraCanvasContainer>(id);
        col->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(AlignItems::Stretch);
        col->layoutItem.SetFlexGrow(1).SetFlexBasis(Dimension::Px(0));
        col->AddChild(MakeLabel(id + "-label", label));
        area = std::make_shared<UltraCanvasTextArea>(id + "-text", 0, 0, 0, 0);
        area->SetEditingMode(TextAreaEditingMode::PlainText);
        area->SetWordWrap(true);
        area->SetPlaceholder(placeholder);
        area->SetBorders(1.0f, Color(200, 200, 200, 255));
        area->layoutItem.SetFlexGrow(1);
        col->AddChild(area);
        return col;
    };
    textRow->AddChild(column("chat-pg-system", Tr("System prompt"), systemPrompt_,
                             Tr("How the model should behave, e.g. \"Answer in one sentence.\"")));
    textRow->AddChild(column("chat-pg-tools", Tr("Tools (JSON)"), toolsJson_, kToolsPlaceholder));
    playgroundPanel_->AddChild(textRow);

    AddChild(playgroundPanel_);
}

void UltraAIChatDialog::TogglePlayground() {
    if (!playgroundPanel_) return;
    const bool show = !playgroundPanel_->IsVisible();
    playgroundPanel_->SetVisible(show);
    if (playgroundButton_) playgroundButton_->SetText(show ? Tr("Hide settings") : Tr("Show settings"));
    InvalidateLayout();
}

void UltraAIChatDialog::RebuildModelPicker() {
    if (!modelPicker_) return;
    modelPicker_->ClearItems();
    pickerModels_.clear();
    int idx = endpointPicker_ ? endpointPicker_->GetSelectedIndex() : 0;
    if (chatEndpoints_.empty() || idx < 0 || idx >= static_cast<int>(chatEndpoints_.size())) {
        modelPicker_->AddItem(Tr("(the provider's default)"));
        pickerModels_.push_back("");
        modelPicker_->SetSelectedIndex(0, false);
        return;
    }
    const Endpoint& e = chatEndpoints_[static_cast<size_t>(idx)];
    modelPicker_->AddItem(e.defaultModel.empty()
        ? Tr("(the provider's default)")
        : TrF("{0} (the endpoint's default)", e.defaultModel));
    pickerModels_.push_back(e.defaultModel);
    for (const auto& m : e.models) {
        if (m.name.empty() || m.name == e.defaultModel) continue;
        if (!m.mode.empty() && m.mode != "chat") continue;   // embeddings, images, ...
        modelPicker_->AddItem(m.name);
        pickerModels_.push_back(m.name);
    }
    modelPicker_->SetSelectedIndex(0, false);
}

std::string UltraAIChatDialog::ChosenModel(const Endpoint& endpoint) const {
    if (customModel_ && !customModel_->GetText().empty()) return customModel_->GetText();
    if (modelPicker_) {
        const int i = modelPicker_->GetSelectedIndex();
        if (i >= 0 && i < static_cast<int>(pickerModels_.size()) && !pickerModels_[static_cast<size_t>(i)].empty()) {
            return pickerModels_[static_cast<size_t>(i)];
        }
    }
    return endpoint.defaultModel;
}

bool UltraAIChatDialog::ApplyPlayground(ChatRequest& req, const Endpoint& endpoint,
                                        std::string& error) const {
    req.model = ChosenModel(endpoint);

    auto readNumber = [&error](const std::shared_ptr<UltraCanvasTextInput>& input,
                               const std::string& name, double low, double high,
                               std::optional<double>& out) {
        out.reset();
        if (!input || input->GetText().empty()) return true;
        double value = 0;
        if (!TryParseFloat(input->GetText(), value) || value < low || value > high) {
            error = TrF("{0} must be a number from {1} to {2}.", name, FormatFloatClassic(low), FormatFloatClassic(high));
            return false;
        }
        out = value;
        return true;
    };
    std::optional<double> temperature, topP, maxTokens;
    if (!readNumber(temperature_, Tr("Temperature"), 0, 2, temperature)) return false;
    if (!readNumber(topP_, Tr("Top p"), 0, 1, topP)) return false;
    if (!readNumber(maxTokens_, Tr("Most output tokens"), 1, 1000000, maxTokens)) return false;
    if (temperature) req.sampling.temperature = *temperature;
    if (topP) req.sampling.topP = *topP;
    if (maxTokens) req.sampling.maxOutputTokens = static_cast<int32_t>(*maxTokens);

    if (systemPrompt_ && !systemPrompt_->GetText().empty()) {
        Message sys;
        sys.role = Role::System;
        sys.text = systemPrompt_->GetText();
        req.messages.insert(req.messages.begin(), std::move(sys));
    }

    // Tools: a JSON array of {"name", "description", "parameters"} - the
    // parameters being a JSON Schema ("input_schema" is taken too).
    const std::string toolsText = toolsJson_ ? toolsJson_->GetText() : std::string{};
    if (!toolsText.empty()) {
        JSONParseResult parsed;
        JSONValue root = JSON::Parse(toolsText, &parsed);
        if (!parsed.success || !(root.IsArray() || root.IsObject())) {
            error = Tr("The tools are not valid JSON: give an array of {\"name\", \"description\", \"parameters\"} objects.");
            return false;
        }
        auto addTool = [&req, &error](const JSONValue& t) {
            if (!t.IsObject() || t.Get("name").GetString().empty()) {
                error = Tr("Every tool needs a \"name\".");
                return false;
            }
            ToolDefinition def;
            def.name = t.Get("name").GetString();
            def.description = t.Get("description").GetString();
            const JSONValue& params = t.Get("parameters").IsObject() ? t.Get("parameters")
                                                                     : t.Get("input_schema");
            def.parametersJsonSchema = params.IsObject() ? JSON::Serialize(params)
                                                         : R"({"type":"object","properties":{}})";
            req.tools.push_back(std::move(def));
            return true;
        };
        if (root.IsObject()) {
            if (!addTool(root)) return false;
        } else {
            for (size_t i = 0; i < root.GetSize(); ++i) {
                if (!addTool(root.At(i))) return false;
            }
        }
    }
    return true;
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
