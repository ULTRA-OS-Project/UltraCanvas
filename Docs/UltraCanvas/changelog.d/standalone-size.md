- **The Linux UltraCanvasStart archive is xz, and the size of a standalone
  package is measured.** `scripts/package-ultracanvasstart.sh` packs the
  Linux package with xz instead of gzip: the same tree went from 49 MB to
  35 MB, since it is mostly shared libraries and the core, which xz packs
  well. `Docs/UltraCanvas/StandaloneSizeInvestigation.md` records what the
  packages weigh and why: the shared core (61 MB on Linux), ICU's data
  reached through Ubuntu's libxml2 (29 MB), libvips' delegates and GTK, and
  the modules an application could do without at 14% of the whole, so an
  on-demand module core would cut the package by about 15%, while a static
  link of the one application halves it. `package-macos.sh` now says in the
  log which load command it could not rewrite (a library without header
  padding), instead of swallowing the error.
