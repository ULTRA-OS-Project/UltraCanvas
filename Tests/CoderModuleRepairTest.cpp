// Tests/CoderModuleRepairTest.cpp
// UltraCanvasCoderModuleRepair on a scratch coder folder: a pseudo-format
// coder with a system DLL's name goes, with its .la; a real format is
// renamed and its .la pointed at the new file; a coder with no .la goes;
// everything else is untouched; a second run changes nothing; and the
// platform entry point does nothing where there is no Windows.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#include "UltraCanvasCoderModuleRepair.h"
#include "UltraCanvasPathUtf8.h"   // PathToUtf8

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas::CoderModuleRepair;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
    if (!ok) ++failures;
}

void CheckEq(const std::string& got, const std::string& want, const char* what) {
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what);
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

struct ScratchFolder {
    fs::path path;
    ScratchFolder() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / "UltraCanvasCoderModuleRepairTest";
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~ScratchFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void WriteFile(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string LaFor(const std::string& dll) {
    return "# libtool library file\ndlname='" + dll + "'\nlibrary_names='" + dll +
           "'\nold_library=''\nlibdir='/clang64/lib/ImageMagick-7.1.2/modules-Q16HDRI/coders'\n";
}

bool Exists(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }

void TestNames() {
    std::printf("Names\n");
    CheckEq(LowerCaseName("MPR.DLL"), "mpr.dll", "lower-cased for comparison");
    Check(IsKnownSystemDllName("mpr.dll") && IsKnownSystemDllName("url.dll") &&
          IsKnownSystemDllName("dpx.dll") && IsKnownSystemDllName("vid.dll"),
          "the four names Windows 10 and 11 also use are known");
    Check(!IsKnownSystemDllName("png.dll"), "png is not");
    Check(IsDroppableCoder("mpr.dll") && IsDroppableCoder("url.dll"), "mpr and url are dropped");
    Check(!IsDroppableCoder("dpx.dll"), "dpx is kept");
    CheckEq(RenamedCoderFileName("dpx.dll"), "dpx-coder.dll", "the renamed file");
}

void TestLibtoolRewrite() {
    std::printf("Libtool archive rewrite\n");
    CheckEq(RewriteLibtoolArchive(LaFor("dpx.dll"), "dpx-coder.dll"),
            LaFor("dpx-coder.dll"), "dlname and library_names point at the new file");
    CheckEq(RewriteLibtoolArchive("dlname='a.dll'\r\nlibdir='/x'\r\n", "b.dll"),
            "dlname='b.dll'\r\nlibdir='/x'\r\n", "CRLF survives");
    CheckEq(RewriteLibtoolArchive("dlname='a.dll'", "b.dll"), "dlname='b.dll'",
            "no trailing newline, none added");
    CheckEq(RewriteLibtoolArchive(LaFor("dpx-coder.dll"), "dpx-coder.dll"),
            LaFor("dpx-coder.dll"), "already pointing there: unchanged");
}

void TestRepair() {
    std::printf("Repair of a coder folder\n");
    ScratchFolder scratch;
    const fs::path dir = scratch.path / "coders";
    fs::create_directories(dir);
    for (const char* c : {"png", "mpr", "url", "dpx", "vid", "bmp"}) {
        WriteFile(dir / (std::string(c) + ".dll"), "dll");
        WriteFile(dir / (std::string(c) + ".la"), LaFor(std::string(c) + ".dll"));
    }
    WriteFile(dir / "nola.dll", "dll");   // colliding, by the predicate below, with no .la
    auto isSystem = [](const std::string& lower) {
        return IsKnownSystemDllName(lower) || lower == "nola.dll";
    };

    CoderModuleRepairResult r = RepairCoderModules(UltraCanvas::PathToUtf8(dir), isSystem);
    Check(r.failed.empty(), "nothing failed");
    Check(r.Changed(), "something changed");
    Check(!Exists(dir / "mpr.dll") && !Exists(dir / "mpr.la"), "mpr.dll and mpr.la are gone");
    Check(!Exists(dir / "url.dll") && !Exists(dir / "url.la"), "url.dll and url.la are gone");
    Check(!Exists(dir / "dpx.dll") && Exists(dir / "dpx-coder.dll"), "dpx.dll became dpx-coder.dll");
    CheckEq(ReadFile(dir / "dpx.la"), LaFor("dpx-coder.dll"), "dpx.la points at dpx-coder.dll");
    Check(!Exists(dir / "vid.dll") && Exists(dir / "vid-coder.dll"), "vid.dll became vid-coder.dll");
    Check(!Exists(dir / "nola.dll"), "a colliding coder without a .la is removed");
    Check(Exists(dir / "png.dll") && Exists(dir / "png.la") && Exists(dir / "bmp.dll"),
          "the others are untouched");
    CheckEq(ReadFile(dir / "png.la"), LaFor("png.dll"), "and so are their .la files");
    Check(r.removed.size() == 5, "five files removed (mpr, url with .la; nola)");
    Check(r.renamed.size() == 2, "two coders renamed");

    CoderModuleRepairResult again = RepairCoderModules(UltraCanvas::PathToUtf8(dir), isSystem);
    Check(!again.Changed() && again.failed.empty(), "a second run changes nothing");

    // An interrupted earlier repair: the renamed file exists and the
    // original is back (a package extracted over the repaired one).
    WriteFile(dir / "dpx.dll", "dll");
    WriteFile(dir / "dpx.la", LaFor("dpx.dll"));
    CoderModuleRepairResult third = RepairCoderModules(UltraCanvas::PathToUtf8(dir), isSystem);
    Check(third.failed.empty() && !Exists(dir / "dpx.dll") && Exists(dir / "dpx-coder.dll"),
          "a re-extracted original beside the renamed file is removed");
    CheckEq(ReadFile(dir / "dpx.la"), LaFor("dpx-coder.dll"), "and the .la is pointed again");

    CoderModuleRepairResult none = RepairCoderModules(UltraCanvas::PathToUtf8(scratch.path / "missing"), isSystem);
    Check(!none.Changed() && none.failed.empty(), "a missing folder is nothing to do");
}

void TestPlatformEntry() {
    std::printf("Platform entry point\n");
    ScratchFolder scratch;
    const fs::path coders = scratch.path / "lib" / "ImageMagick-7.1.2" / "modules-Q16HDRI" / "coders";
    fs::create_directories(coders);
    WriteFile(coders / "mpr.dll", "dll");
    WriteFile(coders / "mpr.la", LaFor("mpr.dll"));
    WriteFile(coders / "dpx.dll", "dll");
    WriteFile(coders / "dpx.la", LaFor("dpx.dll"));
    CoderModuleRepairResult r = RepairPackagedCoderModules(UltraCanvas::PathToUtf8(scratch.path));
#if defined(_WIN32) || defined(_WIN64)
    Check(r.failed.empty(), "Windows: nothing failed");
    Check(!Exists(coders / "mpr.dll") && !Exists(coders / "mpr.la"), "Windows: the mpr coder is gone");
    Check(Exists(coders / "dpx-coder.dll"), "Windows: the dpx coder is renamed");
    Check(NativeIsSystemDllName("kernel32.dll"), "Windows: System32 holds kernel32.dll");
    Check(!NativeIsSystemDllName("png.dll"), "Windows: and no png.dll");
#else
    Check(!r.Changed() && r.failed.empty(), "no Windows: nothing is touched");
    Check(Exists(coders / "mpr.dll") && Exists(coders / "dpx.dll"), "no Windows: the files stay");
    Check(!NativeIsSystemDllName("mpr.dll"), "no Windows: no system directory to ask");
#endif
}

} // namespace

int main() {
    std::printf("=== UltraCanvasCoderModuleRepair ===\n");
    TestNames();
    TestLibtoolRewrite();
    TestRepair();
    TestPlatformEntry();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
