// core/UltraCanvasProxySettingsDialog.cpp
// The proxy settings dialog. See UltraCanvasProxySettingsDialog.h.
//
// One form grid holds every row, so the captions of the manual proxy and of
// its sign-in share one column and their fields start at one x, in any
// language; a row that does not apply to the chosen mode is greyed out
// rather than hidden, so the dialog does not jump while choosing.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasProxySettingsDialog.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTheme.h"
#include "UltraLocalisation/UltraLocalisation.h"   // Tr, TrF

#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace UltraCanvas {

    namespace {

        constexpr int   kRowHeight = 28;
        constexpr float kRowGap    = 6.0f;
        constexpr float kIndent    = 24.0f;   // a group under its choice
        constexpr int   kStatusHeight = 40;   // two lines

        // The address Check connection opened last, offered again.
        std::mutex g_checkUrlMutex;
        std::string g_checkUrl = "https://www.example.com/";

        std::string TrimSpace(const std::string& s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return s.substr(b, e - b);
        }

        // The widgets the callbacks reach, as plain pointers: the dialog owns
        // them, and outlives every callback stored on one of them. The two
        // radio groups live here; a radio's own handler is the group's (raw),
        // so nothing a radio holds owns this.
        struct Form {
            UltraCanvasRadioGroup modeGroup;
            UltraCanvasRadioGroup kindGroup;
            UltraCanvasRadio* noProxy = nullptr;
            UltraCanvasRadio* system = nullptr;
            UltraCanvasRadio* manual = nullptr;
            UltraCanvasRadio* http = nullptr;
            UltraCanvasRadio* socks = nullptr;
            UltraCanvasLabel* systemNote = nullptr;
            UltraCanvasCheckbox* usePac = nullptr;
            UltraCanvasTextInput* pacUrl = nullptr;
            UltraCanvasLabel* pacExample = nullptr;
            UltraCanvasTextInput* host = nullptr;
            UltraCanvasSpinner* port = nullptr;
            UltraCanvasTextInput* noProxyFor = nullptr;
            UltraCanvasLabel* noProxyExample = nullptr;
            UltraCanvasCheckbox* authenticate = nullptr;
            UltraCanvasTextInput* user = nullptr;
            UltraCanvasTextInput* password = nullptr;
            UltraCanvasCheckbox* remember = nullptr;
            UltraCanvasLabel* status = nullptr;
            std::vector<UltraCanvasUIElement*> manualRows;   // captions too
            std::vector<UltraCanvasUIElement*> authRows;
            bool canRunPac = false;
            // The port the kind had before the other was chosen: SOCKS' 1080
            // replaces HTTP's 8080 only while the person has not typed one.
            int lastDefaultPort = 8080;

            ProxyMode Mode() const {
                if (manual && manual->IsChecked()) return ProxyMode::Manual;
                if (noProxy && noProxy->IsChecked()) return ProxyMode::NoProxy;
                return ProxyMode::System;
            }

            // The settings as they stand in the dialog.
            ProxySettings Read() const {
                ProxySettings s;
                s.mode = Mode();
                s.useAutoConfigUrl = usePac->IsChecked();
                s.autoConfigUrl = TrimSpace(pacUrl->GetText());
                s.kind = socks->IsChecked() ? ProxyKind::Socks : ProxyKind::Http;
                s.host = TrimSpace(host->GetText());
                s.port = static_cast<int>(port->GetValue());
                s.noProxyFor = TrimSpace(noProxyFor->GetText());
                s.authenticate = authenticate->IsChecked();
                s.user = TrimSpace(user->GetText());
                s.password = password->GetText();
                s.rememberPassword = remember->IsChecked();
                return s;
            }

            // Greys out what the chosen mode does not use.
            void Apply() const {
                const ProxyMode mode = Mode();
                const bool sys = mode == ProxyMode::System;
                const bool man = mode == ProxyMode::Manual;
                usePac->SetDisabled(!sys || !canRunPac);
                pacUrl->SetDisabled(!sys || !canRunPac || !usePac->IsChecked());
                pacExample->SetDisabled(!sys || !canRunPac);
                systemNote->SetDisabled(!sys);
                for (UltraCanvasUIElement* e : manualRows) e->SetDisabled(!man);
                const bool auth = man && authenticate->IsChecked();
                for (UltraCanvasUIElement* e : authRows) e->SetDisabled(!auth);
            }

            void Say(const std::string& text, bool problem) const {
                const UltraCanvasTheme& theme = UltraCanvasTheme::Current();
                status->SetTextColor(problem ? Color(197, 48, 48, 255) : theme.roles.ink);
                status->SetText(text);
            }

            // Why the dialog cannot be saved as it stands, or empty.
            std::string Problem() const {
                if (Mode() == ProxyMode::Manual && TrimSpace(host->GetText()).empty())
                    return Tr("Enter the proxy's host name.");
                if (Mode() == ProxyMode::System && usePac->IsChecked() && canRunPac &&
                    TrimSpace(pacUrl->GetText()).empty())
                    return Tr("Enter the address of the proxy configuration script.");
                return {};
            }
        };

        std::shared_ptr<UltraCanvasRadio> MakeRadio(const std::string& id, const std::string& text, bool checked) {
            auto radio = UltraCanvasRadio::Create(id, 0, 0, text, checked);
            radio->size.height = CSSLayout::Dimension::Px(static_cast<float>(kRowHeight));
            return radio;
        }

        std::shared_ptr<UltraCanvasLabel> MakeNote(const std::string& id, const std::string& text) {
            auto label = std::make_shared<UltraCanvasLabel>(id, -1, -1, -1, kRowHeight, text);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            label->SetTextColor(UltraCanvasTheme::Current().roles.inkMuted);
            return label;
        }

        // `element` moved in by `indent`: in a row of its own, padded on the
        // left (a grid places its items by their tracks and gives no item a
        // margin of its own). `grow`: the element takes the rest of the row.
        std::shared_ptr<UltraCanvasUIElement> Indented(const std::shared_ptr<UltraCanvasUIElement>& element,
                                                       float indent, bool grow = false) {
            if (indent <= 0) return element;
            auto row = CreateFormCellRow(element->GetIdentifier() + "-indent", 8.0f, kRowHeight);
            row->SetPadding(0, 0, 0, indent);
            if (grow) element->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            else element->layoutItem.SetFlexShrink(0);
            row->AddChild(element);
            return row;
        }

        // A control on its own row across both columns, moved in by `indent`.
        void AddWide(const std::shared_ptr<UltraCanvasContainer>& grid,
                     const std::shared_ptr<UltraCanvasUIElement>& element, float indent) {
            AddFormWideRow(grid, Indented(element, indent));
        }

        // "caption [control]", the caption moved in by `indent`.
        UltraCanvasLabel* AddRow(const std::shared_ptr<UltraCanvasContainer>& grid, const std::string& id,
                                 const std::string& caption, const std::shared_ptr<UltraCanvasUIElement>& control,
                                 float indent) {
            auto label = CreateFormCaption(id + "-caption", caption);
            grid->AddChild(Indented(label, indent));
            grid->AddChild(control);
            return label.get();
        }

        std::string SystemProxyNote() {
#ifdef ULTRACANVAS_HAS_NET
            UltraNetProxyConfig found;
            UltraNet_DetectSystemProxy(found);
            const std::string route = UltraNet_DescribeProxy(found);
            if (!route.empty()) return TrF("The system's proxy now: {0}", route);
#endif
            return Tr("The system names no proxy now: connections go directly.");
        }

    } // namespace

    std::string DescribeProxySettings(const ProxySettings& settings) {
        switch (settings.mode) {
            case ProxyMode::NoProxy: return Tr("No proxy");
            case ProxyMode::System:  return Tr("Use system proxy settings");
            case ProxyMode::Manual:  break;
        }
        // "HTTP" and "SOCKS" are the protocols' names in every language.
        const std::string where = settings.host + ":" + std::to_string(settings.EffectivePort());
        return settings.kind == ProxyKind::Socks ? TrF("SOCKS proxy {0}", where)
                                                 : TrF("HTTP proxy {0}", where);
    }

    void ShowProxySettingsDialog(UltraCanvasWindowBase* parent,
                                 std::function<void(const ProxySettings&)> onSaved) {
        const ProxySettings saved = ProxySettings::Load();

        DialogConfig config;
        config.title = Tr("Proxy settings");
        // Information builds the message column the form goes into; the
        // severity icon is left off and the message area hidden.
        config.dialogType = DialogType::Information;
        config.buttons = DialogButtons::OKCancel;
        config.defaultButton = DialogButton::OK;
        config.showIcon = false;
        config.width = 600;
        config.height = 640;
        auto dialog = UltraCanvasDialogManager::CreateDialog(config);
        if (!dialog) return;
        UltraCanvasModalDialog* dlg = dialog.get();
        if (auto message = dialog->FindChildById("MessageArea")) message->SetVisible(false);

        auto form = std::make_shared<Form>();
        form->canRunPac = ProxySettings::CanUseAutoConfigUrl();

        auto grid = CreateFormGrid("ProxyForm", kRowGap, 12.0f);
        int rows = 0;

        // ---- the three modes, and what belongs to each -------------------
        auto noProxy = MakeRadio("ProxyNone", Tr("No proxy"), saved.mode == ProxyMode::NoProxy);
        AddWide(grid, noProxy, 0); ++rows;

        auto system = MakeRadio("ProxySystem", Tr("Use system proxy settings"), saved.mode == ProxyMode::System);
        AddWide(grid, system, 0); ++rows;

        // Filled in once the system has been asked (below): WPAD can take
        // seconds, which the dialog does not wait for.
        auto systemNote = MakeNote("ProxySystemNote", std::string());
        systemNote->size.width = CSSLayout::Dimension::Auto();
        AddWide(grid, systemNote, kIndent); ++rows;

        auto pacLine = CreateFormCellRow("ProxyPacLine", 8.0f, kRowHeight);
        auto usePac = UltraCanvasCheckbox::CreateCheckbox("ProxyUsePac", 0, 0, 0, kRowHeight,
                                                          Tr("Automatic proxy configuration URL"),
                                                          saved.useAutoConfigUrl);
        usePac->layoutItem.SetFlexShrink(0);
        if (!form->canRunPac)
            usePac->SetTooltip(Tr("This system cannot run a proxy configuration script."));
        pacLine->AddChild(usePac);
        auto pacUrl = CreateTextInput("ProxyPacUrl", 0, 0, 0, kRowHeight);
        pacUrl->SetText(saved.autoConfigUrl);
        pacUrl->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        pacLine->AddChild(pacUrl);
        pacLine->SetPadding(0, 0, 0, kIndent);
        AddFormWideRow(grid, pacLine); ++rows;

        auto pacExample = MakeNote("ProxyPacExample", TrF("Example: {0}", "https://example.com/wpad.pac"));
        AddWide(grid, pacExample, 2 * kIndent); ++rows;

        auto manual = MakeRadio("ProxyManual", Tr("Manual proxy configuration"), saved.mode == ProxyMode::Manual);
        AddWide(grid, manual, 0); ++rows;

        // The protocols' names, the same in every language.
        auto kindLine = CreateFormCellRow("ProxyKindLine", 16.0f, kRowHeight);
        auto http = MakeRadio("ProxyHttp", "HTTP", saved.kind == ProxyKind::Http);
        auto socks = MakeRadio("ProxySocks", "SOCKS", saved.kind == ProxyKind::Socks);
        kindLine->AddChild(http);
        kindLine->AddChild(socks);
        kindLine->SetPadding(0, 0, 0, kIndent);
        AddFormWideRow(grid, kindLine); ++rows;

        auto host = CreateTextInput("ProxyHost", 0, 0, 0, kRowHeight);
        host->SetText(saved.host);
        host->SetPlaceholder("proxy.example.com");
        UltraCanvasLabel* hostCaption = AddRow(grid, "ProxyHost", Tr("Host"), host, kIndent); ++rows;

        const int initialPort = saved.port > 0 ? saved.port : (saved.kind == ProxyKind::Socks ? 1080 : 8080);
        auto port = CreateIntSpinner("ProxyPort", 0, 0, 120, kRowHeight, 1, 65535, initialPort, 1);
        UltraCanvasLabel* portCaption = AddRow(grid, "ProxyPort", Tr("Port"), port, kIndent); ++rows;

        auto noProxyFor = CreateTextInput("ProxyNoProxyFor", 0, 0, 0, kRowHeight);
        noProxyFor->SetText(saved.noProxyFor);
        noProxyFor->SetTooltip(Tr("Hosts reached directly, separated by commas. * stands for any text."));
        UltraCanvasLabel* noProxyCaption = AddRow(grid, "ProxyNoProxyFor", Tr("No proxy for"), noProxyFor, kIndent); ++rows;

        auto noProxyExample = MakeNote("ProxyNoProxyExample", TrF("Example: {0}", "*.example.com, 192.168.*"));
        auto exampleCaption = CreateFormCaption("ProxyNoProxyExample-caption", "");
        AddFormRow(grid, exampleCaption, noProxyExample); ++rows;

        auto authenticate = UltraCanvasCheckbox::CreateCheckbox("ProxyAuth", 0, 0, 0, kRowHeight,
                                                                Tr("Proxy authentication"), saved.authenticate);
        AddWide(grid, authenticate, kIndent); ++rows;

        auto user = CreateTextInput("ProxyUser", 0, 0, 0, kRowHeight);
        user->SetText(saved.user);
        UltraCanvasLabel* userCaption = AddRow(grid, "ProxyUser", Tr("User"), user, 2 * kIndent); ++rows;

        auto password = CreatePasswordInput("ProxyPassword", 0, 0, 0, kRowHeight);
        password->SetText(saved.password);
        UltraCanvasLabel* passwordCaption = AddRow(grid, "ProxyPassword", Tr("Password"), password, 2 * kIndent); ++rows;

        auto rememberLine = CreateFormCellRow("ProxyRememberLine", 16.0f, kRowHeight);
        auto remember = UltraCanvasCheckbox::CreateCheckbox("ProxyRemember", 0, 0, 0, kRowHeight,
                                                            Tr("Remember"), saved.rememberPassword);
        remember->SetTooltip(Tr("Keep the password, encrypted, for the next start and for the other "
                                "applications. Otherwise only this application uses it, until it ends."));
        remember->layoutItem.SetFlexShrink(0);
        rememberLine->AddChild(remember);
        auto clearPasswords = CreateButton("ProxyClearPasswords", 0, 0, 0, kRowHeight, Tr("Clear passwords"));
        clearPasswords->size.width = CSSLayout::Dimension::Auto();
        clearPasswords->layoutItem.SetFlexShrink(0);
        rememberLine->AddChild(clearPasswords);
        auto rememberCaption = CreateFormCaption("ProxyRememberLine-caption", "");
        AddFormRow(grid, rememberCaption, rememberLine); ++rows;

        auto status = std::make_shared<UltraCanvasLabel>("ProxyStatus", -1, -1, -1, kStatusHeight, "");
        status->SetWrap(TextWrap::WrapWord);
        status->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
        AddWide(grid, status, 0);

        // The grid's own height, so the dialog is sized for it before
        // anything is measured.
        grid->size.height = CSSLayout::Dimension::Px(static_cast<float>(rows * kRowHeight + kStatusHeight) +
                                                     kRowGap * static_cast<float>(rows));
        dialog->AddDialogElement(grid);

        form->noProxy = noProxy.get();
        form->system = system.get();
        form->manual = manual.get();
        form->http = http.get();
        form->socks = socks.get();
        form->systemNote = systemNote.get();
        form->usePac = usePac.get();
        form->pacUrl = pacUrl.get();
        form->pacExample = pacExample.get();
        form->host = host.get();
        form->port = port.get();
        form->noProxyFor = noProxyFor.get();
        form->noProxyExample = noProxyExample.get();
        form->authenticate = authenticate.get();
        form->user = user.get();
        form->password = password.get();
        form->remember = remember.get();
        form->status = status.get();
        form->manualRows = {http.get(), socks.get(), host.get(), hostCaption, port.get(), portCaption,
                            noProxyFor.get(), noProxyCaption, noProxyExample.get(), authenticate.get()};
        form->authRows = {user.get(), userCaption, password.get(), passwordCaption, remember.get()};
        form->lastDefaultPort = initialPort;

        form->modeGroup.AddRadioButton(noProxy);
        form->modeGroup.AddRadioButton(system);
        form->modeGroup.AddRadioButton(manual);
        form->kindGroup.AddRadioButton(http);
        form->kindGroup.AddRadioButton(socks);
        // A file from before any choice, or with none checked: the system's.
        if (!form->modeGroup.GetSelectedButton()) form->modeGroup.SelectButton(system);
        if (!form->kindGroup.GetSelectedButton()) form->kindGroup.SelectButton(http);

        Form* f = form.get();
        form->modeGroup.onSelectionChanged = [f](std::shared_ptr<UltraCanvasRadio>) {
            f->Apply();
            f->Say(std::string(), false);
        };
        form->kindGroup.onSelectionChanged = [f](std::shared_ptr<UltraCanvasRadio>) {
            // The other kind's usual port, unless one was typed
            const int now = static_cast<int>(f->port->GetValue());
            const int usual = f->socks->IsChecked() ? 1080 : 8080;
            if (now == f->lastDefaultPort) f->port->SetValue(usual);
            f->lastDefaultPort = usual;
        };
        usePac->onStateChanged = [f](CheckedState, CheckedState) { f->Apply(); };
        authenticate->onStateChanged = [f](CheckedState, CheckedState) { f->Apply(); };
        // `form` lives as long as the dialog through this button's handler
        // (the button owns no radio, so this closes no cycle); the others
        // reach it raw.
        clearPasswords->onClick = [form]() {
            Form* f = form.get();
            const bool cleared = ProxySettings::ForgetPasswords();
            f->password->SetText("");
            f->Say(cleared ? Tr("The remembered proxy passwords were removed.")
                           : Tr("The remembered proxy passwords could not be removed."), !cleared);
        };
        form->Apply();

        // ---- Check connection, at the left of the button bar -------------
        auto check = std::make_shared<UltraCanvasButton>("ProxyCheck", 0, 0, 90, 28);
        check->SetText(Tr("Check connection"));
        check->size.width = CSSLayout::Dimension::Auto();
        check->size.height = CSSLayout::Dimension::Px(28);
        {
            CSSLayout::BoxConstraints limits = check->boxConstraints.value_or(CSSLayout::BoxConstraints{});
            limits.minWidth = CSSLayout::Dimension::Px(dialog->GetStyle().buttonWidth);
            check->boxConstraints = limits;
        }
        std::weak_ptr<UltraCanvasModalDialog> weak = dialog;
        UltraCanvasButton* checkButton = check.get();
        check->onClick = [form, dlg, weak, checkButton]() {
            Form* f = form.get();
            const std::string problem = f->Problem();
            if (!problem.empty()) {
                f->Say(problem, true);
                return;
            }
            std::string offered;
            {
                std::lock_guard<std::mutex> lk(g_checkUrlMutex);
                offered = g_checkUrl;
            }
            UltraCanvasDialogManager::ShowInputDialog(
                Tr("Address to open"), Tr("Check connection"), offered, InputType::Text,
                [form, weak, checkButton](DialogResult result, const std::string& typed) {
                    if (result != DialogResult::OK || weak.expired()) return;
                    std::string url = TrimSpace(typed);
                    if (url.empty()) return;
                    if (url.find("://") == std::string::npos) url = "https://" + url;
                    {
                        std::lock_guard<std::mutex> lk(g_checkUrlMutex);
                        g_checkUrl = url;
                    }
                    const ProxySettings settings = form->Read();
                    form->Say(TrF("Opening {0}…", url), false);
                    checkButton->SetDisabled(true);
                    std::thread([settings, url, weak, form, checkButton]() {
                        const ProxyCheckResult r = CheckProxyConnection(settings, url);
                        auto finish = [r, weak, form, checkButton]() {
                            if (weak.expired()) return;   // the dialog closed meanwhile
                            checkButton->SetDisabled(false);
                            form->Say(r.message, !r.success);
                        };
                        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(finish);
                        else finish();
                    }).detach();
                },
                dlg);
        };
        dialog->AddFooterElement(check);

        // What the system names, asked off the UI thread.
        {
            std::weak_ptr<UltraCanvasModalDialog> alive = dialog;
            std::thread([form, alive]() {
                const std::string note = SystemProxyNote();
                auto show = [form, alive, note]() {
                    if (alive.expired()) return;
                    form->systemNote->SetText(note);
                };
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(show);
                else show();
            }).detach();
        }

        // OK: refuse a setting that cannot work, else save it.
        dialog->onClosing = [f](DialogResult result) {
            if (result != DialogResult::OK) return true;
            const std::string problem = f->Problem();
            if (problem.empty()) return true;
            f->Say(problem, true);
            return false;
        };

        UltraCanvasDialogManager::ShowDialog(dialog,
            [form, onSaved](DialogResult result) {
                if (result != DialogResult::OK) return;
                ProxySettings settings = form->Read();
                if (!settings.authenticate) settings.password.clear();
                if (!settings.Save()) {
                    UltraCanvasDialogManager::ShowError(Tr("The proxy settings could not be saved."),
                                                        Tr("Proxy settings"));
                    return;
                }
                if (onSaved) onSaved(settings);
            },
            parent);
    }

    std::shared_ptr<UltraCanvasContainer> MakeProxySettingsPage(const std::string& id,
                                                                UltraCanvasWindowBase* parent) {
        using namespace SettingsPageKit;
        PageParts parts = MakePage(id + "-page", Tr("Proxy"),
                                   Tr("How the applications connect to servers on the internet:"));
        auto now = MakeText(id + "-now", DescribeProxySettings(ProxySettings::Load(/*withPassword=*/false)));
        parts.body->AddChild(now);
        auto row = MakeButtonRow(id + "-buttons");
        // The line is a child of the page the button is on: reached raw.
        UltraCanvasLabel* line = now.get();
        row->AddChild(MakeButton(id + "-change", Tr("Change"), 120, [line, parent]() {
            ShowProxySettingsDialog(parent, [line](const ProxySettings& saved) {
                line->SetText(DescribeProxySettings(saved));
            });
        }));
        parts.body->AddChild(row);
        AddNote(parts, id + "-note",
                Tr("The proxy settings are shared: every ULTRA OS application connects through "
                   "them, from its next connection on. Until a proxy is chosen here, the "
                   "system's proxy settings apply."));
        return parts.page;
    }

} // namespace UltraCanvas
