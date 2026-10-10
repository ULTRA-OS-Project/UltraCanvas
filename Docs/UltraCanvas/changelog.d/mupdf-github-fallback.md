- **The Linux CI legs fall back to MuPDF's GitHub mirror when mupdf.com
  does not answer.** The install step builds MuPDF 1.23.10 from the release
  archive on mupdf.com, and on 2026-10-09 that host timed out through all
  three download attempts, seven minutes of retries, and failed the leg
  before a line of UltraCanvas was compiled. When the download fails, the
  step now clones the same release tag from `github.com/ArtifexSoftware/mupdf`
  with the third-party submodules the build compiles, and makes it the same
  way.
