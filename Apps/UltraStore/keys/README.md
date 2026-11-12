# Trusted signing keys

The public keys `ultrastore` trusts out of the box, one per file, as 64 hex
digits (`ultrastore pubkey <seed>` prints one; `ultrastore keygen` makes a
pair). The build compiles every `*.pub` here into the program through the
generated `UltraStoreBuiltin.h`, so a fresh install trusts the right key with
nothing fetched; the user adds further keys on the window's Settings page,
and the catalog's own `trusted_keys` list (schema 2, with the admin backend)
rotates them.

`publish.pub` is the production key: the admin backend generated it on the
publishing machine and its public half was committed here with the first
published catalog (the server is `ULTRASTORE_SERVERS`'s default,
`Apps/UltraStore/CMakeLists.txt`). A second publishing machine adds a second
file. A key is retired by deleting its file and releasing the store without
it; the catalog cannot remove a compiled-in key, by design.
