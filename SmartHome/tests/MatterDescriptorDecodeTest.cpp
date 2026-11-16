// SmartHome/tests/MatterDescriptorDecodeTest.cpp
// Built only when ULTRACANVAS_SMARTHOME_MATTER=ON.
//
// After commissioning, the Matter backend asks a node what it is: one read of
// every endpoint's Descriptor DeviceTypeList. This test feeds the decoder
// (protocols/Matter/MatterDescriptorDecode.h) reports written by
// connectedhomeip's own TLV encoder - a whole list, the chunked form a long
// list arrives in, several endpoints, and data that is not a list - and
// checks the ids that come out, then the category they make
// (MatterDeviceTypes.h). No network or device is involved; the read that
// carries these reports over the air is not covered here.
//
// Author: UltraCanvas Framework

#include "MatterDescriptorDecode.h"
#include "MatterDeviceTypes.h"

#include <lib/core/TLVWriter.h>

#include <cstdio>
#include <map>
#include <vector>

using namespace UltraCanvas::SmartHome;
namespace Descriptor = chip::app::Clusters::Descriptor;
using ListOperation = chip::app::ConcreteDataAttributePath::ListOperation;
using Types = std::map<uint16_t, std::vector<uint32_t>>;

static int failures = 0;

static void Check(bool condition, const char* what) {
    if (condition) { std::printf("ok  %s\n", what); }
    else           { std::printf("FAIL %s\n", what); ++failures; }
}

static Descriptor::Structs::DeviceTypeStruct::Type Entry(uint32_t deviceType) {
    Descriptor::Structs::DeviceTypeStruct::Type entry;
    entry.deviceType = deviceType;
    entry.revision = 1;
    return entry;
}

static chip::app::ConcreteDataAttributePath PathFor(uint16_t endpoint, ListOperation op) {
    chip::app::ConcreteDataAttributePath path(endpoint, Descriptor::Id, Descriptor::Attributes::DeviceTypeList::Id);
    path.mListOp = op;
    return path;
}

// Encodes `value` the way an attribute report carries it and hands the
// decoder a reader positioned on it.
template <typename T>
static bool Report(const chip::app::ConcreteDataAttributePath& path, const T& value, Types& types) {
    uint8_t buffer[512];
    chip::TLV::TLVWriter writer;
    writer.Init(buffer);
    if (chip::app::DataModel::Encode(writer, chip::TLV::AnonymousTag(), value) != CHIP_NO_ERROR) return false;
    if (writer.Finalize() != CHIP_NO_ERROR) return false;
    chip::TLV::TLVReader reader;
    reader.Init(buffer, writer.GetLengthWritten());
    if (reader.Next() != CHIP_NO_ERROR) return false;
    return ApplyDeviceTypeListReport(path, reader, types);
}

static bool Report(const chip::app::ConcreteDataAttributePath& path,
                   const std::vector<Descriptor::Structs::DeviceTypeStruct::Type>& list, Types& types) {
    chip::app::DataModel::List<const Descriptor::Structs::DeviceTypeStruct::Type> span(list.data(), list.size());
    return Report(path, span, types);
}

int main() {
    // ---- a whole list, as a short one arrives ----
    {
        Types types;
        Check(Report(PathFor(1, ListOperation::ReplaceAll), {Entry(0x0074), Entry(0x0011)}, types),
              "a whole DeviceTypeList decodes");
        Check(types[1] == std::vector<uint32_t>{0x0074, 0x0011}, "its ids, in order (robot vacuum, power source)");
    }

    // ---- the chunked form: ReplaceAll with an empty list, then appends ----
    {
        Types types;
        bool ok = Report(PathFor(1, ListOperation::ReplaceAll),
                         std::vector<Descriptor::Structs::DeviceTypeStruct::Type>{}, types);
        ok = Report(PathFor(1, ListOperation::AppendItem), Entry(0x0073), types) && ok;
        ok = Report(PathFor(1, ListOperation::AppendItem), Entry(0x0011), types) && ok;
        Check(ok, "a chunked DeviceTypeList decodes");
        Check(types[1] == std::vector<uint32_t>{0x0073, 0x0011}, "the appends make up the list (laundry washer)");
    }

    // ---- a ReplaceAll after appends starts the endpoint's list over ----
    {
        Types types;
        Report(PathFor(2, ListOperation::AppendItem), Entry(0x0100), types);
        Report(PathFor(2, ListOperation::ReplaceAll), {Entry(0x0101)}, types);
        Check(types[2] == std::vector<uint32_t>{0x0101}, "ReplaceAll replaces what came before");
    }

    // ---- a wildcard read: one report per endpoint ----
    Types node;
    Report(PathFor(0, ListOperation::ReplaceAll), {Entry(0x0016)}, node);                // root node
    Report(PathFor(1, ListOperation::ReplaceAll), {Entry(0x0070)}, node);                // refrigerator
    Report(PathFor(2, ListOperation::ReplaceAll), {Entry(0x0071)}, node);                // its cabinets
    Report(PathFor(3, ListOperation::ReplaceAll), {Entry(0x0071)}, node);
    Check(node.size() == 4, "every endpoint is kept apart");
    std::vector<uint32_t> all;
    for (const auto& [endpoint, list] : node) all.insert(all.end(), list.begin(), list.end());
    Check(all == std::vector<uint32_t>{0x0016, 0x0070, 0x0071, 0x0071}, "endpoint order is kept");
    Check(MatterCategoryForDeviceTypes(all) == UltraCanvas::SmartHome::SmartHomeDeviceCategory::Appliance,
          "the node is an appliance");

    // ---- what is not a DeviceTypeList is refused, not half-read ----
    {
        Types types;
        types[1] = {0x0100};
        Check(!Report(PathFor(1, ListOperation::ReplaceAll), uint32_t{42}, types),
              "a number where the list should be is refused");
        Check(types[1] == std::vector<uint32_t>{0x0100}, "and leaves the endpoint's types as they were");
        Check(!Report(PathFor(1, ListOperation::DeleteItem), Entry(0x0100), types),
              "a single-item delete is not part of a read and is refused");
    }

    if (failures) { std::printf("%d check(s) FAILED\n", failures); return 1; }
    std::printf("PASS - DeviceTypeList reports decode with connectedhomeip's TLV\n");
    return 0;
}
