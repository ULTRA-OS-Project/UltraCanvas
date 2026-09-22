// core/UltraDatabase/UltraDatabasePostgresSql.cpp
// The `?` -> `$n` placeholder rewriter used by the PostgreSQL driver.
//
// It lives in its own translation unit, compiled whether or not libpq was
// found, for one reason: it is pure string handling with no libpq in it, and
// gating it on the driver would mean a machine without libpq silently builds
// and ships it untested. The mistakes it guards against - a `?` inside a
// string literal, a comment or a dollar-quoted function body - corrupt a
// statement quietly, so the tests for it must always run.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDatabaseInternal.h"

#include <cctype>
#include <string>

namespace ultradb_internal {

// `?` becomes `$1, $2, ...`, but only where a `?` is actually a placeholder.
// A question mark inside a string literal, a quoted identifier, a dollar-quoted
// body or a comment is data, and rewriting it would corrupt the statement in a
// way that is hard to see and easy to ship.
std::string PostgresPlatzhalter(const std::string& sql) {
    std::string aus;
    aus.reserve(sql.size() + 16);
    int naechste = 1;

    for (size_t i = 0; i < sql.size(); ) {
        const char c = sql[i];

        // '...' - a string literal. '' inside it is an escaped quote.
        if (c == '\'') {
            aus.push_back(c);
            ++i;
            while (i < sql.size()) {
                aus.push_back(sql[i]);
                if (sql[i] == '\'') {
                    if (i + 1 < sql.size() && sql[i + 1] == '\'') {
                        aus.push_back(sql[i + 1]);
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        // "..." - a quoted identifier.
        if (c == '"') {
            aus.push_back(c);
            ++i;
            while (i < sql.size()) {
                aus.push_back(sql[i]);
                if (sql[i] == '"') {
                    if (i + 1 < sql.size() && sql[i + 1] == '"') {
                        aus.push_back(sql[i + 1]);
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        // $tag$ ... $tag$ - a dollar-quoted body, which is how function
        // definitions arrive and which may contain anything at all.
        if (c == '$') {
            size_t ende = sql.find('$', i + 1);
            bool istTag = ende != std::string::npos;
            for (size_t k = i + 1; istTag && k < ende; ++k) {
                const char t = sql[k];
                if (!(std::isalnum(static_cast<unsigned char>(t)) || t == '_')) istTag = false;
            }
            if (istTag) {
                const std::string tag = sql.substr(i, ende - i + 1);
                const size_t schluss = sql.find(tag, ende + 1);
                if (schluss != std::string::npos) {
                    aus.append(sql, i, schluss + tag.size() - i);
                    i = schluss + tag.size();
                    continue;
                }
            }
            aus.push_back(c);
            ++i;
            continue;
        }

        // -- to end of line.
        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            const size_t zeilenende = sql.find('\n', i);
            const size_t bis = (zeilenende == std::string::npos) ? sql.size() : zeilenende;
            aus.append(sql, i, bis - i);
            i = bis;
            continue;
        }

        // /* ... */, which nests in PostgreSQL.
        if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            int tiefe = 0;
            size_t k = i;
            while (k + 1 < sql.size()) {
                if (sql[k] == '/' && sql[k + 1] == '*') { ++tiefe; k += 2; continue; }
                if (sql[k] == '*' && sql[k + 1] == '/') {
                    --tiefe;
                    k += 2;
                    if (tiefe == 0) break;
                    continue;
                }
                ++k;
            }
            if (tiefe != 0) k = sql.size();
            aus.append(sql, i, k - i);
            i = k;
            continue;
        }

        if (c == '?') {
            aus.push_back('$');
            aus.append(std::to_string(naechste++));
            ++i;
            continue;
        }

        aus.push_back(c);
        ++i;
    }
    return aus;
}

} // namespace ultradb_internal
