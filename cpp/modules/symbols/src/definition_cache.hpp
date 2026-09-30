#pragma once
#include <zima/symbols/definition.hpp>
#include <memory>
namespace zima::symbols::detail {
// Immutable parsed data only. Evaluation still consumes current variant/text/font inputs.
[[nodiscard]] std::shared_ptr<const Definition> parsed_definition(const std::string& source);
}
