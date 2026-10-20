// UltraCanvas/include/UltraCanvasStartPage.h
// The page an application shows when it has nothing open yet: a title, the
// reason this page and not the thing itself, the entries opened last as
// buttons, the one action that opens an existing thing, a section of the
// application's own fields for making a new one, and a status line that
// says why something could not be done. UltraPress's start window (a site
// folder) and UltraFIBU's (a bookkeeping file) are both this page with
// their own words and their own "new" fields.
//
// The page lays its parts out top to bottom in the order they are added,
// at the width it was made with, and PreferredHeight() is what the window
// that holds it should be. Everything on it follows the framework theme
// (UltraCanvasTheme): the headings and notes by their label variant, the
// action as the primary button, the recent entries as standard ones.
//
// The application's own fields go into a section (AddSection), a container
// of the height the application names, with the fields placed in it from
// (0, 0) - the section is as wide as the page's content column.
//
// doc-check: void OpenSite(const std::string& dir); void PickFolder();
// doc-check: std::shared_ptr<UltraCanvas::UltraCanvasWindow> window;
// doc-check: example
//   auto page = std::make_shared<UltraCanvas::UltraCanvasStartPage>("start", 620);
//   page->SetTitle("UltraPress");
//   page->SetHint("No site is open yet. Open a site folder, or create a new site.");
//   page->SetRecent("Opened last", {"/home/erika/sites/shop"},
//                   [](const std::string& dir) { OpenSite(dir); });
//   page->AddAction("Open a site folder\xE2\x80\xA6", []() { PickFolder(); },
//                   "A folder with a site.json in it.");
//   auto form = page->AddSection("A new site", 90);   // the app's own fields, from (0, 0)
//   form->AddChild(UltraCanvas::CreateTextInput("name", 0, 0, 300, 34));
//   page->AddStatusLine();
//   page->Say("The folder could not be read.", true);
//   window->AddChild(page);
//   window->SetWindowSize(620, page->PreferredHeight());
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasStartPage : public UltraCanvasContainer {
    public:
        // A page `width` wide at (0, 0); it grows downward as parts are added.
        explicit UltraCanvasStartPage(const std::string& identifier, int width);

        // The heading at the top, and the sentence under it that says why
        // this page is showing (up to three lines).
        void SetTitle(const std::string& text);
        void SetHint(const std::string& text);

        // The entries opened last, the first one first, each a button that
        // shows its last path part and the whole entry (the whole entry as the
        // tooltip, where the button shortens it) and calls `onPick` with the
        // entry. At most `shown` of them; nothing is added for an empty list.
        void SetRecent(const std::string& heading, const std::vector<std::string>& entries,
                       std::function<void(const std::string&)> onPick, int shown = 5);

        // The action that opens an existing thing: the primary button, as
        // wide as the column, and a muted note under it when `note` is
        // given. Returns the button.
        std::shared_ptr<UltraCanvasButton> AddAction(const std::string& text, std::function<void()> onClick,
                                                     const std::string& note = std::string(), bool primary = true);

        // A section for the application's own fields: a heading and a
        // container `height` tall, as wide as the content column, with the
        // fields placed in it from (0, 0).
        std::shared_ptr<UltraCanvasContainer> AddSection(const std::string& heading, int height);

        // The status line, placed where it is added - last, as a rule; Say
        // writes it, muted, or in the theme's error colour.
        std::shared_ptr<UltraCanvasLabel> AddStatusLine();
        void Say(const std::string& text, bool error);

        // What the window holding the page should be, for everything added.
        int PreferredHeight() const;
        int Width() const { return width_; }
        // The margin the content column keeps from the page's edge, and the
        // column's width: for a field laid out beside the page's own parts.
        static int Margin() { return 24; }
        int ContentWidth() const { return width_ - 2 * Margin(); }

    private:
        void Grow(int height);

        int width_;
        int y_;
        int next_ = 0;   // the next part's number, for the identifiers
        std::shared_ptr<UltraCanvasLabel> title_;
        std::shared_ptr<UltraCanvasLabel> hint_;
        std::shared_ptr<UltraCanvasLabel> status_;
    };

} // namespace UltraCanvas
