#!/usr/bin/env python3
"""What scripts/check_label_identifiers.py reports: a label built from one
argument that is text - a string literal that is not a name, or a Tr call -
whichever way it is constructed, and across lines; and nothing for a label
built from a name, from an identifier and a text, from a variable, for
another element, a comment, or a line that says its one string is the
identifier. Run with

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
    return [(f.line, f.shown) for f in check.check_source(source, Path("x.cpp"))]


class ReadsAsText(unittest.TestCase):
    def test_words(self):
        for text in ("Add model file", "Name:", "Größe", "100 %", "…", "Don't", "Ready.", "a b",
                     "Line one\nline two", "-"):
            self.assertTrue(check.reads_as_text(text), text)

    def test_names(self):
        for name in ("IconLabel", "countLabel", "InfoText1", "uai-hf-status", "count_label",
                     "page.title", "settings/page:title", "StatusBar", "x1", "Ready", "OK", ""):
            self.assertFalse(check.reads_as_text(name), name)


class Reported(unittest.TestCase):
    def test_a_sentence(self):
        self.assertEqual(found('auto l = std::make_shared<UltraCanvasLabel>("Select a repository");'),
                         [(1, '"Select a repository"')])
        self.assertEqual(found('l = std::make_shared<UltraCanvas::UltraCanvasLabel>( "Name:" );'),
                         [(1, '"Name:"')])

    def test_a_translated_text(self):
        # The form the one caller in the tree had, and the one a text meant
        # for a person takes.
        self.assertEqual(found('l = CreateLabel(Tr("On the right side"));'), [(1, 'Tr("On the right side")')])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(TrF("{0} files", n));'),
                         [(1, 'TrF("{0} files", n)')])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(UltraCanvas::Tr("Save"));'),
                         [(1, 'UltraCanvas::Tr("Save")')])

    def test_across_lines(self):
        source = 'hint = std::make_shared<UltraCanvasLabel>(\n        Tr("Pick a folder")\n);\n'
        self.assertEqual(found(source), [(2, 'Tr("Pick a folder")')])

    def test_other_forms(self):
        self.assertEqual(found('auto* l = new UltraCanvasLabel("Two words");'), [(1, '"Two words"')])
        self.assertEqual(found('UltraCanvasLabel title("Two words");'), [(1, '"Two words"')])
        self.assertEqual(found('UltraCanvasLabel title{"Two words"};'), [(1, '"Two words"')])
        self.assertEqual(found('row->AddChild(std::make_unique<UltraCanvasLabel>("Two words"));'),
                         [(1, '"Two words"')])
        # A base-class initialiser hands the label its one string too.
        self.assertEqual(found('Caption() : UltraCanvasLabel(Tr("Caption")) {}'), [(1, 'Tr("Caption")')])

    def test_literals_side_by_side(self):
        self.assertEqual(found('l = CreateLabel("Two "\n                "words");'), [(1, '"Two words"')])

    def test_every_one_in_a_file(self):
        source = ('a = std::make_shared<UltraCanvasLabel>(Tr("First"));\n'
                  'b = std::make_shared<UltraCanvasLabel>("First", Tr("First"));\n'
                  'c = std::make_shared<UltraCanvasLabel>("A second one");\n')
        self.assertEqual(found(source), [(1, 'Tr("First")'), (3, '"A second one"')])


class NotReported(unittest.TestCase):
    def test_a_name(self):
        # One string is the identifier, and these are identifiers.
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("CountLabel");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(\n    "StatusBar"\n);'), [])
        self.assertEqual(found('l = CreateLabel("uai-hf-status");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("Ready");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>();'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("");'), [])

    def test_identifier_and_text(self):
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("InfoTitle", Tr("About"));'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("", Tr("Select a repository"));'), [])
        self.assertEqual(found('l = CreateLabel("StatusLabel", 120, 20, "Ready to go");'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("Hint", TrF("{0} of {1}", a, b));'), [])

    def test_what_it_cannot_read(self):
        # A variable or a call may hold either; the framework says so at run time.
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(name);'), [])
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>(MakeId("text"));'), [])
        self.assertEqual(found('label = CreateLabel(identifier);'), [])

    def test_declarations(self):
        # The header's own constructors and factories are not constructions.
        self.assertEqual(found('explicit UltraCanvasLabel(const std::string &identifier = "")'), [])
        self.assertEqual(found('inline std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string &identifier) {'), [])

    def test_other_elements(self):
        self.assertEqual(found('c = std::make_shared<UltraCanvasContainer>("Content Section");'), [])
        self.assertEqual(found('b = std::make_shared<UltraCanvasButton>("OkButton", Tr("OK"));'), [])
        self.assertEqual(found('auto t = std::make_shared<UltraCanvasLabelStyle>(Tr("Body"));'), [])

    def test_comments(self):
        self.assertEqual(found('// l = std::make_shared<UltraCanvasLabel>(Tr("Old"));'), [])
        self.assertEqual(found('/* old:\n l = std::make_shared<UltraCanvasLabel>("Two words");\n*/'), [])
        # A comment marker inside a string is not a comment.
        self.assertEqual(found('url = "https://x//y"; l = std::make_shared<UltraCanvasLabel>("Two words");'),
                         [(1, '"Two words"')])

    def test_said_to_be_the_identifier(self):
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("row 12");   '
                               '// label-identifier-ok: the list names its rows this way'), [])
        self.assertEqual(found('// label-identifier-ok: an id from the document, as it is\n'
                               'l = std::make_shared<UltraCanvasLabel>("section 2");'), [])
        # The marker needs a reason, and belongs to its own construction.
        self.assertEqual(found('l = std::make_shared<UltraCanvasLabel>("row 12");   // label-identifier-ok:'),
                         [(1, '"row 12"')])
        self.assertEqual(found('// label-identifier-ok: this one\n'
                               'a = std::make_shared<UltraCanvasLabel>("row 12");\n'
                               'b = std::make_shared<UltraCanvasLabel>("row 13");'),
                         [(3, '"row 13"')])
        self.assertEqual(found('a = std::make_shared<UltraCanvasLabel>("row 12");   // label-identifier-ok: a row\n'
                               'b = std::make_shared<UltraCanvasLabel>("row 13");'),
                         [(2, '"row 13"')])


class Repository(unittest.TestCase):
    def test_the_tree_is_clean(self):
        roots = [check.REPO_ROOT / r for r in check.SEARCH_ROOTS if (check.REPO_ROOT / r).exists()]
        findings = [str(f) for path in check.iter_sources(roots) for f in check.check_file(path)]
        self.assertEqual(findings, [])


if __name__ == "__main__":
    unittest.main()
