#include <zima/workspace/sheet_profile_operations.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/part_transactions.hpp>

#include <zima/document/bend.hpp>
#include <zima/document/sheet_profile.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
        profile.sheet_profile.initial_width=20;
        profile.sheet_profile.width_direction=document::ExtrusionDirection::Symmetric;
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
    profile.sheet_profile.initial_width=30;
    profile.sheet_profile.thickness_override=false;
    profile.sheet_profile.sheet_attachment=true;
    profile.sheet_profile.material_side=-1; // Attachment derives this value.
    profile.placement.references=references;

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
            std::abs(master->bend.radius-thickness)<1e-8,
        "Attached Sheet Profile first Bend is not R=T");
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

    require(workspace::step_part_document_history(live,document_id,false)&&
            !state->session.document().find_container(profile.id),
        "Attached Sheet Profile Undo failed");
    require(workspace::step_part_document_history(live,document_id,true)&&
            state->session.document().serialized()==complete,
        "Attached Sheet Profile Redo changed its definition");
    auto reopened=document::PartDocument::from_serialized(complete);
    const auto recalculated=workspace::calculate_part_with_resolved_references(
        kernel,reopened);
    require(!recalculated.empty()&&
            recalculated.back().calculation_errors.empty(),
        "Attached Sheet Profile failed cold save/reopen calculation");
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
