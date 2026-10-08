// OS/Linux/UltraCanvasLinuxAccessibility.cpp
// The AT-SPI bridge. The application registers on the accessibility bus as an
// AT-SPI application and answers for its windows and elements there:
//
//   /org/a11y/atspi/accessible/root   the application (Accessible, Application)
//   /org/a11y/atspi/accessible/<id>   a window or element (Accessible,
//                                     Component, and Text when it has text)
//
// What an element says comes from UltraCanvasAccessibility.h (role, name,
// IAccessibleText); the tree, ids and geometry from
// UltraCanvasAccessibilityBridge.h. Events from the accessibility layer become
// AT-SPI signals (focus, caret, text inserted / deleted, selection).
//
// GDBus delivers calls on a private GMainContext, which the toolkit's own
// select() loop services through an fd watch on that context's wake-up fd, so
// every call is answered on the UI thread, between events, with no thread of
// our own and no GLib main loop in the application.
// Version: 1.1.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxAccessibility.h"
#include "UltraCanvasAccessibility.h"
#include "UltraCanvasAccessibilityBridge.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasDebug.h"

#include <gio/gio.h>

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>

namespace UltraCanvas {
namespace LinuxAccessibility {

namespace {

namespace AB = AccessibilityBridge;

// ===== AT-SPI CONSTANTS =====
// From atspi-constants.h (at-spi2-core 2.52); copied so the bridge needs no
// AT-SPI development package, only GIO.
constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
constexpr const char* kNullPath = "/org/a11y/atspi/null";
constexpr const char* kObjectPrefix = "/org/a11y/atspi/accessible";

enum Role : uint32_t {
    RoleCheckBox = 7, RoleComboBox = 11, RoleFiller = 20, RoleFrame = 23, RoleImage = 27, RoleLabel = 29,
    RoleList = 31, RoleListItem = 32, RoleMenu = 33, RoleMenuItem = 35, RolePageTab = 37, RolePageTabList = 38,
    RolePanel = 39, RolePasswordText = 40, RoleProgressBar = 42, RolePushButton = 43, RoleRadioButton = 44,
    RoleSlider = 51, RoleSpinButton = 52, RoleTable = 55, RoleText = 61, RoleToggleButton = 62, RoleToolBar = 63,
    RoleTree = 65, RoleUnknown = 67, RoleApplication = 75, RoleEntry = 79, RoleLink = 88, RoleTreeItem = 91,
    RoleDocumentText = 94, RoleGrouping = 99
};

enum State : uint32_t {
    StateActive = 1, StateChecked = 4, StateEditable = 7, StateEnabled = 8, StateFocusable = 11, StateFocused = 12,
    StateMultiLine = 17, StateResizable = 21, StateSensitive = 24, StateShowing = 25,
    StateSingleLine = 26, StateVisible = 30, StateIndeterminate = 32, StateSelectableText = 38,
    StateCheckable = 41, StateReadOnly = 43, StatePressed = 20
};

enum CoordType : uint32_t { CoordScreen = 0, CoordWindow = 1, CoordParent = 2 };
enum Layer : uint32_t { LayerWidget = 3, LayerWindow = 7 };

// The interfaces this bridge implements, from at-spi2-core's xml/*.xml.
// GDBus checks every incoming call against them.
constexpr const char* kIntrospection = R"XML(<node>
  <interface name="org.a11y.atspi.Accessible">
    <property name="Name" type="s" access="read"/>
    <property name="Description" type="s" access="read"/>
    <property name="Parent" type="(so)" access="read"/>
    <property name="ChildCount" type="i" access="read"/>
    <property name="Locale" type="s" access="read"/>
    <property name="AccessibleId" type="s" access="read"/>
    <property name="HelpText" type="s" access="read"/>
    <method name="GetChildAtIndex"><arg name="index" direction="in" type="i"/><arg direction="out" type="(so)"/></method>
    <method name="GetChildren"><arg direction="out" type="a(so)"/></method>
    <method name="GetIndexInParent"><arg direction="out" type="i"/></method>
    <method name="GetRelationSet"><arg direction="out" type="a(ua(so))"/></method>
    <method name="GetRole"><arg direction="out" type="u"/></method>
    <method name="GetRoleName"><arg direction="out" type="s"/></method>
    <method name="GetLocalizedRoleName"><arg direction="out" type="s"/></method>
    <method name="GetState"><arg direction="out" type="au"/></method>
    <method name="GetAttributes"><arg direction="out" type="a{ss}"/></method>
    <method name="GetApplication"><arg direction="out" type="(so)"/></method>
    <method name="GetInterfaces"><arg direction="out" type="as"/></method>
  </interface>
  <interface name="org.a11y.atspi.Application">
    <property name="ToolkitName" type="s" access="read"/>
    <property name="Version" type="s" access="read"/>
    <property name="AtspiVersion" type="s" access="read"/>
    <property name="Id" type="i" access="readwrite"/>
    <method name="GetLocale"><arg name="lctype" direction="in" type="u"/><arg direction="out" type="s"/></method>
  </interface>
  <interface name="org.a11y.atspi.Component">
    <method name="Contains"><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg name="coord_type" direction="in" type="u"/><arg direction="out" type="b"/></method>
    <method name="GetAccessibleAtPoint"><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg name="coord_type" direction="in" type="u"/><arg direction="out" type="(so)"/></method>
    <method name="GetExtents"><arg name="coord_type" direction="in" type="u"/><arg direction="out" type="(iiii)"/></method>
    <method name="GetPosition"><arg name="coord_type" direction="in" type="u"/><arg name="x" direction="out" type="i"/><arg name="y" direction="out" type="i"/></method>
    <method name="GetSize"><arg name="width" direction="out" type="i"/><arg name="height" direction="out" type="i"/></method>
    <method name="GetLayer"><arg direction="out" type="u"/></method>
    <method name="GetMDIZOrder"><arg direction="out" type="n"/></method>
    <method name="GrabFocus"><arg direction="out" type="b"/></method>
    <method name="GetAlpha"><arg direction="out" type="d"/></method>
    <method name="SetExtents"><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg name="width" direction="in" type="i"/><arg name="height" direction="in" type="i"/><arg name="coord_type" direction="in" type="u"/><arg direction="out" type="b"/></method>
    <method name="SetPosition"><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg name="coord_type" direction="in" type="u"/><arg direction="out" type="b"/></method>
    <method name="SetSize"><arg name="width" direction="in" type="i"/><arg name="height" direction="in" type="i"/><arg direction="out" type="b"/></method>
    <method name="ScrollTo"><arg name="type" direction="in" type="u"/><arg direction="out" type="b"/></method>
    <method name="ScrollToPoint"><arg name="coord_type" direction="in" type="u"/><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg direction="out" type="b"/></method>
  </interface>
  <interface name="org.a11y.atspi.Text">
    <property name="CharacterCount" type="i" access="read"/>
    <property name="CaretOffset" type="i" access="read"/>
    <method name="GetStringAtOffset"><arg name="offset" direction="in" type="i"/><arg name="granularity" direction="in" type="u"/><arg direction="out" type="s"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="GetText"><arg name="startOffset" direction="in" type="i"/><arg name="endOffset" direction="in" type="i"/><arg direction="out" type="s"/></method>
    <method name="SetCaretOffset"><arg name="offset" direction="in" type="i"/><arg direction="out" type="b"/></method>
    <method name="GetTextBeforeOffset"><arg name="offset" direction="in" type="i"/><arg name="type" direction="in" type="u"/><arg direction="out" type="s"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="GetTextAtOffset"><arg name="offset" direction="in" type="i"/><arg name="type" direction="in" type="u"/><arg direction="out" type="s"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="GetTextAfterOffset"><arg name="offset" direction="in" type="i"/><arg name="type" direction="in" type="u"/><arg direction="out" type="s"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="GetCharacterAtOffset"><arg name="offset" direction="in" type="i"/><arg direction="out" type="i"/></method>
    <method name="GetAttributeValue"><arg name="offset" direction="in" type="i"/><arg name="attributeName" direction="in" type="s"/><arg direction="out" type="s"/></method>
    <method name="GetAttributes"><arg name="offset" direction="in" type="i"/><arg direction="out" type="a{ss}"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="GetDefaultAttributes"><arg direction="out" type="a{ss}"/></method>
    <method name="GetCharacterExtents"><arg name="offset" direction="in" type="i"/><arg name="coordType" direction="in" type="u"/><arg name="x" direction="out" type="i"/><arg name="y" direction="out" type="i"/><arg name="width" direction="out" type="i"/><arg name="height" direction="out" type="i"/></method>
    <method name="GetOffsetAtPoint"><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg name="coordType" direction="in" type="u"/><arg direction="out" type="i"/></method>
    <method name="GetNSelections"><arg direction="out" type="i"/></method>
    <method name="GetSelection"><arg name="selectionNum" direction="in" type="i"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="AddSelection"><arg name="startOffset" direction="in" type="i"/><arg name="endOffset" direction="in" type="i"/><arg direction="out" type="b"/></method>
    <method name="RemoveSelection"><arg name="selectionNum" direction="in" type="i"/><arg direction="out" type="b"/></method>
    <method name="SetSelection"><arg name="selectionNum" direction="in" type="i"/><arg name="startOffset" direction="in" type="i"/><arg name="endOffset" direction="in" type="i"/><arg direction="out" type="b"/></method>
    <method name="GetRangeExtents"><arg name="startOffset" direction="in" type="i"/><arg name="endOffset" direction="in" type="i"/><arg name="coordType" direction="in" type="u"/><arg name="x" direction="out" type="i"/><arg name="y" direction="out" type="i"/><arg name="width" direction="out" type="i"/><arg name="height" direction="out" type="i"/></method>
    <method name="GetBoundedRanges"><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg name="width" direction="in" type="i"/><arg name="height" direction="in" type="i"/><arg name="coordType" direction="in" type="u"/><arg name="xClipType" direction="in" type="u"/><arg name="yClipType" direction="in" type="u"/><arg direction="out" type="a(iisv)"/></method>
    <method name="GetAttributeRun"><arg name="offset" direction="in" type="i"/><arg name="includeDefaults" direction="in" type="b"/><arg direction="out" type="a{ss}"/><arg name="startOffset" direction="out" type="i"/><arg name="endOffset" direction="out" type="i"/></method>
    <method name="GetDefaultAttributeSet"><arg direction="out" type="a{ss}"/></method>
    <method name="ScrollSubstringTo"><arg name="startOffset" direction="in" type="i"/><arg name="endOffset" direction="in" type="i"/><arg name="type" direction="in" type="u"/><arg direction="out" type="b"/></method>
    <method name="ScrollSubstringToPoint"><arg name="startOffset" direction="in" type="i"/><arg name="endOffset" direction="in" type="i"/><arg name="type" direction="in" type="u"/><arg name="x" direction="in" type="i"/><arg name="y" direction="in" type="i"/><arg direction="out" type="b"/></method>
  </interface>
  <interface name="org.a11y.atspi.Value">
    <property name="MinimumValue" type="d" access="read"/>
    <property name="MaximumValue" type="d" access="read"/>
    <property name="MinimumIncrement" type="d" access="read"/>
    <property name="CurrentValue" type="d" access="readwrite"/>
    <property name="Text" type="s" access="read"/>
  </interface>
  <interface name="org.a11y.atspi.Action">
    <property name="NActions" type="i" access="read"/>
    <method name="GetDescription"><arg name="index" direction="in" type="i"/><arg direction="out" type="s"/></method>
    <method name="GetName"><arg name="index" direction="in" type="i"/><arg direction="out" type="s"/></method>
    <method name="GetLocalizedName"><arg name="index" direction="in" type="i"/><arg direction="out" type="s"/></method>
    <method name="GetKeyBinding"><arg name="index" direction="in" type="i"/><arg direction="out" type="s"/></method>
    <method name="GetActions"><arg direction="out" type="a(sss)"/></method>
    <method name="DoAction"><arg name="index" direction="in" type="i"/><arg direction="out" type="b"/></method>
  </interface>
  <interface name="org.a11y.atspi.Cache">
    <method name="GetItems"><arg direction="out" name="nodes" type="a((so)(so)(so)iiassusau)"/></method>
  </interface>
</node>)XML";

constexpr const char* kAccessible = "org.a11y.atspi.Accessible";
constexpr const char* kApplication = "org.a11y.atspi.Application";
constexpr const char* kComponent = "org.a11y.atspi.Component";
constexpr const char* kText = "org.a11y.atspi.Text";
constexpr const char* kCache = "org.a11y.atspi.Cache";
constexpr const char* kValue = "org.a11y.atspi.Value";
constexpr const char* kAction = "org.a11y.atspi.Action";
constexpr const char* kCachePath = "/org/a11y/atspi/cache";

// ===== STATE =====

struct Bridge {
    GMainContext* context = nullptr;
    FdWatchId contextWatch = 0;
    bool watching = false;

    GDBusConnection* session = nullptr;
    guint statusSubscription = 0;

    GDBusConnection* bus = nullptr;          // the accessibility bus
    GDBusNodeInfo* nodeInfo = nullptr;
    guint subtree = 0;
    guint cacheObject = 0;
    std::string uniqueName;
    std::string parentName;                  // the registry's desktop, from Embed
    std::string parentPath = kNullPath;
    int applicationId = 0;
    bool connected = false;

    int listener = 0;
    AB::IdMap ids;
    std::map<uint32_t, std::string> textCache;   // last text sent, per element
    UltraCanvasUIElement* focused = nullptr;
    UltraCanvasWindowBase* activeWindow = nullptr;
};

Bridge* gBridge = nullptr;

// GDBus attaches its callbacks to the thread-default context at the time a
// subscription, registration or call is made. This pins ours for a scope.
class ContextScope {
public:
    explicit ContextScope(GMainContext* context) : ctx(context) { g_main_context_push_thread_default(ctx); }
    ~ContextScope() { g_main_context_pop_thread_default(ctx); }
private:
    GMainContext* ctx;
};

// ===== OBJECTS =====

// A node is the application (root) or an element by id.
struct Node {
    bool root = false;
    UltraCanvasUIElement* element = nullptr;
    bool Valid() const { return root || element; }
};

Node NodeFromPath(const char* path) {
    Node node;
    if (!gBridge || !path) return node;
    if (std::strcmp(path, kRootPath) == 0) {
        node.root = true;
        return node;
    }
    const size_t prefix = std::strlen(kObjectPrefix);
    if (std::strncmp(path, kObjectPrefix, prefix) != 0 || path[prefix] != '/') return node;
    const char* name = path + prefix + 1;
    char* end = nullptr;
    const unsigned long id = std::strtoul(name, &end, 10);
    if (end == name || *end != '\0') return node;
    UltraCanvasUIElement* element = gBridge->ids.ElementOf(static_cast<uint32_t>(id));
    if (element && AB::IsLive(element)) node.element = element;
    return node;
}

std::string PathOf(UltraCanvasUIElement* element) {
    if (!element) return kNullPath;
    return std::string(kObjectPrefix) + "/" + std::to_string(gBridge->ids.IdOf(element));
}

GVariant* Reference(const std::string& path) {
    return g_variant_new("(so)", gBridge->uniqueName.c_str(), path.c_str());
}

GVariant* RootReference() { return Reference(kRootPath); }
GVariant* NullReference() { return Reference(kNullPath); }

GVariant* ElementReference(UltraCanvasUIElement* element) {
    return element ? Reference(PathOf(element)) : NullReference();
}

GVariant* ParentReference(const Node& node) {
    if (node.root) return g_variant_new("(so)", gBridge->parentName.c_str(), gBridge->parentPath.c_str());
    if (UltraCanvasUIElement* parent = AB::Parent(node.element)) return ElementReference(parent);
    return RootReference();    // a window
}

std::vector<UltraCanvasUIElement*> ChildrenOf(const Node& node) {
    if (node.root) {
        std::vector<UltraCanvasUIElement*> windows;
        for (UltraCanvasWindowBase* window : AB::Windows()) windows.push_back(window);
        return windows;
    }
    return AB::Children(node.element);
}

uint32_t RoleOf(const Node& node) {
    if (node.root) return RoleApplication;
    if (AB::AsWindow(node.element)) return RoleFrame;
    switch (node.element->GetAccessibleRole()) {
        case AccessibleRole::Window:    return RoleFrame;
        case AccessibleRole::Button:
            // A button that stays down is a toggle button to AT-SPI.
            return node.element->GetAccessibleToggleState() == AccessibleToggleState::NotToggleable
                   ? RolePushButton : RoleToggleButton;
        case AccessibleRole::CheckBox:  return RoleCheckBox;
        case AccessibleRole::Label:     return RoleLabel;
        // AT-SPI has no password state, only a role: Orca then says
        // "password text" and speaks no character typed into it.
        case AccessibleRole::TextField:
            return node.element->IsAccessiblePassword() ? RolePasswordText : RoleEntry;
        case AccessibleRole::TextArea:  return RoleText;
        case AccessibleRole::Document:  return RoleDocumentText;
        case AccessibleRole::List:      return RoleList;
        case AccessibleRole::ListItem:  return RoleListItem;
        case AccessibleRole::Table:     return RoleTable;
        case AccessibleRole::Image:     return RoleImage;
        case AccessibleRole::Link:      return RoleLink;
        case AccessibleRole::Menu:      return RoleMenu;
        case AccessibleRole::MenuItem:  return RoleMenuItem;
        case AccessibleRole::RadioButton: return RoleRadioButton;
        case AccessibleRole::Switch:      return RoleToggleButton;
        case AccessibleRole::ComboBox:    return RoleComboBox;
        case AccessibleRole::Slider:      return RoleSlider;
        case AccessibleRole::SpinButton:  return RoleSpinButton;
        case AccessibleRole::ProgressBar: return RoleProgressBar;
        case AccessibleRole::Toolbar:     return RoleToolBar;
        case AccessibleRole::TabList:     return RolePageTabList;
        case AccessibleRole::Tree:        return RoleTree;
        case AccessibleRole::Group:       return RoleGrouping;
        case AccessibleRole::Unknown:   break;
    }
    // An element that has not described itself: a panel when it holds
    // others (so a reader walks into it), else a filler it can skip.
    return AB::Children(node.element).empty() ? RoleFiller : RolePanel;
}

const char* RoleName(uint32_t role) {
    switch (role) {
        case RoleApplication:  return "application";
        case RoleFrame:        return "frame";
        case RolePushButton:   return "push button";
        case RoleCheckBox:     return "check box";
        case RoleLabel:        return "label";
        case RoleEntry:        return "entry";
        case RolePasswordText: return "password text";
        case RoleText:         return "text";
        case RoleDocumentText: return "document text";
        case RoleList:         return "list";
        case RoleListItem:     return "list item";
        case RoleTable:        return "table";
        case RoleImage:        return "image";
        case RoleLink:         return "link";
        case RoleMenu:         return "menu";
        case RoleMenuItem:     return "menu item";
        case RolePanel:        return "panel";
        case RoleFiller:       return "filler";
        case RoleComboBox:     return "combo box";
        case RolePageTab:      return "page tab";
        case RolePageTabList:  return "page tab list";
        case RoleProgressBar:  return "progress bar";
        case RoleRadioButton:  return "radio button";
        case RoleSlider:       return "slider";
        case RoleSpinButton:   return "spin button";
        case RoleToggleButton: return "toggle button";
        case RoleToolBar:      return "tool bar";
        case RoleTree:         return "tree";
        case RoleTreeItem:     return "tree item";
        case RoleGrouping:     return "grouping";
        default:               return "unknown";
    }
}

GVariant* StateSet(const Node& node) {
    uint32_t words[2] = {0, 0};
    const auto set = [&](uint32_t state) { words[state / 32] |= 1u << (state % 32); };
    if (!node.root) {
        UltraCanvasUIElement* element = node.element;
        UltraCanvasWindowBase* window = AB::WindowOf(element);
        const bool shown = element->IsVisible() && window && window->IsWindowVisible();
        if (!element->IsDisabled()) { set(StateEnabled); set(StateSensitive); }
        if (element->IsVisible()) set(StateVisible);
        if (shown) set(StateShowing);
        if (auto* asWindow = AB::AsWindow(element)) {
            set(StateResizable);
            if (asWindow->IsWindowFocused()) set(StateActive);
        } else {
            if (element->AcceptsFocus()) set(StateFocusable);
            if (element->IsFocused()) set(StateFocused);
        }
        const AccessibleRole role = element->GetAccessibleRole();
        const bool textRole = role == AccessibleRole::TextField || role == AccessibleRole::TextArea ||
                              role == AccessibleRole::Document;
        if (IAccessibleText* text = AB::TextInterface(element); text && textRole) {
            set(StateSelectableText);
            if (role == AccessibleRole::TextField) set(StateSingleLine);
            else set(StateMultiLine);
            if (text->IsReadOnly() || element->IsDisabled()) set(StateReadOnly);
            else set(StateEditable);
        }
        switch (element->GetAccessibleToggleState()) {
            case AccessibleToggleState::NotToggleable: break;
            case AccessibleToggleState::On:
                set(StateCheckable);
                set(StateChecked);
                if (role == AccessibleRole::Button) set(StatePressed);
                break;
            case AccessibleToggleState::Mixed:
                set(StateCheckable);
                set(StateIndeterminate);
                break;
            case AccessibleToggleState::Off:
                set(StateCheckable);
                break;
        }
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
    g_variant_builder_add(&builder, "u", words[0]);
    g_variant_builder_add(&builder, "u", words[1]);
    return g_variant_builder_end(&builder);
}

std::string Locale() {
    const char* messages = std::setlocale(LC_MESSAGES, nullptr);
    std::string locale = messages ? messages : "";
    if (locale.empty() || locale == "C" || locale == "POSIX") {
        const char* lang = std::getenv("LANG");
        locale = lang && *lang ? lang : "en_US";
    }
    const size_t dot = locale.find('.');
    if (dot != std::string::npos) locale.erase(dot);
    return locale;
}

// ===== GEOMETRY =====

AB::ScreenRect Relative(AB::ScreenRect rect, UltraCanvasUIElement* element, uint32_t coordType) {
    if (coordType == CoordWindow) {
        if (UltraCanvasWindowBase* window = AB::WindowOf(element)) {
            const AB::ScreenRect origin = AB::ScreenBounds(window);
            rect.x -= origin.x;
            rect.y -= origin.y;
        }
    } else if (coordType == CoordParent) {
        if (UltraCanvasUIElement* parent = AB::Parent(element)) {
            const AB::ScreenRect origin = AB::ScreenBounds(parent);
            rect.x -= origin.x;
            rect.y -= origin.y;
        }
    }
    return rect;
}

// A point given in `coordType` coordinates, on the screen.
void ToScreen(UltraCanvasUIElement* element, uint32_t coordType, int& x, int& y) {
    AB::ScreenRect zero;
    zero = Relative(zero, element, coordType);
    x -= zero.x;
    y -= zero.y;
}

// ===== TEXT =====

AccessibleTextBoundary BoundaryForGranularity(uint32_t granularity) {
    switch (granularity) {
        case 1:  return AccessibleTextBoundary::Word;
        case 2:  return AccessibleTextBoundary::Sentence;
        case 3:  return AccessibleTextBoundary::Line;
        case 4:  return AccessibleTextBoundary::Paragraph;
        default: return AccessibleTextBoundary::Character;
    }
}

// The legacy boundary types: CHAR 0, WORD_START/END 1-2,
// SENTENCE_START/END 3-4, LINE_START/END 5-6.
AccessibleTextBoundary BoundaryForType(uint32_t type) {
    if (type == 1 || type == 2) return AccessibleTextBoundary::Word;
    if (type == 3 || type == 4) return AccessibleTextBoundary::Sentence;
    if (type == 5 || type == 6) return AccessibleTextBoundary::Line;
    return AccessibleTextBoundary::Character;
}

std::string UnitAt(IAccessibleText* text, int offset, AccessibleTextBoundary boundary, int& start, int& end) {
    const int count = text->GetCharacterCount();
    if (offset < 0 || offset > count) {
        start = end = 0;
        return "";
    }
    if (boundary == AccessibleTextBoundary::Character) {
        start = offset;
        end = std::min(offset + 1, count);
        return AB::Substring(text->GetAccessibleText(), start, end);
    }
    return text->GetTextAtOffset(offset, boundary, start, end);
}

std::string ColorAttribute(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return "";
    const auto channel = [&](size_t at) { return std::to_string(std::strtol(hex.substr(at, 2).c_str(), nullptr, 16)); };
    return channel(1) + "," + channel(3) + "," + channel(5);
}

// The attribute names ATK and Orca use.
std::map<std::string, std::string> AttributeMap(const AccessibleTextAttributes& a) {
    std::map<std::string, std::string> out;
    out["weight"] = a.bold ? "700" : "400";
    out["style"] = a.italic ? "italic" : "normal";
    out["underline"] = a.underline ? "single" : "none";
    out["strikethrough"] = a.strikethrough ? "true" : "false";
    if (a.superscript) out["text-position"] = "super";
    else if (a.subscript) out["text-position"] = "sub";
    if (!a.fontFamily.empty()) out["family-name"] = a.fontFamily;
    if (a.fontSizePt > 0.0f) out["size"] = std::to_string(static_cast<int>(std::lround(a.fontSizePt)));
    const std::string foreground = ColorAttribute(a.color);
    if (!foreground.empty()) out["fg-color"] = foreground;
    const std::string background = ColorAttribute(a.backgroundColor);
    if (!background.empty()) out["bg-color"] = background;
    if (a.misspelled) out["invalid"] = "spelling";
    if (a.headingLevel > 0) out["heading-level"] = std::to_string(a.headingLevel);
    if (a.listItem) out["list-item"] = "true";
    if (!a.link.empty()) out["link"] = a.link;
    if (a.inserted) out["revision"] = "insertion";
    if (a.deleted) out["revision"] = "deletion";
    if (a.commented) out["comment"] = "true";
    return out;
}

GVariant* StringMap(const std::map<std::string, std::string>& map) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    for (const auto& [key, value] : map) g_variant_builder_add(&builder, "{ss}", key.c_str(), value.c_str());
    return g_variant_builder_end(&builder);
}

GVariant* EmptyProperties() {
    return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
}

// ===== EVENTS =====

void Emit(UltraCanvasUIElement* element, const char* interface, const char* member,
          const char* detail, int detail1, int detail2, GVariant* data) {
    if (!gBridge || !gBridge->connected) {
        if (data) g_variant_unref(g_variant_ref_sink(data));
        return;
    }
    const std::string path = element ? PathOf(element) : std::string(kRootPath);
    GVariant* body = g_variant_new("(siiv@a{sv})", detail, detail1, detail2,
                                   data ? data : g_variant_new_int32(0), EmptyProperties());
    g_dbus_connection_emit_signal(gBridge->bus, nullptr, path.c_str(), interface, member, body, nullptr);
}

void EmitObject(UltraCanvasUIElement* element, const char* member, const char* detail,
                int detail1, int detail2, GVariant* data = nullptr) {
    Emit(element, "org.a11y.atspi.Event.Object", member, detail, detail1, detail2, data);
}

void RememberText(UltraCanvasUIElement* element) {
    if (IAccessibleText* text = element ? AB::TextInterface(element) : nullptr) {
        gBridge->textCache[gBridge->ids.IdOf(element)] = text->GetAccessibleText();
    }
}

void OnFocus(UltraCanvasUIElement* element) {
    if (!element) return;
    UltraCanvasWindowBase* window = AB::WindowOf(element);
    if (window && window != gBridge->activeWindow) {
        if (gBridge->activeWindow) EmitObject(gBridge->activeWindow, "StateChanged", "active", 0, 0);
        gBridge->activeWindow = window;
        Emit(window, "org.a11y.atspi.Event.Window", "Activate", "", 0, 0, g_variant_new_string(AB::Name(window).c_str()));
        EmitObject(window, "StateChanged", "active", 1, 0);
    }
    if (gBridge->focused && gBridge->focused != element) {
        EmitObject(gBridge->focused, "StateChanged", "focused", 0, 0);
    }
    gBridge->focused = element;
    RememberText(element);
    EmitObject(element, "StateChanged", "focused", 1, 0);
    Emit(element, "org.a11y.atspi.Event.Focus", "Focus", "", 0, 0, nullptr);
}

void OnTextChanged(UltraCanvasUIElement* element) {
    IAccessibleText* text = element ? AB::TextInterface(element) : nullptr;
    if (!text) return;
    const uint32_t id = gBridge->ids.IdOf(element);
    std::string after = text->GetAccessibleText();
    auto cached = gBridge->textCache.find(id);
    if (cached != gBridge->textCache.end()) {
        int start = 0;
        std::string removed, inserted;
        if (AB::Difference(cached->second, after, start, removed, inserted)) {
            if (!removed.empty()) {
                EmitObject(element, "TextChanged", "delete", start,
                           UltraCanvasAccessibility::CharacterCount(removed), g_variant_new_string(removed.c_str()));
            }
            if (!inserted.empty()) {
                EmitObject(element, "TextChanged", "insert", start,
                           UltraCanvasAccessibility::CharacterCount(inserted), g_variant_new_string(inserted.c_str()));
            }
        }
    }
    gBridge->textCache[id] = std::move(after);
}

void OnAccessibilityEvent(const AccessibilityEvent& event) {
    if (!gBridge || !gBridge->connected) return;
    UltraCanvasUIElement* element = event.element;
    switch (event.type) {
        case AccessibilityEventType::ElementDestroyed:
            if (gBridge->focused == element) gBridge->focused = nullptr;
            if (gBridge->activeWindow == element) gBridge->activeWindow = nullptr;
            gBridge->textCache.erase(gBridge->ids.IdOf(element));
            gBridge->ids.Forget(element);
            break;
        case AccessibilityEventType::FocusChanged:
            OnFocus(element);
            break;
        case AccessibilityEventType::TextChanged:
            OnTextChanged(element);
            break;
        case AccessibilityEventType::CaretMoved:
        case AccessibilityEventType::SelectionChanged: {
            IAccessibleText* text = element ? AB::TextInterface(element) : nullptr;
            if (!text) break;
            const int caret = event.offset >= 0 ? event.offset : text->GetCaretOffset();
            if (event.type == AccessibilityEventType::SelectionChanged) {
                EmitObject(element, "TextSelectionChanged", "", 0, 0);
            }
            EmitObject(element, "TextCaretMoved", "", caret, 0);
            break;
        }
        case AccessibilityEventType::StateChanged: {
            const AccessibleToggleState state = element ? element->GetAccessibleToggleState()
                                                        : AccessibleToggleState::NotToggleable;
            EmitObject(element, "StateChanged", "checked", state == AccessibleToggleState::On ? 1 : 0, 0);
            EmitObject(element, "StateChanged", "indeterminate", state == AccessibleToggleState::Mixed ? 1 : 0, 0);
            if (element && element->GetAccessibleRole() == AccessibleRole::Button) {
                EmitObject(element, "StateChanged", "pressed", state == AccessibleToggleState::On ? 1 : 0, 0);
            }
            break;
        }
        case AccessibilityEventType::ValueChanged: {
            if (!element) break;
            AccessibleRange range;
            if (element->GetAccessibleRange(range)) {
                EmitObject(element, "PropertyChange", "accessible-value", 0, 0, g_variant_new_double(range.value));
            }
            // A text field's or combo box's value is its text.
            if (!element->GetAccessibleTextInterface() && AB::TextInterface(element)) OnTextChanged(element);
            break;
        }
        case AccessibilityEventType::NameChanged:
            EmitObject(element, "PropertyChange", "accessible-name", 0, 0,
                       g_variant_new_string(AB::Name(element).c_str()));
            break;
    }
}

// ===== METHOD CALLS =====

void ReturnBool(GDBusMethodInvocation* invocation, bool value) {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", value ? TRUE : FALSE));
}

void AccessibleCall(const Node& node, const char* method, GVariant* parameters, GDBusMethodInvocation* invocation) {
    if (!std::strcmp(method, "GetChildAtIndex")) {
        gint32 index = 0;
        g_variant_get(parameters, "(i)", &index);
        const auto children = ChildrenOf(node);
        UltraCanvasUIElement* child = index >= 0 && index < static_cast<int>(children.size()) ? children[index] : nullptr;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@(so))", ElementReference(child)));
    } else if (!std::strcmp(method, "GetChildren")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(so)"));
        for (UltraCanvasUIElement* child : ChildrenOf(node)) g_variant_builder_add_value(&builder, ElementReference(child));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(so))", g_variant_builder_end(&builder)));
    } else if (!std::strcmp(method, "GetIndexInParent")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", node.root ? -1 : AB::IndexInParent(node.element)));
    } else if (!std::strcmp(method, "GetRelationSet")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@a(ua(so)))", g_variant_new_array(G_VARIANT_TYPE("(ua(so))"), nullptr, 0)));
    } else if (!std::strcmp(method, "GetRole")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", RoleOf(node)));
    } else if (!std::strcmp(method, "GetRoleName") || !std::strcmp(method, "GetLocalizedRoleName")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", RoleName(RoleOf(node))));
    } else if (!std::strcmp(method, "GetState")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@au)", StateSet(node)));
    } else if (!std::strcmp(method, "GetAttributes")) {
        std::map<std::string, std::string> attributes{{"toolkit", "UltraCanvas"}};
        if (!node.root && !node.element->GetIdentifier().empty()) attributes["id"] = node.element->GetIdentifier();
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a{ss})", StringMap(attributes)));
    } else if (!std::strcmp(method, "GetApplication")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@(so))", RootReference()));
    } else if (!std::strcmp(method, "GetInterfaces")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&builder, "s", kAccessible);
        if (node.root) {
            g_variant_builder_add(&builder, "s", kApplication);
        } else {
            g_variant_builder_add(&builder, "s", kComponent);
            if (AB::TextInterface(node.element)) g_variant_builder_add(&builder, "s", kText);
            AccessibleRange range;
            if (node.element->GetAccessibleRange(range)) g_variant_builder_add(&builder, "s", kValue);
            if (!node.element->GetAccessibleActionName().empty()) g_variant_builder_add(&builder, "s", kAction);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@as)", g_variant_builder_end(&builder)));
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod", method);
    }
}

void ComponentCall(UltraCanvasUIElement* element, const char* method, GVariant* parameters, GDBusMethodInvocation* invocation) {
    if (!std::strcmp(method, "Contains") || !std::strcmp(method, "GetAccessibleAtPoint")) {
        gint32 x = 0, y = 0;
        guint32 coordType = CoordScreen;
        g_variant_get(parameters, "(iiu)", &x, &y, &coordType);
        ToScreen(element, coordType, x, y);
        if (!std::strcmp(method, "Contains")) {
            ReturnBool(invocation, AB::ScreenBounds(element).Contains(x, y));
        } else {
            UltraCanvasUIElement* hit = AB::HitTest(element, x, y);
            if (hit == element) hit = nullptr;   // the point is on the element itself, not a child
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(@(so))", ElementReference(hit)));
        }
    } else if (!std::strcmp(method, "GetExtents")) {
        guint32 coordType = CoordScreen;
        g_variant_get(parameters, "(u)", &coordType);
        const AB::ScreenRect r = Relative(AB::ScreenBounds(element), element, coordType);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("((iiii))", r.x, r.y, r.width, r.height));
    } else if (!std::strcmp(method, "GetPosition")) {
        guint32 coordType = CoordScreen;
        g_variant_get(parameters, "(u)", &coordType);
        const AB::ScreenRect r = Relative(AB::ScreenBounds(element), element, coordType);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ii)", r.x, r.y));
    } else if (!std::strcmp(method, "GetSize")) {
        const AB::ScreenRect r = AB::ScreenBounds(element);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ii)", r.width, r.height));
    } else if (!std::strcmp(method, "GetLayer")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", AB::AsWindow(element) ? LayerWindow : LayerWidget));
    } else if (!std::strcmp(method, "GetMDIZOrder")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(n)", static_cast<gint16>(0)));
    } else if (!std::strcmp(method, "GrabFocus")) {
        bool ok = false;
        if (element->CanReceiveFocus()) {
            if (UltraCanvasWindowBase* window = element->GetWindow()) {
                window->SetFocusedElement(element);
                ok = element->IsFocused();
            }
        }
        ReturnBool(invocation, ok);
    } else if (!std::strcmp(method, "GetAlpha")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(d)", 1.0));
    } else {
        // SetExtents, SetPosition, SetSize, ScrollTo, ScrollToPoint: elements
        // are placed by their layout, not by assistive technology.
        ReturnBool(invocation, false);
    }
}

void TextCall(UltraCanvasUIElement* element, IAccessibleText* text, const char* method,
              GVariant* parameters, GDBusMethodInvocation* invocation) {
    const auto returnUnit = [&](const std::string& unit, int start, int end) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(sii)", unit.c_str(), start, end));
    };
    const auto attributesAt = [&](int offset, int& start, int& end) {
        start = end = 0;
        if (offset < 0 || offset >= text->GetCharacterCount()) return std::map<std::string, std::string>{};
        return AttributeMap(text->GetAttributesAt(offset, start, end));
    };

    if (!std::strcmp(method, "GetText")) {
        gint32 start = 0, end = -1;
        g_variant_get(parameters, "(ii)", &start, &end);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(s)", AB::Substring(text->GetAccessibleText(), start, end).c_str()));
    } else if (!std::strcmp(method, "GetStringAtOffset")) {
        gint32 offset = 0;
        guint32 granularity = 0;
        g_variant_get(parameters, "(iu)", &offset, &granularity);
        int start = 0, end = 0;
        const std::string unit = UnitAt(text, offset, BoundaryForGranularity(granularity), start, end);
        returnUnit(unit, start, end);
    } else if (!std::strcmp(method, "GetTextAtOffset") || !std::strcmp(method, "GetTextBeforeOffset") ||
               !std::strcmp(method, "GetTextAfterOffset")) {
        gint32 offset = 0;
        guint32 type = 0;
        g_variant_get(parameters, "(iu)", &offset, &type);
        const AccessibleTextBoundary boundary = BoundaryForType(type);
        int start = 0, end = 0;
        std::string unit = UnitAt(text, offset, boundary, start, end);
        if (!std::strcmp(method, "GetTextBeforeOffset")) {
            if (start > 0) unit = UnitAt(text, start - 1, boundary, start, end);
            else { unit.clear(); start = end = 0; }
        } else if (!std::strcmp(method, "GetTextAfterOffset")) {
            if (end < text->GetCharacterCount()) unit = UnitAt(text, end, boundary, start, end);
            else { unit.clear(); start = end = text->GetCharacterCount(); }
        }
        returnUnit(unit, start, end);
    } else if (!std::strcmp(method, "SetCaretOffset")) {
        gint32 offset = 0;
        g_variant_get(parameters, "(i)", &offset);
        ReturnBool(invocation, text->SetCaretOffset(offset));
    } else if (!std::strcmp(method, "GetCharacterAtOffset")) {
        gint32 offset = 0;
        g_variant_get(parameters, "(i)", &offset);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(i)", static_cast<gint32>(AB::CodePointAt(text->GetAccessibleText(), offset))));
    } else if (!std::strcmp(method, "GetAttributeValue")) {
        gint32 offset = 0;
        const gchar* name = nullptr;
        g_variant_get(parameters, "(i&s)", &offset, &name);
        int start = 0, end = 0;
        const auto attributes = attributesAt(offset, start, end);
        auto it = attributes.find(name ? name : "");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", it == attributes.end() ? "" : it->second.c_str()));
    } else if (!std::strcmp(method, "GetAttributes") || !std::strcmp(method, "GetAttributeRun")) {
        gint32 offset = 0;
        if (!std::strcmp(method, "GetAttributes")) {
            g_variant_get(parameters, "(i)", &offset);
        } else {
            gboolean includeDefaults = FALSE;
            g_variant_get(parameters, "(ib)", &offset, &includeDefaults);
        }
        int start = 0, end = 0;
        const auto attributes = attributesAt(offset, start, end);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a{ss}ii)", StringMap(attributes), start, end));
    } else if (!std::strcmp(method, "GetDefaultAttributes") || !std::strcmp(method, "GetDefaultAttributeSet")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a{ss})", StringMap({})));
    } else if (!std::strcmp(method, "GetCharacterExtents") || !std::strcmp(method, "GetRangeExtents")) {
        gint32 start = 0, end = 0;
        guint32 coordType = CoordScreen;
        if (!std::strcmp(method, "GetCharacterExtents")) {
            g_variant_get(parameters, "(iu)", &start, &coordType);
            end = start + 1;
        } else {
            g_variant_get(parameters, "(iiu)", &start, &end, &coordType);
        }
        const Rect2Df box = AB::RangeBounds(text, start, end);
        AB::ScreenRect r;
        if (box.width > 0 || box.height > 0) {
            r = Relative(AB::WindowRectToScreen(AB::WindowOf(element), box), element, coordType);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(iiii)", r.x, r.y, r.width, r.height));
    } else if (!std::strcmp(method, "GetOffsetAtPoint")) {
        gint32 x = 0, y = 0;
        guint32 coordType = CoordScreen;
        g_variant_get(parameters, "(iiu)", &x, &y, &coordType);
        ToScreen(element, coordType, x, y);
        const int offset = AB::CharacterAtPoint(text, AB::ScreenToWindow(AB::WindowOf(element), x, y));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", offset));
    } else if (!std::strcmp(method, "GetNSelections")) {
        int start = 0, end = 0;
        const bool selected = text->GetSelection(start, end) && start != end;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", selected ? 1 : 0));
    } else if (!std::strcmp(method, "GetSelection")) {
        gint32 index = 0;
        g_variant_get(parameters, "(i)", &index);
        int start = 0, end = 0;
        if (index != 0 || !text->GetSelection(start, end)) start = end = 0;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ii)", start, end));
    } else if (!std::strcmp(method, "AddSelection")) {
        gint32 start = 0, end = 0;
        g_variant_get(parameters, "(ii)", &start, &end);
        ReturnBool(invocation, text->SetSelection(start, end));
    } else if (!std::strcmp(method, "SetSelection")) {
        gint32 index = 0, start = 0, end = 0;
        g_variant_get(parameters, "(iii)", &index, &start, &end);
        ReturnBool(invocation, index == 0 && text->SetSelection(start, end));
    } else if (!std::strcmp(method, "RemoveSelection")) {
        gint32 index = 0;
        g_variant_get(parameters, "(i)", &index);
        const int caret = text->GetCaretOffset();
        ReturnBool(invocation, index == 0 && text->SetSelection(caret, caret));
    } else if (!std::strcmp(method, "GetBoundedRanges")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@a(iisv))", g_variant_new_array(G_VARIANT_TYPE("(iisv)"), nullptr, 0)));
    } else {
        // ScrollSubstringTo, ScrollSubstringToPoint.
        ReturnBool(invocation, false);
    }
}

// The one action an element offers (Action interface, index 0).
void ActionCall(UltraCanvasUIElement* element, const char* method, GVariant* parameters,
                GDBusMethodInvocation* invocation) {
    const std::string name = element->GetAccessibleActionName();
    if (!std::strcmp(method, "GetActions")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(sss)"));
        if (!name.empty()) g_variant_builder_add(&builder, "(sss)", name.c_str(), name.c_str(), "");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(sss))", g_variant_builder_end(&builder)));
        return;
    }
    gint32 index = 0;
    g_variant_get(parameters, "(i)", &index);
    const bool valid = index == 0 && !name.empty();
    if (!std::strcmp(method, "DoAction")) {
        ReturnBool(invocation, valid && element->DoAccessibleAction());
    } else if (!std::strcmp(method, "GetKeyBinding") || !std::strcmp(method, "GetDescription")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", ""));
    } else {   // GetName, GetLocalizedName
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", valid ? name.c_str() : ""));
    }
}

void HandleMethodCall(GDBusConnection*, const gchar*, const gchar* objectPath, const gchar* interfaceName,
                      const gchar* methodName, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer) {
    const Node node = NodeFromPath(objectPath);
    if (!node.Valid()) {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownObject",
                                                   "The element is gone");
        return;
    }
    if (!std::strcmp(interfaceName, kAccessible)) {
        AccessibleCall(node, methodName, parameters, invocation);
    } else if (!std::strcmp(interfaceName, kApplication)) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", Locale().c_str()));   // GetLocale
    } else if (!std::strcmp(interfaceName, kComponent) && node.element) {
        ComponentCall(node.element, methodName, parameters, invocation);
    } else if (!std::strcmp(interfaceName, kText) && node.element && AB::TextInterface(node.element)) {
        TextCall(node.element, AB::TextInterface(node.element), methodName, parameters, invocation);
    } else if (!std::strcmp(interfaceName, kAction) && node.element) {
        ActionCall(node.element, methodName, parameters, invocation);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownInterface", interfaceName);
    }
}

GVariant* HandleGetProperty(GDBusConnection*, const gchar*, const gchar* objectPath, const gchar* interfaceName,
                            const gchar* propertyName, GError** error, gpointer) {
    const Node node = NodeFromPath(objectPath);
    if (!node.Valid()) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT, "The element is gone");
        return nullptr;
    }
    const std::string property = propertyName;
    if (!std::strcmp(interfaceName, kAccessible)) {
        if (property == "Name") {
            std::string name;
            if (node.root) {
                auto* app = UltraCanvasApplication::GetInstance();
                name = app ? app->GetAppName() : "";
                if (name.empty() && g_get_prgname()) name = g_get_prgname();
            } else {
                name = AB::Name(node.element);
            }
            return g_variant_new_string(name.c_str());
        }
        if (property == "Description") {
            return g_variant_new_string(node.root ? "" : node.element->GetAccessibleDescription().c_str());
        }
        if (property == "HelpText") return g_variant_new_string("");
        if (property == "Parent") return ParentReference(node);
        if (property == "ChildCount") return g_variant_new_int32(static_cast<gint32>(ChildrenOf(node).size()));
        if (property == "Locale") return g_variant_new_string(Locale().c_str());
        if (property == "AccessibleId") return g_variant_new_string(node.root ? "" : node.element->GetIdentifier().c_str());
    } else if (!std::strcmp(interfaceName, kApplication) && node.root) {
        if (property == "ToolkitName") return g_variant_new_string("UltraCanvas");
        if (property == "Version") return g_variant_new_string(versionString);
        if (property == "AtspiVersion") return g_variant_new_string("2.1");
        if (property == "Id") return g_variant_new_int32(gBridge->applicationId);
    } else if (!std::strcmp(interfaceName, kText) && node.element) {
        if (IAccessibleText* text = AB::TextInterface(node.element)) {
            if (property == "CharacterCount") return g_variant_new_int32(text->GetCharacterCount());
            if (property == "CaretOffset") return g_variant_new_int32(text->GetCaretOffset());
        }
    } else if (!std::strcmp(interfaceName, kValue) && node.element) {
        AccessibleRange range;
        if (node.element->GetAccessibleRange(range)) {
            if (property == "CurrentValue") return g_variant_new_double(range.value);
            if (property == "MinimumValue") return g_variant_new_double(range.minimum);
            if (property == "MaximumValue") return g_variant_new_double(range.maximum);
            if (property == "MinimumIncrement") return g_variant_new_double(range.step);
            if (property == "Text") return g_variant_new_string(node.element->GetAccessibleValueText().c_str());
        }
    } else if (!std::strcmp(interfaceName, kAction) && node.element) {
        if (property == "NActions") return g_variant_new_int32(node.element->GetAccessibleActionName().empty() ? 0 : 1);
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No property %s", propertyName);
    return nullptr;
}

gboolean HandleSetProperty(GDBusConnection*, const gchar*, const gchar* objectPath, const gchar* interfaceName,
                           const gchar* propertyName, GVariant* value, GError** error, gpointer) {
    // The registry hands the application its id.
    if (!std::strcmp(interfaceName, kApplication) && !std::strcmp(propertyName, "Id") &&
        NodeFromPath(objectPath).root) {
        gBridge->applicationId = g_variant_get_int32(value);
        return TRUE;
    }
    // A screen reader moving a slider or spin button.
    if (!std::strcmp(interfaceName, kValue) && !std::strcmp(propertyName, "CurrentValue")) {
        const Node node = NodeFromPath(objectPath);
        if (node.element && node.element->SetAccessibleValue(g_variant_get_double(value))) return TRUE;
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "The value cannot be set");
        return FALSE;
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY, "%s is read-only", propertyName);
    return FALSE;
}

const GDBusInterfaceVTable kInterfaceVTable = {HandleMethodCall, HandleGetProperty, HandleSetProperty, {nullptr}};

gchar** EnumerateNodes(GDBusConnection*, const gchar*, const gchar*, gpointer) {
    // The tree is walked through GetChildren, not through D-Bus introspection.
    gchar** nodes = g_new0(gchar*, 2);
    nodes[0] = g_strdup("root");
    return nodes;
}

GDBusInterfaceInfo** IntrospectNode(GDBusConnection*, const gchar*, const gchar* objectPath, const gchar* node, gpointer) {
    if (!node) return nullptr;
    // GLib passes the object's full path here; documented as the subtree's
    // registration path, so fall back to joining `node` onto it.
    Node target = NodeFromPath(objectPath);
    if (!target.Valid()) target = NodeFromPath((std::string(objectPath) + "/" + node).c_str());
    if (!target.Valid()) return nullptr;
    std::vector<const char*> names{kAccessible};
    if (target.root) {
        names.push_back(kApplication);
    } else {
        names.push_back(kComponent);
        if (AB::TextInterface(target.element)) names.push_back(kText);
        AccessibleRange range;
        if (target.element->GetAccessibleRange(range)) names.push_back(kValue);
        if (!target.element->GetAccessibleActionName().empty()) names.push_back(kAction);
    }
    GDBusInterfaceInfo** infos = g_new0(GDBusInterfaceInfo*, names.size() + 1);
    for (size_t i = 0; i < names.size(); i++) {
        infos[i] = g_dbus_interface_info_ref(g_dbus_node_info_lookup_interface(gBridge->nodeInfo, names[i]));
    }
    return infos;
}

const GDBusInterfaceVTable* DispatchNode(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                         const gchar*, gpointer* outUserData, gpointer) {
    *outUserData = nullptr;
    return &kInterfaceVTable;
}

const GDBusSubtreeVTable kSubtreeVTable = {EnumerateNodes, IntrospectNode, DispatchNode, {nullptr}};

// libatspi asks a new application for all its objects at once. The answer
// "none yet" is valid: it then reads the tree lazily through GetChildren,
// which is all a tree that changes with the layout can promise anyway.
void HandleCacheCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                     GVariant*, GDBusMethodInvocation* invocation, gpointer) {
    g_dbus_method_invocation_return_value(invocation,
        g_variant_new("(@a((so)(so)(so)iiassusau))",
                      g_variant_new_array(G_VARIANT_TYPE("((so)(so)(so)iiassusau)"), nullptr, 0)));
}

const GDBusInterfaceVTable kCacheVTable = {HandleCacheCall, nullptr, nullptr, {nullptr}};

// ===== CONNECTION =====

void PumpContext() {
    if (!gBridge) return;
    // The first iteration also acknowledges the wake-up that brought us here.
    while (g_main_context_iteration(gBridge->context, FALSE)) {}
}

void OnEmbedded(GObject* source, GAsyncResult* result, gpointer) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!reply) {
        debugOutput << "UltraCanvas: AT-SPI registration failed: " << (error ? error->message : "?") << std::endl;
        if (error) g_error_free(error);
        return;
    }
    if (gBridge) {
        const gchar* name = nullptr;
        const gchar* path = nullptr;
        g_variant_get(reply, "((&s&o))", &name, &path);
        gBridge->parentName = name ? name : "";
        gBridge->parentPath = path ? path : kNullPath;
        debugOutput << "UltraCanvas: registered on the accessibility bus as " << gBridge->uniqueName << std::endl;
    }
    g_variant_unref(reply);
}

void Connect() {
    if (!gBridge || gBridge->connected || !gBridge->session) return;
    ContextScope scope(gBridge->context);
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(gBridge->session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus",
                                                  "GetAddress", nullptr, G_VARIANT_TYPE("(s)"),
                                                  G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
    if (!reply) {
        debugOutput << "UltraCanvas: no accessibility bus: " << (error ? error->message : "?") << std::endl;
        if (error) g_error_free(error);
        return;
    }
    const gchar* address = nullptr;
    g_variant_get(reply, "(&s)", &address);
    gBridge->bus = g_dbus_connection_new_for_address_sync(
        address, static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                   G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    g_variant_unref(reply);
    if (!gBridge->bus) {
        debugOutput << "UltraCanvas: cannot reach the accessibility bus: " << (error ? error->message : "?") << std::endl;
        if (error) g_error_free(error);
        return;
    }
    gBridge->uniqueName = g_dbus_connection_get_unique_name(gBridge->bus);
    gBridge->subtree = g_dbus_connection_register_subtree(gBridge->bus, kObjectPrefix, &kSubtreeVTable,
                                                          G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
                                                          nullptr, nullptr, &error);
    if (!gBridge->subtree) {
        debugOutput << "UltraCanvas: cannot publish accessible objects: " << (error ? error->message : "?") << std::endl;
        if (error) g_error_free(error);
        g_object_unref(gBridge->bus);
        gBridge->bus = nullptr;
        return;
    }
    gBridge->cacheObject = g_dbus_connection_register_object(
        gBridge->bus, kCachePath, g_dbus_node_info_lookup_interface(gBridge->nodeInfo, kCache),
        &kCacheVTable, nullptr, nullptr, nullptr);
    gBridge->connected = true;
    gBridge->listener = UltraCanvasAccessibility::AddListener(OnAccessibilityEvent);
    // Asynchronous: the registry may call back into the application before it
    // replies, and those calls are answered by this same loop.
    g_dbus_connection_call(gBridge->bus, "org.a11y.atspi.Registry", kRootPath, "org.a11y.atspi.Socket", "Embed",
                           g_variant_new("((so))", gBridge->uniqueName.c_str(), kRootPath), G_VARIANT_TYPE("((so))"),
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnEmbedded, nullptr);
}

void Disconnect() {
    if (!gBridge || !gBridge->connected) return;
    if (gBridge->listener) UltraCanvasAccessibility::RemoveListener(gBridge->listener);
    gBridge->listener = 0;
    gBridge->connected = false;
    // Tell the registry the application is leaving rather than letting it
    // find out when the connection drops.
    g_dbus_connection_call_sync(gBridge->bus, "org.a11y.atspi.Registry", kRootPath, "org.a11y.atspi.Socket",
                                "Unembed", g_variant_new("((so))", gBridge->uniqueName.c_str(), kRootPath),
                                nullptr, G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr);
    g_dbus_connection_unregister_subtree(gBridge->bus, gBridge->subtree);
    gBridge->subtree = 0;
    if (gBridge->cacheObject) g_dbus_connection_unregister_object(gBridge->bus, gBridge->cacheObject);
    gBridge->cacheObject = 0;
    g_dbus_connection_flush_sync(gBridge->bus, nullptr, nullptr);
    g_object_unref(gBridge->bus);
    gBridge->bus = nullptr;
    gBridge->ids.Clear();
    gBridge->textCache.clear();
    gBridge->focused = nullptr;
    gBridge->activeWindow = nullptr;
}

bool StatusSaysOn(GVariant* properties) {
    gboolean enabled = FALSE, screenReader = FALSE;
    g_variant_lookup(properties, "IsEnabled", "b", &enabled);
    g_variant_lookup(properties, "ScreenReaderEnabled", "b", &screenReader);
    return enabled || screenReader;
}

bool AlwaysOn() {
    const char* value = std::getenv("UC_ACCESSIBILITY_ALWAYS_ON");
    return value && std::strcmp(value, "1") == 0;
}

void OnStatusChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                     GVariant* parameters, gpointer) {
    if (!gBridge) return;
    GVariant* changed = nullptr;
    g_variant_get(parameters, "(&s@a{sv}@as)", nullptr, &changed, nullptr);
    if (!changed) return;
    if (StatusSaysOn(changed)) {
        Connect();
    } else if (!AlwaysOn()) {
        // Only the keys that changed are in `changed`; turning one of the two
        // off leaves the other as it was, so ask again.
        GVariant* all = g_dbus_connection_call_sync(gBridge->session, "org.a11y.Bus", "/org/a11y/bus",
                                                    "org.freedesktop.DBus.Properties", "GetAll",
                                                    g_variant_new("(s)", "org.a11y.Status"), G_VARIANT_TYPE("(a{sv})"),
                                                    G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        if (all) {
            GVariant* properties = g_variant_get_child_value(all, 0);
            if (!StatusSaysOn(properties)) Disconnect();
            g_variant_unref(properties);
            g_variant_unref(all);
        }
    }
    g_variant_unref(changed);
}

bool HaveSessionBus() {
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (address && *address) return true;
    // systemd's per-user bus, which GIO finds without the variable.
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    return runtime && *runtime && access((std::string(runtime) + "/bus").c_str(), F_OK) == 0;
}

} // namespace

void Start() {
    if (gBridge) return;
    const char* noBridge = std::getenv("NO_AT_BRIDGE");
    if (noBridge && std::strcmp(noBridge, "1") == 0) return;
    // Without a session bus GIO would try to autolaunch one; there is no
    // accessibility bus to find then anyway.
    if (!HaveSessionBus()) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;

    gBridge = new Bridge();
    gBridge->context = g_main_context_new();
    // Owning the context makes GDBus wake it (its wake-up fd becomes readable)
    // whenever it queues a call for us from its worker thread.
    g_main_context_acquire(gBridge->context);
    gint priority = 0, timeout = 0;
    GPollFD fds[4];
    g_main_context_prepare(gBridge->context, &priority);
    const gint count = g_main_context_query(gBridge->context, priority, &timeout, fds, 4);
    g_main_context_check(gBridge->context, priority, fds, std::min<gint>(count, 4));
    if (count != 1) {
        // A fresh context polls exactly its wake-up fd; anything else is a
        // GLib this code does not know.
        debugOutput << "UltraCanvas: AT-SPI bridge: unexpected GMainContext layout" << std::endl;
        Stop();
        return;
    }
    gBridge->contextWatch = app->AddFdWatch(fds[0].fd, FdWatchType::Read, PumpContext);
    gBridge->watching = true;

    ContextScope scope(gBridge->context);
    GError* error = nullptr;
    gBridge->nodeInfo = g_dbus_node_info_new_for_xml(kIntrospection, &error);
    if (!gBridge->nodeInfo) {
        debugOutput << "UltraCanvas: AT-SPI introspection data: " << (error ? error->message : "?") << std::endl;
        if (error) g_error_free(error);
        Stop();
        return;
    }
    gBridge->session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!gBridge->session) {
        debugOutput << "UltraCanvas: no session bus for accessibility: " << (error ? error->message : "?") << std::endl;
        if (error) g_error_free(error);
        Stop();
        return;
    }
    // Follow the desktop's switch, so a screen reader started later finds
    // the application too.
    gBridge->statusSubscription = g_dbus_connection_signal_subscribe(
        gBridge->session, "org.a11y.Bus", "org.freedesktop.DBus.Properties", "PropertiesChanged",
        "/org/a11y/bus", "org.a11y.Status", G_DBUS_SIGNAL_FLAGS_NONE, OnStatusChanged, nullptr, nullptr);

    bool on = AlwaysOn();
    if (!on) {
        GVariant* all = g_dbus_connection_call_sync(gBridge->session, "org.a11y.Bus", "/org/a11y/bus",
                                                    "org.freedesktop.DBus.Properties", "GetAll",
                                                    g_variant_new("(s)", "org.a11y.Status"), G_VARIANT_TYPE("(a{sv})"),
                                                    G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        if (all) {
            GVariant* properties = g_variant_get_child_value(all, 0);
            on = StatusSaysOn(properties);
            g_variant_unref(properties);
            g_variant_unref(all);
        }
    }
    if (on) Connect();
}

void Stop() {
    if (!gBridge) return;
    Disconnect();
    if (gBridge->session) {
        if (gBridge->statusSubscription) g_dbus_connection_signal_unsubscribe(gBridge->session, gBridge->statusSubscription);
        g_object_unref(gBridge->session);
    }
    if (gBridge->nodeInfo) g_dbus_node_info_unref(gBridge->nodeInfo);
    if (gBridge->watching) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->RemoveFdWatch(gBridge->contextWatch);
    }
    if (gBridge->context) {
        // Run what GDBus queued for the context before it goes.
        while (g_main_context_iteration(gBridge->context, FALSE)) {}
        g_main_context_release(gBridge->context);
        g_main_context_unref(gBridge->context);
    }
    delete gBridge;
    gBridge = nullptr;
}

bool IsConnected() {
    return gBridge && gBridge->connected;
}

} // namespace LinuxAccessibility
} // namespace UltraCanvas
