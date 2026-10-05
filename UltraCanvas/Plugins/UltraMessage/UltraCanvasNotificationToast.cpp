// UltraCanvas/Plugins/UltraMessage/UltraCanvasNotificationToast.cpp
// See include/Plugins/UltraMessage/UltraCanvasNotificationToast.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "Plugins/UltraMessage/UltraCanvasNotificationToast.h"
#include "UltraMessage/UltraMessageUltraCanvas.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace UltraCanvas {

using UltraCanvas::JSONValue;

namespace {

constexpr size_t kMaxSummaryBytes = 160;
constexpr size_t kMaxBodyBytes = 600;
constexpr size_t kMaxActionButtons = 3;
constexpr int64_t kHoldMs = 1500;
constexpr unsigned int kTickMs = 250;

std::string Str(const JSONValue& body, const char* key) {
    const JSONValue* v = body.Find(key);
    return v && v->IsString() ? v->GetString() : std::string();
}

// At most `maxBytes`, cut at a UTF-8 character boundary, with an ellipsis.
std::string Clip(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "\xE2\x80\xA6";
}

// A summary is one line however the application wrote it.
std::string OneLine(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}

// The icon a notification names, as a file this element can draw: a path or
// a file:// URI. An icon-theme name ("mail-unread") is not resolved here.
std::string IconFile(const std::string& icon) {
    std::string path = icon;
    if (path.rfind("file://", 0) == 0) path = path.substr(7);
    if (path.empty() || path[0] != '/') {
#ifdef _WIN32
        if (path.size() < 3 || path[1] != ':') return std::string();
#else
        return std::string();
#endif
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(PathFromUtf8(path), ec) ? path : std::string();
}

} // namespace

// ===========================================================================
// Content
// ===========================================================================

bool NotificationToastContent::FromMessage(const UltraMsgMessage& message, NotificationToastContent& out) {
    if (message.envelope.topic != UltraMsgTopics::SystemNotification) return false;
    UltraMessage::SystemNotification n;
    if (!UltraMessage::ParseSystemNotification(message.body, n)) return false;
    out = NotificationToastContent{};
    out.notificationId = message.envelope.id;
    out.appName = !n.appName.empty() ? n.appName
                  : !message.envelope.from.displayName.empty() ? message.envelope.from.displayName
                                                               : message.envelope.from.appId;
    out.summary = Clip(OneLine(n.summary), kMaxSummaryBytes);
    out.body = Clip(n.body, kMaxBodyBytes);
    out.iconPath = IconFile(n.icon);
    out.urgency = n.urgency.empty() ? std::string("normal") : n.urgency;
    out.actions = n.actions;
    return true;
}

bool NotificationToastContent::HasDefaultAction() const {
    for (const auto& a : actions)
        if (a.id == "default") return true;
    return false;
}

// ===========================================================================
// The element
// ===========================================================================

UltraCanvasNotificationToast::UltraCanvasNotificationToast(const std::string& identifier, float x, float y,
                                                           float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    Build();
}

void UltraCanvasNotificationToast::Build() {
    const NotificationToastStyle& s = style_;
    const float inner = static_cast<float>(s.width - 2 * s.padding - 3);

    layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    SetPadding(s.padding, s.padding, s.padding, s.padding);
    SetBackgroundColor(s.background);
    SetBorders(1, s.border);
    SetBorderLeft(3, s.accent);
    size.width = CSSLayout::Dimension::Px(static_cast<float>(s.width));
    size.height = CSSLayout::Dimension::Auto();

    // [icon] Application name ..................... [×]
    header_ = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-header");
    header_->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    header_->size.height = CSSLayout::Dimension::Px(20);

    icon_ = std::make_shared<UltraCanvasImageElement>(GetIdentifier() + "-icon", 16.0f, 16.0f);
    icon_->SetFitMode(ImageFitMode::Contain);
    icon_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    icon_->SetVisible(false);
    header_->AddChild(icon_);

    app_ = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "-app", "");
    app_->SetFontSize(s.appFontSize);
    app_->SetTextColor(s.textSecondary);
    app_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    app_->size.width = CSSLayout::Dimension::Auto();
    app_->size.height = CSSLayout::Dimension::Auto();
    header_->AddChild(app_);

    // An icon rather than a "×": in a box this small the text is elided.
    // Drawn as a mask, so it takes the button's text colour.
    close_ = std::make_shared<UltraCanvasButton>(GetIdentifier() + "-close", 20.0f, 20.0f, "");
    ButtonStyle closeStyle = close_->GetStyle();
    closeStyle.normalColor = Colors::Transparent;
    closeStyle.hoverColor = Color(0, 0, 0, 30);
    closeStyle.pressedColor = Color(0, 0, 0, 60);
    closeStyle.borderWidth = 0;
    closeStyle.cornerRadius = 10.0f;
    closeStyle.useIconAsMask = true;
    close_->SetStyle(closeStyle);
    close_->SetTextColors(s.textSecondary, s.textPrimary);
    close_->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/notification-close.svg"));
    close_->SetIconSize(12, 12);
    close_->SetTooltip("Close");
    close_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    close_->SetOnClick([this]() { if (onClose) onClose(); });
    header_->AddChild(close_);
    AddChild(header_);

    summary_ = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "-summary", "");
    summary_->SetFontSize(s.summaryFontSize);
    summary_->SetFontWeight(FontWeight::Bold);
    summary_->SetTextColor(s.textPrimary);
    summary_->SetWrap(TextWrap::WrapWordChar);
    summary_->size.width = CSSLayout::Dimension::Px(inner);
    summary_->size.height = CSSLayout::Dimension::Auto();
    summary_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    summary_->onClick = [this]() { if (onAction) onAction("default"); };
    summary_->onHoverEnter = [this]() { if (onHover) onHover(); };
    AddChild(summary_);

    body_ = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "-body", "");
    body_->SetFontSize(s.bodyFontSize);
    body_->SetTextColor(s.textPrimary);
    body_->SetWrap(TextWrap::WrapWordChar);
    body_->size.width = CSSLayout::Dimension::Px(inner);
    body_->size.height = CSSLayout::Dimension::Auto();
    body_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    body_->onClick = [this]() { if (onAction) onAction("default"); };
    body_->onHoverEnter = [this]() { if (onHover) onHover(); };
    AddChild(body_);

    actions_ = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "-actions");
    actions_->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    actions_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    actions_->SetMargin(4, 0, 0, 0);
    actions_->SetVisible(false);
    AddChild(actions_);
}

void UltraCanvasNotificationToast::SetStyle(const NotificationToastStyle& style) {
    style_ = style;
    ClearChildren();
    actionButtons_.clear();
    Build();
    ApplyContent();
}

void UltraCanvasNotificationToast::SetContent(const NotificationToastContent& content) {
    content_ = content;
    ApplyContent();
}

void UltraCanvasNotificationToast::ApplyContent() {
    const NotificationToastStyle& s = style_;
    SetBorderLeft(3, content_.urgency == "critical" ? s.urgentAccent : s.accent);

    app_->SetText(content_.appName);
    const bool hasIcon = !content_.iconPath.empty() && icon_->LoadFromFile(content_.iconPath);
    icon_->SetVisible(hasIcon);

    summary_->SetText(content_.summary);
    summary_->SetVisible(!content_.summary.empty());
    body_->SetText(content_.body);
    body_->SetVisible(!content_.body.empty());
    // The pointer hand says a click does something.
    summary_->SetMouseCursor(content_.HasDefaultAction() ? UCMouseCursor::Hand : UCMouseCursor::Default);
    body_->SetMouseCursor(content_.HasDefaultAction() ? UCMouseCursor::Hand : UCMouseCursor::Default);

    for (auto& button : actionButtons_) actions_->RemoveChild(button);
    actionButtons_.clear();
    for (const auto& action : content_.actions) {
        if (action.id.empty() || action.id == "default") continue;
        if (actionButtons_.size() >= kMaxActionButtons) break;
        const std::string label = action.label.empty() ? action.id : action.label;
        const float width = std::clamp(28.0f + 7.0f * static_cast<float>(label.size()), 60.0f, 140.0f);
        auto button = std::make_shared<UltraCanvasButton>(
            GetIdentifier() + "-action-" + std::to_string(actionButtons_.size()), width, 24.0f, label);
        button->SetFontSize(10.0f);
        button->SetCornerRadius(4.0f);
        button->SetColors(Colors::White, Color(233, 238, 244));
        button->SetTextColors(s.textPrimary);
        button->SetBorder(1.0f, Color(0, 0, 0, 60));
        button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        const std::string actionId = action.id;
        button->SetOnClick([this, actionId]() { if (onAction) onAction(actionId); });
        actions_->AddChild(button);
        actionButtons_.push_back(button);
    }
    actions_->SetVisible(!actionButtons_.empty());
    InvalidateLayout();
    RequestRedraw();
}

// ===========================================================================
// The host
// ===========================================================================

UltraCanvasNotificationToastHost::UltraCanvasNotificationToastHost() = default;

UltraCanvasNotificationToastHost::~UltraCanvasNotificationToastHost() {
    Disconnect();
    if (timer_ != InvalidTimerId)
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(timer_);
    timer_ = InvalidTimerId;
    for (auto& toast : toasts_) CloseWindow(toast);
    toasts_.clear();
}

int64_t UltraCanvasNotificationToastHost::NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

UltraMsgConnectOptions UltraCanvasNotificationToastHost::DefaultConnectOptions() {
    UltraMsgConnectOptions options;
    options.appId = "org.ultraos.notifications";
    options.displayName = "Notifications";
    options.deliverOnUIThread = true;
    options.startBrokerIfAbsent = true;
    return options;
}

bool UltraCanvasNotificationToastHost::Connect(const UltraMsgConnectOptions& options) {
    if (endpoint_ != UltraMsgInvalidHandle) return true;
    if (!UltraMsg_HasUIDispatcher() && UltraCanvasApplicationBase::GetCurrent())
        UltraMsg_UseUltraCanvasApplication();
    UltraMsgResult error;
    endpoint_ = UltraMsg_Connect(options, &error);
    if (endpoint_ == UltraMsgInvalidHandle) {
        lastError_ = error.message.empty() ? "cannot connect to the UltraMessage bus" : error.message;
        return false;
    }
    lastError_.clear();
    auto subscribe = [this](const char* topic, UltraMsgCallback callback) {
        UltraMsgResult result;
        UltraMsgHandle sub = UltraMsg_Subscribe(endpoint_, topic, std::move(callback), {}, &result);
        if (sub == UltraMsgInvalidHandle) lastError_ = "subscribe " + std::string(topic) + ": " + result.message;
        else subscriptions_.push_back(sub);
    };
    subscribe(UltraMsgTopics::SystemNotification, [this](const UltraMsgMessage& m) { Ingest(m); });
    // Dismissed or acted on anywhere - by the application (CloseNotification),
    // the message centre, or this host itself - the toast has had its say.
    subscribe(UltraMsgTopics::SystemNotificationDismissed,
              [this](const UltraMsgMessage& m) { Withdraw(Str(m.body, "notificationId")); });
    subscribe(UltraMsgTopics::SystemNotificationAction,
              [this](const UltraMsgMessage& m) { Withdraw(Str(m.body, "notificationId")); });
    subscribe(UltraMsgTopics::FeedDismissed, [this](const UltraMsgMessage& m) { Withdraw(Str(m.body, "messageId")); });
    return true;
}

void UltraCanvasNotificationToastHost::Disconnect() {
    for (UltraMsgHandle sub : subscriptions_) UltraMsg_Unsubscribe(sub);
    subscriptions_.clear();
    if (endpoint_ != UltraMsgInvalidHandle) UltraMsg_Disconnect(endpoint_);
    endpoint_ = UltraMsgInvalidHandle;
}

bool UltraCanvasNotificationToastHost::IsConnected() const {
    return endpoint_ != UltraMsgInvalidHandle && UltraMsg_IsConnected(endpoint_);
}

bool UltraCanvasNotificationToastHost::ShouldShow(const UltraMsgMessage& message) {
    const UltraMsgEnvelope& e = message.envelope;
    if (e.topic != UltraMsgTopics::SystemNotification) return false;
    if (e.kind != UltraMsgKind::Notice && e.kind != UltraMsgKind::RecordedNotice) return false;
    if (e.flags & UltraMsgFlag_Silent) return false;
    // Forwarded to the desktop's own notification service, or read from one
    // that drew it: on screen already.
    if (message.body.Find("displayed")) return false;
    return true;
}

int UltraCanvasNotificationToastHost::FindToast(const std::string& notificationId) const {
    if (notificationId.empty()) return -1;
    for (size_t i = 0; i < toasts_.size(); ++i)
        if (toasts_[i].content.notificationId == notificationId) return static_cast<int>(i);
    return -1;
}

int64_t UltraCanvasNotificationToastHost::ExpiryFor(const NotificationToastContent& content, int64_t nowMs) const {
    if (content.urgency == "critical") return 0;
    const int timeout = content.urgency == "low" ? lowTimeoutMs_ : normalTimeoutMs_;
    return timeout > 0 ? nowMs + timeout : 0;
}

bool UltraCanvasNotificationToastHost::Ingest(const UltraMsgMessage& message) {
    if (!ShouldShow(message)) return false;
    NotificationToastContent content;
    if (!NotificationToastContent::FromMessage(message, content)) return false;
    const int64_t now = NowMs();

    // The same notification again, or one that replaces a toast on screen:
    // the toast takes the new text and starts its time again.
    int index = FindToast(content.notificationId);
    if (index < 0 && (message.envelope.flags & UltraMsgFlag_Replace) && !message.envelope.replaces.empty())
        index = FindToast(message.envelope.replaces);
    if (index >= 0) {
        Toast& toast = toasts_[static_cast<size_t>(index)];
        toast.content = content;
        toast.expiresMs = ExpiryFor(content, now);
        if (toast.element) {
            toast.element->SetContent(content);
            if (toast.window) toast.window->RequestRedraw();
        }
        if (onShown) onShown(content);
        UpdateTimer();
        return true;
    }

    Toast toast;
    toast.content = content;
    toast.expiresMs = ExpiryFor(content, now);
    toasts_.insert(toasts_.begin(), std::move(toast));
    // The oldest beyond the limit goes; the feed still has it.
    while (maxVisible_ > 0 && toasts_.size() > static_cast<size_t>(maxVisible_)) Remove(toasts_.size() - 1);
    if (windowsEnabled_) OpenWindow(toasts_.front());
    Restack();
    if (onShown) onShown(content);
    UpdateTimer();
    return true;
}

void UltraCanvasNotificationToastHost::Withdraw(const std::string& notificationId) {
    const int index = FindToast(notificationId);
    if (index < 0) return;
    Remove(static_cast<size_t>(index));
    Restack();
    UpdateTimer();
}

void UltraCanvasNotificationToastHost::Post(const char* topic, const std::string& notificationId, const char* key,
                                            const std::string& value) {
    if (!IsConnected()) return;
    JSONValue body = JSONValue::MakeObject();
    body.Set("notificationId", notificationId);
    body.Set(key, value);
    UltraMsg_Post(endpoint_, topic, body);
}

void UltraCanvasNotificationToastHost::InvokeAction(const std::string& notificationId, const std::string& actionId) {
    const int index = FindToast(notificationId);
    if (index < 0) return;
    const NotificationToastContent content = toasts_[static_cast<size_t>(index)].content;
    Remove(static_cast<size_t>(index));
    Restack();
    UpdateTimer();
    Post(UltraMsgTopics::SystemNotificationAction, notificationId, "actionId", actionId);
    if (onAction) onAction(content, actionId);
}

void UltraCanvasNotificationToastHost::Dismiss(const std::string& notificationId) {
    const int index = FindToast(notificationId);
    if (index < 0) return;
    const NotificationToastContent content = toasts_[static_cast<size_t>(index)].content;
    Remove(static_cast<size_t>(index));
    Restack();
    UpdateTimer();
    Post(UltraMsgTopics::SystemNotificationDismissed, notificationId, "reason", "dismissed");
    if (onDismissed) onDismissed(content);
}

void UltraCanvasNotificationToastHost::ActivateBody(const std::string& notificationId) {
    const int index = FindToast(notificationId);
    if (index < 0) return;
    if (toasts_[static_cast<size_t>(index)].content.HasDefaultAction()) {
        InvokeAction(notificationId, "default");
        return;
    }
    // Nothing to open: seen, so it goes - unread in the feed, where it stays.
    Withdraw(notificationId);
}

void UltraCanvasNotificationToastHost::Hold(const std::string& notificationId, int64_t nowMs) {
    const int index = FindToast(notificationId);
    if (index >= 0) toasts_[static_cast<size_t>(index)].heldUntilMs = nowMs + kHoldMs;
}

void UltraCanvasNotificationToastHost::Tick(int64_t nowMs) {
    bool removed = false;
    for (size_t i = toasts_.size(); i-- > 0;) {
        const Toast& toast = toasts_[i];
        if (toast.expiresMs == 0 || nowMs < toast.expiresMs || nowMs < toast.heldUntilMs) continue;
        Remove(i);
        removed = true;
    }
    if (removed) {
        Restack();
        UpdateTimer();
    }
}

std::vector<NotificationToastContent> UltraCanvasNotificationToastHost::GetToasts() const {
    std::vector<NotificationToastContent> out;
    out.reserve(toasts_.size());
    for (const auto& toast : toasts_) out.push_back(toast.content);
    return out;
}

std::shared_ptr<UltraCanvasWindowBase> UltraCanvasNotificationToastHost::GetToastWindow(
        const std::string& notificationId) const {
    const int index = FindToast(notificationId);
    return index < 0 ? nullptr : toasts_[static_cast<size_t>(index)].window;
}

void UltraCanvasNotificationToastHost::SetCorner(NotificationToastCorner corner) {
    corner_ = corner;
    Restack();
}

void UltraCanvasNotificationToastHost::SetScreenMargins(int left, int top, int right, int bottom) {
    marginLeft_ = std::max(0, left);
    marginTop_ = std::max(0, top);
    marginRight_ = std::max(0, right);
    marginBottom_ = std::max(0, bottom);
    Restack();
}

void UltraCanvasNotificationToastHost::SetMaxVisible(int count) {
    maxVisible_ = std::max(1, count);
}

void UltraCanvasNotificationToastHost::SetTimeouts(int lowMs, int normalMs) {
    lowTimeoutMs_ = std::max(0, lowMs);
    normalTimeoutMs_ = std::max(0, normalMs);
}

void UltraCanvasNotificationToastHost::SetStyle(const NotificationToastStyle& style) {
    style_ = style;
}

void UltraCanvasNotificationToastHost::Remove(size_t index) {
    if (index >= toasts_.size()) return;
    CloseWindow(toasts_[index]);
    toasts_.erase(toasts_.begin() + static_cast<std::ptrdiff_t>(index));
}

void UltraCanvasNotificationToastHost::OpenWindow(Toast& toast) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;

    WindowConfig config;
    config.title = toast.content.appName.empty() ? "Notification" : toast.content.appName;
    config.type = WindowType::Notification;
    config.width = style_.width;
    config.height = 96;                      // fitted to the text once laid out
    config.minWidth = style_.width;
    config.maxWidth = style_.width;
    config.minHeight = 48;
    config.maxHeight = style_.maxHeight;
    config.resizable = true;                 // so fitting to the text may change the height
    config.minimizable = false;
    config.maximizable = false;
    config.closable = false;
    config.autoResizeToContent = true;
    config.backgroundColor = style_.background;
    config.x = 0;
    config.y = 0;
    auto window = CreateWindow(config);
    if (!window || !window->IsCreated()) return;

    const std::string id = toast.content.notificationId;
    auto element = CreateNotificationToast("um-toast-" + std::to_string(++serial_), 0, 0,
                                           static_cast<float>(style_.width), 0);
    element->SetStyle(style_);
    element->SetContent(toast.content);
    // The host outlives every toast window (it closes them all when it goes),
    // so the callbacks hold it by a plain pointer.
    element->onAction = [this, id](const std::string& actionId) {
        if (actionId == "default") ActivateBody(id);
        else InvokeAction(id, actionId);
    };
    element->onClose = [this, id]() { Dismiss(id); };
    element->onHover = [this, id]() { Hold(id, NowMs()); };

    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->AddChild(element);
    // The pointer resting anywhere on the toast holds it.
    window->InstallEventFilter("um-toast-hold", [this, id](const UCEvent&) {
        Hold(id, NowMs());
        return false;
    }, {UCEventType::MouseMove, UCEventType::MouseEnter});
    // Fitted to its text: the toasts below move with it.
    window->onWindowResize = [this](int, int) { Restack(); };

    toast.window = window;
    toast.element = element;
    Restack();
    window->Show();
}

void UltraCanvasNotificationToastHost::CloseWindow(Toast& toast) {
    if (!toast.window) return;
    std::shared_ptr<UltraCanvasWindowBase> window = std::move(toast.window);
    toast.element.reset();
    window->onWindowResize = nullptr;
    window->UnInstallWindowEventFilter("um-toast-hold");
    window->Hide();
    // Closed on the next turn of the loop: this may run inside the window's
    // own click handler.
    if (auto* app = UltraCanvasApplicationBase::GetCurrent())
        app->PostToUIThread([window]() { window->Close(); });
    else
        window->Close();
}

void UltraCanvasNotificationToastHost::Restack() {
    // Positions are in the window's native geometry (physical px on X11 and
    // Windows), as SetWindowPosition and GetScreenBounds take them; margins
    // and the gap are logical and scaled the same way as the window.
    const bool fromTop = corner_ == NotificationToastCorner::TopRight || corner_ == NotificationToastCorner::TopLeft;
    const bool atRight = corner_ == NotificationToastCorner::TopRight || corner_ == NotificationToastCorner::BottomRight;
    int offset = 0;
    for (auto& toast : toasts_) {
        if (!toast.window) continue;
        int sx = 0, sy = 0, sw = 0, sh = 0;
        toast.window->GetScreenBounds(sx, sy, sw, sh);
        int ww = 0, wh = 0;
        toast.window->GetNativeWindowSize(ww, wh);
        if (sw <= 0 || sh <= 0 || ww <= 0) continue;
        const double scale = toast.window->GetConfig().width > 0
                                 ? static_cast<double>(ww) / toast.window->GetConfig().width
                                 : 1.0;
        auto px = [scale](int logical) { return static_cast<int>(logical * scale + 0.5); };
        const int edge = px(12);
        const int x = atRight ? sx + sw - px(marginRight_) - edge - ww : sx + px(marginLeft_) + edge;
        const int y = fromTop ? sy + px(marginTop_) + edge + offset
                              : sy + sh - px(marginBottom_) - edge - offset - wh;
        toast.window->SetWindowPosition(x, y);
        offset += wh + px(style_.gap);
    }
}

void UltraCanvasNotificationToastHost::UpdateTimer() {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    bool timed = false;
    for (const auto& toast : toasts_) timed = timed || toast.expiresMs != 0;
    if (timed && timer_ == InvalidTimerId) {
        timer_ = app->StartTimer(kTickMs, true, [this](TimerId) { Tick(NowMs()); });
    } else if (!timed && timer_ != InvalidTimerId) {
        app->StopTimer(timer_);
        timer_ = InvalidTimerId;
    }
}

} // namespace UltraCanvas
