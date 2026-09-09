// Apps/UltraWinManager/UltraWinManagerWindow.cpp
// Implementation of the UltraWin Manager window. See the header for the
// tab layout; the construction pattern follows the UltraAI dashboard
// (fixed-geometry window, catalogue elements as children), and the
// worker/PostToUIThread pattern follows UltraFiler's launch handler.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinManagerWindow.h"

#include <cstdlib>
#include <thread>

namespace UltraWinManager {

using namespace UltraCanvas;

namespace {

// Consistent metrics for the fixed layout.
constexpr long kPad = 16;
constexpr long kHeader = 64;
constexpr long kStatusH = 26;
constexpr long kTabH = 640 - kHeader - kStatusH - kPad;  // inside window
constexpr long kBtnH = 30;

std::shared_ptr<UltraCanvasButton> MakeButton(
    const std::string& id, long x, long y, long w, const std::string& text,
    std::function<void()> onClick) {
    auto b = std::make_shared<UltraCanvasButton>(id, x, y, w, kBtnH);
    b->SetText(text);
    b->onClick = std::move(onClick);
    return b;
}

}  // namespace

ManagerWindow::ManagerWindow(UltraCanvasApplication& app) : app_(app) {}

ManagerWindow::~ManagerWindow() { alive_->store(false); }

bool ManagerWindow::Create() {
    WindowConfig cfg;
    cfg.title = "UltraWin Manager";
    cfg.width = kW;
    cfg.height = kH;
    cfg.x = 140;
    cfg.y = 100;
    cfg.resizable = false;
    cfg.type = WindowType::Standard;
    window_ = CreateWindow(cfg);
    if (!window_) return false;
    window_->onWindowClosing = [this]() {
        alive_->store(false);
        app_.RequestExit();
        return true;
    };

    auto title = std::make_shared<UltraCanvasLabel>(
        "uwm-title", kPad, 14, kW - 2 * kPad, 22,
        "UltraWin Manager — Windows applications on ULTRA OS");
    window_->AddChild(title);

    auto caps = UltraWin_GetCapabilities();
    std::string capLine =
        std::string("Wine: ") + (caps.wineAvailable ? caps.wineVersion
                                                    : "not installed") +
        "   ·   winetricks: " + (caps.winetricksAvailable ? "yes" : "no") +
        "   ·   QEMU: " + (caps.qemuAvailable ? "yes" : "no") +
        "   ·   KVM: " + (caps.kvmAvailable ? "yes" : "no") +
        "   ·   RemoteApp: " + (caps.remoteAppSupported ? "yes" : "no");
    auto capLabel = std::make_shared<UltraCanvasLabel>(
        "uwm-caps", kPad, 38, kW - 2 * kPad, 18, capLine);
    window_->AddChild(capLabel);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "uwm-tabs", kPad, kHeader, kW - 2 * kPad, kTabH);
    tabs->AddTab("Environments", BuildEnvironmentsTab());
    tabs->AddTab("Programs", BuildProgramsTab());
    tabs->AddTab("Windows VM", BuildVmTab());
    window_->AddChild(tabs);

    statusLabel_ = std::make_shared<UltraCanvasLabel>(
        "uwm-status", kPad, kH - kStatusH - 6, kW - 2 * kPad, kStatusH,
        caps.wineAvailable
            ? "Ready."
            : "Wine is not installed — install it (e.g. 'sudo apt install "
              "wine') to run Windows applications.");
    window_->AddChild(statusLabel_);

    RefreshEnvironments();
    RefreshPrograms();
    RefreshVmPanel();
    return true;
}

void ManagerWindow::Show() {
    if (window_) window_->Show();
}

void ManagerWindow::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

// ===========================================================================
// Environments tab
// ===========================================================================

std::shared_ptr<UltraCanvasContainer> ManagerWindow::BuildEnvironmentsTab() {
    const long w = kW - 2 * kPad;
    auto page = std::make_shared<UltraCanvasContainer>(
        "uwm-env-page", 0, 0, static_cast<float>(w),
        static_cast<float>(kTabH - 32));

    const long listW = 300;
    const long listH = kTabH - 32 - 2 * kPad - kBtnH - 8;

    envModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    envModel_->SetColumns({{"Environment", 190, TextAlignment::Left},
                           {"Ready", 70, TextAlignment::Left}});
    envList_ = std::make_shared<UltraCanvasListView>(
        "uwm-env-list", kPad, kPad, listW, listH);
    envList_->SetModel(envModel_);
    envList_->onSelectionChanged = [this](const std::vector<int>&) {
        RefreshComponents();
    };
    page->AddChild(envList_);

    page->AddChild(MakeButton("uwm-env-refresh", kPad, kPad + listH + 8, 96,
                              "Refresh", [this]() {
                                  RefreshEnvironments();
                                  RefreshPrograms();
                              }));
    page->AddChild(MakeButton(
        "uwm-env-delete", kPad + 104, kPad + listH + 8, 196,
        "Delete environment", [this]() {
            const std::string env = SelectedEnvironment();
            if (env.empty()) {
                SetStatus("Select an environment first.");
                return;
            }
            RunWorker("Deleting " + env + "…",
                      [env]() { return UltraWin_DeleteEnvironment(env); },
                      [this]() {
                          RefreshEnvironments();
                          RefreshPrograms();
                      });
        }));

    // Right side: components of the selected environment + actions.
    const long rx = kPad + listW + kPad;
    const long rw = w - rx - kPad;

    auto compTitle = std::make_shared<UltraCanvasLabel>(
        "uwm-comp-title", rx, kPad, rw, 18,
        "Components in the selected environment (winetricks verbs):");
    page->AddChild(compTitle);

    const long compH = listH - 150;
    compModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    compModel_->SetColumns({{"Component", 250, TextAlignment::Left}});
    compList_ = std::make_shared<UltraCanvasListView>(
        "uwm-comp-list", rx, kPad + 24, rw, compH);
    compList_->SetModel(compModel_);
    page->AddChild(compList_);

    long y = kPad + 24 + compH + 12;
    compInput_ = std::make_shared<UltraCanvasTextInput>(
        "uwm-comp-input", rx, y, rw - 130, kBtnH);
    compInput_->SetPlaceholder("component verb, e.g. vcrun2019, corefonts");
    page->AddChild(compInput_);
    page->AddChild(MakeButton(
        "uwm-comp-install", rx + rw - 120, y, 120, "Install", [this]() {
            const std::string env = SelectedEnvironment();
            const std::string verb = compInput_->GetText();
            if (env.empty() || verb.empty()) {
                SetStatus("Select an environment and enter a component.");
                return;
            }
            RunWorker("Installing " + verb + " into " + env +
                          " — downloads can take minutes…",
                      [env, verb]() {
                          return UltraWin_InstallComponent(env, verb);
                      },
                      [this]() { RefreshComponents(); });
        }));

    y += kBtnH + 12;
    exeInput_ = std::make_shared<UltraCanvasTextInput>(
        "uwm-exe-input", rx, y, rw - 130, kBtnH);
    exeInput_->SetPlaceholder(
        "path to a .exe / .msi to run in the selected environment");
    page->AddChild(exeInput_);
    page->AddChild(MakeButton(
        "uwm-exe-run", rx + rw - 120, y, 120, "Launch", [this]() {
            const std::string env = SelectedEnvironment();
            const std::string path = exeInput_->GetText();
            if (path.empty()) {
                SetStatus("Enter the path of a Windows program.");
                return;
            }
            RunWorker(
                "Launching " + path +
                    (env.empty() ? "" : " in " + env) +
                    " — a first launch prepares its environment…",
                [env, path]() {
                    UltraWinRunOptions opt;
                    opt.environment = env;  // "" = per-app default
                    UltraWinHandle h = UltraWinInvalidHandle;
                    return UltraWin_RunApp(path, opt, &h);
                },
                [this]() {
                    RefreshEnvironments();
                    RefreshPrograms();
                });
        }));

    return page;
}

void ManagerWindow::RefreshEnvironments() {
    envModel_->Clear();
    envNames_.clear();
    for (const auto& env : UltraWin_ListEnvironments()) {
        envNames_.push_back(env.name);
        envModel_->AddItem(MultiColumnListItem(
            {env.name, env.initialized ? "yes" : "…"}));
    }
    RefreshComponents();
}

std::string ManagerWindow::SelectedEnvironment() const {
    if (!envList_ || !envList_->GetSelection()) return {};
    int row = envList_->GetSelection()->GetCurrentRow();
    if (row < 0 || row >= static_cast<int>(envNames_.size())) return {};
    return envNames_[static_cast<size_t>(row)];
}

void ManagerWindow::RefreshComponents() {
    compModel_->Clear();
    const std::string env = SelectedEnvironment();
    if (env.empty()) return;
    for (const auto& c : UltraWin_ListComponents(env))
        compModel_->AddItem(MultiColumnListItem({c}));
}

// ===========================================================================
// Programs tab
// ===========================================================================

std::shared_ptr<UltraCanvasContainer> ManagerWindow::BuildProgramsTab() {
    const long w = kW - 2 * kPad;
    auto page = std::make_shared<UltraCanvasContainer>(
        "uwm-prog-page", 0, 0, static_cast<float>(w),
        static_cast<float>(kTabH - 32));

    const long listH = kTabH - 32 - 2 * kPad - kBtnH - 8;
    progModel_ = std::make_shared<UltraCanvasMultiColumnListModel>();
    progModel_->SetColumns({{"Program", 260, TextAlignment::Left},
                            {"Category", 200, TextAlignment::Left},
                            {"Environment", 180, TextAlignment::Left}});
    progList_ = std::make_shared<UltraCanvasListView>(
        "uwm-prog-list", kPad, kPad, w - 2 * kPad, listH);
    progList_->SetModel(progModel_);
    auto launchSelected = [this]() {
        if (!progList_->GetSelection()) return;
        int row = progList_->GetSelection()->GetCurrentRow();
        if (row < 0 || row >= static_cast<int>(progPaths_.size())) {
            SetStatus("Select a program first.");
            return;
        }
        const std::string lnk = progPaths_[static_cast<size_t>(row)];
        const std::string env = progEnvs_[static_cast<size_t>(row)];
        RunWorker("Launching…",
                  [lnk, env]() {
                      // The shortcut only resolves inside the environment
                      // whose prefix holds it.
                      UltraWinRunOptions opt;
                      opt.environment = env;
                      UltraWinHandle h = UltraWinInvalidHandle;
                      return UltraWin_RunApp(lnk, opt, &h);
                  },
                  []() {});
    };
    progList_->onItemActivated = [launchSelected](int) { launchSelected(); };
    page->AddChild(progList_);

    page->AddChild(MakeButton("uwm-prog-refresh", kPad, kPad + listH + 8, 96,
                              "Refresh",
                              [this]() { RefreshPrograms(); }));
    page->AddChild(MakeButton("uwm-prog-launch", kPad + 104,
                              kPad + listH + 8, 120, "Launch",
                              launchSelected));
    return page;
}

void ManagerWindow::RefreshPrograms() {
    progModel_->Clear();
    progPaths_.clear();
    progEnvs_.clear();
    for (const auto& env : UltraWin_ListEnvironments()) {
        for (const auto& p : UltraWin_ListPrograms(env.name)) {
            progPaths_.push_back(p.shortcutPath);
            progEnvs_.push_back(p.environment);
            progModel_->AddItem(MultiColumnListItem(
                {p.name, p.category, p.environment}));
        }
    }
}

// ===========================================================================
// Windows VM tab
// ===========================================================================

std::shared_ptr<UltraCanvasContainer> ManagerWindow::BuildVmTab() {
    const long w = kW - 2 * kPad;
    auto page = std::make_shared<UltraCanvasContainer>(
        "uwm-vm-page", 0, 0, static_cast<float>(w),
        static_cast<float>(kTabH - 32));

    vmStateLabel_ = std::make_shared<UltraCanvasLabel>(
        "uwm-vm-state", kPad, kPad, w - 2 * kPad, 20, "Machine: …");
    page->AddChild(vmStateLabel_);
    vmDetailLabel_ = std::make_shared<UltraCanvasLabel>(
        "uwm-vm-detail", kPad, kPad + 24, w - 2 * kPad, 18, "");
    page->AddChild(vmDetailLabel_);

    long y = kPad + 60;
    auto provTitle = std::make_shared<UltraCanvasLabel>(
        "uwm-vm-prov-title", kPad, y, w - 2 * kPad, 18,
        "Provision — a Windows 10/11 Pro ISO (your license) and the "
        "virtio-win drivers ISO:");
    page->AddChild(provTitle);

    y += 26;
    winIsoInput_ = std::make_shared<UltraCanvasTextInput>(
        "uwm-vm-winiso", kPad, y, w - 2 * kPad - 130, kBtnH);
    winIsoInput_->SetPlaceholder("/path/to/Win11_Pro.iso");
    page->AddChild(winIsoInput_);
    y += kBtnH + 8;
    driversIsoInput_ = std::make_shared<UltraCanvasTextInput>(
        "uwm-vm-drviso", kPad, y, w - 2 * kPad - 130, kBtnH);
    driversIsoInput_->SetPlaceholder("/path/to/virtio-win.iso");
    page->AddChild(driversIsoInput_);
    page->AddChild(MakeButton(
        "uwm-vm-provision", kPad + w - 2 * kPad - 120, y, 120, "Provision",
        [this]() {
            UltraWinVmOptions opt;
            opt.windowsIsoPath = winIsoInput_->GetText();
            opt.driversIsoPath = driversIsoInput_->GetText();
            RunWorker("Provisioning the machine…",
                      [opt]() { return UltraWin_VmProvision(opt); },
                      [this]() { RefreshVmPanel(); });
        }));

    y += kBtnH + 20;
    struct VmAction {
        const char* id;
        const char* label;
        UltraWinResult (*call)();
    };
    const VmAction actions[] = {
        {"uwm-vm-start", "Start", &UltraWin_VmStart},
        {"uwm-vm-suspend", "Suspend", &UltraWin_VmSuspend},
        {"uwm-vm-resume", "Resume", &UltraWin_VmResume},
        {"uwm-vm-stop", "Stop", &UltraWin_VmStop},
    };
    long x = kPad;
    for (const auto& a : actions) {
        auto call = a.call;
        page->AddChild(MakeButton(a.id, x, y, 110,
                                  a.label, [this, call, a]() {
                                      RunWorker(std::string(a.label) + "…",
                                                [call]() { return call(); },
                                                [this]() { RefreshVmPanel(); });
                                  }));
        x += 118;
    }
    page->AddChild(MakeButton("uwm-vm-refresh", x, y, 110, "Refresh",
                              [this]() { RefreshVmPanel(); }));

    y += kBtnH + 24;
    auto runTitle = std::make_shared<UltraCanvasLabel>(
        "uwm-vm-run-title", kPad, y, w - 2 * kPad, 18,
        "Launch into the guest (guest path C:\\…, ||alias, or a host path "
        "under your home):");
    page->AddChild(runTitle);
    y += 24;
    vmRunInput_ = std::make_shared<UltraCanvasTextInput>(
        "uwm-vm-run", kPad, y, w - 2 * kPad - 130, kBtnH);
    vmRunInput_->SetPlaceholder("||notepad");
    page->AddChild(vmRunInput_);
    page->AddChild(MakeButton(
        "uwm-vm-run-btn", kPad + w - 2 * kPad - 120, y, 120, "Launch",
        [this]() {
            const std::string program = vmRunInput_->GetText();
            if (program.empty()) {
                SetStatus("Enter a program to launch in the guest.");
                return;
            }
            RunWorker("Connecting the RemoteApp session…",
                      [program]() {
                          UltraWinRunOptions opt;
                          opt.forceTier = UltraWinTier::Vm;
                          UltraWinHandle h = UltraWinInvalidHandle;
                          return UltraWin_RunApp(program, opt, &h);
                      },
                      [this]() { RefreshVmPanel(); });
        }));

    return page;
}

void ManagerWindow::RefreshVmPanel() {
    // State queries can block on QMP / the RDP probe — worker thread.
    auto alive = alive_;
    std::thread([this, alive]() {
        UltraWinVmInfo info;
        auto r = UltraWin_VmGetInfo(&info);
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, alive, r, info]() {
            if (!alive->load() || !vmStateLabel_) return;
            if (!r) {
                vmStateLabel_->SetText("Machine: " + r.message);
                return;
            }
            const char* state = "?";
            switch (info.state) {
                case UltraWinVmState::NotProvisioned: state = "not provisioned"; break;
                case UltraWinVmState::Stopped: state = "stopped"; break;
                case UltraWinVmState::Running: state = "running"; break;
                case UltraWinVmState::Suspended: state = "suspended"; break;
                case UltraWinVmState::Unknown: state = "unknown"; break;
            }
            vmStateLabel_->SetText(
                std::string("Machine: ") + state +
                (info.windowsInstalled
                     ? "  ·  Windows installed"
                     : "  ·  Windows not installed yet (boots install "
                       "media)"));
            std::string detail = "Directory: " + info.vmDirectory;
            if (info.qemuPid > 0) {
                detail += "  ·  QEMU pid " + std::to_string(info.qemuPid) +
                          (info.kvm ? " (KVM)" : " (TCG)") +
                          "  ·  RDP 127.0.0.1:" +
                          std::to_string(info.rdpHostPort) +
                          (info.homeShared ? "  ·  home shared" : "");
            }
            vmDetailLabel_->SetText(detail);
        });
    }).detach();
}

// ===========================================================================
// Worker plumbing
// ===========================================================================

void ManagerWindow::RunWorker(const std::string& busyText,
                              std::function<UltraWinResult()> work,
                              std::function<void()> refreshAfter) {
    bool expected = false;
    if (!busy_->compare_exchange_strong(expected, true)) {
        SetStatus("Another operation is still running — one at a time.");
        return;
    }
    SetStatus(busyText);
    auto alive = alive_;
    auto busy = busy_;
    std::thread([this, alive, busy, work = std::move(work),
                 refreshAfter = std::move(refreshAfter)]() {
        UltraWinResult r = work();
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        busy->store(false);
        if (!app) return;
        app->PostToUIThread([this, alive, r, refreshAfter]() {
            if (!alive->load()) return;
            SetStatus(r ? "Done." : "Error: " + r.message);
            if (r && refreshAfter) refreshAfter();
        });
    }).detach();
}

}  // namespace UltraWinManager
