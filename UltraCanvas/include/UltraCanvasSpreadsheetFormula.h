// include/UltraCanvasSpreadsheetFormula.h
// Formula parser and evaluation engine (OpenFormula compatible)
// Version: 1.0.0
// Last Modified: 2026-01-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace UltraCanvas {

// Forward declarations
class SpreadsheetSheet;
class UltraCanvasSpreadsheet;
class SpreadsheetCell;

// ============================================================================
// FORMULA TOKEN TYPES
// ============================================================================

enum class FormulaTokenType {
    Number,         // Numeric literal
    String,         // String literal "text"
    Boolean,        // TRUE / FALSE
    Error,          // #VALUE!, #REF!, etc.
    CellRef,        // A1, $B$2, Sheet1.C3
    RangeRef,       // A1:B10
    NamedRef,       // Named range reference
    Function,       // SUM, AVERAGE, IF, etc.
    Operator,       // +, -, *, /, ^, &, =, <, >, etc.
    LeftParen,      // (
    RightParen,     // )
    Comma,          // , (argument separator)
    Semicolon,      // ; (alternative argument separator in ODS)
    Colon,          // : (range operator)
    Space,          // (intersection operator in some contexts)
    EndOfFormula
};

// ============================================================================
// FORMULA TOKEN
// ============================================================================

struct FormulaToken {
    FormulaTokenType type = FormulaTokenType::EndOfFormula;
    std::string text;
    double numValue = 0.0;
    bool boolValue = false;
    CellErrorType errorValue = CellErrorType::None;
    CellAddress cellRef;
    CellRange rangeRef;
    int position = 0;  // Position in original formula string
    
    FormulaToken() = default;
    FormulaToken(FormulaTokenType t, const std::string& txt, int pos = 0)
        : type(t), text(txt), position(pos) {}
    
    static FormulaToken Number(double value, int pos = 0) {
        FormulaToken t(FormulaTokenType::Number, std::to_string(value), pos);
        t.numValue = value;
        return t;
    }
    
    static FormulaToken String(const std::string& value, int pos = 0) {
        return FormulaToken(FormulaTokenType::String, value, pos);
    }
    
    static FormulaToken Boolean(bool value, int pos = 0) {
        FormulaToken t(FormulaTokenType::Boolean, value ? "TRUE" : "FALSE", pos);
        t.boolValue = value;
        return t;
    }
    
    static FormulaToken Error(CellErrorType error, int pos = 0) {
        FormulaToken t(FormulaTokenType::Error, CellErrorToString(error), pos);
        t.errorValue = error;
        return t;
    }
    
    static FormulaToken CellReference(const CellAddress& addr, int pos = 0) {
        FormulaToken t(FormulaTokenType::CellRef, addr.ToString(), pos);
        t.cellRef = addr;
        return t;
    }
    
    static FormulaToken RangeReference(const CellRange& range, int pos = 0) {
        FormulaToken t(FormulaTokenType::RangeRef, range.ToString(), pos);
        t.rangeRef = range;
        return t;
    }
};

// ============================================================================
// FORMULA VALUE (Result of evaluation)
// ============================================================================

struct FormulaValue {
    CellValueType type = CellValueType::Empty;
    CellValueVariant value;
    
    FormulaValue() = default;
    
    // Constructors for different types
    static FormulaValue Empty() { return FormulaValue(); }
    
    static FormulaValue Number(double v) {
        FormulaValue fv;
        fv.type = CellValueType::Number;
        fv.value = v;
        return fv;
    }
    
    static FormulaValue Text(const std::string& v) {
        FormulaValue fv;
        fv.type = CellValueType::Text;
        fv.value = v;
        return fv;
    }
    
    static FormulaValue Boolean(bool v) {
        FormulaValue fv;
        fv.type = CellValueType::Boolean;
        fv.value = v;
        return fv;
    }
    
    static FormulaValue Error(CellErrorType err) {
        FormulaValue fv;
        fv.type = CellValueType::Error;
        fv.value = err;
        return fv;
    }
    
    // Type checks
    bool IsEmpty() const { return type == CellValueType::Empty; }
    bool IsNumber() const { return type == CellValueType::Number; }
    bool IsText() const { return type == CellValueType::Text; }
    bool IsBoolean() const { return type == CellValueType::Boolean; }
    bool IsError() const { return type == CellValueType::Error; }
    
    // Value getters
    double GetNumber() const {
        if (auto* v = std::get_if<double>(&value)) return *v;
        if (auto* b = std::get_if<bool>(&value)) return *b ? 1.0 : 0.0;
        return 0.0;
    }
    
    std::string GetText() const {
        if (auto* v = std::get_if<std::string>(&value)) return *v;
        if (auto* n = std::get_if<double>(&value)) return std::to_string(*n);
        if (auto* b = std::get_if<bool>(&value)) return *b ? "TRUE" : "FALSE";
        if (auto* e = std::get_if<CellErrorType>(&value)) return CellErrorToString(*e);
        return "";
    }
    
    bool GetBoolean() const {
        if (auto* v = std::get_if<bool>(&value)) return *v;
        if (auto* n = std::get_if<double>(&value)) return *n != 0.0;
        return false;
    }
    
    CellErrorType GetError() const {
        if (auto* v = std::get_if<CellErrorType>(&value)) return *v;
        return CellErrorType::None;
    }
    
    // Convert to number (with type coercion)
    double ToNumber() const {
        switch (type) {
            case CellValueType::Number:
            case CellValueType::Date:
            case CellValueType::Time:
            case CellValueType::DateTime:
            case CellValueType::Percentage:
                return GetNumber();
            case CellValueType::Boolean:
                return GetBoolean() ? 1.0 : 0.0;
            case CellValueType::Text: {
                try {
                    return std::stod(GetText());
                } catch (...) {
                    return 0.0;
                }
            }
            default:
                return 0.0;
        }
    }
};

// ============================================================================
// FORMULA TOKENIZER
// ============================================================================

class FormulaTokenizer {
private:
    std::string formula_;
    size_t pos_ = 0;
    std::string currentSheetName_;
    
public:
    FormulaTokenizer(const std::string& formula, const std::string& sheetName = "")
        : formula_(formula), currentSheetName_(sheetName) {}
    
    std::vector<FormulaToken> Tokenize();
    
private:
    char Peek() const { return pos_ < formula_.size() ? formula_[pos_] : '\0'; }
    char PeekNext() const { return pos_ + 1 < formula_.size() ? formula_[pos_ + 1] : '\0'; }
    char Advance() { return pos_ < formula_.size() ? formula_[pos_++] : '\0'; }
    void SkipWhitespace();
    
    FormulaToken ReadNumber();
    FormulaToken ReadString();
    FormulaToken ReadIdentifier();
    FormulaToken ReadOperator();
    FormulaToken ReadCellReference(const std::string& prefix = "");
    
    bool IsAlpha(char c) const { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
    bool IsDigit(char c) const { return c >= '0' && c <= '9'; }
    bool IsAlphaNum(char c) const { return IsAlpha(c) || IsDigit(c); }
};

// ============================================================================
// FORMULA AST NODE
// ============================================================================

enum class FormulaNodeType {
    Literal,        // Number, string, boolean, error
    CellRef,        // Single cell reference
    RangeRef,       // Cell range reference
    NamedRef,       // Named range
    BinaryOp,       // Binary operation (+, -, *, /, etc.)
    UnaryOp,        // Unary operation (-, +, %)
    FunctionCall,   // Function call with arguments
    Array           // Array literal {1,2,3;4,5,6}
};

class FormulaNode {
public:
    FormulaNodeType nodeType;
    
    // For literals
    FormulaValue literalValue;
    
    // For cell/range references
    CellAddress cellRef;
    CellRange rangeRef;
    std::string namedRef;
    
    // For operators
    std::string op;
    
    // For functions
    std::string functionName;
    
    // Children (for operators and functions)
    std::vector<std::unique_ptr<FormulaNode>> children;
    
    FormulaNode(FormulaNodeType type) : nodeType(type) {}
    
    // Factory methods
    static std::unique_ptr<FormulaNode> Literal(const FormulaValue& value);
    static std::unique_ptr<FormulaNode> CellReference(const CellAddress& addr);
    static std::unique_ptr<FormulaNode> RangeReference(const CellRange& range);
    static std::unique_ptr<FormulaNode> NamedReference(const std::string& name);
    static std::unique_ptr<FormulaNode> BinaryOperation(const std::string& op,
                                                         std::unique_ptr<FormulaNode> left,
                                                         std::unique_ptr<FormulaNode> right);
    static std::unique_ptr<FormulaNode> UnaryOperation(const std::string& op,
                                                        std::unique_ptr<FormulaNode> operand);
    static std::unique_ptr<FormulaNode> Function(const std::string& name,
                                                  std::vector<std::unique_ptr<FormulaNode>> args);
};

// ============================================================================
// FORMULA PARSER
// ============================================================================

class FormulaParser {
private:
    std::vector<FormulaToken> tokens_;
    size_t pos_ = 0;
    std::string errorMessage_;
    
public:
    std::unique_ptr<FormulaNode> Parse(const std::vector<FormulaToken>& tokens);
    const std::string& GetErrorMessage() const { return errorMessage_; }
    
private:
    const FormulaToken& Current() const;
    const FormulaToken& Peek() const;
    bool IsAtEnd() const;
    bool Match(FormulaTokenType type);
    bool Check(FormulaTokenType type) const;
    void Advance();
    
    // Recursive descent parsing
    std::unique_ptr<FormulaNode> ParseExpression();
    std::unique_ptr<FormulaNode> ParseComparison();
    std::unique_ptr<FormulaNode> ParseConcatenation();
    std::unique_ptr<FormulaNode> ParseAddition();
    std::unique_ptr<FormulaNode> ParseMultiplication();
    std::unique_ptr<FormulaNode> ParsePower();
    std::unique_ptr<FormulaNode> ParseUnary();
    std::unique_ptr<FormulaNode> ParsePercent();
    std::unique_ptr<FormulaNode> ParsePrimary();
    std::unique_ptr<FormulaNode> ParseFunctionCall(const std::string& name);
    
    void Error(const std::string& message);
};

// ============================================================================
// SPREADSHEET FORMULA
// ============================================================================

class SpreadsheetFormula {
private:
    std::string originalText_;
    std::unique_ptr<FormulaNode> ast_;
    std::vector<CellAddress> dependencies_;  // Cells this formula depends on
    std::vector<CellRange> rangeDependencies_;
    bool valid_ = false;
    std::string errorMessage_;
    CellAddress ownerCell_;
    
public:
    SpreadsheetFormula(const std::string& formulaText, const CellAddress& owner);
    
    // Parse the formula
    bool Parse();
    
    // Check validity
    bool IsValid() const { return valid_; }
    const std::string& GetErrorMessage() const { return errorMessage_; }
    
    // Get original text
    const std::string& GetText() const { return originalText_; }
    
    // Get dependencies
    const std::vector<CellAddress>& GetDependencies() const { return dependencies_; }
    const std::vector<CellRange>& GetRangeDependencies() const { return rangeDependencies_; }
    
    // Check if depends on cell
    bool DependsOn(const CellAddress& cell) const;
    bool DependsOnRange(const CellRange& range) const;
    
    // Get AST (for evaluation)
    const FormulaNode* GetAST() const { return ast_.get(); }
    
    // Adjust references when rows/columns are inserted/deleted
    void AdjustReferences(int startRow, int startCol, int rowDelta, int colDelta);
    
    // Get formula with adjusted references
    std::string GetAdjustedText(int rowOffset, int colOffset) const;
    
private:
    void CollectDependencies(const FormulaNode* node);
};

// ============================================================================
// FORMULA FUNCTION DEFINITION
// ============================================================================

// Function argument types
enum class FunctionArgType {
    Value,      // Single value
    Range,      // Cell range
    Array,      // Array of values
    Any         // Any type
};

struct FunctionArgDef {
    std::string name;
    FunctionArgType type = FunctionArgType::Any;
    bool optional = false;
    FormulaValue defaultValue;
};

// Function implementation signature
using FormulaFunctionImpl = std::function<FormulaValue(
    const std::vector<FormulaValue>& args,
    const std::vector<std::vector<FormulaValue>>& rangeArgs,
    class FormulaEvaluator* evaluator
)>;

struct FunctionDefinition {
    std::string name;
    std::vector<FunctionArgDef> args;
    int minArgs = 0;
    int maxArgs = 255;  // -1 for unlimited
    bool isVolatile = false;  // TRUE for NOW(), RAND(), etc.
    FormulaFunctionImpl implementation;
    std::string description;
    std::string category;
};

// ============================================================================
// FORMULA FUNCTION LIBRARY
// ============================================================================

class FormulaFunctionLibrary {
private:
    std::unordered_map<std::string, FunctionDefinition> functions_;
    
public:
    FormulaFunctionLibrary();
    
    // Register function
    void RegisterFunction(const FunctionDefinition& def);
    
    // Get function
    const FunctionDefinition* GetFunction(const std::string& name) const;
    bool HasFunction(const std::string& name) const;
    
    // Get all functions
    std::vector<std::string> GetFunctionNames() const;
    std::vector<std::string> GetFunctionsByCategory(const std::string& category) const;
    
private:
    // Register built-in functions
    void RegisterMathFunctions();
    void RegisterStatisticalFunctions();
    void RegisterLogicalFunctions();
    void RegisterTextFunctions();
    void RegisterDateTimeFunctions();
    void RegisterLookupFunctions();
    void RegisterInformationFunctions();
    void RegisterFinancialFunctions();
};

// ============================================================================
// FORMULA EVALUATOR
// ============================================================================

class FormulaEvaluator {
private:
    UltraCanvasSpreadsheet* spreadsheet_ = nullptr;
    SpreadsheetSheet* currentSheet_ = nullptr;
    FormulaFunctionLibrary& functionLibrary_;
    
    // Circular reference detection
    std::unordered_set<std::string> evaluationStack_;
    int maxRecursionDepth_ = 100;
    int currentDepth_ = 0;
    
public:
    FormulaEvaluator(FormulaFunctionLibrary& library);
    
    // Set context
    void SetSpreadsheet(UltraCanvasSpreadsheet* spreadsheet) { spreadsheet_ = spreadsheet; }
    void SetCurrentSheet(SpreadsheetSheet* sheet) { currentSheet_ = sheet; }
    
    // Evaluate formula
    FormulaValue Evaluate(const SpreadsheetFormula& formula);
    FormulaValue Evaluate(const FormulaNode* node);
    
    // Get cell value
    FormulaValue GetCellValue(const CellAddress& addr) const;
    
    // Get range values
    std::vector<std::vector<FormulaValue>> GetRangeValues(const CellRange& range) const;
    
    // Flatten range to single array
    std::vector<FormulaValue> FlattenRange(const CellRange& range) const;
    
    // Get named range
    CellRange GetNamedRange(const std::string& name) const;
    
private:
    FormulaValue EvaluateLiteral(const FormulaNode* node);
    FormulaValue EvaluateCellRef(const FormulaNode* node);
    FormulaValue EvaluateRangeRef(const FormulaNode* node);
    FormulaValue EvaluateBinaryOp(const FormulaNode* node);
    FormulaValue EvaluateUnaryOp(const FormulaNode* node);
    FormulaValue EvaluateFunction(const FormulaNode* node);
    
    // Binary operations
    FormulaValue Add(const FormulaValue& left, const FormulaValue& right);
    FormulaValue Subtract(const FormulaValue& left, const FormulaValue& right);
    FormulaValue Multiply(const FormulaValue& left, const FormulaValue& right);
    FormulaValue Divide(const FormulaValue& left, const FormulaValue& right);
    FormulaValue Power(const FormulaValue& left, const FormulaValue& right);
    FormulaValue Concatenate(const FormulaValue& left, const FormulaValue& right);
    FormulaValue Compare(const FormulaValue& left, const FormulaValue& right, const std::string& op);
};

// ============================================================================
// FORMULA ENGINE (Main interface)
// ============================================================================

class SpreadsheetFormulaEngine {
private:
    UltraCanvasSpreadsheet* spreadsheet_ = nullptr;
    FormulaFunctionLibrary functionLibrary_;
    std::unique_ptr<FormulaEvaluator> evaluator_;
    
    // Dependency graph for recalculation
    std::unordered_map<std::string, std::unordered_set<std::string>> dependents_;
    
    // Dirty cells that need recalculation
    std::unordered_set<std::string> dirtyCells_;
    
    // Calculation mode
    bool autoCalculate_ = true;
    bool iterativeCalculation_ = false;
    int maxIterations_ = 100;
    double convergenceThreshold_ = 0.001;
    
public:
    SpreadsheetFormulaEngine();
    ~SpreadsheetFormulaEngine();
    
    // Set spreadsheet
    void SetSpreadsheet(UltraCanvasSpreadsheet* spreadsheet);
    
    // Parse formula
    std::shared_ptr<SpreadsheetFormula> ParseFormula(const std::string& text, 
                                                      const CellAddress& owner);
    
    // Evaluate cell
    FormulaValue EvaluateCell(SpreadsheetCell* cell, SpreadsheetSheet* sheet);
    
    // Mark cell as dirty (needs recalculation)
    void MarkDirty(const CellAddress& cell, const std::string& sheetName);
    
    // Recalculate dirty cells
    void Recalculate();
    
    // Recalculate all
    void RecalculateAll();
    
    // Update dependencies for cell
    void UpdateDependencies(SpreadsheetCell* cell, const std::string& sheetName);
    
    // Check for circular references
    bool HasCircularReference(const CellAddress& cell, const std::string& sheetName) const;
    
    // Calculation settings
    bool IsAutoCalculateEnabled() const { return autoCalculate_; }
    void SetAutoCalculate(bool enabled) { autoCalculate_ = enabled; }
    
    bool IsIterativeCalculationEnabled() const { return iterativeCalculation_; }
    void SetIterativeCalculation(bool enabled) { iterativeCalculation_ = enabled; }
    
    int GetMaxIterations() const { return maxIterations_; }
    void SetMaxIterations(int max) { maxIterations_ = max; }
    
    // Get function library
    FormulaFunctionLibrary& GetFunctionLibrary() { return functionLibrary_; }
    
private:
    std::string MakeCellKey(const CellAddress& cell, const std::string& sheetName) const;
    void PropagateChange(const std::string& cellKey);
    std::vector<std::string> GetCalculationOrder() const;
};

// ============================================================================
// TOKENIZER IMPLEMENTATION
// ============================================================================

inline void FormulaTokenizer::SkipWhitespace() {
    while (pos_ < formula_.size() && (formula_[pos_] == ' ' || formula_[pos_] == '\t')) {
        ++pos_;
    }
}

inline FormulaToken FormulaTokenizer::ReadNumber() {
    int startPos = static_cast<int>(pos_);
    std::string numStr;
    bool hasDecimal = false;
    bool hasExponent = false;
    
    while (pos_ < formula_.size()) {
        char c = formula_[pos_];
        if (IsDigit(c)) {
            numStr += c;
            ++pos_;
        } else if (c == '.' && !hasDecimal && !hasExponent) {
            numStr += c;
            hasDecimal = true;
            ++pos_;
        } else if ((c == 'e' || c == 'E') && !hasExponent && !numStr.empty()) {
            numStr += c;
            hasExponent = true;
            ++pos_;
            if (pos_ < formula_.size() && (formula_[pos_] == '+' || formula_[pos_] == '-')) {
                numStr += formula_[pos_++];
            }
        } else {
            break;
        }
    }
    
    return FormulaToken::Number(std::stod(numStr), startPos);
}

inline FormulaToken FormulaTokenizer::ReadString() {
    int startPos = static_cast<int>(pos_);
    ++pos_;  // Skip opening quote
    std::string str;
    
    while (pos_ < formula_.size()) {
        char c = formula_[pos_++];
        if (c == '"') {
            if (pos_ < formula_.size() && formula_[pos_] == '"') {
                str += '"';  // Escaped quote
                ++pos_;
            } else {
                break;  // End of string
            }
        } else {
            str += c;
        }
    }
    
    return FormulaToken::String(str, startPos);
}

inline FormulaToken FormulaTokenizer::ReadIdentifier() {
    int startPos = static_cast<int>(pos_);
    std::string ident;
    
    while (pos_ < formula_.size() && (IsAlphaNum(Peek()) || Peek() == '$' || Peek() == '.')) {
        ident += Advance();
    }
    
    // Check for boolean
    std::string upper = ident;
    for (auto& c : upper) c = toupper(c);
    
    if (upper == "TRUE") {
        return FormulaToken::Boolean(true, startPos);
    }
    if (upper == "FALSE") {
        return FormulaToken::Boolean(false, startPos);
    }
    
    // Check for error values
    if (upper == "#DIV/0!" || ident == "#DIV/0!") {
        return FormulaToken::Error(CellErrorType::DivisionByZero, startPos);
    }
    if (upper == "#VALUE!" || ident == "#VALUE!") {
        return FormulaToken::Error(CellErrorType::ValueError, startPos);
    }
    if (upper == "#REF!" || ident == "#REF!") {
        return FormulaToken::Error(CellErrorType::ReferenceError, startPos);
    }
    if (upper == "#NAME?" || ident == "#NAME?") {
        return FormulaToken::Error(CellErrorType::NameError, startPos);
    }
    if (upper == "#N/A" || ident == "#N/A") {
        return FormulaToken::Error(CellErrorType::NAError, startPos);
    }
    
    // A name immediately followed by '(' is a function call - even when it
    // contains digits (e.g. LOG10, ATAN2). Cell references are never followed
    // by '(', so resolve this BEFORE the cell-reference classification below,
    // otherwise "LOG10" would be misread as column LOG, row 10.
    if (ident.find('$') == std::string::npos && ident.find('.') == std::string::npos) {
        size_t look = pos_;
        while (look < formula_.size() && (formula_[look] == ' ' || formula_[look] == '\t')) ++look;
        if (look < formula_.size() && formula_[look] == '(') {
            return FormulaToken(FormulaTokenType::Function, ident, startPos);
        }
    }

    // Check if it's a cell reference (starts with letter or $, has digit)
    bool hasLetter = false, hasDigit = false;
    for (char c : ident) {
        if (IsAlpha(c)) hasLetter = true;
        if (IsDigit(c)) hasDigit = true;
    }

    if (hasLetter && hasDigit) {
        // Looks like cell reference - check if followed by :
        if (pos_ < formula_.size() && formula_[pos_] == ':') {
            ++pos_;
            std::string second;
            while (pos_ < formula_.size() && (IsAlphaNum(Peek()) || Peek() == '$')) {
                second += Advance();
            }
            CellAddress start = CellAddress::FromString(ident);
            CellAddress end = CellAddress::FromString(second);
            return FormulaToken::RangeReference(CellRange(start, end), startPos);
        }
        
        CellAddress addr = CellAddress::FromString(ident);
        if (addr.IsValid()) {
            return FormulaToken::CellReference(addr, startPos);
        }
    }
    
    // Check if followed by ( - then it's a function
    SkipWhitespace();
    if (pos_ < formula_.size() && formula_[pos_] == '(') {
        return FormulaToken(FormulaTokenType::Function, ident, startPos);
    }
    
    // Named range or unknown identifier
    return FormulaToken(FormulaTokenType::NamedRef, ident, startPos);
}

inline std::vector<FormulaToken> FormulaTokenizer::Tokenize() {
    std::vector<FormulaToken> tokens;
    
    // Skip leading =
    if (pos_ < formula_.size() && formula_[pos_] == '=') {
        ++pos_;
    }
    
    while (pos_ < formula_.size()) {
        SkipWhitespace();
        if (pos_ >= formula_.size()) break;
        
        char c = Peek();
        int startPos = static_cast<int>(pos_);
        
        if (IsDigit(c) || (c == '.' && pos_ + 1 < formula_.size() && IsDigit(formula_[pos_ + 1]))) {
            tokens.push_back(ReadNumber());
        }
        else if (c == '"') {
            tokens.push_back(ReadString());
        }
        else if (IsAlpha(c) || c == '$' || c == '#') {
            tokens.push_back(ReadIdentifier());
        }
        else if (c == '(') {
            tokens.push_back(FormulaToken(FormulaTokenType::LeftParen, "(", startPos));
            ++pos_;
        }
        else if (c == ')') {
            tokens.push_back(FormulaToken(FormulaTokenType::RightParen, ")", startPos));
            ++pos_;
        }
        else if (c == ',') {
            tokens.push_back(FormulaToken(FormulaTokenType::Comma, ",", startPos));
            ++pos_;
        }
        else if (c == ';') {
            tokens.push_back(FormulaToken(FormulaTokenType::Semicolon, ";", startPos));
            ++pos_;
        }
        else if (c == ':') {
            tokens.push_back(FormulaToken(FormulaTokenType::Colon, ":", startPos));
            ++pos_;
        }
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || 
                 c == '&' || c == '%' || c == '=' || c == '<' || c == '>') {
            std::string op(1, c);
            ++pos_;
            // Check for compound operators
            if ((c == '<' || c == '>') && pos_ < formula_.size()) {
                char next = formula_[pos_];
                if ((c == '<' && next == '>') || (c == '<' && next == '=') || (c == '>' && next == '=')) {
                    op += next;
                    ++pos_;
                }
            }
            tokens.push_back(FormulaToken(FormulaTokenType::Operator, op, startPos));
        }
        else {
            ++pos_;  // Skip unknown character
        }
    }
    
    tokens.push_back(FormulaToken(FormulaTokenType::EndOfFormula, "", static_cast<int>(pos_)));
    return tokens;
}

// ============================================================================
// FORMULA NODE IMPLEMENTATION
// ============================================================================

inline std::unique_ptr<FormulaNode> FormulaNode::Literal(const FormulaValue& value) {
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::Literal);
    node->literalValue = value;
    return node;
}

inline std::unique_ptr<FormulaNode> FormulaNode::CellReference(const CellAddress& addr) {
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::CellRef);
    node->cellRef = addr;
    return node;
}

inline std::unique_ptr<FormulaNode> FormulaNode::RangeReference(const CellRange& range) {
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::RangeRef);
    node->rangeRef = range;
    return node;
}

inline std::unique_ptr<FormulaNode> FormulaNode::NamedReference(const std::string& name) {
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::NamedRef);
    node->namedRef = name;
    return node;
}

inline std::unique_ptr<FormulaNode> FormulaNode::BinaryOperation(
    const std::string& op,
    std::unique_ptr<FormulaNode> left,
    std::unique_ptr<FormulaNode> right)
{
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::BinaryOp);
    node->op = op;
    node->children.push_back(std::move(left));
    node->children.push_back(std::move(right));
    return node;
}

inline std::unique_ptr<FormulaNode> FormulaNode::UnaryOperation(
    const std::string& op,
    std::unique_ptr<FormulaNode> operand)
{
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::UnaryOp);
    node->op = op;
    node->children.push_back(std::move(operand));
    return node;
}

inline std::unique_ptr<FormulaNode> FormulaNode::Function(
    const std::string& name,
    std::vector<std::unique_ptr<FormulaNode>> args)
{
    auto node = std::make_unique<FormulaNode>(FormulaNodeType::FunctionCall);
    node->functionName = name;
    node->children = std::move(args);
    return node;
}

// ============================================================================
// SPREADSHEET FORMULA IMPLEMENTATION
// ============================================================================

inline SpreadsheetFormula::SpreadsheetFormula(const std::string& formulaText, const CellAddress& owner)
    : originalText_(formulaText)
    , ownerCell_(owner)
{
}

inline bool SpreadsheetFormula::Parse() {
    FormulaTokenizer tokenizer(originalText_);
    auto tokens = tokenizer.Tokenize();
    
    FormulaParser parser;
    ast_ = parser.Parse(tokens);
    
    if (!ast_) {
        errorMessage_ = parser.GetErrorMessage();
        valid_ = false;
        return false;
    }
    
    // Collect dependencies
    dependencies_.clear();
    rangeDependencies_.clear();
    CollectDependencies(ast_.get());
    
    valid_ = true;
    return true;
}

inline void SpreadsheetFormula::CollectDependencies(const FormulaNode* node) {
    if (!node) return;
    
    switch (node->nodeType) {
        case FormulaNodeType::CellRef:
            dependencies_.push_back(node->cellRef);
            break;
        case FormulaNodeType::RangeRef:
            rangeDependencies_.push_back(node->rangeRef);
            break;
        default:
            break;
    }
    
    for (const auto& child : node->children) {
        CollectDependencies(child.get());
    }
}

inline bool SpreadsheetFormula::DependsOn(const CellAddress& cell) const {
    for (const auto& dep : dependencies_) {
        if (dep == cell) return true;
    }
    for (const auto& range : rangeDependencies_) {
        if (range.Contains(cell)) return true;
    }
    return false;
}

inline bool SpreadsheetFormula::DependsOnRange(const CellRange& range) const {
    for (const auto& dep : dependencies_) {
        if (range.Contains(dep)) return true;
    }
    for (const auto& r : rangeDependencies_) {
        if (r.Intersects(range)) return true;
    }
    return false;
}

// ============================================================================
// FORMULA ENGINE IMPLEMENTATION
// ============================================================================

inline SpreadsheetFormulaEngine::SpreadsheetFormulaEngine() {
    evaluator_ = std::make_unique<FormulaEvaluator>(functionLibrary_);
}

inline SpreadsheetFormulaEngine::~SpreadsheetFormulaEngine() = default;

inline void SpreadsheetFormulaEngine::SetSpreadsheet(UltraCanvasSpreadsheet* spreadsheet) {
    spreadsheet_ = spreadsheet;
    evaluator_->SetSpreadsheet(spreadsheet);
}

inline std::shared_ptr<SpreadsheetFormula> SpreadsheetFormulaEngine::ParseFormula(
    const std::string& text, const CellAddress& owner)
{
    auto formula = std::make_shared<SpreadsheetFormula>(text, owner);
    formula->Parse();
    return formula;
}

inline std::string SpreadsheetFormulaEngine::MakeCellKey(
    const CellAddress& cell, const std::string& sheetName) const
{
    return sheetName + "!" + cell.ToString();
}

inline void SpreadsheetFormulaEngine::MarkDirty(const CellAddress& cell, const std::string& sheetName) {
    std::string key = MakeCellKey(cell, sheetName);
    dirtyCells_.insert(key);
    PropagateChange(key);
}

inline void SpreadsheetFormulaEngine::PropagateChange(const std::string& cellKey) {
    auto it = dependents_.find(cellKey);
    if (it != dependents_.end()) {
        for (const auto& dep : it->second) {
            if (dirtyCells_.find(dep) == dirtyCells_.end()) {
                dirtyCells_.insert(dep);
                PropagateChange(dep);
            }
        }
    }
}

} // namespace UltraCanvas
