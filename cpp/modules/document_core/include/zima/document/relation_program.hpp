#pragma once

#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <set>
#include <variant>
#include <vector>
#include <utility>

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
    // Preserve the authored source while disabling assignments whose persisted
    // dimension no longer exists, including their dependent targets. Other
    // validation and calculation errors retain their ordinary strict behavior.
    [[nodiscard]] RelationProgram without_missing_dimensions(const RelationInputs&,
        std::vector<RelationError>* skipped = nullptr) const;
    // All names, assignments and cycles are checked, including inactive branches.
    void validate(const RelationInputs&) const;
    // Typed angles use the document's angular unit; scalar sin/cos/tan inputs
    // remain radians and scalar sind/cosd/tand inputs remain degrees.
    [[nodiscard]] std::map<std::string, RelationValue> evaluate(const RelationInputs&, int decimals = 3,
        bool dimensions_only = false, double degrees_per_angle_unit = 1) const;
    // Refresh physical-result parameters against actual stored dimensions. Do
    // not execute dimension assignments or unrelated parameter expressions.
    [[nodiscard]] std::map<std::string, RelationValue> evaluate_physical_parameters(
        const RelationInputs&, int decimals = 3, double degrees_per_angle_unit = 1) const;
    [[nodiscard]] std::map<std::string, std::string> target_expressions() const;
    // Factors convert old document numbers to new numbers (length, angle, mass).
    // Preflight every branch without evaluating domains or changing model data.
    // Explicit factors preserve rounding, function conventions and authored text.
    // Reserved names with an explicit fixed-unit storage contract are exempt.
    [[nodiscard]] RelationUnitConversion convert_units(const RelationInputs&,
        const std::array<double,3>& old_to_new,
        const std::set<std::string>& fixed_unit_names = {},
        double old_degrees_per_angle_unit = 1) const;
    // Re-express explicitly named numeric quantities in another signed basis.
    // Reads, conditional guards and writes are transformed through the AST;
    // comments, strings and unrelated identifiers retain their authored text.
    [[nodiscard]] std::string reexpress_values(const std::map<std::string,double>& old_to_new) const;
private:
    explicit RelationProgram(std::shared_ptr<const RelationProgramData> data):data_(std::move(data)){}
    [[nodiscard]] std::map<std::string, RelationValue> evaluate_impl(const RelationInputs&,
        int, bool dimensions_only, double degrees_per_angle_unit, bool physical_only) const;
    std::shared_ptr<const RelationProgramData> data_;
};
[[nodiscard]] std::string relation_value_text(const RelationValue&, int decimals);
[[nodiscard]] std::string quote_relation_text(const std::string&);
} // namespace zima::document
