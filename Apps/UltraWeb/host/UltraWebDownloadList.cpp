// Apps/UltraWeb/host/UltraWebDownloadList.cpp
// The downloads of this run (UltraWebDownloadList.h).
// Version: 0.2.0 - speed and time left; Ellipsize
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebDownloadList.h"
#include "UltraWebDownloads.h"

#include <algorithm>
#include <chrono>

namespace UltraWeb {

namespace {

// A new speed is measured over at least this long, and weighs a third.
constexpr int64_t kSpeedSampleMs = 1000;

int64_t SteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Where each UTF-8 character starts.
std::vector<size_t> CharStarts(const std::string& s) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) starts.push_back(i);
    return starts;
}

} // namespace

int DownloadList::Add(const std::string& url, const std::string& name, const std::string& path) {
    DownloadEntry entry;
    entry.id = nextId_++;
    entry.url = url;
    entry.name = name;
    entry.path = path;
    entry.sampleMs = clock ? clock() : SteadyMs();
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
    // The speed: what came since the last sample, once a second at most,
    // folded into what it was so one slow second does not swing it.
    const int64_t now = clock ? clock() : SteadyMs();
    if (e->sampleMs >= 0 && now - e->sampleMs >= kSpeedSampleMs && received >= e->sampleReceived) {
        const int64_t measured = (received - e->sampleReceived) * 1000 / (now - e->sampleMs);
        e->bytesPerSecond = e->bytesPerSecond > 0 ? (e->bytesPerSecond * 2 + measured) / 3 : measured;
        e->sampleMs = now;
        e->sampleReceived = received;
    } else if (e->sampleMs < 0) {
        e->sampleMs = now;
        e->sampleReceived = received;
    }
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
        case DownloadEntry::State::Running: {
            std::string text;
            if (e.total > 0)
                text = ByteSize(e.received) + " of " + ByteSize(e.total) + " (" +
                       std::to_string(e.received * 100 / e.total) + "%)";
            else if (e.received > 0)
                text = ByteSize(e.received) + " so far";
            else
                return "Starting ...";
            if (e.bytesPerSecond > 0) {
                text += " - " + ByteSize(e.bytesPerSecond) + "/s";
                if (e.total > e.received) text += ", " + TimeLeft(e.total - e.received, e.bytesPerSecond);
            }
            return text;
        }
        case DownloadEntry::State::Saved:
            return "Saved - " + ByteSize(e.total >= 0 ? e.total : e.received);
        case DownloadEntry::State::Failed:
            return "Failed: " + e.error;
        case DownloadEntry::State::Cancelled:
            return "Cancelled";
    }
    return std::string();
}

std::string DownloadList::TimeLeft(int64_t remaining, int64_t bytesPerSecond) {
    if (bytesPerSecond <= 0 || remaining < 0) return std::string();
    const int64_t seconds = (remaining + bytesPerSecond - 1) / bytesPerSecond;   // rounded up
    if (seconds < 60) return std::to_string(std::max<int64_t>(seconds, 1)) + " s left";
    const int64_t minutes = (seconds + 59) / 60;
    if (minutes < 60) return std::to_string(minutes) + " min left";
    const std::string hours = std::to_string(minutes / 60) + " h";
    return minutes % 60 ? hours + " " + std::to_string(minutes % 60) + " min left" : hours + " left";
}

std::string DownloadList::Ellipsize(const std::string& name, size_t maxChars) {
    const std::vector<size_t> starts = CharStarts(name);
    if (starts.size() <= maxChars || maxChars < 5) return name;
    // Keep the extension ("tar.gz" counts as one when short) and as much of
    // the end before it as of the start: the end of a name tells versions
    // apart, its start says what it is.
    const std::string ellipsis = "\xE2\x80\xA6";   // …
    size_t extChars = 0;
    const size_t dot = name.find('.', name.size() > 12 ? name.size() - 12 : 0);
    if (dot != std::string::npos && dot > 0)
        extChars = static_cast<size_t>(std::count_if(starts.begin(), starts.end(), [dot](size_t s) { return s >= dot; }));
    const size_t room = maxChars - 1;                    // one for the "…"
    if (extChars > room / 2) extChars = 0;               // an extension that long is just more name
    const size_t tail = extChars + (room - extChars) / 3;
    const size_t head = room - tail;
    const size_t headEnd = starts[head];
    const size_t tailStart = starts[starts.size() - tail];
    return name.substr(0, headEnd) + ellipsis + name.substr(tailStart);
}

} // namespace UltraWeb
