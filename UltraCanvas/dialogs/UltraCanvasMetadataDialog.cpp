// dialogs/UltraCanvasMetadataDialog.cpp
// The metadata popup. The text area does the work — Markdown mode lays the
// per-group tables out, and being a real text area it scrolls, selects and
// copies without any of that being written here.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasMetadataDialog.h"

#include "UltraCanvasClipboard.h"
#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {

    namespace {
        constexpr int kPadding = 16;
        constexpr float kMinButtonWidth = 96.0f;

        void SizeButtonToLabel(const std::shared_ptr<UltraCanvasButton>& button) {
            if (!button) return;
            // Hugs its own text so a translated caption fits, never narrower
            // than a comfortable minimum.
            button->size.width = CSSLayout::Dimension::Auto();
            CSSLayout::BoxConstraints limits = button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
            limits.minWidth = CSSLayout::Dimension::Px(kMinButtonWidth);
            button->boxConstraints = limits;
            button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        }
    }

    UltraCanvasMetadataDialog::UltraCanvasMetadataDialog(const std::string& subject,
                                                          std::string metadataText, bool markdown)
            : UltraCanvasWindow(), text(std::move(metadataText)) {
        config_.title = subject.empty() ? "Metadata" : "Metadata - " + subject;
        config_.width = 620;
        config_.height = 540;
        config_.minWidth = 360;
        config_.minHeight = 240;
        config_.resizable = true;
        config_.deleteOnClose = true;

        SetPadding(kPadding);
        SetBackgroundColor(Color(250, 250, 252, 255));
        layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        view = std::make_shared<UltraCanvasTextArea>("MetadataView", 0, 0, 0, 0);
        view->SetEditingMode(markdown ? TextAreaEditingMode::MarkdownHybrid
                                      : TextAreaEditingMode::PlainText);
        view->SetText(text, false);
        view->SetReadOnly(true);
        view->SetShowLineNumbers(false);
        view->SetWordWrap(true);
        view->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(view);

        auto footer = std::make_shared<UltraCanvasContainer>("MetadataFooter", 0, 0, 0, 36);
        footer->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        footer->AddStretchSpacer(1);

        copyButton = std::make_shared<UltraCanvasButton>("MetadataCopy", 0, 0, 96, 30);
        copyButton->SetText("Copy");
        copyButton->SetTooltip("Copy the whole listing to the clipboard");
        copyButton->onClick = [this]() {
            if (auto* clipboard = GetClipboard()) clipboard->SetText(text);
        };
        SizeButtonToLabel(copyButton);
        footer->AddChild(copyButton);

        closeButton = std::make_shared<UltraCanvasButton>("MetadataClose", 0, 0, 96, 30);
        closeButton->SetText("Close");
        closeButton->SetStyle(ButtonStyles::PrimaryStyle());
        closeButton->onClick = [this]() { PerformClose(); };
        SizeButtonToLabel(closeButton);
        footer->AddChild(closeButton);

        AddChild(footer);
    }

    std::shared_ptr<UltraCanvasMetadataDialog> ShowMetadataDialog(
            const std::string& subject, const std::string& text, bool markdown,
            UltraCanvasWindowBase* parent) {
        auto dialog = std::make_shared<UltraCanvasMetadataDialog>(subject, text, markdown);
        dialog->Create();
        if (parent) {
            dialog->SetTransientParent(parent);
            dialog->CenterOnParent(parent);
        }
        dialog->Show();
        return dialog;
    }

} // namespace UltraCanvas
