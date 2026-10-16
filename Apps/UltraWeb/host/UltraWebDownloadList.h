// Apps/UltraWeb/host/UltraWebDownloadList.h
// The downloads of this run, for the downloads panel: what each one is, where
// it goes, how far it got and how it ended. No UI here (the panel,
// ui/UltraWebDownloadsPanel, shows it), so it is tested on its own.
// Version: 0.2.0 - speed and time left; Ellipsize for long names
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
    // How fast it is coming, in bytes a second (0 until it can be told),
    // smoothed over the last few seconds; and the sample it was measured from.
    int64_t bytesPerSecond = 0;
    int64_t sampleMs = -1;
    int64_t sampleReceived = 0;
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

    // Its state as the panel says it: "4.2 MB of 20.0 MB (21%) - 1.3 MB/s,
    // 12 s left", "Saved - 20.0 MB", "Failed: the server answered 404",
    // "Cancelled".
    static std::string Describe(const DownloadEntry& entry);
    // "12 s left", "4 min left", "1 h 5 min left": the time `remaining` bytes
    // take at `bytesPerSecond` ("" when the speed is not known).
    static std::string TimeLeft(int64_t remaining, int64_t bytesPerSecond);
    // `name` cut to at most `maxChars` characters (UTF-8 aware) with "…" in
    // the middle, keeping its extension: "a-very-long-repo…-snapshot.tar.gz".
    static std::string Ellipsize(const std::string& name, size_t maxChars);

    // Milliseconds on a steady clock; tests set their own.
    std::function<int64_t()> clock;

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
