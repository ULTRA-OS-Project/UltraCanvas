// MatterDescriptorDecode.h
// One attribute report of a Descriptor cluster's DeviceTypeList, decoded.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
//
// Needs connectedhomeip (only built with ULTRACANVAS_WITH_MATTER). Kept out
// of MatterProtocol.cpp so tests/MatterDescriptorDecodeTest.cpp can feed it
// TLV the SDK's own encoder wrote, with no network and no device.
#pragma once

#include <app/ConcreteAttributePath.h>
#include <app/data-model/Decode.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <lib/core/TLVReader.h>

#include <cstdint>
#include <map>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {

// Applies one report to `types` (endpoint -> device type ids). A list
// arrives whole, or - when it does not fit one report - as a ReplaceAll
// (often empty) followed by an AppendItem per entry. Returns false when the
// data does not decode; `types` is then unchanged for that endpoint.
inline bool ApplyDeviceTypeListReport(const chip::app::ConcreteDataAttributePath& path,
                                      chip::TLV::TLVReader& data,
                                      std::map<uint16_t, std::vector<uint32_t>>& types) {
    namespace Descriptor = chip::app::Clusters::Descriptor;
    using ListOperation = chip::app::ConcreteDataAttributePath::ListOperation;

    if (path.IsListItemOperation()) {
        // Only appends make up a read's chunked list; replacing or deleting
        // single items happens in subscriptions, which this is not used for.
        if (path.mListOp != ListOperation::AppendItem) return false;
        Descriptor::Structs::DeviceTypeStruct::DecodableType item;
        if (chip::app::DataModel::Decode(data, item) != CHIP_NO_ERROR) return false;
        types[path.mEndpointId].push_back(item.deviceType);
        return true;
    }

    Descriptor::Attributes::DeviceTypeList::TypeInfo::DecodableType value;
    if (chip::app::DataModel::Decode(data, value) != CHIP_NO_ERROR) return false;
    std::vector<uint32_t> list;
    auto it = value.begin();
    while (it.Next()) list.push_back(it.GetValue().deviceType);
    if (it.GetStatus() != CHIP_NO_ERROR) return false;
    types[path.mEndpointId] = std::move(list);
    return true;
}

} // namespace SmartHome
} // namespace UltraCanvas
