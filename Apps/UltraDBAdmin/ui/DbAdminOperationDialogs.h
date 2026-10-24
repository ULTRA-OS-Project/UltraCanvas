// Apps/UltraDBAdmin/ui/DbAdminOperationDialogs.h
// The dialogs of the tree's operations (DbAdminOperations): each says what
// will happen and shows the SQL before anything runs.
//   ShowPlanDialog       - an operation's SQL, read-only, with Cancel and the
//                          action (red for one that removes something);
//   ShowOperationDialog  - a name to type and options to tick (copy a table,
//                          rename it, empty it, dump a schema), a Preview SQL
//                          button that asks the caller for the plan, and the
//                          action, which asks the caller to run it;
//   ShowNewTableDialog   - a new table's name and its columns: name, type,
//                          NOT NULL, key, numbered, default - a line each,
//                          lines added and removed;
//   ShowTextDialog       - text to read: integrity_check's findings;
//   ShowRelayDialog      - an UltraDBAdmin relay's address and the user's
//                          token, for the connections it offers;
//   ShowCompareDialog    - a table's rows beside another table's: the other
//                          connection, schema and table, Compare, what
//                          differs in a grid, and the SQL that makes the
//                          other table the same.
// The dialogs never run SQL themselves: they hand what was chosen back to
// the window, which plans and runs it on its worker. A callback that returns
// an error keeps the dialog open with the error shown; "" closes it.
// Version: 0.2.0 - ShowCompareDialog, ShowRelayDialog
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminOperations.h"
#include "DbAdminTheme.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraDBAdmin {

void ShowPlanDialog(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& title, const std::string& intro,
                    const std::string& sql, const std::string& action, ButtonRole role, std::function<void()> onRun);

struct OperationDialogSpec {
    std::string title;
    std::string intro;                 // markdown-free text above the fields
    std::string nameLabel;             // "" : no name to type
    std::string name;                  // its first value
    std::vector<std::pair<std::string, bool>> options;   // a checkbox each: label, ticked
    std::vector<std::string> optionTips;                  // the checkboxes' tooltips, by index
    std::string action;                // the button: "Copy", "Empty table"
    ButtonRole role = ButtonRole::Primary;
};

struct OperationChoice {
    std::string name;
    std::vector<bool> options;
};

// `onPreview` returns the plan's SQL, or an error (shown, the dialog stays);
// `onRun` returns an error, or "" to close.
void ShowOperationDialog(UltraCanvas::UltraCanvasWindowBase* parent, const OperationDialogSpec& spec,
                         std::function<bool(const OperationChoice&, std::string& sqlOrError)> onPreview,
                         std::function<std::string(const OperationChoice&)> onRun);

// `onPreview` and `onCreate` as above, for the table's name and columns.
void ShowNewTableDialog(UltraCanvas::UltraCanvasWindowBase* parent, SqlEngine engine, const std::string& schema,
                        std::function<bool(const std::string& name, const std::vector<NewColumn>& columns,
                                           std::string& sqlOrError)> onPreview,
                        std::function<std::string(const std::string& name, const std::vector<NewColumn>& columns)> onCreate);

void ShowTextDialog(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& title, const std::string& intro,
                    const std::string& text);

// The relay to sign in to: its address (`url` first) and the token. Connect
// calls `onConnect`, which answers through `done`: "" closes the dialog, an
// error is shown and it stays. `cancellable` false: the browser build, which
// has nothing to show without a relay, offers no Cancel.
void ShowRelayDialog(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& url, bool cancellable,
                     std::function<void(const std::string& url, const std::string& token,
                                        std::function<void(const std::string& error)> done)> onConnect);

struct CompareDialogSpec {
    std::string title;
    std::string intro;
    std::vector<std::string> connections;   // the names to pick the other table's connection from
    int connection = 0;                     // picked first
    std::string schema, table;              // the other table, first guessed
};

struct CompareChoice {
    int connection = 0;
    std::string schema, table;
};

// What a compare found: an error ("" when it ran), a summary, the
// differences as rows, and whether the other table can be made the same.
using CompareShown = std::function<void(const std::string& error, const std::string& summary,
                                        const UltraDbResultSet& rows, bool canSync)>;

// `onCompare` compares on the window's worker and calls `shown` on the UI
// thread; `onSync` shows the plan and runs it, then calls `again`, which
// compares once more.
void ShowCompareDialog(UltraCanvas::UltraCanvasWindowBase* parent, const CompareDialogSpec& spec,
                       std::function<void(const CompareChoice&, CompareShown shown)> onCompare,
                       std::function<void(const CompareChoice&, std::function<void()> again)> onSync);

} // namespace UltraDBAdmin
