# UltraCanvasNotificationActivation

A click on one of an application's notifications reaching that application -
even when the program that put the notification on screen has quit since, and
even when the application itself is not running. Header:
`UltraCanvas/include/UltraCanvasNotificationActivation.h`.

## Contents

- [Why it is needed](#why-it-is-needed)
- [Registering the activator](#registering-the-activator)
- [The launch string](#the-launch-string)
- [What is written, and how it is removed](#what-is-written-and-how-it-is-removed)
- [API](#api)

## Why it is needed

An application posts its notification on the UltraMessage bus, and a presenter
puts it on screen. On Windows that is a toast under the application's own
AppUserModelID, shown by the program that hosts the broker. While that program
runs, a click on the toast comes back over the bus as
`system.notification.action`. Once it has quit, the toast still waits in the
Action Center - and Windows hands a click on it to the COM activator registered
for the AppUserModelID, starting the program the activator names when it is not
running. Without one, the click did nothing.

Elsewhere the notification service reports the click over the bus while the
application runs; there is nothing to register, and the calls below do nothing.

## Registering the activator

Once, early in the start: when Windows starts the program for a click it waits
for the registration.

```cpp
#include "UltraCanvasNotificationActivation.h"

void RegisterForNotificationClicks() {
    UltraCanvas::RegisterNotificationActivator(
        "org.ultraos.ultramail",
        [](const UltraCanvas::NotificationActivation& click) {
            // On the UI thread. click.actionId: "default" for the toast,
            // a button's id, "" for a toast without a default action.
            const std::string what = click.launch;   // the notification's own note
            (void)what;
        });
}
```

The program is started with `-ToastActivated` (and COM may add `-Embedding`);
`IsNotificationActivationArgument` tells such an argument from a file to open.
One click can arrive both ways - over the bus from a broker host still running,
and through the activator - so an application that opens something for it
opens it once.

## The launch string

`SystemNotification::launch` (UltraMessage) is the application's own note of
what a notification is about. The presenter writes it into the toast's
activation arguments with the notification's id and the action, so the
activator gets it back with nothing on the bus to ask. `EncodeNotificationQuery`
and `DecodeNotificationQuery` make a convenient one:

```cpp
#include "UltraCanvasNotificationActivation.h"

std::string LaunchFor(const std::string& accountId, int64_t uid) {
    return UltraCanvas::EncodeNotificationQuery({{"account", accountId}, {"uid", std::to_string(uid)}});
}

int64_t UidOf(const std::string& launch) {
    const auto fields = UltraCanvas::DecodeNotificationQuery(launch);
    auto uid = fields.find("uid");
    return uid == fields.end() ? 0 : std::stoll(uid->second);
}
```

## What is written, and how it is removed

All per user, under `HKEY_CURRENT_USER`, no administrator rights:

| Key | Written by | What |
|---|---|---|
| `Software\Classes\AppUserModelId\<appId>` | the presenter (`DisplayName`, `IconUri`), the activator (`CustomActivator`) | the toast's name and icon, and the class a click goes to |
| `Software\Classes\CLSID\<clsid>\LocalServer32` | the activator | `"<program>" -ToastActivated` |
| `Software\UltraCanvas\Notifications` | both | one value per AppUserModelID, its CLSID as the data |

The CLSID is derived from the AppUserModelID (`NotificationActivatorClassId`),
so every start of the application names the same class, and its program path is
written again on every start, so a moved installation is followed. The
installer's uninstaller (`Windows/Installer/UltraCanvasSetup.nsi`) walks the
list and removes each AppUserModelID key and CLSID key, then the list;
`RemoveNotificationRegistrations()` does the same from code.

## API

| Function | What it does |
|---|---|
| `RegisterNotificationActivator(appId, onActivated)` | Windows: writes the activator's registration and serves it; `onActivated` runs on the UI thread. False when nothing could be registered, and always elsewhere. |
| `RemoveNotificationRegistrations()` | Windows: removes every identity the list names, and the list. |
| `NotificationActivation` | `notificationId`, `actionId`, `launch`. |
| `FormatNotificationArguments(activation)` / `ParseNotificationArguments(arguments)` | A toast's activation arguments and back; arguments in no form of these are a click on the toast itself. |
| `EncodeNotificationQuery(fields)` / `DecodeNotificationQuery(query)` | `key=value&...`, percent-encoded. |
| `NotificationActivatorClassId(appId)` | The activator's CLSID, `{XXXXXXXX-XXXX-5XXX-...}`. |
| `IsNotificationActivationArgument(argument)` | `-ToastActivated`, `-Embedding`, `/Embedding`. |

Tested in `Tests/UltraMessage/test_adapters.cpp` (the arguments, the CLSID, the
toast XML carrying them) and UltraMail's `test_newmail.cpp` (its launch string).
