#pragma once

#include <zima/kernel/occt_kernel.hpp>

namespace zima::test {

inline kernel::BodyResult analyzed_body(
    const kernel::OcctKernel& engine, kernel::BodyResult body) {
    engine.ensure_body_properties(body);
    return body;
}

inline double measured_volume(
    const kernel::OcctKernel& engine, kernel::BodyResult body) {
    try {
        engine.ensure_body_properties(body);
        return body.volume;
    } catch (const std::invalid_argument&) {
        if (body.body_outputs.empty()) {
            throw;
        }
        double total{};
        for (const auto& [id, snapshot] : body.body_outputs) {
            static_cast<void>(id);
            auto output = snapshot.get();
            engine.ensure_body_properties(output);
            total += output.volume;
        }
        return total;
    }
}

inline double measured_surface_area(
    const kernel::OcctKernel& engine, kernel::BodyResult body) {
    try {
        engine.ensure_body_properties(body);
        return body.surface_area;
    } catch (const std::invalid_argument&) {
        if (body.body_outputs.empty()) {
            throw;
        }
        double total{};
        for (const auto& [id, snapshot] : body.body_outputs) {
            static_cast<void>(id);
            auto output = snapshot.get();
            engine.ensure_body_properties(output);
            total += output.surface_area;
        }
        return total;
    }
}

}  // namespace zima::test
