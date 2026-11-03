// Apps/DemoApp/UltraCanvasFilerExamples.cpp
// Demonstration of UltraCanvasFilerWidget: one folder shown with selectable
// view types (details / list / thumbnails / bar size / treemap), sortable
// columns, a file type filter that hides or greys out the other types, the
// hover icon menu and the full file context menu (right-click).
// Version: 1.3.0 - the shared page look (UltraCanvasDemoPageKit): title and
//                  description above a card holding the control rows, the filer
//                  and the status line; the rows wrap, their labels and the
//                  view buttons are as wide as their text
// Version: 1.2.0 - the type buttons hold FileLoader's lists
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <vector>

namespace UltraCanvas {

    namespace {
        // A control row of the card: wraps onto further lines when the page
        // is narrower than its buttons (or their text is longer).
        std::shared_ptr<UltraCanvasContainer> AddFilerRow(const std::shared_ptr<UltraCanvasContainer>& card,
                                                          const std::string& id) {
            auto row = DemoKit::AddRow(card, id, 6);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            return row;
        }

        // The bold "View" / "Sort" / "Types" label at the start of a row.
        void AddFilerRowLabel(const std::shared_ptr<UltraCanvasContainer>& row,
                              const std::string& id, const std::string& text) {
            auto lbl = DemoKit::CreateText(id, text);
            lbl->SetFontWeight(FontWeight::Bold);
            lbl->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            DemoKit::Add(row, lbl);
        }

        // Rebuilds the breadcrumb from the filer's current path through the
        // shared folder path mechanism (BuildFolderBreadcrumb, also used by the
        // media viewer): a leading "Computer" node listing the drives, the drive
        // node, then one clickable node per path segment — each jumping the filer
        // to its cumulative sub-path.
        void RebuildFilerBreadcrumb(UltraCanvasBreadcrumb* crumb,
                                    UltraCanvasFilerWidget* filer,
                                    const std::string& path) {
            if (!crumb || !filer) return;
            BuildFolderBreadcrumb(crumb, path,
                                  [filer](const std::string& folder) { filer->SetPath(folder); });
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
        auto root = DemoKit::CreatePage("FilerExamples",
                "UltraCanvas Filer — folder content with selectable view types",
                "Double-click a folder to enter it. Right-click for the file menu "
                "(Copy / Cut / Delete / Duplicate / Rename / New / Display / Compress / "
                "Extras). Hovering an item shows its quick icon menu; the info bar at "
                "the bottom describes the selection (size, dates, image dimensions, "
                "media duration and codec, folder contents). In the Details, List and "
                "Bar size views the columns are resizable — drag the splitters between "
                "them — and a name too long for its column shows in full as a tooltip.");
        // The filer is the single scroll region of the page: the page is the
        // area's height and the filer takes what the rows above it leave.
        root->SetFillsArea(true);

        auto card = DemoKit::CreateCard("FilerCard");
        card->SetPadding(8);
        DemoKit::Add(root, card, 1);

        // ===== The filer itself (created before the control rows so their
        // callbacks can capture it; added to the layout after them) =====
        auto filer = CreateFilerWidget("demo-filer", 0, 0, 0, 0);

        // ===== Path row: Up button + breadcrumb =====
        // Does not wrap: the breadcrumb collapses its middle nodes instead.
        auto pathRow = DemoKit::CreateRow("FilerPathRow", 6, false);
        pathRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::Add(card, pathRow);
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
        DemoKit::Add(pathRow, upButton);

        // Breadcrumb replaces the plain path label: it shows the current folder
        // as interlocking "arrow steps" and lets the user jump directly to any
        // parent folder (or, via the leading "Computer" node, another drive).
        auto breadcrumb = std::make_shared<UltraCanvasBreadcrumb>("FilerBreadcrumb", 0, 0, 0, 26);
        breadcrumb->SetStyle(MakeFilerBreadcrumbStyle());
        DemoKit::Add(pathRow, breadcrumb, 1);
        breadcrumb->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        {
            auto filerPtr = filer.get();
            upButton->SetOnClick([filerPtr]() {
                std::filesystem::path p = PathFromUtf8(filerPtr->GetPath());
                if (p.has_parent_path() && p.parent_path() != p) {
                    filerPtr->SetPath(PathToUtf8(p.parent_path()));
                }
            });
        }

        // ===== View-type row =====
        {
            auto row = AddFilerRow(card, "FilerViewRow");
            AddFilerRowLabel(row, "FilerViewLbl", "View");

            struct ViewSeed { const char* label; FilerViewType type; };
            const ViewSeed views[] = {
                {"Details",   FilerViewType::Details},
                {"List",      FilerViewType::List},
                {"Thumbs S",  FilerViewType::ThumbnailsSmall},
                {"Thumbs M",  FilerViewType::ThumbnailsMedium},
                {"Thumbs B",  FilerViewType::ThumbnailsBig},
                {"Thumbs XL", FilerViewType::ThumbnailsMaximized},
                {"Bar size",  FilerViewType::BarSize},
                {"Treemap",   FilerViewType::TreeMap},
                {"Gource",    FilerViewType::GourceTree},
                {"3D",        FilerViewType::View3D},
            };
            auto group = std::make_shared<std::vector<UltraCanvasButton*>>();
            auto filerPtr = filer.get();
            for (const ViewSeed& v : views) {
                // Width 0 => the button auto-sizes to its label, as the
                // sort buttons do, so a longer label is never clipped.
                auto b = std::make_shared<UltraCanvasButton>(
                        std::string("FilerView_") + v.label, 0, 0, 0, 24, v.label);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                StyleFilerOptionButton(b.get(), v.type == FilerViewType::Details);
                auto* raw = b.get();
                FilerViewType type = v.type;
                b->SetOnClick([filerPtr, type, group, raw]() {
                    for (auto* gb : *group) StyleFilerOptionButton(gb, gb == raw);
                    filerPtr->SetViewType(type);
                });
                DemoKit::Add(row, b);
                group->push_back(raw);
            }
            // Keep the buttons in sync when the view is switched through the
            // context menu (Display > Type) instead of this row.
            filer->onViewTypeChanged = [group, views, filerPtr](FilerViewType now) {
                for (size_t i = 0; i < group->size() && i < 10; ++i) {
                    StyleFilerOptionButton((*group)[i], views[i].type == now);
                }
            };

            // Puts the column widths of all three column views back to their
            // defaults after they have been dragged around.
            auto reset = std::make_shared<UltraCanvasButton>(
                    "FilerResetColumns", 0, 0, 0, 24, "Reset columns");
            reset->SetFontSize(11);
            reset->SetCornerRadius(4.0f);
            StyleFilerOptionButton(reset.get(), false);
            reset->SetOnClick([filerPtr]() {
                filerPtr->ResetDetailsColumnWidths();
                filerPtr->SetListColumnWidth(FilerStyle().listColumnWidth);
                filerPtr->SetBarSizeNameColumnWidth(220);
                filerPtr->SetBarSizeValueColumnWidth(0);   // auto
            });
            DemoKit::Add(row, reset);
        }

        // ===== Sort row =====
        {
            auto row = AddFilerRow(card, "FilerSortRow");
            AddFilerRowLabel(row, "FilerSortLbl", "Sort");

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
                DemoKit::Add(row, b);
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
                DemoKit::Add(row, b);
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

            // A/B switch for the in-memory QOI thumbnail compression.
            auto compressCb = std::make_shared<UltraCanvasCheckbox>(
                    "FilerCompressedThumbs", 0, 0, 190, 24,
                    "Compressed thumbnails");
            compressCb->SetChecked(filerPtr->GetCompressedThumbnails());
            compressCb->onChecked = [filerPtr]() {
                filerPtr->SetCompressedThumbnails(true);
            };
            compressCb->onUnchecked = [filerPtr]() {
                filerPtr->SetCompressedThumbnails(false);
            };
            DemoKit::AddFixed(row, compressCb);
        }

        // ===== File type filter row: what a load / save dialog does with the
        // files it was not asked for =====
        {
            auto row = AddFilerRow(card, "FilerTypeFilterRow");
            AddFilerRowLabel(row, "FilerTypeFilterLbl", "Types");

            struct TypeSeed { const char* label; std::vector<std::string> extensions; };
            // FileLoader's lists - the types this build reads, as a file
            // dialog's filters hold them (UltraCanvasFileLoader::AddLoadTypes)
            auto reads = [](std::initializer_list<MediaFormatCategory> categories) {
                std::vector<std::string> out;
                for (MediaFormatCategory category : categories)
                    for (const std::string& ext : UltraCanvasFileLoader::GetSupportedLoadExtensions(category))
                        if (std::find(out.begin(), out.end(), ext) == out.end()) out.push_back(ext);
                return out;
            };
            const std::vector<TypeSeed> types = {
                {"All files", {"*"}},
                {"Images",    reads({MediaFormatCategory::Bitmap, MediaFormatCategory::Vector})},
                {"Audio",     reads({MediaFormatCategory::Audio})},
                {"Video",     reads({MediaFormatCategory::Video})},
            };
            auto group = std::make_shared<std::vector<UltraCanvasButton*>>();
            auto filerPtr = filer.get();
            for (const TypeSeed& t : types) {
                if (t.extensions.empty()) continue;      // a kind this build reads none of
                auto b = std::make_shared<UltraCanvasButton>(
                        std::string("FilerTypes_") + t.label, 0, 0, 0, 24, t.label);
                b->SetFontSize(11);
                b->SetCornerRadius(4.0f);
                StyleFilerOptionButton(b.get(), t.extensions.front() == "*");
                auto* raw = b.get();
                std::vector<std::string> extensions = t.extensions;
                b->SetOnClick([filerPtr, extensions, group, raw]() {
                    for (auto* gb : *group) StyleFilerOptionButton(gb, gb == raw);
                    // The mode in force stays: the switch beside the buttons
                    // owns it.
                    filerPtr->SetFileTypeFilter(extensions,
                                                filerPtr->GetFileTypeFilterMode());
                });
                DemoKit::Add(row, b);
                group->push_back(raw);
            }

            // Off: the other types are left out, as a file dialog lists.
            // On: they stay in the listing greyed out, name and thumbnail,
            // and a double-click on one does nothing.
            auto dimCb = std::make_shared<UltraCanvasCheckbox>(
                    "FilerDimOtherTypes", 0, 0, 220, 24,
                    "Show other types greyed out");
            dimCb->SetChecked(false);
            dimCb->onChecked = [filerPtr]() {
                filerPtr->SetFileTypeFilterMode(FilerTypeFilterMode::ShowDimmed);
            };
            dimCb->onUnchecked = [filerPtr]() {
                filerPtr->SetFileTypeFilterMode(FilerTypeFilterMode::Hide);
            };
            DemoKit::AddFixed(row, dimCb);
        }

        // From a zero basis the filer takes exactly the height the rows and
        // the status line leave.
        DemoKit::Add(card, filer, 1);
        filer->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

        // ===== Status line (pinned, below the filer) =====
        auto status = DemoKit::AddText(card, "FilerStatus", "");
        status->SetFontSize(11);
        status->SetTextColor(Color(110, 110, 110, 255));

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
            // Selection details (type, size, media info, ...) are shown by the
            // widget's built-in info bar; the demo status line only counts.
            filer->onSelectionChanged =
                    [statusPtr, filerPtr](const std::vector<FilerEntry>& sel) {
                if (sel.empty()) {
                    statusPtr->SetText(std::to_string(filerPtr->GetEntries().size())
                                       + " entries");
                } else {
                    statusPtr->SetText(std::to_string(sel.size()) + " of "
                                       + std::to_string(filerPtr->GetEntries().size())
                                       + " entries selected");
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
