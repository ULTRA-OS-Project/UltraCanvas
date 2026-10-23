// Apps/UltraDBAdmin/engine/DbAdminRelations.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRelations.h"

#include <algorithm>
#include <set>

namespace UltraDBAdmin {
namespace {

std::set<std::string> AsSet(const std::vector<std::string>& names) { return {names.begin(), names.end()}; }

} // namespace

UltraDbResult ReadSchemaTables(const std::string& connection, const std::string& schema, size_t limit,
                               std::vector<UltraDbTableSchema>& out, bool& truncated,
                               std::vector<std::string>& failed) {
    out.clear();
    failed.clear();
    truncated = false;
    std::vector<UltraDbObjectSummary> objects;
    const UltraDbResult result = UltraDb_ListObjects(connection, schema, objects);
    if (!result) return result;
    for (const auto& object : objects) {
        if (object.kind != UltraDbObjectKind::Table) continue;
        if (out.size() >= limit) { truncated = true; break; }
        UltraDbTableSchema table;
        if (!UltraDb_DescribeTable(connection, schema, object.name, table)) {
            failed.push_back(object.name);
            continue;
        }
        out.push_back(std::move(table));
    }
    return UltraDbResult::Ok();
}

bool ColumnsAreUnique(const UltraDbTableSchema& table, const std::vector<std::string>& columns) {
    if (columns.empty()) return false;
    const std::set<std::string> wanted = AsSet(columns);
    if (!table.primaryKey.empty() && AsSet(table.primaryKey) == wanted) return true;
    for (const auto& index : table.indexes)
        if ((index.unique || index.primary) && AsSet(index.columns) == wanted) return true;
    return false;
}

std::vector<RelationLink> RelationLinks(const std::vector<UltraDbTableSchema>& tables, size_t& outside) {
    outside = 0;
    std::set<std::string> names;
    for (const auto& table : tables) names.insert(table.name);
    std::vector<RelationLink> links;
    for (const auto& table : tables) {
        size_t n = 0;
        for (const auto& key : table.foreignKeys) {
            // SQLite reports the parent's schema empty or as the child's.
            const bool sameSchema = key.refSchema.empty() || key.refSchema == table.schema;
            if (!sameSchema || !names.count(key.refTable)) {
                ++outside;
                continue;
            }
            RelationLink link;
            link.id = table.name + "." + std::to_string(++n);
            link.name = key.name.empty() ? table.name + "_" + key.refTable : key.name;
            link.child = table.name;
            link.parent = key.refTable;
            link.columns = key.columns;
            link.refColumns = key.refColumns;
            link.selfReference = table.name == key.refTable;
            for (const auto& column : table.columns)
                if (column.nullable && std::find(key.columns.begin(), key.columns.end(), column.name) != key.columns.end())
                    link.optional = true;
            link.oneToOne = ColumnsAreUnique(table, key.columns);
            links.push_back(std::move(link));
        }
    }
    return links;
}

} // namespace UltraDBAdmin
