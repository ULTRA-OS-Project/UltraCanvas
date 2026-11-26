// include/UltraCanvasSpreadsheetFormula.h
// Formula parser and evaluation engine (OpenFormula compatible)
// Version: 1.5.0 - arrays: FormulaValue::Array, array constants {1,2;3,4}, ranges and element-wise
//                  arithmetic as arrays, A1# spill references, the @ operator, LET and LAMBDA,
//                  omitted arguments; the engine spills a dynamic array result (#SPILL! when
//                  blocked) and fills a fixed (Ctrl+Shift+Enter) array formula's range;
//                  FunctionDefinition::elementWise, EvaluateArrayFormula, ResolveSpillRange
// Version: 1.4.0 - NamedRangeError: a name whose sheet was deleted is #REF!, not #NAME?
// Version: 1.3.0 - AdjustReferences / GetAdjustedText removed (unused; shifted every reference by a
//                  fixed offset whatever was inserted, and rebuilt the text in parentheses)
// Version: 1.2.0 - Recalculate follows a change to every formula that reads it (directly or through
//                  others): MarkRangeDirty, MarkAllDirty, HasPendingChanges; GetFunctionNames,
//                  ReadsNamedRanges
// Version: 1.1.0
// Last Modified: 2026-10-09
// V1.1.0: error literals (#N/A, #DIV/0!, ...) tokenize instead of stalling the
//   tokenizer on their '#'; a number becomes text as a spreadsheet writes it
//   (12.5, not 12.500000); FormulaEvaluator::GetCallArguments gives a
//   function the order and the shape of its arguments.
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat / ParseFloatClassic - dot-decimal, non-throwing
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
    EndOfFormula,
    LeftBrace,      // { - an array constant {1,2;3,4}: ',' between columns, ';' between rows
    RightBrace,     // }
    SpillRef        // A1# - the whole array spilled from A1
};

// ============================================================================
// FORMULA TOKEN
// ============================================================================

struct FormulaToken {
    FormulaTokenType type = FormulaTokenType::EndOfFormula;
    std::string text;
    double numValue = 0.0;
    bool boolValue = false;
    CellErrorType errorValue = CellErrorType::NoError;
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

struct FormulaArray;
struct FormulaLambda;

// One value, or - with `array` set - a block of values (a range read as a
// value, an array constant {1,2;3,4}, the result of FILTER, SEQUENCE or of
// arithmetic on ranges), or - with `lambda` set - a LAMBDA not yet called.
// An array or a lambda has type Empty and no scalar value of its own:
// IsArray() / IsLambda() say which it is, and IsEmpty() is false for both.
struct FormulaValue {
    CellValueType type = CellValueType::Empty;
    CellValueVariant value;
    std::shared_ptr<const FormulaArray> array;
    std::shared_ptr<const FormulaLambda> lambda;

    FormulaValue() = default;

    // rows x columns values, row by row. A 1 x 1 array is that one value;
    // no value at all is #CALC!, as Excel shows an empty array.
    static FormulaValue Array(int rows, int cols, std::vector<FormulaValue> cells);
    bool IsArray() const { return array != nullptr; }
    bool IsLambda() const { return lambda != nullptr; }
    int ArrayRows() const;
    int ArrayCols() const;
    // Element (r, c) of an array; a single value is itself at every position.
    const FormulaValue& ArrayAt(int r, int c) const;
    // The value a single cell shows: an array's top-left element.
    FormulaValue TopLeft() const;
    
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
    bool IsEmpty() const { return type == CellValueType::Empty && !array && !lambda; }
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
        // As a spreadsheet writes a number into text (=A1&"", CONCATENATE):
        // up to 15 significant digits, no trailing zeros, '.' whatever the
        // locale - 12.5, not std::to_string's locale-dependent 12.500000.
        if (auto* n = std::get_if<double>(&value)) return FormatFloatClassic(*n, 15);
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
        return CellErrorType::NoError;
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
                // Text that looks like a number counts as one; text that does
                // not counts as zero, which is what the old catch produced.
                double parsed = 0.0;
                TryParseFloat(GetText(), parsed);
                return parsed;
            }
            default:
                return 0.0;
        }
    }
};

// The values of an array, row by row.
struct FormulaArray {
    int rows = 0;
    int cols = 0;
    std::vector<FormulaValue> cells;
    const FormulaValue& At(int r, int c) const {
        return cells[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c)];
    }
};

class FormulaNode;

// A LAMBDA(x, y, body) as a value: its parameter names (upper case), its
// body - which stays owned by the formula it was written in - and the LET
// names in force where it was written.
struct FormulaLambda {
    std::vector<std::string> parameters;
    const FormulaNode* body = nullptr;
    std::vector<std::pair<std::string, FormulaValue>> captured;
};

inline FormulaValue FormulaValue::Array(int rows, int cols, std::vector<FormulaValue> cells) {
    if (rows <= 0 || cols <= 0 ||
        cells.size() != static_cast<size_t>(rows) * static_cast<size_t>(cols)) {
        return Error(CellErrorType::Calc);
    }
    if (rows == 1 && cols == 1) return cells.front();
    auto block = std::make_shared<FormulaArray>();
    block->rows = rows;
    block->cols = cols;
    block->cells = std::move(cells);
    FormulaValue fv;
    fv.array = std::move(block);
    return fv;
}

inline int FormulaValue::ArrayRows() const { return array ? array->rows : 1; }
inline int FormulaValue::ArrayCols() const { return array ? array->cols : 1; }

inline const FormulaValue& FormulaValue::ArrayAt(int r, int c) const {
    if (!array) return *this;
    return array->At(r, c);
}

inline FormulaValue FormulaValue::TopLeft() const {
    if (array && !array->cells.empty()) return array->cells.front().TopLeft();
    if (lambda) return Error(CellErrorType::Calc);
    return *this;
}

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
    FormulaToken ReadQuotedSheetReference();
    // An error literal (#N/A, #DIV/0!, #VALUE!, ...). A '#' that starts none
    // is consumed as #NAME?, so the tokenizer always moves on.
    FormulaToken ReadErrorLiteral();
    
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
    Array,          // Array literal {1,2,3;4,5,6}: children row by row, arrayRows x arrayCols
    SpillRef,       // A1#: the array spilled from cellRef
    Invoke          // children[0] called with children[1..]: LAMBDA(x, x*2)(5)
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

    // For array literals
    int arrayRows = 0;
    int arrayCols = 0;

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
    // {1,2;3,4}: constants (a sign allowed), ',' between columns, ';' between rows.
    std::unique_ptr<FormulaNode> ParseArrayLiteral();
    // The arguments of a call after its '(' up to and with its ')'; an
    // argument left out (SEQUENCE(3,,10)) is an empty literal.
    bool ParseArguments(std::vector<std::unique_ptr<FormulaNode>>& args);

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
    std::vector<std::string> functionNames_;  // every function it calls, as written
    bool readsNamedRanges_ = false;
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

    // The functions it calls (NOW, SUM ...), and whether it names a range -
    // which may cover any cell, so such a formula is recalculated on any change.
    const std::vector<std::string>& GetFunctionNames() const { return functionNames_; }
    bool ReadsNamedRanges() const { return readsNamedRanges_; }
    
    // Get AST (for evaluation)
    const FormulaNode* GetAST() const { return ast_.get(); }
    
    // A formula's text is moved by ShiftFormulaReferences (copied or filled)
    // and MoveFormulaReferencesForRowsColumns (rows or columns inserted or
    // deleted), both in UltraCanvasSpreadsheetSheet.h, and parsed again.
    
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
    // Takes single values only, so a range or an array given for one of its
    // arguments calls it once per element and gives an array of the results
    // (=ABS(A1:A3), =IF(B1:B5>0, "+", "-")), as Excel does. Set for the
    // functions in ApplyArrayBehaviour's table. Last, so the brace-initialised
    // definitions keep their order.
    bool elementWise = false;
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

    // The call as a user writes it, with the argument names Excel uses:
    // "ROUND(number, num_digits)", "IF(logical_test, value_if_true,
    // [value_if_false])", "SUM(number1, [number2], ...)". Empty for an
    // unknown function.
    std::string GetSignature(const std::string& name) const;
    // The same, one string per argument ("number", "[num_digits]"), with
    // "..." last when the function takes more arguments than it names.
    std::vector<std::string> GetSignatureArguments(const std::string& name) const;
    
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
    // The Excel-compatible set (UltraCanvasSpreadsheetFormulaFunctions.cpp):
    // lookups, conditional aggregation, rounding, statistics, character-wise
    // text functions, dates, IS* and the newer logical functions.
    void RegisterExcelFunctions();
    // Gives every built-in function its argument names (one table, in
    // UltraCanvasSpreadsheetFormulaFunctions.cpp); run after registration.
    void ApplyArgumentNames();
    // Marks the functions that take single values only (elementWise); run
    // after registration.
    void ApplyArrayBehaviour();
};

// The function call the caret of a formula being typed is in: its name (upper
// case) and the zero-based argument the caret is at. For "=ROUND(A1/B1, |"
// that is ROUND and 1. Strings, quoted sheet names and array constants are
// skipped; a bracket without a name before it (a group) belongs to the call
// around it. False when the caret is in no call.
bool FindFormulaCallAtCaret(const std::string& formula, size_t caret,
                            std::string& functionName, int& argumentIndex);

// ============================================================================
// FORMULA EVALUATOR
// ============================================================================

// One argument of the function call being evaluated, in the order written.
// A function's implementation receives its value arguments and its range
// arguments in two separate lists, and each range flattened; a function that
// needs the order, or a range's rows and columns (VLOOKUP, SUMIF, INDEX),
// reads them here through FormulaEvaluator::GetCallArguments().
struct FunctionCallArgument {
    bool isRange = false;
    size_t index = 0;     // into the value list or the range list
    CellRange range;      // the range itself, when isRange
};

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
    SpreadsheetSheet* GetCurrentSheet() const { return currentSheet_; }

    // Called when a reference reaches a formula cell whose result is not
    // current (its formula is dirty), so that cell is calculated before it is
    // read - whatever order the cells are visited in. The engine installs it;
    // without one a dirty cell is read as it stands.
    using FormulaCellResolver = std::function<void(SpreadsheetCell*, SpreadsheetSheet*)>;
    void SetFormulaCellResolver(FormulaCellResolver resolver) { resolveFormulaCell_ = std::move(resolver); }
    
    // Evaluate formula. The result is one value: an array result gives its
    // top-left element (what conditional formats and validations compare).
    FormulaValue Evaluate(const SpreadsheetFormula& formula);
    // A node's value - an array where the expression gives one.
    FormulaValue Evaluate(const FormulaNode* node);
    // The formula of the cell `at` (on the current sheet), an array where it
    // gives one - what the engine spills. With `implicitIntersection` (a
    // formula read from a file that has no dynamic arrays) a range where one
    // value is expected gives the cell on the formula's row or column, as
    // Excel before dynamic arrays and LibreOffice read it.
    FormulaValue EvaluateArrayFormula(const SpreadsheetFormula& formula, const CellAddress& at,
                                      bool implicitIntersection);

    // The cells spilled from `anchor` (A1#): the anchor and its spill. Not
    // valid when the cell spills nothing (A1# is then #REF!).
    CellRange ResolveSpillRange(const CellAddress& anchor) const;
    
    // Get cell value
    FormulaValue GetCellValue(const CellAddress& addr) const;
    
    // Get range values
    std::vector<std::vector<FormulaValue>> GetRangeValues(const CellRange& range) const;
    
    // Flatten range to single array
    std::vector<FormulaValue> FlattenRange(const CellRange& range) const;
    
    // Get named range
    CellRange GetNamedRange(const std::string& name) const;
    // The error a name that gives no range stands for: #REF! when it names a
    // range on a sheet since deleted, #NAME? when there is no such name.
    CellErrorType NamedRangeError(const std::string& name) const;

    // The arguments of the function call whose implementation is running,
    // in the order written (empty outside a call).
    const std::vector<FunctionCallArgument>& GetCallArguments() const { return callArguments_; }
    
    // Calls a LAMBDA value with these arguments (the lambda functions use it).
    FormulaValue CallLambda(const FormulaValue& lambda, const std::vector<FormulaValue>& arguments);

private:
    std::vector<FunctionCallArgument> callArguments_;
    FormulaCellResolver resolveFormulaCell_;

    // The cell whose formula is evaluated, for implicit intersection.
    CellAddress formulaCell_{-1, -1};
    bool implicitIntersection_ = false;
    // Inside an argument of a function that takes ranges and arrays, where a
    // range stays a range even with implicit intersection.
    bool arrayContext_ = false;
    // The names LET (and a called LAMBDA) give, innermost last, upper case.
    std::vector<std::pair<std::string, FormulaValue>> localNames_;

    FormulaValue EvaluateLiteral(const FormulaNode* node);
    FormulaValue EvaluateCellRef(const FormulaNode* node);
    FormulaValue EvaluateRangeRef(const FormulaNode* node);
    FormulaValue EvaluateBinaryOp(const FormulaNode* node);
    FormulaValue EvaluateUnaryOp(const FormulaNode* node);
    FormulaValue EvaluateFunction(const FormulaNode* node);
    FormulaValue EvaluateArrayLiteral(const FormulaNode* node);
    FormulaValue EvaluateInvoke(const FormulaNode* node);
    // LET(name1, value1, ..., calculation) and LAMBDA(param..., body), which
    // decide themselves what of their arguments to evaluate.
    FormulaValue EvaluateLet(const FormulaNode* node);
    FormulaValue EvaluateLambda(const FormulaNode* node);
    // A range's cells as a value: an array, or one value for one cell (or the
    // cell where the formula's row or column crosses it, when intersecting).
    FormulaValue RangeValue(const CellRange& range);
    const FormulaValue* FindLocalName(const std::string& name) const;
    // `fn` applied element by element over operands broadcast to one shape:
    // a single row or column stretches over the other's rows or columns;
    // positions one operand does not reach are #N/A.
    static FormulaValue ElementWise(const std::vector<FormulaValue>& operands,
                                    const std::function<FormulaValue(const std::vector<FormulaValue>&)>& fn);
    FormulaValue ApplyBinary(const std::string& op, const FormulaValue& left, const FormulaValue& right);
    
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

    // What changed since the last Recalculate, by sheet name: Recalculate
    // starts from these and follows every formula that reads them. Past
    // kMaxChangedRanges entries (a file loaded cell by cell), or after a
    // structural change, everything is recalculated instead.
    static constexpr size_t kMaxChangedRanges = 4096;
    std::vector<std::pair<std::string, CellRange>> changedRanges_;
    bool recalculateAllPending_ = false;

    // The cells being calculated right now, outermost first: a formula that
    // reaches one of them again is circular and reads its current value.
    std::unordered_set<const SpreadsheetCell*> evaluating_;
    // A chain of references went deeper than one nested calculation follows;
    // RecalculateAll then makes another pass.
    bool nestingLimitReached_ = false;
    
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
    // Cells of `sheetName` whose content changed: the formulas among them,
    // and every formula that reads them (directly or through others), are
    // recalculated by the next Recalculate.
    void MarkRangeDirty(const CellRange& range, const std::string& sheetName);
    // Rows or columns moved, or a sheet was replaced: the next Recalculate
    // recalculates everything (RecalculateAll).
    void MarkAllDirty() { recalculateAllPending_ = true; changedRanges_.clear(); }
    // Something was marked and not recalculated yet.
    bool HasPendingChanges() const { return recalculateAllPending_ || !changedRanges_.empty(); }
    
    // Recalculates the formulas whose result may have changed: those marked
    // (or set) since the last call, those that read a changed cell - through
    // any chain of other formulas, on any sheet - those that name a range,
    // and the volatile ones (NOW, TODAY, RAND ...).
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

    // Puts a formula's result into its cell - and an array result into the
    // cells it covers: a fixed array formula (Ctrl+Shift+Enter, t="array"
    // without dynamic-array metadata) fills its range, a dynamic one spills
    // right and down, or shows #SPILL! where a cell in the way is not empty.
    void StoreResult(SpreadsheetCell* cell, SpreadsheetSheet* sheet, const FormulaValue& result);
    // The cells `anchor` filled last time and fills no more are emptied.
    void ClearSpill(SpreadsheetSheet* sheet, SpreadsheetCell* anchor, const CellRange& keep);
    // The spilled values whose formula no longer covers them (it was edited,
    // deleted, or now spills less) are emptied; returns where.
    std::vector<std::pair<const SpreadsheetSheet*, CellRange>> ClearOrphanedSpills();
    // Spills that moved, grew or were blocked or freed in a calculation:
    // RecalculateAll runs again while this changes.
    int spillLayoutChanges_ = 0;
    int recalculateDepth_ = 0;
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
    
    // A formula is stored in the document, so its literals are dot-decimal
    // whatever the reader's locale is.
    double literal = 0.0;
    TryParseFloat(numStr, literal);
    return FormulaToken::Number(literal, startPos);
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
            FormulaToken ref = FormulaToken::CellReference(addr, startPos);
            if (Peek() == '#') {   // A1#: the array spilled from A1
                Advance();
                ref.type = FormulaTokenType::SpillRef;
            }
            return ref;
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

inline FormulaToken FormulaTokenizer::ReadQuotedSheetReference() {
    // Reads a sheet-qualified reference whose sheet name is single-quoted, e.g.
    // 'Sheet Name'.B55 or 'Sheet Name'.B4:C40. ODF quotes any sheet name that
    // contains spaces or special characters; the plain ReadIdentifier path only
    // handles unquoted, space-free names. We reassemble the textual reference and
    // let CellAddress/CellRange::FromString interpret it (they already strip the
    // surrounding quotes and split the sheet from the cell on the '.').
    int startPos = static_cast<int>(pos_);

    auto readQuotedName = [&]() -> std::string {
        std::string q;
        q += Advance();  // opening quote
        while (pos_ < formula_.size()) {
            char c = Advance();
            if (c == '\'') {
                if (Peek() == '\'') { q += "''"; Advance(); continue; }  // '' escapes a quote
                q += '\'';
                break;
            }
            q += c;
        }
        return q;
    };
    auto readCellPart = [&]() -> std::string {
        std::string p;
        while (pos_ < formula_.size() && (IsAlphaNum(Peek()) || Peek() == '$')) {
            p += Advance();
        }
        return p;
    };

    std::string startStr = readQuotedName();       // 'Sheet Name'
    if (Peek() == '.') startStr += Advance();       // sheet/cell separator
    startStr += readCellPart();                     // 'Sheet Name'.B4

    if (Peek() == ':') {
        Advance();  // consume ':'
        std::string endStr;
        if (Peek() == '\'') {                       // 'S1'.A1:'S2'.B2
            endStr = readQuotedName();
            if (Peek() == '.') endStr += Advance();
            endStr += readCellPart();
        } else {
            if (Peek() == '.') Advance();           // tolerate a bare leading '.'
            endStr = readCellPart();
        }
        CellAddress start = CellAddress::FromString(startStr);
        CellAddress end = CellAddress::FromString(endStr);
        if (end.sheetName.empty()) end.sheetName = start.sheetName;
        return FormulaToken::RangeReference(CellRange(start, end), startPos);
    }

    FormulaToken ref = FormulaToken::CellReference(CellAddress::FromString(startStr), startPos);
    if (Peek() == '#') {   // 'Sheet'.A1#
        Advance();
        ref.type = FormulaTokenType::SpillRef;
    }
    return ref;
}

inline FormulaToken FormulaTokenizer::ReadErrorLiteral() {
    const int startPos = static_cast<int>(pos_);
    static const CellErrorType kErrors[] = {
        CellErrorType::GettingData, CellErrorType::DivisionByZero, CellErrorType::ValueError,
        CellErrorType::ReferenceError, CellErrorType::NameError, CellErrorType::NumError,
        CellErrorType::NullError, CellErrorType::NAError, CellErrorType::Spill,
        CellErrorType::Connect, CellErrorType::Blocked, CellErrorType::Calc};
    for (CellErrorType error : kErrors) {
        const std::string text = CellErrorToString(error);
        if (pos_ + text.size() > formula_.size()) continue;
        bool same = true;
        for (size_t i = 0; i < text.size() && same; ++i) {
            same = std::toupper(static_cast<unsigned char>(formula_[pos_ + i])) ==
                   static_cast<unsigned char>(text[i]);
        }
        if (same) {
            pos_ += text.size();
            return FormulaToken::Error(error, startPos);
        }
    }
    ++pos_;   // a lone '#' or an unknown error name
    while (pos_ < formula_.size() && (IsAlphaNum(Peek()) || Peek() == '/')) ++pos_;
    return FormulaToken::Error(CellErrorType::NameError, startPos);
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
        // Every pass consumes at least one character (see the end of the
        // loop): a reader that stopped without moving would spin forever.
        const size_t passStart = pos_;
        
        char c = Peek();
        int startPos = static_cast<int>(pos_);
        
        if (IsDigit(c) || (c == '.' && pos_ + 1 < formula_.size() && IsDigit(formula_[pos_ + 1]))) {
            tokens.push_back(ReadNumber());
        }
        else if (c == '"') {
            tokens.push_back(ReadString());
        }
        else if (c == '\'') {
            // Single-quoted, sheet-qualified reference: 'Sheet Name'.A1
            tokens.push_back(ReadQuotedSheetReference());
        }
        else if (c == '#') {
            tokens.push_back(ReadErrorLiteral());
        }
        else if (IsAlpha(c) || c == '$') {
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
        else if (c == '{') {
            tokens.push_back(FormulaToken(FormulaTokenType::LeftBrace, "{", startPos));
            ++pos_;
        }
        else if (c == '}') {
            tokens.push_back(FormulaToken(FormulaTokenType::RightBrace, "}", startPos));
            ++pos_;
        }
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' ||
                 c == '&' || c == '%' || c == '=' || c == '<' || c == '>' || c == '@') {
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
        if (pos_ == passStart) ++pos_;
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
    functionNames_.clear();
    readsNamedRanges_ = false;
    CollectDependencies(ast_.get());
    
    valid_ = true;
    return true;
}

inline void SpreadsheetFormula::CollectDependencies(const FormulaNode* node) {
    if (!node) return;
    
    switch (node->nodeType) {
        case FormulaNodeType::CellRef:
        case FormulaNodeType::SpillRef:   // A1# reads A1's whole spill, which changes with A1
            dependencies_.push_back(node->cellRef);
            break;
        case FormulaNodeType::RangeRef:
            rangeDependencies_.push_back(node->rangeRef);
            break;
        case FormulaNodeType::NamedRef:
            readsNamedRanges_ = true;
            break;
        case FormulaNodeType::FunctionCall:
            functionNames_.push_back(node->functionName);
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
    // A reference to a formula cell not yet calculated calculates it first,
    // in its own sheet's context, and then carries on in the caller's.
    evaluator_->SetFormulaCellResolver([this](SpreadsheetCell* cell, SpreadsheetSheet* sheet) {
        SpreadsheetSheet* caller = evaluator_->GetCurrentSheet();
        EvaluateCell(cell, sheet);
        evaluator_->SetCurrentSheet(caller);
    });
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
    MarkRangeDirty(CellRange(cell.row, cell.col, cell.row, cell.col), sheetName);
}

inline void SpreadsheetFormulaEngine::MarkRangeDirty(const CellRange& range, const std::string& sheetName) {
    if (recalculateAllPending_) return;
    if (!changedRanges_.empty() && changedRanges_.back().first == sheetName &&
        changedRanges_.back().second == range) {
        return;   // the grid and the sheet both report a single edit
    }
    if (changedRanges_.size() >= kMaxChangedRanges) {
        MarkAllDirty();
        return;
    }
    changedRanges_.emplace_back(sheetName, range);
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
