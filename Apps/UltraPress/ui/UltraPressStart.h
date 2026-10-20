// Apps/UltraPress/ui/UltraPressStart.h
// The window that appears when there is no site to show yet: open a site
// folder, or create a new site in one.
//
// Started with no argument - a double-click on the program - ultrapress
// used to print its usage to a console and end, and on Windows that
// console closed again before anybody could read it, which looked exactly
// like a crash. A folder that held no site ended the same way. Now both
// open this window, which says why, and offers the two ways on: the
// folder dialog onto an existing site, or a name and a folder for a new
// one (the starter site `ultrapress new` writes). The editor opens from
// here; this window closes itself only once it has, because the
// application ends the moment it has no window left. The same move as
// UltraFIBU's start window - and the same page: the framework's
// UltraCanvasStartPage, with UltraPress's words and its one field.
// Version: 0.8.5 - BuildPage builds the page without a window, for the render tests
// Version: 0.8.4 - on the framework's UltraCanvasStartPage
// Version: 0.8.3 - a recent site's button names its whole path in a tooltip
// Version: 0.8.2 - the sites opened last, offered first
// Version: 0.8.1
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasStartPage.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraPress {

class StartWindow {
public:
    // Called with the site folder the user settled on: one just created, or
    // an existing one chosen. The caller opens it in the editor and returns
    // an empty string, or returns why it could not, which this window shows
    // instead of closing.
    std::function<std::string(const std::string& siteDir)> onSiteChosen;
    // Called with a recent folder that holds no site any more, so the caller
    // can drop it from the list (UltraPressSettings.h).
    std::function<void(const std::string& siteDir)> onSiteGone;

    // `hint` says why this window is showing instead of a site; `suggestion`
    // is the folder that was given and could not be opened, offered for a new
    // site; `recent` the folders opened last, first first, each a button
    // (the five newest). Returns the window, to Show().
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> Build(const std::string& hint, const std::string& suggestion,
                                                          const std::vector<std::string>& recent = {});
    // The page alone, without a window - what Build puts into one. A test
    // builds it without a display and reads it back.
    std::shared_ptr<UltraCanvas::UltraCanvasStartPage> BuildPage(const std::string& hint, const std::string& suggestion,
                                                                 const std::vector<std::string>& recent = {});
    void Close();
    // The new site's name as the field holds it.
    std::string SiteName() const;

private:
    void OpenExisting();
    void CreateNew();
    void HandOver(const std::string& siteDir);
    void Say(const std::string& text, bool error);

    std::string suggestion_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasStartPage> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> name_;
};

} // namespace UltraPress
