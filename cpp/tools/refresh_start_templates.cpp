#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/relation_program.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <algorithm>
#include <charconv>
#include <limits>
#include <cmath>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <iterator>

namespace {
auto comparable_history(std::vector<zima::document::HistoryContainer> history) {
    // Regeneration writes embedded native Sketch JSON in its normal
    // formatted representation. Whitespace is not an authored modeling edit;
    // retain every parsed field, including signed floating-point zero.
    for(auto& item:history)zima::document::visit_feature_sketches(item,[](auto& sketch,std::size_t){sketch=nlohmann::json::parse(sketch).dump();});
    return history;
}
nlohmann::json reparameterize_legacy_single_orientation_placements(
        zima::document::PartDocument& part,
        const std::vector<zima::kernel::BodyResult>& cached) {
    zima::kernel::ViewerReferenceGeometry geometry;
    for (const auto& boundary : cached)
        zima::workspace::append_reference_geometry(
            geometry, boundary.mesh.original_references);
    zima::workspace::append_reference_geometry(
        geometry, part.origin_viewer_mesh().original_references);
    zima::workspace::append_reference_geometry(
        geometry, part.body_origin_reference_geometry());
    zima::workspace::append_reference_geometry(
        geometry, part.history_origin_reference_geometry_before({}));
    zima::workspace::append_reference_geometry(
        geometry, part.construction_viewer_mesh().original_references);
    zima::workspace::append_reference_geometry(
        geometry, part.sketch_placement_reference_geometry());

    nlohmann::json migrations = nlohmann::json::array();
    const auto normalize = [&](zima::document::Placement& placement,
            const std::string& owner, const char* kind) {
        const auto before = placement;
        bool normalized =
            zima::document::reparameterize_single_orientation_placement(
                placement, geometry);
        // A full cache contains successive versions of the same persistent
        // original face. If an older duplicate precedes the feature's actual
        // input boundary, retry each exact boundary from newest to oldest
        // rather than letting enumeration order choose a different plane.
        for (auto boundary = cached.rbegin(); !normalized &&
                boundary != cached.rend(); ++boundary) {
            auto candidate = before;
            if (zima::document::reparameterize_single_orientation_placement(
                    candidate, boundary->mesh.original_references)) {
                placement = std::move(candidate);
                normalized = true;
            }
        }
        if (!normalized || placement == before) return;
        migrations.push_back({
            {"owner", owner}, {"kind", kind},
            {"absolute_rotation_before", {before.absolute_rotation_x,
                before.absolute_rotation_y, before.absolute_rotation_z}},
            {"absolute_rotation_after", {placement.absolute_rotation_x,
                placement.absolute_rotation_y,
                placement.absolute_rotation_z}},
            {"resolved_rotation", {before.rotation_x, before.rotation_y,
                before.rotation_z}}});
    };
    for (auto& feature : part.history)
        normalize(feature.placement, feature.id, "history");
    const auto bodies = part.body_history.bodies();
    for (const auto& original : bodies) {
        auto body = original;
        normalize(body.scope.placement, body.scope.id, "body");
        if (body != original) part.body_history.update_body(std::move(body));
    }
    return migrations;
}
void collect_entity_ids(const nlohmann::json& value,std::set<std::string>& ids) {
    if(value.is_object()) {
        if(const auto id=value.find("id");id!=value.end()&&id->is_string())
            ids.insert(id->get<std::string>());
        for(const auto& child:value)collect_entity_ids(child,ids);
    } else if(value.is_array())for(const auto& child:value)
        collect_entity_ids(child,ids);
}
std::vector<std::set<std::string>> sketch_entity_ids(
        const zima::document::PartDocument& part) {
    std::vector<std::set<std::string>> result;
    const auto append=[&](const auto& sketch) {
        std::set<std::string> ids;
        collect_entity_ids(nlohmann::json::parse(sketch),ids);
        result.push_back(std::move(ids));
    };
    for(const auto& sketch:part.sketches)append(sketch.serialized());
    for(const auto& feature:part.history)
        zima::document::visit_feature_sketches(feature,
            [&](const auto& sketch,std::size_t){append(sketch);});
    return result;
}
bool same_history_structure(const zima::document::PartDocument& before,
        const zima::document::PartDocument& after) {
    if(before.body_history!=after.body_history||
            before.history_order!=after.history_order||
            before.history.size()!=after.history.size()||
            before.constructions.size()!=after.constructions.size())return false;
    for(std::size_t index=0;index<before.history.size();++index) {
        const auto& left=before.history[index];const auto& right=after.history[index];
        if(left.id!=right.id||left.feature_id!=right.feature_id||
                left.feature_parent_id!=right.feature_parent_id||
                left.name!=right.name||left.feature_kind!=right.feature_kind||
                left.container_origin!=right.container_origin||
                left.combine_mode!=right.combine_mode||
                left.suppressed!=right.suppressed||
                left.value_locks!=right.value_locks)return false;
    }
    for(std::size_t index=0;index<before.constructions.size();++index)
        if(before.constructions[index].id!=after.constructions[index].id||
                before.constructions[index].kind!=after.constructions[index].kind)
            return false;
    return sketch_entity_ids(before)==sketch_entity_ids(after);
}
bool resolved_reference_state_improvement(const nlohmann::json& change) {
    const auto path=change.value("path",std::string{});
    return change.value("op",std::string{})=="replace"&&
        change.contains("value")&&change.at("value").is_boolean()&&
        ((path.ends_with("/broken")&&!change.at("value").get<bool>())||
         (path.ends_with("/reference_valid")&&
            change.at("value").get<bool>()));
}
bool allowed_topology_migration_change(const nlohmann::json& change) {
    const auto path=change.value("path",std::string{});
    // Regeneration may refresh only derived bookkeeping. In particular, a
    // topology migration must never rewrite an authored reference, placement,
    // Sketch definition, side choice or history parameter merely because the
    // calculated BodyResult uses a newer native representation.
    return path=="/calculation_definition_fingerprint"||
        path=="/user_parameters/mass"||
        path.starts_with("/user_parameter_values/mass/")||
        resolved_reference_state_improvement(change);
}
bool topology_migration_preserves_authored_definition(
        const zima::document::PartDocument& before,
        const zima::document::PartDocument& after,
        nlohmann::json& changes) {
    changes=nlohmann::json::diff(before.serialized(),after.serialized());
    return std::ranges::all_of(changes,allowed_topology_migration_change);
}
void erase_optional_calculation_caches(nlohmann::json& value) {
    if(value.is_object()) {
        value.erase("measured_area");
        value.erase("kernel_bindings");
        value.erase("measurement_sources");
        value.erase("kernel_shape");
        for(auto& child:value)erase_optional_calculation_caches(child);
    } else if(value.is_array())for(auto& child:value)
        erase_optional_calculation_caches(child);
}
auto reference_identity_key(const auto& reference) {
    return std::tuple{reference.owner_id,reference.semantic_key,
        reference.instance_path};
}
auto edge_order_key(const zima::kernel::ViewerEdge& edge) {
    const auto& first=edge.points.front();
    const auto& last=edge.points.back();
    return std::tuple{edge.reference.owner_id,edge.reference.semantic_key,
        edge.reference.instance_path,edge.reference.display_owner_id,
        edge.measured_length.has_value(),edge.measured_length.value_or(0.0),
        edge.surface_result,edge.parameter_seam,first.x,first.y,first.z,
        last.x,last.y,last.z,edge.points.size()};
}
zima::kernel::ViewerReferenceGeometry canonical_reference_geometry(
        const zima::kernel::ViewerReferenceGeometry& geometry) {
    zima::kernel::ViewerReferenceGeometry result;
    // Display tessellation is deliberately not part of this verification
    // contract. Keep only the exact reference entities and put every parallel
    // serialized array into a deterministic persistent-identity order. OCCT
    // may enumerate the same result in a different traversal order.
    result.triangle_references=geometry.triangle_references;
    std::ranges::sort(result.triangle_references,{},reference_identity_key<
        zima::kernel::FaceReference>);
    result.edges=geometry.edges;
    std::ranges::sort(result.edges,{},edge_order_key);
    result.points=geometry.points;
    std::ranges::sort(result.points,{},[](const zima::kernel::ViewerPoint& point) {
        return std::tuple{point.reference.owner_id,point.reference.semantic_key,
            point.reference.instance_path,point.surface_result};
    });
    result.axes=geometry.axes;
    std::ranges::sort(result.axes,{},[](const zima::kernel::ViewerAxis& axis) {
        return std::tuple{axis.reference.owner_id,axis.reference.semantic_key,
            axis.reference.instance_path,axis.point.x,axis.point.y,axis.point.z,
            axis.direction.x,axis.direction.y,axis.direction.z,
            axis.display_length};
    });
    return result;
}
nlohmann::json calculated_reference_contract(
        const zima::kernel::ViewerReferenceGeometry& geometry) {
    const auto canonical=canonical_reference_geometry(geometry);
    auto result=zima::document::serialize_viewer_reference_geometry(canonical);
    erase_optional_calculation_caches(result);
    for(auto& reference:result.at("references"))
        for(const auto* name:{"surface","contour_surface"}) {
            const auto found=reference.find(name);
            if(found==reference.end())continue;
            auto& surface=*found;
            const auto kind=static_cast<zima::kernel::SurfaceGeometry::Kind>(
                surface.at("kind").get<int>());
            if(kind==zima::kernel::SurfaceGeometry::Kind::Plane) {
                // A plane's persisted side is its oriented normal.  (+N,
                // reversed) and (-N, not reversed) are the same exact support
                // and side. Any point in the plane is an equivalent origin.
                if(surface.at("reversed").get<bool>())
                    for(auto& coordinate:surface.at("axis"))
                        coordinate=-coordinate.get<double>();
                const auto& axis=surface.at("axis");
                const auto& origin=surface.at("origin");
                double length{},distance{};
                for(std::size_t coordinate=0;coordinate<3;++coordinate) {
                    const double a=axis[coordinate],o=origin[coordinate];
                    length+=a*a;distance+=a*o;
                }
                auto canonical_origin=nlohmann::json::array();
                for(const auto& coordinate:axis)
                    canonical_origin.push_back(
                        coordinate.get<double>()*distance/length);
                surface["origin"]=std::move(canonical_origin);
                surface["reversed"]=false;
                surface.erase("radial");
            } else if(kind==zima::kernel::SurfaceGeometry::Kind::Cylinder||
                    kind==zima::kernel::SurfaceGeometry::Kind::Cone) {
                // The angular zero direction is arbitrary on an axially
                // symmetric support. Its exact trim is owned by the boundary.
                surface.erase("radial");
            }
        }
    // Triangles and sampled edge points are display tessellation. They may be
    // regenerated with a different sampling while the exact reference curve,
    // surface and persistent identity remain unchanged.
    for(const auto* field:{"vertices_binary","triangles_binary",
            "triangle_reference_runs_binary","edge_points_binary",
            "edge_offsets_binary","point_positions_binary","edge_splines"})
        result.erase(field);
    // Vertex identities remain exact in point_references_binary. Their
    // coordinates are redundant edge endpoints and periodic seams may choose
    // another parameter origin for the same closed analytic boundary.
    return result;
}
bool approximately_same_contract(const nlohmann::json& left,
        const nlohmann::json& right) {
    if(left.is_number()&&right.is_number()) {
        const double a=left.get<double>(),b=right.get<double>();
        return std::abs(a-b)<=std::max(1.0e-7,
            1.0e-9*std::max(std::abs(a),std::abs(b)));
    }
    if(left.type()!=right.type())return false;
    if(left.is_array()) {
        if(left.size()!=right.size())return false;
        for(std::size_t index=0;index<left.size();++index)
            if(!approximately_same_contract(left[index],right[index]))return false;
        return true;
    }
    if(left.is_object()) {
        if(left.size()!=right.size())return false;
        for(const auto& [key,value]:left.items()) {
            const auto found=right.find(key);
            if(found==right.end()||
                    !approximately_same_contract(value,*found))return false;
        }
        return true;
    }
    return left==right;
}
bool same_reference_curves(const zima::kernel::ViewerReferenceGeometry& left,
        const zima::kernel::ViewerReferenceGeometry& right) {
    if(left.edges.size()!=right.edges.size())return false;
    std::vector<const zima::kernel::ViewerEdge*> left_edges,right_edges;
    left_edges.reserve(left.edges.size());right_edges.reserve(right.edges.size());
    for(const auto& edge:left.edges)left_edges.push_back(&edge);
    for(const auto& edge:right.edges)right_edges.push_back(&edge);
    const auto edge_less=[](const auto* a,const auto* b) {
        return edge_order_key(*a)<edge_order_key(*b);
    };
    std::ranges::sort(left_edges,edge_less);
    std::ranges::sort(right_edges,edge_less);
    const auto distance_squared=[](const auto& a,const auto& b) {
        const double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
        return x*x+y*y+z*z;
    };
    const auto same_point=[&](const auto& a,const auto& b) {
        return distance_squared(a,b)<=1.0e-12;
    };
    const auto point_segment_distance_squared=[&](const auto& p,
            const auto& a,const auto& b) {
        const double x=b.x-a.x,y=b.y-a.y,z=b.z-a.z;
        const double length=x*x+y*y+z*z;
        const double parameter=length<=1e-24?0.:std::clamp(
            ((p.x-a.x)*x+(p.y-a.y)*y+(p.z-a.z)*z)/length,0.,1.);
        const zima::kernel::Vec3 projected{
            a.x+parameter*x,a.y+parameter*y,a.z+parameter*z};
        return distance_squared(p,projected);
    };
    const auto follows_polyline=[&](const auto& source,const auto& target) {
        if(target.size()<2)return false;
        for(const auto& point:source) {
            double best=std::numeric_limits<double>::infinity();
            for(std::size_t segment=1;segment<target.size();++segment)
                best=std::min(best,point_segment_distance_squared(point,
                    target[segment-1],target[segment]));
            if(best>1.0e-12)return false;
        }
        return true;
    };
    const auto spline_contract=[](const zima::kernel::BSplineGeometry& curve) {
        auto poles=nlohmann::json::array();
        for(const auto& point:curve.poles)
            poles.push_back({point.x,point.y,point.z});
        return nlohmann::json{{"degree",curve.degree},
            {"poles",std::move(poles)},{"knots",curve.knots},
            {"weights",curve.weights}};
    };
    for(std::size_t index=0;index<left_edges.size();++index) {
        const auto& a=*left_edges[index];const auto& b=*right_edges[index];
        if(a.reference!=b.reference||a.points.size()<2||b.points.size()<2)
            return false;
        // A UV seam is not a physical boundary and is deliberately neither
        // drawn nor pickable. Its radial parameter origin may move while the
        // exact supporting periodic face and persistent edge identity remain.
        if(a.parameter_seam||b.parameter_seam)continue;
        const bool closed_a=same_point(a.points.front(),a.points.back());
        const bool closed_b=same_point(b.points.front(),b.points.back());
        if(closed_a!=closed_b)return false;
        if(!closed_a&&(!same_point(a.points.front(),b.points.front())||
                !same_point(a.points.back(),b.points.back())))return false;
        if(a.exact_spline&&b.exact_spline&&
                approximately_same_contract(
                    spline_contract(*a.exact_spline),
                    spline_contract(*b.exact_spline)))continue;
        if(!follows_polyline(a.points,b.points)||
                !follows_polyline(b.points,a.points))return false;
    }
    return true;
}
nlohmann::json annotated_contract_diff(const nlohmann::json& before,
        const nlohmann::json& after) {
    auto changes=nlohmann::json::diff(before,after);
    for(auto& change:changes)if(change.value("op",std::string{})=="replace"||
            change.value("op",std::string{})=="remove") {
        try {
            const auto pointer=nlohmann::json::json_pointer(
                change.at("path").get<std::string>());
            change["previous"]=before.at(pointer);
        } catch(const std::exception&) {}
    }
    return changes;
}
std::string format_migration_calculated_contract_error(
        const std::vector<zima::kernel::BodyResult>& before,
        const std::vector<zima::kernel::BodyResult>& after) {
    if(before.size()!=after.size())return "boundary count "+
        std::to_string(before.size())+" -> "+std::to_string(after.size());
    for(std::size_t index=0;index<before.size();++index) {
        const auto old_packet=zima::document::serialize_body_result(
            before[index]);
        const auto new_packet=zima::document::serialize_body_result(
            after[index]);
        const auto old_references=calculated_reference_contract(
            before[index].mesh.original_references);
        const auto new_references=calculated_reference_contract(
            after[index].mesh.original_references);
        if(!approximately_same_contract(old_references,new_references)||
                !same_reference_curves(
                    before[index].mesh.original_references,
                    after[index].mesh.original_references))
            return "boundary "+std::to_string(index)+
                " changed original reference geometry";
        const auto same_property=[&](const char* name) {
            const auto& first=old_packet.at(name);
            const auto& second=new_packet.at(name);
            if(!first.is_number()||!second.is_number())return first==second;
            const double a=first,b=second;
            return std::abs(a-b)<=std::max(1.0e-6,
                1.0e-8*std::max(std::abs(a),std::abs(b)));
        };
        if(!same_property("volume")||!same_property("surface_area"))
            return "boundary "+std::to_string(index)+
                " changed volume "+old_packet.at("volume").dump()+" -> "+
                new_packet.at("volume").dump()+" or surface area "+
                old_packet.at("surface_area").dump()+" -> "+
                new_packet.at("surface_area").dump();
    }
    return {};
}
// Explicit offline authoring operation. Input must already satisfy the current
// native contract; the application retains no legacy reader or dual semantics.
std::map<std::string,double> reexpress_coordinates(zima::document::PartDocument& part) {
    using namespace zima;
    std::map<std::string,double> factors;
    std::set<std::pair<std::string,std::string>> bindings;
    const auto visit=[&](const sketcher::Sketch& sketch) {
        for(const auto& d:sketch.dimensions)if(sketcher::dimension_coordinate_sign(d,sketch.plane)<0) {
            const auto key="dimension:"+d.id;bindings.emplace(sketch.id,key);
            const auto name=part.dimension_identifiers.identifier(sketch.id,key);
            if(!name.empty())factors[name]=-1.;
        }
    };
    for(const auto& sketch:part.sketches)visit(sketch);
    for(const auto& feature:part.history)document::visit_feature_sketches(feature,[&](const auto& data,std::size_t){visit(sketcher::Sketch::from_serialized(data));});
    if(factors.empty()&&bindings.empty())return factors;
    if(!part.relations.empty())part.relations=document::RelationProgram(part.relations).reexpress_values(factors);
    auto table=document::parse_family_table(part.family_table);
    for(const auto& column:table.columns) {
        const auto binding=table.bindings.find(column);
        const bool flipped=binding==table.bindings.end()?factors.contains(column):
            binding->second.kind=="dimension"&&bindings.contains({binding->second.owner_id,binding->second.semantic_key});
        if(flipped)for(auto& row:table.instances)
            if(auto value=row.values.find(column);value!=row.values.end()&&!value->second.empty()) {
                double number{};const auto& text=value->second;
                const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
                if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(number))
                    throw std::runtime_error("Family coordinate must be a finite native number");
                char buffer[64];const auto formatted=std::to_chars(buffer,buffer+sizeof(buffer),-number,std::chars_format::general,std::numeric_limits<double>::max_digits10);
                if(formatted.ec!=std::errc{})throw std::runtime_error("Cannot re-express Family coordinate");
                value->second.assign(buffer,formatted.ptr);
            }
    }
    part.family_table=document::serialize_family_table(table);
    return factors;
}
// Embedded FORM definitions own their calculated native source, rather than
// referring to a factory file. Rebuild that source before its receiving Part;
// changing its Sketch contract invalidates the previous calculation key.
std::size_t regenerate_embedded_forms(zima::document::PartDocument& part,
        zima::kernel::OcctKernel& engine,bool reexpress_coordinate_values) {
    using namespace zima;
    std::size_t count{};
    for(auto& feature:part.history)if(feature.feature_kind==document::FeatureKind::SheetForm) {
        auto embedded=document::PartDocument::from_serialized(nlohmann::json::parse(*feature.sheet_form.definition));
        count+=regenerate_embedded_forms(embedded,engine,
            reexpress_coordinate_values);
        if(reexpress_coordinate_values)
            static_cast<void>(reexpress_coordinates(embedded));
        std::set<std::string> evaluated;
        for(const auto& [row,packet]:embedded.family.evaluated)evaluated.insert(row);
        embedded.family.evaluated.clear();
        const auto id=embedded.document_id;workspace::Workspace live;live.add_part(embedded);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;policy.refresh_physical_properties=true;
        static_cast<void>(workspace::regenerate_part(live,engine,id,policy));
        for(const auto& row:document::parse_family_table(embedded.family_table).instances)
            if(evaluated.contains(row.id))static_cast<void>(workspace::open_family_instance(live,engine,id,row.name,false));
        const auto* current=live.open_part(id);
        if(!current)throw std::runtime_error("Embedded FORM conversion lost its Part");
        auto updated=current->session.document();
        auto calculated=current->session.calculated_boundaries();
        static_cast<void>(workspace::complete_part_physical_analysis(
            engine,updated,calculated,true));
        if(comparable_history(updated.history)!=comparable_history(embedded.history)||
                updated.body_history!=embedded.body_history||
                updated.sketches.size()!=embedded.sketches.size())
            throw std::runtime_error("Embedded FORM conversion changed authored history or Body roles");
        for(std::size_t i=0;i<embedded.sketches.size();++i)
            if(embedded.sketches[i].serialized()!=updated.sketches[i].serialized())
                throw std::runtime_error("Embedded FORM conversion changed authored Sketch geometry or references");
        feature.sheet_form.definition=std::make_shared<const std::string>(
            updated.serialized(calculated).dump());
        ++count;
    }
    return count;
}
void migrate_native_part_contract(const std::filesystem::path& source,
        const std::filesystem::path& output,
        const std::filesystem::path& report,bool upgrade_format) {
    if(std::filesystem::absolute(source).lexically_normal()==
            std::filesystem::absolute(output).lexically_normal())
        throw std::runtime_error("Migration output must be separate from its source");
    std::ifstream input(source,std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(input)),{});
    if(!input&& !input.eof())
        throw std::runtime_error("Cannot read native Part for migration");
    std::string migrated=original;
    constexpr std::string_view prefix="param.cpp_history=";
    std::size_t cursor{},change_count{};
    auto changed=nlohmann::json::array();
    const auto add_individual_centroid_contract=[](auto&& self,
            nlohmann::json& value)->std::size_t {
        std::size_t additions{};
        if(value.is_object()) {
            if(value.contains("origin_centerline")&&
                    value.contains("centroid_centerline")&&
                    !value.contains("individual_centroid_centerlines")) {
                value["individual_centroid_centerlines"]=false;
                ++additions;
            }
            for(auto& [key,child]:value.items())
                additions+=self(self,child);
        } else if(value.is_array())for(auto& child:value)
            additions+=self(self,child);
        return additions;
    };
    while(cursor<migrated.size()) {
        const auto line_end=migrated.find('\n',cursor);
        const auto physical_end=line_end==std::string::npos
            ?migrated.size():line_end;
        auto value_end=physical_end;
        if(value_end>cursor&&migrated[value_end-1]=='\r')--value_end;
        if(migrated.compare(cursor,prefix.size(),prefix)==0) {
            auto packet=nlohmann::json::parse(migrated.substr(
                cursor+prefix.size(),value_end-cursor-prefix.size()));
            const auto type=packet.value("type",std::string{});
            const auto centerline_additions=upgrade_format
                ?add_individual_centroid_contract(
                    add_individual_centroid_contract,packet):0;
            bool packet_changed=centerline_additions!=0;
            auto evidence=nlohmann::json{{"id",packet.value("id",std::string{})},
                {"type",type},
                {"individual_centroid_contracts_added",
                    centerline_additions}};
            if(type=="fillet"||type=="chamfer") {
                const bool add_method=!packet.contains("calculation_method");
                const bool add_end_faces=!packet.contains("route_end_faces");
                // Format 47 predates the selectable calculation path. Its
                // only behavior was the OCCT path, so preserve that authored
                // intent explicitly rather than silently selecting Automatic.
                if(add_method)packet["calculation_method"]="occt";
                const auto& routes=packet.at("routes");
                if(!routes.is_array()||routes.empty())
                    throw std::runtime_error(
                        "Edge-treatment migration found invalid routes");
                if(add_end_faces) {
                    auto ends=nlohmann::json::array();
                    for(std::size_t route=0;route<routes.size();++route)
                        ends.push_back({{"first",nullptr},{"last",nullptr}});
                    packet["route_end_faces"]=std::move(ends);
                }
                packet_changed=packet_changed||add_method||add_end_faces;
                evidence["routes"]=routes.size();
                evidence["calculation_method_added"]=add_method;
                evidence["route_end_faces_added"]=add_end_faces;
            }
            if(packet_changed) {
                const auto replacement=prefix.data()+packet.dump();
                migrated.replace(cursor,value_end-cursor,replacement);
                changed.push_back(std::move(evidence));
                ++change_count;
            }
        }
        const auto next=migrated.find('\n',cursor);
        cursor=next==std::string::npos?migrated.size():next+1;
    }
    bool format_upgraded{};
    if(upgrade_format) {
        constexpr std::string_view old_version="format_version=47";
        std::size_t version_count{};
        for(std::size_t position=migrated.find(old_version);
                position!=std::string::npos;
                position=migrated.find(old_version,position+1)) {
            const bool line_start=position==0||migrated[position-1]=='\n';
            const auto after=position+old_version.size();
            const bool line_end=after==migrated.size()||
                migrated[after]=='\n'||migrated[after]=='\r';
            if(line_start&&line_end)++version_count;
        }
        if(version_count!=1)throw std::runtime_error(
            "Expected exactly one native Part format 47 declaration");
        const auto position=migrated.find(old_version);
        migrated.replace(position,old_version.size(),"format_version=48");
        format_upgraded=true;
    }
    if(change_count==0&&!format_upgraded)throw std::runtime_error(
        "Native Part requires no empty edge-treatment end-face migration");
    // The added authored fields change the calculation fingerprint. A format
    // upgrade also crosses a validation boundary, so never offer its old
    // optional derived packet to the strict reader as current evidence.
    bool cache_removed{};
    if(change_count!=0||format_upgraded) {
        const auto cache=migrated.find("[CachedBodies]");
        if(cache==std::string::npos||
                (cache!=0&&migrated[cache-1]!='\n'))
            throw std::runtime_error("Native Part cache section is missing");
        const auto next_section=migrated.find("\n[",cache+1);
        if(next_section==std::string::npos)
            throw std::runtime_error("Native Part cache section is invalid");
        migrated.erase(cache,next_section+1-cache);
        cache_removed=true;
    }
    std::filesystem::create_directories(output.parent_path());
    std::ofstream native(output,std::ios::binary);
    native.write(migrated.data(),static_cast<std::streamsize>(migrated.size()));
    native.close();
    if(!native)throw std::runtime_error("Cannot write migrated native Part");
    std::filesystem::create_directories(report.parent_path());
    std::ofstream evidence(report);
    evidence<<nlohmann::json{{"migration",upgrade_format
            ?"part-format-47-to-48":"empty-edge-treatment-end-faces"},
        {"format_upgraded",format_upgraded},
        {"stale_derived_cache_removed",cache_removed},
        {"changed_containers",std::move(changed)},
        {"changed_container_count",change_count}}.dump(2)<<'\n';
    if(!evidence)throw std::runtime_error("Cannot write migration evidence");
}
}

int main(int argc,char** argv){try{
    using namespace zima;
    if(argc==5&&std::string_view(argv[1])==
            "--add-empty-edge-treatment-end-faces") {
        migrate_native_part_contract(
            std::filesystem::u8path(argv[2]),
            std::filesystem::u8path(argv[3]),
            std::filesystem::u8path(argv[4]),false);
        return 0;
    }
    if(argc==5&&std::string_view(argv[1])==
            "--upgrade-part-format47-to48") {
        migrate_native_part_contract(
            std::filesystem::u8path(argv[2]),
            std::filesystem::u8path(argv[3]),
            std::filesystem::u8path(argv[4]),true);
        return 0;
    }
    if(argc==4&&std::string_view(argv[1])=="--audit-boundary-properties") {
        std::vector<kernel::BodyResult> boundaries;
        static_cast<void>(document::PartDocument::load(std::filesystem::u8path(argv[2]),&boundaries));
        kernel::OcctKernel engine;auto report=nlohmann::json::array();
        for(auto& boundary:boundaries) {
            engine.ensure_body_properties(boundary);
            report.push_back({{"volume",boundary.volume},{"area",boundary.surface_area},{"current",boundary.physical_properties_current}});
        }
        std::ofstream output(std::filesystem::u8path(argv[3]));output<<report.dump(2)<<'\n';return output?0:1;
    }
    if(argc==5&&std::string_view(argv[1])=="--rewrite-template-fixture") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        const auto prepare=[](auto&){}; // Preserve authored, already embedded glyph contours.
        const auto sketch=drawing::load_template_sketch(source,prepare);
        std::filesystem::create_directories(output.parent_path());drawing::save_template_sketch(sketch,output);
        if(drawing::load_template_sketch(output,prepare).serialized()!=sketch.serialized())
            throw std::runtime_error("Drawing template readback changed its authored Sketch");
        std::ofstream report(std::filesystem::u8path(argv[4]));report<<nlohmann::json{{"native_readback_exact",true},{"sketch_identity_unchanged",true}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==5&&std::string_view(argv[1])=="--rewrite-sketch-fixture") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        std::ifstream input(source);const std::string data((std::istreambuf_iterator<char>(input)),{});
        const auto sketch=sketcher::Sketch::from_serialized(data);sketch.validate();
        std::filesystem::create_directories(output.parent_path());std::ofstream saved(output);saved<<sketch.serialized();saved.close();
        std::ifstream restored(output);const std::string readback((std::istreambuf_iterator<char>(restored)),{});
        if(sketcher::Sketch::from_serialized(readback).serialized()!=sketch.serialized())
            throw std::runtime_error("Sketch fixture readback changed its definition");
        std::ofstream report(std::filesystem::u8path(argv[4]));report<<nlohmann::json{{"native_readback_exact",true},{"sketch_identity_unchanged",true}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==5&&std::string_view(argv[1])==
            "--rewrite-current-part-fixture") {
        const auto source=std::filesystem::u8path(argv[2]);
        const auto output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==
                std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error(
                "Verification output must be separate from its source");
        std::vector<kernel::BodyResult> cached;
        const auto value=document::PartDocument::load(source,&cached);
        std::filesystem::create_directories(output.parent_path());
        value.save(output,cached);
        std::vector<kernel::BodyResult> restored;
        const auto readback=document::PartDocument::load(output,&restored);
        if(readback.serialized()!=value.serialized()||
                restored.size()!=cached.size())
            throw std::runtime_error(
                "Part fixture readback changed its definition or boundary count");
        for(std::size_t index=0;index<cached.size();++index)
            if(document::serialize_body_result(restored[index])!=
                    document::serialize_body_result(cached[index]))
                throw std::runtime_error(
                    "Part fixture readback changed its cached boundary");
        std::ofstream report(std::filesystem::u8path(argv[4]));
        report<<nlohmann::json{{"native_readback_exact",true},
            {"authored_history_unchanged",true},
            {"cached_boundaries_unchanged",true},
            {"calculated_boundaries",cached.size()}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==5&&std::string_view(argv[1])=="--rewrite-native-asset") {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        std::filesystem::create_directories(output.parent_path());
        if(source.extension()==".asmz") {
            const auto value=assembly::AssemblyDocument::load(source);value.save(output);
            if(assembly::AssemblyDocument::load(output).serialized()!=value.serialized())
                throw std::runtime_error("Assembly authoring readback changed its definition");
        } else if(source.extension()==".drwz") {
            const auto value=drawing::DrawingDocument::load(source);value.save(output);
            std::ifstream first(output,std::ios::binary);const std::string original((std::istreambuf_iterator<char>(first)),{});first.close();
            auto readback=output;readback+=".readback.drwz";
            if(std::filesystem::exists(readback))throw std::runtime_error("Readback target must be unused");
            drawing::DrawingDocument::load(output).save(readback);
            std::ifstream second(readback,std::ios::binary);const std::string restored((std::istreambuf_iterator<char>(second)),{});second.close();
            std::filesystem::remove(readback);
            if(restored!=original)throw std::runtime_error("Drawing native readback changed its saved definition");
        } else throw std::runtime_error("Expected Assembly or Drawing asset");
        std::ofstream report(std::filesystem::u8path(argv[4]));
        report<<nlohmann::json{{"native_readback_exact",true}}.dump(2)<<'\n';
        return report?0:1;
    }
    if(argc==4&&std::string_view(argv[1])=="--write-assembly-template") {
        std::ifstream input(std::filesystem::u8path(argv[2]));nlohmann::json packet;input>>packet;
        auto value=assembly::AssemblyDocument::from_serialized(packet);
        if(!value.components.empty()||!value.cuts.empty()||!value.sketches.empty()||!value.constructions.empty())
            throw std::runtime_error("Start Assembly must have no authored history");
        const auto path=std::filesystem::u8path(argv[3]);value.save(path);
        if(assembly::AssemblyDocument::load(path).serialized()!=value.serialized())
            throw std::runtime_error("Assembly start template readback changed its definition");
        std::cout<<"Assembly start template written and reopened with the current serializer\n";
        return 0;
    }
    if(argc==5&&(std::string_view(argv[1])=="--regenerate-asset"||
            std::string_view(argv[1])=="--regenerate-format-asset"||
            std::string_view(argv[1])==
                "--regenerate-format-asset-from-authored-history"||
            std::string_view(argv[1])=="--regenerate-topology-asset"||
            std::string_view(argv[1])=="--reexpress-coordinate-asset")) {
        const auto source=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
        const auto report=std::filesystem::u8path(argv[4]);
        const bool authored_history_format_baseline=
            std::string_view(argv[1])==
                "--regenerate-format-asset-from-authored-history";
        const bool topology_migration=
            std::string_view(argv[1])=="--regenerate-topology-asset";
        const bool format_migration=
            std::string_view(argv[1])=="--regenerate-format-asset"||
            authored_history_format_baseline;
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("Verification output must be separate from its source");
        std::vector<kernel::BodyResult> cached;
        // This explicit recovery mode defines both sides of its oracle from
        // authored history. Do not attach a derived cache whose fingerprint
        // necessarily became stale after a declared definition migration.
        const auto source_authored=authored_history_format_baseline
            ?document::PartDocument::load(source)
            :document::PartDocument::load(source,&cached);
        auto authored=source_authored;
        kernel::OcctKernel engine;std::size_t embedded_forms{};
        std::vector<kernel::BodyResult> authored_history_baseline;
        if(authored_history_format_baseline) {
            workspace::PartCalculationPolicy baseline_policy;
            baseline_policy.reject_errors=true;
            baseline_policy.refresh_physical_properties=true;
            baseline_policy.publish_complete_history=true;
            authored_history_baseline=workspace::calculate_part(
                engine,source_authored,nullptr,baseline_policy);
            for(auto& boundary:authored_history_baseline)
                engine.ensure_body_properties(boundary);
            std::cout<<"Authored-history format baseline calculated: "
                <<authored_history_baseline.size()<<" boundaries"<<std::endl;
        }
        std::map<std::string,double> coordinate_factors;
        const auto& placement_migration_geometry=
            authored_history_format_baseline
                ?authored_history_baseline:cached;
        const auto placement_frame_migrations = format_migration
            ? reparameterize_legacy_single_orientation_placements(
                authored, placement_migration_geometry)
            : nlohmann::json::array();
        if(format_migration||
                std::string_view(argv[1])=="--reexpress-coordinate-asset") {
            embedded_forms=regenerate_embedded_forms(authored,engine,
                std::string_view(argv[1])=="--reexpress-coordinate-asset");
        }
        if(std::string_view(argv[1])=="--reexpress-coordinate-asset") {
            coordinate_factors=reexpress_coordinates(authored);
        }
        std::set<std::string> evaluated_rows;
        if(std::string_view(argv[1])=="--reexpress-coordinate-asset") {
            for(const auto& [row,packet]:authored.family.evaluated)evaluated_rows.insert(row);
            authored.family.evaluated.clear();
        }
        const auto id=authored.document_id;workspace::Workspace live;
        live.add_part(authored,cached,source);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;policy.refresh_physical_properties=true;
        static_cast<void>(workspace::regenerate_part(live,engine,id,policy));
        if(!evaluated_rows.empty())for(const auto& row:document::parse_family_table(authored.family_table).instances)
            if(evaluated_rows.contains(row.id))static_cast<void>(workspace::open_family_instance(live,engine,id,row.name,false));
        const auto* current=live.open_part(id);if(!current)throw std::runtime_error("Factory asset disappeared");
        auto updated=current->session.document();auto calculated=current->session.calculated_boundaries();
        static_cast<void>(workspace::complete_part_physical_analysis(
            engine,updated,calculated,true));
        // Format verification compares every old full history boundary, not
        // only the final Body. Ordinary calculation deliberately publishes
        // compact intermediate packets, so replay a complete offline oracle
        // without changing the packets persisted by normal regeneration.
        auto verification_calculated=calculated;
        if(format_migration) {
            auto verification_policy=policy;
            verification_policy.publish_complete_history=true;
            verification_calculated=workspace::calculate_part(
                engine,updated,nullptr,verification_policy);
            for(auto& boundary:verification_calculated)
                engine.ensure_body_properties(boundary);
            std::cout<<"Migrated complete-history verification calculated: "
                <<verification_calculated.size()<<" boundaries; placement "
                <<"migrations: "<<placement_frame_migrations.size()
                <<std::endl;
        }
        const auto& format_baseline=authored_history_format_baseline
            ?authored_history_baseline:cached;
        const auto calculated_contract_error=format_migration
            ?format_migration_calculated_contract_error(
                format_baseline,verification_calculated)
            :std::string{};
        const bool calculated_contracts_preserved=
            calculated_contract_error.empty();
        if(!calculated_contracts_preserved) {
            nlohmann::json boundary_differences=nlohmann::json::array();
            nlohmann::json reference_field_differences=nlohmann::json::array();
            nlohmann::json reference_parameter_changes=nlohmann::json::array();
            nlohmann::json reference_definition_changes=nlohmann::json::array();
            for(std::size_t index=0;
                    index<std::min(format_baseline.size(),
                        verification_calculated.size());++index) {
                auto before=calculated_reference_contract(
                    format_baseline[index].mesh.original_references);
                auto after=calculated_reference_contract(
                    verification_calculated[index].mesh.original_references);
                if(!approximately_same_contract(before,after))
                    boundary_differences.push_back({
                    {"boundary",index},
                    {"changes",annotated_contract_diff(before,after)}});
                const auto& before_definitions=before.at("references");
                const auto& after_definitions=after.at("references");
                for(std::size_t reference=0;reference<std::min(
                        before_definitions.size(),after_definitions.size());
                        ++reference)
                    if(!approximately_same_contract(before_definitions[reference],
                            after_definitions[reference]))
                        reference_definition_changes.push_back({
                            {"boundary",index},{"index",reference},
                            {"before",before_definitions[reference]},
                            {"after",after_definitions[reference]}});
                const auto& old_geometry=
                    format_baseline[index].mesh.original_references;
                const auto& new_geometry=
                    verification_calculated[index].mesh.original_references;
                const auto same_point=[](const auto& a,const auto& b) {
                    return std::abs(a.x-b.x)<=1e-7&&
                        std::abs(a.y-b.y)<=1e-7&&std::abs(a.z-b.z)<=1e-7;
                };
                for(std::size_t edge=0;edge<std::min(old_geometry.edges.size(),
                        new_geometry.edges.size());++edge) {
                    const auto& a=old_geometry.edges[edge];
                    const auto& b=new_geometry.edges[edge];
                    if(a.points.empty()||b.points.empty()||
                            (same_point(a.points.front(),b.points.front())&&
                             same_point(a.points.back(),b.points.back())))continue;
                    reference_parameter_changes.push_back({{"boundary",index},
                        {"kind","edge"},{"index",edge},
                        {"owner",a.reference.owner_id},
                        {"key",a.reference.semantic_key},
                        {"parameter_seam_before",a.parameter_seam},
                        {"parameter_seam_after",b.parameter_seam}});
                }
                for(std::size_t point=0;point<std::min(old_geometry.points.size(),
                        new_geometry.points.size());++point) {
                    const auto& a=old_geometry.points[point];
                    const auto& b=new_geometry.points[point];
                    if(same_point(a.position,b.position))continue;
                    reference_parameter_changes.push_back({{"boundary",index},
                        {"kind","point"},{"index",point},
                        {"owner",a.reference.owner_id},
                        {"key",a.reference.semantic_key}});
                }
                const auto old_packet=document::serialize_body_result(
                    format_baseline[index]);
                const auto new_packet=document::serialize_body_result(
                    verification_calculated[index]);
                for(const auto* field:{"face_references","edge_references",
                        "point_references","axes","dimensions",
                        "solid_state_reference_views","sheet_cuts"}) {
                    const auto old_value=old_packet.value(field,nlohmann::json(nullptr));
                    const auto new_value=new_packet.value(field,nlohmann::json(nullptr));
                    if(old_value!=new_value)
                        reference_field_differences.push_back({
                            {"boundary",index},{"field",field},
                            {"changes",nlohmann::json::diff(old_value,new_value)}});
                }
            }
            std::filesystem::create_directories(report.parent_path());
            std::ofstream diagnostic(report);
            diagnostic<<nlohmann::json{
                {"calculated_contract_error",calculated_contract_error},
                {"format_migration_baseline",authored_history_format_baseline
                    ?"recalculated-authored-history":"persisted-cache"},
                {"placement_frame_migrations",placement_frame_migrations},
                {"original_reference_changes",boundary_differences},
                {"reference_field_changes",reference_field_differences},
                {"reference_definition_changes",reference_definition_changes},
                {"reference_parameter_changes",reference_parameter_changes}}
                .dump(2)<<'\n';
            throw std::runtime_error(
                "Format migration changed calculated contracts: "+
                calculated_contract_error);
        }
        nlohmann::json topology_changes=nlohmann::json::array();
        const bool authored_definition_preserved=!topology_migration||
            topology_migration_preserves_authored_definition(
                authored,updated,topology_changes);
        if((topology_migration&&(!same_history_structure(authored,updated)||
                    !authored_definition_preserved))||
                (!topology_migration&&(comparable_history(updated.history)!=
                    comparable_history(authored.history)||
                    updated.body_history!=authored.body_history||
                    updated.sketches.size()!=authored.sketches.size()))) {
            std::filesystem::create_directories(report.parent_path());
            std::ofstream diagnostic(report);diagnostic<<nlohmann::json{{"authored_definition_changes",
                nlohmann::json::diff(authored.serialized(),updated.serialized())}}.dump(2)<<'\n';
            throw std::runtime_error("Factory regeneration changed authored history, sides or Body ownership");
        }
        for(std::size_t i=0;!topology_migration&&i<authored.sketches.size();++i)
            if(authored.sketches[i].serialized()!=updated.sketches[i].serialized())
                throw std::runtime_error("Factory regeneration changed authored Sketch geometry or references");
        std::filesystem::create_directories(output.parent_path());updated.save(output,calculated);
        std::vector<kernel::BodyResult> restored;const auto reopened=document::PartDocument::load(output,&restored);
        if(reopened.serialized()!=updated.serialized()||restored.size()!=calculated.size())
            throw std::runtime_error("Factory native readback changed definition or boundary count");
        for(std::size_t i=0;i<calculated.size();++i)
            if(document::serialize_body_result(restored[i])!=document::serialize_body_result(calculated[i]))
                throw std::runtime_error("Factory native readback lost geometry or side identities");
        nlohmann::json evidence={{"calculated_boundaries",calculated.size()},{"history_count",updated.history.size()},
            {"authored_history_unchanged",!topology_migration&&
                placement_frame_migrations.empty()},
            {"authored_history_preserved_after_declared_migration",
                !topology_migration},
            {"placement_frame_reparameterized",
                !placement_frame_migrations.empty()},
            {"placement_frame_migrations",placement_frame_migrations},
            {"authored_structure_unchanged",true},
            {"sketches_unchanged",!topology_migration},
            {"sketch_entity_id_sets_unchanged",true},
            {"topology_reference_migration",topology_migration},
            {"format_migration",format_migration},
            {"format_migration_baseline",authored_history_format_baseline
                ?"recalculated-authored-history":"persisted-cache"},
            {"persisted_cache_contract_checked",
                !authored_history_format_baseline},
            {"calculated_contracts_preserved",
                calculated_contracts_preserved},
            {"native_readback_exact",true}};
        if(topology_migration) {
            evidence["authored_definition_change_count"]=topology_changes.size();
            evidence["authored_definition_change_paths"]=nlohmann::json::array();
            for(const auto& change:topology_changes)
                evidence["authored_definition_change_paths"].push_back(
                    change.value("path",std::string{}));
            evidence["authored_reference_identities_unchanged"]=true;
            evidence["resolved_reference_state_improvements"]=
                std::ranges::count_if(topology_changes,
                    resolved_reference_state_improvement);
        }
        evidence["coordinate_reexpression"]=coordinate_factors;
        evidence["embedded_form_definitions_recalculated"]=embedded_forms;
        if(!calculated.empty()) {
            evidence["volume"]=calculated.back().volume;evidence["area"]=calculated.back().surface_area;
            evidence["physical_properties_current"]=calculated.back().physical_properties_current;
            evidence["errors"]=calculated.back().calculation_errors;
        }
        if(!cached.empty()){evidence["previous_volume"]=cached.back().volume;evidence["previous_area"]=cached.back().surface_area;}
        evidence["parameters"]=updated.serialized().value("user_parameter_values",nlohmann::json::object());
        std::ofstream stream(report);stream<<evidence.dump(2)<<'\n';stream.close();
        if(!stream)throw std::runtime_error("Cannot write factory verification evidence");
        return 0;
    }
    const auto root=std::filesystem::current_path()/"config/templates";
    for(const auto* name:{"START_PART_mm.prtz","START_PART_in.prtz","START_SKELETON_mm.prtz","START_SKELETON_in.prtz"}) {
        const auto path=root/name;auto part=document::PartDocument::load(path);
        if(!part.history.empty()||!part.body_history.order().empty())throw std::runtime_error("Start template must have no modeling history");
        part.save(path);static_cast<void>(document::PartDocument::load(path));
    }
    for(const auto* name:{"START_ASSEMBLY_mm.asmz","START_ASSEMBLY_in.asmz"}) {
    const auto path=root/name;auto assembly=assembly::AssemblyDocument::load(path);
    if(!assembly.components.empty())throw std::runtime_error("Start Assembly must have no components");
    assembly.save(path);static_cast<void>(assembly::AssemblyDocument::load(path));
    }
    if(argc==2&&std::string_view(argv[1])=="--all-factory") {
        kernel::OcctKernel engine;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root.parent_path())) {
            if(!entry.is_regular_file()||entry.path().parent_path()==root)continue;
            const auto extension=entry.path().extension();
            if(extension!=".prtz"&&extension!=".frmz"&&extension!=".tblz"&&extension!=".symz")continue;
            const auto part=document::PartDocument::load(entry.path());
            const auto authored=part.serialized();
            const auto calculated=engine.evaluate_history(part.kernel_operations());
            part.save(entry.path(),calculated);
            std::vector<kernel::BodyResult> restored;
            const auto reopened=document::PartDocument::load(entry.path(),&restored);
            if(reopened.serialized()!=authored||restored.size()!=calculated.size())
                throw std::runtime_error("Factory asset changed its authored definition or cache count: "+entry.path().string());
            for(std::size_t i=0;i<calculated.size();++i)
                if(document::serialize_body_result(restored[i])!=document::serialize_body_result(calculated[i]))
                    throw std::runtime_error("Factory asset lost calculated geometry or reference data: "+entry.path().string());
            std::cout<<entry.path().filename().string()<<": calculated, saved and reopened\n";
        }
    }
    std::cout<<"Native start templates rewritten and reopened with the current serializer\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
