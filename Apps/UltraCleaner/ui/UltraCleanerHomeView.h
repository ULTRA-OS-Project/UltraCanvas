// Apps/UltraCleaner/ui/UltraCleanerHomeView.h
// The page UltraCleaner opens on: how full each drive is, and what you can
// do about it.
//
// The point of the drive row is to answer "is this even a problem?" before
// the user commits to a scan — a disk at 40% needs nothing, and the app
// should say so rather than inviting a hunt. Each drive is an
// UltraCanvasCircularProgressChart, which is also what makes the number
// readable at a glance instead of as a percentage buried in a table.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerVolumes.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCleaner {

class HomeView {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float width,
                                                             float height);

    // Re-reads the drives and repaints the row.
    void Refresh();

    std::function<void()> onCleanSystemJunk;
    std::function<void()> onCleanPhotos;

private:
    void RebuildDrives();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> driveRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> verdictLabel_;
    std::vector<VolumeInfo> volumes_;
};

} // namespace UltraCleaner
