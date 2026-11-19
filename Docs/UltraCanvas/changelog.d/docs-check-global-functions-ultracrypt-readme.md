- **The docs check knows functions outside the `UltraCanvas` namespace, and
  the UltraCrypt README's listings match the headers.**
  `scripts/check_doc_examples.py` (1.11.0) looked for a listed free function
  in `UltraCanvas` alone, so every function of a module that carries a
  prefix instead of a namespace was reported as unknown - all 66 of the
  UltraCrypt README's `UltraCrypt_` functions, which is why nothing could be
  read from its 70 findings. A listing written in no namespace is now tried
  in the global one as well, with its signature compared there, and a
  constructor listed `= delete` is checked to be absent rather than present.
  What was left were real differences, and they are corrected:
  `UltraCrypt_ConstantTimeEquals` takes two sizes, not one;
  `UltraCryptHasher` takes no options and cannot be moved; the CMS functions
  and the RSA / ECDSA checks are written with their parameter types instead
  of as sketches; and `UltraCryptHmacHasher`, a streaming HMAC class the
  README and the module registry both listed, was proposed and never built
  - HMAC is the one call `UltraCrypt_Hmac`. The examples' variables have
  their types, so the examples are compiled in full. `UltraCrypt` joins
  `CHECKED_MODULE_DOCS`, and `doc-examples.yml` starts for a change under
  `Docs/Modules/UltraCrypt/`; the README passes under clang 18 and under
  clang 14 with libstdc++ 12.
