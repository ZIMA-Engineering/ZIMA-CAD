#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <nlohmann/json_fwd.hpp>

namespace zima::document {

[[nodiscard]] nlohmann::json serialize_body_result(
    const zima::kernel::BodyResult& result);
[[nodiscard]] zima::kernel::BodyResult load_body_result(
    const nlohmann::json& source);

}  // namespace zima::document
