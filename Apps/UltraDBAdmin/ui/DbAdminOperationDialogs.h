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
//   ShowTextDialog       - text to read: integrity_check's findings.
// The dialogs never run SQL themselves: they hand what was chosen back to
// the window, which plans and runs it on its worker. A callback that returns
// an error keeps the dialog open with the error shown; "" closes it.
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

} // namespace UltraDBAdmin
