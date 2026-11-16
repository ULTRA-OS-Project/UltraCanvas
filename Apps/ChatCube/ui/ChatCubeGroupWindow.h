// Apps/ChatCube/ui/ChatCubeGroupWindow.h
// The group editor: a group's name and description, public or private, and
// its members - each with Message, and for the owner Remove and Block - and
// those blocked, with Unblock.
//
//   ULTRA OS lounge                                  12 members · public group
//   ┌ Group ───────────────────────────────────────────────────────────────┐
//   │ Name         [ ULTRA OS lounge                                     ] │
//   │ Description  [ Questions and news about ULTRA OS ...               ] │
//   │              [                                                     ] │
//   │ [Save]  [Make private]                          status               │
//   └──────────────────────────────────────────────────────────────────────┘
//   ┌ Members (12) ─────────────────────────────── [Add members] ──────────┐
//   │ You (owner)                                                         │
//   │ Bob Smith                              [Message] [Remove] [Block]    │
//   └──────────────────────────────────────────────────────────────────────┘
//   ┌ Blocked (1) ─────────────────────────────────────────────────────────┐
//   │ Eve                                                       [Unblock]  │
//   └──────────────────────────────────────────────────────────────────────┘
//                                                           [Leave group]
//
// What may be changed follows the network and the role: a ChatCube group's
// description, public / private and blocking are ChatCube's (Matrix and
// Telegram groups show the name and the members); in a public group only its
// owner edits the name and description, in a private one any member; taking
// someone out is the owner's where the network has one. The actions that ask
// first (remove, block, leave, add) are the main window's, through the
// callbacks; the window follows the group as it changes (Update).
//
// Version: 0.1.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMTypes.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasContainer;
class UltraCanvasLabel;
class UltraCanvasTextArea;
class UltraCanvasTextInput;
}

namespace ChatCube {

class ChatCubeGroupWindow {
public:
    ChatCubeGroupWindow() = default;
    ~ChatCubeGroupWindow();

    // Opens the editor for `group` over `host` (another group's editor, open
    // already, shows this one instead). `me` is the local user's id on the
    // group's network; `direct` says the group is ChatCube's own.
    void Show(UltraCanvas::UltraCanvasWindowBase* host, const UltraIMConversation& group, const std::string& me,
              bool direct);
    // The group changed (a ConversationUpdate): shown afresh when it is this one.
    void Update(const UltraIMConversation& group);
    // The group is gone from the list (left, taken out): the editor closes.
    void Removed(const std::string& conversationId);
    void Close();
    bool IsOpenFor(const std::string& conversationId) const;

    std::function<void(const std::string& text)> onStatus;
    std::function<std::string(const UltraIMConversation& group, const std::string& memberId)> nameOf;
    std::function<void(const std::string& providerId, const std::string& memberId)> onMessage;
    std::function<void(const std::string& conversationId, const std::string& memberId)> onRemove;
    std::function<void(const std::string& conversationId, const std::string& memberId)> onBlock;
    std::function<void(const std::string& conversationId)> onAddMembers;
    std::function<void(const std::string& conversationId, bool isPublic)> onSetPublic;
    std::function<void(const std::string& conversationId)> onLeave;
    // After a save: the group as the network answers it.
    std::function<void(const UltraIMConversation& group)> onChanged;

    // Whom the owner blocked, asked for again (after a block from the main window).
    void RefreshBlocked();

private:
    void Build(UltraCanvas::UltraCanvasWindowBase* host);
    void Fill();
    void FillMembers();
    void Save();
    void Unblock(const std::string& memberId, const std::string& name);
    void SetStatus(const std::string& text);
    std::string ProviderId() const;
    bool CanEdit() const;
    bool IsOwner() const;

    UltraIMConversation group_;
    std::string me_;
    bool direct_ = false;
    bool closed_ = true;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>    window_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     heading_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     subtitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> name_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> descriptionField_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  description_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     editNote_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    save_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    publicButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     status_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     membersHeading_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    addMembers_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> members_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> blockedCard_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     blockedHeading_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> blocked_;
};

} // namespace ChatCube
