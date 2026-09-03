#### 2026-09-03 *0.2.0*
- **Failures are now reported instead of swallowed.** UltraMail used to fail
  silently almost everywhere: a rejected password, an untrusted certificate, an
  unreachable server, a database that would not open, an attachment that could
  not be written — each ended in a bare `return`, so the app simply appeared to
  do nothing. The diagnosis already existed in `UltraNetResult::message`,
  `UltraDbResult::message` and `SyncOutcome::message`; it was being dropped at
  the UI boundary. Every one of those paths now raises an alert that names the
  cause. This delivers the error-handling contract in `Concept.md`.
- Alerts are `UltraCanvasAlert` (`UltraCanvas/include/UltraCanvasAlert.h`), so a
  failure carries an Error severity and icon rather than the Information dialog
  a failed send used to show. The one-line summary goes in the alert's message
  and the underlying diagnostic in its `details` line.
- New `ui/UltraMailAlerts.{h,cpp}`: `FriendlyMessage()` maps the UltraNet result
  codes a mail client actually hits — `AuthenticationFailed`,
  `TlsCertificateInvalid`, `TlsCertificateExpired`, `HostNotFound`,
  `ConnectionRefused`, `PluginNotFound` and the rest — to text a user can act
  on, and `IsRetryable()` decides when a Retry button is offered.
- A failed send now offers **Retry**, which re-flushes the outbox, rather than
  only stating that the message was queued.
- Input is validated where it is entered: sending with no recipient, adding an
  account with an empty or malformed address, and saving a nameless contact each
  explain what is wrong instead of discarding what was typed. New pure helper
  `LooksLikeEmailAddress()` beside `EmailDomain()` / `EmailLocalPart()` in the
  discovery engine.
- Background sync failures surface. `RunDueSyncs()` discarded its `SyncOutcome`
  entirely, so a wrong password or an expired certificate meant mail silently
  never arrived. The outcome is now reported once per run of failures — not on
  every timer tick — and re-arms when a sync succeeds again.
- A startup failure shows an alert naming the data folder and the database error
  before exiting, instead of terminating with no window and no message.
- `Outbox::FlushStats` carries `lastFailure` (the `UltraNetResult` of the most
  recent failed send) so the UI can say *why* a message stayed in the queue; the
  reason was already persisted as the outbox row's `last_error` but was never
  read back. `OutboxStore::IsOpen()` added to mirror `LocalStore` /
  `ContactStore`, so "the queue is unavailable" is distinguishable from "the
  queue is empty".
- `UltraMailApp::Initialize()` takes an optional `outError` and records why the
  contacts / outbox stores failed to open, so the Contacts button reports the
  problem rather than doing nothing.

#### 2026-08-31 *0.1.0*
- **UltraMail keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the mail client (`Apps/UltraMail`) is described here and
  carries this file's version, and UltraMail no longer moves when the framework
  releases.
- A framework change UltraMail needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
