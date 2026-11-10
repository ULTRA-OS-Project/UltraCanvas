// Apps/UltraAIApp/UltraAIAgentTools.cpp
// Version: 0.1.0

#include "UltraAIAgentTools.h"

#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic
#include "UltraLocalisation/UltraLocalisation.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <random>

namespace UltraAIApp {

using namespace UltraAI;

namespace {

// A recursive-descent reader for the calculate tool:
//   expr   := term (('+' | '-') term)*
//   term   := power (('*' | '/' | '%') power)*
//   power  := unary ('^' power)?
//   unary  := '-' unary | '+' unary | atom
//   atom   := number | '(' expr ')'
class Arithmetic {
public:
    explicit Arithmetic(const std::string& text) : text_(text) {}

    std::optional<double> Evaluate(std::string& error) {
        double value = 0;
        if (!Expr(value)) { error = error_; return std::nullopt; }
        Skip();
        if (pos_ != text_.size()) {
            error = "unexpected '" + std::string(1, text_[pos_]) + "'";
            return std::nullopt;
        }
        if (!std::isfinite(value)) { error = "the result is not a finite number"; return std::nullopt; }
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;
    int depth_ = 0;
    std::string error_;

    void Skip() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) ++pos_;
    }
    bool Peek(char c) {
        Skip();
        return pos_ < text_.size() && text_[pos_] == c;
    }
    bool Fail(const std::string& message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool Expr(double& out) {
        if (!Term(out)) return false;
        for (;;) {
            if (Peek('+')) { ++pos_; double r; if (!Term(r)) return false; out += r; }
            else if (Peek('-')) { ++pos_; double r; if (!Term(r)) return false; out -= r; }
            else return true;
        }
    }
    bool Term(double& out) {
        if (!Power(out)) return false;
        for (;;) {
            if (Peek('*')) { ++pos_; double r; if (!Power(r)) return false; out *= r; }
            else if (Peek('/') || Peek('%')) {
                const bool mod = text_[pos_] == '%';
                ++pos_;
                double r;
                if (!Power(r)) return false;
                if (r == 0) return Fail("division by zero");
                out = mod ? std::fmod(out, r) : out / r;
            }
            else return true;
        }
    }
    bool Power(double& out) {
        if (!Unary(out)) return false;
        if (Peek('^')) {
            ++pos_;
            double r;
            if (!Power(r)) return false;      // right-associative
            out = std::pow(out, r);
        }
        return true;
    }
    bool Unary(double& out) {
        if (Peek('-')) { ++pos_; if (!Unary(out)) return false; out = -out; return true; }
        if (Peek('+')) { ++pos_; return Unary(out); }
        return Atom(out);
    }
    bool Atom(double& out) {
        Skip();
        if (pos_ >= text_.size()) return Fail("the expression ends too early");
        if (text_[pos_] == '(') {
            if (++depth_ > 64) return Fail("too many parentheses");
            ++pos_;
            if (!Expr(out)) return false;
            if (!Peek(')')) return Fail("a ')' is missing");
            ++pos_;
            --depth_;
            return true;
        }
        const size_t start = pos_;
        while (pos_ < text_.size() &&
               ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.')) {
            ++pos_;
        }
        if (start == pos_) {
            return Fail("unexpected '" + std::string(1, text_[pos_]) + "'");
        }
        // Dot-decimal whatever the locale: the model writes 2.5, not 2,5.
        if (!UltraCanvas::TryParseFloat(text_.substr(start, pos_ - start), out)) {
            return Fail("'" + text_.substr(start, pos_ - start) + "' is not a number");
        }
        return true;
    }
};

std::string LocalTimeText() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S (%A), UTC offset %z", &local);
    return buffer;
}

ToolResult TextStatistics(const ToolCall& call) {
    ToolArguments args(call.argumentsJson);
    auto text = args.String("text");
    if (!text) return ToolResult::Fail("the \"text\" argument is missing");
    size_t characters = 0, words = 0, lines = text->empty() ? 0 : 1, sentences = 0;
    bool inWord = false;
    for (size_t i = 0; i < text->size(); ++i) {
        const unsigned char c = static_cast<unsigned char>((*text)[i]);
        if ((c & 0xC0) != 0x80) ++characters;          // count code points, not bytes
        if (c == '\n') ++lines;
        if (c == '.' || c == '!' || c == '?') ++sentences;
        const bool space = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!space && !inWord) ++words;
        inWord = !space;
    }
    return ToolResult::Ok("{\"characters\":" + std::to_string(characters) +
                          ",\"words\":" + std::to_string(words) +
                          ",\"lines\":" + std::to_string(lines) +
                          ",\"sentences\":" + std::to_string(sentences) + "}");
}

ToolResult RandomNumber(const ToolCall& call) {
    ToolArguments args(call.argumentsJson);
    const int64_t low = args.Integer("min").value_or(1);
    const int64_t high = args.Integer("max").value_or(100);
    if (low > high) return ToolResult::Fail("\"min\" is larger than \"max\"");
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<int64_t> pick(low, high);
    return ToolResult::Ok(std::to_string(pick(engine)));
}

} // namespace

std::optional<double> EvaluateArithmetic(const std::string& expression, std::string* error) {
    std::string message;
    Arithmetic reader(expression);
    auto value = reader.Evaluate(message);
    if (!value && error) *error = message;
    return value;
}

std::vector<DemoTool> DemoTools() {
    std::vector<DemoTool> tools;

    DemoTool time;
    time.id = "current_time";
    time.label = UltraCanvas::Tr("Clock");
    time.tool.definition = {"current_time",
        "The current local date, time, weekday and UTC offset on the user's computer.",
        R"({"type":"object","properties":{}})"};
    time.tool.handler = [](const ToolCall&) { return ToolResult::Ok(LocalTimeText()); };
    tools.push_back(std::move(time));

    DemoTool calc;
    calc.id = "calculate";
    calc.label = UltraCanvas::Tr("Calculator");
    calc.tool.definition = {"calculate",
        "Evaluate an arithmetic expression exactly. Supports + - * / % ^ and "
        "parentheses, with '.' as the decimal point, e.g. \"(17.5 * 3) ^ 2 / 7\". "
        "Use it for any arithmetic instead of working it out yourself.",
        R"({"type":"object","properties":{"expression":{"type":"string",)"
        R"("description":"The expression to evaluate."}},"required":["expression"]})"};
    calc.tool.handler = [](const ToolCall& call) {
        ToolArguments args(call.argumentsJson);
        auto expression = args.String("expression");
        if (!expression) return ToolResult::Fail("the \"expression\" argument is missing");
        std::string error;
        auto value = EvaluateArithmetic(*expression, &error);
        if (!value) return ToolResult::Fail(error);
        return ToolResult::Ok(UltraCanvas::FormatFloatClassic(*value, 15));
    };
    tools.push_back(std::move(calc));

    DemoTool stats;
    stats.id = "text_statistics";
    stats.label = UltraCanvas::Tr("Text statistics");
    stats.tool.definition = {"text_statistics",
        "Count the characters, words, lines and sentences of a text.",
        R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"]})"};
    stats.tool.handler = TextStatistics;
    tools.push_back(std::move(stats));

    DemoTool random;
    random.id = "random_number";
    random.label = UltraCanvas::Tr("Random numbers");
    random.tool.definition = {"random_number",
        "A random whole number between min and max, both included (default 1 to 100).",
        R"({"type":"object","properties":{"min":{"type":"integer"},"max":{"type":"integer"}}})"};
    random.tool.handler = RandomNumber;
    tools.push_back(std::move(random));

    return tools;
}

} // namespace UltraAIApp
