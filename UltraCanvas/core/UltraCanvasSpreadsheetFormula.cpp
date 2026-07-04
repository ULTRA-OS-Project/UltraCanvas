// core/UltraCanvasSpreadsheetFormula.cpp
// Formula engine implementation - parser, evaluator, and function library.
// Version: 1.0.0
// Last Modified: 2026-05-30
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
#include <cmath>
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
        if (op == "+" || op == "-") {
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
            return ParseFunctionCall(name);
        }

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

std::unique_ptr<FormulaNode> FormulaParser::ParseFunctionCall(const std::string& name) {
    if (!Check(FormulaTokenType::LeftParen)) {
        Error("Expected '(' after function name");
        return nullptr;
    }
    Advance();  // consume '('

    std::vector<std::unique_ptr<FormulaNode>> args;

    if (!Check(FormulaTokenType::RightParen)) {
        auto first = ParseExpression();
        if (!first) return nullptr;
        args.push_back(std::move(first));

        while (Check(FormulaTokenType::Comma) || Check(FormulaTokenType::Semicolon)) {
            Advance();
            auto arg = ParseExpression();
            if (!arg) return nullptr;
            args.push_back(std::move(arg));
        }
    }

    if (!Check(FormulaTokenType::RightParen)) {
        Error("Expected ')' to close function call");
        return nullptr;
    }
    Advance();  // consume ')'

    // Function names are matched case-insensitively by the library; the inline
    // tokenizer preserves the original case, so leave the name as-is.
    return FormulaNode::Function(name, std::move(args));
}

// ============================================================================
// SPREADSHEET FORMULA - non-inline methods
// ============================================================================

void SpreadsheetFormula::AdjustReferences(int startRow, int startCol, int rowDelta, int colDelta) {
    // Re-tokenize/re-parse against the adjusted text so the AST and
    // dependency lists stay consistent.
    originalText_ = GetAdjustedText(rowDelta, colDelta);
    // startRow/startCol describe the insertion/deletion point; the simple
    // model here shifts all relative references uniformly (see GetAdjustedText).
    (void)startRow;
    (void)startCol;
    Parse();
}

std::string SpreadsheetFormula::GetAdjustedText(int rowOffset, int colOffset) const {
    // Rebuild the dependency references shifted by the given offsets. Only
    // relative components move; absolute ($) components stay fixed.
    if (!ast_) return originalText_;

    // Helper to shift a single address according to its reference types.
    auto shiftAddr = [rowOffset, colOffset](CellAddress addr) -> CellAddress {
        bool rowAbs = (addr.rowRef == ReferenceType::Absolute ||
                       addr.rowRef == ReferenceType::MixedRowAbs);
        bool colAbs = (addr.colRef == ReferenceType::Absolute ||
                       addr.colRef == ReferenceType::MixedColAbs);
        if (!rowAbs) addr.row += rowOffset;
        if (!colAbs) addr.col += colOffset;
        return addr;
    };

    // Walk the AST and emit a textual representation with adjusted refs.
    std::function<std::string(const FormulaNode*)> emit =
        [&](const FormulaNode* node) -> std::string {
        if (!node) return "";
        switch (node->nodeType) {
            case FormulaNodeType::Literal: {
                const FormulaValue& v = node->literalValue;
                if (v.IsText()) return "\"" + v.GetText() + "\"";
                if (v.IsBoolean()) return v.GetBoolean() ? "TRUE" : "FALSE";
                if (v.IsError()) return CellErrorToString(v.GetError());
                std::ostringstream ss;
                ss << v.GetNumber();
                return ss.str();
            }
            case FormulaNodeType::CellRef:
                return shiftAddr(node->cellRef).ToString();
            case FormulaNodeType::RangeRef: {
                CellRange r(shiftAddr(node->rangeRef.start), shiftAddr(node->rangeRef.end));
                return r.ToString();
            }
            case FormulaNodeType::NamedRef:
                return node->namedRef;
            case FormulaNodeType::BinaryOp:
                if (node->children.size() == 2) {
                    return "(" + emit(node->children[0].get()) + node->op +
                           emit(node->children[1].get()) + ")";
                }
                return "";
            case FormulaNodeType::UnaryOp:
                if (node->children.size() == 1) {
                    if (node->op == "%") return "(" + emit(node->children[0].get()) + "%)";
                    return "(" + node->op + emit(node->children[0].get()) + ")";
                }
                return "";
            case FormulaNodeType::FunctionCall: {
                std::string out = node->functionName + "(";
                for (size_t i = 0; i < node->children.size(); ++i) {
                    if (i) out += ",";
                    out += emit(node->children[i].get());
                }
                out += ")";
                return out;
            }
            default:
                return "";
        }
    };

    return "=" + emit(ast_.get());
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

    RegisterFunction({"LEN", {{"text", FunctionArgType::Value}}, 1, 1, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Number(static_cast<double>(args[0].GetText().length()));
        }, "Text length", "Text"});

    RegisterFunction({"LEFT", {{"text", FunctionArgType::Value}, {"num", FunctionArgType::Value, true}}, 1, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            std::string text = args[0].GetText();
            int num = args.size() > 1 ? static_cast<int>(args[1].ToNumber()) : 1;
            if (num < 0) return FormulaValue::Error(CellErrorType::ValueError);
            return FormulaValue::Text(text.substr(0, num));
        }, "Left characters", "Text"});

    RegisterFunction({"RIGHT", {{"text", FunctionArgType::Value}, {"num", FunctionArgType::Value, true}}, 1, 2, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            std::string text = args[0].GetText();
            int num = args.size() > 1 ? static_cast<int>(args[1].ToNumber()) : 1;
            if (num < 0) return FormulaValue::Error(CellErrorType::ValueError);
            if (num >= static_cast<int>(text.length())) return FormulaValue::Text(text);
            return FormulaValue::Text(text.substr(text.length() - num));
        }, "Right characters", "Text"});

    RegisterFunction({"MID", {{"text", FunctionArgType::Value}, {"start", FunctionArgType::Value}, {"num", FunctionArgType::Value}}, 3, 3, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>&,
           FormulaEvaluator*) -> FormulaValue {
            if (args.size() < 3) return FormulaValue::Error(CellErrorType::ValueError);
            std::string text = args[0].GetText();
            int start = static_cast<int>(args[1].ToNumber()) - 1;
            int num = static_cast<int>(args[2].ToNumber());
            if (start < 0 || num < 0) return FormulaValue::Error(CellErrorType::ValueError);
            if (start >= static_cast<int>(text.length())) return FormulaValue::Text("");
            return FormulaValue::Text(text.substr(start, num));
        }, "Middle characters", "Text"});

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
    RegisterFunction({"INDEX", {{"array", FunctionArgType::Range}, {"row", FunctionArgType::Value}, {"col", FunctionArgType::Value, true}}, 2, 3, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty() || ranges.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            int row = static_cast<int>(args[0].ToNumber()) - 1;
            if (row < 0 || row >= static_cast<int>(ranges[0].size())) return FormulaValue::Error(CellErrorType::ReferenceError);
            return ranges[0][row];
        }, "Value at index", "Lookup"});

    RegisterFunction({"MATCH", {{"value", FunctionArgType::Value}, {"array", FunctionArgType::Range}, {"type", FunctionArgType::Value, true}}, 2, 3, false,
        [](const std::vector<FormulaValue>& args, const std::vector<std::vector<FormulaValue>>& ranges,
           FormulaEvaluator*) -> FormulaValue {
            if (args.empty() || ranges.empty()) return FormulaValue::Error(CellErrorType::ValueError);
            const FormulaValue& lookup = args[0];
            for (size_t i = 0; i < ranges[0].size(); ++i) {
                if ((lookup.IsNumber() && ranges[0][i].IsNumber() && lookup.GetNumber() == ranges[0][i].GetNumber()) ||
                    (lookup.GetText() == ranges[0][i].GetText())) {
                    return FormulaValue::Number(static_cast<double>(i + 1));
                }
            }
            return FormulaValue::Error(CellErrorType::NAError);
        }, "Find position", "Lookup"});
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

FormulaValue FormulaEvaluator::Evaluate(const SpreadsheetFormula& formula) {
    if (!formula.IsValid()) return FormulaValue::Error(CellErrorType::NameError);
    return Evaluate(formula.GetAST());
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
        case FormulaNodeType::NamedRef: {
            CellRange range = GetNamedRange(node->namedRef);
            if (!range.IsValid()) return FormulaValue::Error(CellErrorType::NameError);
            auto values = FlattenRange(range);
            return values.empty() ? FormulaValue::Empty() : values[0];
        }
        default:
            return FormulaValue::Error(CellErrorType::ValueError);
    }
}

FormulaValue FormulaEvaluator::EvaluateLiteral(const FormulaNode* node) {
    return node->literalValue;
}

FormulaValue FormulaEvaluator::EvaluateCellRef(const FormulaNode* node) {
    return GetCellValue(node->cellRef);
}

FormulaValue FormulaEvaluator::EvaluateRangeRef(const FormulaNode* node) {
    auto values = FlattenRange(node->rangeRef);
    return values.empty() ? FormulaValue::Empty() : values[0];
}

FormulaValue FormulaEvaluator::EvaluateBinaryOp(const FormulaNode* node) {
    if (node->children.size() < 2) return FormulaValue::Error(CellErrorType::ValueError);
    FormulaValue left = Evaluate(node->children[0].get());
    FormulaValue right = Evaluate(node->children[1].get());

    if (left.IsError()) return left;
    if (right.IsError()) return right;

    const std::string& op = node->op;
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
    FormulaValue operand = Evaluate(node->children[0].get());
    if (operand.IsError()) return operand;

    const std::string& op = node->op;
    if (op == "-") return FormulaValue::Number(-operand.ToNumber());
    if (op == "+") return FormulaValue::Number(operand.ToNumber());
    if (op == "%") return FormulaValue::Number(operand.ToNumber() / 100.0);
    return FormulaValue::Error(CellErrorType::ValueError);
}

FormulaValue FormulaEvaluator::EvaluateFunction(const FormulaNode* node) {
    const FunctionDefinition* func = functionLibrary_.GetFunction(node->functionName);
    if (!func) return FormulaValue::Error(CellErrorType::NameError);

    std::vector<FormulaValue> args;
    std::vector<std::vector<FormulaValue>> rangeArgs;

    for (const auto& child : node->children) {
        if (child->nodeType == FormulaNodeType::RangeRef) {
            rangeArgs.push_back(FlattenRange(child->rangeRef));
        } else if (child->nodeType == FormulaNodeType::NamedRef) {
            CellRange range = GetNamedRange(child->namedRef);
            if (range.IsValid()) {
                rangeArgs.push_back(FlattenRange(range));
            } else {
                args.push_back(FormulaValue::Error(CellErrorType::NameError));
            }
        } else {
            args.push_back(Evaluate(child.get()));
        }
    }

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
    const SpreadsheetSheet* sheet = currentSheet_;
    if (!addr.sheetName.empty() && spreadsheet_) {
        const SpreadsheetSheet* named = spreadsheet_->GetSheetByName(addr.sheetName);
        if (named) sheet = named;
    }
    if (!sheet) return FormulaValue::Empty();

    const SpreadsheetCell* cell = sheet->GetCellIfExists(addr.row, addr.col);
    if (!cell || cell->IsEmpty()) return FormulaValue::Empty();
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
        if (named) sheet = named;
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

CellRange FormulaEvaluator::GetNamedRange(const std::string& name) const {
    if (currentSheet_) {
        if (const NamedRange* nr = currentSheet_->GetNamedRange(name)) {
            return nr->range;
        }
    }
    if (spreadsheet_) {
        return spreadsheet_->GetNamedRange(name);
    }
    return CellRange();
}

// ============================================================================
// SPREADSHEET FORMULA ENGINE - non-inline methods
// ============================================================================

FormulaValue SpreadsheetFormulaEngine::EvaluateCell(SpreadsheetCell* cell, SpreadsheetSheet* sheet) {
    if (!cell || !cell->HasFormula()) return FormulaValue::Empty();

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
        result = evaluator_->Evaluate(*formula);
    } else {
        result = FormulaValue::Error(CellErrorType::NameError);
    }

    // Store the result back into the cell using the variant API.
    switch (result.type) {
        case CellValueType::Number:
            cell->SetFormulaResult(CellValueVariant(result.GetNumber()), CellValueType::Number);
            break;
        case CellValueType::Text:
            cell->SetFormulaResult(CellValueVariant(result.GetText()), CellValueType::Text);
            break;
        case CellValueType::Boolean:
            cell->SetFormulaResult(CellValueVariant(result.GetBoolean()), CellValueType::Boolean);
            break;
        case CellValueType::Error:
            cell->SetFormulaResult(CellValueVariant(result.GetError()), CellValueType::Error);
            break;
        default:
            cell->SetFormulaResult(CellValueVariant(std::monostate{}), CellValueType::Empty);
            break;
    }

    cell->MarkFormulaClean();
    return result;
}

void SpreadsheetFormulaEngine::Recalculate() {
    if (!spreadsheet_ || !autoCalculate_) return;

    for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
        auto* sheet = spreadsheet_->GetSheet(s);
        if (!sheet) continue;

        sheet->ForEachCell([this, sheet](int /*row*/, int /*col*/, SpreadsheetCell& cell) {
            if (cell.HasFormula() && cell.IsFormulaDirty()) {
                EvaluateCell(&cell, sheet);
            }
        });
    }

    dirtyCells_.clear();
}

void SpreadsheetFormulaEngine::RecalculateAll() {
    if (!spreadsheet_) return;

    for (int s = 0; s < spreadsheet_->GetSheetCount(); ++s) {
        auto* sheet = spreadsheet_->GetSheet(s);
        if (!sheet) continue;

        sheet->ForEachCell([this, sheet](int /*row*/, int /*col*/, SpreadsheetCell& cell) {
            if (cell.HasFormula()) {
                cell.MarkFormulaDirty();
                EvaluateCell(&cell, sheet);
            }
        });
    }

    dirtyCells_.clear();
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
