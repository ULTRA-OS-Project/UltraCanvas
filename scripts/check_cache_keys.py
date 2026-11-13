#!/usr/bin/env python3
"""Checks the Actions cache keys in .github/workflows against what
cache-prune.yml can manage.

GitHub keeps 10 GB of Actions cache per repository and evicts the least
recently used entries past that, and the budget is shared by every
workflow. Two shapes of key keep the budget in check:

- **Keyed by content**: a hash of the inputs, a version. One entry of its
  kind; a run whose key hits saves nothing (the combined actions/cache does
  that by itself). Fine with the combined action.
- **Run-numbered**: `...-${{ github.run_id }}` (optionally
  `-${{ github.run_attempt }}`), restored by prefix with `restore-keys` and
  saved on main alone - actions/cache/restore on every run, actions/cache/save
  under `if: github.ref == 'refs/heads/main'`. scripts/prune_actions_caches.py
  deletes each entry a newer one of its kind supersedes, so one entry per kind
  stays. The workflow must be in cache-prune.yml's `workflows:` list, or the
  prune never runs after it.

What leaks:

- A key ending in a **commit hash** (`${{ github.sha }}`, the pull request's
  head sha) or in **`${{ github.run_number }}`**: a new entry on every run,
  and the prune does not recognise either as run-numbered (its rule needs a
  run id, six digits or more, so a short version number at the end of a key
  stays out), so they pile up until GitHub's seven-day expiry.
  ultraai-llamacpp.yml shipped such a key on 2026-11-12: 300 MB per run,
  pull requests included.
- The **combined `actions/cache`** with a run-numbered key: it saves a fresh
  entry on every run of every branch, pull requests included, which is what
  crowded main's caches out of the budget before build.yml split restore
  from save (2026-10-13).
- A run-numbered restore **without `restore-keys`**: the exact key is new
  every run, so it never hits.

    python3 scripts/check_cache_keys.py [--strict] [files...]

Lists each finding as file:line and exits 1 with --strict when there are
any. With no files it reads every workflow under .github/workflows. The
analysis (check_workflow) is pure and tested in
scripts/tests/test_check_cache_keys.py.
"""
import argparse
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"
PRUNE = WORKFLOWS / "cache-prune.yml"

_RUN_ID = re.compile(r"\$\{\{\s*github\.run_id\s*\}\}")
_RUN_ATTEMPT = re.compile(r"\$\{\{\s*github\.run_attempt\s*\}\}")
_COMMIT = re.compile(r"\$\{\{\s*github\.(?:sha|event\.pull_request\.head\.sha|event\.workflow_run\.head_sha)\s*\}\}")
_RUN_NUMBER = re.compile(r"\$\{\{\s*github\.run_number\s*\}\}")
_OUTPUT_KEY = re.compile(r"^\$\{\{\s*steps\.([A-Za-z0-9_-]+)\.outputs\.cache-primary-key\s*\}\}$")


def _steps(workflow):
    """(job id, step dict) for every step of every job."""
    for job_id, job in (workflow.get("jobs") or {}).items():
        for step in (job or {}).get("steps") or []:
            if isinstance(step, dict):
                yield job_id, step


def _cache_steps(workflow):
    """The cache steps: (job id, step, flavour) with flavour one of
    'combined', 'restore', 'save'."""
    out = []
    for job_id, step in _steps(workflow):
        uses = str(step.get("uses") or "")
        if not uses.startswith("actions/cache"):
            continue
        action = uses.split("@", 1)[0]
        flavour = {"actions/cache": "combined",
                   "actions/cache/restore": "restore",
                   "actions/cache/save": "save"}.get(action)
        if flavour:
            out.append((job_id, step, flavour))
    return out


def resolve_key(step, job_steps):
    """The key a step saves or restores: its own `key`, or the primary key of
    the restore step it names (`${{ steps.<id>.outputs.cache-primary-key }}`)."""
    key = str((step.get("with") or {}).get("key") or "").strip()
    m = _OUTPUT_KEY.match(key)
    if not m:
        return key
    for other in job_steps:
        if other.get("id") == m.group(1):
            return str((other.get("with") or {}).get("key") or "").strip()
    return key


def key_shape(key):
    """'run' for a run-numbered key, 'commit' for one ending in a commit hash,
    'run-number' for one ending in github.run_number, 'content' otherwise."""
    tail = key.rstrip()
    tail = _RUN_ATTEMPT.sub("", tail).rstrip("-")
    if _RUN_ID.search(tail) and tail.endswith(_RUN_ID.search(tail).group(0)):
        return "run"
    if _COMMIT.search(key) and key.endswith(_COMMIT.search(key).group(0)):
        return "commit"
    if _RUN_NUMBER.search(key):
        return "run-number"
    return "content"


def check_workflow(workflow, pruned_names=None):
    """Findings for one parsed workflow, as (step name, message) pairs.
    `pruned_names` are the workflow names cache-prune.yml runs after; None
    skips that rule."""
    findings = []
    jobs = workflow.get("jobs") or {}
    saves_run_numbered = False
    for job_id, step, flavour in _cache_steps(workflow):
        job_steps = [s for s in (jobs[job_id].get("steps") or []) if isinstance(s, dict)]
        key = resolve_key(step, job_steps)
        name = str(step.get("name") or step.get("id") or step.get("uses"))
        shape = key_shape(key)
        if shape == "commit":
            findings.append((name, f"key ends in a commit hash ({key}): a new entry on every "
                                   "run that the prune never recognises, kept until GitHub's "
                                   "seven-day expiry. Key by content, or end the key in "
                                   "${{ github.run_id }}, restore by prefix and save on main."))
            continue
        if shape == "run-number":
            findings.append((name, f"key carries github.run_number ({key}): the prune's rule "
                                   "needs a run id (six digits or more), so these entries are "
                                   "never deleted. Use ${{ github.run_id }}."))
            continue
        if shape != "run":
            continue
        if flavour == "combined":
            findings.append((name, f"actions/cache with a run-numbered key ({key}) saves a "
                                   "fresh entry on every run of every branch, pull requests "
                                   "included. Use actions/cache/restore here and an "
                                   "actions/cache/save guarded by github.ref == "
                                   "'refs/heads/main'."))
        if flavour in ("combined", "restore"):
            if not (step.get("with") or {}).get("restore-keys"):
                findings.append((name, f"run-numbered key ({key}) without restore-keys: the "
                                       "exact key is new every run, so it never hits."))
        if flavour in ("combined", "save"):
            saves_run_numbered = True
    if saves_run_numbered and pruned_names is not None:
        wf_name = str(workflow.get("name") or "")
        if wf_name not in pruned_names:
            findings.append((wf_name or "(unnamed workflow)",
                             "saves run-numbered caches but is not in cache-prune.yml's "
                             "`workflows:` list, so its superseded entries are never deleted."))
    return findings


def pruned_workflow_names(prune_path=PRUNE):
    """The workflow names cache-prune.yml runs after, or None without it."""
    if not prune_path.exists():
        return None
    wf = yaml.safe_load(prune_path.read_text(encoding="utf-8")) or {}
    on = wf.get("on") or wf.get(True) or {}
    run = (on.get("workflow_run") or {}) if isinstance(on, dict) else {}
    names = run.get("workflows") or []
    return set(str(n) for n in names)


def _line_of(text, step_name):
    for i, line in enumerate(text.splitlines(), 1):
        if step_name and f"name: {step_name}" in line:
            return i
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="*", help="workflow files (default: all)")
    parser.add_argument("--strict", action="store_true", help="exit 1 on any finding")
    args = parser.parse_args()

    files = [Path(f) for f in args.files] or sorted(WORKFLOWS.glob("*.yml"))
    pruned = pruned_workflow_names()
    total_steps = 0
    findings = []
    for path in files:
        text = path.read_text(encoding="utf-8")
        wf = yaml.safe_load(text) or {}
        total_steps += len(_cache_steps(wf))
        for step_name, message in check_workflow(wf, pruned):
            rel = path.relative_to(ROOT) if path.is_absolute() and ROOT in path.parents else path
            findings.append(f"{rel}:{_line_of(text, step_name)}: {step_name}: {message}")

    for f in findings:
        print(f)
    if findings:
        print(f"check_cache_keys: {len(findings)} finding(s) in {len(files)} workflows "
              f"({total_steps} cache steps)")
        return 1 if args.strict else 0
    print(f"check_cache_keys: clean ({len(files)} workflows, {total_steps} cache steps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
