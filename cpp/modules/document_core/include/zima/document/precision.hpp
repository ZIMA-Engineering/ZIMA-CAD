#pragma once

#include <charconv>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

namespace zima::document {

// Persisted numbers use a decimal point regardless of the UI/system locale.
inline double precision_value(const std::map<std::string, std::string>& values,
                              const std::string& key, double fallback) {
    const auto found = values.find(key);
    if (found == values.end()) return fallback;
    const auto& text = found->second;
    double value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(value)) {
        throw std::invalid_argument("Invalid document precision: " + key);
    }
    return value;
}

} // namespace zima::document
