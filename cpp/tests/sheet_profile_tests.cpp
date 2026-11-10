#include <zima/workspace/sheet_profile_operations.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/sheet_state_operations.hpp>

#include <zima/document/bend.hpp>
#include <zima/document/sheet_profile.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace {
using namespace zima;

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

struct Fixture {
    document::PartDocument part=document::PartDocument::create_default();
    sketcher::Sketch driver=sketcher::Sketch::create_default();
    document::HistoryContainer profile;
    Fixture() {
        document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Body"));
        part.set_body_history(std::move(graph));
        profile=document::PartDocument::create_sheet_profile_container(driver.id);
        driver.owner_container_id=profile.id;driver.name=profile.name;
        profile.sheet_profile.width.symmetric=true;
        profile.sheet_profile.width.sides[0].length=10;
        profile.sheet_profile.thickness=1;
        profile.sheet_profile.thickness_override=true;
    }
    void line_arc_line(double radius=5) {
        static_cast<void>(driver.add_segment(0,0,20,0));
        static_cast<void>(driver.add_arc(20,radius,20,0,20+radius,radius));
        static_cast<void>(driver.add_segment(20+radius,radius,20+radius,25));
    }
};

const document::HistoryContainer& child(const workspace::PreparedSheetProfile& value,
        std::size_t logical) {
    const auto* result=value.document.find_container(value.logical_children.at(logical));
    if(!result)throw std::runtime_error("Prepared Sheet Profile child is missing");
    return *result;
}

std::pair<double,double> flat_width_interval(
        const document::PartDocument& part,
        const document::HistoryContainer& flat) {
    require(flat.feature_kind==document::FeatureKind::Flat,
        "Sheet Profile width probe is not a Flat");
    const auto sketch=std::ranges::find(part.sketches,flat.flat.sketch_id,
        &sketcher::Sketch::id);
    require(sketch!=part.sketches.end()&&!sketch->points.empty(),
        "Sheet Profile width probe lost its Flat Sketch");
    const auto [first,last]=std::ranges::minmax_element(
        sketch->points,{},&sketcher::SketchPoint::x);
    return {first->x,last->x};
}

void calculate(const document::PartDocument& part) {
    kernel::OcctKernel kernel;
    const auto calculated=kernel.evaluate_history(part.kernel_operations());
    require(!calculated.empty(),"Sheet Profile produced no calculated boundary");
    require(calculated.back().calculation_errors.empty(),
        "Sheet Profile produced a calculation error");
    const auto body=calculated.back().body_outputs.find(
        part.body_history.bodies().front().scope.id);
    require(body!=calculated.back().body_outputs.end()&&
        !body->second->kernel_shape.empty(),"Sheet Profile produced no Body");
}

void free_flat_bend_flat() {
    Fixture fixture;fixture.line_arc_line();
    auto prepared=workspace::prepare_sheet_profile(fixture.part,
        fixture.profile,fixture.driver);
    require(prepared.logical_children.size()==3,
        "Line-arc-line Profile did not create three native features");
    require(child(prepared,0).feature_kind==document::FeatureKind::Flat&&
        child(prepared,1).feature_kind==document::FeatureKind::Bend&&
        child(prepared,2).feature_kind==document::FeatureKind::Flat,
        "Profile did not preserve logical Flat/Bend/Flat order");
    const auto* parent=prepared.document.find_container(fixture.profile.id);
    require(parent&&parent->sheet_profile.master_feature_id==
        prepared.logical_children.front(),
        "Free Profile width is not driven by its first Flat");
    require(std::abs(child(prepared,1).bend.radius-4)<1e-9&&
        std::abs(child(prepared,1).bend.angle_degrees-90)<1e-9,
        "Outer Profile arc did not map to ordinary Bend inner radius R-T");
    require(child(prepared,0).flat.thickness_override&&
        std::abs(child(prepared,0).flat.thickness-1)<1e-9,
        "First Flat did not own common Profile thickness");
    require(child(prepared,2).flat.direction==
            document::ExtrusionDirection::Reverse,
        "Flat following a Profile Bend was extruded away from the Bend material");
    static_cast<void>(prepared.document.serialized());
    const auto reopened=document::PartDocument::from_serialized(
        prepared.document.serialized());
    const auto* reopened_parent=reopened.find_container(fixture.profile.id);
    require(reopened_parent&&reopened_parent->sheet_profile==
            parent->sheet_profile&&
        document::sheet_profile_route(*std::ranges::find(reopened.sketches,
            reopened_parent->sheet_profile.sketch_id,&sketcher::Sketch::id),
            reopened_parent->sheet_profile.start_point_id).spans.size()==3,
        "Sheet Profile identities or route did not survive save/reopen");
    calculate(prepared.document);

    auto flipped=*parent;flipped.sheet_profile.material_side=-1;
    const auto driver=*std::ranges::find(prepared.document.sketches,
        flipped.sheet_profile.sketch_id,&sketcher::Sketch::id);
    auto regenerated=workspace::prepare_sheet_profile(prepared.document,
        flipped,driver);
    require(regenerated.logical_children==prepared.logical_children,
        "Profile regeneration changed stable child identities");
    require(std::abs(child(regenerated,1).bend.radius-5)<1e-9,
        "Inner Profile arc did not map to ordinary Bend radius R");
    require(child(regenerated,0).flat.direction==
        document::ExtrusionDirection::Reverse,
        "Profile material-side flip did not reverse its Flats");
    require(child(regenerated,2).flat.direction==
            document::ExtrusionDirection::Reverse,
        "Profile side flip reapplied material direction after a Bend");
    calculate(regenerated.document);
}

void group_history_contracts() {
    Fixture fixture;fixture.line_arc_line();
    auto prepared=workspace::prepare_sheet_profile(fixture.part,
        fixture.profile,fixture.driver);
    const auto parent_id=fixture.profile.id;
    const auto child_ids=prepared.logical_children;
    const auto suppressed=workspace::part_history_dependency_closure(
        prepared.document,parent_id,true);
    require(std::ranges::all_of(child_ids,[&](const auto& id) {
        return suppressed.contains(id);
    }),"Profile suppression dependency did not include every generated child");
    const auto plan=workspace::plan_history_deletion(
        prepared.document,parent_id);
    require(plan.removed.contains(parent_id)&&
        std::ranges::all_of(child_ids,[&](const auto& id) {
            return plan.removed.contains(id);
        }),"Profile deletion plan did not remove its complete native group");
    auto deleted=prepared.document;deleted.erase_history_object(parent_id);
    require(!deleted.find_container(parent_id)&&
        std::ranges::none_of(child_ids,[&](const auto& id) {
            return deleted.find_container(id)!=nullptr;
        })&&deleted.history_order.empty()&&deleted.sketches.empty(),
        "Profile deletion left generated history or owned Sketches behind");
    bool rejected=false;
    try {
        static_cast<void>(workspace::plan_history_deletion(
            prepared.document,child_ids.front()));
    } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,
        "Deleting one generated Profile child was not rejected");
}

void arc_first_and_flat_only() {
    {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_arc(0,5,0,0,5,5));
        auto prepared=workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver);
        require(prepared.logical_children.size()==1&&
            child(prepared,0).feature_kind==document::FeatureKind::Bend,
            "Arc-first Profile did not create a master Bend");
        const auto* parent=prepared.document.find_container(fixture.profile.id);
        require(parent&&parent->sheet_profile.master_feature_id==
            prepared.logical_children.front(),
            "Arc-first Profile is not driven by its first Bend");
        calculate(prepared.document);
    }
    {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_segment(0,0,30,0));
        auto prepared=workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver);
        require(prepared.logical_children.size()==1&&
            child(prepared,0).feature_kind==document::FeatureKind::Flat,
            "Straight free Profile did not create one master Flat");
        calculate(prepared.document);
    }
}

void free_width_directions() {
    struct WidthCase {bool reversed;bool symmetric;double length;
        double expected_first;double expected_last;};
    const std::array cases{
        WidthCase{false,false,24,0,24},
        WidthCase{true,false,24,-24,0},
        WidthCase{false,true,12,-12,12}};
    for(const auto& value:cases) {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_segment(0,0,30,0));
        auto& width=fixture.profile.sheet_profile.width;
        width.reversed_sides=value.reversed;
        width.symmetric=value.symmetric;
        width.sides[0].operation=
            document::FeatureSideOperation::Extrusion;
        width.sides[0].length=value.length;
        width.sides[1].operation=
            document::FeatureSideOperation::None;
        auto prepared=workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver);
        const auto interval=flat_width_interval(prepared.document,
            child(prepared,0));
        require(std::abs(interval.first-value.expected_first)<1e-9&&
                std::abs(interval.second-value.expected_last)<1e-9,
            "Free Sheet Profile width direction produced the wrong interval");
        const auto reopened=document::PartDocument::from_serialized(
            prepared.document.serialized());
        const auto* parent=reopened.find_container(fixture.profile.id);
        require(parent&&parent->sheet_profile.width==width,
            "Free Sheet Profile width direction did not survive save/reopen");
    }

    {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_segment(0,0,30,0));
        auto& width=fixture.profile.sheet_profile.width;
        width.symmetric=false;width.reversed_sides=false;
        width.sides[0].operation=document::FeatureSideOperation::Extrusion;
        width.sides[0].length=24;
        width.sides[1].operation=document::FeatureSideOperation::Extrusion;
        width.sides[1].length=8;
        const auto prepared=workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver);
        const auto interval=flat_width_interval(prepared.document,
            child(prepared,0));
        require(std::abs(interval.first+8)<1e-9&&
                std::abs(interval.second-24)<1e-9,
            "Independent Sheet Profile width branches produced the wrong interval");
    }

    Fixture fixture;
    static_cast<void>(fixture.driver.add_segment(0,0,30,0));
    auto prepared=workspace::prepare_sheet_profile(fixture.part,
        fixture.profile,fixture.driver);
    auto edited=*prepared.document.find_container(fixture.profile.id);
    edited.sheet_profile.width.symmetric=false;
    edited.sheet_profile.width.reversed_sides=false;
    edited.sheet_profile.width.sides[0].operation=
        document::FeatureSideOperation::Extrusion;
    edited.sheet_profile.width.sides[0].length=30;
    edited.sheet_profile.width.sides[1].operation=
        document::FeatureSideOperation::None;
    const auto driver=*std::ranges::find(prepared.document.sketches,
        edited.sheet_profile.sketch_id,&sketcher::Sketch::id);
    auto regenerated=workspace::prepare_sheet_profile(prepared.document,
        edited,driver);
    const auto interval=flat_width_interval(regenerated.document,
        child(regenerated,0));
    require(std::abs(interval.first)<1e-9&&
            std::abs(interval.second-30)<1e-9,
        "Profile Properties width edit did not reset the master interval");
    calculate(regenerated.document);
}

void free_up_to_targets() {
    Fixture fixture;
    static_cast<void>(fixture.driver.add_segment(0,0,30,0));
    auto& width=fixture.profile.sheet_profile.width;
    width.symmetric=false;width.reversed_sides=false;
    width.sides[0].operation=document::FeatureSideOperation::Extrusion;
    width.sides[0].extrusion_extent=document::EndCondition::UpTo;
    document::ExtrusionParameters::EndTarget point;
    point.kind=document::EndTargetKind::Point;
    point.reference={"stop-sketch","point:forward",{}};
    point.label="Forward point";
    width.sides[0].targets={point};
    width.sides[1].operation=document::FeatureSideOperation::Extrusion;
    width.sides[1].extrusion_extent=document::EndCondition::UpTo;
    document::ExtrusionParameters::EndTarget plane;
    plane.kind=document::EndTargetKind::Face;
    plane.reference={"stop-plane","plane",{}};
    plane.label="Reverse plane";
    width.sides[1].targets={plane};
    width.sides[1].up_to.length_correction=3;

    kernel::ViewerReferenceGeometry geometry;
    geometry.points.push_back({{0,0,30},
        {"stop-sketch","point:forward",{}},"Forward point"});
    geometry.vertices={{-10,-10,-12},{10,-10,-12},{0,10,-12}};
    geometry.triangles={0,1,2};
    kernel::FaceReference plane_reference;
    plane_reference.owner_id="stop-plane";
    plane_reference.semantic_key="plane";
    auto surface=std::make_shared<kernel::SurfaceGeometry>();
    surface->kind=kernel::SurfaceGeometry::Kind::Plane;
    surface->origin={0,0,-12};surface->axis={0,0,1};
    plane_reference.surface=surface;
    geometry.triangle_references={plane_reference};

    const auto prepared=workspace::prepare_sheet_profile(fixture.part,
        fixture.profile,fixture.driver,geometry);
    const auto interval=flat_width_interval(prepared.document,
        child(prepared,0));
    require(std::abs(interval.first+15)<1e-9&&
            std::abs(interval.second-30)<1e-9,
        "Point/plane Up-to targets produced the wrong Profile interval");
    const auto* parent=prepared.document.find_container(fixture.profile.id);
    require(parent&&parent->sheet_profile.width.sides[0].targets.size()==1&&
            parent->sheet_profile.width.sides[0].targets.front().kind==
                document::EndTargetKind::Point&&
            parent->sheet_profile.width.sides[1].targets.size()==1&&
            parent->sheet_profile.width.sides[1].targets.front().kind==
                document::EndTargetKind::Plane,
        "Profile Up-to target kinds were not retained after resolution");
    const auto reopened=document::PartDocument::from_serialized(
        prepared.document.serialized());
    require(reopened.find_container(fixture.profile.id)->sheet_profile.width==
            parent->sheet_profile.width,
        "Profile Up-to targets did not survive save/reopen");

    auto invalid=fixture.profile;
    auto invalid_surface=std::make_shared<kernel::SurfaceGeometry>(*surface);
    invalid_surface->origin={-12,0,0};invalid_surface->axis={1,0,0};
    geometry.vertices={{-12,-10,-10},{-12,10,-10},{-12,0,10}};
    geometry.triangle_references.front().surface=invalid_surface;
    bool rejected=false;
    try {
        static_cast<void>(workspace::prepare_sheet_profile(fixture.part,
            invalid,fixture.driver,geometry));
    } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,
        "Profile accepted an Up-to plane that intersects its route plane");
}

void master_edit_propagation() {
    {
        Fixture fixture;fixture.line_arc_line();
        auto prepared=workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver);
        auto master=child(prepared,0);
        auto master_sketch=*std::ranges::find(prepared.document.sketches,
            master.flat.sketch_id,&sketcher::Sketch::id);
        for(auto& point:master_sketch.points)if(point.y>10)point.y=30;
        auto regenerated=workspace::prepare_sheet_profile_master_edit(
            prepared.document,master,master_sketch);
        require(regenerated.logical_children==prepared.logical_children,
            "Master Flat edit changed stable Profile child identities");
        const auto* parent=regenerated.document.find_container(
            fixture.profile.id);
        const auto driver=std::ranges::find(regenerated.document.sketches,
            parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
        const auto route=document::sheet_profile_route(*driver,
            parent->sheet_profile.start_point_id);
        require(std::abs(route.spans.front().length-30)<1e-8,
            "Master Flat length did not propagate to the Profile route");
        bool rejected=false;
        try {
            auto later=child(regenerated,1);
            const auto later_sketch=*std::ranges::find(
                regenerated.document.sketches,later.bend.sketch_id,
                &sketcher::Sketch::id);
            static_cast<void>(workspace::prepare_sheet_profile_master_edit(
                regenerated.document,later,later_sketch));
        } catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,
            "A non-master Profile child accepted independent native editing");
        calculate(regenerated.document);
    }
    {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_arc(0,5,0,0,5,5));
        auto prepared=workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver);
        auto master=child(prepared,0);
        auto master_sketch=*std::ranges::find(prepared.document.sketches,
            master.bend.sketch_id,&sketcher::Sketch::id);
        master.bend.radius=7;master.bend.angle_degrees=45;
        auto regenerated=workspace::prepare_sheet_profile_master_edit(
            prepared.document,master,master_sketch);
        const auto* parent=regenerated.document.find_container(
            fixture.profile.id);
        const auto driver=std::ranges::find(regenerated.document.sketches,
            parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
        const auto route=document::sheet_profile_route(*driver,
            parent->sheet_profile.start_point_id);
        require(std::abs(route.spans.front().radius-8)<1e-8&&
            std::abs(route.spans.front().turn_degrees-45)<1e-8,
            "Master Bend radius/angle did not propagate to the Profile arc");
        calculate(regenerated.document);
    }
}

void attached_profile_seed_and_history() {
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Body"));
    part.set_body_history(std::move(graph));
    auto stock=document::PartDocument::create_sketch_container();
    stock.feature_kind=document::FeatureKind::Flat;
    stock.flat.thickness=2.5;stock.flat.thickness_override=true;
    auto outline=sketcher::Sketch::create_default();
    outline.owner_container_id=stock.id;stock.flat.sketch_id=outline.id;
    static_cast<void>(outline.add_rectangle(0,0,60,40));
    part.insert_history_entry(document::PartHistoryKind::Feature,stock.id);
    part.history.push_back(stock);part.sketches.push_back(outline);
    kernel::OcctKernel kernel;
    auto baseline=workspace::calculate_part_with_resolved_references(
        kernel,part);
    const auto& geometry=baseline.back().mesh.original_references;
    const kernel::ViewerEdge* boundary{};
    std::vector<document::ConstructionReference> references;
    double frame_side{};
    for(const auto& edge:geometry.edges)try {
        auto candidate=document::bend_sheet_references(edge);
        const auto side=document::bend_attachment_profile_direction(
            candidate,geometry);
        if(side) {
            boundary=&edge;references=std::move(candidate);
            frame_side=*side;break;
        }
    } catch(const std::exception&) {}
    require(boundary,"Attached Profile fixture has no valid sheet boundary");
    const double thickness=boundary->edge_treatment_side_references.front().sheet_thickness;
    require(std::abs(thickness-2.5)<1e-9,
        "Attached Profile fixture lost its sheet thickness");

    const auto close=[](kernel::Vec3 a,kernel::Vec3 b) {
        return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z)<1e-8;
    };
    const auto cross=[](kernel::Vec3 a,kernel::Vec3 b) {
        return kernel::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,
            a.x*b.y-a.y*b.x};
    };
    const auto unit=[](kernel::Vec3 value) {
        const auto length=std::hypot(std::hypot(value.x,value.y),value.z);
        return kernel::Vec3{value.x/length,value.y/length,value.z/length};
    };
    const auto verify_bend_route_frame=[&](
            const kernel::ViewerReferenceGeometry& current_geometry,
            const kernel::ViewerEdge& current_edge,
            const kernel::VertexReference& endpoint) {
        const auto current_references=document::bend_sheet_references(
            current_edge,endpoint);

        auto profile_route=sketcher::Sketch::create_default();
        auto attached=document::PartDocument::create_sheet_profile_container(
            profile_route.id);
        profile_route.owner_container_id=attached.id;
        attached.sheet_profile.start_point_id=
            document::initialize_attached_sheet_profile(
                profile_route,thickness);
        attached.sheet_profile.sheet_attachment=true;
        attached.placement.references=current_references;
        document::PartDocument profile_carrier;
        profile_carrier.document_id=part.document_id;
        profile_carrier.history={attached};
        profile_carrier.sketches={profile_route};
        profile_carrier.resolve_constructions(current_geometry);
        const auto& resolved_route=profile_carrier.sketches.front();

        auto bend=document::PartDocument::create_sketch_container();
        bend.feature_kind=document::FeatureKind::Bend;
        auto bend_start=sketcher::Sketch::create_default();
        bend_start.owner_container_id=bend.id;
        document::initialize_bend_start_profile(bend_start,40);
        bend.bend.sketch_id=bend_start.id;
        bend.bend.sheet_attachment=true;
        bend.placement.references=current_references;
        document::PartDocument bend_carrier;
        bend_carrier.document_id=part.document_id;
        bend_carrier.history={bend};bend_carrier.sketches={bend_start};
        bend_carrier.resolve_constructions(current_geometry);
        const auto& resolved_bend=bend_carrier.sketches.front();
        const auto& line=*std::ranges::find_if(resolved_bend.segments,
            [](const auto& value){return !value.construction;});
        const auto* first=resolved_bend.find_point(line.first_point_id);
        const auto* last=resolved_bend.find_point(line.second_point_id);
        require(first&&last,"Resolved Bend attachment lost its start line");
        const auto first_world=resolved_bend.world_point(first->x,first->y);
        const auto last_world=resolved_bend.world_point(last->x,last->y);
        const auto along=unit({last_world.x-first_world.x,
            last_world.y-first_world.y,last_world.z-first_world.z});
        const auto inward=cross(resolved_bend.resolved_normal,along);
        const kernel::Vec3 route_normal{-along.x,-along.y,-along.z};
        require(close(resolved_route.resolved_origin,
                    resolved_bend.resolved_origin)&&
                close(resolved_route.resolved_x_axis,
                    resolved_bend.resolved_normal)&&
                close(resolved_route.resolved_y_axis,inward)&&
                close(resolved_route.resolved_normal,route_normal),
            "Attached Sheet Profile frame differs from Bend trajectory");
        const auto endpoint_index=current_edge.edge_treatment_endpoint_references[1]
                ==endpoint?1u:0u;
        require(close(resolved_route.resolved_origin,
                endpoint_index?current_edge.points.back():
                    current_edge.points.front()),
            "Attached Sheet Profile origin is not the selected Bend endpoint");

        // The selected edge, rather than the free-Profile width setting,
        // drives every generated span. Exercise both endpoint selections and
        // the reversed ViewerEdge packet so an attached Flat cannot grow from
        // the Bend origin in the opposite direction.
        attached.sheet_profile.width.symmetric=false;
        attached.sheet_profile.width.sides[0].length=7;
        auto prepared=workspace::prepare_sheet_profile(part,attached,
            profile_route,current_geometry);
        require(prepared.logical_children.size()==2,
            "Attached Profile edge-width matrix lost its Bend/Flat route");
        const auto* stored_parent=prepared.document.find_container(attached.id);
        const auto* stored_bend=prepared.document.find_container(
            prepared.logical_children.front());
        const auto* stored_flat=prepared.document.find_container(
            prepared.logical_children.back());
        require(stored_parent&&stored_bend&&stored_flat&&
                stored_parent->sheet_profile.master_feature_id==stored_bend->id&&
                stored_bend->feature_kind==document::FeatureKind::Bend&&
                stored_flat->feature_kind==document::FeatureKind::Flat,
            "Attached Profile did not select its first Bend as the driver");
        const auto flat_sketch=std::ranges::find(prepared.document.sketches,
            stored_flat->flat.sketch_id,&sketcher::Sketch::id);
        require(flat_sketch!=prepared.document.sketches.end(),
            "Attached Profile Flat lost its generated Sketch");
        const auto [flat_first,flat_last]=std::ranges::minmax_element(
            flat_sketch->points,{},&sketcher::SketchPoint::x);
        double expected_first=std::numeric_limits<double>::infinity();
        double expected_last=-std::numeric_limits<double>::infinity();
        const auto coordinate=[&](kernel::Vec3 value) {
            const kernel::Vec3 delta{
                value.x-flat_sketch->resolved_origin.x,
                value.y-flat_sketch->resolved_origin.y,
                value.z-flat_sketch->resolved_origin.z};
            return delta.x*flat_sketch->resolved_x_axis.x+
                delta.y*flat_sketch->resolved_x_axis.y+
                delta.z*flat_sketch->resolved_x_axis.z;
        };
        for(const auto point:current_edge.points) {
            const double value=coordinate(point);
            expected_first=std::min(expected_first,value);
            expected_last=std::max(expected_last,value);
        }
        double route_first=std::numeric_limits<double>::infinity();
        double route_last=-std::numeric_limits<double>::infinity();
        for(const auto point:current_edge.points) {
            const kernel::Vec3 delta{
                point.x-resolved_route.resolved_origin.x,
                point.y-resolved_route.resolved_origin.y,
                point.z-resolved_route.resolved_origin.z};
            const double value=
                delta.x*resolved_route.resolved_normal.x+
                delta.y*resolved_route.resolved_normal.y+
                delta.z*resolved_route.resolved_normal.z;
            route_first=std::min(route_first,value);
            route_last=std::max(route_last,value);
        }
        require(std::abs(flat_first->x-expected_first)<1e-7&&
                std::abs(flat_last->x-expected_last)<1e-7&&
                !stored_parent->sheet_profile.width.symmetric&&
                stored_parent->sheet_profile.width.sides[0].operation==
                    document::FeatureSideOperation::Extrusion&&
                stored_parent->sheet_profile.width.sides[1].operation==
                    document::FeatureSideOperation::None&&
                std::abs(stored_parent->sheet_profile.width.sides[0].length-
                    (route_last-route_first))<1e-7&&
                stored_parent->sheet_profile.width.side_direction_sign(0)==
                    (std::abs(route_first)<1e-7?1.:-1.),
            "Attached Profile Flat did not inherit the complete Bend edge width");
        const auto bend_end=sketcher::Sketch::from_serialized(
            stored_bend->bend.auxiliary_sketches[1]);
        const kernel::Vec3 opposite_end_y{
            -bend_end.resolved_y_axis.x,
            -bend_end.resolved_y_axis.y,
            -bend_end.resolved_y_axis.z};
        require(close(bend_end.resolved_origin,
                    flat_sketch->resolved_origin)&&
                close(bend_end.resolved_x_axis,
                    flat_sketch->resolved_x_axis)&&
                close(bend_end.resolved_normal,
                    flat_sketch->resolved_y_axis)&&
                close(opposite_end_y,
                    flat_sketch->resolved_normal),
            "Attached Profile Flat frame does not follow its driving Bend end");
    };
    for(const auto& endpoint:boundary->edge_treatment_endpoint_references)
        verify_bend_route_frame(geometry,*boundary,endpoint);
    auto reversed_geometry=geometry;
    auto reversed_boundary=std::ranges::find_if(reversed_geometry.edges,
        [&](const auto& edge){return edge.reference==boundary->reference;});
    require(reversed_boundary!=reversed_geometry.edges.end(),
        "Attached Profile reversed-edge fixture lost its boundary");
    std::ranges::reverse(reversed_boundary->points);
    std::ranges::reverse(
        reversed_boundary->edge_treatment_endpoint_references);
    if(reversed_boundary->exact_spline) {
        std::ranges::reverse(reversed_boundary->exact_spline->poles);
        kernel::reverse_bspline_parameters(
            reversed_boundary->exact_spline->knots,
            reversed_boundary->exact_spline->weights);
    }
    for(const auto& endpoint:
            reversed_boundary->edge_treatment_endpoint_references)
        verify_bend_route_frame(reversed_geometry,*reversed_boundary,endpoint);

    auto route=sketcher::Sketch::create_default();
    auto profile=document::PartDocument::create_sheet_profile_container(
        route.id);
    route.owner_container_id=profile.id;route.name=profile.name;
    profile.sheet_profile.start_point_id=
        document::initialize_attached_sheet_profile(route,thickness);
    const auto radius_dimension_id=route.id+":profile:radius";
    const auto length_dimension_id=route.id+":profile:length";
    const auto angle_dimension_id=route.id+":profile:angle";
    const auto verify_seed_dimensions=[&](const sketcher::Sketch& candidate,
            double radius_value,double length_value,bool locked=false,
            double angle_value=90.) {
        const auto radius=std::ranges::find(candidate.dimensions,
            radius_dimension_id,&sketcher::SketchDimension::id);
        const auto length=std::ranges::find(candidate.dimensions,
            length_dimension_id,&sketcher::SketchDimension::id);
        const auto angle=std::ranges::find(candidate.dimensions,
            angle_dimension_id,&sketcher::SketchDimension::id);
        const bool valid=radius!=candidate.dimensions.end()&&
                length!=candidate.dimensions.end()&&
                angle!=candidate.dimensions.end()&&
                radius->kind==sketcher::DimensionKind::Radius&&
                length->kind==sketcher::DimensionKind::Distance&&
                angle->kind==sketcher::DimensionKind::AngleThreePoint&&
                radius->driving&&length->driving&&angle->driving&&
                radius->locked==locked&&length->locked==locked&&
                angle->locked==locked&&
                std::abs(radius->value-radius_value)<1e-8&&
                std::abs(length->value-length_value)<1e-8&&
                std::abs(angle->value-angle_value)<1e-8;
        if(!valid&&radius!=candidate.dimensions.end()&&
                length!=candidate.dimensions.end()&&
                angle!=candidate.dimensions.end())
            std::cerr<<"Profile dimension state R="<<radius->value
                <<" L="<<length->value<<" A="<<angle->value
                <<" locks="<<radius->locked<<length->locked<<angle->locked
                <<" expected="<<radius_value<<','<<length_value<<','
                <<angle_value<<" lock="<<locked<<'\n';
        require(valid,
            "Profile seed lacks editable radius, angle or line-length dimensions");
        const auto& first_arc=candidate.arcs.front();
        require(std::ranges::any_of(candidate.constraints,[&](const auto& value) {
                    return !value.suppressed&&value.kind==
                            sketcher::ConstraintKind::PointReference&&
                        value.first_point_id==first_arc.start_point_id&&
                        value.second_point_id=="sketch_origin";
                })&&
                std::ranges::any_of(candidate.constraints,[&](const auto& value) {
                    return !value.suppressed&&value.kind==
                            sketcher::ConstraintKind::PointOnLine&&
                        value.first_point_id==first_arc.center_point_id&&
                        value.geometry_id=="sketch_axis:y";
                }),
            "Profile seed does not constrain its start to Origin and its arc centre to the axis");
        const auto seeded=document::sheet_profile_route(candidate,
            profile.sheet_profile.start_point_id);
        require(seeded.spans.size()==2&&
                seeded.spans.front().kind==
                    document::SheetProfileSpanKind::Bend&&
                seeded.spans.back().kind==
                    document::SheetProfileSpanKind::Flat&&
                std::abs(seeded.spans.front().radius-radius_value)<1e-8&&
                std::abs(std::abs(seeded.spans.front().turn_degrees)-
                    angle_value)<1e-8&&
                std::abs(seeded.spans.back().length-length_value)<1e-8,
            "Attached Profile dimensions do not drive its Bend/Flat route");
    };
    verify_seed_dimensions(route,2*thickness,40);
    const auto dimension_mesh=route.viewer_mesh();
    require(dimension_mesh.dimensions.size()==3&&
            std::ranges::any_of(dimension_mesh.dimensions,
                [](const auto& dimension) {
                    return dimension.kind==kernel::ViewerDimensionKind::Radius&&
                        dimension.label_prefix=="R";
                })&&
            std::ranges::any_of(dimension_mesh.dimensions,
                [](const auto& dimension) {
                    return dimension.kind==kernel::ViewerDimensionKind::Linear;
                })&&
            std::ranges::any_of(dimension_mesh.dimensions,
                [](const auto& dimension) {
                    return dimension.kind==kernel::ViewerDimensionKind::Angular&&
                        std::abs(dimension.value-90)<1e-8;
                }),
        "Profile Sketch does not display R, angle and line-length dimensions");
    auto tangent_extension=route;
    const auto seeded_line=tangent_extension.segments.front();
    const auto* seeded_end=tangent_extension.find_point(
        seeded_line.second_point_id);
    require(seeded_end,"Profile seed line has no final point");
    const auto extension=tangent_extension.add_tangent_arc(
        seeded_line.second_point_id,seeded_end->x+10,seeded_end->y+10,
        seeded_line.id);
    require(!extension.empty()&&document::sheet_profile_route(
                tangent_extension,profile.sheet_profile.start_point_id).spans.size()==3,
        "Profile final endpoint cannot continue as a tangent arc");
    auto dimension_probe=route;
    for(const double value:{3*thickness,2.5*thickness,2*thickness}) {
        require(dimension_probe.set_dimension_value(radius_dimension_id,value),
            "Attached Profile radius rejected a successive edit");
        verify_seed_dimensions(dimension_probe,value,40);
    }
    for(const double value:{55.,25.,40.}) {
        require(dimension_probe.set_dimension_value(length_dimension_id,value),
            "Attached Profile line length rejected a successive edit");
        verify_seed_dimensions(dimension_probe,2*thickness,value);
    }
    for(const double value:{75.,120.,90.}) {
        require(dimension_probe.set_dimension_value(angle_dimension_id,value),
            "Attached Profile angle rejected a successive edit");
        verify_seed_dimensions(dimension_probe,2*thickness,40,false,value);
    }
    auto dragged=dimension_probe;
    const auto line=dragged.segments.front();
    const auto old_end=*dragged.find_point(line.second_point_id);
    require(dragged.move_point(line.second_point_id,
                old_end.x+4,old_end.y+3),
        "Underconstrained attached Profile cannot rotate by dragging");
    const auto* moved_end=dragged.find_point(line.second_point_id);
    require(std::hypot(moved_end->x-old_end.x,moved_end->y-old_end.y)>1e-4,
        "Accepted attached Profile drag did not move its endpoint");
    const auto dragged_length=std::ranges::find(dragged.dimensions,
        length_dimension_id,&sketcher::SketchDimension::id);
    const auto dragged_angle=std::ranges::find(dragged.dimensions,
        angle_dimension_id,&sketcher::SketchDimension::id);
    require(dragged_length!=dragged.dimensions.end()&&
            dragged_angle!=dragged.dimensions.end()&&
            std::abs(dragged_length->value-40)>1e-4,
        "Unlocked attached Profile length did not follow its endpoint drag");
    verify_seed_dimensions(dragged,2*thickness,dragged_length->value,false,
        dragged_angle->value);
    require(dragged.set_dimension_value(length_dimension_id,40),
        "Attached Profile line length could not be restored after dragging");
    const auto restored_angle=std::ranges::find(dragged.dimensions,
        angle_dimension_id,&sketcher::SketchDimension::id);
    require(restored_angle!=dragged.dimensions.end(),
        "Attached Profile drag lost its angle dimension");
    verify_seed_dimensions(dragged,2*thickness,40,false,
        restored_angle->value);
    auto locked=dimension_probe;
    for(const auto& id:{radius_dimension_id,length_dimension_id,
            angle_dimension_id}) {
        auto dimension=*std::ranges::find(locked.dimensions,id,
            &sketcher::SketchDimension::id);
        dimension.locked=true;locked.apply_dimension(std::move(dimension));
    }
    const auto locked_line=locked.segments.front();
    const auto locked_old=*locked.find_point(locked_line.second_point_id);
    const auto locked_definition=locked.serialized();
    require(!locked.move_point(locked_line.second_point_id,
                locked_old.x+3,locked_old.y+2)&&
            locked.serialized()==locked_definition,
        "Locked attached Profile escaped its dimensions while dragging");
    verify_seed_dimensions(locked,2*thickness,40,true);
    auto frozen=dimension_probe;
    for(auto& point:frozen.points)point.fixed=true;
    const auto frozen_definition=frozen.serialized();
    require(!frozen.move_point(frozen.segments.front().second_point_id,80,80)&&
            frozen.serialized()==frozen_definition&&
            !frozen.set_dimension_value(radius_dimension_id,4*thickness)&&
            frozen.serialized()==frozen_definition,
        "Fully constrained attached Profile changed after an impossible edit");
    verify_seed_dimensions(sketcher::Sketch::from_serialized(
        dimension_probe.serialized()),2*thickness,40);
    profile.sheet_profile.width.symmetric=false;
    profile.sheet_profile.width.sides[0].length=30;
    profile.sheet_profile.thickness_override=false;
    profile.sheet_profile.sheet_attachment=true;
    profile.sheet_profile.material_side=-1; // Attachment derives this value.
    profile.placement.references=references;

    const auto first_preview=workspace::prepare_sheet_profile(
        part,profile,route,geometry);
    require(first_preview.logical_children.size()==2,
        "Attached Profile first preview lost its Bend/Flat route");
    const auto* preview_master=first_preview.document.find_container(
        first_preview.logical_children.front());
    require(preview_master&&preview_master->feature_kind==
            document::FeatureKind::Bend&&preview_master->bend.sheet_attachment,
        "Attached Profile first preview has no attached Bend");
    const auto preview_master_sketch=std::ranges::find(
        first_preview.document.sketches,preview_master->bend.sketch_id,
        &sketcher::Sketch::id);
    require(preview_master_sketch!=first_preview.document.sketches.end(),
        "Attached Profile first preview lost its Bend Sketch");
    const auto preview_line=std::ranges::find_if(
        preview_master_sketch->segments,
        [](const auto& value){return !value.construction;});
    require(preview_line!=preview_master_sketch->segments.end(),
        "Attached Profile first preview lost its edge line");
    const auto* preview_first=preview_master_sketch->find_point(
        preview_line->first_point_id);
    const auto* preview_last=preview_master_sketch->find_point(
        preview_line->second_point_id);
    require(preview_first&&preview_last,
        "Attached Profile first preview lost its edge endpoints");
    const auto preview_first_world=preview_master_sketch->world_point(
        preview_first->x,preview_first->y);
    const auto preview_last_world=preview_master_sketch->world_point(
        preview_last->x,preview_last->y);
    require((close(preview_first_world,boundary->points.front())&&
                close(preview_last_world,boundary->points.back()))||
            (close(preview_first_world,boundary->points.back())&&
                close(preview_last_world,boundary->points.front())),
        "Attached Profile first cyan preview does not span the complete selected edge");
    const auto preview_driver=std::ranges::find(first_preview.document.sketches,
        route.id,&sketcher::Sketch::id);
    require(preview_driver!=first_preview.document.sketches.end(),
        "Attached Profile first preview lost its route Sketch");
    verify_seed_dimensions(*preview_driver,2*thickness,40);

    workspace::Workspace live;
    const auto document_id=part.document_id;
    live.add_part(std::move(part),std::move(baseline));live.activate(document_id);
    require(workspace::commit_sheet_profile(live,kernel,document_id,
            profile,route),
        "Attached Sheet Profile did not commit");
    auto* state=live.open_part(document_id);
    require(state,"Attached Sheet Profile lost its Part");
    const auto complete=state->session.document().serialized();
    const auto* stored=state->session.document().find_container(profile.id);
    require(stored&&stored->sheet_profile.sheet_attachment&&
            stored->sheet_profile.material_side==1&&
            !stored->sheet_profile.thickness_override&&
            std::abs(stored->sheet_profile.thickness-thickness)<1e-9,
        "Attached Sheet Profile did not derive material side and thickness");
    const auto stored_route=std::ranges::find(
        state->session.document().sketches,route.id,&sketcher::Sketch::id);
    require(stored_route!=state->session.document().sketches.end(),
        "Attached Sheet Profile lost its route Sketch");
    const auto spans=document::sheet_profile_route(*stored_route,
        stored->sheet_profile.start_point_id);
    require(spans.spans.size()==2&&
            spans.spans[0].kind==document::SheetProfileSpanKind::Bend&&
            spans.spans[1].kind==document::SheetProfileSpanKind::Flat&&
            std::ranges::all_of(stored_route->points,[](const auto& point) {
                return point.x>=-1e-9&&point.y>=-1e-9;
            }),
        "Attached Sheet Profile is not a positive Bend/Flat route");
    const auto* master=state->session.document().find_container(
        stored->sheet_profile.master_feature_id);
    require(master&&master->feature_kind==document::FeatureKind::Bend&&
            master->bend.sheet_attachment&&
            !master->bend.thickness_override&&
            master->placement.reference_valid&&
            master->placement.references==stored->placement.references&&
            std::abs(master->bend.thickness-thickness)<1e-8&&
            std::abs(master->bend.radius-thickness)<1e-8,
        "Attached Sheet Profile first Bend is not attached to the source edge");
    const auto master_sketch=std::ranges::find(
        state->session.document().sketches,master->bend.sketch_id,
        &sketcher::Sketch::id);
    require(master_sketch!=state->session.document().sketches.end(),
        "Attached Sheet Profile first Bend lost its owned Sketch");
    const auto master_line=std::ranges::find_if(master_sketch->segments,
        [](const auto& value){return !value.construction;});
    require(master_line!=master_sketch->segments.end(),
        "Attached Sheet Profile first Bend lost its start segment");
    const auto* master_first=master_sketch->find_point(
        master_line->first_point_id);
    const auto* master_last=master_sketch->find_point(
        master_line->second_point_id);
    require(master_first&&master_last,
        "Attached Sheet Profile first Bend lost its endpoints");
    const auto master_first_world=master_sketch->world_point(
        master_first->x,master_first->y);
    const auto master_last_world=master_sketch->world_point(
        master_last->x,master_last->y);
    require((close(master_first_world,boundary->points.front())&&
                close(master_last_world,boundary->points.back()))||
            (close(master_first_world,boundary->points.back())&&
                close(master_last_world,boundary->points.front())),
        "Attached Sheet Profile Bend does not span the selected edge endpoints");
    require(std::ranges::all_of(
            boundary->edge_treatment_endpoint_references,[&](const auto& endpoint) {
                return std::ranges::any_of(master_sketch->external_references,
                    [&](const auto& external) {
                        return external.source_owner_id==endpoint.owner_id&&
                            external.source_semantic_key==endpoint.semantic_key&&
                            external.source_instance_path==endpoint.instance_path;
                    });
            }),"Attached Sheet Profile Bend did not persist both endpoint links");
    const kernel::Vec3 rotation{stored->placement.rotation_x,
        stored->placement.rotation_y,stored->placement.rotation_z};
    const auto base_x=document::construction_direction_from_local_axis(
        "x",rotation);
    const auto base_y=document::construction_direction_from_local_axis(
        "y",rotation);
    const auto base_z=document::construction_direction_from_local_axis(
        "z",rotation);
    const auto dot=[](kernel::Vec3 a,kernel::Vec3 b) {
        return a.x*b.x+a.y*b.y+a.z*b.z;
    };
    require(dot(stored_route->resolved_x_axis,base_z)>1-1e-8&&
            dot(stored_route->resolved_y_axis,base_x)*-frame_side>1-1e-8&&
            dot(stored_route->resolved_normal,base_y)*-frame_side>1-1e-8,
        "Attached Sheet Profile does not reuse the Bend trajectory frame");
    const auto* start=stored_route->find_point(
        stored->sheet_profile.start_point_id);
    require(start,"Attached Sheet Profile start point is missing");
    const auto world_start=stored_route->world_point(start->x,start->y);
    require(std::hypot(std::hypot(world_start.x-stored->placement.x,
                world_start.y-stored->placement.y),
                world_start.z-stored->placement.z)<1e-8,
        "Attached Sheet Profile does not start at the selected Bend endpoint");

    auto stable_references=workspace::construction_reference_source_geometry(
        state->session.calculated_boundaries());
    const auto stable_profile=workspace::prepare_sheet_profile(
        state->session.document(),*stored,*stored_route,
        std::move(stable_references));
    require(stable_profile.document.serialized()==
            state->session.document().serialized(),
        "Unchanged attached Profile preparation is not idempotent");

    // The next Flat is bounded by the Bend's actual end edge, not by the
    // original attachment edge. Both a start-edge offset and an end extension
    // therefore have to change its generated width in the same transaction.
    const auto span_interval=[](const sketcher::Sketch& sketch) {
        const auto segment=std::ranges::find_if(sketch.segments,
            [](const auto& value){return !value.construction;});
        require(segment!=sketch.segments.end(),
            "Profile span has no material edge");
        const auto* first=sketch.find_point(segment->first_point_id);
        const auto* last=sketch.find_point(segment->second_point_id);
        require(first&&last,"Profile span edge has no endpoints");
        std::array result{first->x,last->x};
        std::ranges::sort(result);return result;
    };
    const auto following_flat=std::ranges::find_if(
        state->session.document().history,[&](const auto& feature) {
            return feature.sheet_profile_binding.parent_id==profile.id&&
                feature.sheet_profile_binding.curve_id==
                    spans.spans.back().curve_id;
        });
    require(following_flat!=state->session.document().history.end(),
        "Attached Profile following Flat is missing before width edits");
    const auto master_id=master->id;
    const auto following_flat_id=following_flat->id;
    const auto initial_end=sketcher::Sketch::from_serialized(
        master->bend.auxiliary_sketches[1]);
    const auto initial_end_interval=span_interval(initial_end);
    workspace::Workspace width_live;
    width_live.add_part(state->session.document(),
        state->session.calculated_boundaries());
    width_live.activate(document_id);
    auto* width_state=width_live.open_part(document_id);
    auto offset_master=*width_state->session.document().find_container(
        master_id);
    auto offset_start=*std::ranges::find(width_state->session.document().sketches,
        offset_master.bend.sketch_id,&sketcher::Sketch::id);
    require(offset_start.set_dimension_value(
                offset_start.id+":position:first",6),
        "Attached Profile first Bend offset rejected an edit");
    require(workspace::commit_bend(width_live,kernel,document_id,
            offset_master,offset_start),
        "Attached Profile first Bend offset did not commit");
    width_state=width_live.open_part(document_id);
    const auto* offset_result=width_state->session.document().find_container(
        master_id);
    const auto* offset_flat=width_state->session.document().find_container(
        following_flat_id);
    const auto offset_result_start=std::ranges::find(
        width_state->session.document().sketches,
        offset_result->bend.sketch_id,&sketcher::Sketch::id);
    const auto offset_result_flat=std::ranges::find(
        width_state->session.document().sketches,
        offset_flat->flat.sketch_id,&sketcher::Sketch::id);
    const auto offset_end=sketcher::Sketch::from_serialized(
        offset_result->bend.auxiliary_sketches[1]);
    const auto offset_end_interval=span_interval(offset_end);
    const auto offset_flat_interval=span_interval(*offset_result_flat);
    const auto offset_dimension=std::ranges::find(
        offset_result_start->dimensions,
        offset_result_start->id+":position:first",
        &sketcher::SketchDimension::id);
    require(offset_result&&offset_flat&&
            offset_result_start!=width_state->session.document().sketches.end()&&
            offset_result_flat!=width_state->session.document().sketches.end()&&
            offset_dimension!=offset_result_start->dimensions.end()&&
            std::abs(offset_dimension->value-6)<1e-8&&
            std::abs(offset_end_interval[0]-offset_flat_interval[0])<1e-8&&
            std::abs(offset_end_interval[1]-offset_flat_interval[1])<1e-8&&
            std::abs((offset_end_interval[1]-offset_end_interval[0])-
                (initial_end_interval[1]-initial_end_interval[0]))>1,
        "Following Profile Flat retained the Bend's initial width after an endpoint offset");
    const auto offset_complete=width_state->session.document().serialized();

    auto extended_master=*offset_result;
    auto extended_start=*offset_result_start;
    auto extended_end=sketcher::Sketch::from_serialized(
        extended_master.bend.auxiliary_sketches[1]);
    require(extended_end.set_dimension_value(
                extended_end.id+":difference:first",4),
        "Attached Profile Bend end extension rejected an edit");
    document::accept_bend_sketch(extended_master,extended_start,1,
        std::move(extended_end),document::sheet_metal_defaults(
            width_state->session.document()));
    require(workspace::commit_bend(width_live,kernel,document_id,
            extended_master,extended_start),
        "Attached Profile Bend end extension did not commit");
    width_state=width_live.open_part(document_id);
    const auto* extended_result=width_state->session.document().find_container(
        master_id);
    const auto* extended_flat=width_state->session.document().find_container(
        following_flat_id);
    const auto extended_result_flat=std::ranges::find(
        width_state->session.document().sketches,
        extended_flat->flat.sketch_id,&sketcher::Sketch::id);
    const auto extended_result_end=sketcher::Sketch::from_serialized(
        extended_result->bend.auxiliary_sketches[1]);
    const auto extended_end_interval=span_interval(extended_result_end);
    const auto extended_flat_interval=span_interval(*extended_result_flat);
    require(extended_result&&extended_flat&&
            extended_result_flat!=width_state->session.document().sketches.end()&&
            std::abs(extended_end_interval[0]-extended_flat_interval[0])<1e-8&&
            std::abs(extended_end_interval[1]-extended_flat_interval[1])<1e-8&&
            std::abs((extended_end_interval[1]-extended_end_interval[0])-
                (offset_end_interval[1]-offset_end_interval[0]))>1,
        "Following Profile Flat retained the Bend's initial width after an end extension");
    const auto extended_complete=width_state->session.document().serialized();
    require(workspace::step_part_document_history(width_live,document_id,false)&&
            width_state->session.document().serialized()==offset_complete,
        "Profile Bend end-width Undo did not restore the endpoint offset");
    require(workspace::step_part_document_history(width_live,document_id,true)&&
            width_state->session.document().serialized()==extended_complete,
        "Profile Bend end-width Redo changed the regenerated following Flat");
    const auto width_file=std::filesystem::path("build")/
        ("zima-profile-end-width-"+profile.id+".prtz");
    width_state->session.document().save(width_file,
        width_state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> width_cache;
    const auto width_reopened=document::PartDocument::load(width_file,
        &width_cache);
    std::filesystem::remove(width_file);
    const auto* width_parent=width_reopened.find_container(profile.id);
    const auto width_driver=std::ranges::find(width_reopened.sketches,
        width_parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
    auto width_references=workspace::construction_reference_source_geometry(
        width_cache);
    require(width_driver!=width_reopened.sketches.end()&&
            workspace::prepare_sheet_profile(width_reopened,*width_parent,
                *width_driver,std::move(width_references)).document.serialized()==
                width_reopened.serialized(),
        "Profile Bend end width changed after save/reopen");

    // A Profile attached to a Flat remains one dependent feature. Resizing
    // the source must update both its real attached Bend and every generated
    // downstream span before Properties is reopened.
    auto resize_document=state->session.document();
    auto resize_boundaries=state->session.calculated_boundaries();
    workspace::Workspace resize_live;
    resize_live.add_part(std::move(resize_document),
        std::move(resize_boundaries));
    resize_live.activate(document_id);
    auto* resize_state=resize_live.open_part(document_id);
    auto resized_stock=*resize_state->session.document().find_container(
        stock.id);
    auto resized_outline=*std::ranges::find(
        resize_state->session.document().sketches,outline.id,
        &sketcher::Sketch::id);
    for(auto& point:resized_outline.points) {
        if(std::abs(point.x-60)<1e-9)point.x=90;
        if(std::abs(point.y-40)<1e-9)point.y=70;
    }
    require(workspace::commit_flat(resize_live,kernel,document_id,
            resized_stock,resized_outline),
        "Resizing the source Flat did not commit");
    resize_state=resize_live.open_part(document_id);
    const auto& resized_document=resize_state->session.document();
    const auto* resized_parent=resized_document.find_container(profile.id);
    const auto resized_driver=std::ranges::find(resized_document.sketches,
        route.id,&sketcher::Sketch::id);
    require(resized_parent&&resized_driver!=resized_document.sketches.end(),
        "Source Flat resize lost the attached Profile");
    const auto resized_route=document::sheet_profile_route(*resized_driver,
        resized_parent->sheet_profile.start_point_id);
    const auto* resized_master=resized_document.find_container(
        resized_parent->sheet_profile.master_feature_id);
    const auto resized_flat=std::ranges::find_if(resized_document.history,
        [&](const auto& feature) {
            return feature.sheet_profile_binding.parent_id==profile.id&&
                feature.sheet_profile_binding.curve_id==
                    resized_route.spans.back().curve_id;
        });
    require(resized_master&&resized_flat!=resized_document.history.end()&&
            resized_master->feature_kind==document::FeatureKind::Bend&&
            resized_flat->feature_kind==document::FeatureKind::Flat,
        "Source Flat resize lost the Profile Bend/Flat children");
    const auto resized_master_sketch=std::ranges::find(
        resized_document.sketches,resized_master->bend.sketch_id,
        &sketcher::Sketch::id);
    const auto resized_flat_sketch=std::ranges::find(
        resized_document.sketches,resized_flat->flat.sketch_id,
        &sketcher::Sketch::id);
    require(resized_master_sketch!=resized_document.sketches.end()&&
            resized_flat_sketch!=resized_document.sketches.end(),
        "Source Flat resize lost a generated Profile Sketch");
    const auto resized_master_line=std::ranges::find_if(
        resized_master_sketch->segments,
        [](const auto& value){return !value.construction;});
    const auto* resized_master_first=resized_master_sketch->find_point(
        resized_master_line->first_point_id);
    const auto* resized_master_last=resized_master_sketch->find_point(
        resized_master_line->second_point_id);
    const double master_width=std::abs(
        resized_master_last->x-resized_master_first->x);
    const auto [flat_min,flat_max]=std::ranges::minmax_element(
        resized_flat_sketch->points,{},&sketcher::SketchPoint::x);
    const double flat_width=flat_max->x-flat_min->x;
    require(master_width>60&&std::abs(flat_width-master_width)<1e-8,
        "Source Flat resize updated the Profile Bend but not its following Flat");
    const auto resized_bend_end=sketcher::Sketch::from_serialized(
        resized_master->bend.auxiliary_sketches[1]);
    require(close(resized_flat_sketch->resolved_origin,
                resized_bend_end.resolved_origin)&&
            close(resized_flat_sketch->resolved_x_axis,
                resized_bend_end.resolved_x_axis)&&
            close(resized_flat_sketch->resolved_y_axis,
                resized_bend_end.resolved_normal)&&
            close(resized_flat_sketch->resolved_normal,
                kernel::Vec3{-resized_bend_end.resolved_y_axis.x,
                    -resized_bend_end.resolved_y_axis.y,
                    -resized_bend_end.resolved_y_axis.z}),
        "Source Flat resize left the following Profile Flat on the old Bend path");
    auto resized_references=workspace::construction_reference_source_geometry(
        resize_state->session.calculated_boundaries());
    const auto resized_prepared=workspace::prepare_sheet_profile(
        resized_document,*resized_parent,*resized_driver,
        std::move(resized_references));
    require(resized_prepared.document.serialized()==
            resized_document.serialized(),
        "Source Flat resize left stale attached Profile children");
    const auto resized_complete=resized_document.serialized();

    // Moving the selected source edge changes the complete Profile path, not
    // merely its width. The downstream Flat must move with the attached Bend.
    auto moved_stock=*resize_state->session.document().find_container(stock.id);
    auto moved_outline=*std::ranges::find(
        resize_state->session.document().sketches,outline.id,
        &sketcher::Sketch::id);
    for(auto& point:moved_outline.points) {
        point.x+=12;point.y+=8;
    }
    require(workspace::commit_flat(resize_live,kernel,document_id,
            moved_stock,moved_outline),
        "Moving the source Flat path did not commit");
    resize_state=resize_live.open_part(document_id);
    const auto& moved_document=resize_state->session.document();
    const auto* moved_parent=moved_document.find_container(profile.id);
    const auto moved_driver=std::ranges::find(moved_document.sketches,route.id,
        &sketcher::Sketch::id);
    require(moved_parent&&moved_driver!=moved_document.sketches.end(),
        "Moving the source Flat path lost the attached Profile");
    const auto moved_route=document::sheet_profile_route(*moved_driver,
        moved_parent->sheet_profile.start_point_id);
    const auto* moved_master=moved_document.find_container(
        moved_parent->sheet_profile.master_feature_id);
    const auto moved_flat=std::ranges::find_if(moved_document.history,
        [&](const auto& feature) {
            return feature.sheet_profile_binding.parent_id==profile.id&&
                feature.sheet_profile_binding.curve_id==
                    moved_route.spans.back().curve_id;
        });
    require(moved_master&&moved_flat!=moved_document.history.end(),
        "Moving the source Flat path lost the Profile Bend/Flat children");
    const auto moved_flat_sketch=std::ranges::find(moved_document.sketches,
        moved_flat->flat.sketch_id,&sketcher::Sketch::id);
    const auto moved_bend_end=sketcher::Sketch::from_serialized(
        moved_master->bend.auxiliary_sketches[1]);
    const double moved_path_distance=std::hypot(std::hypot(
        moved_bend_end.resolved_origin.x-resized_bend_end.resolved_origin.x,
        moved_bend_end.resolved_origin.y-resized_bend_end.resolved_origin.y),
        moved_bend_end.resolved_origin.z-resized_bend_end.resolved_origin.z);
    require(moved_path_distance>1&&
            moved_flat_sketch!=moved_document.sketches.end()&&
            close(moved_flat_sketch->resolved_origin,
                moved_bend_end.resolved_origin)&&
            close(moved_flat_sketch->resolved_x_axis,
                moved_bend_end.resolved_x_axis)&&
            close(moved_flat_sketch->resolved_y_axis,
                moved_bend_end.resolved_normal)&&
            close(moved_flat_sketch->resolved_normal,
                kernel::Vec3{-moved_bend_end.resolved_y_axis.x,
                    -moved_bend_end.resolved_y_axis.y,
                    -moved_bend_end.resolved_y_axis.z}),
        "Moving the source Flat updated the Profile Bend but not its following Flat path");
    auto moved_references=workspace::construction_reference_source_geometry(
        resize_state->session.calculated_boundaries());
    const auto moved_prepared=workspace::prepare_sheet_profile(moved_document,
        *moved_parent,*moved_driver,std::move(moved_references));
    require(moved_prepared.document.serialized()==moved_document.serialized(),
        "Moving the source Flat left stale attached Profile children");
    const auto moved_complete=moved_document.serialized();
    require(workspace::step_part_document_history(resize_live,document_id,
                false)&&
            resize_live.open_part(document_id)->session.document().serialized()==
                resized_complete,
        "Source Flat path move Undo did not restore the resized Profile");
    require(workspace::step_part_document_history(resize_live,document_id,
                true)&&
            resize_live.open_part(document_id)->session.document().serialized()==
                moved_complete,
        "Source Flat path move Redo changed the regenerated Profile");
    resize_state=resize_live.open_part(document_id);

    // Changing the source sheet thickness moves the attached Bend end even
    // when the authored outer route remains unchanged. The following Flat
    // must inherit both the new thickness and that new end frame.
    auto thicker_stock=*resize_state->session.document().find_container(
        stock.id);
    auto thicker_outline=*std::ranges::find(
        resize_state->session.document().sketches,outline.id,
        &sketcher::Sketch::id);
    thicker_stock.flat.thickness=4;
    require(workspace::commit_flat(resize_live,kernel,document_id,
            thicker_stock,thicker_outline),
        "Changing the source Flat thickness did not commit");
    resize_state=resize_live.open_part(document_id);
    const auto& thicker_document=resize_state->session.document();
    const auto* thicker_parent=thicker_document.find_container(profile.id);
    const auto thicker_driver=std::ranges::find(thicker_document.sketches,
        route.id,&sketcher::Sketch::id);
    require(thicker_parent&&thicker_driver!=thicker_document.sketches.end()&&
            std::abs(thicker_parent->sheet_profile.thickness-4)<1e-8,
        "Source thickness change did not reach the attached Profile");
    const auto thicker_route=document::sheet_profile_route(*thicker_driver,
        thicker_parent->sheet_profile.start_point_id);
    const auto* thicker_master=thicker_document.find_container(
        thicker_parent->sheet_profile.master_feature_id);
    const auto thicker_flat=std::ranges::find_if(thicker_document.history,
        [&](const auto& feature) {
            return feature.sheet_profile_binding.parent_id==profile.id&&
                feature.sheet_profile_binding.curve_id==
                    thicker_route.spans.back().curve_id;
        });
    require(thicker_master&&thicker_flat!=thicker_document.history.end()&&
            std::abs(thicker_master->bend.thickness-4)<1e-8&&
            std::abs(thicker_flat->flat.thickness-4)<1e-8,
        "Source thickness updated the Profile Bend but not its following Flat");
    const auto thicker_flat_sketch=std::ranges::find(
        thicker_document.sketches,thicker_flat->flat.sketch_id,
        &sketcher::Sketch::id);
    const auto thicker_bend_end=sketcher::Sketch::from_serialized(
        thicker_master->bend.auxiliary_sketches[1]);
    require(thicker_flat_sketch!=thicker_document.sketches.end()&&
            close(thicker_flat_sketch->resolved_origin,
                thicker_bend_end.resolved_origin)&&
            close(thicker_flat_sketch->resolved_x_axis,
                thicker_bend_end.resolved_x_axis)&&
            close(thicker_flat_sketch->resolved_y_axis,
                thicker_bend_end.resolved_normal)&&
            close(thicker_flat_sketch->resolved_normal,
                kernel::Vec3{-thicker_bend_end.resolved_y_axis.x,
                    -thicker_bend_end.resolved_y_axis.y,
                    -thicker_bend_end.resolved_y_axis.z}),
        "Source thickness left the following Profile Flat on the old Bend path");
    auto thicker_references=workspace::construction_reference_source_geometry(
        resize_state->session.calculated_boundaries());
    const auto thicker_prepared=workspace::prepare_sheet_profile(
        thicker_document,*thicker_parent,*thicker_driver,
        std::move(thicker_references));
    require(thicker_prepared.document.serialized()==
            thicker_document.serialized(),
        "Source thickness change left stale attached Profile children");
    require(!resize_state->session.calculated_boundaries().empty()&&
            resize_state->session.calculated_boundaries().back().
                calculation_errors.empty(),
        "Source thickness change broke the calculated attached Profile");
    const auto thicker_complete=thicker_document.serialized();
    require(workspace::step_part_document_history(resize_live,document_id,
                false)&&
            resize_live.open_part(document_id)->session.document().serialized()==
                moved_complete,
        "Source thickness Undo did not restore the moved attached Profile");
    require(workspace::step_part_document_history(resize_live,document_id,
                true)&&
            resize_live.open_part(document_id)->session.document().serialized()==
                thicker_complete,
        "Source thickness Redo changed the regenerated attached Profile");
    resize_state=resize_live.open_part(document_id);
    const auto resized_file=std::filesystem::path("build")/
        ("zima-attached-profile-resize-"+profile.id+".prtz");
    resize_state->session.document().save(resized_file,
        resize_state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> resized_reopened_cache;
    const auto resized_reopened=document::PartDocument::load(resized_file,
        &resized_reopened_cache);
    std::filesystem::remove(resized_file);
    const auto* reopened_resized_parent=
        resized_reopened.find_container(profile.id);
    const auto reopened_resized_driver=std::ranges::find(
        resized_reopened.sketches,route.id,&sketcher::Sketch::id);
    require(reopened_resized_parent&&
            reopened_resized_driver!=resized_reopened.sketches.end()&&
            !resized_reopened_cache.empty()&&
            resized_reopened_cache.back().calculation_errors.empty(),
        "Reopened source-resized attached Profile lost its definition or Body");
    auto reopened_resized_references=
        workspace::construction_reference_source_geometry(
            resized_reopened_cache);
    const auto reopened_resized_prepared=workspace::prepare_sheet_profile(
        resized_reopened,*reopened_resized_parent,*reopened_resized_driver,
        std::move(reopened_resized_references));
    require(reopened_resized_prepared.document.serialized()==
            resized_reopened.serialized(),
        "Save/reopen made the source-resized attached Profile stale");

    auto edited_profile=*stored;
    auto edited_route=*stored_route;
    require(edited_route.set_dimension_value(radius_dimension_id,3*thickness)&&
            edited_route.set_dimension_value(length_dimension_id,55),
        "Attached Profile dimensions rejected a downstream edit");
    require(workspace::commit_sheet_profile(live,kernel,document_id,
            edited_profile,edited_route),
        "Attached Profile dimension edit did not commit");
    state=live.open_part(document_id);
    const auto edited_complete=state->session.document().serialized();
    const auto* edited_parent=state->session.document().find_container(profile.id);
    const auto edited_driver=std::ranges::find(state->session.document().sketches,
        route.id,&sketcher::Sketch::id);
    require(edited_parent&&
            edited_driver!=state->session.document().sketches.end(),
        "Edited attached Profile lost its parent or route Sketch");
    const auto edited_spans=document::sheet_profile_route(*edited_driver,
        edited_parent->sheet_profile.start_point_id);
    const auto* edited_master=state->session.document().find_container(
        edited_parent->sheet_profile.master_feature_id);
    const auto edited_flat=std::ranges::find_if(state->session.document().history,
        [&](const auto& feature) {
            return feature.sheet_profile_binding.parent_id==profile.id&&
                feature.sheet_profile_binding.curve_id==
                    edited_spans.spans.back().curve_id;
        });
    require(edited_master&&edited_master->bend.sheet_attachment&&
            std::abs(edited_master->bend.radius-2*thickness)<1e-8&&
            edited_flat!=state->session.document().history.end()&&
            edited_flat->feature_kind==document::FeatureKind::Flat&&
            std::abs(edited_spans.spans.back().length-55)<1e-8,
        "Profile R/line dimensions did not regenerate its attached Bend/Flat");
    const auto edited_master_id=edited_master->id;
    const auto edited_flat_id=edited_flat->id;
    require(workspace::step_part_document_history(live,document_id,false)&&
            state->session.document().serialized()==complete,
        "Attached Profile dimension edit Undo failed");
    require(workspace::step_part_document_history(live,document_id,true)&&
            state->session.document().serialized()==edited_complete,
        "Attached Profile dimension edit Redo changed its definition");

    const auto edit_master_angle=[&](double angle) {
        state=live.open_part(document_id);
        const auto* current_parent=
            state->session.document().find_container(profile.id);
        require(current_parent,
            "Attached Profile parent disappeared before master edit");
        auto current_master=*state->session.document().find_container(
            current_parent->sheet_profile.master_feature_id);
        auto current_sketch=*std::ranges::find(
            state->session.document().sketches,current_master.bend.sketch_id,
            &sketcher::Sketch::id);
        current_master.bend.angle_degrees=angle;
        require(workspace::commit_bend(live,kernel,document_id,
                current_master,current_sketch),
            "Attached Profile master Bend angle edit did not commit");
        state=live.open_part(document_id);
        current_parent=state->session.document().find_container(profile.id);
        const auto current_driver=std::ranges::find(
            state->session.document().sketches,
            current_parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
        require(current_driver!=state->session.document().sketches.end(),
            "Attached Profile master edit lost its route Sketch");
        const auto current_route=document::sheet_profile_route(*current_driver,
            current_parent->sheet_profile.start_point_id);
        const auto angle_dimension=std::ranges::find(
            current_driver->dimensions,angle_dimension_id,
            &sketcher::SketchDimension::id);
        const auto length_dimension=std::ranges::find(
            current_driver->dimensions,length_dimension_id,
            &sketcher::SketchDimension::id);
        require(angle_dimension!=current_driver->dimensions.end()&&
                length_dimension!=current_driver->dimensions.end()&&
                std::abs(angle_dimension->value-angle)<1e-8&&
                std::abs(length_dimension->value-55)<1e-8&&
                std::abs(std::abs(current_route.spans.front().turn_degrees)-
                    angle)<1e-8&&
                std::abs(current_route.spans.back().length-55)<1e-8,
            "Repeated master Bend edit accumulated or restored an old Profile value");
        const auto* regenerated_master=state->session.document().find_container(
            current_parent->sheet_profile.master_feature_id);
        const auto regenerated_flat=std::ranges::find_if(
            state->session.document().history,[&](const auto& feature) {
                return feature.sheet_profile_binding.parent_id==profile.id&&
                    feature.sheet_profile_binding.curve_id==
                        current_route.spans.back().curve_id;
            });
        require(regenerated_master&&
                regenerated_flat!=state->session.document().history.end()&&
                regenerated_flat->flat.direction==
                    document::ExtrusionDirection::Reverse,
            "Attached Profile master edit flipped the following Flat material side");
        const auto bend_end=sketcher::Sketch::from_serialized(
            regenerated_master->bend.auxiliary_sketches[1]);
        const auto flat_sketch=std::ranges::find(
            state->session.document().sketches,
            regenerated_flat->flat.sketch_id,&sketcher::Sketch::id);
        require(flat_sketch!=state->session.document().sketches.end()&&
                close(flat_sketch->resolved_origin,bend_end.resolved_origin)&&
                dot(flat_sketch->resolved_normal,
                    bend_end.resolved_y_axis)<-1+1e-8,
            "Attached Profile following Flat detached from the edited Bend end frame");
        const auto reopened=document::PartDocument::from_serialized(
            state->session.document().serialized());
        const auto* reopened_parent=reopened.find_container(profile.id);
        const auto reopened_driver=std::ranges::find(reopened.sketches,
            reopened_parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
        const auto reopened_route=document::sheet_profile_route(*reopened_driver,
            reopened_parent->sheet_profile.start_point_id);
        require(std::abs(std::abs(reopened_route.spans.front().turn_degrees)-
                    angle)<1e-8&&
                std::abs(reopened_route.spans.back().length-55)<1e-8,
            "Attached Profile repeated master edit changed after save/reopen");
        return state->session.document().serialized();
    };
    static_cast<void>(edit_master_angle(60));
    const auto before_last_master_edit=edit_master_angle(105);
    const auto after_last_master_edit=edit_master_angle(60);
    require(workspace::step_part_document_history(live,document_id,false)&&
            state->session.document().serialized()==before_last_master_edit,
        "Attached Profile repeated master edit Undo failed");
    require(workspace::step_part_document_history(live,document_id,true)&&
            state->session.document().serialized()==after_last_master_edit,
        "Attached Profile repeated master edit Redo failed");

    require(workspace::calculated_operation_input(state->session)&&
            workspace::calculated_operation_input(state->session,profile.id)&&
            !workspace::calculated_operation_output(state->session,profile.id),
        "Attached Profile parent incorrectly consumed a calculated Body row");
    const auto material_regions=workspace::sheet_state_regions(
        state->session.document(),{});
    const auto master_region=std::ranges::find(material_regions,
        edited_master_id,&kernel::SheetMaterialDefinition::owner_id);
    const auto flat_region=std::ranges::find(material_regions,
        edited_flat_id,&kernel::SheetMaterialDefinition::owner_id);
    require(master_region!=material_regions.end()&&
            flat_region!=material_regions.end()&&
            flat_region->parent_owner_id==master_region->owner_id,
        "Attached Profile material regions lost their Bend/Flat ancestry");

    auto folded=state->session.calculated_boundaries().back();
    kernel.ensure_body_properties(folded);
    auto unbend=document::PartDocument::create_sketch_container();
    unbend.feature_kind=document::FeatureKind::Unbend;
    unbend.name="Unbend Profile";unbend.sheet_state.all=true;
    require(workspace::commit_sheet_state(live,kernel,document_id,unbend),
        "Attached Profile could not be unfolded");
    state=live.open_part(document_id);
    require(!state->session.calculated_boundaries().empty()&&
            state->session.calculated_boundaries().back().calculation_errors.empty(),
        "Attached Profile unfold produced a calculation error");
    auto unfolded=state->session.calculated_boundaries().back();
    kernel.ensure_body_properties(unfolded);
    require(unfolded.volume>0&&
            std::abs(unfolded.volume-folded.volume)<
                std::max(1e-5,folded.volume*2e-4),
        "Attached Profile unfold changed its material volume");
    const auto unfolded_complete=state->session.document().serialized();
    const bool undo_unfold=workspace::step_part_document_history(
        live,document_id,false);
    const auto undo_document=state->session.document().serialized();
    const auto authored_definition=[](auto value) {
        value.erase("calculation_definition_fingerprint");
        value.erase("dimension_identifiers");
        return value;
    };
    require(undo_unfold&&
            authored_definition(undo_document)==
                authored_definition(after_last_master_edit)&&
            !state->session.document().find_container(unbend.id),
        "Attached Profile unfold Undo failed");
    require(workspace::step_part_document_history(live,document_id,true)&&
            state->session.document().serialized()==unfolded_complete,
        "Attached Profile unfold Redo changed its definition");

    const auto native_file=std::filesystem::path("build")/
        ("zima-attached-profile-"+profile.id+".prtz");
    state->session.document().save(native_file,
        state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> reopened_cache;
    auto reopened=document::PartDocument::load(native_file,&reopened_cache);
    std::filesystem::remove(native_file);
    const auto* reopened_parent=reopened.find_container(profile.id);
    const auto reopened_driver=std::ranges::find(reopened.sketches,route.id,
        &sketcher::Sketch::id);
    require(reopened_parent&&reopened_driver!=reopened.sketches.end(),
        "Reopened attached Profile lost its parent or route Sketch");
    verify_seed_dimensions(*reopened_driver,3*thickness,55,false,60);
    const auto* reopened_master=reopened.find_container(
        reopened_parent->sheet_profile.master_feature_id);
    require(reopened_master&&reopened_master->feature_kind==
                document::FeatureKind::Bend&&
            reopened_master->bend.sheet_attachment&&
            reopened.find_container(unbend.id),
        "Reopened attached Profile lost its edge attachment or Unbend");
    auto recalculated=workspace::calculate_part_with_resolved_references(
        kernel,reopened);
    require(!recalculated.empty()&&
            recalculated.back().calculation_errors.empty(),
        "Unfolded attached Profile failed cold save/reopen calculation");
    auto reopened_unfolded=recalculated.back();
    kernel.ensure_body_properties(reopened_unfolded);
    require(reopened_unfolded.volume>0&&
            std::abs(reopened_unfolded.volume-unfolded.volume)<
                std::max(1e-5,unfolded.volume*2e-4),
        "Reopened attached Profile unfold changed its material volume");
}

void rejected_routes() {
    {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_segment(0,0,20,0));
        static_cast<void>(fixture.driver.add_arc(20,5,20,0,15,5));
        bool rejected=false;
        try {static_cast<void>(document::sheet_profile_route(fixture.driver));}
        catch(const std::runtime_error&){rejected=true;}
        require(rejected,"Non-tangent Sheet Profile route was accepted");
    }
    {
        Fixture fixture;fixture.line_arc_line(.5);
        bool rejected=false;
        try {static_cast<void>(workspace::prepare_sheet_profile(fixture.part,
            fixture.profile,fixture.driver));}
        catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Outer Profile radius smaller than thickness was accepted");
    }
    {
        Fixture fixture;
        static_cast<void>(fixture.driver.add_arc(0,5,0,0,-5,5,
            false,1e-6,true));
        // Clockwise construction above normalizes to the short quarter arc;
        // explicitly author a long sweep to verify the Bend limit.
        fixture.driver.arcs.front().end_angle=
            fixture.driver.arcs.front().start_angle+1.5*std::numbers::pi;
        bool rejected=false;
        try {static_cast<void>(document::sheet_profile_route(fixture.driver));}
        catch(const std::runtime_error&){rejected=true;}
        require(rejected,"Sheet Profile Bend arc above 180 degrees was accepted");
    }
}
}

int main() {
    try {
        free_flat_bend_flat();
        arc_first_and_flat_only();
        free_width_directions();
        free_up_to_targets();
        master_edit_propagation();
        attached_profile_seed_and_history();
        group_history_contracts();
        rejected_routes();
        std::cout<<"Sheet Profile contracts passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
