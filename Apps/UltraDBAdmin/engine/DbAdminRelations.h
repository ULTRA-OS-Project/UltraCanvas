// Apps/UltraDBAdmin/engine/DbAdminRelations.h
// What the Relations tab draws (Docs/Research/UltraDBAdminDesignProposal.md,
// 5.2 Relations, stage 4): the tables of a schema and the foreign keys
// between them, read through the schema introspection (UltraDb_ListObjects,
// UltraDb_DescribeTable) and turned into links an ER diagram draws as
// crow's-foot lines.
//
// A link runs from the table that holds the foreign key (the child, "many")
// to the table it refers to (the parent, "one"). The parent's end is
// optional when one of the key's columns may be NULL; the child's end is
// "one" too when the key's columns are the child's whole primary key or a
// unique index - a one-to-one. A key to a table outside the schema is
// counted, not drawn.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <string>
#include <vector>

namespace UltraDBAdmin {

struct RelationLink {
    std::string id;                      // unique among the links: child.n
    std::string name;                    // the constraint's name, or "child_parent" where SQLite gives none
    std::string child;                   // holds the key
    std::string parent;                  // is referred to
    std::vector<std::string> columns;    // in the child
    std::vector<std::string> refColumns; // in the parent
    bool optional = false;               // a key column may be NULL: the child need not have a parent
    bool oneToOne = false;               // the key's columns are unique in the child
    bool selfReference = false;          // child and parent are one table
};

// The tables of `schema`, described, by name - views, sequences and foreign
// tables left out. At most `limit`; `truncated` says whether there were
// more. A table that cannot be described is named in `failed` and left out.
// Call it on a worker.
UltraDbResult ReadSchemaTables(const std::string& connection, const std::string& schema, size_t limit,
                               std::vector<UltraDbTableSchema>& out, bool& truncated,
                               std::vector<std::string>& failed);

// The foreign keys between `tables`, in table order. `outside` counts the
// keys to a table not among them (another schema, or one not read).
std::vector<RelationLink> RelationLinks(const std::vector<UltraDbTableSchema>& tables, size_t& outside);

// Whether `columns` are the table's whole primary key or a unique index, in
// any order.
bool ColumnsAreUnique(const UltraDbTableSchema& table, const std::vector<std::string>& columns);

} // namespace UltraDBAdmin
