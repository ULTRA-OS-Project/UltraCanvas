#### 2026-08-24 *0.51*
- **"Move to trash" emptied nothing when what was selected was the trash.**
  The Trash category has been in the cleaning tab since the first release,
  but ticking it with the removal mode set to *Move to trash* moved the trash
  into itself: each file was renamed beside itself, a second `.trashinfo` was
  written for it (down to `olddoc.txt.trashinfo.trashinfo`), the trash ended
  up **larger** than before, and the report claimed the space had been freed
  — on a real run here, "3 items, 50 KB freed" against a trash that grew from
  72 KB to 84 KB. Items already inside the trash are now left alone and
  counted separately, and both the window and the `--clean` output name the
  reason: emptying the trash is what *Delete permanently* is for. The Windows
  recycle bin follows the same rule, where that mode used to reach
  `SHEmptyRecycleBinW` and empty it for good — a permanent delete from the
  gentlest of the three modes.
- The Overview's "Clean system junk" card now mentions the trash, which the
  tab behind it has always covered.

#### 2026-08-23 *0.50*
- **UltraCleaner opens on a picture of your drives.** The app used to open
  on a list of cleanup rules, which asks the user to decide what to clean
  before telling them whether anything needs cleaning. The new **Overview**
  tab answers that first: one `UltraCanvasCircularProgressChart` per mounted
  volume, coloured green below 75% used, amber to 90% and red above it, so
  the state of the machine reads before any number does. Below the drives, a
  one-line verdict names the fullest drive and how much is left, and two
  buttons — *Clean system junk* and *Find duplicate photos* — lead into the
  two tabs that do the work, each with a sentence saying what it will and
  will not remove. Volumes come from a new `ListVolumes()` (`/proc/self/mounts`
  on Linux, `getmntinfo` on macOS, `GetLogicalDriveStringsW` on Windows;
  capacity from `std::filesystem::space`), which skips pseudo filesystems
  and puts the system volume first.
- **UltraCleaner finds duplicate and near-duplicate photographs.** A second
  half to the app: alongside rule-driven system junk, a **Photo albums**
  tab that groups pictures which are the same, or shots of the same
  moment, and proposes which one of each group to keep. Reviewed as
  groups of thumbnails rather than a file list, because deciding one by
  one across a thousand photos is not a thing anyone finishes.
  - **Two questions kept apart.** Byte-identical files and files whose
    decoded pixels match are facts — removing one loses nothing. Shots
    that merely look alike are judgements, never pre-selected.
  - **Two signals, both required.** Each picture reduces to a DCT pHash
    (composition, survives rescaling), a dHash, a 4×4 grid of mean
    colours and Laplacian variance. pHash and the colour grid must both
    agree, because either alone is not enough: on a 60-photograph
    reference album pHash could not separate a true burst member from an
    unrelated photo of the same person — both 14 bits away — while the
    colour grid put them at 100% and 81%. The grid scores the *fraction*
    of cells that agree rather than a summed distance, so a changing
    background behind a fixed subject cannot veto a real match.
  - **Three levels**, measured rather than guessed: Duplicates only
    (6 bits / 90%), Same moment (14 / 85%, the default, which reproduced
    every known group with no false positive) and Same scene (22 / 80%).
    Re-levelling costs only the comparisons, so the control is instant
    rather than a rescan.
  - **Screenshots are excluded from similarity.** They share their whole
    layout with every other capture of the same application and differ
    only in text, which these descriptors discard: on a real set two
    unrelated bank transfers scored 8 bits apart while the same transfer
    captured twice, one cropped, scored 36. The ranking inverts, so no
    threshold helps. They are matched only when byte-identical.
  - **Keeper selection prefers resolution over sharpness.** Downscaling
    raises Laplacian variance, so a small web copy looks "sharper" than
    its original; ranking on sharpness first made the scanner propose
    keeping a 479×670 copy over the 1197×1662 it came from.
  - Removal reuses the existing Remover, inheriting the PathGuard, trash
    support and simulate-by-default posture. CLI: `--album <folder>`
    with `--level` and `--within`; a bare folder path opens the window on
    that album.

---

UltraCleaner versions itself from this file, independently of the UltraCanvas
framework it is built on: `cmake/UltraCanvasVersion.cmake` reads the first
line above and defines `ULTRACLEANER_VERSION`, which the window title and
`--version` print. Framework changes the app needs still belong in
[`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md).

The app's first release predates this file and was written against the
framework changelog; it is still there, under *0.3.56*.
