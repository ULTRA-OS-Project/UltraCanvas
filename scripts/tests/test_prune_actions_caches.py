#!/usr/bin/env python3
"""What scripts/prune_actions_caches.py deletes: run-numbered cache entries
superseded by a newer one of their kind on their ref, and every entry of a
closed pull request - never the newest of a kind, never an entry keyed by its
content. Run with

    python3 -m unittest discover -s scripts/tests -v

which .github/workflows/locale-catalogues.yml does on every change under
scripts/tests.
"""
import importlib.util
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "prune_actions_caches.py"
spec = importlib.util.spec_from_file_location("prune_actions_caches", SCRIPT)
prune = importlib.util.module_from_spec(spec)
sys.modules["prune_actions_caches"] = prune
spec.loader.exec_module(prune)

MAIN = "refs/heads/main"
HASH = "5466db7569c61fa15222b1060af01ac773eba8018695b81ba4d8963fe76eced7"


def cache(id, key, ref=MAIN, created="2026-11-10T12:00:00Z", size=100):
    return {"id": id, "key": key, "ref": ref, "created_at": created, "size_in_bytes": size}


def deleted_ids(caches, closed=()):
    return sorted(c["id"] for c, _ in prune.plan_deletions(caches, set(closed)))


class RunKind(unittest.TestCase):
    def test_a_run_numbered_key_loses_its_run(self):
        self.assertEqual(prune.run_kind("ccache-macos-15-Release-42833026773"),
                         "ccache-macos-15-Release")
        self.assertEqual(prune.run_kind(f"vcpkg-macos-15-arm64-{HASH}-42833026773"),
                         f"vcpkg-macos-15-arm64-{HASH}")

    def test_the_attempt_goes_with_the_run(self):
        self.assertEqual(prune.run_kind(f"android-vcpkg-arm64-android-{HASH}-42833026773-2"),
                         f"android-vcpkg-arm64-android-{HASH}")

    def test_keys_by_content_are_not_run_numbered(self):
        for key in (f"vcpkg-assets-{HASH}",
                    "apt-debs-ubuntu-22.04-0f3a9c",
                    "libjxl-v4-ubuntu-22.04-0.7.0",
                    "libheif-nogpl-v2-ubuntu-22.04-1.19.8-kvazaar-2.3.1",
                    "nsis-Windows-X64-1",
                    "msys2-pkgs-CLANG64-"):
            self.assertIsNone(prune.run_kind(key), key)

    def test_the_two_macos_legs_are_two_kinds(self):
        self.assertNotEqual(prune.run_kind("ccache-macos-15-Release-1000001"),
                            prune.run_kind("ccache-macos-15-intel-Release-1000001"))


class PullNumber(unittest.TestCase):
    def test_pull_refs(self):
        self.assertEqual(prune.pull_number("refs/pull/1166/merge"), 1166)
        self.assertEqual(prune.pull_number("refs/pull/7/head"), 7)

    def test_branches(self):
        self.assertIsNone(prune.pull_number(MAIN))
        self.assertIsNone(prune.pull_number("refs/heads/claude/pull-thing"))
        self.assertIsNone(prune.pull_number(None))


class Plan(unittest.TestCase):
    def test_only_the_newest_of_a_kind_stays(self):
        caches = [
            cache(1, "ccache-macos-15-Release-1000001", created="2026-11-10T10:00:00Z"),
            cache(2, "ccache-macos-15-Release-1000003", created="2026-11-10T14:00:00Z"),
            cache(3, "ccache-macos-15-Release-1000002", created="2026-11-10T12:00:00Z"),
        ]
        self.assertEqual(deleted_ids(caches), [1, 3])

    def test_a_kind_alone_is_kept(self):
        self.assertEqual(deleted_ids([cache(1, "ccache-ubuntu-22.04-Release-1000001")]), [])

    def test_kinds_and_legs_are_kept_apart(self):
        caches = [
            cache(1, "ccache-macos-15-Release-1000001"),
            cache(2, "ccache-macos-15-intel-Release-1000002"),
            cache(3, "ccache-ubuntu-22.04-Release-1000003"),
            cache(4, "ccache-ubuntu-22.04-arm-Release-1000004"),
        ]
        self.assertEqual(deleted_ids(caches), [])

    def test_the_newest_is_chosen_by_time_not_by_run_number(self):
        # A run that started earlier can save later: the later save is the
        # one a restore by prefix takes, so it is the one kept.
        caches = [
            cache(1, "ccache-macos-15-Release-1000009", created="2026-11-10T10:00:00Z"),
            cache(2, "ccache-macos-15-Release-1000001", created="2026-11-10T11:00:00Z"),
        ]
        self.assertEqual(deleted_ids(caches), [1])

    def test_each_ref_keeps_its_own_newest(self):
        caches = [
            cache(1, "ccache-macos-15-Release-1000001", ref=MAIN, created="2026-11-10T10:00:00Z"),
            cache(2, "ccache-macos-15-Release-1000002", ref="refs/pull/5/merge",
                  created="2026-11-10T11:00:00Z"),
        ]
        self.assertEqual(deleted_ids(caches), [])

    def test_content_keyed_entries_stay(self):
        caches = [
            cache(1, f"vcpkg-assets-{HASH}"),
            cache(2, "apt-debs-ubuntu-22.04-0f3a9c"),
            cache(3, "apt-debs-ubuntu-22.04-77aa01"),
        ]
        self.assertEqual(deleted_ids(caches), [])

    def test_a_closed_pull_request_loses_everything(self):
        caches = [
            cache(1, f"vcpkg-macos-15-arm64-{HASH}-1000001", ref="refs/pull/9/merge"),
            cache(2, "msys2-pkgs-CLANG64-abc", ref="refs/pull/9/merge"),
            cache(3, f"vcpkg-macos-15-arm64-{HASH}-1000002", ref="refs/pull/10/merge"),
        ]
        self.assertEqual(deleted_ids(caches, closed={9}), [1, 2])

    def test_every_deletion_says_why(self):
        caches = [
            cache(1, "ccache-macos-15-Release-1000001", created="2026-11-10T10:00:00Z"),
            cache(2, "ccache-macos-15-Release-1000002", created="2026-11-10T11:00:00Z"),
            cache(3, "x", ref="refs/pull/4/merge"),
        ]
        reasons = {c["id"]: why for c, why in prune.plan_deletions(caches, {4})}
        self.assertIn("ccache-macos-15-Release-1000002", reasons[1])
        self.assertIn("#4", reasons[3])


if __name__ == "__main__":
    unittest.main()
