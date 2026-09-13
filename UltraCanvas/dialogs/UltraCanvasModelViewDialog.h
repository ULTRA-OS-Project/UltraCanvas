// dialogs/UltraCanvasModelViewDialog.h
// "Turn this 3D model into a bitmap" — the dialog that asks *which view*.
//
// A model has no picture of its own, so a size is not enough: somebody has to
// choose where the camera stands. This shows the model in an
// UltraCanvasMediaViewer (drag to orbit, wheel to zoom — the framework's own
// 3D pane, so every format the build can read opens here), lets the user pick
// the raster size and the background, and hands back both the pose and a
// ready-made UCRasterLayer of exactly what was on screen.
//
// The accept buttons are the caller's: an editor that asked because a file was
// dropped offers "Merge image" and "Open new window", one that asked from a
// menu offers "Open". Cancel is always there and is never reported.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasContainer.h"
#include "../include/UltraCanvasLabel.h"
#include "../include/UltraCanvasButton.h"
#include "../include/UltraCanvasSpinner.h"
#include "../include/UltraCanvasDropdown.h"
#include "../include/UltraCanvasMediaViewer.h"
#include "../include/UltraCanvasModelRaster.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // One accept button.
    struct ModelViewAction {
        std::string id;        // reported back, so the caller knows which was pressed
        std::string label;
        bool primary = false;  // drawn as the default action
    };

    // What the user settled on.
    struct ModelViewResult {
        std::string actionId;
        int width = 1024;
        int height = 768;
        ModelViewPose pose;
        RasterPixel background = RasterPixel(0, 0, 0, 0);
    };

    class UltraCanvasModelViewDialog : public UltraCanvasWindow {
    public:
        // `path` is opened in the viewer; `actions` default to a single
        // "open" button when left empty.
        UltraCanvasModelViewDialog(const std::string& path,
                                   const std::vector<ModelViewAction>& actions = {});

        // The chosen view rendered into a layer, from the mesh the viewer
        // already holds — the file is not read again. Null with `error` set
        // when the model cannot be drawn.
        std::shared_ptr<UCRasterLayer> Rasterize(std::string& error) const;

        // Runs when an accept button is pressed; Cancel closes silently.
        std::function<void(const ModelViewResult&)> onAccept;

        const std::string& GetPath() const { return path; }

    private:
        ModelViewResult Collect(const std::string& actionId) const;
        void Finish(const std::string& actionId);
        void SyncFromWidth();
        void SyncFromHeight();
        RasterPixel BackgroundColour() const;

        std::string path;
        bool syncing = false;
        double aspect = 4.0 / 3.0;

        std::shared_ptr<UltraCanvasMediaViewer> viewer;
        std::shared_ptr<UltraCanvasSpinner> widthSpin, heightSpin;
        std::shared_ptr<UltraCanvasDropdown> backgroundDrop;
        std::shared_ptr<UltraCanvasLabel> summaryLabel;
    };

    // Creates, shows and returns the dialog, parented to `parent` when given.
    // The callback gets the dialog as well as the answer, because the thing a
    // caller actually wants - Rasterize() - lives on it, and it is only alive
    // while the callback runs.
    std::shared_ptr<UltraCanvasModelViewDialog> ShowModelViewDialog(
            const std::string& path,
            const std::vector<ModelViewAction>& actions,
            UltraCanvasWindowBase* parent,
            std::function<void(UltraCanvasModelViewDialog&, const ModelViewResult&)> onAccept);

} // namespace UltraCanvas
