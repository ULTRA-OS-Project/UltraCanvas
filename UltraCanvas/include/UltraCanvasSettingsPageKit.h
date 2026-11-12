// include/UltraCanvasSettingsPageKit.h
// The look of an application's settings window, once: the window with the
// tree of pages on the left and the chosen page on the right, the page's
// "Restore defaults" and Close at the foot; a page's skeleton - the title,
// the caption, the controls, the notes block at the foot - and the controls
// in the window's one type scale (a labelled radio, a checkbox, a button
// with an icon, a slider with its value, a colour box, a row). UltraFiler's
// settings window set the style; UltraSnip's is built on it, and so is the
// next application's, so every settings window reads as one family and a
// change to the look lands everywhere.
//
// doc-check: UltraCanvasWindowBase* parent; struct MySettings { bool dark = false; void Save() {} } settings;
// doc-check: example
//   using namespace UltraCanvas::SettingsPageKit;
//   auto shell = std::make_shared<UltraCanvasSettingsWindow>();
//   shell->Create(parent, "MyApp - Settings");
//   shell->AddSection("look", "Look");
//   shell->AddTreeNode("look", "look/colours", "Colours");
//   PageParts page = MakePage("set-colours", "Colours", "How the window is drawn:");
//   page.body->AddChild(MakeCheckbox("set-dark", "Dark", kTextWidth, settings.dark, [&](bool on) {
//       settings.dark = on;
//       settings.Save();
//   }));
//   AddNote(page, "set-colours-note", "Every change applies at once.");
//   shell->AddPage("look/colours", page.page);
//   shell->resets["look/colours"] = PageReset{"Restore defaults", 170, [&]() { settings.dark = false; }};
//   shell->Finish("look/colours");
//
// Version: 1.2.0 - the colours and sizes are the theme's roles and text roles, read as functions
//                  (TextColor(), TextFontSize() ...): a settings window is dark on a dark desktop
// Version: 1.1.0 - ShowSaveSettingsDialog: a file type's settings in the window's type size
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasWindow.h"   // UltraCanvasWindow is the platform's window type, not a class to forward-declare

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace UltraCanvas {

class UltraCanvasButton;
class UltraCanvasCheckbox;
class UltraCanvasContainer;
class UltraCanvasLabel;
class UltraCanvasRadio;
class UltraCanvasSlider;
class UltraCanvasTreeView;

namespace SettingsPageKit {

// ===== ONE TYPE SCALE =====
// Titles above the text, the text and every control at one size, and the
// notes a step below it and greyed: what is read first is the largest. The
// sizes are the theme's text roles - the desktop's interface font size and
// the ratios of UltraCanvasTheme - and the colours its roles, read as
// functions when a page is built, so a settings window is dark on a dark
// desktop (T6 of the host theme proposal); a window built after the theme
// changed takes the new look.
inline float TitleFontSize() { return static_cast<float>(UltraCanvasTheme::Current().FontSize(ThemeTextRole::Heading)); }   // page title (bold)
inline float TextFontSize()  { return static_cast<float>(UltraCanvasTheme::Current().FontSize(ThemeTextRole::Body)); }      // captions, choices, buttons, fields
inline float NoteFontSize()  { return static_cast<float>(UltraCanvasTheme::Current().FontSize(ThemeTextRole::Caption)); }   // the notes under the controls
inline float TreeFontSize()  { return static_cast<float>(UltraCanvasTheme::Current().FontSize(ThemeTextRole::Caption)); }   // the tree of pages

inline Color TextColor()        { return UltraCanvasTheme::Current().roles.ink; }
inline Color NoteTextColor()    { return UltraCanvasTheme::Current().roles.inkMuted; }
inline Color NoteBackground()   { return UltraCanvasTheme::Current().roles.well; }
inline Color NoteAccent()       { return UltraCanvasTheme::Current().roles.lineStrong; }
inline Color WindowBackground() { return UltraCanvasTheme::Current().roles.ground; }
inline Color TreeBackground()   { return UltraCanvasTheme::Current().roles.well; }
inline Color PageBackground()   { return UltraCanvasTheme::Current().roles.panel; }
inline Color ButtonFace()       { return UltraCanvasTheme::Current().roles.raised; }   // a button at rest
inline Color ButtonHover()      { return UltraCanvasTheme::Current().roles.hover; }
inline Color Rule()             { return UltraCanvasTheme::Current().roles.line; }     // hairlines and thin borders

// Page geometry: the window is 700 wide, the tree takes 180, so a page has
// 520; with its padding that leaves ~480 for content. Every text is wrapped
// at a fixed width because content measuring needs a render context, which
// the window does not have while it is first laid out.
constexpr int kWindowWidth = 700;
constexpr int kWindowHeight = 560;
constexpr int kTreeWidth = 180;
constexpr int kPagePadding = 20;
constexpr int kTextWidth = 440;     // wrapped texts and choice rows
constexpr int kNoteWidth = 430;     // wrapped notes inside their block
constexpr int kControlHeight = 22;  // one checkbox / radio row

// ===== TEXT =====
// A single-line label that hugs its text.
std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                            float fontSize = TextFontSize(), const Color& color = TextColor());
// A paragraph: word-wrapped at `width`, as tall as its lines.
std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id, const std::string& text, int width = kTextWidth,
                                           float fontSize = TextFontSize(), const Color& color = TextColor());

// ===== PAGE SKELETON =====
// Title, caption, the controls, and the notes block at the foot - the same
// order and the same spacing on every page, so the eye finds the control
// where it found it on the last page.
struct PageParts {
    std::shared_ptr<UltraCanvasContainer> page;    // the whole page
    std::shared_ptr<UltraCanvasContainer> body;    // the controls
    std::shared_ptr<UltraCanvasContainer> notes;   // the explanations
};

// The greyed block with the accent bar that holds a page's notes.
std::shared_ptr<UltraCanvasContainer> MakeNotesBlock(const std::string& id);
// `scrolls`: a page longer than the window (a list of formats) scrolls, and
// there the notes sit right under the caption, before the list, so they are
// read before the hundred checkboxes rather than found after them. Every
// other page keeps its notes at the foot, clear of the controls.
PageParts MakePage(const std::string& id, const std::string& title, const std::string& caption,
                   bool scrolls = false);
// One paragraph of a page's notes.
void AddNote(PageParts& parts, const std::string& id, const std::string& text);
// A caption inside the body, introducing a second group of controls.
void AddBodyCaption(PageParts& parts, const std::string& id, const std::string& text);

// ===== CONTROLS =====
// A file of the framework's icon set, media/icons/<fileName>.
std::string IconPath(const std::string& fileName);
// Standard window button (Close, Restore defaults, the ones on the pages).
// `iconFile` may be empty; an empty `label` gives an icon-only button.
std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& label, int width,
                                              std::function<void()> onClick, const std::string& iconFile = "");
// A row of buttons inside a page body.
std::shared_ptr<UltraCanvasContainer> MakeButtonRow(const std::string& id);
// A row of small choices that wraps (a chip per option).
std::shared_ptr<UltraCanvasContainer> MakeChoiceRow(const std::string& id);
// "<caption>  [control]": a labelled control on one line.
std::shared_ptr<UltraCanvasContainer> MakeLabelledRow(const std::string& id, const std::string& caption,
                                                      const std::shared_ptr<UltraCanvasUIElement>& control,
                                                      int labelWidth = 190, int height = 30);
// One choice of a radio group, in the window's text size.
std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id, const std::string& text, bool checked,
                                             int width = kTextWidth);
std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(const std::string& id, const std::string& text, int width,
                                                  bool checked, std::function<void(bool)> onChange);
// A whole-number slider over [minValue, maxValue]; `onChange` while the
// handle moves and when it is let go.
std::shared_ptr<UltraCanvasSlider> MakeIntSlider(const std::string& id, int minValue, int maxValue, int value,
                                                 std::function<void(int)> onChange);
// The value label beside such a slider - "56 px", "20 fps".
std::shared_ptr<UltraCanvasLabel> MakeSliderValueLabel(const std::string& id, int width = 50);
// "<caption>  [slider]  value" on one line, indented under its choice.
std::shared_ptr<UltraCanvasContainer> MakeSliderRow(const std::string& id, const std::string& caption,
                                                    const std::shared_ptr<UltraCanvasSlider>& slider,
                                                    const std::shared_ptr<UltraCanvasLabel>& value,
                                                    int indent = 24, int labelWidth = 130);
// The box showing a configured colour: a button filled with that colour, the
// same shade in every state, that reports its click like any other control.
std::shared_ptr<UltraCanvasButton> MakeColorBox(const std::string& id, const Color& color,
                                                const std::string& tooltip, std::function<void()> onClick);
void SetColorBoxColor(const std::shared_ptr<UltraCanvasButton>& box, const Color& color);

// ===== DIALOGS =====
// A file type's save settings (JPEG quality, PNG compression ...) - the
// framework's UltraCanvasFileLoader::ShowSaveSettingsDialog - in this
// window's type size, so the dialog reads as part of the window it is opened
// from rather than in the larger type of a dialog on its own.
void ShowSaveSettingsDialog(const std::string& extensionOrPath, UltraCanvasWindowBase* parent);

// ===== THE WINDOW =====
// A page's "Restore default ..." action, shown in the bottom bar while that
// page is up.
struct PageReset {
    std::string label;
    int width = 170;
    std::function<void()> action;
};

// How the window is made: its size, the tree's width, what stands above the
// tree, whether it is a dialog of a parent window or an application's own
// main window, and the Close button's text.
struct SettingsWindowOptions {
    int width = kWindowWidth;
    int height = kWindowHeight;
    // The tree's width: wider for a long page list, or long page names
    int treeWidth = kTreeWidth;
    // An element shown above the tree, in the sidebar: a logo. Null for none.
    std::shared_ptr<UltraCanvasUIElement> sidebarHeader;
    // A dialog of `parent` (WindowType::Dialog), or with false an ordinary
    // main window (UOS-Settings is one: the settings are the application)
    bool dialog = true;
    // The Close button's text, for a translated window
    std::string closeLabel = "Close";
};

// The settings window's skeleton: the tree of pages on the left, the page
// area on the right, the bottom bar with the shown page's restore button
// and Close. An application's dialog state derives from it (or holds one),
// adds its sections, nodes, pages and resets, and calls Finish. Every
// section starts closed, the window opens on `startPage`, and selecting a
// section moves on to its first page.
class UltraCanvasSettingsWindow {
public:
    std::shared_ptr<UltraCanvasWindow> window;
    bool closed = false;
    std::shared_ptr<UltraCanvasTreeView> tree;
    std::shared_ptr<UltraCanvasContainer> pageArea;
    std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;
    std::map<std::string, PageReset> resets;
    std::shared_ptr<UltraCanvasButton> restoreButton;
    std::string shownPage;

    // Called after a page is shown - a page of live figures re-reads them.
    std::function<void(const std::string& pageId)> onPageShown;
    // Called when the window has closed (closed is set first).
    std::function<void()> onClosed;

    // The window (not shown yet), the empty tree and the bottom bar. False
    // when the window could not be made.
    bool Create(UltraCanvasWindowBase* parent, const std::string& title,
                const SettingsWindowOptions& options = SettingsWindowOptions());
    // A top-level section of the tree, and a node under any node.
    void AddSection(const std::string& id, const std::string& text);
    void AddTreeNode(const std::string& parentId, const std::string& id, const std::string& text);
    // A page under its node id (the tree node of the same id shows it).
    void AddPage(const std::string& pageId, const std::shared_ptr<UltraCanvasContainer>& page);
    // After the nodes and pages: hides the tree's root (which promotes the
    // sections to the top level), wires the selection, shows `startPage`
    // and - unless `show` is false, for a main window shown later - the
    // window.
    void Finish(const std::string& startPage, bool show = true);

    // Show the page of `nodeId`; a section without a page of its own falls
    // through to its first page.
    void ShowPage(const std::string& nodeId);
    // Selects the tree row of `pageId` (its section opened) and shows it.
    void SelectPage(const std::string& pageId);
    // The bottom bar's restore button follows the shown page: labelled and
    // wired for it, or hidden while the page has nothing to restore.
    void UpdateRestoreButton();
    bool IsOpen() const { return window != nullptr && !closed; }
};

} // namespace SettingsPageKit
} // namespace UltraCanvas
