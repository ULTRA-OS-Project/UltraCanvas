- **CI: UltraMail's Unicode normalization table is checked.** A new workflow,
  `ultramail-nfc-table.yml`, runs `scripts/generate_ultramail_nfc_table.py
  --check` on the Python the table was made with whenever the table, the
  script or the code that reads it changes, so a hand edit or a stale table
  fails the pull request. Once a week it also runs on the newest Python and
  fails when a newer Unicode version is out, saying to regenerate the table.
