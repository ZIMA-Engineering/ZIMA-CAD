#pragma once

#include <map>
#include <string>
#include <vector>

namespace zima::document {

struct ModelRelation {
    std::string target;
    std::string expression;
    bool operator==(const ModelRelation&) const = default;
};

void validate_model_relations(const std::vector<ModelRelation>&);

// Finite arithmetic only: numbers, +, -, *, / and parentheses. No model
// lookup or mutation; used by transient numeric editors before committing.
[[nodiscard]] double evaluate_numeric_expression(const std::string& expression);

// Evaluates relations in order. The input map is changed only after every
// expression succeeds, so a failed OK remains transactional.
[[nodiscard]] std::map<std::string, std::string> evaluate_relations(
    const std::map<std::string, std::string>& parameters,
    const std::vector<ModelRelation>& relations,
    const std::map<std::string, double>& model_values = {},
    int decimal_places = 3);

}  // namespace zima::document
