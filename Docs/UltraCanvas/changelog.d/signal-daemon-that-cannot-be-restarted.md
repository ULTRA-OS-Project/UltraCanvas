- **UltraIM Signal: a daemon that cannot be started again says why.**
  When the provider's own signal-cli ended while signed in and could not be
  started again - Java gone, the program removed - the outcome of the
  restart was dropped: the state stayed `Reconnecting` for ever, with a new
  try (a Java runtime started) every two seconds. The state is now `Error`
  with what the start said, told once for one reason - and what it quotes
  from signal-cli's log is that start's own lines, not the end of the whole
  log, so the same failure reads the same each time. The tries go on behind
  it, each failure in a row doubling the wait before the next
  (`SignalTimings::retryMs`, up to a minute), and the state is `Connected`
  again once one works. Tested in `SignalProviderTests`.
