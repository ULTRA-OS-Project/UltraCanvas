#!/usr/bin/env python3
"""The rules scripts/extract_ui_strings.py reads the sources by, held the way
Tests/SystemFontTest.cpp holds the font parsers: a C++ string literal's
escapes (a \\xHH or octal escape is a byte of the UTF-8 text, \\uXXXX a code
point), comments stripped without a "/*" or "//" inside a string literal
swallowing the code after it, the leading literal arguments of a call
(continued literals joined, a variable stopping the list), and the sites a
file yields. Run with

    python3 -m unittest discover -s scripts/tests -v

which .github/workflows/locale-catalogues.yml does on every change to the
extractor.
"""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "extract_ui_strings.py"
spec = importlib.util.spec_from_file_location("extract_ui_strings", SCRIPT)
extract = importlib.util.module_from_spec(spec)
sys.modules["extract_ui_strings"] = extract
spec.loader.exec_module(extract)


class Unescape(unittest.TestCase):
    def test_text_without_a_backslash_is_itself(self):
        self.assertEqual(extract.unescape("Save As..."), "Save As...")
        self.assertEqual(extract.unescape("plain · text × more"), "plain · text × more")

    def test_simple_escapes(self):
        self.assertEqual(extract.unescape(r"a\nb\tc"), "a\nb\tc")
        self.assertEqual(extract.unescape(r'say \"hi\" \\ done'), 'say "hi" \\ done')
        self.assertEqual(extract.unescape(r"it\'s"), "it's")

    def test_hex_escapes_are_bytes_of_the_utf8_text(self):
        # UltraSnip writes the multiplication sign as its two UTF-8 bytes.
        self.assertEqual(extract.unescape(r"{0} \xc3\x97 {1} px"), "{0} × {1} px")
        self.assertEqual(extract.unescape(r"\xe2\x80\xa2 item"), "• item")
        self.assertEqual(extract.unescape(r"\x41\x42"), "AB")

    def test_octal_escapes_are_bytes_too(self):
        self.assertEqual(extract.unescape(r"\101\102"), "AB")
        self.assertEqual(extract.unescape(r"\342\200\242 item"), "• item")

    def test_unicode_escapes_are_code_points(self):
        self.assertEqual(extract.unescape(r"\u2022 Multi-file editing"), "• Multi-file editing")
        self.assertEqual(extract.unescape(r"\u00d7"), "×")
        self.assertEqual(extract.unescape(r"\U0001F600"), "😀")

    def test_mixed_forms_in_one_literal(self):
        self.assertEqual(extract.unescape(r"\u2022 \xc3\x97 \101\n"), "• × A\n")

    def test_an_unknown_escape_keeps_its_character(self):
        self.assertEqual(extract.unescape(r"\q"), "q")


class StripComments(unittest.TestCase):
    def test_line_and_block_comments_go(self):
        self.assertEqual(extract.strip_comments("a; // c\nb; /* d */ e;"), "a; \nb;  e;")

    def test_a_block_comment_keeps_its_newlines(self):
        self.assertEqual(extract.strip_comments("a /* x\ny\nz */ b"), "a \n\n b")

    def test_a_comment_opener_inside_a_string_is_text(self):
        # Texter's list of comment markers: "/*" swallowed 1,800 lines.
        text = 'const char* markers[] = {"<!--", "/*"};\nlabel = Tr("Save");\n/* real */ x;'
        self.assertEqual(extract.strip_comments(text), 'const char* markers[] = {"<!--", "/*"};\nlabel = Tr("Save");\n x;')

    def test_a_url_inside_a_string_is_text(self):
        text = 'OpenURL("https://ultraos.eu/feedback"); Tr("Feedback");'
        self.assertEqual(extract.strip_comments(text), text)

    def test_an_escaped_quote_does_not_end_the_string(self):
        text = 'Tr("say \\"//\\" here"); // gone'
        self.assertEqual(extract.strip_comments(text), 'Tr("say \\"//\\" here"); ')

    def test_a_character_literal_quote_is_not_a_string_start(self):
        text = "if (c == '\"') x; // gone\nTr(\"Kept\");"
        self.assertEqual(extract.strip_comments(text), "if (c == '\"') x; \nTr(\"Kept\");")


class LiteralArgs(unittest.TestCase):
    def args(self, call):
        return extract.literal_args(call, call.index("("))

    def test_one_literal(self):
        self.assertEqual(self.args('Tr("Save")'), ["Save"])

    def test_two_literals(self):
        self.assertEqual(self.args('TrC("button", "Open")'), ["button", "Open"])

    def test_a_continued_literal_is_one_argument(self):
        self.assertEqual(self.args('Tr("The hunspell "\n   "package is missing")'), ["The hunspell package is missing"])

    def test_a_variable_stops_the_list(self):
        self.assertEqual(self.args('TrN("{n} file", "{n} files", count)'), ["{n} file", "{n} files"])
        self.assertEqual(self.args('TrF("Found {0}", name)'), ["Found {0}"])
        self.assertEqual(self.args("Tr(name)"), [])

    def test_escapes_are_decoded_in_the_argument(self):
        self.assertEqual(self.args(r'Tr("A \u2022 B\n")'), ["A • B\n"])


class SitesIn(unittest.TestCase):
    def sites(self, source):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "Sample.cpp"
            path.write_text(source, encoding="utf-8")
            return extract.sites_in(path)

    def test_every_call_kind(self):
        source = (
            'label = Tr("Save");\n'
            'month = TrC("month-short", "Apr");\n'
            'files = TrN("{n} file",\n            "{n} files",\n            count);\n'
            'msg = TrF("\\"{0}\\" is write-protected.", name);\n'
        )
        self.assertEqual(self.sites(source), [
            ("entry", "Save", None),
            ("context", "month-short|Apr", "Apr"),
            ("plural", "{n} file", "{n} files"),
            ("entry", '"{0}" is write-protected.', None),
        ])

    def test_sites_after_a_comment_opener_in_a_string_are_found(self):
        source = 'const char* m[] = {"/*"};\n' + "\n" * 5 + 'x = Tr("After");\n/* c */\ny = Tr("Later");\n'
        self.assertEqual([s[1] for s in self.sites(source)], ["After", "Later"])

    def test_a_site_in_a_comment_is_not_a_site(self):
        source = '// Tr("Commented out")\n/* Tr("Also") */\nz = Tr("Real");\n'
        self.assertEqual([s[1] for s in self.sites(source)], ["Real"])

    def test_a_longer_name_is_not_a_call_and_a_space_before_the_paren_is(self):
        source = 'Str("no");\nQTr("no");\nvalue = Tr ("yes");\n'
        self.assertEqual([s[1] for s in self.sites(source)], ["yes"])

    def test_a_bullet_and_a_byte_escape_reach_the_key(self):
        source = 'a = Tr("\\u2022 Dark/Light themes");\nb = Tr("{0} \\xc3\\x97 {1} px");\n'
        self.assertEqual([s[1] for s in self.sites(source)], ["• Dark/Light themes", "{0} × {1} px"])


if __name__ == "__main__":
    unittest.main()
