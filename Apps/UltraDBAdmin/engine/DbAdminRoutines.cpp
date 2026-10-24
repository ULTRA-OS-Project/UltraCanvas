// Apps/UltraDBAdmin/engine/DbAdminRoutines.cpp
// Version: 0.2.0 - MySQL / MariaDB: triggers, functions and procedures; DELIMITER scripts to edit
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRoutines.h"

#include "DbAdminExport.h"   // QuotedName

namespace UltraDBAdmin {
namespace {

std::string Text(const UltraDbRow& row, const char* column) {
    return row[column].IsNull() ? std::string() : row[column].AsString();
}

// SHOW CREATE FUNCTION / PROCEDURE names the routine without its database,
// so run in the SQL tab it would land in the session's default one: the
// name after `word` ("FUNCTION ") made `qualified`. The body needs nothing:
// a routine runs with its own database as the default.
std::string WithQualifiedName(const std::string& sql, const std::string& word, const std::string& qualified) {
    const size_t at = sql.find(word);
    if (at == std::string::npos) return sql;
    size_t begin = at + word.size();
    while (begin < sql.size() && sql[begin] == ' ') ++begin;
    size_t end = begin;
    while (end < sql.size() && sql[end] != '(' && sql[end] != ' ' && sql[end] != '\n') {
        if (sql[end] == '`') {
            const size_t close = sql.find('`', end + 1);
            end = close == std::string::npos ? sql.size() : close + 1;
        } else {
            ++end;
        }
    }
    return sql.substr(0, begin) + qualified + sql.substr(end);
}

UltraDbResult ListMysqlRoutines(const std::string& connection, const std::string& schema,
                                std::vector<RoutineInfo>& out) {
    UltraDbResultSet rows;
    UltraDbResult result = UltraDb_Query(
        connection,
        "SELECT TRIGGER_NAME AS name, EVENT_OBJECT_TABLE AS tbl, ACTION_TIMING AS timing,"
        "       EVENT_MANIPULATION AS event, ACTION_STATEMENT AS body FROM information_schema.TRIGGERS"
        " WHERE TRIGGER_SCHEMA = ? ORDER BY EVENT_OBJECT_TABLE, ACTION_ORDER, TRIGGER_NAME",
        {UltraDbValue(schema)}, rows);
    if (!result) return result;
    for (size_t i = 0; i < rows.Size(); ++i) {
        const UltraDbRow& row = rows.Row(i);
        RoutineInfo routine;
        routine.kind = RoutineInfo::Kind::Trigger;
        routine.schema = schema;
        routine.name = Text(row, "name");
        routine.table = Text(row, "tbl");
        // Made from its parts, the trigger and its table named with the
        // database, which the statement it was written as may not say.
        routine.definition = "CREATE TRIGGER " + QualifiedName(SqlEngine::MySql, schema, routine.name) + " " +
                             Text(row, "timing") + " " + Text(row, "event") + " ON " +
                             QualifiedName(SqlEngine::MySql, schema, routine.table) + " FOR EACH ROW " + Text(row, "body");
        out.push_back(std::move(routine));
    }
    rows.Clear();
    // Functions and procedures, with their parameters as the statement
    // that made them lists them.
    result = UltraDb_Query(
        connection,
        "SELECT r.ROUTINE_NAME AS name, r.ROUTINE_TYPE AS type, r.DTD_IDENTIFIER AS returns,"
        "       r.ROUTINE_BODY AS language,"
        "       (SELECT GROUP_CONCAT(CONCAT_WS(' ', IF(r.ROUTINE_TYPE = 'PROCEDURE', p.PARAMETER_MODE, NULL),"
        "                                      p.PARAMETER_NAME, p.DTD_IDENTIFIER)"
        "                            ORDER BY p.ORDINAL_POSITION SEPARATOR ', ')"
        "          FROM information_schema.PARAMETERS p"
        "         WHERE p.SPECIFIC_SCHEMA = r.ROUTINE_SCHEMA AND p.SPECIFIC_NAME = r.SPECIFIC_NAME"
        "           AND p.ORDINAL_POSITION > 0) AS args"
        "  FROM information_schema.ROUTINES r"
        " WHERE r.ROUTINE_SCHEMA = ? ORDER BY r.ROUTINE_NAME",
        {UltraDbValue(schema)}, rows);
    if (!result) return result;
    for (size_t i = 0; i < rows.Size(); ++i) {
        const UltraDbRow& row = rows.Row(i);
        RoutineInfo routine;
        const bool procedure = Text(row, "type") == "PROCEDURE";
        routine.kind = procedure ? RoutineInfo::Kind::Procedure : RoutineInfo::Kind::Function;
        routine.schema = schema;
        routine.name = Text(row, "name");
        routine.arguments = Text(row, "args");
        routine.returns = procedure ? std::string() : Text(row, "returns");
        routine.language = Text(row, "language");
        UltraDbResultSet shown;
        if (UltraDb_Query(connection,
                          std::string(procedure ? "SHOW CREATE PROCEDURE " : "SHOW CREATE FUNCTION ") +
                              QualifiedName(SqlEngine::MySql, schema, routine.name),
                          {}, shown) && shown.Size() == 1)
            routine.definition = WithQualifiedName(Text(shown.Row(0), procedure ? "Create Procedure" : "Create Function"),
                                                   procedure ? "PROCEDURE " : "FUNCTION ",
                                                   QualifiedName(SqlEngine::MySql, schema, routine.name));
        out.push_back(std::move(routine));
    }
    return UltraDbResult::Ok();
}

std::string Trimmed(std::string sql) {
    while (!sql.empty() && (sql.back() == ';' || sql.back() == ' ' || sql.back() == '\n' || sql.back() == '\r' ||
                            sql.back() == '\t'))
        sql.pop_back();
    return sql;
}

} // namespace

std::string WithoutDefiner(const std::string& sql) {
    const size_t at = sql.find(" DEFINER=");
    if (at == std::string::npos || at > 40) return sql;
    size_t end = at + 9;
    // `user`@`host`, each part in backticks or bare up to the next space.
    while (end < sql.size() && sql[end] != ' ') {
        if (sql[end] == '`') {
            const size_t close = sql.find('`', end + 1);
            end = close == std::string::npos ? sql.size() : close + 1;
        } else {
            ++end;
        }
    }
    return sql.substr(0, at) + sql.substr(end);
}

const char* RoutineKindText(RoutineInfo::Kind kind) {
    switch (kind) {
        case RoutineInfo::Kind::Trigger:   return "trigger";
        case RoutineInfo::Kind::Function:  return "function";
        case RoutineInfo::Kind::Procedure: return "procedure";
        case RoutineInfo::Kind::Aggregate: return "aggregate";
        case RoutineInfo::Kind::Window:    return "window function";
    }
    return "routine";
}

UltraDbResult ListRoutines(const std::string& connection, SqlEngine engine, const std::string& schema,
                           std::vector<RoutineInfo>& out) {
    out.clear();
    UltraDbResultSet rows;
    if (engine == SqlEngine::MySql) return ListMysqlRoutines(connection, schema, out);
    if (engine == SqlEngine::Sqlite) {
        const UltraDbResult result = UltraDb_Query(
            connection,
            "SELECT name, tbl_name, sql FROM " + QualifiedName(engine, schema.empty() ? std::string("main") : schema,
                                                               "sqlite_master") +
                " WHERE type = 'trigger' ORDER BY tbl_name, name",
            {}, rows);
        if (!result) return result;
        for (size_t i = 0; i < rows.Size(); ++i) {
            RoutineInfo routine;
            routine.kind = RoutineInfo::Kind::Trigger;
            routine.schema = schema;
            routine.name = Text(rows.Row(i), "name");
            routine.table = Text(rows.Row(i), "tbl_name");
            routine.definition = Text(rows.Row(i), "sql");
            out.push_back(std::move(routine));
        }
        return UltraDbResult::Ok();
    }

    UltraDbResult result = UltraDb_Query(
        connection,
        "SELECT t.tgname AS name, c.relname AS tbl, pg_get_triggerdef(t.oid, true) AS def"
        "  FROM pg_catalog.pg_trigger t"
        "  JOIN pg_catalog.pg_class c ON c.oid = t.tgrelid"
        "  JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = ? AND NOT t.tgisinternal"
        " ORDER BY c.relname, t.tgname",
        {UltraDbValue(schema)}, rows);
    if (!result) return result;
    for (size_t i = 0; i < rows.Size(); ++i) {
        RoutineInfo routine;
        routine.kind = RoutineInfo::Kind::Trigger;
        routine.schema = schema;
        routine.name = Text(rows.Row(i), "name");
        routine.table = Text(rows.Row(i), "tbl");
        routine.definition = Text(rows.Row(i), "def");
        out.push_back(std::move(routine));
    }
    rows.Clear();
    // Functions, procedures, aggregates - not those an extension installed.
    result = UltraDb_Query(
        connection,
        "SELECT p.proname AS name, p.prokind::text AS kind,"
        "       pg_get_function_identity_arguments(p.oid) AS args,"
        "       pg_get_function_result(p.oid) AS returns, l.lanname AS language,"
        "       CASE WHEN p.prokind IN ('f', 'p', 'w') THEN pg_get_functiondef(p.oid) END AS def"
        "  FROM pg_catalog.pg_proc p"
        "  JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace"
        "  JOIN pg_catalog.pg_language l ON l.oid = p.prolang"
        " WHERE n.nspname = ?"
        "   AND NOT EXISTS (SELECT 1 FROM pg_catalog.pg_depend d"
        "                    WHERE d.classid = 'pg_catalog.pg_proc'::regclass AND d.objid = p.oid AND d.deptype = 'e')"
        " ORDER BY p.proname, args",
        {UltraDbValue(schema)}, rows);
    if (!result) return result;
    for (size_t i = 0; i < rows.Size(); ++i) {
        const UltraDbRow& row = rows.Row(i);
        RoutineInfo routine;
        const std::string kind = Text(row, "kind");
        routine.kind = kind == "p"   ? RoutineInfo::Kind::Procedure
                       : kind == "a" ? RoutineInfo::Kind::Aggregate
                       : kind == "w" ? RoutineInfo::Kind::Window
                                     : RoutineInfo::Kind::Function;
        routine.schema = schema;
        routine.name = Text(row, "name");
        routine.arguments = Text(row, "args");
        routine.returns = Text(row, "returns");
        routine.language = Text(row, "language");
        routine.definition = Text(row, "def");
        out.push_back(std::move(routine));
    }
    return UltraDbResult::Ok();
}

OperationPlan DropRoutinePlan(SqlEngine engine, const RoutineInfo& routine) {
    OperationPlan plan;
    if (engine == SqlEngine::MySql) {
        // One routine to a name in MySQL: no arguments needed, nor taken.
        const char* what = routine.kind == RoutineInfo::Kind::Trigger     ? "TRIGGER "
                           : routine.kind == RoutineInfo::Kind::Procedure ? "PROCEDURE "
                                                                          : "FUNCTION ";
        plan.statements.push_back(std::string("DROP ") + what + QualifiedName(engine, routine.schema, routine.name));
        plan.transaction = false;   // MySQL commits a DROP as it runs
        if (routine.kind != RoutineInfo::Kind::Trigger)
            plan.notes.push_back("A trigger, a view or another routine that calls it fails from now on: MySQL does "
                                 "not check.");
        return plan;
    }
    switch (routine.kind) {
        case RoutineInfo::Kind::Trigger:
            plan.statements.push_back(engine == SqlEngine::Sqlite
                                          ? "DROP TRIGGER " + QualifiedName(engine, routine.schema, routine.name)
                                          : "DROP TRIGGER " + QuotedName(engine, routine.name) + " ON " +
                                                QualifiedName(engine, routine.schema, routine.table));
            break;
        case RoutineInfo::Kind::Procedure:
            plan.statements.push_back("DROP PROCEDURE " + QualifiedName(engine, routine.schema, routine.name) + "(" +
                                      routine.arguments + ")");
            break;
        case RoutineInfo::Kind::Aggregate:
            plan.statements.push_back("DROP AGGREGATE " + QualifiedName(engine, routine.schema, routine.name) + "(" +
                                      (routine.arguments.empty() ? std::string("*") : routine.arguments) + ")");
            break;
        case RoutineInfo::Kind::Function:
        case RoutineInfo::Kind::Window:
            plan.statements.push_back("DROP FUNCTION " + QualifiedName(engine, routine.schema, routine.name) + "(" +
                                      routine.arguments + ")");
            plan.notes.push_back("A trigger or a default that calls it makes the server refuse: CASCADE is never "
                                 "written here.");
            break;
    }
    return plan;
}

std::string RoutineTemplate(SqlEngine engine, RoutineInfo::Kind kind, const std::string& schema,
                            const std::string& table) {
    const std::string target =
        table.empty() ? QualifiedName(engine, schema, "table_name") : QualifiedName(engine, schema, table);
    if (engine == SqlEngine::MySql) {
        // A body of several statements is written between DELIMITER lines,
        // as the mysql client takes it - and the SQL tab does too.
        if (kind == RoutineInfo::Kind::Trigger)
            return "-- A new trigger: name it, pick its event, write its body.\n"
                   "DELIMITER $$\n"
                   "CREATE TRIGGER " + QualifiedName(engine, schema, "trigger_name") + "\n"
                   "BEFORE INSERT ON " + target + "\n"
                   "FOR EACH ROW\n"
                   "BEGIN\n"
                   "    -- NEW.column is the row being inserted\n"
                   "    SET @trigger_ran = 1;\n"
                   "END$$\n"
                   "DELIMITER ;\n";
        if (kind == RoutineInfo::Kind::Procedure)
            return "DELIMITER $$\n"
                   "CREATE PROCEDURE " + QualifiedName(engine, schema, "procedure_name") + "(IN arg INT)\n"
                   "BEGIN\n"
                   "    -- statements; a procedure may COMMIT\n"
                   "    SELECT arg;\n"
                   "END$$\n"
                   "DELIMITER ;\n";
        // DETERMINISTIC: with binary logging on, MySQL refuses a function
        // that does not say what it does.
        return "DELIMITER $$\n"
               "CREATE FUNCTION " + QualifiedName(engine, schema, "function_name") + "(arg INT)\n"
               "RETURNS INT DETERMINISTIC\n"
               "BEGIN\n"
               "    RETURN arg * 2;\n"
               "END$$\n"
               "DELIMITER ;\n";
    }
    if (engine == SqlEngine::Sqlite) {
        // SQLite has triggers alone.
        return "-- A new trigger: name it, pick its event, write its body.\n"
               "CREATE TRIGGER " + QualifiedName(engine, schema, "trigger_name") + "\n"
               "AFTER INSERT ON " + (table.empty() ? QuotedName(engine, "table_name") : QuotedName(engine, table)) + "\n"
               "FOR EACH ROW\n"
               "BEGIN\n"
               "    -- new.column is the row inserted\n"
               "    SELECT 1;\n"
               "END;\n";
    }
    if (kind == RoutineInfo::Kind::Trigger) {
        return "-- A new trigger: its function first, then the trigger that calls it.\n"
               "CREATE OR REPLACE FUNCTION " + QualifiedName(engine, schema, "trigger_function") + "()\n"
               "RETURNS trigger LANGUAGE plpgsql AS $$\n"
               "BEGIN\n"
               "    -- NEW is the row inserted or updated\n"
               "    RETURN NEW;\n"
               "END;\n"
               "$$;\n\n"
               "CREATE TRIGGER trigger_name\n"
               "BEFORE INSERT OR UPDATE ON " + target + "\n"
               "FOR EACH ROW EXECUTE FUNCTION " + QualifiedName(engine, schema, "trigger_function") + "();\n";
    }
    if (kind == RoutineInfo::Kind::Procedure) {
        return "CREATE OR REPLACE PROCEDURE " + QualifiedName(engine, schema, "procedure_name") + "(arg integer)\n"
               "LANGUAGE plpgsql AS $$\n"
               "BEGIN\n"
               "    -- statements; a procedure may COMMIT\n"
               "END;\n"
               "$$;\n";
    }
    return "CREATE OR REPLACE FUNCTION " + QualifiedName(engine, schema, "function_name") + "(arg integer)\n"
           "RETURNS integer LANGUAGE sql STABLE AS $$\n"
           "    SELECT arg * 2\n"
           "$$;\n";
}

std::string EditableDefinition(SqlEngine engine, const RoutineInfo& routine) {
    if (routine.definition.empty()) return std::string();
    if (engine == SqlEngine::MySql) {
        // MySQL replaces no routine (MariaDB's CREATE OR REPLACE is its own),
        // and a DROP commits as it runs: the old one is gone before the new
        // one is made. The DEFINER clause is left out (WithoutDefiner).
        const char* what = routine.kind == RoutineInfo::Kind::Trigger     ? "TRIGGER "
                           : routine.kind == RoutineInfo::Kind::Procedure ? "PROCEDURE "
                                                                          : "FUNCTION ";
        return "-- MySQL replaces no routine: it is dropped and created again. Should the new one fail,\n"
               "-- the old one is gone: keep this text until it has run.\n"
               "DELIMITER $$\n"
               "DROP " + std::string(what) + "IF EXISTS " + QualifiedName(engine, routine.schema, routine.name) +
               "$$\n" + Trimmed(WithoutDefiner(routine.definition)) + "$$\n"
               "DELIMITER ;\n";
    }
    if (engine == SqlEngine::Sqlite) {
        return "-- SQLite replaces no trigger: it is dropped and created again, in one transaction.\n"
               "BEGIN;\n"
               "DROP TRIGGER " + QualifiedName(engine, routine.schema, routine.name) + ";\n" +
               Trimmed(routine.definition) + ";\n"
               "COMMIT;\n";
    }
    std::string sql = Trimmed(routine.definition);
    // pg_get_triggerdef says CREATE TRIGGER; PostgreSQL 14 replaces one in place.
    if (routine.kind == RoutineInfo::Kind::Trigger && sql.rfind("CREATE TRIGGER", 0) == 0)
        sql = "CREATE OR REPLACE TRIGGER" + sql.substr(14);
    return sql + ";\n";
}

} // namespace UltraDBAdmin
