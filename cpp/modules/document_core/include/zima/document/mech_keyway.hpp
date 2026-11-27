#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/sketcher/sketch.hpp>
#include <nlohmann/json_fwd.hpp>

namespace zima::document {

enum class KeywayType { Parallel, Woodruff };
enum class KeywayEnd { Round, Open };
enum class KeywayPosition { FromReference, Symmetric, TwoSided };
enum class KeywayTermination { Length, UpTo };
enum class KeywayFloor { ParallelToAxis, ParallelToGenerator };

struct KeywayAxialLimit {
    KeywayTermination termination{KeywayTermination::Length};
    double length{}, correction{};
    kernel::FaceReference reference;
    bool operator==(const KeywayAxialLimit&) const = default;
};

// Authored dimensions are millimetres/degrees. A cone's diameter and radial
// depth are specified at measurement_station, measured from its source origin.
// Extents describe the intended key, never the cutter's artificial overrun.
struct MechKeywayParameters {
    kernel::FaceReference support;
    // Forward and reverse limits from the same authored axial origin.
    // A positive correction extends outward beyond the selected end plane.
    std::array<KeywayAxialLimit,2> limits{{{KeywayTermination::Length,45},{}}};
    KeywayType type{KeywayType::Parallel};
    KeywayPosition position{KeywayPosition::FromReference};
    KeywayFloor floor{KeywayFloor::ParallelToAxis};
    KeywayEnd first_end{KeywayEnd::Round}, last_end{KeywayEnd::Round};
    bool internal{};
    double width{10}, key_height{8}, depth{4.7}, length{45};
    double axial_offset{}, angular_offset{}, measurement_station{};
    int axial_direction{1};
    double first_overrun{}, last_overrun{};
    double bottom_radius{}, woodruff_diameter{28};
    std::string table_id, table_row_id;
    std::string width_fit{"P9"};
    std::string depth_fit;
    std::string depth_upper_tolerance, depth_lower_tolerance;
    unsigned retaining_screws{};
    double retaining_diameter{3}, retaining_pitch{.5}, retaining_depth{8};
    // Symbol instances themselves own editable property values and definitions.
    std::string walls_symbol_id, floor_symbol_id;
    bool show_roughness{};
    bool operator==(const MechKeywayParameters&) const = default;
};

struct MechKeywayBinding {
    std::string parent_id;
    std::string role;
    bool operator==(const MechKeywayBinding&) const = default;
};

struct KeywayFrame {
    kernel::Vec3 origin, axis, radial, tangent;
    double radius{}, slope{};
    bool internal{};
};

// Consumes exact analytic support and an orientation vector supplied by the
// existing placement contract. It performs no OCCT calculation or mesh fitting.
[[nodiscard]] KeywayFrame keyway_frame(const kernel::SurfaceGeometry&,
    kernel::Vec3 orientation, double station, double angle_degrees);
void validate_mech_keyway(const MechKeywayParameters&);
[[nodiscard]] nlohmann::json serialize_mech_keyway(const MechKeywayParameters&);
[[nodiscard]] MechKeywayParameters load_mech_keyway(const nlohmann::json&);
[[nodiscard]] std::array<double,2> keyway_axial_extents(
    const MechKeywayParameters&, double reference_station,
    const std::array<std::optional<double>,2>& target_stations = {});

struct KeywayCutDefinition {
    sketcher::Sketch profile;
    // A generator-parallel conical hub is the exact ruled transition between
    // two perpendicular sections; all other cuts use the ordinary extrusion.
    std::vector<sketcher::Sketch> loft_sections;
    double forward{}, reverse{};
    double nominal_start{}, nominal_end{}, measurement{};
    std::vector<kernel::Vec3> retaining_centres;
};

// Stable semantic Sketch IDs survive parameter changes and decomposition.
// For a Parallel key the profile is the plan view; for Woodruff it is the
// circular cutter section, extruded symmetrically by the key width.
[[nodiscard]] KeywayCutDefinition keyway_cut_definition(
    const std::string& owner_id, const MechKeywayParameters&, const KeywayFrame&,
    double reference_station, const std::array<std::optional<double>,2>& target_stations = {});

// A real non-driving point-pair dimension. The compound lifecycle refreshes
// its references from the finished floor; the Sketch cannot drive the cut.
[[nodiscard]] sketcher::Sketch keyway_measurement_sketch(
    const std::string& owner_id, const MechKeywayParameters&, const KeywayFrame&);
}
