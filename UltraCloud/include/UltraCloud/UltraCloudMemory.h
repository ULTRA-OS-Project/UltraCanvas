// UltraCloud/include/UltraCloud/UltraCloudMemory.h
// The in-memory provider (id "memory"): a fake cloud that lives in the
// process. For tests, demos and UI work without a server — nothing is
// contacted. Links look like https://demo.ultra-os.local/s/<n>.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudProvider.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCloud {

class MemoryProvider : public ICloudProvider {
public:
    std::string Id() const override { return "memory"; }
    std::string DisplayName() const override { return "Demo storage (in memory)"; }
    ProviderCapabilities Capabilities() const override;

    Result Verify(const Account& account, const Credentials& credentials) override;
    Result List(const Account& account, const Credentials& credentials,
                const std::string& path, std::vector<Entry>& out) override;
    Result MakeDirectory(const Account& account, const Credentials& credentials,
                         const std::string& path) override;
    Result Upload(const Account& account, const Credentials& credentials,
                  const std::string& localPath, const std::string& remotePath) override;
    Result Download(const Account& account, const Credentials& credentials,
                    const std::string& remotePath, const std::string& localPath) override;
    Result CreateShareLink(const Account& account, const Credentials& credentials,
                           const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out) override;

    // Put a file (or a folder when `size` < 0) into an account's fake storage.
    static void Seed(const std::string& accountId, const std::string& path, int64_t size,
                     const std::string& modified = "");
    static void Clear();
};

} // namespace UltraCloud
