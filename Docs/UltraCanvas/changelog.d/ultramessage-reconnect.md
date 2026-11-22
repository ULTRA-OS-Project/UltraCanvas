- **UltraMessage endpoints reconnect when the broker goes.** The message bus's
  broker runs inside whichever program connected first, and when that program
  ended every other program's endpoint lost the bus for good: the
  notification toasts, UltraNotifications, the Message Centre, a feed
  publisher and the single-instance hand-off all went quiet until each was
  restarted. An endpoint whose connection drops without
  `UltraMsg_Disconnect` now reconnects by itself (`UltraMsgConnectOptions::autoReconnect`,
  on by default): after 200 ms, then at growing intervals up to five seconds,
  hosting the next broker when `startBrokerIfAbsent` allows, and registering
  its subscriptions again under the same handles, so their callbacks keep
  coming; one made with `replaySinceMs` gets what was journaled meanwhile.
  `UltraCanvasSingleInstance` relies on it and keeps its watch only for a
  copy that never got onto the bus. `UltraMessageTests` stops the broker
  under two endpoints and starts it again: they come back on their own and
  the subscription delivers.
- **The UltraMessage module doc is compiled by the docs check.** Two of its
  examples called functions of the reader's (`OpenPaths`, `Open`) without
  declaring them; they are declared in `doc-check` comments now, and
  `Docs/Modules/UltraMessage` is in `CHECKED_MODULE_DOCS`, so CI keeps it
  compiling.
