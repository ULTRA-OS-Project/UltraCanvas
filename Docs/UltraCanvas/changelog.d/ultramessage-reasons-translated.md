- **UltraMessage says why a bus could not be hosted or joined in the person's
  language.** `UltraMsg_Connect`'s `error.message` is what an application
  shows, and the single-instance "already running" alert now puts it on
  screen: a German alert ended in an English clause ("... konnte nicht in den
  Vordergrund geholt werden: the bus folder ... is not this user's alone").
  The fourteen reasons a person can meet go through `Tr` - no broker
  listening or reachable, a broker that cannot be started, the five ways a
  bus folder is not this user's alone, a bus path too long (with and without
  `/proc`), a bus folder that cannot be made or opened, a broker that does
  not confirm the connection - and are in the standard set with drafts in
  all 29 languages. What only a programmer meets (an invalid topic, a missing
  callback, an endpoint that is not connected, the journal's and the system
  calls' errors) stays English.
  - The five folder reasons are whole sentences now, one entry each, so a
    translator has a sentence and not two halves: `the bus folder {0} is not
    this user's alone: it belongs to another user` where it was `... alone
    (it belongs to another user)`.
  - UltraMessage links UltraLocalisation for it (`uc-localisation`, by its
    archive name; UI-free, as UltraDatabase is). The standalone test tree
    (`Tests/UltraMessage/CMakeLists.txt`) compiles it in.
  - A reason is worded when it happens, so `UltraCanvasSingleInstance::Claim`
    loads the language before it asks the bus - a second start has no
    application yet. A program that loads none (`ultramsg`, the tests) gets
    the English, which is the catalogue key.
  - `UltraCanvasUnixSocketAddress.h` 1.1.0: `LastFailure()` / `FolderError()`
    say why `Resolve` failed, so the bus words a path that is too long
    itself instead of matching the header's English.
- **`UltraCanvasSingleInstance::HandOffMessage`: the terminal's line about
  the running copy, translated.** UltraClipboard, UltraToDo,
  UltraNotifications and UltraSnip each printed "<App> is already running:
  brought it forward." in English from a literal of their own. One call
  gives the line now - `Running`, `BroughtForward`, `RequestTaken`,
  `OpeningHere` - from the standard set, and loads the language for a start
  that quits before it has an application.
