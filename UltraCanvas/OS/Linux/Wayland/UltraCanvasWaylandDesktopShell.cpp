// OS/Linux/Wayland/UltraCanvasWaylandDesktopShell.cpp
// The UltraCanvasDesktopShell backend of a Wayland session. The screens
// come from the compositor's outputs, the devices from the shared Linux
// probes, the keyboard layout from the seat's keymap; the session's end and
// lock (logind), the loudspeaker (the sound server) and the tray host
// (D-Bus) are the shared Linux files, compiled into this build too.
//
// The window list is wlr-foreign-toplevel-management-unstable-v1, the
// protocol every wlroots compositor (the one ULTRA OS ships among them)
// and KWin give a taskbar: every toplevel of every client with its title,
// app id and state, activate / minimize / close through the compositor.
// It is spoken on a connection of this backend's own, not the
// application's, as the X11 backend reads EWMH on its query connection:
// the monitor's thread blocks on it, ListWindows is called from any
// thread, and the application's event loop must not be in the middle of
// it. Everything on that connection runs under one mutex; the monitor
// polls the socket outside it and reads inside it, and a ListWindows that
// dispatched a change on the monitor's behalf nudges it through its pipe.
// The id a window is listed by is this backend's own counter, stable for
// the handle's life (the protocol numbers nothing).
//
// The reserved edges (ReserveScreenEdges) are layer-shell exclusive zones
// on the desktop window itself (UltraCanvasWaylandWindow::SetReservedEdges),
// so they go through the application: the window must be one of this
// program's Desktop windows, on the layer shell, and the call the UI
// thread's - which is where UltraDesktop makes it.
//
// The virtual desktops are ext-workspace-v1 on the same connection: the
// workspaces of the group that holds the first output (the primary, as
// ListScreens names it), in the order of their coordinates, then their
// names; the active one is the current desktop, activate + commit switches,
// create_workspace / remove set the count where the compositor allows. A
// compositor without the manager has one desktop. The protocol ties no
// toplevel to a workspace, so a window's desktop is unknown (-1) and
// MoveWindowToVirtualDesktop answers false.
//
// Screenshots and global shortcuts go through the freedesktop portals,
// in UltraCanvasWaylandPortals.cpp. A window's bounds and position are
// the compositor's alone (no protocol tells a taskbar where a window is):
// GetWindowBounds and MoveWindowTo answer false.
// Version: 0.3.0 - the virtual desktops (ext-workspace-v1); the portals in a
//                  file of their own
// Version: 0.2.0 - the window list, the monitor and the reserved edges
//                  (wlr-foreign-toplevel, wlr-layer-shell); the screens from
//                  the shell connection's outputs where there is no
//                  application; KillProcess shared; the backend entry points
//                  added since phase 1
// Version: 0.1.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasWaylandApplication.h"
#include "UltraCanvasWaylandWindow.h"
#include "../UltraCanvasLinuxDeviceProbes.h"
#include "../UltraCanvasLinuxDesktopShellInternal.h"

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "ext-workspace-v1-client-protocol.h"

#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {
    UltraCanvasWaylandApplication* App() { return UltraCanvasWaylandApplication::GetInstance(); }

    // ===== THE SHELL CONNECTION =====
    struct ForeignToplevel {
        zwlr_foreign_toplevel_handle_v1* handle = nullptr;
        uint64_t id = 0;
        std::string title;
        std::string appId;
        bool activated = false;
        bool minimized = false;
        bool maximized = false;
        bool fullscreen = false;
        bool announced = false;   // the first `done` arrived: title, app id and state are whole
    };

    // An output as the shell connection sees it, for a program that has no
    // UltraCanvasApplication (a headless --list, this backend's test):
    // logical position and size, the mode over the scale.
    struct ShellOutput {
        wl_output* output = nullptr;
        uint32_t name = 0;
        int x = 0, y = 0, pixelWidth = 0, pixelHeight = 0, scale = 1;
        bool done = false;
        int Width() const { return scale > 0 ? pixelWidth / scale : pixelWidth; }
        int Height() const { return scale > 0 ? pixelHeight / scale : pixelHeight; }
    };

    // ext-workspace-v1: a group (one per set of outputs, usually one) and
    // its workspaces, as the compositor describes them.
    struct WorkspaceGroup {
        ext_workspace_group_handle_v1* handle = nullptr;
        uint32_t capabilities = 0;
        std::vector<wl_output*> outputs;
    };
    struct Workspace {
        ext_workspace_handle_v1* handle = nullptr;
        WorkspaceGroup* group = nullptr;    // from workspace_enter; nullptr before
        uint64_t arrival = 0;               // the order the compositor announced them in
        std::string id;
        std::string name;
        std::vector<uint32_t> coordinates;
        uint32_t state = 0;
        uint32_t capabilities = 0;
    };

    struct ShellConnection {
        wl_display* display = nullptr;
        wl_registry* registry = nullptr;
        zwlr_foreign_toplevel_manager_v1* manager = nullptr;
        uint32_t managerName = 0;
        wl_seat* seat = nullptr;
        std::vector<ShellOutput> outputs;
        ext_workspace_manager_v1* workspaces = nullptr;
        uint32_t workspacesName = 0;
        bool workspacesGone = false;
        std::vector<std::unique_ptr<WorkspaceGroup>> groups;
        std::vector<std::unique_ptr<Workspace>> workspaceList;
        uint64_t nextArrival = 1;
        std::vector<std::unique_ptr<ForeignToplevel>> toplevels;
        uint64_t nextId = 1;
        bool changed = false;        // something the monitor reports happened since it last looked
        bool managerGone = false;    // the compositor withdrew the manager (`finished`)
        int monitorNudgeFd = -1;     // the monitor's pipe, while one waits
    };

    std::mutex g_shellMutex;        // everything below, and every request on the connection
    ShellConnection* g_shell = nullptr;

    void NoteChange(ShellConnection* shell) {
        shell->changed = true;
        if (shell->monitorNudgeFd >= 0) {
            const char byte = 'n';
            if (::write(shell->monitorNudgeFd, &byte, 1) < 0) {
                // Full or gone: the monitor wakes anyway, or is no more.
            }
        }
    }

    ForeignToplevel* Find(ShellConnection* shell, zwlr_foreign_toplevel_handle_v1* handle) {
        for (auto& t : shell->toplevels) {
            if (t->handle == handle) return t.get();
        }
        return nullptr;
    }

    ForeignToplevel* FindById(ShellConnection* shell, uint64_t id) {
        for (auto& t : shell->toplevels) {
            if (t->id == id) return t.get();
        }
        return nullptr;
    }

    // ----- the handle's events -----
    void HandleTitle(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* title) {
        auto* shell = static_cast<ShellConnection*>(data);
        if (auto* t = Find(shell, handle)) t->title = title ? title : "";
    }
    void HandleAppId(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* appId) {
        auto* shell = static_cast<ShellConnection*>(data);
        if (auto* t = Find(shell, handle)) t->appId = appId ? appId : "";
    }
    void HandleOutputEnter(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}
    void HandleOutputLeave(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}
    void HandleState(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_array* states) {
        auto* shell = static_cast<ShellConnection*>(data);
        auto* t = Find(shell, handle);
        if (!t) return;
        t->activated = t->minimized = t->maximized = t->fullscreen = false;
        if (!states || !states->size) return;
        const auto* begin = static_cast<const uint32_t*>(states->data);
        const auto* end = begin + states->size / sizeof(uint32_t);
        for (const uint32_t* s = begin; s != end; ++s) {
            switch (*s) {
                case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED: t->maximized = true; break;
                case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED: t->minimized = true; break;
                case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED: t->activated = true; break;
                case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN: t->fullscreen = true; break;
                default: break;
            }
        }
    }
    void HandleDone(void* data, zwlr_foreign_toplevel_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        if (auto* t = Find(shell, handle)) t->announced = true;
        NoteChange(shell);
    }
    void HandleClosed(void* data, zwlr_foreign_toplevel_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        for (auto it = shell->toplevels.begin(); it != shell->toplevels.end(); ++it) {
            if ((*it)->handle != handle) continue;
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
            shell->toplevels.erase(it);
            break;
        }
        NoteChange(shell);
    }
    void HandleParent(void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*) {}

    const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
            HandleTitle,
            HandleAppId,
            HandleOutputEnter,
            HandleOutputLeave,
            HandleState,
            HandleDone,
            HandleClosed,
            HandleParent,
    };

    // ----- the manager's events -----
    void ManagerToplevel(void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        auto t = std::make_unique<ForeignToplevel>();
        t->handle = handle;
        t->id = shell->nextId++;
        zwlr_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, shell);
        shell->toplevels.push_back(std::move(t));
        // Reported on the handle's `done`, when its title and state are in.
    }
    void ManagerFinished(void* data, zwlr_foreign_toplevel_manager_v1*) {
        auto* shell = static_cast<ShellConnection*>(data);
        shell->managerGone = true;
        NoteChange(shell);
    }
    const zwlr_foreign_toplevel_manager_v1_listener kManagerListener = {
            ManagerToplevel,
            ManagerFinished,
    };

    // ----- the workspaces -----
    WorkspaceGroup* FindGroup(ShellConnection* shell, ext_workspace_group_handle_v1* handle) {
        for (auto& g : shell->groups) {
            if (g->handle == handle) return g.get();
        }
        return nullptr;
    }
    Workspace* FindWorkspace(ShellConnection* shell, ext_workspace_handle_v1* handle) {
        for (auto& w : shell->workspaceList) {
            if (w->handle == handle) return w.get();
        }
        return nullptr;
    }
    void GroupCapabilities(void* data, ext_workspace_group_handle_v1* handle, uint32_t capabilities) {
        if (auto* g = FindGroup(static_cast<ShellConnection*>(data), handle)) g->capabilities = capabilities;
    }
    void GroupOutputEnter(void* data, ext_workspace_group_handle_v1* handle, wl_output* output) {
        if (auto* g = FindGroup(static_cast<ShellConnection*>(data), handle)) g->outputs.push_back(output);
    }
    void GroupOutputLeave(void* data, ext_workspace_group_handle_v1* handle, wl_output* output) {
        if (auto* g = FindGroup(static_cast<ShellConnection*>(data), handle)) {
            g->outputs.erase(std::remove(g->outputs.begin(), g->outputs.end(), output), g->outputs.end());
        }
    }
    void GroupWorkspaceEnter(void* data, ext_workspace_group_handle_v1* handle, ext_workspace_handle_v1* workspace) {
        auto* shell = static_cast<ShellConnection*>(data);
        if (auto* w = FindWorkspace(shell, workspace)) w->group = FindGroup(shell, handle);
    }
    void GroupWorkspaceLeave(void* data, ext_workspace_group_handle_v1* handle, ext_workspace_handle_v1* workspace) {
        auto* shell = static_cast<ShellConnection*>(data);
        if (auto* w = FindWorkspace(shell, workspace)) {
            if (w->group == FindGroup(shell, handle)) w->group = nullptr;
        }
    }
    void GroupRemoved(void* data, ext_workspace_group_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        for (auto& w : shell->workspaceList) {
            if (w->group && w->group->handle == handle) w->group = nullptr;
        }
        for (auto it = shell->groups.begin(); it != shell->groups.end(); ++it) {
            if ((*it)->handle != handle) continue;
            ext_workspace_group_handle_v1_destroy(handle);
            shell->groups.erase(it);
            break;
        }
    }
    const ext_workspace_group_handle_v1_listener kGroupListener = {
            GroupCapabilities,
            GroupOutputEnter,
            GroupOutputLeave,
            GroupWorkspaceEnter,
            GroupWorkspaceLeave,
            GroupRemoved,
    };

    void WorkspaceId(void* data, ext_workspace_handle_v1* handle, const char* id) {
        if (auto* w = FindWorkspace(static_cast<ShellConnection*>(data), handle)) w->id = id ? id : "";
    }
    void WorkspaceName(void* data, ext_workspace_handle_v1* handle, const char* name) {
        if (auto* w = FindWorkspace(static_cast<ShellConnection*>(data), handle)) w->name = name ? name : "";
    }
    void WorkspaceCoordinates(void* data, ext_workspace_handle_v1* handle, wl_array* coordinates) {
        auto* w = FindWorkspace(static_cast<ShellConnection*>(data), handle);
        if (!w) return;
        w->coordinates.clear();
        if (!coordinates || !coordinates->size) return;
        const auto* begin = static_cast<const uint32_t*>(coordinates->data);
        w->coordinates.assign(begin, begin + coordinates->size / sizeof(uint32_t));
    }
    void WorkspaceState(void* data, ext_workspace_handle_v1* handle, uint32_t state) {
        if (auto* w = FindWorkspace(static_cast<ShellConnection*>(data), handle)) w->state = state;
    }
    void WorkspaceCapabilities(void* data, ext_workspace_handle_v1* handle, uint32_t capabilities) {
        if (auto* w = FindWorkspace(static_cast<ShellConnection*>(data), handle)) w->capabilities = capabilities;
    }
    void WorkspaceRemoved(void* data, ext_workspace_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        for (auto it = shell->workspaceList.begin(); it != shell->workspaceList.end(); ++it) {
            if ((*it)->handle != handle) continue;
            ext_workspace_handle_v1_destroy(handle);
            shell->workspaceList.erase(it);
            break;
        }
    }
    const ext_workspace_handle_v1_listener kWorkspaceListener = {
            WorkspaceId,
            WorkspaceName,
            WorkspaceCoordinates,
            WorkspaceState,
            WorkspaceCapabilities,
            WorkspaceRemoved,
    };

    void WorkspacesGroup(void* data, ext_workspace_manager_v1*, ext_workspace_group_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        auto g = std::make_unique<WorkspaceGroup>();
        g->handle = handle;
        ext_workspace_group_handle_v1_add_listener(handle, &kGroupListener, shell);
        shell->groups.push_back(std::move(g));
    }
    void WorkspacesWorkspace(void* data, ext_workspace_manager_v1*, ext_workspace_handle_v1* handle) {
        auto* shell = static_cast<ShellConnection*>(data);
        auto w = std::make_unique<Workspace>();
        w->handle = handle;
        w->arrival = shell->nextArrival++;
        ext_workspace_handle_v1_add_listener(handle, &kWorkspaceListener, shell);
        shell->workspaceList.push_back(std::move(w));
    }
    void WorkspacesDone(void* data, ext_workspace_manager_v1*) {
        // Every change since the last done is in; one report for them all.
        NoteChange(static_cast<ShellConnection*>(data));
    }
    void WorkspacesFinished(void* data, ext_workspace_manager_v1*) {
        auto* shell = static_cast<ShellConnection*>(data);
        shell->workspacesGone = true;
        NoteChange(shell);
    }
    const ext_workspace_manager_v1_listener kWorkspacesListener = {
            WorkspacesGroup,
            WorkspacesWorkspace,
            WorkspacesDone,
            WorkspacesFinished,
    };

    // The virtual desktops: the workspaces of the group on the first
    // output (the primary), else of the first group, else every workspace
    // of no group; hidden ones left out; by coordinates, then by name as a
    // number, then by name, then as announced. The caller holds the mutex.
    bool Before(const Workspace* a, const Workspace* b) {
        if (a->coordinates != b->coordinates) {
            if (a->coordinates.empty() != b->coordinates.empty()) return !a->coordinates.empty();
            return a->coordinates < b->coordinates;
        }
        char* endA = nullptr;
        char* endB = nullptr;
        const long numberA = std::strtol(a->name.c_str(), &endA, 10);   // locale-ok: digits only
        const long numberB = std::strtol(b->name.c_str(), &endB, 10);   // locale-ok: digits only
        const bool numericA = !a->name.empty() && endA && *endA == 0;
        const bool numericB = !b->name.empty() && endB && *endB == 0;
        if (numericA && numericB && numberA != numberB) return numberA < numberB;
        if (numericA != numericB) return numericA;
        if (a->name != b->name) return a->name < b->name;
        return a->arrival < b->arrival;
    }
    std::vector<Workspace*> Desktops(ShellConnection* shell) {
        std::vector<Workspace*> desktops;
        if (!shell || !shell->workspaces || shell->workspacesGone) return desktops;
        WorkspaceGroup* primary = nullptr;
        wl_output* first = shell->outputs.empty() ? nullptr : shell->outputs.front().output;
        for (auto& g : shell->groups) {
            if (first && std::find(g->outputs.begin(), g->outputs.end(), first) != g->outputs.end()) {
                primary = g.get();
                break;
            }
        }
        if (!primary && !shell->groups.empty()) primary = shell->groups.front().get();
        for (auto& w : shell->workspaceList) {
            if (w->state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN) continue;
            if (w->group != primary) continue;
            desktops.push_back(w.get());
        }
        std::sort(desktops.begin(), desktops.end(), Before);
        return desktops;
    }
    WorkspaceGroup* PrimaryGroup(ShellConnection* shell) {
        const std::vector<Workspace*> desktops = Desktops(shell);
        if (!desktops.empty() && desktops.front()->group) return desktops.front()->group;
        return shell->groups.empty() ? nullptr : shell->groups.front().get();
    }

    // ----- the outputs -----
    ShellOutput* FindOutput(ShellConnection* shell, wl_output* output) {
        for (auto& o : shell->outputs) {
            if (o.output == output) return &o;
        }
        return nullptr;
    }
    void OutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t, int32_t, int32_t,
                        const char*, const char*, int32_t) {
        if (auto* o = FindOutput(static_cast<ShellConnection*>(data), output)) {
            o->x = x;
            o->y = y;
        }
    }
    void OutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t) {
        if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
        if (auto* o = FindOutput(static_cast<ShellConnection*>(data), output)) {
            o->pixelWidth = width;
            o->pixelHeight = height;
        }
    }
    void OutputDone(void* data, wl_output* output) {
        if (auto* o = FindOutput(static_cast<ShellConnection*>(data), output)) o->done = true;
    }
    void OutputScale(void* data, wl_output* output, int32_t factor) {
        if (auto* o = FindOutput(static_cast<ShellConnection*>(data), output)) o->scale = factor > 0 ? factor : 1;
    }
    void OutputName(void*, wl_output*, const char*) {}
    void OutputDescription(void*, wl_output*, const char*) {}
    const wl_output_listener kOutputListener = {
            OutputGeometry,
            OutputMode,
            OutputDone,
            OutputScale,
            OutputName,
            OutputDescription,
    };

    // ----- the registry -----
    void RegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto* shell = static_cast<ShellConnection*>(data);
        const std::string what = interface ? interface : "";
        if (what == wl_output_interface.name) {
            ShellOutput o;
            o.output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min<uint32_t>(version, 4)));
            o.name = name;
            shell->outputs.push_back(o);
            wl_output_add_listener(shell->outputs.back().output, &kOutputListener, shell);
        } else if (what == zwlr_foreign_toplevel_manager_v1_interface.name && !shell->manager) {
            shell->manager = static_cast<zwlr_foreign_toplevel_manager_v1*>(
                    wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min<uint32_t>(version, 3)));
            shell->managerName = name;
            zwlr_foreign_toplevel_manager_v1_add_listener(shell->manager, &kManagerListener, shell);
        } else if (what == wl_seat_interface.name && !shell->seat) {
            // The seat an activation names; its devices are not wanted here.
            shell->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        } else if (what == ext_workspace_manager_v1_interface.name && !shell->workspaces) {
            shell->workspaces = static_cast<ext_workspace_manager_v1*>(
                    wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, 1));
            shell->workspacesName = name;
            ext_workspace_manager_v1_add_listener(shell->workspaces, &kWorkspacesListener, shell);
        }
    }
    void RegistryGlobalRemove(void* data, wl_registry*, uint32_t name) {
        auto* shell = static_cast<ShellConnection*>(data);
        if (shell->manager && name == shell->managerName) {
            shell->managerGone = true;
            NoteChange(shell);
        }
        if (shell->workspaces && name == shell->workspacesName) {
            shell->workspacesGone = true;
            NoteChange(shell);
        }
        for (auto it = shell->outputs.begin(); it != shell->outputs.end(); ++it) {
            if (it->name != name) continue;
            wl_output_release(it->output);
            shell->outputs.erase(it);
            break;
        }
    }
    const wl_registry_listener kRegistryListener = {
            RegistryGlobal,
            RegistryGlobalRemove,
    };

    void CloseShell(ShellConnection* shell) {
        if (!shell) return;
        for (auto& t : shell->toplevels) {
            if (t->handle) zwlr_foreign_toplevel_handle_v1_destroy(t->handle);
        }
        shell->toplevels.clear();
        if (shell->manager) {
            if (!shell->managerGone) zwlr_foreign_toplevel_manager_v1_stop(shell->manager);
            zwlr_foreign_toplevel_manager_v1_destroy(shell->manager);
        }
        for (auto& w : shell->workspaceList) {
            if (w->handle) ext_workspace_handle_v1_destroy(w->handle);
        }
        shell->workspaceList.clear();
        for (auto& g : shell->groups) {
            if (g->handle) ext_workspace_group_handle_v1_destroy(g->handle);
        }
        shell->groups.clear();
        if (shell->workspaces) {
            if (!shell->workspacesGone) ext_workspace_manager_v1_stop(shell->workspaces);
            ext_workspace_manager_v1_destroy(shell->workspaces);
        }
        if (shell->seat) wl_seat_destroy(shell->seat);
        for (auto& o : shell->outputs) {
            if (o.output) wl_output_release(o.output);
        }
        shell->outputs.clear();
        if (shell->registry) wl_registry_destroy(shell->registry);
        if (shell->display) {
            wl_display_flush(shell->display);
            wl_display_disconnect(shell->display);
        }
        delete shell;
    }

    // The connection, opened on first use (the caller holds g_shellMutex).
    // nullptr where there is no compositor to connect to - tried again on
    // the next call, a failed connect being one socket call, so a program
    // whose WAYLAND_DISPLAY was not yet set, or whose compositor restarted,
    // gets there later; a compositor without the manager connects, with an
    // empty, unchanging list.
    ShellConnection* Shell() {
        if (g_shell) return g_shell;
        wl_display* display = wl_display_connect(nullptr);
        if (!display) return nullptr;
        auto* shell = new ShellConnection();
        shell->display = display;
        shell->registry = wl_display_get_registry(display);
        wl_registry_add_listener(shell->registry, &kRegistryListener, shell);
        // One round trip binds the manager, the next brings the toplevels it
        // already knows and their first `done`.
        if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0) {
            CloseShell(shell);
            return nullptr;
        }
        shell->changed = false;
        g_shell = shell;
        return g_shell;
    }

    // Whatever the compositor sent since: a round trip (the caller holds
    // g_shellMutex). False when the connection is gone, which drops it;
    // the next call connects anew.
    bool Sync() {
        ShellConnection* shell = Shell();
        if (!shell) return false;
        if (wl_display_roundtrip(shell->display) < 0) {
            CloseShell(shell);
            g_shell = nullptr;
            return false;
        }
        return true;
    }

    // The pipe a Wake writes to and a Wait drains, both ends non-blocking
    // (as the X11 backend's: a drain must not wait for a byte).
    bool OpenWakePipe(int fds[2]) {
        if (::pipe(fds) != 0) return false;
        for (int i = 0; i < 2; ++i) {
            const int flags = ::fcntl(fds[i], F_GETFL);
            if (flags < 0 || ::fcntl(fds[i], F_SETFL, flags | O_NONBLOCK) < 0) {
                ::close(fds[0]);
                ::close(fds[1]);
                fds[0] = fds[1] = -1;
                return false;
            }
            ::fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        }
        return true;
    }
} // namespace

// ===== WHAT THE TRAY HOST SHARES =====
namespace X11 {
    bool QueryPointerPosition(int& x, int& y) {
        // There is no screen position on Wayland; the last place the pointer
        // was in one of our windows is the best a tray click can get.
        auto* app = App();
        if (app && app->GetLastPointerPosition(x, y)) return true;
        x = y = 0;
        return false;
    }
}

// ===== BACKEND =====

std::string BackendName() { return "wayland"; }

bool IsAvailable() {
    auto* app = App();
    if (app && app->GetDisplay() != nullptr) return true;
    std::lock_guard<std::mutex> lock(g_shellMutex);
    return Shell() != nullptr;
}

std::vector<DesktopWindowInfo> ListWindows() {
    std::vector<DesktopWindowInfo> windows;
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync()) return windows;
    for (const auto& t : g_shell->toplevels) {
        if (!t->announced) continue;
        DesktopWindowInfo info;
        info.id = t->id;
        info.title = t->title;
        // The app id is what a Wayland client has in place of WM_CLASS: the
        // desktop entry's name ("ultrafiler", "org.gnome.Nautilus"), which
        // the core resolves to the icon as it does a class.
        info.appClass = t->appId;
        info.appName = t->appId;
        info.virtualDesktop = -1;   // ext-workspace is the next step
        info.processId = 0;         // the protocol names no process
        info.active = t->activated;
        info.minimized = t->minimized;
        info.maximized = t->maximized;
        info.skipTaskbar = false;   // the compositor lists only what a taskbar shows
        windows.push_back(info);
    }
    return windows;
}

uint64_t GetActiveWindow() {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync()) return 0;
    for (const auto& t : g_shell->toplevels) {
        if (t->announced && t->activated) return t->id;
    }
    return 0;
}

bool ActivateWindow(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync() || !g_shell->seat) return false;
    ForeignToplevel* t = FindById(g_shell, id);
    if (!t) return false;
    // Activation brings a minimized window back as well; the compositor
    // decides, as the manager does on X11.
    zwlr_foreign_toplevel_handle_v1_unset_minimized(t->handle);
    zwlr_foreign_toplevel_handle_v1_activate(t->handle, g_shell->seat);
    wl_display_flush(g_shell->display);
    return true;
}

bool MinimizeWindow(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync()) return false;
    ForeignToplevel* t = FindById(g_shell, id);
    if (!t) return false;
    zwlr_foreign_toplevel_handle_v1_set_minimized(t->handle);
    wl_display_flush(g_shell->display);
    return true;
}

bool CloseWindow(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync()) return false;
    ForeignToplevel* t = FindById(g_shell, id);
    if (!t) return false;
    zwlr_foreign_toplevel_handle_v1_close(t->handle);
    wl_display_flush(g_shell->display);
    return true;
}

bool GetWindowBounds(uint64_t, int&, int&, int&, int&) {
    // No protocol tells a taskbar where another client's window is: a
    // Wayland client's position is the compositor's alone.
    return false;
}

bool MoveWindowTo(uint64_t, int, int) { return false; }

int GetVirtualDesktopCount() {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync()) return 1;
    const std::vector<Workspace*> desktops = Desktops(g_shell);
    return desktops.empty() ? 1 : static_cast<int>(desktops.size());
}

int GetCurrentVirtualDesktop() {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync()) return 0;
    const std::vector<Workspace*> desktops = Desktops(g_shell);
    for (size_t i = 0; i < desktops.size(); ++i) {
        if (desktops[i]->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) return static_cast<int>(i);
    }
    return 0;
}

bool SetCurrentVirtualDesktop(int index) {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync() || index < 0) return false;
    const std::vector<Workspace*> desktops = Desktops(g_shell);
    if (static_cast<size_t>(index) >= desktops.size()) return false;
    Workspace* target = desktops[static_cast<size_t>(index)];
    if (!(target->capabilities & EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE)) return false;
    ext_workspace_handle_v1_activate(target->handle);
    ext_workspace_manager_v1_commit(g_shell->workspaces);
    wl_display_flush(g_shell->display);
    return true;
}

bool SetVirtualDesktopCount(int count) {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    if (!Sync() || count < 1) return false;
    std::vector<Workspace*> desktops = Desktops(g_shell);
    const int have = static_cast<int>(desktops.size());
    if (count == have) return true;
    bool changed = false;
    if (count > have) {
        // New ones named by their number, as the compositors that let a
        // client create workspaces name theirs.
        WorkspaceGroup* group = PrimaryGroup(g_shell);
        if (!group || !(group->capabilities & EXT_WORKSPACE_GROUP_HANDLE_V1_GROUP_CAPABILITIES_CREATE_WORKSPACE)) {
            return false;
        }
        for (int n = have + 1; n <= count; ++n) {
            ext_workspace_group_handle_v1_create_workspace(group->handle, std::to_string(n).c_str());
            changed = true;
        }
    } else {
        // The last ones go, the active one never.
        for (int i = have - 1; i >= count; --i) {
            Workspace* w = desktops[static_cast<size_t>(i)];
            if (w->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) return false;
            if (!(w->capabilities & EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_REMOVE)) return false;
            ext_workspace_handle_v1_remove(w->handle);
            changed = true;
        }
    }
    if (changed) {
        ext_workspace_manager_v1_commit(g_shell->workspaces);
        wl_display_flush(g_shell->display);
    }
    return changed;
}

// ext-workspace ties no toplevel to a workspace, and wlr-foreign-toplevel
// knows none: a window's desktop cannot be set from here.
bool MoveWindowToVirtualDesktop(uint64_t, int) { return false; }

// The screens: the application's outputs, or the shell connection's for a
// program without an application. Wayland names no primary; the first
// the compositor announced is it.
std::vector<DesktopScreenInfo> ListScreens() {
    std::vector<DesktopScreenInfo> screens;
    auto push = [&screens](int x, int y, int width, int height) {
        if (width <= 0 || height <= 0) return;
        DesktopScreenInfo screen;
        screen.x = x;
        screen.y = y;
        screen.width = width;
        screen.height = height;
        screens.push_back(screen);
    };
    if (auto* app = App()) {
        for (const auto& output : app->GetOutputs()) push(output.x, output.y, output.Width(), output.Height());
    } else {
        std::lock_guard<std::mutex> lock(g_shellMutex);
        if (Sync()) {
            for (const auto& output : g_shell->outputs) {
                if (output.done) push(output.x, output.y, output.Width(), output.Height());
            }
        }
    }
    if (!screens.empty()) screens.front().primary = true;
    return screens;
}

bool GetScreenSize(int& width, int& height) {
    width = height = 0;
    if (auto* app = App()) {
        app->GetScreenSize(width, height);
    } else {
        // The largest screen, as the application's GetScreenSize answers.
        for (const DesktopScreenInfo& screen : ListScreens()) {
            if (static_cast<long>(screen.width) * screen.height > static_cast<long>(width) * height) {
                width = screen.width;
                height = screen.height;
            }
        }
    }
    return width > 0 && height > 0;
}

bool ReserveScreenEdges(uint64_t id, int left, int right, int top, int bottom) {
    auto* app = App();
    if (!app || id == 0) return false;
    auto* window = dynamic_cast<UltraCanvasWaylandWindow*>(app->FindWindow(reinterpret_cast<NativeWindowHandle>(id)));
    if (!window || !window->IsLayerSurface()) return false;
    // The caller measures in physical pixels, as the X11 strut does; the
    // layer shell in the output's logical ones. Rounded up, so a bar is
    // never short a pixel.
    const float scale = std::max(1.0f, window->GetDeviceScale());
    auto logical = [scale](int physical) {
        return physical <= 0 ? 0 : static_cast<int>(std::ceil(static_cast<float>(physical) / scale));
    };
    return window->SetReservedEdges(logical(left), logical(right), logical(top), logical(bottom));
}

// Desktop entries are this platform's application list; the core reads them.
bool ListNativeApplications(int, std::vector<UCDesktopEntry>&) { return false; }
bool LaunchShortcut(const std::string& shortcutPath, std::string& error) {
    return FileAssociations::OpenWithDefaultApplication({shortcutPath}, error);
}
bool LaunchPackagedApplication(const std::string&, std::string& error) {
    error = "Microsoft Store applications run on Windows only.";
    return false;
}

bool CanReplaceSystemDesktop(DesktopReplacement) { return false; }
DesktopReplacement GetSystemDesktopReplacement() { return DesktopReplacement::Off; }
bool ReplaceSystemDesktop(const std::string&, const std::vector<std::string>&, DesktopReplacement,
                          std::string& error) {
    error = "On this system the session starts the desktop; choose UltraDesktop's session at sign-in.";
    return false;
}
bool RestoreSystemDesktop(std::string&) { return true; }
bool IsSystemDesktopRunning() { return true; }
std::string SystemDesktopSwitchUnavailableReason() { return {}; }

void ReadDeviceActivity(DesktopDeviceActivity& activity) {
    LinuxProbes::ProbeAudio(activity);
    LinuxProbes::ProbeCamera(activity);
    LinuxProbes::ProbeBattery(activity);
    if (auto* app = App()) {
        const std::string layout = app->GetKeyboardLayoutName();
        if (!layout.empty()) activity.keyboardLayout = layout;
    }
}

// ===== THE MONITOR =====
// Wakes on the shell connection's events: a toplevel announced, changed or
// closed, the manager withdrawn. The outputs are the application's and
// change through its own loop.
struct MonitorState {
    int wakePipe[2] = {-1, -1};
};

MonitorState* MonitorOpen() {
    std::lock_guard<std::mutex> lock(g_shellMutex);
    ShellConnection* shell = Shell();
    if (!shell || (!shell->manager && !shell->workspaces)) return nullptr;   // nothing to notify about
    auto* state = new MonitorState();
    if (!OpenWakePipe(state->wakePipe)) {
        delete state;
        return nullptr;
    }
    shell->monitorNudgeFd = state->wakePipe[1];
    shell->changed = false;
    return state;
}

bool MonitorWait(MonitorState* state) {
    if (!state) return false;
    for (;;) {
        int fd = -1;
        {
            std::lock_guard<std::mutex> lock(g_shellMutex);
            ShellConnection* shell = g_shell;
            if (!shell) return false;
            // Read what the socket holds, dispatch it, and send what is
            // queued, all inside the lock (another thread may be in a
            // round trip between two of these calls, which is fine: a read
            // of an emptied socket does not block).
            while (wl_display_prepare_read(shell->display) != 0) {
                wl_display_dispatch_pending(shell->display);
            }
            wl_display_read_events(shell->display);
            wl_display_dispatch_pending(shell->display);
            wl_display_flush(shell->display);
            if (wl_display_get_error(shell->display) != 0) return false;
            if (shell->changed) {
                shell->changed = false;
                return true;
            }
            fd = wl_display_get_fd(shell->display);
        }
        pollfd fds[2];
        fds[0].fd = fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = state->wakePipe[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        const int n = ::poll(fds, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (fds[1].revents & POLLIN) {
            // 's' stops the monitor; 'n' says a round trip elsewhere
            // dispatched a change, which the loop above now reports.
            bool stop = false;
            char byte;
            while (::read(state->wakePipe[0], &byte, 1) > 0) {
                if (byte == 's') stop = true;
            }
            if (stop) return false;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
    }
}

void MonitorWake(MonitorState* state) {
    if (!state || state->wakePipe[1] < 0) return;
    const char byte = 's';
    if (::write(state->wakePipe[1], &byte, 1) < 0) {
        // Nothing to do: the reader is gone, or the pipe is full of wakes.
    }
}

void MonitorClose(MonitorState* state) {
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(g_shellMutex);
        if (g_shell && g_shell->monitorNudgeFd == state->wakePipe[1]) g_shell->monitorNudgeFd = -1;
    }
    if (state->wakePipe[0] >= 0) ::close(state->wakePipe[0]);
    if (state->wakePipe[1] >= 0) ::close(state->wakePipe[1]);
    delete state;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
