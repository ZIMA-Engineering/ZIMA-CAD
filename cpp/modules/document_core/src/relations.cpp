#include <zima/document/relations.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace zima::document {
namespace {

class Parser {
public:
    Parser(std::string_view text, const std::map<std::string, double>& values)
        : text_(text), values_(values) {}

    double parse() {
        const double value = expression();
        spaces();
        if (position_ != text_.size()) fail("unexpected token");
        if (!std::isfinite(value)) fail("non-finite result");
        return value;
    }

private:
    std::string_view text_;
    const std::map<std::string, double>& values_;
    std::size_t position_{};

    [[noreturn]] void fail(const std::string& message) const {
        throw std::invalid_argument("Relation expression: " + message);
    }
    void spaces() { while (position_ < text_.size() && std::isspace(
        static_cast<unsigned char>(text_[position_]))) ++position_; }
    bool take(char character) {
        spaces();
        if (position_ < text_.size() && text_[position_] == character) {
            ++position_; return true;
        }
        return false;
    }
    double expression() {
        double value = term();
        while (true) {
            if (take('+')) value += term();
            else if (take('-')) value -= term();
            else return value;
        }
    }
    double term() {
        double value = power();
        while (true) {
            if (take('*')) value *= power();
            else if (take('/')) {
                const double divisor = power();
                if (divisor == 0.0) fail("division by zero");
                value /= divisor;
            } else if (take('%')) {
                const double divisor = power();
                if (divisor == 0.0) fail("division by zero");
                value = std::fmod(value, divisor);
            } else return value;
        }
    }
    double power() {
        double value = unary();
        if (take('^')) value = std::pow(value, power());
        return value;
    }
    double unary() {
        if (take('+')) return unary();
        if (take('-')) return -unary();
        return primary();
    }
    std::string identifier() {
        spaces();
        const auto begin = position_;
        while (position_ < text_.size()) {
            const unsigned char value = text_[position_];
            if (!std::isalnum(value) && value != '_' && value != '.') break;
            ++position_;
        }
        return std::string(text_.substr(begin, position_ - begin));
    }
    double primary() {
        spaces();
        if (take('(')) {
            const double value = expression();
            if (!take(')')) fail("missing ')'");
            return value;
        }
        if (position_ < text_.size() && (std::isdigit(
                static_cast<unsigned char>(text_[position_])) ||
                text_[position_] == '.')) {
            const char* begin = text_.data() + position_;
            char* end{};
            const double value = std::strtod(begin, &end);
            if (end == begin) fail("invalid number");
            position_ += static_cast<std::size_t>(end - begin);
            return value;
        }
        const std::string name = identifier();
        if (name.empty()) fail("expected value");
        if (take('(')) {
            std::vector<double> arguments;
            if (!take(')')) {
                do { arguments.push_back(expression()); } while (take(','));
                if (!take(')')) fail("missing ')' after function");
            }
            if (name == "abs" && arguments.size() == 1) return std::abs(arguments[0]);
            if (name == "sqrt" && arguments.size() == 1) return std::sqrt(arguments[0]);
            if (name == "sin" && arguments.size() == 1) return std::sin(arguments[0]);
            if (name == "cos" && arguments.size() == 1) return std::cos(arguments[0]);
            if (name == "tan" && arguments.size() == 1) return std::tan(arguments[0]);
            if (name == "min" && arguments.size() == 2) return std::min(arguments[0], arguments[1]);
            if (name == "max" && arguments.size() == 2) return std::max(arguments[0], arguments[1]);
            if (name == "round" && arguments.size() == 1) return std::round(arguments[0]);
            fail("unsupported function " + name);
        }
        const auto found = values_.find(name);
        if (found == values_.end()) fail("unknown name " + name);
        return found->second;
    }
};

double numeric(const std::string& text, const std::string& name) {
    std::size_t consumed{};
    const double value = std::stod(text, &consumed);
    if (consumed != text.size()) throw std::invalid_argument(
        "Parameter " + name + " is not numeric");
    return value;
}
}  // namespace

std::map<std::string, std::string> evaluate_relations(
    const std::map<std::string, std::string>& parameters,
    const std::vector<ModelRelation>& relations,
    const std::map<std::string, double>& model_values, int decimal_places) {
    auto output = parameters;
    std::map<std::string, double> values = model_values;
    for (const auto& [name, value] : parameters) {
        try { values[name] = numeric(value, name); } catch (const std::exception&) {}
    }
    for (const auto& relation : relations) {
        if (relation.target.empty() || relation.expression.empty())
            throw std::invalid_argument("Relation target and expression are required");
        const double result = Parser(relation.expression, values).parse();
        values[relation.target] = result;
        std::ostringstream rendered;
        rendered << std::fixed << std::setprecision(std::max(0, decimal_places)) << result;
        output[relation.target] = rendered.str();
    }
    return output;
}

}  // namespace zima::document
