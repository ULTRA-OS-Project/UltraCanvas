- **UltraIM: a Matrix provider.** `UltraCanvas/Plugins/UltraIM/matrix/`
  (module `ultraim_matrix`, provider `matrix`) speaks the Matrix
  client-server API to any homeserver: password or token sign-in with
  `.well-known` discovery, the session kept in a `DeviceKeyVault`, `/sync`
  as a long poll, direct chats (`m.direct`) and groups, invitations accepted
  by answering, replies, edits, redactions, read receipts, typing, presence
  and history paged back with `/messages`; our own messages go out under the
  client message id as the transaction id, so their echo meets the
  optimistic row. `Tests/UltraIM/MatrixProviderTests.cpp` runs against a
  real Synapse (`Tests/UltraIM/matrix_test_server.py`); CI installs it.
- **Matrix: end-to-end encryption (Olm and Megolm).** The cryptography is
  vodozemac, Matrix's audited Rust implementation, behind a C ABI shim
  (`Plugins/UltraIM/matrix/olm-c`, built with Corrosion; option
  `ULTRAIM_MATRIX_E2EE`, on, needs cargo - without it the provider builds and
  encrypted rooms say they cannot be read). `MatrixCrypto` keeps the rest:
  this device's keys signed and published with one-time and fallback keys
  topped up, other users' device lists with their keys trusted on first use,
  an Olm session per device carrying each room's key to every device of its
  members, a Megolm session per room rotated by the room's settings and when
  someone leaves, decryption with a replay guard and room and sender checks,
  and messages whose key is late shown as waiting and replaced when it comes.
  New direct chats and groups are encrypted; signing in again reuses the
  device while its keys are here. Everything is in one internal file per
  device, sealed with a key the vault holds. `UltraIM_ExportEncryptionKeys` /
  `UltraIM_ImportEncryptionKeys` (new provider calls) write and read the
  room-key export file Element uses; `UltraIMConversation::isEncrypted` marks
  encrypted conversations. `cmake/UltraCanvasRust.cmake` now holds the
  Windows Rust toolchain pick and the Corrosion fetch the Vectorizer plugin
  had to itself. Tests: `Tests/UltraIM/MatrixCryptoTests.cpp` (no server),
  and in `MatrixProviderTests` only ciphertext on the server, a second
  device, a key import, the same device after signing in again, and an
  encrypted room with matrix-nio (`matrix_nio_peer.py`), key exports read
  both ways; CI installs `matrix-nio[e2e]`.
