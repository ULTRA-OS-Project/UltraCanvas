- **The catalog announces the active keys.** Every publish writes a schema 2
  catalog with `trusted_keys`, every key not retired: `key new`, one publish
  with the old key, then publishing with the new one rotates the signing
  key without a new store binary; `key retire` drops a key from the list.
