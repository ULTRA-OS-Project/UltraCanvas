// Apps/UltraToDo/ui/SettingsDialog.cpp
// See SettingsDialog.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SettingsDialog.h"

#include "ToDoTheme.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRadio.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraToDo {

namespace {
// One choice: the radio, and under it what it means, indented to the
// radio's text.
std::shared_ptr<UltraCanvasContainer> Choice(const std::string& id, const std::shared_ptr<UltraCanvasRadio>& radio,
                                             const std::string& meaning) {
    auto box = CreateContainer(id, 0, 0, 0, 0);
    box->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    box->size.height = CSSLayout::Dimension::Auto();
    box->layoutItem.SetFlexShrink(0);
    box->AddChild(radio);
    auto text = Theme::MakeRoleWrapped(id + ".meaning", meaning, Theme::SizeSmall(), LabelVariant::Muted);
    text->SetMargin(0, 0, 0, 26);
    box->AddChild(text);
    return box;
}
} // namespace

void SettingsDialog::Show(UltraCanvasWindowBase* parent, const ToDoSettings& settings,
                          std::function<void(const ToDoSettings&)> onSave) {
    DialogConfig config;
    config.title      = Tr("UltraToDo settings");
    config.width      = 520;
    config.height     = 340;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    config.showIcon   = false;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);

    dialog->AddChild(Theme::MakeRoleLine("setTitle", Tr("Settings"), 28, Theme::SizeTitle(), LabelVariant::Heading,
                                         FontWeight::Bold));

    // ----- level of management -----
    dialog->AddChild(Theme::MakeRoleLine("setManagement", Tr("Level of management"), 22, Theme::SizeHeading(),
                                         LabelVariant::Body, FontWeight::Bold));
    const bool supervised = settings.Supervised();
    auto simple = std::make_shared<UltraCanvasRadio>("setSimple", 0, 0, 0, Theme::kControlHeight, Tr("Simple"));
    auto watched = std::make_shared<UltraCanvasRadio>("setSupervised", 0, 0, 0, Theme::kControlHeight,
                                                      Tr("With task supervision"));
    simple->SetChecked(!supervised);
    watched->SetChecked(supervised);
    // The group keeps the two exclusive; it lives as long as the dialog's
    // answer can come (the result handler below holds it).
    auto group = std::make_shared<UltraCanvasRadioGroup>();
    group->AddRadioButton(simple);
    group->AddRadioButton(watched);
    dialog->AddChild(Choice("setSimpleChoice", simple,
                            Tr("Tasks with their planned end, the time they need, reminders and alarms.")));
    dialog->AddChild(Choice("setSupervisedChoice", watched,
                            Tr("As Simple, and UltraToDo follows the work: a Start button to say you have begun a "
                               "task, an orange ring blinking round a task to start now and a red border "
                               "blinking round an overdue one.")));

    auto spacer = CreateContainer("setSpacer", 0, 0, 0, 0);
    spacer->layoutItem.SetFlexGrow(1);
    dialog->AddChild(spacer);

    // ----- Cancel / Save -----
    auto buttons = CreateContainer("setButtons", 0, 0, 0, Theme::kControlHeight + 4);
    buttons->layout.SetFlexRow().SetFlexGap(Theme::kInnerGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    buttons->AddStretchSpacer(1);
    auto cancel = Theme::MakePill("setCancel", Tr("Cancel"), 90);
    cancel->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttons->AddChild(cancel);
    auto save = Theme::MakePill("setSave", Tr("Save"), 90, /*main=*/true);
    save->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttons->AddChild(save);
    dialog->AddChild(buttons);

    dialog->SetFollowTheme(true);
    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [settings, group, watched, onSave](DialogResult result) {
            if (result != DialogResult::OK || !onSave) return;
            ToDoSettings out = settings;
            out.management = watched->IsChecked() ? ManagementLevel::Supervised : ManagementLevel::Simple;
            onSave(out);
        },
        parent);
}

} // namespace UltraToDo
