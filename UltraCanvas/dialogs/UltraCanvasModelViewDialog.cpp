// dialogs/UltraCanvasModelViewDialog.cpp
// The 3D import dialog. The viewer does the 3D work — it is the framework's
// own media viewer with its top bars off, so orbiting, zooming and reading
// every model format the build has come for free — and this adds the two
// questions a bitmap needs answering: how big, and on what background.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasModelViewDialog.h"

#include "UltraCanvasFormLayout.h"
#include "CSSLayout/CSSLayout.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace UltraCanvas {

    namespace {
        constexpr int kPadding = 14;
        constexpr float kMinButtonWidth = 96.0f;

        std::string FileNameOnly(const std::string& path) {
            if (path.empty()) return "Model";
            return std::filesystem::path(path).filename().string();
        }

        std::string TriangleCountText(size_t triangles) {
            return std::to_string(triangles) + (triangles == 1 ? " triangle" : " triangles");
        }

        void SizeButtonToText(const std::shared_ptr<UltraCanvasButton>& button) {
            if (!button) return;
            button->size.width = CSSLayout::Dimension::Auto();
            CSSLayout::BoxConstraints limits = button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
            limits.minWidth = CSSLayout::Dimension::Px(kMinButtonWidth);
            button->boxConstraints = limits;
            button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        }
    }

    UltraCanvasModelViewDialog::UltraCanvasModelViewDialog(
            const std::string& modelPath, const std::vector<ModelViewAction>& actions)
            : UltraCanvasWindow(), path(modelPath) {
        config_.title = "Import 3D model - " + FileNameOnly(path);
        config_.width = 760;
        config_.height = 640;
        config_.minWidth = 520;
        config_.minHeight = 420;
        config_.resizable = true;
        config_.deleteOnClose = true;

        SetPadding(kPadding);
        SetBackgroundColor(Color(250, 250, 252, 255));
        layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // ----- the viewer -----
        viewer = CreateMediaViewer("ModelViewDialogViewer", 0, 0, 0, 0);
        // Embedded mode: no breadcrumb, no toolbars, and it does not steal the
        // keyboard from the size fields.
        viewer->SetTopBarsVisible(false);
        viewer->SetGrabFocusOnAttach(false);
        viewer->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(viewer);
        viewer->OpenFile(path);

        auto hint = std::make_shared<UltraCanvasLabel>("ModelViewHint", -1, -1, -1, -1,
                "Drag to turn the model, wheel to zoom. The bitmap is taken from the view shown here.");
        hint->SetFontSize(11);
        hint->SetTextColor(Color(100, 100, 110, 255));
        hint->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(hint);

        // ----- what the bitmap should be -----
        auto form = CreateFormGrid("ModelViewForm", 8.0f, 12.0f);
        AddChild(form);

        auto sizeRow = CreateFormCellRow("ModelViewSizeRow", 8.0f);
        widthSpin = CreateIntSpinner("ModelViewWidth", 0, 0, 92, 26, 1, 20000, 1024, 1);
        widthSpin->SetSuffix(" px");
        widthSpin->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        widthSpin->onValueChanged = [this](double) { SyncFromWidth(); };
        auto times = std::make_shared<UltraCanvasLabel>("ModelViewX", -1, -1, -1, -1, "x");
        times->SetTextColor(Color(100, 100, 110, 255));
        heightSpin = CreateIntSpinner("ModelViewHeight", 0, 0, 92, 26, 1, 20000, 768, 1);
        heightSpin->SetSuffix(" px");
        heightSpin->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        heightSpin->onValueChanged = [this](double) { SyncFromHeight(); };
        sizeRow->AddChild(widthSpin);
        sizeRow->AddChild(times);
        sizeRow->AddChild(heightSpin);

        summaryLabel = std::make_shared<UltraCanvasLabel>("ModelViewSummary", -1, -1, -1, -1, "");
        summaryLabel->SetFontSize(11);
        summaryLabel->SetTextColor(Color(100, 100, 110, 255));
        summaryLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        sizeRow->AddSpacer(12);
        sizeRow->AddChild(summaryLabel);
        AddFormRow(form, "ModelViewSize", "Bitmap size:", sizeRow);

        backgroundDrop = CreateDropdown("ModelViewBackground", 0, 0, 180, 26);
        for (const char* name : { "Transparent", "White", "Black" }) backgroundDrop->AddItem(name);
        backgroundDrop->SetSelectedIndex(0, false);
        AddFormRow(form, "ModelViewBg", "Background:", backgroundDrop);

        // The model's own numbers, so the user knows what they are importing.
        const ModelSourceInfo info = InspectModelFile(path);
        if (info.ok) {
            summaryLabel->SetText(TriangleCountText(info.triangleCount));
        } else if (!info.error.empty()) {
            summaryLabel->SetText(info.error);
        }

        // ----- buttons -----
        auto row = std::make_shared<UltraCanvasContainer>("ModelViewButtons", 0, 0, 0, 36);
        row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                   .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto reset = std::make_shared<UltraCanvasButton>("ModelViewReset", 0, 0, 110, 30, "Reset view");
        reset->SetTooltip("Back to the framing the viewer opened at");
        reset->onClick = [this]() {
            if (viewer) viewer->SetModelViewPose(ModelViewPose::Default());
        };
        SizeButtonToText(reset);
        row->AddChild(reset);
        row->AddStretchSpacer(1);

        auto cancel = std::make_shared<UltraCanvasButton>("ModelViewCancel", 0, 0, 96, 30, "Cancel");
        cancel->onClick = [this]() { PerformClose(); };
        SizeButtonToText(cancel);
        row->AddChild(cancel);

        std::vector<ModelViewAction> accepts = actions;
        if (accepts.empty()) accepts.push_back(ModelViewAction{"open", "Open", true});
        for (const auto& action : accepts) {
            auto button = std::make_shared<UltraCanvasButton>("ModelViewAction-" + action.id,
                                                              0, 0, 120, 30, action.label);
            if (action.primary) button->SetStyle(ButtonStyles::PrimaryStyle());
            const std::string id = action.id;
            button->onClick = [this, id]() { Finish(id); };
            SizeButtonToText(button);
            row->AddChild(button);
        }
        AddChild(row);
    }

    void UltraCanvasModelViewDialog::SyncFromWidth() {
        if (syncing || !widthSpin || !heightSpin) return;
        syncing = true;
        heightSpin->SetValue(std::max(1.0, std::round(widthSpin->GetValue() / aspect)));
        syncing = false;
    }

    void UltraCanvasModelViewDialog::SyncFromHeight() {
        if (syncing || !widthSpin || !heightSpin) return;
        syncing = true;
        widthSpin->SetValue(std::max(1.0, std::round(heightSpin->GetValue() * aspect)));
        syncing = false;
    }

    RasterPixel UltraCanvasModelViewDialog::BackgroundColour() const {
        switch (backgroundDrop ? backgroundDrop->GetSelectedIndex() : 0) {
            case 1:  return RasterPixel(255, 255, 255, 255);
            case 2:  return RasterPixel(0, 0, 0, 255);
            default: return RasterPixel(0, 0, 0, 0);
        }
    }

    ModelViewResult UltraCanvasModelViewDialog::Collect(const std::string& actionId) const {
        ModelViewResult result;
        result.actionId = actionId;
        result.width = widthSpin ? static_cast<int>(widthSpin->GetValue()) : 1024;
        result.height = heightSpin ? static_cast<int>(heightSpin->GetValue()) : 768;
        if (viewer) viewer->GetModelViewPose(result.pose);
        result.background = BackgroundColour();
        return result;
    }

    std::shared_ptr<UCRasterLayer> UltraCanvasModelViewDialog::Rasterize(std::string& error) const {
        const ModelViewResult result = Collect(std::string());
        ModelRasterOptions options;
        options.width = result.width;
        options.height = result.height;
        options.pose = result.pose;
        options.background = result.background;

        // The viewer already read the file; rasterize the mesh it is holding
        // rather than parsing the model a second time.
        if (viewer) {
            if (const Mesh3D* mesh = viewer->GetModelMesh()) {
                if (!mesh->Empty()) {
                    auto layer = RasterizeMesh(*mesh, options, error);
                    if (layer) layer->name = FileNameOnly(path);
                    return layer;
                }
            }
        }
        return RasterizeModelFile(path, options, error);
    }

    void UltraCanvasModelViewDialog::Finish(const std::string& actionId) {
        // Same order as the other dialogs: the callback runs while the dialog
        // is still alive (it reads the pose out of the viewer), then the
        // window closes itself.
        if (onAccept) onAccept(Collect(actionId));
        PerformClose();
    }

    std::shared_ptr<UltraCanvasModelViewDialog> ShowModelViewDialog(
            const std::string& path,
            const std::vector<ModelViewAction>& actions,
            UltraCanvasWindowBase* parent,
            std::function<void(UltraCanvasModelViewDialog&, const ModelViewResult&)> onAccept) {
        auto dialog = std::make_shared<UltraCanvasModelViewDialog>(path, actions);
        if (onAccept) {
            // Finish() calls this on itself and closes afterwards, so the
            // reference is alive for exactly as long as the callback runs.
            UltraCanvasModelViewDialog* self = dialog.get();
            dialog->onAccept = [self, accept = std::move(onAccept)](const ModelViewResult& result) {
                accept(*self, result);
            };
        }
        dialog->Create();
        if (parent) {
            dialog->SetTransientParent(parent);
            dialog->CenterOnParent(parent);
        }
        dialog->Show();
        return dialog;
    }

} // namespace UltraCanvas
