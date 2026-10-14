// Apps/UltraPress/ui/UltraPressWindow.h
// The UltraPress window: the site's pages and the current page's blocks on
// the left, the page rendered on real elements in the middle, the selected
// block's properties on the right. Every change goes into the content
// model and re-renders the preview; Save writes the site folder; Publish
// writes the static site and uploads it onto the site's host (an UltraCloud
// account, hosting/UltraPressHosting.h) on a worker thread.
// Version: 0.7.3 - a richtext block edited in place with the RichTextEdit
// Version: 0.7.0 - text edited in place on the page
// Version: 0.6.0 - the media library: Pick... on a picture property, Media library... in the side column
// Version: 0.5.0 - Publish and Host...
// Version: 0.3.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressMedia.h"
#include "UltraPressModel.h"
#include "UltraPressRender.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace UltraPress {

class Hosts;

class EditorWindow {
public:
    EditorWindow(Site site, std::string siteDirectory);
    ~EditorWindow();

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
    // Writes the static site under <site>/.ultrapress/publish and uploads
    // it onto the host site.json names - the hosts' default when it names
    // none - on a worker thread; the status line follows. Without a host,
    // ChooseHost. False when nothing was started.
    bool Publish();
    // UltraCloud's add-account dialog, limited to the web hosts; the host
    // added becomes the site's, and is saved.
    void ChooseHost();
    bool IsPublishing() const { return publishing; }

    // The media library. `Media()` lists the site's media folder with the
    // uses; AddMedia copies a file in and names it; RemoveMediaFile removes
    // an unused one. ShowMediaLibrary opens the dialog (browse), PickImage
    // opens it for the selected block's picture property `name`.
    std::vector<MediaFile> Media() const;
    bool AddMedia(const std::string& sourcePath, std::string& mediaPath, std::string& error);
    bool RemoveMediaFile(const std::string& mediaPath, std::string& error);
    void ShowMediaLibrary();
    void PickImage(const std::string& name);
    // Sets a property of the selected block and re-renders; false when no
    // block is selected.
    bool SetProperty(const std::string& name, UltraCanvas::JSONValue value);

    // Text edited in place: a click on a piece of text on the page (a
    // binding of the rendered page) selects its block and puts an input
    // where the text was - a single line for a title, an area for a
    // paragraph, the rich text editor with Done and Cancel for a richtext
    // block. Enter (Ctrl+Enter in an area) commits, Escape cancels, and
    // so does leaving the field; a commit writes the block and re-renders.
    const RenderedPage& Shown() const { return shown; }
    bool IsEditingText() const { return editing >= 0; }
    void BeginTextEdit(int binding);
    // True when the block was changed (and the page re-rendered); false
    // when the text was the same, and the label is simply back.
    bool CommitTextEdit();                        // what the field holds
    bool CommitTextEdit(const std::string& text); // this text (tests)
    void CancelTextEdit();

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

    int editing = -1;                      // the binding being edited, or -1
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> editInput;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> editArea;
    std::shared_ptr<UltraCanvas::UltraCanvasRichTextEdit> editRich;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> editButtons;   // Done / Cancel under the rich editor
    std::vector<std::string> editPictures;                             // the rich editor's pictures, by media index

    std::weak_ptr<UltraCanvas::UltraCanvasWindow> windowRef;
    std::shared_ptr<Hosts> hosts;          // opened on the first Publish or Host...
    std::thread publishWorker;
    std::atomic<bool> publishing{false};
};

} // namespace UltraPress
