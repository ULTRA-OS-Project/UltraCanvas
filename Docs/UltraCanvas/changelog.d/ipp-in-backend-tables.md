- **The IODeviceManager backend tables now list IPP printing.** The IPP
  driverless printer backend and the rewrite of those tables to list only
  backends that exist merged one after the other, so the tables said nothing
  of IPP - and the README still marked it "IPP discovery planned". The
  Printers rows of `Docs/Dependencies.md` and the DemoApp's copy of it, the
  module README's overview and backend tables, and `intro.md` now say that
  driverless network printers work over IPP on every platform, and the README
  gains a *Network Printers* section showing `ULTRACANVAS_IPP_PRINTERS`, next
  to the one for eSCL scanners.
- **The changelog check never ran on a pending entry.** `changelog.yml`
  triggered on `CHANGELOG.md`, the version cmake file, the script and itself,
  but not on `Docs/UltraCanvas/changelog.d/**` - and since the framework's
  number moved to `main`, a pending entry is how nearly every pull request
  records its change. So `check_changelog.py`'s rule that a pending entry
  carries no `####` header was never enforced in CI; #579's two entries, for
  one, merged without the check running. The workflow now triggers on that
  directory too.
