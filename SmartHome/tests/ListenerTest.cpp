// SmartHome/tests/ListenerTest.cpp
// Several listeners per notification: the SetOn* slot and any number of
// AddListener listeners all hear the same change, a removed one stops
// hearing, and removal is safe from inside a callback and against a
// notifying thread. A room change is the notification used - CreateRoom
// fires OnHomeChange on the calling thread.
//
// Author: UltraCanvas Framework
#include "UltraCanvasSmartHome.h"
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

using namespace UltraCanvas::SmartHome;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static int roomNumber = 0;
static void ChangeHome() {
    SmartHomeRoom room;
    room.Name = "Room " + std::to_string(++roomNumber);   // locale-ok: test data
    SMARTHOME_API.CreateRoom(room);
}

int main() {
    if (!SMARTHOME_API.Initialize()) { std::puts("FAIL: Initialize"); return 1; }

    // 1. The slot and two listeners all hear one change.
    std::atomic<int> slotCalls{0}, aCalls{0}, bCalls{0};
    SMARTHOME_API.SetOnHomeChange([&] { ++slotCalls; });
    SmartHomeListener a; a.HomeChange = [&] { ++aCalls; };
    SmartHomeListener b; b.HomeChange = [&] { ++bCalls; };
    const SmartHomeListenerId aId = SMARTHOME_API.AddListener(a);
    const SmartHomeListenerId bId = SMARTHOME_API.AddListener(b);
    Check(aId != 0 && bId != 0 && aId != bId, "AddListener hands out distinct non-zero ids");
    ChangeHome();
    Check(slotCalls == 1 && aCalls == 1 && bCalls == 1, "slot and both listeners called once");

    // 2. A listener without the notification's member is skipped, not called empty.
    SmartHomeListener other; other.DeviceUpdate = [](const std::string&) {};
    const SmartHomeListenerId otherId = SMARTHOME_API.AddListener(other);
    ChangeHome();
    Check(aCalls == 2 && bCalls == 2, "a listener for other notifications does not get in the way");
    SMARTHOME_API.RemoveListener(otherId);

    // 3. Removing one leaves the others; clearing the slot leaves the listeners.
    SMARTHOME_API.RemoveListener(aId);
    SMARTHOME_API.SetOnHomeChange(nullptr);
    ChangeHome();
    Check(aCalls == 2 && bCalls == 3 && slotCalls == 2, "removed listener and cleared slot stop, the rest go on");
    SMARTHOME_API.RemoveListener(aId);   // twice, and an id never issued: no-ops
    SMARTHOME_API.RemoveListener(987654321);
    Check(true, "removing an unknown id is a no-op");

    // 4. A listener removing itself from inside its own callback (no deadlock),
    //    and hearing nothing after.
    std::atomic<int> selfCalls{0};
    SmartHomeListenerId selfId = 0;
    SmartHomeListener self;
    self.HomeChange = [&] { ++selfCalls; SMARTHOME_API.RemoveListener(selfId); };
    selfId = SMARTHOME_API.AddListener(self);
    ChangeHome();
    ChangeHome();
    Check(selfCalls == 1, "a listener can remove itself from inside its callback");

    // 5. A listener added from inside a callback is in place for the next change.
    std::atomic<int> lateCalls{0};
    SmartHomeListenerId lateId = 0, adderId = 0;
    SmartHomeListener adder;
    adder.HomeChange = [&] {
        if (lateId == 0) {
            SmartHomeListener late; late.HomeChange = [&] { ++lateCalls; };
            lateId = SMARTHOME_API.AddListener(late);
        }
    };
    adderId = SMARTHOME_API.AddListener(adder);
    ChangeHome();
    ChangeHome();
    Check(lateCalls >= 1, "a listener added during a notification hears the next one");
    SMARTHOME_API.RemoveListener(adderId);
    SMARTHOME_API.RemoveListener(lateId);
    SMARTHOME_API.RemoveListener(bId);

    // 6. Across threads: once RemoveListener returns, the callback is not
    //    running and is never called again, while another thread notifies.
    std::atomic<bool> stop{false};
    std::thread notifier([&] { while (!stop) ChangeHome(); });
    bool raceOk = true;
    for (int round = 0; round < 200 && raceOk; ++round) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        std::atomic<int> callsAfterRemove{0};
        SmartHomeListener l;
        l.HomeChange = [alive, &callsAfterRemove] {
            if (!*alive) ++callsAfterRemove;
            std::this_thread::yield();
        };
        const SmartHomeListenerId id = SMARTHOME_API.AddListener(l);
        std::this_thread::yield();
        SMARTHOME_API.RemoveListener(id);
        *alive = false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (callsAfterRemove != 0) raceOk = false;
    }
    stop = true;
    notifier.join();
    Check(raceOk, "no callback runs after RemoveListener returns (200 rounds against a notifying thread)");

    SMARTHOME_API.Shutdown();
    std::printf("\n%s - %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
