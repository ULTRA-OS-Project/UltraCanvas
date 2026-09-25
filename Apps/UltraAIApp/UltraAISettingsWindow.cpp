// Apps/UltraAIApp/UltraAISettingsWindow.cpp
// UltraAI settings window: settings-page tree on the left, the selected page
// on the right — the layout, type scale and page skeleton of UltraFiler's
// settings window, so the two applications are set up the same way.
//
// Every page is built the same way (MakePage): a bold title, a one-line
// caption, the controls, and at the foot of the page, in its own tinted
// block, the notes that explain the setting. A page's "Restore defaults"
// button sits at the left end of the bottom bar, opposite Close.
// Version: 0.1.0
// Last Modified: 2026-09-24
// Author: UltraAI Module

#include "UltraAISettingsWindow.h"

#include "UltraAIAppSettings.h"
#include "UltraAISettingsDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraAIApp {

using namespace UltraCanvas;

namespace {

    // ----- one type scale for the whole window (as UltraFiler's) -----
    constexpr float kTitleFontSize = 15.0f;
    constexpr float kTextFontSize  = 10.0f;
    constexpr float kNoteFontSize  = 9.0f;
    constexpr float kTreeFontSize  = 9.0f;

    const Color kTextColor      = Color(40, 40, 44, 255);
    const Color kNoteTextColor  = Color(96, 96, 104, 255);
    const Color kNoteBackground = Color(244, 245, 248, 255);
    const Color kNoteAccent     = Color(160, 176, 200, 255);
    const Color kLocalColor     = Color(30, 120, 60, 255);
    const Color kCloudColor     = Color(170, 90, 20, 255);

    // Page geometry: the window is 720 wide, the tree takes 180, so a page
    // has 540; with its padding that leaves ~500 for content. Texts are
    // wrapped at fixed widths because content measuring needs a render
    // context, which the window does not have while it is first laid out.
    constexpr int kPagePadding  = 20;
    constexpr int kTextWidth    = 480;
    constexpr int kNoteWidth    = 470;
    constexpr int kControlHeight = 22;

    // Default providers page: one row per service.
    constexpr int kServiceLabelWidth = 130;
    constexpr int kProviderWidth     = 210;
    constexpr int kResolvedWidth     = 130;

    constexpr const char* kAutomaticLabel = "Automatic — local first";

    // Page ids double as tree node ids.
    constexpr const char* kPageServices         = "services";
    constexpr const char* kPageDefaultProviders = "services/default-providers";
    constexpr const char* kPageLocalAndCloud    = "services/local-and-cloud";
    constexpr const char* kPageAccounts         = "accounts";
    constexpr const char* kPageEndpoints        = "accounts/endpoints";
    constexpr const char* kPageStart            = "start";

    struct PageReset {
        std::string           label;
        int                   width = 170;
        std::function<void()> action;
    };

    // One service row on the Default providers page.
    struct ServiceRow {
        AICapability                          cap;
        std::shared_ptr<UltraCanvasDropdown>  dropdown;
        std::vector<std::string>              providerIds;   // [0] = automatic ("")
        std::shared_ptr<UltraCanvasLabel>     resolved;
    };

    // The one open settings window (or the last closed one, until reopened).
    struct DialogState {
        std::shared_ptr<UltraCanvasWindow>    window;
        bool                                  closed = false;
        std::shared_ptr<UltraCanvasTreeView>  tree;
        std::shared_ptr<UltraCanvasContainer> pageArea;
        std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;
        std::map<std::string, PageReset>      resets;
        std::shared_ptr<UltraCanvasButton>    restoreButton;
        std::string                           shownPage;

        std::vector<ServiceRow>               serviceRows;
        std::shared_ptr<UltraCanvasCheckbox>  cloudFallbackBox;
        std::shared_ptr<UltraCanvasLabel>     endpointsSummary;
    };

    std::shared_ptr<DialogState> g_dialog;

    // ===== TEXT =====

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id,
                                                const std::string& text,
                                                float fontSize = kTextFontSize,
                                                const Color& color = kTextColor) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
        l->SetText(text);
        l->SetFontSize(fontSize);
        l->SetTextColor(color);
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        l->size.width  = CSSLayout::Dimension::Auto();
        l->size.height = CSSLayout::Dimension::Auto();
        return l;
    }

    std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id,
                                               const std::string& text,
                                               int width = kTextWidth,
                                               float fontSize = kTextFontSize,
                                               const Color& color = kTextColor) {
        auto l = MakeLabel(id, text, fontSize, color);
        l->SetWrap(TextWrap::WrapWord);
        l->size.width = CSSLayout::Dimension::Px(width);
        return l;
    }

    // ===== PAGE SKELETON =====

    struct PageParts {
        std::shared_ptr<UltraCanvasContainer> page;
        std::shared_ptr<UltraCanvasContainer> body;
        std::shared_ptr<UltraCanvasContainer> notes;
    };

    PageParts MakePage(const std::string& id, const std::string& title,
                       const std::string& caption) {
        PageParts parts;
        parts.page = std::make_shared<UltraCanvasContainer>(id);
        parts.page->layout.SetFlexColumn().SetFlexGap(0)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);

        auto titleLabel = MakeLabel(id + "-title", title, kTitleFontSize);
        titleLabel->SetFontWeight(FontWeight::Bold);
        parts.page->AddChild(titleLabel);

        auto captionLabel = MakeText(id + "-caption", caption);
        captionLabel->SetMargin(6, 0, 0, 0);
        parts.page->AddChild(captionLabel);

        parts.body = std::make_shared<UltraCanvasContainer>(id + "-body");
        parts.body->layout.SetFlexColumn().SetFlexGap(8)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        parts.body->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parts.body->SetMargin(14, 0, 0, 0);
        parts.page->AddChild(parts.body);

        // The spacer pushes the notes to the foot of the page.
        auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
        spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        spacer->size.height = CSSLayout::Dimension::Px(16);
        parts.page->AddChild(spacer);

        parts.notes = std::make_shared<UltraCanvasContainer>(id + "-notes");
        parts.notes->layout.SetFlexColumn().SetFlexGap(6)
                           .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        parts.notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parts.notes->SetBackgroundColor(kNoteBackground);
        parts.notes->SetBorderLeft(3, kNoteAccent);
        parts.notes->SetPadding(10, 12, 10, 12);
        parts.page->AddChild(parts.notes);
        return parts;
    }

    void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
        parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize,
                                       kNoteTextColor));
    }

    // ===== CONTROLS =====

    std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id,
                                                  const std::string& label,
                                                  int width,
                                                  std::function<void()> onClick) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
        b->SetFontSize(kTextFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(kTextColor);
        b->SetBorder(1.0f, Color(0, 0, 0, 60));
        if (onClick) b->SetOnClick(std::move(onClick));
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    }

    std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(
            const std::string& id, const std::string& text, int width,
            bool checked, std::function<void(bool)> onChange) {
        auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0,
                static_cast<float>(width), static_cast<float>(kControlHeight),
                text);
        box->SetFontSize(kTextFontSize);
        CheckboxVisualStyle style = box->GetVisualStyle();
        style.base.textColor      = kTextColor;
        style.base.textHoverColor = kTextColor;
        box->SetVisualStyle(style);
        box->SetChecked(checked);
        box->size.width  = CSSLayout::Dimension::Px(width);
        box->size.height = CSSLayout::Dimension::Px(kControlHeight);
        box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        box->onStateChanged = [onChange](CheckedState, CheckedState now) {
            if (onChange) onChange(now == CheckedState::Checked);
        };
        return box;
    }

    // ===== APPLYING =====

    // Hand the settings to UltraAI's routing, save them, and bring the
    // "resolves to" column up to date — a change on one page can move what
    // "Automatic" resolves to on the other.
    void ApplyAndSave(DialogState* d) {
        auto& settings = UltraAIAppSettings::Instance();
        settings.Apply();
        settings.Save();

        for (auto& row : d->serviceRows) {
            if (!row.resolved) continue;
            const std::string resolved = ResolvedDefaultProvider(row.cap);
            if (resolved.empty()) {
                row.resolved->SetText("→ nothing available");
                row.resolved->SetTextColor(kNoteTextColor);
                continue;
            }
            const std::string kind = ProviderKind(resolved);
            row.resolved->SetText("→ " + resolved);
            row.resolved->SetTextColor(kind == "local" ? kLocalColor
                                     : kind == "cloud" ? kCloudColor
                                                       : kNoteTextColor);
        }
    }

    // ===== PAGES =====

    std::shared_ptr<UltraCanvasContainer> BuildDefaultProvidersPage(DialogState* d) {
        PageParts parts = MakePage("uai-set-page-defaults", "Default providers",
                "Which provider each service uses when a dialog is left on "
                "\"(default route)\":");

        auto& settings = UltraAIAppSettings::Instance();
        for (const auto& info : AllCapabilities()) {
            ServiceRow row;
            row.cap = info.cap;
            const std::string idBase = std::string("uai-set-def-") + info.id;

            auto line = std::make_shared<UltraCanvasContainer>(idBase + "-row");
            line->layout.SetFlexRow().SetFlexGap(8)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            line->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            line->size.width  = CSSLayout::Dimension::Px(kTextWidth);
            line->size.height = CSSLayout::Dimension::Px(28);

            auto name = MakeLabel(idBase + "-lbl", info.label);
            name->size.width = CSSLayout::Dimension::Px(kServiceLabelWidth);
            line->AddChild(name);

            row.dropdown = std::make_shared<UltraCanvasDropdown>(
                idBase + "-dd", 0, 0, kProviderWidth, 26);
            row.dropdown->size.width  = CSSLayout::Dimension::Px(kProviderWidth);
            row.dropdown->size.height = CSSLayout::Dimension::Px(26);
            row.dropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

            row.dropdown->AddItem(kAutomaticLabel);
            row.providerIds.push_back("");
            const std::string chosen = settings.DefaultProviderFor(info.cap);
            int selected = 0;
            for (const std::string& id : ProvidersFor(info.cap)) {
                row.dropdown->AddItem(id + "   (" + ProviderKind(id) + ")");
                row.providerIds.push_back(id);
                if (id == chosen) selected = static_cast<int>(row.providerIds.size()) - 1;
            }
            // A saved choice this build no longer has stays listed, so the
            // window shows what is stored instead of silently "Automatic".
            if (!chosen.empty() && selected == 0) {
                row.dropdown->AddItem(chosen + "   (not in this build)");
                row.providerIds.push_back(chosen);
                selected = static_cast<int>(row.providerIds.size()) - 1;
            }
            row.dropdown->SetSelectedIndex(selected, /*runNotifications=*/false);
            line->AddChild(row.dropdown);

            row.resolved = MakeLabel(idBase + "-resolved", "", kNoteFontSize);
            row.resolved->size.width = CSSLayout::Dimension::Px(kResolvedWidth);
            line->AddChild(row.resolved);

            const size_t index = d->serviceRows.size();
            row.dropdown->onSelectionChanged = [d, index](int selectedIndex,
                                                          const DropdownItem&) {
                if (index >= d->serviceRows.size()) return;
                ServiceRow& r = d->serviceRows[index];
                if (selectedIndex < 0 ||
                    selectedIndex >= static_cast<int>(r.providerIds.size())) {
                    return;
                }
                UltraAIAppSettings::Instance().SetDefaultProviderFor(
                    r.cap, r.providerIds[static_cast<size_t>(selectedIndex)]);
                ApplyAndSave(d);
            };

            parts.body->AddChild(line);
            d->serviceRows.push_back(std::move(row));
        }

        AddNote(parts, "uai-set-def-note1",
                "Automatic uses a provider that runs on this computer — "
                "llama.cpp or an Ollama / vLLM server for chat, ComfyUI for "
                "images, and so on — and the built-in test double where "
                "nothing local is available. The column on the right shows "
                "what Automatic resolves to right now.");
        AddNote(parts, "uai-set-def-note2",
                "Picking a cloud provider here is a deliberate choice: it is "
                "used even while cloud fallback is off (Services > Local & "
                "cloud), and its usage may be charged.");

        d->resets[kPageDefaultProviders] = {"Restore defaults", 150, [d]() {
            UltraAIAppSettings::Instance().defaultProviders.clear();
            for (auto& row : d->serviceRows) {
                if (row.dropdown) row.dropdown->SetSelectedIndex(0, false);
            }
            ApplyAndSave(d);
        }};
        return parts.page;
    }

    std::shared_ptr<UltraCanvasContainer> BuildLocalAndCloudPage(DialogState* d) {
        PageParts parts = MakePage("uai-set-page-cloud", "Local & cloud",
                "What the default route may use when no local provider is "
                "available:");

        d->cloudFallbackBox = MakeCheckbox("uai-set-cloud-fallback",
                "Fall back to cloud services when nothing local is available",
                kTextWidth, UltraAIAppSettings::Instance().allowCloudFallback,
                [d](bool on) {
                    UltraAIAppSettings::Instance().allowCloudFallback = on;
                    ApplyAndSave(d);
                });
        parts.body->AddChild(d->cloudFallbackBox);

        AddNote(parts, "uai-set-cloud-note1",
                "Off (the default): every service runs locally. A cloud "
                "service — ElevenLabs, OpenAI, Anthropic, MiniMax, ... — is "
                "only used when it is chosen: in a service dialog, on the "
                "Default providers page, or through an endpoint.");
        AddNote(parts, "uai-set-cloud-note2",
                "On: a service with no local provider uses the first cloud "
                "provider that is set up, instead of the test double. Cloud "
                "services send your input over the internet and may charge "
                "for it.");
        AddNote(parts, "uai-set-cloud-note3",
                "Deployments can also turn this on with the environment "
                "variable ULTRAAI_ALLOW_CLOUD_FALLBACK=1.");

        d->resets[kPageLocalAndCloud] = {"Restore default", 150, [d]() {
            UltraAIAppSettings::Instance().allowCloudFallback = false;
            if (d->cloudFallbackBox) d->cloudFallbackBox->SetChecked(false);
            ApplyAndSave(d);
        }};
        return parts.page;
    }

    std::string EndpointsSummaryText() {
        const auto& all = EndpointStore::Instance().All();
        if (all.empty()) return "No endpoints are configured yet.";
        std::string text = std::to_string(all.size()) +
                           (all.size() == 1 ? " endpoint: " : " endpoints: ");
        for (size_t i = 0; i < all.size(); ++i) {
            if (i) text += ", ";
            text += all[i].name.empty() ? all[i].id : all[i].name;
        }
        return text;
    }

    std::shared_ptr<UltraCanvasContainer> BuildEndpointsPage(DialogState* d) {
        PageParts parts = MakePage("uai-set-page-endpoints", "Endpoints",
                "Providers set up once — base URL, default model, API key — "
                "and the services each one may serve:");

        d->endpointsSummary = MakeText("uai-set-endpoints-summary",
                                       EndpointsSummaryText());
        parts.body->AddChild(d->endpointsSummary);
        parts.body->AddChild(MakeButton("uai-set-endpoints-edit",
                "Edit endpoints...", 150, [d]() {
            auto dlg = std::make_shared<UltraAISettingsDialog>();
            dlg->CreateSettingsDialog();
            dlg->onResult = [d](DialogResult) {
                if (d->endpointsSummary) {
                    d->endpointsSummary->SetText(EndpointsSummaryText());
                }
            };
            dlg->ShowModal(d->window.get());
        }));

        AddNote(parts, "uai-set-endpoints-note1",
                "API keys are kept in UltraVault, never in the settings "
                "files. Local servers (Ollama, vLLM, llama.cpp server) need "
                "no key: pick \"openai\" and set the server's base URL.");
        AddNote(parts, "uai-set-endpoints-note2",
                "For the hosted ULTRA service, an endpoint points at the "
                "relay's URL; the account's own keys are used when it points "
                "at the provider directly.");
        return parts.page;
    }

    std::shared_ptr<UltraCanvasContainer> BuildStartPage(DialogState* /*d*/) {
        PageParts parts = MakePage("uai-set-page-start", "Settings",
                "Open a section on the left and choose the page to set:");
        AddNote(parts, "uai-set-start-note1",
                "Services - which provider each service uses by default, and "
                "whether the default may fall back to a cloud service. Out "
                "of the box everything runs locally.");
        AddNote(parts, "uai-set-start-note2",
                "Accounts - the endpoints: providers with their base URL, "
                "model and API key.");
        AddNote(parts, "uai-set-start-note3",
                "Every change applies straight away and is saved; there is "
                "nothing to confirm.");
        return parts.page;
    }

    // ===== PAGE SWITCHING =====

    void UpdateRestoreButton(DialogState* d) {
        if (!d->restoreButton) return;
        auto it = d->resets.find(d->shownPage);
        if (it == d->resets.end()) {
            d->restoreButton->SetVisible(false);
            d->restoreButton->RequestRedraw();
            return;
        }
        d->restoreButton->SetText(it->second.label);
        d->restoreButton->size.width = CSSLayout::Dimension::Px(it->second.width);
        d->restoreButton->SetVisible(true);
        d->restoreButton->RequestRedraw();
    }

    void ShowPage(DialogState* d, const std::string& nodeId) {
        std::string target = nodeId;
        if (!d->pages.count(target)) {
            // A section: fall back to its first page in tree order.
            TreeNode* node = d->tree ? d->tree->FindNode(nodeId) : nullptr;
            while (node && node->HasChildren()) node = node->FirstChild();
            if (!node || !d->pages.count(node->data.nodeId)) return;
            target = node->data.nodeId;
        }
        d->shownPage = target;
        for (auto& [id, page] : d->pages) page->SetVisible(id == target);
        if (target == kPageEndpoints && d->endpointsSummary) {
            d->endpointsSummary->SetText(EndpointsSummaryText());
        }
        UpdateRestoreButton(d);
    }

    void SelectPage(DialogState* d, const std::string& pageId) {
        TreeNode* node = d->tree ? d->tree->FindNode(pageId) : nullptr;
        if (node) {
            for (TreeNode* parent = node->parent; parent; parent = parent->parent)
                d->tree->ExpandNode(parent);
            d->tree->SelectNode(node);
        }
        ShowPage(d, pageId);
    }

    void AddPage(DialogState* d, const char* pageId,
                 const std::shared_ptr<UltraCanvasContainer>& page) {
        page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[pageId] = page;
        d->pageArea->AddChild(page);
    }

    void AddTreeNode(DialogState* d, const char* parentId, const char* id,
                     const char* text) {
        TreeNodeData data;
        data.nodeId = id;
        data.text = text;
        d->tree->AddNode(parentId, data);
    }

    void BuildWindow(DialogState* d, UltraCanvasWindowBase* parent) {
        WindowConfig wc;
        wc.title = "UltraAI - Settings";
        wc.width = 720;
        wc.height = 660;
        wc.resizable = true;
        wc.type = WindowType::Dialog;
        wc.parentWindow = parent;
        d->window = CreateWindow(wc);
        if (!d->window || !d->window->IsCreated()) { d->window.reset(); return; }

        d->window->layout.SetFlexColumn()
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->window->SetBackgroundColor(Color(249, 249, 251, 255));

        // ----- settings-page tree | page area -----
        auto content = std::make_shared<UltraCanvasContainer>("uai-set-content");
        content->layout.SetFlexRow()
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        d->tree = std::make_shared<UltraCanvasTreeView>("uai-set-tree");
        d->tree->SetFontSize(kTreeFontSize);
        d->tree->SetRowHeight(24);
        d->tree->SetSelectionMode(TreeSelectionMode::Single);
        d->tree->SetLineStyle(TreeLineStyle::NoLine);
        d->tree->SetBackgroundColor(Color(243, 243, 246, 255));
        d->tree->SetShowFirstChildOnExpand(true);
        d->tree->SetAutoExpandSelectedNode(true);
        d->tree->size.width = CSSLayout::Dimension::Px(180);
        d->tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        TreeNodeData rootData;
        rootData.nodeId = "settings";
        rootData.text = "Settings";
        d->tree->SetRootNode(rootData);

        AddTreeNode(d, "settings", kPageServices, "Services");
        AddTreeNode(d, kPageServices, kPageDefaultProviders, "Default providers");
        AddTreeNode(d, kPageServices, kPageLocalAndCloud, "Local & cloud");
        AddTreeNode(d, "settings", kPageAccounts, "Accounts");
        AddTreeNode(d, kPageAccounts, kPageEndpoints, "Endpoints");

        d->tree->SetRootVisible(false);
        content->AddChild(d->tree);

        d->pageArea = std::make_shared<UltraCanvasContainer>("uai-set-pages");
        d->pageArea->layout.SetFlexColumn()
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pageArea->SetBackgroundColor(Color(255, 255, 255, 255));
        content->AddChild(d->pageArea);

        AddPage(d, kPageDefaultProviders, BuildDefaultProvidersPage(d));
        AddPage(d, kPageLocalAndCloud, BuildLocalAndCloudPage(d));
        AddPage(d, kPageEndpoints, BuildEndpointsPage(d));
        AddPage(d, kPageStart, BuildStartPage(d));

        d->window->AddChild(content);

        // ----- bottom bar: [Restore defaults]            [Close] -----
        auto bottom = std::make_shared<UltraCanvasContainer>("uai-set-bottom");
        bottom->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bottom->SetPadding(8, 12, 8, 12);
        bottom->SetBorderTop(1, Color(225, 225, 230, 255));

        auto bottomLeft = std::make_shared<UltraCanvasContainer>("uai-set-bottom-left");
        bottomLeft->layout.SetFlexRow()
                          .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottomLeft->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->restoreButton = MakeButton("uai-set-restore", "Restore defaults", 150,
                [d]() {
            auto it = d->resets.find(d->shownPage);
            if (it != d->resets.end() && it->second.action) it->second.action();
        });
        d->restoreButton->SetVisible(false);
        bottomLeft->AddChild(d->restoreButton);
        bottom->AddChild(bottomLeft);

        bottom->AddChild(MakeButton("uai-set-close", "Close", 90,
                [d]() { if (d->window) d->window->Close(); }));
        d->window->AddChild(bottom);

        d->tree->onNodeSelected = [d](TreeNode* node) {
            if (!node) return;
            // A section has no page of its own: selecting one moves on to
            // its first page.
            TreeNode* leaf = node;
            while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
            if (leaf && leaf != node) {
                d->tree->SelectNode(leaf);
                return;
            }
            ShowPage(d, node->data.nodeId);
        };

        ApplyAndSave(d);   // fills the "resolves to" column
        ShowPage(d, kPageStart);

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

        d->window->Show();
    }

} // namespace

void UltraAISettingsWindow::Show(UltraCanvasWindowBase* parent, Page page) {
    const char* pageId = nullptr;
    switch (page) {
        case Page::DefaultProviders: pageId = kPageDefaultProviders; break;
        case Page::LocalAndCloud:    pageId = kPageLocalAndCloud; break;
        case Page::Endpoints:        pageId = kPageEndpoints; break;
        default: break;
    }
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        if (pageId) SelectPage(g_dialog.get(), pageId);
        g_dialog->window->Show();
        return;
    }

    auto state = std::make_shared<DialogState>();
    BuildWindow(state.get(), parent);
    if (!state->window) return;
    g_dialog = state;   // keeps the widgets alive
    if (pageId) SelectPage(state.get(), pageId);
}

void UltraAISettingsWindow::Shutdown() {
    g_dialog.reset();
}

} // namespace UltraAIApp
