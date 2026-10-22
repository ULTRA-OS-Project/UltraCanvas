- **Signing up on a relay behind an HTTPS redirect failed with HTTP 422.**
  A relay address typed as `http://…` or without a scheme reached a server
  that redirects to `https://`, and the redirected request arrived without
  its body. ChatCube now takes a bare host as `https://`, follows the
  redirect with the request intact, and remembers the address it ended at;
  a request the relay cannot read says why instead of "HTTP 422".
