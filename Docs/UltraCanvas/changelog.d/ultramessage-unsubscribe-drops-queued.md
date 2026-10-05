- **UltraMessage: a delivery queued for the UI thread is dropped once its
  subscription ends.** `UltraMsg_Unsubscribe` and `UltraMsg_Disconnect` took
  a subscription out of the tables, but a delivery the reader thread had
  already handed to the UI dispatcher kept its own reference and still called
  back. A subscriber that unsubscribed in its destructor was then called on
  freed memory. The Message Centre does exactly that, so the DemoApp crashed a
  moment after the Ultra Message page opened: the page was built twice at
  startup, and the first Message Centre was destroyed while the seven
  messages it had just seeded were still queued. Each subscription now
  carries an `active` flag that both calls clear and that a queued delivery
  checks before calling back. A dropped delivery is not acknowledged, exactly
  as if it had arrived after the unsubscribe. `UltraMessage.h` and the module
  README state the guarantee. New tests `unsubscribe_cancels_deliveries_already_queued`,
  `disconnect_cancels_deliveries_already_queued` and
  `message_center_destroyed_with_deliveries_queued` hold queued deliveries in
  a test dispatcher and run them after the unsubscribe; without the fix the
  first two fail and the Message Centre one aborts.
