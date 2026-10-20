// Apps/Texter/UltraCanvasLanguageDetect.h
// Guess a text's language from its content alone
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {

    // The language a piece of text is written in, judged from the text alone -
    // what UltraTexter uses when text is pasted into a new, empty tab that has
    // no file name to go by. Returns one of the names the syntax highlighter
    // registers ("C++", "Python", "JSON", ...) or "Markdown", or "" when the
    // text does not say clearly enough (prose, a one-word paste): a wrong
    // guess is worse than plain text, so the answer is "" unless one language
    // is ahead.
    //
    // Decided in this order: a #! line names the interpreter; a document that
    // opens as one (<?php, <?xml, <!DOCTYPE html, a complete JSON value) is
    // that; otherwise every line votes for the languages whose syntax it can
    // only be, and the clear winner is the answer. Lines inside a Markdown
    // code fence vote for Markdown, not for the code they show.
    std::string DetectLanguageFromContent(const std::string& text);

} // namespace UltraCanvas
