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
// as wide as the page's content column that grows with what is put in it:
// a captioned field (AddField - the caption at the left, the input beside
// it, as wide as the rest of the column unless it has a width of its own),
// a note or a line of text across it (AddNote), a button (AddButton). The
// rows take the page's row height, so no application lays out a form by
// hand; a section made with a height holds what the application places in
// it from (0, 0) itself.
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
//   page->AddSection("A new site");                                   // the app's own fields
//   auto name = UltraCanvas::CreateTextInput("name", 0, 0, 0, 0);
//   page->AddField("Site name", name, 96);                             // caption, the input beside it
//   page->AddButton("Create it in a folder\xE2\x80\xA6", []() { PickFolder(); }, 300);
//   page->AddNote("The starter site goes into the folder you choose.", 40);
//   page->AddStatusLine();
//   page->Say("The folder could not be read.", true);
//   window->AddChild(page);
//   window->SetWindowSize(620, page->PreferredHeight());
//
// Version: 1.1.1 - a recent entry too long for its button loses its middle, not its end
// Version: 1.1.0 - AddField, AddNote and AddButton lay out a section's rows; a section grows with them
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
        // shows its last path part and the whole entry (shortened in the
        // middle where it does not fit, so the drive and the last part stay;
        // the whole entry as the tooltip) and calls `onPick` with the entry.
        // At most `shown` of them; nothing is added for an empty list.
        void SetRecent(const std::string& heading, const std::vector<std::string>& entries,
                       std::function<void(const std::string&)> onPick, int shown = 5);

        // The action that opens an existing thing: the primary button, as
        // wide as the column, and a muted note under it when `note` is
        // given. Returns the button.
        std::shared_ptr<UltraCanvasButton> AddAction(const std::string& text, std::function<void()> onClick,
                                                     const std::string& note = std::string(), bool primary = true);

        // A section for the application's own fields: a heading and a
        // container as wide as the content column. With a `height` the
        // application places its fields in it from (0, 0) itself; without
        // one the section grows with the rows the three calls below add to
        // it (the section added last, until the next part is added).
        std::shared_ptr<UltraCanvasContainer> AddSection(const std::string& heading, int height = 0);
        // A row of the current section: `caption` in a label `captionWidth`
        // wide at the left, `input` beside it - as wide as the rest of the
        // column and a row tall unless it has a width of its own, in which
        // case it keeps its size. Returns the row's y in the section.
        int AddField(const std::string& caption, const std::shared_ptr<UltraCanvasUIElement>& input,
                     int captionWidth = 190);
        // A line of text across the current section, `height` tall, muted
        // and wrapping: a note under the fields, or the reason a button is
        // greyed (set its text later). Returns the label.
        std::shared_ptr<UltraCanvasLabel> AddNote(const std::string& text, int height = 40);
        // A button in the current section, `width` wide (0: the column),
        // styled as the secondary action. Returns the button.
        std::shared_ptr<UltraCanvasButton> AddButton(const std::string& text, std::function<void()> onClick,
                                                     int width = 0);
        // The row height the page lays fields and buttons out with.
        static int RowHeight() { return 34; }

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
        // Makes the current section `height` taller (and the page with it)
        // and returns the y the new row goes at; -1 without a section.
        int GrowSection(int height);

        int width_;
        int y_;
        int next_ = 0;   // the next part's number, for the identifiers
        std::shared_ptr<UltraCanvasLabel> title_;
        std::shared_ptr<UltraCanvasLabel> hint_;
        std::shared_ptr<UltraCanvasLabel> status_;
        std::shared_ptr<UltraCanvasContainer> section_;   // the one added last, while it is the last part
        int sectionHeight_ = 0;
    };

} // namespace UltraCanvas
