- **Windows: one application as an MSIX package of its own, for a product of
  its own in the Microsoft Store** - `./package-win-msix.sh --app Texter`
  builds UltraTexter's (`dist-msix/UltraTexter-Windows-<version>-<arch>[-test|-store].msix`).
  - The package holds the program and what it loads, cut out of `dist/` by
    `scripts/make_msix_layout.py --app`: the DLLs the import tables reach from
    the program, the plug-ins and the run-time modules in `lib/`
    (`scripts/pe_imports.py`), the plug-in families but UltraIM, `lib/`,
    `etc/`, the media the framework reads and `cacert.pem` - not the other
    programs, their DLLs, or the demo's samples.
  - Its version is the application's own, line 1 of its changelog; a
    two-part version becomes four parts (1.94 -> 1.94.0.0). `MSIX_VERSION`
    sets another, for a Store resubmission when only the framework changed.
    `MSIX_DISPLAY_NAME` and `MSIX_DESCRIPTION` set the package's name and
    description (the suite's too).
  - CI builds UltraTexter's test package on every packaged Windows run, and
    its Store package on a release build once the Actions variable
    `MSIX_TEXTER_IDENTITY_NAME` holds the product's identity name from
    Partner Center.
  - The suite package is unchanged. `Docs/UltraCanvas/UltraCanvasWindowsMSIX.md`,
    *One application on its own* and *UltraTexter as its own product*.
