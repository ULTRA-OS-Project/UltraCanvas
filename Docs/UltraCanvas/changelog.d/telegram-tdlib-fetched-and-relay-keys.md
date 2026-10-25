- **The Telegram provider ships with TDLib.** `libtdjson` no longer has to
  be found on the machine: the configure step fetches TDLib 1.8.69 prebuilt
  (the pinned `tdjson` wheel, checked by SHA-256; Linux x86_64 and aarch64,
  macOS arm64, Windows x64) into `Plugins/UltraIM/tdlib/`, the packages ship
  that folder, and `TelegramTdlibTests` runs against it by default.
  `ULTRAIM_TDJSON_FILE` ships a library of your own, `ULTRAIM_FETCH_TDLIB=OFF`
  skips the download, and a platform with no prebuilt one says so.
- **A provider's service can keep another provider's sign-in keys.**
  `UltraIM_FetchSharedCredentials(provider, forProvider, cb)` and
  `IUltraIMProviderPlugin::FetchSharedCredentials`: ChatCube Direct asks its
  relay for the Telegram application's API ID and hash
  (`GET /v1/telegram-credentials`), so they are set on the server once.
- **A Telegram sign-in no longer hangs in silence.** TDLib answers a step
  only once Telegram's servers have, and with no network it kept trying
  without a word while the application waited forever. The provider now
  reports TDLib's connection state while it signs in, and a step TDLib does
  not answer within 45 s fails with `Timeout` saying what TDLib was doing.
  What is sent and received goes to the framework's debug stream (types and
  errors, never a number, code, password or message), TDLib's own log to
  `telegram/tdlib.log` in the data directory, and on Windows the wheel's
  OpenSSL and zlib DLLs next to `tdjson.dll` are found when it is loaded.
