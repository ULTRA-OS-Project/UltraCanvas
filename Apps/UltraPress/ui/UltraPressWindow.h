// Apps/UltraPress/ui/UltraPressWindow.h
// The UltraPress window: the site's pages and the current page's blocks on
// the left, the page rendered on real elements in the middle, the selected
// block's properties on the right. Every change goes into the content
// model and re-renders the preview; Save writes the site folder.
// Version: 0.3.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressModel.h"
#include "UltraPressRender.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraPress {

class EditorWindow {
public:
    EditorWindow(Site site, std::string siteDirectory);

    // Builds the three columns into `window`; call once.
    void Build(const std::shared_ptr<UltraCanvas::UltraCanvasWindow>& window);

    // The page shown and edited; -1 selects no block.
    void ShowPage(const std::string& slug);
    void SelectBlock(int index);

    // Edits, each followed by a re-render and a rebuild of the lists.
    void AddBlock(const std::string& type);
    void MoveBlock(int index, int delta);
    void RemoveBlock(int index);
    bool Save();

    const Site& CurrentSite() const { return site; }
    const std::string& CurrentPage() const { return current; }
    int SelectedBlock() const { return selected; }

private:
    using Container = std::shared_ptr<UltraCanvas::UltraCanvasContainer>;

    Page* CurrentPageMutable();
    void RenderPreview();
    void RebuildSide();
    void RebuildProperties();
    void Later(std::function<void()> task);   // on the UI thread, after the current callback
    void SetStatus(const std::string& text);

    Site site;
    std::string siteDir;
    std::string current;
    int selected = -1;
    bool dirty = false;

    Container side, pageArea, properties;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status;
    RenderedPage shown;
};

} // namespace UltraPress
