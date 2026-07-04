// VirtualFS/core/VirtualFSManager.cpp
// Core manager implementation for VirtualFS
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework

#include "VirtualFSManager.h"
#include "VirtualFSPath.h"

#ifdef VIRTUALFS_HAS_LIBARCHIVE
#include "VirtualFSLibArchiveProvider.h"
#endif

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace VirtualFS {

// ============================================================================
// INITIALIZATION
// ============================================================================

VirtualFSResult VirtualFSManager::Initialize() {
    {
        std::lock_guard<std::mutex> lock(providerMutex);

        if (initialized) {
            return VirtualFSResult::Success;
        }

        // Set default temp directory
        tempDirectory = std::filesystem::temp_directory_path().string();

        initialized = true;
    }

    // Register built-in providers outside the lock — RegisterProvider()
    // takes providerMutex itself
    RegisterBuiltInProviders();

    return VirtualFSResult::Success;
}

void VirtualFSManager::Shutdown() {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    if (!initialized) {
        return;
    }
    
    // Close all open archives
    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        for (auto& pair : openArchives) {
            if (pair.second.provider) {
                pair.second.provider->Close();
            }
        }
        openArchives.clear();
    }
    
    // Clear providers
    providers.clear();
    extensionMap.clear();
    providerNameMap.clear();
    
    initialized = false;
}

void VirtualFSManager::RegisterBuiltInProviders() {
#ifdef VIRTUALFS_HAS_LIBARCHIVE
    RegisterProvider(CreateLibArchiveProvider());
#endif
}

// ============================================================================
// PROVIDER MANAGEMENT
// ============================================================================

VirtualFSResult VirtualFSManager::RegisterProvider(std::shared_ptr<IVirtualFSProvider> provider) {
    if (!provider) {
        return VirtualFSResult::InvalidArgument;
    }
    
    std::lock_guard<std::mutex> lock(providerMutex);
    
    const std::string name = provider->GetName();
    
    // Check for duplicate
    if (providerNameMap.find(name) != providerNameMap.end()) {
        return VirtualFSResult::AlreadyExists;
    }
    
    // Add to providers list
    providers.push_back(provider);
    providerNameMap[name] = provider;
    
    // Map extensions
    for (const auto& ext : provider->GetSupportedExtensions()) {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
        
        // Check priority if extension already mapped
        auto it = extensionMap.find(lowerExt);
        if (it != extensionMap.end()) {
            if (provider->GetPriority() > it->second->GetPriority()) {
                extensionMap[lowerExt] = provider;
            }
        } else {
            extensionMap[lowerExt] = provider;
        }
    }
    
    return VirtualFSResult::Success;
}

void VirtualFSManager::UnregisterProvider(const std::string& providerName) {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    auto it = providerNameMap.find(providerName);
    if (it == providerNameMap.end()) {
        return;
    }
    
    auto provider = it->second;
    
    // Remove from extension map
    for (auto extIt = extensionMap.begin(); extIt != extensionMap.end();) {
        if (extIt->second == provider) {
            extIt = extensionMap.erase(extIt);
        } else {
            ++extIt;
        }
    }
    
    // Remove from providers list
    providers.erase(
        std::remove(providers.begin(), providers.end(), provider),
        providers.end()
    );
    
    // Remove from name map
    providerNameMap.erase(it);
}

std::vector<VirtualFSProviderInfo> VirtualFSManager::GetRegisteredProviders() const {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    std::vector<VirtualFSProviderInfo> result;
    result.reserve(providers.size());
    
    for (const auto& provider : providers) {
        result.push_back(provider->GetInfo());
    }
    
    return result;
}

VirtualFSProviderInfo VirtualFSManager::GetProviderInfo(const std::string& providerName) const {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    auto it = providerNameMap.find(providerName);
    if (it != providerNameMap.end()) {
        return it->second->GetInfo();
    }
    
    return VirtualFSProviderInfo();
}

std::shared_ptr<IVirtualFSProvider> VirtualFSManager::GetProviderForPath(const std::string& path) const {
    std::string ext = VirtualFSPath::GetExtensionLower(path);
    return GetProviderForExtension(ext);
}

std::shared_ptr<IVirtualFSProvider> VirtualFSManager::GetProviderForExtension(const std::string& extension) const {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    std::string lowerExt = extension;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
    
    auto it = extensionMap.find(lowerExt);
    if (it != extensionMap.end()) {
        return it->second;
    }
    
    return nullptr;
}

std::shared_ptr<IVirtualFSProvider> VirtualFSManager::GetProviderByMagic(const uint8_t* data, size_t size) const {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    // Find provider with highest priority that recognizes the format
    std::shared_ptr<IVirtualFSProvider> bestProvider = nullptr;
    int bestPriority = -1;
    
    for (const auto& provider : providers) {
        if (provider->CanHandleByMagic(data, size)) {
            if (provider->GetPriority() > bestPriority) {
                bestProvider = provider;
                bestPriority = provider->GetPriority();
            }
        }
    }
    
    return bestProvider;
}

std::vector<std::string> VirtualFSManager::GetSupportedExtensions() const {
    std::lock_guard<std::mutex> lock(providerMutex);
    
    std::vector<std::string> result;
    result.reserve(extensionMap.size());
    
    for (const auto& pair : extensionMap) {
        result.push_back(pair.first);
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================================
// PATH OPERATIONS
// ============================================================================

VirtualFSResolvedPath VirtualFSManager::ResolvePath(const std::string& virtualPath) const {
    return VirtualFSPath::Resolve(virtualPath);
}

bool VirtualFSManager::IsArchivePath(const std::string& path) const {
    auto resolved = ResolvePath(path);
    return !resolved.archiveStack.empty();
}

bool VirtualFSManager::IsInsideArchive(const std::string& path) const {
    auto resolved = ResolvePath(path);
    return resolved.isInsideArchive;
}

bool VirtualFSManager::IsArchive(const std::string& path) const {
    std::string ext = VirtualFSPath::GetExtensionLower(path);
    return GetProviderForExtension(ext) != nullptr;
}

std::string VirtualFSManager::NormalizePath(const std::string& path) const {
    return VirtualFSPath::Normalize(path);
}

std::string VirtualFSManager::JoinPath(const std::string& base, const std::string& relative) const {
    return VirtualFSPath::Join(base, relative);
}

std::string VirtualFSManager::GetParentPath(const std::string& path) const {
    return VirtualFSPath::GetParent(path);
}

std::string VirtualFSManager::GetFileName(const std::string& path) const {
    return VirtualFSPath::GetFileName(path);
}

std::string VirtualFSManager::GetExtension(const std::string& path) const {
    return VirtualFSPath::GetExtension(path);
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

std::vector<VirtualFSEntry> VirtualFSManager::ListDirectory(const std::string& path) {
    std::string normalized = NormalizePath(path);
    auto resolved = ResolvePath(normalized);
    
    if (resolved.archiveStack.empty()) {
        // Real filesystem
        return ListRealDirectory(normalized);
    }
    
    // Inside archive
    auto provider = GetOrOpenArchive(resolved.realPath);
    if (!provider) {
        return {};
    }
    
    return provider->ListDirectory(resolved.virtualPath);
}

std::vector<VirtualFSEntry> VirtualFSManager::ListDirectoryFiltered(
    const std::string& path,
    VirtualFSFilterCallback filter) {
    
    auto entries = ListDirectory(path);
    
    if (filter) {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [&filter](const VirtualFSEntry& entry) {
                    return !filter(entry);
                }),
            entries.end()
        );
    }
    
    return entries;
}

std::vector<VirtualFSEntry> VirtualFSManager::ListDirectoryRecursive(
    const std::string& path,
    int maxDepth) {
    
    std::vector<VirtualFSEntry> result;
    
    std::function<void(const std::string&, int)> recurse = [&](const std::string& currentPath, int depth) {
        if (maxDepth >= 0 && depth > maxDepth) {
            return;
        }
        
        auto entries = ListDirectory(currentPath);
        for (auto& entry : entries) {
            entry.path = JoinPath(currentPath, entry.name);
            result.push_back(entry);
            
            if (entry.IsDirectory() || entry.isArchive) {
                recurse(entry.path, depth + 1);
            }
        }
    };
    
    recurse(path, 0);
    return result;
}

void VirtualFSManager::EnumerateDirectory(
    const std::string& path,
    VirtualFSEntryCallback callback,
    bool recursive) {
    
    if (!callback) return;
    
    std::function<void(const std::string&)> enumerate = [&](const std::string& currentPath) {
        auto entries = ListDirectory(currentPath);
        for (auto& entry : entries) {
            entry.path = JoinPath(currentPath, entry.name);
            callback(entry);
            
            if (recursive && (entry.IsDirectory() || entry.isArchive)) {
                enumerate(entry.path);
            }
        }
    };
    
    enumerate(path);
}

std::vector<VirtualFSEntry> VirtualFSManager::ListRealDirectory(const std::string& path) {
    std::vector<VirtualFSEntry> entries;
    
    try {
        std::filesystem::path fsPath(path);
        
        if (!std::filesystem::exists(fsPath)) {
            return entries;
        }
        
        if (!std::filesystem::is_directory(fsPath)) {
            // If it's an archive, list its contents
            if (IsArchive(path)) {
                auto provider = GetOrOpenArchive(path);
                if (provider) {
                    return provider->ListDirectory("");
                }
            }
            return entries;
        }
        
        for (const auto& dirEntry : std::filesystem::directory_iterator(fsPath)) {
            VirtualFSEntry entry;
            entry.name = dirEntry.path().filename().string();
            entry.path = dirEntry.path().string();
            entry.realPath = entry.path;
            
            if (dirEntry.is_directory()) {
                entry.type = VirtualFSEntryType::Directory;
            } else if (dirEntry.is_regular_file()) {
                entry.type = VirtualFSEntryType::File;
                entry.size = dirEntry.file_size();
                
                // Check if it's a browsable archive
                if (IsArchive(entry.name)) {
                    entry.isArchive = true;
                }
            } else if (dirEntry.is_symlink()) {
                entry.type = VirtualFSEntryType::Symlink;
                entry.linkTarget = std::filesystem::read_symlink(dirEntry.path()).string();
            }
            
            // Get timestamps
            auto ftime = std::filesystem::last_write_time(dirEntry.path());
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            auto time = std::chrono::system_clock::to_time_t(sctp);
            std::stringstream ss;
            ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
            entry.modifiedTime = ss.str();
            
            entries.push_back(entry);
        }
    } catch (const std::exception& e) {
        NotifyError(VirtualFSResult::ReadError, e.what(), path);
    }
    
    return entries;
}

// ============================================================================
// ENTRY INFORMATION
// ============================================================================

bool VirtualFSManager::Exists(const std::string& path) {
    std::string normalized = NormalizePath(path);
    auto resolved = ResolvePath(normalized);
    
    if (resolved.archiveStack.empty()) {
        return std::filesystem::exists(normalized);
    }
    
    auto provider = GetOrOpenArchive(resolved.realPath);
    if (!provider) {
        return false;
    }
    
    if (resolved.virtualPath.empty()) {
        return true; // Archive itself exists
    }
    
    return provider->Exists(resolved.virtualPath);
}

VirtualFSEntry VirtualFSManager::GetInfo(const std::string& path) {
    std::string normalized = NormalizePath(path);
    auto resolved = ResolvePath(normalized);
    
    if (resolved.archiveStack.empty()) {
        return GetRealFSInfo(normalized);
    }
    
    auto provider = GetOrOpenArchive(resolved.realPath);
    if (!provider) {
        return VirtualFSEntry();
    }
    
    if (resolved.virtualPath.empty()) {
        // Return info about the archive itself
        return GetRealFSInfo(resolved.realPath);
    }
    
    return provider->GetInfo(resolved.virtualPath);
}

VirtualFSEntry VirtualFSManager::GetRealFSInfo(const std::string& path) {
    VirtualFSEntry entry;
    
    try {
        std::filesystem::path fsPath(path);
        
        if (!std::filesystem::exists(fsPath)) {
            return entry;
        }
        
        entry.name = fsPath.filename().string();
        entry.path = path;
        entry.realPath = path;
        
        if (std::filesystem::is_directory(fsPath)) {
            entry.type = VirtualFSEntryType::Directory;
        } else if (std::filesystem::is_regular_file(fsPath)) {
            entry.type = VirtualFSEntryType::File;
            entry.size = std::filesystem::file_size(fsPath);
            
            if (IsArchive(path)) {
                entry.isArchive = true;
            }
        } else if (std::filesystem::is_symlink(fsPath)) {
            entry.type = VirtualFSEntryType::Symlink;
            entry.linkTarget = std::filesystem::read_symlink(fsPath).string();
        }
        
        // Get timestamps
        auto ftime = std::filesystem::last_write_time(fsPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time = std::chrono::system_clock::to_time_t(sctp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
        entry.modifiedTime = ss.str();
        
    } catch (const std::exception& e) {
        NotifyError(VirtualFSResult::ReadError, e.what(), path);
    }
    
    return entry;
}

VirtualFSEntryType VirtualFSManager::GetType(const std::string& path) {
    return GetInfo(path).type;
}

uint64_t VirtualFSManager::GetSize(const std::string& path) {
    return GetInfo(path).size;
}

bool VirtualFSManager::IsDirectory(const std::string& path) {
    return GetType(path) == VirtualFSEntryType::Directory;
}

bool VirtualFSManager::IsFile(const std::string& path) {
    return GetType(path) == VirtualFSEntryType::File;
}

// ============================================================================
// FILE READING
// ============================================================================

VirtualFSResult VirtualFSManager::ReadFile(const std::string& path, std::vector<uint8_t>& outData) {
    std::string normalized = NormalizePath(path);
    auto resolved = ResolvePath(normalized);
    
    if (resolved.archiveStack.empty()) {
        return ReadFromRealFS(normalized, outData);
    }
    
    auto provider = GetOrOpenArchive(resolved.realPath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    return provider->ReadFile(resolved.virtualPath, outData);
}

VirtualFSResult VirtualFSManager::ReadFromRealFS(const std::string& path, std::vector<uint8_t>& outData) {
    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return VirtualFSResult::NotFound;
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        outData.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(outData.data()), size)) {
            return VirtualFSResult::ReadError;
        }
        
        return VirtualFSResult::Success;
    } catch (const std::exception& e) {
        NotifyError(VirtualFSResult::ReadError, e.what(), path);
        return VirtualFSResult::ReadError;
    }
}

VirtualFSResult VirtualFSManager::ReadFilePartial(
    const std::string& path,
    uint64_t offset,
    uint64_t length,
    std::vector<uint8_t>& outData) {
    
    std::string normalized = NormalizePath(path);
    auto resolved = ResolvePath(normalized);
    
    if (resolved.archiveStack.empty()) {
        // Real filesystem partial read
        try {
            std::ifstream file(normalized, std::ios::binary | std::ios::ate);
            if (!file) {
                return VirtualFSResult::NotFound;
            }
            
            std::streamsize fileSize = file.tellg();
            if (static_cast<int64_t>(offset) >= fileSize) {
                outData.clear();
                return VirtualFSResult::Success;
            }
            
            uint64_t actualLength = std::min(length, static_cast<uint64_t>(fileSize) - offset);
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            
            outData.resize(static_cast<size_t>(actualLength));
            if (!file.read(reinterpret_cast<char*>(outData.data()), static_cast<std::streamsize>(actualLength))) {
                return VirtualFSResult::ReadError;
            }
            
            return VirtualFSResult::Success;
        } catch (const std::exception& e) {
            NotifyError(VirtualFSResult::ReadError, e.what(), path);
            return VirtualFSResult::ReadError;
        }
    }
    
    auto provider = GetOrOpenArchive(resolved.realPath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    return provider->ReadFilePartial(resolved.virtualPath, offset, length, outData);
}

VirtualFSResult VirtualFSManager::ReadFileString(
    const std::string& path,
    std::string& outText,
    const std::string& encoding) {
    
    std::vector<uint8_t> data;
    auto result = ReadFile(path, data);
    
    if (result == VirtualFSResult::Success) {
        outText.assign(data.begin(), data.end());
        // TODO: Handle encoding conversion if needed
    }
    
    return result;
}

std::unique_ptr<VirtualFSStream> VirtualFSManager::OpenStream(
    const std::string& path,
    VirtualFSOpenMode mode) {
    
    std::string normalized = NormalizePath(path);
    auto resolved = ResolvePath(normalized);
    
    if (resolved.archiveStack.empty()) {
        // Real filesystem - not implemented here
        return nullptr;
    }
    
    auto provider = GetOrOpenArchive(resolved.realPath);
    if (!provider) {
        return nullptr;
    }
    
    return provider->OpenStream(resolved.virtualPath);
}

// ============================================================================
// EXTRACTION
// ============================================================================

VirtualFSResult VirtualFSManager::ExtractFile(
    const std::string& archivePath,
    const std::string& entryPath,
    const std::string& destPath,
    const VirtualFSExtractOptions& options) {
    
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    return provider->ExtractFile(entryPath, destPath, options);
}

VirtualFSResult VirtualFSManager::ExtractAll(
    const std::string& archivePath,
    const std::string& destDirectory,
    const VirtualFSExtractOptions& options,
    VirtualFSProgressCallback progressCallback) {
    
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    return provider->ExtractAll(destDirectory, options, progressCallback);
}

VirtualFSResult VirtualFSManager::ExtractFiltered(
    const std::string& archivePath,
    const std::string& destDirectory,
    VirtualFSFilterCallback filter,
    const VirtualFSExtractOptions& options,
    VirtualFSProgressCallback progressCallback) {
    
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    return provider->ExtractFiltered(destDirectory, filter, options, progressCallback);
}

VirtualFSResult VirtualFSManager::ExtractToMemory(const std::string& path, std::vector<uint8_t>& outData) {
    return ReadFile(path, outData);
}

// ============================================================================
// ARCHIVE CREATION
// ============================================================================

VirtualFSResult VirtualFSManager::CreateArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const VirtualFSOpenOptions& options,
    VirtualFSProgressCallback progressCallback) {
    
    auto provider = GetProviderForPath(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    if (!HasCapability(provider->GetCapabilities(), VirtualFSCapability::Create)) {
        return VirtualFSResult::NotSupported;
    }
    
    auto result = provider->CreateArchive(archivePath, options);
    if (result != VirtualFSResult::Success) {
        return result;
    }
    
    // Add files
    VirtualFSProgress progress;
    progress.totalFiles = sourcePaths.size();
    
    for (size_t i = 0; i < sourcePaths.size(); ++i) {
        const auto& sourcePath = sourcePaths[i];
        std::string destPath = VirtualFSPath::GetFileName(sourcePath);
        
        if (progressCallback) {
            progress.currentFile = sourcePath;
            progress.filesProcessed = i;
            progress.UpdatePercent();
            if (!progressCallback(progress)) {
                provider->Close();
                return VirtualFSResult::Cancelled;
            }
        }
        
        if (std::filesystem::is_directory(sourcePath)) {
            result = provider->AddDirectory(sourcePath, destPath, true, options, nullptr);
        } else {
            result = provider->AddFile(sourcePath, destPath, options);
        }
        
        if (result != VirtualFSResult::Success) {
            provider->Close();
            return result;
        }
    }
    
    return provider->Finalize();
}

VirtualFSResult VirtualFSManager::AddToArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const std::string& destPathInArchive,
    const VirtualFSOpenOptions& options) {
    
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    if (!HasCapability(provider->GetCapabilities(), VirtualFSCapability::Write)) {
        return VirtualFSResult::NotSupported;
    }
    
    for (const auto& sourcePath : sourcePaths) {
        std::string destPath = destPathInArchive.empty() 
            ? VirtualFSPath::GetFileName(sourcePath)
            : VirtualFSPath::Join(destPathInArchive, VirtualFSPath::GetFileName(sourcePath));
        
        VirtualFSResult result;
        if (std::filesystem::is_directory(sourcePath)) {
            result = provider->AddDirectory(sourcePath, destPath, true, options, nullptr);
        } else {
            result = provider->AddFile(sourcePath, destPath, options);
        }
        
        if (result != VirtualFSResult::Success) {
            return result;
        }
    }
    
    return VirtualFSResult::Success;
}

VirtualFSResult VirtualFSManager::DeleteFromArchive(
    const std::string& archivePath,
    const std::vector<std::string>& entryPaths) {
    
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    if (!HasCapability(provider->GetCapabilities(), VirtualFSCapability::Delete)) {
        return VirtualFSResult::NotSupported;
    }
    
    for (const auto& entryPath : entryPaths) {
        auto result = provider->Delete(entryPath);
        if (result != VirtualFSResult::Success) {
            return result;
        }
    }
    
    return VirtualFSResult::Success;
}

// ============================================================================
// UTILITY
// ============================================================================

std::string VirtualFSManager::GetErrorMessage(VirtualFSResult result) const {
    return VirtualFSResultToString(result);
}

bool VirtualFSManager::ValidateArchive(const std::string& archivePath) {
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return false;
    }
    
    return provider->Validate();
}

VirtualFSResult VirtualFSManager::TestArchive(
    const std::string& archivePath,
    VirtualFSProgressCallback progressCallback) {
    
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSResult::ProviderNotFound;
    }
    
    return provider->Test(progressCallback);
}

VirtualFSArchiveInfo VirtualFSManager::GetArchiveInfo(const std::string& archivePath) {
    auto provider = GetOrOpenArchive(archivePath);
    if (!provider) {
        return VirtualFSArchiveInfo();
    }
    
    return provider->GetArchiveInfo();
}

std::string VirtualFSManager::DetectFormat(const std::string& path) {
    // Read first 64 bytes for magic detection
    std::vector<uint8_t> header(64);
    
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return "";
        }
        
        file.read(reinterpret_cast<char*>(header.data()), header.size());
        size_t bytesRead = static_cast<size_t>(file.gcount());
        
        auto provider = GetProviderByMagic(header.data(), bytesRead);
        if (provider) {
            return provider->GetName();
        }
    } catch (...) {
        // Ignore errors
    }
    
    // Fall back to extension
    auto provider = GetProviderForPath(path);
    if (provider) {
        return provider->GetName();
    }
    
    return "";
}

std::string VirtualFSManager::GetMimeType(const std::string& path) {
    std::string ext = VirtualFSPath::GetExtensionLower(path);
    
    // Common archive MIME types
    static const std::map<std::string, std::string> mimeTypes = {
        {"zip", "application/zip"},
        {"7z", "application/x-7z-compressed"},
        {"rar", "application/vnd.rar"},
        {"tar", "application/x-tar"},
        {"gz", "application/gzip"},
        {"tgz", "application/gzip"},
        {"bz2", "application/x-bzip2"},
        {"xz", "application/x-xz"},
        {"lzma", "application/x-lzma"},
        {"zst", "application/zstd"},
        {"lz4", "application/x-lz4"},
        {"cab", "application/vnd.ms-cab-compressed"},
        {"iso", "application/x-iso9660-image"},
        {"jar", "application/java-archive"},
        {"apk", "application/vnd.android.package-archive"},
        {"deb", "application/vnd.debian.binary-package"},
        {"rpm", "application/x-rpm"}
    };
    
    auto it = mimeTypes.find(ext);
    if (it != mimeTypes.end()) {
        return it->second;
    }
    
    return "application/octet-stream";
}

// ============================================================================
// CACHE MANAGEMENT
// ============================================================================

void VirtualFSManager::ClearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    for (auto& pair : openArchives) {
        if (pair.second.provider) {
            pair.second.provider->Close();
        }
    }
    
    openArchives.clear();
    currentCacheSize = 0;
}

void VirtualFSManager::ClearCacheForPath(const std::string& archivePath) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    std::string normalized = NormalizePath(archivePath);
    auto it = openArchives.find(normalized);
    
    if (it != openArchives.end()) {
        if (it->second.provider) {
            it->second.provider->Close();
        }
        openArchives.erase(it);
    }
}

size_t VirtualFSManager::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return currentCacheSize;
}

void VirtualFSManager::SetMaxCacheSize(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    maxCacheSize = maxBytes;
    CleanupCache();
}

void VirtualFSManager::SetCacheEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cacheEnabled = enabled;
    
    if (!enabled) {
        ClearCache();
    }
}

std::shared_ptr<IVirtualFSProvider> VirtualFSManager::GetOrOpenArchive(const std::string& archivePath) {
    std::string normalized = NormalizePath(archivePath);
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Check cache
    auto it = openArchives.find(normalized);
    if (it != openArchives.end()) {
        it->second.lastAccess = std::chrono::steady_clock::now();
        return it->second.provider;
    }
    
    // Get provider for this format
    auto provider = GetProviderForPath(normalized);
    if (!provider) {
        return nullptr;
    }
    
    // Create new instance
    // Note: In a real implementation, we'd need a factory to create new instances
    // For now, we'll just use the registered provider directly
    auto result = provider->Open(normalized);
    if (result != VirtualFSResult::Success) {
        return nullptr;
    }
    
    if (cacheEnabled) {
        OpenArchive cached;
        cached.provider = provider;
        cached.path = normalized;
        cached.lastAccess = std::chrono::steady_clock::now();
        openArchives[normalized] = cached;
    }
    
    return provider;
}

void VirtualFSManager::CloseArchive(const std::string& archivePath) {
    ClearCacheForPath(archivePath);
}

void VirtualFSManager::CleanupCache() {
    // Remove least recently used archives if cache is too large
    // This is a simplified implementation
    
    if (openArchives.size() > 10) {
        // Find oldest entry
        auto oldest = openArchives.begin();
        for (auto it = openArchives.begin(); it != openArchives.end(); ++it) {
            if (it->second.lastAccess < oldest->second.lastAccess) {
                oldest = it;
            }
        }
        
        if (oldest != openArchives.end()) {
            if (oldest->second.provider) {
                oldest->second.provider->Close();
            }
            openArchives.erase(oldest);
        }
    }
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void VirtualFSManager::SetPasswordCallback(VirtualFSPasswordCallback callback) {
    passwordCallback = callback;
}

void VirtualFSManager::SetErrorCallback(VirtualFSErrorCallback callback) {
    errorCallback = callback;
}

void VirtualFSManager::SetTempDirectory(const std::string& path) {
    tempDirectory = path;
}

std::string VirtualFSManager::GetTempDirectory() const {
    return tempDirectory;
}

void VirtualFSManager::SetDefaultEncoding(const std::string& encoding) {
    defaultEncoding = encoding;
}

std::string VirtualFSManager::GetDefaultEncoding() const {
    return defaultEncoding;
}

void VirtualFSManager::NotifyError(VirtualFSResult result, const std::string& message, const std::string& path) {
    if (errorCallback) {
        errorCallback(result, message, path);
    }
}

} // namespace VirtualFS
