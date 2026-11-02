// Apps/UltraDBAdmin/engine/DbAdminSnippets.h
// Saved SQL snippets: a query kept under a name, to be put back in the
// console's editor - the "saved snippets" of the SQL view
// (Docs/Research/UltraDBAdminDesignProposal.md, 5.2), beside the history of
// what was run. Kept in snippets.json in the data folder, one list for every
// connection: a snippet is SQL text, not tied to a database. The app's own
// file, read and written directly.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraDBAdmin {

struct Snippet {
    std::string name;
    std::string sql;
};

class SnippetStore {
public:
    // Reads `directory`/snippets.json; a missing file is an empty list. An
    // empty directory keeps the list in memory.
    bool Open(const std::string& directory, std::string& error);

    // By name, in no case: the order the menu lists them in.
    const std::vector<Snippet>& All() const { return snippets_; }
    const Snippet* Find(const std::string& name) const;

    // Adds the snippet, or replaces the one of the same name (in any case),
    // and saves the list. False, with `error`, for an empty name or SQL.
    bool Put(const Snippet& snippet, std::string& error);
    bool Remove(const std::string& name, std::string& error);

private:
    bool Save(std::string& error) const;

    std::string file_;              // empty: in memory
    std::vector<Snippet> snippets_;
};

} // namespace UltraDBAdmin
