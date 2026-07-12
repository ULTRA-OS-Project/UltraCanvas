// VirtualFS/include/VirtualFSPath.h
// Path parsing and manipulation utilities for VirtualFS
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSTypes.h"
#include <string>
#include <vector>
#include <algorithm>

namespace VirtualFS {

/**
 * @class VirtualFSPath
 * @brief Static utility class for path manipulation
 * 
 * Handles virtual paths that may span real filesystem and archives.
 * Example path: "/home/user/archive.zip/docs/readme.txt"
 */
class VirtualFSPath {
public:
    // =========================================================================
    // PATH SEPARATORS
    // =========================================================================
    
    static constexpr char Separator = '/';
    static constexpr char WindowsSeparator = '\\';
    static constexpr char ExtensionSeparator = '.';
    
    // =========================================================================
    // PATH NORMALIZATION
    // =========================================================================
    
    /**
     * @brief Normalizes a path (forward slashes, removes redundant components)
     * @param path Input path
     * @return Normalized path
     */
    static std::string Normalize(const std::string& path) {
        if (path.empty()) return "";
        
        std::string result = path;
        
        // Convert backslashes to forward slashes
        std::replace(result.begin(), result.end(), WindowsSeparator, Separator);
        
        // Remove duplicate slashes
        std::string cleaned;
        bool prevSlash = false;
        for (char c : result) {
            if (c == Separator) {
                if (!prevSlash || cleaned.empty()) {
                    cleaned += c;
                }
                prevSlash = true;
            } else {
                cleaned += c;
                prevSlash = false;
            }
        }
        result = cleaned;
        
        // Handle . and .. components
        std::vector<std::string> parts = Split(result);
        std::vector<std::string> resolved;
        bool isAbsolute = !result.empty() && result[0] == Separator;
        
        for (const auto& part : parts) {
            if (part == ".") {
                continue;
            } else if (part == "..") {
                if (!resolved.empty() && resolved.back() != "..") {
                    resolved.pop_back();
                } else if (!isAbsolute) {
                    resolved.push_back(part);
                }
            } else if (!part.empty()) {
                resolved.push_back(part);
            }
        }
        
        // Reconstruct path
        result = isAbsolute ? "/" : "";
        for (size_t i = 0; i < resolved.size(); ++i) {
            if (i > 0) result += Separator;
            result += resolved[i];
        }
        
        // Handle root path
        if (result.empty() && isAbsolute) {
            result = "/";
        }
        
        return result;
    }
    
    // =========================================================================
    // PATH COMPONENTS
    // =========================================================================
    
    /**
     * @brief Splits path into components
     * @param path Input path
     * @return Vector of path components
     */
    static std::vector<std::string> Split(const std::string& path) {
        std::vector<std::string> components;
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), WindowsSeparator, Separator);
        
        size_t start = 0;
        size_t end;
        
        while ((end = normalized.find(Separator, start)) != std::string::npos) {
            if (end > start) {
                components.push_back(normalized.substr(start, end - start));
            }
            start = end + 1;
        }
        
        if (start < normalized.length()) {
            components.push_back(normalized.substr(start));
        }
        
        return components;
    }
    
    /**
     * @brief Joins path components with separator
     * @param components Vector of path components
     * @return Joined path
     */
    static std::string Join(const std::vector<std::string>& components) {
        std::string result;
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0 && !result.empty() && result.back() != Separator) {
                result += Separator;
            }
            result += components[i];
        }
        return result;
    }
    
    /**
     * @brief Joins two path components
     * @param base Base path
     * @param relative Relative path to append
     * @return Combined path
     */
    static std::string Join(const std::string& base, const std::string& relative) {
        if (base.empty()) return relative;
        if (relative.empty()) return base;
        
        // If relative is absolute, return it as-is
        if (!relative.empty() && (relative[0] == Separator || relative[0] == WindowsSeparator)) {
            return Normalize(relative);
        }
        
        std::string result = base;
        if (!result.empty() && result.back() != Separator && result.back() != WindowsSeparator) {
            result += Separator;
        }
        result += relative;
        
        return Normalize(result);
    }
    
    /**
     * @brief Gets parent directory path
     * @param path Input path
     * @return Parent path
     */
    static std::string GetParent(const std::string& path) {
        std::string normalized = Normalize(path);
        if (normalized.empty() || normalized == "/") return "";
        
        // Remove trailing slash
        if (normalized.back() == Separator) {
            normalized.pop_back();
        }
        
        size_t pos = normalized.rfind(Separator);
        if (pos == std::string::npos) {
            return "";
        } else if (pos == 0) {
            return "/";
        }
        
        return normalized.substr(0, pos);
    }
    
    /**
     * @brief Gets filename component (last component)
     * @param path Input path
     * @return Filename
     */
    static std::string GetFileName(const std::string& path) {
        std::string normalized = Normalize(path);
        if (normalized.empty()) return "";
        
        // Remove trailing slash
        if (normalized.back() == Separator) {
            normalized.pop_back();
        }
        
        size_t pos = normalized.rfind(Separator);
        if (pos == std::string::npos) {
            return normalized;
        }
        
        return normalized.substr(pos + 1);
    }
    
    /**
     * @brief Gets file extension (without dot)
     * @param path Input path
     * @return Extension or empty string
     */
    static std::string GetExtension(const std::string& path) {
        std::string filename = GetFileName(path);
        size_t pos = filename.rfind(ExtensionSeparator);
        
        if (pos == std::string::npos || pos == 0 || pos == filename.length() - 1) {
            return "";
        }
        
        return filename.substr(pos + 1);
    }
    
    /**
     * @brief Gets file extension in lowercase
     * @param path Input path
     * @return Lowercase extension
     */
    static std::string GetExtensionLower(const std::string& path) {
        std::string ext = GetExtension(path);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }
    
    /**
     * @brief Gets filename without extension
     * @param path Input path
     * @return Filename without extension
     */
    static std::string GetFileNameWithoutExtension(const std::string& path) {
        std::string filename = GetFileName(path);
        size_t pos = filename.rfind(ExtensionSeparator);
        
        if (pos == std::string::npos || pos == 0) {
            return filename;
        }
        
        return filename.substr(0, pos);
    }
    
    /**
     * @brief Changes file extension
     * @param path Input path
     * @param newExtension New extension (without dot)
     * @return Path with new extension
     */
    static std::string ChangeExtension(const std::string& path, const std::string& newExtension) {
        std::string parent = GetParent(path);
        std::string baseName = GetFileNameWithoutExtension(path);
        
        std::string result;
        if (!parent.empty()) {
            result = parent + Separator;
        }
        result += baseName;
        
        if (!newExtension.empty()) {
            result += ExtensionSeparator;
            result += newExtension;
        }
        
        return result;
    }
    
    // =========================================================================
    // PATH QUERIES
    // =========================================================================
    
    /**
     * @brief Checks if path is absolute
     * @param path Input path
     * @return true if absolute
     */
    static bool IsAbsolute(const std::string& path) {
        if (path.empty()) return false;
        
        // Unix absolute path
        if (path[0] == Separator) return true;
        
        // Windows absolute path (e.g., C:\)
        if (path.length() >= 3 && std::isalpha(path[0]) && 
            path[1] == ':' && (path[2] == Separator || path[2] == WindowsSeparator)) {
            return true;
        }
        
        // UNC path (\\server\share)
        if (path.length() >= 2 && path[0] == WindowsSeparator && path[1] == WindowsSeparator) {
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Checks if path is relative
     * @param path Input path
     * @return true if relative
     */
    static bool IsRelative(const std::string& path) {
        return !IsAbsolute(path);
    }
    
    /**
     * @brief Checks if path has extension
     * @param path Input path
     * @return true if has extension
     */
    static bool HasExtension(const std::string& path) {
        return !GetExtension(path).empty();
    }
    
    /**
     * @brief Checks if path has specific extension (case-insensitive)
     * @param path Input path
     * @param extension Extension to check (without dot)
     * @return true if matches
     */
    static bool HasExtension(const std::string& path, const std::string& extension) {
        std::string pathExt = GetExtensionLower(path);
        std::string checkExt = extension;
        std::transform(checkExt.begin(), checkExt.end(), checkExt.begin(), ::tolower);
        return pathExt == checkExt;
    }
    
    /**
     * @brief Checks if child path is under parent path
     * @param parent Parent path
     * @param child Child path
     * @return true if child is under parent
     */
    static bool IsUnder(const std::string& parent, const std::string& child) {
        std::string normParent = Normalize(parent);
        std::string normChild = Normalize(child);
        
        if (normParent.empty() || normChild.empty()) return false;
        if (normChild.length() <= normParent.length()) return false;
        
        // Ensure parent ends with separator for comparison
        if (normParent.back() != Separator) {
            normParent += Separator;
        }
        
        return normChild.substr(0, normParent.length()) == normParent;
    }
    
    /**
     * @brief Gets relative path from base to target
     * @param base Base path
     * @param target Target path
     * @return Relative path
     */
    static std::string GetRelative(const std::string& base, const std::string& target) {
        std::string normBase = Normalize(base);
        std::string normTarget = Normalize(target);
        
        if (IsUnder(normBase, normTarget)) {
            size_t baseLen = normBase.length();
            if (normBase.back() != Separator) baseLen++;
            return normTarget.substr(baseLen);
        }
        
        return normTarget;
    }
    
    // =========================================================================
    // ARCHIVE-AWARE PATH HANDLING
    // =========================================================================
    
    /**
     * @brief List of known archive extensions
     */
    static const std::vector<std::string>& GetArchiveExtensions() {
        static std::vector<std::string> extensions = {
            "zip", "7z", "rar", "tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz",
            "lzma", "lz4", "zst", "zstd", "cab", "iso", "udf", "cpio", "ar",
            "deb", "rpm", "jar", "war", "ear", "apk", "ipa", "xpi", "crx",
            "cbz", "cbr", "cb7", "epub", "docx", "xlsx", "pptx", "odt", "ods"
        };
        return extensions;
    }
    
    /**
     * @brief Checks if extension is a known archive format
     * @param extension Extension to check (without dot)
     * @return true if archive extension
     */
    static bool IsArchiveExtension(const std::string& extension) {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        const auto& archiveExts = GetArchiveExtensions();
        return std::find(archiveExts.begin(), archiveExts.end(), ext) != archiveExts.end();
    }
    
    /**
     * @brief Checks if path points to an archive file
     * @param path Input path
     * @return true if path has archive extension
     */
    static bool IsArchivePath(const std::string& path) {
        return IsArchiveExtension(GetExtension(path));
    }
    
    /**
     * @brief Finds the first archive in a path
     * @param path Virtual path
     * @return Position after archive extension, or npos if no archive
     */
    static size_t FindArchiveInPath(const std::string& path) {
        std::string normalized = Normalize(path);
        std::vector<std::string> components = Split(normalized);
        
        size_t position = 0;
        for (const auto& component : components) {
            position += component.length();
            
            if (IsArchivePath(component)) {
                return position;
            }
            
            position++; // For separator
        }
        
        return std::string::npos;
    }
    
    /**
     * @brief Resolves a virtual path into real and virtual components
     * @param path Full virtual path
     * @return Resolved path structure
     */
    static VirtualFSResolvedPath Resolve(const std::string& path) {
        VirtualFSResolvedPath result;
        result.fullPath = Normalize(path);
        
        if (result.fullPath.empty()) {
            return result;
        }
        
        std::vector<std::string> components = Split(result.fullPath);
        std::string currentPath;
        bool isAbsolute = IsAbsolute(result.fullPath);
        
        if (isAbsolute) {
            currentPath = "/";
        }
        
        bool foundArchive = false;
        std::string archivePath;
        std::vector<std::string> virtualComponents;
        
        for (size_t i = 0; i < components.size(); ++i) {
            const auto& component = components[i];
            
            if (!foundArchive) {
                // Building real path
                if (currentPath.empty() || currentPath == "/") {
                    currentPath = isAbsolute ? "/" + component : component;
                } else {
                    currentPath += Separator;
                    currentPath += component;
                }
                
                if (IsArchivePath(component)) {
                    foundArchive = true;
                    archivePath = currentPath;
                    result.archiveStack.push_back(currentPath);
                    result.realPath = currentPath;
                }
            } else {
                // Building virtual path
                virtualComponents.push_back(component);
                
                // Check for nested archives
                if (IsArchivePath(component)) {
                    std::string nestedPath = Join(virtualComponents);
                    result.archiveStack.push_back(nestedPath);
                }
            }
        }
        
        if (!foundArchive) {
            result.realPath = currentPath;
            result.isInsideArchive = false;
        } else {
            result.virtualPath = Join(virtualComponents);
            result.isInsideArchive = !virtualComponents.empty();
        }
        
        return result;
    }
    
    // =========================================================================
    // PATTERN MATCHING
    // =========================================================================
    
    /**
     * @brief Matches path against glob pattern
     * @param path Path to match
     * @param pattern Glob pattern (*, ?, **)
     * @return true if matches
     */
    static bool MatchGlob(const std::string& path, const std::string& pattern) {
        return MatchGlobImpl(path.c_str(), pattern.c_str());
    }
    
private:
    static bool MatchGlobImpl(const char* path, const char* pattern) {
        while (*pattern) {
            if (*pattern == '*') {
                // Handle ** (matches any path)
                if (*(pattern + 1) == '*') {
                    pattern += 2;
                    if (*pattern == '/') pattern++;
                    if (!*pattern) return true;
                    
                    while (*path) {
                        if (MatchGlobImpl(path, pattern)) return true;
                        path++;
                    }
                    return false;
                }
                
                // Handle * (matches within component)
                pattern++;
                while (*path && *path != '/') {
                    if (MatchGlobImpl(path, pattern)) return true;
                    path++;
                }
                return MatchGlobImpl(path, pattern);
            } else if (*pattern == '?') {
                if (!*path || *path == '/') return false;
                path++;
                pattern++;
            } else {
                if (*path != *pattern) return false;
                if (!*path) return true;
                path++;
                pattern++;
            }
        }
        return !*path;
    }
};

} // namespace VirtualFS
