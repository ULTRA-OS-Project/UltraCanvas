// Apps/UltraAIApp/UltraAIChatDialog.h
// A ChatGPT/Claude-style conversation dialog for the LLM capability. Unlike
// the single-turn service dialogs, this one keeps the whole conversation,
// streams the assistant's reply token-by-token into a markdown transcript,
// and lets the user attach images and generic files to a message.
//
// The provider comes from the Settings dialog: the endpoint selector lists
// every configured endpoint whose modes include Chat.
//
// It is also the module's playground: a settings panel above the
// transcript (Hide settings / Show settings) picks the model, sets the
// system prompt, temperature, top p and the most output tokens, and
// declares tools as JSON. When the model calls a tool the call is shown
// and the next message is sent back as its result. The status line reads
// out the tokens of the last reply and of the whole chat.
// Version: 0.3.0 - the playground: model, system prompt, sampling, tools, token usage
// Version: 0.2.0 - a model the local runtime has not installed is offered for download
// Version: 0.1.0
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"

#include "UltraAIEndpoints.h"
#include "UltraAITextLLM.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAIChatDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    UltraAIChatDialog();
    ~UltraAIChatDialog() override;

    void CreateChatDialog();

private:
    // A file the user attached to the next message.
    struct Attachment {
        std::string filename;
        std::string mimeType;
        std::vector<uint8_t> bytes;
        bool isImage = false;
    };

    // Rebuild the picker from the Chat endpoints. When `selectId` names an
    // endpoint still in the list its row stays selected; otherwise the first
    // endpoint is chosen (first open, or the selected one was just deleted).
    void RebuildEndpointPicker(const std::string& selectId = "");
    void OnSend();
    // Stream the conversation so far to `endpoint`: the second half of
    // OnSend, run again after the "Get a local model" dialog installed what
    // the endpoint needs.
    void StartStream(const Endpoint& endpoint);
    // The `local` provider answered "needs-install": offer the download
    // (UltraAILocalModelDialog) and resend when it is done.
    void OnNeedsLocalModel(const Endpoint& endpoint, const std::string& message);
    void OnAttach();
    void OnNewChat();
    void OnOpenSettings();

    // ===== Playground =====
    void CreatePlaygroundPanel();
    void TogglePlayground();
    // The model choices of the selected endpoint: its default and the models
    // its server advertised.
    void RebuildModelPicker();
    // Model, system prompt, sampling and tools from the panel, applied to
    // `req`. False (and `error`) when a number or the tools JSON is unusable.
    bool ApplyPlayground(UltraAI::ChatRequest& req, const Endpoint& endpoint,
                         std::string& error) const;
    std::string ChosenModel(const Endpoint& endpoint) const;
    void ShowUsage();

    void RefreshAttachmentChips();
    void AppendTranscript(const std::string& markdown);
    void SetStatus(const std::string& text);

    // Marshalled onto the UI thread from the streaming callback.
    void OnStreamDelta(const std::string& delta);
    void OnStreamToolCall(const UltraAI::ToolCall& call);
    void OnStreamUsage(const UltraAI::TokenUsage& usage);
    void OnStreamFinished(const std::string& errorMessage);

    // Widgets.
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  endpointPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  transcript_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     chipsLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  input_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    sendButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;

    // Playground panel.
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    playgroundButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> playgroundPanel_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  modelPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> customModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> temperature_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> topP_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> maxTokens_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  systemPrompt_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  toolsJson_;
    std::vector<std::string> pickerModels_;   // aligned with modelPicker_ (index 0: endpoint default)

    // Endpoints offering Chat, aligned with the picker's item indices.
    std::vector<Endpoint> chatEndpoints_;

    // Conversation state.
    std::vector<UltraAI::Message> messages_;   // full multi-turn history
    std::vector<Attachment> pending_;          // attachments for the next turn
    std::string assistantAccum_;               // text streamed for the live reply
    std::vector<UltraAI::ToolCall> streamToolCalls_;   // tool calls of the live reply
    std::vector<UltraAI::ToolCall> pendingToolCalls_;  // waiting for the user's result
    UltraAI::TokenUsage lastUsage_;            // the last reply's tokens
    UltraAI::TokenUsage chatUsage_;            // the whole conversation's
    std::string lastModel_;
    bool sending_ = false;

    // Cancel the in-flight stream when the dialog closes.
    UltraAI::StreamHandle stream_;

    // Flipped false in the destructor so a late stream delivery becomes a
    // no-op instead of calling into a destroyed dialog (the RunOffThread
    // pattern from UltraAIServiceDialog).
    std::shared_ptr<std::atomic<bool>> uiAlive_ =
        std::make_shared<std::atomic<bool>>(true);

    static constexpr long kW      = 860;
    static constexpr long kH      = 780;
    static constexpr long kMargin = 16;
};

} // namespace UltraAIApp
