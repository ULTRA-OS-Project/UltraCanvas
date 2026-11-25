# UltraCanvasUnixSocketAddress

The address of a Unix domain socket at a path **of any length** (Linux, macOS,
the BSDs; the header is empty on Windows).

A socket's address (`sockaddr_un`) holds a path of 107 bytes on Linux and 103
on macOS and the BSDs. The sockets a program makes for itself lie under
folders whose length it does not choose - the user's runtime folder, a
virtual machine's own - and a longer path could not be bound or connected at
all. `UnixSocketAddress` fills the address for a path that fits, and on Linux
reaches a longer one **through its folder**: the folder is opened, which
takes a path of any length, and the socket named as
`/proc/self/fd/<n>/<file>`, which the kernel resolves to the same file. The
socket stays where its path says; only the way to name it is shorter.

```cpp
#include "UltraCanvasUnixSocketAddress.h"

UltraCanvas::UnixSocketAddress address;
std::string error;
if (!address.Resolve(socketPath, error)) {
    // a path too long on macOS / the BSDs, or a folder that cannot be opened
    return false;
}
const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
if (::connect(fd, address.Get(), address.Size()) != 0) {   // or bind()
    ::close(fd);
    return false;
}
```

<!-- doc-check: std::string socketPath -->

The address keeps the folder open, so it lives until `bind()` or `connect()`
has returned. The folder must exist: a server makes it before it resolves the
address, and a client that waits for a server reads `folderMissing` as "not
yet".

## API

| Call | Meaning |
|---|---|
| `UnixSocketAddress::Fits(path)` | Whether `path` can be an address as it is (shorter than `sun_path`). |
| `Resolve(path, error, folderMissing = nullptr)` | Fills the address. False with `error` when the path is too long and cannot be reached through its folder: on macOS and the BSDs always, on Linux when the folder cannot be opened (`*folderMissing` is set when it is simply not there), when the file's own name is longer than about 80 bytes, or when `/proc` is not mounted. |
| `LastFailure()`, `FolderError()` | Why the last `Resolve` answered false, for a caller that words the failure itself (UltraMessage does, in the person's language): `Failure::TooLong`, `FolderNotOpened` (with the `errno` in `FolderError()`), `NoProc`; `NoFailure` after a success. |
| `Get()`, `Size()` | The `sockaddr*` and its length for `bind()` / `connect()`. |

## When another program makes the socket

A `/proc/self/fd/<n>/...` name means something only in the process that
opened the folder, so it cannot be handed to a program this one starts (QEMU,
virtiofsd). There, start the other program **in** the socket's folder and
give it the file's name alone - a relative name is resolved against the
folder the program runs in - and keep every other path it gets absolute.
`Fits()` says when that is needed. UltraWin starts its machine that way
(`core/UltraWin/UltraWinVm.cpp`), and reaches the control socket QEMU made
with `Resolve`.

## Who uses it

UltraMessage's transport (the bus under `$XDG_RUNTIME_DIR`) and UltraWin's
QMP client. The header has nothing of the framework in it, so the modules
that do not link the core can include it.
