#pragma once

#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <set>
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
struct RelationUnitTarget {
    std::array<int,3> units{};
    double factor{1};
    int line{};
};
struct RelationUnitConversion {
    std::string source;
    std::map<std::string,RelationUnitTarget> outputs;
};
struct RelationProgramData;
class RelationProgram {
public:
    explicit RelationProgram(const std::string& source);
    // All names, assignments and cycles are checked, including inactive branches.
    void validate(const RelationInputs&) const;
    // Typed angles use the document's angular unit; scalar sin/cos/tan inputs
    // remain radians and scalar sind/cosd/tand inputs remain degrees.
    [[nodiscard]] std::map<std::string, RelationValue> evaluate(const RelationInputs&, int decimals = 3,
        bool dimensions_only = false, double degrees_per_angle_unit = 1) const;
    [[nodiscard]] std::map<std::string, std::string> target_expressions() const;
    // Factors convert old document numbers to new numbers (length, angle, mass).
    // Preflight every branch without evaluating domains or changing model data.
    // Explicit factors preserve rounding, function conventions and authored text.
    // Reserved names with an explicit fixed-unit storage contract are exempt.
    [[nodiscard]] RelationUnitConversion convert_units(const RelationInputs&,
        const std::array<double,3>& old_to_new,
        const std::set<std::string>& fixed_unit_names = {},
        double old_degrees_per_angle_unit = 1) const;
private:
    std::shared_ptr<const RelationProgramData> data_;
};
[[nodiscard]] std::string relation_value_text(const RelationValue&, int decimals);
[[nodiscard]] std::string quote_relation_text(const std::string&);
} // namespace zima::document
