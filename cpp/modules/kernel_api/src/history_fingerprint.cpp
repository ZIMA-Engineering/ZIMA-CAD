#include <zima/kernel/geometry_kernel.hpp>
namespace zima::kernel {
namespace {
// Each prefix starts with its own count. Encode each operation once and feed
// its bytes to every prefix that contains it. Retain only O(operation count)
// hash state, not a duplicate stream of profiles or imported B-Rep data.
struct FingerprintBatch {std::vector<std::uint64_t> hashes;std::size_t skip{8},first{1};};
template<bool Capture>
std::string encode_history_fingerprint(
    const std::vector<HistoryOperation>& operations,
    std::size_t operation_count, FingerprintBatch* stream=nullptr) {
    // A dedicated byte stream keeps primitive kind part of the identity while
    // preserving the established Box fingerprint for existing tests.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto byte = [&](std::uint8_t value) {
        if constexpr(Capture) {
            if(stream->skip)--stream->skip;
            else for(std::size_t index=stream->first;index<stream->hashes.size();++index) {
                stream->hashes[index]^=value;stream->hashes[index]*=1099511628211ULL;
            }
        }
        else {hash ^= value;hash *= 1099511628211ULL;}
    };
    const auto u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    // Only rigid-frame coordinates/angles normalize signed zero. Do not use
    // this for authored dimensional parameters: a signed zero can encode a side.
    const auto number_bits=[](double value) {
        return std::bit_cast<std::uint64_t>(value==0.0?0.0:value);
    };
    operation_count = std::min(operation_count, operations.size());
    u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        if(operation.feature_copy) {
            HistoryOperation key;key.body=*operation.feature_copy;
            const auto copy_key=history_fingerprint({key},1);
            byte(0xc7);u64(copy_key.size());for(unsigned char c:copy_key)byte(c);
        }
        u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) byte(value);
        byte(static_cast<std::uint8_t>(operation.operation));
        byte(operation.suppressed ? 1U : 0U);
        if (!operation.input_error.empty()) {
            u64(operation.input_error.size());
            for (const unsigned char value : operation.input_error) byte(value);
        }
        u64(std::bit_cast<std::uint64_t>(operation.boolean_tolerance));
        u64(std::bit_cast<std::uint64_t>(operation.mesh_deflection));
        u64(static_cast<std::uint64_t>(operation.sheet_operation));
        u64(std::bit_cast<std::uint64_t>(operation.sheet_thickness));
        if(operation.sheet_material) {
            const auto& material=*operation.sheet_material;u64(1);u64(static_cast<unsigned>(material.kind));
            for(const auto* text:{&material.owner_id,&material.parent_owner_id,&material.curved_source_id,&material.continuation_source_id}) {
                u64(text->size());for(unsigned char c:*text)byte(c);
            }
            for(const auto vector:{material.origin,material.along,material.tangent,material.radial})
                for(double v:{vector.x,vector.y,vector.z})u64(std::bit_cast<std::uint64_t>(v));
            for(double v:{material.radius,material.neutral_radius,material.angle,material.thickness,material.continuation,material.cone_half_angle,material.thickness_sign})
                u64(std::bit_cast<std::uint64_t>(v));
            byte(material.unfolded);
        }
        if(!operation.sheet_regions.empty()) {
            byte(0xeb);u64(operation.sheet_regions.size());
            for(const auto& material:operation.sheet_regions) {
                HistoryOperation child;child.owner_id=material.owner_id;child.sheet_material=material;
                const auto digest=history_fingerprint({child},1);u64(digest.size());for(unsigned char c:digest)byte(c);
                u64(material.feature_owner_id.size());for(unsigned char c:material.feature_owner_id)byte(c);
            }
        }
        if (!operation.body.id.empty()) {
            for (const auto& text : {operation.body.id, operation.body.target_id, operation.body.source_id}) {
                u64(text.size());
                for (const unsigned char value : text) byte(value);
            }
            byte(static_cast<std::uint8_t>(operation.body.combination));
            byte(operation.body.result_suppressed);
            for (const auto value : {operation.body.translation.x, operation.body.translation.y,
                    operation.body.translation.z, operation.body.rotation_degrees.x,
                    operation.body.rotation_degrees.y, operation.body.rotation_degrees.z})
                u64(number_bits(value));
        }
        if(operation.body.combination==BodyCombination::Scale)
            for(double v:{operation.body.scale_factor,operation.body.scale_center.x,operation.body.scale_center.y,operation.body.scale_center.z})u64(number_bits(v));
        if(operation.body.linked_body) {
            const auto& stamp=operation.body.linked_body->source_fingerprint;
            u64(stamp.size());for(unsigned char c:stamp)byte(c);
        }
        if(operation.body.combination==BodyCombination::Mirror)
            for(double v:{operation.body.mirror_plane.point.x,operation.body.mirror_plane.point.y,operation.body.mirror_plane.point.z,
                    operation.body.mirror_plane.normal.x,operation.body.mirror_plane.normal.y,operation.body.mirror_plane.normal.z})
                u64(number_bits(v));
        if(!operation.body.source_feature_id.empty()) {
            u64(operation.body.source_feature_id.size());
            for(const unsigned char value:operation.body.source_feature_id)byte(value);
        }
        if(operation.body.combination==BodyCombination::Pattern) {
            const auto& p=operation.body.pattern;u64(p.count);byte(p.circular);byte(p.full_circle);
            u64(std::bit_cast<std::uint64_t>(p.angle_degrees));
            for(double v:{p.origin.x,p.origin.y,p.origin.z,p.axis.x,p.axis.y,p.axis.z})
                u64(number_bits(v));
            for (const auto& d : p.linear) {
                u64(d.local_axis + 1);u64(d.count);u64(d.reverse_count);byte(static_cast<std::uint8_t>(d.distribution));
                u64(std::bit_cast<std::uint64_t>(d.spacing));
                for (double v : {d.direction.x,d.direction.y,d.direction.z}) u64(number_bits(v));
            }
        }
        byte(static_cast<std::uint8_t>(operation.primitive.index()));
        std::visit([&](const auto& primitive) {
            using Request = std::decay_t<decltype(primitive)>;
            if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
                if(primitive.draft_angle_degrees!=0) {
                    for(unsigned char c:std::string_view("extrusion-draft-v1"))byte(c);
                    u64(std::bit_cast<std::uint64_t>(primitive.draft_angle_degrees));
                }
                byte(primitive.centerlines.origin_enabled);byte(primitive.centerlines.centroid_enabled);
                for(const auto& text:{primitive.centerlines.origin_id,primitive.centerlines.profile_id}){u64(text.size());for(unsigned char c:text)byte(c);}
                for(double value:{primitive.centerlines.origin.x,primitive.centerlines.origin.y,primitive.centerlines.origin.z,primitive.centerlines.normal.x,primitive.centerlines.normal.y,primitive.centerlines.normal.z})u64(std::bit_cast<std::uint64_t>(value));

                byte(primitive.sheet_cut);
                byte(primitive.sheet_cut_clearance);
                if(primitive.sheet_cut)u64(std::bit_cast<std::uint64_t>(primitive.sheet_cut_tolerance));
                if(primitive.surface_result) {byte(0xf1);for(const auto c:primitive.open_profile_end_id)byte(c);}
                // Exact profile bounds reject an inclined plane crossing away from seam vertices.
                if (primitive.extent == ExtrusionRequest::Extent::UpToPlane) byte(2);
                if (primitive.extent == ExtrusionRequest::Extent::UpToPlane ||
                    primitive.extent == ExtrusionRequest::Extent::UpToSurface || primitive.reverse_limit)
                    for (const unsigned char value : std::string_view("original-extrusion-limits-v1")) byte(value);
                const auto append_profile = [&](const auto& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices) {
                                for (const double value : {point.x, point.y, point.z}) {
                                    u64(std::bit_cast<std::uint64_t>(value));
                                }
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.major_axis_direction.x,
                                    profile.major_axis_direction.y,
                                    profile.major_axis_direction.z,
                                    profile.major_radius, profile.minor_radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    const auto append_point = [&](const Vec3& point) {
                                        for (const double value : {
                                                point.x, point.y, point.z}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                    };
                                    append_point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::ArcCurve>) {
                                        append_point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::EllipticalArcCurve>) {
                                        append_point(exact_curve.center);
                                        append_point(exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.knots.size());
                                        for (double v : exact_curve.knots) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.weights.size());
                                        for (double v : exact_curve.weights) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.degree);
                                        byte(exact_curve.interpolating);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& point : exact_curve.control_points) {
                                            append_point(point);
                                        }
                                    }
                                    append_point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                append_profile(primitive.outer_profile);
                u64(primitive.profile_region_id.size());
                for (const unsigned char value : primitive.profile_region_id) byte(value);
                u64(primitive.outer_boundary_id.size());
                for (const unsigned char value : primitive.outer_boundary_id) byte(value);
                u64(primitive.inner_boundary_ids.size());
                for (const auto& id : primitive.inner_boundary_ids) {
                    u64(id.size()); for (const unsigned char value : id) byte(value);
                }
                const auto append_source_ids = [&](const auto& groups) {
                    u64(groups.size());
                    for (const auto& group : groups) {
                        u64(group.size());
                        for (const auto& id : group) {
                            u64(id.size());
                            for (const unsigned char value : id) byte(value);
                        }
                    }
                };
                // One group, viewed without copying its persisted identities.
                append_source_ids(std::span{&primitive.outer_edge_source_ids, 1});
                append_source_ids(primitive.inner_edge_source_ids);
                append_source_ids(std::span{&primitive.outer_vertex_source_ids, 1});
                append_source_ids(primitive.inner_vertex_source_ids);
                u64(primitive.inner_profiles.size());
                for (const auto& profile : primitive.inner_profiles) {
                    append_profile(profile);
                }
                u64(primitive.additional_profile_regions.size());
                for (const auto& region : primitive.additional_profile_regions) {
                    u64(region.region_id.size());
                    for (const unsigned char value : region.region_id) byte(value);
                    u64(region.outer_boundary_id.size());
                    for (const unsigned char value : region.outer_boundary_id) byte(value);
                    u64(region.inner_boundary_ids.size());
                    for (const auto& id : region.inner_boundary_ids) {
                        u64(id.size()); for (const unsigned char value : id) byte(value);
                    }
                    append_source_ids(std::span{&region.outer_edge_source_ids, 1});
                    append_source_ids(region.inner_edge_source_ids);
                    append_source_ids(std::span{&region.outer_vertex_source_ids, 1});
                    append_source_ids(region.inner_vertex_source_ids);
                    append_profile(region.outer_profile);
                    u64(region.inner_profiles.size());
                    for (const auto& profile : region.inner_profiles) append_profile(profile);
                }
                for (const double value : {
                        primitive.direction.x, primitive.direction.y,
                        primitive.direction.z, primitive.start_offset}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(static_cast<std::uint8_t>(primitive.extent));
                byte(primitive.first_cap_is_start);
                byte(primitive.through_all_forward);
                byte(primitive.through_all_reverse);
                u64(primitive.target_face.owner_id.size());
                for (const unsigned char value : primitive.target_face.owner_id) byte(value);
                u64(primitive.target_face.semantic_key.size());
                for (const unsigned char value : primitive.target_face.semantic_key) byte(value);
                byte(primitive.target_is_datum);
                for (const double value : {primitive.target_plane_origin.x,
                        primitive.target_plane_origin.y, primitive.target_plane_origin.z,
                        primitive.target_plane_normal.x, primitive.target_plane_normal.y,
                        primitive.target_plane_normal.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                u64(primitive.target_surface_triangles.size());
                for (const auto& point : primitive.target_surface_triangles) {
                    for (const double value : {point.x, point.y, point.z}) {
                        u64(std::bit_cast<std::uint64_t>(value));
                    }
                }
                if (primitive.symmetric_limit) for(const unsigned char c:std::string_view("symmetric-extrusion-limit-v1"))byte(c);
                if (primitive.mirror_forward_limit) for(const unsigned char c:std::string_view("mirrored-forward-limit-v1"))byte(c);
                if (primitive.reverse_limit) {
                    for (const unsigned char c : std::string_view("extrusion-reverse-limit-v1")) byte(c);
                    const auto& limit=*primitive.reverse_limit;
                    byte(limit.planar);byte(limit.datum);
                    for(const auto& text:{limit.reference.owner_id,limit.reference.semantic_key,limit.reference.instance_path}) {
                        u64(text.size());for(const unsigned char c:text)byte(c);
                    }
                    for(const auto p:{limit.origin,limit.normal})for(const double v:{p.x,p.y,p.z})u64(std::bit_cast<std::uint64_t>(v));
                    u64(limit.triangles.size());for(const auto p:limit.triangles)for(const double v:{p.x,p.y,p.z})u64(std::bit_cast<std::uint64_t>(v));
                }
                if (primitive.wall) {
                    for (const unsigned char c : std::string_view("profile-wall-v1")) byte(c);
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->first_offset));
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->second_offset));
                    u64(primitive.wall->end_point_id.size());
                    for (const unsigned char c : primitive.wall->end_point_id) byte(c);
                }
            } else if constexpr (std::is_same_v<Request, FeatureGroupRequest>) {
                if(primitive.bend_line_end_length!=0){for(unsigned char c:std::string_view("bend-line-ends"))byte(c);u64(std::bit_cast<std::uint64_t>(primitive.bend_line_end_length));}
                byte(primitive.allow_empty);
                u64(primitive.reference_points.size());
                for(const auto& point:primitive.reference_points) {
                    for(const auto* text:{&point.reference.owner_id,&point.reference.semantic_key,&point.reference.instance_path,&point.display_owner_id}) {
                        u64(text->size());for(const unsigned char value:*text)byte(value);
                    }
                    for(double value:{point.position.x,point.position.y,point.position.z})u64(std::bit_cast<std::uint64_t>(value));
                }
                if(!primitive.reference_planes.empty()) {
                    for(const unsigned char c:std::string_view("reference-planes"))byte(c);
                    u64(primitive.reference_planes.size());
                    for(const auto& plane:primitive.reference_planes) {
                        for(const auto* text:{&plane.reference.owner_id,&plane.reference.semantic_key,&plane.reference.instance_path}) {
                            u64(text->size());for(const unsigned char value:*text)byte(value);
                        }
                        for(const auto& p:plane.corners)for(double value:{p.x,p.y,p.z})u64(std::bit_cast<std::uint64_t>(value));
                    }
                }
                u64(primitive.children.size());
                u64(primitive.axes.size());
                for (const auto& axis : primitive.axes) {
                    for (const auto* text : {&axis.reference.owner_id,&axis.reference.semantic_key,&axis.reference.instance_path,&axis.label}) {
                        u64(text->size()); for (const unsigned char value : *text) byte(value);
                    }
                    for (const double value : {axis.point.x,axis.point.y,axis.point.z,
                            axis.direction.x,axis.direction.y,axis.direction.z,axis.display_length})
                        u64(std::bit_cast<std::uint64_t>(value));
                }
                std::vector<HistoryOperation> child_operations;
                child_operations.reserve(primitive.children.size());
                for (std::size_t child_index = 0;
                     child_index < primitive.children.size(); ++child_index) {
                    HistoryOperation child;
                    child.owner_id = operation.owner_id + ":child:" +
                        std::to_string(child_index);
                    std::visit([&](const auto& value) {
                        child.primitive = value;
                    }, primitive.children[child_index]);
                    child.operation = operation.operation;
                    child.boolean_tolerance = operation.boolean_tolerance;
                    child_operations.push_back(std::move(child));
                }
                const auto child_fingerprint = history_fingerprint(
                    child_operations, child_operations.size());
                u64(child_fingerprint.size());
                for (const unsigned char value : child_fingerprint) byte(value);
                u64(primitive.reference_profiles.size());
                for(const auto& profile:primitive.reference_profiles) {
                    HistoryOperation reference;reference.owner_id=operation.owner_id;
                    std::visit([&](const auto& value){reference.primitive=value;},profile);
                    const auto key=history_fingerprint({reference},1);
                    u64(key.size());for(const unsigned char value:key)byte(value);
                }
            } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                byte(primitive.centerlines.origin_enabled);byte(primitive.centerlines.centroid_enabled);
                for(const auto& text:{primitive.centerlines.origin_id,primitive.centerlines.profile_id}){u64(text.size());for(unsigned char c:text)byte(c);}
                for(double value:{primitive.centerlines.origin.x,primitive.centerlines.origin.y,primitive.centerlines.origin.z,primitive.centerlines.normal.x,primitive.centerlines.normal.y,primitive.centerlines.normal.z})u64(std::bit_cast<std::uint64_t>(value));

                if(primitive.surface_result) {byte(0xf1);for(const auto c:primitive.open_profile_end_id)byte(c);}
                const auto append_profile = [&](const auto& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices) {
                                for (const double value : {point.x, point.y, point.z}) {
                                    u64(std::bit_cast<std::uint64_t>(value));
                                }
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.major_axis_direction.x,
                                    profile.major_axis_direction.y,
                                    profile.major_axis_direction.z,
                                    profile.major_radius, profile.minor_radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    const auto point = [&](const Vec3& value) {
                                        for (const double coordinate : {
                                                value.x, value.y, value.z}) {
                                            u64(std::bit_cast<std::uint64_t>(coordinate));
                                        }
                                    };
                                    point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::ArcCurve>) {
                                        point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::EllipticalArcCurve>) {
                                        point(exact_curve.center);
                                        point(exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.knots.size());
                                        for (double v : exact_curve.knots) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.weights.size());
                                        for (double v : exact_curve.weights) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.degree);
                                        byte(exact_curve.interpolating);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& control : exact_curve.control_points) {
                                            point(control);
                                        }
                                    }
                                    point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                append_profile(primitive.outer_profile);
                u64(primitive.profile_region_id.size());
                for (const unsigned char value : primitive.profile_region_id) byte(value);
                u64(primitive.outer_boundary_id.size());
                for (const unsigned char value : primitive.outer_boundary_id) byte(value);
                u64(primitive.inner_boundary_ids.size());
                for (const auto& id : primitive.inner_boundary_ids) {
                    u64(id.size()); for (const unsigned char value : id) byte(value);
                }
                const auto append_source_ids = [&](const auto& groups) {
                    u64(groups.size());
                    for (const auto& group : groups) {
                        u64(group.size());
                        for (const auto& id : group) {
                            u64(id.size());
                            for (const unsigned char value : id) byte(value);
                        }
                    }
                };
                // One group, viewed without copying its persisted identities.
                append_source_ids(std::span{&primitive.outer_edge_source_ids, 1});
                append_source_ids(primitive.inner_edge_source_ids);
                append_source_ids(std::span{&primitive.outer_vertex_source_ids, 1});
                append_source_ids(primitive.inner_vertex_source_ids);
                u64(primitive.inner_profiles.size());
                for (const auto& profile : primitive.inner_profiles) {
                    append_profile(profile);
                }
                u64(primitive.additional_profile_regions.size());
                for (const auto& region : primitive.additional_profile_regions) {
                    u64(region.region_id.size());
                    for (const unsigned char value : region.region_id) byte(value);
                    u64(region.outer_boundary_id.size());
                    for (const unsigned char value : region.outer_boundary_id) byte(value);
                    u64(region.inner_boundary_ids.size());
                    for (const auto& id : region.inner_boundary_ids) {
                        u64(id.size()); for (const unsigned char value : id) byte(value);
                    }
                    append_source_ids(std::span{&region.outer_edge_source_ids, 1});
                    append_source_ids(region.inner_edge_source_ids);
                    append_source_ids(std::span{&region.outer_vertex_source_ids, 1});
                    append_source_ids(region.inner_vertex_source_ids);
                    append_profile(region.outer_profile);
                    u64(region.inner_profiles.size());
                    for (const auto& profile : region.inner_profiles) append_profile(profile);
                }
                for (const double value : {
                        primitive.profile_normal.x, primitive.profile_normal.y,
                        primitive.profile_normal.z,
                        primitive.axis_point.x, primitive.axis_point.y,
                        primitive.axis_point.z, primitive.axis_direction.x,
                        primitive.axis_direction.y, primitive.axis_direction.z,
                        primitive.start_angle_degrees,
                        primitive.angle_degrees}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(primitive.first_cap_is_start);
                if (primitive.wall) {
                    for (const unsigned char c : std::string_view("profile-wall-v1")) byte(c);
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->first_offset));
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->second_offset));
                    u64(primitive.wall->end_point_id.size());
                    for (const unsigned char c : primitive.wall->end_point_id) byte(c);
                }
            } else if constexpr (std::is_same_v<Request, Sweep3DRequest>) {
                const auto append_string = [&](const std::string& value) {
                    u64(value.size());
                    for (const unsigned char character : value) byte(character);
                };
                const auto append_point = [&](const Vec3& point) {
                    for (const double value : {point.x, point.y, point.z}) {
                        u64(std::bit_cast<std::uint64_t>(value));
                    }
                };
                const auto append_profile = [&](
                        const ExtrusionRequest::ProfileLoop& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices)
                                append_point(point);
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            append_point(profile.center);
                            u64(std::bit_cast<std::uint64_t>(profile.radius));
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            append_point(profile.center);
                            append_point(profile.major_axis_direction);
                            u64(std::bit_cast<std::uint64_t>(
                                profile.major_radius));
                            u64(std::bit_cast<std::uint64_t>(
                                profile.minor_radius));
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    append_point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                            std::decay_t<decltype(exact_curve)>,
                                            ExtrusionRequest::ArcCurve>) {
                                        append_point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                            std::decay_t<decltype(exact_curve)>,
                                            ExtrusionRequest::EllipticalArcCurve>) {
                                        append_point(exact_curve.center);
                                        append_point(
                                            exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(
                                                value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                            std::decay_t<decltype(exact_curve)>,
                                            ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.knots.size());
                                        for (double v : exact_curve.knots) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.weights.size());
                                        for (double v : exact_curve.weights) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.degree);
                                        byte(exact_curve.interpolating);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& point :
                                             exact_curve.control_points) {
                                            append_point(point);
                                        }
                                    }
                                    append_point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                u64(primitive.path_points.size());
                byte(primitive.separate_segments);
                for (const auto& point : primitive.path_points)
                    append_point(point);
                u64(primitive.path_point_ids.size());
                for (const auto& id : primitive.path_point_ids)
                    append_string(id);
                u64(primitive.path_segments.size());
                for (const auto& segment : primitive.path_segments) {
                    append_string(segment.source_id);
                    append_point(segment.start);
                    append_point(segment.end);
                    u64(segment.arc_midpoint.has_value());
                    if(segment.arc_midpoint)append_point(*segment.arc_midpoint);
                    u64(segment.bezier_control_points.size());
                    for (const auto& point : segment.bezier_control_points)
                        append_point(point);
                    u64(segment.bezier_spans.size());
                    for(const auto& span:segment.bezier_spans)for(const auto& point:span)append_point(point);
                }
                u64(primitive.sections.size());
                for (const auto& section : primitive.sections) {
                    append_string(section.profile_id);
                    append_string(section.point_id);
                    u64(section.point_index);
                    append_point(section.profile_normal);
                    u64(section.circle_radial_direction.has_value());
                    if(section.circle_radial_direction) append_point(*section.circle_radial_direction);
                    append_string(section.profile.region_id);
                    append_string(section.profile.outer_boundary_id);
                    u64(section.profile.outer_edge_source_ids.size());
                    for (const auto& id :
                         section.profile.outer_edge_source_ids) {
                        append_string(id);
                    }
                    u64(section.profile.outer_vertex_source_ids.size());
                    for (const auto& id :
                         section.profile.outer_vertex_source_ids) {
                        append_string(id);
                    }
                    append_profile(section.profile.outer_profile);
                    u64(section.profile.inner_profiles.size());
                    for(const auto& inner:section.profile.inner_profiles)append_profile(inner);
                    for(const auto& id:section.profile.inner_boundary_ids)append_string(id);
                    for(const auto& loop:section.profile.inner_edge_source_ids){u64(loop.size());for(const auto& id:loop)append_string(id);}
                    for(const auto& loop:section.profile.inner_vertex_source_ids){u64(loop.size());for(const auto& id:loop)append_string(id);}
                    append_string(section.thin_end_point_id);

                }
                byte(primitive.make_solid);
                byte(primitive.transported);
                if(primitive.smooth_loft) {byte(0xe9);byte(1);}
                if(primitive.fixed_section_frames) {byte(0xea);byte(1);}
                if(primitive.attachment_endpoints) {byte(0xeb);byte(1);}
                if(primitive.rotation_axis) {
                    byte(0xec);
                    const auto& axis=*primitive.rotation_axis;
                    for(const auto* text:{&axis.reference.owner_id,&axis.reference.semantic_key,&axis.reference.instance_path,&axis.label})append_string(*text);
                    for(double value:{axis.point.x,axis.point.y,axis.point.z,axis.direction.x,axis.direction.y,axis.direction.z,axis.display_length})
                        u64(std::bit_cast<std::uint64_t>(value));
                }
                u64(std::bit_cast<std::uint64_t>(primitive.linear_tolerance));
                byte(primitive.thin);
                u64(std::bit_cast<std::uint64_t>(primitive.thin_first));u64(std::bit_cast<std::uint64_t>(primitive.thin_second));
            } else if constexpr (std::is_same_v<Request, StepRequest>) {
                u64(primitive.source_path.size());
                for (const unsigned char value : primitive.source_path) byte(value);
                u64(primitive.component_path.size());
                for (const unsigned char value : primitive.component_path) byte(value);
                for (const double value : {primitive.translation.x, primitive.translation.y, primitive.translation.z,
                        primitive.rotation_degrees.x, primitive.rotation_degrees.y, primitive.rotation_degrees.z})
                    u64(std::bit_cast<std::uint64_t>(value));
            } else if constexpr (std::is_same_v<Request, ThreadSurfaceRequest>) {
                if (primitive.shaft_face) {
                    byte(255); // Standalone external thread reference contract.
                    for (const auto& face : {primitive.shaft_face, std::optional<FaceReference>{primitive.shaft_start},
                            primitive.shaft_chamfer,primitive.shaft_end}) {
                        byte(face.has_value());
                        if (face) for (const auto* text : {&face->owner_id,&face->semantic_key,&face->instance_path}) {
                            u64(text->size());for (const unsigned char ch : *text) byte(ch);
                        }
                    }
                    byte(primitive.shaft_through_all);byte(primitive.shaft_runout);
                }
                byte(primitive.end_plane_origin.has_value());
                if (primitive.end_plane_origin) {
                    for (double value : {primitive.end_plane_origin->x,primitive.end_plane_origin->y,
                            primitive.end_plane_origin->z,primitive.end_plane_normal.x,
                            primitive.end_plane_normal.y,primitive.end_plane_normal.z})
                        u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(primitive.end_plane_reference.has_value());
                if (primitive.end_plane_reference)
                    for (const auto* text : {&primitive.end_plane_reference->owner_id,
                            &primitive.end_plane_reference->semantic_key,&primitive.end_plane_reference->instance_path}) {
                        u64(text->size());for (const unsigned char ch : *text) byte(ch);
                    }
                // Opening result revision: validate and resolve original end faces.
                byte(3);
                for (const double value : {primitive.nominal_radius,
                        primitive.root_radius, primitive.start_offset,
                        primitive.length, primitive.runout_start,
                        primitive.runout_end, primitive.origin.x,
                        primitive.origin.y, primitive.origin.z,
                        primitive.axis_direction.x, primitive.axis_direction.y,
                        primitive.axis_direction.z,
                        primitive.radial_direction.x,
                        primitive.radial_direction.y,
                        primitive.radial_direction.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(primitive.through_all_forward);
                byte(primitive.through_all_reverse);
                byte(static_cast<std::uint8_t>(primitive.side));
                byte(primitive.enabled);
                for (const auto& group : {primitive.cuts_before, primitive.cuts_after}) {
                    byte(static_cast<bool>(group));
                    if (!group) continue;
                    HistoryOperation cut;
                    cut.owner_id = operation.owner_id;
                    cut.primitive = *group;
                    cut.operation = BooleanOperation::Subtract;
                    cut.boolean_tolerance = operation.boolean_tolerance;
                    const auto fingerprint = history_fingerprint({cut}, 1);
                    u64(fingerprint.size());
                    for (const unsigned char value : fingerprint) byte(value);
                }
            } else if constexpr (std::is_same_v<Request, BoundarySurfaceRequest>) {
                u64(1);u64(std::bit_cast<std::uint64_t>(primitive.tolerance));
                const auto text=[&](const std::string& s){u64(s.size());for(unsigned char c:s)byte(c);};
                text(primitive.region_id);
                for(std::size_t i=0;i<4;++i) {
                    text(primitive.source_owners[i]);
                    HistoryOperation boundary;boundary.primitive=primitive.boundaries[i];
                    text(history_fingerprint({boundary},1));
                }
            } else if constexpr (std::is_same_v<Request, SheetStateRequest>) {
                u64(2);byte(primitive.unfold);byte(primitive.all);
                u64(std::bit_cast<std::uint64_t>(primitive.tolerance));
                u64(primitive.owners.size());for(const auto& owner:primitive.owners){u64(owner.size());for(unsigned char c:owner)byte(c);}
            } else if constexpr (std::is_same_v<Request, DrillPointRequest>) {
                // Source-parent topology replaces selection-order identities.
                // Explicit calculation must not reuse the former derived cache.
                u64(1); // Drill-point topology schema.
                u64(primitive.bottom_faces.size());
                for (const auto& face : primitive.bottom_faces) {
                    for (const auto* text : {&face.owner_id,
                            &face.semantic_key, &face.instance_path}) {
                        u64(text->size());
                        for (const unsigned char value : *text) byte(value);
                    }
                }
                u64(std::bit_cast<std::uint64_t>(
                    primitive.included_angle_degrees));
            } else if constexpr (std::is_same_v<Request, ShellRequest>) {
                // Do not reuse derived Shell topology which treated unowned
                // spherical seams/poles as persistent reference entities.
                u64(1); // Shell topology schema.
                u64(primitive.removed_faces.size());
                for (const auto& face : primitive.removed_faces) {
                    u64(face.owner_id.size());
                    for (const unsigned char value : face.owner_id) byte(value);
                    u64(face.semantic_key.size());
                    for (const unsigned char value : face.semantic_key) byte(value);
                }
                u64(std::bit_cast<std::uint64_t>(primitive.thickness));
            } else {
                u64(primitive.edges.size());
                for (const auto& edge : primitive.edges) {
                    u64(edge.owner_id.size());
                    for (const unsigned char value : edge.owner_id) byte(value);
                    u64(edge.semantic_key.size());
                    for (const unsigned char value : edge.semantic_key) byte(value);
                }
                if constexpr (std::is_same_v<Request, FilletRequest>) {
                    // R1 is mapped from each persisted request to all matching
                    // runtime edge uses. Earlier multi-route caches are unsafe.
                    u64(1); // Fillet calculation schema.
                    byte(static_cast<std::uint8_t>(primitive.mode));
                    u64(std::bit_cast<std::uint64_t>(primitive.radius_start));
                    u64(std::bit_cast<std::uint64_t>(primitive.radius_end));
                    byte(primitive.reverse ? 1U : 0U);
                    u64(primitive.contour_start_vertices.size());
                    for (const auto& vertex :
                         primitive.contour_start_vertices) {
                        u64(vertex.owner_id.size());
                        for (const unsigned char value : vertex.owner_id)
                            byte(value);
                        u64(vertex.semantic_key.size());
                        for (const unsigned char value : vertex.semantic_key)
                            byte(value);
                    }
                } else {
                    byte(static_cast<std::uint8_t>(primitive.mode));
                    u64(std::bit_cast<std::uint64_t>(primitive.distance_a));
                    u64(std::bit_cast<std::uint64_t>(primitive.distance_b));
                    u64(std::bit_cast<std::uint64_t>(primitive.angle_radians));
                    byte(primitive.flip ? 1U : 0U);
                }
            }
        }, operation.primitive);
        if constexpr(Capture)++stream->first;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

} // namespace
std::string history_fingerprint(const std::vector<HistoryOperation>& operations,std::size_t count) {
    return encode_history_fingerprint<false>(operations,count);
}
std::vector<std::string> history_fingerprints(const std::vector<HistoryOperation>& operations) {
    if(operations.empty())return {history_fingerprint(operations,0)};
    if(operations.size()==1)return {history_fingerprint(operations,0),history_fingerprint(operations,1)};
    FingerprintBatch state;state.hashes.resize(operations.size()+1,1469598103934665603ULL);
    for(std::size_t count=0;count<state.hashes.size();++count)for(unsigned shift=0;shift<64;shift+=8) {
        state.hashes[count]^=(static_cast<std::uint64_t>(count)>>shift)&0xffU;state.hashes[count]*=1099511628211ULL;
    }
    static_cast<void>(encode_history_fingerprint<true>(operations,operations.size(),&state));
    std::vector<std::string> result;result.reserve(state.hashes.size());
    constexpr char digits[]="0123456789abcdef";
    for(auto hash:state.hashes) {
        std::string text(16,'0');for(int index=15;index>=0;--index){text[index]=digits[hash&0xfU];hash>>=4;}
        result.push_back(std::move(text));
    }
    return result;
}

} // namespace zima::kernel
