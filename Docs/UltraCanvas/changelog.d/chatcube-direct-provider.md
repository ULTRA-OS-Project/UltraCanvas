- **ChatCube Direct: the ULTRA OS native messenger, as an UltraIM provider.**
  A provider module (`UltraCanvas/Plugins/UltraIM/direct/`, loaded as
  `Plugins/UltraIM/ultraim_direct`) with a key-pair identity made on the
  device and kept in its `DeviceKeyVault`, end-to-end encrypted messages
  (X25519 key agreement, HKDF, XChaCha20-Poly1305, authenticated header)
  sent peer to peer over an UltraRTC data channel whose set-up is signed
  with the identity keys, the ChatCube relay for sign-in, presence,
  signalling, the offline queue and groups, delivery and read receipts, and
  a local SQLite record of contacts, conversations, history and the outbox,
  and 1:1 voice calls (signed invite / accept / reject / hang-up over the
  relay, an UltraRTC Opus peer per call, mute).
  `Tests/UltraIM/DirectProviderTests.cpp` runs two providers against a local
  relay end to end; CI installs the relay for it.
