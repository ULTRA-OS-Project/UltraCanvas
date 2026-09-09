// Apps/UltraAIApp/UltraAIChatDialog.h
// A ChatGPT/Claude-style conversation dialog for the LLM capability. Unlike
// the single-turn service dialogs, this one keeps the whole conversation,
// streams the assistant's reply token-by-token into a markdown transcript,
// and lets the user attach images and generic files to a message.
//
// The provider comes from the Settings dialog: the endpoint selector lists
// every configured endpoint whose modes include Chat.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"

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

    void RebuildEndpointPicker();
    void OnSend();
    void OnAttach();
    void OnNewChat();
    void OnOpenSettings();

    void RefreshAttachmentChips();
    void AppendTranscript(const std::string& markdown);
    void SetStatus(const std::string& text);

    // Marshalled onto the UI thread from the streaming callback.
    void OnStreamDelta(const std::string& delta);
    void OnStreamFinished(const std::string& errorMessage);

    // Widgets.
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  endpointPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  transcript_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     chipsLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  input_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    sendButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;

    // Endpoints offering Chat, aligned with the picker's item indices.
    std::vector<Endpoint> chatEndpoints_;

    // Conversation state.
    std::vector<UltraAI::Message> messages_;   // full multi-turn history
    std::vector<Attachment> pending_;          // attachments for the next turn
    std::string assistantAccum_;               // text streamed for the live reply
    bool sending_ = false;

    // Cancel the in-flight stream when the dialog closes.
    UltraAI::StreamHandle stream_;

    // Flipped false in the destructor so a late stream delivery becomes a
    // no-op instead of calling into a destroyed dialog (the RunOffThread
    // pattern from UltraAIServiceDialog).
    std::shared_ptr<std::atomic<bool>> uiAlive_ =
        std::make_shared<std::atomic<bool>>(true);

    static constexpr long kW      = 860;
    static constexpr long kH      = 680;
    static constexpr long kMargin = 16;
};

} // namespace UltraAIApp
