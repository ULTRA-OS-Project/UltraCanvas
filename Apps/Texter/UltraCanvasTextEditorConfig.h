// Apps/Texter/UltraCanvasTextEditorConfig.h
// Persistent configuration file manager for UltraTexter
// Version: 1.0.0
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
                std::cerr << "UltraTexter: Failed to create config directory: "
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

    private:
        std::string configDir;
        std::string configPath;
        std::string recentFilesPath;
        std::map<std::string, std::string> settings;

        static std::string TrimWhitespace(const std::string& str) {
            size_t start = str.find_first_not_of(" \t");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t");
            return str.substr(start, end - start + 1);
        }
    };

} // namespace UltraCanvas
