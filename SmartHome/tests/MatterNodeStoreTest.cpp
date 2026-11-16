// SmartHome/tests/MatterNodeStoreTest.cpp
// The list of Matter nodes a controller commissioned, kept between runs
// (protocols/Matter/MatterNodeStore.h). No connectedhomeip needed, so it runs
// on every build, under whatever locale the test is started in.
//
// Author: UltraCanvas Framework
#include "MatterNodeStore.h"
#include "UltraCanvasPathUtf8.h"

#include <clocale>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace UltraCanvas::SmartHome;

static int failures = 0;

static void Check(bool condition, const char* what) {
    if (condition) { std::printf("ok  %s\n", what); }
    else           { std::printf("FAIL %s\n", what); ++failures; }
}

int main() {
    std::setlocale(LC_ALL, "");   // a comma-decimal locale must not change the file

    // ---- the text form ----
    MatterNodeStoreData data;
    data.NextNodeId = 0x102;
    data.Nodes.push_back({0x100, {{0, {0x16}}, {1, {0x74, 0x11}}}});
    data.Nodes.push_back({0x101, {{0, {0x16}}, {1, {0x10d}}}});
    const std::string text = FormatMatterNodeStore(data);
    Check(text.find("next-node-id 0x102\n") != std::string::npos, "the counter is written in hex");
    Check(text.find("node 0x100 0=0x16 1=0x74,0x11\n") != std::string::npos,
          "a node is its id and each endpoint's device types");

    MatterNodeStoreData back;
    std::string error;
    Check(ParseMatterNodeStore(text, back, error), "what was written reads back");
    Check(back.NextNodeId == 0x102 && back.Nodes.size() == 2, "counter and both nodes");
    Check(back.Nodes[0].NodeId == 0x100 && back.Nodes[0].DeviceTypes[1] == std::vector<uint32_t>{0x74, 0x11},
          "the first node's types, in order");
    Check(back.Nodes[1].DeviceTypes[1] == std::vector<uint32_t>{0x10d}, "the second node's types");

    // ---- what is not a node list is refused and leaves the data alone ----
    for (const char* bad : {"node 256 1=0x74\n", "node 0x100 1=0x74;0x11\n", "node 0x100 one=0x74\n",
                            "next-node-id\n", "node 0x100\nnode 0x100\n", "devices 3\n", "node 0x0\n"}) {
        MatterNodeStoreData kept = back;
        std::string why;
        const bool parsed = ParseMatterNodeStore(bad, kept, why);
        Check(!parsed && !why.empty() && kept.Nodes.size() == 2, bad);
    }
    {
        MatterNodeStoreData empty;
        Check(ParseMatterNodeStore("# only a comment\n\n", empty, error) && empty.Nodes.empty(),
              "comments and blank lines are an empty list");
        Check(ParseMatterNodeStore("node 0x100 1=0x74\r\n", empty, error) && empty.Nodes.size() == 1,
              "CRLF line ends read too");
    }

    // ---- the next id never reuses one on the fabric ----
    Check(NextFreeMatterNodeId(MatterNodeStoreData{}) == kFirstMatterNodeId, "an empty list starts at 0x100");
    Check(NextFreeMatterNodeId(data) == 0x102, "the counter is honoured");
    {
        MatterNodeStoreData behind = data;
        behind.NextNodeId = 0x100;   // a counter that fell behind (an older file, a crash)
        Check(NextFreeMatterNodeId(behind) == 0x102, "never an id a stored node holds");
    }

    // ---- on disk ----
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "uc-matter-node-store-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const std::string path = MatterNodeStorePath(UltraCanvas::PathToUtf8(directory));
    Check(path.find("ultracanvas-matter-nodes.txt") != std::string::npos, "the file sits in the storage directory");

    MatterNodeStoreData loaded;
    loaded.NextNodeId = 7;
    Check(LoadMatterNodeStore(path, loaded, error) && loaded.Nodes.empty() && loaded.NextNodeId == 0,
          "a missing file is an empty list");
    Check(SaveMatterNodeStore(path, data, error), "saves");
    Check(!std::filesystem::exists(UltraCanvas::PathFromUtf8(path + ".tmp")), "no temporary file is left");
    Check(LoadMatterNodeStore(path, loaded, error) && loaded.Nodes.size() == 2 && loaded.NextNodeId == 0x102,
          "and loads back");
    data.Nodes.erase(data.Nodes.begin());
    Check(SaveMatterNodeStore(path, data, error) && LoadMatterNodeStore(path, loaded, error) &&
          loaded.Nodes.size() == 1 && loaded.Nodes[0].NodeId == 0x101,
          "a removed node stays removed after the next save");
    {
        std::ofstream(UltraCanvas::PathFromUtf8(path), std::ios::trunc) << "garbage\n";
        MatterNodeStoreData kept = loaded;
        Check(!LoadMatterNodeStore(path, kept, error) && error.find(path) != std::string::npos,
              "a damaged file is refused, naming it");
    }
    std::filesystem::remove_all(directory);

    if (failures) { std::printf("%d check(s) FAILED\n", failures); return 1; }
    std::printf("PASS - the Matter node list survives a restart\n");
    return 0;
}
