// Apps/ChatCube/android/ChatCubeAndroidProviders.cpp
// ChatCube for Android: the providers linked in, registered with UltraIM.
// Which ones are linked is the build's choice: CMake defines
// CHATCUBE_LINKED_<NAME> for each provider archive it found
// (Apps/ChatCube/CMakeLists.txt). Telegram and Signal are never among them
// on Android - TDLib and signal-cli do not run there.
// Version: 0.1.0
// Last Modified: 2026-11-21
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeAndroidProviders.h"

#include "UltraIM/UltraIMCore.h"

#include <memory>

#if defined(CHATCUBE_LINKED_DIRECT)
#include "DirectProvider.h"
#endif
#if defined(CHATCUBE_LINKED_MATRIX)
#include "MatrixProvider.h"
#endif
#if defined(CHATCUBE_LINKED_MASTODON)
#include "MastodonProvider.h"
#endif
#if defined(CHATCUBE_LINKED_MATTERMOST)
#include "MattermostProvider.h"
#endif
#if defined(CHATCUBE_LINKED_ZULIP)
#include "ZulipProvider.h"
#endif
#if defined(CHATCUBE_LINKED_XMPP)
#include "XmppProvider.h"
#endif
#if defined(CHATCUBE_LINKED_IRC)
#include "IrcProvider.h"
#endif

namespace ChatCube {

namespace {

void Register(std::vector<std::string>& ids, std::shared_ptr<IUltraIMProviderPlugin> provider) {
    const std::string id = provider->GetProviderId();
    if (UltraIM_RegisterProvider(std::move(provider))) ids.push_back(id);
}

} // namespace

std::vector<std::string> RegisterLinkedProviders() {
    std::vector<std::string> ids;
#if defined(CHATCUBE_LINKED_DIRECT)
    Register(ids, std::make_shared<UltraIMDirect::DirectProvider>());
#endif
#if defined(CHATCUBE_LINKED_MATRIX)
    Register(ids, std::make_shared<UltraIMMatrix::MatrixProvider>());
#endif
#if defined(CHATCUBE_LINKED_MASTODON)
    Register(ids, std::make_shared<UltraIMMastodon::MastodonProvider>());
#endif
#if defined(CHATCUBE_LINKED_MATTERMOST)
    Register(ids, std::make_shared<UltraIMMattermost::MattermostProvider>());
#endif
#if defined(CHATCUBE_LINKED_ZULIP)
    Register(ids, std::make_shared<UltraIMZulip::ZulipProvider>());
#endif
#if defined(CHATCUBE_LINKED_XMPP)
    Register(ids, std::make_shared<UltraIMXmpp::XmppProvider>());
#endif
#if defined(CHATCUBE_LINKED_IRC)
    Register(ids, std::make_shared<UltraIMIrc::IrcProvider>());
#endif
    return ids;
}

} // namespace ChatCube
