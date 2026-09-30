#pragma once

#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

namespace zima::document {

// Quantities are expressed in the document's units. Exponents: length, angle, mass.
struct RelationValue {
    std::variant<double, std::string, bool> data{0.0};
    std::array<int, 3> units{};
    bool literal{false};
};
struct RelationInput {
    RelationValue value;
    bool writable{false};
    bool calculated{false};
};
using RelationInputs = std::map<std::string, RelationInput>;
class RelationError : public std::invalid_argument {
public:
    RelationError(int line, int column, const std::string& message, std::string detail = {});
    int line;
    int column;
    std::string message;
    std::string detail;
};
struct RelationProgramData;
class RelationProgram {
public:
    explicit RelationProgram(const std::string& source);
    // All names, assignments and cycles are checked, including inactive branches.
    void validate(const RelationInputs&) const;
    [[nodiscard]] std::map<std::string, RelationValue> evaluate(const RelationInputs&, int decimals = 3, bool dimensions_only = false) const;
    [[nodiscard]] std::map<std::string, std::string> target_expressions() const;
private:
    std::shared_ptr<const RelationProgramData> data_;
};
[[nodiscard]] std::string relation_value_text(const RelationValue&, int decimals);
[[nodiscard]] std::string quote_relation_text(const std::string&);
} // namespace zima::document
