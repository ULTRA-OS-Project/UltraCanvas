// UltraCanvasSyntaxHighlightingRules.h
// Comprehensive syntax highlighting rules for major programming languages
// Version: 1.0.0
// Last Modified: 2025-09-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <memory>

namespace UltraCanvas {

// ===== TOKEN TYPES =====
    enum class TokenType {
        Keyword,           // int, class, public, etc.
        Type,              // int, float, string, custom types
        Function,          // function calls
        Variable,          // variable names
        Number,            // 123, 3.14f, 0xFF
        String,            // "hello", 'c'
        Character,         // 'a', '\n'
        Comment,           // // /* */
        Operator,          // +, -, *, /, =, ==, !=
        Punctuation,       // ( ) [ ] { } ; ,
        Preprocessor,      // #include, #define
        Constant,          // true, false, NULL
        Identifier,        // user-defined names
        Whitespace,        // spaces, tabs
        Unknown,           // unrecognized tokens
        StringInterpolation, // ${variable} in template strings
        Attribute,         // @annotation, [[attribute]]
        Label,             // goto labels
        NamespaceScope,    // :: in C++, . in others
        Builtin,           // Built-in functions and types
        Decorator,         // Python decorators, C# attributes
        Assembly,          // Assembly instructions
        Register           // CPU registers in assembly
    };

// ===== STYLE DEFINITION =====
    struct TokenStyle {
        Color color = Color(0, 0, 0);
        bool bold = false;
        bool italic = false;
        bool underline = false;

        TokenStyle() = default;
        TokenStyle(const Color& c, bool b = false, bool i = false, bool u = false)
                : color(c), bold(b), italic(i), underline(u) {}
    };

// ===== LANGUAGE RULES =====
    struct LanguageRules {
        std::string name;
        std::vector<std::string> fileExtensions;

        // Keywords and identifiers
        std::unordered_set<std::string> keywords;
        std::unordered_set<std::string> types;
        std::unordered_set<std::string> builtins;
        std::unordered_set<std::string> constants;
        std::unordered_set<std::string> operators;

        // Comment patterns
        std::vector<std::string> lineCommentStart;
        std::vector<std::pair<std::string, std::string>> blockComments;

        // String patterns
        std::vector<char> stringDelimiters;
        std::vector<char> characterDelimiters;
        char escapeCharacter = '\\';
        bool hasRawStrings = false;
        std::string rawStringPrefix;

        // Number patterns
        bool hasHexNumbers = true;
        bool hasBinaryNumbers = false;
        bool hasOctalNumbers = true;
        bool hasFloatNumbers = true;
        std::vector<std::string> numberSuffixes;

        // Special features
        bool isCaseSensitive = true;
        bool hasPreprocessor = false;
        std::string preprocessorPrefix = "#";
        bool hasAttributes = false;
        std::vector<std::pair<std::string, std::string>> attributeDelimiters;
        bool hasStringInterpolation = false;
        std::vector<std::pair<std::string, std::string>> interpolationDelimiters;

        // Assembly-specific
        bool isAssembly = false;
        std::unordered_set<std::string> registers;
        std::unordered_set<std::string> instructions;
    };

// ===== SYNTAX HIGHLIGHTER CLASS =====
    class SyntaxHighlighter {
    public:
        struct Token {
            TokenType type;
            std::string text;
            size_t start;
            size_t length;
            int line;
            int column;
        };

    private:
        std::unordered_map<std::string, LanguageRules> languages;
        std::unordered_map<TokenType, TokenStyle> tokenStyles;
        LanguageRules* currentLanguage = nullptr;

    public:
        SyntaxHighlighter();

        // Language management
        void RegisterLanguage(const LanguageRules& rules);
        bool SetLanguage(const std::string& languageName);
        bool SetLanguageByExtension(const std::string& fileExtension);
        std::vector<std::string> GetSupportedLanguages() const;

        // Style management
        void SetTokenStyle(TokenType type, const TokenStyle& style);
        TokenStyle GetTokenStyle(TokenType type) const;
        void LoadDefaultStyles();
        void LoadDarkTheme();
        void LoadLightTheme();

        // Tokenization
        std::vector<Token> Tokenize(const std::string& text) const;
        std::vector<Token> TokenizeLine(const std::string& line, int lineNumber = 0) const;

    private:
        // Tokenization helpers
        bool IsKeyword(const std::string& word) const;
        bool IsType(const std::string& word) const;
        bool IsBuiltin(const std::string& word) const;
        bool IsConstant(const std::string& word) const;
        bool IsOperator(const std::string& text) const;
        bool IsNumber(const std::string& text) const;
        bool IsIdentifier(const std::string& text) const;
        bool IsRegister(const std::string& text) const;
        bool IsInstruction(const std::string& text) const;

        TokenType ClassifyWord(const std::string& word) const;
        std::pair<size_t, TokenType> ParseString(const std::string& text, size_t pos, char delimiter) const;
        std::pair<size_t, TokenType> ParseComment(const std::string& text, size_t pos) const;
        std::pair<size_t, TokenType> ParseNumber(const std::string& text, size_t pos) const;
        std::pair<size_t, TokenType> ParseWord(const std::string& text, size_t pos) const;
        std::pair<size_t, TokenType> ParseOperator(const std::string& text, size_t pos) const;

        bool IsWordCharacter(char c) const;
        bool IsDigit(char c) const;
        bool IsHexDigit(char c) const;
        bool IsWhitespace(char c) const;
    };

// ===== LANGUAGE DEFINITIONS =====

// Factory functions for creating language rules
    LanguageRules CreateCppRules();
    LanguageRules CreateCRules();
    LanguageRules CreateJavaRules();
    LanguageRules CreateCSharpRules();
    LanguageRules CreatePythonRules();
    LanguageRules CreateJavaScriptRules();
    LanguageRules CreateTypeScriptRules();
    LanguageRules CreatePascalRules();

    LanguageRules CreateFortranRules();
    LanguageRules CreateBasicRules();
    LanguageRules CreateLuaRules();
    LanguageRules CreateLispRules();
    LanguageRules CreateCommonLispRules();
    LanguageRules CreateSmalltalkRules();
    LanguageRules CreatePrologRules();
    LanguageRules CreatePerlRules();
    LanguageRules CreateRubyRules();
    LanguageRules CreateGoRules();
    LanguageRules CreateSwiftRules();
    LanguageRules CreateKotlinRules();
    LanguageRules CreateDartRules();
    LanguageRules CreateRustRules();
    LanguageRules CreateElixirRules();
    LanguageRules CreateHtmlRules();
    LanguageRules CreateCssRules();
    LanguageRules CreateSqlRules();
    LanguageRules CreatePhpRules();

// Assembly language rules
    LanguageRules CreateX86AssemblyRules();
    LanguageRules CreateArmAssemblyRules();
    LanguageRules Create68000AssemblyRules();
    LanguageRules CreateZ80AssemblyRules();

// ===== IMPLEMENTATION =====

    inline SyntaxHighlighter::SyntaxHighlighter() {
        // Register all supported languages
        RegisterLanguage(CreateCppRules());
        RegisterLanguage(CreateCRules());
        RegisterLanguage(CreateJavaRules());
        RegisterLanguage(CreateCSharpRules());
        RegisterLanguage(CreatePythonRules());
        RegisterLanguage(CreateJavaScriptRules());
        RegisterLanguage(CreateTypeScriptRules());
        RegisterLanguage(CreatePascalRules());
        RegisterLanguage(CreateFortranRules());
        RegisterLanguage(CreateBasicRules());
        RegisterLanguage(CreateLuaRules());
        RegisterLanguage(CreateLispRules());
        RegisterLanguage(CreateCommonLispRules());
        RegisterLanguage(CreateSmalltalkRules());
        RegisterLanguage(CreatePrologRules());
        RegisterLanguage(CreatePerlRules());
        RegisterLanguage(CreateRubyRules());
        RegisterLanguage(CreateGoRules());
        RegisterLanguage(CreateSwiftRules());
        RegisterLanguage(CreateKotlinRules());
        RegisterLanguage(CreateDartRules());
        RegisterLanguage(CreateRustRules());
        RegisterLanguage(CreateElixirRules());
        RegisterLanguage(CreateHtmlRules());
        RegisterLanguage(CreateCssRules());
        RegisterLanguage(CreateSqlRules());
        RegisterLanguage(CreatePhpRules());

        // Additional languages from the top 20 list
        RegisterLanguage(CreateRRules());
        RegisterLanguage(CreateScalaRules());
        RegisterLanguage(CreateMatlabRules());
        RegisterLanguage(CreateVbaRules());
        RegisterLanguage(CreateShellScriptRules());

        // Assembly languages
        RegisterLanguage(CreateX86AssemblyRules());
        RegisterLanguage(CreateArmAssemblyRules());
        RegisterLanguage(Create68000AssemblyRules());
        RegisterLanguage(CreateZ80AssemblyRules());

        LoadDefaultStyles();
    }

    inline void SyntaxHighlighter::RegisterLanguage(const LanguageRules& rules) {
        languages[rules.name] = rules;
    }

    inline bool SyntaxHighlighter::SetLanguage(const std::string& languageName) {
        auto it = languages.find(languageName);
        if (it != languages.end()) {
            currentLanguage = &it->second;
            return true;
        }
        return false;
    }

    inline bool SyntaxHighlighter::SetLanguageByExtension(const std::string& fileExtension) {
        std::string ext = fileExtension;
        if (ext.front() == '.') ext = ext.substr(1);

        for (auto& [name, rules] : languages) {
            for (const std::string& ruleExt : rules.fileExtensions) {
                if (ruleExt == ext) {
                    currentLanguage = &rules;
                    return true;
                }
            }
        }
        return false;
    }

    inline std::vector<std::string> SyntaxHighlighter::GetSupportedLanguages() const {
        std::vector<std::string> result;
        for (const auto& [name, rules] : languages) {
            result.push_back(name);
        }
        return result;
    }

    inline void SyntaxHighlighter::LoadDefaultStyles() {
        tokenStyles[TokenType::Keyword] = TokenStyle(Color(0, 0, 255), true);        // Blue, bold
        tokenStyles[TokenType::Type] = TokenStyle(Color(43, 145, 175), true);        // Teal, bold
        tokenStyles[TokenType::String] = TokenStyle(Color(163, 21, 21));             // Dark red
        tokenStyles[TokenType::Character] = TokenStyle(Color(163, 21, 21));          // Dark red
        tokenStyles[TokenType::Comment] = TokenStyle(Color(0, 128, 0), false, true); // Green, italic
        tokenStyles[TokenType::Number] = TokenStyle(Color(255, 140, 0));             // Orange
        tokenStyles[TokenType::Operator] = TokenStyle(Color(0, 0, 0), true);         // Black, bold
        tokenStyles[TokenType::Preprocessor] = TokenStyle(Color(128, 0, 128));       // Purple
        tokenStyles[TokenType::Function] = TokenStyle(Color(255, 20, 147));          // Deep pink
        tokenStyles[TokenType::Constant] = TokenStyle(Color(0, 0, 255), true);       // Blue, bold
        tokenStyles[TokenType::Builtin] = TokenStyle(Color(128, 0, 128));            // Purple
        tokenStyles[TokenType::Attribute] = TokenStyle(Color(255, 165, 0));          // Orange
        tokenStyles[TokenType::Assembly] = TokenStyle(Color(139, 69, 19), true);     // Brown, bold
        tokenStyles[TokenType::Register] = TokenStyle(Color(220, 20, 60));           // Crimson
    }

    inline void SyntaxHighlighter::LoadDarkTheme() {
        tokenStyles[TokenType::Keyword] = TokenStyle(Color(86, 156, 214), true);     // Light blue
        tokenStyles[TokenType::Type] = TokenStyle(Color(78, 201, 176));              // Turquoise
        tokenStyles[TokenType::String] = TokenStyle(Color(206, 145, 120));           // Light brown
        tokenStyles[TokenType::Character] = TokenStyle(Color(206, 145, 120));        // Light brown
        tokenStyles[TokenType::Comment] = TokenStyle(Color(106, 153, 85), false, true); // Green, italic
        tokenStyles[TokenType::Number] = TokenStyle(Color(181, 206, 168));           // Light green
        tokenStyles[TokenType::Operator] = TokenStyle(Color(212, 212, 212));         // Light gray
        tokenStyles[TokenType::Preprocessor] = TokenStyle(Color(155, 155, 155));     // Gray
        tokenStyles[TokenType::Function] = TokenStyle(Color(220, 220, 170));         // Light yellow
        tokenStyles[TokenType::Constant] = TokenStyle(Color(86, 156, 214), true);    // Light blue
        tokenStyles[TokenType::Builtin] = TokenStyle(Color(197, 134, 192));          // Light purple
    }

// ===== C++ LANGUAGE RULES =====
    inline LanguageRules CreateCppRules() {
        LanguageRules rules;
        rules.name = "C++";
        rules.fileExtensions = {"cpp", "cxx", "cc", "c++", "hpp", "hxx", "h++", "h"};

        rules.keywords = {
                "alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit",
                "atomic_noexcept", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
                "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const",
                "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
                "co_return", "co_yield", "decltype", "default", "delete", "do", "double",
                "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
                "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
                "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
                "or_eq", "private", "protected", "public", "reflexpr", "register",
                "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
                "static", "static_assert", "static_cast", "struct", "switch", "synchronized",
                "template", "this", "thread_local", "throw", "true", "try", "typedef",
                "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
                "volatile", "wchar_t", "while", "xor", "xor_eq"
        };

        rules.types = {
                "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                "size_t", "ptrdiff_t", "intptr_t", "uintptr_t", "std::string", "std::vector",
                "std::map", "std::set", "std::list", "std::deque", "std::array", "std::unique_ptr",
                "std::shared_ptr", "std::weak_ptr"
        };

        rules.constants = {"true", "false", "nullptr", "NULL"};

        rules.operators = {
                "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=",
                "&&", "||", "!", "&", "|", "^", "~", "<<", ">>", "++", "--",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
                "->", ".", "::", "?:", ",", ";", "(", ")", "[", "]", "{", "}"
        };

        rules.lineCommentStart = {"//"};
        rules.blockComments = {{"/*", "*/"}};
        rules.stringDelimiters = {'"'};
        rules.characterDelimiters = {'\''};
        rules.hasPreprocessor = true;
        rules.hasAttributes = true;
        rules.attributeDelimiters = {{"[[", "]]"}};
        rules.numberSuffixes = {"f", "F", "l", "L", "u", "U", "ul", "UL", "ll", "LL"};

        return rules;
    }

// ===== R LANGUAGE RULES =====
    inline LanguageRules CreateRRules() {
        LanguageRules rules;
        rules.name = "R";
        rules.fileExtensions = {"r", "R", "rmd", "Rmd"};

        rules.keywords = {
                "if", "else", "repeat", "while", "function", "for", "in", "next", "break",
                "TRUE", "FALSE", "NULL", "Inf", "NaN", "NA", "NA_integer_", "NA_real_",
                "NA_complex_", "NA_character_"
        };

        rules.builtins = {
                "c", "list", "vector", "matrix", "array", "data.frame", "factor", "length",
                "names", "dim", "nrow", "ncol", "str", "summary", "head", "tail", "class",
                "typeof", "mode", "attributes", "attr", "is.null", "is.na", "is.nan",
                "is.infinite", "is.finite", "is.numeric", "is.integer", "is.double",
                "is.logical", "is.character", "is.factor", "is.matrix", "is.array",
                "is.data.frame", "is.list", "is.vector", "as.numeric", "as.integer",
                "as.double", "as.logical", "as.character", "as.factor", "as.matrix",
                "as.array", "as.data.frame", "as.list", "as.vector", "mean", "median",
                "var", "sd", "min", "max", "sum", "prod", "range", "quantile", "sort",
                "order", "rank", "unique", "duplicated", "which", "which.min", "which.max",
                "match", "pmatch", "charmatch", "grep", "grepl", "sub", "gsub", "substr",
                "nchar", "paste", "paste0", "sprintf", "cat", "print", "show", "plot",
                "hist", "boxplot", "barplot", "pie", "pairs", "coplot", "par", "layout",
                "mfrow", "mfcol", "apply", "lapply", "sapply", "mapply", "tapply",
                "aggregate", "by", "merge", "rbind", "cbind", "t", "solve", "eigen",
                "svd", "qr", "chol", "det", "norm", "diag", "lower.tri", "upper.tri",
                "crossprod", "tcrossprod", "outer", "kronecker", "expand.grid", "gl",
                "rep", "seq", "sequence", "sample", "choose", "factorial", "gamma",
                "lgamma", "digamma", "trigamma", "beta", "lbeta", "sin", "cos", "tan",
                "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "asinh",
                "acosh", "atanh", "exp", "expm1", "log", "log10", "log2", "log1p",
                "sqrt", "abs", "sign", "floor", "ceiling", "trunc", "round", "signif"
        };

        rules.constants = {"TRUE", "FALSE", "NULL", "Inf", "NaN", "NA"};

        rules.operators = {
                "+", "-", "*", "/", "^", "%%", "%/%", "=", "<-", "<<-", "->", "->>",
                "==", "!=", "<", ">", "<=", ">=", "&", "|", "!", "&&", "||", "%in%",
                "%*%", "%o%", "%x%", "$", "@", "::", ":::", "[", "]", "[[", "]]",
                "(", ")", "{", "}", ",", ";", ":", "?", "~"
        };

        rules.lineCommentStart = {"#"};
        rules.stringDelimiters = {'"', '\''};
        rules.hasStringInterpolation = false;

        return rules;
    }

// ===== SCALA LANGUAGE RULES =====
    inline LanguageRules CreateScalaRules() {
        LanguageRules rules;
        rules.name = "Scala";
        rules.fileExtensions = {"scala", "sc"};

        rules.keywords = {
                "abstract", "case", "catch", "class", "def", "do", "else", "extends",
                "false", "final", "finally", "for", "forSome", "if", "implicit",
                "import", "lazy", "match", "new", "null", "object", "override", "package",
                "private", "protected", "return", "sealed", "super", "this", "throw",
                "trait", "try", "true", "type", "val", "var", "while", "with", "yield"
        };

        rules.types = {
                "Any", "AnyRef", "AnyVal", "Nothing", "Null", "Unit", "Boolean", "Byte",
                "Short", "Char", "Int", "Long", "Float", "Double", "String", "Array",
                "List", "Vector", "Set", "Map", "Seq", "Option", "Some", "None",
                "Either", "Left", "Right", "Future", "Promise", "Try", "Success",
                "Failure", "Stream", "Iterator", "Range", "Tuple2", "Tuple3", "Function1",
                "Function2", "PartialFunction", "BigInt", "BigDecimal"
        };

        rules.builtins = {
                "println", "print", "printf", "readLine", "readInt", "readDouble",
                "require", "assert", "assume", "ensuring", "implicitly", "locally",
                "classOf", "isInstanceOf", "asInstanceOf"
        };

        rules.constants = {"true", "false", "null"};

        rules.operators = {
                "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=", "&&",
                "||", "!", "&", "|", "^", "~", "<<", ">>", ">>>", "+=", "-=", "*=",
                "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ">>>=", "++", "--", "=>",
                "<-", "->", "::", ":::", "#::", "+:", ":+", "++:", "++=", "?", ":",
                "_", "@", "(", ")", "[", "]", "{", "}", ",", ";", ".", "`"
        };

        rules.lineCommentStart = {"//"};
        rules.blockComments = {{"/*", "*/"}};
        rules.stringDelimiters = {'"'};
        rules.characterDelimiters = {'\''};
        rules.hasAttributes = true;
        rules.attributeDelimiters = {{"@", ""}};
        rules.hasStringInterpolation = true;
        rules.interpolationDelimiters = {{"${", "}"}, {"$", ""}};
        rules.hasRawStrings = true;
        rules.rawStringPrefix = "\"\"\"";

        return rules;
    }

// ===== MATLAB LANGUAGE RULES =====
    inline LanguageRules CreateMatlabRules() {
        LanguageRules rules;
        rules.name = "MATLAB";
        rules.fileExtensions = {"m", "mlx", "mat"};

        rules.keywords = {
                "break", "case", "catch", "classdef", "continue", "else", "elseif", "end",
                "for", "function", "global", "if", "otherwise", "parfor", "persistent",
                "return", "spmd", "switch", "try", "while"
        };

        rules.builtins = {
                "abs", "acos", "acosh", "all", "angle", "any", "asin", "asinh", "atan",
                "atan2", "atanh", "ceil", "cell", "char", "class", "clear", "conj",
                "cos", "cosh", "diag", "diff", "double", "eps", "error", "eval", "exp",
                "eye", "fft", "fftshift", "find", "finite", "fix", "floor", "format",
                "function", "global", "hist", "hold", "i", "ifft", "imag", "inf",
                "input", "int8", "int16", "int32", "int64", "inv", "ischar", "isempty",
                "isequal", "isfield", "isfinite", "isinf", "isletter", "islogical",
                "isnan", "isnumeric", "isreal", "isspace", "j", "length", "linspace",
                "load", "log", "log10", "log2", "logical", "lower", "max", "mean",
                "median", "min", "mod", "nan", "nargin", "nargout", "ndims", "numel",
                "ones", "pause", "pi", "plot", "rand", "randn", "real", "rem", "reshape",
                "round", "save", "sign", "sin", "sinh", "size", "sort", "sqrt", "std",
                "strcmp", "strcmpi", "strfind", "strmatch", "strncmp", "strncmpi",
                "subplot", "sum", "tan", "tanh", "title", "transpose", "uint8", "uint16",
                "uint32", "uint64", "upper", "var", "who", "whos", "xlabel", "ylabel",
                "zeros"
        };

        rules.constants = {"true", "false", "pi", "eps", "inf", "nan", "i", "j"};

        rules.operators = {
                "+", "-", "*", "/", "\\", "^", ".*", "./", ".\\", ".^", "=", "==", "~=",
                "<", ">", "<=", ">=", "&", "|", "~", "&&", "||", ":", "(", ")", "[", "]",
                "{", "}", ",", ";", ".", "'", "...", "@"
        };

        rules.lineCommentStart = {"%"};
        rules.blockComments = {{"%{", "%}"}};
        rules.stringDelimiters = {'"', '\''};

        return rules;
    }

// ===== VBA (VISUAL BASIC FOR APPLICATIONS) LANGUAGE RULES =====
    inline LanguageRules CreateVbaRules() {
        LanguageRules rules;
        rules.name = "VBA";
        rules.fileExtensions = {"vba", "bas", "cls", "frm"};
        rules.isCaseSensitive = false;

        rules.keywords = {
                "and", "as", "boolean", "byref", "byte", "byval", "call", "case", "class",
                "const", "currency", "date", "declare", "dim", "do", "double", "each",
                "else", "elseif", "end", "enum", "exit", "false", "for", "function",
                "get", "global", "gosub", "goto", "if", "implements", "in", "integer",
                "is", "let", "lib", "long", "loop", "lset", "me", "mod", "new", "next",
                "not", "nothing", "object", "on", "optional", "or", "paramarray",
                "preserve", "private", "property", "public", "redim", "rem", "resume",
                "return", "rset", "select", "set", "single", "static", "step", "stop",
                "string", "sub", "then", "to", "true", "type", "typeof", "until",
                "variant", "wend", "while", "with", "xor"
        };

        rules.types = {
                "boolean", "byte", "currency", "date", "double", "integer", "long",
                "object", "single", "string", "variant"
        };

        rules.builtins = {
                "abs", "array", "asc", "atn", "cbool", "cbyte", "ccur", "cdate", "cdbl",
                "choose", "chr", "cint", "clng", "cos", "createobject", "csng", "cstr",
                "cvar", "date", "dateadd", "datediff", "datepart", "dateserial",
                "datevalue", "day", "dir", "eof", "error", "exp", "fileattr", "filedatetime",
                "filelen", "fix", "format", "formatcurrency", "formatdatetime", "formatnumber",
                "formatpercent", "freefile", "getattr", "getobject", "hex", "hour",
                "iif", "instr", "instrrev", "int", "isarray", "isdate", "isempty",
                "iserror", "ismissing", "isnull", "isnumeric", "isobject", "lbound",
                "lcase", "left", "len", "loadpicture", "loc", "lof", "log", "ltrim",
                "mid", "minute", "month", "now", "oct", "replace", "right", "rnd",
                "round", "rtrim", "second", "seek", "sgn", "sin", "space", "split",
                "sqr", "str", "strcomp", "strconv", "string", "strreverse", "tan",
                "time", "timer", "timeserial", "timevalue", "trim", "typename", "ubound",
                "ucase", "val", "vartype", "weekday", "year"
        };

        rules.constants = {"true", "false", "nothing", "null", "empty"};

        rules.operators = {
                "+", "-", "*", "/", "\\", "mod", "^", "=", "<>", "<", ">", "<=", ">=",
                "and", "or", "not", "xor", "eqv", "imp", "&", "(", ")", ",", ";", ":",
                ".", "_"
        };

        rules.lineCommentStart = {"'", "rem"};
        rules.stringDelimiters = {'"'};

        return rules;
    }

// ===== SHELL SCRIPTING LANGUAGE RULES =====
    inline LanguageRules CreateShellScriptRules() {
        LanguageRules rules;
        rules.name = "Shell Script";
        rules.fileExtensions = {"sh", "bash", "zsh", "ksh", "csh", "tcsh", "fish"};

        rules.keywords = {
                "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
                "while", "until", "do", "done", "in", "function", "time", "coproc",
                "break", "continue", "return", "exit", "export", "readonly", "local",
                "declare", "typeset", "unset", "shift", "eval", "exec", "source",
                "alias", "unalias", "history", "fc", "jobs", "bg", "fg", "wait",
                "suspend", "disown", "kill", "trap", "true", "false", "test", "let"
        };

        rules.builtins = {
                "echo", "printf", "read", "cd", "pwd", "pushd", "popd", "dirs", "ls",
                "cat", "head", "tail", "grep", "egrep", "fgrep", "sed", "awk", "cut",
                "sort", "uniq", "wc", "tr", "find", "xargs", "which", "whereis",
                "locate", "file", "stat", "df", "du", "free", "ps", "top", "kill",
                "killall", "pgrep", "pkill", "nohup", "screen", "tmux", "ssh", "scp",
                "rsync", "wget", "curl", "tar", "gzip", "gunzip", "zip", "unzip",
                "chmod", "chown", "chgrp", "umask", "ln", "cp", "mv", "rm", "mkdir",
                "rmdir", "touch", "basename", "dirname", "realpath", "readlink",
                "date", "cal", "uptime", "who", "whoami", "id", "groups", "su", "sudo",
                "mount", "umount", "fdisk", "mkfs", "fsck", "crontab", "at", "batch",
                "sleep", "usleep", "yes", "seq", "shuf", "factor", "expr", "bc", "dc"
        };

        rules.constants = {"true", "false"};

        rules.operators = {
                "=", "==", "!=", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-z", "-n",
                "-e", "-f", "-d", "-r", "-w", "-x", "-s", "-L", "-S", "-b", "-c", "-p",
                "-u", "-g", "-k", "-O", "-G", "-N", "-nt", "-ot", "-ef", "&&", "||",
                "!", "&", "|", ";", ";;", "(", ")", "[", "]", "[[", "]]", "{", "}",
                "<", ">", "<<", ">>", "<<<", "&>", "2>", "2>&1", "|&", "$", "${", "}",
                "$(", ")", "`", "~", "*", "?", "+", "^", "\\", "/", "%", ":", ",", "."
        };

        rules.lineCommentStart = {"#"};
        rules.stringDelimiters = {'"', '\''};
        rules.hasStringInterpolation = true;
        rules.interpolationDelimiters = {{"${", "}"}, {"$(", ")"}, {"`", "`"}, {"$", ""}};

        return rules;
    }

// ===== C LANGUAGE RULES =====
    inline LanguageRules CreateCRules() {
        LanguageRules rules;
        rules.name = "C";
        rules.fileExtensions = {"c", "h"};

        rules.keywords = {
                "auto", "break", "case", "char", "const", "continue", "default", "do",
                "double", "else", "enum", "extern", "float", "for", "goto", "if",
                "inline", "int", "long", "register", "restrict", "return", "short",
                "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
                "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof",
                "_Atomic", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local"
        };

        rules.types = {
                "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                "size_t", "ptrdiff_t", "intptr_t", "uintptr_t", "FILE", "bool"
        };

        rules.constants = {"true", "false", "NULL"};
        rules.operators = {
                "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=",
                "&&", "||", "!", "&", "|", "^", "~", "<<", ">>", "++", "--",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
                "->", ".", "?:", ",", ";", "(", ")", "[", "]", "{", "}"
        };

        rules.lineCommentStart = {"//"};
        rules.blockComments = {{"/*", "*/"}};
        rules.stringDelimiters = {'"'};
        rules.characterDelimiters = {'\''};
        rules.hasPreprocessor = true;
        rules.numberSuffixes = {"f", "F", "l", "L", "u", "U", "ul", "UL", "ll", "LL"};

        return rules;
    }

// ===== JAVA LANGUAGE RULES =====
    inline LanguageRules CreateJavaRules() {
        LanguageRules rules;
        rules.name = "Java";
        rules.fileExtensions = {"java"};

        rules.keywords = {
                "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
                "class", "const", "continue", "default", "do", "double", "else", "enum",
                "extends", "final", "finally", "float", "for", "goto", "if", "implements",
                "import", "instanceof", "int", "interface", "long", "native", "new",
                "package", "private", "protected", "public", "return", "short", "static",
                "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
                "transient", "try", "void", "volatile", "while"
        };

        rules.types = {
                "String", "Integer", "Long", "Double", "Float", "Boolean", "Character",
                "Byte", "Short", "Object", "ArrayList", "HashMap", "HashSet", "Vector",
                "StringBuilder", "StringBuffer", "BigInteger", "BigDecimal"
        };

        rules.constants = {"true", "false", "null"};
        rules.operators = {
                "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=",
                "&&", "||", "!", "&", "|", "^", "~", "<<", ">>", ">>>", "++", "--",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ">>>=",
                ".", "?:", ",", ";", "(", ")", "[", "]", "{", "}"
        };

        rules.lineCommentStart = {"//"};
        rules.blockComments = {{"/*", "*/"}};
        rules.stringDelimiters = {'"'};
        rules.characterDelimiters = {'\''};
        rules.hasAttributes = true;
        rules.attributeDelimiters = {{"@", ""}};

        return rules;
    }

// ===== C# LANGUAGE RULES =====
    inline LanguageRules CreateCSharpRules() {
        LanguageRules rules;
        rules.name = "C#";
        rules.fileExtensions = {"cs"};

        rules.keywords = {
                "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char",
                "checked", "class", "const", "continue", "decimal", "default", "delegate",
                "do", "double", "else", "enum", "event", "explicit", "extern", "false",
                "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit",
                "in", "int", "interface", "internal", "is", "lock", "long", "namespace",
                "new", "null", "object", "operator", "out", "override", "params", "private",
                "protected", "public", "readonly", "ref", "return", "sbyte", "sealed",
                "short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
                "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked",
                "unsafe", "ushort", "using", "virtual", "void", "volatile", "while"
        };

        rules.types = {
                "String", "Int32", "Int64", "Double", "Single", "Boolean", "Char", "Byte",
                "Object", "List", "Dictionary", "Array", "StringBuilder", "DateTime"
        };

        rules.constants = {"true", "false", "null"};
        rules.operators = {
                "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=",
                "&&", "||", "!", "&", "|", "^", "~", "<<", ">>", "++", "--",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
                "?.", "??", "=>", ".", "?:", ",", ";", "(", ")", "[", "]", "{", "}"
        };

        rules.lineCommentStart = {"//"};
        rules.blockComments = {{"/*", "*/"}};
        rules.stringDelimiters = {'"'};
        rules.characterDelimiters = {'\''};
        rules.hasPreprocessor = true;
        rules.hasAttributes = true;
        rules.attributeDelimiters = {{"[", "]"}};
        rules.hasStringInterpolation = true;
        rules.interpolationDelimiters = {{"${", "}"}};

        return rules;
    }

// ===== PYTHON LANGUAGE RULES =====
    inline LanguageRules CreatePythonRules() {
        LanguageRules rules;
        rules.name = "Python";
        rules.fileExtensions = {"py", "pyw", "pyi"};

        rules.keywords = {
                "False", "None", "True", "and", "as", "assert", "async", "await", "break",
                "class", "continue", "def", "del", "elif", "else", "except", "finally",
                "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
                "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"
        };

        rules.builtins = {
                "abs", "all", "any", "ascii", "bin", "bool", "bytearray", "bytes", "callable",
                "chr", "classmethod", "compile", "complex", "delattr", "dict", "dir", "divmod",
                "enumerate", "eval", "exec", "filter", "float", "format", "frozenset",
                "getattr", "globals", "hasattr", "hash", "help", "hex", "id", "input",
                "int", "isinstance", "issubclass", "iter", "len", "list", "locals", "map",
                "max", "memoryview", "min", "next", "object", "oct", "open", "ord", "pow",
                "print", "property", "range", "repr", "reversed", "round", "set", "setattr",
                "slice", "sorted", "staticmethod", "str", "sum", "super", "tuple", "type",
                "vars", "zip", "__import__"
        };

        rules.constants = {"True", "False", "None"};
        rules.operators = {
                "+", "-", "*", "/", "//", "%", "**", "=", "==", "!=", "<", ">", "<=", ">=",
                "and", "or", "not", "&", "|", "^", "~", "<<", ">>", "+=", "-=", "*=",
                "/=", "//=", "%=", "**=", "&=", "|=", "^=", "<<=", ">>=", ".", ",", ":",
                ";", "(", ")", "[", "]", "{", "}"
        };

        rules.lineCommentStart = {"#"};
        rules.stringDelimiters = {'"', '\''};
        rules.hasRawStrings = true;
        rules.rawStringPrefix = "r";
        rules.hasAttributes = true;
        rules.attributeDelimiters = {{"@", ""}};
        rules.hasStringInterpolation = true;
        rules.interpolationDelimiters = {{"{", "}"}};

        return rules;
    }

// ===== PASCAL LANGUAGE RULES =====
    inline LanguageRules CreatePascalRules() {
        LanguageRules rules;
        rules.name = "Pascal";
        rules.fileExtensions = {"pas", "pp", "inc"};
        rules.isCaseSensitive = false;

        rules.keywords = {
                "and", "array", "begin", "case", "const", "div", "do", "downto", "else",
                "end", "file", "for", "function", "goto", "if", "in", "label", "mod",
                "nil", "not", "of", "or", "packed", "procedure", "program", "record",
                "repeat", "set", "then", "to", "type", "until", "var", "while", "with",
                "absolute", "abstract", "as", "assembler", "class", "constructor",
                "destructor", "dispinterface", "except", "exports", "finalization",
                "finally", "inherited", "initialization", "inline", "interface", "is",
                "library", "object", "on", "out", "override", "private", "property",
                "protected", "public", "published", "raise", "string", "try", "unit",
                "uses", "virtual"
        };

        rules.types = {
                "integer", "real", "boolean", "char", "string", "byte", "word", "longint",
                "single", "double", "extended", "comp", "currency", "pointer", "pchar",
                "shortint", "smallint", "longword", "int64", "cardinal", "qword"
        };

        rules.constants = {"true", "false", "nil"};
        rules.operators = {
                "+", "-", "*", "/", "div", "mod", "=", "<>", "<", ">", "<=", ">=",
                "and", "or", "not", "xor", "shl", "shr", ":=", ".", "..", ",", ";",
                "(", ")", "[", "]", "^", "@"
        };

        rules.lineCommentStart = {"//"};
        rules.blockComments = {{"(*", "*)"}, {"{", "}"}};
        rules.stringDelimiters = {'\''};

        return rules;
    }

// ===== FORTRAN LANGUAGE RULES =====
    inline LanguageRules CreateFortranRules() {
        LanguageRules rules;
        rules.name = "Fortran";
        rules.fileExtensions = {"f", "f90", "f95", "f03", "f08", "for", "ftn"};
        rules.isCaseSensitive = false;

        rules.keywords = {
                "allocatable", "allocate", "assign", "assignment", "associate", "asynchronous",
                "backspace", "bind", "block", "call", "case", "class", "close", "codimension",
                "common", "contains", "continue", "critical", "cycle", "data", "deallocate",
                "default", "deferred", "dimension", "do", "else", "elseif", "elsewhere",
                "end", "endfile", "endif", "entry", "enum", "enumerator", "equivalence",
                "error", "exit", "extends", "external", "file", "final", "forall", "format",
                "function", "generic", "goto", "if", "implicit", "import", "include",
                "inquire", "intent", "interface", "intrinsic", "kind", "len", "lock",
                "module", "namelist", "none", "non_overridable", "nopass", "nullify",
                "only", "open", "operator", "optional", "parameter", "pass", "pause",
                "pointer", "print", "private", "procedure", "program", "protected",
                "public", "pure", "read", "recursive", "result", "return", "rewind",
                "rewrite", "save", "select", "sequence", "stop", "subroutine", "sync",
                "target", "then", "type", "unlock", "use", "value", "volatile", "wait",
                "where", "while", "write"
        };

        rules.types = {
                "integer", "real", "double", "precision", "complex", "logical", "character",
                "type", "class"
        };

        rules.constants = {".true.", ".false."};
        rules.operators = {
                "+", "-", "*", "/", "**", "=", "==", "/=", "<", ">", "<=", ">=",
                ".and.", ".or.", ".not.", ".eqv.", ".neqv.", ".eq.", ".ne.", ".lt.",
                ".le.", ".gt.", ".ge.", "(", ")", ",", ";"
        };

        rules.lineCommentStart = {"!", "C", "c", "*"};
        rules.stringDelimiters = {'"', '\''};

        return rules;
    }

// ===== BASIC LANGUAGE RULES =====
    inline LanguageRules CreateBasicRules() {
        LanguageRules rules;
        rules.name = "BASIC";
        rules.fileExtensions = {"bas", "basic"};
        rules.isCaseSensitive = false;

        rules.keywords = {
                "and", "as", "byref", "byval", "call", "case", "const", "declare", "dim",
                "do", "each", "else", "elseif", "end", "exit", "for", "function", "global",
                "gosub", "goto", "if", "in", "let", "local", "loop", "next", "not", "or",
                "private", "public", "redim", "return", "select", "step", "sub", "then",
                "to", "type", "until", "wend", "while", "with", "xor"
        };

        rules.types = {
                "integer", "long", "single", "double", "string", "boolean", "byte", "variant"
        };

        rules.constants = {"true", "false", "nothing", "null"};
        rules.operators = {
                "+", "-", "*", "/", "\\", "mod", "^", "=", "<>", "<", ">", "<=", ">=",
                "and", "or", "not", "xor", "&", "(", ")", ",", ";", "."
        };

        rules.lineCommentStart = {"'", "REM", "rem"};
        rules.stringDelimiters = {'"'};

        return rules;
    }

// ===== LUA LANGUAGE RULES =====
    inline LanguageRules CreateLuaRules() {
        LanguageRules rules;
        rules.name = "Lua";
        rules.fileExtensions = {"lua"};

        rules.keywords = {
                "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
                "then", "true", "until", "while"
        };

        rules.builtins = {
                "assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs",
                "load", "loadfile", "next", "pairs", "pcall", "print", "rawequal",
                "rawget", "rawlen", "rawset", "require", "select", "setmetatable",
                "tonumber", "tostring", "type", "xpcall", "_G", "_VERSION"
        };

        rules.constants = {"true", "false", "nil"};
        rules.operators = {
                "+", "-", "*", "/", "%", "^", "#", "==", "~=", "<=", ">=", "<", ">",
                "=", "(", ")", "{", "}", "[", "]", ";", ":", ",", ".", "..", "..."
        };

        rules.lineCommentStart = {"--"};
        rules.blockComments = {{"--[[", "]]"}};
        rules.stringDelimiters = {'"', '\''};
        rules.hasRawStrings = true;
        rules.rawStringPrefix = "[[";

        return rules;
    }

// ===== LISP LANGUAGE RULES =====
    inline LanguageRules CreateLispRules() {
        LanguageRules rules;
        rules.name = "Lisp";
        rules.fileExtensions = {"lisp", "lsp", "l"};

        rules.keywords = {
                "defun", "defvar", "defparameter", "defconstant", "defmacro", "defclass",
                "defgeneric", "defmethod", "defstruct", "deftype", "lambda", "let", "let*",
                "flet", "labels", "macrolet", "symbol-macrolet", "progn", "prog1", "prog2",
                "if", "when", "unless", "cond", "case", "typecase", "and", "or", "not",
                "loop", "do", "do*", "dotimes", "dolist", "return", "return-from", "go",
                "tagbody", "catch", "throw", "unwind-protect", "multiple-value-bind",
                "multiple-value-call", "multiple-value-prog1", "values", "setf", "setq",
                "psetf", "psetq", "incf", "decf", "push", "pop", "pushnew"
        };

        rules.builtins = {
                "cons", "car", "cdr", "list", "append", "length", "reverse", "nreverse",
                "member", "assoc", "rassoc", "subst", "mapcar", "maplist", "mapc", "mapl",
                "reduce", "sort", "stable-sort", "find", "position", "count", "remove",
                "delete", "substitute", "nsubstitute", "concatenate", "merge", "search",
                "mismatch", "replace", "fill", "copy-seq", "elt", "nth", "nthcdr",
                "last", "butlast", "nbutlast", "ldiff", "tailp", "list-length",
                "make-list", "copy-list", "copy-tree", "tree-equal", "endp", "null",
                "listp", "consp", "atom", "symbolp", "stringp", "numberp", "integerp",
                "rationalp", "floatp", "realp", "complexp", "characterp", "arrayp",
                "vectorp", "simple-vector-p", "bit-vector-p", "simple-bit-vector-p"
        };

        rules.constants = {"t", "nil"};
        rules.operators = {
                "+", "-", "*", "/", "=", "/=", "<", ">", "<=", ">=", "eq", "eql", "equal",
                "equalp", "and", "or", "not", "(", ")", "'", "`", ",", ",@", "#"
        };

        rules.lineCommentStart = {";"};
        rules.blockComments = {{"#|", "|#"}};
        rules.stringDelimiters = {'"'};

        return rules;
    }

// ===== COMMON LISP LANGUAGE RULES =====
    inline LanguageRules CreateCommonLispRules() {
        LanguageRules rules = CreateLispRules();
        rules.name = "Common Lisp";
        rules.fileExtensions = {"cl", "lisp", "lsp"};

        // Add Common Lisp specific keywords
        rules.keywords.insert({
                                      "declare", "locally", "the", "ignore", "ignorable", "dynamic-extent",
                                      "type", "ftype", "inline", "notinline", "optimize", "special",
                                      "compilation-speed", "debug", "safety", "space", "speed"
                              });

        return rules;
    }

// ===== SMALLTALK LANGUAGE RULES =====
    inline LanguageRules CreateSmalltalkRules() {
        LanguageRules rules;
        rules.name = "Smalltalk";
        rules.fileExtensions = {"st"};

        rules.keywords = {
                "self", "super", "true", "false", "nil", "thisContext"
        };

        rules.builtins = {
                "Object", "Behavior", "ClassDescription", "Class", "Metaclass",
                "Collection", "SequenceableCollection", "ArrayedCollection", "Array",
                "String", "Symbol", "OrderedCollection", "SortedCollection", "Bag",
                "Set", "Dictionary", "Association", "Interval", "Number", "Integer",
                "SmallInteger", "LargeInteger", "Fraction", "Float", "Point", "Rectangle",
                "Date", "Time", "Timestamp", "Duration", "Boolean", "True", "False",
                "UndefinedObject", "Block", "BlockClosure", "Context", "MethodContext",
                "Process", "Processor", "Semaphore", "Mutex", "Monitor", "Exception",
                "Error", "Warning", "Notification", "Stream", "ReadStream", "WriteStream",
                "File", "FileDirectory", "Transcript"
        };

        rules.constants = {"true", "false", "nil"};
        rules.operators = {
                "+", "-", "*", "/", "//", "\\\\", "=", "~=", "<", ">", "<=", ">=",
                "&", "|", "not", "(", ")", "[", "]", "{", "}", ".", ";", ":"
        };

        rules.lineCommentStart = {"\""};
        rules.stringDelimiters = {'\''};
        rules.characterDelimiters = {'};

                return rules;
        }

// ===== PROLOG LANGUAGE RULES =====
        inline LanguageRules CreatePrologRules() {
            LanguageRules rules;
            rules.name = "Prolog";
            rules.fileExtensions = {"pl", "pro", "prolog"};

            rules.keywords = {
                    "is", "=", "\\=", "==", "\\==", "=..", "=:=", "=\\=", "<", ">", "=<", ">=",
                    "atom", "atomic", "compound", "float", "integer", "nonvar", "number", "var",
                    "callable", "ground", "acyclic_term", "subsumes_term", "unify_with_occurs_check",
                    "assert", "asserta", "assertz", "retract", "retractall", "abolish", "clause",
                    "current_predicate", "predicate_property", "functor", "arg", "copy_term",
                    "bagof", "setof", "findall", "forall", "once", "ignore", "call", "apply",
                    "not", "\\+", "if", "then", "else", "catch", "throw", "halt", "abort",
                    "true", "false", "fail", "repeat", "cut", "!"
            };

            rules.builtins = {
                    "append", "member", "length", "reverse", "sort", "msort", "keysort",
                    "predsort", "permutation", "sublist", "select", "nth0", "nth1", "last",
                    "prefix", "suffix", "subseq", "flatten", "max_list", "min_list",
                    "sum_list", "max_member", "min_member", "include", "exclude", "partition",
                    "write", "writeln", "writeq", "write_term", "print", "portray", "read",
                    "read_term", "get", "get_char", "put", "put_char", "nl", "tab", "flush_output",
                    "see", "seen", "tell", "told", "open", "close", "current_input", "current_output",
                    "set_input", "set_output", "stream_property", "at_end_of_stream", "peek_char",
                    "peek_code", "get_byte", "put_byte", "peek_byte"
            };

            rules.constants = {"true", "false", "fail"};
            rules.operators = {
                    ":-", "?-", "=", "\\=", "==", "\\==", "=..", "is", "=:=", "=\\=",
                    "<", ">", "=<", ">=", "+", "-", "*", "/", "//", "mod", "rem", "**",
                    "^", "abs", "sign", "max", "min", "(", ")", "[", "]", "{", "}",
                    "|", ",", ";", "!", "\\+", "=\\=", "@<", "@>", "@=<", "@>="
            };

            rules.lineCommentStart = {"%"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"'};
            rules.characterDelimiters = {'\''};

            return rules;
        }

// ===== PERL LANGUAGE RULES =====
        inline LanguageRules CreatePerlRules() {
            LanguageRules rules;
            rules.name = "Perl";
            rules.fileExtensions = {"pl", "pm", "perl"};

            rules.keywords = {
                    "abs", "accept", "alarm", "and", "atan2", "bind", "binmode", "bless",
                    "caller", "chdir", "chmod", "chomp", "chop", "chown", "chr", "chroot",
                    "close", "closedir", "connect", "continue", "cos", "crypt", "dbmclose",
                    "dbmopen", "defined", "delete", "die", "do", "dump", "each", "else",
                    "elsif", "endgrent", "endhostent", "endnetent", "endprotoent", "endpwent",
                    "endservent", "eof", "eval", "exec", "exists", "exit", "exp", "fcntl",
                    "fileno", "flock", "for", "foreach", "fork", "format", "formline",
                    "getc", "getgrent", "getgrgid", "getgrnam", "gethostbyaddr", "gethostbyname",
                    "gethostent", "getlogin", "getnetbyaddr", "getnetbyname", "getnetent",
                    "getpeername", "getpgrp", "getppid", "getpriority", "getprotobyname",
                    "getprotobynumber", "getprotoent", "getpwent", "getpwnam", "getpwuid",
                    "getservbyname", "getservbyport", "getservent", "getsockname", "getsockopt",
                    "glob", "gmtime", "goto", "grep", "hex", "if", "import", "index", "int",
                    "ioctl", "join", "keys", "kill", "last", "lc", "lcfirst", "length",
                    "link", "listen", "local", "localtime", "log", "lstat", "map", "mkdir",
                    "msgctl", "msgget", "msgrcv", "msgsnd", "my", "next", "no", "not",
                    "oct", "open", "opendir", "or", "ord", "our", "pack", "package", "pipe",
                    "pop", "pos", "print", "printf", "prototype", "push", "quotemeta",
                    "rand", "read", "readdir", "readline", "readlink", "readpipe", "recv",
                    "redo", "ref", "rename", "require", "reset", "return", "reverse",
                    "rewinddir", "rindex", "rmdir", "scalar", "seek", "seekdir", "select",
                    "semctl", "semget", "semop", "send", "setgrent", "sethostent", "setnetent",
                    "setpgrp", "setpriority", "setprotoent", "setpwent", "setservent",
                    "setsockopt", "shift", "shmctl", "shmget", "shmread", "shmwrite",
                    "shutdown", "sin", "sleep", "socket", "socketpair", "sort", "splice",
                    "split", "sprintf", "sqrt", "srand", "stat", "study", "sub", "substr",
                    "symlink", "syscall", "sysopen", "sysread", "sysseek", "system",
                    "syswrite", "tell", "telldir", "tie", "tied", "time", "times", "tr",
                    "truncate", "uc", "ucfirst", "umask", "undef", "unless", "unlink",
                    "unpack", "unshift", "untie", "until", "use", "utime", "values",
                    "vec", "wait", "waitpid", "wantarray", "warn", "while", "write", "xor"
            };

            rules.constants = {"undef"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "**", "=", "+=", "-=", "*=", "/=", "%=", "**=",
                    "==", "!=", "<", ">", "<=", ">=", "<=>", "eq", "ne", "lt", "gt", "le",
                    "ge", "cmp", "&&", "||", "!", "and", "or", "not", "xor", "&", "|", "^",
                    "~", "<<", ">>", ".", "x", "=~", "!~", "?:", "(", ")", "[", "]", "{", "}",
                    ",", ";", "->", "::", "$", "@", "%", "&", "*"
            };

            rules.lineCommentStart = {"#"};
            rules.stringDelimiters = {'"', '\''};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"${", "}"}, {"$", ""}, {"@", ""}};

            return rules;
        }

// ===== RUBY LANGUAGE RULES =====
        inline LanguageRules CreateRubyRules() {
            LanguageRules rules;
            rules.name = "Ruby";
            rules.fileExtensions = {"rb", "ruby", "rbw"};

            rules.keywords = {
                    "alias", "and", "begin", "break", "case", "class", "def", "defined",
                    "do", "else", "elsif", "end", "ensure", "false", "for", "if", "in",
                    "module", "next", "nil", "not", "or", "redo", "rescue", "retry",
                    "return", "self", "super", "then", "true", "undef", "unless", "until",
                    "when", "while", "yield", "__FILE__", "__LINE__"
            };

            rules.builtins = {
                    "Array", "BasicObject", "Bignum", "Binding", "Class", "Comparable",
                    "Complex", "Data", "Dir", "Enumerable", "Enumerator", "ENV", "Exception",
                    "FalseClass", "File", "Fixnum", "Float", "GC", "Hash", "Integer", "IO",
                    "Kernel", "Marshal", "MatchData", "Math", "Method", "Module", "NilClass",
                    "Numeric", "Object", "ObjectSpace", "Proc", "Process", "Random", "Range",
                    "Rational", "Regexp", "String", "Struct", "Symbol", "Thread", "Time",
                    "TrueClass", "UnboundMethod"
            };

            rules.constants = {"true", "false", "nil"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "**", "=", "+=", "-=", "*=", "/=", "%=", "**=",
                    "==", "!=", "<", ">", "<=", ">=", "<=>", "===", "=~", "!~", "&&", "||",
                    "!", "and", "or", "not", "&", "|", "^", "~", "<<", ">>", ".", "::",
                    "?:", "(", ")", "[", "]", "{", "}", ",", ";", "=>", "..", "..."
            };

            rules.lineCommentStart = {"#"};
            rules.stringDelimiters = {'"', '\''};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"#{", "}"}};

            return rules;
        }

// ===== GO LANGUAGE RULES =====
        inline LanguageRules CreateGoRules() {
            LanguageRules rules;
            rules.name = "Go";
            rules.fileExtensions = {"go"};

            rules.keywords = {
                    "break", "case", "chan", "const", "continue", "default", "defer", "else",
                    "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
                    "map", "package", "range", "return", "select", "struct", "switch", "type",
                    "var"
            };

            rules.types = {
                    "bool", "byte", "complex64", "complex128", "error", "float32", "float64",
                    "int", "int8", "int16", "int32", "int64", "rune", "string", "uint",
                    "uint8", "uint16", "uint32", "uint64", "uintptr"
            };

            rules.builtins = {
                    "append", "cap", "close", "complex", "copy", "delete", "imag", "len",
                    "make", "new", "panic", "print", "println", "real", "recover"
            };

            rules.constants = {"true", "false", "iota", "nil"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "&^", "+=", "-=",
                    "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "&^=", "&&", "||",
                    "<-", "++", "--", "==", "<", ">", "=", "!", "!=", "<=", ">=", ":=",
                    "...", "(", ")", "[", "]", "{", "}", ",", ";", ".", ":"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"', '`'};
            rules.characterDelimiters = {'\''};

            return rules;
        }

// ===== SWIFT LANGUAGE RULES =====
        inline LanguageRules CreateSwiftRules() {
            LanguageRules rules;
            rules.name = "Swift";
            rules.fileExtensions = {"swift"};

            rules.keywords = {
                    "associatedtype", "class", "deinit", "enum", "extension", "fileprivate",
                    "func", "import", "init", "inout", "internal", "let", "open", "operator",
                    "private", "protocol", "public", "rethrows", "static", "struct", "subscript",
                    "typealias", "var", "break", "case", "continue", "default", "defer", "do",
                    "else", "fallthrough", "for", "guard", "if", "in", "repeat", "return",
                    "switch", "where", "while", "as", "catch", "false", "is", "nil", "super",
                    "self", "Self", "throw", "throws", "true", "try", "__COLUMN__", "__FILE__",
                    "__FUNCTION__", "__LINE__"
            };

            rules.types = {
                    "Any", "AnyObject", "Array", "Bool", "Character", "Dictionary", "Double",
                    "Float", "Int", "Int8", "Int16", "Int32", "Int64", "Optional", "Set",
                    "String", "UInt", "UInt8", "UInt16", "UInt32", "UInt64", "Void"
            };

            rules.constants = {"true", "false", "nil"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "=", "+=", "-=", "*=", "/=", "%=", "==", "!=",
                    "<", ">", "<=", ">=", "&&", "||", "!", "&", "|", "^", "~", "<<", ">>",
                    "&=", "|=", "^=", "<<=", ">>=", "?", ":", "??", "?.", "(", ")", "[", "]",
                    "{", "}", ",", ";", ".", "->"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"'};
            rules.characterDelimiters = {'\''};
            rules.hasAttributes = true;
            rules.attributeDelimiters = {{"@", ""}};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"\\(", ")"}};

            return rules;
        }

// ===== KOTLIN LANGUAGE RULES =====
        inline LanguageRules CreateKotlinRules() {
            LanguageRules rules;
            rules.name = "Kotlin";
            rules.fileExtensions = {"kt", "kts"};

            rules.keywords = {
                    "as", "as?", "break", "class", "continue", "do", "else", "false", "for",
                    "fun", "if", "in", "!in", "interface", "is", "!is", "null", "object",
                    "package", "return", "super", "this", "throw", "true", "try", "typealias",
                    "typeof", "val", "var", "when", "while", "by", "catch", "constructor",
                    "delegate", "dynamic", "field", "file", "finally", "get", "import",
                    "init", "param", "property", "receiver", "set", "setparam", "where",
                    "actual", "abstract", "annotation", "companion", "const", "crossinline",
                    "data", "enum", "expect", "external", "final", "infix", "inline",
                    "inner", "internal", "lateinit", "noinline", "open", "operator",
                    "out", "override", "private", "protected", "public", "reified",
                    "sealed", "suspend", "tailrec", "vararg"
            };

            rules.types = {
                    "Any", "Array", "Boolean", "Byte", "Char", "Double", "Float", "Int",
                    "Long", "Nothing", "Short", "String", "Unit", "ByteArray", "CharArray",
                    "DoubleArray", "FloatArray", "IntArray", "LongArray", "ShortArray",
                    "List", "MutableList", "Set", "MutableSet", "Map", "MutableMap"
            };

            rules.constants = {"true", "false", "null"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "=", "+=", "-=", "*=", "/=", "%=", "++", "--",
                    "==", "!=", "===", "!==", "<", ">", "<=", ">=", "&&", "||", "!", "&",
                    "|", "^", "~", "shl", "shr", "ushr", "and", "or", "xor", "inv", "?:",
                    "?.", "!!", "..", "in", "!in", "is", "!is", "as", "as?", "(", ")", "[",
                    "]", "{", "}", ",", ";", ".", "::", "->"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"'};
            rules.characterDelimiters = {'\''};
            rules.hasAttributes = true;
            rules.attributeDelimiters = {{"@", ""}};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"${", "}"}, {"$", ""}};

            return rules;
        }

// ===== DART LANGUAGE RULES =====
        inline LanguageRules CreateDartRules() {
            LanguageRules rules;
            rules.name = "Dart";
            rules.fileExtensions = {"dart"};

            rules.keywords = {
                    "abstract", "as", "assert", "async", "await", "break", "case", "catch",
                    "class", "const", "continue", "covariant", "default", "deferred", "do",
                    "dynamic", "else", "enum", "export", "extends", "extension", "external",
                    "factory", "false", "final", "finally", "for", "Function", "get", "hide",
                    "if", "implements", "import", "in", "interface", "is", "library", "mixin",
                    "new", "null", "on", "operator", "part", "required", "rethrow", "return",
                    "set", "show", "static", "super", "switch", "sync", "this", "throw",
                    "true", "try", "typedef", "var", "void", "while", "with", "yield"
            };

            rules.types = {
                    "bool", "double", "int", "num", "String", "Object", "dynamic", "void",
                    "List", "Map", "Set", "Iterable", "Iterator", "Stream", "Future",
                    "Completer", "Duration", "DateTime", "RegExp", "StringBuffer", "Symbol"
            };

            rules.constants = {"true", "false", "null"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "~/", "=", "+=", "-=", "*=", "/=", "%=", "~/=",
                    "++", "--", "==", "!=", "<", ">", "<=", ">=", "&&", "||", "!", "&", "|",
                    "^", "~", "<<", ">>", ">>>", "&=", "|=", "^=", "<<=", ">>=", ">>>=",
                    "?", ":", "??", "??=", "?.", "!.", "..", "...", "(", ")", "[", "]",
                    "{", "}", ",", ";", ".", "=>", "<", ">"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"', '\''};
            rules.hasAttributes = true;
            rules.attributeDelimiters = {{"@", ""}};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"${", "}"}, {"$", ""}};

            return rules;
        }

// ===== RUST LANGUAGE RULES =====
        inline LanguageRules CreateRustRules() {
            LanguageRules rules;
            rules.name = "Rust";
            rules.fileExtensions = {"rs"};

            rules.keywords = {
                    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
                    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
                    "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
                    "self", "Self", "static", "struct", "super", "trait", "true", "type",
                    "union", "unsafe", "use", "where", "while", "abstract", "become", "box",
                    "do", "final", "macro", "override", "priv", "typeof", "unsized", "virtual",
                    "yield", "try"
            };

            rules.types = {
                    "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64", "i128", "isize",
                    "str", "u8", "u16", "u32", "u64", "u128", "usize", "String", "Vec",
                    "HashMap", "HashSet", "BTreeMap", "BTreeSet", "Option", "Result", "Box",
                    "Rc", "Arc", "RefCell", "Cell", "Mutex", "RwLock"
            };

            rules.constants = {"true", "false"};
            rules.operators = {
                    "+", "-", "*", "/", "%", "=", "+=", "-=", "*=", "/=", "%=", "==", "!=",
                    "<", ">", "<=", ">=", "&&", "||", "!", "&", "|", "^", "~", "<<", ">>",
                    "&=", "|=", "^=", "<<=", ">>=", "?", ":", "..", "..=", "=>", "->",
                    "::", "&mut", "(", ")", "[", "]", "{", "}", ",", ";", ".", "#"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"'};
            rules.characterDelimiters = {'\''};
            rules.hasAttributes = true;
            rules.attributeDelimiters = {{"#[", "]"}, {"#![", "]"}};
            rules.hasRawStrings = true;
            rules.rawStringPrefix = "r";

            return rules;
        }

// ===== ELIXIR LANGUAGE RULES =====
        inline LanguageRules CreateElixirRules() {
            LanguageRules rules;
            rules.name = "Elixir";
            rules.fileExtensions = {"ex", "exs"};

            rules.keywords = {
                    "after", "and", "case", "catch", "cond", "def", "defp", "defmacro",
                    "defmacrop", "defmodule", "defprotocol", "defimpl", "defstruct",
                    "defexception", "defdelegate", "do", "else", "elsif", "end", "false",
                    "fn", "for", "if", "in", "import", "not", "or", "quote", "receive",
                    "rescue", "true", "try", "unless", "unquote", "unquote_splicing",
                    "use", "when", "with"
            };

            rules.builtins = {
                    "abs", "apply", "atom_to_binary", "atom_to_list", "binary_part",
                    "binary_to_atom", "binary_to_existing_atom", "binary_to_list",
                    "binary_to_term", "bit_size", "byte_size", "ceil", "div", "elem",
                    "exit", "float", "floor", "hd", "insert_elem", "inspect", "integer_to_binary",
                    "integer_to_list", "iodata_length", "iodata_to_binary", "is_atom",
                    "is_binary", "is_bitstring", "is_boolean", "is_float", "is_function",
                    "is_integer", "is_list", "is_map", "is_nil", "is_number", "is_pid",
                    "is_port", "is_process_alive", "is_reference", "is_tuple", "length",
                    "list_to_atom", "list_to_binary", "list_to_existing_atom", "list_to_float",
                    "list_to_integer", "list_to_tuple", "make_ref", "map_size", "max", "min",
                    "node", "nodes", "now", "open_port", "pid_to_list", "port_close",
                    "port_command", "port_connect", "port_control", "put_elem", "rem",
                    "round", "self", "setelement", "size", "spawn", "spawn_link", "spawn_monitor",
                    "spawn_opt", "split_binary", "term_to_binary", "throw", "tl", "trunc",
                    "tuple_size", "tuple_to_list"
            };

            rules.constants = {"true", "false", "nil"};
            rules.operators = {
                    "+", "-", "*", "/", "div", "rem", "=", "==", "!=", "===", "!==", "<",
                    ">", "<=", ">=", "and", "or", "not", "&&", "||", "!", "&&&", "|||",
                    "^^^", "~~~", "<<<", ">>>", "++", "--", "<>", "++", "<-", "->", "|>",
                    "=~", "in", "..", "<-", "\\\\", "::", "=>", "(", ")", "[", "]", "{",
                    "}", ",", ";", ".", "|"
            };

            rules.lineCommentStart = {"#"};
            rules.stringDelimiters = {'"'};
            rules.characterDelimiters = {'\''};
            rules.hasAttributes = true;
            rules.attributeDelimiters = {{"@", ""}};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"#{", "}"}};

            return rules;
        }

// ===== ASSEMBLY LANGUAGE RULES =====

// X86 Assembly
        inline LanguageRules CreateX86AssemblyRules() {
            LanguageRules rules;
            rules.name = "x86 Assembly";
            rules.fileExtensions = {"asm", "s", "S"};
            rules.isAssembly = true;
            rules.isCaseSensitive = false;

            rules.instructions = {
                    "aaa", "aad", "aam", "aas", "adc", "add", "and", "arpl", "bound",
                    "bsf", "bsr", "bswap", "bt", "btc", "btr", "bts", "call", "cbw",
                    "cdq", "clc", "cld", "cli", "clts", "cmc", "cmp", "cmps", "cmpsb",
                    "cmpsd", "cmpsw", "cmpxchg", "cwd", "cwde", "daa", "das", "dec",
                    "div", "enter", "hlt", "idiv", "imul", "in", "inc", "ins", "insb",
                    "insd", "insw", "int", "into", "invd", "invlpg", "iret", "iretd",
                    "ja", "jae", "jb", "jbe", "jc", "jcxz", "je", "jecxz", "jg", "jge",
                    "jl", "jle", "jmp", "jna", "jnae", "jnb", "jnbe", "jnc", "jne",
                    "jng", "jnge", "jnl", "jnle", "jno", "jnp", "jns", "jnz", "jo",
                    "jp", "jpe", "jpo", "js", "jz", "lahf", "lar", "lea", "leave",
                    "lgdt", "lidt", "lgs", "lss", "lds", "les", "lfs", "lldt", "lmsw",
                    "lock", "lods", "lodsb", "lodsd", "lodsw", "loop", "loope", "loopne",
                    "loopnz", "loopz", "lsl", "ltr", "mov", "movs", "movsb", "movsd",
                    "movsw", "movsx", "movzx", "mul", "neg", "nop", "not", "or", "out",
                    "outs", "outsb", "outsd", "outsw", "pop", "popa", "popad", "popf",
                    "popfd", "push", "pusha", "pushad", "pushf", "pushfd", "rcl", "rcr",
                    "rep", "repe", "repne", "repnz", "repz", "ret", "retf", "rol", "ror",
                    "sahf", "sal", "sar", "sbb", "scas", "scasb", "scasd", "scasw",
                    "seta", "setae", "setb", "setbe", "setc", "sete", "setg", "setge",
                    "setl", "setle", "setna", "setnae", "setnb", "setnbe", "setnc",
                    "setne", "setng", "setnge", "setnl", "setnle", "setno", "setnp",
                    "setns", "setnz", "seto", "setp", "setpe", "setpo", "sets", "setz",
                    "sgdt", "shl", "shld", "shr", "shrd", "sidt", "sldt", "smsw", "stc",
                    "std", "sti", "stos", "stosb", "stosd", "stosw", "str", "sub", "test",
                    "verr", "verw", "wait", "wbinvd", "xchg", "xlat", "xlatb", "xor"
            };

            rules.registers = {
                    "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp", "eip",
                    "ax", "bx", "cx", "dx", "si", "di", "sp", "bp", "ip",
                    "al", "bl", "cl", "dl", "ah", "bh", "ch", "dh",
                    "cs", "ds", "es", "fs", "gs", "ss", "cr0", "cr2", "cr3", "cr4",
                    "dr0", "dr1", "dr2", "dr3", "dr6", "dr7", "tr3", "tr4", "tr5",
                    "tr6", "tr7", "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7"
            };

            rules.keywords = {
                    "byte", "word", "dword", "qword", "fword", "tbyte", "ptr", "offset",
                    "seg", "short", "near", "far", "proc", "endp", "macro", "endm",
                    "struc", "ends", "union", "record", "segment", "assume", "group",
                    "public", "extern", "extrn", "global", "section", "org", "align",
                    "db", "dw", "dd", "dq", "dt", "resb", "resw", "resd", "resq", "rest",
                    "times", "equ", "label", "end"
            };

            rules.operators = {
                    "+", "-", "*", "/", "%", "&", "|", "^", "~", "<<", ">>", "(", ")",
                    "[", "]", ",", ":", ";"
            };

            rules.lineCommentStart = {";"};
            rules.stringDelimiters = {'"', '\''};

            return rules;
        }

// ARM Assembly
        inline LanguageRules CreateArmAssemblyRules() {
            LanguageRules rules;
            rules.name = "ARM Assembly";
            rules.fileExtensions = {"s", "S", "arm"};
            rules.isAssembly = true;
            rules.isCaseSensitive = false;

            rules.instructions = {
                    "adc", "add", "and", "asr", "b", "bic", "bl", "bx", "cmn", "cmp",
                    "eor", "ldr", "ldrb", "ldrh", "ldm", "ldmia", "ldmib", "ldmda",
                    "ldmdb", "lsl", "lsr", "mla", "mov", "mul", "mvn", "orr", "ror",
                    "rrx", "rsb", "rsc", "sbc", "str", "strb", "strh", "stm", "stmia",
                    "stmib", "stmda", "stmdb", "sub", "swi", "swp", "swpb", "teq", "tst",
                    "umlal", "umull", "smlal", "smull", "bkpt", "blx", "cdp", "clz",
                    "ldc", "mcr", "mcrr", "mrc", "mrrc", "pld", "qadd", "qdadd", "qdsub",
                    "qsub", "smlabb", "smlabt", "smlalbb", "smlalbt", "smlaltb", "smlaltt",
                    "smlatb", "smlatt", "smlawb", "smlawt", "smulbb", "smulbt", "smultb",
                    "smultt", "smulwb", "smulwt", "stc", "sxtab", "sxtab16", "sxtah",
                    "sxtb", "sxtb16", "sxth", "uxtab", "uxtab16", "uxtah", "uxtb",
                    "uxtb16", "uxth"
            };

            rules.registers = {
                    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
                    "r11", "r12", "r13", "r14", "r15", "sp", "lr", "pc", "cpsr", "spsr"
            };

            rules.keywords = {
                    "eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi",
                    "ls", "ge", "lt", "gt", "le", "al", "nv", ".byte", ".hword", ".word",
                    ".quad", ".ascii", ".asciz", ".string", ".space", ".skip", ".align",
                    ".balign", ".global", ".globl", ".extern", ".equ", ".set", ".macro",
                    ".endm", ".if", ".ifdef", ".ifndef", ".else", ".endif", ".include",
                    ".section", ".text", ".data", ".bss", ".arm", ".thumb", ".code"
            };

            rules.operators = {
                    "+", "-", "*", "/", "%", "&", "|", "^", "~", "<<", ">>", "(", ")",
                    "[", "]", ",", ":", ";", "!", "#"
            };

            rules.lineCommentStart = {"@", ";"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"'};

            return rules;
        }

// 68000 Assembly
        inline LanguageRules Create68000AssemblyRules() {
            LanguageRules rules;
            rules.name = "68000 Assembly";
            rules.fileExtensions = {"68k", "asm", "s"};
            rules.isAssembly = true;
            rules.isCaseSensitive = false;

            rules.instructions = {
                    "abcd", "add", "adda", "addi", "addq", "addx", "and", "andi", "asl",
                    "asr", "bcc", "bcs", "beq", "bge", "bgt", "bhi", "ble", "bls", "blt",
                    "bmi", "bne", "bpl", "bra", "bset", "bsr", "btst", "bvc", "bvs",
                    "bchg", "bclr", "chk", "clr", "cmp", "cmpa", "cmpi", "cmpm", "dbcc",
                    "dbcs", "dbeq", "dbf", "dbge", "dbgt", "dbhi", "dble", "dbls", "dblt",
                    "dbmi", "dbne", "dbpl", "dbra", "dbt", "dbvc", "dbvs", "divs", "divu",
                    "eor", "eori", "exg", "ext", "illegal", "jmp", "jsr", "lea", "link",
                    "lsl", "lsr", "move", "movea", "movem", "movep", "moveq", "muls",
                    "mulu", "nbcd", "neg", "negx", "nop", "not", "or", "ori", "pea",
                    "reset", "rol", "ror", "roxl", "roxr", "rte", "rtr", "rts", "sbcd",
                    "scc", "scs", "seq", "sf", "sge", "sgt", "shi", "sle", "sls", "slt",
                    "smi", "sne", "spl", "st", "stop", "sub", "suba", "subi", "subq",
                    "subx", "svc", "svs", "swap", "tas", "trap", "trapv", "tst", "unlk"
            };

            rules.registers = {
                    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
                    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
                    "sp", "pc", "sr", "ccr", "usp", "ssp"
            };

            rules.keywords = {
                    ".b", ".w", ".l", ".byte", ".word", ".long", ".ascii", ".asciz",
                    ".string", ".space", ".align", ".even", ".odd", ".org", ".equ",
                    ".set", ".macro", ".endm", ".if", ".else", ".endif", ".include",
                    ".section", ".text", ".data", ".bss", ".global", ".globl", ".extern"
            };

            rules.operators = {
                    "+", "-", "*", "/", "%", "&", "|", "^", "~", "<<", ">>", "(", ")",
                    "[", "]", ",", ":", ";", "#", "$"
            };

            rules.lineCommentStart = {"*", ";"};
            rules.stringDelimiters = {'"', '\''};

            return rules;
        }

// Z80 Assembly
        inline LanguageRules CreateZ80AssemblyRules() {
            LanguageRules rules;
            rules.name = "Z80 Assembly";
            rules.fileExtensions = {"z80", "asm", "s"};
            rules.isAssembly = true;
            rules.isCaseSensitive = false;

            rules.instructions = {
                    "adc", "add", "and", "bit", "call", "ccf", "cp", "cpd", "cpdr", "cpi",
                    "cpir", "cpl", "daa", "dec", "di", "djnz", "ei", "ex", "exx", "halt",
                    "im", "in", "inc", "ind", "indr", "ini", "inir", "jp", "jr", "ld",
                    "ldd", "lddr", "ldi", "ldir", "neg", "nop", "or", "otdr", "otir",
                    "out", "outd", "outi", "pop", "push", "res", "ret", "reti", "retn",
                    "rl", "rla", "rlc", "rlca", "rld", "rr", "rra", "rrc", "rrca", "rrd",
                    "rst", "sbc", "scf", "set", "sla", "sll", "sra", "srl", "sub", "xor"
            };

            rules.registers = {
                    "a", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "sp", "pc",
                    "i", "r", "ix", "iy", "ixh", "ixl", "iyh", "iyl", "af'", "bc'", "de'", "hl'"
            };

            rules.keywords = {
                    "nz", "z", "nc", "c", "po", "pe", "p", "m", ".db", ".dw", ".ds", ".org",
                    ".equ", ".end", ".include", ".macro", ".endm", ".if", ".else", ".endif",
                    "defb", "defw", "defs", "org", "equ", "end", "include"
            };

            rules.operators = {
                    "+", "-", "*", "/", "%", "&", "|", "^", "~", "<<", ">>", "(", ")",
                    "[", "]", ",", ":", ";", "#", "$"
            };

            rules.lineCommentStart = {";"};
            rules.stringDelimiters = {'"', '\''};

            return rules;
        }

// ===== HTML LANGUAGE RULES =====
        inline LanguageRules CreateHtmlRules() {
            LanguageRules rules;
            rules.name = "HTML";
            rules.fileExtensions = {"html", "htm", "xhtml"};
            rules.isCaseSensitive = false;

            rules.keywords = {
                    "html", "head", "title", "meta", "link", "style", "script", "body",
                    "div", "span", "p", "h1", "h2", "h3", "h4", "h5", "h6", "a", "img",
                    "ul", "ol", "li", "table", "tr", "td", "th", "thead", "tbody", "tfoot",
                    "form", "input", "textarea", "select", "option", "button", "label",
                    "fieldset", "legend", "iframe", "canvas", "svg", "video", "audio",
                    "source", "track", "embed", "object", "param", "article", "section",
                    "nav", "aside", "header", "footer", "main", "figure", "figcaption",
                    "details", "summary", "mark", "time", "progress", "meter"
            };

            rules.builtins = {
                    "id", "class", "style", "src", "href", "alt", "title", "width", "height",
                    "type", "name", "value", "placeholder", "required", "disabled", "readonly",
                    "checked", "selected", "multiple", "size", "rows", "cols", "maxlength",
                    "pattern", "min", "max", "step", "autocomplete", "autofocus", "hidden",
                    "draggable", "contenteditable", "spellcheck", "translate", "dir", "lang",
                    "accesskey", "tabindex", "role", "aria-label", "aria-describedby",
                    "data-", "onclick", "onload", "onchange", "onsubmit", "onmouseover"
            };

            rules.operators = {"<", ">", "/", "=", "\"", "'", "&", ";"};
            rules.blockComments = {{"<!--", "-->"}};
            rules.stringDelimiters = {'"', '\''};

            return rules;
        }

// ===== CSS LANGUAGE RULES =====
        inline LanguageRules CreateCssRules() {
            LanguageRules rules;
            rules.name = "CSS";
            rules.fileExtensions = {"css"};

            rules.keywords = {
                    "color", "background", "background-color", "background-image", "background-repeat",
                    "background-position", "background-size", "background-attachment", "border",
                    "border-color", "border-style", "border-width", "border-radius", "margin",
                    "padding", "width", "height", "min-width", "max-width", "min-height",
                    "max-height", "position", "top", "bottom", "left", "right", "float",
                    "clear", "display", "visibility", "overflow", "overflow-x", "overflow-y",
                    "z-index", "opacity", "font", "font-family", "font-size", "font-weight",
                    "font-style", "font-variant", "line-height", "text-align", "text-decoration",
                    "text-transform", "text-indent", "letter-spacing", "word-spacing",
                    "white-space", "vertical-align", "list-style", "list-style-type",
                    "list-style-position", "list-style-image", "cursor", "outline",
                    "box-shadow", "text-shadow", "transform", "transition", "animation"
            };

            rules.types = {
                    "auto", "inherit", "initial", "unset", "none", "normal", "bold", "italic",
                    "underline", "overline", "line-through", "uppercase", "lowercase",
                    "capitalize", "left", "right", "center", "justify", "top", "middle",
                    "bottom", "absolute", "relative", "fixed", "static", "sticky", "block",
                    "inline", "inline-block", "flex", "grid", "table", "table-cell",
                    "table-row", "hidden", "visible", "scroll", "clip", "ellipsis"
            };

            rules.builtins = {
                    "px", "em", "rem", "vh", "vw", "vmin", "vmax", "%", "pt", "pc", "in",
                    "cm", "mm", "ex", "ch", "deg", "rad", "grad", "turn", "s", "ms", "Hz",
                    "kHz", "dpi", "dpcm", "dppx", "fr"
            };

            rules.constants = {
                    "transparent", "currentColor", "red", "green", "blue", "yellow", "orange",
                    "purple", "pink", "brown", "black", "white", "gray", "grey", "silver",
                    "maroon", "navy", "aqua", "lime", "fuchsia", "olive", "teal"
            };

            rules.operators = {
                    "{", "}", "(", ")", "[", "]", ":", ";", ",", ".", "#", "+", ">", "~",
                    "*", "=", "^", "$", "|", "~", "/", "-", "!"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"', '\''};

            return rules;
        }

// ===== SQL LANGUAGE RULES =====
        inline LanguageRules CreateSqlRules() {
            LanguageRules rules;
            rules.name = "SQL";
            rules.fileExtensions = {"sql"};
            rules.isCaseSensitive = false;

            rules.keywords = {
                    "select", "from", "where", "insert", "update", "delete", "create", "drop",
                    "alter", "table", "column", "index", "view", "database", "schema", "trigger",
                    "procedure", "function", "declare", "begin", "end", "if", "else", "while",
                    "for", "loop", "break", "continue", "return", "case", "when", "then",
                    "else", "end", "union", "intersect", "except", "join", "inner", "left",
                    "right", "full", "outer", "cross", "on", "using", "group", "by", "having",
                    "order", "asc", "desc", "limit", "offset", "top", "distinct", "all",
                    "exists", "in", "not", "and", "or", "like", "between", "is", "null",
                    "true", "false", "as", "alias", "into", "values", "set", "default",
                    "constraint", "primary", "foreign", "key", "references", "unique", "check",
                    "not", "null", "auto_increment", "identity", "sequence", "grant", "revoke",
                    "commit", "rollback", "transaction", "savepoint", "lock", "unlock"
            };

            rules.types = {
                    "int", "integer", "smallint", "bigint", "decimal", "numeric", "float",
                    "real", "double", "precision", "money", "smallmoney", "bit", "tinyint",
                    "char", "varchar", "nchar", "nvarchar", "text", "ntext", "binary",
                    "varbinary", "image", "date", "time", "datetime", "datetime2", "smalldatetime",
                    "timestamp", "year", "interval", "boolean", "bool", "json", "xml", "uuid",
                    "serial", "blob", "clob", "longtext", "mediumtext", "tinytext", "enum"
            };

            rules.builtins = {
                    "count", "sum", "avg", "min", "max", "abs", "ceil", "floor", "round",
                    "sqrt", "power", "exp", "log", "sin", "cos", "tan", "upper", "lower",
                    "length", "substr", "substring", "concat", "trim", "ltrim", "rtrim",
                    "replace", "reverse", "left", "right", "charindex", "patindex", "stuff",
                    "len", "datalength", "ascii", "char", "unicode", "nchar", "soundex",
                    "difference", "quotename", "replicate", "space", "str", "format",
                    "cast", "convert", "try_cast", "try_convert", "isnull", "nullif",
                    "coalesce", "case", "iif", "choose", "parse", "try_parse", "eomonth",
                    "dateadd", "datediff", "datepart", "datename", "getdate", "getutcdate",
                    "sysdatetime", "sysutcdatetime", "sysdatetimeoffset", "current_timestamp",
                    "db_name", "user_name", "host_name", "app_name", "suser_name", "system_user"
            };

            rules.constants = {"true", "false", "null"};

            rules.operators = {
                    "=", "<>", "!=", "<", ">", "<=", ">=", "+", "-", "*", "/", "%", "||",
                    "&&", "!", "&", "|", "^", "~", "<<", ">>", "(", ")", ",", ";", "."
            };

            rules.lineCommentStart = {"--"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"', '\''};

            return rules;
        }

// ===== PHP LANGUAGE RULES =====
        inline LanguageRules CreatePhpRules() {
            LanguageRules rules;
            rules.name = "PHP";
            rules.fileExtensions = {"php", "php3", "php4", "php5", "phtml"};

            rules.keywords = {
                    "abstract", "and", "array", "as", "break", "callable", "case", "catch",
                    "class", "clone", "const", "continue", "declare", "default", "die", "do",
                    "echo", "else", "elseif", "empty", "enddeclare", "endfor", "endforeach",
                    "endif", "endswitch", "endwhile", "eval", "exit", "extends", "final",
                    "finally", "for", "foreach", "function", "global", "goto", "if",
                    "implements", "include", "include_once", "instanceof", "insteadof",
                    "interface", "isset", "list", "namespace", "new", "or", "print",
                    "private", "protected", "public", "require", "require_once", "return",
                    "static", "switch", "throw", "trait", "try", "unset", "use", "var",
                    "while", "xor", "yield", "yield_from"
            };

            rules.types = {
                    "array", "bool", "boolean", "callable", "float", "double", "int", "integer",
                    "iterable", "mixed", "object", "resource", "string", "void", "null"
            };

            rules.builtins = {
                    "abs", "acos", "acosh", "addcslashes", "addslashes", "array_change_key_case",
                    "array_chunk", "array_column", "array_combine", "array_count_values",
                    "array_diff", "array_diff_assoc", "array_diff_key", "array_diff_uassoc",
                    "array_diff_ukey", "array_fill", "array_fill_keys", "array_filter",
                    "array_flip", "array_intersect", "array_intersect_assoc", "array_intersect_key",
                    "array_intersect_uassoc", "array_intersect_ukey", "array_key_exists",
                    "array_key_first", "array_key_last", "array_keys", "array_map", "array_merge",
                    "array_merge_recursive", "array_multisort", "array_pad", "array_pop",
                    "array_product", "array_push", "array_rand", "array_reduce", "array_replace",
                    "array_replace_recursive", "array_reverse", "array_search", "array_shift",
                    "array_slice", "array_splice", "array_sum", "array_udiff", "array_udiff_assoc",
                    "array_udiff_uassoc", "array_uintersect", "array_uintersect_assoc",
                    "array_uintersect_uassoc", "array_unique", "array_unshift", "array_values",
                    "array_walk", "array_walk_recursive", "arsort", "asin", "asinh", "asort",
                    "atan", "atan2", "atanh", "base64_decode", "base64_encode", "basename",
                    "bin2hex", "bindec", "ceil", "chr", "chunk_split", "class_exists",
                    "clearstatcache", "closedir", "compact", "copy", "cos", "cosh", "count",
                    "crc32", "crypt", "current", "date", "decbin", "dechex", "decoct",
                    "deg2rad", "dirname", "each", "end", "explode", "extract", "fclose",
                    "feof", "fflush", "fgetc", "fgetcsv", "fgets", "fgetss", "file",
                    "file_exists", "file_get_contents", "file_put_contents", "fileatime",
                    "filectime", "filegroup", "fileinode", "filemtime", "fileowner",
                    "fileperms", "filesize", "filetype", "floor", "fmod", "fopen", "fpassthru",
                    "fprintf", "fputcsv", "fputs", "fread", "fscanf", "fseek", "fstat",
                    "ftell", "ftruncate", "fwrite", "getcwd", "getdate", "getenv", "gettimeofday",
                    "gettype", "glob", "gmdate", "gmmktime", "hash", "hash_file", "header",
                    "hexdec", "htmlentities", "htmlspecialchars", "hypot", "implode", "in_array",
                    "intval", "is_array", "is_bool", "is_callable", "is_dir", "is_executable",
                    "is_file", "is_finite", "is_float", "is_infinite", "is_int", "is_link",
                    "is_nan", "is_null", "is_numeric", "is_object", "is_readable", "is_resource",
                    "is_scalar", "is_string", "is_uploaded_file", "is_writable", "is_writeable",
                    "join", "key", "key_exists", "krsort", "ksort", "lcfirst", "levenshtein",
                    "log", "log10", "log1p", "ltrim", "max", "md5", "md5_file", "metaphone",
                    "microtime", "min", "mkdir", "mktime", "move_uploaded_file", "mt_getrandmax",
                    "mt_rand", "mt_srand", "natcasesort", "natsort", "next", "nl2br", "number_format",
                    "ob_clean", "ob_end_clean", "ob_end_flush", "ob_flush", "ob_get_clean",
                    "ob_get_contents", "ob_get_flush", "ob_get_length", "ob_get_level",
                    "ob_get_status", "ob_gzhandler", "ob_implicit_flush", "ob_list_handlers",
                    "ob_start", "octdec", "opendir", "ord", "parse_str", "parse_url", "pathinfo",
                    "pow", "preg_grep", "preg_match", "preg_match_all", "preg_quote", "preg_replace",
                    "preg_replace_callback", "preg_split", "prev", "print_r", "printf", "quotemeta",
                    "rad2deg", "rand", "range", "rawurldecode", "rawurlencode", "readdir", "readfile",
                    "realpath", "rename", "reset", "rewind", "rewinddir", "rmdir", "round",
                    "rsort", "rtrim", "scandir", "serialize", "setcookie", "setrawcookie",
                    "settype", "sha1", "sha1_file", "shuffle", "similar_text", "sin", "sinh",
                    "sizeof", "sleep", "sort", "soundex", "sprintf", "sqrt", "srand", "str_getcsv",
                    "str_ireplace", "str_pad", "str_repeat", "str_replace", "str_rot13",
                    "str_shuffle", "str_split", "str_word_count", "strcasecmp", "strchr",
                    "strcmp", "strcoll", "strcspn", "strip_tags", "stripcslashes", "stripslashes",
                    "stripos", "stristr", "strlen", "strnatcasecmp", "strnatcmp", "strncasecmp",
                    "strncmp", "strpbrk", "strpos", "strrchr", "strrev", "strripos", "strrpos",
                    "strspn", "strstr", "strtok", "strtolower", "strtotime", "strtoupper",
                    "strtr", "strval", "substr", "substr_compare", "substr_count", "substr_replace",
                    "tan", "tanh", "tempnam", "time", "tmpfile", "touch", "trim", "ucfirst",
                    "ucwords", "uksort", "uniqid", "unlink", "unpack", "unserialize", "urldecode",
                    "urlencode", "usort", "utf8_decode", "utf8_encode", "var_dump", "var_export",
                    "version_compare", "vfprintf", "vprintf", "vsprintf", "wordwrap"
            };

            rules.constants = {"true", "false", "null", "TRUE", "FALSE", "NULL"};

            rules.operators = {
                    "+", "-", "*", "/", "%", "=", "+=", "-=", "*=", "/=", "%=", ".=", "==",
                    "===", "!=", "!==", "<", ">", "<=", ">=", "<=>", "&&", "||", "!", "and",
                    "or", "xor", "not", "&", "|", "^", "~", "<<", ">>", "++", "--", "?:",
                    "??", ".", "->", "::", "$", "@", "(", ")", "[", "]", "{", "}", ",", ";",
                    "=>", "...", "\\"
            };

            rules.lineCommentStart = {"//", "#"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"', '\''};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"${", "}"}, {"{$", "}"}, {"$", ""}};

            return rules;
        }

// ===== JAVASCRIPT LANGUAGE RULES =====
        inline LanguageRules CreateJavaScriptRules() {
            LanguageRules rules;
            rules.name = "JavaScript";
            rules.fileExtensions = {"js", "mjs", "jsx"};

            rules.keywords = {
                    "async", "await", "break", "case", "catch", "class", "const", "continue",
                    "debugger", "default", "delete", "do", "else", "export", "extends", "false",
                    "finally", "for", "function", "if", "import", "in", "instanceof", "let",
                    "new", "null", "return", "super", "switch", "this", "throw", "true", "try",
                    "typeof", "undefined", "var", "void", "while", "with", "yield", "of"
            };

            rules.builtins = {
                    "Array", "Boolean", "Date", "Error", "Function", "JSON", "Math", "Number",
                    "Object", "RegExp", "String", "Symbol", "Promise", "Map", "Set", "WeakMap",
                    "WeakSet", "Proxy", "Reflect", "ArrayBuffer", "DataView", "Int8Array",
                    "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array",
                    "Uint32Array", "Float32Array", "Float64Array", "BigInt", "BigInt64Array",
                    "BigUint64Array", "Intl", "WebAssembly", "console", "window", "document",
                    "global", "process", "Buffer", "require", "module", "exports", "__dirname",
                    "__filename", "setTimeout", "setInterval", "clearTimeout", "clearInterval",
                    "setImmediate", "clearImmediate", "parseInt", "parseFloat", "isNaN",
                    "isFinite", "encodeURI", "encodeURIComponent", "decodeURI", "decodeURIComponent"
            };

            rules.constants = {"true", "false", "null", "undefined", "NaN", "Infinity"};

            rules.operators = {
                    "+", "-", "*", "/", "%", "**", "=", "+=", "-=", "*=", "/=", "%=", "**=",
                    "==", "===", "!=", "!==", "<", ">", "<=", ">=", "&&", "||", "!", "&",
                    "|", "^", "~", "<<", ">>", ">>>", "&=", "|=", "^=", "<<=", ">>=", ">>>=",
                    "++", "--", "?:", "??", "?.", "=>", "...", "(", ")", "[", "]", "{", "}",
                    ",", ";", ".", ":"
            };

            rules.lineCommentStart = {"//"};
            rules.blockComments = {{"/*", "*/"}};
            rules.stringDelimiters = {'"', '\'', '`'};
            rules.hasStringInterpolation = true;
            rules.interpolationDelimiters = {{"${", "}"}};

            return rules;
        }

// ===== TYPESCRIPT LANGUAGE RULES =====
        inline LanguageRules CreateTypeScriptRules() {
            LanguageRules rules = CreateJavaScriptRules();
            rules.name = "TypeScript";
            rules.fileExtensions = {"ts", "tsx"};

            // Add TypeScript-specific keywords
            rules.keywords.insert({
                                          "abstract", "any", "as", "asserts", "bigint", "boolean", "constructor",
                                          "declare", "enum", "implements", "infer", "interface", "is", "keyof",
                                          "module", "namespace", "never", "number", "object", "private", "protected",
                                          "public", "readonly", "require", "string", "type", "unique", "unknown"
                                  });

            // Add TypeScript-specific types
            rules.types = {
                    "any", "unknown", "never", "void", "boolean", "number", "bigint", "string",
                    "symbol", "object", "undefined", "null", "Array", "ReadonlyArray", "Tuple",
                    "Record", "Partial", "Required", "Pick", "Omit", "Exclude", "Extract",
                    "NonNullable", "Parameters", "ConstructorParameters", "ReturnType",
                    "InstanceType", "ThisParameterType", "OmitThisParameter", "ThisType",
                    "Uppercase", "Lowercase", "Capitalize", "Uncapitalize"
            };

            // Add TypeScript-specific operators
            rules.operators.insert({"!", "?", "|", "&", "satisfies"});

            return rules;
        }

    } // namespace UltraCanvas
