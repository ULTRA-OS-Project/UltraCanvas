- **The model server's lists are signed, and the client checks them.** The
  model server signs `index.json`, `catalogue.json` and `topics.json` with its
  Ed25519 key (the signature in the `X-UltraAI-Signature` header and as
  `<document>.sig`) and publishes its keys in `keys.json`. The local runtime
  pins those keys on the first refresh from a server
  (`LocalRuntimeSettings::trustedKeys`) and from then on refuses a refresh
  whose index, catalogue or topics are not signed by a pinned key - nothing
  is replaced, and the error's `providerCode` is `"bad-signature"`. A new
  model server address drops the old pins; a gallery that publishes no keys
  (the built-in one, a plain mirror) stays unsigned. Settings > Model topics
  says whether the lists in use are signed and by which key.
- The settings window's pages - Default providers, Local models, Model
  topics, Local & cloud, Endpoints - go through `Tr`, so they show in the
  desktop's language once `media/locale/<lang>/UltraAI.json` has them.
