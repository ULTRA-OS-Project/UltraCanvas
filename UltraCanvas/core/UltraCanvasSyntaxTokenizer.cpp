// core/UltraCanvasSyntaxHighlighter.cpp
// Implementation of Tokenize and TokenizeLine methods for SyntaxTokenizer
// Version: 1.0.0
// Last Modified: 2024-12-20
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

// Main tokenization method - tokenizes entire text
    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::Tokenize(const std::string& text) const {
        std::vector<Token> tokens;

        if (!currentRules || text.empty()) {
            return tokens;
        }

        if (currentRules->isCss) {
            // Line by line, as the text area highlights it.
            size_t start = 0;
            while (start <= text.length()) {
                size_t end = text.find('\n', start);
                std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto lineTokens = TokenizeCssLine(line);
                tokens.insert(tokens.end(), lineTokens.begin(), lineTokens.end());
                if (end == std::string::npos) break;
                tokens.push_back({TokenType::Newline, "\n", 1});
                start = end + 1;
            }
            return tokens;
        }

//        int currentLine = 0;
        size_t position = 0;
        int currentColumn = 0;
        std::string whitespacePrefix = "";

        while (position < text.length()) {
            // Track newlines
            if (text[position] == '\r' && text[position + 1] == '\n') {
//                currentLine++;
                currentColumn = 0;
                position += 2;
                Token token;
                token.type = TokenType::Newline;
                token.text = "\n";
                token.length = 1;
                continue;
            }

            if (text[position] == '\n') {
//                currentLine++;
                currentColumn = 0;
                position++;
                Token token;
                token.type = TokenType::Newline;
                token.text = "\n";
                token.length = 1;
                continue;
            }

            // Skip whitespace (but track column position)
            if (IsWhitespace(text[position])) {
                whitespacePrefix.push_back(text[position]);
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
//            token.line = currentLine;
//            token.column = currentColumn;
//            token.start = position;

            // Try to match comments first (they have priority)
            auto commentResult = ParseComment(text, position);
            if (commentResult.first > position) {
                token.type = commentResult.second;
                token.length = commentResult.first - position;
                token.text = text.substr(position, token.length);
                tokens.push_back(token);

                // Update position and column
                for (size_t i = 0; i < token.length; i++) {
                    if (text[position + i] == '\n') {
//                        currentLine++;
                        currentColumn = 0;
                    } else {
                        currentColumn++;
                    }
                }
                position = commentResult.first;
                continue;
            }

            // Try to match strings
//            if (text[position] == '\n') {
//                auto stringResult = ParseString(text, position, text[position]);
//                if (stringResult.first > position) {
//                    token.type = stringResult.second;
//                    token.length = stringResult.first - position;
//                    token.text = text.substr(position, token.length);
//                    tokens.push_back(token);
//
//                    currentColumn += token.length;
//                    position = stringResult.first;
//                    continue;
//                }
//            }

            // Try to match character literals
            if (IsCharacterDelimiter(text[position])) {
                auto charResult = ParseCharacter(text, position);
                if (charResult.first > position) {
                    token.type = TokenType::Character;
                    token.length = charResult.first - position;
                    token.text = text.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = charResult.first;
                    continue;
                }
            }

            // Try to match numbers
            if (IsDigit(text[position]) ||
                (text[position] == '.' && position + 1 < text.length() && IsDigit(text[position + 1]))) {
                auto numberResult = ParseNumber(text, position);
                if (numberResult.first > position) {
                    token.type = numberResult.second;
                    token.length = numberResult.first - position;
                    token.text = text.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = numberResult.first;
                    continue;
                }
            }

            // Try to match preprocessor directives
            if (currentRules->hasPreprocessor && text[position] == '#' && currentColumn == 0) {
                auto preprocessorResult = ParsePreprocessor(text, position);
                if (preprocessorResult.first > position) {
                    token.type = TokenType::Preprocessor;
                    token.length = preprocessorResult.first - position;
                    token.text = text.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = preprocessorResult.first;
                    continue;
                }
            }

            // Try to match operators (before words to handle things like ++ correctly)
            auto operatorResult = ParseOperator(text, position);
            if (operatorResult.first > position) {
                token.type = operatorResult.second;
                token.length = operatorResult.first - position;
                token.text = text.substr(position, token.length);
                tokens.push_back(token);

                currentColumn += token.length;
                position = operatorResult.first;
                continue;
            }

            // Try to match words (keywords, identifiers, etc.)
            if (IsWordCharacter(text[position]) || text[position] == '_') {
                auto wordResult = ParseWord(text, position);
                if (wordResult.first > position) {
                    token.type = wordResult.second;
                    token.length = wordResult.first - position;
                    token.text = text.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = wordResult.first;
                    continue;
                }
            }

            // Handle punctuation and other single characters (UTF-8 aware)
            int charBytes = Utf8CharBytes(static_cast<unsigned char>(text[position]));
            token.type = TokenType::Punctuation;
            token.length = charBytes;
            token.text = text.substr(position, charBytes);
            tokens.push_back(token);

            currentColumn++;
            position += charBytes;
        }

        return tokens;
    }

// Tokenize a single line
    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::TokenizeLine(const std::string& line, int lineNumber) const {
        std::vector<Token> tokens;

        if (!currentRules || line.empty()) {
            return tokens;
        }

        if (currentRules->isCss) {
            return TokenizeCssLine(line);
        }

        size_t position = 0;
        int currentColumn = 0;
        std::string whitespacePrefix = "";

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

            // Try to match single-line comments
            bool foundComment = false;
            for (const auto& commentPrefix : currentRules->singleLineComments) {
                if (line.substr(position, commentPrefix.length()) == commentPrefix) {
                    token.type = TokenType::Comment;
                    token.length = line.length() - position;
                    token.text = line.substr(position);
                    tokens.push_back(token);
                    return tokens; // Rest of line is comment
                }
            }

            // Try to match multi-line comment starts
            for (const auto& [startDelim, endDelim] : currentRules->multiLineComments) {
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
                        return tokens;
                    }
                }
            }

            if (foundComment) continue;

            // Try to match strings
            if (IsStringDelimiter(line[position])) {
                auto stringResult = ParseStringInLine(line, position, line[position]);
                if (stringResult.first > position) {
                    token.type = TokenType::String;
                    token.length = stringResult.first - position;
                    token.text = line.substr(position, token.length);
                    tokens.push_back(token);

                    currentColumn += token.length;
                    position = stringResult.first;
                    continue;
                }
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

            // Try to match preprocessor directives (only at start of line)
            if (currentRules->hasPreprocessor && line[position] == '#' && currentColumn == 0) {
                token.type = TokenType::Preprocessor;
                token.length = line.length() - position;
                token.text = line.substr(position);
                tokens.push_back(token);
                return tokens; // Preprocessor takes whole line
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

        // Check for decimal/float number
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

// Check if text is a valid identifier
    bool SyntaxTokenizer::IsIdentifier(const std::string& text) const {
        if (text.empty()) return false;

        if (!std::isalpha(text[0]) && text[0] != '_') return false;

        for (size_t i = 1; i < text.length(); i++) {
            if (!std::isalnum(text[i]) && text[i] != '_') return false;
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

// Parse a string literal
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseString(const std::string& text, size_t pos, char delimiter) const {
        if (!currentRules) return {pos, TokenType::Unknown};

        size_t endPos = pos + 1;

        while (endPos < text.length()) {
            if (text[endPos] == delimiter) {
                // Check if it's escaped
                if (endPos > 0 && text[endPos - 1] == '\\' && currentRules->hasEscapeSequences) {
                    // Count consecutive backslashes
                    int backslashCount = 0;
                    size_t checkPos = endPos - 1;
                    while (checkPos > pos && text[checkPos] == '\\') {
                        backslashCount++;
                        checkPos--;
                    }
                    // If odd number of backslashes, the quote is escaped
                    if (backslashCount % 2 == 1) {
                        endPos++;
                        continue;
                    }
                }
                return {endPos + 1, TokenType::String};
            }

            // Check for string interpolation (template literals)
            if (currentRules->hasStringInterpolation) {
                if (delimiter == '`' && text[endPos] == '$' && endPos + 1 < text.length() && text[endPos + 1] == '{') {
                    // Handle interpolation - for now, just continue
                    endPos++;
                    continue;
                }
            }

            endPos++;
        }

        // Unclosed string
        return {text.length(), TokenType::String};
    }

// Parse a comment
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseComment(const std::string& text, size_t pos) const {
        if (!currentRules) return {pos, TokenType::Unknown};

        // Check single-line comments
        for (const auto& commentPrefix : currentRules->singleLineComments) {
            if (text.substr(pos, commentPrefix.length()) == commentPrefix) {
                size_t endPos = text.find('\n', pos);
                if (endPos == std::string::npos) {
                    endPos = text.length();
                }
                return {endPos, TokenType::Comment};
            }
        }

        // Check multi-line comments
        for (const auto& [startDelim, endDelim] : currentRules->multiLineComments) {
            if (text.substr(pos, startDelim.length()) == startDelim) {
                size_t endPos = text.find(endDelim, pos + startDelim.length());
                if (endPos != std::string::npos) {
                    endPos += endDelim.length();
                } else {
                    endPos = text.length();
                }
                return {endPos, TokenType::Comment};
            }
        }

        return {pos, TokenType::Unknown};
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

// Parse a word (identifier, keyword, etc.)
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseWord(const std::string& text, size_t pos) const {
        if (!IsWordCharacter(text[pos]) && text[pos] != '_') {
            return {pos, TokenType::Unknown};
        }

        size_t endPos = pos;
        while (endPos < text.length() && (IsWordCharacter(text[endPos]) || text[endPos] == '_' || IsDigit(text[endPos]))) {
            endPos++;
        }

        if (endPos > pos) {
            std::string word = text.substr(pos, endPos - pos);
            return {endPos, ClassifyWord(word)};
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
    std::pair<size_t, TokenType> SyntaxTokenizer::ParseStringInLine(const std::string& line, size_t pos, char delimiter) const {
        size_t endPos = pos + 1;

        while (endPos < line.length()) {
            if (line[endPos] == delimiter) {
                if (endPos > 0 && line[endPos - 1] == '\\' && currentRules->hasEscapeSequences) {
                    int backslashCount = 0;
                    size_t checkPos = endPos - 1;
                    while (checkPos > pos && line[checkPos] == '\\') {
                        backslashCount++;
                        checkPos--;
                    }
                    if (backslashCount % 2 == 1) {
                        endPos++;
                        continue;
                    }
                }
                return {endPos + 1, TokenType::String};
            }
            endPos++;
        }

        return {line.length(), TokenType::String};
    }

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

    std::pair<size_t, TokenType> SyntaxTokenizer::ParseCharacter(const std::string& text, size_t pos) const {
        if (!currentRules || pos >= text.length()) return {pos, TokenType::Unknown};

        if (!IsCharacterDelimiter(text[pos])) return {pos, TokenType::Unknown};

        size_t endPos = pos + 1;

        // Handle escape sequences
        if (endPos < text.length() && text[endPos] == '\\' && currentRules->hasEscapeSequences) {
            endPos += 2; // Skip escape sequence
        } else if (endPos < text.length()) {
            endPos++; // Regular character
        }

        // Check for closing delimiter
        if (endPos < text.length() && text[endPos] == text[pos]) {
            return {endPos + 1, TokenType::Character};
        }

        // Unclosed character literal
        return {endPos, TokenType::Character};
    }

    std::pair<size_t, TokenType> SyntaxTokenizer::ParsePreprocessor(const std::string& text, size_t pos) const {
        if (!currentRules || !currentRules->hasPreprocessor) return {pos, TokenType::Unknown};

        if (text[pos] != '#') return {pos, TokenType::Unknown};

        // Preprocessor directives usually go to end of line
        size_t endPos = text.find('\n', pos);
        if (endPos == std::string::npos) {
            endPos = text.length();
        }

        // Handle line continuations
        while (endPos > pos && endPos < text.length() && text[endPos - 1] == '\\') {
            size_t nextLine = text.find('\n', endPos + 1);
            if (nextLine == std::string::npos) {
                endPos = text.length();
                break;
            }
            endPos = nextLine;
        }

        return {endPos, TokenType::Preprocessor};
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
// The text area highlights one line at a time with no state from the line
// above, so a line that does not open a block itself starts from a guess
// (GuessCssLineContext). Minified CSS - one long line - is scanned exactly.
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

    std::vector<SyntaxTokenizer::Token> SyntaxTokenizer::TokenizeCssLine(const std::string& line) const {
        std::vector<Token> tokens;
        auto emit = [&](TokenType type, size_t from, size_t to) {
            if (to > from) tokens.push_back({type, line.substr(from, to - from), to - from});
        };

        size_t pos = 0;

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

        CssContext ctx = GuessCssLineContext(line.substr(pos));
        CssContext preludeParent = CssContext::Selector;
        std::string atName;
        std::vector<CssContext> stack;
        bool expectProperty = (ctx == CssContext::Declaration);
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
                continue;
            }

            if (c == '"' || c == '\'') {
                pos = ScanCssString(line, pos);
                emit(TokenType::String, start, pos);
                continue;
            }

            if (c == '@' && IsCssNameStartAt(line, pos + 1)) {
                pos = ScanCssName(line, pos + 1);
                atName = line.substr(start + 1, pos - start - 1);
                emit(TokenType::Preprocessor, start, pos);
                if (ctx != CssContext::AtPrelude) preludeParent = ctx;
                ctx = CssContext::AtPrelude;
                continue;
            }

            if (c == '{') {
                emit(TokenType::Punctuation, start, ++pos);
                if (ctx == CssContext::AtPrelude) {
                    stack.push_back(preludeParent);
                    ctx = CssAtRuleHoldsDeclarations(atName) ? CssContext::Declaration : CssContext::Selector;
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
                    // (a multi-line value guessed as a selector).
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

        return tokens;
    }

} // namespace UltraCanvas