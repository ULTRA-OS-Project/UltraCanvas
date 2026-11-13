#!/usr/bin/env python3
"""What scripts/check_cache_keys.py reports: a cache key ending in a commit
hash or carrying github.run_number (the prune never deletes those), the
combined actions/cache with a run-numbered key (saves on every run of every
branch), a run-numbered restore without restore-keys (never hits), and a
workflow that saves run-numbered caches without being on cache-prune.yml's
list - and nothing for the shapes build.yml uses. Run with

    python3 -m unittest discover -s scripts/tests -v
"""
import importlib.util
import sys
import unittest
from pathlib import Path

import yaml

SCRIPT = Path(__file__).resolve().parent.parent / "check_cache_keys.py"
spec = importlib.util.spec_from_file_location("check_cache_keys", SCRIPT)
check = importlib.util.module_from_spec(spec)
sys.modules["check_cache_keys"] = check
spec.loader.exec_module(check)

PRUNED = {"Build", "Wayland build"}


def workflow(name, steps):
    text = f"name: {name}\non:\n  push:\njobs:\n  job:\n    runs-on: ubuntu-22.04\n    steps:\n{steps}"
    return yaml.safe_load(text)


def messages(name, steps, pruned=PRUNED):
    return [m for _, m in check.check_workflow(workflow(name, steps), pruned)]


class KeyShape(unittest.TestCase):
    def test_run_numbered(self):
        self.assertEqual(check.key_shape("ccache-ubuntu-22.04-Release-${{ github.run_id }}"), "run")
        self.assertEqual(check.key_shape(
            "android-vcpkg-${{ hashFiles('a') }}-${{ github.run_id }}-${{ github.run_attempt }}"), "run")

    def test_commit_hash(self):
        self.assertEqual(check.key_shape("ccache-x-${{ github.sha }}"), "commit")
        self.assertEqual(check.key_shape("x-${{ github.event.pull_request.head.sha }}"), "commit")

    def test_run_number(self):
        self.assertEqual(check.key_shape("x-${{ github.run_number }}"), "run-number")

    def test_content(self):
        self.assertEqual(check.key_shape("libjxl-v4-${{ matrix.os }}-0.7.0"), "content")
        self.assertEqual(check.key_shape("vcpkg-assets-${{ hashFiles('MacOS/deps/**') }}"), "content")


class Findings(unittest.TestCase):
    def test_build_yml_shape_is_clean(self):
        steps = """
      - name: Restore ccache
        id: ccache
        uses: actions/cache/restore@v5
        with:
          path: .ccache
          key: ccache-${{ matrix.os }}-Release-${{ github.run_id }}
          restore-keys: |
            ccache-${{ matrix.os }}-Release-
      - name: Save ccache (main)
        if: github.ref == 'refs/heads/main'
        uses: actions/cache/save@v5
        with:
          path: .ccache
          key: ${{ steps.ccache.outputs.cache-primary-key }}
      - name: Restore the libjxl build
        uses: actions/cache@v5
        with:
          path: .libjxl
          key: libjxl-v4-${{ matrix.os }}-0.7.0
"""
        self.assertEqual(messages("Build", steps), [])

    def test_commit_hash_key_is_reported(self):
        steps = """
      - name: Restore ccache
        uses: actions/cache@v5
        with:
          path: .ccache
          key: ccache-ultraai-llamacpp-abc-${{ github.sha }}
          restore-keys: |
            ccache-ultraai-llamacpp-
"""
        found = messages("UltraAI llama.cpp adapter", steps)
        self.assertEqual(len(found), 1)
        self.assertIn("commit hash", found[0])

    def test_run_number_key_is_reported(self):
        steps = """
      - uses: actions/cache@v5
        with:
          path: .x
          key: x-${{ github.run_number }}
"""
        self.assertTrue(any("run_number" in m for m in messages("Build", steps)))

    def test_combined_action_with_run_numbered_key_is_reported(self):
        steps = """
      - name: Restore ccache
        uses: actions/cache@v5
        with:
          path: .ccache
          key: ccache-x-${{ github.run_id }}
          restore-keys: |
            ccache-x-
"""
        found = messages("Build", steps)
        self.assertEqual(len(found), 1)
        self.assertIn("every run of every branch", found[0])

    def test_run_numbered_restore_without_restore_keys_is_reported(self):
        steps = """
      - name: Restore ccache
        uses: actions/cache/restore@v5
        with:
          path: .ccache
          key: ccache-x-${{ github.run_id }}
"""
        found = messages("Build", steps)
        self.assertEqual(len(found), 1)
        self.assertIn("never hits", found[0])

    def test_saving_workflow_must_be_on_the_prune_list(self):
        steps = """
      - name: Restore ccache
        id: ccache
        uses: actions/cache/restore@v5
        with:
          path: .ccache
          key: ccache-x-${{ github.run_id }}
          restore-keys: |
            ccache-x-
      - name: Save ccache (main)
        uses: actions/cache/save@v5
        with:
          path: .ccache
          key: ${{ steps.ccache.outputs.cache-primary-key }}
"""
        found = messages("Not pruned", steps)
        self.assertEqual(len(found), 1)
        self.assertIn("cache-prune.yml", found[0])
        self.assertEqual(messages("Build", steps), [])
        # A restore-only workflow saves nothing and need not be listed.
        restore_only = steps.split("      - name: Save ccache")[0]
        self.assertEqual(messages("Not pruned", restore_only), [])

    def test_prune_list_is_read_from_the_real_file(self):
        names = check.pruned_workflow_names()
        self.assertIsNotNone(names)
        self.assertIn("Build", names)


if __name__ == "__main__":
    unittest.main()
