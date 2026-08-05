// Apps/UltraFiler/UltraFilerSettingsDialog.cpp
// UltraFiler settings window: settings-page tree on the left, the selected
// page on the right. Currently one main page (Media Viewer) with one sub page
// (Transparent Images): the backdrop behind transparent images — checkered
// pattern or a preset colour chosen with the colour picker. Changes apply
// live and are saved immediately.
// Version: 1.0.0
// Last Modified: 2026-08-05
// Author: UltraCanvas Framework

#include "UltraFilerSettingsDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <map>
#include <memory>

namespace UltraCanvas {

namespace {

    constexpr float kFontSize = 9.0f;   // matches the main window's UI font

    // Page ids double as tree node ids.
    constexpr const char* kPageMediaViewer = "media-viewer";
    constexpr const char* kPageTransparentImages = "media-viewer/transparent-images";

    // The one open settings window (or the last closed one, until reopened).
    struct DialogState {
        std::shared_ptr<UltraCanvasWindow>    window;
        bool                                  closed = false;
        std::shared_ptr<UltraCanvasTreeView>  tree;
        std::shared_ptr<UltraCanvasContainer> pageArea;
        std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;

        std::shared_ptr<UltraCanvasRadio>       solidRadio;
        std::shared_ptr<UltraCanvasRadio>       checkeredRadio;
        UltraCanvasRadioGroup                   backgroundGroup;
        std::shared_ptr<UltraCanvasColorPicker> colorPicker;

        UltraFilerSettings*   settings = nullptr;
        std::function<void()> onChanged;
    };

    std::shared_ptr<DialogState> g_dialog;

    void ApplyAndSave(DialogState* d) {
        if (d->onChanged) d->onChanged();
        if (d->settings) d->settings->Save();
    }

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id,
                                                const std::string& text,
                                                float fontSize = kFontSize) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
        l->SetText(text);
        l->SetFontSize(fontSize);
        l->SetTextColor(Color(40, 40, 44, 255));
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        l->size.width  = CSSLayout::Dimension::Auto();
        l->size.height = CSSLayout::Dimension::Auto();
        return l;
    }

    // A colour picker restyled for the dialog's light surface.
    ColorPickerStyle LightPickerStyle() {
        ColorPickerStyle s;
        s.backgroundColor = Color(249, 249, 251, 255);
        s.panelColor      = Color(240, 240, 244, 255);
        s.borderColor     = Color(210, 210, 216, 255);
        s.textColor       = Color(40, 40, 44, 255);
        s.mutedTextColor  = Color(120, 120, 126, 255);
        s.fieldColor      = Color(255, 255, 255, 255);
        s.fieldBorderColor = Color(190, 190, 196, 255);
        s.accentColor     = Color(60, 140, 220, 255);
        s.markerOutline   = Color(90, 90, 96, 255);
        return s;
    }

    // ===== MEDIA VIEWER > TRANSPARENT IMAGES =====
    std::shared_ptr<UltraCanvasContainer> BuildTransparentImagesPage(DialogState* d) {
        auto page = std::make_shared<UltraCanvasContainer>("ufl-set-page-transparent");
        page->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        page->SetPadding(16, 18, 16, 18);

        page->AddChild(MakeLabel("ufl-set-ti-title", "Transparent images", 12.0f));
        page->AddChild(MakeLabel("ufl-set-ti-caption",
                "Background shown behind images with transparency in the media viewer:"));

        const bool checkered = d->settings->previewCheckeredBackground;

        d->solidRadio = UltraCanvasRadio::Create(
                "ufl-set-ti-solid", -1, -1, "Preset colour (picked below)", !checkered);
        d->checkeredRadio = UltraCanvasRadio::Create(
                "ufl-set-ti-checkered", -1, -1, "Checkered pattern (shows transparency)",
                checkered);
        d->backgroundGroup.AddRadioButton(d->solidRadio);
        d->backgroundGroup.AddRadioButton(d->checkeredRadio);
        d->backgroundGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->previewCheckeredBackground = (selected == d->checkeredRadio);
            ApplyAndSave(d);
        };
        page->AddChild(d->solidRadio);
        page->AddChild(d->checkeredRadio);

        page->AddChild(MakeLabel("ufl-set-ti-color-lbl", "Preset colour:"));

        d->colorPicker = CreateColorPicker("ufl-set-ti-picker",
                d->settings->previewTransparentColor, 0, 0, 280, 380);
        d->colorPicker->SetStyle(LightPickerStyle());
        d->colorPicker->SetUIScale(0.85f);
        d->colorPicker->SetShowAlpha(false);   // the backdrop is always opaque
        d->colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Live preview while dragging; the final value also selects the
        // solid-colour mode (picking a colour means the user wants it shown)
        // and persists.
        d->colorPicker->onColorChanging = [d](const Color& c) {
            if (!d->settings) return;
            d->settings->previewTransparentColor = Color(c.r, c.g, c.b, 255);
            if (d->onChanged) d->onChanged();
        };
        d->colorPicker->onColorChanged = [d](const Color& c) {
            if (!d->settings) return;
            d->settings->previewTransparentColor = Color(c.r, c.g, c.b, 255);
            if (!d->settings->previewCheckeredBackground) {
                ApplyAndSave(d);
            } else {
                d->backgroundGroup.SelectButton(d->solidRadio);  // applies + saves
            }
        };
        page->AddChild(d->colorPicker);

        return page;
    }

    // Show the page of `nodeId`; a main page without its own panel falls
    // through to its first sub page.
    void ShowPage(DialogState* d, const std::string& nodeId) {
        std::string target = nodeId;
        if (!d->pages.count(target)) {
            // Fall back to the first page below this node in tree order.
            TreeNode* node = d->tree ? d->tree->FindNode(nodeId) : nullptr;
            std::function<TreeNode*(TreeNode*)> firstPage =
                    [&](TreeNode* n) -> TreeNode* {
                if (!n) return nullptr;
                if (d->pages.count(n->data.nodeId)) return n;
                for (auto& child : n->children)
                    if (TreeNode* hit = firstPage(child.get())) return hit;
                return nullptr;
            };
            TreeNode* hit = firstPage(node);
            if (!hit) return;
            target = hit->data.nodeId;
        }
        for (auto& [id, pageContainer] : d->pages)
            pageContainer->SetVisible(id == target);
    }

    void BuildDialog(DialogState* d, UltraCanvasWindowBase* parent) {
        WindowConfig wc;
        wc.title = "UltraFiler - Settings";
        wc.width = 620;
        wc.height = 540;
        wc.resizable = true;
        wc.type = WindowType::Dialog;
        wc.parentWindow = parent;
        d->window = CreateWindow(wc);
        if (!d->window || !d->window->IsCreated()) { d->window.reset(); return; }

        d->window->layout.SetFlexColumn()
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->window->SetBackgroundColor(Color(249, 249, 251, 255));

        // ----- settings-page tree | page area -----
        auto content = std::make_shared<UltraCanvasContainer>("ufl-set-content");
        content->layout.SetFlexRow()
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        d->tree = std::make_shared<UltraCanvasTreeView>("ufl-set-tree");
        d->tree->SetFontSize(kFontSize);
        d->tree->SetRowHeight(24);
        d->tree->SetSelectionMode(TreeSelectionMode::Single);
        d->tree->SetLineStyle(TreeLineStyle::NoLine);
        d->tree->SetBackgroundColor(Color(243, 243, 246, 255));
        d->tree->size.width = CSSLayout::Dimension::Px(180);
        d->tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        TreeNodeData rootData;
        rootData.nodeId = "settings";
        rootData.text = "Settings";
        TreeNode* root = d->tree->SetRootNode(rootData);

        TreeNodeData mediaViewer;
        mediaViewer.nodeId = kPageMediaViewer;
        mediaViewer.text = "Media Viewer";
        d->tree->AddNode("settings", mediaViewer);

        TreeNodeData transparent;
        transparent.nodeId = kPageTransparentImages;
        transparent.text = "Transparent Images";
        d->tree->AddNode(kPageMediaViewer, transparent);

        d->tree->ExpandAll();
        content->AddChild(d->tree);

        d->pageArea = std::make_shared<UltraCanvasContainer>("ufl-set-pages");
        d->pageArea->layout.SetFlexColumn()
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pageArea->SetBackgroundColor(Color(255, 255, 255, 255));
        content->AddChild(d->pageArea);

        // ----- pages -----
        auto transparentPage = BuildTransparentImagesPage(d);
        transparentPage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                   .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[kPageTransparentImages] = transparentPage;
        d->pageArea->AddChild(transparentPage);

        d->tree->onNodeSelected = [d](TreeNode* node) {
            if (node) ShowPage(d, node->data.nodeId);
        };
        if (TreeNode* initial = d->tree->FindNode(kPageTransparentImages))
            d->tree->SelectNode(initial);
        ShowPage(d, kPageTransparentImages);

        d->window->AddChild(content);

        // ----- bottom bar -----
        auto bottom = std::make_shared<UltraCanvasContainer>("ufl-set-bottom");
        bottom->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bottom->SetPadding(8, 12, 8, 12);
        bottom->SetBorderTop(1, Color(225, 225, 230, 255));

        auto closeButton = std::make_shared<UltraCanvasButton>(
                "ufl-set-close", 0, 0, 90, 28, "Close");
        closeButton->SetFontSize(kFontSize);
        closeButton->SetCornerRadius(4.0f);
        closeButton->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        closeButton->SetTextColors(Color(40, 40, 44, 255));
        closeButton->SetBorder(1.0f, Color(0, 0, 0, 60));
        closeButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        closeButton->SetOnClick([d]() { if (d->window) d->window->Close(); });
        bottom->AddChild(closeButton);
        d->window->AddChild(bottom);

        // Escape closes, matching the framework's dialog convention.
        d->window->SetEventCallback([](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp &&
                event.virtualKey == UCKeys::Escape) {
                if (auto tw = event.targetWindow.lock())
                    static_cast<UltraCanvasWindow*>(tw.get())->Close();
                return true;
            }
            return false;
        });
        d->window->onWindowClosed = [d]() { d->closed = true; };

        (void)root;
        d->window->Show();
    }

} // namespace

void UltraFilerSettingsDialog::Show(UltraCanvasWindowBase* parent,
                                    UltraFilerSettings* settings,
                                    std::function<void()> onChanged) {
    // Raise the already open window instead of opening a second one.
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        g_dialog->window->Show();
        return;
    }

    auto state = std::make_shared<DialogState>();
    state->settings = settings;
    state->onChanged = std::move(onChanged);
    BuildDialog(state.get(), parent);
    if (state->window) g_dialog = state;   // keeps the widgets alive
}

} // namespace UltraCanvas
