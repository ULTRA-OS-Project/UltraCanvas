// core/UltraCanvasToolPanelStack.cpp
// The column of tool panels, its order, context panels, the rail and its
// flyouts (UltraCanvasToolPanelStack.h).
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework
#include "UltraCanvasToolPanelStack.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolRows.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

    namespace {
        // How near the column's top or bottom edge a drag scrolls it, and by how much.
        constexpr float kDragScrollEdge = 24.0f;
        constexpr int kDragScrollStep = 14;

        std::string EscapeId(const std::string& id) {
            std::string out;
            for (char c : id) {
                if (c == '%' || c == ',' || c == ';' || c == '=') {
                    static const char* hex = "0123456789ABCDEF";
                    out += '%';
                    out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += hex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
            }
            return out;
        }

        std::string UnescapeId(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '%' && i + 2 < text.size()) {
                    auto value = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        return -1;
                    };
                    const int hi = value(text[i + 1]), lo = value(text[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        out += static_cast<char>(hi * 16 + lo);
                        i += 2;
                        continue;
                    }
                }
                out += text[i];
            }
            return out;
        }

        std::vector<std::string> SplitIds(const std::string& text) {
            std::vector<std::string> ids;
            std::string part;
            std::istringstream in(text);
            while (std::getline(in, part, ',')) {
                if (!part.empty()) ids.push_back(UnescapeId(part));
            }
            return ids;
        }

        std::string JoinIds(const std::vector<std::string>& ids) {
            std::string out;
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i) out += ',';
                out += EscapeId(ids[i]);
            }
            return out;
        }

        // The first character of a title, for a rail button without an icon.
        std::string FirstCharacter(const std::string& text) {
            if (text.empty()) return std::string();
            const unsigned char lead = static_cast<unsigned char>(text[0]);
            size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            return text.substr(0, std::min(length, text.size()));
        }

        bool ContainsId(const std::vector<std::string>& ids, const std::string& id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }
    }

    // ===== STATE AS TEXT =====

    std::string ToolPanelStackState::ToString() const {
        return "order=" + JoinIds(order) + ";open=" + JoinIds(open) + ";rail=" + (railMode ? "1" : "0");
    }

    ToolPanelStackState ToolPanelStackState::FromString(const std::string& text) {
        ToolPanelStackState state;
        std::string field;
        std::istringstream in(text);
        while (std::getline(in, field, ';')) {
            const size_t eq = field.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = field.substr(0, eq);
            const std::string value = field.substr(eq + 1);
            if (key == "order") state.order = SplitIds(value);
            else if (key == "open") state.open = SplitIds(value);
            else if (key == "rail") state.railMode = value == "1";
        }
        return state;
    }

    // ===== STYLE =====

    ToolPanelStackStyle ToolPanelStackStyle::Default() {
        ToolPanelStackStyle s;
        s.titleFont.fontSize = 12.0f;
        s.titleFont.fontWeight = FontWeight::Bold;
        return s;
    }

    ToolPanelStackStyle ToolPanelStackStyle::FromTheme(const UltraCanvasTheme& theme, const ToolPanelStackStyle& base) {
        ToolPanelStackStyle s = base;
        const ThemeRoles& r = theme.roles;
        const FontStyle body = theme.Font(ThemeTextRole::Body);
        s.titleFont.fontFamily = body.fontFamily;
        s.titleFont.fontSize = body.fontSize;
        s.chipFontSize = static_cast<float>(theme.FontSize(ThemeTextRole::Small));
        s.backgroundColor = r.ground;
        s.panelColor = r.panel;
        s.panelBorderColor = r.line;
        s.headerColor = theme.Role("tool-panel.header", r.well);
        s.titleColor = r.ink;
        s.iconColor = r.inkMuted;
        s.iconHoverColor = r.ink;
        s.hoverColor = r.hover;
        s.chipColor = r.ground;
        s.chipTextColor = r.inkMuted;
        s.contextBorderColor = r.accent;
        s.contextHeaderColor = theme.Role("tool-panel.context-header", r.accentSoft);
        s.contextChipColor = r.accent;
        s.contextChipTextColor = r.onAccent;
        s.dropIndicatorColor = r.accent;
        s.draggedPanelVeil = Color(r.ground.r, r.ground.g, r.ground.b, 150);
        s.focusColor = r.focus;
        s.railColor = r.ground;
        s.railBorderColor = r.line;
        s.flyoutBorderColor = r.lineStrong;
        s.flyoutShadowColor = theme.dark ? Color(0, 0, 0, 120) : Color(0, 0, 0, 56);
        return s;
    }

    // ===== THE COLUMN =====
    // The scrolling column the panels sit in. It tells the stack when it has
    // been laid out (a context panel's scroll anchor, a header to reveal) and
    // draws the drag's feedback over the panels.
    class UltraCanvasToolPanelColumn : public UltraCanvasContainer {
    public:
        UltraCanvasToolPanelColumn(const std::string& identifier, UltraCanvasToolPanelStack* owner)
                : UltraCanvasContainer(identifier, 0, 0, 0, 0), stack(owner) {}

        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override {
            UltraCanvasContainer::Arrange(finalRect, ctx);
            if (stack) stack->ApplyScrollAnchor();
        }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
            UltraCanvasContainer::Render(ctx, dirtyRect);
            if (stack && stack->IsDraggingPanel()) {
                ctx->PushState();
                stack->RenderDragFeedback(ctx);
                ctx->PopState();
            }
        }

        UltraCanvasToolPanelStack* stack;
    };

    // ===== THE FLYOUT =====
    // One panel beside the rail, as a popup: Escape and a click outside close
    // it, and the panel goes back into the (hidden) column.
    class UltraCanvasToolPanelFlyout : public UltraCanvasContainer {
    public:
        UltraCanvasToolPanelFlyout(const std::string& identifier, UltraCanvasToolPanelStack* owner)
                : UltraCanvasContainer(identifier, 0, 0, 0, 0), stack(owner) {
            layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            ContainerStyle scroll = GetContainerStyle();
            scroll.autoShowScrollbars = true;
            scroll.autoShowHorizontalScrollbar = false;
            SetContainerStyle(scroll);
        }

        PopupShadow GetPopupShadow() const override {
            PopupShadow shadow;
            if (!stack) return shadow;
            shadow.color = stack->GetStyle().flyoutShadowColor;
            shadow.blur = 14;
            shadow.offset = Point2Di(0, 3);
            shadow.cornerRadius = stack->GetStyle().cornerRadius;
            return shadow;
        }

        UltraCanvasToolPanelStack* stack;

    protected:
        void OnPopupClosed(ClosePopupReason reason) override {
            UltraCanvasContainer::OnPopupClosed(reason);
            if (stack) stack->FlyoutClosed();
        }
    };

    // ===== CONSTRUCTION =====

    UltraCanvasToolPanelStack::UltraCanvasToolPanelStack(const std::string& identifier, float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h), style(ToolPanelStackStyle::FromTheme()) {
        if (w > 0) {
            style.columnWidth = w;
            style.flyoutWidth = w;
        }
        layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        SetPadding(0);
        layoutItem.SetFlexShrink(0);

        column = std::make_shared<UltraCanvasToolPanelColumn>(identifier + ".column", this);
        column->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        column->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        ContainerStyle scroll = column->GetContainerStyle();
        scroll.autoShowScrollbars = true;
        scroll.autoShowHorizontalScrollbar = false;
        column->SetContainerStyle(scroll);
        AddChild(column);

        rail = std::make_shared<UltraCanvasContainer>(identifier + ".rail", 0, 0, 0, 0);
        rail->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Center);
        rail->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        rail->SetVisible(false);
        AddChild(rail);

        flyout = std::make_shared<UltraCanvasToolPanelFlyout>(identifier + ".flyout", this);

        SetStyle(style);
        // SetStyle by hand stops the following; the constructor's is the theme's.
        SetFollowTheme(FollowThemeByDefault());
    }

    UltraCanvasToolPanelStack::~UltraCanvasToolPanelStack() {
        if (flyout) {
            flyout->stack = nullptr;
            // A window closes its popups before its elements go, so a flyout
            // still open here is on a window that is still there.
            if (flyout->IsPopupElement()) {
                if (auto* win = flyout->GetWindow()) win->ClosePopup(*flyout);
            }
        }
        if (column) column->stack = nullptr;
        if (headerMenu) headerMenu->CloseMenu();
    }

    void UltraCanvasToolPanelStack::ApplyTheme(const UltraCanvasTheme& theme) {
        UltraCanvasContainer::ApplyTheme(theme);
        SetStyle(ToolPanelStackStyle::FromTheme(theme, style));
    }

    void UltraCanvasToolPanelStack::SetStyle(const ToolPanelStackStyle& newStyle) {
        StyleSetByHand();
        style = newStyle;
        SetBackgroundColor(style.backgroundColor);
        column->SetBackgroundColor(Colors::Transparent);
        column->SetPadding(style.padding);
        column->layout.SetFlexGap(style.panelGap);
        rail->SetBackgroundColor(style.railColor);
        rail->SetBorderRight(1.0f, style.railBorderColor);
        rail->SetPadding(style.padding, 0, style.padding, 0);
        rail->layout.SetFlexGap(4.0f);
        flyout->SetBackgroundColor(style.backgroundColor);
        flyout->SetBorders(1.0f, style.flyoutBorderColor, style.cornerRadius);
        flyout->SetPadding(style.padding);
        for (auto& panel : panels) {
            ApplyPanelLook(panel);
            ApplyRailButtonLook(panel, flyoutPanel == panel.id);
        }
        size.width = CSSLayout::Dimension::Px(railActive ? style.railWidth : style.columnWidth);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasToolPanelStack::SetColumnWidth(float width) {
        if (width <= 0.0f) return;
        style.columnWidth = width;
        if (!railActive) {
            size.width = CSSLayout::Dimension::Px(width);
            InvalidateLayout();
            RequestRedraw();
        }
    }

    // ===== LOOK OF A PANEL =====

    void UltraCanvasToolPanelStack::ApplyPanelLook(Panel& panel) {
        const bool context = panel.options.context;
        GroupBoxVisualStyle s = GroupBoxVisualStyle::Default();
        s.backgroundColor = style.panelColor;
        s.borderColor = context ? style.contextBorderColor : style.panelBorderColor;
        s.borderWidth = 1.0f;
        s.cornerRadius = style.cornerRadius;
        s.titleFont = style.titleFont;
        s.titleColor = style.titleColor;
        s.titleIndent = 8.0f;
        s.titleVerticalPadding = 6.0f;
        s.headerBackgroundColor = context ? style.contextHeaderColor : style.headerColor;
        s.showHeaderSeparator = true;
        s.infoIconColor = style.iconColor;
        s.infoIconHoverColor = style.iconHoverColor;
        s.closeIconColor = style.iconColor;
        s.closeIconHoverColor = style.iconHoverColor;
        s.closeHoverBackgroundColor = style.hoverColor;
        s.chipBackgroundColor = context ? style.contextChipColor : style.chipColor;
        s.chipTextColor = context ? style.contextChipTextColor : style.chipTextColor;
        s.chipFontSize = style.chipFontSize;
        s.focusRingColor = style.focusColor;
        s.contentPadding = style.contentPadding;
        panel.box->SetVisualStyle(s);
    }

    void UltraCanvasToolPanelStack::ApplyRailButtonLook(Panel& panel, bool selected) {
        if (!panel.railButton) return;
        const ButtonVariant variant = selected ? ButtonVariant::Selected : ButtonVariant::Ghost;
        ButtonStyle base = panel.railButton->GetStyle();
        base.cornerRadius = 5.0f;
        base.borderWidth = 0.0f;
        base.useIconAsMask = true;
        panel.railButton->SetThemeVariant(variant);
        panel.railButton->SetStyle(ButtonStyle::FromTheme(UltraCanvasTheme::Current(), base, variant));
    }

    // ===== PANELS =====

    UltraCanvasToolPanelStack::Panel* UltraCanvasToolPanelStack::Find(const std::string& id) {
        for (auto& p : panels) if (p.id == id) return &p;
        return nullptr;
    }

    const UltraCanvasToolPanelStack::Panel* UltraCanvasToolPanelStack::Find(const std::string& id) const {
        for (const auto& p : panels) if (p.id == id) return &p;
        return nullptr;
    }

    int UltraCanvasToolPanelStack::IndexOf(const std::string& id) const {
        for (size_t i = 0; i < panels.size(); ++i) if (panels[i].id == id) return static_cast<int>(i);
        return -1;
    }

    UltraCanvasToolPanelStack::Panel* UltraCanvasToolPanelStack::PanelOfBox(const UltraCanvasUIElement* element) {
        if (!element) return nullptr;
        for (auto& p : panels) {
            if (p.box.get() == element || p.railButton.get() == element) return &p;
        }
        return nullptr;
    }

    std::shared_ptr<UltraCanvasGroupBox> UltraCanvasToolPanelStack::AddPanel(const std::string& id, const std::string& title,
                                                                            std::shared_ptr<UltraCanvasContainer> content,
                                                                            const ToolPanelOptions& options) {
        if (id.empty() || Find(id)) return nullptr;
        Panel panel;
        panel.id = id;
        panel.title = title;
        panel.options = options;
        panel.addIndex = nextAddIndex++;
        panel.shown = !options.context;
        panel.open = options.context ? true : options.open;
        panel.content = content ? std::move(content)
                                : std::make_shared<UltraCanvasContainer>(GetIdentifier() + "." + id + ".content", 0, 0, 0, 0);

        auto box = std::make_shared<UltraCanvasGroupBox>(GetIdentifier() + "." + id, title);
        box->SetFrameStyle(GroupBoxFrameStyle::Header);
        box->SetCollapsible(true);
        box->SetHeaderFocusable(true);
        box->SetHeaderDraggable(true);
        box->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        box->layoutItem.SetFlexShrink(0);
        if (!options.helpText.empty()) box->SetHelpText(options.helpText);
        if (options.dialogAction) {
            box->SetMoreButton(true);
            box->SetMoreButtonTooltip(options.dialogTooltip);
            box->onMoreClicked = options.dialogAction;
        }
        if (!options.chip.empty()) box->SetHeaderChip(options.chip);
        panel.content->layoutItem.SetFlexShrink(0);
        box->AddChild(panel.content);

        // The stack is above the box: its callbacks hold it raw.
        box->onCollapsedChanged = [this, id](bool collapsed) { OnBoxCollapsedChanged(id, collapsed); };
        box->onHeaderDragStarted = [this, id](const Point2Di&) { BeginDrag(id); };
        box->onHeaderDragMoved = [this](const Point2Di& at) { UpdateDrag(at); };
        box->onHeaderDragEnded = [this](const Point2Di& at, bool cancelled) { EndDrag(at, cancelled); };
        box->onHeaderContextMenu = [this, id](const Point2Di& at) { ShowHeaderMenu(id, at); };
        panel.box = box;

        auto button = std::make_shared<UltraCanvasButton>(GetIdentifier() + ".rail." + id, 0, 0,
                                                          style.railButtonSize, style.railButtonSize,
                                                          options.iconPath.empty() ? FirstCharacter(title) : std::string());
        if (!options.iconPath.empty()) {
            button->SetIcon(options.iconPath);
            const int icon = static_cast<int>(style.railIconSize);
            button->SetIconSize(icon, icon);
        }
        button->SetTooltip(title);
        button->SetAccessibleName(title);
        button->layoutItem.SetFlexShrink(0);
        button->onClick = [this, id]() {
            if (IsFlyoutOpen() && flyoutPanel == id) CloseFlyout();
            else OpenFlyout(id);
        };
        panel.railButton = button;

        // A context panel goes below the other context panels, at the top;
        // any other at the bottom.
        size_t at = panels.size();
        if (options.context) {
            at = 0;
            while (at < panels.size() && panels[at].options.context) ++at;
        }
        panels.insert(panels.begin() + static_cast<long>(at), std::move(panel));
        Panel& added = panels[at];
        column->AddChild(added.box);
        rail->AddChild(added.railButton);
        ApplyPanelLook(added);
        ApplyRailButtonLook(added, false);
        ApplyOpen(added);
        added.box->SetVisible(added.shown);
        added.railButton->SetVisible(added.shown);
        ApplyOrderToChildren();
        InvalidateLayout();
        RequestRedraw();
        return added.box;
    }

    bool UltraCanvasToolPanelStack::RemovePanel(const std::string& id) {
        const int index = IndexOf(id);
        if (index < 0) return false;
        if (flyoutPanel == id) CloseFlyout();
        if (dragPanel == id) {
            dragPanel.clear();
            dropIndex = -1;
        }
        Panel& panel = panels[static_cast<size_t>(index)];
        panel.box->onCollapsedChanged = nullptr;
        panel.box->onHeaderDragStarted = nullptr;
        panel.box->onHeaderDragMoved = nullptr;
        panel.box->onHeaderDragEnded = nullptr;
        panel.box->onHeaderContextMenu = nullptr;
        panel.box->CancelHeaderDrag();
        if (auto* parent = panel.box->GetParentContainer()) parent->RemoveChild(panel.box);
        rail->RemoveChild(panel.railButton);
        panels.erase(panels.begin() + index);
        if (anchorPanel == id) anchorPanel.clear();
        if (revealPanel == id) revealPanel.clear();
        InvalidateLayout();
        RequestRedraw();
        return true;
    }

    bool UltraCanvasToolPanelStack::HasPanel(const std::string& id) const { return Find(id) != nullptr; }

    std::shared_ptr<UltraCanvasGroupBox> UltraCanvasToolPanelStack::GetPanel(const std::string& id) const {
        const Panel* p = Find(id);
        return p ? p->box : nullptr;
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasToolPanelStack::GetPanelContent(const std::string& id) const {
        const Panel* p = Find(id);
        return p ? p->content : nullptr;
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolPanelStack::GetRailButton(const std::string& id) const {
        const Panel* p = Find(id);
        return p ? p->railButton : nullptr;
    }

    void UltraCanvasToolPanelStack::SetPanelTitle(const std::string& id, const std::string& title) {
        Panel* p = Find(id);
        if (!p) return;
        p->title = title;
        p->box->SetTitle(title);
        p->railButton->SetTooltip(title);
        p->railButton->SetAccessibleName(title);
        if (p->options.iconPath.empty()) p->railButton->SetText(FirstCharacter(title));
    }

    void UltraCanvasToolPanelStack::SetPanelChip(const std::string& id, const std::string& chip) {
        Panel* p = Find(id);
        if (!p) return;
        p->options.chip = chip;
        p->box->SetHeaderChip(chip);
    }

    // ===== OPEN / CLOSED =====

    void UltraCanvasToolPanelStack::ApplyOpen(Panel& panel) {
        const bool inFlyout = flyoutPanel == panel.id && IsFlyoutOpen();
        const bool open = panel.open || inFlyout;
        const bool was = suppressOpenEvents;
        suppressOpenEvents = true;
        panel.box->SetCollapsed(!open);
        suppressOpenEvents = was;
        // A closed panel's controls are out of the Tab order too.
        panel.content->SetVisible(open);
    }

    void UltraCanvasToolPanelStack::OnBoxCollapsedChanged(const std::string& id, bool collapsed) {
        if (suppressOpenEvents) return;
        Panel* p = Find(id);
        if (!p) return;
        p->open = !collapsed;
        p->content->SetVisible(p->open);
        InvalidateLayout();
        RequestRedraw();
        if (onPanelOpenChanged) onPanelOpenChanged(id, p->open);
    }

    void UltraCanvasToolPanelStack::SetPanelOpen(const std::string& id, bool open) {
        Panel* p = Find(id);
        if (!p || p->open == open) return;
        p->open = open;
        ApplyOpen(*p);
        InvalidateLayout();
        RequestRedraw();
    }

    bool UltraCanvasToolPanelStack::IsPanelOpen(const std::string& id) const {
        const Panel* p = Find(id);
        return p && p->open;
    }

    // ===== CONTEXT PANELS =====

    void UltraCanvasToolPanelStack::SetPanelShown(const std::string& id, bool shown) {
        Panel* p = Find(id);
        if (!p || !p->options.context || p->shown == shown) return;
        RememberScrollAnchor(id);
        // Its focus goes to the next header before it goes.
        if (!shown && GetFocusedPanel() == id) {
            const int index = IndexOf(id);
            int next = NeighbourIndex(index, 1);
            if (next < 0) {
                for (size_t i = static_cast<size_t>(index) + 1; i < panels.size() && next < 0; ++i) {
                    if (panels[i].shown) next = static_cast<int>(i);
                }
            }
            if (next >= 0) FocusPanel(panels[static_cast<size_t>(next)].id);
        }
        p = Find(id);
        p->shown = shown;
        if (shown) {
            p->open = true;
            ApplyOpen(*p);
        } else if (flyoutPanel == id) {
            CloseFlyout();
        }
        p->box->SetVisible(shown);
        p->railButton->SetVisible(shown);
        InvalidateLayout();
        RequestRedraw();
    }

    bool UltraCanvasToolPanelStack::IsPanelShown(const std::string& id) const {
        const Panel* p = Find(id);
        return p && p->shown;
    }

    bool UltraCanvasToolPanelStack::IsContextPanel(const std::string& id) const {
        const Panel* p = Find(id);
        return p && p->options.context;
    }

    void UltraCanvasToolPanelStack::RememberScrollAnchor(const std::string& changing) {
        if (railActive || !column) return;
        // A change made before the last one was laid out keeps that one's
        // anchor: the panels' positions are still the ones on the screen.
        if (!anchorPanel.empty()) return;
        const float scroll = static_cast<float>(column->GetVerticalScrollPosition());

        const Panel* anchor = nullptr;
        float anchorTop = 0.0f;
        // The panel under the pointer.
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            UltraCanvasUIElement* hovered = app->GetHoveredElement();
            for (size_t i = 0; hovered && i < panels.size(); ++i) {
                const Panel& p = panels[i];
                if (!p.shown || p.box->GetParentContainer() != column.get()) continue;
                if (hovered != p.box.get() && !hovered->IsDescendantOf(p.box.get())) continue;
                anchorTop = p.box->GetY();
                anchor = &p;
                if (p.id == changing) {
                    // The panel under the pointer is the one going: the
                    // next one takes its place.
                    anchor = nullptr;
                    for (size_t j = i + 1; j < panels.size() && !anchor; ++j) {
                        if (panels[j].shown && panels[j].box->GetParentContainer() == column.get()) anchor = &panels[j];
                    }
                }
                break;
            }
        }
        // Scrolled: the first panel in view. At the top, nothing is
        // anchored, so a panel coming in at the top is seen.
        if (!anchor && scroll > 0.0f) {
            for (const auto& p : panels) {
                if (!p.shown || p.id == changing || p.box->GetParentContainer() != column.get()) continue;
                if (p.box->GetY() + p.box->GetHeight() > scroll) {
                    anchor = &p;
                    anchorTop = p.box->GetY();
                    break;
                }
            }
        }
        if (!anchor) return;
        anchorPanel = anchor->id;
        anchorOffset = anchorTop - scroll;
    }

    void UltraCanvasToolPanelStack::ApplyScrollAnchor() {
        if (!anchorPanel.empty()) {
            const std::string id = anchorPanel;
            anchorPanel.clear();
            const Panel* p = Find(id);
            if (p && p->shown && p->box->GetParentContainer() == column.get()) {
                const int target = static_cast<int>(std::lround(p->box->GetY() - anchorOffset));
                column->ScrollToVertical(std::max(0, target));
            }
        }
        if (!revealPanel.empty()) {
            const std::string id = revealPanel;
            revealPanel.clear();
            const Panel* p = Find(id);
            if (p && p->shown && p->box->GetParentContainer() == column.get()) {
                const float scroll = static_cast<float>(column->GetVerticalScrollPosition());
                const float top = p->box->GetY() - style.padding;
                const float bottom = p->box->GetY() + p->box->GetTitleBarRect().height + style.padding;
                const float view = column->GetHeight();
                if (top < scroll) column->ScrollToVertical(std::max(0, static_cast<int>(std::floor(top))));
                else if (bottom > scroll + view) column->ScrollToVertical(static_cast<int>(std::ceil(bottom - view)));
            }
        }
    }

    void UltraCanvasToolPanelStack::RevealHeader(const Panel& panel) {
        revealPanel = panel.id;
        column->InvalidateLayout();
        RequestRedraw();
    }

    // ===== ORDER =====

    std::vector<std::string> UltraCanvasToolPanelStack::GetOrder() const {
        std::vector<std::string> order;
        order.reserve(panels.size());
        for (const auto& p : panels) order.push_back(p.id);
        return order;
    }

    void UltraCanvasToolPanelStack::SetOrder(const std::vector<std::string>& order) {
        std::vector<Panel> named;
        std::vector<bool> used(panels.size(), false);
        for (const auto& id : order) {
            const int index = IndexOf(id);
            if (index < 0 || used[static_cast<size_t>(index)]) continue;
            used[static_cast<size_t>(index)] = true;
            named.push_back(panels[static_cast<size_t>(index)]);
        }
        // A panel the order does not name: after the panel added before it.
        std::vector<Panel> unnamed;
        for (size_t i = 0; i < panels.size(); ++i) if (!used[i]) unnamed.push_back(panels[i]);
        std::sort(unnamed.begin(), unnamed.end(), [](const Panel& a, const Panel& b) { return a.addIndex < b.addIndex; });
        for (auto& p : unnamed) {
            size_t at = 0;
            int best = -1;
            for (size_t i = 0; i < named.size(); ++i) {
                if (named[i].addIndex < p.addIndex && named[i].addIndex > best) {
                    best = named[i].addIndex;
                    at = i + 1;
                }
            }
            named.insert(named.begin() + static_cast<long>(at), p);
        }
        std::stable_partition(named.begin(), named.end(), [](const Panel& p) { return p.options.context; });
        bool changed = named.size() != panels.size();
        for (size_t i = 0; !changed && i < named.size(); ++i) changed = named[i].id != panels[i].id;
        if (!changed) return;
        panels = std::move(named);
        ApplyOrderToChildren();
    }

    void UltraCanvasToolPanelStack::ApplyOrderToChildren() {
        // Adding a child it already has moves it to the end, keeping its
        // focus: in order, every one, and the children follow the panels.
        for (auto& p : panels) {
            if (p.box->GetParentContainer() == column.get()) column->AddChild(p.box);
            rail->AddChild(p.railButton);
        }
        column->InvalidateLayout();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasToolPanelStack::NotifyOrderChanged() {
        if (onOrderChanged) onOrderChanged(GetOrder());
    }

    int UltraCanvasToolPanelStack::NeighbourIndex(int index, int delta) const {
        if (index < 0 || index >= static_cast<int>(panels.size()) || delta == 0) return -1;
        const bool context = panels[static_cast<size_t>(index)].options.context;
        for (int i = index + (delta < 0 ? -1 : 1); i >= 0 && i < static_cast<int>(panels.size()); i += (delta < 0 ? -1 : 1)) {
            const Panel& p = panels[static_cast<size_t>(i)];
            if (p.options.context != context) break;
            if (p.shown) return i;
        }
        return -1;
    }

    bool UltraCanvasToolPanelStack::CanMove(const std::string& id, int delta) const {
        return NeighbourIndex(IndexOf(id), delta) >= 0;
    }

    bool UltraCanvasToolPanelStack::MovePanel(const std::string& id, int delta) {
        const int index = IndexOf(id);
        const int neighbour = NeighbourIndex(index, delta);
        if (neighbour < 0) return false;
        Panel moving = panels[static_cast<size_t>(index)];
        panels.erase(panels.begin() + index);
        // Above the neighbour going up; below it going down, where erasing
        // the panel moved the neighbour one place up - both at its index.
        panels.insert(panels.begin() + neighbour, std::move(moving));
        ApplyOrderToChildren();
        if (const Panel* p = Find(id)) RevealHeader(*p);
        return true;
    }

    // ===== SAVED STATE =====

    ToolPanelStackState UltraCanvasToolPanelStack::SaveState() const {
        ToolPanelStackState state;
        state.order = GetOrder();
        for (const auto& p : panels) {
            if (!p.options.context && p.open) state.open.push_back(p.id);
        }
        state.railMode = railChosen;
        return state;
    }

    void UltraCanvasToolPanelStack::RestoreState(const ToolPanelStackState& state) {
        if (state.order.empty() && state.open.empty() && !state.railMode) return;
        if (!state.order.empty()) SetOrder(state.order);
        for (auto& p : panels) {
            if (p.options.context) continue;
            const bool named = ContainsId(state.order, p.id) || ContainsId(state.open, p.id);
            if (named) SetPanelOpen(p.id, ContainsId(state.open, p.id));
        }
        SetRailMode(state.railMode);
    }

    // ===== VALUES =====

    void UltraCanvasToolPanelStack::Sync() {
        for (auto& p : panels) SyncToolRows(p.content.get());
    }

    // ===== RAIL MODE =====

    void UltraCanvasToolPanelStack::SetRailMode(bool value) {
        railChosen = value;
        ApplyMode();
    }

    void UltraCanvasToolPanelStack::SetAutoRailBelowWidth(float windowWidth) {
        autoRailBelowWidth = std::max(0.0f, windowWidth);
        ApplyMode();
    }

    void UltraCanvasToolPanelStack::ApplyMode() {
        bool want = railChosen;
        if (!want && autoRailBelowWidth > 0.0f) {
            if (auto* win = GetWindow()) want = win->GetWidth() > 0 && win->GetWidth() < autoRailBelowWidth;
        }
        SetRailActive(want);
    }

    void UltraCanvasToolPanelStack::SetRailActive(bool value) {
        if (railActive == value) return;
        // Where the keyboard was: the same panel in the other form.
        std::string focused = GetFocusedPanel();
        if (!value) CloseFlyout();
        railActive = value;
        column->SetVisible(!railActive);
        rail->SetVisible(railActive);
        size.width = CSSLayout::Dimension::Px(railActive ? style.railWidth : style.columnWidth);
        InvalidateLayout();
        RequestRedraw();
        if (!focused.empty()) FocusPanel(focused);
        if (onRailModeChanged) onRailModeChanged(railActive);
    }

    void UltraCanvasToolPanelStack::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // A window made narrower than SetAutoRailBelowWidth folds the
        // column (and a wider one unfolds it); the new width is laid out on
        // the next frame. Not in the layout pass: a layout asked for during
        // one is taken as done when it ends.
        if (autoRailBelowWidth > 0.0f && !railChosen) {
            if (auto* win = GetWindow()) {
                const bool want = win->GetWidth() > 0 && win->GetWidth() < autoRailBelowWidth;
                if (want != railActive) SetRailActive(want);
            }
        }
        UltraCanvasContainer::Render(ctx, dirtyRect);
    }

    bool UltraCanvasToolPanelStack::IsFlyoutOpen() const {
        return flyout && flyout->IsPopupElement() && !flyoutPanel.empty();
    }

    Point2Di UltraCanvasToolPanelStack::FlyoutPosition(const Panel& panel, const Size2Df& flyoutSize) const {
        const Point2Df stackAt = GetPositionInWindow();
        const Point2Df buttonAt = panel.railButton->GetPositionInWindow();
        float x = stackAt.x + GetWidth() + 2.0f;
        float y = buttonAt.y - style.padding;
        if (auto* win = GetWindow()) {
            const float bottom = static_cast<float>(win->GetHeight()) - 8.0f;
            if (y + flyoutSize.height > bottom) y = bottom - flyoutSize.height;
        }
        y = std::max(y, 8.0f);
        return Point2Di(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    }

    bool UltraCanvasToolPanelStack::OpenFlyout(const std::string& id) {
        if (!railActive) return false;
        Panel* p = Find(id);
        auto* win = GetWindow();
        if (!p || !p->shown || !win) return false;
        if (IsFlyoutOpen()) {
            if (flyoutPanel == id) return true;
            CloseFlyout();
            p = Find(id);
            if (!p) return false;
        }
        flyoutPanel = id;
        // The panel itself moves into the flyout, open, and back into the
        // column when the flyout closes.
        flyout->AddChild(p->box);
        p->box->SetHeaderDraggable(false);
        // Open in the flyout, whatever it is in the column: not the user's
        // opening, and nothing to save.
        suppressOpenEvents = true;
        p->box->SetCollapsible(false);
        suppressOpenEvents = false;
        p->content->SetVisible(true);

        const float border = 1.0f;
        const float width = style.flyoutWidth;
        CSSLayout::LayoutContext lctx;
        lctx.viewportWidth = static_cast<float>(win->GetWidth());
        lctx.viewportHeight = static_cast<float>(win->GetHeight());
        const float inner = width - 2.0f * (style.padding + border);
        p->box->Measure(CSSLayout::MeasureConstraints{{CSSLayout::ConstraintMode::Exact, inner},
                                                      {CSSLayout::ConstraintMode::Unbounded, INFINITY}}, lctx);
        float height = std::ceil(p->box->measured.measuredHeight) + 2.0f * (style.padding + border);
        height = std::min(height, std::max(60.0f, static_cast<float>(win->GetHeight()) - 16.0f));
        const Size2Df flyoutSize(width, height);
        flyout->SetElementSize(flyoutSize);

        PopupElementSettings settings;
        settings.closeByEscapeKey = true;
        settings.closeByClickOutside = true;
        // A click on the rail is the rail's: its icons switch the flyout.
        settings.popupOwner = rail;
        win->OpenPopup(FlyoutPosition(*p, flyoutSize), *flyout, settings);
        ApplyRailButtonLook(*p, true);
        RequestRedraw();
        return true;
    }

    void UltraCanvasToolPanelStack::CloseFlyout() {
        if (!flyout || !flyout->IsPopupElement()) return;
        if (auto* win = flyout->GetWindow()) win->ClosePopup(*flyout);
    }

    void UltraCanvasToolPanelStack::FlyoutClosed() {
        const std::string id = flyoutPanel;
        flyoutPanel.clear();
        Panel* p = Find(id);
        if (!p) return;
        bool hadFocus = false;
        if (auto* win = GetWindow()) {
            UltraCanvasUIElement* focused = win->GetFocusedElement();
            hadFocus = focused && (focused == p->box.get() || focused->IsDescendantOf(p->box.get()));
        }
        column->AddChild(p->box);
        suppressOpenEvents = true;
        p->box->SetCollapsible(true);
        suppressOpenEvents = false;
        p->box->SetHeaderDraggable(true);
        ApplyOpen(*p);
        ApplyOrderToChildren();
        ApplyRailButtonLook(*p, false);
        if (hadFocus && railActive) p->railButton->SetFocus(true);
        RequestRedraw();
    }

    // ===== KEYBOARD =====

    std::string UltraCanvasToolPanelStack::GetFocusedPanel() const {
        auto* win = GetWindow();
        if (!win) return std::string();
        UltraCanvasUIElement* focused = win->GetFocusedElement();
        if (!focused) return std::string();
        for (const auto& p : panels) {
            if (p.box.get() == focused || p.railButton.get() == focused) return p.id;
        }
        return std::string();
    }

    bool UltraCanvasToolPanelStack::FocusPanel(const std::string& id) {
        Panel* p = Find(id);
        if (!p || !p->shown) return false;
        if (railActive && !(IsFlyoutOpen() && flyoutPanel == id)) {
            return p->railButton->SetFocus(true);
        }
        const bool focused = p->box->SetFocus(true);
        if (focused && !railActive) RevealHeader(*p);
        return focused;
    }

    bool UltraCanvasToolPanelStack::FocusFirst() {
        for (const auto& p : panels) {
            if (p.shown) return FocusPanel(p.id);
        }
        return false;
    }

    bool UltraCanvasToolPanelStack::HandleNavigationKey(const UCEvent& event) {
        if (event.type != UCEventType::KeyDown || event.alt || event.meta) return false;
        auto* win = GetWindow();
        if (!win) return false;
        Panel* p = PanelOfBox(win->GetFocusedElement());
        if (!p) return false;
        const std::string id = p->id;
        const int index = IndexOf(id);
        const bool up = event.virtualKey == UCKeys::Up;
        const bool down = event.virtualKey == UCKeys::Down;

        if ((up || down) && event.ctrl && !event.shift) {
            // Ctrl+Up / Ctrl+Down move the panel; the focus stays on it.
            if (MovePanel(id, up ? -1 : 1)) NotifyOrderChanged();
            FocusPanel(id);
            return true;
        }
        if ((up || down) && !event.ctrl && !event.shift) {
            // To the next header of any kind that is shown.
            const int step = up ? -1 : 1;
            for (int i = index + step; i >= 0 && i < static_cast<int>(panels.size()); i += step) {
                if (panels[static_cast<size_t>(i)].shown) {
                    FocusPanel(panels[static_cast<size_t>(i)].id);
                    break;
                }
            }
            return true;
        }
        if ((event.virtualKey == UCKeys::Home || event.virtualKey == UCKeys::End) && !event.ctrl && !event.shift) {
            if (event.virtualKey == UCKeys::Home) {
                FocusFirst();
            } else {
                for (auto it = panels.rbegin(); it != panels.rend(); ++it) {
                    if (it->shown) {
                        FocusPanel(it->id);
                        break;
                    }
                }
            }
            return true;
        }
        return false;
    }

    bool UltraCanvasToolPanelStack::OnEvent(const UCEvent& event) {
        if (event.IsKeyboardEvent() && HandleNavigationKey(event)) return true;
        return UltraCanvasContainer::OnEvent(event);
    }

    // ===== THE HEADER'S MENU =====

    void UltraCanvasToolPanelStack::ShowHeaderMenu(const std::string& id, const Point2Di& windowPos) {
        auto* win = GetWindow();
        Panel* p = Find(id);
        if (!win || !p) return;
        if (!headerMenu) {
            headerMenu = CreateMenu(GetIdentifier() + ".menu", 0, 0, 0, 0);
            headerMenu->SetMenuType(MenuType::PopupMenu);
        } else {
            headerMenu->CloseMenu();
        }
        headerMenu->Clear();
        // The menu belongs to the stack: its entries hold the stack raw.
        MenuItemData openItem = MenuItemData::Action(p->open ? Tr("Close") : Tr("Open"), [this, id]() {
            if (Panel* panel = Find(id)) {
                const bool open = !panel->open;
                SetPanelOpen(id, open);
                if (onPanelOpenChanged) onPanelOpenChanged(id, open);
            }
        });
        MenuItemData moveUp = MenuItemData::ActionWithShortcut(Tr("Move up"), "Ctrl+Up", [this, id]() {
            if (MovePanel(id, -1)) NotifyOrderChanged();
        });
        moveUp.enabled = CanMove(id, -1);
        MenuItemData moveDown = MenuItemData::ActionWithShortcut(Tr("Move down"), "Ctrl+Down", [this, id]() {
            if (MovePanel(id, 1)) NotifyOrderChanged();
        });
        moveDown.enabled = CanMove(id, 1);
        headerMenu->AddItem(openItem);
        headerMenu->AddItem(MenuItemData::Separator());
        headerMenu->AddItem(moveUp);
        headerMenu->AddItem(moveDown);
        headerMenu->OpenMenu(windowPos, *win, PopupElementSettings());
    }

    // ===== DRAGGING A HEADER =====

    void UltraCanvasToolPanelStack::BeginDrag(const std::string& id) {
        const Panel* p = Find(id);
        if (!p || railActive || p->box->GetParentContainer() != column.get()) return;
        dragPanel = id;
        dropBefore.clear();
        dropIndex = IndexOf(id);
        column->RequestRedraw();
    }

    int UltraCanvasToolPanelStack::DropTargetAt(const Point2Di& windowPos, std::string& before) const {
        before.clear();
        const int dragged = IndexOf(dragPanel);
        if (dragged < 0) return -1;
        const bool context = panels[static_cast<size_t>(dragged)].options.context;
        const float y = static_cast<float>(windowPos.y) - column->GetPositionInWindow().y +
                        static_cast<float>(column->GetVerticalScrollPosition());

        // Above the first shown panel of its kind whose middle is below the
        // pointer; past them all, the end of its kind.
        int target = -1;
        for (size_t i = 0; i < panels.size(); ++i) {
            const Panel& q = panels[i];
            if (q.options.context != context || !q.shown || q.box->GetParentContainer() != column.get()) continue;
            if (q.box->GetY() + q.box->GetHeight() * 0.5f > y) {
                target = static_cast<int>(i);
                break;
            }
        }
        if (target >= 0) before = panels[static_cast<size_t>(target)].id;

        // The index the panel then has.
        std::vector<std::string> order = GetOrder();
        order.erase(order.begin() + dragged);
        int at;
        if (target >= 0) {
            at = static_cast<int>(std::find(order.begin(), order.end(), before) - order.begin());
        } else {
            at = 0;
            for (size_t i = 0; i < order.size(); ++i) {
                if (IsContextPanel(order[i]) == context) at = static_cast<int>(i) + 1;
            }
            if (!context && at == 0) {
                // No other plain panel: after the context ones.
                for (size_t i = 0; i < order.size(); ++i) if (IsContextPanel(order[i])) at = static_cast<int>(i) + 1;
            }
        }
        return at;
    }

    void UltraCanvasToolPanelStack::UpdateDrag(const Point2Di& windowPos) {
        if (dragPanel.empty()) return;
        // Near the column's top or bottom edge the column scrolls.
        const float local = static_cast<float>(windowPos.y) - column->GetPositionInWindow().y;
        if (local < kDragScrollEdge) column->ScrollByVertical(-kDragScrollStep);
        else if (local > column->GetHeight() - kDragScrollEdge) column->ScrollByVertical(kDragScrollStep);
        std::string before;
        const int at = DropTargetAt(windowPos, before);
        if (at != dropIndex || before != dropBefore) {
            dropIndex = at;
            dropBefore = before;
        }
        column->RequestRedraw();
    }

    void UltraCanvasToolPanelStack::EndDrag(const Point2Di& windowPos, bool cancelled) {
        if (dragPanel.empty()) return;
        const std::string id = dragPanel;
        std::string before;
        const int at = cancelled ? -1 : DropTargetAt(windowPos, before);
        dragPanel.clear();
        dropBefore.clear();
        dropIndex = -1;
        const int from = IndexOf(id);
        if (at >= 0 && from >= 0 && at != from) {
            Panel moving = panels[static_cast<size_t>(from)];
            panels.erase(panels.begin() + from);
            panels.insert(panels.begin() + std::min<int>(at, static_cast<int>(panels.size())), std::move(moving));
            ApplyOrderToChildren();
            NotifyOrderChanged();
        }
        column->RequestRedraw();
        RequestRedraw();
    }

    float UltraCanvasToolPanelStack::DropIndicatorY() const {
        const int dragged = IndexOf(dragPanel);
        if (dragged < 0) return -1.0f;
        if (!dropBefore.empty()) {
            if (const Panel* p = Find(dropBefore)) return p->box->GetY() - style.panelGap * 0.5f;
        }
        // The end of its kind: below the last shown panel of its kind.
        const bool context = panels[static_cast<size_t>(dragged)].options.context;
        float y = -1.0f;
        for (const auto& q : panels) {
            if (q.options.context != context || !q.shown || q.box->GetParentContainer() != column.get()) continue;
            y = q.box->GetY() + q.box->GetHeight() + style.panelGap * 0.5f;
        }
        return y;
    }

    void UltraCanvasToolPanelStack::RenderDragFeedback(IRenderContext* ctx) {
        const Panel* dragged = Find(dragPanel);
        if (!dragged) return;
        const float scroll = static_cast<float>(column->GetVerticalScrollPosition());
        // The panel being dragged, veiled: it is leaving its place.
        const Rect2Df b = dragged->box->GetBounds();
        ctx->SetFillPaint(style.draggedPanelVeil);
        ctx->FillRoundedRectangle(Rect2Dd(b.x, b.y - scroll, b.width, b.height), style.cornerRadius);
        // The line where it goes, with a dot at each end.
        const float y = DropIndicatorY();
        if (y < 0.0f) return;
        const float lineY = y - scroll;
        const float x0 = b.x + 2.0f;
        const float x1 = b.x + b.width - 2.0f;
        ctx->SetStrokePaint(style.dropIndicatorColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLine(Point2Dd(x0 + 3.0, lineY), Point2Dd(x1 - 3.0, lineY));
        ctx->SetFillPaint(style.dropIndicatorColor);
        ctx->FillCircle(Point2Dd(x0 + 1.5, lineY), 3.0);
        ctx->FillCircle(Point2Dd(x1 - 1.5, lineY), 3.0);
    }

} // namespace UltraCanvas
