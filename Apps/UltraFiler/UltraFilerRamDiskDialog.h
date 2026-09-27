// Apps/UltraFiler/UltraFilerRamDiskDialog.h
// "+ Drive > RAM disc..." - the dialog that asks what to call a new RAM disc
// and how large to make it. The sizes offered stop at what the machine can
// give right now, and the note under them says what the size means on this
// platform (reserved up front on macOS and with ImDisk, a ceiling shared by
// all discs on Linux, not memory at all on Windows without ImDisk).
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraFilerRamDiskDialog : public UltraCanvasModalDialog {
public:
    UltraFilerRamDiskDialog() = default;

    void Initialize();

    // Invoked on Create with a valid name and the chosen size in bytes.
    std::function<void(const std::string& name, uint64_t sizeBytes)> onCreate;

protected:
    // The size, not the name: the name is filled in already, and the size is
    // the choice this dialog exists for.
    void FocusInitialElement() override;

private:
    // Hands name and size to onCreate and closes - or, for a name that
    // cannot be used, says why and stays open.
    void Accept();

    std::shared_ptr<UltraCanvasTextInput> nameInput_;
    std::shared_ptr<UltraCanvasDropdown>  sizeDropdown_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;
    std::vector<uint64_t>                 sizes_;   // per dropdown row
};

// Build and show the dialog; onCreate fires only on Create.
std::shared_ptr<UltraFilerRamDiskDialog> ShowRamDiskDialog(
    std::function<void(const std::string& name, uint64_t sizeBytes)> onCreate,
    UltraCanvasWindowBase* parent);

}  // namespace UltraCanvas
