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

// Evaluates relations in order. The input map is changed only after every
// expression succeeds, so a failed OK remains transactional.
[[nodiscard]] std::map<std::string, std::string> evaluate_relations(
    const std::map<std::string, std::string>& parameters,
    const std::vector<ModelRelation>& relations,
    const std::map<std::string, double>& model_values = {},
    int decimal_places = 3);

}  // namespace zima::document
