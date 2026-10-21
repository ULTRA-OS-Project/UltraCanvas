- **Pull requests stop colliding over generated files and each other.**
  - `llms.txt` / `llms-full.txt` are written on `main`: the new
    `llms-regenerate.yml` regenerates them after every docs change lands and
    commits the result, and `llms-txt.yml` refuses a pull request that changes
    them (it still checks the `## Contents` inventories and the README
    mirrors, which branches keep committing). They are the whole docs corpus,
    so any two docs pull requests open at once conflicted there, and the
    second to merge had to merge `main`, regenerate and run the whole CI
    matrix again - the changelog's line-1 collision, cured the same way.
  - `scripts/check_pr_overlap.py` predicts a branch's conflicts with the other
    open pull requests before they reach GitHub: for each one it simulates
    the merge order GitHub will see (that branch into `main` first, this one
    on top) with `git merge-tree`, and reports real conflicts, two branches
    giving one application changelog the same top version (which git can
    merge with no conflict at all), generated files, and shared files that
    merge cleanly. It reads the open pull requests from `gh`, from
    `--branches`, or falls back to recently active branches. `AGENTS.md`
    rule 4 of the branch rules: run it before opening a pull request and
    before every push.
  - The Windows install-and-uninstall step gives the uninstaller ten seconds
    to remove the Start-menu folder before calling it left behind, and names
    what is still in it when it is; it failed a pull request that changes no
    packaging while `main` at the same base passed.
