// Apps/UltraDBAdmin/ui/DbAdminBlobDialog.h
// A blob, looked at: what it is (SniffBlob, from its bytes), and then the
// picture when it is one, the text when it reads as text, a hex dump
// otherwise. Save to file writes the bytes as they are; Load from file,
// offered only where the cell can be edited, stages a file's bytes for it.
// The dialog does neither itself: it hands the request back to the window,
// which owns the file dialogs and the staged changes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraDBAdmin {

// `title` names the cell ("data of row 3"). `onSave` is always offered;
// `onLoad`, when set, adds Load from file. Both close the dialog first.
// Non-blocking.
void ShowBlobDialog(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& title,
                    const std::vector<uint8_t>& bytes, std::function<void()> onSave,
                    std::function<void()> onLoad);

} // namespace UltraDBAdmin
