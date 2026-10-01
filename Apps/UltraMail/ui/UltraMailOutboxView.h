// Apps/UltraMail/ui/UltraMailOutboxView.h
// The Outbox window: the messages waiting to be sent - to, subject, the
// account they go from, how often they were tried and why they have not gone
// out yet - with Send now, Edit and Delete. A message that can never be sent
// (a recipient the server rejects) is deleted or corrected here instead of
// being tried every 30 minutes for ever.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before engine headers (X11 macro ordering).
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"

#include "UltraMailOutbox.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace UltraMail {

class OutboxView {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Resize(float width, float height);
    // Release the elements (the window closed).
    void Release();

    // The waiting messages; `sending` = a pass is running now, so Edit and
    // Delete wait (the pass may be sending the very message). `held`: the
    // ones open in a compose window to be corrected.
    void SetItems(const std::vector<OutboxItem>& items, bool sending,
                  const std::set<int64_t>& held = {});

    std::function<void()> onSendNow;
    std::function<void(int64_t id)> onEdit;
    std::function<void(int64_t id)> onDelete;
    std::function<void()> onClose;

private:
    int64_t SelectedId() const;   // 0 = none
    void UpdateButtons();

    std::vector<OutboxItem> items_;
    bool sending_ = false;
    std::set<int64_t> held_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> model_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> summary_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> sendNow_, edit_, delete_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
};

} // namespace UltraMail
