// OS/MacOS/UltraCanvasMacOSLaunchAtLogin.cpp
// UltraCanvasLaunchAtLogin on macOS: a LaunchAgent,
// ~/Library/LaunchAgents/<id>.plist with RunAtLoad, which launchd reads at
// every sign-in (System Settings > General > Login Items lists it under
// "Allow in the Background"). It is not loaded now - that would start a
// second copy at once - only at the next sign-in. A program inside an .app
// bundle is started through `open -a <bundle>`, so it starts as an
// application (Dock icon, menu bar) rather than as a bare process.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasLaunchAtLogin.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include <mach-o/dyld.h>   // _NSGetExecutablePath

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

fs::path AgentPath(const std::string& id) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return PathFromUtf8(home) / "Library" / "LaunchAgents" / PathFromUtf8(id + ".plist");
}

std::string ThisProgram() {
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) return {};
    char resolved[PATH_MAX];
    return realpath(buffer, resolved) ? std::string(resolved) : std::string(buffer);
}

// "/Applications/ChatCube.app" for "/Applications/ChatCube.app/Contents/MacOS/ChatCube".
std::string BundleOf(const std::string& program) {
    const std::string marker = ".app/Contents/MacOS/";
    const size_t at = program.rfind(marker);
    if (at == std::string::npos) return {};
    return program.substr(0, at + 4);
}

std::vector<std::string> ProgramArguments(const LaunchAtLoginEntry& entry) {
    const std::string program = entry.executable.empty() ? ThisProgram() : entry.executable;
    if (program.empty()) return {};
    std::vector<std::string> argv;
    const std::string bundle = BundleOf(program);
    if (!bundle.empty()) {
        argv = {"/usr/bin/open", "-a", bundle};
        if (!entry.arguments.empty()) argv.push_back("--args");
    } else {
        argv = {program};
    }
    argv.insert(argv.end(), entry.arguments.begin(), entry.arguments.end());
    return argv;
}

std::string XmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// launchctl's own "Disabled" key: a plist that has it true does not start.
bool AgentStarts(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    std::string plist = text.str();
    std::string compact;
    for (char c : plist)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') compact += c;
    return compact.find("<key>Disabled</key><true/>") == std::string::npos;
}

} // namespace

namespace LaunchAtLoginBackend {

bool Available() {
    return !AgentPath("x").empty();
}

bool IsEnabled(const std::string& id) {
    const fs::path file = AgentPath(id);
    std::error_code ec;
    return !file.empty() && fs::is_regular_file(file, ec) && AgentStarts(file);
}

bool Set(const LaunchAtLoginEntry& entry, bool enabled, std::string& error) {
    const fs::path file = AgentPath(entry.id);
    if (file.empty()) {
        error = Tr("This computer cannot start programs when you sign in.");
        return false;
    }
    std::error_code ec;
    if (!enabled) {
        fs::remove(file, ec);
        if (ec) {
            error = TrF("The startup list could not be changed: {0}", PathToUtf8(file) + ": " + ec.message());
            return false;
        }
        return true;
    }
    const std::vector<std::string> argv = ProgramArguments(entry);
    if (argv.empty()) {
        error = Tr("Where this program is installed could not be found.");
        return false;
    }
    std::string text =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n";
    text += "\t<key>Label</key>\n\t<string>" + XmlEscape(entry.id) + "</string>\n";
    text += "\t<key>ProgramArguments</key>\n\t<array>\n";
    for (const std::string& arg : argv) text += "\t\t<string>" + XmlEscape(arg) + "</string>\n";
    text += "\t</array>\n";
    text += "\t<key>RunAtLoad</key>\n\t<true/>\n";
    text += "\t<key>ProcessType</key>\n\t<string>Interactive</string>\n";
    text += "</dict>\n</plist>\n";

    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        error = TrF("The startup list could not be changed: {0}", PathToUtf8(file.parent_path()) + ": " + ec.message());
        return false;
    }
    fs::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) {
            fs::remove(temp, ec);
            error = TrF("The startup list could not be changed: {0}", PathToUtf8(temp));
            return false;
        }
    }
    fs::rename(temp, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        error = TrF("The startup list could not be changed: {0}", PathToUtf8(file) + ": " + ec.message());
        return false;
    }
    return true;
}

std::string Command(const LaunchAtLoginEntry& entry) {
    std::string line;
    for (const std::string& arg : ProgramArguments(entry)) {
        if (!line.empty()) line += ' ';
        line += arg;
    }
    return line;
}

} // namespace LaunchAtLoginBackend

} // namespace UltraCanvas
