// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.7.0 - a mouse press the element under the pointer does not take
//                  climbs to the elements around it (DispatchPressToAncestors)
// Version: 1.6.0 - Windows: the system fonts are scanned on a thread; the start uses
//                  the bundled fonts when the scan takes long (AdoptSystemFontsWithin);
//                  Initialize() times each of its steps (GetStartupTimings)
// Version: 1.5.2 - modal fixes: close transient children with parent, ignore unmapped modals, raise modal on outside click
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include <algorithm>
#include <cmath>
#include <atomic>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#if !defined(__APPLE__)
#include <fontconfig/fontconfig.h>
#endif
#if defined(ULTRACANVAS_HAS_PANGOFT2)
#include <pango/pangofc-fontmap.h>
#endif
#include <pango/pangocairo.h>
#include "UltraCanvasPathUtf8.h"
#if !defined(__APPLE__)
#include "../libspecific/Cairo/RenderContextCairo.h"   // the text caches, after the font set changed
#endif
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(__linux__) || defined(__unix__)
#include <unistd.h>
#include <climits>   // PATH_MAX (<linux/limits.h> does not exist on musl/Emscripten)
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include "UltraCanvasDebug.h"
#endif


namespace UltraCanvas {
    namespace {
        // The element a mouse press landed on and the ones around it, below
        // the window, while that press is handed out. A stack, because a
        // handler can run a nested event loop (a modal dialog) that hands out
        // presses of its own. ~UltraCanvasUIElement nulls its entries
        // (CleanupElementReferences), so a press never reaches an element a
        // handler before it destroyed.
        std::vector<std::vector<UltraCanvasUIElement*>*>& PressChains() {
            static std::vector<std::vector<UltraCanvasUIElement*>*> chains;
            return chains;
        }

        // A press the element under the pointer did not take, handed to the
        // elements around it, innermost first, until one takes it. `chain`
        // is that element followed by its ancestors below the window.
        bool DispatchPressToAncestors(UltraCanvasApplicationBase& app,
                                      const std::vector<UltraCanvasUIElement*>& chain,
                                      const UCEvent& event) {
            for (size_t i = 1; i < chain.size(); ++i) {
                UltraCanvasUIElement* below = chain[i - 1];
                UltraCanvasUIElement* ancestor = chain[i];
                // Destroyed by a handler so far (CleanupElementReferences
                // nulled it), or no longer where the press found it - a click
                // that closed its row, say: the press has done what it was
                // for, and the elements it left must not act on it too.
                if (!below || !ancestor || below->GetParentContainer() != ancestor) return false;
                if (app.DispatchEventToElement(ancestor, event)) return true;
            }
            return false;
        }
    }


    // ===== Singleton accessor for cross-thread PostToUIThread =====
    namespace {
        std::atomic<UltraCanvasApplicationBase*> g_currentApplication{nullptr};
    }

    UltraCanvasApplicationBase::UltraCanvasApplicationBase() {
        // Latch the most-recently-constructed app as the "current" one;
        // UltraCanvas assumes one app per process.
        g_currentApplication.store(this, std::memory_order_release);
        memset(keyStates, 0, sizeof(keyStates));
    }

    UltraCanvasApplicationBase::~UltraCanvasApplicationBase() {
        UltraCanvasApplicationBase* expected = this;
        g_currentApplication.compare_exchange_strong(
            expected, nullptr, std::memory_order_release);
    }

    UltraCanvasApplicationBase* UltraCanvasApplicationBase::GetCurrent() {
        return g_currentApplication.load(std::memory_order_acquire);
    }

    void UltraCanvasApplicationBase::PostToUIThread(std::function<void()> task) {
        if (!task) return;
        {
            std::lock_guard<std::mutex> lk(postedTasksMutex_);
            postedTasks_.push_back(std::move(task));
        }
        // Wake the loop so the task runs promptly instead of waiting for
        // the next OS event.
        WakeUpEventLoop();
    }

    void UltraCanvasApplicationBase::ProcessPostedTasks() {
        // Swap under lock so concurrent posts can keep enqueueing while we
        // run tasks lock-free. Tasks running here might re-enter
        // PostToUIThread to chain follow-up work; that lands in the next
        // iteration, never the current vector.
        std::vector<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lk(postedTasksMutex_);
            local.swap(postedTasks_);
        }
        for (auto& fn : local) {
            try {
                fn();
            } catch (const std::exception& e) {
                std::cerr << "UltraCanvas PostToUIThread task threw: "
                          << e.what() << std::endl;
            } catch (...) {
                std::cerr << "UltraCanvas PostToUIThread task threw "
                             "non-std exception" << std::endl;
            }
        }
    }

    // ===== FILE-DESCRIPTOR WATCHES =====
    // These let a host integration (e.g. Ladybird's IPC to its WebContent process)
    // have its sockets serviced by the toolkit's own event loop. The platform loop
    // (Linux: CollectAndProcessNativeEvents) folds the watched fds into its select()
    // set and calls FireFdWatch() for each fd that became ready.

    FdWatchId UltraCanvasApplicationBase::AddFdWatch(int fd, FdWatchType type, std::function<void()> callback) {
        std::lock_guard<std::mutex> lk(fdWatchesMutex_);
        FdWatchId id = nextFdWatchId_++;
        fdWatches_.push_back(FdWatch { id, fd, type, std::move(callback) });
        // Wake the loop so the new fd is added to the wait set on the next iteration
        // rather than only after the current (possibly indefinite) select() returns.
        WakeUpEventLoop();
        return id;
    }

    void UltraCanvasApplicationBase::RemoveFdWatch(FdWatchId id) {
        std::lock_guard<std::mutex> lk(fdWatchesMutex_);
        std::erase_if(fdWatches_, [id](const FdWatch& w) { return w.id == id; });
    }

    std::vector<UltraCanvasApplicationBase::FdWatchKey>
    UltraCanvasApplicationBase::SnapshotFdWatchKeys() const {
        std::lock_guard<std::mutex> lk(fdWatchesMutex_);
        std::vector<FdWatchKey> keys;
        keys.reserve(fdWatches_.size());
        for (const auto& w : fdWatches_)
            keys.push_back(FdWatchKey { w.id, w.fd, w.type });
        return keys;
    }

    void UltraCanvasApplicationBase::FireFdWatch(FdWatchId id) {
        // Copy the callback out under the lock, then invoke it unlocked so it can
        // safely re-enter AddFdWatch/RemoveFdWatch (e.g. an IPC handler that closes
        // a connection removes its own watch).
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> lk(fdWatchesMutex_);
            for (const auto& w : fdWatches_) {
                if (w.id == id) {
                    callback = w.callback;
                    break;
                }
            }
        }
        if (!callback)
            return;
        try {
            callback();
        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas FdWatch callback threw: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "UltraCanvas FdWatch callback threw non-std exception" << std::endl;
        }
    }

    const char* const kEmbeddedAllFonts[] = {
        "Ubuntu-R.ttf", "Ubuntu-B.ttf",
        "Ubuntu-RI.ttf", "Ubuntu-BI.ttf",
        "Ubuntu-C.ttf", "Ubuntu-L.ttf",
        "Ubuntu-M.ttf", "Ubuntu-LI.ttf",
        "Ubuntu-MI.ttf", "Ubuntu-Th.ttf",
        "UbuntuMono-R.ttf", "UbuntuMono-B.ttf",
        "UbuntuMono-RI.ttf", "UbuntuMono-BI.ttf",
        "OpenSans-Bold.ttf", "OpenSans-BoldItalic.ttf",
//        "OpenSans-Italic.ttf", "OpenSans-Regular.ttf",
//        "OpenSans-Bold.ttf", "OpenSans-BoldItalic.ttf",
//        "OpenSans-CondBold.ttf", "OpenSans-CondLight.ttf",
//        "OpenSans-CondLightItalic.ttf", "OpenSans-ExtraBold.ttf",
//        "OpenSans-Light.ttf", "OpenSans-LightItalic.ttf",
//        "OpenSans-Semibold.ttf", "OpenSans-SemiboldItalic.ttf",
    };
    const size_t kEmbeddedAllFontsCount = sizeof(kEmbeddedAllFonts) / sizeof(kEmbeddedAllFonts[0]);

    const char* const kEmbeddedMonoFonts[] = {
            "UbuntuMono-R.ttf", "UbuntuMono-B.ttf",
            "UbuntuMono-RI.ttf", "UbuntuMono-BI.ttf",
    };
    const size_t kEmbeddedMonoFontsCount = sizeof(kEmbeddedMonoFonts) / sizeof(kEmbeddedMonoFonts[0]);

    std::string GetBundledFontsDir() {
        std::string p = NormalizePath(GetResourcesDir() + "media/fonts/");
        return p;
    }

#if !defined(__APPLE__)
    // ===== RUNTIME FONTCONFIG BOOTSTRAP =====
    // Packaged builds ship no fonts.conf (the MSYS2 Windows ZIP most
    // visibly), so the very first fontconfig call made from inside
    // Pango/cairo/vips prints
    //     Fontconfig error: Cannot load default config file: No such file: (null)
    // on stderr and leaves the process with a config that knows no font
    // directory at all. The helpers below detect that before fontconfig is
    // first touched and write a minimal config for FONTCONFIG_FILE to point
    // at, so no error is printed and font matching has real directories to
    // work with.
    namespace {
        bool FontconfigFileExists(const std::string& path) {
            if (path.empty()) return false;
            std::error_code ec;
            return std::filesystem::exists(UltraCanvas::PathFromUtf8(path), ec);
        }

        std::vector<std::string> SplitSearchPath(const char* value) {
#if defined(_WIN32) || defined(_WIN64)
            const char separator = ';';
#else
            const char separator = ':';
#endif
            std::vector<std::string> parts;
            if (!value) return parts;
            std::string current;
            for (const char* c = value; *c; ++c) {
                if (*c == separator) {
                    if (!current.empty()) parts.push_back(current);
                    current.clear();
                } else {
                    current.push_back(*c);
                }
            }
            if (!current.empty()) parts.push_back(current);
            return parts;
        }

        // Mirrors fontconfig's own lookup closely enough to answer "would
        // FcInit() find a config file?" without calling into fontconfig (any
        // FcConfig* call would already trigger the failing init we want to
        // avoid). Order: FONTCONFIG_FILE, FONTCONFIG_PATH entries, then the
        // platform's built-in location. On Windows that location is derived
        // from the libfontconfig DLL directory (<dir>\etc\fonts, with a
        // trailing "bin"/"lib" component stripped), which for a packaged app
        // is the executable directory.
        bool SystemFontconfigConfigFound() {
            const char* explicitFile = std::getenv("FONTCONFIG_FILE");
            if (explicitFile && *explicitFile && FontconfigFileExists(explicitFile)) {
                return true;
            }

            std::vector<std::string> dirs = SplitSearchPath(std::getenv("FONTCONFIG_PATH"));
            const std::string exeDir = GetExecutableDir();
#if defined(_WIN32) || defined(_WIN64)
            dirs.push_back(exeDir + "/etc/fonts");
            dirs.push_back(exeDir + "/../etc/fonts");
#else
            dirs.push_back("/etc/fonts");
            dirs.push_back("/usr/local/etc/fonts");
            dirs.push_back(exeDir + "/../etc/fonts");
#endif
            for (const std::string& dir : dirs) {
                if (FontconfigFileExists(dir + "/fonts.conf")) return true;
            }
            return false;
        }

        // fontconfig understands forward slashes on every platform; keeping
        // backslashes out of the generated XML avoids any escaping question.
        std::string ToFontconfigPath(const std::string& in) {
            std::string out = in;
            std::replace(out.begin(), out.end(), '\\', '/');
            while (out.size() > 1 && out.back() == '/') out.pop_back();
            return out;
        }

        std::string XmlEscape(const std::string& in) {
            std::string out;
            out.reserve(in.size());
            for (char c : in) {
                switch (c) {
                    case '&':  out += "&amp;";  break;
                    case '<':  out += "&lt;";   break;
                    case '>':  out += "&gt;";   break;
                    case '"':  out += "&quot;"; break;
                    case '\'': out += "&apos;"; break;
                    default:   out += c;        break;
                }
            }
            return out;
        }

        // Writable directory for the generated config. The app directory is
        // deliberately not used - a packaged app may live under Program Files
        // or a read-only mount.
        std::string RuntimeFontconfigDir() {
#if defined(_WIN32) || defined(_WIN64)
            // UTF-8 from the wide environment: the directory is created and
            // the file written through PathFromUtf8. (fontconfig is then given
            // the file's name as described where FONTCONFIG_FILE is set.)
            for (const char* name : { "LOCALAPPDATA", "TEMP", "TMP" }) {
                const std::string root = GetEnvUtf8(name);
                if (!root.empty()) return root + "/UltraCanvas/fontconfig";
            }
            return {};
#else
            if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
                if (*xdg) return std::string(xdg) + "/UltraCanvas/fontconfig";
            }
            if (const char* home = std::getenv("HOME")) {
                if (*home) return std::string(home) + "/.cache/UltraCanvas/fontconfig";
            }
#if defined(__ANDROID__)
            // No /tmp in the app sandbox. HOME/TMPDIR are exported by the
            // android_main glue (files dir / cache dir); if neither is set
            // there is no writable location to offer.
            if (const char* tmp = std::getenv("TMPDIR")) {
                if (*tmp) return std::string(tmp) + "/UltraCanvas-fontconfig";
            }
            return {};
#else
            return "/tmp/UltraCanvas-fontconfig";
#endif
#endif
        }

        // Deliberately free of rendering rules (antialias/hinting/lcdfilter):
        // this config only replaces a *missing* one, so it must not change how
        // text looks compared to a system that has its own fonts.conf.
        // `withSystemFonts` false (Windows only): the bundled fonts alone, for
        // the start while the system fonts are scanned in the background.
        std::string GenerateFontsConf(bool withSystemFonts = true) {
            std::string bundledDir;
            {
                const std::string dir = GetBundledFontsDir();
                std::error_code ec;
                if (std::filesystem::is_directory(UltraCanvas::PathFromUtf8(dir), ec)) {
                    bundledDir = XmlEscape(ToFontconfigPath(dir));
                }
            }

            std::ostringstream conf;
            conf << "<?xml version=\"1.0\"?>\n"
                    "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
                    "<!-- Generated by UltraCanvas: no system fonts.conf was found. -->\n"
                    "<fontconfig>\n";

            if (!bundledDir.empty()) {
                conf << "  <dir>" << bundledDir << "</dir>\n";
            }
#if defined(_WIN32) || defined(_WIN64)
            // WINDOWSFONTDIR / WINDOWSUSERFONTDIR / LOCAL_APPDATA_FONTCONFIG_CACHE
            // are fontconfig's built-in Windows keywords.
            if (withSystemFonts) {
                conf << "  <dir>WINDOWSFONTDIR</dir>\n"
                        "  <dir>WINDOWSUSERFONTDIR</dir>\n";
            }
            conf << "  <cachedir>LOCAL_APPDATA_FONTCONFIG_CACHE</cachedir>\n";
            const char* const kSans[] = { "Ubuntu", "Segoe UI", "Tahoma", "Arial" };
            const char* const kSerif[] = { "Times New Roman", "Georgia" };
            const char* const kMono[] = { "Ubuntu Mono", "Consolas", "Courier New" };
#elif defined(__ANDROID__)
            // System fonts live in fixed directories (no /etc/fonts at all,
            // so this generated config is always the active one on Android).
            conf << "  <dir>/system/fonts</dir>\n"
                    "  <dir>/product/fonts</dir>\n"
                    "  <cachedir prefix=\"xdg\">fontconfig</cachedir>\n"
                    "  <cachedir>~/.fontconfig</cachedir>\n";
            const char* const kSans[] = { "Roboto", "Noto Sans", "Droid Sans" };
            const char* const kSerif[] = { "Noto Serif", "Droid Serif" };
            const char* const kMono[] = { "Droid Sans Mono", "Roboto Mono", "Cutive Mono" };
#else
            conf << "  <dir>/usr/share/fonts</dir>\n"
                    "  <dir>/usr/local/share/fonts</dir>\n"
                    "  <dir prefix=\"xdg\">fonts</dir>\n"
                    "  <dir>~/.fonts</dir>\n"
                    "  <cachedir prefix=\"xdg\">fontconfig</cachedir>\n"
                    "  <cachedir>~/.fontconfig</cachedir>\n"
                    "  <include ignore_missing=\"yes\">/etc/fonts/conf.d</include>\n";
            const char* const kSans[] = { "Ubuntu", "DejaVu Sans", "Liberation Sans", "Noto Sans" };
            const char* const kSerif[] = { "DejaVu Serif", "Liberation Serif", "Noto Serif" };
            const char* const kMono[] = { "Ubuntu Mono", "DejaVu Sans Mono", "Liberation Mono" };
#endif
            // Generic family aliases - without a system config nothing maps
            // "sans-serif"/"monospace" (what Pango asks for by default) onto a
            // real family, and every request falls back to the first font
            // fontconfig happens to list.
            struct GenericAlias {
                const char* generic;
                const char* const* preferred;
                size_t preferredCount;
            };
            const GenericAlias aliases[] = {
                { "sans-serif", kSans,  sizeof(kSans) / sizeof(kSans[0]) },
                { "sans",       kSans,  sizeof(kSans) / sizeof(kSans[0]) },
                { "serif",      kSerif, sizeof(kSerif) / sizeof(kSerif[0]) },
                { "monospace",  kMono,  sizeof(kMono) / sizeof(kMono[0]) },
            };
            for (const GenericAlias& alias : aliases) {
                conf << "  <alias>\n"
                     << "    <family>" << alias.generic << "</family>\n"
                     << "    <prefer>\n";
                for (size_t i = 0; i < alias.preferredCount; ++i) {
                    conf << "      <family>" << alias.preferred[i] << "</family>\n";
                }
                conf << "    </prefer>\n"
                     << "  </alias>\n";
            }

            conf << "</fontconfig>\n";
            return conf.str();
        }
    } // namespace
#endif // !__APPLE__

#if !defined(__APPLE__)
    namespace {
        // The system fonts' scan (SetupBundledFontconfig, Windows): one per process,
        // on a thread of its own. Leaked on purpose - the thread may still be
        // scanning when the process ends.
        struct SystemFontScan {
            std::mutex              mutex;
            std::condition_variable ended;
            SystemFontScanStatus    status;
            FcConfig*               config = nullptr;   // the full set, until it is made current
            bool                    switchWhenDone = false;   // the start went on without it
            std::function<void(const SystemFontScanStatus&)> onSwitched;
        };

        SystemFontScan& FontScan() {
            static SystemFontScan* scan = new SystemFontScan();
            return *scan;
        }

        // Layout and text caches of every element of a window: the next
        // frame measures all text again, in the fonts now available.
        void InvalidateLayoutTree(UltraCanvasUIElement* element) {
            if (!element) return;
            element->InvalidateLayout();
            if (auto* container = dynamic_cast<UltraCanvasContainer*>(element)) {
                for (const auto& child : container->GetChildren()) InvalidateLayoutTree(child.get());
            }
        }

        // UI thread: the full set becomes the current one and every window is
        // laid out and drawn again with it.
        void SwitchToSystemFonts() {
            SystemFontScan& scan = FontScan();
            FcConfig* config = nullptr;
            SystemFontScanStatus status;
            std::function<void(const SystemFontScanStatus&)> onSwitched;
            {
                std::lock_guard<std::mutex> lock(scan.mutex);
                config = scan.config;
                scan.config = nullptr;
                status = scan.status;
                onSwitched = scan.onSwitched;
            }
            if (!config) {
                debugOutput << "UltraCanvas: the system font scan failed; the bundled fonts stay"
                            << std::endl;
                return;
            }
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            // Fonts registered at run time are application fonts of the set in
            // use; the new set gets them too (the bundled fonts are a <dir> of it).
            if (app) {
                for (const std::string& fontFile : app->GetRegisteredFontFiles())
                    FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(fontFile.c_str()));
            }
            FcConfigSetCurrent(config);
            FcConfigDestroy(config);   // the current-config reference keeps it
#if defined(ULTRACANVAS_HAS_PANGOFT2)
            PangoFontMap* fontMap = pango_cairo_font_map_get_default();
            if (fontMap && PANGO_IS_FC_FONT_MAP(fontMap)) {
                pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(fontMap), FcConfigGetCurrent());
            } else {
                RefreshFontConfiguration();
            }
#else
            RefreshFontConfiguration();
#endif
            RenderContextCairo::InvalidateAllFontMetricsCaches();
            if (app) {
                for (const auto& window : app->GetWindows()) InvalidateLayoutTree(window.get());
            }
            debugOutput << "UltraCanvas: system fonts ready after "
                        << static_cast<long long>(status.ms + 0.5)
                        << " ms; the windows now use them" << std::endl;
            if (onSwitched) onSwitched(status);
        }

        // The scan itself, on its thread: the full config parsed from memory
        // (no path for a code page to get wrong) and its fonts read - from
        // fontconfig's cache when it is warm, from every font file when not.
        [[maybe_unused]] void StartSystemFontScan(const std::string& fullConf) {
            SystemFontScan& scan = FontScan();
            {
                std::lock_guard<std::mutex> lock(scan.mutex);
                if (scan.status.started) return;
                scan.status.started = true;
            }
            std::thread([fullConf]() {
                const auto start = std::chrono::steady_clock::now();
                FcConfig* config = FcConfigCreate();
                const bool ok = config &&
                    FcConfigParseAndLoadFromMemory(config, reinterpret_cast<const FcChar8*>(fullConf.c_str()),
                                                   FcTrue) &&
                    FcConfigBuildFonts(config);
                if (!ok && config) {
                    FcConfigDestroy(config);
                    config = nullptr;
                }
                SystemFontScan& scan = FontScan();
                bool post = false;
                {
                    std::lock_guard<std::mutex> lock(scan.mutex);
                    scan.config = config;
                    scan.status.finished = true;
                    scan.status.succeeded = ok;
                    scan.status.ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
                    post = scan.switchWhenDone;
                }
                scan.ended.notify_all();
                if (!post) return;   // the start waits for it, or has not asked yet
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                    app->PostToUIThread([]() { SwitchToSystemFonts(); });
                }
            }).detach();
        }
    } // namespace
#endif

    bool AdoptSystemFontsWithin(int waitMs) {
#if !defined(__APPLE__)
        SystemFontScan& scan = FontScan();
        FcConfig* config = nullptr;
        {
            std::unique_lock<std::mutex> lock(scan.mutex);
            if (!scan.status.started) return true;
            if (scan.status.adoptedAtStart || scan.switchWhenDone) return scan.status.adoptedAtStart;
            scan.ended.wait_for(lock, std::chrono::milliseconds(waitMs > 0 ? waitMs : 0),
                                [&scan]() { return scan.status.finished; });
            if (!scan.status.finished) {
                // The start goes on with the bundled fonts; the scan's end
                // switches the windows over.
                scan.switchWhenDone = true;
                debugOutput << "UltraCanvas: the system fonts are still being scanned; "
                               "starting with the bundled fonts" << std::endl;
                return false;
            }
            config = scan.config;
            scan.config = nullptr;
            scan.status.adoptedAtStart = config != nullptr;
        }
        if (!config) {
            debugOutput << "UltraCanvas: the system font scan failed; starting with the "
                           "bundled fonts" << std::endl;
            return false;
        }
        FcConfigSetCurrent(config);
        FcConfigDestroy(config);   // the current-config reference keeps it
        return true;
#else
        (void)waitMs;
        return true;
#endif
    }

    SystemFontScanStatus GetSystemFontScanStatus() {
#if !defined(__APPLE__)
        SystemFontScan& scan = FontScan();
        std::lock_guard<std::mutex> lock(scan.mutex);
        return scan.status;
#else
        return {};
#endif
    }

    void SetSystemFontsSwitchedHandler(std::function<void(const SystemFontScanStatus&)> handler) {
#if !defined(__APPLE__)
        SystemFontScan& scan = FontScan();
        std::lock_guard<std::mutex> lock(scan.mutex);
        scan.onSwitched = std::move(handler);
#else
        (void)handler;
#endif
    }

    void SetupBundledFontconfig() {
#if defined(__APPLE__)
        // macOS renders text through CoreText - fontconfig is not in the stack.
#else
        if (SystemFontconfigConfigFound()) return;

        const std::string dir = RuntimeFontconfigDir();
        if (dir.empty()) {
            debugOutput << "UltraCanvas: no writable directory for a runtime "
                           "fonts.conf" << std::endl;
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(UltraCanvas::PathFromUtf8(dir), ec);
        if (ec) {
            debugOutput << "UltraCanvas: cannot create " << dir << ": "
                        << ec.message() << std::endl;
            return;
        }

        // Rewrite only when stale - the baked-in bundled-fonts path changes
        // whenever the application is moved, and rewriting invalidates
        // fontconfig's cache for that config.
        auto writeConf = [](const std::string& file, const std::string& contents) {
            bool needsWrite = true;
            {
                std::ifstream existing(UltraCanvas::PathFromUtf8(file), std::ios::binary);
                if (existing) {
                    std::ostringstream current;
                    current << existing.rdbuf();
                    needsWrite = (current.str() != contents);
                }
            }
            if (!needsWrite) return true;
            std::ofstream out(UltraCanvas::PathFromUtf8(file), std::ios::binary | std::ios::trunc);
            if (!out) {
                debugOutput << "UltraCanvas: cannot write runtime fonts.conf to "
                            << file << std::endl;
                return false;
            }
            out << contents;
            if (!out) {
                debugOutput << "UltraCanvas: failed writing runtime fonts.conf to "
                            << file << std::endl;
                return false;
            }
            return true;
        };

        std::string file = dir + "/fonts.conf";
        const std::string contents = GenerateFontsConf();
        if (!writeConf(file, contents)) return;

#if defined(_WIN32) || defined(_WIN64)
        // The bundled fonts alone for the start, the system fonts scanned on
        // a thread (see AdoptSystemFontsWithin) - only with bundled fonts to
        // start with: without them the start would have no font at all.
        {
            std::error_code fontsEc;
            if (std::filesystem::is_directory(UltraCanvas::PathFromUtf8(GetBundledFontsDir()), fontsEc)) {
                const std::string startupFile = dir + "/fonts-startup.conf";
                if (writeConf(startupFile, GenerateFontsConf(/*withSystemFonts=*/false))) {
                    file = startupFile;
                    StartSystemFontScan(contents);
                }
            }
        }
#endif

        // fontconfig reads FONTCONFIG_FILE through the narrow CRT getenv(), so
        // set it through the CRT (not SetEnvironmentVariable) and keep it UTF-8
        // - UltraCanvas executables embed a manifest selecting the UTF-8 active
        // code page, so a non-ASCII path survives the round trip.
#if defined(_WIN32) || defined(_WIN64)
        const bool environmentSet = (_putenv_s("FONTCONFIG_FILE", file.c_str()) == 0);
#else
        const bool environmentSet = (setenv("FONTCONFIG_FILE", file.c_str(), 1) == 0);
#endif
        if (!environmentSet) {
            debugOutput << "UltraCanvas: could not set FONTCONFIG_FILE=" << file << std::endl;
            return;
        }
        debugOutput << "UltraCanvas: no system fonts.conf found, using generated "
                    << file << std::endl;
#endif
    }


    // ===== RUNTIME FONT REGISTRATION =====

    void RefreshFontConfiguration() {
#if !defined(__APPLE__)
        // FcConfigAppFontAddFile only records the file; the FontSet FcMatch
        // (and therefore Pango) searches is materialised by FcConfigBuildFonts.
        if (FcConfig* cfg = FcConfigGetCurrent()) FcConfigBuildFonts(cfg);
#endif
#if defined(ULTRACANVAS_HAS_PANGOFT2)
        // The font map caches what it has already matched, and every live
        // PangoContext holds a reference to it - so replacing the default map
        // would leave existing windows on the old one. config_changed clears
        // the caches of the map in place instead, which is what makes a
        // just-registered family resolvable in the very next layout.
        PangoFontMap* fontMap = pango_cairo_font_map_get_default();
        if (fontMap && PANGO_IS_FC_FONT_MAP(fontMap)) {
            pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(fontMap));
            return;
        }
#endif
        // No FontConfig-backed map to signal - macOS, where PangoCoreTextFontMap
        // enumerates the installed families once when it is constructed, and any
        // build without PangoFT2. The only way to see a newly registered family
        // is then a new map: dropping the default has the next
        // pango_cairo_font_map_get_default() build one. Contexts that already
        // exist keep the map they hold until their surface is recreated.
        pango_cairo_font_map_set_default(nullptr);
    }

    bool UltraCanvasApplicationBase::RegisterFontFile(const std::string& fontFilePath) {
        if (fontFilePath.empty()) return false;

        std::error_code ec;
        if (!std::filesystem::exists(UltraCanvas::PathFromUtf8(fontFilePath), ec) || ec) {
            debugOutput << "UltraCanvas: RegisterFontFile: no such file: "
                        << fontFilePath << std::endl;
            return false;
        }

        // Resolved so that two spellings of one file - a relative path and an
        // absolute one, a symlink and its target - are recognised as the same
        // registration rather than handed to the platform twice.
        std::string key = PathToUtf8(std::filesystem::weakly_canonical(UltraCanvas::PathFromUtf8(fontFilePath), ec));
        if (ec || key.empty()) key = fontFilePath;

        {
            std::lock_guard<std::mutex> lk(registeredFontsMutex_);
            if (std::find(registeredFontFiles_.begin(), registeredFontFiles_.end(),
                          key) != registeredFontFiles_.end()) {
                return true;
            }
        }

        if (!RegisterFontFileNative(fontFilePath)) {
            debugOutput << "UltraCanvas: RegisterFontFile: platform rejected "
                        << fontFilePath << std::endl;
            return false;
        }
        RefreshFontConfiguration();

        std::lock_guard<std::mutex> lk(registeredFontsMutex_);
        registeredFontFiles_.push_back(std::move(key));
        return true;
    }

    bool UltraCanvasApplicationBase::IsFontFileRegistered(
            const std::string& fontFilePath) const {
        if (fontFilePath.empty()) return false;
        std::error_code ec;
        std::string key = PathToUtf8(std::filesystem::weakly_canonical(UltraCanvas::PathFromUtf8(fontFilePath), ec));
        if (ec || key.empty()) key = fontFilePath;
        std::lock_guard<std::mutex> lk(registeredFontsMutex_);
        return std::find(registeredFontFiles_.begin(), registeredFontFiles_.end(),
                         key) != registeredFontFiles_.end();
    }

    std::vector<std::string> UltraCanvasApplicationBase::GetRegisteredFontFiles() const {
        std::lock_guard<std::mutex> lk(registeredFontsMutex_);
        return registeredFontFiles_;
    }

    FontStyle UltraCanvasApplicationBase::GetSystemFontStyle() {
        if (!cachedSystemFontStyle_.has_value()) {
            cachedSystemFontStyle_ = DetectSystemFontStyleNative();
        }
        return cachedSystemFontStyle_.value();
    }

    FontStyle UltraCanvasApplicationBase::GetDefaultMonospacedFontStyle() {
        if (!cachedMonospacedFontStyle_.has_value()) {
            cachedMonospacedFontStyle_ = DetectMonospacedFontStyleNative();
        }
        return cachedMonospacedFontStyle_.value();
    }

    bool UltraCanvasApplicationBase::Initialize(const std::string& app) {
        appName = app;

        // Each step's time, for GetStartupTimings(): a slow start is usually
        // one of these (a font cache rebuilt, a virus scanner reading every
        // library), and an application cannot see inside this call.
        startupTimings.clear();
        auto stepStart = std::chrono::steady_clock::now();
        auto endStep = [this, &stepStart](const char* stage) {
            const auto now = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - stepStart).count();
            startupTimings.push_back({stage, ms});
            debugOutput << "UltraCanvas: startup step " << stage << " took "
                        << static_cast<long long>(ms + 0.5) << " ms" << std::endl;
            stepStart = now;
        };

        // Must run before anything can touch fontconfig - the image subsystem
        // (vips -> pango) and every native backend below do.
        SetupBundledFontconfig();
        endStep("fontconfig setup");

        UCImage::InitializeImageSubsysterm(appName.c_str());
        endStep("image subsystem");

        const bool nativeOk = InitializeNative();
        endStep("native backend");
        if (nativeOk) {
            // Register bundled DejaVu fonts before any text rendering / default
            // detection runs, so platform Detect*FontStyleNative() can return
            // the just-registered families.
            LoadBundledFontsNative();
            endStep("bundled and system fonts");

            if (!InitializeClipboard()) {
                debugOutput << "UltraCanvas: Failed to initialize clipboard" << std::endl;
            }
            endStep("clipboard");

            // Auto-set default window icon if available
            std::string iconPath = GetDefaultIcon();
            if (std::filesystem::exists(UltraCanvas::PathFromUtf8(iconPath))) {
                SetDefaultWindowIcon(iconPath);
                debugOutput << "UltraCanvas: Default window icon set to: " << iconPath << std::endl;
            }
#ifdef UCAPP_ICON_PATH
            else {
                // Fallback to app-specific icon defined at build time
                std::string appIconPath = NormalizePath(GetResourcesDir() + UCAPP_ICON_PATH);
                if (std::filesystem::exists(UltraCanvas::PathFromUtf8(appIconPath))) {
                    SetDefaultWindowIcon(appIconPath);
                    debugOutput << "UltraCanvas: App icon set to: " << appIconPath << std::endl;
                } else {
                    debugOutput << "UltraCanvas: App icon not found at: " << appIconPath << std::endl;
                }
            }
#else
            else {
                debugOutput << "UltraCanvas: Default icon not found at: " << iconPath << std::endl;
            }
#endif
            endStep("default window icon");

            return true;
        } else {
            debugOutput << "UltraCanvas: Failed to initialize application" << std::endl;
            return false;
        }
    }

    namespace {
        // Set from a signal handler, read by the loop. A lock-free atomic
        // store is the whole of what the handler does.
        std::atomic<bool> g_exitRequestedFromSignal{false};
    }

    void UltraCanvasApplicationBase::RequestExitFromSignal() {
        g_exitRequestedFromSignal.store(true, std::memory_order_relaxed);
    }

    void UltraCanvasApplicationBase::RunOnce() {
        // Service native events (X11 + wakeup + registered fd watches), then drain
        // the UI event queue, fire timers, run PostToUIThread tasks, and render.
        // Factored out of Run() so a host embedding UltraCanvas under its own event
        // loop (e.g. Ladybird's Core::EventLoop bridge) can drive one iteration.
        // A signal's exit request is honoured here, on the main thread, where
        // logging and the exit-request callback are safe.
        if (g_exitRequestedFromSignal.exchange(false, std::memory_order_relaxed)) {
            RequestExit();
            if (!running) return;
        }
        CollectAndProcessNativeEvents();
        ProcessEvents();
        ProcessTimers();
        ProcessPostedTasks();

        std::erase_if(windows, [](const auto& w) {
            return (w->GetState() == WindowState::Closed && w->GetConfig().deleteOnClose);
        });

        for (auto it = windows.begin(); it != windows.end(); it++) {
            auto window = it->get();
            if (window->IsVisible()) {
                window->UpdateAndRender();
            }
        }

        // Clean up stale modal windows (expired weak_ptrs)
        activeModalWindows.erase(
            std::remove_if(activeModalWindows.begin(), activeModalWindows.end(),
                [](const std::weak_ptr<UltraCanvasWindowBase>& w) {
                    return w.expired();
                }),
            activeModalWindows.end());

        if (auto clipbrd = GetClipboard())
            clipbrd->Update();
        if (eventLoopCallback)
            eventLoopCallback();

        RunInEventLoop();
    }

    void UltraCanvasApplicationBase::Run() {
        debugOutput << "UltraCanvasBaseApplication::Run Starting app" << std::endl;
        if (!initialized) {
            debugOutput << "UltraCanvas: Cannot run - application not initialized" << std::endl;
            return;
        }

        running = true;

        // Start the event processing thread
        RunBeforeMainLoop();

        debugOutput << "UltraCanvas: Starting main loop..." << std::endl;
        try {
            while (running && !windows.empty()) {
                RunOnce();
            }
        } catch (const std::exception& e) {
            debugOutput << "UltraCanvas: Exception in main loop: " << e.what() << std::endl;
        }

        // Clean shutdown
        debugOutput << "UltraCanvas: Main loop ended, performing cleanup..." << std::endl;
        //StopEventThread();

        debugOutput << "UltraCanvas: Destroying all windows..." << std::endl;
        while (!windows.empty()) {
            try {
                auto window = windows.back();
                window->PerformClose();
                windows.pop_back();
            } catch (const std::exception& e) {
                debugOutput << "UltraCanvas: Exception destroying window: " << e.what() << std::endl;
            }
        }

        if (onApplicationExit) {
            onApplicationExit();
        }

        initialized = false;
        debugOutput << "UltraCanvas: main loop completed, shutting down.." << std::endl;
        
        ShutdownClipboard();
        ShutdownNative();

        UCImage::ShutdownImageSubsysterm();
    }

    bool UltraCanvasApplicationBase::RequestExit() {
        debugOutput << "UltraCanvas: application exit requested" << std::endl;
        if (onApplicationExitRequest) {
            if (onApplicationExitRequest()) {
                Exit();
                return true;
            } else {
                debugOutput << "UltraCanvas: application exit requested denied" << std::endl;
                return false;
            }
        } else {
            Exit();
            return true;
        }
    }

    void UltraCanvasApplicationBase::Exit() {
        debugOutput << "UltraCanvas: application exit (set running=false)" << std::endl;
        running = false;
    }

    void UltraCanvasApplicationBase::PushEvent(const UCEvent& event) {
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            eventQueue.push_back(event);
        }
        eventCondition.notify_one();
        WakeUpEventLoop();
    }

    bool UltraCanvasApplicationBase::PopEvent(UCEvent& event) {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        if (eventQueue.empty()) {
            return false;
        }

        event = eventQueue.front();
        eventQueue.pop_front();
        return true;
    }

    void UltraCanvasApplicationBase::ProcessEvents() {
        UCEvent event;
        int processedEvents = 0;

        while (PopEvent(event) && processedEvents < 100) {
            processedEvents++;
            if (!running) {
                break;
            }
            DispatchEvent(event);

            if (event.type == UCEventType::MouseUp && capturedMouseButtonDown == event.button) {
                ReleaseMouse();
            }
        }
    }

    void UltraCanvasApplicationBase::WaitForEvents(int timeoutMs) {
        std::unique_lock<std::mutex> lock(eventQueueMutex);
        if (timeoutMs < 0) {
            eventCondition.wait(lock, [this] { return !eventQueue.empty() || !running; });
        } else {
            eventCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                    [this] { return !eventQueue.empty() || !running; });
        }
    }

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasApplicationBase::RegisterWindow(const std::shared_ptr<UltraCanvasWindowBase>& window) {
        if (window && window->GetNativeHandle() != 0) {
            windows.push_back(window);
            debugOutput << "UltraCanvas: Window registered with Native ID: " << window->GetNativeHandle() << std::endl;

            // Auto-register modal windows
            if (window->GetConfig().modal) {
                RegisterModalWindow(window);
            }
        }
    }

    void UltraCanvasApplicationBase::CleanupWindowReferences(UltraCanvasWindowBase* win) {
        UnregisterModalWindow(win);
        // Called from PerformClose() while the window is still alive, so the weak_ptrs
        // can still be locked and compared here.
        windowFocusHistory.Remove(win);
        if (focusedWindow.lock().get() == win) {
            focusedWindow.reset();

            // if (win->IsFocused()) {
            //     auto parentWin = win->GetParentWindow();
            //     if (parentWin && parentWin->IsVisible()) {
            //         auto parentWinState = parentWin->GetState();
            //         if ( parentWinState == WindowState::Normal || parentWinState == WindowState::Maximized || parentWinState == WindowState::Fullscreen) {
            //             parentWin->RaiseAndFocus();
            //             focusedWindow = parentWin->GetWindowWeakPtr();
            //         }
            //     }
            // }
        }
        if (capturedElement && capturedElement->GetWindow() == win) {
            ReleaseMouse();
        }
        if (hoveredElement && hoveredElement->GetWindow() == win) {
            hoveredElement = nullptr;
        }
        debugOutput << "UltraCanvas: window found and unregistered successfully" << std::endl;
    }

    void UltraCanvasApplicationBase::CleanupElementReferences(UltraCanvasUIElement* elem) {
        // Called from ~UltraCanvasUIElement

        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            for (auto &eventsIt: eventQueue) {
                if (eventsIt.targetElement == elem) {
                    eventsIt.targetElement = nullptr;
                }
            }
        }

        if (currentEvent.targetElement == elem) {
            currentEvent.targetElement = nullptr;
        }

        if (capturedElement == elem) {
            ReleaseMouse();
        }
        if (hoveredElement == elem) {
            hoveredElement = nullptr;
        }
        for (auto* chain : PressChains()) {
            std::replace(chain->begin(), chain->end(), elem, static_cast<UltraCanvasUIElement*>(nullptr));
        }
        auto win = elem->GetWindow();
        if (win && win->_focusedElement == elem) {
            win->_focusedElement = nullptr;
        }
    }

    // ===== MODAL WINDOW MANAGEMENT =====
    UltraCanvasWindowBase* UltraCanvasApplicationBase::GetCurrentModalWindow() {
        for (auto it = activeModalWindows.rbegin(); it != activeModalWindows.rend(); ++it) {
            auto locked = it->lock();
            if (!locked) continue;
            // Native visibility, not the CSS display flag: a window that is not
            // mapped on screen (never shown, hidden, or unmapped by the WM) must
            // never block the application's input as an invisible modal.
            if (!locked->IsWindowVisible()) continue;
            auto state = locked->GetState();
            if (state == WindowState::Closing || state == WindowState::Closed) continue;
            return locked.get();
        }
        return nullptr;
    }

    bool UltraCanvasApplicationBase::HasActiveModalWindow() {
        return GetCurrentModalWindow() != nullptr;
    }

    bool UltraCanvasApplicationBase::HandleModalWindowEvents(const UCEvent& event, UltraCanvasWindow* targetWindow) {
        auto* modalWindow = GetCurrentModalWindow();
        if (!modalWindow) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
            case UCEventType::MouseDoubleClick:
                // A click on another window while a modal is active: bring the
                // modal back to the front so the user sees what is blocking the
                // application instead of the click being swallowed silently
                // (the modal can be buried when the WM restacks windows, e.g.
                // after its transient parent went away).
                if (targetWindow != modalWindow) {
                    modalWindow->RaiseAndFocus();
                    return true;
                }
                break;
            case UCEventType::MouseUp:
            case UCEventType::MouseMove:
            case UCEventType::MouseWheel:
            case UCEventType::MouseEnter:
            case UCEventType::MouseLeave:
            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
            case UCEventType::TextInput:
            case UCEventType::TextComposition:
                if (targetWindow != modalWindow) return true;
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && targetWindow != modalWindow) {
                    modalWindow->RaiseAndFocus();
                    return true;
                }
                break;
            default:
                return false;
        }
        return false;
    }

    void UltraCanvasApplicationBase::RegisterModalWindow(const std::shared_ptr<UltraCanvasWindowBase>& window) {
        if (window) {
            activeModalWindows.push_back(window);
        }
    }

    void UltraCanvasApplicationBase::UnregisterModalWindow(UltraCanvasWindowBase* window) {
        std::erase_if(activeModalWindows,
            [window](const std::weak_ptr<UltraCanvasWindowBase>& w) {
                auto locked = w.lock();
                return !locked || locked.get() == window;
            }
        );
    }

    void UltraCanvasApplicationBase::CloseChildWindows(UltraCanvasWindowBase* parent) {
        // Collect first: each child's PerformClose() recurses back here for its
        // own children and mutates the focus/modal bookkeeping while we iterate.
        // Holding shared_ptr copies keeps the children alive through the loop.
        std::vector<std::shared_ptr<UltraCanvasWindowBase>> children;
        for (auto& w : windows) {
            if (w && w.get() != parent && w->GetConfig().parentWindow == parent) {
                children.push_back(w);
            }
        }
        for (auto& child : children) {
            auto state = child->GetState();
            if (state == WindowState::Closing || state == WindowState::Closed) continue;
            debugOutput << "UltraCanvas: closing child window " << child.get()
                        << " of closing parent " << parent << std::endl;
            child->PerformClose();
        }
    }

    void UltraCanvasApplicationBase::UnregisterWindow(UltraCanvasWindowBase* window) {
        std::erase_if(windows, 
            [window](const std::shared_ptr<UltraCanvasWindowBase>& w) {
                return w.get() == window;
            }
        );
    }
    
    bool UltraCanvasApplicationBase::IsWindowRegistered(UltraCanvasWindowBase* window) {
        return (std::find_if(windows.begin(), windows.end(), 
            [window](const std::shared_ptr<UltraCanvasWindowBase>& w) {
                return w.get() == window;
            }
        ) != windows.end());
    }

    UltraCanvasWindow* UltraCanvasApplicationBase::FindWindow(NativeWindowHandle nativeHandle) {
        auto it = std::find_if(windows.begin(), windows.end(),
                               [nativeHandle](const std::shared_ptr<UltraCanvasWindowBase>& ptr) {
                                   return ptr->GetNativeHandle() == nativeHandle;
                               });

        if (it != windows.end()) {
           return (UltraCanvasWindow*)(it->get());
        } else {
            return nullptr;
        }
    }

    UltraCanvasWindow* UltraCanvasApplicationBase::GetFocusedWindow() {
        return static_cast<UltraCanvasWindow*>(focusedWindow.lock().get());
    }

    UltraCanvasUIElement* UltraCanvasApplicationBase::GetFocusedElement() {
        if (auto fw = focusedWindow.lock()) {
            return fw->GetFocusedElement();
        }
        return nullptr;
    }

    std::vector<UCKeys> UltraCanvasApplicationBase::GetPressedKeys() const {
        std::vector<UCKeys> pressed;
        for (int key = 0; key < 256; ++key) {
            if (keyStates[key]) {
                pressed.push_back(static_cast<UCKeys>(key));
            }
        }
        return pressed;
    }

    void UltraCanvasApplicationBase::ClearKeyboardState() {
        memset(keyStates, 0, sizeof(keyStates));
        shiftHeld = false;
        ctrlHeld = false;
        altHeld = false;
        metaHeld = false;
    }

    void UltraCanvasApplicationBase::SetFocusedWindowInternal(UltraCanvasWindowBase* window) {
        auto current = focusedWindow.lock();
        if (current.get() == window) {
            // No focus change, but make sure the window is at the front of the
            // history (covers the very first focus after window creation).
            if (current) windowFocusHistory.Touch(current);
            return;
        }
        if (current) {
            UCEvent blurEvent;
            blurEvent.type = UCEventType::WindowBlur;
            DispatchEventToElement(current.get(), blurEvent);
        }
        if (window) {
            UCEvent focusEvent;
            focusEvent.type = UCEventType::WindowFocus;
            DispatchEventToElement(window, focusEvent);
            focusedWindow = window->GetWindowWeakPtr();
            if (auto shared = focusedWindow.lock()) {
                windowFocusHistory.Touch(shared);
            }
        } else {
            focusedWindow.reset();
        }
    }

    // ===== JUMP TO LAST WINDOW =====
    bool UltraCanvasApplicationBase::JumpToLastWindow() {
        // A modal window owns the interaction; switching away would bypass it.
        if (HasActiveModalWindow()) {
            return false;
        }

        auto* current = focusedWindow.lock().get();
        auto eligible = [](const std::shared_ptr<UltraCanvasWindowBase>& w) {
            if (!w->IsCreated()) return false;
            auto state = w->GetState();
            if (state == WindowState::Closing || state == WindowState::Closed ||
                state == WindowState::Hidden) {
                return false;
            }
            // Popup windows (menus, dropdowns) are transient — never jump targets.
            if (w->GetConfig().type == WindowType::Popup) return false;
            return w->IsWindowVisible() || state == WindowState::Minimized;
        };

        auto target = windowFocusHistory.FindMostRecent(current, eligible);
        if (!target) {
            // No usable history yet (e.g. shortcut pressed right after startup):
            // fall back to the first other eligible registered window.
            for (const auto& w : windows) {
                if (w.get() != current && eligible(w)) {
                    target = w;
                    break;
                }
            }
        }
        if (!target) {
            return false;
        }

        if (target->GetState() == WindowState::Minimized) {
            target->Restore();
        }
        target->RaiseAndFocus();

        // Update focus bookkeeping immediately instead of waiting for the
        // asynchronous native focus notification, so repeated triggers toggle
        // between the two most recent windows predictably. The native
        // WindowFocus event that arrives later becomes a no-op. The target
        // window keeps its _focusedElement, so keyboard input resumes in the
        // element (input field) that was focused when the window was left.
        SetFocusedWindowInternal(target.get());
        return true;
    }

    void UltraCanvasApplicationBase::SetJumpToLastWindowKey(UCKeys key, bool ctrl, bool shift,
                                                            bool alt, bool meta) {
        jumpLastWindowKeyEnabled = (key != UCKeys::Unknown);
        jumpLastWindowKey = key;
        jumpLastWindowKeyCtrl = ctrl;
        jumpLastWindowKeyShift = shift;
        jumpLastWindowKeyAlt = alt;
        jumpLastWindowKeyMeta = meta;
    }

    void UltraCanvasApplicationBase::ClearJumpToLastWindowKey() {
        jumpLastWindowKeyEnabled = false;
        jumpLastWindowKey = UCKeys::Unknown;
    }

    void UltraCanvasApplicationBase::SetJumpToLastWindowMouseButton(UCMouseButton button) {
        jumpLastWindowMouseButton = button;
    }

    bool UltraCanvasApplicationBase::MatchesJumpToLastWindowTrigger(const UCEvent& event) const {
        if (jumpLastWindowKeyEnabled && event.type == UCEventType::KeyDown) {
            return event.virtualKey == jumpLastWindowKey &&
                   event.ctrl == jumpLastWindowKeyCtrl &&
                   event.shift == jumpLastWindowKeyShift &&
                   event.alt == jumpLastWindowKeyAlt &&
                   event.meta == jumpLastWindowKeyMeta;
        }
        if (jumpLastWindowMouseButton != UCMouseButton::NoneButton &&
            event.type == UCEventType::MouseDown) {
            return event.button == jumpLastWindowMouseButton;
        }
        return false;
    }

    void UltraCanvasApplicationBase::DispatchEvent(const UCEvent& event) {
        // Update modifier states and key pressed status
        if (event.type == UCEventType::KeyDown || event.type == UCEventType::KeyUp) {
            shiftHeld = event.shift;
            ctrlHeld = event.ctrl;
            altHeld = event.alt;
            metaHeld = event.meta;
            if (event.virtualKey >= 0 && event.virtualKey < 256) {
                keyStates[event.virtualKey] = (event.type == UCEventType::KeyDown);
            }
        }

        // Call global handlers first
        for (auto& handler : globalEventHandlers) {
            if (handler(event)) {
                return; // Event consumed by global handler
            }
        }

        // Jump-to-last-window trigger is an application-level action; consume
        // the event before any window/element sees it so a bound key or mouse
        // button never leaks into the UI as input.
        if (MatchesJumpToLastWindowTrigger(event)) {
            JumpToLastWindow();
            return;
        }

        // ===== NEW: IMPROVED TARGET WINDOW DETECTION =====
        UltraCanvasWindow* targetWindow = nullptr;

        // First priority: Use the window information stored in the event. An expired
        // weak_ptr (window already destroyed) naturally falls through to the fallbacks.
        if (auto tw = event.targetWindow.lock()) {
            targetWindow = static_cast<UltraCanvasWindow*>(tw.get());
            if (std::find_if(windows.begin(), windows.end(), [&tw](auto const &item) {
                return item == tw;
            }) == windows.end()) {
                debugOutput << "UltraCanvasApplicationBase::DispatchEvent stale event for already deleted window ev=" << event.ToString() << " win="<<targetWindow << std::endl;
                return;
            }
        }
            // Fallback: Try to find window by native handle
        else if (event.nativeWindowHandle != 0) {
            targetWindow = FindWindow(event.nativeWindowHandle);
        }
            // Last resort: Use focused window for certain event types
        else {
            // Only use focused window for keyboard events when no target is found
            if (event.type == UCEventType::KeyDown ||
                event.type == UCEventType::KeyUp) {
                targetWindow = GetFocusedWindow();
            }
        }

        // block some events if modal window active
        if (HandleModalWindowEvents(event, targetWindow)) {
            return;
        }

       if (event.type == UCEventType::MouseDown) {
           if (targetWindow && GetFocusedWindow() != targetWindow) {
               debugOutput << "Window clicked but not focused, set focus. target=" << targetWindow << " focused=" << GetFocusedWindow() << std::endl;
               SetFocusedWindowInternal(targetWindow);
           }
       }

        // Handle different event types
        switch (event.type) {
            case UCEventType::MouseMove:
            case UCEventType::MouseUp:
                if (capturedElement) {
                    if (DispatchEventToElement(capturedElement, event)) {
                        return;
                    }
                }
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && GetFocusedWindow() != targetWindow) {
                    // Update focused window + MRU focus history
                    SetFocusedWindowInternal(targetWindow);
                    //debugOutput << "UltraCanvasBaseApplication: Window " << targetWindow << " (native=" << targetWindow->GetNativeHandle() << ") gained focus" << std::endl;
                }
                return;
            case UCEventType::WindowBlur:
                if (targetWindow && targetWindow == GetFocusedWindow()) {
                    //debugOutput << "UltraCanvasBaseApplication: Window " << targetWindow << " (native=" << targetWindow->GetNativeHandle() << ") lost focus" << std::endl;
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow.reset();
                }
                return;
        }
        // Dispatch other events to focused element
        if (targetWindow) {
            UltraCanvasUIElement* elementUnderPointer = nullptr;
//            UltraCanvasUIElement* originalElementUnderPointer = nullptr;

            PopupElement* popupElement = targetWindow->GetActivePopupElement();
//            std::shared_ptr<UltraCanvasUIElement> popupOwner;
            bool isPointerOutsidePopupElement = false;

//            if (popupElement) {
//                popupOwner = popupElement->settings.popUpOwner.lock();
//            }

            // Check if an element is the popup owner or a child of it
//            auto isPopupOwnerOrChild = [&popupElement](UltraCanvasUIElement* elem) -> bool {
//                if (!popupElement || !elem) return false;
//                auto owner = popupElement->settings.popUpOwner.lock();
//                if (!owner) return false;
//                if (elem == owner.get()) return true;
//                auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(owner.get());
//                return ownerContainer && ownerContainer->HasChild(elem);
//            };

            if (event.IsMouseEvent() || event.IsDragEvent()) { // change mouse cursor first
                elementUnderPointer = targetWindow->FindElementAtPoint(event.pointerWindow, true);
  //              originalElementUnderPointer = elementUnderPointer;
                // set pointerElem to popup element if it points outside
                if (popupElement) {
                    if (elementUnderPointer) {
                        if (elementUnderPointer != popupElement->element) {
                            auto *popupContainer = dynamic_cast<UltraCanvasContainer *>(popupElement->element);
                            if (!popupContainer || !popupContainer->HasChild(elementUnderPointer)) {
                                auto popupOwner = popupElement->settings.popupOwner.lock();
                                if (!popupOwner) {
                                    isPointerOutsidePopupElement = true;
                                } else {
                                    if (popupOwner.get() != elementUnderPointer) {
                                        auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(popupOwner.get());
                                        if (!ownerContainer || !ownerContainer->HasChild(elementUnderPointer)) {
                                            isPointerOutsidePopupElement = true;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        isPointerOutsidePopupElement = true;
                    }

                    if (isPointerOutsidePopupElement) {
                        elementUnderPointer = popupElement->element;
                    }
                }
                // change mouse cursor
                if (targetWindow->IsBusyPointerVisible()) {
                    // A launch this window is waiting on owns the pointer:
                    // element cursors take over again once it comes down.
                } else if (elementUnderPointer) {
                    if (targetWindow->GetCurrentMouseCursor() != elementUnderPointer->GetMouseCursor()) {
                        targetWindow->SelectMouseCursor(elementUnderPointer->GetMouseCursor());
                    }
                } else {
                    // if no element pointed then select window's cursor
                    if (targetWindow->GetCurrentMouseCursor() != targetWindow->GetMouseCursor()) {
                        targetWindow->SelectMouseCursor(targetWindow->GetMouseCursor());
                    }
                }
            }

            // close popup element handling first
            if (popupElement) {
                // THERE element->Contains() WHICH IS NOT THE SAME AS element->GetBounds().Contains(),
                // THE element->Contains() MAY CHECK CHILD ELEMENTS TOO (SUBMENUS OR PARENT MENUS FOR EXAMPLE)
                if (popupElement->settings.closeByClickOutside
                    && event.type == UCEventType::MouseDown
                    && event.button != UCMouseButton::NoneButton
                    && isPointerOutsidePopupElement
                    && !popupElement->element->ContainsInWindow(event.pointerWindow)) {
                    targetWindow->ClosePopup(*popupElement->element, ClosePopupReason::ClickOutside);
                    goto finish;
                }
                if (popupElement->settings.closeByEscapeKey
                    && event.IsKeyboardEvent()
                    && event.virtualKey == UCKeys::Escape) {
                    targetWindow->ClosePopup(*popupElement->element, ClosePopupReason::EscapeKey);
                    goto finish;
                }
            }

            if (event.IsKeyboardEvent()) {
                UltraCanvasUIElement* focused =  targetWindow->GetFocusedElement();
                // sent event to popup directly if real focused element outside popup
                if (popupElement && focused != popupElement->element) {
                    UltraCanvasContainer* popupContainer = dynamic_cast<UltraCanvasContainer*>(popupElement->element);
                    if (!popupContainer || !popupContainer->HasChild(focused)) {
                        auto popupOwner = popupElement->settings.popupOwner.lock();
                        if (!popupOwner) {
                            focused = popupElement->element;
                        } else if (popupOwner.get() != focused) {
                            auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(popupOwner.get());
                            if (!ownerContainer || !ownerContainer->HasChild(focused)) {
                                focused = popupElement->element;
                            }
                        }
                    }
                }
                if (focused) {
                    HandleEventWithBubbling(focused, event);
                    goto finish;
                }
            }

            // Touch events go straight to the element under that finger. No
            // hover, cursor or capture handling: those are pointer concepts a
            // finger has no equivalent for, and a second finger must not
            // disturb the state the first one established. Backends that
            // synthesise mouse events from single-finger touches (Android)
            // keep every existing widget working through the mouse path.
            if (event.IsTouchEvent()) {
                UltraCanvasUIElement* touched =
                        targetWindow->FindElementAtPoint(event.pointerWindow, true);
                if (touched) {
                    HandleEventWithBubbling(touched, event);
                } else {
                    DispatchEventToElement(targetWindow, event);
                }

                // Widgets see the raw fingers first, then any gesture they
                // add up to: a handler that tracks touches itself has already
                // had its say by the time PinchZoom arrives.
                if (event.type == UCEventType::TouchStart ||
                    event.type == UCEventType::TouchMove ||
                    event.type == UCEventType::TouchEnd) {
                    UpdateTouchGesture(event);
                }
                goto finish;
            }

            if (event.type == UCEventType::MouseWheel && elementUnderPointer) {
                HandleEventWithBubbling(elementUnderPointer, event);
                goto finish;

            }
            if (event.IsDragEvent()) {
                if (elementUnderPointer) {
                    HandleEventWithBubbling(elementUnderPointer, event);
                } else {
                    DispatchEventToElement(targetWindow, event);
                }
                goto finish;
            }

            if (event.IsMouseEvent()) {
                // A press answers the tooltip's question: the element is
                // being used, not wondered about. Without this a click that
                // changes the layout underneath the pointer (a button that
                // docks a pane) left the tooltip floating over the new
                // content until the mouse moved.
                if (event.type == UCEventType::MouseDown) {
                    UltraCanvasTooltipManager::HideTooltipImmediately();
                }
                if (hoveredElement && hoveredElement != elementUnderPointer) {
                    if (hoveredElement->GetWindow() == targetWindow && hoveredElement->IsVisible()) {
                        UCEvent leaveEvent = event;
                        leaveEvent.type = UCEventType::MouseLeave;
                        leaveEvent.pointer = { -1, -1 };
                        DispatchEventToElement(hoveredElement, leaveEvent);
                    }
                    UltraCanvasTooltipManager::HideTooltip();
                    hoveredElement = nullptr;
                }
                if (!elementUnderPointer || elementUnderPointer == targetWindow) {
                    UltraCanvasTooltipManager::HideTooltip();
                }
                if (elementUnderPointer) {
                    if (hoveredElement != elementUnderPointer) {
                        auto enterEvent = event.Clone();
                        enterEvent.type = UCEventType::MouseEnter;
                        DispatchEventToElement(elementUnderPointer, enterEvent);

                        hoveredElement = elementUnderPointer;
                        // Show tooltip if element has one; a structured
                        // TooltipContent wins over the plain-text tooltip
                        auto& tooltipContent = elementUnderPointer->GetTooltipContent();
                        if (tooltipContent && !tooltipContent->Empty()) {
                            UltraCanvasTooltipManager::UpdateAndShowTooltip(
                                    targetWindow, *tooltipContent,
                                    event.pointerWindow);
                        } else if (!elementUnderPointer->GetTooltip().empty()) {
                            UltraCanvasTooltipManager::UpdateAndShowTooltip(
                                    targetWindow, elementUnderPointer->GetTooltip(),
                                    event.pointerWindow);
                        }
                    }
                    // Update tooltip position as mouse moves
                    if (elementUnderPointer->HasTooltip() &&
                        (UltraCanvasTooltipManager::IsVisible() || UltraCanvasTooltipManager::IsPending())) {
                        UltraCanvasTooltipManager::UpdateTooltipPosition(
                            event.pointerWindow);
                    }

                    // A press the element does not take climbs to the
                    // elements around it, as wheel, drag, touch and keys do:
                    // a click on the label inside a clickable card is the
                    // card's. It used to go to the window and nowhere else,
                    // so such a card answered only on its padding. The chain
                    // is taken before the element runs, because a press can
                    // rebuild the tree under the pointer; it stops below the
                    // window, which still gets an untaken press once, below -
                    // and so never leaves a popup, a child of the window.
                    std::vector<UltraCanvasUIElement*> pressChain;
                    const bool climbs = event.IsMouseClickEvent();
                    if (climbs) {
                        pressChain.push_back(elementUnderPointer);
                        for (UltraCanvasUIElement* up = elementUnderPointer->GetParentContainer();
                             up && up != targetWindow; up = up->GetParentContainer()) {
                            pressChain.push_back(up);
                        }
                    }
                    struct PressChainScope {
                        std::vector<std::vector<UltraCanvasUIElement*>*>& chains;
                        std::vector<UltraCanvasUIElement*>* chain;
                        PressChainScope(std::vector<std::vector<UltraCanvasUIElement*>*>& cs,
                                        std::vector<UltraCanvasUIElement*>* c) : chains(cs), chain(c) {
                            chains.push_back(chain);
                        }
                        ~PressChainScope() {
                            chains.erase(std::find(chains.begin(), chains.end(), chain));
                        }
                    } pressChainScope(PressChains(), &pressChain);

                    if (DispatchEventToElement(elementUnderPointer, event)) {
                        goto finish;
                    }
                    if (climbs && DispatchPressToAncestors(*this, pressChain, event)) {
                        goto finish;
                    }
                }
            }

            if (event.isCommandEvent()) {
                HandleEventWithBubbling(event.targetElement, event);
                goto finish;
            }
            DispatchEventToElement(targetWindow, event);

    finish:
            return;
//            // Debug logging
//            if (event.type != UCEventType::MouseMove) {
//                debugOutput << "UltraCanvas: Event type " << static_cast<int>(event.type)
//                          << " dispatched to window " << targetWindow
//                          << " (X11 Window: " << std::hex << event.nativeWindowHandle << std::dec << ")"
//                          << " focused=" << (targetWindow == focusedWindow ? "yes" : "no") << std::endl;
        } else {
            // No target window found - this might be normal for some system events
            debugOutput << "UltraCanvas: Warning - Event " << event.ToString()
                      << " has no target window (Native Window: " << std::hex << event.nativeWindowHandle << std::dec << ")" << std::endl;
        }
    }

    bool UltraCanvasApplicationBase::HandleEventWithBubbling(UltraCanvasUIElement *elem, const UCEvent &event) {
        if (!elem) {
            return false;  // target element was destroyed before the event was processed
        }
        if (!event.isCommandEvent()) {
            if (DispatchEventToElement(elem, event)) {
                return true;
            }
        }
        auto parent = elem->GetParentContainer();
        while(parent) {
            if (DispatchEventToElement(parent, event)) {
                return true;
            }
            parent = parent->GetParentContainer();
        }
        return false;
    }


    void UltraCanvasApplicationBase::FocusNextElement() {
        if (auto fw = focusedWindow.lock()) {
            fw->FocusNextElement();
        }
    }

    void UltraCanvasApplicationBase::FocusPreviousElement() {
        if (auto fw = focusedWindow.lock()) {
            fw->FocusPreviousElement();
        }
    }

    void UltraCanvasApplicationBase::RegisterEventLoopRunCallback(std::function<void()> callback) {
        eventLoopCallback = callback;
    }

    bool UltraCanvasApplicationBase::DispatchEventToElement(UltraCanvasUIElement* elem, UCEvent event) {
        event.targetElement = elem;
        auto window = elem->GetWindow();
        if (!window) {
            debugOutput << "UltraCanvasApplicationBase::DispatchEventToElement window == null for elem=" << elem << std::ends;
            return false;
        }
//        if (event.type != UCEventType::MouseMove) {
//            debugOutput << "DispatchEventToElement ev=" << event.ToString() << " target elem=" << elem << " target win=" << elem->GetWindow() << " focused=" << focusedWindow << std::endl;
//        }
        if (event.IsMouseEvent() || event.IsDragEvent() || event.IsTouchEvent()
            || event.type == UCEventType::MouseEnter) {
            event.pointer = elem->MapToLocal(event.pointerWindow, nullptr);
        }

        currentEvent = event;

        if (window->HandleEventFilters(event)) {
            return true;
        }

        return elem->OnEvent(event);
    }

    // ===== TOUCH GESTURE RECOGNITION =====

    void UltraCanvasApplicationBase::ResetTouchGesture() {
        activeTouches.clear();
        gestureActive = false;
        gestureBaseDistance = 0.0;
        gestureBaseAngle = 0.0;
        gestureWindow.reset();
    }

    void UltraCanvasApplicationBase::UpdateTouchGesture(const UCEvent& touchEvent) {
        auto window = touchEvent.targetWindow.lock();
        if (!window) return;

        // Fingers on a different window are a different gesture entirely.
        if (!gestureWindow.expired() && gestureWindow.lock() != window) {
            ResetTouchGesture();
        }
        gestureWindow = window;

        auto existing = std::find_if(activeTouches.begin(), activeTouches.end(),
                [&](const TouchPoint& p) { return p.pointerId == touchEvent.pointerId; });

        switch (touchEvent.type) {
            case UCEventType::TouchStart:
                if (existing == activeTouches.end()) {
                    // A pointer id is reused once its finger lifts, so a start
                    // for an id we already hold means we missed the end.
                    activeTouches.push_back({touchEvent.pointerId, touchEvent.pointerWindow});
                } else {
                    existing->position = touchEvent.pointerWindow;
                }
                // A finger landing or leaving changes the geometry the gesture
                // was measured against; re-baseline rather than report a jump.
                gestureActive = false;
                return;

            case UCEventType::TouchEnd:
                if (existing != activeTouches.end()) activeTouches.erase(existing);
                gestureActive = false;
                if (activeTouches.empty()) ResetTouchGesture();
                return;

            case UCEventType::TouchMove:
                if (existing == activeTouches.end()) {
                    activeTouches.push_back({touchEvent.pointerId, touchEvent.pointerWindow});
                } else {
                    existing->position = touchEvent.pointerWindow;
                }
                break;

            default:
                return;
        }

        // Exactly two fingers: more than that is a gesture this does not model
        // (and reporting a pinch from an arbitrary pair would be worse than
        // reporting nothing).
        if (activeTouches.size() != 2) {
            gestureActive = false;
            return;
        }

        const Point2Di& a = activeTouches[0].position;
        const Point2Di& b = activeTouches[1].position;
        const double dx = static_cast<double>(b.x) - a.x;
        const double dy = static_cast<double>(b.y) - a.y;
        const double distance = std::sqrt(dx * dx + dy * dy);
        const double angle = std::atan2(dy, dx);

        // Below this the fingers are close enough that the scale ratio becomes
        // wildly unstable (and at zero it is a division by zero).
        constexpr double kMinBaseDistance = 20.0;

        if (!gestureActive) {
            if (distance < kMinBaseDistance) return;
            gestureBaseDistance = distance;
            gestureBaseAngle = angle;
            gestureActive = true;
            return;   // nothing has changed yet: this frame IS the baseline
        }

        UCEvent gesture = touchEvent;
        gesture.type = UCEventType::PinchZoom;
        gesture.pointerWindow = Point2Di((a.x + b.x) / 2, (a.y + b.y) / 2);
        gesture.pointer = gesture.pointerWindow;
        gesture.pointerGlobal = gesture.pointerWindow;
        gesture.touchPointCount = static_cast<int>(activeTouches.size());
        gesture.scale = static_cast<float>(distance / gestureBaseDistance);

        // Wrap into (-pi, pi] so a gesture crossing the angle discontinuity
        // reports a small rotation rather than a full turn. Spelled out
        // rather than using M_PI, which MSVC only defines with
        // _USE_MATH_DEFINES set before <cmath>.
        constexpr double kPi = 3.14159265358979323846;
        double rotation = angle - gestureBaseAngle;
        while (rotation > kPi) rotation -= 2.0 * kPi;
        while (rotation <= -kPi) rotation += 2.0 * kPi;
        gesture.rotation = static_cast<float>(rotation);

        auto* target = static_cast<UltraCanvasWindow*>(window.get());
        if (UltraCanvasUIElement* under = target->FindElementAtPoint(gesture.pointerWindow, true)) {
            HandleEventWithBubbling(under, gesture);
        } else {
            DispatchEventToElement(target, gesture);
        }
    }

    void UltraCanvasApplicationBase::CaptureMouse(UltraCanvasUIElement *element) {
        capturedMouseButtonDown = currentEvent.button;
        capturedElement = element;
        CaptureMouseNative();
    }

    void UltraCanvasApplicationBase::ReleaseMouse() {
        if (capturedElement) {
            ReleaseMouseNative();
        }
        capturedElement = nullptr;
        capturedMouseButtonDown = UCMouseButton::NoneButton;
    }

    // ===== TIMER SYSTEM =====

    TimerId UltraCanvasApplicationBase::StartTimer(unsigned int ms_interval, bool periodic,
                                                    std::function<void(TimerId)> callback) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        UltraCanvasTimer timer;
        timer.id = nextTimerId_++;
        timer.interval = std::chrono::milliseconds(ms_interval);
        timer.periodic = periodic;
        timer.active = true;
        timer.nextFire = std::chrono::steady_clock::now() + timer.interval;
        timer.callback = std::move(callback);
        timers_.push_back(std::move(timer));
        WakeUpEventLoop();
        return timers_.back().id;
    }

    void UltraCanvasApplicationBase::StopTimer(TimerId id) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        for (auto& timer : timers_) {
            if (timer.id == id) {
                timer.active = false;
                return;
            }
        }
    }

    void UltraCanvasApplicationBase::ProcessTimers() {
        auto now = std::chrono::steady_clock::now();

        // Collect the work to run for timers that are due. We must NOT invoke
        // callbacks (or touch a timers_ element) while iterating: callbacks are
        // documented to call StartTimer()/StopTimer() from any thread, and
        // StartTimer() does timers_.push_back() which can reallocate the vector.
        // So we snapshot each due timer's id + callback under the lock, advance
        // or deactivate the timer in place (still under the lock), and only run
        // the callbacks after the lock is released. A null callback means push a
        // Timer UCEvent instead. Timers added during callbacks appear next round.
        std::vector<std::pair<TimerId, std::function<void(TimerId)>>> due;
        {
            std::lock_guard<std::mutex> lock(timersMutex_);
            for (auto& timer : timers_) {
                if (!timer.active) continue;
                if (timer.nextFire > now) continue;

                due.emplace_back(timer.id, timer.callback);

                // Advance (periodic) or deactivate (one-shot). If a collected
                // callback later calls StopTimer() on its own periodic timer, it
                // marks active=false and the timer is cleaned up next round, so a
                // self-stopped timer never fires again.
                if (timer.periodic) {
                    timer.nextFire += timer.interval;
                    // If we fell behind, skip to next future fire time
                    if (timer.nextFire <= now) {
                        auto elapsed = now - timer.nextFire;
                        auto periods = elapsed / timer.interval + 1;
                        timer.nextFire += timer.interval * periods;
                    }
                } else {
                    timer.active = false;
                }
            }

            // Clean up inactive timers (safe: everything needed is already in `due`)
            timers_.erase(
                std::remove_if(timers_.begin(), timers_.end(),
                               [](const UltraCanvasTimer& t) { return !t.active; }),
                timers_.end());
        }

        // Run callbacks / push events without holding timersMutex_, so a callback
        // is free to call StartTimer()/StopTimer() without deadlocking, and no
        // timers_ element is accessed across a callback.
        for (auto& [id, callback] : due) {
            if (callback) {
                callback(id);
            } else {
                UCEvent timerEvent;
                timerEvent.type = UCEventType::Timer;
                timerEvent.userDataInt = static_cast<int>(id);
                // Push directly to queue without calling WakeUpEventLoop (we're already on the main thread)
                std::lock_guard<std::mutex> lock(eventQueueMutex);
                eventQueue.push_back(timerEvent);
            }
        }
    }

    std::chrono::milliseconds UltraCanvasApplicationBase::GetTimeUntilNextTimer() const {
        std::lock_guard<std::mutex> lock(timersMutex_);
        auto earliest = std::chrono::steady_clock::time_point::max();
        for (const auto& timer : timers_) {
            if (timer.active && timer.nextFire < earliest) {
                earliest = timer.nextFire;
            }
        }
        if (earliest == std::chrono::steady_clock::time_point::max()) {
            return std::chrono::milliseconds::max(); // No active timers
        }
        auto now = std::chrono::steady_clock::now();
        if (earliest <= now) {
            return std::chrono::milliseconds(0);
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now);
    }
}
