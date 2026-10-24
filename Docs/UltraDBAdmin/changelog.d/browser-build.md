- **The browser build.** UltraDBAdmin compiles to WebAssembly
  (`UltraDBAdmin.html`) and reaches its databases through
  **ultradbadmin-relay** on the server: the page holds no password and names
  no host, the relay offers the connections configured on its side to the
  users its tokens name, with read or write access each. The page opens its
  relay at start-up (`?relay=` in its address, else `/relay` on its own
  host).
- **Relay connections on the desktop too.** **Relay** in the header asks for
  a relay's address and a token and adds a connection for each name the
  relay offers; every tab works through it. The token is kept in the vault,
  where a password goes.
- **ultradbadmin-relay**: an INI configuration (connections, users by token
  hash, limits), passwords in its own vault (`--set-secret`),
  `--hash-token`, `--check`, an audit log of every statement with its user
  and address, and a WebSocket listener on loopback for the TLS proxy in
  front, with `/health`. A plain password in the file, a network address
  without TLS and an SSH option are refused.
