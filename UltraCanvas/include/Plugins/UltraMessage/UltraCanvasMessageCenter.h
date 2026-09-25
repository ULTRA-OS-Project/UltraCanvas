// UltraCanvas/include/Plugins/UltraMessage/UltraCanvasMessageCenter.h
// UltraCanvasMessageCenter — the desktop message centre as one composite
// element (UltraMessage proposal §11): every chat message, mail and system
// notification on the UltraMessage feed in one structured view, for any
// application to embed — the ULTRA OS desktop, UltraMail's toolbox, a DemoApp
// page.
//
// Built from catalogue elements only: an UltraCanvasSegmentedControl for the
// sections (All / Chats / Mail / System), UltraCanvasChip filters per service
// and for "unread", an UltraCanvasTextInput search field, an
// UltraCanvasTreeView of sources (conversations, mail accounts, applications)
// beside an UltraCanvasListView of feed rows (sender or title, snippet, time,
// unread mark) and a detail pane whose UltraCanvasButtons mark read, dismiss,
// open, and invoke a notification's own actions.
//
// It is a client of the bus and nothing else: Connect() opens an UltraMessage
// endpoint (or hosts the broker when none runs), queries the journal for the
// history and subscribes to `messaging.message`, `mail.message`,
// `system.notification` and `system.notification.dismissed` for what arrives
// afterwards. Reading a row posts `feed.read`; dismissing posts
// `feed.dismissed` (and `system.notification.dismissed` for a notification, so
// the adapter closes the toast); an action button posts
// `system.notification.action`. It never talks to a messenger or mail server.
//
// Threading: deliveries reach the element on the UI thread. Connect() installs
// the UltraCanvas dispatcher (UltraMsg_UseUltraCanvasApplication) when none is
// installed yet and an application exists; a program without one must call
// UltraMsg_ProcessPending itself. Journal calls are short blocking round trips
// to the local broker.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasChip.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasBadge.h"
#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class MessageCenterSection { All = 0, Chats = 1, Mail = 2, System = 3 };

struct MessageCenterStyle {
    Color background     = Colors::White;
    Color sidebar        = Color(247, 248, 250);
    Color rowSelected    = Colors::SelectionHover;
    Color rowHover       = Color(240, 244, 250);
    Color textPrimary    = Color(24, 28, 35);
    Color textSecondary  = Color(110, 116, 128);
    Color unreadMark     = Colors::Selection;
    Color urgentMark     = Color(200, 40, 40);
    float fontSize       = 10.0f;
    float titleFontSize  = 13.0f;
    int   rowHeight      = 44;
    int   controlHeight  = 26;
    int   sourcesMinWidth = 160;
    int   listMinWidth    = 260;
    int   detailMinWidth  = 220;
    bool  showSources = true;   // the tree of conversations / accounts / applications
    bool  showDetail  = true;   // the pane with the full text and the action buttons
    bool  showSearch  = true;
    bool  showFilters = true;   // the chip row
};

// One row of the feed, as the element understands it.
struct MessageCenterEntry {
    UltraMsgMessage message;
    MessageCenterSection section = MessageCenterSection::System;
    std::string service;        // "telegram", "mail", "org.example.app", ...
    std::string sourceKey;      // conversation key / mail account / application id
    std::string sourceTitle;    // what the sources tree shows for it
    std::string sender;         // who: chat sender, mail from, application name
    std::string title;          // the row's first line: sender, or subject / summary
    std::string snippet;        // the row's second line
    std::string body;           // the detail pane's text
    bool unread = true;
    bool urgent = false;
    int64_t timeMs = 0;
    std::vector<UltraMessage::NotificationAction> actions;   // system notifications only
    std::string mirrorOf;       // a chat / mail row mirrored from a notification: its id
};

class UltraCanvasMessageCenter : public UltraCanvasContainer {
public:
    UltraCanvasMessageCenter(const std::string& identifier, float x, float y, float w, float h);
    ~UltraCanvasMessageCenter() override;

    // ---- the bus ---------------------------------------------------------

    // App id "org.ultraos.messagecenter", display name "Message Centre",
    // UI-thread delivery, hosting a broker when none answers.
    static UltraMsgConnectOptions DefaultConnectOptions();
    // Connects, loads the history and subscribes. False (with onError) when
    // no broker could be reached or started; the element then shows the
    // reason in its status line and stays usable for rows fed by Ingest().
    bool Connect(const UltraMsgConnectOptions& options = DefaultConnectOptions());
    void Disconnect();
    bool IsConnected() const;
    UltraMsgHandle GetEndpoint() const { return endpoint_; }

    // Re-reads the journal (the last `historyLimit` rows of the feed topics).
    void Refresh();
    void SetHistoryLimit(int rows) { historyLimit_ = rows > 0 ? rows : 1; }
    int  GetHistoryLimit() const { return historyLimit_; }

    // ---- what is shown ---------------------------------------------------

    void SetSection(MessageCenterSection section);
    MessageCenterSection GetSection() const { return section_; }
    // A source key from the tree ("" = every source of the section).
    void SetSource(const std::string& sourceKey);
    const std::string& GetSource() const { return sourceKey_; }
    // A service chip ("" = every service).
    void SetServiceFilter(const std::string& service);
    void SetUnreadOnly(bool unreadOnly);
    bool IsUnreadOnly() const { return unreadOnly_; }
    void SetSearchText(const std::string& text);

    int GetUnreadCount() const;     // over every entry, whatever the filters
    int GetVisibleCount() const { return static_cast<int>(visible_.size()); }
    const MessageCenterEntry* GetSelectedEntry() const;
    const std::vector<MessageCenterEntry>& GetEntries() const { return entries_; }

    // Adds a feed message, or replaces the row with the same id (what the
    // subscriptions call; a host or a test may push rows without a bus).
    void Ingest(const UltraMsgMessage& message);
    void RemoveEntry(const std::string& messageId);
    void Clear();

    // ---- acting on rows ---------------------------------------------------

    void MarkRead(const std::string& messageId, bool read);
    void Dismiss(const std::string& messageId);
    void InvokeAction(const std::string& messageId, const std::string& actionId);
    // What "Open" and a double-click do: onOpen when set, else the first
    // action of a notification.
    void Open(const std::string& messageId);

    // ---- callbacks --------------------------------------------------------

    std::function<void(const MessageCenterEntry&)> onOpen;
    std::function<void(const MessageCenterEntry*)> onSelectionChanged;   // nullptr = nothing selected
    std::function<void(int unread)> onUnreadCountChanged;
    std::function<void(const UltraMsgResult&)> onError;

    // ---- looks and parts --------------------------------------------------

    void SetStyle(const MessageCenterStyle& style);
    const MessageCenterStyle& GetStyle() const { return style_; }

    std::shared_ptr<UltraCanvasListView>         GetListView() const { return list_; }
    std::shared_ptr<UltraCanvasTreeView>         GetSourcesTree() const { return sources_; }
    std::shared_ptr<UltraCanvasSegmentedControl> GetSectionControl() const { return sections_; }
    std::shared_ptr<UltraCanvasTextInput>        GetSearchInput() const { return search_; }
    std::shared_ptr<UltraCanvasContainer>        GetDetailPane() const { return detail_; }
    std::shared_ptr<UltraCanvasLabel>            GetStatusLabel() const { return status_; }

    // Section of a topic, and the entry the element builds from a message
    // (exposed for tests).
    static MessageCenterSection SectionForTopic(const std::string& topic);
    static bool BuildEntry(const UltraMsgMessage& message, MessageCenterEntry& out);
    static std::string FormatTime(int64_t timestampMs, int64_t nowMs);

private:
    class RowDelegate;
    class RowModel;

    void BuildHeader();
    void BuildFilters();
    void BuildBody();
    void BuildDetail();
    void ApplyStyle();

    void Subscribe();
    void Unsubscribe();
    void SetStatus(const std::string& text);
    void Fail(const UltraMsgResult& result, const std::string& what);

    void RebuildVisible();
    void RebuildSources();
    void RebuildServiceChips();
    void ShowDetail(const MessageCenterEntry* entry);
    void SelectVisibleRow(int row);
    int  FindEntry(const std::string& messageId) const;
    bool Matches(const MessageCenterEntry& entry) const;
    void NotifyUnread();

    void PostFeedState(const char* topic, const MessageCenterEntry& entry);

    MessageCenterStyle style_;
    UltraMsgHandle endpoint_ = UltraMsgInvalidHandle;
    bool ownsEndpoint_ = false;
    std::vector<UltraMsgHandle> subscriptions_;
    int historyLimit_ = 500;

    std::vector<MessageCenterEntry> entries_;
    std::vector<int> visible_;              // indexes into entries_, newest first
    MessageCenterSection section_ = MessageCenterSection::All;
    std::string sourceKey_;
    std::string serviceFilter_;
    bool unreadOnly_ = false;
    std::string searchText_;
    std::string selectedId_;
    int lastUnread_ = -1;
    bool suppressTreeCallback_ = false;

    // parts
    std::shared_ptr<UltraCanvasContainer> header_;
    std::shared_ptr<UltraCanvasSegmentedControl> sections_;
    std::shared_ptr<UltraCanvasBadge> unreadBadge_;
    std::shared_ptr<UltraCanvasTextInput> search_;
    std::shared_ptr<UltraCanvasButton> refreshButton_;
    std::shared_ptr<UltraCanvasContainer> filters_;
    std::shared_ptr<UltraCanvasChip> unreadChip_;
    std::vector<std::shared_ptr<UltraCanvasChip>> serviceChips_;
    std::shared_ptr<UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvasContainer> sourcesPane_;
    std::shared_ptr<UltraCanvasContainer> listPane_;
    std::shared_ptr<UltraCanvasContainer> detailPane_;
    std::shared_ptr<UltraCanvasTreeView> sources_;
    std::shared_ptr<UltraCanvasListView> list_;
    std::shared_ptr<RowModel> model_;
    std::shared_ptr<RowDelegate> delegate_;
    std::shared_ptr<UltraCanvasContainer> detail_;
    std::shared_ptr<UltraCanvasLabel> detailTitle_;
    std::shared_ptr<UltraCanvasLabel> detailMeta_;
    std::shared_ptr<UltraCanvasLabel> detailBody_;
    std::shared_ptr<UltraCanvasContainer> detailButtons_;
    std::shared_ptr<UltraCanvasButton> openButton_;
    std::shared_ptr<UltraCanvasButton> readButton_;
    std::shared_ptr<UltraCanvasButton> dismissButton_;
    std::vector<std::shared_ptr<UltraCanvasButton>> actionButtons_;
    std::shared_ptr<UltraCanvasLabel> status_;
};

inline std::shared_ptr<UltraCanvasMessageCenter> CreateMessageCenter(
        const std::string& identifier, float x = 0, float y = 0, float w = 0, float h = 0) {
    return std::make_shared<UltraCanvasMessageCenter>(identifier, x, y, w, h);
}

} // namespace UltraCanvas
