- **The design documents are checked too.** `--all` first left out the
  Proposal / Plan / Investigation docs, whose code is of APIs not written
  yet. They are in now, with their findings baselined (one `<doc>::<message>`
  line each): the file is the record of what each proposal still waits
  for, and when an API is written its entries stop being found and the
  strict run says so. The component docs stay at zero; only the changelog
  is left out, being a record of what shipped rather than a description
  of an API.
