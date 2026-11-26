// core/UltraCanvasSpreadsheetFormula.cpp
// Formula engine implementation - parser, evaluator, and function library.
// The Excel-compatible functions added in 1.1.0 live in
// UltraCanvasSpreadsheetFormulaFunctions.cpp.
// Version: 1.3.0 - arrays and spills: array constants, element-wise operators and functions, A1#,
//                  @, LET / LAMBDA, omitted arguments; spills (#SPILL!) and fixed array formulas
// Version: 1.2.0 - Recalculate follows changes to every formula that reads them, through chains and
//                  across sheets (it recalculated only formulas already marked dirty)
// Version: 1.1.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
//
// NOTE: The tokenizer, FormulaNode factories, FormulaValue/FormulaToken
// factories, SpreadsheetFormula::Parse/CollectDependencies/DependsOn, and
// several SpreadsheetFormulaEngine methods are defined INLINE in the header.
// This file implements only the remaining (non-inline) declarations.

#include "UltraCanvasSpreadsheetFormula.h"
#include <stdexcept>
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetCell.h"
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace UltraCanvas {

// ============================================================================
// FORMULA PARSER
//
// FormulaParser::Parse takes the token vector as an argument (there is no
// token-taking constructor). It stores into tokens_/pos_ and returns the AST.
// ============================================================================

std::unique_ptr<FormulaNode> FormulaParser::Parse(const std::vector<FormulaToken>& tokens) {
    tokens_ = tokens;
    pos_ = 0;
    errorMessage_.clear();

    if (tokens_.empty()) {
        Error("Empty formula");
        return nullptr;
    }

    try {
        auto result = ParseExpression();
        if (!result) return nullptr;
        if (!IsAtEnd()) {
            Error("Unexpected token at end of formula");
            return nullptr;
        }
        return result;
    } catch (const std::exception& e) {
        errorMessage_ = e.what();
        return nullptr;
    } catch (...) {
        errorMessage_ = "Parse error";
        return nullptr;
    }
}

const FormulaToken& FormulaParser::Current() const {
    return tokens_[pos_];
}

const FormulaToken& FormulaParser::Peek() const {
    if (pos_ + 1 < tokens_.size()) return tokens_[pos_ + 1];
    return tokens_.back();
}

bool FormulaParser::IsAtEnd() const {
    return Current().type == FormulaTokenType::EndOfFormula;
}

bool FormulaParser::Check(FormulaTokenType type) const {
    return Current().type == type;
}

bool FormulaParser::Match(FormulaTokenType type) {
    if (Check(type)) {
        Advance();
        return true;
    }
    return false;
}

void FormulaParser::Advance() {
    if (pos_ + 1 < tokens_.size()) ++pos_;
}

void FormulaParser::Error(const std::string& message) {
    if (errorMessage_.empty()) errorMessage_ = message;
}

std::unique_ptr<FormulaNode> FormulaParser::ParseExpression() {
    return ParseComparison();
}

std::unique_ptr<FormulaNode> FormulaParser::ParseComparison() {
    auto left = ParseConcatenation();
    if (!left) return nullptr;

    while (Check(FormulaTokenType::Operator)) {
        const std::string& op = Current().text;
        if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            Advance();
            auto right = ParseConcatenation();
            if (!right) return nullptr;
            left = FormulaNode::BinaryOperation(op, std::move(left), std::move(right));
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<FormulaNode> FormulaParser::ParseConcatenation() {
    auto left = ParseAddition();
    if (!left) return nullptr;

    while (Check(FormulaTokenType::Operator) && Current().text == "&") {
        Advance();
        auto right = ParseAddition();
        if (!right) return nullptr;
        left = FormulaNode::BinaryOperation("&", std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<FormulaNode> FormulaParser::ParseAddition() {
    auto left = ParseMultiplication();
    if (!left) return nullptr;

    while (Check(FormulaTokenType::Operator)) {
        const std::string& op = Current().text;
        if (op == "+" || op == "-") {
            Advance();
            auto right = ParseMultiplication();
            if (!right) return nullptr;
            left = FormulaNode::BinaryOperation(op, std::move(left), std::move(right));
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<FormulaNode> FormulaParser::ParseMultiplication() {
    auto left = ParsePower();
    if (!left) return nullptr;

    while (Check(FormulaTokenType::Operator)) {
        const std::string& op = Current().text;
        if (op == "*" || op == "/") {
            Advance();
            auto right = ParsePower();
            if (!right) return nullptr;
            left = FormulaNode::BinaryOperation(op, std::move(left), std::move(right));
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<FormulaNode> FormulaParser::ParsePower() {
    auto left = ParseUnary();
    if (!left) return nullptr;

    if (Check(FormulaTokenType::Operator) && Current().text == "^") {
        Advance();
        auto right = ParsePower();  // Right associative
        if (!right) return nullptr;
        left = FormulaNode::BinaryOperation("^", std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<FormulaNode> FormulaParser::ParseUnary() {
    if (Check(FormulaTokenType::Operator)) {
        const std::string& op = Current().text;
        if (op == "+" || op == "-" || op == "@") {   // @: implicit intersection
            Advance();
            auto operand = ParseUnary();
            if (!operand) return nullptr;
            return FormulaNode::UnaryOperation(op, std::move(operand));
        }
    }
    return ParsePercent();
}

std::unique_ptr<FormulaNode> FormulaParser::ParsePercent() {
    auto operand = ParsePrimary();
    if (!operand) return nullptr;

    while (Check(FormulaTokenType::Operator) && Current().text == "%") {
        Advance();
        operand = FormulaNode::UnaryOperation("%", std::move(operand));
    }
    return operand;
}

std::unique_ptr<FormulaNode> FormulaParser::ParsePrimary() {
    const FormulaToken& token = Current();

    switch (token.type) {
        case FormulaTokenType::Number:
            Advance();
            return FormulaNode::Literal(FormulaValue::Number(token.numValue));

        case FormulaTokenType::String:
            Advance();
            return FormulaNode::Literal(FormulaValue::Text(token.text));

        case FormulaTokenType::Boolean:
            Advance();
            return FormulaNode::Literal(FormulaValue::Boolean(token.boolValue));

        case FormulaTokenType::Error:
            Advance();
            return FormulaNode::Literal(FormulaValue::Error(token.errorValue));

        case FormulaTokenType::CellRef:
            Advance();
            return FormulaNode::CellReference(token.cellRef);

        case FormulaTokenType::RangeRef:
            Advance();
            return FormulaNode::RangeReference(token.rangeRef);

        case FormulaTokenType::NamedRef:
            Advance();
            return FormulaNode::NamedReference(token.text);

        case FormulaTokenType::Function: {
            std::string name = token.text;
            Advance();  // consume function name token
            // The inline tokenizer decides function vs. cell-ref based on a
            // trailing '('. Because identifiers that contain digits but are
            // ALSO valid cell references (e.g. LOG10 -> column LOG row 10) are
            // emitted as CellRef tokens, we cannot fully fix function names
            // with digits here. Where the tokenizer DOES hand us a Function
            // token (followed by '('), names with digits such as ATAN2 / LOG10
            // are handled correctly as function calls.
            auto call = ParseFunctionCall(name);
            // LAMBDA(x, x*2)(5): the LAMBDA called on the spot.
            while (call && Check(FormulaTokenType::LeftParen)) {
                Advance();
                std::vector<std::unique_ptr<FormulaNode>> args;
                args.push_back(std::move(call));
                if (!ParseArguments(args)) return nullptr;
                call = std::make_unique<FormulaNode>(FormulaNodeType::Invoke);
                call->children = std::move(args);
            }
            return call;
        }

        case FormulaTokenType::SpillRef: {
            Advance();
            auto node = FormulaNode::CellReference(token.cellRef);
            node->nodeType = FormulaNodeType::SpillRef;
            return node;
        }

        case FormulaTokenType::LeftBrace:
            return ParseArrayLiteral();

        case FormulaTokenType::LeftParen: {
            Advance();
            auto expr = ParseExpression();
            if (!expr) return nullptr;
            if (!Check(FormulaTokenType::RightParen)) {
                Error("Expected ')'");
                return nullptr;
            }
            Advance();
            return expr;
        }

        default:
            Error("Unexpected token");
            return nullptr;
    }
}

bool FormulaParser::ParseArguments(std::vector<std::unique_ptr<FormulaNode>>& args) {
    auto isSeparator = [this]() {
        return Check(FormulaTokenType::Comma) || Check(FormulaTokenType::Semicolon);
    };
    if (!Check(FormulaTokenType::RightParen)) {
        while (true) {
            // An argument left out - (3,,10), (1,) - is empty: the function
            // takes its default for it.
            if (isSeparator() || Check(FormulaTokenType::RightParen)) {
                args.push_back(FormulaNode::Literal(FormulaValue::Empty()));
            } else {
                auto arg = ParseExpression();
                if (!arg) return false;
                args.push_back(std::move(arg));
            }
            if (!isSeparator()) break;
            Advance();
        }
    }
    if (!Check(FormulaTokenType::RightParen)) {
        Error("Expected ')' to close function call");
        return false;
    }
    Advance();  // consume ')'
    return true;
}

std::unique_ptr<FormulaNode> FormulaParser::ParseFunctionCall(const std::string& name) {
    if (!Check(FormulaTokenType::LeftParen)) {
        Error("Expected '(' after function name");
        return nullptr;
    }
    Advance();  // consume '('

    std::vector<std::unique_ptr<FormulaNode>> args;
    if (!ParseArguments(args)) return nullptr;

    // Function names are matched case-insensitively by the library; the inline
    // tokenizer preserves the original case, so leave the name as-is.
    return FormulaNode::Function(name, std::move(args));
}

std::unique_ptr<FormulaNode> FormulaParser::ParseArrayLiteral() {
    Advance();  // consume '{'
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::Array);
    int rows = 0, cols = -1, inRow = 0;
    while (true) {
        // One constant: a number (with a sign), text, TRUE / FALSE, an error.
        bool negative = false;
        while (Check(FormulaTokenType::Operator) && (Current().text == "-" || Current().text == "+")) {
            if (Current().text == "-") negative = !negative;
            Advance();
        }
        const FormulaToken& t = Current();
        FormulaValue v;
        switch (t.type) {
            case FormulaTokenType::Number: v = FormulaValue::Number(negative ? -t.numValue : t.numValue); break;
            case FormulaTokenType::String: v = FormulaValue::Text(t.text); break;
            case FormulaTokenType::Boolean: v = FormulaValue::Boolean(t.boolValue); break;
            case FormulaTokenType::Error: v = FormulaValue::Error(t.errorValue); break;
            default:
                Error("An array constant holds numbers, text, TRUE / FALSE or errors only");
                return nullptr;
        }
        Advance();
        node->children.push_back(FormulaNode::Literal(v));
        ++inRow;
        if (Check(FormulaTokenType::Comma)) {           // next column
            Advance();
            continue;
        }
        if (Check(FormulaTokenType::Semicolon) || Check(FormulaTokenType::RightBrace)) {
            if (cols >= 0 && inRow != cols) {
                Error("Every row of an array constant has the same number of values");
                return nullptr;
            }
            cols = inRow;
            inRow = 0;
            ++rows;
            const bool end = Check(FormulaTokenType::RightBrace);
            Advance();
            if (end) break;
            continue;
        }
        Error("Expected ',', ';' or '}' in an array constant");
        return nullptr;
    }
    node->arrayRows = rows;
    node->arrayCols = cols;
    return node;
}

// ============================================================================
// FORMULA FUNCTION LIBRARY
// ============================================================================

FormulaFunctionLibrary::FormulaFunctionLibrary() {
    RegisterMathFunctions();
    RegisterStatisticalFunctions();
    RegisterLogicalFunctions();
    RegisterTextFunctions();
    RegisterDateTimeFunctions();
    RegisterLookupFunctions();
    RegisterInformationFunctions();
    RegisterFinancialFunctions();
    RegisterExcelFunctions();
    ApplyArgumentNames();
    ApplyArrayBehaviour();
}

std::vector<std::string> FormulaFunctionLibrary::GetSignatureArguments(const std::string& name) const {
    std::vector<std::string> parts;
    const FunctionDefinition* def = GetFunction(name);
    if (!def) return parts;
    for (const auto& arg : def->args) {
        parts.push_back(arg.optional ? "[" + arg.name + "]" : arg.name);
    }
    if (def->maxArgs < 0 || def->maxArgs > static_cast<int>(def->args.size())) parts.push_back("...");
    return parts;
}

std::string FormulaFunctionLibrary::GetSignature(const std::string& name) const {
    const FunctionDefinition* def = GetFunction(name);
    if (!def) return std::string();
    std::string text = def->name + "(";
    const auto parts = GetSignatureArguments(name);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) text += ", ";
        text += parts[i];
    }
    return text + ")";
}

bool FindFormulaCallAtCaret(const std::string& formula, size_t caret,
                            std::string& functionName, int& argumentIndex) {
    struct Frame { std::string name; int arg = 0; bool array = false; };
    std::vector<Frame> stack;
    caret = std::min(caret, formula.size());
    char quote = 0;   // inside "text" or 'sheet name'
    for (size_t i = 0; i < caret; ++i) {
        const char c = formula[i];
        if (quote) {
            if (c == quote) quote = 0;   // a doubled quote reopens at once
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            size_t start = i;
            while (start > 0 && (std::isalnum(static_cast<unsigned char>(formula[start - 1])) ||
                                 formula[start - 1] == '.' || formula[start - 1] == '_')) {
                --start;
            }
            std::string name = formula.substr(start, i - start);
            if (!name.empty() && !std::isalpha(static_cast<unsigned char>(name[0]))) name.clear();
            std::transform(name.begin(), name.end(), name.begin(), ::toupper);
            stack.push_back({ name, 0, false });
        } else if (c == '{') {
            stack.push_back({ std::string(), 0, true });
        } else if (c == ')' || c == '}') {
            if (!stack.empty()) stack.pop_back();
        } else if ((c == ',' || c == ';') && !stack.empty() && !stack.back().array) {
            ++stack.back().arg;
        }
    }
    // The innermost named call; a group in it is part of its argument.
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        if (!it->name.empty()) {
            functionName = it->name;
            argumentIndex = it->arg;
            return true;
        }
    }
    return false;
}

void FormulaFunctionLibrary::RegisterFunction(const FunctionDefinition& def) {
    std::string name = def.name;
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    functions_[name] = def;
}

const FunctionDefinition* FormulaFunctionLibrary::GetFunction(const std::string& name) const {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto it = functions_.find(upper);
    return it != functions_.end() ? &it->second : nullptr;
}

bool FormulaFunctionLibrary::HasFunction(const std::string& name) const {
    return GetFunction(name) != nullptr;
}

std::vector<std::string> FormulaFunctionLibrary::GetFunctionNames() const {
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& [name, def] : functions_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> FormulaFunctionLibrary::GetFunctionsByCategory(const std::string& category) const {
    std::vector<std::string> names;
    for (const auto& [name, def] : functions_) {
        if (def.category == category) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void FormulaFunctionLibrary::RegisterMathFunctions() {
    RegisterFunction({"SUM", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            double sum = 0;
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                if (arg.IsNumber()) sum += arg.GetNumber();
            }
            for (const auto& range : ranges) {
                for (const auto& val : range) {
                    if (val.IsError()) return val;
                    if (val.IsNumber()) sum += val.GetNumber();
                }
            }
            return FormulaValue::Number(sum);
        }, "Sum of numbers", "Math"});

    RegisterFunction({"ABS", {{"number", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            if (args[0].IsError()) return args[0];
            return FormulaValue::Number(std::abs(args[0].ToNumber()));
        }, "Absolute value", "Math"});

    RegisterFunction({"SQRT", {{"number", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            if (args[0].IsError()) return args[0];
            double val = args[0].ToNumber();
            if (val < 0) return FormulaValue::Error(CellErrorType::NumError);
            return FormulaValue::Number(std::sqrt(val));
        }, "Square root", "Math"});

    RegisterFunction({"POWER", {{"number", FunctionArgType::Value}, {"power", FunctionArgType::Value}}, 2, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::pow(args[0].ToNumber(), args[1].ToNumber()));
        }, "Power", "Math"});

    RegisterFunction({"ROUND", {{"number", FunctionArgType::Value}, {"digits", FunctionArgType::Value}}, 2, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
            double val = args[0].ToNumber();
            int digits = static_cast<int>(args[1].ToNumber());
            double mult = std::pow(10.0, digits);
            return FormulaValue::Number(std::round(val * mult) / mult);
        }, "Round", "Math"});

    RegisterFunction({"MOD", {{"number", FunctionArgType::Value}, {"divisor", FunctionArgType::Value}}, 2, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
            double divisor = args[1].ToNumber();
            if (divisor == 0) return FormulaValue::Error(CellErrorType::DivisionByZero);
            return FormulaValue::Number(std::fmod(args[0].ToNumber(), divisor));
        }, "Modulo", "Math"});

    RegisterFunction({"INT", {{"number", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::floor(args[0].ToNumber()));
        }, "Integer part", "Math"});

    RegisterFunction({"PI", {}, 0, 0, false,
        [](const std::vector<FormulaValue>&, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Number(3.14159265358979323846);
        }, "Pi constant", "Math"});

    RegisterFunction({"RAND", {}, 0, 0, true,
        [](const std::vector<FormulaValue>&, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_real_distribution<> dis(0.0, 1.0);
            return FormulaValue::Number(dis(gen));
        }, "Random number 0-1", "Math"});

    RegisterFunction({"SIN", {{"angle", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::sin(args[0].ToNumber()));
        }, "Sine", "Math"});

    RegisterFunction({"COS", {{"angle", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::cos(args[0].ToNumber()));
        }, "Cosine", "Math"});

    RegisterFunction({"TAN", {{"angle", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::tan(args[0].ToNumber()));
        }, "Tangent", "Math"});

    RegisterFunction({"ATAN2", {{"x", FunctionArgType::Value}, {"y", FunctionArgType::Value}}, 2, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            // Function name contains a digit (ATAN2). It is handled correctly
            // whenever the tokenizer emits it as a Function token (i.e. when
            // immediately followed by '(').
            if (args.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::atan2(args[0].ToNumber(), args[1].ToNumber()));
        }, "Arctangent of x,y", "Math"});

    RegisterFunction({"LN", {{"number", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            double val = args[0].ToNumber();
            if (val <= 0) return FormulaValue::Error(CellErrorType::NumError);
            return FormulaValue::Number(std::log(val));
        }, "Natural log", "Math"});

    RegisterFunction({"LOG", {{"number", FunctionArgType::Value}, {"base", FunctionArgType::Value, true}}, 1, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            double val = args[0].ToNumber();
            double base = args.size() > 1 ? args[1].ToNumber() : 10.0;
            if (val <= 0 || base <= 0 || base == 1) return FormulaValue::Error(CellErrorType::NumError);
            return FormulaValue::Number(std::log(val) / std::log(base));
        }, "Logarithm", "Math"});

    RegisterFunction({"LOG10", {{"number", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            // Function name contains digits (LOG10). Handled when tokenizer
            // emits a Function token. Note: if written without parentheses the
            // inline tokenizer would treat "LOG10" as cell ref column LOG row
            // 10; in normal usage LOG10(x) is always followed by '('.
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            double val = args[0].ToNumber();
            if (val <= 0) return FormulaValue::Error(CellErrorType::NumError);
            return FormulaValue::Number(std::log10(val));
        }, "Base-10 logarithm", "Math"});

    RegisterFunction({"EXP", {{"number", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(std::exp(args[0].ToNumber()));
        }, "e^x", "Math"});
}

void FormulaFunctionLibrary::RegisterStatisticalFunctions() {
    RegisterFunction({"AVERAGE", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            double sum = 0;
            int count = 0;
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                if (arg.IsNumber()) { sum += arg.GetNumber(); ++count; }
            }
            for (const auto& range : ranges) {
                for (const auto& val : range) {
                    if (val.IsError()) return val;
                    if (val.IsNumber()) { sum += val.GetNumber(); ++count; }
                }
            }
            if (count == 0) return FormulaValue::Error(CellErrorType::DivisionByZero);
            return FormulaValue::Number(sum / count);
        }, "Average", "Statistical"});

    RegisterFunction({"COUNT", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            int count = 0;
            for (const auto& arg : args) if (arg.IsNumber()) ++count;
            for (const auto& range : ranges) {
                for (const auto& val : range) if (val.IsNumber()) ++count;
            }
            return FormulaValue::Number(count);
        }, "Count numbers", "Statistical"});

    RegisterFunction({"COUNTA", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            int count = 0;
            for (const auto& arg : args) if (!arg.IsEmpty()) ++count;
            for (const auto& range : ranges) {
                for (const auto& val : range) if (!val.IsEmpty()) ++count;
            }
            return FormulaValue::Number(count);
        }, "Count non-empty", "Statistical"});

    RegisterFunction({"MIN", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            double minVal = std::numeric_limits<double>::max();
            bool found = false;
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                if (arg.IsNumber()) { minVal = std::min(minVal, arg.GetNumber()); found = true; }
            }
            for (const auto& range : ranges) {
                for (const auto& val : range) {
                    if (val.IsError()) return val;
                    if (val.IsNumber()) { minVal = std::min(minVal, val.GetNumber()); found = true; }
                }
            }
            return found ? FormulaValue::Number(minVal) : FormulaValue::Number(0);
        }, "Minimum", "Statistical"});

    RegisterFunction({"MAX", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            double maxVal = std::numeric_limits<double>::lowest();
            bool found = false;
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                if (arg.IsNumber()) { maxVal = std::max(maxVal, arg.GetNumber()); found = true; }
            }
            for (const auto& range : ranges) {
                for (const auto& val : range) {
                    if (val.IsError()) return val;
                    if (val.IsNumber()) { maxVal = std::max(maxVal, val.GetNumber()); found = true; }
                }
            }
            return found ? FormulaValue::Number(maxVal) : FormulaValue::Number(0);
        }, "Maximum", "Statistical"});

    RegisterFunction({"MEDIAN", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            std::vector<double> values;
            for (const auto& arg : args) if (arg.IsNumber()) values.push_back(arg.GetNumber());
            for (const auto& range : ranges) {
                for (const auto& val : range) if (val.IsNumber()) values.push_back(val.GetNumber());
            }
            if (values.empty()) return FormulaValue::Error(CellErrorType::NumError);
            std::sort(values.begin(), values.end());
            size_t n = values.size();
            if (n % 2 == 0) return FormulaValue::Number((values[n/2-1] + values[n/2]) / 2.0);
            return FormulaValue::Number(values[n/2]);
        }, "Median", "Statistical"});
}

void FormulaFunctionLibrary::RegisterLogicalFunctions() {
    RegisterFunction({"IF", {{"condition", FunctionArgType::Value}, {"true_val", FunctionArgType::Value},
                            {"false_val", FunctionArgType::Value, true}}, 2, 3, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            if (args[0].IsError()) return args[0];
            bool condition = args[0].GetBoolean();
            if (condition) return args.size() > 1 ? args[1] : FormulaValue::Boolean(true);
            return args.size() > 2 ? args[2] : FormulaValue::Boolean(false);
        }, "Conditional", "Logical"});

    RegisterFunction({"AND", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                if (!arg.GetBoolean()) return FormulaValue::Boolean(false);
            }
            for (const auto& range : ranges) {
                for (const auto& val : range) {
                    if (val.IsError()) return val;
                    if (!val.GetBoolean()) return FormulaValue::Boolean(false);
                }
            }
            return FormulaValue::Boolean(true);
        }, "Logical AND", "Logical"});

    RegisterFunction({"OR", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                if (arg.GetBoolean()) return FormulaValue::Boolean(true);
            }
            for (const auto& range : ranges) {
                for (const auto& val : range) {
                    if (val.IsError()) return val;
                    if (val.GetBoolean()) return FormulaValue::Boolean(true);
                }
            }
            return FormulaValue::Boolean(false);
        }, "Logical OR", "Logical"});

    RegisterFunction({"NOT", {{"value", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            if (args[0].IsError()) return args[0];
            return FormulaValue::Boolean(!args[0].GetBoolean());
        }, "Logical NOT", "Logical"});

    RegisterFunction({"IFERROR", {{"value", FunctionArgType::Value}, {"error_value", FunctionArgType::Value}}, 2, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
            return args[0].IsError() ? args[1] : args[0];
        }, "Handle errors", "Logical"});

    RegisterFunction({"TRUE", {}, 0, 0, false,
        [](const std::vector<FormulaValue>&, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Boolean(true);
        }, "True", "Logical"});

    RegisterFunction({"FALSE", {}, 0, 0, false,
        [](const std::vector<FormulaValue>&, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Boolean(false);
        }, "False", "Logical"});
}

void FormulaFunctionLibrary::RegisterTextFunctions() {
    RegisterFunction({"CONCATENATE", {}, 1, -1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            std::string result;
            for (const auto& arg : args) {
                if (arg.IsError()) return arg;
                result += arg.GetText();
            }
            return FormulaValue::Text(result);
        }, "Concatenate text", "Text"});

    RegisterFunction({"UPPER", {{"text", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            std::string text = args[0].GetText();
            std::transform(text.begin(), text.end(), text.begin(), ::toupper);
            return FormulaValue::Text(text);
        }, "Uppercase", "Text"});

    RegisterFunction({"LOWER", {{"text", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            std::string text = args[0].GetText();
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            return FormulaValue::Text(text);
        }, "Lowercase", "Text"});

    RegisterFunction({"TRIM", {{"text", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            std::string text = args[0].GetText();
            std::string result;
            bool inSpace = true;
            for (char c : text) {
                if (c == ' ') {
                    if (!inSpace) { result += c; inSpace = true; }
                } else {
                    result += c; inSpace = false;
                }
            }
            if (!result.empty() && result.back() == ' ') result.pop_back();
            return FormulaValue::Text(result);
        }, "Trim spaces", "Text"});

    RegisterFunction({"TEXT", {{"value", FunctionArgType::Value}, {"format", FunctionArgType::Value}}, 2, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
            double val = args[0].ToNumber();
            std::string fmt = args[1].GetText();
            std::ostringstream ss;
            if (fmt.find('%') != std::string::npos) {
                ss << std::fixed << std::setprecision(0) << (val * 100) << "%";
            } else {
                ss << val;
            }
            return FormulaValue::Text(ss.str());
        }, "Format as text", "Text"});
}

void FormulaFunctionLibrary::RegisterDateTimeFunctions() {
    // Date/time epoch: ODS serial day system (day 0 = Dec 30, 1899), matching
    // DateTimeValue in Types.h. We use the DateTimeValue helpers (FromDate /
    // ToDate) throughout rather than mktime to avoid timezone drift and keep a
    // single consistent epoch across DATE/TODAY/NOW/YEAR/MONTH/DAY.
    RegisterFunction({"TODAY", {}, 0, 0, true,
        [](const std::vector<FormulaValue>&, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            std::time_t t = std::time(nullptr);
            std::tm utc{};
#if defined(_WIN32)
            gmtime_s(&utc, &t);
#else
            gmtime_r(&t, &utc);
#endif
            DateTimeValue d = DateTimeValue::FromDate(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
            return FormulaValue::Number(std::floor(d.serialNumber));
        }, "Current date", "DateTime"});

    RegisterFunction({"NOW", {}, 0, 0, true,
        [](const std::vector<FormulaValue>&, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            std::time_t t = std::time(nullptr);
            std::tm utc{};
#if defined(_WIN32)
            gmtime_s(&utc, &t);
#else
            gmtime_r(&t, &utc);
#endif
            DateTimeValue d = DateTimeValue::FromDateTime(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                                                          utc.tm_hour, utc.tm_min, utc.tm_sec);
            return FormulaValue::Number(d.serialNumber);
        }, "Current date/time", "DateTime"});

    RegisterFunction({"DATE", {{"year", FunctionArgType::Value}, {"month", FunctionArgType::Value}, {"day", FunctionArgType::Value}}, 3, 3, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 3) return FormulaValue::Error(CellErrorType::ValueError);
            int year = static_cast<int>(args[0].ToNumber());
            int month = static_cast<int>(args[1].ToNumber());
            int day = static_cast<int>(args[2].ToNumber());
            DateTimeValue d = DateTimeValue::FromDate(year, month, day);
            return FormulaValue::Number(d.serialNumber);
        }, "Create date", "DateTime"});

    RegisterFunction({"YEAR", {{"date", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            DateTimeValue d(args[0].ToNumber());
            int year, month, day;
            d.ToDate(year, month, day);
            return FormulaValue::Number(year);
        }, "Year from date", "DateTime"});

    RegisterFunction({"MONTH", {{"date", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            DateTimeValue d(args[0].ToNumber());
            int year, month, day;
            d.ToDate(year, month, day);
            return FormulaValue::Number(month);
        }, "Month from date", "DateTime"});

    RegisterFunction({"DAY", {{"date", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            DateTimeValue d(args[0].ToNumber());
            int year, month, day;
            d.ToDate(year, month, day);
            return FormulaValue::Number(day);
        }, "Day from date", "DateTime"});
}

void FormulaFunctionLibrary::RegisterLookupFunctions() {
    // INDEX, MATCH, VLOOKUP and the other lookups need a range's rows and
    // columns: RegisterExcelFunctions (UltraCanvasSpreadsheetFormulaFunctions.cpp).
}

void FormulaFunctionLibrary::RegisterInformationFunctions() {
    RegisterFunction({"ISBLANK", {{"value", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Boolean(args.empty() || args[0].IsEmpty());
        }, "Check if blank", "Information"});

    RegisterFunction({"ISERROR", {{"value", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Boolean(!args.empty() && args[0].IsError());
        }, "Check if error", "Information"});

    RegisterFunction({"ISNUMBER", {{"value", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Boolean(!args.empty() && args[0].IsNumber());
        }, "Check if number", "Information"});

    RegisterFunction({"ISTEXT", {{"value", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            return FormulaValue::Boolean(!args.empty() && args[0].IsText());
        }, "Check if text", "Information"});
}

void FormulaFunctionLibrary::RegisterFinancialFunctions() {
    RegisterFunction({"PMT", {{"rate", FunctionArgType::Value}, {"nper", FunctionArgType::Value}, {"pv", FunctionArgType::Value},
                             {"fv", FunctionArgType::Value, true}, {"type", FunctionArgType::Value, true}}, 3, 5, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 3) return FormulaValue::Error(CellErrorType::ValueError);
            double rate = args[0].ToNumber();
            double nper = args[1].ToNumber();
            double pv = args[2].ToNumber();
            double fv = args.size() > 3 ? args[3].ToNumber() : 0;
            int type = args.size() > 4 ? static_cast<int>(args[4].ToNumber()) : 0;
            if (nper == 0) return FormulaValue::Error(CellErrorType::DivisionByZero);
            if (rate == 0) return FormulaValue::Number(-(pv + fv) / nper);
            double pmt = rate * (pv * std::pow(1 + rate, nper) + fv) /
                         ((1 + rate * type) * (std::pow(1 + rate, nper) - 1));
            return FormulaValue::Number(-pmt);
        }, "Loan payment", "Financial"});
}

// ============================================================================
// FORMULA EVALUATOR
// ============================================================================

FormulaEvaluator::FormulaEvaluator(FormulaFunctionLibrary& library)
    : functionLibrary_(library)
{
}

namespace {

std::string UpperName(const std::string& name) {
    std::string upper = name;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return upper;
}

// Restores a value when the scope ends - also when an implementation throws.
template <typename T>
struct ScopedValue {
    T& slot;
    T saved;
    ScopedValue(T& s, T value) : slot(s), saved(std::move(s)) { slot = std::move(value); }
    ~ScopedValue() { slot = std::move(saved); }
    ScopedValue(const ScopedValue&) = delete;
    ScopedValue& operator=(const ScopedValue&) = delete;
};

} // namespace

FormulaValue FormulaEvaluator::Evaluate(const SpreadsheetFormula& formula) {
    if (!formula.IsValid()) return FormulaValue::Error(CellErrorType::NameError);
    FormulaValue v = Evaluate(formula.GetAST());
    return v.TopLeft();
}

FormulaValue FormulaEvaluator::EvaluateArrayFormula(const SpreadsheetFormula& formula, const CellAddress& at,
                                                    bool implicitIntersection) {
    if (!formula.IsValid()) return FormulaValue::Error(CellErrorType::NameError);
    // A cell calculated while another is (a reference to a dirty formula)
    // has its own context; the other's is restored afterwards.
    ScopedValue<CellAddress> cellScope(formulaCell_, at);
    ScopedValue<bool> intersectScope(implicitIntersection_, implicitIntersection);
    ScopedValue<bool> contextScope(arrayContext_, false);
    ScopedValue<std::vector<std::pair<std::string, FormulaValue>>> namesScope(localNames_, {});
    FormulaValue v = Evaluate(formula.GetAST());
    if (v.IsLambda()) return FormulaValue::Error(CellErrorType::Calc);   // a LAMBDA never called
    if (implicitIntersection && v.IsArray()) return v.TopLeft();
    return v;
}

FormulaValue FormulaEvaluator::Evaluate(const FormulaNode* node) {
    if (!node) return FormulaValue::Empty();

    switch (node->nodeType) {
        case FormulaNodeType::Literal:
            return EvaluateLiteral(node);
        case FormulaNodeType::CellRef:
            return EvaluateCellRef(node);
        case FormulaNodeType::RangeRef:
            return EvaluateRangeRef(node);
        case FormulaNodeType::BinaryOp:
            return EvaluateBinaryOp(node);
        case FormulaNodeType::UnaryOp:
            return EvaluateUnaryOp(node);
        case FormulaNodeType::FunctionCall:
            return EvaluateFunction(node);
        case FormulaNodeType::Array:
            return EvaluateArrayLiteral(node);
        case FormulaNodeType::Invoke:
            return EvaluateInvoke(node);
        case FormulaNodeType::SpillRef: {
            const CellRange range = ResolveSpillRange(node->cellRef);
            if (!range.IsValid()) return FormulaValue::Error(CellErrorType::ReferenceError);
            return RangeValue(range);
        }
        case FormulaNodeType::NamedRef: {
            if (const FormulaValue* local = FindLocalName(UpperName(node->namedRef))) return *local;
            CellRange range = GetNamedRange(node->namedRef);
            if (!range.IsValid()) return FormulaValue::Error(NamedRangeError(node->namedRef));
            return RangeValue(range);
        }
        default:
            return FormulaValue::Error(CellErrorType::ValueError);
    }
}

const FormulaValue* FormulaEvaluator::FindLocalName(const std::string& name) const {
    for (auto it = localNames_.rbegin(); it != localNames_.rend(); ++it) {
        if (it->first == name) return &it->second;
    }
    return nullptr;
}

FormulaValue FormulaEvaluator::RangeValue(const CellRange& range) {
    auto rows = GetRangeValues(range);
    if (rows.empty() || rows.front().empty()) return FormulaValue::Empty();
    if (rows.size() == 1 && rows.front().size() == 1) return rows.front().front();
    if (implicitIntersection_ && !arrayContext_) {
        // Where one value is expected: the cell on the formula's own row (a
        // column of cells) or column (a row of cells); none is #VALUE!.
        const int r = formulaCell_.row, c = formulaCell_.col;
        if (range.ColCount() == 1 && r >= range.start.row && r <= range.end.row) {
            return rows[static_cast<size_t>(r - range.start.row)].front();
        }
        if (range.RowCount() == 1 && c >= range.start.col && c <= range.end.col) {
            return rows.front()[static_cast<size_t>(c - range.start.col)];
        }
        return FormulaValue::Error(CellErrorType::ValueError);
    }
    const int rowCount = static_cast<int>(rows.size());
    const int colCount = static_cast<int>(rows.front().size());
    std::vector<FormulaValue> cells;
    cells.reserve(static_cast<size_t>(rowCount) * static_cast<size_t>(colCount));
    for (auto& row : rows) {
        for (auto& v : row) cells.push_back(std::move(v));
    }
    return FormulaValue::Array(rowCount, colCount, std::move(cells));
}

FormulaValue FormulaEvaluator::EvaluateArrayLiteral(const FormulaNode* node) {
    std::vector<FormulaValue> cells;
    cells.reserve(node->children.size());
    for (const auto& child : node->children) cells.push_back(Evaluate(child.get()));
    return FormulaValue::Array(node->arrayRows, node->arrayCols, std::move(cells));
}

FormulaValue FormulaEvaluator::ElementWise(
    const std::vector<FormulaValue>& operands,
    const std::function<FormulaValue(const std::vector<FormulaValue>&)>& fn)
{
    int rows = 1, cols = 1;
    for (const FormulaValue& v : operands) {
        rows = std::max(rows, v.ArrayRows());
        cols = std::max(cols, v.ArrayCols());
    }
    std::vector<FormulaValue> cells;
    cells.reserve(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    std::vector<FormulaValue> at(operands.size());
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            bool outside = false;
            for (size_t i = 0; i < operands.size(); ++i) {
                const FormulaValue& v = operands[i];
                if (!v.IsArray()) { at[i] = v; continue; }
                // A single row or column stretches over the other's size.
                const int rr = v.ArrayRows() == 1 ? 0 : r;
                const int cc = v.ArrayCols() == 1 ? 0 : c;
                if (rr >= v.ArrayRows() || cc >= v.ArrayCols()) { outside = true; break; }
                at[i] = v.ArrayAt(rr, cc);
            }
            cells.push_back(outside ? FormulaValue::Error(CellErrorType::NAError) : fn(at));
        }
    }
    return FormulaValue::Array(rows, cols, std::move(cells));
}

FormulaValue FormulaEvaluator::EvaluateLiteral(const FormulaNode* node) {
    return node->literalValue;
}

FormulaValue FormulaEvaluator::EvaluateCellRef(const FormulaNode* node) {
    return GetCellValue(node->cellRef);
}

FormulaValue FormulaEvaluator::EvaluateRangeRef(const FormulaNode* node) {
    return RangeValue(node->rangeRef);
}

FormulaValue FormulaEvaluator::EvaluateBinaryOp(const FormulaNode* node) {
    if (node->children.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
    FormulaValue left = Evaluate(node->children[0].get());
    FormulaValue right = Evaluate(node->children[1].get());
    if (left.IsLambda() || right.IsLambda()) return FormulaValue::Error(CellErrorType::ValueError);

    // An array on either side: the operation element by element.
    if (left.IsArray() || right.IsArray()) {
        const std::string op = node->op;
        return ElementWise({left, right}, [this, &op](const std::vector<FormulaValue>& v) {
            return ApplyBinary(op, v[0], v[1]);
        });
    }
    return ApplyBinary(node->op, left, right);
}

FormulaValue FormulaEvaluator::ApplyBinary(const std::string& op, const FormulaValue& left,
                                           const FormulaValue& right) {
    if (left.IsError()) return left;
    if (right.IsError()) return right;

    if (op == "+") return Add(left, right);
    if (op == "-") return Subtract(left, right);
    if (op == "*") return Multiply(left, right);
    if (op == "/") return Divide(left, right);
    if (op == "^") return Power(left, right);
    if (op == "&") return Concatenate(left, right);
    if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        return Compare(left, right, op);
    }
    return FormulaValue::Error(CellErrorType::ValueError);
}

FormulaValue FormulaEvaluator::EvaluateUnaryOp(const FormulaNode* node) {
    if (node->children.empty()) return FormulaValue::Error(CellErrorType::ValueError);
    const std::string& op = node->op;
    if (op == "@") {
        // Implicit intersection: a range gives the cell on the formula's row
        // or column, any other array its top-left value.
        ScopedValue<bool> intersect(implicitIntersection_, true);
        ScopedValue<bool> context(arrayContext_, false);
        return Evaluate(node->children[0].get()).TopLeft();
    }
    FormulaValue operand = Evaluate(node->children[0].get());
    if (operand.IsLambda()) return FormulaValue::Error(CellErrorType::ValueError);
    auto apply = [&op](const FormulaValue& v) -> FormulaValue {
        if (v.IsError()) return v;
        if (op == "-") return FormulaValue::Number(-v.ToNumber());
        if (op == "+") return FormulaValue::Number(v.ToNumber());
        if (op == "%") return FormulaValue::Number(v.ToNumber() / 100.0);
        return FormulaValue::Error(CellErrorType::ValueError);
    };
    if (operand.IsArray()) {
        return ElementWise({operand}, [&apply](const std::vector<FormulaValue>& v) { return apply(v[0]); });
    }
    return apply(operand);
}

FormulaValue FormulaEvaluator::EvaluateLet(const FormulaNode* node) {
    // LET(name1, value1, [name2, value2, ...], calculation)
    const size_t n = node->children.size();
    if (n < 3 || n % 2 == 0) return FormulaValue::Error(CellErrorType::ValueError);
    ScopedValue<std::vector<std::pair<std::string, FormulaValue>>> scope(localNames_, localNames_);
    for (size_t i = 0; i + 1 < n; i += 2) {
        const FormulaNode* name = node->children[i].get();
        if (name->nodeType != FormulaNodeType::NamedRef) return FormulaValue::Error(CellErrorType::NameError);
        FormulaValue value = Evaluate(node->children[i + 1].get());
        localNames_.emplace_back(UpperName(name->namedRef), std::move(value));
    }
    return Evaluate(node->children.back().get());
}

FormulaValue FormulaEvaluator::EvaluateLambda(const FormulaNode* node) {
    // LAMBDA([parameter1, ...], calculation): a value until it is called.
    if (node->children.empty()) return FormulaValue::Error(CellErrorType::ValueError);
    auto lambda = std::make_shared<FormulaLambda>();
    for (size_t i = 0; i + 1 < node->children.size(); ++i) {
        const FormulaNode* name = node->children[i].get();
        if (name->nodeType != FormulaNodeType::NamedRef) return FormulaValue::Error(CellErrorType::ValueError);
        lambda->parameters.push_back(UpperName(name->namedRef));
    }
    lambda->body = node->children.back().get();
    lambda->captured = localNames_;
    FormulaValue v;
    v.lambda = std::move(lambda);
    return v;
}

FormulaValue FormulaEvaluator::CallLambda(const FormulaValue& lambda, const std::vector<FormulaValue>& arguments) {
    if (!lambda.IsLambda()) return FormulaValue::Error(CellErrorType::ValueError);
    const FormulaLambda& l = *lambda.lambda;
    if (arguments.size() != l.parameters.size()) return FormulaValue::Error(CellErrorType::ValueError);
    if (currentDepth_ >= maxRecursionDepth_) return FormulaValue::Error(CellErrorType::NumError);
    ScopedValue<int> depth(currentDepth_, currentDepth_ + 1);
    ScopedValue<std::vector<std::pair<std::string, FormulaValue>>> scope(localNames_, l.captured);
    for (size_t i = 0; i < arguments.size(); ++i) localNames_.emplace_back(l.parameters[i], arguments[i]);
    return Evaluate(l.body);
}

FormulaValue FormulaEvaluator::EvaluateInvoke(const FormulaNode* node) {
    if (node->children.empty()) return FormulaValue::Error(CellErrorType::ValueError);
    const FormulaValue callee = Evaluate(node->children[0].get());
    if (!callee.IsLambda()) return callee.IsError() ? callee : FormulaValue::Error(CellErrorType::ValueError);
    std::vector<FormulaValue> arguments;
    for (size_t i = 1; i < node->children.size(); ++i) arguments.push_back(Evaluate(node->children[i].get()));
    return CallLambda(callee, arguments);
}

FormulaValue FormulaEvaluator::EvaluateFunction(const FormulaNode* node) {
    const std::string upper = UpperName(node->functionName);
    if (upper == "LET") return EvaluateLet(node);
    if (upper == "LAMBDA") return EvaluateLambda(node);

    const FunctionDefinition* func = functionLibrary_.GetFunction(node->functionName);
    if (!func) {
        // A LET name (or LAMBDA parameter) holding a LAMBDA: f(2).
        if (const FormulaValue* local = FindLocalName(upper); local && local->IsLambda()) {
            const FormulaValue callee = *local;
            std::vector<FormulaValue> arguments;
            for (const auto& child : node->children) arguments.push_back(Evaluate(child.get()));
            return CallLambda(callee, arguments);
        }
        return FormulaValue::Error(CellErrorType::NameError);
    }

    // A function of single values called with arrays: once per element.
    if (func->elementWise) {
        std::vector<FormulaValue> operands;
        bool anyArray = false;
        {
            // Where one value is expected a range intersects (a formula from
            // a file without dynamic arrays); otherwise it is an array.
            ScopedValue<bool> context(arrayContext_, false);
            for (const auto& child : node->children) {
                operands.push_back(Evaluate(child.get()));
                if (operands.back().IsLambda()) operands.back() = FormulaValue::Error(CellErrorType::ValueError);
                anyArray = anyArray || operands.back().IsArray();
            }
        }
        auto call = [this, func](const std::vector<FormulaValue>& values) -> FormulaValue {
            std::vector<FunctionCallArgument> order;
            for (size_t i = 0; i < values.size(); ++i) order.push_back({false, i, CellRange()});
            ScopedValue<std::vector<FunctionCallArgument>> publish(callArguments_, std::move(order));
            return func->implementation(values, {}, this);
        };
        if (!anyArray) return call(operands);
        return ElementWise(operands, call);
    }

    std::vector<FormulaValue> args;
    std::vector<std::vector<FormulaValue>> rangeArgs;
    std::vector<FunctionCallArgument> order;

    // An argument of a function that takes ranges keeps a range a range,
    // also in a formula that otherwise intersects.
    ScopedValue<bool> context(arrayContext_, true);
    auto asRange = [&](FunctionCallArgument& a, const CellRange& range) {
        a.isRange = true;
        a.index = rangeArgs.size();
        a.range = range;
        rangeArgs.push_back(FlattenRange(range));
    };
    for (const auto& child : node->children) {
        FunctionCallArgument a;
        const bool local = child->nodeType == FormulaNodeType::NamedRef &&
                           FindLocalName(UpperName(child->namedRef)) != nullptr;
        if (child->nodeType == FormulaNodeType::RangeRef) {
            asRange(a, child->rangeRef);
        } else if (child->nodeType == FormulaNodeType::SpillRef &&
                   ResolveSpillRange(child->cellRef).IsValid()) {
            asRange(a, ResolveSpillRange(child->cellRef));
        } else if (!local && child->nodeType == FormulaNodeType::NamedRef &&
                   GetNamedRange(child->namedRef).IsValid()) {
            asRange(a, GetNamedRange(child->namedRef));
        } else if (!local && child->nodeType == FormulaNodeType::NamedRef) {
            a.index = args.size();
            args.push_back(FormulaValue::Error(NamedRangeError(child->namedRef)));
        } else {
            FormulaValue v = Evaluate(child.get());
            if (v.IsArray()) {
                // An array is handed on as a range of its shape, so the
                // functions that take ranges (SUM, INDEX, SUMPRODUCT ...)
                // read every value and its rows and columns.
                a.isRange = true;
                a.index = rangeArgs.size();
                a.range = CellRange(0, 0, v.ArrayRows() - 1, v.ArrayCols() - 1);
                rangeArgs.push_back(v.array->cells);
            } else {
                a.index = args.size();
                args.push_back(std::move(v));
            }
        }
        order.push_back(std::move(a));
    }

    // The arguments are evaluated (nested calls included) before this call's
    // order is published, and the outer call's is restored afterwards - also
    // when the implementation throws.
    struct Publish {
        std::vector<FunctionCallArgument>& slot;
        std::vector<FunctionCallArgument>& saved;
        Publish(std::vector<FunctionCallArgument>& s, std::vector<FunctionCallArgument>& o)
            : slot(s), saved(o) { std::swap(slot, saved); }
        ~Publish() { std::swap(slot, saved); }
    } publish(callArguments_, order);
    return func->implementation(args, rangeArgs, this);
}

FormulaValue FormulaEvaluator::Add(const FormulaValue& left, const FormulaValue& right) {
    return FormulaValue::Number(left.ToNumber() + right.ToNumber());
}

FormulaValue FormulaEvaluator::Subtract(const FormulaValue& left, const FormulaValue& right) {
    return FormulaValue::Number(left.ToNumber() - right.ToNumber());
}

FormulaValue FormulaEvaluator::Multiply(const FormulaValue& left, const FormulaValue& right) {
    return FormulaValue::Number(left.ToNumber() * right.ToNumber());
}

FormulaValue FormulaEvaluator::Divide(const FormulaValue& left, const FormulaValue& right) {
    double div = right.ToNumber();
    if (div == 0) return FormulaValue::Error(CellErrorType::DivisionByZero);
    return FormulaValue::Number(left.ToNumber() / div);
}

FormulaValue FormulaEvaluator::Power(const FormulaValue& left, const FormulaValue& right) {
    return FormulaValue::Number(std::pow(left.ToNumber(), right.ToNumber()));
}

FormulaValue FormulaEvaluator::Concatenate(const FormulaValue& left, const FormulaValue& right) {
    return FormulaValue::Text(left.GetText() + right.GetText());
}

FormulaValue FormulaEvaluator::Compare(const FormulaValue& left, const FormulaValue& right, const std::string& op) {
    int cmp;
    if (left.IsNumber() && right.IsNumber()) {
        double l = left.GetNumber(), r = right.GetNumber();
        cmp = (l < r) ? -1 : (l > r) ? 1 : 0;
    } else if ((left.IsNumber() || left.IsBoolean()) && (right.IsNumber() || right.IsBoolean())) {
        double l = left.ToNumber(), r = right.ToNumber();
        cmp = (l < r) ? -1 : (l > r) ? 1 : 0;
    } else {
        std::string l = left.GetText(), r = right.GetText();
        cmp = l.compare(r);
        if (cmp < 0) cmp = -1; else if (cmp > 0) cmp = 1;
    }

    if (op == "=") return FormulaValue::Boolean(cmp == 0);
    if (op == "<>") return FormulaValue::Boolean(cmp != 0);
    if (op == "<") return FormulaValue::Boolean(cmp < 0);
    if (op == ">") return FormulaValue::Boolean(cmp > 0);
    if (op == "<=") return FormulaValue::Boolean(cmp <= 0);
    if (op == ">=") return FormulaValue::Boolean(cmp >= 0);
    return FormulaValue::Error(CellErrorType::ValueError);
}

FormulaValue FormulaEvaluator::GetCellValue(const CellAddress& addr) const {
    // Resolve the sheet: an explicit sheet name on the address takes priority,
    // otherwise use the current sheet context.
    // A sheet that does not exist is #REF!, as in Excel - not the current
    // sheet's cell of the same address.
    SpreadsheetSheet* sheet = currentSheet_;
    if (!addr.sheetName.empty() && spreadsheet_) {
        SpreadsheetSheet* named = spreadsheet_->GetSheetByName(addr.sheetName);
        if (!named) return FormulaValue::Error(CellErrorType::ReferenceError);
        sheet = named;
    }
    if (!sheet) return FormulaValue::Empty();

    SpreadsheetCell* cell = sheet->GetCellIfExists(addr.row, addr.col);
    // A formula cell not calculated yet in this pass is calculated now, so
    // a reference to a later cell (a total above its rows, an earlier sheet
    // reading a later one) does not read a stale or empty result.
    if (cell && cell->GetValueType() == CellValueType::Formula && cell->IsFormulaDirty() &&
        resolveFormulaCell_) {
        resolveFormulaCell_(cell, sheet);
    }
    // A value spilled from a formula not calculated yet: that formula first.
    if (cell && cell->IsSpilledValue() && resolveFormulaCell_) {
        const CellAddress at = cell->GetSpillAnchor();
        SpreadsheetCell* anchor = sheet->GetCellIfExists(at.row, at.col);
        if (anchor && anchor->HasFormula() && anchor->IsFormulaDirty()) resolveFormulaCell_(anchor, sheet);
    }
    if (!cell || cell->IsEmpty()) return FormulaValue::Empty();
    // A formula cell is worth its result, with the result's own type: a
    // number stays a number - it was handed on as its display text, so =B5
    // of a formula cell held "1008.75" and SUM over formula cells left them
    // out.
    if (cell->GetValueType() == CellValueType::Formula) {
        const CellValueVariant& result = cell->GetRawValue();
        if (auto* n = std::get_if<double>(&result)) return FormulaValue::Number(*n);
        if (auto* b = std::get_if<bool>(&result)) return FormulaValue::Boolean(*b);
        if (auto* e = std::get_if<CellErrorType>(&result)) return FormulaValue::Error(*e);
        if (auto* c = std::get_if<CurrencyValue>(&result)) return FormulaValue::Number(c->amount);
        if (auto* t = std::get_if<std::string>(&result)) return FormulaValue::Text(*t);
        return FormulaValue::Empty();
    }
    if (cell->HasError()) return FormulaValue::Error(cell->GetError());
    if (cell->IsNumeric()) return FormulaValue::Number(cell->GetNumber());
    if (cell->GetValueType() == CellValueType::Boolean) return FormulaValue::Boolean(cell->GetBoolean());
    return FormulaValue::Text(cell->GetText());
}

std::vector<std::vector<FormulaValue>> FormulaEvaluator::GetRangeValues(const CellRange& range) const {
    std::vector<std::vector<FormulaValue>> result;

    const SpreadsheetSheet* sheet = currentSheet_;
    if (!range.start.sheetName.empty() && spreadsheet_) {
        const SpreadsheetSheet* named = spreadsheet_->GetSheetByName(range.start.sheetName);
        if (!named) return {{FormulaValue::Error(CellErrorType::ReferenceError)}};
        sheet = named;
    }
    if (!sheet) return result;

    for (int row = range.start.row; row <= range.end.row; ++row) {
        std::vector<FormulaValue> rowValues;
        for (int col = range.start.col; col <= range.end.col; ++col) {
            CellAddress addr(row, col);
            addr.sheetName = range.start.sheetName;
            rowValues.push_back(GetCellValue(addr));
        }
        result.push_back(std::move(rowValues));
    }
    return result;
}

std::vector<FormulaValue> FormulaEvaluator::FlattenRange(const CellRange& range) const {
    std::vector<FormulaValue> result;
    auto rows = GetRangeValues(range);
    for (auto& row : rows) {
        for (auto& v : row) {
            result.push_back(std::move(v));
        }
    }
    return result;
}

CellRange FormulaEvaluator::ResolveSpillRange(const CellAddress& anchor) const {
    const CellRange none(CellAddress(-1, -1));
    SpreadsheetSheet* sheet = currentSheet_;
    if (!anchor.sheetName.empty() && spreadsheet_) sheet = spreadsheet_->GetSheetByName(anchor.sheetName);
    if (!sheet) return none;
    SpreadsheetCell* cell = sheet->GetCellIfExists(anchor.row, anchor.col);
    if (!cell || !cell->HasFormula()) return none;
    if (cell->IsFormulaDirty() && resolveFormulaCell_) resolveFormulaCell_(cell, sheet);
    // A dynamic array formula (even one giving a single value) or a fixed
    // array formula; a blocked spill or an ordinary formula has none.
    const bool spills = cell->GetFormulaArrayMode() == FormulaArrayMode::Spilling &&
                        cell->HasArrayResult() && !cell->IsSpillBlocked();
    if (!spills && cell->GetFormulaArrayMode() != FormulaArrayMode::FixedRange) return none;
    CellRange range(anchor.row, anchor.col, anchor.row + cell->GetArrayRows() - 1,
                    anchor.col + cell->GetArrayCols() - 1);
    range.start.sheetName = range.end.sheetName = anchor.sheetName;
    return range;
}

CellErrorType FormulaEvaluator::NamedRangeError(const std::string& name) const {
    // A name the formula sees whose sheet was deleted: #REF!; no such name: #NAME?.
    if (spreadsheet_ &&
        spreadsheet_->IsNamedRangeVisible(name, currentSheet_ ? currentSheet_->GetName() : std::string())) {
        return CellErrorType::ReferenceError;
    }
    return CellErrorType::NameError;
}

CellRange FormulaEvaluator::GetNamedRange(const std::string& name) const {
    if (currentSheet_) {
        if (const NamedRange* nr = currentSheet_->GetNamedRange(name)) {
            return nr->range;
        }
    }
    // The workbook's names: the one scoped to the formula's sheet, else the
    // workbook-wide one - never one scoped to another sheet.
    if (spreadsheet_) {
        return spreadsheet_->ResolveNamedRange(name, currentSheet_ ? currentSheet_->GetName() : std::string());
    }
    return CellRange(CellAddress(-1, -1));   // no such name: #NAME?
}

// ============================================================================
// SPREADSHEET FORMULA ENGINE - non-inline methods
// ============================================================================

FormulaValue SpreadsheetFormulaEngine::EvaluateCell(SpreadsheetCell* cell, SpreadsheetSheet* sheet) {
    if (!cell || !cell->HasFormula()) return FormulaValue::Empty();

    // A cell reached again while it is being calculated is circular; and a
    // chain of references deeper than this is calculated by the outer pass
    // rather than by an ever deeper stack.
    constexpr size_t kMaxNestedCells = 200;
    if (evaluating_.count(cell)) return FormulaValue::Empty();
    if (evaluating_.size() >= kMaxNestedCells) {
        nestingLimitReached_ = true;
        return FormulaValue::Empty();
    }
    evaluating_.insert(cell);
    struct Leave {
        std::unordered_set<const SpreadsheetCell*>& set;
        const SpreadsheetCell* cell;
        ~Leave() { set.erase(cell); }
    } leave{evaluating_, cell};

    evaluator_->SetSpreadsheet(spreadsheet_);
    evaluator_->SetCurrentSheet(sheet);

    // Parse the formula lazily if it hasn't been parsed yet.
    if (!cell->GetFormula()) {
        auto formula = ParseFormula(cell->GetFormulaText(), cell->GetAddress());
        cell->SetParsedFormula(formula);
    }

    FormulaValue result;
    auto formula = cell->GetFormula();
    if (formula && formula->IsValid()) {
        const bool intersect = cell->GetFormulaArrayMode() == FormulaArrayMode::SingleValue;
        result = evaluator_->EvaluateArrayFormula(*formula, cell->GetAddress(), intersect);
    } else {
        result = FormulaValue::Error(CellErrorType::NameError);
    }

    StoreResult(cell, sheet, result);
    cell->MarkFormulaClean();
    return result.TopLeft();
}

namespace {

// A value as a cell holds it. An empty element of an array shows 0, as in
// Excel; an empty single result stays empty.
void ValueForCell(const FormulaValue& v, bool emptyIsZero, CellValueVariant& out, CellValueType& type) {
    switch (v.type) {
        case CellValueType::Number:
            out = v.GetNumber(); type = CellValueType::Number; return;
        case CellValueType::Text:
            out = v.GetText(); type = CellValueType::Text; return;
        case CellValueType::Boolean:
            out = v.GetBoolean(); type = CellValueType::Boolean; return;
        case CellValueType::Error:
            out = v.GetError(); type = CellValueType::Error; return;
        default:
            if (v.IsArray() || v.IsLambda()) {
                out = CellErrorType::Calc; type = CellValueType::Error; return;
            }
            if (emptyIsZero) { out = 0.0; type = CellValueType::Number; return; }
            out = std::monostate{}; type = CellValueType::Empty; return;
    }
}

// Element (r, c) of `v` in a fixed array formula's range: an array smaller
// than the range repeats a single row or column, and is #N/A beyond that.
FormulaValue FitElement(const FormulaValue& v, int r, int c) {
    if (!v.IsArray()) return v;
    const int rr = v.ArrayRows() == 1 ? 0 : r;
    const int cc = v.ArrayCols() == 1 ? 0 : c;
    if (rr >= v.ArrayRows() || cc >= v.ArrayCols()) return FormulaValue::Error(CellErrorType::NAError);
    return v.ArrayAt(rr, cc);
}

bool OwnSpill(const SpreadsheetCell* c, int anchorRow, int anchorCol) {
    if (!c || !c->IsSpilledValue()) return false;
    const CellAddress a = c->GetSpillAnchor();
    return a.row == anchorRow && a.col == anchorCol;
}

} // namespace

void SpreadsheetFormulaEngine::ClearSpill(SpreadsheetSheet* sheet, SpreadsheetCell* anchor, const CellRange& keep) {
    if (!sheet || !anchor) return;
    const int row = anchor->GetRow(), col = anchor->GetColumn();
    const CellRange old = anchor->GetArrayRange();
    for (int r = old.start.row; r <= old.end.row; ++r) {
        for (int c = old.start.col; c <= old.end.col; ++c) {
            if ((r == row && c == col) || keep.Contains(r, c)) continue;
            SpreadsheetCell* child = sheet->GetCellIfExists(r, c);
            if (OwnSpill(child, row, col)) child->ClearContents();
        }
    }
}

void SpreadsheetFormulaEngine::StoreResult(SpreadsheetCell* cell, SpreadsheetSheet* sheet, const FormulaValue& result) {
    const int row = cell->GetRow(), col = cell->GetColumn();
    const FormulaArrayMode mode = cell->GetFormulaArrayMode();
    const CellRange before = cell->CoversArray() ? cell->GetArrayRange() : CellRange(row, col, row, col);
    const bool blockedBefore = cell->IsSpillBlocked();
    FormulaValue top = result.TopLeft();
    CellRange covered(row, col, row, col);

    if (mode == FormulaArrayMode::FixedRange && sheet) {
        // A legacy array formula fills its own range, whatever is there.
        const int rows = cell->GetArrayRows(), cols = cell->GetArrayCols();
        covered = CellRange(row, col, row + rows - 1, col + cols - 1);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (r == 0 && c == 0) continue;
                if (row + r >= SpreadsheetLimits::MaxRows || col + c >= SpreadsheetLimits::MaxColumns) continue;
                CellValueVariant v;
                CellValueType type;
                ValueForCell(FitElement(result, r, c), true, v, type);
                sheet->GetCell(row + r, col + c)->SetSpilledValue(v, type, r, c);
            }
        }
        top = FitElement(result, 0, 0);
        cell->SetArrayExtent(rows, cols, false, result.IsArray());
    } else if (mode == FormulaArrayMode::Spilling && result.IsArray() && sheet) {
        const int rows = result.ArrayRows(), cols = result.ArrayCols();
        // Blocked by any cell in the way that holds something other than this
        // formula's own spill, by a merge, or by the sheet's edge.
        bool blocked = row + rows > SpreadsheetLimits::MaxRows || col + cols > SpreadsheetLimits::MaxColumns;
        for (int r = 0; r < rows && !blocked; ++r) {
            for (int c = 0; c < cols && !blocked; ++c) {
                if (r == 0 && c == 0) continue;
                const SpreadsheetCell* other = sheet->GetCellIfExists(row + r, col + c);
                if (other && !other->IsEmpty() && !OwnSpill(other, row, col)) blocked = true;
                if (sheet->GetMergedCell(row + r, col + c)) blocked = true;
            }
        }
        if (blocked) {
            ClearSpill(sheet, cell, CellRange(row, col, row, col));
            cell->SetArrayExtent(rows, cols, true, true);
            top = FormulaValue::Error(CellErrorType::Spill);
        } else {
            covered = CellRange(row, col, row + rows - 1, col + cols - 1);
            ClearSpill(sheet, cell, covered);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    if (r == 0 && c == 0) continue;
                    CellValueVariant v;
                    CellValueType type;
                    ValueForCell(result.ArrayAt(r, c), true, v, type);
                    sheet->GetCell(row + r, col + c)->SetSpilledValue(v, type, r, c);
                }
            }
            cell->SetArrayExtent(rows, cols, false, true);
        }
    } else {
        // One value: whatever it spilled before goes.
        ClearSpill(sheet, cell, CellRange(row, col, row, col));
        cell->SetArrayExtent(1, 1, false, result.IsArray());
    }

    const CellRange after = cell->CoversArray() ? cell->GetArrayRange() : CellRange(row, col, row, col);
    if (!(after.start == before.start && after.end == before.end) || blockedBefore != cell->IsSpillBlocked()) {
        ++spillLayoutChanges_;
    }

    CellValueVariant v;
    CellValueType type;
    ValueForCell(top, result.IsArray(), v, type);
    cell->SetFormulaResult(v, type);
}

std::vector<std::pair<const SpreadsheetSheet*, CellRange>> SpreadsheetFormulaEngine::ClearOrphanedSpills() {
    std::vector<std::pair<const SpreadsheetSheet*, CellRange>> cleared;
    if (!spreadsheet_) return cleared;
    for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
        SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
        if (!sheet) continue;
        std::vector<SpreadsheetCell*> orphans;
        sheet->ForEachCell([&](int row, int col, SpreadsheetCell& cell) {
            if (!cell.IsSpilledValue()) return;
            const CellAddress at = cell.GetSpillAnchor();
            const SpreadsheetCell* anchor = sheet->GetCellIfExists(at.row, at.col);
            const bool claimed = anchor && anchor->CoversArray() && anchor->GetArrayRange().Contains(row, col);
            if (!claimed) orphans.push_back(&cell);
        });
        for (SpreadsheetCell* cell : orphans) {
            cell->ClearContents();
            cleared.emplace_back(sheet, CellRange(cell->GetRow(), cell->GetColumn(), cell->GetRow(), cell->GetColumn()));
        }
    }
    if (!cleared.empty()) ++spillLayoutChanges_;
    return cleared;
}

namespace {

// A cell or a block of cells on one sheet.
struct SheetArea {
    const SpreadsheetSheet* sheet = nullptr;
    CellRange range;
    long long CellCount() const {
        return static_cast<long long>(range.RowCount()) * static_cast<long long>(range.ColCount());
    }
};

struct SheetCellKey {
    const SpreadsheetSheet* sheet;
    int row;
    int col;
    bool operator==(const SheetCellKey& o) const { return sheet == o.sheet && row == o.row && col == o.col; }
};

struct SheetCellKeyHash {
    size_t operator()(const SheetCellKey& k) const {
        size_t h = std::hash<const void*>()(k.sheet);
        h ^= std::hash<long long>()((static_cast<long long>(k.row) << 20) ^ k.col) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace

void SpreadsheetFormulaEngine::Recalculate() {
    if (!spreadsheet_ || !autoCalculate_) return;
    if (recalculateAllPending_) {
        RecalculateAll();
        return;
    }

    // Where each formula is, and what it reads. A reference without a sheet
    // name reads the formula's own sheet; one naming a sheet that does not
    // exist reads nothing that can change.
    // A formula's area is the cells it fills: itself, or its array's range
    // (also a blocked spill's, which a cleared cell may free).
    struct Watched {
        const SpreadsheetSheet* sheet;
        CellAddress at;
        bool dirty;
        CellRange area;
    };
    std::vector<Watched> formulas;
    std::unordered_map<SheetCellKey, int, SheetCellKeyHash> formulaAt;
    std::unordered_map<SheetCellKey, std::vector<int>, SheetCellKeyHash> readersOfCell;
    std::vector<std::pair<int, SheetArea>> rangeReaders;
    std::vector<int> nameReaders;
    std::vector<std::pair<int, SheetArea>> arrayAreas;   // formulas filling more than their cell

    // Spilled values whose formula went or shrank are emptied first, and
    // count as changed for whatever reads them.
    for (const auto& [sheet, range] : ClearOrphanedSpills()) changedRanges_.emplace_back(sheet->GetName(), range);

    for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
        SpreadsheetSheet* sheet = spreadsheet_->GetSheet(s);
        if (!sheet) continue;
        auto sheetOf = [this, sheet](const std::string& name) -> const SpreadsheetSheet* {
            return name.empty() ? sheet : spreadsheet_->GetSheetByName(name);
        };
        sheet->ForEachCell([&](int row, int col, SpreadsheetCell& cell) {
            if (!cell.HasFormula()) return;
            if (!cell.GetFormula()) cell.SetParsedFormula(ParseFormula(cell.GetFormulaText(), CellAddress(row, col)));
            const int index = static_cast<int>(formulas.size());
            bool dirty = cell.IsFormulaDirty();
            if (auto formula = cell.GetFormula(); formula && formula->IsValid()) {
                for (const auto& dep : formula->GetDependencies()) {
                    if (const auto* from = sheetOf(dep.sheetName)) {
                        readersOfCell[SheetCellKey{from, dep.row, dep.col}].push_back(index);
                    }
                }
                for (const auto& range : formula->GetRangeDependencies()) {
                    if (const auto* from = sheetOf(range.start.sheetName)) {
                        rangeReaders.push_back({index, SheetArea{from, CellRange(range.start.row, range.start.col,
                                                                                  range.end.row, range.end.col)}});
                    }
                }
                if (formula->ReadsNamedRanges()) nameReaders.push_back(index);
                for (const auto& name : formula->GetFunctionNames()) {
                    const FunctionDefinition* def = functionLibrary_.GetFunction(name);
                    if (def && def->isVolatile) dirty = true;   // NOW, TODAY, RAND ...
                }
            }
            CellRange area(row, col, row, col);
            if (cell.CoversArray() || cell.IsSpillBlocked()) {
                area = CellRange(row, col, row + cell.GetArrayRows() - 1, col + cell.GetArrayCols() - 1);
                arrayAreas.push_back({index, SheetArea{sheet, area}});
            }
            formulas.push_back({sheet, CellAddress(row, col), dirty, area});
            formulaAt[SheetCellKey{sheet, row, col}] = index;
        });
    }

    // From the changed cells to everything that reads them: a formula that
    // becomes dirty is itself a change - of every cell it fills - for the
    // formulas that read it.
    std::vector<SheetArea> pending;
    for (const auto& [name, range] : changedRanges_) {
        if (const auto* sheet = spreadsheet_->GetSheetByName(name)) pending.push_back({sheet, range});
    }
    changedRanges_.clear();
    for (const auto& f : formulas) {
        if (f.dirty) pending.push_back({f.sheet, f.area});
    }
    const bool anyChange = !pending.empty();

    auto mark = [&](int index) {
        Watched& f = formulas[static_cast<size_t>(index)];
        if (f.dirty) return;
        f.dirty = true;
        pending.push_back({f.sheet, f.area});
    };
    if (anyChange) {
        for (int index : nameReaders) mark(index);   // a name may cover any cell
    }
    while (!pending.empty()) {
        const SheetArea area = pending.back();
        pending.pop_back();
        const long long cells = area.CellCount();

        // Formulas standing in the area (pasted, filled, restored by undo),
        // and formulas reading one of its cells: through the cell indexes
        // when the area is small, by walking the indexes when it is large.
        if (cells <= static_cast<long long>(formulaAt.size() + readersOfCell.size())) {
            for (int row = area.range.start.row; row <= area.range.end.row; ++row) {
                for (int col = area.range.start.col; col <= area.range.end.col; ++col) {
                    const SheetCellKey key{area.sheet, row, col};
                    if (auto it = formulaAt.find(key); it != formulaAt.end()) mark(it->second);
                    if (auto it = readersOfCell.find(key); it != readersOfCell.end()) {
                        for (int index : it->second) mark(index);
                    }
                }
            }
        } else {
            for (const auto& [key, index] : formulaAt) {
                if (key.sheet == area.sheet && area.range.Contains(key.row, key.col)) mark(index);
            }
            for (const auto& [key, readers] : readersOfCell) {
                if (key.sheet == area.sheet && area.range.Contains(key.row, key.col)) {
                    for (int index : readers) mark(index);
                }
            }
        }
        for (const auto& [index, read] : rangeReaders) {
            if (read.sheet == area.sheet && read.range.Intersects(area.range)) mark(index);
        }
        // An array whose cells were written into (its spill is blocked now)
        // or emptied (it may spill again).
        for (const auto& [index, filled] : arrayAreas) {
            if (filled.sheet == area.sheet && filled.range.Intersects(area.range)) mark(index);
        }
    }

    auto markCells = [&formulas]() {
        for (const auto& f : formulas) {
            if (!f.dirty) continue;
            auto* sheet = const_cast<SpreadsheetSheet*>(f.sheet);
            if (SpreadsheetCell* cell = sheet->GetCellIfExists(f.at.row, f.at.col)) cell->MarkFormulaDirty();
        }
    };

    // Calculate them. A reference to a dirty formula calculates it first
    // (FormulaEvaluator's resolver), so the order of the walk does not
    // matter. A chain deeper than one nested calculation follows leaves the
    // cells above the limit calculated from a value not yet current, so
    // another pass marks the same set again and reaches further, as
    // RecalculateAll does. The cells are listed before any is calculated:
    // a spill creates cells, which the sheet's cell map must not see while
    // it is walked.
    const int layoutBefore = spillLayoutChanges_;
    constexpr int kMaxPasses = 16;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        markCells();
        nestingLimitReached_ = false;
        for (const auto& f : formulas) {
            if (!f.dirty) continue;
            auto* sheet = const_cast<SpreadsheetSheet*>(f.sheet);
            SpreadsheetCell* cell = sheet->GetCellIfExists(f.at.row, f.at.col);
            if (cell && cell->HasFormula() && cell->IsFormulaDirty()) EvaluateCell(cell, sheet);
        }
        if (!nestingLimitReached_) break;
    }

    dirtyCells_.clear();

    // A spill that grew, moved or was blocked or freed changed cells nobody
    // was marked for: what reads them is calculated once more.
    if (spillLayoutChanges_ != layoutBefore && recalculateDepth_ < 4) {
        for (const auto& f : formulas) {
            if (!f.dirty) continue;
            auto* sheet = const_cast<SpreadsheetSheet*>(f.sheet);
            const SpreadsheetCell* cell = sheet->GetCellIfExists(f.at.row, f.at.col);
            if (!cell) continue;
            const CellRange now = (cell->CoversArray() || cell->IsSpillBlocked()) ? cell->GetArrayRange()
                                                                                   : CellRange(f.at.row, f.at.col, f.at.row, f.at.col);
            if (now.start == f.area.start && now.end == f.area.end) continue;
            const CellRange both(std::min(now.start.row, f.area.start.row), std::min(now.start.col, f.area.start.col),
                                 std::max(now.end.row, f.area.end.row), std::max(now.end.col, f.area.end.col));
            changedRanges_.emplace_back(f.sheet->GetName(), both);
        }
        if (!changedRanges_.empty()) {
            ++recalculateDepth_;
            Recalculate();
            --recalculateDepth_;
        }
    }
}

void SpreadsheetFormulaEngine::RecalculateAll() {
    if (!spreadsheet_) return;

    // Every formula is marked first, so a reference to one further on finds it
    // dirty and calculates it on the spot (FormulaEvaluator::GetCellValue);
    // the cells it reached are clean by the time the walk gets to them. A
    // chain deeper than one nested calculation follows (EvaluateCell) gets
    // another pass, each reaching further, rather than an ever deeper stack.
    //
    // Spills: values whose formula no longer covers them are emptied first;
    // a value spilled from a dirty formula calculates that formula before it
    // is read. A formula that read a cell a spill reached only later in the
    // pass read it empty, so the pass is repeated while spills still move
    // (a few times at most). The cells are listed before any is calculated:
    // a spill creates cells, which the cell map must not see while walked.
    ClearOrphanedSpills();
    constexpr int kMaxPasses = 16;
    constexpr int kMaxSpillPasses = 4;
    int spillPasses = 0;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        nestingLimitReached_ = false;
        const int layoutBefore = spillLayoutChanges_;
        std::vector<std::pair<SpreadsheetCell*, SpreadsheetSheet*>> cells;
        for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
            auto* sheet = spreadsheet_->GetSheet(s);
            if (!sheet) continue;
            sheet->ForEachCell([&cells, sheet](int /*row*/, int /*col*/, SpreadsheetCell& cell) {
                if (!cell.HasFormula()) return;
                cell.MarkFormulaDirty();
                cells.emplace_back(&cell, sheet);
            });
        }
        for (const auto& [cell, sheet] : cells) {
            if (cell->HasFormula() && cell->IsFormulaDirty()) EvaluateCell(cell, sheet);
        }
        const bool spillsMoved = spillLayoutChanges_ != layoutBefore && ++spillPasses < kMaxSpillPasses;
        if (spillsMoved) ClearOrphanedSpills();
        if (!nestingLimitReached_ && !spillsMoved) break;
    }

    dirtyCells_.clear();
    changedRanges_.clear();
    recalculateAllPending_ = false;
}

void SpreadsheetFormulaEngine::UpdateDependencies(SpreadsheetCell* cell, const std::string& sheetName) {
    if (!cell) return;

    CellAddress addr = cell->GetAddress();
    std::string cellKey = MakeCellKey(addr, sheetName);

    // Remove this cell from any dependents lists it currently participates in.
    for (auto& [key, deps] : dependents_) {
        deps.erase(cellKey);
    }

    if (!cell->HasFormula()) return;

    // Ensure the formula is parsed so we can read its dependencies.
    if (!cell->GetFormula()) {
        auto formula = ParseFormula(cell->GetFormulaText(), addr);
        cell->SetParsedFormula(formula);
    }

    auto formula = cell->GetFormula();
    if (!formula || !formula->IsValid()) return;

    // Register this cell as a dependent of each cell/range it references.
    for (const auto& dep : formula->GetDependencies()) {
        std::string depName = dep.sheetName.empty() ? sheetName : dep.sheetName;
        std::string depKey = MakeCellKey(dep, depName);
        dependents_[depKey].insert(cellKey);
    }
    for (const auto& range : formula->GetRangeDependencies()) {
        std::string depName = range.start.sheetName.empty() ? sheetName : range.start.sheetName;
        for (int row = range.start.row; row <= range.end.row; ++row) {
            for (int col = range.start.col; col <= range.end.col; ++col) {
                CellAddress da(row, col);
                std::string depKey = MakeCellKey(da, depName);
                dependents_[depKey].insert(cellKey);
            }
        }
    }
}

bool SpreadsheetFormulaEngine::HasCircularReference(const CellAddress& cell, const std::string& sheetName) const {
    std::string startKey = MakeCellKey(cell, sheetName);

    // Walk the dependents graph; if we can reach the starting cell from itself,
    // there is a cycle.
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack;
    stack.push_back(startKey);

    bool first = true;
    while (!stack.empty()) {
        std::string current = stack.back();
        stack.pop_back();

        if (!first && current == startKey) return true;
        first = false;

        if (visited.count(current)) continue;
        visited.insert(current);

        auto it = dependents_.find(current);
        if (it != dependents_.end()) {
            for (const auto& dep : it->second) {
                if (dep == startKey) return true;
                stack.push_back(dep);
            }
        }
    }
    return false;
}

std::vector<std::string> SpreadsheetFormulaEngine::GetCalculationOrder() const {
    // Topological sort of the dependency graph (Kahn's algorithm).
    // dependents_[A] = set of cells that depend on A, i.e. edges A -> dependent.
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adj;

    for (const auto& [source, deps] : dependents_) {
        if (inDegree.find(source) == inDegree.end()) inDegree[source] = 0;
        for (const auto& dep : deps) {
            adj[source].push_back(dep);
            inDegree[dep]++;
        }
    }

    std::vector<std::string> queue;
    for (const auto& [key, deg] : inDegree) {
        if (deg == 0) queue.push_back(key);
    }
    std::sort(queue.begin(), queue.end());

    std::vector<std::string> order;
    size_t head = 0;
    while (head < queue.size()) {
        std::string current = queue[head++];
        order.push_back(current);

        auto it = adj.find(current);
        if (it != adj.end()) {
            for (const auto& next : it->second) {
                if (--inDegree[next] == 0) {
                    queue.push_back(next);
                }
            }
        }
    }

    // If a cycle exists, remaining nodes are appended in sorted order so the
    // caller still receives every cell.
    if (order.size() < inDegree.size()) {
        for (const auto& [key, deg] : inDegree) {
            if (std::find(order.begin(), order.end(), key) == order.end()) {
                order.push_back(key);
            }
        }
    }

    return order;
}

} // namespace UltraCanvas
