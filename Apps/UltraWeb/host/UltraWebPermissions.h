// Apps/UltraWeb/host/UltraWebPermissions.h
// What the user answered when an app asked for a device - the camera or the
// microphone (uc_rtc_camera, uc_rtc_microphone) - kept per origin (an app
// from a file: per file; UltraWebStorage::PartitionFor) so that the bar asks
// once. One line per answer in a text file, written through a temporary
// file and a rename; Ctrl+Shift+Delete forgets them with the cookies.
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace UltraWeb {

class PermissionStore {
public:
    enum class Answer { NotAsked, Allowed, Blocked };

    // Kept in the UTF-8 file `path` (read now; a missing or unreadable file
    // is an empty store). Empty: in memory only.
    explicit PermissionStore(std::string path = std::string());

    Answer Get(const std::string& partition, const std::string& device) const;
    // Records the answer and writes the file.
    void Set(const std::string& partition, const std::string& device, bool allowed);
    size_t Count() const { return answers_.size(); }
    // Forgets every answer (and removes the file).
    void Clear();

    // UltraWeb's store, <settings folder>/UltraWeb/permissions.txt.
    static std::shared_ptr<PermissionStore> Shared();

private:
    bool Write() const;

    std::string path_;
    std::map<std::pair<std::string, std::string>, bool> answers_;   // (partition, device) -> allowed
};

} // namespace UltraWeb
