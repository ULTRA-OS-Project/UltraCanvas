// Apps/Texter/UltraCanvasTextEditorConfig.h
// Persistent configuration file manager for UltraTexter
// Version: 1.0.1
// Last Modified: 2026-02-26
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <functional>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    // ===== CONFIG FILE MANAGER =====
    // Reads/writes a simple key=value config file
    // Stores settings in ~/.config/UltraTexter/config.ini (Linux)
    // or %APPDATA%\UltraTexter\config.ini (Windows)
    // or ~/Library/Application Support/UltraTexter/config.ini (macOS)

    class TextEditorConfigFile {
    public:
        TextEditorConfigFile() {
            configDir = GetConfigDirectory();
            configPath = configDir + "/config.ini";
            recentFilesPath = configDir + "/recent_files.txt";
            sessionPath = configDir + "/session.txt";
        }

        // ===== CONFIG DIRECTORY =====

        /// Get the platform-specific config directory
        static std::string GetConfigDirectory() {
            std::string dir;

#if defined(_WIN32) || defined(_WIN64)
            const char* appdata = std::getenv("APPDATA");
            if (appdata) {
                dir = std::string(appdata) + "\\UltraTexter";
            } else {
                dir = "UltraTexter";
            }
#elif defined(__APPLE__)
            const char* home = std::getenv("HOME");
            if (home) {
                dir = std::string(home) + "/Library/Application Support/UltraTexter";
            } else {
                dir = "UltraTexter";
            }
#else
            // Linux / Unix
            const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
            if (xdgConfig) {
                dir = std::string(xdgConfig) + "/UltraTexter";
            } else {
                const char* home = std::getenv("HOME");
                if (home) {
                    dir = std::string(home) + "/.config/UltraTexter";
                } else {
                    dir = "UltraTexter";
                }
            }
#endif
            return dir;
        }

        /// Ensure the config directory exists
        bool EnsureConfigDirectory() {
            try {
                std::filesystem::create_directories(configDir);
                return true;
            } catch (const std::exception& e) {
                debugOutput << "UltraTexter: Failed to create config directory: "
                          << e.what() << std::endl;
                return false;
            }
        }

        // ===== GENERAL SETTINGS =====

        /// Load all settings from config file
        bool Load() {
            std::ifstream file(configPath);
            if (!file.is_open()) return false;

            settings.clear();
            std::string line;
            while (std::getline(file, line)) {
                // Skip comments and empty lines
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;

                size_t eqPos = line.find('=');
                if (eqPos == std::string::npos) continue;

                std::string key = TrimWhitespace(line.substr(0, eqPos));
                std::string value = TrimWhitespace(line.substr(eqPos + 1));
                settings[key] = value;
            }
            return true;
        }

        /// Save all settings to config file
        bool Save() {
            if (!EnsureConfigDirectory()) return false;

            std::ofstream file(configPath);
            if (!file.is_open()) return false;

            file << "# UltraTexter Configuration" << std::endl;
            file << "# Auto-generated — manual edits are preserved" << std::endl;
            file << std::endl;

            for (const auto& [key, value] : settings) {
                file << key << " = " << value << std::endl;
            }
            return true;
        }

        // Typed getters with defaults
        std::string GetString(const std::string& key, const std::string& defaultValue = "") const {
            auto it = settings.find(key);
            return (it != settings.end()) ? it->second : defaultValue;
        }

        int GetInt(const std::string& key, int defaultValue = 0) const {
            auto it = settings.find(key);
            if (it == settings.end()) return defaultValue;
            try { return std::stoi(it->second); }
            catch (...) { return defaultValue; }
        }

        bool GetBool(const std::string& key, bool defaultValue = false) const {
            auto it = settings.find(key);
            if (it == settings.end()) return defaultValue;
            return (it->second == "true" || it->second == "1" || it->second == "yes");
        }

        // Setters
        void SetString(const std::string& key, const std::string& value) {
            settings[key] = value;
        }
        void SetInt(const std::string& key, int value) {
            settings[key] = std::to_string(value);
        }
        void SetBool(const std::string& key, bool value) {
            settings[key] = value ? "true" : "false";
        }

        // ===== RECENT FILES =====

        /// Load recent files list from file
        std::vector<std::string> LoadRecentFiles() {
            std::vector<std::string> files;
            std::ifstream file(recentFilesPath);
            if (!file.is_open()) return files;

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    files.push_back(line);
                }
            }
            return files;
        }

        /// Save recent files list to file
        bool SaveRecentFiles(const std::vector<std::string>& files) {
            if (!EnsureConfigDirectory()) return false;

            std::ofstream file(recentFilesPath);
            if (!file.is_open()) return false;

            for (const auto& path : files) {
                file << path << std::endl;
            }
            return true;
        }

        void SaveSearchHistory(const std::vector<std::string>& searchHist,
                                                     const std::vector<std::string>& replaceHist) {
            std::string historyPath = GetConfigDirectory() + "/search_history.txt";
            std::ofstream out(historyPath);
            if (!out.is_open()) return;

            out << "[find]" << std::endl;
            for (const auto& item : searchHist) {
                out << item << std::endl;
            }
            out << "[replace]" << std::endl;
            for (const auto& item : replaceHist) {
                out << item << std::endl;
            }
        }

        void LoadSearchHistory(std::vector<std::string>& searchHist,
                                                     std::vector<std::string>& replaceHist) {
            std::string historyPath = GetConfigDirectory() + "/search_history.txt";
            std::ifstream in(historyPath);
            if (!in.is_open()) return;

            std::string line;
            std::string currentSection;

            while (std::getline(in, line)) {
                if (line == "[find]") {
                    currentSection = "find";
                    continue;
                }
                if (line == "[replace]") {
                    currentSection = "replace";
                    continue;
                }
                if (line.empty()) continue;

                if (currentSection == "find") {
                    searchHist.push_back(line);
                } else if (currentSection == "replace") {
                    replaceHist.push_back(line);
                }
            }
        }

        // ===== SESSION FILES =====

        /// A single document entry in the persisted session.
        /// For saved files only filePath is required; display name is re-derived on load.
        /// For unsaved tabs that carry content, backupPath points at the autosave file
        /// and displayName carries the tab title.
        struct SessionDocument {
            std::string filePath;      // Absolute path on disk, or empty for unsaved tabs
            std::string displayName;   // Tab title — only persisted when filePath is empty
            std::string backupPath;    // Autosave backup path — only when wasModified
            std::string language;
            std::string encoding;
            int eolType = 0;
            bool isNewFile = true;
            bool wasModified = false;
        };

        /// Persist all open documents and the active tab index.
        bool SaveSession(const std::vector<SessionDocument>& docs, int activeIndex) {
            if (!EnsureConfigDirectory()) return false;

            std::ofstream file(sessionPath);
            if (!file.is_open()) {
                debugOutput << "UltraTexter: SaveSession failed to open " << sessionPath << std::endl;
                return false;
            }

            file << "ACTIVE=" << activeIndex << std::endl;
            for (const auto& d : docs) {
                file << std::endl << "[DOCUMENT]" << std::endl;
                file << "FILE_PATH=" << d.filePath << std::endl;
                file << "DISPLAY_NAME=" << d.displayName << std::endl;
                file << "BACKUP_PATH=" << d.backupPath << std::endl;
                file << "LANGUAGE=" << d.language << std::endl;
                file << "ENCODING=" << d.encoding << std::endl;
                file << "EOL_TYPE=" << d.eolType << std::endl;
                file << "IS_NEW_FILE=" << (d.isNewFile ? "true" : "false") << std::endl;
                file << "WAS_MODIFIED=" << (d.wasModified ? "true" : "false") << std::endl;
            }
            debugOutput << "UltraTexter: SaveSession wrote " << docs.size()
                        << " document(s), active=" << activeIndex << std::endl;
            return true;
        }

        /// Load the persisted session. Returns document entries in tab order.
        std::vector<SessionDocument> LoadSession(int& activeIndex) {
            std::vector<SessionDocument> docs;
            activeIndex = 0;

            std::ifstream file(sessionPath);
            if (!file.is_open()) return docs;

            SessionDocument current;
            bool inDoc = false;

            auto flush = [&]() {
                if (inDoc) {
                    docs.push_back(current);
                    current = SessionDocument{};
                    inDoc = false;
                }
            };

            std::string line;
            while (std::getline(file, line)) {
                // Strip trailing \r for files written on Windows
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;

                if (line == "[DOCUMENT]") {
                    flush();
                    inDoc = true;
                    continue;
                }

                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = TrimWhitespace(line.substr(0, eq));
                std::string value = line.substr(eq + 1); // keep value untrimmed — paths may have leading spaces

                if (!inDoc) {
                    if (key == "ACTIVE") {
                        try { activeIndex = std::stoi(value); }
                        catch (...) { activeIndex = 0; }
                    }
                    continue;
                }

                if      (key == "FILE_PATH")    current.filePath = value;
                else if (key == "DISPLAY_NAME") current.displayName = value;
                else if (key == "BACKUP_PATH")  current.backupPath = value;
                else if (key == "LANGUAGE")     current.language = value;
                else if (key == "ENCODING")     current.encoding = value;
                else if (key == "EOL_TYPE") {
                    try { current.eolType = std::stoi(value); }
                    catch (...) { current.eolType = 0; }
                }
                else if (key == "IS_NEW_FILE")  current.isNewFile  = (value == "true" || value == "1");
                else if (key == "WAS_MODIFIED") current.wasModified = (value == "true" || value == "1");
            }
            flush();

            debugOutput << "UltraTexter: LoadSession read " << docs.size()
                        << " document(s), active=" << activeIndex << std::endl;
            return docs;
        }

    private:
        std::string configDir;
        std::string configPath;
        std::string recentFilesPath;
        std::string sessionPath;
        std::map<std::string, std::string> settings;

        static std::string TrimWhitespace(const std::string& str) {
            size_t start = str.find_first_not_of(" \t");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t");
            return str.substr(start, end - start + 1);
        }
    };

} // namespace UltraCanvas
