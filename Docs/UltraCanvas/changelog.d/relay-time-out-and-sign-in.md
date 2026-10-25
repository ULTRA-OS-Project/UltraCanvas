- **UltraDatabase relay: a statement time-out.** `UltraDbRelayLimits::statementTimeoutSeconds`
  (300 by default): a query, a script or a cursor's page still running then
  is cancelled on the session's own connection - every engine - and answered
  `Cancelled`, saying why.
- **UltraDatabase relay: a sign-in of the program's own.** `UltraDbRelayServer::SetSignIn`
  takes a label and a callback that knows tokens no user's hash matches (the
  tickets of a program's own sign-in); `UltraDb_RelaySignInOffer` asks a
  relay what sign-in it offers.
- **The WebAssembly workflow tests UltraDBAdmin's page end to end**: a new
  `dbadmin-browser` job runs the baseline's page in Chrome against a relay
  it builds natively and a test OpenID provider, and checks the relay's
  audit log for the sign-in, a table browsed and a query stopped.
