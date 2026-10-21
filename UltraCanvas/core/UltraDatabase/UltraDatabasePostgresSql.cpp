// core/UltraDatabase/UltraDatabasePostgresSql.cpp
// The PostgreSQL driver's SQL lexing: the `?` -> `$n` placeholder rewriter,
// and the statement splitter UltraDb_ExecBatch cuts a script with.
//
// It lives in its own translation unit, compiled whether or not libpq was
// found, for one reason: it is pure string handling with no libpq in it, and
// gating it on the driver would mean a machine without libpq silently builds
// and ships it untested. The mistakes it guards against - a `?` inside a
// string literal, a comment or a dollar-quoted function body - corrupt a
// statement quietly, so the tests for it must always run.
//
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDatabaseInternal.h"

#include <cctype>
#include <string>
#include <vector>

namespace ultradb_internal {

namespace {

bool IstWortzeichen(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

// What PostgreSQL treats as data or as a comment, starting at `i`: a string
// literal ('...', with '' for a quote), an escape string (E'...', where a
// backslash escapes too), a quoted identifier ("..."), a dollar-quoted body
// ($tag$...$tag$), a -- comment (to the end of the line, the newline left out)
// or a /* */ comment, which nests. Returns the index just past it, or `i`
// itself when none starts there. An unterminated one runs to the end. The
// placeholder rewriter and the statement splitter both step over these, so
// neither sees a ? or a ; inside text.
size_t PostgresUeberspringen(const std::string& sql, size_t i) {
    const size_t n = sql.size();
    if (i >= n) return i;
    const char c = sql[i];

    // E'...' - an escape string: \' is a quote inside it, as is ''. Only an E
    // that starts a word; "type'..." is an identifier followed by a literal.
    if ((c == 'E' || c == 'e') && i + 1 < n && sql[i + 1] == '\'' &&
        (i == 0 || !IstWortzeichen(sql[i - 1]))) {
        size_t k = i + 2;
        while (k < n) {
            if (sql[k] == '\\') { k += 2; continue; }
            if (sql[k] == '\'') {
                if (k + 1 < n && sql[k + 1] == '\'') { k += 2; continue; }
                return k + 1;
            }
            ++k;
        }
        return n;
    }

    // '...' and "...": a doubled quote character stays inside.
    if (c == '\'' || c == '"') {
        size_t k = i + 1;
        while (k < n) {
            if (sql[k] == c) {
                if (k + 1 < n && sql[k + 1] == c) { k += 2; continue; }
                return k + 1;
            }
            ++k;
        }
        return n;
    }

    // $tag$ ... $tag$ - a dollar-quoted body, which is how function
    // definitions arrive and which may contain anything at all. A $ that opens
    // no closed body (a $1 parameter, say) is not one.
    if (c == '$') {
        const size_t ende = sql.find('$', i + 1);
        if (ende == std::string::npos) return i;
        for (size_t k = i + 1; k < ende; ++k)
            if (!IstWortzeichen(sql[k])) return i;
        const std::string tag = sql.substr(i, ende - i + 1);
        const size_t schluss = sql.find(tag, ende + 1);
        return schluss == std::string::npos ? i : schluss + tag.size();
    }

    // -- to end of line.
    if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
        const size_t zeilenende = sql.find('\n', i);
        return zeilenende == std::string::npos ? n : zeilenende;
    }

    // /* ... */, which nests in PostgreSQL.
    if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
        int tiefe = 0;
        size_t k = i;
        while (k + 1 < n) {
            if (sql[k] == '/' && sql[k + 1] == '*') { ++tiefe; k += 2; continue; }
            if (sql[k] == '*' && sql[k + 1] == '/') {
                --tiefe;
                k += 2;
                if (tiefe == 0) return k;
                continue;
            }
            ++k;
        }
        return n;
    }
    return i;
}

// `?` becomes `$1, $2, ...`, but only where a `?` is actually a placeholder.
// A question mark inside a string literal, a quoted identifier, a dollar-quoted
// body or a comment is data, and rewriting it would corrupt the statement in a
// way that is hard to see and easy to ship.
std::string PostgresPlatzhalter(const std::string& sql) {
    std::string aus;
    aus.reserve(sql.size() + 16);
    int naechste = 1;

    for (size_t i = 0; i < sql.size(); ) {
        const size_t weiter = PostgresUeberspringen(sql, i);
        if (weiter > i) {
            aus.append(sql, i, weiter - i);
            i = weiter;
            continue;
        }
        if (sql[i] == '?') {
            aus.push_back('$');
            aus.append(std::to_string(naechste++));
        } else {
            aus.push_back(sql[i]);
        }
        ++i;
    }
    return aus;
}

// A script cut into its statements at the semicolons that end one - not those
// inside text or comments (see PostgresUeberspringen), and not those inside a
// SQL-standard function body: CREATE FUNCTION ... BEGIN ATOMIC ...; ...; END
// holds statements of its own. Like psql, inside a CREATE statement BEGIN and
// CASE open a level and END closes one, and a semicolon ends the statement
// only at level zero. Each statement comes back trimmed, without its
// semicolon; one that is only whitespace and comments is dropped.
std::vector<std::string> PostgresAnweisungen(const std::string& sql) {
    std::vector<std::string> anweisungen;
    size_t anfang = 0;
    bool inhalt = false;          // anything but whitespace and comments yet
    bool erstesWort = true;       // the statement's first word not yet seen
    bool create = false;          // the statement is a CREATE ...
    int tiefe = 0;                // BEGIN / CASE ... END inside a CREATE

    auto abschliessen = [&](size_t bis) {
        if (inhalt) {
            size_t a = anfang, b = bis;
            while (a < b && std::isspace(static_cast<unsigned char>(sql[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(sql[b - 1]))) --b;
            anweisungen.push_back(sql.substr(a, b - a));
        }
        inhalt = false;
        erstesWort = true;
        create = false;
        tiefe = 0;
    };

    for (size_t i = 0; i < sql.size(); ) {
        const size_t weiter = PostgresUeberspringen(sql, i);
        if (weiter > i) {
            // A comment is not content; a literal or identifier is.
            const char c = sql[i];
            if (!((c == '-' && sql[i + 1] == '-') || (c == '/' && sql[i + 1] == '*')))
                inhalt = true;
            erstesWort = erstesWort && (c == '-' || c == '/');
            i = weiter;
            continue;
        }
        const char c = sql[i];
        if (c == ';' && tiefe == 0) {
            abschliessen(i);
            anfang = ++i;
            continue;
        }
        if (IstWortzeichen(c) && (i == 0 || !IstWortzeichen(sql[i - 1]))) {
            size_t k = i;
            std::string wort;
            while (k < sql.size() && IstWortzeichen(sql[k]))
                wort.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[k++]))));
            if (erstesWort) create = wort == "CREATE";
            erstesWort = false;
            if (create) {
                if (wort == "BEGIN" || wort == "CASE") ++tiefe;
                else if (wort == "END" && tiefe > 0) --tiefe;
            }
            inhalt = true;
            i = k;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) {
            inhalt = true;
            erstesWort = false;
        }
        ++i;
    }
    abschliessen(sql.size());
    return anweisungen;
}

} // namespace ultradb_internal
