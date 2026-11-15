#!/usr/bin/env python3
"""What scripts/check_label_identifiers.py reports: a label or checkbox built
from one string literal that reads as an identifier - whichever way it is
constructed, and across lines - and nothing for a label with an identifier
and a text, a label whose one string is words, a text that goes through Tr,
another element, a comment, or a line that says its one string is the text.
Run with

    python3 -m unittest discover -s scripts/tests -v
"""
import importlib.util
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "check_label_identifiers.py"
spec = importlib.util.spec_from_file_location("check_label_identifiers", SCRIPT)
check = importlib.util.module_from_spec(spec)
sys.modules["check_label_identifiers"] = check
spec.loader.exec_module(check)


def found(source):
    return [(f.line, f.text) for f in check.check_source(source, Path("x.cpp"))]


class ReadsAsIdentifier(unittest.TestCase):
    def test_names(self):
        for name in ("IconLabel", "countLabel", "InfoText1", "uai-hf-status", "count_label",
                     "page.title", "StatusBar", "x1"):
            self.assertTrue(check.reads_as_identifier(name), name)

    def test_words(self):
        for text in ("", "Ready", "OK", "PDF", "Name:", "Add model file", "Größe", "100 %", "…",
                     "Don't", "a/b", "-", "3D"):
            self.assertFalse(check.reads_as_identifier(text), text)


class Constructions(unittest.TestCase):
    def test_make_shared(self):
        self.assertEqual(found('auto l = std::make_shared<UltraCanvasLabel>("CountLabel");'),
                         [(1, "CountLabel")])
        self.assertEqual(found('l = std::make_shared<UltraCanvas::UltraCanvasLabel>( "IconLabel" );'),
                         [(1, "IconLabel")])
        self.assertEqual(found('auto l = std::make_unique<UltraCanvasLabel>("uai-status");'),
                         [(1, "uai-status")])

    def test_across_lines(self):
        # Texter's status bar was written this way, and a line-by-line search missed it.
        source = 'statusLabel = std::make_shared<UltraCanvasLabel>(\n        "StatusBar"\n);\n'
        self.assertEqual(found(source), [(2, "StatusBar")])

    def test_other_forms(self):
        self.assertEqual(found('auto* l = new UltraCanvasLabel("InfoTitle");'), [(1, "InfoTitle")])
        self.assertEqual(found('UltraCanvasLabel title("HeaderTitle");'), [(1, "HeaderTitle")])
        self.assertEqual(found('UltraCanvasLabel title{"HeaderTitle"};'), [(1, "HeaderTitle")])
        self.assertEqual(found('auto l = CreateLabel("InfoText1");'), [(1, "InfoText1")])

    def test_checkbox(self):
        # The same pair of constructors: one string is the box's text.
        self.assertEqual(found('auto c = std::make_shared<UltraCanvasCheckbox>("WrapLines");'),
                         [(1, "WrapLines")])
        self.assertEqual(found('auto c = CreateCheckbox("wrap_lines");'), [(1, "wrap_lines")])

    def test_every_one_in_a_file(self):
        source = ('a = std::make_shared<UltraCanvasLabel>("FirstLabel");\n'
                  'b = std::make_shared<UltraCanvasLabel>("First", "");\n'
                  'c = std::make_shared<UltraCanvasLabel>("SecondLabel");\n')
        self.assertEqual(found(source), [(1, "FirstLabel"), (3, "SecondLabel")])


class NotReported(unittest.TestCase):
    def test_identifier_and_text(self):
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("CountLabel", "");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("InfoTitle", Tr("About"));'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("IconLabel", 0, 0, 48, 48);'), [])
        self.assertEqual(found('l = CreateLabel("StatusLabel", 120, 20, "Ready");'), [])

    def test_words(self):
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("Ready");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("Select a repository");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(Tr("CountLabel"));'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(name);'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>();'), [])

    def test_other_elements(self):
        # A container's one string IS its identifier.
        self.assertEqual(found('c = std::make_shared<UltraCanvasContainer>("ContentSection");'), [])
        self.assertEqual(found('b = std::make_shared<UltraCanvasButton>("OkButton", "OK");'), [])
        self.assertEqual(found('auto t = std::make_shared<UltraCanvasLabelStyle>("BodyText");'), [])

    def test_comments(self):
        self.assertEqual(found('// l = std::make_shared<UltraCanvasLabel>("CountLabel");'), [])
        self.assertEqual(found('/* old:\n l = std::make_shared<UltraCanvasLabel>("CountLabel");\n*/'), [])
        # A comment marker inside a string is not a comment.
        self.assertEqual(found('url = "https://x//y"; l = std::make_shared<UltraCanvasLabel>("CountLabel");'),
                         [(1, "CountLabel")])

    def test_said_to_be_text(self):
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("UltraCanvas");   '
                               '// label-text-ok: the product\'s name'), [])
        self.assertEqual(found('// label-text-ok: a file name, shown as it is\n'
                               'l = std::make_shared<UltraCanvasLabel>("config.json");'), [])
        # The marker needs a reason, and belongs to its own construction.
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("config.json");   // label-text-ok:'),
                         [(1, "config.json")])
        self.assertEqual(found('// label-text-ok: this one\n'
                               'a = std::make_shared<UltraCanvasLabel>("config.json");\n'
                               'b = std::make_shared<UltraCanvasLabel>("other.json");'),
                         [(3, "other.json")])
        self.assertEqual(found('a = std::make_shared<UltraCanvasLabel>("config.json");   // label-text-ok: a file\n'
                               'b = std::make_shared<UltraCanvasLabel>("other.json");'),
                         [(2, "other.json")])


class Repository(unittest.TestCase):
    def test_the_tree_is_clean(self):
        roots = [check.REPO_ROOT / r for r in check.SEARCH_ROOTS if (check.REPO_ROOT / r).exists()]
        findings = [str(f) for path in check.iter_sources(roots) for f in check.check_file(path)]
        self.assertEqual(findings, [])


if __name__ == "__main__":
    unittest.main()
