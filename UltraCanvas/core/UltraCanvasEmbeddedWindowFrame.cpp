// core/UltraCanvasEmbeddedWindowFrame.cpp
// The embedded window manager's title bar: a borderless window built from
// an UltraCanvasLabel and three UltraCanvasButtons.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasEmbeddedWindowFrame.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

namespace UltraCanvas {

    namespace {
        bool g_creatingFrame = false;

        std::string TitleBarIcon(const char* name) {
            return NormalizePath(GetResourcesDir() + "media/icons/desktop/" + name);
        }
    }

    bool UltraCanvasEmbeddedWindowFrame::IsCreatingFrame() { return g_creatingFrame; }

    std::shared_ptr<UltraCanvasEmbeddedWindowFrame> UltraCanvasEmbeddedWindowFrame::Create(
            const std::string& title, int width, int height, bool minimizable, bool maximizable,
            bool closable, Actions actions) {
        std::shared_ptr<UltraCanvasEmbeddedWindowFrame> frame(new UltraCanvasEmbeddedWindowFrame());
        frame->actions = std::move(actions);
        frame->title = title;

        WindowConfig config;
        config.title = title;
        config.type = WindowType::Borderless;
        config.width = std::max(1, width);
        config.height = std::max(1, height);
        config.minWidth = 1;
        config.minHeight = 1;
        config.resizable = false;
        config.minimizable = false;
        config.maximizable = false;
        config.deleteOnClose = true;

        g_creatingFrame = true;
        frame->window = CreateWindow(config);
        g_creatingFrame = false;
        if (!frame->window) return nullptr;

        frame->Build(minimizable, maximizable, closable);
        return frame;
    }

    UltraCanvasEmbeddedWindowFrame::~UltraCanvasEmbeddedWindowFrame() {
        Close();
    }

    UltraCanvasWindowBase* UltraCanvasEmbeddedWindowFrame::GetWindow() const {
        return window.get();
    }

    void UltraCanvasEmbeddedWindowFrame::Build(bool minimizable, bool maximizable, bool closable) {
        auto* win = window.get();
        win->layout.SetFlexRow()
                   .SetFlexGap(2)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        win->SetPadding(0);

        titleLabel = std::make_shared<UltraCanvasLabel>("FrameTitle", title);
        titleLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        titleLabel->SetAlignment(TextAlignment::Left);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetMargin(0, 0, 0, 10);
        win->AddChild(titleLabel);

        const int side = std::max(16, win->GetConfig().height - 4);
        auto makeButton = [&](const char* id, const char* icon, std::function<void()> click) {
            auto button = std::make_shared<UltraCanvasButton>(id, side + 8, side, "");
            ButtonStyle style = ButtonStyles::FlatStyle();
            style.useIconAsMask = true;
            style.cornerRadius = 4.0f;
            button->SetStyle(style);
            button->SetIcon(TitleBarIcon(icon));
            button->SetIconSize(side - 8, side - 8);
            button->onClick = std::move(click);
            win->AddChild(button);
            return button;
        };
        if (minimizable) {
            minimizeButton = makeButton("FrameMinimize", "titlebar-minimize.svg",
                                        [this]() { if (actions.minimize) actions.minimize(); });
        }
        if (maximizable) {
            maximizeButton = makeButton("FrameMaximize", "titlebar-maximize.svg",
                                        [this]() { if (actions.toggleMaximize) actions.toggleMaximize(); });
        }
        if (closable) {
            closeButton = makeButton("FrameClose", "titlebar-close.svg",
                                     [this]() { if (actions.close) actions.close(); });
            closeButton->SetTooltip(Tr("Close"));
        }

        // A press anywhere but on a button takes the window; a double click
        // there maximizes or restores it.
        win->InstallEventFilter("embedded-frame-pointer",
                                [this](const UCEvent& event) { return OnPointer(event); },
                                {UCEventType::MouseDown, UCEventType::MouseDoubleClick});
        ApplyColours();
    }

    bool UltraCanvasEmbeddedWindowFrame::OnPointer(const UCEvent& event) {
        if (!window || event.button != UCMouseButton::Left) return false;
        UltraCanvasUIElement* hit = window->FindElementAtPoint(
                Point2Df(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y)), true);
        if (hit && dynamic_cast<UltraCanvasButton*>(hit)) return false;   // the button's own
        if (event.type == UCEventType::MouseDoubleClick) {
            if (maximizeButton && actions.toggleMaximize) actions.toggleMaximize();
            return true;
        }
        if (actions.beginMove) actions.beginMove();
        return true;
    }

    void UltraCanvasEmbeddedWindowFrame::ApplyColours() {
        if (!window) return;
        const ThemeRoles& roles = UltraCanvasTheme::Current().roles;
        const Color ground = active ? roles.raised : roles.panel;
        const Color ink = active ? roles.ink : roles.inkMuted;
        window->SetBackgroundColor(ground);
        if (titleLabel) {
            titleLabel->SetTextColor(ink);
            titleLabel->SetBackgroundColor(Colors::Transparent);
        }
        for (const auto& button : {minimizeButton, maximizeButton, closeButton}) {
            if (!button) continue;
            ButtonStyle style = button->GetStyle();
            style.normalColor = Colors::Transparent;
            style.hoverColor = roles.hover;
            style.pressedColor = roles.pressed;
            style.normalTextColor = ink;
            style.hoverTextColor = roles.ink;
            style.pressedTextColor = roles.ink;
            style.borderWidth = 0.0f;
            if (button == closeButton) {
                style.hoverColor = roles.danger;
                style.hoverTextColor = Colors::White;
                style.pressedColor = roles.danger;
                style.pressedTextColor = Colors::White;
            }
            button->SetStyle(style);
        }
        window->RequestRedraw();
    }

    void UltraCanvasEmbeddedWindowFrame::SetTitle(const std::string& newTitle) {
        if (newTitle == title) return;
        title = newTitle;
        if (titleLabel) titleLabel->SetText(title);
        if (window) window->SetWindowTitle(title);
    }

    void UltraCanvasEmbeddedWindowFrame::SetActive(bool isActive) {
        if (isActive == active) return;
        active = isActive;
        ApplyColours();
    }

    void UltraCanvasEmbeddedWindowFrame::SetMaximized(bool isMaximized) {
        if (isMaximized == maximized) return;
        maximized = isMaximized;
        if (maximizeButton) {
            maximizeButton->SetIcon(TitleBarIcon(maximized ? "titlebar-restore.svg" : "titlebar-maximize.svg"));
        }
    }

    void UltraCanvasEmbeddedWindowFrame::Close() {
        if (closed) return;
        closed = true;
        if (window) {
            window->UnInstallWindowEventFilter("embedded-frame-pointer");
            window->Close();
        }
    }

} // namespace UltraCanvas
