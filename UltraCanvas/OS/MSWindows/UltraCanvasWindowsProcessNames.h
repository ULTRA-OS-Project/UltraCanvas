// OS/MSWindows/UltraCanvasWindowsProcessNames.h
// The executable name of every process, from a Toolhelp snapshot: no handle
// to the process and no elevation needed, so a service another user runs is
// "AvastSvc" rather than "pid 4720". Shared by the IP Helper backend (one
// refresh per snapshot) and the kernel network ETW source (a refresh when a
// PID it has not seen turns up, at most every couple of seconds). Path and
// user still need a handle, and therefore elevation.
//
// Internal to the Windows NetworkMonitor sources; not a public header.
//
// Version: 0.8.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>

namespace UltraCanvas {

// Re-reads the process list now. Thread-safe.
void NetworkMonitor_WindowsRefreshProcessNames();

// The executable's name for the PID, ".exe" stripped, or empty when the list
// does not have it. With refreshOnMiss, a PID the list lacks re-reads the
// list first, unless it was read within the last two seconds - a new
// process is the usual reason for a miss, a churn of them the reason for
// the limit. Thread-safe.
std::string NetworkMonitor_WindowsProcessName(uint32_t pid, bool refreshOnMiss);

} // namespace UltraCanvas
