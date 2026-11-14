// core/UltraCanvasSyntaxHighlighter.cpp
// Implementation of Tokenize and TokenizeLine methods for SyntaxTokenizer
// Version: 1.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "UltraCanvasSyntaxTokenizer.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace UltraCanvas {

// Returns the byte length of a UTF-8 character from its leading byte
    static int Utf8CharBytes(unsigned char leadByte) {
        if (leadByte < 0x80) return 1;
        if ((leadByte & 0xE0) == 0xC0) return 2;
        if ((leadByte & 0xF0) == 0xE0) return 3;
        if ((leadByte & 0xF8) == 0xF0) return 4;
        return 1; // fallback for continuation/invalid bytes
    }

// Tokenize a whole text: line by line, each line continuing from what the
// line above left open, exactly as the text area highlights it - one
// Newline token between lines (for \n or \r\n).
    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::Tokenize(const std::string& text) const {
        std::vector<Token> tokens;
        if (!currentRules || text.empty()) {
            return tokens;
        }
        SyntaxLineState state;
        size_t start = 0;
        while (start <= text.length()) {
            size_t end = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto lineTokens = TokenizeLine(line, state);
            tokens.insert(tokens.end(), lineTokens.begin(), lineTokens.end());
            if (end == std::string::npos) break;
            tokens.push_back({TokenType::Newline, "\n", 1});
            state = state.AtLineBreak();
            start = end + 1;
        }
        return tokens;
    }

// End of a string whose opening quote is just before `from`: past the
// closing quote, or the line end with `closed` false.
    static size_t FindStringEnd(const std::string& line, size_t from, char delimiter,
                                bool escapes, bool& closed) {
        size_t i = from;
        while (i < line.length()) {
            if (escapes && line[i] == '\\' && i + 1 < line.length()) { i += 2; continue; }
            if (line[i] == delimiter) { closed = true; return i + 1; }
            i++;
        }
        closed = false;
        return line.length();
    }

// The same for a string closed by a multi-character delimiter (""", ]]).
    static size_t FindLongStringEnd(const std::string& line, size_t from, const std::string& endDelim,
                                    bool escapes, bool& closed) {
        size_t i = from;
        while (i < line.length()) {
            if (escapes && line[i] == '\\' && i + 1 < line.length()) { i += 2; continue; }
            if (line.compare(i, endDelim.length(), endDelim) == 0) {
                closed = true;
                return i + endDelim.length();
            }
            i++;
        }
        closed = false;
        return line.length();
    }

// A C++ raw string opening at `pos`: [u8|u|U|L]R"delim( - not inside a word
// (myR"(...)" is an identifier and a string). On a match, `bodyStart` is just
// past the "(" and `endMarker` is )delim". The delimiter is at most 16
// characters, none of them a space, ( ) or \.
    static bool MatchRawStringOpen(const std::string& line, size_t pos,
                                   size_t& bodyStart, std::string& endMarker) {
        if (pos > 0) {
            unsigned char prev = static_cast<unsigned char>(line[pos - 1]);
            if (std::isalnum(prev) || prev == '_') return false;
        }
        size_t i = pos;
        if (line.compare(i, 2, "u8") == 0) i += 2;
        else if (i < line.length() && (line[i] == 'u' || line[i] == 'U' || line[i] == 'L')) i += 1;
        if (line.compare(i, 2, "R\"") != 0) return false;
        i += 2;
        size_t delimStart = i;
        while (i < line.length() && line[i] != '(') {
            char c = line[i];
            if (c == ' ' || c == ')' || c == '\\' || c == '\t' || i - delimStart >= 16) return false;
            i++;
        }
        if (i >= line.length()) return false;
        endMarker = ")" + line.substr(delimStart, i - delimStart) + "\"";
        bodyStart = i + 1;
        return true;
    }

// A Lua long bracket opening at `pos`: '[', any number of '=', '['. On a
// match, `bodyStart` is just past it and `endMarker` is ']', as many '=', ']'.
    static bool MatchLongBracket(const std::string& line, size_t pos,
                                 size_t& bodyStart, std::string& endMarker) {
        if (pos >= line.length() || line[pos] != '[') return false;
        size_t i = pos + 1;
        while (i < line.length() && line[i] == '=') i++;
        if (i >= line.length() || line[i] != '[') return false;
        const size_t level = i - pos - 1;
        endMarker = "]" + std::string(level, '=') + "]";
        bodyStart = i + 1;
        return true;
    }

// A Rust raw string opening at `pos`: [b|c]r, any number of '#', '"' - not
// inside a word, and not a raw identifier (r#match has no quote). On a match
// `endMarker` is '"' and as many '#'.
    static bool MatchHashRawString(const std::string& line, size_t pos,
                                   size_t& bodyStart, std::string& endMarker) {
        if (pos > 0) {
            unsigned char prev = static_cast<unsigned char>(line[pos - 1]);
            if (std::isalnum(prev) || prev == '_') return false;
        }
        size_t i = pos;
        if (i < line.length() && (line[i] == 'b' || line[i] == 'c')) i++;
        if (i >= line.length() || line[i] != 'r') return false;
        i++;
        size_t hashStart = i;
        while (i < line.length() && line[i] == '#') i++;
        if (i >= line.length() || line[i] != '"') return false;
        endMarker = "\"" + std::string(i - hashStart, '#');
        bodyStart = i + 1;
        return true;
    }

// A C# verbatim string opening at `pos`: @", $@" or @$" - not inside a word.
    static bool MatchVerbatimString(const std::string& line, size_t pos, size_t& bodyStart) {
        if (pos > 0) {
            unsigned char prev = static_cast<unsigned char>(line[pos - 1]);
            if (std::isalnum(prev) || prev == '_' || prev == '@' || prev == '$') return false;
        }
        for (const char* open : {"@\"", "$@\"", "@$\""}) {
            size_t n = std::strlen(open);
            if (line.compare(pos, n, open) == 0) {
                bodyStart = pos + n;
                return true;
            }
        }
        return false;
    }

// A C# 11 raw string opening at `pos`: any number of '$' (interpolated), then
// three or more quotes - not inside a word. `endMarker` is the same run of
// quotes; an ordinary "" (two quotes) is an empty string, not this.
    static bool MatchQuotedRawString(const std::string& line, size_t pos,
                                     size_t& bodyStart, std::string& endMarker) {
        if (pos > 0) {
            unsigned char prev = static_cast<unsigned char>(line[pos - 1]);
            if (std::isalnum(prev) || prev == '_' || prev == '$' || prev == '"' || prev == '@') return false;
        }
        size_t i = pos;
        while (i < line.length() && line[i] == '$') i++;
        size_t quoteStart = i;
        while (i < line.length() && line[i] == '"') i++;
        if (i - quoteStart < 3) return false;
        endMarker = std::string(i - quoteStart, '"');
        bodyStart = i;
        return true;
    }

// A string that ends only at its own marker - a C++ raw string, a Lua long
// bracket, a Rust raw string, a C# verbatim string - opening at `pos`, as
// the language's rules allow. `doubledQuotes`: a doubled marker inside it
// is a quote character (C#).
    static bool MatchMarkedString(const SyntaxTokenizationRules& rules, const std::string& line,
                                  size_t pos, size_t& bodyStart, std::string& endMarker,
                                  bool& doubledQuotes) {
        doubledQuotes = false;
        if (rules.levelledLongBrackets && MatchLongBracket(line, pos, bodyStart, endMarker)) return true;
        if (rules.delimitedRawStrings && MatchRawStringOpen(line, pos, bodyStart, endMarker)) return true;
        if (rules.hashRawStrings && MatchHashRawString(line, pos, bodyStart, endMarker)) return true;
        if (rules.quotedRawStrings && MatchQuotedRawString(line, pos, bodyStart, endMarker)) return true;
        if (rules.verbatimStrings && MatchVerbatimString(line, pos, bodyStart)) {
            endMarker = "\"";
            doubledQuotes = true;
            return true;
        }
        return false;
    }

// Where such a string ends from `from` on: just past its marker, or npos
// when it is still open at the end of the line.
    static size_t FindMarkedEnd(const std::string& line, size_t from, const std::string& marker,
                                bool doubledQuotes) {
        size_t i = from;
        while (true) {
            size_t at = line.find(marker, i);
            if (at == std::string::npos) return std::string::npos;
            if (doubledQuotes && line.compare(at + marker.length(), marker.length(), marker) == 0) {
                i = at + 2 * marker.length();   // "" - a quote inside the string
                continue;
            }
            return at + marker.length();
        }
    }

// Does a string opened with `delimiter` and still open at the end of `line`
// carry on into the next line? Per language: a quote whose strings span lines,
// or a backslash continuation (an odd run of backslashes ends the line).
    static bool StringContinuesPastLine(const SyntaxTokenizationRules& rules,
                                        const std::string& line, char delimiter) {
        if (std::find(rules.multiLineStringDelimiters.begin(), rules.multiLineStringDelimiters.end(),
                      delimiter) != rules.multiLineStringDelimiters.end()) {
            return true;
        }
        if (!rules.stringLineContinuation) return false;
        size_t backslashes = 0;
        for (size_t i = line.length(); i > 0 && line[i - 1] == '\\'; --i) backslashes++;
        return backslashes % 2 == 1;
    }

// A string token just added to `tokens` takes in a prefix written directly
// before its quote (Python's rb, C's u8): the identifier right before it -
// nothing in between - when the language lists it, in any case.
    static void AbsorbStringPrefix(std::vector<SyntaxTokenizer::Token>& tokens,
                                   const SyntaxTokenizationRules& rules) {
        if (rules.stringPrefixes.empty() || tokens.size() < 2) return;
        SyntaxTokenizer::Token& prefix = tokens[tokens.size() - 2];
        if (prefix.type != TokenType::Identifier) return;
        std::string lower = prefix.text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        bool listed = false;
        for (const std::string& p : rules.stringPrefixes) {
            std::string lp = p;
            std::transform(lp.begin(), lp.end(), lp.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lp == lower) { listed = true; break; }
        }
        if (!listed) return;
        prefix.type = TokenType::String;
        prefix.text += tokens.back().text;
        prefix.length = prefix.text.size();
        tokens.pop_back();
    }

// Tokenize a single line with no knowledge of the lines around it
    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::TokenizeLine(const std::string& line, int /*lineNumber*/) const {
        if (!currentRules || line.empty()) {
            return {};
        }
        SyntaxLineState state;
        if (currentRules->isCss) {
            return TokenizeCssLine(line, state, true);
        }
        return TokenizeLine(line, state);
    }

// Tokenize one line of a sequence, continuing what the line above left open
    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::TokenizeLine(const std::string& line, SyntaxLineState& state) const {
        if (!currentRules || line.empty()) {
            return {};
        }

        if (currentRules->isCss) {
            return TokenizeCssLine(line, state, false);
        }

        // A preprocessor directive: a line whose first character is '#' -
        // unless it starts inside a comment or string the line above left
        // open - or the next line of one continued with a backslash. It is
        // tokenized like code, so a string, raw string or comment in it keeps
        // its colour and its state; everything else takes the directive's.
        bool directive = false;
        if (currentRules->hasPreprocessor) {
            const bool startsClean = state.blockComment < 0 && state.blockCommentEnd.empty() &&
                                     !state.lineComment && state.openString == 0 &&
                                     state.openLongString < 0 && state.rawStringEnd.empty();
            const size_t first = line.find_first_not_of(" \t");
            directive = state.inDirective ||
                        (startsClean && first != std::string::npos && line[first] == '#');
        }

        std::vector<Token> tokens = TokenizeCodeLine(line, state);

        if (directive) {
            for (Token& t : tokens) {
                if (t.type != TokenType::String && t.type != TokenType::Character &&
                    t.type != TokenType::Comment && t.type != TokenType::Whitespace) {
                    t.type = TokenType::Preprocessor;
                }
            }
            size_t backslashes = 0;
            for (size_t i = line.length(); i > 0 && line[i - 1] == '\\'; --i) backslashes++;
            state.inDirective = true;
            state.directiveContinues = backslashes % 2 == 1;
        } else {
            state.inDirective = false;
            state.directiveContinues = false;
        }
        return tokens;
    }

    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::TokenizeCodeLine(const std::string& line, SyntaxLineState& state) const {
        std::vector<Token> tokens;

        size_t position = 0;
        int currentColumn = 0;
        std::string whitespacePrefix = "";

        // What the line above left open: a block comment, or - in the next
        // segment of a line split for length - a line comment or a string.
        if (state.blockComment >= 0 &&
            state.blockComment < static_cast<int>(currentRules->multiLineComments.size())) {
            const std::string& endDelim = currentRules->multiLineComments[state.blockComment].second;
            size_t endPos = line.find(endDelim);
            if (endPos == std::string::npos) {
                tokens.push_back({TokenType::Comment, line, line.length()});
                return tokens;
            }
            position = endPos + endDelim.length();
            tokens.push_back({TokenType::Comment, line.substr(0, position), position});
        }
        state.blockComment = -1;
        if (!state.blockCommentEnd.empty()) {
            size_t endPos = line.find(state.blockCommentEnd, position);
            if (endPos == std::string::npos) {
                tokens.push_back({TokenType::Comment, line.substr(position), line.length() - position});
                return tokens;
            }
            size_t end = endPos + state.blockCommentEnd.length();
            tokens.push_back({TokenType::Comment, line.substr(position, end - position), end - position});
            position = end;
            state.blockCommentEnd.clear();
        }
        if (state.lineComment) {
            tokens.push_back({TokenType::Comment, line, line.length()});
            return tokens;
        }
        if (state.openLongString >= 0 &&
            state.openLongString < static_cast<int>(currentRules->multiLineStrings.size())) {
            bool closed = false;
            size_t end = FindLongStringEnd(line, 0, currentRules->multiLineStrings[state.openLongString].second,
                                           currentRules->hasEscapeSequences, closed);
            tokens.push_back({TokenType::String, line.substr(0, end), end});
            if (!closed) return tokens;
            position = end;
        }
        state.openLongString = -1;
        if (!state.rawStringEnd.empty()) {
            size_t end = FindMarkedEnd(line, 0, state.rawStringEnd, state.rawStringDoubledQuotes);
            if (end == std::string::npos) {
                tokens.push_back({TokenType::String, line, line.length()});
                return tokens;
            }
            tokens.push_back({TokenType::String, line.substr(0, end), end});
            position = end;
            state.rawStringEnd.clear();
            state.rawStringDoubledQuotes = false;
        }
        if (state.openString) {
            bool closed = false;
            size_t end = FindStringEnd(line, 0, state.openString, currentRules->hasEscapeSequences, closed);
            tokens.push_back({TokenType::String, line.substr(0, end), end});
            if (!closed) {
                state.openStringSpansLines = StringContinuesPastLine(*currentRules, line, state.openString);
                return tokens;
            }
            position = end;
            state.openString = 0;
            state.openStringSpansLines = false;
        }

        while (position < line.length()) {
            // Skip whitespace
            if (IsWhitespace(line[position])) {
                whitespacePrefix.push_back(line[position]);
                currentColumn++;
                position++;
                continue;
            }

            if (!whitespacePrefix.empty()) {
                Token token;
                token.type = TokenType::Whitespace;
                token.length = whitespacePrefix.length();
                token.text = whitespacePrefix;
                tokens.push_back(token);
                whitespacePrefix.clear();
            }

            Token token;
//            token.line = lineNumber;
//            token.column = currentColumn;
//            token.start = position;

            // Block comments before line comments: in some languages the
            // block opener starts with the line-comment marker (Lua's --[[
            // after --, MATLAB's %{ after %), and the line comment would take
            // it and the rest of the line, leaving the block never opened.
            if (currentRules->levelledLongBrackets && line.compare(position, 2, "--") == 0) {
                size_t bodyStart = 0;
                std::string endMarker;
                if (MatchLongBracket(line, position + 2, bodyStart, endMarker)) {
                    size_t endPos = line.find(endMarker, bodyStart);
                    size_t end = endPos == std::string::npos ? line.length() : endPos + endMarker.length();
                    token.type = TokenType::Comment;
                    token.length = end - position;
                    token.text = line.substr(position, token.length);
                    tokens.push_back(token);
                    if (endPos == std::string::npos) {
                        state.blockCommentEnd = endMarker;
                        return tokens;
                    }
                    position = end;
                    continue;
                }
            }
            bool foundComment = false;
            for (size_t k = 0; k < currentRules->multiLineComments.size(); ++k) {
                const auto& [startDelim, endDelim] = currentRules->multiLineComments[k];
                if (line.substr(position, startDelim.length()) == startDelim) {
                    // Find the end delimiter on the same line
                    size_t endPos = line.find(endDelim, position + startDelim.length());
                    if (endPos != std::string::npos) {
                        token.type = TokenType::Comment;
                        token.length = endPos + endDelim.length() - position;
                        token.text = line.substr(position, token.length);
                        tokens.push_back(token);

//                        currentColumn += token.length;
                        position += token.length;
                        foundComment = true;
                        break;
                    } else {
                        // Comment continues to next line
                        token.type = TokenType::Comment;
                        token.length = line.length() - position;
                        token.text = line.substr(position);
                        tokens.push_back(token);
                        state.blockComment = static_cast<int8_t>(k);
                        return tokens;
                    }
                }
            }

            if (foundComment) continue;

            // Try to match single-line comments
            for (const auto& commentPrefix : currentRules->singleLineComments) {
                if (line.substr(position, commentPrefix.length()) == commentPrefix) {
                    token.type = TokenType::Comment;
                    token.length = line.length() - position;
                    token.text = line.substr(position);
                    tokens.push_back(token);
                    state.lineComment = true;
                    return tokens; // Rest of line is comment
                }
            }

            // Strings that end only at their own marker, with no escapes:
            // C++ raw strings, Lua long brackets, Rust raw strings, C#
            // verbatim strings.
            {
                size_t bodyStart = 0;
                std::string endMarker;
                bool doubledQuotes = false;
                if (MatchMarkedString(*currentRules, line, position, bodyStart, endMarker, doubledQuotes)) {
                    size_t endAt = FindMarkedEnd(line, bodyStart, endMarker, doubledQuotes);
                    size_t end = endAt == std::string::npos ? line.length() : endAt;
                    token.type = TokenType::String;
                    token.length = end - position;
                    token.text = line.substr(position, token.length);
                    tokens.push_back(token);
                    if (endAt == std::string::npos) {
                        state.rawStringEnd = endMarker;
                        state.rawStringDoubledQuotes = doubledQuotes;
                        return tokens;
                    }
                    currentColumn += token.length;
                    position = end;
                    continue;
                }
            }

            // Strings that may span lines and open with more than one
            // character (""", [[): before the one-character delimiters,
            // which would read """ as an empty string and a quote.
            bool foundLongString = false;
            for (size_t k = 0; k < currentRules->multiLineStrings.size(); ++k) {
                const auto& [openDelim, closeDelim] = currentRules->multiLineStrings[k];
                if (line.compare(position, openDelim.length(), openDelim) != 0) continue;
                bool closed = false;
                size_t end = FindLongStringEnd(line, position + openDelim.length(), closeDelim,
                                               currentRules->hasEscapeSequences, closed);
                token.type = TokenType::String;
                token.length = end - position;
                token.text = line.substr(position, token.length);
                tokens.push_back(token);
                AbsorbStringPrefix(tokens, *currentRules);
                if (!closed) {
                    state.openLongString = static_cast<int8_t>(k);
                    return tokens;
                }
                currentColumn += token.length;
                position = end;
                foundLongString = true;
                break;
            }
            if (foundLongString) continue;

            // Try to match strings
            if (IsStringDelimiter(line[position])) {
                bool closed = false;
                size_t end = FindStringEnd(line, position + 1, line[position],
                                           currentRules->hasEscapeSequences, closed);
                token.type = TokenType::String;
                token.length = end - position;
                token.text = line.substr(position, token.length);
                tokens.push_back(token);
                AbsorbStringPrefix(tokens, *currentRules);
                if (!closed) {
                    state.openString = line[position];
                    state.openStringSpansLines = StringContinuesPastLine(*currentRules, line, line[position]);
                    return tokens;
                }

                currentColumn += token.length;
                position = end;
                continue;
            }

            // Try to match character literals
            if (IsCharacterDelimiter(line[position])) {
                auto charResult = ParseCharacterInLine(line, position);
                if (charResult.first > position) {
                    token.type = TokenType::Character;
                    token.length = charResult.first - position;
                    token.text = line.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = charResult.first;
                    continue;
                }
            }
            if (IsDigit(line[position]) ||
                (line[position] == '.' && position + 1 < line.length() && IsDigit(line[position + 1])) ||
                (currentRules && !currentRules->hexNumberPrefix.empty() &&
                 line.compare(position, currentRules->hexNumberPrefix.length(), currentRules->hexNumberPrefix) == 0 &&
                 position + currentRules->hexNumberPrefix.length() < line.length() &&
                 IsHexDigit(line[position + currentRules->hexNumberPrefix.length()]))) {

                auto numberResult = ParseNumberInLine(line, position);
                if (numberResult.first > position) {
                    token.type = numberResult.second;
                    token.length = numberResult.first - position;
                    token.text = line.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = numberResult.first;
                    continue;
                }
            }

            // Try to match operators
            auto operatorResult = ParseOperatorInLine(line, position);
            if (operatorResult.first > position) {
                token.type = operatorResult.second;
                token.length = operatorResult.first - position;
                token.text = line.substr(position, token.length);
                tokens.push_back(token);

//                currentColumn += token.length;
                position = operatorResult.first;
                continue;
            }

            // Try to match words (keywords, identifiers, etc.)
            if (IsWordCharacter(line[position]) || line[position] == '_') {
                auto wordResult = ParseWordInLine(line, position);
                if (wordResult.first > position) {
                    std::string word = line.substr(position, wordResult.first - position);
                    token.type = ClassifyWord(word);
                    token.length = wordResult.first - position;
                    token.text = word;
                    tokens.push_back(token);

//                    currentColumn += token.length;
                    position = wordResult.first;
                    continue;
                }
            }

            // Handle punctuation and other single characters (UTF-8 aware)
            int charBytes = Utf8CharBytes(static_cast<unsigned char>(line[position]));
            token.type = TokenType::Punctuation;
            token.length = charBytes;
            token.text = line.substr(position, charBytes);
            tokens.push_back(token);

            currentColumn++;
            position += charBytes;
        }

        return tokens;
    }

// ===== HELPER METHODS IMPLEMENTATION =====

// Check if a word is a keyword
    bool SyntaxTokenizer::IsKeyword(const std::string& word) const {
        if (!currentRules) return false;
        return currentRules->keywords.find(word) != currentRules->keywords.end();
    }

// Check if a word is a type
    bool SyntaxTokenizer::IsType(const std::string& word) const {
        if (!currentRules) return false;
        return currentRules->types.find(word) != currentRules->types.end();
    }

// Check if a word is a built-in function
    bool SyntaxTokenizer::IsBuiltin(const std::string& word) const {
        if (!currentRules) return false;
        return currentRules->builtins.find(word) != currentRules->builtins.end();
    }

// Check if a word is a constant
    bool SyntaxTokenizer::IsConstant(const std::string& word) const {
        if (!currentRules) return false;
        return currentRules->constants.find(word) != currentRules->constants.end();
    }

// Check if text is an operator
    bool SyntaxTokenizer::IsOperator(const std::string& text) const {
        if (!currentRules) return false;
        return std::find(currentRules->operators.begin(),
                         currentRules->operators.end(),
                         text) != currentRules->operators.end();
    }

// Check if text is a number
    bool SyntaxTokenizer::IsNumber(const std::string& text) const {
        if (text.empty()) return false;

        // Check for hex number
        if (text.length() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            for (size_t i = 2; i < text.length(); i++) {
                if (!IsHexDigit(text[i])) return false;
            }
            return true;
        }

        // Check for binary number
        if (text.length() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
            for (size_t i = 2; i < text.length(); i++) {
                if (text[i] != '0' && text[i] != '1') return false;
            }
            return true;
        }

        // Check for decimal/float number. It starts with a digit or a point
        // and holds a digit: a lone suffix or exponent letter - a variable
        // named f, L, u or e - is a name, not a number.
        if (!IsDigit(text[0]) && text[0] != '.') return false;
        if (std::none_of(text.begin(), text.end(), [this](char c) { return IsDigit(c); })) return false;
        bool hasDecimalPoint = false;
        bool hasExponent = false;

        for (size_t i = 0; i < text.length(); i++) {
            if (text[i] == '.') {
                if (hasDecimalPoint) return false;
                hasDecimalPoint = true;
            } else if (text[i] == 'e' || text[i] == 'E') {
                if (hasExponent) return false;
                hasExponent = true;
                if (i + 1 < text.length() && (text[i + 1] == '+' || text[i + 1] == '-')) {
                    i++; // Skip sign
                }
            } else if (!IsDigit(text[i])) {
                // Check for number suffixes (f, L, u, etc.)
                if (i == text.length() - 1) {
                    return text[i] == 'f' || text[i] == 'F' ||
                           text[i] == 'l' || text[i] == 'L' ||
                           text[i] == 'u' || text[i] == 'U';
                }
                return false;
            }
        }

        return true;
    }

// Check if text is a register (for assembly languagesRules)
    bool SyntaxTokenizer::IsRegister(const std::string& text) const {
        if (!currentRules) return false;
        return currentRules->registers.find(text) != currentRules->registers.end();
    }

// Check if text is an instruction (for assembly languagesRules)
    bool SyntaxTokenizer::IsInstruction(const std::string& text) const {
        if (!currentRules) return false;
        return currentRules->instructions.find(text) != currentRules->instructions.end();
    }

// Classify a word into its token type
    TokenType SyntaxTokenizer::ClassifyWord(const std::string& word) const {
        if (!currentRules) return TokenType::Identifier;

        // Check in order of priority
        if (IsKeyword(word)) return TokenType::Keyword;
        if (IsType(word)) return TokenType::Type;
        if (IsOperator(word)) return TokenType::Operator;
        if (IsConstant(word)) return TokenType::Constant;
        if (IsBuiltin(word)) return TokenType::Builtin;
        if (IsRegister(word)) return TokenType::Register;
        if (IsInstruction(word)) return TokenType::Assembly;
        if (IsNumber(word)) return TokenType::Number;

        // Check if it looks like a function call (followed by '(')
        // This would need context from the next token

        return TokenType::Identifier;
    }

// Parse a number
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseNumber(const std::string& text, size_t pos) const {
        if (!currentRules) return {pos, TokenType::Unknown};

        size_t endPos = pos;
        if (currentRules->hasHexNumbers &&
            !currentRules->hexNumberPrefix.empty() &&
            text.compare(endPos, currentRules->hexNumberPrefix.length(), currentRules->hexNumberPrefix) == 0) {
            endPos += currentRules->hexNumberPrefix.length();
            size_t hexStart = endPos;
            while (endPos < text.length() && IsHexDigit(text[endPos])) {
                endPos++;
            }
            if (endPos > hexStart) {
                return {endPos, TokenType::Number};
            }
            return {pos, TokenType::Unknown};  // Prefix without digits — not a number
        }

        // Check for hex numbers (0x or 0X)
        if (currentRules->hasHexNumbers &&
            endPos + 1 < text.length() &&
            text[endPos] == '0' &&
            (text[endPos + 1] == 'x' || text[endPos + 1] == 'X')) {
            endPos += 2;
            while (endPos < text.length() && IsHexDigit(text[endPos])) {
                endPos++;
            }
            return {endPos, TokenType::Number};
        }

        // Check for binary numbers (0b or 0B)
        if (currentRules->hasBinaryNumbers &&
            endPos + 1 < text.length() &&
            text[endPos] == '0' &&
            (text[endPos + 1] == 'b' || text[endPos + 1] == 'B')) {
            endPos += 2;
            while (endPos < text.length() && (text[endPos] == '0' || text[endPos] == '1')) {
                endPos++;
            }
            return {endPos, TokenType::Number};
        }

        // Parse decimal/float number
        bool hasDecimalPoint = false;
        bool hasExponent = false;

        while (endPos < text.length()) {
            if (IsDigit(text[endPos])) {
                endPos++;
            } else if (currentRules->hasFloatNumbers && text[endPos] == '.' && !hasDecimalPoint && !hasExponent) {
                hasDecimalPoint = true;
                endPos++;
            } else if (currentRules->hasFloatNumbers &&
                       (text[endPos] == 'e' || text[endPos] == 'E') &&
                       !hasExponent) {
                hasExponent = true;
                endPos++;
                if (endPos < text.length() && (text[endPos] == '+' || text[endPos] == '-')) {
                    endPos++;
                }
            } else if (endPos > pos && IsNumberSuffix(text[endPos])) {
                // Handle number suffixes (f, L, u, etc.)
                endPos++;
                break;
            } else {
                break;
            }
        }

        if (endPos > pos) {
            return {endPos, TokenType::Number};
        }

        return {pos, TokenType::Unknown};
    }

// Parse an operator
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseOperator(const std::string& text, size_t pos) const {
        if (!currentRules) return {pos, TokenType::Unknown};

        // Sort operators by length (longest first) to match multi-character operators
        std::vector<std::string> sortedOps = currentRules->operators;
        std::sort(sortedOps.begin(), sortedOps.end(),
                  [](const std::string& a, const std::string& b) { return a.length() > b.length(); });

        for (const auto& op : sortedOps) {
            if (text.substr(pos, op.length()) == op) {
                return {pos + op.length(), TokenType::Operator};
            }
        }

        return {pos, TokenType::Unknown};
    }

// ===== CHARACTER CLASSIFICATION HELPERS =====

    bool SyntaxTokenizer::IsWordCharacter(char c) const {
        return std::isalpha(c);
    }

    bool SyntaxTokenizer::IsDigit(char c) const {
        return std::isdigit(c);
    }

    bool SyntaxTokenizer::IsHexDigit(char c) const {
        return std::isxdigit(c);
    }

    bool SyntaxTokenizer::IsWhitespace(char c) const {
        return std::isspace(c);
    }

// ===== ADDITIONAL HELPER METHODS =====

    bool SyntaxTokenizer::IsStringDelimiter(char c) const {
        if (!currentRules) return false;
        return std::find(currentRules->stringDelimiters.begin(),
                         currentRules->stringDelimiters.end(),
                         c) != currentRules->stringDelimiters.end();
    }

    bool SyntaxTokenizer::IsCharacterDelimiter(char c) const {
        if (!currentRules) return false;
        return std::find(currentRules->characterDelimiters.begin(),
                         currentRules->characterDelimiters.end(),
                         c) != currentRules->characterDelimiters.end();
    }

    bool SyntaxTokenizer::IsNumberSuffix(char c) const {
        return c == 'f' || c == 'F' || c == 'l' || c == 'L' ||
               c == 'u' || c == 'U' || c == 'd' || c == 'D';
    }

// Line-specific parsing helpers (similar to main parsing but bounded to single line)
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseCharacterInLine(const std::string& line, size_t pos) const {
        if (pos + 1 >= line.length()) return {pos, TokenType::Unknown};

        size_t endPos = pos + 1;

        // Handle escape sequences
        if (line[endPos] == '\\' && currentRules->hasEscapeSequences && endPos + 1 < line.length()) {
            endPos += 2;
        } else {
            endPos++;
        }

        // Check for closing quote
        if (endPos < line.length() && IsCharacterDelimiter(line[endPos])) {
            return {endPos + 1, TokenType::Character};
        }

        return {pos, TokenType::Unknown};
    }

    std::pair<size_t, TokenType> SyntaxTokenizer::ParseNumberInLine(const std::string& line, size_t pos) const {
        return ParseNumber(line, pos); // Same logic applies for single line
    }

    std::pair<size_t, TokenType> SyntaxTokenizer::ParseOperatorInLine(const std::string& line, size_t pos) const {
        return ParseOperator(line, pos); // Same logic applies for single line
    }

    std::pair<size_t, TokenType> SyntaxTokenizer::ParseWordInLine(const std::string& line, size_t pos) const {
        size_t endPos = pos;
        while (endPos < line.length() && (IsWordCharacter(line[endPos]) || line[endPos] == '_' || IsDigit(line[endPos]))) {
            endPos++;
        }
        return {endPos, TokenType::Identifier}; // Classification happens in calling code
    }

// ===== CSS =====
// CSS has almost no reserved words: "color" is a property before a ':' in a
// block, a value after one, and could be a class name in a selector. So a CSS
// line is scanned by where each word sits:
//   selector      tag -> Keyword, .class -> Identifier, #id -> Constant,
//                 :hover / ::before -> Builtin, combinators -> Operator
//   declaration   property -> Keyword, value word -> Constant,
//                 var( calc( url( -> Function, --custom -> Identifier,
//                 1.5rem 50% #fff -> Number, !important -> Preprocessor
//   at-rule       @media -> Preprocessor, (feature: value) as a declaration
// Strings and url(...) are String, /* */ is Comment.
//
// The text area highlights one line at a time and hands each line the
// SyntaxLineState the line above ended in: the context, the open blocks, an
// open comment or string. Only TokenizeLine(line) without a state starts a
// line from a guess (GuessCssLineContext).
    namespace {
        enum class CssContext { Selector, Declaration, AtPrelude };

        bool IsCssNameChar(unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c >= 0x80;
        }

        bool IsCssNameStartAt(const std::string& s, size_t i) {
            if (i >= s.size()) return false;
            unsigned char c = s[i];
            if (std::isalpha(c) || c == '_' || c >= 0x80) return true;
            if (c == '\\' && i + 1 < s.size()) return true;
            if (c == '-' && i + 1 < s.size()) {
                unsigned char n = s[i + 1];
                return std::isalpha(n) || n == '_' || n == '-' || n >= 0x80 || n == '\\';
            }
            return false;
        }

        size_t ScanCssName(const std::string& s, size_t i) {
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) { i += 2; continue; }
                if (!IsCssNameChar(static_cast<unsigned char>(s[i]))) break;
                ++i;
            }
            return i;
        }

        size_t SkipCssSpaces(const std::string& s, size_t i) {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            return i;
        }

        // End of the string opened at i (past the closing quote, or the line end).
        size_t ScanCssString(const std::string& s, size_t i) {
            char quote = s[i++];
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) { i += 2; continue; }
                if (s[i++] == quote) break;
            }
            return i;
        }

        // A number with its unit: 12px, -.25rem, 1.5e3, 50%, 2n.
        size_t ScanCssNumber(const std::string& s, size_t i) {
            if (s[i] == '+' || s[i] == '-') ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            if (i + 1 < s.size() && s[i] == '.' && std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
                ++i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
                if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
                    i = j;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                }
            }
            if (i < s.size() && s[i] == '%') return i + 1;
            if (IsCssNameStartAt(s, i)) return ScanCssName(s, i);
            return i;
        }

        bool StartsCssNumber(const std::string& s, size_t i) {
            auto digit = [&](size_t k) { return k < s.size() && std::isdigit(static_cast<unsigned char>(s[k])); };
            if (digit(i)) return true;
            if (s[i] == '.' && digit(i + 1)) return true;
            if ((s[i] == '+' || s[i] == '-') && (digit(i + 1) || (i + 1 < s.size() && s[i + 1] == '.' && digit(i + 2)))) {
                // A sign belongs to the number unless it follows a word or a
                // closing bracket: "2n+1" and "a-1" are not "+1" / "-1".
                if (i == 0) return true;
                unsigned char prev = s[i - 1];
                return !(IsCssNameChar(prev) || prev == ')' || prev == ']' || prev == '%');
            }
            return false;
        }

        bool EqualsNoCase(const std::string& a, const char* b) {
            size_t n = std::strlen(b);
            if (a.size() != n) return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != b[i]) return false;
            return true;
        }

        // At-rules whose block holds declarations; the others (@media,
        // @supports, @keyframes, @layer, ...) hold rules with selectors.
        bool CssAtRuleHoldsDeclarations(const std::string& name) {
            static const char* names[] = {
                "font-face", "page", "property", "counter-style", "font-palette-values",
                "viewport", "-ms-viewport", "font-feature-values", "position-try",
                "view-transition", "top-left", "top-center", "top-right", "bottom-left",
                "bottom-center", "bottom-right", "left-top", "left-middle", "left-bottom",
                "right-top", "right-middle", "right-bottom"
            };
            for (const char* n : names)
                if (EqualsNoCase(name, n)) return true;
            return false;
        }

        // Where a line starts when nothing before it is known: the first
        // brace or semicolon on it tells ("a {" is a selector, "color: red;"
        // and a lone "}" are inside a block); a line with neither is a
        // declaration if it has a ':' and does not end in ',' (a selector list).
        CssContext GuessCssLineContext(const std::string& line) {
            bool hasColon = false;
            for (size_t i = 0; i < line.size(); ++i) {
                char c = line[i];
                if (c == '"' || c == '\'') { i = ScanCssString(line, i) - 1; continue; }
                if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                    size_t end = line.find("*/", i + 2);
                    if (end == std::string::npos) break;
                    i = end + 1;
                    continue;
                }
                if (c == '{') return CssContext::Selector;
                if (c == '}' || c == ';') return CssContext::Declaration;
                if (c == ':') hasColon = true;
            }
            size_t last = line.find_last_not_of(" \t\r");
            if (last != std::string::npos && line[last] == ',') return CssContext::Selector;
            return hasColon ? CssContext::Declaration : CssContext::Selector;
        }
    }

    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::TokenizeCssLine(const std::string& line,
                                                                      SyntaxLineState& state, bool guess) const {
        std::vector<Token> tokens;
        auto emit = [&](TokenType type, size_t from, size_t to) {
            if (to > from) tokens.push_back({type, line.substr(from, to - from), to - from});
        };

        size_t pos = 0;

        if (!guess) {
            // Continue what the line above left open.
            if (state.blockComment >= 0) {
                size_t end = line.find("*/");
                if (end == std::string::npos) {
                    emit(TokenType::Comment, 0, line.size());
                    return tokens;
                }
                pos = end + 2;
                emit(TokenType::Comment, 0, pos);
                state.blockComment = -1;
            } else if (state.openString) {
                bool closed = false;
                size_t end = FindStringEnd(line, 0, state.openString, true, closed);
                emit(TokenType::String, 0, end);
                if (!closed) return tokens;
                pos = end;
                state.openString = 0;
            }
        } else {
            // The tail of a comment opened on an earlier line: "... */" with no
            // "/*" before it, or a " * text" line of a comment block.
            size_t close = line.find("*/");
            size_t open = line.find("/*");
            size_t quote = line.find_first_of("\"'");
            if (close != std::string::npos && (open == std::string::npos || open > close) &&
                (quote == std::string::npos || quote > close)) {
                emit(TokenType::Comment, 0, close + 2);
                pos = close + 2;
            } else {
                size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos && line[first] == '*' &&
                    (first + 1 == line.size() || line[first + 1] == ' ' || line[first + 1] == '\t') &&
                    line.find('{') == std::string::npos) {
                    emit(TokenType::Comment, 0, line.size());
                    return tokens;
                }
            }
        }

        CssContext ctx;
        CssContext preludeParent;
        bool atHoldsDeclarations;
        std::vector<CssContext> stack;
        bool expectProperty;
        if (guess) {
            ctx = GuessCssLineContext(line.substr(pos));
            preludeParent = CssContext::Selector;
            atHoldsDeclarations = false;
            expectProperty = (ctx == CssContext::Declaration);
        } else {
            ctx = static_cast<CssContext>(state.cssContext);
            preludeParent = static_cast<CssContext>(state.cssPreludeParent);
            atHoldsDeclarations = state.cssAtHoldsDeclarations;
            expectProperty = state.cssExpectProperty;
            for (int d = 0; d < state.cssDepth; ++d) {
                bool decl = d < 32 && (state.cssStack >> d) & 1u;
                stack.push_back(decl ? CssContext::Declaration : CssContext::Selector);
            }
        }
        // Hand the context at the end of the line to the next one.
        auto saveState = [&]() {
            state.cssContext = static_cast<uint8_t>(ctx);
            state.cssPreludeParent = static_cast<uint8_t>(preludeParent);
            state.cssAtHoldsDeclarations = atHoldsDeclarations;
            state.cssExpectProperty = expectProperty;
            state.cssDepth = static_cast<uint8_t>(std::min<size_t>(stack.size(), 255));
            state.cssStack = 0;
            for (size_t d = 0; d < stack.size() && d < 32; ++d)
                if (stack[d] == CssContext::Declaration) state.cssStack |= (1u << d);
        };
        int attrDepth = 0;           // inside [attr=value] of a selector
        bool attrAfterOperator = false;

        while (pos < line.size()) {
            const size_t start = pos;
            const char c = line[pos];

            if (std::isspace(static_cast<unsigned char>(c))) {
                pos = SkipCssSpaces(line, pos);
                emit(TokenType::Whitespace, start, pos);
                continue;
            }

            if (c == '/' && pos + 1 < line.size() && line[pos + 1] == '*') {
                size_t end = line.find("*/", pos + 2);
                pos = (end == std::string::npos) ? line.size() : end + 2;
                emit(TokenType::Comment, start, pos);
                if (end == std::string::npos) state.blockComment = 0;
                continue;
            }

            if (c == '"' || c == '\'') {
                bool closed = false;
                pos = FindStringEnd(line, pos + 1, c, true, closed);
                emit(TokenType::String, start, pos);
                if (!closed) state.openString = c;
                continue;
            }

            if (c == '@' && IsCssNameStartAt(line, pos + 1)) {
                pos = ScanCssName(line, pos + 1);
                atHoldsDeclarations = CssAtRuleHoldsDeclarations(line.substr(start + 1, pos - start - 1));
                emit(TokenType::Preprocessor, start, pos);
                if (ctx != CssContext::AtPrelude) preludeParent = ctx;
                ctx = CssContext::AtPrelude;
                continue;
            }

            if (c == '{') {
                emit(TokenType::Punctuation, start, ++pos);
                if (ctx == CssContext::AtPrelude) {
                    stack.push_back(preludeParent);
                    ctx = atHoldsDeclarations ? CssContext::Declaration : CssContext::Selector;
                } else {
                    stack.push_back(ctx);
                    ctx = CssContext::Declaration;
                }
                expectProperty = (ctx == CssContext::Declaration);
                attrDepth = 0;
                continue;
            }

            if (c == '}') {
                emit(TokenType::Punctuation, start, ++pos);
                if (stack.empty()) {
                    ctx = CssContext::Selector;
                } else {
                    ctx = stack.back();
                    stack.pop_back();
                }
                expectProperty = (ctx == CssContext::Declaration);
                continue;
            }

            if (c == ';') {
                emit(TokenType::Punctuation, start, ++pos);
                if (ctx == CssContext::AtPrelude) ctx = preludeParent;   // @import ...;
                expectProperty = (ctx == CssContext::Declaration);
                continue;
            }

            if (ctx == CssContext::Selector) {
                if (c == ':') {
                    // :hover, ::before, :not( - the colons go with the name.
                    size_t nameAt = pos + 1;
                    if (nameAt < line.size() && line[nameAt] == ':') ++nameAt;
                    if (IsCssNameStartAt(line, nameAt)) {
                        pos = ScanCssName(line, nameAt);
                        emit(TokenType::Builtin, start, pos);
                    } else {
                        pos = nameAt;
                        emit(TokenType::Punctuation, start, pos);
                    }
                    continue;
                }
                if (c == '[') { ++attrDepth; attrAfterOperator = false; emit(TokenType::Punctuation, start, ++pos); continue; }
                if (c == ']') { if (attrDepth > 0) --attrDepth; emit(TokenType::Punctuation, start, ++pos); continue; }
                if (attrDepth > 0) {
                    if (IsCssNameStartAt(line, pos)) {
                        pos = ScanCssName(line, pos);
                        emit(attrAfterOperator ? TokenType::String : TokenType::Identifier, start, pos);
                        continue;
                    }
                    if (c == '=' || ((c == '~' || c == '|' || c == '^' || c == '$' || c == '*') &&
                                     pos + 1 < line.size() && line[pos + 1] == '=')) {
                        pos += (c == '=') ? 1 : 2;
                        attrAfterOperator = true;
                        emit(TokenType::Operator, start, pos);
                        continue;
                    }
                }
                if (c == '.' && IsCssNameStartAt(line, pos + 1)) {
                    pos = ScanCssName(line, pos + 1);
                    emit(TokenType::Identifier, start, pos);
                    continue;
                }
                if (c == '#' && pos + 1 < line.size() && (IsCssNameChar(static_cast<unsigned char>(line[pos + 1])) || line[pos + 1] == '\\')) {
                    pos = ScanCssName(line, pos + 1);
                    emit(TokenType::Constant, start, pos);
                    continue;
                }
                if (StartsCssNumber(line, pos)) {          // keyframe 50%, nth-child(2n)
                    pos = ScanCssNumber(line, pos);
                    emit(TokenType::Number, start, pos);
                    continue;
                }
                if (IsCssNameStartAt(line, pos)) {
                    pos = ScanCssName(line, pos);
                    // No selector word is followed by '(' - this is a value
                    // (a value on a line guessed as a selector).
                    bool call = pos < line.size() && line[pos] == '(';
                    emit(call ? TokenType::Function : TokenType::Keyword, start, pos);
                    continue;
                }
                if (c == ',' || c == '>' || c == '+' || c == '~' || c == '*' || c == '&' || c == '|') {
                    emit(TokenType::Operator, start, ++pos);
                    continue;
                }
            } else {
                // Declaration or at-rule prelude: property: value.
                if (c == ':') {
                    expectProperty = false;
                    emit(TokenType::Punctuation, start, ++pos);
                    continue;
                }
                if (c == '!' ) {
                    size_t nameAt = SkipCssSpaces(line, pos + 1);
                    if (IsCssNameStartAt(line, nameAt)) {
                        pos = ScanCssName(line, nameAt);
                        emit(TokenType::Preprocessor, start, pos);   // !important
                        continue;
                    }
                }
                if (c == '#' && pos + 1 < line.size() && IsCssNameChar(static_cast<unsigned char>(line[pos + 1]))) {
                    pos = ScanCssName(line, pos + 1);
                    emit(TokenType::Number, start, pos);             // #fff, #d3d4d5
                    continue;
                }
                if (StartsCssNumber(line, pos)) {
                    pos = ScanCssNumber(line, pos);
                    emit(TokenType::Number, start, pos);
                    continue;
                }
                if (IsCssNameStartAt(line, pos)) {
                    pos = ScanCssName(line, pos);
                    const std::string word = line.substr(start, pos - start);
                    const size_t after = SkipCssSpaces(line, pos);
                    const bool call = pos < line.size() && line[pos] == '(';
                    const bool beforeColon = after < line.size() && line[after] == ':';

                    if (call) {
                        emit(TokenType::Function, start, pos);
                        emit(TokenType::Punctuation, pos, pos + 1);
                        pos += 1;
                        if (EqualsNoCase(word, "url")) {
                            // url(unquoted) is one string up to ')'.
                            size_t body = SkipCssSpaces(line, pos);
                            emit(TokenType::Whitespace, pos, body);
                            pos = body;
                            if (pos < line.size() && line[pos] != '"' && line[pos] != '\'') {
                                size_t end = pos;
                                while (end < line.size() && line[end] != ')') {
                                    if (line[end] == '\\' && end + 1 < line.size()) ++end;
                                    ++end;
                                }
                                emit(TokenType::String, pos, end);
                                pos = end;
                            }
                        }
                        continue;
                    }
                    if (ctx == CssContext::Declaration && expectProperty && beforeColon) {
                        emit(TokenType::Keyword, start, pos);        // property, --custom-property
                    } else if (ctx == CssContext::AtPrelude && beforeColon) {
                        emit(TokenType::Keyword, start, pos);        // (min-width: ...)
                    } else if (word.size() > 2 && word[0] == '-' && word[1] == '-') {
                        emit(TokenType::Identifier, start, pos);     // var(--name)
                    } else {
                        emit(TokenType::Constant, start, pos);       // none, flex, screen, and
                    }
                    continue;
                }
                if (c == ',' || c == '/' || c == '*' || c == '+' || c == '-' || c == '=' || c == '<' || c == '>') {
                    emit(TokenType::Operator, start, ++pos);
                    continue;
                }
            }

            int bytes = Utf8CharBytes(static_cast<unsigned char>(c));
            pos = std::min(line.size(), pos + bytes);
            emit(TokenType::Punctuation, start, pos);
        }

        saveState();
        return tokens;
    }

} // namespace UltraCanvas