// core/UltraCanvasSettingsPageKit.cpp
// See the header. The look came from UltraFiler's settings window
// (Apps/UltraFiler/UltraFilerSettingsDialog.cpp), where it was written
// first; UltraSnip copied it, and the copy became this kit.
// Version: 1.1.0 - ShowSaveSettingsDialog
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "UltraCanvasSettingsPageKit.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

namespace UltraCanvas {
namespace SettingsPageKit {

// ===== TEXT =====

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text, float fontSize,
                                            const Color& color) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id, const std::string& text, int width, float fontSize,
                                           const Color& color) {
    auto l = MakeLabel(id, text, fontSize, color);
    l->SetWrap(TextWrap::WrapWord);
    l->size.width = CSSLayout::Dimension::Px(width);
    return l;
}

// ===== PAGE SKELETON =====

std::shared_ptr<UltraCanvasContainer> MakeNotesBlock(const std::string& id) {
    auto notes = std::make_shared<UltraCanvasContainer>(id);
    notes->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    notes->SetBackgroundColor(kNoteBackground);
    notes->SetBorderLeft(3, kNoteAccent);
    notes->SetPadding(10, 12, 10, 12);
    return notes;
}

PageParts MakePage(const std::string& id, const std::string& title, const std::string& caption, bool scrolls) {
    PageParts parts;
    parts.page = std::make_shared<UltraCanvasContainer>(id);
    parts.page->layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    if (scrolls) {
        // A scroll view has to opt in - containers do not scroll unless
        // asked. Vertically only: the vertical bar narrows the viewport,
        // which would otherwise fabricate a horizontal overflow of its own
        // width.
        ContainerStyle cs;
        cs.autoShowScrollbars = true;
        cs.autoShowHorizontalScrollbar = false;
        parts.page->SetContainerStyle(cs);
    }

    auto titleLabel = MakeLabel(id + "-title", title, kTitleFontSize);
    titleLabel->SetFontWeight(FontWeight::Bold);
    parts.page->AddChild(titleLabel);

    auto captionLabel = MakeText(id + "-caption", caption);
    captionLabel->SetMargin(6, 0, 0, 0);
    parts.page->AddChild(captionLabel);

    parts.body = std::make_shared<UltraCanvasContainer>(id + "-body");
    parts.body->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    parts.body->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    parts.body->SetMargin(14, 0, 0, 0);

    parts.notes = MakeNotesBlock(id + "-notes");

    if (scrolls) {
        parts.notes->SetMargin(12, 0, 0, 0);
        parts.page->AddChild(parts.notes);
        parts.page->AddChild(parts.body);
    } else {
        parts.page->AddChild(parts.body);
        // The spacer pushes the notes to the foot of the page, away from the
        // controls, and gives way when the window is made small.
        auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
        spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        spacer->size.height = CSSLayout::Dimension::Px(24);
        parts.page->AddChild(spacer);
        parts.page->AddChild(parts.notes);
    }
    return parts;
}

void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
    parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize, kNoteTextColor));
}

void AddBodyCaption(PageParts& parts, const std::string& id, const std::string& text) {
    auto l = MakeText(id, text);
    l->SetMargin(10, 0, 0, 0);
    parts.body->AddChild(l);
}

// ===== CONTROLS =====

std::string IconPath(const std::string& fileName) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + fileName);
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& label, int width,
                                              std::function<void()> onClick, const std::string& iconFile) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
    b->SetFontSize(kTextFontSize);
    b->SetCornerRadius(4.0f);
    b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
    b->SetTextColors(kTextColor);
    b->SetBorder(1.0f, Color(0, 0, 0, 60));
    if (!iconFile.empty()) {
        b->SetIcon(IconPath(iconFile));
        b->SetIconSize(15, 15);
        b->SetIconPosition(ButtonIconPosition::Left);
        b->SetIconSpacing(label.empty() ? 0 : 5);
        b->SetUseIconAsMask(true);
        b->SetIconMaskColor(Color(55, 55, 60, 255));
    }
    if (onClick) b->SetOnClick(std::move(onClick));
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

std::shared_ptr<UltraCanvasContainer> MakeButtonRow(const std::string& id) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(34);
    return row;
}

std::shared_ptr<UltraCanvasContainer> MakeChoiceRow(const std::string& id) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(6).SetFlexWrap(CSSLayout::FlexWrap::Wrap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    return row;
}

std::shared_ptr<UltraCanvasContainer> MakeLabelledRow(const std::string& id, const std::string& caption,
                                                      const std::shared_ptr<UltraCanvasUIElement>& control,
                                                      int labelWidth, int height) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(height);
    auto label = MakeLabel(id + "-label", caption);
    label->size.width = CSSLayout::Dimension::Px(labelWidth);
    label->size.height = CSSLayout::Dimension::Px(20);
    row->AddChild(label);
    if (control) row->AddChild(control);
    return row;
}

std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id, const std::string& text, bool checked, int width) {
    auto radio = UltraCanvasRadio::Create(id, -1, -1, text, checked);
    RadioVisualStyle style = radio->GetVisualStyle();
    style.base.fontSize = kTextFontSize;
    style.base.textColor = kTextColor;
    style.base.textHoverColor = kTextColor;
    radio->SetVisualStyle(style);
    // Explicit sizes: content measuring needs a render context, which the
    // window does not have while it is first laid out.
    radio->size.width = CSSLayout::Dimension::Px(width);
    radio->size.height = CSSLayout::Dimension::Px(kControlHeight);
    radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return radio;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(const std::string& id, const std::string& text, int width,
                                                  bool checked, std::function<void(bool)> onChange) {
    auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, static_cast<float>(width),
                                                     static_cast<float>(kControlHeight), text);
    box->SetFontSize(kTextFontSize);
    CheckboxVisualStyle style = box->GetVisualStyle();
    style.base.textColor = kTextColor;
    style.base.textHoverColor = kTextColor;
    box->SetVisualStyle(style);
    box->SetChecked(checked);
    box->size.width = CSSLayout::Dimension::Px(width);
    box->size.height = CSSLayout::Dimension::Px(kControlHeight);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->onStateChanged = [onChange](CheckedState, CheckedState now) {
        if (onChange) onChange(now == CheckedState::Checked);
    };
    return box;
}

std::shared_ptr<UltraCanvasSlider> MakeIntSlider(const std::string& id, int minValue, int maxValue, int value,
                                                 std::function<void(int)> onChange) {
    auto slider = CreateSlider(id, 0, 0, 200, 24);
    slider->SetRange(static_cast<float>(minValue), static_cast<float>(maxValue));
    slider->SetStep(1.0f);
    slider->SetValue(static_cast<float>(value));
    slider->size.width = CSSLayout::Dimension::Px(200);
    slider->size.height = CSSLayout::Dimension::Px(24);
    slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // Reported both while the handle is dragged and when it is let go, so
    // what it sets follows the slider instead of jumping at the end.
    slider->onValueChanging = [onChange](float v) {
        if (onChange) onChange(static_cast<int>(v + 0.5f));
    };
    slider->onValueChanged = [onChange](float v) {
        if (onChange) onChange(static_cast<int>(v + 0.5f));
    };
    return slider;
}

std::shared_ptr<UltraCanvasLabel> MakeSliderValueLabel(const std::string& id, int width) {
    auto label = MakeLabel(id, "");
    label->size.width = CSSLayout::Dimension::Px(width);
    label->size.height = CSSLayout::Dimension::Px(20);
    return label;
}

std::shared_ptr<UltraCanvasContainer> MakeSliderRow(const std::string& id, const std::string& caption,
                                                    const std::shared_ptr<UltraCanvasSlider>& slider,
                                                    const std::shared_ptr<UltraCanvasLabel>& value, int indent,
                                                    int labelWidth) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(32);
    row->SetPadding(0, 0, 0, indent);
    auto label = MakeLabel(id + "-label", caption);
    label->size.width = CSSLayout::Dimension::Px(labelWidth);
    label->size.height = CSSLayout::Dimension::Px(20);
    row->AddChild(label);
    row->AddChild(slider);
    row->AddChild(value);
    return row;
}

std::shared_ptr<UltraCanvasButton> MakeColorBox(const std::string& id, const Color& color, const std::string& tooltip,
                                                std::function<void()> onClick) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 56, 24, "");
    b->SetCornerRadius(3.0f);
    // Same colour in every state: a colour box that changes shade under the
    // pointer no longer shows the colour it stands for.
    b->SetColors(color, color, color, color);
    b->SetBorder(1.0f, Color(120, 120, 128, 255));
    b->SetTooltip(tooltip);
    if (onClick) b->SetOnClick(std::move(onClick));
    b->size.width = CSSLayout::Dimension::Px(56);
    b->size.height = CSSLayout::Dimension::Px(24);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

void SetColorBoxColor(const std::shared_ptr<UltraCanvasButton>& box, const Color& color) {
    if (!box) return;
    box->SetColors(color, color, color, color);
    box->RequestRedraw();
}

// ===== DIALOGS =====

void ShowSaveSettingsDialog(const std::string& extensionOrPath, UltraCanvasWindowBase* parent) {
    UltraCanvasFileLoader::ShowSaveSettingsDialog(extensionOrPath, FileSavePicture(), FileSaveImageSize(), parent,
                                                  nullptr, kTextFontSize);
}

// ===== THE WINDOW =====

bool UltraCanvasSettingsWindow::Create(UltraCanvasWindowBase* parent, const std::string& title, int width,
                                       int height) {
    WindowConfig wc;
    wc.title = title;
    wc.width = width;
    wc.height = height;
    wc.resizable = true;
    wc.type = WindowType::Dialog;
    wc.parentWindow = parent;
    window = CreateWindow(wc);
    if (!window || !window->IsCreated()) {
        window.reset();
        return false;
    }
    closed = false;
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(kWindowBackground);

    // ----- settings-page tree | page area -----
    auto content = std::make_shared<UltraCanvasContainer>("settings-content");
    content->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    tree = std::make_shared<UltraCanvasTreeView>("settings-tree");
    tree->SetFontSize(kTreeFontSize);
    tree->SetRowHeight(24);
    tree->SetSelectionMode(TreeSelectionMode::Single);
    tree->SetLineStyle(TreeLineStyle::NoLine);
    tree->SetBackgroundColor(kTreeBackground);
    // A section has no page of its own - it shows its first page - so
    // selecting one moves straight on to that first entry rather than
    // leaving two rows that show the same thing.
    tree->SetShowFirstChildOnExpand(true);
    tree->SetAutoExpandSelectedNode(true);
    tree->size.width = CSSLayout::Dimension::Px(kTreeWidth);
    tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // The root row itself is not shown (Finish hides it, once its children
    // exist): the sections are the top level of the list.
    TreeNodeData rootData;
    rootData.nodeId = "settings";
    rootData.text = "Settings";
    tree->SetRootNode(rootData);
    content->AddChild(tree);

    pageArea = std::make_shared<UltraCanvasContainer>("settings-pages");
    pageArea->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pageArea->SetBackgroundColor(kPageBackground);
    content->AddChild(pageArea);
    window->AddChild(content);

    // ----- bottom bar: [Restore default ...]            [Close] -----
    auto bottom = std::make_shared<UltraCanvasContainer>("settings-bottom");
    bottom->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bottom->SetPadding(8, 12, 8, 12);
    bottom->SetBorderTop(1, Color(225, 225, 230, 255));
    // The left half holds the page's restore button and takes the width the
    // Close button leaves, so Close keeps its right-hand place whether or
    // not a restore button is showing.
    auto bottomLeft = std::make_shared<UltraCanvasContainer>("settings-bottom-left");
    bottomLeft->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottomLeft->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    restoreButton = MakeButton("settings-restore", "Restore defaults", 170, [this]() {
        auto it = resets.find(shownPage);
        if (it != resets.end() && it->second.action) it->second.action();
    });
    restoreButton->SetVisible(false);
    bottomLeft->AddChild(restoreButton);
    bottom->AddChild(bottomLeft);
    bottom->AddChild(MakeButton("settings-close", "Close", 90, [this]() {
        if (window) window->Close();
    }));
    window->AddChild(bottom);

    // Escape closes, matching the framework's dialog convention.
    window->SetEventCallback([](const UCEvent& event) {
        if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
            if (auto tw = event.targetWindow.lock()) static_cast<UltraCanvasWindow*>(tw.get())->Close();
            return true;
        }
        return false;
    });
    window->onWindowClosed = [this]() {
        closed = true;
        if (onClosed) onClosed();
    };
    return true;
}

void UltraCanvasSettingsWindow::AddSection(const std::string& id, const std::string& text) {
    AddTreeNode("settings", id, text);
}

void UltraCanvasSettingsWindow::AddTreeNode(const std::string& parentId, const std::string& id,
                                            const std::string& text) {
    if (!tree) return;
    TreeNodeData data;
    data.nodeId = id;
    data.text = text;
    tree->AddNode(parentId, data);
}

void UltraCanvasSettingsWindow::AddPage(const std::string& pageId, const std::shared_ptr<UltraCanvasContainer>& page) {
    if (!pageArea || !page) return;
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pages[pageId] = page;
    pageArea->AddChild(page);
}

void UltraCanvasSettingsWindow::Finish(const std::string& startPage) {
    if (!window || !tree) return;
    // After the nodes: hiding the root promotes its children to the top
    // level, which it can only do once they exist. The sections themselves
    // stay closed - the window opens on the list of sections, and opening
    // one is the first step of finding a setting.
    tree->SetRootVisible(false);
    tree->onNodeSelected = [this](TreeNode* node) {
        if (!node) return;
        // A section has no page of its own: selecting one moves on to its
        // first page. The tree jumps one level per selection, so this takes
        // the remaining steps.
        TreeNode* leaf = node;
        while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
        if (leaf && leaf != node) {
            tree->SelectNode(leaf);
            return;
        }
        ShowPage(node->data.nodeId);
    };
    ShowPage(startPage);
    window->Show();
}

void UltraCanvasSettingsWindow::UpdateRestoreButton() {
    if (!restoreButton) return;
    auto it = resets.find(shownPage);
    if (it == resets.end()) {
        restoreButton->SetVisible(false);
        restoreButton->RequestRedraw();
        return;
    }
    restoreButton->SetText(it->second.label);
    restoreButton->size.width = CSSLayout::Dimension::Px(it->second.width);
    restoreButton->SetVisible(true);
    restoreButton->RequestRedraw();
}

void UltraCanvasSettingsWindow::ShowPage(const std::string& nodeId) {
    std::string target = nodeId;
    if (!pages.count(target)) {
        // Fall back to the first page below this node in tree order.
        TreeNode* node = tree ? tree->FindNode(nodeId) : nullptr;
        std::function<TreeNode*(TreeNode*)> firstPage = [&](TreeNode* n) -> TreeNode* {
            if (!n) return nullptr;
            if (pages.count(n->data.nodeId)) return n;
            for (auto& child : n->children)
                if (TreeNode* hit = firstPage(child.get())) return hit;
            return nullptr;
        };
        TreeNode* hit = firstPage(node);
        if (!hit) return;
        target = hit->data.nodeId;
    }
    shownPage = target;
    for (auto& [id, page] : pages) page->SetVisible(id == target);
    if (onPageShown) onPageShown(target);
    UpdateRestoreButton();
}

void UltraCanvasSettingsWindow::SelectPage(const std::string& pageId) {
    TreeNode* node = tree ? tree->FindNode(pageId) : nullptr;
    if (node) {
        // The sections start closed, so the one holding this page has to be
        // opened for its row to be on screen at all.
        for (TreeNode* parent = node->parent; parent; parent = parent->parent) tree->ExpandNode(parent);
        tree->SelectNode(node);
    }
    ShowPage(pageId);
}

} // namespace SettingsPageKit
} // namespace UltraCanvas
