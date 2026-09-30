#pragma once

#include <string>

namespace zima::document {

// Finite arithmetic only: numbers, +, -, *, / and parentheses. No model
// lookup or mutation; used by transient numeric editors before committing.
[[nodiscard]] double evaluate_numeric_expression(const std::string& expression);

}  // namespace zima::document
