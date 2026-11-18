- **UltraIM Signal: a delivery tick that came too early was lost.** When the
  other side's receipt reached signal-cli before its answer to `send`
  (a slow machine: the ARM CI runner), the provider had not recorded the
  message yet and dropped the receipt, so the message never showed as
  delivered. Such a receipt is now kept and applied once the send is
  recorded. `SignalProviderTests` forces the order (`/_test/send_delay` in
  the stand-in daemon).
