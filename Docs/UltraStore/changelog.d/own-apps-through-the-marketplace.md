- **The ULTRA OS Project's own apps go through the marketplace** (decided
  2026-11-18, the README's *The operator tier*): the project is the merchant
  `ultra-os-project`, its admin backend submits each release, and the
  public catalog has one writer, the marketplace's publish; the merchant
  backend's `publish` stays for a store of one's own. The README names what
  the handover needs - the operator's key committed as `keys/market.pub`
  and announced by a last catalog signed with `publish.pub` (which the
  admin backend cannot do yet), a first sequence above the public
  catalog's, the project's codes leaving the public catalog - and a new
  section, *Packaging an application for the store*, on
  `scripts/package-store-app.sh`.
