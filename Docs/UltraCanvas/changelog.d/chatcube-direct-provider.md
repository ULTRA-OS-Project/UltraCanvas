- **ChatCube Direct: the ULTRA OS native messenger, as an UltraIM provider.**
  A provider module (`UltraCanvas/Plugins/UltraIM/direct/`, loaded as
  `Plugins/UltraIM/ultraim_direct`) with a key-pair identity made on the
  device and kept in its `DeviceKeyVault`, end-to-end encrypted messages
  (X25519 key agreement, HKDF, XChaCha20-Poly1305, authenticated header)
  sent peer to peer over an UltraRTC data channel whose set-up is signed
  with the identity keys, the ChatCube relay for sign-in, presence,
  signalling, the offline queue and groups, delivery and read receipts, and
  a local SQLite record of contacts, conversations, history and the outbox,
  and 1:1 voice and video calls (signed invite / accept / reject / camera /
  hang-up over the relay, an UltraRTC peer per call with Opus and VP8, mute,
  camera off; a dropped media connection waits briefly for the hang-up
  that explains it), and the local network with or without a relay: an
  mDNS announcement (`_chatcube._tcp`) and discovery, a TCP connection both
  ends authenticate with their identity keys carrying the same signed
  signalling, the nearby list, and the outbox waiting for the link when
  there is no relay.
  `Tests/UltraIM/DirectProviderTests.cpp` runs two providers against a local
  relay end to end; CI installs the relay for it.
