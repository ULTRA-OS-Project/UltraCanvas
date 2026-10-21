- **UltraIM: a Telegram provider, and sign-in in steps.**
  `UltraCanvas/Plugins/UltraIM/telegram/` (module `ultraim_telegram`,
  provider `telegram`) is a client of TDLib through its JSON interface,
  **loaded at run time** (`TdJson.h`: `tdlib/` next to the modules,
  `ULTRAIM_TDJSON_PATH`, or the system's libtdjson) - nothing of TDLib is
  linked, so it builds everywhere. Phone-number sign-in with the code and the
  two-step password, registering a new number, TDLib's database encrypted
  with a key the vault keeps, the session resumed; the main chat list,
  private chats and groups, text with replies, edits, deletion, read marks,
  typing, presence, history, people found by username or phone number, new
  groups. The application's API ID and hash come from the user, the build
  (`ULTRAIM_TELEGRAM_API_ID` / `ULTRAIM_TELEGRAM_API_HASH`) or the
  environment. For it UltraIM gains `UltraIMAuthStep`, the result code
  `NeedsInput`, `UltraIMProviderInfo::authStep` / `authHint`, the
  `AuthStepChange` event and the plugin calls `GetAuthStep` / `GetAuthHint`.
  Tests: `Tests/UltraIM/TelegramProviderTests.cpp` against `FakeTdJson` (a
  libtdjson stand-in with a small Telegram in memory) and
  `Tests/UltraIM/TelegramTdlibTests.cpp`, every request handed to the real
  TDLib offline (CI fetches it from the `tdjson` wheel).
