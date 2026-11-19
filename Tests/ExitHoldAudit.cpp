// Tests/ExitHoldAudit.cpp
// A glibc audit library (LD_AUDIT) that holds the process for a moment right
// after libgomp's finaliser has run during exit.
//
// glibc's exit runs each library's finaliser and marks the library
// uninitialised; a dlopen from another thread after that runs the library's
// constructor again. For libgomp that aborts: its OpenACC runtime registers
// the host device a second time - "goacc_register: Assertion
// `!dispatchers[disp->type]' failed". A thread still loading something that
// needs libgomp when the program exits (the framework's ImageMagick warm-up,
// whose coders do) hits that only when its dlopen falls in the microseconds
// between libgomp's finaliser and the end of the process: ClipboardTargetsTest
// aborted that way on the Linux x86_64 row once in many runs. _dl_fini tells
// an audit library about each object right after calling its finaliser
// (la_objclose), so holding there opens the window every time, and a test run
// with this library fails on every exit that still has such a thread.
// ExitDuringWarmUpTest is run with it.
// Version: 1.0.0
// Last Modified: 2026-11-19
// Author: UltraCanvas Framework

#include <link.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

extern "C" {

unsigned int la_version(unsigned int version) {
    return version;
}

unsigned int la_objopen(struct link_map* map, Lmid_t, uintptr_t* cookie) {
    *cookie = reinterpret_cast<uintptr_t>(map);
    return 0;
}

unsigned int la_objclose(uintptr_t* cookie) {
    const auto* map = reinterpret_cast<const struct link_map*>(*cookie);
    if (map && map->l_name && std::strstr(map->l_name, "libgomp")) usleep(300000);
    return 0;
}

} // extern "C"
