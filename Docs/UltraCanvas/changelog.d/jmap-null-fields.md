- **The UltraNet JMAP plug-in no longer aborts on a null field.** Fastmail
  answers `Email/get` with `"subject": null`, `"to": null`, `"cc": null`
  and `"name": null` for a draft without a subject or recipients, and
  nlohmann's `value()` throws on a null where the key exists (its default
  is for a missing key only); the uncaught `type_error` took UltraMail
  down with SIGABRT in the sync thread. Every string and number the core
  reads from a server now goes through null-safe helpers (`Str`, `Uint`
  in `JmapCore.h`): a null, a wrong type or a missing key reads as empty
  or zero, and an out-of-range number is clamped. The crash report's
  response is a test.
