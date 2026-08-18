// Apps/UltraFiler/UltraFilerPropertiesDialogs.cpp
// "Extras > Attributes" and "Extras > Access" windows of the filer context
// menu. Attributes is read-only: one detail grid for a single entry, a
// summary plus a scrollable per-item list for a multi selection. Access edits
// permissions: the POSIX Owner / Group / Others x Read / Write / Execute
// matrix (special bits like setuid are preserved), reduced to a single
// "Read-only" switch on Windows, where only the write bits map onto the
// filesystem attribute.
// Version: 1.0.0
// Last Modified: 2026-08-17
// Author: UltraCanvas Framework

#include "UltraFilerPropertiesDialogs.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

    constexpr float kFontSize = 9.0f;   // matches the main window's UI font

    // The one open window of each kind. Reopening replaces it: the content
    // describes one specific selection.
    std::shared_ptr<UltraCanvasWindow> g_attributesWindow;
    std::shared_ptr<UltraCanvasWindow> g_accessWindow;
    // Keeps the Access page's checkboxes (and the Apply capture) alive.
    struct AccessState;
    std::shared_ptr<AccessState> g_accessState;

    std::string FormatTime(std::time_t t) {
        if (t == 0) return "-";
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
        return buf;
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

    std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id,
                                                  const std::string& label,
                                                  int width,
                                                  std::function<void()> onClick) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
        b->SetFontSize(kFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(Color(40, 40, 44, 255));
        b->SetBorder(1.0f, Color(0, 0, 0, 60));
        if (onClick) b->SetOnClick(std::move(onClick));
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    }

    // "Label:   value" row of the detail grid.
    std::shared_ptr<UltraCanvasContainer> MakeDetailRow(const std::string& id,
                                                        const std::string& label,
                                                        const std::string& value) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        row->size.height = CSSLayout::Dimension::Px(20);

        auto name = MakeLabel(id + "-l", label);
        name->SetTextColor(Color(110, 110, 118, 255));
        name->SetAlignment(TextAlignment::Right, VerticalAlignment::Top);
        name->size.width = CSSLayout::Dimension::Px(90);
        row->AddChild(name);

        auto val = MakeLabel(id + "-v", value);
        val->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(val);
        return row;
    }

    // The attribute flags, spelled out ("D" alone says little in a dialog).
    std::string DescribeFlags(const FilerEntry& e) {
        std::string flags;
        auto add = [&flags](const char* f) {
            if (!flags.empty()) flags += ", ";
            flags += f;
        };
        if (e.isDirectory) add("Directory");
        if (e.isSymlink)   add("Symlink");
        if (e.isReadOnly)  add("Read-only");
        if (e.isHidden)    add("Hidden");
        if (e.isArchive)   add("Archive");
        return flags.empty() ? "-" : flags;
    }

    std::string DescribeSize(const FilerEntry& e) {
        if (e.isDirectory) return "-";
        std::string s = FormatFileSize(e.size);
        if (e.size >= 1024)
            s += " (" + std::to_string(e.size) + " bytes)";
        return s;
    }

    std::shared_ptr<UltraCanvasWindow> MakeDialogWindow(
            UltraCanvasWindowBase* parent, const std::string& title,
            int width, int height) {
        WindowConfig wc;
        wc.title = title;
        wc.width = width;
        wc.height = height;
        wc.resizable = true;
        wc.type = WindowType::Dialog;
        wc.parentWindow = parent;
        auto window = CreateWindow(wc);
        if (!window || !window->IsCreated()) return nullptr;

        window->layout.SetFlexColumn()
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        window->SetBackgroundColor(Color(249, 249, 251, 255));
        // Escape closes, matching the framework's dialog convention.
        window->SetEventCallback([](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp &&
                event.virtualKey == UCKeys::Escape) {
                if (auto tw = event.targetWindow.lock())
                    static_cast<UltraCanvasWindow*>(tw.get())->Close();
                return true;
            }
            return false;
        });
        return window;
    }

    // Content area (grows) + bottom bar with a Close button.
    std::shared_ptr<UltraCanvasContainer> AddDialogFrame(
            const std::shared_ptr<UltraCanvasWindow>& window,
            const std::string& idPrefix) {
        auto content = std::make_shared<UltraCanvasContainer>(idPrefix + "-content");
        content->layout.SetFlexColumn().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->SetPadding(14, 16, 10, 16);
        content->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        window->AddChild(content);

        auto bottom = std::make_shared<UltraCanvasContainer>(idPrefix + "-bottom");
        bottom->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bottom->SetPadding(8, 12, 8, 12);
        bottom->SetBorderTop(1, Color(225, 225, 230, 255));
        UltraCanvasWindow* raw = window.get();
        bottom->AddChild(MakeButton(idPrefix + "-close", "Close", 90,
                [raw]() { raw->Close(); }));
        window->AddChild(bottom);

        return content;
    }

    // ===== ACCESS (permissions) =====

    struct AccessState {
        std::vector<FilerEntry> entries;
        std::function<void()> onApplied;
        std::shared_ptr<UltraCanvasLabel> status;
#ifdef _WIN32
        std::shared_ptr<UltraCanvasCheckbox> readOnlyBox;
#else
        // [owner, group, others] x [read, write, execute]
        std::shared_ptr<UltraCanvasCheckbox> boxes[3][3];
#endif
    };

#ifndef _WIN32
    constexpr fs::perms kPermMatrix[3][3] = {
        {fs::perms::owner_read,  fs::perms::owner_write,  fs::perms::owner_exec},
        {fs::perms::group_read,  fs::perms::group_write,  fs::perms::group_exec},
        {fs::perms::others_read, fs::perms::others_write, fs::perms::others_exec},
    };
    constexpr const char* kPermRowLabels[3] = {"Owner", "Group", "Others"};
    constexpr const char* kPermColLabels[3] = {"Read", "Write", "Execute"};
#endif

    void ApplyAccess(AccessState* st) {
        size_t applied = 0;
        std::string firstError;
        for (const FilerEntry& e : st->entries) {
            std::error_code ec;
#ifdef _WIN32
            // Windows maps the write bits onto FILE_ATTRIBUTE_READONLY.
            if (st->readOnlyBox && st->readOnlyBox->IsChecked())
                fs::permissions(e.path, fs::perms::owner_write | fs::perms::group_write |
                                        fs::perms::others_write,
                                fs::perm_options::remove, ec);
            else
                fs::permissions(e.path, fs::perms::owner_write,
                                fs::perm_options::add, ec);
#else
            const fs::perms current = fs::status(e.path, ec).permissions();
            if (!ec) {
                fs::perms rwx = fs::perms::none;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        if (st->boxes[r][c] && st->boxes[r][c]->IsChecked())
                            rwx |= kPermMatrix[r][c];
                // Keep the special bits (setuid / setgid / sticky) untouched.
                constexpr fs::perms kAllRwx =
                        fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all;
                fs::permissions(e.path, (current & ~kAllRwx) | rwx,
                                fs::perm_options::replace, ec);
            }
#endif
            if (ec) {
                if (firstError.empty())
                    firstError = e.name + ": " + ec.message();
            } else {
                ++applied;
            }
        }

        if (st->status) {
            std::string text = "Applied to " + std::to_string(applied) +
                    (applied == 1 ? " item." : " items.");
            if (!firstError.empty()) text = "Error: " + firstError;
            st->status->SetText(text);
            st->status->RequestRedraw();
        }
        if (applied > 0 && st->onApplied) st->onApplied();
    }

} // namespace

// ===== ATTRIBUTES =====

void UltraFilerPropertiesDialogs::ShowAttributes(
        UltraCanvasWindowBase* parent, const std::vector<FilerEntry>& entries) {
    if (entries.empty()) return;
    if (g_attributesWindow) {
        g_attributesWindow->Close();   // stale content: an older selection
        g_attributesWindow.reset();
    }

    const bool single = entries.size() == 1;
    auto window = MakeDialogWindow(parent, "Attributes - UltraFiler",
                                   460, single ? 280 : 380);
    if (!window) return;
    auto content = AddDialogFrame(window, "ufl-attr");

    if (single) {
        const FilerEntry& e = entries.front();
        content->AddChild(MakeDetailRow("ufl-attr-name", "Name:", e.name));
        content->AddChild(MakeDetailRow("ufl-attr-type", "Type:", e.typeName));
        content->AddChild(MakeDetailRow("ufl-attr-loc", "Location:",
                fs::path(e.path).parent_path().string()));
        content->AddChild(MakeDetailRow("ufl-attr-size", "Size:", DescribeSize(e)));
        if (e.compressedSize > 0) {
            content->AddChild(MakeDetailRow("ufl-attr-csize", "Compressed:",
                    FormatFileSize(e.compressedSize)));
        }
        content->AddChild(MakeDetailRow("ufl-attr-created", "Created:",
                FormatTime(e.createdTime)));
        content->AddChild(MakeDetailRow("ufl-attr-modified", "Modified:",
                FormatTime(e.modifiedTime)));
        content->AddChild(MakeDetailRow("ufl-attr-flags", "Attributes:",
                DescribeFlags(e)));
        if (!e.info.empty())
            content->AddChild(MakeDetailRow("ufl-attr-info", "Info:", e.info));
    } else {
        size_t files = 0, folders = 0;
        uint64_t bytes = 0;
        for (const FilerEntry& e : entries) {
            if (e.isDirectory) ++folders;
            else { ++files; bytes += e.size; }
        }
        std::string counts = std::to_string(entries.size()) + " items (" +
                std::to_string(files) + (files == 1 ? " file, " : " files, ") +
                std::to_string(folders) + (folders == 1 ? " folder)" : " folders)");
        content->AddChild(MakeDetailRow("ufl-attr-items", "Selection:", counts));
        content->AddChild(MakeDetailRow("ufl-attr-size", "Total size:",
                FormatFileSize(bytes) + " (files only)"));

        // The per-item list scrolls; the detail rows above stay put.
        auto list = std::make_shared<UltraCanvasContainer>("ufl-attr-list");
        list->layout.SetFlexColumn().SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        list->SetPadding(4, 6, 4, 6);
        list->SetBackgroundColor(Color(255, 255, 255, 255));
        list->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        constexpr size_t kMaxListed = 500;
        for (size_t i = 0; i < entries.size() && i < kMaxListed; ++i) {
            const FilerEntry& e = entries[i];
            std::string line = e.name + "  -  " + e.typeName;
            if (!e.isDirectory) line += ", " + FormatFileSize(e.size);
            if (!e.attributes.empty()) line += "  [" + e.attributes + "]";
            auto item = MakeLabel("ufl-attr-item-" + std::to_string(i), line);
            item->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            item->size.height = CSSLayout::Dimension::Px(18);
            list->AddChild(item);
        }
        if (entries.size() > kMaxListed) {
            list->AddChild(MakeLabel("ufl-attr-item-more", "... and " +
                    std::to_string(entries.size() - kMaxListed) + " more"));
        }
        content->AddChild(list);
    }

    window->onWindowClosed = []() { g_attributesWindow.reset(); };
    g_attributesWindow = window;
    window->Show();
}

// ===== ACCESS =====

void UltraFilerPropertiesDialogs::ShowAccess(
        UltraCanvasWindowBase* parent, const std::vector<FilerEntry>& entries,
        std::function<void()> onApplied) {
    if (entries.empty()) return;
    if (g_accessWindow) {
        g_accessWindow->Close();
        g_accessWindow.reset();
        g_accessState.reset();
    }

#ifdef _WIN32
    const int height = 200;
#else
    const int height = 260;
#endif
    auto window = MakeDialogWindow(parent, "Access - UltraFiler", 400, height);
    if (!window) return;
    auto content = AddDialogFrame(window, "ufl-acc");

    auto state = std::make_shared<AccessState>();
    state->entries = entries;
    state->onApplied = std::move(onApplied);

    const FilerEntry& first = entries.front();
    std::string caption = entries.size() == 1
            ? "Permissions of \"" + first.name + "\":"
            : "Permissions of " + std::to_string(entries.size()) +
              " items (shown: \"" + first.name + "\"):";
    content->AddChild(MakeLabel("ufl-acc-caption", caption));

#ifdef _WIN32
    state->readOnlyBox = UltraCanvasCheckbox::CreateCheckbox(
            "ufl-acc-readonly", -1, -1, 200, 22, "Read-only", first.isReadOnly);
    state->readOnlyBox->SetFontSize(kFontSize);
    state->readOnlyBox->size.width  = CSSLayout::Dimension::Px(200);
    state->readOnlyBox->size.height = CSSLayout::Dimension::Px(22);
    state->readOnlyBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    content->AddChild(state->readOnlyBox);
#else
    std::error_code ec;
    const fs::perms current = fs::status(first.path, ec).permissions();

    for (int r = 0; r < 3; ++r) {
        auto row = std::make_shared<UltraCanvasContainer>(
                std::string("ufl-acc-row-") + kPermRowLabels[r]);
        row->layout.SetFlexRow().SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        row->size.height = CSSLayout::Dimension::Px(26);

        auto name = MakeLabel(std::string("ufl-acc-lbl-") + kPermRowLabels[r],
                              kPermRowLabels[r]);
        name->SetTextColor(Color(110, 110, 118, 255));
        name->size.width = CSSLayout::Dimension::Px(56);
        row->AddChild(name);

        for (int c = 0; c < 3; ++c) {
            const bool checked =
                    !ec && (current & kPermMatrix[r][c]) != fs::perms::none;
            auto box = UltraCanvasCheckbox::CreateCheckbox(
                    "ufl-acc-" + std::to_string(r) + std::to_string(c),
                    -1, -1, 88, 22, kPermColLabels[c], checked);
            box->SetFontSize(kFontSize);
            box->size.width  = CSSLayout::Dimension::Px(88);
            box->size.height = CSSLayout::Dimension::Px(22);
            box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            state->boxes[r][c] = box;
            row->AddChild(box);
        }
        content->AddChild(row);
    }
#endif

    auto buttonRow = std::make_shared<UltraCanvasContainer>("ufl-acc-buttons");
    buttonRow->layout.SetFlexRow().SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    buttonRow->size.height = CSSLayout::Dimension::Px(34);
    AccessState* raw = state.get();
    buttonRow->AddChild(MakeButton("ufl-acc-apply", "Apply", 90,
            [raw]() { ApplyAccess(raw); }));
    content->AddChild(buttonRow);

    state->status = MakeLabel("ufl-acc-status", "");
    state->status->SetTextColor(Color(110, 110, 118, 255));
    content->AddChild(state->status);

    window->onWindowClosed = []() {
        g_accessWindow.reset();
        g_accessState.reset();
    };
    g_accessState = state;
    g_accessWindow = window;
    window->Show();
}

} // namespace UltraCanvas
