// Apps/MobilePC/MobilePCHostedApps.h
// The applications MobilePC carries: registered with UltraCanvasHostedApps,
// so UltraDesktop's Apps window lists them and starts them in this process.
// Version: 0.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

namespace MobilePC {

    // Registers every application this build carries (each one only when
    // its sources were compiled in: MOBILEPC_WITH_<APP>).
    void RegisterHostedApps();

} // namespace MobilePC
