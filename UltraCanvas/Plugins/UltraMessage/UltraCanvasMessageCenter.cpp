// UltraCanvas/Plugins/UltraMessage/UltraCanvasMessageCenter.cpp
// See include/Plugins/UltraMessage/UltraCanvasMessageCenter.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "Plugins/UltraMessage/UltraCanvasMessageCenter.h"
#include "UltraMessage/UltraMessageUltraCanvas.h"
#include "UltraCanvasApplication.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>

namespace UltraCanvas {

namespace {

constexpr const char* kSectionNames[] = {"All", "Chats", "Mail", "System"};

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needleLower) {
    return needleLower.empty() || Lower(haystack).find(needleLower) != std::string::npos;
}

std::string FirstLine(const std::string& text) {
    const size_t nl = text.find('\n');
    return nl == std::string::npos ? text : text.substr(0, nl);
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Str(const UltraCanvas::JSONValue& body, const char* key) {
    const JSONValue* v = body.IsObject() ? body.Find(key) : nullptr;
    return v && v->IsString() ? v->GetString() : std::string();
}

// A child that fills its host (the mail view's arrangement).
void FillWith(const std::shared_ptr<UltraCanvasContainer>& host,
              const std::shared_ptr<UltraCanvasUIElement>& child) {
    host->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    host->AddChild(child);
    child->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

// A fixed-size gap in a flex row.
std::shared_ptr<UltraCanvasContainer> Spacer(const std::string& id, float width) {
    auto s = CreateContainer(id, 0, 0, width, 1);
    s->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return s;
}

void FixedInRow(const std::shared_ptr<UltraCanvasUIElement>& element) {
    element->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
}

} // namespace

// ---------------------------------------------------------------------------
// The list's model and delegate
// ---------------------------------------------------------------------------

class UltraCanvasMessageCenter::RowModel : public UltraCanvasMultiColumnListModel {
public:
    using UltraCanvasMultiColumnListModel::UltraCanvasMultiColumnListModel;
};

// Paints a feed row: the unread mark, then two lines (who, and what) and the
// time. Content painting inside a ListView delegate is the view's licence;
// every control around the list is a real element.
class UltraCanvasMessageCenter::RowDelegate : public IItemDelegate {
public:
    const std::vector<MessageCenterEntry>* entries = nullptr;
    const std::vector<int>* visible = nullptr;
    MessageCenterStyle style;

    const MessageCenterEntry* EntryAt(int row) const {
        if (!entries || !visible || row < 0 || row >= static_cast<int>(visible->size())) return nullptr;
        const int index = (*visible)[row];
        if (index < 0 || index >= static_cast<int>(entries->size())) return nullptr;
        return &(*entries)[index];
    }

    void RenderItem(IRenderContext* ctx, const IListModel* model, int row, int column,
                    const ListItemStyleOption& option) override {
        if (!ctx || !model) return;
        const MessageCenterEntry* entry = EntryAt(row);
        const double x = option.columnX;
        const double w = option.columnWidth;
        const double y = option.rect.y;
        const double h = option.rect.height;
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);

        if (column == 0) {
            if (entry && entry->unread) {
                ctx->SetFillPaint(entry->urgent ? style.urgentMark : style.unreadMark);
                ctx->FillCircle(Point2Dd(x + w / 2.0, y + h / 2.0), 4.0);
            }
            return;
        }
        if (column == 2) {
            const std::string text = GetStringValue(model->GetData(ListIndex{row, column}, ListDataRole::DisplayRole));
            ctx->SetFontSize(style.fontSize - 1);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextAlignment(TextAlignment::Right);
            ctx->SetTextPaint(style.textSecondary);
            ctx->DrawTextInRect(text, Rect2Dd(x, y, w - 8, h));
            return;
        }
        if (!entry) return;
        const double pad = 6;
        const double half = h / 2.0;
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetFontSize(style.fontSize + 1);
        ctx->SetFontWeight(entry->unread ? FontWeight::Bold : FontWeight::Normal);
        ctx->SetTextPaint(style.textPrimary);
        ctx->DrawTextInRect(entry->title, Rect2Dd(x + pad, y + 2, w - pad * 2, half));
        ctx->SetFontSize(style.fontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(entry->unread ? style.textPrimary : style.textSecondary);
        ctx->DrawTextInRect(entry->snippet, Rect2Dd(x + pad, y + half - 2, w - pad * 2, half));
    }

    int GetRowHeight(const IListModel*, int) const override { return style.rowHeight; }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

UltraCanvasMessageCenter::UltraCanvasMessageCenter(const std::string& identifier, float x, float y,
                                                   float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    BuildHeader();
    BuildFilters();
    BuildBody();
    status_ = CreateLabel(identifier + ".status", 0, 0, 0, 18);
    status_->SetFontSize(style_.fontSize - 1);
    AddChild(status_);
    status_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    ApplyStyle();
    RebuildSources();
    RebuildVisible();
    ShowDetail(nullptr);
    SetStatus("Not connected");
}

UltraCanvasMessageCenter::~UltraCanvasMessageCenter() {
    Disconnect();
}

void UltraCanvasMessageCenter::BuildHeader() {
    const std::string id = GetIdentifier();
    header_ = CreateContainer(id + ".header", 0, 0, 0, 0);
    header_->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header_->SetPadding(4, 6);

    sections_ = CreateSegmentedControl(id + ".sections", 0, 0, 320, static_cast<float>(style_.controlHeight));
    for (const char* name : kSectionNames) sections_->AddSegment(name);
    sections_->SetSelectedIndex(0);
    sections_->onSegmentSelected = [this](int index) {
        if (index >= 0 && index <= 3) SetSection(static_cast<MessageCenterSection>(index));
    };
    FixedInRow(sections_);
    header_->AddChild(sections_);

    header_->AddChild(Spacer(id + ".gap1", 8));
    unreadBadge_ = CreateCountBadge(id + ".unread", 0, 0, 0);
    unreadBadge_->SetVariant(BadgeVariant::Primary);
    FixedInRow(unreadBadge_);
    header_->AddChild(unreadBadge_);

    auto stretch = CreateContainer(id + ".gap2", 0, 0, 0, 1);
    stretch->layoutItem.SetFlexGrow(1);
    header_->AddChild(stretch);

    search_ = CreateTextInput(id + ".search", 0, 0, 220, style_.controlHeight);
    search_->SetPlaceholder("Search messages");
    search_->onTextChanged = [this](const std::string& text) { SetSearchText(text); };
    FixedInRow(search_);
    header_->AddChild(search_);

    header_->AddChild(Spacer(id + ".gap3", 6));
    refreshButton_ = CreateButton(id + ".refresh", 0, 0, 84, static_cast<float>(style_.controlHeight), "Refresh");
    refreshButton_->onClick = [this]() { Refresh(); };
    FixedInRow(refreshButton_);
    header_->AddChild(refreshButton_);

    AddChild(header_);
    header_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void UltraCanvasMessageCenter::BuildFilters() {
    const std::string id = GetIdentifier();
    filters_ = CreateContainer(id + ".filters", 0, 0, 0, 0);
    filters_->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    filters_->SetPadding(2, 6);

    unreadChip_ = CreateFilterChip(id + ".chip.unread", 0, 0, "Unread", false);
    unreadChip_->SetVariant(ChipVariant::Outlined);
    unreadChip_->onSelectedChanged = [this](bool selected) { SetUnreadOnly(selected); };
    FixedInRow(unreadChip_);
    filters_->AddChild(unreadChip_);

    AddChild(filters_);
    filters_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void UltraCanvasMessageCenter::BuildBody() {
    const std::string id = GetIdentifier();
    split_ = std::make_shared<UltraCanvasSplitPane>(id + ".split", 0, 0, 0, 0, SplitOrientation::Horizontal);
    sourcesPane_ = split_->AddPane(1.0);
    listPane_ = split_->AddPane(2.4);
    detailPane_ = split_->AddPane(1.6);
    split_->SetPaneMinSize(0, style_.sourcesMinWidth);
    split_->SetPaneMinSize(1, style_.listMinWidth);
    split_->SetPaneMinSize(2, style_.detailMinWidth);

    // Sources: All, then the conversations, mail accounts and applications.
    sources_ = std::make_shared<UltraCanvasTreeView>(id + ".sources", 0, 0, 0, 0);
    sources_->SetRootVisible(false);
    sources_->SetShowExpandButtons(true);
    sources_->SetShowRootLines(false);
    sources_->SetRowHeight(22);
    sources_->SetIndentSize(14);
    sources_->SetSelectionMode(TreeSelectionMode::Single);
    sources_->onNodeSelected = [this](TreeNode* node) {
        if (suppressTreeCallback_ || !node) return;
        const std::string& nodeId = node->data.nodeId;
        if (nodeId == "all") {
            sourceKey_.clear();
            SetSection(MessageCenterSection::All);
        } else if (nodeId.rfind("sec:", 0) == 0) {
            sourceKey_.clear();
            SetSection(static_cast<MessageCenterSection>(std::atoi(nodeId.c_str() + 4)));
        } else if (nodeId.rfind("src:", 0) == 0) {
            const size_t colon = nodeId.find(':', 4);
            if (colon == std::string::npos) return;
            const int section = std::atoi(nodeId.substr(4, colon - 4).c_str());
            sourceKey_ = nodeId.substr(colon + 1);
            section_ = static_cast<MessageCenterSection>(section);
            if (sections_) sections_->SetSelectedIndex(section);
            RebuildVisible();
        }
    };
    FillWith(sourcesPane_, sources_);

    // The feed rows.
    model_ = std::make_shared<RowModel>();
    model_->SetColumns({
        ListColumnDef("", 18, TextAlignment::Center, "Unread"),
        ListColumnDef("Message", 300, TextAlignment::Left),
        ListColumnDef("Time", 64, TextAlignment::Right),
    });
    list_ = std::make_shared<UltraCanvasListView>(id + ".list", 0, 0, 0, 0);
    list_->SetModel(model_);
    list_->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    delegate_ = std::make_shared<RowDelegate>();
    delegate_->entries = &entries_;
    delegate_->visible = &visible_;
    list_->SetDelegate(delegate_);
    list_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (!rows.empty()) SelectVisibleRow(rows.front());
    };
    list_->onItemClicked = [this](int row) { SelectVisibleRow(row); };
    list_->onItemDoubleClicked = [this](int) { if (!selectedId_.empty()) Open(selectedId_); };
    list_->onItemActivated = [this](int) { if (!selectedId_.empty()) Open(selectedId_); };
    FillWith(listPane_, list_);

    BuildDetail();
    FillWith(detailPane_, detail_);

    AddChild(split_);
    split_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void UltraCanvasMessageCenter::BuildDetail() {
    const std::string id = GetIdentifier();
    detail_ = CreateContainer(id + ".detail", 0, 0, 0, 0);
    detail_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    detail_->SetPadding(8, 10);

    detailTitle_ = CreateLabel(id + ".detail.title", 0, 0, 0, 26);
    detailTitle_->SetFontSize(style_.titleFontSize);
    detailTitle_->SetFontWeight(FontWeight::Bold);
    detailTitle_->SetWrap(TextWrap::WrapWord);
    detail_->AddChild(detailTitle_);
    detailTitle_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    detailMeta_ = CreateLabel(id + ".detail.meta", 0, 0, 0, 20);
    detailMeta_->SetFontSize(style_.fontSize - 1);
    detail_->AddChild(detailMeta_);
    detailMeta_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    detailBody_ = CreateLabel(id + ".detail.body", 0, 0, 0, 0);
    detailBody_->SetFontSize(style_.fontSize);
    detailBody_->SetWrap(TextWrap::WrapWord);
    detail_->AddChild(detailBody_);
    detailBody_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    detailButtons_ = CreateContainer(id + ".detail.buttons", 0, 0, 0, 0);
    detailButtons_->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    const float bh = static_cast<float>(style_.controlHeight);
    openButton_ = CreateButton(id + ".open", 0, 0, 80, bh, "Open");
    openButton_->onClick = [this]() { if (!selectedId_.empty()) Open(selectedId_); };
    readButton_ = CreateButton(id + ".read", 0, 0, 104, bh, "Mark unread");
    readButton_->onClick = [this]() {
        const MessageCenterEntry* entry = GetSelectedEntry();
        if (entry) MarkRead(entry->message.envelope.id, entry->unread);
    };
    dismissButton_ = CreateButton(id + ".dismiss", 0, 0, 80, bh, "Dismiss");
    dismissButton_->onClick = [this]() { if (!selectedId_.empty()) Dismiss(selectedId_); };
    for (auto& button : {openButton_, readButton_, dismissButton_}) {
        FixedInRow(button);
        detailButtons_->AddChild(button);
        detailButtons_->AddChild(Spacer(button->GetIdentifier() + ".gap", 6));
    }
    detail_->AddChild(detailButtons_);
    detailButtons_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void UltraCanvasMessageCenter::SetStyle(const MessageCenterStyle& style) {
    style_ = style;
    ApplyStyle();
    RebuildVisible();
}

void UltraCanvasMessageCenter::ApplyStyle() {
    if (list_) {
        ListViewStyle st;
        st.backgroundColor = style_.background;
        st.showHeader = false;
        st.rowHeight = style_.rowHeight;
        st.showGridLines = false;
        st.selectionBackgroundColor = style_.rowSelected;
        st.hoverBackgroundColor = style_.rowHover;
        list_->SetStyle(st);
    }
    if (delegate_) delegate_->style = style_;
    if (sources_) {
        sources_->SetBackgroundColor(style_.sidebar);
        sources_->SetSelectionColor(style_.rowSelected);
        sources_->SetHoverColor(style_.rowHover);
        sources_->SetTextColor(style_.textPrimary);
        sources_->SetFontSize(style_.fontSize);
    }
    if (detailTitle_) { detailTitle_->SetFontSize(style_.titleFontSize); detailTitle_->SetTextColor(style_.textPrimary); }
    if (detailMeta_)  { detailMeta_->SetFontSize(style_.fontSize - 1); detailMeta_->SetTextColor(style_.textSecondary); }
    if (detailBody_)  { detailBody_->SetFontSize(style_.fontSize); detailBody_->SetTextColor(style_.textPrimary); }
    if (status_)      { status_->SetFontSize(style_.fontSize - 1); status_->SetTextColor(style_.textSecondary); }
    if (sourcesPane_) sourcesPane_->SetVisible(style_.showSources);
    if (detailPane_)  detailPane_->SetVisible(style_.showDetail);
    if (search_)      search_->SetVisible(style_.showSearch);
    if (filters_)     filters_->SetVisible(style_.showFilters);
    if (split_) {
        split_->SetPaneMinSize(0, style_.showSources ? style_.sourcesMinWidth : 0);
        split_->SetPaneMinSize(1, style_.listMinWidth);
        split_->SetPaneMinSize(2, style_.showDetail ? style_.detailMinWidth : 0);
    }
    RequestRedraw();
}

// ---------------------------------------------------------------------------
// The bus
// ---------------------------------------------------------------------------

UltraMsgConnectOptions UltraCanvasMessageCenter::DefaultConnectOptions() {
    UltraMsgConnectOptions options;
    options.appId = "org.ultraos.messagecenter";
    options.displayName = "Message Centre";
    options.deliverOnUIThread = true;
    options.startBrokerIfAbsent = true;
    return options;
}

bool UltraCanvasMessageCenter::Connect(const UltraMsgConnectOptions& options) {
    if (endpoint_ != UltraMsgInvalidHandle) return true;
    if (!UltraMsg_HasUIDispatcher() && UltraCanvasApplicationBase::GetCurrent())
        UltraMsg_UseUltraCanvasApplication();
    UltraMsgResult error;
    endpoint_ = UltraMsg_Connect(options, &error);
    if (endpoint_ == UltraMsgInvalidHandle) {
        Fail(error, "connect");
        SetStatus("Not connected: " + error.message);
        return false;
    }
    ownsEndpoint_ = true;
    Subscribe();
    Refresh();
    UltraMsgBrokerInfo info;
    if (UltraMsg_GetBrokerInfo(endpoint_, info))
        SetStatus(std::string("Connected to ") + info.busPath + (info.inProcess ? " (hosting the broker)" : ""));
    else
        SetStatus("Connected");
    return true;
}

void UltraCanvasMessageCenter::Disconnect() {
    Unsubscribe();
    if (endpoint_ != UltraMsgInvalidHandle && ownsEndpoint_) UltraMsg_Disconnect(endpoint_);
    endpoint_ = UltraMsgInvalidHandle;
    ownsEndpoint_ = false;
    SetStatus("Not connected");
}

bool UltraCanvasMessageCenter::IsConnected() const {
    return endpoint_ != UltraMsgInvalidHandle && UltraMsg_IsConnected(endpoint_);
}

void UltraCanvasMessageCenter::Subscribe() {
    Unsubscribe();
    auto subscribe = [this](const char* topic, UltraMsgCallback callback) {
        UltraMsgResult error;
        UltraMsgHandle sub = UltraMsg_Subscribe(endpoint_, topic, std::move(callback), {}, &error);
        if (sub == UltraMsgInvalidHandle) Fail(error, std::string("subscribe ") + topic);
        else subscriptions_.push_back(sub);
    };
    for (const char* topic : {UltraMsgTopics::MessagingMessage, UltraMsgTopics::MailMessage,
                              UltraMsgTopics::SystemNotification})
        subscribe(topic, [this](const UltraMsgMessage& message) { Ingest(message); });
    subscribe(UltraMsgTopics::SystemNotificationDismissed, [this](const UltraMsgMessage& message) {
        RemoveEntry(Str(message.body, "notificationId"));
    });
    subscribe(UltraMsgTopics::FeedRead, [this](const UltraMsgMessage& message) {
        const int index = FindEntry(Str(message.body, "messageId"));
        if (index < 0 || !entries_[index].unread) return;
        entries_[index].unread = false;
        entries_[index].message.read = true;
        RebuildVisible();
        RebuildSources();
        NotifyUnread();
    });
    subscribe(UltraMsgTopics::FeedDismissed, [this](const UltraMsgMessage& message) {
        RemoveEntry(Str(message.body, "messageId"));
    });
}

void UltraCanvasMessageCenter::Unsubscribe() {
    for (UltraMsgHandle sub : subscriptions_) UltraMsg_Unsubscribe(sub);
    subscriptions_.clear();
}

void UltraCanvasMessageCenter::Refresh() {
    if (!IsConnected()) {
        RebuildVisible();
        return;
    }
    UltraMsgQuery query;
    query.topics = {UltraMsgTopics::MessagingMessage, UltraMsgTopics::MailMessage,
                    UltraMsgTopics::SystemNotification};
    query.limit = historyLimit_;
    query.includeDismissed = false;
    std::vector<UltraMsgMessage> rows;
    UltraMsgResult result = UltraMsg_Query(endpoint_, query, rows);
    if (!result) {
        Fail(result, "query");
        return;
    }
    entries_.clear();
    std::set<std::string> mirrored;
    std::vector<MessageCenterEntry> built;
    for (const auto& row : rows) {
        MessageCenterEntry entry;
        if (!BuildEntry(row, entry)) continue;
        if (!entry.mirrorOf.empty()) mirrored.insert(entry.mirrorOf);
        built.push_back(std::move(entry));
    }
    for (auto& entry : built)
        if (!mirrored.count(entry.message.envelope.id)) entries_.push_back(std::move(entry));
    RebuildServiceChips();
    RebuildSources();
    RebuildVisible();
    NotifyUnread();
}

void UltraCanvasMessageCenter::SetStatus(const std::string& text) {
    if (status_) status_->SetText(text);
}

void UltraCanvasMessageCenter::Fail(const UltraMsgResult& result, const std::string& what) {
    if (onError) onError(result);
    SetStatus(what + ": " + result.message);
}

// ---------------------------------------------------------------------------
// Entries
// ---------------------------------------------------------------------------

MessageCenterSection UltraCanvasMessageCenter::SectionForTopic(const std::string& topic) {
    if (topic == UltraMsgTopics::MessagingMessage) return MessageCenterSection::Chats;
    if (topic == UltraMsgTopics::MailMessage) return MessageCenterSection::Mail;
    return MessageCenterSection::System;
}

bool UltraCanvasMessageCenter::BuildEntry(const UltraMsgMessage& message, MessageCenterEntry& out) {
    const std::string& topic = message.envelope.topic;
    if (topic != UltraMsgTopics::MessagingMessage && topic != UltraMsgTopics::MailMessage &&
        topic != UltraMsgTopics::SystemNotification)
        return false;
    out = MessageCenterEntry{};
    out.message = message;
    out.section = SectionForTopic(topic);
    out.unread = !message.read;
    out.urgent = (message.envelope.flags & UltraMsgFlag_Urgent) != 0;
    out.timeMs = message.envelope.timestampMs;
    out.mirrorOf = Str(message.body, "mirrorOf");

    if (out.section == MessageCenterSection::Chats) {
        UltraMessage::MessagingMessage m;
        if (!UltraMessage::ParseMessagingMessage(message.body, m)) return false;
        out.service = m.service.empty() ? "chat" : m.service;
        out.sourceKey = !message.envelope.conversation.empty() ? message.envelope.conversation
                                                                : UltraMessage::ConversationKey(m);
        out.sourceTitle = !m.conversationTitle.empty() ? m.conversationTitle
                          : !m.conversationId.empty() ? m.conversationId : out.service;
        out.sender = !m.sender.name.empty() ? m.sender.name : m.sender.id;
        out.title = out.sender.empty() ? out.sourceTitle : out.sender;
        if (m.isGroup && !m.conversationTitle.empty() && out.sender != m.conversationTitle)
            out.title += " \xC2\xB7 " + m.conversationTitle;
        out.snippet = FirstLine(m.text);
        out.body = m.text;
        return true;
    }
    if (out.section == MessageCenterSection::Mail) {
        UltraMessage::MailMessage m;
        if (!UltraMessage::ParseMailMessage(message.body, m)) return false;
        out.service = "mail";
        out.sourceKey = "mail:" + (m.account.empty() ? std::string("account") : m.account);
        out.sourceTitle = m.account.empty() ? "Mail" : m.account;
        out.sender = !m.from.name.empty() ? m.from.name : m.from.address;
        out.title = out.sender.empty() ? out.sourceTitle : out.sender;
        out.snippet = m.subject.empty() ? "(no subject)" : m.subject;
        out.body = m.subject + (m.snippet.empty() ? "" : "\n\n" + m.snippet);
        return true;
    }
    UltraMessage::SystemNotification n;
    if (!UltraMessage::ParseSystemNotification(message.body, n)) return false;
    out.service = !n.appId.empty() ? Lower(n.appId) : !n.appName.empty() ? Lower(n.appName) : "notification";
    out.sourceKey = "app:" + out.service;
    out.sourceTitle = !n.appName.empty() ? n.appName : !n.appId.empty() ? n.appId : "Notifications";
    out.sender = out.sourceTitle;
    out.title = out.sourceTitle;
    out.snippet = n.summary + (n.body.empty() ? "" : " \xE2\x80\x94 " + FirstLine(n.body));
    out.body = n.summary + (n.body.empty() ? "" : "\n\n" + n.body);
    out.actions = n.actions;
    if (n.urgency == "critical") out.urgent = true;
    return true;
}

std::string UltraCanvasMessageCenter::FormatTime(int64_t timestampMs, int64_t nowMs) {
    if (timestampMs <= 0) return "";
    const time_t t = static_cast<time_t>(timestampMs / 1000);
    const time_t now = static_cast<time_t>(nowMs / 1000);
    std::tm local{};
    std::tm today{};
#ifdef _WIN32
    localtime_s(&local, &t);
    localtime_s(&today, &now);
#else
    localtime_r(&t, &local);
    localtime_r(&now, &today);
#endif
    char buffer[32];
    if (local.tm_year == today.tm_year && local.tm_yday == today.tm_yday) {
        std::snprintf(buffer, sizeof buffer, "%02d:%02d", local.tm_hour, local.tm_min);
    } else if (now - t < 6 * 86400) {
        static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        std::snprintf(buffer, sizeof buffer, "%s", kDays[local.tm_wday % 7]);
    } else if (local.tm_year == today.tm_year) {
        std::snprintf(buffer, sizeof buffer, "%02d.%02d.", local.tm_mday, local.tm_mon + 1);
    } else {
        std::snprintf(buffer, sizeof buffer, "%02d.%02d.%04d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
    }
    return buffer;
}

int UltraCanvasMessageCenter::FindEntry(const std::string& messageId) const {
    if (messageId.empty()) return -1;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].message.envelope.id == messageId) return static_cast<int>(i);
    return -1;
}

void UltraCanvasMessageCenter::Ingest(const UltraMsgMessage& message) {
    MessageCenterEntry entry;
    if (!BuildEntry(message, entry)) return;
    const std::string& id = message.envelope.id;
    // A mirror supersedes the notification it was made from: one row, the
    // first-class one, and the notification's id kept for dismissing it.
    if (!entry.mirrorOf.empty()) {
        const int raw = FindEntry(entry.mirrorOf);
        if (raw >= 0) entries_.erase(entries_.begin() + raw);
    } else {
        for (const auto& existing : entries_)
            if (existing.mirrorOf == id) return;
    }
    int index = FindEntry(id);
    if (index < 0 && !message.envelope.replaces.empty() &&
        (message.envelope.flags & UltraMsgFlag_Replace))
        index = FindEntry(message.envelope.replaces);
    if (index >= 0) entries_[index] = std::move(entry);
    else entries_.push_back(std::move(entry));
    RebuildServiceChips();
    RebuildSources();
    RebuildVisible();
    NotifyUnread();
}

void UltraCanvasMessageCenter::RemoveEntry(const std::string& messageId) {
    const int index = FindEntry(messageId);
    if (index < 0) return;
    if (selectedId_ == messageId) selectedId_.clear();
    entries_.erase(entries_.begin() + index);
    RebuildServiceChips();
    RebuildSources();
    RebuildVisible();
    NotifyUnread();
}

void UltraCanvasMessageCenter::Clear() {
    entries_.clear();
    selectedId_.clear();
    RebuildServiceChips();
    RebuildSources();
    RebuildVisible();
    NotifyUnread();
}

int UltraCanvasMessageCenter::GetUnreadCount() const {
    int count = 0;
    for (const auto& entry : entries_) count += entry.unread ? 1 : 0;
    return count;
}

const MessageCenterEntry* UltraCanvasMessageCenter::GetSelectedEntry() const {
    const int index = FindEntry(selectedId_);
    return index < 0 ? nullptr : &entries_[index];
}

void UltraCanvasMessageCenter::NotifyUnread() {
    const int count = GetUnreadCount();
    if (unreadBadge_) {
        unreadBadge_->SetCount(count);
        unreadBadge_->SetVisible(count > 0);
    }
    if (count == lastUnread_) return;
    lastUnread_ = count;
    if (onUnreadCountChanged) onUnreadCountChanged(count);
}

// ---------------------------------------------------------------------------
// Filters and what is shown
// ---------------------------------------------------------------------------

void UltraCanvasMessageCenter::SetSection(MessageCenterSection section) {
    if (section_ != section) sourceKey_.clear();
    section_ = section;
    if (sections_ && sections_->GetSelectedIndex() != static_cast<int>(section))
        sections_->SetSelectedIndex(static_cast<int>(section));
    RebuildSources();
    RebuildVisible();
}

void UltraCanvasMessageCenter::SetSource(const std::string& sourceKey) {
    sourceKey_ = sourceKey;
    RebuildSources();
    RebuildVisible();
}

void UltraCanvasMessageCenter::SetServiceFilter(const std::string& service) {
    serviceFilter_ = service;
    for (auto& chip : serviceChips_)
        chip->SetSelected(!service.empty() && chip->GetLabel() == service);
    RebuildVisible();
}

void UltraCanvasMessageCenter::SetUnreadOnly(bool unreadOnly) {
    unreadOnly_ = unreadOnly;
    if (unreadChip_ && unreadChip_->IsSelected() != unreadOnly) unreadChip_->SetSelected(unreadOnly);
    RebuildVisible();
}

void UltraCanvasMessageCenter::SetSearchText(const std::string& text) {
    searchText_ = Lower(text);
    if (search_ && search_->GetText() != text) search_->SetText(text);
    RebuildVisible();
}

bool UltraCanvasMessageCenter::Matches(const MessageCenterEntry& entry) const {
    if (section_ != MessageCenterSection::All && entry.section != section_) return false;
    if (!sourceKey_.empty() && entry.sourceKey != sourceKey_) return false;
    if (!serviceFilter_.empty() && entry.service != serviceFilter_) return false;
    if (unreadOnly_ && !entry.unread) return false;
    if (!searchText_.empty() &&
        !ContainsNoCase(entry.title, searchText_) && !ContainsNoCase(entry.snippet, searchText_) &&
        !ContainsNoCase(entry.body, searchText_) && !ContainsNoCase(entry.sender, searchText_) &&
        !ContainsNoCase(entry.sourceTitle, searchText_))
        return false;
    return true;
}

void UltraCanvasMessageCenter::RebuildVisible() {
    visible_.clear();
    for (size_t i = 0; i < entries_.size(); ++i)
        if (Matches(entries_[i])) visible_.push_back(static_cast<int>(i));
    std::stable_sort(visible_.begin(), visible_.end(), [this](int a, int b) {
        return entries_[a].timeMs > entries_[b].timeMs;
    });
    if (!model_) return;
    const int64_t now = NowMs();
    model_->Clear();
    int selectedRow = -1;
    for (size_t row = 0; row < visible_.size(); ++row) {
        const MessageCenterEntry& entry = entries_[visible_[row]];
        MultiColumnListItem item({entry.unread ? "\xE2\x97\x8F" : "", entry.title, FormatTime(entry.timeMs, now)});
        item.tooltip = entry.snippet;
        model_->AddItem(item);
        if (entry.message.envelope.id == selectedId_) selectedRow = static_cast<int>(row);
    }
    if (list_ && list_->GetSelection()) {
        list_->GetSelection()->Clear();
        if (selectedRow >= 0) list_->GetSelection()->Select(selectedRow);
    }
    if (selectedRow < 0 && !selectedId_.empty()) {
        // Filtered out: keep the detail pane on it, but nothing is highlighted.
    }
    ShowDetail(GetSelectedEntry());
    if (list_) list_->RequestRedraw();
}

void UltraCanvasMessageCenter::RebuildSources() {
    if (!sources_) return;
    suppressTreeCallback_ = true;
    sources_->SetRootNode(TreeNodeData("root", "Sources"));
    TreeNodeData all("all", "All messages");
    all.showCheckbox = false;
    sources_->AddNode("root", all);

    struct Source { std::string title; int unread = 0; int total = 0; };
    std::map<std::string, Source> perSection[4];
    for (const auto& entry : entries_) {
        Source& s = perSection[static_cast<int>(entry.section)][entry.sourceKey];
        if (s.title.empty()) s.title = entry.sourceTitle;
        s.total += 1;
        s.unread += entry.unread ? 1 : 0;
    }
    std::string selectedNode = sourceKey_.empty()
        ? (section_ == MessageCenterSection::All ? "all" : "sec:" + std::to_string(static_cast<int>(section_)))
        : "src:" + std::to_string(static_cast<int>(section_)) + ":" + sourceKey_;
    for (int section = 1; section <= 3; ++section) {
        const auto& sources = perSection[section];
        int unread = 0;
        for (const auto& [key, s] : sources) unread += s.unread;
        std::string label = kSectionNames[section];
        if (unread > 0) label += " (" + std::to_string(unread) + ")";
        TreeNodeData node("sec:" + std::to_string(section), label);
        node.showCheckbox = false;
        sources_->AddNode("root", node);
        for (const auto& [key, s] : sources) {
            std::string text = s.title;
            if (s.unread > 0) text += " (" + std::to_string(s.unread) + ")";
            TreeNodeData child("src:" + std::to_string(section) + ":" + key, text);
            child.showCheckbox = false;
            child.tooltip = std::to_string(s.total) + (s.total == 1 ? " message" : " messages");
            sources_->AddNode("sec:" + std::to_string(section), child);
        }
    }
    sources_->ExpandAll();
    if (TreeNode* node = sources_->FindNode(selectedNode)) sources_->SelectNode(node);
    suppressTreeCallback_ = false;
}

void UltraCanvasMessageCenter::RebuildServiceChips() {
    if (!filters_) return;
    std::set<std::string> services;
    for (const auto& entry : entries_) services.insert(entry.service);
    std::vector<std::string> wanted(services.begin(), services.end());
    std::vector<std::string> have;
    for (const auto& chip : serviceChips_) have.push_back(chip->GetLabel());
    if (wanted == have) return;
    for (auto& chip : serviceChips_) filters_->RemoveChild(chip);
    serviceChips_.clear();
    for (const auto& service : wanted) {
        auto chip = CreateFilterChip(GetIdentifier() + ".chip." + service, 0, 0, service,
                                     service == serviceFilter_);
        chip->SetVariant(ChipVariant::Outlined);
        chip->onSelectedChanged = [this, service](bool selected) {
            SetServiceFilter(selected ? service : (serviceFilter_ == service ? std::string() : serviceFilter_));
        };
        FixedInRow(chip);
        filters_->AddChild(chip);
        serviceChips_.push_back(chip);
    }
    if (!serviceFilter_.empty() && !services.count(serviceFilter_)) serviceFilter_.clear();
    filters_->RequestRedraw();
}

void UltraCanvasMessageCenter::SelectVisibleRow(int row) {
    if (row < 0 || row >= static_cast<int>(visible_.size())) return;
    MessageCenterEntry& entry = entries_[visible_[row]];
    const bool changed = selectedId_ != entry.message.envelope.id;
    selectedId_ = entry.message.envelope.id;
    ShowDetail(&entry);
    if (changed && onSelectionChanged) onSelectionChanged(&entry);
    if (entry.unread) MarkRead(selectedId_, true);
}

void UltraCanvasMessageCenter::ShowDetail(const MessageCenterEntry* entry) {
    if (!detail_) return;
    for (auto& button : actionButtons_) detailButtons_->RemoveChild(button);
    actionButtons_.clear();
    if (!entry) {
        detailTitle_->SetText("Nothing selected");
        detailMeta_->SetText(entries_.empty() ? "The feed is empty." : "Select a message.");
        detailBody_->SetText("");
        openButton_->SetVisible(false);
        readButton_->SetVisible(false);
        dismissButton_->SetVisible(false);
        detail_->RequestRedraw();
        return;
    }
    std::string title;
    switch (entry->section) {
        case MessageCenterSection::Chats:
            title = entry->sender.empty() ? entry->sourceTitle : entry->sender;
            if (!entry->sourceTitle.empty() && entry->sourceTitle != title) title += " in " + entry->sourceTitle;
            break;
        case MessageCenterSection::Mail:
            title = entry->snippet;   // the subject
            break;
        default:
            title = entry->snippet.empty() ? entry->title : entry->snippet;
            break;
    }
    detailTitle_->SetText(title);
    std::string meta = entry->sourceTitle;
    if (!entry->service.empty() && entry->service != Lower(entry->sourceTitle)) meta += " \xC2\xB7 " + entry->service;
    if (entry->section == MessageCenterSection::Mail && !entry->sender.empty()) meta += " \xC2\xB7 " + entry->sender;
    meta += " \xC2\xB7 " + FormatTime(entry->timeMs, NowMs());
    if (!entry->message.envelope.from.displayName.empty())
        meta += " \xC2\xB7 via " + entry->message.envelope.from.displayName;
    if (entry->urgent) meta += " \xC2\xB7 urgent";
    detailMeta_->SetText(meta);
    detailBody_->SetText(entry->body);
    openButton_->SetVisible(true);
    readButton_->SetVisible(true);
    readButton_->SetText(entry->unread ? "Mark read" : "Mark unread");
    dismissButton_->SetVisible(true);
    const float bh = static_cast<float>(style_.controlHeight);
    for (const auto& action : entry->actions) {
        auto button = CreateButton(GetIdentifier() + ".action." + action.id, 0, 0, 110, bh,
                                   action.label.empty() ? action.id : action.label);
        const std::string messageId = entry->message.envelope.id;
        const std::string actionId = action.id;
        button->onClick = [this, messageId, actionId]() { InvokeAction(messageId, actionId); };
        FixedInRow(button);
        detailButtons_->AddChild(button);
        actionButtons_.push_back(button);
    }
    detail_->RequestRedraw();
}

// ---------------------------------------------------------------------------
// Acting on rows
// ---------------------------------------------------------------------------

void UltraCanvasMessageCenter::PostFeedState(const char* topic, const MessageCenterEntry& entry) {
    if (!IsConnected()) return;
    JSONValue body = JSONValue::MakeObject();
    body.Set("messageId", entry.message.envelope.id);
    UltraMsgResult result = UltraMsg_Post(endpoint_, topic, body);
    if (!result) Fail(result, std::string("post ") + topic);
}

void UltraCanvasMessageCenter::MarkRead(const std::string& messageId, bool read) {
    const int index = FindEntry(messageId);
    if (index < 0) return;
    MessageCenterEntry& entry = entries_[index];
    if (entry.unread == !read) return;
    entry.unread = !read;
    entry.message.read = read;
    if (IsConnected()) {
        UltraMsgResult result = read ? UltraMsg_MarkRead(endpoint_, {messageId})
                                     : UltraMsg_MarkUnread(endpoint_, {messageId});
        if (!result) Fail(result, read ? "mark read" : "mark unread");
        if (read) PostFeedState(UltraMsgTopics::FeedRead, entry);
    }
    RebuildSources();
    RebuildVisible();
    NotifyUnread();
}

void UltraCanvasMessageCenter::Dismiss(const std::string& messageId) {
    const int index = FindEntry(messageId);
    if (index < 0) return;
    const MessageCenterEntry entry = entries_[index];
    if (IsConnected()) {
        UltraMsgResult result = UltraMsg_Dismiss(endpoint_, {messageId});
        if (!result) Fail(result, "dismiss");
        PostFeedState(UltraMsgTopics::FeedDismissed, entry);
        const std::string notificationId = !entry.mirrorOf.empty() ? entry.mirrorOf
                                           : entry.section == MessageCenterSection::System ? messageId
                                                                                            : std::string();
        if (!notificationId.empty()) {
            JSONValue body = JSONValue::MakeObject();
            body.Set("notificationId", notificationId);
            body.Set("reason", "dismissed");
            UltraMsg_Post(endpoint_, UltraMsgTopics::SystemNotificationDismissed, body);
        }
    }
    RemoveEntry(messageId);
}

void UltraCanvasMessageCenter::InvokeAction(const std::string& messageId, const std::string& actionId) {
    const int index = FindEntry(messageId);
    if (index < 0) return;
    const MessageCenterEntry& entry = entries_[index];
    if (IsConnected()) {
        JSONValue body = JSONValue::MakeObject();
        body.Set("notificationId", !entry.mirrorOf.empty() ? entry.mirrorOf : messageId);
        body.Set("actionId", actionId);
        UltraMsgResult result = UltraMsg_Post(endpoint_, UltraMsgTopics::SystemNotificationAction, body);
        if (!result) Fail(result, "action " + actionId);
    }
    MarkRead(messageId, true);
}

void UltraCanvasMessageCenter::Open(const std::string& messageId) {
    const int index = FindEntry(messageId);
    if (index < 0) return;
    const MessageCenterEntry entry = entries_[index];
    MarkRead(messageId, true);
    if (onOpen) {
        onOpen(entry);
        return;
    }
    if (!entry.actions.empty()) InvokeAction(messageId, entry.actions.front().id);
}

} // namespace UltraCanvas
