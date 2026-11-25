- **The QMP client closes what it opened.** `QmpClient::Connect` left the
  socket of a connection that failed open until the client was destroyed,
  and opened a new one over it when asked again: on one client, each further
  try lost a descriptor. Every caller makes a fresh client per command, so
  nothing leaked in practice - it was a trap for the next caller. `Connect`
  now closes an earlier session first and leaves nothing open when it answers
  false; `Disconnect()` ends a session by hand; and a command on a client
  that is not connected says "QMP not connected" at once instead of waiting
  out its timeout. `Tests/UltraWin/test_vm.cpp` counts the open descriptors
  over failed connects and has a stand-in server see the first session end
  when the client connects again.
