// Apps/UltraWeb/host/UltraWebDownloadList.cpp
// The downloads of this run (UltraWebDownloadList.h).
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebDownloadList.h"
#include "UltraWebDownloads.h"

#include <algorithm>

namespace UltraWeb {

int DownloadList::Add(const std::string& url, const std::string& name, const std::string& path) {
    DownloadEntry entry;
    entry.id = nextId_++;
    entry.url = url;
    entry.name = name;
    entry.path = path;
    entries_.insert(entries_.begin(), entry);
    Changed(entry.id, false);
    return entry.id;
}

int DownloadList::AddSaved(const std::string& url, const std::string& name, const std::string& path, int64_t size) {
    const int id = nextId_++;
    DownloadEntry entry;
    entry.id = id;
    entry.url = url;
    entry.name = name;
    entry.path = path;
    entry.state = DownloadEntry::State::Saved;
    entry.received = entry.total = size;
    entries_.insert(entries_.begin(), entry);
    Changed(id, false);
    return id;
}

DownloadEntry* DownloadList::Get(int id) {
    for (auto& e : entries_) if (e.id == id) return &e;
    return nullptr;
}

const DownloadEntry* DownloadList::Find(int id) const {
    for (const auto& e : entries_) if (e.id == id) return &e;
    return nullptr;
}

void DownloadList::Progress(int id, int64_t received, int64_t total) {
    DownloadEntry* e = Get(id);
    if (!e || e->state != DownloadEntry::State::Running) return;
    e->received = received;
    e->total = total;
    Changed(id, true);
}

void DownloadList::Finish(int id, bool ok, const std::string& error) {
    DownloadEntry* e = Get(id);
    if (!e || e->state != DownloadEntry::State::Running) return;
    if (ok) {
        e->state = DownloadEntry::State::Saved;
        if (e->total > 0) e->received = e->total;
        else e->total = e->received;
    } else {
        e->state = error == "cancelled" ? DownloadEntry::State::Cancelled : DownloadEntry::State::Failed;
        e->error = error;
    }
    Changed(id, false);
}

bool DownloadList::Remove(int id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [id](const DownloadEntry& e) { return e.id == id; });
    if (it == entries_.end() || it->state == DownloadEntry::State::Running) return false;
    entries_.erase(it);
    Changed(id, false);
    return true;
}

void DownloadList::ClearFinished() {
    const size_t before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const DownloadEntry& e) { return e.state != DownloadEntry::State::Running; }),
                   entries_.end());
    if (entries_.size() != before) Changed(-1, false);
}

int DownloadList::RunningCount() const {
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
        [](const DownloadEntry& e) { return e.state == DownloadEntry::State::Running; }));
}

std::string DownloadList::Describe(const DownloadEntry& e) {
    switch (e.state) {
        case DownloadEntry::State::Running:
            if (e.total > 0)
                return ByteSize(e.received) + " of " + ByteSize(e.total) + " (" +
                       std::to_string(e.received * 100 / e.total) + "%)";
            return e.received > 0 ? ByteSize(e.received) + " so far" : std::string("Starting ...");
        case DownloadEntry::State::Saved:
            return "Saved - " + ByteSize(e.total >= 0 ? e.total : e.received);
        case DownloadEntry::State::Failed:
            return "Failed: " + e.error;
        case DownloadEntry::State::Cancelled:
            return "Cancelled";
    }
    return std::string();
}

} // namespace UltraWeb
