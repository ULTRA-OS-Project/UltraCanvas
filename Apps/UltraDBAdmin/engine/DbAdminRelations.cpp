// Apps/UltraDBAdmin/engine/DbAdminRelations.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRelations.h"

#include <algorithm>
#include <map>
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

std::vector<std::vector<std::string>> RelationLayers(const std::vector<UltraDbTableSchema>& tables,
                                                     const std::vector<RelationLink>& links) {
    std::map<std::string, size_t> level;
    std::set<std::string> linked;
    for (const auto& table : tables) level[table.name] = 0;
    for (const auto& link : links) {
        if (link.selfReference || !level.count(link.child) || !level.count(link.parent)) continue;
        linked.insert(link.child);
        linked.insert(link.parent);
    }
    // Longest path from a table with no parent; a cycle stops rising after
    // as many rounds as there are tables.
    for (size_t round = 0; round < tables.size(); ++round) {
        bool changed = false;
        for (const auto& link : links) {
            if (link.selfReference || !level.count(link.child) || !level.count(link.parent)) continue;
            if (level[link.child] < level[link.parent] + 1) {
                level[link.child] = level[link.parent] + 1;
                changed = true;
            }
        }
        if (!changed) break;
    }
    std::map<size_t, std::vector<std::string>> byLevel;
    std::vector<std::string> alone;
    for (const auto& table : tables) {
        if (linked.count(table.name)) byLevel[level[table.name]].push_back(table.name);
        else alone.push_back(table.name);
    }
    std::vector<std::vector<std::string>> layers;
    for (auto& entry : byLevel) layers.push_back(std::move(entry.second));   // no empty layer between
    // Under its parents: a table's place is the mean of its parents' places
    // in the layers above.
    std::map<std::string, double> place;
    for (size_t i = 0; i < layers.size(); ++i) {
        auto& layer = layers[i];
        if (i > 0) {
            std::map<std::string, double> key;
            for (size_t j = 0; j < layer.size(); ++j) {
                double sum = 0.0;
                size_t parents = 0;
                for (const auto& link : links)
                    if (link.child == layer[j] && !link.selfReference && place.count(link.parent)) {
                        sum += place[link.parent];
                        ++parents;
                    }
                key[layer[j]] = parents > 0 ? sum / static_cast<double>(parents) : 1.0 + static_cast<double>(j);
            }
            std::stable_sort(layer.begin(), layer.end(),
                             [&key](const std::string& a, const std::string& b) { return key[a] < key[b]; });
        }
        for (size_t j = 0; j < layer.size(); ++j)
            place[layer[j]] = (static_cast<double>(j) + 0.5) / static_cast<double>(layer.size());
    }
    if (!alone.empty()) layers.push_back(std::move(alone));
    return layers;
}

} // namespace UltraDBAdmin
