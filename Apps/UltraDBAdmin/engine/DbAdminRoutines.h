// Apps/UltraDBAdmin/engine/DbAdminRoutines.h
// The code a schema holds (Docs/Research/UltraDBAdminDesignProposal.md, 5.2
// Triggers / Routines, stage 4): its triggers on both engines, and on
// PostgreSQL its functions, procedures and aggregates - each with the
// statement that makes it, as the database reports it (sqlite_master's sql,
// pg_get_functiondef, pg_get_triggerdef), so editing one is changing that
// statement and running it again (CREATE OR REPLACE; a SQLite trigger is
// dropped and created). Functions an extension brought are left out: they
// are the extension's.
// Version: 0.2.0 - MySQL / MariaDB; WithoutDefiner shared with the dump
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminOperations.h"      // OperationPlan
#include "DbAdminStructureEdit.h"   // SqlEngine

#include <UltraDatabase/UltraDatabase.h>

#include <string>
#include <vector>

namespace UltraDBAdmin {

struct RoutineInfo {
    enum class Kind { Trigger, Function, Procedure, Aggregate, Window };
    Kind kind = Kind::Trigger;
    std::string schema;
    std::string name;
    std::string arguments;      // a function's identity arguments: "integer, text"
    std::string table;          // a trigger's table
    std::string returns;        // a function's result type
    std::string language;       // plpgsql, sql, c ...
    std::string definition;     // the CREATE statement; "" where none is reported (an aggregate)
};

const char* RoutineKindText(RoutineInfo::Kind kind);   // "trigger", "function", ...

// The routines of `schema`: triggers by table and name, then functions,
// procedures and aggregates by name. Call it on a worker.
UltraDbResult ListRoutines(const std::string& connection, SqlEngine engine, const std::string& schema,
                           std::vector<RoutineInfo>& out);

// DROP TRIGGER / FUNCTION / PROCEDURE / AGGREGATE, the function named with
// its arguments, so an overload is told from another. Never CASCADE.
OperationPlan DropRoutinePlan(SqlEngine engine, const RoutineInfo& routine);

// What to start a new one from, in the SQL tab: a trigger on `table` (any
// table when ""), or a function.
std::string RoutineTemplate(SqlEngine engine, RoutineInfo::Kind kind, const std::string& schema,
                            const std::string& table);

// The definition to edit and run again: PostgreSQL's CREATE OR REPLACE as it
// is (a trigger's CREATE becomes CREATE OR REPLACE); a SQLite trigger behind
// its DROP TRIGGER, in one transaction, as SQLite replaces no trigger; a
// MySQL one behind its DROP ... IF EXISTS, between DELIMITER lines.
std::string EditableDefinition(SqlEngine engine, const RoutineInfo& routine);

// A MySQL definition (SHOW CREATE ...) without its DEFINER clause: run again
// by another user, "DEFINER=`root`@`localhost`" needs a privilege that user
// may not have, and without it the object belongs to whoever runs it.
std::string WithoutDefiner(const std::string& sql);

} // namespace UltraDBAdmin
