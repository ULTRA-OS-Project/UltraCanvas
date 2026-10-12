// Apps/EmailCleaner/engine/EmailCleanerText.h
// The text pipeline both sides of the keyword match run through.
//
// This exists as its own module for one reason: a rule term and the message
// text must be normalised *identically*. If only the message were folded,
// a rule written as "no-reply@" could never match text in which '@' has
// already become 'a'. So RuleSet normalises every term it stores with the same
// functions the Classifier applies to a message.
//
// Pure string work: no database, no network, no UI.
// Version: 0.2.0 - no StripHtml of its own: HTML goes through HTML::ExtractPlainText
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace EmailCleaner {

// Collapse letter-separator obfuscation: "v.i.a.g.r.a", "v-i-a-g-r-a" and
// "v i a g r a" all become "viagra". Ordinary prose is left alone — a run has
// to be at least three letters long (five when the separator is a space, so
// "a b test" and "Plan B is ready" survive).
std::string CollapseObfuscation(const std::string& text);

// The full pipeline: strip HTML, lowercase, fold the leet substitutions spam
// relies on (0->o, 1->i, 3->e, 4->a, 5->s, 7->t, $->s, @->a), undo separator
// obfuscation, collapse whitespace. Applied to message text and to rule terms
// alike.
//
// Text with a '<' in it is read as HTML through the HTMLReader module
// (HTML::ExtractPlainText): <script> and <style> contents dropped, every
// entity decoded, a no-break space a space. An inline formatting element
// (<b>, <span>, <a> ...) leaves no space, because "<b>via</b>gra" is one word
// split for camouflage; a block-level element is a real word boundary. En and
// em dashes fold to "-" and the ellipsis to "...".
//
// Anything in angle brackets is markup, so a bare address written
// "<erika@example.com>" in a plain-text body goes with it. That costs nothing
// where it matters: the classifier matches sender rules against the display
// name and address ParseAddress has already separated, not against a raw
// From: header.
std::string NormalizeForMatching(const std::string& text);

} // namespace EmailCleaner
