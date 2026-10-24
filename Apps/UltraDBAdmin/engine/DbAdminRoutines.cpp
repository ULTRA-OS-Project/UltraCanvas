// Apps/UltraDBAdmin/engine/DbAdminRoutines.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRoutines.h"

#include "DbAdminExport.h"   // QuotedName

namespace UltraDBAdmin {
namespace {

std::string Text(const UltraDbRow& row, const char* column) {
    return row[column].IsNull() ? std::string() : row[column].AsString();
}

std::string Trimmed(std::string sql) {
    while (!sql.empty() && (sql.back() == ';' || sql.back() == ' ' || sql.back() == '\n' || sql.back() == '\r' ||
                            sql.back() == '\t'))
        sql.pop_back();
    return sql;
}

} // namespace

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
    if (engine == SqlEngine::Sqlite) {
        const UltraDbResult result = UltraDb_Query(
            connection,
            "SELECT name, tbl_name, sql FROM " + QualifiedName(schema.empty() ? std::string("main") : schema,
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
    switch (routine.kind) {
        case RoutineInfo::Kind::Trigger:
            plan.statements.push_back(engine == SqlEngine::Sqlite
                                          ? "DROP TRIGGER " + QualifiedName(routine.schema, routine.name)
                                          : "DROP TRIGGER " + QuotedName(routine.name) + " ON " +
                                                QualifiedName(routine.schema, routine.table));
            break;
        case RoutineInfo::Kind::Procedure:
            plan.statements.push_back("DROP PROCEDURE " + QualifiedName(routine.schema, routine.name) + "(" +
                                      routine.arguments + ")");
            break;
        case RoutineInfo::Kind::Aggregate:
            plan.statements.push_back("DROP AGGREGATE " + QualifiedName(routine.schema, routine.name) + "(" +
                                      (routine.arguments.empty() ? std::string("*") : routine.arguments) + ")");
            break;
        case RoutineInfo::Kind::Function:
        case RoutineInfo::Kind::Window:
            plan.statements.push_back("DROP FUNCTION " + QualifiedName(routine.schema, routine.name) + "(" +
                                      routine.arguments + ")");
            plan.notes.push_back("A trigger or a default that calls it makes the server refuse: CASCADE is never "
                                 "written here.");
            break;
    }
    return plan;
}

std::string RoutineTemplate(SqlEngine engine, RoutineInfo::Kind kind, const std::string& schema,
                            const std::string& table) {
    const std::string target = table.empty() ? QualifiedName(schema, "table_name") : QualifiedName(schema, table);
    if (engine == SqlEngine::Sqlite) {
        // SQLite has triggers alone.
        return "-- A new trigger: name it, pick its event, write its body.\n"
               "CREATE TRIGGER " + QualifiedName(schema, "trigger_name") + "\n"
               "AFTER INSERT ON " + (table.empty() ? QuotedName("table_name") : QuotedName(table)) + "\n"
               "FOR EACH ROW\n"
               "BEGIN\n"
               "    -- new.column is the row inserted\n"
               "    SELECT 1;\n"
               "END;\n";
    }
    if (kind == RoutineInfo::Kind::Trigger) {
        return "-- A new trigger: its function first, then the trigger that calls it.\n"
               "CREATE OR REPLACE FUNCTION " + QualifiedName(schema, "trigger_function") + "()\n"
               "RETURNS trigger LANGUAGE plpgsql AS $$\n"
               "BEGIN\n"
               "    -- NEW is the row inserted or updated\n"
               "    RETURN NEW;\n"
               "END;\n"
               "$$;\n\n"
               "CREATE TRIGGER trigger_name\n"
               "BEFORE INSERT OR UPDATE ON " + target + "\n"
               "FOR EACH ROW EXECUTE FUNCTION " + QualifiedName(schema, "trigger_function") + "();\n";
    }
    if (kind == RoutineInfo::Kind::Procedure) {
        return "CREATE OR REPLACE PROCEDURE " + QualifiedName(schema, "procedure_name") + "(arg integer)\n"
               "LANGUAGE plpgsql AS $$\n"
               "BEGIN\n"
               "    -- statements; a procedure may COMMIT\n"
               "END;\n"
               "$$;\n";
    }
    return "CREATE OR REPLACE FUNCTION " + QualifiedName(schema, "function_name") + "(arg integer)\n"
           "RETURNS integer LANGUAGE sql STABLE AS $$\n"
           "    SELECT arg * 2\n"
           "$$;\n";
}

std::string EditableDefinition(SqlEngine engine, const RoutineInfo& routine) {
    if (routine.definition.empty()) return std::string();
    if (engine == SqlEngine::Sqlite) {
        return "-- SQLite replaces no trigger: it is dropped and created again, in one transaction.\n"
               "BEGIN;\n"
               "DROP TRIGGER " + QualifiedName(routine.schema, routine.name) + ";\n" +
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
