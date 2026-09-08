// core/SmartHomeProtocolRegistry.cpp
// Protocol backend factories: the bridge between "enable Zigbee" in an
// application and a concrete ZigbeeProtocol object.
//
// ISmartHomeProtocol.h has declared RegisterProtocolFactory /
// UnregisterProtocolFactory / CreateSmartHomeProtocol since the module was
// written, but nothing ever defined them, so any caller would have failed to
// link. They are defined here, together with the list of backends compiled
// into this build.
//
// Which backends exist is decided by CMake, one ULTRACANVAS_SMARTHOME_<NAME>
// definition per enabled backend. With none defined this file still compiles
// and RegisterBuiltinProtocols() simply reports zero — which is the state of
// the module until the vendor SDKs are vendored.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHome.h"
#include "ISmartHomeProtocol.h"

#include <map>
#include <mutex>

#ifdef ULTRACANVAS_SMARTHOME_MATTER
#include "MatterProtocol.h"
#endif
#ifdef ULTRACANVAS_SMARTHOME_THREAD
#include "ThreadProtocol.h"
#endif
#ifdef ULTRACANVAS_SMARTHOME_ZIGBEE
#include "ZigbeeProtocol.h"
#endif
#ifdef ULTRACANVAS_SMARTHOME_ZWAVE
#include "ZWaveProtocol.h"
#endif
#ifdef ULTRACANVAS_SMARTHOME_KNX
#include "KNXProtocol.h"
#endif

namespace UltraCanvas {
namespace SmartHome {

namespace {

// Guarded by its own mutex: factories are registered during start-up but may
// also be replaced later by an application substituting its own backend.
std::map<SmartHomeProtocolType, SmartHomeProtocolFactory>& FactoryTable() {
    static std::map<SmartHomeProtocolType, SmartHomeProtocolFactory> table;
    return table;
}

std::mutex& FactoryMutex() {
    static std::mutex m;
    return m;
}

}  // namespace

void RegisterProtocolFactory(SmartHomeProtocolType type,
                             SmartHomeProtocolFactory factory) {
    if (!factory) {
        return;
    }
    std::lock_guard<std::mutex> lock(FactoryMutex());
    FactoryTable()[type] = std::move(factory);
}

void UnregisterProtocolFactory(SmartHomeProtocolType type) {
    std::lock_guard<std::mutex> lock(FactoryMutex());
    FactoryTable().erase(type);
}

std::shared_ptr<ISmartHomeProtocol> CreateSmartHomeProtocol(SmartHomeProtocolType type) {
    SmartHomeProtocolFactory factory;
    {
        std::lock_guard<std::mutex> lock(FactoryMutex());
        auto it = FactoryTable().find(type);
        if (it == FactoryTable().end()) {
            return nullptr;
        }
        factory = it->second;
    }
    // Called with the lock released: a backend constructor may probe hardware,
    // and must not be able to deadlock against another registration.
    return factory ? factory() : nullptr;
}

int RegisterBuiltinProtocols() {
    int count = 0;
#ifdef ULTRACANVAS_SMARTHOME_MATTER
    RegisterProtocolFactory(SmartHomeProtocolType::Matter, &CreateMatterProtocol);
    ++count;
#endif
#ifdef ULTRACANVAS_SMARTHOME_THREAD
    RegisterProtocolFactory(SmartHomeProtocolType::Thread, &CreateThreadProtocol);
    ++count;
#endif
#ifdef ULTRACANVAS_SMARTHOME_ZIGBEE
    RegisterProtocolFactory(SmartHomeProtocolType::Zigbee, &CreateZigbeeProtocol);
    ++count;
#endif
#ifdef ULTRACANVAS_SMARTHOME_ZWAVE
    RegisterProtocolFactory(SmartHomeProtocolType::ZWave, &CreateZWaveProtocol);
    ++count;
#endif
#ifdef ULTRACANVAS_SMARTHOME_KNX
    RegisterProtocolFactory(SmartHomeProtocolType::KNX, &CreateKNXProtocol);
    ++count;
#endif
    return count;
}

}  // namespace SmartHome
}  // namespace UltraCanvas
