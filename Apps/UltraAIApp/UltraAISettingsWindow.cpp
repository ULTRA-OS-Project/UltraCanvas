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
#include "UltraAI.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#ifdef ULTRAAI_HAS_ULTRAVAULT
#include <UltraVault/UltraVault.h>
#endif

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

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
    constexpr const char* kPageEndpointEditor   = "accounts/editor";
    constexpr const char* kPageStart            = "start";

    // Tree node ids for the Accounts > endpoint list. "New endpoint" is the
    // first child; each configured endpoint is kNodeEndpointPrefix + its id.
    constexpr const char* kNodeNewEndpoint      = "accounts/new";
    constexpr const char* kNodeEndpointPrefix   = "accounts/ep/";

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

        // Inline endpoint editor (the one reusable "accounts/editor" page).
        std::shared_ptr<UltraCanvasTextInput> epName;
        std::shared_ptr<UltraCanvasDropdown>  epProvider;
        std::shared_ptr<UltraCanvasTextInput> epBaseUrl;
        std::shared_ptr<UltraCanvasTextInput> epModel;
        std::shared_ptr<UltraCanvasTextInput> epKey;
        std::map<AICapability, std::shared_ptr<UltraCanvasCheckbox>> epModes;
        std::shared_ptr<UltraCanvasLabel>     epStatus;
        std::string                           epEditingId;   // "" => New endpoint
        bool                                  rebuildingNodes = false;
    };

    std::shared_ptr<DialogState> g_dialog;

    // Inline endpoint editor helpers (defined further down).
    void LoadEndpointIntoEditor(DialogState* d, const Endpoint* e);
    void SaveEndpoint(DialogState* d);
    void DeleteEndpoint(DialogState* d);
    void RebuildEndpointNodes(DialogState* d, const std::string& selectNodeId);

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

    // A "label above input" field group for the endpoint editor; the input is
    // handed back through `out`.
    std::shared_ptr<UltraCanvasContainer> MakeField(
            const std::string& id, const std::string& label,
            const std::string& placeholder,
            std::shared_ptr<UltraCanvasTextInput>& out) {
        auto col = std::make_shared<UltraCanvasContainer>(id + "-grp");
        col->layout.SetFlexColumn().SetFlexGap(4)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        col->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        col->size.height = CSSLayout::Dimension::Px(50);

        auto lbl = MakeLabel(id + "-lbl", label);
        lbl->size.height = CSSLayout::Dimension::Px(18);
        col->AddChild(lbl);

        out = std::make_shared<UltraCanvasTextInput>(id, 0, 0, 0, 28);
        out->SetPlaceholder(placeholder);
        out->size.height = CSSLayout::Dimension::Px(28);
        out->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        col->AddChild(out);
        return col;
    }

    // Union of every capability's registered providers, de-duplicated and
    // sorted — the choices for the editor's Provider dropdown.
    std::vector<std::string> AllKnownProviders() {
        std::set<std::string> uniq;
        auto merge = [&](const std::vector<std::string>& v) {
            for (const auto& s : v) uniq.insert(s);
        };
        merge(ListTextLLMProviders());
        merge(ListEmbeddingsProviders());
        merge(ListSpeechToTextProviders());
        merge(ListTextToSpeechProviders());
        merge(ListTranslatorProviders());
        merge(ListImageGenProviders());
        merge(ListVisionAnalyzerProviders());
        merge(ListVideoGenProviders());
        merge(ListMusicGenProviders());
        merge(ListCodeAssistProviders());
        return {uniq.begin(), uniq.end()};
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

    // Fill the editor widgets from `e` (nullptr => an empty "New endpoint"
    // form). The API key is never surfaced — the field always starts blank.
    void LoadEndpointIntoEditor(DialogState* d, const Endpoint* e) {
        d->epEditingId = e ? e->id : "";

        if (d->epName)    d->epName->SetText(e ? e->name : "");
        if (d->epBaseUrl) d->epBaseUrl->SetText(e ? e->baseUrl : "");
        if (d->epModel)   d->epModel->SetText(e ? e->defaultModel : "");
        if (d->epKey)     d->epKey->SetText("");

        if (d->epProvider && e && !e->providerId.empty()) {
            for (int i = 0; i < d->epProvider->GetItemCount(); ++i) {
                if (const auto* item = d->epProvider->GetItem(i);
                    item && item->text == e->providerId) {
                    d->epProvider->SetSelectedIndex(i, false);
                    break;
                }
            }
        } else if (d->epProvider && d->epProvider->GetItemCount() > 0) {
            d->epProvider->SetSelectedIndex(0, false);
        }

        for (auto& [cap, cb] : d->epModes) {
            cb->SetChecked(e ? e->Supports(cap) : false);
        }

        if (d->epStatus) {
            d->epStatus->SetText(
                e ? "" : "Editing a new endpoint — fill in the form and Save.");
        }
    }

    std::shared_ptr<UltraCanvasContainer> BuildEndpointEditorPage(DialogState* d) {
        PageParts parts = MakePage("uai-set-page-endpoint-editor", "Endpoints",
                "Providers set up once — base URL, default model, API key — "
                "and the services each one may serve:");

        // The form is a fixed-width column so its inputs line up like the
        // controls on the sibling pages.
        auto form = std::make_shared<UltraCanvasContainer>("uai-ep-form");
        form->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        form->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        form->size.width = CSSLayout::Dimension::Px(kTextWidth);

        form->AddChild(MakeField("uai-ep-name", "Name", "e.g. My OpenAI account",
                                 d->epName));

        // Provider.
        auto provCol = std::make_shared<UltraCanvasContainer>("uai-ep-prov-grp");
        provCol->layout.SetFlexColumn().SetFlexGap(4)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        provCol->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        provCol->size.height = CSSLayout::Dimension::Px(50);
        auto provLbl = MakeLabel("uai-ep-prov-lbl", "Provider");
        provLbl->size.height = CSSLayout::Dimension::Px(18);
        provCol->AddChild(provLbl);
        d->epProvider = std::make_shared<UltraCanvasDropdown>("uai-ep-provider",
                                                              0, 0, 0, 28);
        d->epProvider->size.height = CSSLayout::Dimension::Px(28);
        d->epProvider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        for (const auto& p : AllKnownProviders()) d->epProvider->AddItem(p);
        if (d->epProvider->GetItemCount() > 0)
            d->epProvider->SetSelectedIndex(0, false);
        provCol->AddChild(d->epProvider);
        form->AddChild(provCol);

        form->AddChild(MakeText("uai-ep-localhint",
            "Local server? Pick \"openai\", set Base URL http://127.0.0.1:8080, "
            "leave the key empty.", kTextWidth, kNoteFontSize, kNoteTextColor));

        form->AddChild(MakeField("uai-ep-baseurl",
            "Base URL (optional, for self-hosted)",
            "https://... — leave empty for the provider default", d->epBaseUrl));
        form->AddChild(MakeField("uai-ep-model",
            "Default model (id, GGUF path or checkpoint)",
            "provider default", d->epModel));
        form->AddChild(MakeField("uai-ep-key",
            "API key — cloud providers only; leave empty for local/self-hosted",
            "leave empty to keep the stored key", d->epKey));

        // Mode checkboxes in a fixed two-column grid (as the former dialog).
        form->AddChild(MakeLabel("uai-ep-modes-lbl",
                                 "Modes this endpoint can serve"));
        auto modesCol = std::make_shared<UltraCanvasContainer>("uai-ep-modes");
        const auto& caps = AllCapabilities();
        constexpr int kCols   = 2;
        constexpr int kRowH   = 24;
        constexpr int kRowGap = 4;
        const int rows = static_cast<int>((caps.size() + kCols - 1) / kCols);
        {
            using namespace CSSLayout;
            std::vector<GridTrackSize> colTracks(
                kCols, {GridTrackSizeKind::Fr, Dimension::Fr(1)});
            std::vector<GridTrackSize> rowTracks(
                static_cast<size_t>(std::max(0, rows)),
                {GridTrackSizeKind::Fixed, Dimension::Px(kRowH)});
            modesCol->layout.SetGrid()
                            .SetGridColumns(colTracks)
                            .SetGridRows(rowTracks)
                            .SetGridGap(kRowGap, 8)   // rowGap, columnGap
                            .SetGridJustifyItems(JustifyItems::Stretch)
                            .SetGridAlignItems(AlignItems::Stretch);
        }
        modesCol->size.height = CSSLayout::Dimension::Px(
            static_cast<float>(rows * kRowH + std::max(0, rows - 1) * kRowGap));
        modesCol->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        for (size_t i = 0; i < caps.size(); ++i) {
            auto cb = std::make_shared<UltraCanvasCheckbox>(
                std::string("uai-ep-mode-") + caps[i].id, 0, 0, 0, 24,
                caps[i].label);
            cb->SetFontSize(kTextFontSize);
            CheckboxVisualStyle style = cb->GetVisualStyle();
            style.base.textColor      = kTextColor;
            style.base.textHoverColor = kTextColor;
            cb->SetVisualStyle(style);
            cb->size.height = CSSLayout::Dimension::Px(24);
            d->epModes[caps[i].cap] = cb;
            modesCol->AddChild(cb);
            cb->layoutItem.SetGridRowColSimplified(
                static_cast<int>(i / kCols), static_cast<int>(i % kCols));
        }
        form->AddChild(modesCol);

        // Footer: Save + status + Delete (the window keeps its own Close).
        auto footer = std::make_shared<UltraCanvasContainer>("uai-ep-footer");
        footer->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        footer->size.height = CSSLayout::Dimension::Px(34);
        footer->AddChild(MakeButton("uai-ep-save", "Save endpoint", 150,
                                    [d]() { SaveEndpoint(d); }));
        d->epStatus = MakeLabel("uai-ep-status", "", kTextFontSize, kNoteTextColor);
        d->epStatus->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        footer->AddChild(d->epStatus);
        footer->AddChild(MakeButton("uai-ep-delete", "Delete", 110,
                                    [d]() { DeleteEndpoint(d); }));
        form->AddChild(footer);

        parts.body->AddChild(form);

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

    // Rebuild the Accounts section's children — "New endpoint" first, then one
    // node per stored endpoint — and, when `selectNodeId` is given, reveal and
    // select that node (which reloads the editor through onNodeSelected). Only
    // the Accounts subtree is touched; Services is left alone.
    void RebuildEndpointNodes(DialogState* d, const std::string& selectNodeId) {
        if (!d->tree) return;
        TreeNode* accounts = d->tree->FindNode(kPageAccounts);
        if (!accounts) return;

        d->rebuildingNodes = true;
        std::vector<std::string> childIds;
        for (const auto& child : accounts->children)
            childIds.push_back(child->data.nodeId);
        for (const auto& id : childIds) d->tree->RemoveNode(id);

        AddTreeNode(d, kPageAccounts, kNodeNewEndpoint, "New endpoint");
        for (const auto& e : EndpointStore::Instance().All()) {
            TreeNodeData data;
            data.nodeId = std::string(kNodeEndpointPrefix) + e.id;
            data.text   = e.name.empty() ? e.id : e.name;
            d->tree->AddNode(kPageAccounts, data);
        }
        d->rebuildingNodes = false;

        if (!selectNodeId.empty()) {
            if (TreeNode* node = d->tree->FindNode(selectNodeId)) {
                for (TreeNode* p = node->parent; p; p = p->parent)
                    d->tree->ExpandNode(p);
                d->tree->SelectNode(node);
            }
        }
    }

    void SaveEndpoint(DialogState* d) {
        const std::string name = d->epName ? d->epName->GetText() : "";
        std::string provider;
        if (d->epProvider) {
            if (const auto* item = d->epProvider->GetSelectedItem())
                provider = item->text;
        }
        if (provider.empty()) {
            if (d->epStatus) d->epStatus->SetText("Pick a provider first.");
            return;
        }

        Endpoint e;
        auto& store = EndpointStore::Instance();
        if (!d->epEditingId.empty()) {
            if (const auto* existing = store.FindById(d->epEditingId)) e = *existing;
        }
        if (e.id.empty()) e.id = store.MakeId(name.empty() ? provider : name);

        e.name         = name.empty() ? e.id : name;
        e.providerId   = provider;
        e.baseUrl      = d->epBaseUrl ? d->epBaseUrl->GetText() : "";
        e.defaultModel = d->epModel ? d->epModel->GetText() : "";
        e.modes.clear();
        for (auto& [cap, cb] : d->epModes) {
            if (cb->IsChecked()) e.modes.insert(cap);
        }

        // Store a freshly-typed API key in the vault, then blank the field.
        if (d->epKey && !d->epKey->GetText().empty()) {
#ifdef ULTRAAI_HAS_ULTRAVAULT
            if (!UltraVault::IsAvailable()) UltraVault::Initialize();
            auto stored = UltraVault::Put(
                e.VaultRef(),
                UltraVault::SecretValue::FromString(d->epKey->GetText()));
            if (!stored.IsOk()) {
                if (d->epStatus)
                    d->epStatus->SetText("Could not store the key: " + stored.message);
                return;
            }
#endif
            d->epKey->SetText("");
        }

        store.Upsert(e);
        if (!store.Save()) {
            if (d->epStatus)
                d->epStatus->SetText("Saved in memory, but writing " +
                                     EndpointStore::ConfigPath() + " failed.");
            return;
        }
        d->epEditingId = e.id;
        // Reselecting the saved node reloads the editor and clears the status,
        // so the confirmation is set afterwards.
        RebuildEndpointNodes(d, std::string(kNodeEndpointPrefix) + e.id);
        if (d->epStatus) d->epStatus->SetText("Saved \"" + e.name + "\".");
    }

    void DeleteEndpoint(DialogState* d) {
        if (d->epEditingId.empty()) {
            if (d->epStatus) d->epStatus->SetText("Nothing to delete.");
            return;
        }
        EndpointStore::Instance().Remove(d->epEditingId);
        EndpointStore::Instance().Save();
        d->epEditingId.clear();
        RebuildEndpointNodes(d, kNodeNewEndpoint);
        if (d->epStatus) d->epStatus->SetText("Endpoint deleted.");
    }

    void BuildWindow(DialogState* d, UltraCanvasWindowBase* parent) {
        WindowConfig wc;
        wc.title = "UltraAI - Settings";
        wc.width = 720;
        wc.height = 800;
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
        // Accounts' children — "New endpoint" plus one node per endpoint — are
        // built dynamically (and rebuilt after every Save/Delete). No selection
        // here: the window opens on the start page.
        RebuildEndpointNodes(d, "");

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
        AddPage(d, kPageEndpointEditor, BuildEndpointEditorPage(d));
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
            if (d->rebuildingNodes) return;
            const std::string& id = node->data.nodeId;

            // Accounts' endpoint nodes all share the one editor page; selecting
            // one loads that endpoint (or an empty form for "New endpoint").
            if (id == kNodeNewEndpoint) {
                ShowPage(d, kPageEndpointEditor);
                LoadEndpointIntoEditor(d, nullptr);
                return;
            }
            if (id.rfind(kNodeEndpointPrefix, 0) == 0) {
                ShowPage(d, kPageEndpointEditor);
                const std::string epId =
                    id.substr(std::strlen(kNodeEndpointPrefix));
                LoadEndpointIntoEditor(d,
                                       EndpointStore::Instance().FindById(epId));
                return;
            }

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
        case Page::Endpoints:        pageId = kNodeNewEndpoint; break;
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
