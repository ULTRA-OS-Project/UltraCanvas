- **SmartHome: several listeners per notification.** `SmartHomeListener`
  holds any of the module's twelve callbacks; `SmartHomeAPI::AddListener`
  adds one and returns the id `RemoveListener` takes. Each notification calls
  the `SetOn*` slot, as before, and then every listener with that member set,
  with no module lock held. Once `RemoveListener` returns the listener is not
  running and is not called again (from inside a notification it stops every
  later call). `SmartHomePanel` listens this way, so the application hosting
  it keeps its own callbacks - before, the two took the single slot from
  each other.
  - `SmartHomeListenerTest` covers it, removal raced against a notifying
    thread included.
