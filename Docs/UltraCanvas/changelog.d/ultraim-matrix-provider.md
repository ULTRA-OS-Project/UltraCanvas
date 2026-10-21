- **UltraIM: a Matrix provider.** `UltraCanvas/Plugins/UltraIM/matrix/`
  (module `ultraim_matrix`, provider `matrix`) speaks the Matrix
  client-server API to any homeserver: password or token sign-in with
  `.well-known` discovery, the session kept in a `DeviceKeyVault`, `/sync`
  as a long poll, direct chats (`m.direct`) and groups, invitations accepted
  by answering, replies, edits, redactions, read receipts, typing, presence
  and history paged back with `/messages`; our own messages go out under the
  client message id as the transaction id, so their echo meets the
  optimistic row. End-to-end encrypted rooms are marked and not written in
  yet. `Tests/UltraIM/MatrixProviderTests.cpp` runs against a real Synapse
  (`Tests/UltraIM/matrix_test_server.py`); CI installs it.
