#pragma once
#include <nlohmann/json.hpp>
#include <zima/kernel/dimension_layout.hpp>
namespace zima::document {
inline nlohmann::json dimension_text_style_json(const kernel::DimensionTextStyle& s) {
    return {{"prefix",s.prefix},{"suffix",s.suffix},{"text_override",s.text_override},{"decimals",s.decimals},
        {"tolerance_mode",s.tolerance_mode},{"symmetric_tolerance",s.symmetric_tolerance},{"single_tolerance",s.single_tolerance},
        {"upper_tolerance",s.upper_tolerance},{"lower_tolerance",s.lower_tolerance}};
}
inline kernel::DimensionTextStyle dimension_text_style_from_json(const nlohmann::json& j) {
    kernel::DimensionTextStyle s;s.prefix=j.at("prefix");s.suffix=j.at("suffix");s.text_override=j.at("text_override");s.decimals=j.at("decimals");
    s.tolerance_mode=j.at("tolerance_mode");s.symmetric_tolerance=j.at("symmetric_tolerance");s.single_tolerance=j.at("single_tolerance");s.upper_tolerance=j.at("upper_tolerance");s.lower_tolerance=j.at("lower_tolerance");
    if(s.decimals<0 || s.decimals>12)throw std::invalid_argument("Invalid dimension text precision");return s;
}
inline nlohmann::json dimension_layout_json(const kernel::DimensionLayout &v) {
    return {{"text_style",v.text_style?dimension_text_style_json(*v.text_style):nlohmann::json(nullptr)},
            {"plane_quarter_turns", v.plane_quarter_turns},
            {"radius_rotation_degrees",v.radius_rotation_degrees},
            {"envelope_offset",
             v.envelope_offset ? nlohmann::json(*v.envelope_offset) : nlohmann::json(nullptr)},
            {"text_along", v.text_along},
            {"text_outward", v.text_outward}, {"arrows_reversed", v.arrows_reversed}, {"line_offset", v.line_offset}, {"radius_center_line_hidden", v.radius_center_line_hidden}};
}
inline kernel::DimensionLayout dimension_layout_from_json(const nlohmann::json &j) {
    kernel::DimensionLayout v;
    if(j.contains("text_style")&&!j.at("text_style").is_null())v.text_style=dimension_text_style_from_json(j.at("text_style"));
    v.plane_quarter_turns = j.at("plane_quarter_turns");
    v.radius_rotation_degrees=j.value("radius_rotation_degrees",0.);
    if (!j.at("envelope_offset").is_null())
        v.envelope_offset = j.at("envelope_offset");
    v.text_along = j.at("text_along");
    v.text_outward = j.at("text_outward");
    v.arrows_reversed = j.value("arrows_reversed", false);
    v.line_offset = j.value("line_offset", 0.);
    v.radius_center_line_hidden = j.value("radius_center_line_hidden", false);
    kernel::validate_dimension_layout(v);
    return v;
}
inline nlohmann::json
dimension_layouts_json(const std::vector<kernel::DimensionLayoutEntry> &entries) {
    auto j = nlohmann::json::array();
    for (const auto &e : entries)
        j.push_back({{"owner_id", e.owner_id},
                     {"semantic_key", e.semantic_key},
                     {"layout", dimension_layout_json(e.layout)}});
    return j;
}
inline std::vector<kernel::DimensionLayoutEntry>
dimension_layouts_from_json(const nlohmann::json &j) {
    std::vector<kernel::DimensionLayoutEntry> entries;
    for (const auto &e : j) {
        kernel::EdgeReference ref{e.at("owner_id"), e.at("semantic_key"), {}};
        if (!ref.valid() || kernel::find_dimension_layout(entries, ref))
            throw std::invalid_argument("Invalid dimension layout identity");
        entries.push_back(
            {ref.owner_id, ref.semantic_key, dimension_layout_from_json(e.at("layout"))});
    }
    return entries;
}

inline nlohmann::json dimension_vec_json(kernel::Vec3 v) { return {v.x, v.y, v.z}; }
inline kernel::Vec3 dimension_vec_from_json(const nlohmann::json &j) {
    kernel::Vec3 v{j.at(0), j.at(1), j.at(2)};
    if (j.size() != 3 || !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        throw std::invalid_argument("Invalid annotation coordinate");
    return v;
}
inline nlohmann::json
annotation_frames_json(const std::map<kernel::ObjectEnvelopeKey, kernel::ModelEnvelope> &frames) {
    auto result = nlohmann::json::array();
    for (const auto &[key, f] : frames)
        result.push_back({{"owner_id", key.first},
                          {"instance_path", key.second},
                          {"origin", dimension_vec_json(f.origin)},
                          {"axes",
                           {dimension_vec_json(f.axes[0]), dimension_vec_json(f.axes[1]),
                            dimension_vec_json(f.axes[2])}},
                          {"minimum", dimension_vec_json(f.minimum)},
                          {"maximum", dimension_vec_json(f.maximum)},
                          {"valid", f.valid}});
    return result;
}
inline std::map<kernel::ObjectEnvelopeKey, kernel::ModelEnvelope>
annotation_frames_from_json(const nlohmann::json &rows) {
    std::map<kernel::ObjectEnvelopeKey, kernel::ModelEnvelope> result;
    for (const auto &row : rows) {
        kernel::ModelEnvelope f;
        f.origin = dimension_vec_from_json(row.at("origin"));
        for (int i = 0; i < 3; ++i)
            f.axes[i] = dimension_vec_from_json(row.at("axes").at(i));
        f.minimum = dimension_vec_from_json(row.at("minimum"));
        f.maximum = dimension_vec_from_json(row.at("maximum"));
        f.valid = row.at("valid");
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (std::abs(kernel::dimension_dot(f.axes[i], f.axes[j]) - (i == j ? 1. : 0.)) >
                    1e-6)
                    throw std::invalid_argument("Invalid annotation frame basis");
        if (f.valid &&
            (f.minimum.x > f.maximum.x || f.minimum.y > f.maximum.y || f.minimum.z > f.maximum.z))
            throw std::invalid_argument("Invalid annotation bounds");
        if (!result
                 .emplace(kernel::ObjectEnvelopeKey{row.at("owner_id"), row.at("instance_path")}, f)
                 .second)
            throw std::invalid_argument("Duplicate annotation frame");
    }
    return result;
}
inline nlohmann::json dimension_geometry_json(const kernel::ViewerDimension &d) {
    return {{"source_text_style",d.source_text_style?dimension_text_style_json(*d.source_text_style):nlohmann::json(nullptr)},
            {"witness_first", dimension_vec_json(d.witness_first)},
            {"witness_second", dimension_vec_json(d.witness_second)},
            {"line_first", dimension_vec_json(d.line_first)},
            {"line_second", dimension_vec_json(d.line_second)},
            {"normal", dimension_vec_json(d.plane_normal)},
            {"label",
             d.label_position ? dimension_vec_json(*d.label_position) : nlohmann::json(nullptr)},
            {"value", d.value},
            {"sweep", d.sweep_degrees},
            {"kind", static_cast<int>(d.kind)},
            {"owner_id", d.reference.owner_id},
            {"semantic_key", d.reference.semantic_key},
            {"instance_path", d.reference.instance_path},
            {"prefix", d.label_prefix},
            {"suffix", d.unit_suffix},
            {"text", d.display_text_override},
            {"lock_key", d.value_lock_key},
            {"driving", d.driving},
            {"locked", d.locked}, {"arrows_reversed", d.arrows_reversed}, {"radius_center_line_hidden", d.radius_center_line_hidden},
            {"participants", d.participant_semantic_keys}};
}
inline kernel::ViewerDimension dimension_geometry_from_json(const nlohmann::json &j) {
    kernel::ViewerDimension d;
    if(j.contains("source_text_style")&&!j.at("source_text_style").is_null())d.source_text_style=dimension_text_style_from_json(j.at("source_text_style"));
    d.witness_first = dimension_vec_from_json(j.at("witness_first"));
    d.witness_second = dimension_vec_from_json(j.at("witness_second"));
    d.line_first = dimension_vec_from_json(j.at("line_first"));
    d.line_second = dimension_vec_from_json(j.at("line_second"));
    d.plane_normal = dimension_vec_from_json(j.at("normal"));
    if (!j.at("label").is_null())
        d.label_position = dimension_vec_from_json(j.at("label"));
    d.value = j.at("value");
    d.sweep_degrees = j.at("sweep");
    d.kind = static_cast<kernel::ViewerDimensionKind>(j.at("kind").get<int>());
    d.reference = {j.at("owner_id"), j.at("semantic_key"), j.at("instance_path")};
    d.label_prefix = j.at("prefix");
    d.unit_suffix = j.at("suffix");
    d.display_text_override = j.at("text");
    d.value_lock_key = j.at("lock_key");
    d.driving = j.at("driving");
    d.locked = j.at("locked");
    d.arrows_reversed = j.value("arrows_reversed", false);
    d.radius_center_line_hidden = j.value("radius_center_line_hidden", false);
    d.participant_semantic_keys = j.at("participants").get<std::vector<std::string>>();
    return d;
}
} // namespace zima::document
