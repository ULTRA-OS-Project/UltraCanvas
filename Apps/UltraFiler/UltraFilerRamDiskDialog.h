// Apps/UltraFiler/UltraFilerRamDiskDialog.h
// "+ Drive > RAM disc..." - the dialog that asks what to call a new RAM disc
// and how large to make it. The sizes offered stop at what the machine can
// give right now, and the note under them says what the size means on this
// platform (reserved up front on macOS and with ImDisk, a ceiling shared by
// all discs on Linux, not memory at all on Windows without ImDisk).
//
// What the dialog shows about the machine - the memory free now and a name
// no disc uses yet - is gathered by the caller (RamDiskDialogFacts) and
// handed in, not read here: reading it means a hardware-information capture
// and a pass over the mounted discs, which the window does on a worker
// thread before opening the dialog, so the UI thread never waits on either.
// Version: 1.1.0 - the facts about the machine come from the caller
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

// What the dialog needs to know about the machine, read off the UI thread.
struct RamDiskDialogFacts {
    // Most a new disc can hold (UltraFilerRamDisks::LargestPossibleBytes);
    // 0 when it could not be read, and every size is then offered.
    uint64_t largestBytes = 0;
    // The name offered in the name field (UltraFilerRamDisks::SuggestName).
    std::string suggestedName;
};

class UltraFilerRamDiskDialog : public UltraCanvasModalDialog {
public:
    UltraFilerRamDiskDialog() = default;

    void Initialize(const RamDiskDialogFacts& facts);

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

// Build and show the dialog; onCreate fires only on Create. `facts` is
// what GatherRamDiskDialogFacts answers - call that on a worker thread.
std::shared_ptr<UltraFilerRamDiskDialog> ShowRamDiskDialog(
    const RamDiskDialogFacts& facts,
    std::function<void(const std::string& name, uint64_t sizeBytes)> onCreate,
    UltraCanvasWindowBase* parent);

// Reads what the dialog shows about the machine. Safe on any thread; meant
// for a worker, since it captures hardware information and lists the discs.
RamDiskDialogFacts GatherRamDiskDialogFacts();

}  // namespace UltraCanvas
