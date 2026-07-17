// Apps/DemoApp/UltraCanvasFilerExamples.cpp
// Demonstration of UltraCanvasFilerWidget: one folder shown with selectable
// view types (details / list / thumbnails / bar size / treemap), sortable
// columns, the hover icon menu and the full file context menu (right-click).
// Version: 1.0.0
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasUtils.h"
#include <filesystem>
#include <vector>

namespace UltraCanvas {

    namespace {
        // A plain flex wrapper that must never scroll itself — the filer is the
        // single scroll region of the page.
        std::shared_ptr<UltraCanvasContainer> MakeFilerLayoutBox(const std::string& id) {
            auto c = std::make_shared<UltraCanvasContainer>(id);
            ContainerStyle st;
            st.autoShowScrollbars           = false;
            st.forceShowVerticalScrollbar   = false;
            st.forceShowHorizontalScrollbar = false;
            c->SetContainerStyle(st);
            return c;
        }

        std::shared_ptr<UltraCanvasContainer> MakeFilerRow(const std::string& id) {
            auto row = MakeFilerLayoutBox(id);
            row->layout.SetFlexRow().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            row->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            return row;
        }

        // Available drive roots / mounted volumes, used by the breadcrumb's
        // leading "Computer" node so the user can jump straight to another drive.
        std::vector<std::string> ListDriveRoots() {
            std::vector<std::string> roots;
            std::error_code ec;
#ifdef _WIN32
            for (char c = 'A'; c <= 'Z'; ++c) {
                std::string drive = std::string(1, c) + ":\\";
                if (std::filesystem::exists(drive, ec)) roots.push_back(drive);
            }
#else
            roots.push_back("/");
            for (const char* base : {"/media", "/mnt", "/Volumes"}) {
                std::error_code e2;
                if (!std::filesystem::is_directory(base, e2)) continue;
                for (const auto& entry :
                         std::filesystem::directory_iterator(base, e2)) {
                    std::error_code e3;
                    if (entry.is_directory(e3)) roots.push_back(entry.path().string());
                }
            }
#endif
            return roots;
        }

        // Rebuilds the breadcrumb from the filer's current path: a leading
        // "Computer" node (drive list) followed by one clickable node per path
        // segment, each jumping the filer to its cumulative sub-path.
        void RebuildFilerBreadcrumb(UltraCanvasBreadcrumb* crumb,
                                    UltraCanvasFilerWidget* filer,
                                    const std::string& path) {
            if (!crumb || !filer) return;
            crumb->Clear();

            std::filesystem::path p(path);
            std::filesystem::path base = p.root_path();
            if (base.empty()) base = "/";

            // "Computer": jumps to the current drive root and drops down the
            // full list of drives / volumes so any of them can be selected.
            BreadcrumbItem computer("__computer__", "Computer");
            computer.tooltip = "Computer — show all drives";
            {
                std::string rootTarget = base.string();
                computer.onClick = [filer, rootTarget]() { filer->SetPath(rootTarget); };
            }
            std::vector<std::string> drives = ListDriveRoots();
            if (!drives.empty()) {
                computer.hasDropdown = true;
                for (const std::string& d : drives) {
                    computer.dropdownItems.emplace_back(
                            d, [filer, d]() { filer->SetPath(d); });
                }
            }
            crumb->AddItem(computer);

            // On Windows the drive letter ("C:") is its own node; on Unix the
            // root "/" is already covered by "Computer", so segments start below.
            std::string driveLabel = p.root_name().string();
            if (!driveLabel.empty()) {
                BreadcrumbItem drive(driveLabel);
                std::string target = base.string();
                drive.onClick = [filer, target]() { filer->SetPath(target); };
                crumb->AddItem(drive);
            }

            std::filesystem::path accum = base;
            for (const auto& part : p.relative_path()) {
                std::string seg = part.string();
                if (seg.empty() || seg == "/" || seg == "\\") continue;
                accum /= part;
                BreadcrumbItem item(seg);
                std::string target = accum.string();
                item.onClick = [filer, target]() { filer->SetPath(target); };
                crumb->AddItem(item);
            }
        }

        // A compact "arrow steps" breadcrumb style, smaller than the standalone
        // breadcrumb example so it sits neatly on the filer's path row.
        BreadcrumbStyle MakeFilerBreadcrumbStyle() {
            BreadcrumbStyle s = BreadcrumbStyle::Arrow();
            s.overflowMode = BreadcrumbOverflowMode::Collapse;
            s.keepFirstItemOnCollapse = true;
            s.minVisibleAfterCollapse = 2;
            s.arrowSize = 8;
            s.itemPaddingHorizontal = 9;
            s.itemPaddingVertical = 3;
            s.fontStyle.fontSize = 11.0f;
            return s;
        }

        // Option-button styling shared with the slideshow / album examples: the
        // active choice gets a gold border and a warm fill.
        void StyleFilerOptionButton(UltraCanvasButton* b, bool selected) {
            const Color baseBg(255, 255, 255, 255);
            const Color selBg(246, 214, 158, 255);
            const Color baseBorder(0, 0, 0, 77);
            const Color selBorder(201, 143, 46, 255);
            const Color text(40, 40, 40, 255);
            b->SetColors(selected ? selBg : baseBg, selBg);
            b->SetTextColors(text);
            b->SetBorder(selected ? 2.0f : 1.0f, selected ? selBorder : baseBorder);
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFilerExamples() {
        auto root = MakeFilerLayoutBox("FilerExamples");
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);

        // ===== Header (pinned) =====
        auto title = std::make_shared<UltraCanvasLabel>("FilerTitle", 0, 0, 0, 28);
        title->SetText("UltraCanvas Filer — folder content with selectable view types");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("FilerSubtitle", 0, 0, 0, 20);
        subtitle->SetText("Double-click a folder to enter it. Right-click for the file menu "
                          "(Copy / Cut / Delete / Duplicate / Rename / New / Display / Compress / "
                          "Extras). Hovering an item shows its quick icon menu.");
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(110, 110, 110, 255));
        subtitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(subtitle);

        // ===== The filer itself (created before the control rows so their
        // callbacks can capture it; added to the layout after them) =====
        auto filer = CreateFilerWidget("demo-filer", 0, 0, 0, 0);
        filer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // ===== Path row: Up button + breadcrumb =====
        auto pathRow = MakeFilerRow("FilerPathRow");
        // Width 0 lets the button auto-size to its icon + label (intrinsic
        // sizing) so the up-arrow and text always fit.
        auto upButton = std::make_shared<UltraCanvasButton>("FilerUp", 0, 0, 0, 24, "Up");
        upButton->SetFontSize(11);
        upButton->SetCornerRadius(4.0f);
        upButton->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/arrow-up.svg"));
        upButton->SetIconSize(12, 12);
        upButton->SetIconPosition(ButtonIconPosition::Left);
        upButton->SetIconSpacing(4);
        upButton->SetUseIconAsMask(true);
        upButton->SetIconMaskColor(Color(40, 40, 40, 255));
        StyleFilerOptionButton(upButton.get(), false);
        upButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        pathRow->AddChild(upButton);

        // Breadcrumb replaces the plain path label: it shows the current folder
        // as interlocking "arrow steps" and lets the user jump directly to any
        // parent folder (or, via the leading "Computer" node, another drive).
        auto breadcrumb = std::make_shared<UltraCanvasBreadcrumb>("FilerBreadcrumb", 0, 0, 0, 26);
        breadcrumb->SetStyle(MakeFilerBreadcrumbStyle());
        breadcrumb->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Center);
        pathRow->AddChild(breadcrumb);
        root->AddChild(pathRow);

        {
            auto filerPtr = filer.get();
            upButton->SetOnClick([filerPtr]() {
                std::filesystem::path p(filerPtr->GetPath());
                if (p.has_parent_path() && p.parent_path() != p) {
                    filerPtr->SetPath(p.parent_path().string());
                }
            });
        }

        // ===== View-type row =====
        {
            auto row = MakeFilerRow("FilerViewRow");
            auto lbl = std::make_shared<UltraCanvasLabel>("FilerViewLbl", 0, 0, 46, 24);
            lbl->SetText("View");
            lbl->SetFontSize(12);
            lbl->SetFontWeight(FontWeight::Bold);
            lbl->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            row->AddChild(lbl);

            struct ViewSeed { const char* label; FilerViewType type; int width; };
            const ViewSeed views[] = {
                {"Details",   FilerViewType::Details,             74},
                {"List",      FilerViewType::List,                56},
                {"Thumbs S",  FilerViewType::ThumbnailsSmall,     84},
                {"Thumbs M",  FilerViewType::ThumbnailsMedium,    86},
                {"Thumbs B",  FilerViewType::ThumbnailsBig,       84},
                {"Thumbs XL", FilerViewType::ThumbnailsMaximized, 90},
                {"Bar size",  FilerViewType::BarSize,             76},
                {"Treemap",   FilerViewType::TreeMap,             78},
                {"Gource",    FilerViewType::GourceTree,          70},
                {"3D",        FilerViewType::View3D,              44},
            };
            auto group = std::make_shared<std::vector<UltraCanvasButton*>>();
            auto filerPtr = filer.get();
            for (const ViewSeed& v : views) {
                auto b = std::make_shared<UltraCanvasButton>(
                        std::string("FilerView_") + v.label, 0, 0, v.width, 24, v.label);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                StyleFilerOptionButton(b.get(), v.type == FilerViewType::Details);
                auto* raw = b.get();
                FilerViewType type = v.type;
                b->SetOnClick([filerPtr, type, group, raw]() {
                    for (auto* gb : *group) StyleFilerOptionButton(gb, gb == raw);
                    filerPtr->SetViewType(type);
                });
                b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                row->AddChild(b);
                group->push_back(raw);
            }
            // Keep the buttons in sync when the view is switched through the
            // context menu (Display > Type) instead of this row.
            filer->onViewTypeChanged = [group, views, filerPtr](FilerViewType now) {
                for (size_t i = 0; i < group->size() && i < 10; ++i) {
                    StyleFilerOptionButton((*group)[i], views[i].type == now);
                }
            };
            root->AddChild(row);
        }

        // ===== Sort row =====
        {
            auto row = MakeFilerRow("FilerSortRow");
            auto lbl = std::make_shared<UltraCanvasLabel>("FilerSortLbl", 0, 0, 46, 24);
            lbl->SetText("Sort");
            lbl->SetFontSize(12);
            lbl->SetFontWeight(FontWeight::Bold);
            lbl->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            row->AddChild(lbl);

            struct SortSeed { const char* label; FilerSortField field; };
            const SortSeed sorts[] = {
                {"Name",     FilerSortField::Name},
                {"Size",     FilerSortField::Size},
                {"Type",     FilerSortField::Type},
                {"Modified", FilerSortField::ModifiedDate},
                {"Created",  FilerSortField::CreatedDate},
            };
            auto fieldGroup = std::make_shared<std::vector<UltraCanvasButton*>>();
            auto filerPtr = filer.get();
            for (const SortSeed& s : sorts) {
                // Width 0 => the button auto-sizes to its label (intrinsic
                // sizing), so "Modified" / "Created" are never clipped.
                auto b = std::make_shared<UltraCanvasButton>(
                        std::string("FilerSort_") + s.label, 0, 0, 0, 24, s.label);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                StyleFilerOptionButton(b.get(), s.field == FilerSortField::Name);
                auto* raw = b.get();
                FilerSortField field = s.field;
                b->SetOnClick([filerPtr, field, fieldGroup, raw]() {
                    for (auto* gb : *fieldGroup) StyleFilerOptionButton(gb, gb == raw);
                    filerPtr->SetSortField(field);
                });
                b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                row->AddChild(b);
                fieldGroup->push_back(raw);
            }

            auto orderGroup = std::make_shared<std::vector<UltraCanvasButton*>>();
            const char* orderLabels[] = {"Up", "Down"};
            for (int i = 0; i < 2; ++i) {
                auto b = std::make_shared<UltraCanvasButton>(
                        std::string("FilerOrder_") + orderLabels[i], 0, 0, 0, 24,
                        orderLabels[i]);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                StyleFilerOptionButton(b.get(), i == 0);
                auto* raw = b.get();
                bool ascending = (i == 0);
                b->SetOnClick([filerPtr, ascending, orderGroup, raw]() {
                    for (auto* gb : *orderGroup) StyleFilerOptionButton(gb, gb == raw);
                    filerPtr->SetSortAscending(ascending);
                });
                b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                row->AddChild(b);
                orderGroup->push_back(raw);
            }

            // Sync when sorting is changed via the context menu / column headers.
            filer->onSortChanged = [fieldGroup, sorts, orderGroup](FilerSortField f,
                                                                   bool asc) {
                for (size_t i = 0; i < fieldGroup->size() && i < 5; ++i) {
                    StyleFilerOptionButton((*fieldGroup)[i], sorts[i].field == f);
                }
                if (orderGroup->size() == 2) {
                    StyleFilerOptionButton((*orderGroup)[0], asc);
                    StyleFilerOptionButton((*orderGroup)[1], !asc);
                }
            };
            root->AddChild(row);
        }

        root->AddChild(filer);

        // ===== Status line (pinned, below the filer) =====
        auto status = std::make_shared<UltraCanvasLabel>("FilerStatus", 0, 0, 0, 20);
        status->SetFontSize(11);
        status->SetTextColor(Color(110, 110, 110, 255));
        status->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        status->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(status);

        // ===== Wire the filer to the page =====
        {
            auto* statusPtr = status.get();
            auto* crumbPtr = breadcrumb.get();
            auto filerPtr = filer.get();

            filer->onPathChanged = [crumbPtr, statusPtr, filerPtr](const std::string& path) {
                RebuildFilerBreadcrumb(crumbPtr, filerPtr, path);
                statusPtr->SetText(std::to_string(filerPtr->GetEntries().size())
                                   + " entries");
            };
            filer->onSelectionChanged =
                    [statusPtr, filerPtr](const std::vector<FilerEntry>& sel) {
                if (sel.empty()) {
                    statusPtr->SetText(std::to_string(filerPtr->GetEntries().size())
                                       + " entries");
                } else if (sel.size() == 1) {
                    statusPtr->SetText("Selected: " + sel[0].name + "  ·  "
                                       + sel[0].typeName);
                } else {
                    statusPtr->SetText(std::to_string(sel.size()) + " items selected");
                }
            };
            filer->onFileActivated = [statusPtr](const FilerEntry& e) {
                statusPtr->SetText("Activated: " + e.path);
            };
            filer->onError = [statusPtr](const std::string& message) {
                statusPtr->SetText("Error: " + message);
            };
            filer->onPrint = [statusPtr](const std::vector<FilerEntry>& targets) {
                statusPtr->SetText("Print requested for "
                                   + std::to_string(targets.size()) + " item(s)");
            };
            filer->onShare = [statusPtr](const std::vector<FilerEntry>& targets) {
                statusPtr->SetText("Share requested for "
                                   + std::to_string(targets.size()) + " item(s)");
            };
            filer->onAttributes = [statusPtr](const std::vector<FilerEntry>& targets) {
                std::string text = "Attributes:";
                for (const FilerEntry& e : targets) {
                    text += " " + e.name + " [" + (e.attributes.empty() ? "-" : e.attributes)
                            + "]";
                    if (text.size() > 120) { text += " …"; break; }
                }
                statusPtr->SetText(text);
            };
            filer->onAccess = [statusPtr](const std::vector<FilerEntry>& targets) {
                statusPtr->SetText("Access requested for "
                                   + std::to_string(targets.size()) + " item(s)");
            };
            filer->onSettings = [statusPtr]() {
                statusPtr->SetText("Settings requested");
            };
            filer->AddOpenWithApp({"Status line (demo)", "",
                    [statusPtr](const std::vector<FilerEntry>& targets) {
                statusPtr->SetText("Open with requested for "
                                   + std::to_string(targets.size()) + " item(s)");
            }});
        }

        // Show the bundled media folder — it has images, audio, video and
        // subfolders, so every view type has something interesting to show.
        filer->SetPath(NormalizePath(GetResourcesDir() + "media"));

        return root;
    }

} // namespace UltraCanvas
