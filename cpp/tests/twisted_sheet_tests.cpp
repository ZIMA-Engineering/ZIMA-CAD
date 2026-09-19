#include <zima/document/part_document.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/workspace.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace zima;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
double distance(kernel::Vec3 a,kernel::Vec3 b) {
    return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);
}
kernel::Vec3 unit(kernel::Vec3 value) {
    const double length=std::hypot(std::hypot(value.x,value.y),value.z);
    check(length>1e-12,"Cannot normalize a zero-length test vector");
    return {value.x/length,value.y/length,value.z/length};
}
double dot(kernel::Vec3 a,kernel::Vec3 b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
void verify_join(const document::PartDocument& part,const document::HistoryContainer& feature,
        const kernel::ViewerReferenceGeometry& geometry) {
    const auto preview=part.primitive_preview_edges(feature);
    const auto start=std::ranges::find_if(preview,[](const auto& edge){return edge.reference.semantic_key=="preview:twist:start";});
    check(start!=preview.end(),"Missing twist joining outline");
    const auto& face=feature.placement.references.at(1);
    using namespace kernel::sheet_material;
    for(const auto point:start->points) {
        bool inside=false;
        for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
            const auto& r=geometry.triangle_references[i];
            if(r.owner_id!=face.owner_id||r.semantic_key!=face.semantic_key||r.instance_path!=face.instance_path)continue;
            const auto a=geometry.vertices[geometry.triangles[3*i]],b=geometry.vertices[geometry.triangles[3*i+1]],c=geometry.vertices[geometry.triangles[3*i+2]];
            const auto v0=sub(b,a),v1=sub(c,a),v2=sub(point,a);
            const double d00=::dot(v0,v0),d01=::dot(v0,v1),d11=::dot(v1,v1),denom=d00*d11-d01*d01;
            if(denom<1e-14)continue;
            const double u=(d11*::dot(v2,v0)-d01*::dot(v2,v1))/denom;
            const double v=(d00*::dot(v2,v1)-d01*::dot(v2,v0))/denom;
            inside=inside||(u>=-1e-7&&v>=-1e-7&&u+v<=1+1e-7&&
                distance(point,add(a,add(mul(v0,u),mul(v1,v))))<1e-7);
        }
        check(inside,"Twisted Sheet starts outside the joining thickness face");
    }
}
void verify_chain() {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto part=document::PartDocument::create_default();const auto id=part.document_id;live.add_part(part);
    auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
    auto outline=sketcher::Sketch::create_default();outline.owner_container_id=flat.id;flat.flat.sketch_id=outline.id;
    flat.flat.thickness=1;flat.flat.thickness_override=true;static_cast<void>(outline.add_rectangle(0,0,40,30));
    check(workspace::commit_flat(live,kernel,id,flat,outline),"Cannot create chain source");
    auto* state=live.open_part(id);
    std::string parent=flat.id,end_key;
    for(int depth=0;depth<4;++depth) {
        const auto geometry=state->session.calculated_boundaries().back().mesh.original_references;
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
            return e.reference.owner_id==parent&&kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
                e.measured_length&&std::abs(*e.measured_length-40)<1e-6&&
                (end_key.empty()||e.reference.semantic_key.find(end_key)!=std::string::npos);
        });
        check(edge!=geometry.edges.end(),"Chain has no continuation boundary");
        for(const bool last:{false,true}) {
            std::cout<<"Twist chain depth="<<depth<<" last="<<last<<std::endl;
            auto candidate=state->session.document();
            auto twist=document::PartDocument::create_twisted_sheet_container();twist.twisted_sheet.sheet_attachment=true;
            twist.twisted_sheet.length=30;twist.twisted_sheet.angle_degrees=45;
            twist.placement.references=document::bend_sheet_references(*edge,edge->edge_treatment_endpoint_references[last?1:0]);
            candidate.insert_history_entry(document::PartHistoryKind::Feature,twist.id);candidate.history.push_back(twist);
            candidate.resolve_constructions(geometry);
            verify_join(candidate,*candidate.find_container(twist.id),geometry);
            const auto formed=workspace::calculate_part_with_resolved_references(kernel,candidate);
            check(formed.back().calculation_errors.empty(),"Chained twist failed calculation");
            auto unbend=document::PartDocument::create_sketch_container();unbend.feature_kind=document::FeatureKind::Unbend;
            candidate.insert_history_entry(document::PartHistoryKind::Feature,unbend.id);candidate.history.push_back(unbend);
            auto unfolded=workspace::calculate_part_with_resolved_references(kernel,candidate);
            check(unfolded.back().calculation_errors.empty(),"Chained twist cannot unfold");
            std::vector<kernel::BodyResult> restored;
            candidate=document::PartDocument::from_serialized(candidate.serialized(unfolded),&restored);
            auto back=document::PartDocument::create_sketch_container();back.feature_kind=document::FeatureKind::BendBack;
            candidate.insert_history_entry(document::PartHistoryKind::Feature,back.id);candidate.history.push_back(back);
            const auto folded=workspace::calculate_part_with_resolved_references(kernel,candidate);
            check(folded.back().calculation_errors.empty()&&std::abs(folded.back().volume-formed.back().volume)<.05,
                "Chained twist Bend Back changed formed material after persistence");
        }
        if(depth==3)break;
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=bend.id;
        document::initialize_bend_start_profile(profile,40);bend.bend.sketch_id=profile.id;
        bend.bend.sheet_attachment=true;bend.bend.angle_degrees=30;bend.bend.radius=5;bend.bend.radius_follows_thickness=false;
        bend.placement.references=document::bend_sheet_references(*edge);
        check(workspace::commit_bend(live,kernel,id,bend,profile),"Cannot attach next sheet profile");
        const auto* saved=state->session.document().find_container(bend.id);
        const auto path=sketcher::Sketch::from_serialized(saved->bend.auxiliary_sketches[0]);
        end_key=path.arcs.back().end_point_id;parent=bend.id;
    }
}
kernel::Vec3 rotated(kernel::Vec3 value,kernel::Vec3 degrees) {
    constexpr double radians=std::numbers::pi/180.0;
    const double cx=std::cos(degrees.x*radians),sx=std::sin(degrees.x*radians);
    const double cy=std::cos(degrees.y*radians),sy=std::sin(degrees.y*radians);
    const double cz=std::cos(degrees.z*radians),sz=std::sin(degrees.z*radians);
    value={value.x,value.y*cx-value.z*sx,value.y*sx+value.z*cx};
    value={value.x*cy+value.z*sy,value.y,-value.x*sy+value.z*cy};
    return {value.x*cz-value.y*sz,value.x*sz+value.y*cz,value.z};
}
void verify() {
    kernel::OcctKernel kernel;
    workspace::Workspace live;
    auto document=document::PartDocument::create_default();
    document.name="twisted-sheet";
    const auto id=document.document_id;
    live.add_part(std::move(document));
    auto feature=document::PartDocument::create_twisted_sheet_container();
    feature.twisted_sheet.width=40;
    feature.twisted_sheet.length=80;
    feature.twisted_sheet.thickness=2;
    feature.twisted_sheet.angle_degrees=90;
    feature.twisted_sheet.developed_length_correction=2.5;
    const auto owner=feature.id;
    check(workspace::commit_primitive(live,kernel,id,feature,
        workspace::PrimitiveEditMode::Create),"Twisted Sheet was not committed");
    auto* state=live.open_part(id);
    check(state!=nullptr&&!state->session.calculated_boundaries().empty(),
        "Twisted Sheet has no calculated body");
    const auto& body=state->session.calculated_boundaries().back();
    check(body.calculation_errors.empty()&&body.volume>6200&&body.volume<6600,
        "Twisted Sheet volume is outside its geometric tolerance");
    bool side_a=false,side_b=false,boundary=false,end_edge=false;
    for(const auto& face:body.mesh.original_references.triangle_references) {
        if(face.owner_id!=owner)continue;
        side_a|=face.sheet_role==kernel::SheetFaceRole::SideA;
        side_b|=face.sheet_role==kernel::SheetFaceRole::SideB;
    }
    for(const auto& edge:body.mesh.original_references.edges) {
        if(edge.reference.owner_id!=owner)continue;
        boundary|=kernel::sheet_edge_role(edge)==kernel::SheetEdgeRole::Boundary;
        end_edge|=edge.reference.semantic_key.find("twist:path:end")!=std::string::npos;
    }
    check(side_a&&side_b&&boundary,"Twisted Sheet lost sheet face/edge roles");
    check(end_edge,"Twisted Sheet has no stable selectable end edge");
    check(kernel::sheet_material::twist_progress_derivative(0)==0&&
        kernel::sheet_material::twist_progress_derivative(1)==0&&
        kernel::sheet_material::twist_progress_derivative(.5)>1,
        "Twisted Sheet lost its clamped-end transition law");
    bool smooth_rail=false;
    for(const auto& edge:body.mesh.original_references.edges) {
        if(edge.reference.owner_id!=owner||
            edge.reference.semantic_key.find("twist:point:")==std::string::npos||
            !edge.exact_spline)continue;
        const auto start=kernel::bspline_derivative(*edge.exact_spline,0);
        const auto end=kernel::bspline_derivative(*edge.exact_spline,1);
        const auto axial=[](const auto& value) {
            return std::abs(value.z)/std::hypot(std::hypot(value.x,value.y),value.z);
        };
        smooth_rail=axial(start)>.999&&axial(end)>.999;
        if(smooth_rail)break;
    }
    check(smooth_rail,
        "Twisted Sheet longitudinal spline is not tangent to its end clamps");

    auto uncorrected=feature.twisted_sheet;
    uncorrected.developed_length_correction=0;
    const double developed=document::twisted_sheet_developed_length(feature.twisted_sheet);
    check(std::abs(developed-document::twisted_sheet_developed_length(uncorrected)-2.5)<1e-9,
        "Twisted Sheet manufacturing correction was not added to its development");
    auto operations=state->session.document().kernel_operations();
    kernel::HistoryOperation unbend;
    unbend.owner_id="twist-unbend";unbend.primitive=kernel::SheetStateRequest{};
    operations.push_back(unbend);
    const auto unfolded=kernel.evaluate_history(operations).back();
    check(unfolded.calculation_errors.empty(),"Twisted Sheet did not unfold");
    double first=INFINITY,last=-INFINITY;
    for(const auto& point:unfolded.mesh.points) {
        first=std::min(first,point.position.z);last=std::max(last,point.position.z);
    }
    check(std::abs((last-first)-developed)<0.05,
        "Twisted Sheet flat geometry ignored its corrected developed length");

    auto cut_operations=operations;
    kernel::HistoryOperation cut;
    cut.owner_id="twist-flat-cut";cut.operation=kernel::BooleanOperation::Subtract;
    kernel::ExtrusionRequest cut_request;
    cut_request.sheet_cut=true;cut_request.sheet_cut_tolerance=.05;
    cut_request.extent=kernel::ExtrusionRequest::Extent::ThroughAll;
    cut_request.through_all_forward=true;cut_request.through_all_reverse=true;
    cut_request.direction={0,1,0};
    cut_request.profile_region_id="twist-cut-region";
    cut_request.outer_boundary_id="twist-cut-boundary";
    cut_request.outer_edge_source_ids={"cut:a","cut:b","cut:c","cut:d"};
    cut_request.outer_vertex_source_ids={"cut:p0","cut:p1","cut:p2","cut:p3"};
    cut_request.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{
        {-5,0,30},{5,0,30},{5,0,40},{-5,0,40}}};
    cut.primitive=cut_request;
    auto formed_cut_operations=state->session.document().kernel_operations();
    formed_cut_operations.push_back(cut);
    const auto formed_cut=kernel.evaluate_history(formed_cut_operations).back();
    check(formed_cut.calculation_errors.empty()&&formed_cut.volume<body.volume&&
        std::ranges::any_of(formed_cut.sheet_cuts,[](const auto& region) {
            return region.surface_type=="twist";
        }),"Sheet Cut did not cut the formed Twisted Sheet spline surface");
    const auto persisted_formed_cut=document::load_body_result(
        document::serialize_body_result(formed_cut));
    check(std::ranges::any_of(persisted_formed_cut.sheet_cuts,
        [](const auto& region) {
            return region.surface_type=="twist"&&!region.source.surface;
        }),"Native viewer packet lost the Twisted Sheet cut domain");
    cut_operations.push_back(cut);
    const auto cut_flat=kernel.evaluate_history(cut_operations).back();
    check(cut_flat.calculation_errors.empty()&&cut_flat.volume<unfolded.volume,
        "Sheet Cut did not cut the unfolded Twisted Sheet");
    kernel::HistoryOperation cut_back;
    cut_back.owner_id="twist-cut-back";
    cut_back.primitive=kernel::SheetStateRequest{false,true,{}};
    cut_operations.push_back(cut_back);
    const auto cut_formed=kernel.evaluate_history(cut_operations).back();
    check(cut_formed.calculation_errors.empty()&&cut_formed.volume<body.volume,
        "Bend Back did not carry the flat Sheet Cut into Twisted Sheet geometry");
    kernel::HistoryOperation back;
    back.owner_id="twist-back";back.primitive=kernel::SheetStateRequest{false,true,{}};
    operations.push_back(back);
    const auto restored=kernel.evaluate_history(operations).back();
    check(restored.calculation_errors.empty()&&std::abs(restored.volume-body.volume)<0.05,
        "Bend Back did not restore the original Twisted Sheet");
    for(const auto& point:body.mesh.points) {
        double distance=INFINITY;
        for(const auto& candidate:restored.mesh.points)
            distance=std::min(distance,std::hypot(point.position.x-candidate.position.x,
                point.position.y-candidate.position.y,point.position.z-candidate.position.z));
        check(distance<1e-8,"Bend Back accumulated error in Twisted Sheet geometry");
    }

    const auto directory=std::filesystem::temp_directory_path()/
        ("zima-twisted-sheet-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path=directory/"twisted-sheet.prtz";
    state->session.document().save(path);
    const auto loaded=document::PartDocument::load(path);
    check(loaded.history.size()==1&&
        loaded.history.front().feature_kind==document::FeatureKind::TwistedSheet&&
        loaded.history.front().twisted_sheet==feature.twisted_sheet,
        "Twisted Sheet persistence changed its parameters");
    std::filesystem::remove_all(directory);

    auto attached_document=document::PartDocument::create_default();
    attached_document.name="attached-twisted-sheet";
    const auto attached_id=attached_document.document_id;
    workspace::Workspace attached_live;
    attached_live.add_part(std::move(attached_document));
    auto flat=document::PartDocument::create_sketch_container();
    flat.feature_kind=document::FeatureKind::Flat;
    flat.name="Flat";flat.flat.thickness=2;flat.flat.thickness_override=true;
    auto outline=sketcher::Sketch::create_default();
    outline.owner_container_id=flat.id;flat.flat.sketch_id=outline.id;
    static_cast<void>(outline.add_rectangle(0,0,40,30));
    check(workspace::commit_flat(attached_live,kernel,attached_id,flat,outline),
        "Flat source for Twisted Sheet was not committed");
    auto* attached_state=attached_live.open_part(attached_id);
    const auto source_geometry=attached_state->session.calculated_boundaries().back().mesh.original_references;
    const auto edge=std::ranges::find_if(source_geometry.edges,[](const auto& value) {
        return kernel::sheet_edge_role(value)==kernel::SheetEdgeRole::Boundary&&
            value.measured_length&&std::abs(*value.measured_length-40)<1e-6;
    });
    check(edge!=source_geometry.edges.end(),"Flat has no eligible long boundary edge");
    auto attached_feature=document::PartDocument::create_twisted_sheet_container();
    attached_feature.twisted_sheet.sheet_attachment=true;
    attached_feature.twisted_sheet.width=*edge->measured_length;
    attached_feature.twisted_sheet.thickness=
        edge->edge_treatment_side_references.front().sheet_thickness;
    attached_feature.twisted_sheet.length=50;
    attached_feature.twisted_sheet.angle_degrees=45;
    attached_feature.placement.references=document::bend_sheet_references(*edge);
    check(workspace::commit_primitive(attached_live,kernel,attached_id,
        attached_feature,workspace::PrimitiveEditMode::Create),
        "Attached Twisted Sheet was not committed");
    const auto& attached_body=attached_state->session.calculated_boundaries().back();
    check(attached_body.calculation_errors.empty()&&attached_body.volume>0,
        "Attached Twisted Sheet failed to fuse with its source sheet");
    const auto saved_feature=attached_state->session.document().find_container(attached_feature.id);
    check(saved_feature&&saved_feature->twisted_sheet.sheet_attachment&&
        saved_feature->placement.references.size()==3,
        "Attached Twisted Sheet lost its derived edge placement");
    const auto preview=attached_state->session.document().primitive_preview_edges(*saved_feature);
    const auto start_outline=std::ranges::find_if(preview,[](const auto& value) {
        return value.reference.semantic_key=="preview:twist:start";
    });
    check(start_outline!=preview.end()&&start_outline->points.size()==5,
        "Attached Twisted Sheet has no complete cyan start outline");
    // The first broad edge of the new sheet must be the selected source edge
    // itself.  This catches a valid-volume solid that is nevertheless built
    // in the wrong local plane or starts beside/perpendicular to its parent.
    const auto endpoint_distance=[&](const auto& source_point) {
        return std::min(distance(source_point,start_outline->points[0]),
            distance(source_point,start_outline->points[1]));
    };
    check(endpoint_distance(edge->points.front())<1e-7&&
        endpoint_distance(edge->points.back())<1e-7,
        "Attached Twisted Sheet cyan wire does not start on the selected edge");
    check(distance(start_outline->points.front(),edge->points.front())<1e-7,
        "Attached Twisted Sheet does not start at its selected endpoint");
    const auto source_direction=unit({edge->points.back().x-edge->points.front().x,
        edge->points.back().y-edge->points.front().y,
        edge->points.back().z-edge->points.front().z});
    const auto preview_width=unit({start_outline->points[1].x-start_outline->points[0].x,
        start_outline->points[1].y-start_outline->points[0].y,
        start_outline->points[1].z-start_outline->points[0].z});
    check(std::abs(std::abs(dot(source_direction,preview_width))-1)<1e-8,
        "Attached Twisted Sheet width is not parallel to the selected edge");
    const kernel::Vec3 preview_thickness{
        start_outline->points[4-1].x-start_outline->points[0].x,
        start_outline->points[4-1].y-start_outline->points[0].y,
        start_outline->points[4-1].z-start_outline->points[0].z};
    check(std::abs(dot(source_direction,unit(preview_thickness)))<1e-8&&
        std::abs(distance(start_outline->points[0],start_outline->points[3])-
            saved_feature->twisted_sheet.thickness)<1e-7,
        "Attached Twisted Sheet thickness is not normal to its selected edge");
    const auto placed_y=unit(rotated({0,1,0},
        {saved_feature->placement.rotation_x,saved_feature->placement.rotation_y,
         saved_feature->placement.rotation_z}));
    check(std::abs(std::abs(dot(source_direction,placed_y))-1)<1e-8,
        "Attached Twisted Sheet container axis is not aligned with its selected edge");
    auto alternate_document=attached_state->session.document();
    auto* alternate=alternate_document.find_container(attached_feature.id);
    check(alternate!=nullptr&&edge->edge_treatment_endpoint_references.size()==2,
        "Attached Twisted Sheet cannot test its alternate endpoint");
    alternate->placement.references=document::bend_sheet_references(
        *edge,edge->edge_treatment_endpoint_references.back());
    alternate->placement.references[2].flip=true;
    alternate_document.resolve_constructions(source_geometry);
    const auto alternate_preview=alternate_document.primitive_preview_edges(*alternate);
    const auto alternate_start=std::ranges::find_if(alternate_preview,[](const auto& value) {
        return value.reference.semantic_key=="preview:twist:start";
    });
    check(alternate_start!=alternate_preview.end()&&alternate_start->points.size()==5&&
        distance(alternate_start->points.front(),edge->points.back())<1e-7,
        "Changing the Twisted Sheet endpoint did not move its origin to that endpoint");
    const auto alternate_width=unit({
        alternate_start->points[1].x-alternate_start->points[0].x,
        alternate_start->points[1].y-alternate_start->points[0].y,
        alternate_start->points[1].z-alternate_start->points[0].z});
    check(std::abs(std::abs(dot(source_direction,alternate_width))-1)<1e-8&&
        dot(preview_width,alternate_width)<-.999999,
        "Changing the Twisted Sheet endpoint did not reverse width along the same edge");
    auto attached_operations=attached_state->session.document().kernel_operations();
    kernel::HistoryOperation attached_unbend;
    attached_unbend.owner_id="attached-twist-unbend";
    attached_unbend.primitive=kernel::SheetStateRequest{};
    attached_operations.push_back(attached_unbend);
    const auto attached_flat=kernel.evaluate_history(attached_operations).back();
    check(attached_flat.calculation_errors.empty()&&attached_flat.volume>0,
        "Attached Twisted Sheet did not unfold with its parent sheet");
    kernel::HistoryOperation attached_back;
    attached_back.owner_id="attached-twist-back";
    attached_back.primitive=kernel::SheetStateRequest{false,true,{}};
    attached_operations.push_back(attached_back);
    const auto attached_restored=kernel.evaluate_history(attached_operations).back();
    check(attached_restored.calculation_errors.empty()&&
        std::abs(attached_restored.volume-attached_body.volume)<0.05,
        "Attached Twisted Sheet did not return to its authored formed body");
}
}
int main() {
    try {
        verify();verify_chain();
        kernel::OcctKernel kernel;
        auto part=document::PartDocument::load("cpp/tests/fixtures/sheet/profile-side-twist.prtz");
        const auto formed=workspace::calculate_part_with_resolved_references(kernel,part);
        check(formed.back().calculation_errors.empty(),"Profile-side fixture cannot calculate");
        const auto twist=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::TwistedSheet;});
        check(twist!=part.history.end()&&twist->twisted_sheet.attachment_material_side==-1,
            "Profile-side fixture lost its opposite material side");
        verify_join(part,*twist,formed.back().mesh.original_references);
        auto unfold=document::PartDocument::create_sketch_container();unfold.feature_kind=document::FeatureKind::Unbend;
        part.insert_history_entry(document::PartHistoryKind::Feature,unfold.id);part.history.push_back(unfold);
        const auto flat=workspace::calculate_part_with_resolved_references(kernel,part);
        check(flat.back().calculation_errors.empty(),"Profile-side twist cannot unfold");
        part=document::PartDocument::from_serialized(part.serialized(flat));
        auto back=document::PartDocument::create_sketch_container();back.feature_kind=document::FeatureKind::BendBack;
        part.insert_history_entry(document::PartHistoryKind::Feature,back.id);part.history.push_back(back);
        const auto folded=workspace::calculate_part_with_resolved_references(kernel,part);
        check(folded.back().calculation_errors.empty()&&std::abs(folded.back().volume-formed.back().volume)<.05,
            "Profile-side twist did not fold back after persistence");
        std::cout<<"Twisted Sheet geometry, chained attachment, sides, unfolding and persistence passed\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
