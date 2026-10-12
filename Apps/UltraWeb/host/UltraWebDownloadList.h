// Apps/UltraWeb/host/UltraWebDownloadList.h
// The downloads of this run, for the downloads panel: what each one is, where
// it goes, how far it got and how it ended. No UI here (the panel,
// ui/UltraWebDownloadsPanel, shows it), so it is tested on its own.
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraWeb {

struct DownloadEntry {
    enum class State { Running, Saved, Failed, Cancelled };
    int id = 0;
    std::string url;
    std::string name;        // the file's name, as saved
    std::string path;        // where it is saved (UTF-8)
    State state = State::Running;
    int64_t received = 0;
    int64_t total = -1;      // -1: the server did not say
    std::string error;       // why it failed
};

class DownloadList {
public:
    // A download starting; its id. Newest first in Entries().
    int Add(const std::string& url, const std::string& name, const std::string& path);
    // A download that was saved at once (the bytes were already there).
    int AddSaved(const std::string& url, const std::string& name, const std::string& path, int64_t size);
    void Progress(int id, int64_t received, int64_t total);
    // How it ended: saved, or failed with why ("cancelled" is Cancelled).
    void Finish(int id, bool ok, const std::string& error);
    // Drops a finished download from the list (not the file); false for a
    // running one or an unknown id.
    bool Remove(int id);
    // Drops every finished download.
    void ClearFinished();

    const std::vector<DownloadEntry>& Entries() const { return entries_; }
    const DownloadEntry* Find(int id) const;
    int RunningCount() const;

    // Its state as the panel says it: "4.2 MB of 20.0 MB (21%)", "Saved -
    // 20.0 MB", "Failed: the server answered 404", "Cancelled".
    static std::string Describe(const DownloadEntry& entry);

    // The list changed: an entry was added, finished or removed (`id` -1 for
    // several) - or, for `progressOnly`, only its numbers moved.
    std::function<void(int id, bool progressOnly)> onChanged;

private:
    DownloadEntry* Get(int id);
    void Changed(int id, bool progressOnly) { if (onChanged) onChanged(id, progressOnly); }
    std::vector<DownloadEntry> entries_;
    int nextId_ = 1;
};

} // namespace UltraWeb
