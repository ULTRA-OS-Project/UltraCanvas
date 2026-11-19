- **CI: the chat-network (UltraIM) tests run on the macOS and Windows rows,
  and the Signal provider's daemon process has a test that runs on
  Windows.** Every UltraIM suite was built on all six rows and run on the
  two Linux ones only, so what is written per platform was run where it
  cannot differ - and the Windows half of `SignalDaemon`, which hands the
  proxy to signal-cli through the child's environment, had been compiled
  and never run.
  - **`SignalDaemonTests`** (new, `Tests/UltraIM/`): `SignalDaemon` on its
    own, with the test as its own child program (no python). The variables
    handed to `Start` arrive exactly - Java options with spaces, quotes,
    `|`, `&`, Thai - one the environment already has is replaced and not
    doubled (whatever its spelling on Windows), the rest is kept; arguments
    arrive exactly (a folder with spaces and a trailing backslash, quotes,
    an empty one, Thai); output and error are appended to a log named in
    Thai; the same through a wrapper script as signal-cli is one (a `.bat`
    run by `cmd.exe`, a shell script elsewhere); `Stop` ends the script and
    the program it started; a program that ends by itself, and one that is
    not there. 72 checks on Linux; the Windows build of it, run under Wine
    here, 73 - and each of eight deliberate breaks of the two branches
    fails it.
  - **Two things it found in the Windows branch**, both corrected:
    a process that had ended by itself kept its id and its handles until
    the next start (`ProcessId()` went on naming it, where POSIX reports 0),
    and a `.bat` was run by a bare `cmd.exe`, which Windows looks for in the
    program's own folder and the current one before the system's - it is
    named by its full path now (`GetSystemDirectoryW`).
  - **The build workflow** (`.github/workflows/build.yml`, "Run the
    chat-network tests (macOS/Windows)"): `UltraIMTests`,
    `MatrixCryptoTests`, `SignalDaemonTests` and `TelegramProviderTests` on
    every Windows and macOS row; `TelegramTdlibTests` where the packages
    ship a TDLib (Windows x64, macOS arm64 - there no library is a failure,
    not a skip); `DirectProviderTests` on Windows, for its envelopes and
    store; and on the Apple silicon row the suites that sign in to a python
    stand-in - Signal, IRC, XMPP, Mastodon, Mattermost, Zulip - with a
    missing python3 or openssl a failure. Each suite is one `ctest` call, so
    a name that matches nothing is an error. Not there: Windows for the
    suites that fork (they print "skipped"), and the Intel macOS row for
    the ones that wait on clocks (its runner is five to ten times slower).
    `TelegramProviderTests` may take 300 s like the other providers' suites.
