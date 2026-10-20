// Apps/Texter/UltraCanvasLanguageDetect.cpp
// Guess a text's language from its content alone
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#include "UltraCanvasLanguageDetect.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {

        // Only the head of a paste is read: a language shows itself early, and
        // a megabyte paste must not stall the paste that triggered this.
        constexpr size_t kScanBytes = 64 * 1024;
        constexpr size_t kMaxLineBytes = 1000;
        // The least a winner must score, and how far ahead of the runner-up
        // (from another family) it must be, before it is an answer at all.
        constexpr int kMinScore = 4;
        constexpr int kMinLead = 2;

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string Trim(const std::string& s) {
            const char* ws = " \t\r\n\f\v";
            const size_t first = s.find_first_not_of(ws);
            if (first == std::string::npos) return "";
            const size_t last = s.find_last_not_of(ws);
            return s.substr(first, last - first + 1);
        }

        bool StartsWith(const std::string& s, const char* prefix) {
            return s.compare(0, std::strlen(prefix), prefix) == 0;
        }

        bool EndsWith(const std::string& s, const char* suffix) {
            const size_t n = std::strlen(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }

        bool Contains(const std::string& s, const char* part) {
            return s.find(part) != std::string::npos;
        }

        bool IsIdentChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        // `s` starts with `word` followed by a space, '(' or the end.
        bool StartsWithWord(const std::string& s, const char* word) {
            const size_t n = std::strlen(word);
            if (s.compare(0, n, word) != 0) return false;
            return s.size() == n || !IsIdentChar(s[n]);
        }

        // ===== #! LINE =====

        std::string LanguageFromShebang(const std::string& firstLine) {
            if (!StartsWith(firstLine, "#!")) return "";
            std::vector<std::string> words;
            size_t pos = 2;
            while (pos < firstLine.size()) {
                while (pos < firstLine.size() && std::isspace(static_cast<unsigned char>(firstLine[pos]))) pos++;
                size_t end = pos;
                while (end < firstLine.size() && !std::isspace(static_cast<unsigned char>(firstLine[end]))) end++;
                if (end > pos) words.push_back(firstLine.substr(pos, end - pos));
                pos = end;
            }
            std::string interpreter;
            for (size_t i = 0; i < words.size(); i++) {
                std::string name = words[i].substr(words[i].find_last_of('/') + 1);
                // "#!/usr/bin/env -S python3 -u": the program is the first
                // word after env that is not an option or an assignment.
                if (name == "env") {
                    for (size_t j = i + 1; j < words.size(); j++) {
                        if (words[j][0] != '-' && !Contains(words[j], "=")) {
                            interpreter = words[j].substr(words[j].find_last_of('/') + 1);
                            break;
                        }
                    }
                } else {
                    interpreter = name;
                }
                break;
            }
            interpreter = ToLower(interpreter);
            // python3.12 -> python, luajit -> lua
            while (!interpreter.empty() &&
                   (std::isdigit(static_cast<unsigned char>(interpreter.back())) || interpreter.back() == '.')) {
                interpreter.pop_back();
            }
            if (interpreter == "python" || interpreter == "pypy") return "Python";
            if (interpreter == "sh" || interpreter == "bash" || interpreter == "zsh" ||
                interpreter == "ksh" || interpreter == "dash" || interpreter == "ash" ||
                interpreter == "fish") return "Shell Script";
            if (interpreter == "perl") return "Perl";
            if (interpreter == "ruby") return "Ruby";
            if (interpreter == "node" || interpreter == "nodejs") return "JavaScript";
            if (interpreter == "deno" || interpreter == "bun" || interpreter == "ts-node") return "TypeScript";
            if (interpreter == "php") return "PHP";
            if (interpreter == "lua" || interpreter == "luajit") return "Lua";
            if (interpreter == "rscript") return "R";
            if (interpreter == "elixir") return "Elixir";
            if (interpreter == "swift") return "Swift";
            return "";
        }

        // ===== JSON =====

        // A strict JSON reader that only answers "is this one complete JSON
        // value" - with `allowComments`, the // and /* */ JSONC permits.
        class JsonChecker {
        public:
            JsonChecker(const std::string& text, bool allowComments)
                    : s(text), comments(allowComments) {}

            bool IsWholeValue() {
                SkipSpace();
                if (!Value(0)) return false;
                SkipSpace();
                return failed ? false : i == s.size();
            }

            bool SawComment() const { return sawComment; }

        private:
            const std::string& s;
            bool comments;
            bool sawComment = false;
            bool failed = false;
            size_t i = 0;

            void SkipSpace() {
                while (i < s.size()) {
                    const char c = s[i];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                        i++;
                    } else if (comments && c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                        sawComment = true;
                        while (i < s.size() && s[i] != '\n') i++;
                    } else if (comments && c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                        sawComment = true;
                        const size_t end = s.find("*/", i + 2);
                        if (end == std::string::npos) { failed = true; i = s.size(); return; }
                        i = end + 2;
                    } else {
                        return;
                    }
                }
            }

            bool Literal(const char* word) {
                const size_t n = std::strlen(word);
                if (s.compare(i, n, word) != 0) return false;
                i += n;
                return true;
            }

            bool String() {
                if (i >= s.size() || s[i] != '"') return false;
                i++;
                while (i < s.size()) {
                    const char c = s[i++];
                    if (c == '"') return true;
                    if (c == '\\') {
                        if (i >= s.size()) return false;
                        i++;
                    } else if (static_cast<unsigned char>(c) < 0x20) {
                        return false;
                    }
                }
                return false;
            }

            bool Number() {
                const size_t start = i;
                if (i < s.size() && s[i] == '-') i++;
                size_t digits = 0;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { i++; digits++; }
                if (digits == 0) { i = start; return false; }
                if (i < s.size() && s[i] == '.') {
                    i++;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
                }
                if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                    i++;
                    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
                }
                return true;
            }

            bool Value(int depth) {
                if (depth > 256 || i >= s.size()) return false;
                const char c = s[i];
                if (c == '{') return Container(depth, '}', true);
                if (c == '[') return Container(depth, ']', false);
                if (c == '"') return String();
                if (c == 't') return Literal("true");
                if (c == 'f') return Literal("false");
                if (c == 'n') return Literal("null");
                return Number();
            }

            bool Container(int depth, char close, bool isObject) {
                i++;
                SkipSpace();
                if (i < s.size() && s[i] == close) { i++; return true; }
                while (i < s.size()) {
                    if (isObject) {
                        if (!String()) return false;
                        SkipSpace();
                        if (i >= s.size() || s[i] != ':') return false;
                        i++;
                        SkipSpace();
                    }
                    if (!Value(depth + 1)) return false;
                    SkipSpace();
                    if (failed || i >= s.size()) return false;
                    if (s[i] == close) { i++; return true; }
                    if (s[i] != ',') return false;
                    i++;
                    SkipSpace();
                    // JSONC tolerates a trailing comma before the close.
                    if (comments && i < s.size() && s[i] == close) { i++; sawComment = true; return true; }
                }
                return false;
            }
        };

        std::string LanguageFromJson(const std::string& trimmed) {
            if (trimmed.empty() || (trimmed[0] != '{' && trimmed[0] != '[')) return "";
            if (trimmed.size() > kScanBytes * 16) return "";
            JsonChecker strict(trimmed, false);
            if (strict.IsWholeValue()) {
                // A one-element "[1]" or "{}" says little - a list in prose
                // reads the same - so only a value with some structure counts.
                if (trimmed.size() < 8) return "";
                const bool geo = Contains(trimmed, "\"type\"") &&
                                 (Contains(trimmed, "\"FeatureCollection\"") ||
                                  (Contains(trimmed, "\"Feature\"") && Contains(trimmed, "\"geometry\"")));
                return geo ? "GeoJSON" : "JSON";
            }
            JsonChecker relaxed(trimmed, true);
            if (relaxed.IsWholeValue() && relaxed.SawComment()) return "JSONC";
            return "";
        }

        // ===== MARKUP =====

        bool IsHtmlElementName(const std::string& name) {
            static const char* const names[] = {
                    "html", "head", "body", "div", "span", "p", "a", "ul", "ol", "li",
                    "table", "tr", "td", "th", "thead", "tbody", "h1", "h2", "h3", "h4",
                    "h5", "h6", "header", "footer", "main", "nav", "section", "article",
                    "aside", "form", "input", "button", "img", "br", "hr", "script",
                    "style", "link", "meta", "title", "pre", "code", "em", "strong",
                    "b", "i", "label", "select", "option", "textarea", "iframe", "canvas",
                    "video", "audio", "figure", "template", "blockquote",
            };
            for (const char* n : names) {
                if (name == n) return true;
            }
            return false;
        }

        // Text that is markup from its first byte to its last: the root
        // element names the dialect.
        std::string LanguageFromMarkup(const std::string& trimmed) {
            if (StartsWith(trimmed, "<?php")) return "PHP";
            if (trimmed.empty() || trimmed[0] != '<' || trimmed.back() != '>') return "";

            const bool xmlDeclaration = StartsWith(trimmed, "<?xml");
            size_t pos = 0;
            while (pos < trimmed.size()) {
                pos = trimmed.find('<', pos);
                if (pos == std::string::npos) break;
                if (trimmed.compare(pos, 4, "<!--") == 0) {
                    const size_t end = trimmed.find("-->", pos);
                    if (end == std::string::npos) return "";
                    pos = end + 3;
                    continue;
                }
                if (trimmed.compare(pos, 2, "<?") == 0) {
                    const size_t end = trimmed.find("?>", pos);
                    if (end == std::string::npos) return "";
                    pos = end + 2;
                    continue;
                }
                if (trimmed.compare(pos, 2, "<!") == 0) {
                    if (ToLower(trimmed.substr(pos, 14)) == "<!doctype html") return "HTML";
                    const size_t end = trimmed.find('>', pos);
                    if (end == std::string::npos) return "";
                    pos = end + 1;
                    continue;
                }
                break;
            }
            if (pos == std::string::npos || pos + 1 >= trimmed.size()) {
                return xmlDeclaration ? "XML" : "";
            }
            size_t end = pos + 1;
            while (end < trimmed.size() &&
                   (IsIdentChar(trimmed[end]) || trimmed[end] == '-' || trimmed[end] == ':' || trimmed[end] == '.')) {
                end++;
            }
            if (end == pos + 1) return "";
            const std::string root = ToLower(trimmed.substr(pos + 1, end - pos - 1));
            if (root == "svg") return "SVG";
            if (root == "rss" || root == "feed") return "RSS/Atom";
            if (root == "kml") return "KML";
            if (root == "project" && Contains(trimmed, "maven")) return "POM";
            if (IsHtmlElementName(root)) return xmlDeclaration ? "XML" : "HTML";
            // Anything else that opens and closes as markup is XML; a lone tag
            // could just as well be a placeholder in prose.
            const size_t closing = trimmed.rfind("</");
            if (xmlDeclaration || (closing != std::string::npos && closing > pos)) return "XML";
            return "";
        }

        // ===== LINE VOTES =====

        class Votes {
        public:
            void Add(const char* language, int points) { scores[language] += points; }
            int Get(const char* language) const {
                const auto it = scores.find(language);
                return it == scores.end() ? 0 : it->second;
            }
            const std::map<std::string, int>& All() const { return scores; }

        private:
            std::map<std::string, int> scores;
        };

        bool IsYamlKeyLine(const std::string& t) {
            size_t i = 0;
            if (i < t.size() && (t[i] == '"' || t[i] == '\'')) {
                const size_t close = t.find(t[i], i + 1);
                if (close == std::string::npos) return false;
                i = close + 1;
            } else {
                while (i < t.size() && (IsIdentChar(t[i]) || t[i] == '-' || t[i] == '.')) i++;
                if (i == 0) return false;
            }
            if (i >= t.size() || t[i] != ':') return false;
            if (i + 1 < t.size() && t[i + 1] != ' ') return false;
            // Statements and blocks of the C family and Python end like this;
            // a YAML value does not.
            return !EndsWith(t, ";") && !EndsWith(t, "{") && !EndsWith(t, "(") && !EndsWith(t, ",");
        }

        bool IsPythonBlockKeyword(const std::string& t) {
            static const char* const words[] = {"else", "try", "finally", "except", "elif", "if",
                                                "for", "while", "with", "def", "class", "async",
                                                "case", "match", "default", "public", "private",
                                                "protected"};
            for (const char* w : words) {
                if (StartsWithWord(t, w)) return true;
            }
            return false;
        }

        // `#include <stdio.h>` - the header name, or "" when the line is not one.
        std::string IncludedHeader(const std::string& t) {
            if (!StartsWith(t, "#include") && !StartsWith(t, "# include")) return "";
            const size_t open = t.find_first_of("<\"");
            if (open == std::string::npos) return "";
            const size_t close = t.find_first_of(">\"", open + 1);
            if (close == std::string::npos) return "";
            return t.substr(open + 1, close - open - 1);
        }

        bool HasX86Register(const std::string& lt) {
            static const char* const regs[] = {"eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp",
                                               "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
                                               " al", " ah", " r8", " r9"};
            for (const char* r : regs) {
                if (Contains(lt, r)) return true;
            }
            return false;
        }

        // `text](` with a `[` before it: a Markdown link or image.
        bool HasMarkdownLink(const std::string& t) {
            const size_t mid = t.find("](");
            if (mid == std::string::npos || mid == 0) return false;
            const size_t open = t.rfind('[', mid);
            return open != std::string::npos && open + 1 < mid && t.find(')', mid) != std::string::npos;
        }

        bool IsMarkdownTableRule(const std::string& t) {
            if (!Contains(t, "|") || !Contains(t, "---")) return false;
            for (char c : t) {
                if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
            }
            return true;
        }

        bool IsAll(const std::string& t, char c, size_t minCount) {
            return t.size() >= minCount && t.find_first_not_of(c) == std::string::npos;
        }

        bool IsOrderedListItem(const std::string& t) {
            size_t i = 0;
            while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) i++;
            return i > 0 && i + 1 < t.size() && (t[i] == '.' || t[i] == ')') && t[i + 1] == ' ';
        }

        // A CSS property declaration: `font-size: 12px;`
        bool IsCssDeclaration(const std::string& t) {
            size_t i = 0;
            while (i < t.size() && (std::islower(static_cast<unsigned char>(t[i])) || t[i] == '-')) i++;
            if (i == 0 || i >= t.size()) return false;
            size_t colon = i;
            while (colon < t.size() && t[colon] == ' ') colon++;
            if (colon >= t.size() || t[colon] != ':') return false;
            if (!EndsWith(t, ";")) return false;
            return !Contains(t, "(") || Contains(t, "url(") || Contains(t, "rgb") ||
                   Contains(t, "var(") || Contains(t, "calc(");
        }

        // A CSS rule's selector line: `.menu > li a:hover {`
        bool IsCssSelector(const std::string& t) {
            if (!EndsWith(t, "{") || t.size() < 2) return false;
            const char first = t[0];
            if (first != '.' && first != '#' && first != '*' && first != ':' && first != '[') return false;
            if (StartsWith(t, "#include") || StartsWith(t, "#define") || StartsWith(t, "#[")) return false;
            return !Contains(t, "=") || Contains(t, "[");
        }

        void VoteLine(const std::string& t, Votes& v) {
            const std::string lt = ToLower(t);

            // ----- C and C++ -----
            const std::string header = IncludedHeader(t);
            if (!header.empty()) {
                v.Add("C", 2);
                v.Add("C++", 2);
                if (!Contains(header, ".")) v.Add("C++", 3);      // <iostream>, <vector>
                else if (EndsWith(header, ".h")) v.Add("C", 1);
            }
            if (StartsWith(t, "#define ") || StartsWith(t, "#ifndef ") || StartsWith(t, "#ifdef ") ||
                StartsWith(t, "#endif") || StartsWith(t, "#pragma ") || StartsWith(t, "#undef ")) {
                v.Add("C", 1);
                v.Add("C++", 1);
            }
            if (Contains(t, "std::")) v.Add("C++", 3);
            if (StartsWith(t, "template<") || StartsWith(t, "template <")) v.Add("C++", 4);
            if (t == "public:" || t == "private:" || t == "protected:") v.Add("C++", 4);
            if (Contains(t, "cout <<") || Contains(t, "cerr <<") || Contains(t, "cout<<")) v.Add("C++", 3);
            if (StartsWith(t, "using namespace ")) v.Add("C++", 4);
            if (Contains(t, "nullptr")) v.Add("C++", 2);
            if (StartsWith(t, "typedef struct")) v.Add("C", 3);
            if (Contains(t, "malloc(") || Contains(t, "printf(\"")) v.Add("C", 1);
            if (StartsWith(t, "int main(")) { v.Add("C", 1); v.Add("C++", 1); }

            // ----- C# -----
            if (StartsWith(t, "using System")) v.Add("C#", 6);
            else if (StartsWith(t, "using ") && EndsWith(t, ";") && !StartsWith(t, "using namespace ") &&
                     !Contains(t, "::")) v.Add("C#", 2);
            if (Contains(t, "Console.Write")) v.Add("C#", 4);
            if (Contains(t, "{ get;") || Contains(t, "get; set;")) v.Add("C#", 4);
            if (StartsWith(t, "#region") || StartsWith(t, "#endregion")) v.Add("C#", 4);
            if (Contains(t, "static void Main(")) v.Add("C#", 5);
            if (StartsWith(t, "namespace ")) {
                if (EndsWith(t, ";")) v.Add("C#", 3);
                else if (Contains(t, "\\")) v.Add("PHP", 4);
                else { v.Add("C++", 2); v.Add("C#", 2); }
            }

            // ----- Java -----
            if (StartsWith(t, "import java.") || StartsWith(t, "import javax.")) v.Add("Java", 6);
            else if (StartsWith(t, "import ") && EndsWith(t, ";") && !Contains(t, "'") && !Contains(t, "\"")) {
                v.Add("Java", 2);
            }
            if (StartsWith(t, "package ")) {
                if (EndsWith(t, ";")) v.Add("Java", 4);
                else if (t == "package main") v.Add("Go", 6);
                else if (Contains(t, ".")) { v.Add("Kotlin", 2); v.Add("Scala", 2); }
                else v.Add("Go", 3);
            }
            if (Contains(t, "System.out.print")) v.Add("Java", 5);
            if (Contains(t, "static void main(String")) v.Add("Java", 6);
            if (t == "@Override") v.Add("Java", 2);
            if (StartsWith(t, "public class ") || StartsWith(t, "public final class ")) {
                v.Add("Java", 2);
                v.Add("C#", 1);
            }

            // ----- Kotlin, Scala, Swift, Dart -----
            if (StartsWith(t, "fun ") || Contains(t, " fun ")) v.Add("Kotlin", 4);
            if (StartsWith(t, "import kotlin") || StartsWith(t, "import android") ||
                StartsWith(t, "import androidx")) v.Add("Kotlin", 6);
            if (StartsWith(t, "val ") && Contains(t, "=")) { v.Add("Kotlin", 2); v.Add("Scala", 2); }
            if (StartsWith(t, "import scala.")) v.Add("Scala", 6);
            if (StartsWith(t, "case class ")) v.Add("Scala", 5);
            if (StartsWith(t, "object ") && (EndsWith(t, "{") || Contains(t, " extends "))) {
                v.Add("Scala", 3);
                v.Add("Kotlin", 1);
            }
            if (StartsWith(t, "def ") && Contains(t, ") =")) v.Add("Scala", 3);
            if (StartsWith(t, "import Foundation") || StartsWith(t, "import UIKit") ||
                StartsWith(t, "import SwiftUI") || StartsWith(t, "import Cocoa")) v.Add("Swift", 7);
            if (StartsWith(t, "guard let ") || StartsWith(t, "if let ") || StartsWith(t, "guard ")) v.Add("Swift", 4);
            if (StartsWith(t, "import 'package:") || StartsWith(t, "import 'dart:")) v.Add("Dart", 7);
            if (t == "@override") v.Add("Dart", 3);
            if (Contains(t, "Widget build(")) v.Add("Dart", 5);
            if (StartsWith(t, "final ") && Contains(t, " = ") && EndsWith(t, ";")) v.Add("Dart", 2);

            // ----- Go -----
            if (StartsWith(t, "func (")) v.Add("Go", 5);
            else if (StartsWith(t, "func ")) {
                if (Contains(t, ") -> ")) v.Add("Swift", 4);
                else { v.Add("Go", 3); v.Add("Swift", 2); }
            }
            if (Contains(t, " := ")) {
                if (EndsWith(t, ";")) v.Add("Pascal", 1);
                else v.Add("Go", 2);
            }
            if (t == "import (") v.Add("Go", 5);
            if (StartsWith(t, "import \"")) v.Add("Go", 4);
            if (Contains(t, "fmt.")) v.Add("Go", 4);
            if (Contains(t, "err != nil")) v.Add("Go", 5);

            // ----- Rust -----
            if (StartsWith(t, "fn ") || StartsWith(t, "pub fn ") || StartsWith(t, "pub(crate) fn ") ||
                StartsWith(t, "async fn ") || StartsWith(t, "pub async fn ")) v.Add("Rust", 5);
            if (StartsWith(t, "let mut ")) v.Add("Rust", 5);
            if (StartsWith(t, "use std::") || StartsWith(t, "use crate::") || StartsWith(t, "use super::")) {
                v.Add("Rust", 6);
            } else if (StartsWith(t, "use ") && Contains(t, "::") && EndsWith(t, ";")) {
                v.Add("Rust", 3);
            }
            if (StartsWith(t, "impl ") || StartsWith(t, "impl<")) v.Add("Rust", 5);
            if (StartsWith(t, "#[") || StartsWith(t, "#![")) v.Add("Rust", 4);
            if (Contains(t, "println!(") || Contains(t, "vec![") || Contains(t, "format!(")) v.Add("Rust", 4);
            if (StartsWith(t, "pub struct ") || StartsWith(t, "pub enum ")) v.Add("Rust", 4);

            // ----- Python -----
            if (StartsWith(t, "def ") && EndsWith(t, ":")) v.Add("Python", 4);
            else if (StartsWith(t, "async def ")) v.Add("Python", 5);
            else if (StartsWith(t, "def ") && !Contains(t, "=")) { v.Add("Ruby", 2); v.Add("Elixir", 1); }
            if (StartsWith(t, "class ") && EndsWith(t, ":")) v.Add("Python", 4);
            if (StartsWith(t, "from ") && Contains(t, " import ")) v.Add("Python", 5);
            else if (StartsWith(t, "import ") && !EndsWith(t, ";") && !Contains(t, "'") &&
                     !Contains(t, "\"") && !Contains(t, "{") && !Contains(t, "(")) {
                v.Add("Python", 2);
                if (Contains(t, ".")) { v.Add("Kotlin", 1); v.Add("Scala", 1); }
            }
            if (StartsWith(t, "if __name__")) v.Add("Python", 6);
            if (StartsWith(t, "elif ")) v.Add("Python", 4);
            if (StartsWith(t, "except") && EndsWith(t, ":")) v.Add("Python", 4);
            if (EndsWith(t, ":") && (StartsWithWord(t, "if") || StartsWithWord(t, "for") ||
                                     StartsWithWord(t, "while") || StartsWithWord(t, "with") ||
                                     t == "try:" || t == "else:" || t == "finally:")) {
                v.Add("Python", 2);
            }
            if (StartsWith(t, "self.") || Contains(t, "(self")) v.Add("Python", 2);
            if (StartsWith(t, "\"\"\"") || StartsWith(t, "'''")) v.Add("Python", 2);
            if (StartsWith(t, "print(") && !EndsWith(t, ";")) v.Add("Python", 1);

            // ----- Ruby, Elixir -----
            if (t == "end") { v.Add("Ruby", 1); v.Add("Lua", 1); v.Add("Elixir", 1); }
            if (StartsWith(t, "require '") || StartsWith(t, "require \"") || StartsWith(t, "require_relative")) {
                v.Add("Ruby", 3);
                v.Add("Lua", 1);
            }
            if (StartsWith(t, "puts ")) v.Add("Ruby", 3);
            if (StartsWith(t, "attr_accessor") || StartsWith(t, "attr_reader") || StartsWith(t, "attr_writer")) {
                v.Add("Ruby", 5);
            }
            if (Contains(t, " do |")) v.Add("Ruby", 4);
            if (StartsWith(t, "class ") && Contains(t, " < ")) v.Add("Ruby", 4);
            if (StartsWith(t, "module ") && !EndsWith(t, ";")) v.Add("Ruby", 2);
            if (StartsWith(t, "elsif ")) { v.Add("Ruby", 3); v.Add("Perl", 3); }
            if (StartsWith(t, "defmodule ")) v.Add("Elixir", 7);
            if (StartsWith(t, "defp ")) v.Add("Elixir", 5);
            if (StartsWith(t, "def ") && EndsWith(t, " do")) v.Add("Elixir", 3);
            if (StartsWith(t, "|> ")) v.Add("Elixir", 3);

            // ----- Lua -----
            if (StartsWith(t, "local function ")) v.Add("Lua", 5);
            else if (StartsWith(t, "local ")) v.Add("Lua", 3);
            if (StartsWith(t, "function ") && !EndsWith(t, "{") && Contains(t, "(") && !Contains(t, "$")) {
                if (Contains(t, " = ") || StartsWith(t, "function [")) v.Add("MATLAB", 4);
                else v.Add("Lua", 2);
            }
            if (StartsWith(t, "elseif ")) { v.Add("Lua", 2); v.Add("PHP", 1); }
            if ((StartsWith(t, "if ") || StartsWith(t, "elseif ")) && EndsWith(t, " then") && !Contains(t, ";")) {
                v.Add("Lua", 2);
            }
            if (Contains(t, " ~= ")) v.Add("Lua", 3);
            if (StartsWith(t, "--[[")) v.Add("Lua", 5);

            // ----- Perl, PHP -----
            if (StartsWith(t, "my $") || StartsWith(t, "my @") || StartsWith(t, "my %") || StartsWith(t, "my (")) {
                v.Add("Perl", 5);
            }
            if (StartsWith(t, "use strict") || StartsWith(t, "use warnings")) v.Add("Perl", 6);
            if (StartsWith(t, "sub ") && EndsWith(t, "{")) v.Add("Perl", 4);
            if (Contains(t, " =~ ")) { v.Add("Perl", 2); v.Add("Ruby", 1); }
            if (Contains(t, "$this->")) v.Add("PHP", 5);
            if (StartsWith(t, "<?=")) v.Add("PHP", 3);
            if (StartsWith(t, "function ") && Contains(t, "($")) v.Add("PHP", 3);
            if (StartsWith(t, "echo $")) v.Add("PHP", 2);
            if (StartsWith(t, "$") && Contains(t, " = ") && EndsWith(t, ";")) v.Add("PHP", 2);

            // ----- JavaScript, TypeScript -----
            if ((StartsWith(t, "const ") || StartsWith(t, "let ")) && Contains(t, " = ")) {
                v.Add("JavaScript", 1);
                v.Add("TypeScript", 1);
                if (Contains(t, "require(")) v.Add("JavaScript", 4);
            }
            if (StartsWith(t, "var ") && Contains(t, " = ") && EndsWith(t, ";")) {
                v.Add("JavaScript", 1);
                v.Add("C#", 1);
            }
            if ((StartsWith(t, "function ") || StartsWith(t, "async function ") ||
                 StartsWith(t, "export function ") || StartsWith(t, "export async function ")) &&
                EndsWith(t, "{")) {
                v.Add("JavaScript", 3);
                v.Add("TypeScript", 2);
            }
            if (Contains(t, ") => ") || Contains(t, ") => {") || Contains(t, " => {")) {
                v.Add("JavaScript", 1);
                v.Add("TypeScript", 1);
            }
            if (Contains(t, "console.log(") || Contains(t, "console.error(")) {
                v.Add("JavaScript", 3);
                v.Add("TypeScript", 2);
            }
            if (Contains(t, "module.exports")) v.Add("JavaScript", 5);
            if (Contains(t, "document.") || Contains(t, "window.")) v.Add("JavaScript", 2);
            if (StartsWith(t, "export default ") || StartsWith(t, "export const ")) {
                v.Add("JavaScript", 2);
                v.Add("TypeScript", 2);
            }
            if (StartsWith(t, "import ") && (Contains(t, " from '") || Contains(t, " from \""))) {
                v.Add("JavaScript", 3);
                v.Add("TypeScript", 3);
            }
            if (Contains(t, " === ") || Contains(t, " !== ")) {
                v.Add("JavaScript", 2);
                v.Add("TypeScript", 2);
                v.Add("PHP", 1);
            }
            if (StartsWith(t, "interface ") || StartsWith(t, "export interface ")) v.Add("TypeScript", 4);
            if ((StartsWith(t, "type ") || StartsWith(t, "export type ")) && Contains(t, " = ")) v.Add("TypeScript", 4);
            if (StartsWith(t, "import type ")) v.Add("TypeScript", 5);
            if (Contains(t, ": string") || Contains(t, ": number") || Contains(t, ": boolean") ||
                Contains(t, ": any") || Contains(t, "): void")) v.Add("TypeScript", 3);
            if (Contains(t, " readonly ")) v.Add("TypeScript", 3);
            if (Contains(t, " as const")) v.Add("TypeScript", 3);

            // ----- Shell -----
            if (t == "fi" || t == "esac" || t == "done" || t == "fi;" || t == "done;") v.Add("Shell Script", 4);
            if (Contains(t, "; then") || Contains(t, "; do")) v.Add("Shell Script", 4);
            if (StartsWith(t, "if [") || StartsWith(t, "[[ ") || StartsWith(t, "while [")) v.Add("Shell Script", 4);
            if (StartsWith(t, "export ") && Contains(t, "=") && !Contains(t, " = ") && !EndsWith(t, ";")) {
                v.Add("Shell Script", 3);
            }
            if (StartsWith(t, "echo ")) v.Add("Shell Script", 1);
            if (StartsWith(t, "sudo ") || StartsWith(t, "apt ") || StartsWith(t, "apt-get ") ||
                StartsWith(t, "cd ") || StartsWith(t, "mkdir ") || StartsWith(t, "chmod ") ||
                StartsWith(t, "rm -") || StartsWith(t, "git ") || StartsWith(t, "cmake ") ||
                StartsWith(t, "make ") || StartsWith(t, "npm ") || StartsWith(t, "pip ") ||
                StartsWith(t, "curl ") || StartsWith(t, "$ ")) {
                v.Add("Shell Script", 2);
            }
            if (Contains(t, "$(") && !Contains(t, "$(\"") && !Contains(t, "$('")) v.Add("Shell Script", 1);

            // ----- SQL -----
            if (StartsWith(lt, "select ") && (Contains(lt, " from ") || EndsWith(lt, "select *"))) v.Add("SQL", 4);
            if (StartsWith(lt, "insert into ") || StartsWith(lt, "delete from ")) v.Add("SQL", 5);
            if (StartsWith(lt, "create table") || StartsWith(lt, "alter table") || StartsWith(lt, "drop table") ||
                StartsWith(lt, "create index") || StartsWith(lt, "create view") ||
                StartsWith(lt, "create database") || StartsWith(lt, "create unique index")) v.Add("SQL", 6);
            if (StartsWith(lt, "update ") && Contains(lt, " set ")) v.Add("SQL", 4);
            if ((StartsWith(lt, "from ") && !Contains(lt, " import ")) || StartsWith(lt, "where ") ||
                StartsWith(lt, "inner join ") || StartsWith(lt, "left join ") || StartsWith(lt, "join ") ||
                StartsWith(lt, "group by ") || StartsWith(lt, "order by ")) v.Add("SQL", 1);

            // ----- CSS -----
            if (StartsWith(t, "@media") || StartsWith(t, "@keyframes") || StartsWith(t, "@font-face") ||
                StartsWith(t, "@charset")) v.Add("CSS", 5);
            if (StartsWith(t, "@import url(") || StartsWith(t, "@import \"")) v.Add("CSS", 3);
            if (IsCssSelector(t)) v.Add("CSS", 3);
            else if (EndsWith(t, " {") && !Contains(t, "(") && !Contains(t, "=") && !Contains(t, ";") &&
                     t.find_first_of(".>:,") != std::string::npos &&
                     std::islower(static_cast<unsigned char>(t[0]))) {
                v.Add("CSS", 1);   // `body, html {`, `ul li.active {`
            }
            if (IsCssDeclaration(t)) v.Add("CSS", 2);

            // ----- Pascal, Fortran, BASIC / VBA -----
            if (StartsWith(lt, "program ") && EndsWith(lt, ";")) v.Add("Pascal", 6);
            if (lt == "begin") v.Add("Pascal", 3);
            if (lt == "end;" || lt == "end.") v.Add("Pascal", 3);
            if ((StartsWith(lt, "procedure ") || StartsWith(lt, "function ")) && EndsWith(lt, ";")) v.Add("Pascal", 3);
            if (StartsWith(lt, "uses ") && EndsWith(lt, ";")) v.Add("Pascal", 5);
            if (StartsWith(lt, "writeln(")) v.Add("Pascal", 4);
            if (lt == "var") v.Add("Pascal", 2);
            if (lt == "implicit none") v.Add("Fortran", 7);
            if (StartsWith(lt, "end program") || StartsWith(lt, "end subroutine") || lt == "end do" ||
                lt == "end if" || StartsWith(lt, "end module")) v.Add("Fortran", 4);
            if (StartsWith(lt, "subroutine ")) v.Add("Fortran", 4);
            if (StartsWith(lt, "program ") && !EndsWith(lt, ";")) v.Add("Fortran", 2);
            if ((StartsWith(lt, "integer") || StartsWith(lt, "real") || StartsWith(lt, "character") ||
                 StartsWith(lt, "logical")) && Contains(lt, " :: ")) v.Add("Fortran", 4);
            if (StartsWith(lt, "write(*,") || StartsWith(lt, "print *,")) v.Add("Fortran", 5);
            if (StartsWith(lt, "option explicit")) v.Add("VBA", 7);
            if (StartsWith(lt, "end sub") || StartsWith(lt, "end function")) v.Add("VBA", 4);
            if ((StartsWith(lt, "sub ") || StartsWith(lt, "public sub ") || StartsWith(lt, "private sub ")) &&
                Contains(lt, "(")) v.Add("VBA", 4);
            if (StartsWith(lt, "dim ") && Contains(lt, " as ")) v.Add("VBA", 4);
            if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) {
                size_t i = 0;
                while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) i++;
                if (i < t.size() && t[i] == ' ') {
                    const std::string rest = t.substr(i + 1);
                    if (StartsWith(rest, "PRINT") || StartsWith(rest, "GOTO") || StartsWith(rest, "LET ") ||
                        StartsWith(rest, "IF ") || StartsWith(rest, "INPUT") || StartsWith(rest, "GOSUB") ||
                        StartsWith(rest, "REM") || StartsWith(rest, "FOR ") || StartsWith(rest, "NEXT")) {
                        v.Add("BASIC", 4);
                    }
                }
            }

            // ----- R, MATLAB -----
            if (Contains(t, " <- ") && !Contains(t, "<<")) v.Add("R", 3);
            if (StartsWith(t, "library(") || StartsWith(t, "require(")) v.Add("R", 5);
            if (Contains(t, "data.frame(")) v.Add("R", 4);
            if (StartsWith(t, "disp(")) v.Add("MATLAB", 3);
            if (StartsWith(t, "% ") || t == "%") v.Add("MATLAB", 1);

            // ----- Lisp -----
            if (StartsWith(t, "(defun ") || StartsWith(t, "(defvar ") || StartsWith(t, "(defmacro ") ||
                StartsWith(t, "(defparameter ") || StartsWith(t, "(defpackage ")) v.Add("Common Lisp", 5);
            if (StartsWith(t, "(define ")) v.Add("Lisp", 5);

            // ----- Prolog -----
            if (EndsWith(t, ".") && Contains(t, " :- ")) v.Add("Prolog", 4);
            if (StartsWith(t, ":- ")) v.Add("Prolog", 4);

            // ----- x86 assembly -----
            if (StartsWith(lt, "section .") || StartsWith(lt, "global _start") || StartsWith(lt, "global main") ||
                StartsWith(lt, ".intel_syntax")) v.Add("x86 Assembly", 5);
            if ((StartsWith(lt, "mov ") || StartsWith(lt, "push ") || StartsWith(lt, "pop ") ||
                 StartsWith(lt, "xor ") || StartsWith(lt, "add ") || StartsWith(lt, "sub ") ||
                 StartsWith(lt, "lea ") || StartsWith(lt, "cmp ") || StartsWith(lt, "inc ") ||
                 StartsWith(lt, "movq ") || StartsWith(lt, "movl ")) && HasX86Register(lt)) {
                v.Add("x86 Assembly", 3);
            }
            if (lt == "syscall" || lt == "int 0x80" || lt == "ret") v.Add("x86 Assembly", 2);

            // ----- HTML fragments -----
            if (t[0] == '<' && t.size() > 2) {
                size_t end = 1;
                while (end < t.size() && std::isalnum(static_cast<unsigned char>(t[end]))) end++;
                if (end > 1 && IsHtmlElementName(ToLower(t.substr(1, end - 1)))) v.Add("HTML", 2);
            }
        }

    } // namespace

    std::string DetectLanguageFromContent(const std::string& text) {
        const std::string head = text.substr(0, kScanBytes);
        const std::string trimmed = Trim(head);
        if (trimmed.empty()) return "";

        // The first line - past a UTF-8 BOM - may name its interpreter.
        std::string firstLine = trimmed.substr(0, trimmed.find('\n'));
        if (StartsWith(firstLine, "\xEF\xBB\xBF")) firstLine.erase(0, 3);
        const std::string fromShebang = LanguageFromShebang(Trim(firstLine));
        if (!fromShebang.empty()) return fromShebang;

        // A document that is wholly one value or one tree.
        if (head.size() == text.size()) {
            const std::string json = LanguageFromJson(trimmed);
            if (!json.empty()) return json;
        }
        const std::string markup = LanguageFromMarkup(head.size() == text.size() ? trimmed : Trim(text));
        if (!markup.empty()) return markup;

        Votes votes;
        bool inFence = false;
        std::string previous;          // the last non-blank line, trimmed
        int nonBlank = 0, yamlLines = 0, yamlKeys = 0;
        bool firstNonBlank = true;

        size_t pos = 0;
        while (pos < head.size()) {
            size_t end = head.find('\n', pos);
            if (end == std::string::npos) end = head.size();
            std::string t = Trim(head.substr(pos, std::min(end - pos, kMaxLineBytes)));
            pos = end + 1;
            if (t.empty()) continue;
            nonBlank++;

            // ----- Markdown, and the code fences it hides -----
            if (StartsWith(t, "```") || StartsWith(t, "~~~")) {
                votes.Add("Markdown", 3);
                inFence = !inFence;
                previous = t;
                firstNonBlank = false;
                continue;
            }
            if (inFence) continue;

            if (t[0] == '#') {
                size_t level = 0;
                while (level < t.size() && t[level] == '#') level++;
                if (level <= 6 && level < t.size() && t[level] == ' ' && level + 1 < t.size()) {
                    // "# comment" is also every #-commenting language's comment,
                    // so a single-hash heading counts only when it opens the text.
                    if (level >= 2) votes.Add("Markdown", 3);
                    else votes.Add("Markdown", firstNonBlank ? 3 : 1);
                }
            }
            if (HasMarkdownLink(t)) votes.Add("Markdown", 2);
            if (StartsWith(t, "- [ ] ") || StartsWith(t, "- [x] ") || StartsWith(t, "* [ ] ")) votes.Add("Markdown", 3);
            if (IsMarkdownTableRule(t)) votes.Add("Markdown", 4);
            if (!previous.empty() && !IsAll(previous, '-', 1) && !IsAll(previous, '=', 1)) {
                if (IsAll(t, '=', 3)) votes.Add("Markdown", 3);
            }
            if (StartsWith(t, "> ")) votes.Add("Markdown", 1);
            const size_t bold = t.find("**");
            if (bold != std::string::npos && t.find("**", bold + 2) != std::string::npos) votes.Add("Markdown", 1);
            const bool listItem = StartsWith(t, "- ") || StartsWith(t, "* ") || StartsWith(t, "+ ") ||
                                  IsOrderedListItem(t);
            if (listItem) votes.Add("Markdown", 1);

            // ----- YAML is judged on the whole -----
            if (t[0] == '#') {
                yamlLines++;
            } else if (IsYamlKeyLine(t) && !IsPythonBlockKeyword(t)) {
                yamlLines++;
                yamlKeys++;
            } else if (StartsWith(t, "- ") || t == "-" || t == "---" || t == "...") {
                yamlLines++;
            }

            VoteLine(t, votes);
            previous = t;
            firstNonBlank = false;
        }

        // Every line YAML (comments, keys, list items) and a few keys - prose
        // with one "Note: ..." line, or code, is not.
        if (yamlKeys >= 2 && yamlLines * 10 >= nonBlank * 9) {
            votes.Add("YAML", 2 * yamlKeys);
        }

        // C++ reads C and TypeScript reads JavaScript: within such a family
        // any lead decides, and a tie goes to the one that reads both. Against
        // every other language the winner must be clearly ahead.
        auto familyOf = [](const std::string& language) -> std::string {
            if (language == "C" || language == "C++") return "C++";
            if (language == "JavaScript" || language == "TypeScript") return "JavaScript";
            return language;
        };
        std::string best;
        int bestScore = 0;
        for (const auto& [language, score] : votes.All()) {
            const bool tieGoesHere = score == bestScore && familyOf(best) == familyOf(language) &&
                                     familyOf(language) == language;
            if (score > bestScore || tieGoesHere) {
                best = language;
                bestScore = score;
            }
        }
        if (bestScore < kMinScore) return "";
        int rivalScore = 0;
        for (const auto& [language, score] : votes.All()) {
            if (familyOf(language) != familyOf(best)) rivalScore = std::max(rivalScore, score);
        }
        return bestScore - rivalScore >= kMinLead ? best : "";
    }

} // namespace UltraCanvas
