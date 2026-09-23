// Apps/UltraAuthenticator/SettingsDialog.h
// The three things a user can change: when the vault locks itself, whether
// minimising locks it, and whether codes stay hidden until clicked.
//
// It edits a copy and hands the result back on Save; nothing is applied while
// the dialog is open, so Cancel is genuinely a no-op.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "Preferences.h"

#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSwitch.h"

#include <functional>
#include <memory>

namespace UltraCanvas {
namespace Authenticator {

class SettingsDialog : public UltraCanvasModalDialog {
public:
    SettingsDialog() = default;
    ~SettingsDialog() override = default;

    void CreateSettingsDialog(const Preferences& current);

    // The edited preferences. Called only on Save, and only when something
    // changed.
    std::function<void(const Preferences& edited)> onSave;

private:
    void Save();
    Preferences Collect() const;

    Preferences original_;

    std::shared_ptr<UltraCanvasDropdown> idleDropdown_;
    std::shared_ptr<UltraCanvasSwitch>   minimizeSwitch_;
    std::shared_ptr<UltraCanvasSwitch>   hideSwitch_;

    static constexpr long kDialogWidth  = 460;
    static constexpr long kDialogHeight = 330;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // SETTINGSDIALOG_H
