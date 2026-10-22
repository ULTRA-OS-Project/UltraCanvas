# ChatCube Developer Guide
# Version: 1.0.0
# Last Modified: 2026-04-13
# Author: UltraCanvas Framework / Cloverleaf

---

## Overview

ChatCube is a multi-protocol instant messenger built on the UltraCanvas API.
It has two goals:

1. **Plugin framework** — any messenger that offers login and API access
   can be integrated as a `.module` plugin.
2. **Native P2P** — a built-in ChatCube Direct plugin provides peer-to-peer
   messaging with optional server backup, no third-party account required.

---

## Project structure

```
ChatCube/
├── AppInfo.txt                         UOS bundle metadata
├── CMakeLists.txt                      Cross-platform build
├── main.cpp                            Application entry point
│
├── include/
│   ├── ChatCubeTypes.h                 All core data types and enums
│   ├── IChatCubeProviderPlugin.h       Plugin interface contract
│   ├── ChatCubeProviderManager.h       Plugin manager (singleton)
│   ├── ChatMessageModel.h              ListView data model
│   ├── ChatBubbleDelegate.h            Chat bubble renderer
│   └── ChatCubeWindow.h               Main application window
│
├── core/
│   ├── ChatCubeTypes.cpp
│   ├── ChatMessageModel.cpp
│   ├── ChatBubbleDelegate.cpp
│   ├── ChatCubeProviderManager.cpp
│   └── ChatCubeWindow.cpp
│
├── Plugins/
│   ├── Matrix/
│   │   ├── MatrixProviderPlugin.h
│   │   └── MatrixProviderPlugin.cpp    Reference implementation
│   ├── Telegram/                       (future)
│   ├── XMPP/                           (future)
│   └── Direct/                         (future — P2P plugin)
│
└── Resources/
    ├── Media/
    │   ├── Images/
    │   │   ├── providers/              Provider logo icons
    │   │   └── icons/                  Toolbar icons
    │   ├── Audio/
    │   └── Video/
    ├── Locale/                         Translation strings
    ├── Data/
    │   └── cache/
    │       ├── avatars/
    │       └── attachments/
    ├── Frameworks/
    └── Modules/                        Built .module plugin files
```

---

## Building

### Prerequisites

- CMake 3.16+
- C++20 compiler (GCC 11+, Clang 13+, MSVC 2022+)
- UltraCanvas framework (built separately)
- libcurl (for HTTP in Matrix plugin)
- nlohmann/json (auto-fetched if not present)

### Build steps

```bash
# From the ChatCube directory
mkdir build && cd build

cmake .. \
    -DULTRACANVAS_DIR=/path/to/UltraCanvas \
    -DCMAKE_BUILD_TYPE=Release \
    -DCHATCUBE_BUILD_MATRIX_PLUGIN=ON

cmake --build . --parallel

# Output: build/ChatCube.app/
#         build/ChatCube.app/ChatCube  (executable)
#         build/ChatCube.app/Resources/Modules/MatrixPlugin.module
```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `ULTRACANVAS_DIR` | `../UltraCanvas` | Path to UltraCanvas root |
| `CHATCUBE_BUILD_MATRIX_PLUGIN` | ON | Build Matrix plugin |
| `CHATCUBE_BUILD_TELEGRAM_PLUGIN` | OFF | Build Telegram plugin |
| `CHATCUBE_BUILD_XMPP_PLUGIN` | OFF | Build XMPP plugin |
| `CHATCUBE_BUILD_DIRECT_PLUGIN` | ON | Build P2P Direct plugin |
| `CHATCUBE_HTTP_BACKEND_CURL` | ON | Use libcurl for HTTP |

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  ChatCubeWindow  (UltraCanvasWindow)                     │
│  ┌──────────────────────────────────────────────────┐    │
│  │ Left panel                                        │    │
│  │  Toolbar + contact list (UltraCanvasTemplate)    │    │
│  ├──────────────────────────────────────────────────┤    │
│  │ Right panel                                       │    │
│  │  Header toolbar + ListView(ChatBubbleDelegate)   │    │
│  │  Input toolbar + TextArea + send button          │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
          │ calls                          ▲ fires callbacks
          ▼                                │
┌──────────────────────────────────────────────────────────┐
│  ChatCubeProviderManager  (singleton)                    │
│  Routes send calls → plugins                             │
│  Aggregates push events ← plugins                        │
│  Owns ChatMessageModel ref for active conversation        │
└──────────────────────────────────────────────────────────┘
          │ implements
          ▼
┌───────────────────┐  ┌─────────────────┐  ┌────────────┐
│  MatrixPlugin     │  │  TelegramPlugin  │  │  Direct    │
│  HTTP+JSON REST   │  │  TDLib           │  │  WebRTC P2P│
│  Long-poll /sync  │  │  MTProto         │  │  + relay   │
└───────────────────┘  └─────────────────┘  └────────────┘
```

### Key data flow — incoming message

```
Network  →  Plugin worker thread
         →  eventCallback_(ChatPluginEvent{MessageReceived})
         →  ChatCubeProviderManager::HandlePluginEvent()
         →  RouteIncomingMessage()  →  model->AppendMessage()
         →  ListView::onRowInserted  →  ScrollToRow()
         →  onMessageReceive()  →  ChatCubeWindow::OnIncomingMessage()
         →  RebuildContactList()  (updates unread badge)
```

### Key data flow — sending a message

```
User types + presses Enter / Send button
→  ChatCubeWindow::OnSendButtonClick()
→  model->AppendMessage(optimistic local message, status=Sending)
→  messageInput_->SetText("")
→  ChatCubeProviderManager::SendTextMessage(providerId, convId, text)
→  Plugin::SendTextMessage()  →  HTTP PUT /rooms/{id}/send/...
→  On success:  model->UpdateDeliveryStatus(msgId, Sent)
→  On failure:  model->UpdateDeliveryStatus(msgId, Failed)
```

---

## Adding a new provider plugin

### Step 1 — Create the plugin files

```
Plugins/MyProvider/
    MyProviderPlugin.h
    MyProviderPlugin.cpp
```

### Step 2 — Inherit IChatCubeProviderPlugin

```cpp
// MyProviderPlugin.h
#include "IChatCubeProviderPlugin.h"

namespace UltraCanvas { namespace ChatCube {

class MyProviderPlugin : public IChatCubeProviderPlugin {
public:
    // Section 1 — Identity (required)
    std::string GetPluginId()      const override { return "myprovider"; }
    std::string GetDisplayName()   const override { return "My Provider"; }
    std::string GetPluginVersion() const override { return "1.0.0"; }
    std::string GetIconPath()      const override {
        return "Resources/Media/Images/providers/myprovider.png";
    }
    ChatCapabilityFlags GetCapabilities() const override {
        return ChatCapabilityFlags::TextMessages
             | ChatCapabilityFlags::FileTransfer
             | ChatCapabilityFlags::ReadReceipts;
    }
    ChatLoginType GetLoginType() const override {
        return ChatLoginType::UsernamePassword;
    }

    // Section 2 — Connection (required)
    void Authenticate(const ChatCredentials& creds,
                      ChatAuthResultCallback onResult) override;
    void RequestQRCode(std::function<void(bool, const std::string&)> onResult) override {
        if (onResult) onResult(false, "Not supported");
    }
    void Disconnect() override;
    ChatConnectionState GetConnectionState() const override;

    // Section 3 — Data retrieval (required)
    void GetContacts(ChatContactsCallback onResult) override;
    void GetConversations(
        std::function<void(const std::vector<ChatConversation>&)> onResult) override;
    void GetMessages(const std::string& convId,
                     const std::string& beforeId,
                     int limit,
                     ChatMessagesCallback onResult) override;

    // Section 4 — Send text (required)
    ChatPluginResult SendTextMessage(const std::string& convId,
                                     const std::string& text,
                                     const ChatSendOptions& opts = {}) override;

    // Sections 5-10: implement what your provider supports.
    // For unsupported operations return:
    //   ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing)
    // ... (all pure virtual methods must be overridden)

    void SubscribeToEvents(ChatPluginEventCallback cb) override {
        eventCallback_ = cb;
    }
    void UnsubscribeFromEvents() override {
        eventCallback_ = nullptr;
    }
    void DownloadAvatar(const std::string& contactId,
                        std::function<void(const std::string&)> onResult) override {
        if (onResult) onResult("");
    }
    void DownloadAttachment(const std::string& url,
                            const std::string& dest,
                            std::function<void(float)> onProgress,
                            std::function<void(bool, const std::string&)> onResult) override {
        if (onResult) onResult(false, "");
    }

private:
    std::atomic<ChatConnectionState> state_{ ChatConnectionState::Disconnected };
    ChatPluginEventCallback          eventCallback_;
    std::mutex                       cbMutex_;
};

}} // namespace
```

### Step 3 — Fire push events from your worker thread

When a new message arrives from the network, call:

```cpp
ChatPluginEvent evt;
evt.EventType      = ChatPluginEventType::MessageReceived;
evt.ProviderId     = GetPluginId();
evt.ConversationId = roomId;
evt.Message        = ParsedMessage;   // ChatMessage struct

std::lock_guard<std::mutex> lock(cbMutex_);
if (eventCallback_) eventCallback_(evt);
```

Event types to implement for a good user experience:

| Event type | When to fire |
|---|---|
| `MessageReceived` | New message arrives |
| `MessageDelivered` | Server confirms delivery / recipient read |
| `TypingStarted` / `TypingStopped` | Typing indicator update |
| `ContactStatusChanged` | Online/offline presence change |
| `ConversationUpdated` | Unread count / last message changed |
| `ConnectionStateChanged` | Connect / disconnect / error |
| `AuthRequired` | Session expired, need re-login |

### Step 4 — Declare capabilities honestly

Only set flags for features your provider actually supports.
The manager checks `HasCapability()` before calling optional methods:

```cpp
ChatCapabilityFlags GetCapabilities() const override {
    using F = ChatCapabilityFlags;
    return F::TextMessages      // Always include this
         | F::GroupChats        // If you support group rooms
         | F::FileTransfer      // If SendFile() works
         | F::ReadReceipts      // If MarkConversationRead() works
         | F::TypingIndicator;  // If SetTypingStatus() works
         // Don't include flags for features you haven't implemented
}
```

### Step 5 — Register the plugin in main.cpp

```cpp
// In main.cpp, after mgr.Initialize():
mgr.RegisterPlugin(std::make_shared<MyProviderPlugin>());
```

### Step 6 — Add to CMakeLists.txt

```cmake
# In CMakeLists.txt, add a new module target:
add_library(ChatCubeMyProviderPlugin SHARED
    Plugins/MyProvider/MyProviderPlugin.cpp
)
target_link_libraries(ChatCubeMyProviderPlugin PRIVATE ChatCubeCore UltraCanvas)
set_target_properties(ChatCubeMyProviderPlugin PROPERTIES
    OUTPUT_NAME "MyProviderPlugin"
    SUFFIX      ".module"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ChatCube.app/Resources/Modules"
)
```

---

## Implementing the HTTP layer (MatrixProviderPlugin)

`MatrixProviderPlugin` has four stub methods that must be replaced with
real HTTP calls. The contract for each:

```cpp
// HttpGet: perform GET with Authorization: Bearer <accessToken_>
// Returns response body as string. Sets httpStatus to HTTP code.
// On network error: set httpStatus = 0 and return "".
std::string MatrixProviderPlugin::HttpGet(
    const std::string& url, int& httpStatus) const {
    // libcurl example:
    CURL* curl = curl_easy_init();
    // ... set URL, headers, write callback ...
    // curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // curl_easy_perform(curl);
    // curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    // curl_easy_cleanup(curl);
}

// HttpPost: POST with Content-Type: application/json
// HttpPut:  PUT  with Content-Type: application/json
// HttpUploadFile: POST binary file body to /_matrix/media/v3/upload
//   Returns the mxc:// URI from the response JSON field "content_uri"
```

Replace the four stub bodies in `MatrixProviderPlugin.cpp`. No other
changes are needed — all protocol logic calls these four methods only.

---

## UltraCanvas compliance checklist

Before submitting code, verify all items:

```
□ All UI via UltraCanvas components only (App coding guide Rule 1)
□ No manual coordinate calculations — layout managers only (Rule 15.1)
□ File open/save via UltraCanvasDialogManager (App coding guide §3b)
□ File loading via FileLoader module (App coding guide §3a)
□ Camera / microphone via IODeviceManager (App coding guide §3d)
□ Image manipulation via PixelFX (App coding guide §3c)
□ Callbacks use base verb form: onX not onXed (master_callbacks_V4.1)
□ No new UltraCanvas elements created without permission (Rule 14)
□ No manual OnEvent forwarding — containers propagate automatically
□ Plugin naming: I[Type]Plugin pattern (FileLoader_Master_Registry)
□ All new types checked against master_types_registry_V4
□ All new functions checked against master_functions_V4.1
□ Header format: // Path, // Description, // Version, // Author, #pragma once
```

---

## Roadmap — next implementation steps

| Step | Component | Status |
|---|---|---|
| 7 | Sticker picker dialog | Planned |
| 8 | Login/settings dialog (per provider) | Planned |
| 9 | XMPP plugin | Planned |
| 10 | Telegram plugin (TDLib) | Planned |
| 11 | ChatCube Direct P2P plugin | Planned |
| 12 | Voice/video call UI overlay | Planned |
| 13 | Notification system integration | Planned |
| 14 | Message search dialog | Planned |

---

## Provider status matrix

| Provider | API | Auth type | Text | Files | Calls | Stickers | Status |
|---|---|---|---|---|---|---|---|
| Matrix | Open REST | User+pass / SSO | ✅ | ✅ | Signalling stub | MSC2545 | **v1 implemented** |
| XMPP | Open TCP/XML | JID+pass | Planned | Planned | Jingle | - | Step 9 |
| Telegram | TDLib (open) | Phone OTP | Planned | Planned | Planned | Planned | Step 10 |
| WhatsApp Biz | Business API | OAuth | Planned | Planned | No | No | Future |
| ChatCube P2P | Native | Key pair | Planned | Planned | WebRTC | - | Step 11 |
| Signal | No official API | - | - | - | - | - | Not planned |

---

*ChatCube Developer Guide — UltraCanvas Framework / Cloverleaf*
