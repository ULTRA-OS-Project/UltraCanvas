// UltraCanvas/include/Plugins/UltraMessage/UltraCanvasNotificationToast.h
// Notifications drawn on screen by UltraCanvas itself, for the desktops where
// nothing else draws them: where UltraMessage is the notification server
// (ULTRA OS, or a Linux session without a notification daemon) every
// application's notification arrives on the bus and, until now, reached only
// the feed.
//
// UltraCanvasNotificationToast is one notification as an element: the
// application's icon and name, the summary, the body, the notification's own
// action buttons and a close button, built from catalogue elements (labels,
// buttons, an image) in a container with an accent stripe. A click on the
// summary or the body is the notification's "default" action.
//
// UltraCanvasNotificationToastHost is the screen of last resort: a client of
// the UltraMessage bus that shows each `system.notification` nothing else
// shows - live, not Silent, and without `displayed` (the broker marks what a
// presenter forwarded to the desktop's own notification service, and the
// notification adapters what the platform drew itself) - as a toast in its own
// WindowType::Notification window, stacked in a corner of the screen clear of
// the desktop's bars. What the user does goes back on the bus naming the
// notification: a click or an action button as `system.notification.action`,
// the close button as `system.notification.dismissed` - which the
// freedesktop adapter turns into ActionInvoked / NotificationClosed for the
// application, and which UltraMail takes as "open the mail". A toast leaves
// when its time is up (low 5 s, normal 8 s; critical stays until closed; the
// pointer resting on it holds it), when it is dismissed or acted on from
// anywhere on the bus, and a replacement updates it in place.
//
//   UltraCanvasNotificationToastHost toasts;
//   toasts.SetScreenMargins(0, 0, rightBarWidth, 0);   // keep clear of the bars
//   toasts.Connect();                                   // or hosts the broker
//
// Threading: deliveries reach the host on the UI thread (Connect installs the
// UltraCanvas dispatcher when none is installed and an application exists).
// The ULTRA OS desktop (Apps/UltraDesktop) hosts one.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTimer.h"
#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasWindowBase;   // UltraCanvasWindow is a per-platform alias of a subclass

struct NotificationToastStyle {
    Color background    = Color(252, 252, 253);
    Color border        = Color(0, 0, 0, 46);
    Color accent        = Colors::Selection;       // the stripe on the left
    Color urgentAccent  = Color(220, 53, 69);      // ... of a critical one: the Error alert's red
    Color textPrimary   = Color(24, 28, 35);
    Color textSecondary = Color(110, 116, 128);
    float appFontSize     = 9.5f;
    float summaryFontSize = 12.0f;
    float bodyFontSize    = 10.5f;
    int   width     = 340;    // of a toast, logical px
    int   maxHeight = 240;    // a longer body is cut off
    int   padding   = 10;
    int   gap       = 8;      // between two toasts
};

// What a toast shows, read from a `system.notification`.
struct NotificationToastContent {
    std::string notificationId;   // the bus message id the actions name
    std::string appName;
    std::string summary;
    std::string body;
    std::string iconPath;         // a file; an icon-theme name is not drawn
    std::string urgency = "normal";   // "low" | "normal" | "critical"
    std::vector<UltraMessage::NotificationAction> actions;   // "default" is the click on the body

    // False when the message is not a well-formed system.notification.
    static bool FromMessage(const UltraMsgMessage& message, NotificationToastContent& out);
    bool HasDefaultAction() const;
};

// ===== ONE NOTIFICATION =====
class UltraCanvasNotificationToast : public UltraCanvasContainer {
public:
    UltraCanvasNotificationToast(const std::string& identifier, float x, float y, float w, float h);

    void SetContent(const NotificationToastContent& content);
    const NotificationToastContent& GetContent() const { return content_; }
    void SetStyle(const NotificationToastStyle& style);
    const NotificationToastStyle& GetStyle() const { return style_; }

    // "default" for a click on the summary or the body, else the button's action id.
    std::function<void(const std::string& actionId)> onAction;
    std::function<void()> onClose;     // the close button
    std::function<void()> onHover;     // the pointer came onto it

    std::shared_ptr<UltraCanvasLabel>  GetSummaryLabel() const { return summary_; }
    std::shared_ptr<UltraCanvasLabel>  GetBodyLabel() const { return body_; }
    std::shared_ptr<UltraCanvasButton> GetCloseButton() const { return close_; }
    const std::vector<std::shared_ptr<UltraCanvasButton>>& GetActionButtons() const { return actionButtons_; }

private:
    void Build();
    void ApplyContent();

    NotificationToastStyle style_;
    NotificationToastContent content_;
    std::shared_ptr<UltraCanvasContainer> header_;
    std::shared_ptr<UltraCanvasImageElement> icon_;
    std::shared_ptr<UltraCanvasLabel> app_;
    std::shared_ptr<UltraCanvasButton> close_;
    std::shared_ptr<UltraCanvasLabel> summary_;
    std::shared_ptr<UltraCanvasLabel> body_;
    std::shared_ptr<UltraCanvasContainer> actions_;
    std::vector<std::shared_ptr<UltraCanvasButton>> actionButtons_;
};

inline std::shared_ptr<UltraCanvasNotificationToast> CreateNotificationToast(
        const std::string& identifier, float x = 0, float y = 0, float w = 340, float h = 0) {
    return std::make_shared<UltraCanvasNotificationToast>(identifier, x, y, w, h);
}

// ===== THE SCREEN OF LAST RESORT =====
enum class NotificationToastCorner { TopRight, BottomRight, TopLeft, BottomLeft };

class UltraCanvasNotificationToastHost {
public:
    UltraCanvasNotificationToastHost();
    ~UltraCanvasNotificationToastHost();
    UltraCanvasNotificationToastHost(const UltraCanvasNotificationToastHost&) = delete;
    UltraCanvasNotificationToastHost& operator=(const UltraCanvasNotificationToastHost&) = delete;

    // ---- the bus ---------------------------------------------------------

    // App id "org.ultraos.notifications", display name "Notifications",
    // UI-thread delivery, hosting a broker when none answers.
    static UltraMsgConnectOptions DefaultConnectOptions();
    // Connects and subscribes to `system.notification` (live only: what is
    // already in the journal is the feed's) and to the dismissals and
    // actions that take a toast away. False when no broker could be reached
    // or started (LastError says why).
    bool Connect(const UltraMsgConnectOptions& options = DefaultConnectOptions());
    void Disconnect();
    bool IsConnected() const;
    std::string LastError() const { return lastError_; }

    // Whether a toast host draws `message`: a live `system.notification` that
    // is not Silent and that nothing shows yet (no `displayed`).
    static bool ShouldShow(const UltraMsgMessage& message);

    // ---- what is shown ---------------------------------------------------

    // Shows a toast for `message` when ShouldShow, or updates the one it
    // replaces (UltraMsgFlag_Replace) or repeats (the same id). What the
    // subscription calls; a host or a test may call it without a bus.
    // True when a toast now shows it.
    bool Ingest(const UltraMsgMessage& message);
    // Takes a toast away without a word: it was dismissed or acted on
    // elsewhere. No-op for an id no toast shows.
    void Withdraw(const std::string& notificationId);
    // What the user did, as the toast's own controls do it: posts on the bus
    // (when connected), raises the callback and takes the toast away.
    void InvokeAction(const std::string& notificationId, const std::string& actionId);
    void Dismiss(const std::string& notificationId);
    // A click on the body: the "default" action where the notification has
    // one, else the toast just goes (the feed keeps the notification).
    void ActivateBody(const std::string& notificationId);
    // Takes away the toasts whose time is up at `nowMs` (steady clock, ms).
    // The host's timer calls it; tests call it with their own clock.
    void Tick(int64_t nowMs);
    // The pointer is on the toast until `nowMs` + 1.5 s: its time is held.
    void Hold(const std::string& notificationId, int64_t nowMs);

    size_t GetToastCount() const { return toasts_.size(); }
    // Newest first.
    std::vector<NotificationToastContent> GetToasts() const;
    // The toast's window, or null (windows off, or no such toast).
    std::shared_ptr<UltraCanvasWindowBase> GetToastWindow(const std::string& notificationId) const;

    // ---- placement and looks ---------------------------------------------

    void SetCorner(NotificationToastCorner corner);
    // Logical px along each screen edge the toasts keep clear of: the bars
    // of a desktop, a dock.
    void SetScreenMargins(int left, int top, int right, int bottom);
    void SetMaxVisible(int count);                 // the oldest goes beyond it; default 4
    // How long a toast of urgency low / normal stays, ms; 0 = until closed.
    // Critical ones always stay until closed.
    void SetTimeouts(int lowMs, int normalMs);
    void SetStyle(const NotificationToastStyle& style);
    // Off: no windows (tests; a host that draws the toasts itself through
    // the callbacks). On by default.
    void SetWindowsEnabled(bool enabled) { windowsEnabled_ = enabled; }

    // ---- callbacks (besides the bus posts) --------------------------------

    std::function<void(const NotificationToastContent&)> onShown;
    std::function<void(const NotificationToastContent&, const std::string& actionId)> onAction;
    std::function<void(const NotificationToastContent&)> onDismissed;

    static int64_t NowMs();   // the steady clock Tick and Hold expect

private:
    struct Toast {
        NotificationToastContent content;
        int64_t expiresMs = 0;     // 0: until closed
        int64_t heldUntilMs = 0;
        std::shared_ptr<UltraCanvasWindowBase> window;
        std::shared_ptr<UltraCanvasNotificationToast> element;
    };

    int  FindToast(const std::string& notificationId) const;
    int64_t ExpiryFor(const NotificationToastContent& content, int64_t nowMs) const;
    void Remove(size_t index);
    void OpenWindow(Toast& toast);
    void CloseWindow(Toast& toast);
    void Restack();
    void UpdateTimer();
    void Post(const char* topic, const std::string& notificationId, const char* key, const std::string& value);

    UltraMsgHandle endpoint_ = UltraMsgInvalidHandle;
    std::vector<UltraMsgHandle> subscriptions_;
    std::string lastError_;

    std::vector<Toast> toasts_;    // newest first
    NotificationToastCorner corner_ = NotificationToastCorner::TopRight;
    int marginLeft_ = 0, marginTop_ = 0, marginRight_ = 0, marginBottom_ = 0;
    int maxVisible_ = 4;
    int lowTimeoutMs_ = 5000;
    int normalTimeoutMs_ = 8000;
    NotificationToastStyle style_;
    bool windowsEnabled_ = true;
    TimerId timer_ = 0;
    int serial_ = 0;               // element identifiers
};

} // namespace UltraCanvas
