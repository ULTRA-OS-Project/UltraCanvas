// MatterNodeStore.h
// The Matter nodes this controller commissioned, kept between runs.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
//
// connectedhomeip keeps the controller's fabric - its keys and certificates -
// in its own ini file, but not which nodes it commissioned: that is the
// application's to remember (chip-tool leaves it to whoever types the node id).
// Without this list every start forgot every device, and node ids were handed
// out from the same first number again, so a new device could be given the id
// of one still on the fabric.
//
// The file is a program's own internal state, written next to the SDK's ini
// (the storage directory, or $TMPDIR / /tmp as the SDK does without one):
//
//   # UltraCanvas SmartHome - Matter nodes commissioned by this controller
//   next-node-id 0x102
//   node 0x100 0=0x16 1=0x74,0x11
//   node 0x101 0=0x16 1=0x10d
//
// A node line is its node id and, per endpoint, the device types it declared
// (MatterDeviceTypes.h makes the category from them). Numbers are hex, read
// and written without the locale. Saving writes a temporary file and renames
// it over the old one, so a crash leaves the previous list, not half a list.
//
// No connectedhomeip here: tests/MatterNodeStoreTest.cpp runs on every build.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {

struct MatterStoredNode {
    uint64_t NodeId = 0;
    std::map<uint16_t, std::vector<uint32_t>> DeviceTypes;   // endpoint -> device type ids
};

struct MatterNodeStoreData {
    uint64_t NextNodeId = 0;                 // 0: not recorded yet
    std::vector<MatterStoredNode> Nodes;     // in the order they were commissioned
};

// The first operational node id this controller gives a device. Its own id
// (112233, chip-tool's) is far above, so the two cannot meet in practice.
constexpr uint64_t kFirstMatterNodeId = 0x100;

// Where the list lives for a storage directory ("" = the SDK's default).
std::string MatterNodeStorePath(const std::string& storageDirectory);

std::string FormatMatterNodeStore(const MatterNodeStoreData& data);

// False with `error` naming the line for text that is not a node list;
// `data` is then left as it was.
bool ParseMatterNodeStore(const std::string& text, MatterNodeStoreData& data, std::string& error);

// A missing file is an empty list (true). Paths are UTF-8.
bool LoadMatterNodeStore(const std::string& path, MatterNodeStoreData& data, std::string& error);
bool SaveMatterNodeStore(const std::string& path, const MatterNodeStoreData& data, std::string& error);

// The id the next commissioning gets: past the recorded counter, past every
// stored node, and never below kFirstMatterNodeId.
uint64_t NextFreeMatterNodeId(const MatterNodeStoreData& data);

} // namespace SmartHome
} // namespace UltraCanvas
