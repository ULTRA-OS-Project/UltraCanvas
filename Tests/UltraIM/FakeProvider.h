// Tests/UltraIM/FakeProvider.h
// A provider that answers from memory, for the UltraIM tests
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraIM/UltraIMPlugins.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class FakeProvider : public IUltraIMProviderPlugin {
public:
    explicit FakeProvider(std::string id, bool connected = true)
        : id_(std::move(id)), connected_(connected) {}

    std::string GetProviderId() const override { return id_; }
    std::string GetDisplayName() const override { return "Fake " + id_; }
    std::string GetVersion() const override { return "1.0"; }
    UltraIMCapabilityFlags GetCapabilities() const override {
        return UltraIMCapabilityFlags::TextMessages | UltraIMCapabilityFlags::VoiceCalls;
    }
    UltraIMLoginType GetLoginType() const override { return UltraIMLoginType::KeyPair; }

    UltraIMResult Initialize(const UltraIMConfig& config, UltraIMEventSink sink) override {
        (void)config;
        sink_ = std::move(sink);
        return UltraIMResult::Ok();
    }
    void Shutdown() override { shutDown = true; }

    void SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) override {
        (void)credentials;
        connected_ = true;
        onResult(UltraIMResult::Ok());
    }
    void SignOut() override { connected_ = false; }
    UltraIMConnectionState GetConnectionState() const override {
        return connected_ ? UltraIMConnectionState::Connected : UltraIMConnectionState::Disconnected;
    }

    void FetchConversations(UltraIMConversationsCallback onResult) override {
        onResult(UltraIMResult::Ok(), conversations);
    }
    void FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                       UltraIMMessagesCallback onResult) override {
        (void)beforeMessageId; (void)limit;
        UltraIMMessage message;
        message.conversationId = conversationId;
        message.text = "history";
        onResult(UltraIMResult::Ok(), {message});
    }

    UltraIMResult SendText(const std::string& conversationId, const std::string& text,
                           const UltraIMSendOptions& options) override {
        std::lock_guard<std::mutex> lock(mutex);
        lastConversation = conversationId;
        lastText = text;
        lastClientId = options.clientMessageId;
        return UltraIMResult::Ok();
    }

    bool CanCall(const std::string& conversationId, bool video) const override {
        (void)video;
        return conversationId == "direct";
    }
    UltraIMResult StartCall(const std::string& conversationId, bool video, std::string& outCallId) override {
        (void)video;
        outCallId = "call-" + conversationId;
        return UltraIMResult::Ok();
    }

    void FetchSharedCredentials(const std::string& forProviderId, UltraIMSharedCredentialsCallback onResult) override {
        if (forProviderId != "telegram")
            return onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "none for " + forProviderId), {});
        UltraIMCredentials c;
        c.apiId = "1017053";
        c.apiHash = "0123456789abcdef0123456789abcdef";
        onResult(UltraIMResult::Ok(), c);
    }

    void Raise(const UltraIMEvent& event) {
        if (sink_) sink_(event);
    }

    std::vector<UltraIMConversation> conversations;
    std::atomic<bool> shutDown{false};
    std::mutex mutex;
    std::string lastConversation, lastText, lastClientId;

private:
    std::string id_;
    std::atomic<bool> connected_;
    UltraIMEventSink sink_;
};
