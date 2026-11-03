// Apps/UltraDBAdmin/engine/DbAdminCompare.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminCompare.h"

#include "DbAdminExport.h"   // QuotedName, SqlLiteral

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace UltraDBAdmin {
namespace {

constexpr size_t kPage = 2000;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The column of `columns` named `name`, in any case (MySQL and SQLite do not
// tell case apart in names); -1 when there is none.
int ColumnIndex(const std::vector<std::string>& columns, const std::string& name) {
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i] == name) return static_cast<int>(i);
    for (size_t i = 0; i < columns.size(); ++i)
        if (Lower(columns[i]) == Lower(name)) return static_cast<int>(i);
    return -1;
}

// A decimal number without the fraction's zeros that say nothing: 10.50 ->
// 10.5, 3.0 -> 3, -0.0 -> 0. Anything else as it is.
std::string NumberText(const std::string& text) {
    size_t i = text[0] == '-' || text[0] == '+' ? 1 : 0;
    const size_t digits = i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    if (i == digits) return text;
    size_t point = std::string::npos;
    if (i < text.size() && text[i] == '.') {
        point = i++;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    }
    if (i != text.size()) return text;
    std::string out = text[0] == '+' ? text.substr(1) : text;
    if (point != std::string::npos) {
        while (out.back() == '0') out.pop_back();
        if (out.back() == '.') out.pop_back();
    }
    // Leading zeros stay: a code "007" is not the number 7.
    if (out == "-0") out = "0";
    return out;
}

std::string KeyText(const UltraDbRow& row, const std::vector<int>& key) {
    std::string text;
    for (int k : key) {
        const std::string part = CompareText(row[static_cast<size_t>(k)]);
        text += std::to_string(part.size()) + ":" + part;   // length-prefixed: no two keys run together
    }
    return text;
}

struct Table {
    std::vector<std::string> columns;
    std::vector<std::string> key;
};

UltraDbResult Describe(const CompareSide& side, Table& out) {
    UltraDbTableSchema schema;
    const UltraDbResult result = UltraDb_DescribeTable(side.connection, side.schema, side.table, schema);
    if (!result) return result;
    out.columns.clear();
    for (const auto& column : schema.columns) out.columns.push_back(column.name);
    out.key = schema.primaryKey;
    if (out.columns.empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound, side.schema + "." + side.table + " has no columns");
    return UltraDbResult::Ok();
}

std::string SelectSql(const CompareSide& side, const std::vector<std::string>& columns) {
    std::string sql = "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) sql += (i ? ", " : "") + QuotedName(side.engine, columns[i]);
    return sql + " FROM " + QualifiedName(side.engine, side.schema, side.table);
}

// A value written for the target table. The two sides need not share their
// types - SQLite keeps a boolean as 1 - so PostgreSQL, which takes no 1 for
// a boolean, gets numbers and truth values as quoted text: an untyped
// literal it reads as the column's own type.
std::string TargetLiteral(const UltraDbValue& value, SqlEngine engine) {
    if (engine != SqlEngine::Postgres) return SqlLiteral(value, engine);
    switch (value.Type()) {
        case UltraDbType::Boolean: return value.AsBool() ? "'true'" : "'false'";
        case UltraDbType::Int:
        case UltraDbType::Double: return SqlLiteral(UltraDbValue(SqlLiteral(value, engine)), engine);
        default: return SqlLiteral(value, engine);
    }
}

bool Stopped(const CompareLimits& limits) {
    return limits.stop && limits.stop->load();
}

} // namespace

std::string CompareText(const UltraDbValue& value) {
    switch (value.Type()) {
        case UltraDbType::Null:    return std::string(1, '\x01');
        case UltraDbType::Boolean: return value.AsBool() ? "1" : "0";
        case UltraDbType::Blob: {
            const std::vector<uint8_t> bytes = value.AsBlob();
            return std::string(bytes.begin(), bytes.end());
        }
        case UltraDbType::Int:
        case UltraDbType::Double:
        case UltraDbType::Text: {
            const std::string text = value.AsString();
            return text.empty() ? text : NumberText(text);
        }
    }
    return value.AsString();
}

UltraDbResult CompareTables(const CompareSide& left, const CompareSide& right, const CompareLimits& limits,
                            CompareResult& out) {
    out = CompareResult();
    Table l, r;
    UltraDbResult result = Describe(left, l);
    if (!result) return result;
    result = Describe(right, r);
    if (!result) return result;
    if (l.key.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    left.table + " has no primary key: rows cannot be matched");
    // The key first, then every other column both tables have.
    for (const std::string& k : l.key) {
        if (ColumnIndex(r.columns, k) < 0)
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        right.table + " has no column " + k + " of " + left.table + "'s key");
        out.key.push_back(k);
        out.columns.push_back(k);
    }
    std::vector<std::string> rightNames;   // the right table's spelling of each compared column
    for (const std::string& k : l.key) rightNames.push_back(r.columns[static_cast<size_t>(ColumnIndex(r.columns, k))]);
    for (const std::string& c : l.columns) {
        if (ColumnIndex(out.key, c) >= 0) continue;
        const int at = ColumnIndex(r.columns, c);
        if (at < 0) {
            out.onlyLeftColumns.push_back(c);
            continue;
        }
        out.columns.push_back(c);
        rightNames.push_back(r.columns[static_cast<size_t>(at)]);
    }
    for (const std::string& c : r.columns)
        if (ColumnIndex(out.columns, c) < 0) out.onlyRightColumns.push_back(c);
    std::vector<int> key;
    for (size_t i = 0; i < out.key.size(); ++i) key.push_back(static_cast<int>(i));
    const size_t width = out.columns.size();

    // The right table, held by key.
    struct Held {
        std::vector<UltraDbValue> values;
        bool seen = false;
    };
    std::unordered_map<std::string, Held> held;
    std::vector<std::string> order;        // the right's keys as read, for the rows left lacks
    UltraDbResult error;
    UltraDbHandle cursor = UltraDb_OpenCursor(right.connection, SelectSql(right, rightNames), {}, &error);
    if (cursor == UltraDbInvalidHandle) return error;
    bool exhausted = false;
    while (!exhausted) {
        if (Stopped(limits)) {
            UltraDb_CloseCursor(cursor);
            return UltraDbResult::Error(UltraDbResultCode::Cancelled, "stopped");
        }
        UltraDbResultSet page;
        result = UltraDb_FetchRows(cursor, kPage, page, exhausted);
        if (!result) {
            UltraDb_CloseCursor(cursor);
            return result;
        }
        for (const auto& row : page) {
            if (++out.rightRows > limits.maxRows) {
                UltraDb_CloseCursor(cursor);
                return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                            right.table + " holds more than " + std::to_string(limits.maxRows) +
                                                " rows: too many to compare here");
            }
            Held entry;
            for (size_t i = 0; i < width; ++i) entry.values.push_back(row[i]);
            const std::string k = KeyText(row, key);
            order.push_back(k);
            held.emplace(k, std::move(entry));
        }
    }
    UltraDb_CloseCursor(cursor);

    auto keep = [&out, &limits](RowDifference&& difference) {
        if (out.differences.size() < limits.maxDifferences) out.differences.push_back(std::move(difference));
        else out.truncated = true;
    };
    // The left table streams past them.
    cursor = UltraDb_OpenCursor(left.connection, SelectSql(left, out.columns), {}, &error);
    if (cursor == UltraDbInvalidHandle) return error;
    exhausted = false;
    while (!exhausted) {
        if (Stopped(limits)) {
            UltraDb_CloseCursor(cursor);
            return UltraDbResult::Error(UltraDbResultCode::Cancelled, "stopped");
        }
        UltraDbResultSet page;
        result = UltraDb_FetchRows(cursor, kPage, page, exhausted);
        if (!result) {
            UltraDb_CloseCursor(cursor);
            return result;
        }
        for (const auto& row : page) {
            ++out.leftRows;
            std::vector<UltraDbValue> values;
            for (size_t i = 0; i < width; ++i) values.push_back(row[i]);
            const auto found = held.find(KeyText(row, key));
            if (found == held.end() || found->second.seen) {
                ++out.onlyLeft;
                keep(RowDifference{ RowDifference::Kind::OnlyLeft, std::move(values), {}, {} });
                continue;
            }
            found->second.seen = true;
            std::vector<size_t> changed;
            for (size_t i = out.key.size(); i < width; ++i)
                if (CompareText(values[i]) != CompareText(found->second.values[i])) changed.push_back(i);
            if (changed.empty()) {
                ++out.same;
            } else {
                ++out.changed;
                keep(RowDifference{ RowDifference::Kind::Changed, std::move(values), found->second.values,
                                    std::move(changed) });
            }
        }
    }
    UltraDb_CloseCursor(cursor);
    for (const std::string& k : order) {
        auto found = held.find(k);
        if (found == held.end() || found->second.seen) continue;
        found->second.seen = true;
        ++out.onlyRight;
        keep(RowDifference{ RowDifference::Kind::OnlyRight, {}, found->second.values, {} });
    }
    return UltraDbResult::Ok();
}

UltraDbResultSet DifferenceRows(const CompareResult& result, size_t max) {
    UltraDbResultSet rows;
    std::vector<std::string> columns = { "difference" };
    columns.insert(columns.end(), result.columns.begin(), result.columns.end());
    rows.SetColumns(columns);
    auto text = [](const UltraDbValue& v) { return v.IsNull() ? std::string("NULL") : v.AsString(); };
    for (const RowDifference& d : result.differences) {
        if (rows.Size() >= max) break;
        std::vector<UltraDbValue> values;
        if (d.kind == RowDifference::Kind::Changed) {
            values.push_back(UltraDbValue("changed"));
            for (size_t i = 0; i < d.left.size(); ++i) {
                const bool differs = std::find(d.changed.begin(), d.changed.end(), i) != d.changed.end();
                values.push_back(differs ? UltraDbValue(text(d.left[i]) + " -> " + text(d.right[i])) : d.left[i]);
            }
        } else {
            const bool here = d.kind == RowDifference::Kind::OnlyLeft;
            values.push_back(UltraDbValue(here ? "only here" : "only there"));
            for (const UltraDbValue& v : here ? d.left : d.right) values.push_back(v);
        }
        rows.AddRow(std::move(values));
    }
    return rows;
}

std::string CompareSummary(const CompareResult& r, const std::string& here, const std::string& there) {
    auto rows = [](size_t n) { return std::to_string(n) + (n == 1 ? " row" : " rows"); };
    std::string text = rows(r.leftRows) + " " + here + ", " + rows(r.rightRows) + " " + there + ": ";
    if (r.Differences() == 0) {
        text += "the same.";
    } else {
        text += std::to_string(r.same) + " the same, " + std::to_string(r.changed) + " changed, " +
                std::to_string(r.onlyLeft) + " only " + here + ", " + std::to_string(r.onlyRight) + " only " + there +
                ".";
    }
    auto list = [](const std::vector<std::string>& names) {
        std::string out;
        for (const std::string& n : names) out += (out.empty() ? "" : ", ") + n;
        return out;
    };
    if (!r.onlyLeftColumns.empty()) text += " Not compared, only " + here + ": " + list(r.onlyLeftColumns) + ".";
    if (!r.onlyRightColumns.empty()) text += " Not compared, only " + there + ": " + list(r.onlyRightColumns) + ".";
    if (r.truncated) text += " The first " + std::to_string(r.differences.size()) + " differences are kept.";
    return text;
}

bool SyncPlan(const CompareSide& right, const CompareResult& result, OperationPlan& out, std::string& error) {
    out = OperationPlan();
    if (result.truncated) {
        error = std::to_string(result.Differences()) + " differences are more than were kept: compare a part at a time";
        return false;
    }
    const std::string table = QualifiedName(right.engine, right.schema, right.table);
    const size_t keyWidth = result.key.size();
    auto where = [&](const std::vector<UltraDbValue>& values) {
        std::string sql = " WHERE ";
        for (size_t i = 0; i < keyWidth; ++i) {
            sql += (i ? " AND " : "") + QuotedName(right.engine, result.columns[i]);
            sql += values[i].IsNull() ? " IS NULL" : " = " + TargetLiteral(values[i], right.engine);
        }
        return sql;
    };
    for (const RowDifference& d : result.differences) {
        if (d.kind == RowDifference::Kind::OnlyRight) {
            out.statements.push_back("DELETE FROM " + table + where(d.right));
        } else if (d.kind == RowDifference::Kind::OnlyLeft) {
            std::string names, values;
            for (size_t i = 0; i < result.columns.size(); ++i) {
                names += (i ? ", " : "") + QuotedName(right.engine, result.columns[i]);
                values += (i ? ", " : "") + TargetLiteral(d.left[i], right.engine);
            }
            out.statements.push_back("INSERT INTO " + table + " (" + names + ") VALUES (" + values + ")");
        } else {
            std::string set;
            for (size_t i : d.changed)
                set += (set.empty() ? "" : ", ") + QuotedName(right.engine, result.columns[i]) + " = " +
                       TargetLiteral(d.left[i], right.engine);
            out.statements.push_back("UPDATE " + table + " SET " + set + where(d.left));
        }
    }
    out.notes.push_back(std::to_string(result.onlyLeft) + " inserted, " + std::to_string(result.changed) +
                        " updated, " + std::to_string(result.onlyRight) + " deleted in " + right.table);
    if (!result.onlyRightColumns.empty())
        out.notes.push_back("columns only " + right.table + " has are left as they are");
    return true;
}

} // namespace UltraDBAdmin
