// Apps/UltraViewer/UltraViewerWindow.h
// UltraViewer - universal media viewer main window: one full-window
// UltraCanvasMediaViewer, which brings its own folder breadcrumb, toolbar
// rows (open / navigation / slideshow / zoom / rotate / adjust / save),
// adjustments panel, display views for every media kind and the bottom info
// bar. The window merely hosts it and feeds it the paths from the command
// line (or drag & drop, which the viewer widget handles itself).
//
// Media kinds displayed, all through the viewer widget:
//   - bitmaps and vector graphics (JPEG/PNG/WebP/AVIF/HEIC/SVG/… via the
//     image pipeline)
//   - video and audio with full player transport controls (play / pause /
//     seek / scrub / volume) through UltraCanvasVideoPlayerElement /
//     UltraCanvasAudioPlayerElement
//   - documents (PDF via MuPDF)
//   - e-books (EPUB / FB2 / MOBI / AZW) in the reading widget
//   - spreadsheets (ODS / CSV / TSV)
//   - 3D models (STL)
//   - text / source / markdown
//   - UltraCanvas Document containers (*.ucd): embedded preview thumbnail +
//     container details until the UCD v2 engine lands
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasMediaViewer.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraViewerWindow {
public:
    // Creates the window and the media viewer, then opens `paths`:
    // none  -> empty viewer (use the Open button or drop files onto it)
    // one   -> a folder browses that folder; a file browses its folder with
    //          the file shown first
    // many  -> exactly those files as the playlist (folders are expanded)
    bool Initialize(const std::vector<std::string>& paths);
    void Show();

private:
    std::shared_ptr<UltraCanvasWindow>      window;
    std::shared_ptr<UltraCanvasMediaViewer> viewer;
};

} // namespace UltraCanvas
