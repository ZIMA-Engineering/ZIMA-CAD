#include <zima/document/boundary_surface.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/surface_results.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/viewer/picking.hpp>
#include <filesystem>
#include <cstdlib>
#include <zima/workspace/boundary_surface_operations.hpp>
#include <zima/workspace/surface_sewing_operations.hpp>
#include <zima/document/surface_sewing.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/workspace/general_surface_operations.hpp>
#include <zima/workspace/surface_intersection_operations.hpp>
#include <zima/document/surface_intersection.hpp>
#include <zima/document/surface_trim.hpp>
#include <zima/workspace/surface_trim_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include "../app/surface_wire_visibility.hpp"
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/history_operations.hpp>
#include <iostream>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Geom_BSplineSurface.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLProp_SLProps.hxx>
#include <sstream>
using namespace zima;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
kernel::BoundarySurfaceRequest rectangle(double height=0){
    kernel::BoundarySurfaceRequest r;r.region_id="region";
    const std::array<kernel::Vec3,4> p{{{0,0,0},{100,0,0},{100,80,height},{0,80,0}}};
    for(unsigned i=0;i<4;++i){auto& b=r.boundaries[i];r.source_owners[i]="source"+std::to_string(i);
        b.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{p[i],p[(i+1)%4]}}};
        b.outer_edge_source_ids={"curve"};b.outer_vertex_source_ids={"a"};b.open_profile_end_id="b";b.direction={0,0,1};}
    return r;
}
int main(){try{
    kernel::OcctKernel k;
    const auto calculate=[&](const auto& r){return k.evaluate_history({{"boundary",r}}).back();};
    auto r=rectangle();auto flat=calculate(r);
    require(flat.calculation_errors.empty()&&std::abs(flat.surface_area-8000)<0.01&&std::abs(flat.volume)<1e-9,"Planar surface area or volume failed");
    require(kernel::has_surface_results(flat.mesh),"Surface classification lost");
    require(flat.mesh.original_references.edges.size()==4,"Boundary edge identities missing");
    require(flat.mesh.original_references.points.size()==4,"Corner identities missing");
    auto section_plane=rectangle();section_plane.region_id="section-plane";
    for(unsigned i=0;i<4;++i) {
        section_plane.source_owners[i]="section-boundary-"+std::to_string(i);
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(section_plane.boundaries[i].outer_profile).curves.front());
        for(auto* point:{&line.start,&line.end})*point={50,point->x-10,point->y-40};
    }
    kernel::SurfaceIntersectionRequest section;
    section.faces={kernel::FaceReference{"boundary","surface:from:region",{}},kernel::FaceReference{"section-plane","surface:from:section-plane",{}}};
    const auto intersection_operations=std::vector<kernel::HistoryOperation>{{"boundary",rectangle()},{"section-plane",section_plane},{"intersection",section}};
    const auto section_result=k.evaluate_history(intersection_operations).back();
    require(section_result.calculation_errors.empty()&&std::abs(section_result.surface_area-16000)<.02&&std::abs(section_result.volume)<1e-9,
        "Intersection modified its input surfaces");
    std::vector<kernel::ViewerEdge> section_curves;
    for(const auto& edge:section_result.mesh.original_references.edges)if(edge.reference.owner_id=="intersection")section_curves.push_back(edge);
    require(section_curves.size()==1&&section_curves.front().exact_spline&&section_curves.front().measured_length&&
        std::abs(*section_curves.front().measured_length-80)<.001,"Bounded planar intersection lost its exact reusable curve");
    require(section_curves.front().reference.semantic_key.find("boundary")!=std::string::npos&&
        section_curves.front().reference.semantic_key.find("section-plane")!=std::string::npos,"Intersection curve lost one source parent");
    for(const auto& p:section_curves.front().points)require(std::abs(p.x-50)<.001&&std::abs(p.z)<.001&&p.y>=-.001&&p.y<=80.001,
        "Intersection silently extended a bounded source face");
    std::swap(section.faces[0],section.faces[1]);auto reversed_intersection=intersection_operations;reversed_intersection.back().primitive=section;
    const auto reverse_section=k.evaluate_history(reversed_intersection).back();
    require(std::ranges::any_of(reverse_section.mesh.original_references.edges,[&](const auto& edge){return edge.reference==section_curves.front().reference;}),
        "Reversing the input face order renamed the intersection branch");
    const auto reversed_branch=std::ranges::find_if(reverse_section.mesh.original_references.edges,[&](const auto& edge){return edge.reference==section_curves.front().reference;});
    require(reversed_branch->edge_treatment_endpoint_references==section_curves.front().edge_treatment_endpoint_references,
        "Reversing the input face order exchanged native intersection endpoints");
    kernel::SurfaceTrimRequest trim;trim.target=std::get<kernel::SurfaceIntersectionRequest>(intersection_operations.back().primitive).faces[0];
    trim.tools={{{"section-plane","surface:from:section-plane",{}},true}};trim.seed={25,40,0};
    auto trim_operations=intersection_operations;trim_operations.push_back({"trim",trim});
    const auto trimmed_history=k.evaluate_history(trim_operations);const auto& trimmed=trimmed_history.back();
    require(trimmed.calculation_errors.empty()&&std::abs(trimmed.surface_area-12000)<.02&&std::abs(trimmed.volume)<1e-9,
        "Face trim did not retain exactly half the target and the independent cutting surface");
    const auto trim_face=std::ranges::find_if(trimmed.mesh.triangle_references,[](const auto& face){return face.owner_id=="trim";});
    require(trim_face!=trimmed.mesh.triangle_references.end()&&trim_face->surface_result&&trim_face->semantic_key.starts_with("trim:face:from:"),
        "Trim lost its native surface ancestry");
    const auto retained_key=trim_face->semantic_key;trim.expected_region_key=retained_key;
    require(retained_key.find("trim:cut")==std::string::npos,"Trim persisted a nonexistent intermediate Section parent");
    trim_operations.back().primitive=trim;const auto trimmed_warm=k.evaluate_history_incremental(trim_operations,trimmed_history).back();
    require(trimmed_warm.calculation_errors.empty(),"Capturing retained region intent changed valid geometry");
    trim.expected_region_key="missing-region";trim_operations.back().primitive=trim;
    require(!k.evaluate_history_recovering(trim_operations,trimmed_history).back().calculation_errors.empty(),"Trim silently rebound a missing retained region");
    trim.expected_region_key.clear();trim.seed={75,40,0};trim_operations.back().primitive=trim;
    const auto other_half=k.evaluate_history(trim_operations).back();
    require(std::abs(other_half.surface_area-12000)<.02&&std::ranges::any_of(other_half.mesh.triangle_references,[&](const auto& face){return face.owner_id=="trim"&&face.semantic_key!=retained_key;}),
        "Opposite retained trim regions shared one persistent identity");
    trim.seed={50,40,0};trim_operations.back().primitive=trim;
    bool trim_boundary_rejected=false;try{trim_boundary_rejected=!k.evaluate_history(trim_operations).back().calculation_errors.empty();}catch(const std::exception&){trim_boundary_rejected=true;}
    require(trim_boundary_rejected,"Trim accepted an ambiguous seed on the cutting boundary");
    trim.seed={25,40,0};trim.tools={{section_curves.front().reference,false}};trim_operations.back().primitive=trim;
    const auto edge_trimmed=k.evaluate_history(trim_operations).back();
    require(edge_trimmed.calculation_errors.empty()&&std::abs(edge_trimmed.surface_area-12000)<.02,
        "Trim could not consume the native reusable intersection curve");
    auto coincident=intersection_operations;coincident[1].primitive=rectangle();
    auto coincident_request=std::get<kernel::SurfaceIntersectionRequest>(coincident.back().primitive);coincident_request.faces[1].semantic_key="surface:from:region";
    coincident.back().primitive=coincident_request;
    bool coincident_rejected=false;try{coincident_rejected=!k.evaluate_history(coincident).back().calculation_errors.empty();}catch(const std::exception&){coincident_rejected=true;}
    require(coincident_rejected,"Coincident surface areas invented an intersection curve");
    auto disjoint_operations=intersection_operations;
    auto disjoint_plane=section_plane;
    for(auto& boundary:disjoint_plane.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.x=200;line.end.x=200;
    }
    disjoint_operations[1].primitive=disjoint_plane;
    const auto disjoint_section=k.evaluate_history(disjoint_operations).back();
    require(disjoint_section.calculation_errors.empty()&&std::ranges::none_of(disjoint_section.mesh.original_references.edges,
        [](const auto& edge){return edge.reference.owner_id=="intersection";}),"Disjoint faces became an intersection or a calculation error");
    auto concave=rectangle();
    const std::array<kernel::Vec3,8> concave_points{{{0,0,0},{100,0,0},{100,20,0},{20,20,0},{20,60,0},{100,60,0},{100,80,0},{0,80,0}}};
    concave.boundaries.resize(8);concave.source_owners.resize(8);
    for(unsigned i=0;i<8;++i) {
        auto& boundary=concave.boundaries[i];concave.source_owners[i]="concave-source-"+std::to_string(i);
        boundary.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{concave_points[i],concave_points[(i+1)%8]}}};
        boundary.outer_edge_source_ids={"curve"};boundary.outer_vertex_source_ids={"a"};boundary.open_profile_end_id="b";
    }
    auto multi_operations=intersection_operations;multi_operations[0].primitive=concave;
    const auto multiple=k.evaluate_history(multi_operations).back();
    unsigned branch_count=0;double branch_length=0;std::set<std::string> branch_names;
    for(const auto& edge:multiple.mesh.original_references.edges)if(edge.reference.owner_id=="intersection") {
        ++branch_count;branch_length+=edge.measured_length.value_or(0);branch_names.insert(edge.reference.semantic_key);
    }
    require(multiple.calculation_errors.empty()&&branch_count==2&&branch_names.size()==2&&std::abs(branch_length-40)<.001,
        "Disconnected bounded intersection branches were lost or numbered by traversal");
    auto moved_intersection=intersection_operations;auto moved_plane=section_plane;
    for(auto& boundary:moved_plane.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.x=line.end.x=60;
    }
    moved_intersection[1].primitive=moved_plane;
    kernel::OcctKernel changed_intersection_kernel;
    const auto old_sections=changed_intersection_kernel.evaluate_history(intersection_operations);
    const auto changed_sections=changed_intersection_kernel.evaluate_history_incremental(moved_intersection,old_sections);
    require(changed_sections.back().calculation_errors.empty(),"Changed intersection source failed regeneration");
    bool changed_branch=false;
    for(const auto& edge:changed_sections.back().mesh.original_references.edges)if(edge.reference.owner_id=="intersection") {
        changed_branch=true;require(edge.reference==section_curves.front().reference,"Source displacement renamed an unchanged intersection branch");
        for(const auto& point:edge.points)require(std::abs(point.x-60)<.001,"Source edit reused stale intersection geometry");
    }
    require(changed_branch,"Source edit lost the intersection branch");
    auto warped_operations=intersection_operations;warped_operations[0].primitive=rectangle(20);
    const auto warped_section=k.evaluate_history(warped_operations).back();
    require(warped_section.calculation_errors.empty(),"Warped B-spline surface intersection failed");
    const auto warped_input=k.evaluate_history({warped_operations.front()}).back();
    TopoDS_Shape warped_shape;BRep_Builder warped_builder;std::istringstream warped_stream(warped_input.kernel_shape);
    BRepTools::Read(warped_shape,warped_stream,warped_builder);
    Handle(Geom_Surface) warped_surface;
    for(TopExp_Explorer it(warped_shape,TopAbs_FACE);it.More();it.Next())warped_surface=BRep_Tool::Surface(TopoDS::Face(it.Current()));
    bool warped_branch=false;
    for(const auto& edge:warped_section.mesh.original_references.edges)if(edge.reference.owner_id=="intersection") {
        warped_branch=true;require(edge.exact_spline.has_value(),"Warped intersection lost its exact curve");
        for(unsigned sample=0;sample<=64;++sample) {
            const auto point=kernel::bspline_value(*edge.exact_spline,sample/64.);
            GeomAPI_ProjectPointOnSurf projection(gp_Pnt(point.x,point.y,point.z),warped_surface);
            require(std::abs(point.x-50)<.001&&projection.NbPoints()>0&&projection.LowerDistance()<.001,
                "Warped intersection does not lie on both source surfaces");
        }
    }
    require(warped_branch,"Warped intersection omitted a branch");
    auto warped_trim_request=trim;warped_trim_request.tools={{{"section-plane","surface:from:section-plane",{}},true}};
    auto warped_trim_operations=warped_operations;warped_trim_operations.push_back({"warped-trim",warped_trim_request});
    const auto warped_trim=k.evaluate_history(warped_trim_operations).back();
    require(warped_trim.calculation_errors.empty()&&warped_trim.surface_area>11000&&warped_trim.surface_area<14000&&std::abs(warped_trim.volume)<1e-9,
        "Trim could not split a general B-spline surface");
    for(std::size_t i=0;i<warped_trim.mesh.triangle_references.size();++i)if(warped_trim.mesh.triangle_references[i].owner_id=="warped-trim")
        for(unsigned corner=0;corner<3;++corner) {
            const auto point=warped_trim.mesh.vertices[warped_trim.mesh.triangles[3*i+corner]];
            GeomAPI_ProjectPointOnSurf projection(gp_Pnt(point.x,point.y,point.z),warped_surface);
            require(point.x<=50.002&&projection.NbPoints()>0&&projection.LowerDistance()<.001,
                "Trim changed the original curved surface or retained the wrong region");
        }
    const auto constrained=[&](kernel::SurfaceContinuity continuity,bool side=false) {
        auto request=rectangle();request.region_id="constrained";request.constraints.resize(4);
        for(unsigned i=0;i<4;++i) {
            request.source_owners[i]="boundary";
            auto& constraint=request.constraints[i];constraint.edge=kernel::EdgeReference{"boundary","boundary:source:source"+std::to_string(i)+":edge:from:curve",{}};
            constraint.continuity=continuity;constraint.support=kernel::FaceReference{"boundary","surface:from:region",{}};constraint.support_reversed=side;
        }
        return request;
    };
    for(const auto continuity:{kernel::SurfaceContinuity::G0,kernel::SurfaceContinuity::G1,kernel::SurfaceContinuity::G2})for(bool side:{false,true}) {
        const auto request=constrained(continuity,side);
        const auto result=k.evaluate_history({{"boundary",rectangle()},{"fill",request}}).back();
        require(result.calculation_errors.empty()&&std::abs(result.surface_area-16000)<.01&&std::abs(result.volume)<1e-9,
            "Original-edge fill or planar G1/G2 support failed");
        for(const auto& point:result.mesh.original_references.vertices)require(std::abs(point.z)<1e-7,"Planar continuity produced nonplanar geometry");
        require(std::ranges::count_if(result.mesh.original_references.edges,[](const auto& edge){return edge.reference.owner_id=="fill";})==4,
            "Original-edge filling lost its parent edge identities");
    }
    require(kernel::history_fingerprint({{"fill",constrained(kernel::SurfaceContinuity::G1,false)}},1)!=
        kernel::history_fingerprint({{"fill",constrained(kernel::SurfaceContinuity::G1,true)}},1),"Support side was removed from calculation identity");
    kernel::OcctKernel incremental_kernel;const auto original_prefix=incremental_kernel.evaluate_history({{"boundary",rectangle()}});
    const auto extended=incremental_kernel.evaluate_history_incremental({{"boundary",rectangle()},{"fill",constrained(kernel::SurfaceContinuity::G1)}},original_prefix);
    require(extended.back().calculation_errors.empty()&&std::abs(extended.back().surface_area-16000)<.01,
        "Adding the first original-edge fill could not reconstruct its cached input ancestry");
    auto missing_support=constrained(kernel::SurfaceContinuity::G1);missing_support.constraints[0].support.reset();
    bool support_rejected=false;try{support_rejected=!k.evaluate_history({{"boundary",rectangle()},{"fill",missing_support}}).back().calculation_errors.empty();}catch(const std::exception&){support_rejected=true;}
    require(support_rejected,"G1 silently accepted a missing support");
    auto missing_edge=constrained(kernel::SurfaceContinuity::G0);missing_edge.constraints[0].edge->semantic_key="missing";
    bool edge_rejected=false;try{edge_rejected=!k.evaluate_history({{"boundary",rectangle()},{"fill",missing_edge}}).back().calculation_errors.empty();}catch(const std::exception&){edge_rejected=true;}
    require(edge_rejected,"Missing original edge was silently replaced");
    const auto cylinder_source=[](double radius) {
        kernel::ExtrusionRequest source;source.surface_result=true;source.direction={0,0,30};source.profile_region_id="cylinder-region";
        source.outer_edge_source_ids={"arc"};source.outer_vertex_source_ids={"p0"};source.open_profile_end_id="p1";
        source.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{radius,0,0},
            {radius/std::sqrt(2.),radius/std::sqrt(2.),0},{0,radius,0}}}};return source;
    };
    auto cylinder_plane=rectangle();
    for(auto& boundary:cylinder_plane.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.z=line.end.z=15;
    }
    kernel::SurfaceIntersectionRequest curved_section;
    curved_section.faces={kernel::FaceReference{"cylinder","generated:arc",{}},kernel::FaceReference{"plane","surface:from:region",{}}};
    const auto cylinder_section=k.evaluate_history({{"cylinder",cylinder_source(50)},{"plane",cylinder_plane},{"section",curved_section}}).back();
    require(cylinder_section.calculation_errors.empty(),"Cylinder/plane intersection failed");
    unsigned cylinder_branches=0;
    for(const auto& edge:cylinder_section.mesh.original_references.edges)if(edge.reference.owner_id=="section") {
        ++cylinder_branches;require(edge.exact_spline&&edge.measured_length&&std::abs(*edge.measured_length-25*std::acos(-1.))<.001,
            "Cylinder intersection lost exact arc length");
        for(unsigned sample=0;sample<=64;++sample) {
            const auto point=kernel::bspline_value(*edge.exact_spline,sample/64.);
            require(std::abs(std::hypot(point.x,point.y)-50)<.001&&std::abs(point.z-15)<.001,
                "Cylinder intersection failed independent geometric check");
        }
    }
    require(cylinder_branches==1,"Cylinder intersection dropped or duplicated a branch");
    kernel::RevolutionRequest sphere;sphere.surface_result=true;sphere.axis_direction={0,1,0};sphere.profile_region_id="sphere-region";
    sphere.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{0,-20,0},{20,0,0},{0,20,0}}}};
    sphere.outer_edge_source_ids={"meridian"};sphere.outer_vertex_source_ids={"south"};sphere.open_profile_end_id="north";
    const auto sphere_plane=[](double x) {
        auto request=rectangle();const std::array<kernel::Vec3,4> points{{{x,-30,-30},{x,30,-30},{x,30,30},{x,-30,30}}};
        for(unsigned i=0;i<4;++i)request.boundaries[i].outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{points[i],points[(i+1)%4]}}};
        return request;
    };
    kernel::SurfaceIntersectionRequest sphere_section;
    sphere_section.faces={kernel::FaceReference{"sphere","generated:meridian",{}},kernel::FaceReference{"plane","surface:from:region",{}}};
    const auto closed_section=k.evaluate_history({{"sphere",sphere},{"plane",sphere_plane(5)},{"section",sphere_section}}).back();
    require(closed_section.calculation_errors.empty(),"Closed sphere/plane intersection failed");
    double closed_length=0;unsigned closed_count=0;
    for(const auto& edge:closed_section.mesh.original_references.edges)if(edge.reference.owner_id=="section") {
        ++closed_count;closed_length+=edge.measured_length.value_or(0);
        require(edge.exact_spline.has_value(),"Closed intersection omitted exact curve");
    }
    require(closed_count==1&&std::abs(closed_length-2*std::acos(-1.)*std::sqrt(375.))<.001,
        "Closed intersection lost its bounded circle");
    require(std::ranges::none_of(closed_section.mesh.original_references.points,[](const auto& point){return point.reference.owner_id=="section";}),
        "Closed intersection invented a native point at the parameter seam");
    kernel::SurfaceTrimRequest closed_trim;closed_trim.target={"plane","surface:from:region",{}};
    closed_trim.tools={{{"sphere","generated:meridian",{}},true}};closed_trim.seed={5,0,0};
    const auto trim_circle=[&](const auto& request){return k.evaluate_history({{"sphere",sphere},{"plane",sphere_plane(5)},{"closed-trim",request}}).back();};
    const auto inner_circle=trim_circle(closed_trim);
    require(inner_circle.calculation_errors.empty()&&std::abs(inner_circle.surface_area-(1600+375)*std::acos(-1.))<.05,
        "A closed cutting loop could not retain its interior");
    closed_trim.seed={5,25,25};const auto outer_circle=trim_circle(closed_trim);
    require(outer_circle.calculation_errors.empty()&&std::abs(outer_circle.surface_area-(1600-375)*std::acos(-1.)-3600)<.05,
        "A closed cutting loop could not retain the exterior with a real hole");
    const auto tangent_section=k.evaluate_history({{"sphere",sphere},{"plane",sphere_plane(20)},{"section",sphere_section}}).back();
    require(tangent_section.calculation_errors.empty()&&std::ranges::none_of(tangent_section.mesh.original_references.edges,
        [](const auto& edge){return edge.reference.owner_id=="section";}),"Tangent point invented a curve");
    require(std::ranges::count_if(tangent_section.mesh.original_references.points,[](const auto& point){return point.reference.owner_id=="section"&&
        std::hypot(point.position.x-20,point.position.y,point.position.z)<.001;})==1,"Isolated tangent contact was lost");
    auto reused_intersection=rectangle();reused_intersection.constraints.resize(4);
    for(auto& boundary:reused_intersection.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.x=50+line.start.x/2;line.end.x=50+line.end.x/2;
    }
    reused_intersection.constraints[3].edge=section_curves.front().reference;
    reused_intersection.source_owners[3]="intersection";
    auto reused_operations=intersection_operations;reused_operations.push_back({"reused-fill",reused_intersection});
    const auto reused_result=k.evaluate_history(reused_operations).back();
    require(reused_result.calculation_errors.empty()&&std::abs(reused_result.surface_area-20000)<.02,
        "Fill could not consume an exact original intersection curve");
    const auto bottom_branch=std::ranges::find_if(multiple.mesh.original_references.edges,[](const auto& edge){return edge.reference.owner_id=="intersection"&&
        std::ranges::all_of(edge.points,[](const auto& point){return point.y>=-.001&&point.y<=20.001;});});
    require(bottom_branch!=multiple.mesh.original_references.edges.end(),"Missing lower disconnected branch fixture");
    auto branch_fill=reused_intersection;
    for(auto& boundary:branch_fill.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.y/=4;line.end.y/=4;
    }
    branch_fill.constraints[3].edge=bottom_branch->reference;
    auto split_consumer=multi_operations;split_consumer.push_back({"branch-fill",branch_fill});
    const auto split_result=k.evaluate_history(split_consumer);
    require(split_result.back().calculation_errors.empty(),"Fill could not consume one selected disconnected branch");
    auto merged_plane=section_plane;
    for(auto& boundary:merged_plane.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.x=line.end.x=10;
    }
    split_consumer[1].primitive=merged_plane;
    bool missing_branch_rejected=false;
    try{missing_branch_rejected=!k.evaluate_history_incremental(split_consumer,split_result).back().calculation_errors.empty();}catch(const std::exception&){missing_branch_rejected=true;}
    require(missing_branch_rejected,"A removed intersection branch silently rebound its Fill reference to the merged curve");
    auto curved_constraints=rectangle();curved_constraints.region_id="curved-fill";curved_constraints.constraints.resize(4);
    const std::array<std::string,4> cylinder_edges{"start:arc","generated:p1","end:arc","generated:p0"};
    for(int i=0;i<4;++i) {
        curved_constraints.source_owners[i]="cylinder";auto& constraint=curved_constraints.constraints[i];
        constraint.edge=kernel::EdgeReference{"cylinder",cylinder_edges[i],{}};constraint.continuity=kernel::SurfaceContinuity::G2;
        constraint.support=kernel::FaceReference{"cylinder","generated:arc",{}};
    }
    const auto check_cylinder_fill=[&](double radius,const kernel::BodyResult& result) {
        require(result.calculation_errors.empty()&&std::abs(result.surface_area-30*radius*std::acos(-1.))<.1,"Curved G2 filling failed cylinder area");
        TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(result.kernel_shape);BRepTools::Read(shape,stream,builder);
        Handle(Geom_Surface) surface;unsigned spline_faces=0;
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto candidate=BRep_Tool::Surface(TopoDS::Face(it.Current()));
            if(!Handle(Geom_BSplineSurface)::DownCast(candidate).IsNull()){surface=candidate;++spline_faces;}
        }
        require(spline_faces==1,"Curved fill did not produce one independently testable parametric surface");
        const auto pi=std::acos(-1.);
        for(const auto& sample:std::array<std::pair<double,double>,4>{std::pair{pi/4,0.},std::pair{pi/4,30.},std::pair{0.,15.},std::pair{pi/2,15.}}) {
            const auto [angle,z]=sample;const gp_Pnt point(radius*std::cos(angle),radius*std::sin(angle),z);
            GeomAPI_ProjectPointOnSurf projection(point,surface);require(projection.NbPoints()>0&&projection.LowerDistance()<curved_constraints.tolerance,
                "Curved G2 boundary fit failed independent projection");
            double u,v;projection.LowerDistanceParameters(u,v);GeomLProp_SLProps properties(surface,u,v,2,1e-9);
            require(properties.IsNormalDefined()&&properties.IsCurvatureDefined(),"Curved G2 derivatives are undefined");
            const gp_Dir normal(std::cos(angle),std::sin(angle),0);
            const double cosine=std::clamp(std::abs(normal.Dot(properties.Normal())),0.,1.);
            require(std::acos(cosine)<=curved_constraints.angular_tolerance,"Curved G2 failed independent tangent-plane check");
            const auto a=std::abs(properties.MinCurvature()),b=std::abs(properties.MaxCurvature());
            require(std::min(a,b)<=curved_constraints.curvature_tolerance&&std::abs(std::max(a,b)-1/radius)<=curved_constraints.curvature_tolerance,
                "Curved G2 failed independent principal-curvature check");
            gp_Dir maximum,minimum;properties.CurvatureDirections(maximum,minimum);
            const auto axial=a<b?minimum:maximum;
            require(std::abs(axial.Z())>=std::cos(curved_constraints.angular_tolerance),
                "Curved G2 principal directions do not follow the source cylinder");
        }
        for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="curved-fill")
            for(const auto& point:edge.points)require(std::abs(std::hypot(point.x,point.y)-radius)<.001,"Source edit reused stale fill reference geometry");
    };
    check_cylinder_fill(50,k.evaluate_history({{"cylinder",cylinder_source(50)},{"curved-fill",curved_constraints}}).back());
    check_cylinder_fill(60,k.evaluate_history({{"cylinder",cylinder_source(60)},{"curved-fill",curved_constraints}}).back());
    const auto vertical_patch=[](double height) {
        auto request=rectangle();const std::array<kernel::Vec3,4> points{{{100,0,0},{100,80,0},{100,80,height},{100,0,height}}};
        for(int i=0;i<4;++i)request.boundaries[i].outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{points[i],points[(i+1)%4]}}};
        return request;
    };
    kernel::SurfaceSewingRequest sew;sew.faces={{"floor","surface:from:region",{}},{"wall","surface:from:region",{}}};
    const auto sewing_history=[&](double height,const auto& request) {
        return std::vector<kernel::HistoryOperation>{{"floor",rectangle()},{"wall",vertical_patch(height)},{"sewn",request}};
    };
    const auto sewn_result=k.evaluate_history(sewing_history(80,sew)).back();
    require(sewn_result.calculation_errors.empty()&&std::abs(sewn_result.surface_area-14400)<.01&&std::abs(sewn_result.volume)<1e-9,
        "Sewing changed area or created material");
    {
        TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(sewn_result.kernel_shape);BRepTools::Read(shape,stream,builder);
        unsigned shells=0,solids=0;for(TopExp_Explorer it(shape,TopAbs_SHELL);it.More();it.Next())++shells;
        for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
        require(shells==1&&solids==0,"Sewing did not create one open shell");
    }
    std::set<std::string> sewn_keys;kernel::EdgeReference joint;
    for(const auto& edge:sewn_result.mesh.original_references.edges)if(edge.reference.owner_id=="sewn") {
        sewn_keys.insert(edge.reference.semantic_key);
        if(std::ranges::all_of(edge.points,[](const auto& point){return std::abs(point.x-100)<1e-7&&std::abs(point.z)<1e-7;}))joint=edge.reference;
    }
    require(sewn_keys.size()==7&&joint.valid(),"Sewing lost native boundaries or the shared edge");
    const auto check_sewn_keys=[&](const kernel::BodyResult& result) {
        std::set<std::string> keys;for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="sewn")keys.insert(edge.reference.semantic_key);
        require(keys==sewn_keys,"Sewing changed identity after selection order or source size changed");
    };
    auto reordered_sew=sew;std::reverse(reordered_sew.faces.begin(),reordered_sew.faces.end());
    check_sewn_keys(k.evaluate_history(sewing_history(80,reordered_sew)).back());
    check_sewn_keys(k.evaluate_history(sewing_history(90,sew)).back());
    for(double height:{80.,90.}) {
        const auto source=k.evaluate_history({{"wall",vertical_patch(height)}}).back();
        bool found_start=false;
        for(const auto& point:source.mesh.points)if(point.reference.owner_id=="wall"&&point.reference.semantic_key=="boundary:source:source0:vertex:from:a")
            {found_start=true;require(std::hypot(point.position.x-100,point.position.y,point.position.z)<1e-7,"Boundary start point moved to the opposite runtime face endpoint");}
        require(found_start,"Boundary source start point identity is missing");
    }
    auto fillet_history=sewing_history(80,sew);fillet_history.push_back({"shell-fillet",kernel::FilletRequest{{joint},5}});
    const auto shell_fillet=k.evaluate_history(fillet_history).back();
    require(shell_fillet.calculation_errors.empty()&&std::abs(shell_fillet.volume)<1e-9&&shell_fillet.surface_area>14000&&shell_fillet.surface_area<14400,
        "Fillet did not operate on a sewn surface shell");
    auto disconnected=rectangle();for(auto& boundary:disconnected.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());line.start.x+=200;line.end.x+=200;
    }
    bool disconnected_rejected=false;try{disconnected_rejected=!k.evaluate_history({{"floor",rectangle()},{"wall",disconnected},{"sewn",sew}}).back().calculation_errors.empty();}catch(const std::exception&){disconnected_rejected=true;}
    require(disconnected_rejected,"Sewing silently accepted disconnected surfaces");
    auto partial_wall=vertical_patch(80);for(auto& boundary:partial_wall.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());
        line.start.y=20+line.start.y/2;line.end.y=20+line.end.y/2;
    }
    const auto partial_sew=k.evaluate_history({{"floor",rectangle()},{"wall",partial_wall},{"sewn",sew}}).back();
    require(partial_sew.calculation_errors.empty()&&std::abs(partial_sew.surface_area-11200)<.01&&std::abs(partial_sew.volume)<1e-9,
        "Sewing did not join partially overlapping boundaries");
    require(std::ranges::count_if(partial_sew.mesh.original_references.edges,[](const auto& edge){return edge.reference.owner_id=="sewn";})==9,
        "Partial sewing lost native split-edge ancestry");
    auto solid=cylinder_source(50);solid.surface_result=false;solid.open_profile_end_id.clear();
    solid.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{200,0,0},{210,0,0},{210,10,0},{200,10,0}}};
    solid.outer_edge_source_ids={"a","b","c","d"};solid.outer_vertex_source_ids={"p0","p1","p2","p3"};
    auto with_solid=sewing_history(80,sew);with_solid.insert(with_solid.begin()+2,{"unaffected-solid",solid});
    const auto mixed_sew=k.evaluate_history(with_solid).back();
    require(mixed_sew.calculation_errors.empty()&&std::abs(mixed_sew.volume-3000)<1e-7&&std::abs(mixed_sew.surface_area-15800)<.01,
        "Sewing changed an unselected solid or its mass properties");
    auto mixed_trim=intersection_operations;auto mixed_trim_request=trim;
    mixed_trim.insert(mixed_trim.begin()+2,{"unaffected-solid",solid});mixed_trim.push_back({"mixed-trim",mixed_trim_request});
    const auto mixed_trimmed=k.evaluate_history(mixed_trim).back();
    require(mixed_trimmed.calculation_errors.empty()&&std::abs(mixed_trimmed.volume-3000)<1e-7&&std::abs(mixed_trimmed.surface_area-13400)<.02,
        "Trim changed an unselected solid or its mass properties");
    auto duplicate_sew=sew;duplicate_sew.faces[1]=duplicate_sew.faces[0];
    bool duplicate_rejected=false;try{duplicate_rejected=!k.evaluate_history(sewing_history(80,duplicate_sew)).back().calculation_errors.empty();}catch(const std::exception&){duplicate_rejected=true;}
    require(duplicate_rejected,"Sewing silently accepted a duplicate surface");
    const auto patch_from_points=[&](const std::array<kernel::Vec3,4>& points) {
        auto request=rectangle();for(int i=0;i<4;++i)
            request.boundaries[i].outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{points[i],points[(i+1)%4]}}};
        return request;
    };
    const std::array<std::array<kernel::Vec3,4>,6> box_patches{{
        {{{0,0,0},{100,0,0},{100,80,0},{0,80,0}}},
        {{{0,0,80},{100,0,80},{100,80,80},{0,80,80}}},
        {{{0,0,0},{100,0,0},{100,0,80},{0,0,80}}},
        {{{0,80,0},{100,80,0},{100,80,80},{0,80,80}}},
        {{{0,0,0},{0,80,0},{0,80,80},{0,0,80}}},
        {{{100,0,0},{100,80,0},{100,80,80},{100,0,80}}}
    }};
    std::vector<kernel::HistoryOperation> closed_history;kernel::SurfaceSewingRequest close_shell;
    for(int i=0;i<6;++i) {const auto owner="box-patch"+std::to_string(i);closed_history.push_back({owner,patch_from_points(box_patches[i])});
        close_shell.faces.push_back({owner,"surface:from:region",{}});}
    closed_history.push_back({"closed-shell",close_shell});const auto closed=k.evaluate_history(closed_history).back();
    require(closed.calculation_errors.empty()&&std::abs(closed.surface_area-44800)<.01&&std::abs(closed.volume)<1e-9,
        "Closed sewing shell acquired solid material or changed surface area");
    require(std::ranges::count_if(closed.mesh.original_references.edges,[](const auto& edge){return edge.reference.owner_id=="closed-shell";})==12,
        "Closed sewing shell lost native joined boundaries");
    auto gap_wall=vertical_patch(80);for(auto& boundary:gap_wall.boundaries) {
        auto& line=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(boundary.outer_profile).curves.front());line.start.x+=.01;line.end.x+=.01;
    }
    bool gap_rejected=false;try{gap_rejected=!k.evaluate_history({{"floor",rectangle()},{"wall",gap_wall},{"sewn",sew}}).back().calculation_errors.empty();}catch(const std::exception&){gap_rejected=true;}
    require(gap_rejected,"Sewing enlarged tolerance to close an excessive gap");
    auto nonmanifold=sew;nonmanifold.faces.push_back({"lower-wall","surface:from:region",{}});
    bool nonmanifold_rejected=false;try{nonmanifold_rejected=!k.evaluate_history({{"floor",rectangle()},{"wall",vertical_patch(80)},
        {"lower-wall",vertical_patch(-80)},{"sewn",nonmanifold}}).back().calculation_errors.empty();}catch(const std::exception&){nonmanifold_rejected=true;}
    require(nonmanifold_rejected,"Sewing accepted a non-manifold three-face join");
    const auto reverse_shell=k.evaluate_history(sewing_history(-80,sew)).back();kernel::EdgeReference reverse_joint;
    for(const auto& edge:reverse_shell.mesh.edges)if(edge.reference.owner_id=="sewn"&&!edge.points.empty()&&
        std::ranges::all_of(edge.points,[](const auto& point){return std::abs(point.x-100)<1e-7&&std::abs(point.z)<1e-7;}))reverse_joint=edge.reference;
    require(reverse_joint.valid(),"Opposite-sided shell lost its common edge");
    auto reverse_fillet_history=sewing_history(-80,sew);reverse_fillet_history.push_back({"reverse-fillet",kernel::FilletRequest{{reverse_joint},5}});
    const auto reverse_fillet=k.evaluate_history(reverse_fillet_history).back();
    require(reverse_fillet.calculation_errors.empty()&&std::abs(reverse_fillet.volume)<1e-9&&reverse_fillet.surface_area>14000&&reverse_fillet.surface_area<14400,
        "Opposite-sided shell Fillet failed or created solid material");
    auto excessive_fillet_history=sewing_history(80,sew);excessive_fillet_history.push_back({"excessive-fillet",kernel::FilletRequest{{joint},100}});
    bool radius_rejected=false;try{radius_rejected=!k.evaluate_history(excessive_fillet_history).back().calculation_errors.empty();}catch(const std::exception&){radius_rejected=true;}
    require(radius_rejected,"Shell Fillet silently accepted an excessive radius");
    const auto curved_wall=patch_from_points({{{50,0,0},{60,0,0},{60,0,30},{50,0,30}}});
    kernel::SurfaceSewingRequest curved_sew;curved_sew.faces={{"cylinder","generated:arc",{}},{"curved-wall","surface:from:region",{}}};
    const auto curved_shell=k.evaluate_history({{"cylinder",cylinder_source(50)},{"curved-wall",curved_wall},{"curved-shell",curved_sew}}).back();
    require(curved_shell.calculation_errors.empty()&&std::abs(curved_shell.surface_area-(750*std::acos(-1.)+300))<.01&&std::abs(curved_shell.volume)<1e-9,
        "Sewing changed a curved source surface or failed its native join");
    const auto polygon=[&](unsigned sides) {
        auto request=rectangle();request.boundaries.resize(sides);request.source_owners.resize(sides);
        const double turn=2*std::acos(-1.)/sides;
        for(unsigned i=0;i<sides;++i) {
            const kernel::Vec3 a{50*std::cos(turn*i),50*std::sin(turn*i),0};
            const kernel::Vec3 b{50*std::cos(turn*(i+1)),50*std::sin(turn*(i+1)),0};
            auto& input=request.boundaries[i];request.source_owners[i]="side-"+std::to_string(i);
            input.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{a,b}}};
            input.outer_edge_source_ids={"curve"};input.outer_vertex_source_ids={"a"};input.open_profile_end_id="b";
            if(i%2) {
                std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(input.outer_profile).curves[0])={b,a};
                input.outer_vertex_source_ids={"b"};input.open_profile_end_id="a";
            }
        }
        return request;
    };
    for(unsigned sides:{3u,5u,17u}) {
        const auto request=polygon(sides);const auto result=calculate(request);
        const double expected=sides*1250*std::sin(2*std::acos(-1.)/sides);
        require(result.calculation_errors.empty()&&std::abs(result.surface_area-expected)<.01&&std::abs(result.volume)<1e-9,
            "N-sided planar surface area or classification failed");
        require(result.mesh.original_references.edges.size()==sides&&result.mesh.original_references.points.size()==sides,
            "N-sided surface lost source edges or corners");
    }
    auto lens=rectangle();lens.boundaries.resize(2);lens.source_owners.resize(2);
    lens.boundaries[0].outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{0,0,0},{50,-50,0},{100,0,0}}}};
    lens.boundaries[1].outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{100,0,0},{50,50,0},{0,0,0}}}};
    const auto lens_result=calculate(lens);
    require(lens_result.calculation_errors.empty()&&std::abs(lens_result.surface_area-2500*std::acos(-1.))<.01,
        "Two-arc closed surface failed");
    require(lens_result.mesh.original_references.edges.size()==2&&lens_result.mesh.original_references.points.size()==2,
        "Two-boundary surface lost ancestry");
    auto malformed=polygon(3);malformed.source_owners.pop_back();
    static_cast<void>(kernel::history_fingerprint({{"boundary",malformed}},1));
    bool malformed_rejected=false;try{malformed_rejected=!calculate(malformed).calculation_errors.empty();}catch(const std::exception&){malformed_rejected=true;}
    require(malformed_rejected,"Mismatched boundary packet was accepted");
    const auto verify_cut_points=[](const kernel::BodyResult& result,const std::string& owner) {
        std::size_t count=0;
        for(const auto& point:result.mesh.points) {
            if(point.reference.owner_id!=owner||!point.reference.semantic_key.starts_with("boolean:subtract:vertex"))continue;
            ++count;
            const auto found=std::ranges::find_if(result.mesh.original_references.points,
                [&](const auto& reference){return reference.reference==point.reference;});
            require(found!=result.mesh.original_references.points.end(),"Cut endpoint missing from persisted reference geometry");
            require(std::hypot(found->position.x-point.position.x,found->position.y-point.position.y,found->position.z-point.position.z)<1e-9,
                "Cut endpoint reference has the wrong position");
            const auto candidates=viewer::ordered_viewer_candidates(result.mesh,
                {point.position.x,point.position.y,point.position.z+1000},{0,0,-1},1e-5);
            require(std::ranges::any_of(candidates,[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::Vertex&&
                candidate.owner_id==point.reference.owner_id&&candidate.semantic_key==point.reference.semantic_key&&
                candidate.geometry==viewer::CandidateGeometry::OriginalReference;}),"Common picker omitted a persisted cut endpoint");
        }
        require(count>=2,"Cut produced no independently identified endpoints");
    };
    kernel::ExtrusionRequest tool;
    tool.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{40,-10,-10},{60,-10,-10},{60,90,-10},{40,90,-10}}};
    tool.direction={0,0,20};tool.profile_region_id="cut-region";
    tool.outer_edge_source_ids={"e0","e1","e2","e3"};tool.outer_vertex_source_ids={"p0","p1","p2","p3"};
    const auto cut=k.evaluate_history({{"boundary",rectangle()},{"cut",tool,kernel::BooleanOperation::Subtract}});
    require(cut.back().calculation_errors.empty()&&std::abs(cut.back().surface_area-6400)<.01,"Boundary strip cut failed");
    verify_cut_points(cut.back(),"cut");
    const auto original_fill=k.evaluate_history({{"boundary",rectangle()},{"cut",tool,kernel::BooleanOperation::Subtract},{"fill",constrained(kernel::SurfaceContinuity::G1)}});
    require(original_fill.back().calculation_errors.empty()&&std::abs(original_fill.back().surface_area-14400)<.01,
        "Later filling substituted trimmed result edges for original boundaries");
    auto wider=tool;std::get<kernel::ExtrusionRequest::PolygonProfile>(wider.outer_profile).vertices[1].x=65;
    std::get<kernel::ExtrusionRequest::PolygonProfile>(wider.outer_profile).vertices[2].x=65;
    const auto changed_cut=k.evaluate_history({{"boundary",rectangle()},{"cut",wider,kernel::BooleanOperation::Subtract}});
    verify_cut_points(changed_cut.back(),"cut");
    for(const auto& point:cut.back().mesh.points)if(point.reference.owner_id=="cut")
        require(std::ranges::any_of(changed_cut.back().mesh.points,[&](const auto& changed){return changed.reference==point.reference;}),
            "Changing cut width changed endpoint ancestry");
    if(const auto* file=std::getenv("ZIMA_VERIFY_BOUNDARY_SOURCE")) {
        const auto example=document::PartDocument::load(std::filesystem::path(file));
        const auto operations=example.kernel_operations();
        const auto calculated=k.evaluate_history(operations);
        require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"User cut example failed calculation");
        verify_cut_points(calculated.back(),operations.back().owner_id);
        std::size_t curves=0;
        for(const auto& edge:calculated.back().mesh.original_references.edges) {
            if(edge.reference.owner_id!=operations.back().owner_id||!edge.reference.semantic_key.starts_with("boolean:subtract:"))continue;
            require(edge.exact_spline.has_value(),"Cut curve lacks exact geometry");
            ++curves;
            const auto& spline=*edge.exact_spline;
            const auto p=kernel::bspline_value(spline,.5);
            document::Placement placed;placed.x=p.x;placed.y=p.y;placed.z=p.z;
            placed.references={{{},edge.reference.owner_id,edge.reference.semantic_key}};
            require(document::resolve_placement(placed,calculated.back().mesh.original_references),"Cut curve placement failed");
            require(std::hypot(placed.x-p.x,placed.y-p.y,placed.z-p.z)<1e-6,"Cut curve forced placement to its endpoint");
        }
        require(curves>0,"User cut has no intersection curves");
        const auto copy=std::filesystem::temp_directory_path()/"zima-boundary-cut-reference-test.prtz";
        example.save(copy,calculated);
        std::vector<kernel::BodyResult> restored;
        static_cast<void>(document::PartDocument::load(copy,&restored));
        require(!restored.empty(),"User cut cache was not restored");
        verify_cut_points(restored.back(),operations.back().owner_id);
        std::filesystem::remove(copy);
        std::cout<<"User cut endpoints: calculated and native save/reopen passed\n";
    }
    const auto first_corner=std::ranges::find_if(flat.mesh.original_references.points,[](const auto& p){return p.reference.semantic_key=="boundary:source:source0:vertex:from:a";});
    require(first_corner!=flat.mesh.original_references.points.end()&&std::hypot(first_corner->position.x,first_corner->position.y)<1e-9,"Source corner identity was attached to the wrong endpoint");
    auto cycled=rectangle();std::rotate(cycled.boundaries.begin(),cycled.boundaries.begin()+1,cycled.boundaries.end());
    std::rotate(cycled.source_owners.begin(),cycled.source_owners.begin()+1,cycled.source_owners.end());
    const auto cycled_result=calculate(cycled);
    for(const auto& corner:flat.mesh.original_references.points) {
        const auto found=std::ranges::find_if(cycled_result.mesh.original_references.points,[&](const auto& p){return p.reference.semantic_key==corner.reference.semantic_key;});
        require(found!=cycled_result.mesh.original_references.points.end()&&std::hypot(found->position.x-corner.position.x,found->position.y-corner.position.y)<1e-9,"Changing the first boundary changed corner identity");
    }
    auto warped=calculate(rectangle(20));require(warped.calculation_errors.empty()&&warped.surface_area>8000,"Spatial patch failed");
    std::get<kernel::ExtrusionRequest::CurvedProfile>(r.boundaries[0].outer_profile).curves[0]=kernel::ExtrusionRequest::ArcCurve{{0,0,0},{50,-15,10},{100,0,0}};
    const auto curved=calculate(r);require(curved.calculation_errors.empty()&&curved.surface_area>8000,"Exact arc boundary failed");
    kernel::ExtrusionRequest::BSplineCurve rational;
    rational.start={0,0,0};rational.end={100,0,0};
    rational.control_points={{0,0,0},{20,-20,15},{80,-20,15},{100,0,0}};rational.weights={1,2,2,1};
    std::get<kernel::ExtrusionRequest::CurvedProfile>(r.boundaries[0].outer_profile).curves[0]=rational;
    const auto weighted=calculate(r);require(weighted.calculation_errors.empty()&&weighted.surface_area>8000,"Rational B-spline boundary failed");
    auto broken=rectangle();std::get<kernel::ExtrusionRequest::CurvedProfile>(broken.boundaries[0].outer_profile).curves[0]=kernel::ExtrusionRequest::LineCurve{{5,0,0},{100,0,0}};
    bool rejected=false;
    try { rejected=!calculate(broken).calculation_errors.empty(); } catch(const std::exception&) { rejected=true; }
    require(rejected,"Open perimeter accepted");
    auto reversed=rectangle();
    auto& reverse_curve=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(reversed.boundaries[2].outer_profile).curves[0]);
    std::swap(reverse_curve.start,reverse_curve.end);
    require(std::abs(calculate(reversed).surface_area-8000)<.01,"Automatic chain reversal changed the patch");
    auto crossing=rectangle();const std::array<kernel::Vec3,4> crossed{{{0,0,0},{100,80,0},{0,80,0},{100,0,0}}};
    for(int i=0;i<4;++i)std::get<kernel::ExtrusionRequest::CurvedProfile>(crossing.boundaries[i].outer_profile).curves[0]=kernel::ExtrusionRequest::LineCurve{crossed[i],crossed[(i+1)%4]};
    rejected=false;try{rejected=!calculate(crossing).calculation_errors.empty();}catch(const std::exception&){rejected=true;}
    require(rejected,"Crossing boundaries were accepted");
    auto part=document::PartDocument::create_default();part.history.clear();part.sketches.clear();part.history_order.clear();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Boundary"));
    auto feature=document::create_boundary_surface();
    auto sketch=sketcher::Sketch::create_default();auto container=document::PartDocument::create_sketch_container();sketch.owner_container_id=container.id;
    const std::array<std::array<double,4>,4> lines{{{0,0,100,0},{100,0,100,80},{100,80,0,80},{0,80,0,0}}};
    for(unsigned i=0;i<4;++i)feature.boundary_surface.boundaries[i]={container.id,sketch.add_segment(lines[i][0],lines[i][1],lines[i][2],lines[i][3])};
    part.history={container,feature};part.sketches={sketch};graph.insert({document::PartHistoryKind::Feature,container.id});graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    const auto native_operations=part.kernel_operations();
    const auto output=k.evaluate_history(native_operations);require(!output.empty()&&output.back().calculation_errors.empty(),"Native Sketch boundary failed");
    auto sewing_part=part;auto wall_source=document::PartDocument::create_sketch_container();auto wall_feature=document::create_boundary_surface();
    auto wall_sketch=sketcher::Sketch::create_default();wall_sketch.owner_container_id=wall_source.id;wall_sketch.plane=sketcher::SketchPlane::YZ;
    wall_sketch.plane_auto=false;wall_sketch.plane_offset=100;wall_source.feature_kind=document::FeatureKind::Feature;
    wall_source.feature.type=document::FeatureType::Sketch;wall_source.feature.sketch_id=wall_sketch.id;
    const std::array<std::array<double,4>,4> wall_lines{{{0,0,80,0},{80,0,80,80},{80,80,0,80},{0,80,0,0}}};
    for(int i=0;i<4;++i)wall_feature.boundary_surface.boundaries[i]={wall_source.id,wall_sketch.add_segment(wall_lines[i][0],wall_lines[i][1],wall_lines[i][2],wall_lines[i][3])};
    for(const auto& element:{wall_source,wall_feature}) {sewing_part.insert_history_entry(document::PartHistoryKind::Feature,element.id);sewing_part.history.push_back(element);}
    sewing_part.sketches.push_back(wall_sketch);sewing_part.resolve_constructions();
    const auto sewing_inputs=k.evaluate_history(sewing_part.kernel_operations());
    require(sewing_inputs.back().calculation_errors.empty()&&std::abs(sewing_inputs.back().surface_area-14400)<.01,"Native sewing source frame failed");
    auto sewing_feature=document::create_surface_sewing();sewing_feature.surface_sewing.faces={{feature.id,"surface:from:"+feature.feature_id,{}},
        {wall_feature.id,"surface:from:"+wall_feature.feature_id,{}}};
    workspace::Workspace sewing_live;sewing_live.add_part(sewing_part,sewing_inputs);sewing_live.activate(sewing_part.document_id);
    require(workspace::commit_surface_sewing(sewing_live,k,sewing_part.document_id,sewing_feature),"Native sewing did not commit");
    require(!workspace::commit_surface_sewing(sewing_live,k,sewing_part.document_id,sewing_feature),"Unchanged sewing OK created a transaction");
    const auto& sewing_session=sewing_live.open_part(sewing_part.document_id)->session;
    auto sewing_copy=document::PartDocument::from_serialized(sewing_session.document().serialized());
    require(sewing_copy.find_container(sewing_feature.id)->surface_sewing==sewing_feature.surface_sewing,"Native sewing references did not round trip");
    const auto sewing_dependencies=workspace::part_history_dependencies(sewing_copy);
    require(sewing_dependencies.contains({feature.id,sewing_feature.id})&&sewing_dependencies.contains({wall_feature.id,sewing_feature.id}),
        "Sewing lost its prerequisite dependencies");
    const auto sewing_file=std::filesystem::temp_directory_path()/"zima-surface-sewing-test.prtz";
    sewing_copy.save(sewing_file,sewing_session.calculated_boundaries());std::vector<kernel::BodyResult> sewing_cache;
    const auto sewing_reopened=document::PartDocument::load(sewing_file,&sewing_cache);std::filesystem::remove(sewing_file);
    kernel::OcctKernel sewing_cold;
    const auto sewing_rebuilt=sewing_cold.evaluate_history_incremental(sewing_reopened.kernel_operations(),sewing_cache);
    require(sewing_rebuilt.back().calculation_errors.empty()&&std::abs(sewing_rebuilt.back().surface_area-14400)<.01&&
        std::abs(sewing_rebuilt.back().volume)<1e-9,"Cold native sewing lost its shell geometry");
    auto missing_sewing=sewing_feature;missing_sewing.surface_sewing.faces[0].semantic_key="missing";
    const auto sewing_before=sewing_session.document().serialized();bool sewing_edit_rejected=false;
    try{static_cast<void>(workspace::commit_surface_sewing(sewing_live,k,sewing_part.document_id,missing_sewing));}catch(const std::exception&){sewing_edit_rejected=true;}
    require(sewing_edit_rejected&&sewing_session.document().serialized()==sewing_before,"Failed sewing edit committed partial state");
    require(workspace::step_part_document_history(sewing_live,sewing_part.document_id,false)&&!sewing_session.document().find_container(sewing_feature.id),"Sewing Undo failed");
    require(workspace::step_part_document_history(sewing_live,sewing_part.document_id,true)&&sewing_session.document().find_container(sewing_feature.id)->surface_sewing==sewing_feature.surface_sewing,
        "Sewing Redo lost references");
    const auto complete_sewing=sewing_session.document().serialized();
    require(workspace::set_part_history_suppressed(sewing_live,sewing_part.document_id,k,feature.id,true)&&
        sewing_session.document().find_container(sewing_feature.id)->suppressed&&!sewing_session.document().find_container(wall_feature.id)->suppressed,
        "Surface suppression omitted Sewing or suppressed its independent source");
    require(workspace::set_part_history_suppressed(sewing_live,sewing_part.document_id,k,sewing_feature.id,false)&&
        sewing_session.document().serialized()==complete_sewing&&sewing_session.calculated_boundaries().back().calculation_errors.empty(),
        "Sewing restoration omitted its preceding prerequisites");
    workspace::delete_part_history(sewing_live,sewing_part.document_id,k,feature.id);
    const auto incomplete_sewing=document::PartDocument::from_serialized(sewing_session.document().serialized());
    require(incomplete_sewing.find_container(sewing_feature.id)&&incomplete_sewing.find_container(sewing_feature.id)->surface_sewing.faces.size()==1&&
        incomplete_sewing.removed_reference_states.contains(sewing_feature.id),"Deleting a Sewing source lost the retained unresolved feature");
    require(workspace::step_part_document_history(sewing_live,sewing_part.document_id,false)&&sewing_session.document().serialized()==complete_sewing,
        "Deleting a Sewing source could not undo its reference removal");
    auto current=part;auto* current_source=current.find_container(container.id);
    current_source->feature_kind=document::FeatureKind::Feature;
    current_source->feature.type=document::FeatureType::Sketch;
    current_source->feature.sketch_id=sketch.id;
    require(document::boundary_surface_source_allowed(current,feature.id,{container.id,{}}),"Current Sketch Feature was rejected as a whole boundary");
    const auto current_output=k.evaluate_history(current.kernel_operations());
    require(current_output.back().calculation_errors.empty()&&std::abs(current_output.back().surface_area-output.back().surface_area)<1e-7,
        "Current Sketch Feature boundaries changed the calculated surface");
    current_source->feature.type=document::FeatureType::Modeling;
    require(!document::boundary_surface_source_allowed(current,feature.id,{container.id,{}}),"Solid modeling Feature was offered as a Sketch boundary");
    const auto saved=document::PartDocument::from_serialized(part.serialized());
    require(saved.find_container(feature.id)->boundary_surface==feature.boundary_surface,"Boundary persistence failed");
    auto supported=part;auto fill=document::create_boundary_surface();
    for(unsigned i=0;i<4;++i) {
        auto& source=fill.boundary_surface.boundaries[i];source.owner_id=feature.id;
        source.curve_id="boundary:source:"+container.id+":edge:from:"+feature.boundary_surface.boundaries[i].curve_id;
        source.kind=document::BoundaryCurveSource::Kind::Edge;source.continuity=kernel::SurfaceContinuity::G2;
        source.support=kernel::FaceReference{feature.id,"surface:from:"+feature.feature_id,{}};source.support_reversed=i%2;
    }
    supported.insert_history_entry(document::PartHistoryKind::Feature,fill.id);supported.history.push_back(fill);
    const auto supported_output=k.evaluate_history(supported.kernel_operations());
    require(supported_output.back().calculation_errors.empty()&&std::abs(supported_output.back().surface_area-16000)<.01,
        "Native original-edge G2 filling failed");
    const auto supported_copy=document::PartDocument::from_serialized(supported.serialized());
    require(supported_copy.find_container(fill.id)->boundary_surface==fill.boundary_surface,"Native support, continuity or side did not round trip");
    const auto support_file=std::filesystem::temp_directory_path()/"zima-supported-fill-test.prtz";
    supported.save(support_file,supported_output);std::vector<kernel::BodyResult> supported_cache;
    auto supported_reopened=document::PartDocument::load(support_file,&supported_cache);std::filesystem::remove(support_file);
    kernel::OcctKernel cold_kernel;const auto cold_output=cold_kernel.evaluate_history_incremental(supported_reopened.kernel_operations(),supported_cache);
    require(cold_output.back().calculation_errors.empty()&&std::abs(cold_output.back().surface_area-16000)<.01&&
        supported_reopened.find_container(fill.id)->boundary_surface==fill.boundary_surface,"Cold native fill changed supported topology or side");
    const auto supported_dependencies=workspace::part_history_dependencies(supported);
    require(supported_dependencies.contains({feature.id,fill.id}),"Supported fill lost prerequisite dependency");
    workspace::Workspace supported_live;supported_live.add_part(part,output);supported_live.activate(part.document_id);
    require(workspace::commit_boundary_surface(supported_live,k,part.document_id,fill),"Supported fill transaction did not commit");
    require(!workspace::commit_boundary_surface(supported_live,k,part.document_id,fill),"Unchanged supported OK created a transaction");
    const auto supported_before=supported_live.open_part(part.document_id)->session.document().serialized();
    auto broken_supported=fill;broken_supported.boundary_surface.boundaries[0].support->semantic_key="missing";
    bool support_edit_rejected=false;try{static_cast<void>(workspace::commit_boundary_surface(supported_live,k,part.document_id,broken_supported));}catch(const std::exception&){support_edit_rejected=true;}
    require(support_edit_rejected&&supported_live.open_part(part.document_id)->session.document().serialized()==supported_before,
        "Failed support edit committed partial geometry or references");
    require(workspace::step_part_document_history(supported_live,part.document_id,false)&&
        !supported_live.open_part(part.document_id)->session.document().find_container(fill.id),"Supported fill Undo failed");
    require(workspace::step_part_document_history(supported_live,part.document_id,true)&&
        supported_live.open_part(part.document_id)->session.document().find_container(fill.id)->boundary_surface==fill.boundary_surface,
        "Supported fill Redo lost native support or side");
    auto triangular=part;auto& triangle_sketch=triangular.sketches.front();
    triangle_sketch.segments.clear();triangle_sketch.points.clear();
    auto& triangle_definition=triangular.find_container(feature.id)->boundary_surface;triangle_definition.boundaries.resize(3);
    for(unsigned i=0;i<3;++i) {
        const std::array<std::array<double,4>,3> edges{{{0,0,100,0},{100,0,0,80},{0,80,0,0}}};
        triangle_definition.boundaries[i]={container.id,triangle_sketch.add_segment(edges[i][0],edges[i][1],edges[i][2],edges[i][3])};
    }
    const auto triangle_output=k.evaluate_history(triangular.kernel_operations());
    require(triangle_output.back().calculation_errors.empty()&&std::abs(triangle_output.back().surface_area-4000)<.01,
        "Three-boundary native definition failed calculation");
    const auto triangle_copy=document::PartDocument::from_serialized(triangular.serialized());
    require(triangle_copy.find_container(feature.id)->boundary_surface==triangle_definition,
        "Native serializer lost variable boundary count");
    const auto triangle_file=std::filesystem::temp_directory_path()/"zima-triangle-surface-test.prtz";
    triangular.save(triangle_file,triangle_output);std::vector<kernel::BodyResult> triangle_cache;
    const auto triangle_reopened=document::PartDocument::load(triangle_file,&triangle_cache);std::filesystem::remove(triangle_file);
    require(triangle_reopened.find_container(feature.id)->boundary_surface==triangle_definition&&!triangle_cache.empty()&&
        std::abs(triangle_cache.back().surface_area-4000)<.01,"Native file lost triangle definition or calculated result");
    require(kernel::history_fingerprint(triangular.kernel_operations(),triangular.kernel_operations().size())!=
        kernel::history_fingerprint(part.kernel_operations(),part.kernel_operations().size()),"Boundary count did not invalidate calculation identity");
    auto changed=part;changed.sketches.front().points.front().x+=2;
    require(kernel::history_fingerprint(changed.kernel_operations(),changed.kernel_operations().size())!=kernel::history_fingerprint(part.kernel_operations(),part.kernel_operations().size()),"Source edit did not invalidate surface cache");
    const auto temporary=std::filesystem::temp_directory_path()/"zima-boundary-surface-test.prtz";
    part.save(temporary,output);std::vector<kernel::BodyResult> persisted;
    const auto reopened=document::PartDocument::load(temporary,&persisted);std::filesystem::remove(temporary);
    require(reopened.find_container(feature.id)->boundary_surface==feature.boundary_surface&&!persisted.empty(),"Native file lost definition or calculated geometry");
    require(std::abs(persisted.back().surface_area-8000)<.01,"Native file lost surface area");
    const auto dependencies=workspace::part_history_dependencies(part);
    require(!dependencies.empty(),"Boundary source dependency missing");
    auto input=part;input.history.pop_back();document::BodyHistoryGraph input_graph;
    static_cast<void>(input_graph.create_body("Boundary"));input_graph.insert({document::PartHistoryKind::Feature,container.id});input.set_body_history(input_graph);
    workspace::Workspace live;live.add_part(input,workspace::calculate_part_with_resolved_references(k,input));live.activate(input.document_id);
    require(workspace::commit_boundary_surface(live,k,input.document_id,feature),"Boundary transaction did not commit");
    require(!workspace::commit_boundary_surface(live,k,input.document_id,feature),"Unchanged OK created a transaction");
    require(workspace::step_part_document_history(live,input.document_id,false),"Surface Undo failed");
    require(!live.open_part(input.document_id)->session.document().find_container(feature.id),"Undo retained surface");
    require(workspace::step_part_document_history(live,input.document_id,true),"Surface Redo failed");
    require(live.open_part(input.document_id)->session.document().find_container(feature.id),"Redo lost surface");
    const auto before=live.open_part(input.document_id)->session.document().serialized();auto invalid=feature;invalid.boundary_surface.boundaries[3]={};
    rejected=false;try{static_cast<void>(workspace::commit_boundary_surface(live,k,input.document_id,invalid));}catch(const std::exception&){rejected=true;}
    require(rejected&&before==live.open_part(input.document_id)->session.document().serialized(),"Invalid edit changed the document");
    auto spatial=document::PartDocument::create_default();spatial.history.clear();spatial.sketches.clear();spatial.constructions.clear();
    document::BodyHistoryGraph curve_graph;static_cast<void>(curve_graph.create_body("3D boundaries"));
    auto patch=document::create_boundary_surface();
    const std::array<kernel::Vec3,4> corners{{{0,0,0},{100,0,0},{100,80,20},{0,80,0}}};
    for(int i=0;i<4;++i) {
        auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
        curve.curve_points.clear();curve.curve_type=i==0?document::Curve3DType::InterpolatingSpline:document::Curve3DType::Polyline;
        for(const auto p:{corners[i],corners[(i+1)%4]}){auto point=document::create_owned_point(curve.id);point.origin=p;curve.curve_points.push_back(point);}
        if(i==0){auto middle=document::create_owned_point(curve.id);middle.origin={50,-15,10};curve.curve_points.insert(curve.curve_points.begin()+1,middle);}
        patch.boundary_surface.boundaries[i]={curve.id,{}};curve_graph.insert({document::PartHistoryKind::Construction,curve.id});spatial.constructions.push_back(curve);
    }
    curve_graph.insert({document::PartHistoryKind::Feature,patch.id});spatial.history={patch};spatial.set_body_history(curve_graph);spatial.resolve_constructions();
    const auto spatial_result=k.evaluate_history(spatial.kernel_operations());
    require(spatial_result.back().calculation_errors.empty()&&spatial_result.back().surface_area>8000,"3D Curve boundary surface failed");
    require(document::PartDocument::from_serialized(spatial.serialized()).find_container(patch.id)->boundary_surface==patch.boundary_surface,"3D curve references did not round trip");
    const auto original_start=document::boundary_surface_request(spatial,patch).boundaries[0].outer_vertex_source_ids.front();
    auto refined=spatial;auto& first_curve=refined.constructions.front();auto inserted=document::create_owned_point(first_curve.id);
    inserted.origin={25,-10,5};first_curve.curve_points.insert(first_curve.curve_points.begin()+1,inserted);
    require(document::boundary_surface_request(refined,patch).boundaries[0].outer_vertex_source_ids.front()==original_start,"Inserting a 3D Curve point changed its unchanged endpoint identity");
    if(const auto* example=std::getenv("ZIMA_BOUNDARY_EXAMPLE"))spatial.save(std::filesystem::path(example),spatial_result);
    auto owned_input=document::PartDocument::create_default();
    document::BodyHistoryGraph owned_graph;static_cast<void>(owned_graph.create_body("Owned surface"));owned_input.set_body_history(owned_graph);
    auto owned=document::create_general_surface();
    const std::array<kernel::Vec3,4> owned_corners{{{0,0,0},{100,0,0},{100,80,0},{0,80,0}}};
    for(unsigned i=0;i<3;++i) {
        auto sketch=sketcher::Sketch::from_serialized(owned.general_surface.boundaries[i].sketch_serialized);
        auto& placement=owned.general_surface.boundaries[i].sketch_feature->placement;
        if(i==0){placement.x=13;static_cast<void>(sketch.add_segment(-13,0,87,0));}
        else {placement.x=100;placement.y=i==2?80:0;
            placement.rotation_z=placement.absolute_rotation_z=i==2?180:90;
            static_cast<void>(sketch.add_segment(0,0,i==2?100:80,0));}
        owned.general_surface.boundaries[i].sketch_serialized=sketch.serialized();
    }
    owned.general_surface.boundaries[3]=document::create_general_surface_curve(owned);
    auto& closing=*owned.general_surface.boundaries[3].curve;
    closing.curve_points.clear();
    for(const auto p:{owned_corners[3],owned_corners[0]}) {
        auto point=document::create_owned_point(closing.id);point.origin=p;closing.curve_points.push_back(std::move(point));
    }
    owned.placement.x=20;owned.placement.y=-30;owned.placement.z=40;
    owned.placement.rotation_x=25;owned.placement.rotation_y=35;owned.placement.rotation_z=55;
    owned.placement.absolute_rotation_x=25;owned.placement.absolute_rotation_y=35;owned.placement.absolute_rotation_z=55;
    workspace::Workspace owned_live;owned_live.add_part(owned_input);owned_live.activate(owned_input.document_id);
    require(workspace::commit_general_surface(owned_live,k,owned_input.document_id,owned),"Owned general surface did not commit");
    auto* owned_state=owned_live.open_part(owned_input.document_id);
    const auto stored_owned=*owned_state->session.document().find_container(owned.id);
    {
        auto presentation=owned_state->session.document();
        const auto hidden=app::hidden_surface_wires(presentation);
        require(hidden.size()==4&&hidden.contains(closing.id),"Committed Surface does not hide all owned definitions");
        require(app::surface_wire_owner(presentation,closing.entity_id)==closing.id,"Curve wire visibility uses a transient packet identity");
        const auto owners=app::surface_wire_packet_owners(presentation,hidden);
        require(owners.contains(closing.entity_id)&&owners.contains(closing.curve_points.front().id),"Hidden Curve retains visible entity/Point aliases");
        auto display=document::general_surface_definition_mesh(stored_owned);
        const auto references=display.original_references;
        app::filter_surface_wires(display,owners);
        require(display.edges.empty()&&display.points.empty()&&display.original_references.edges.size()==references.edges.size()&&
            display.original_references.points.size()==references.points.size(),"Hiding Surface definitions removed their native references or retained visible geometry");
        presentation.surface_wire_visibility[closing.id]=true;
        presentation.surface_wire_visibility[stored_owned.general_surface.boundaries[0].sketch_feature->feature.sketch_id]=false;
        require(!app::hidden_surface_wires(presentation).contains(closing.id),"Manual visibility cannot show an owned Curve");
        const auto restored=document::PartDocument::from_serialized(presentation.serialized());
        require(restored.surface_wire_visibility==presentation.surface_wire_visibility&&
            !app::hidden_surface_wires(restored).contains(closing.id),"Native Surface wire visibility did not survive serialization");
        const auto visibility_file=std::filesystem::temp_directory_path()/"zima-native-surface-visibility-test.prtz";
        presentation.save(visibility_file,owned_state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> visibility_cache;
        const auto file_restored=document::PartDocument::load(visibility_file,&visibility_cache);std::filesystem::remove(visibility_file);
        require(file_restored.surface_wire_visibility==presentation.surface_wire_visibility&&!visibility_cache.empty()&&
            visibility_cache.back().source_fingerprint==owned_state->session.calculated_boundaries().back().source_fingerprint,
            "Native Part file lost wire presentation overrides or calculated geometry");
        require(kernel::history_fingerprint(presentation.kernel_operations(),presentation.kernel_operations().size())==
            kernel::history_fingerprint(owned_state->session.document().kernel_operations(),owned_state->session.document().kernel_operations().size()),
            "Presentation-only Surface visibility invalidates geometry calculation");
    }
    require(stored_owned.general_surface.boundaries[0].sketch_feature->placement.x==13&&
        stored_owned.general_surface.boundaries[1].sketch_feature->placement.absolute_rotation_z==90&&
        stored_owned.general_surface.boundaries[2].sketch_feature->placement.absolute_rotation_z==180,
        "Owned Sketch features lost independent parent-local placement");
    const auto& owned_result=owned_state->session.calculated_boundaries().back();
    require(owned_result.calculation_errors.empty()&&std::abs(owned_result.surface_area-8000)<.01&&std::abs(owned_result.volume)<1e-9,
        "Mixed owned Sketch/3D Curve surface lost area or became solid");
    require(owned_state->session.document().constructions.empty()&&owned_state->session.document().sketches.size()==owned_input.sketches.size(),
        "General surface leaked owned geometry to document root");
    const auto owned_request=document::general_surface_request(owned_state->session.document(),stored_owned);
    require(owned_request.source_owners[0]==sketcher::Sketch::from_serialized(stored_owned.general_surface.boundaries[0].sketch_serialized).id&&
        owned_request.source_owners[3]==closing.id,"Owned boundary identity was inferred from row order");
    const auto& start=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(owned_request.boundaries[0].outer_profile).curves.front()).start;
    require(std::hypot(start.x-20,start.y+30,start.z-40)<1e-8,"Owned boundary did not follow complete parent placement");
    const auto owner_body=owned_state->session.document().body_owner_for_object(closing.curve_points.front().id);
    require(owner_body&&owner_body==owned_state->session.document().body_owner_for_object(owned.id),"Owned 3D Point lost Body ownership");
    require(owner_body==owned_state->session.document().body_owner_for_object(stored_owned.general_surface.boundaries[0].sketch_feature->id),
        "Owned Sketch Feature lost its Surface Body ownership");
    {
        auto displayed=owned_state->session.document();
        auto& surface=*displayed.find_container(owned.id);
        auto& own=*surface.general_surface.boundaries[0].sketch_feature;
        own.feature.show_point=true;own.feature.show_text=true;
        const auto mesh=displayed.construction_viewer_mesh();
        const auto marker=std::ranges::find_if(mesh.points,[&](const auto& point){return point.reference.owner_id==own.id&&point.reference.semantic_key=="point";});
        const auto expected=document::general_surface_display_sketch(surface,surface.general_surface.boundaries[0]).resolved_origin;
        require(marker!=mesh.points.end()&&marker->always_visible&&marker->label==own.name&&
            std::hypot(marker->position.x-expected.x,marker->position.y-expected.y,marker->position.z-expected.z)<1e-7,
            "Owned Sketch Feature point/text display escaped its parent placement");
    }
    const auto point_frames=document::part_annotation_frames(owned_state->session.document());
    const auto point_frame=point_frames.at({closing.curve_points.front().id,{}});
    const auto& curve_start=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(owned_request.boundaries[3].outer_profile).curves.front()).start;
    require(std::hypot(point_frame.origin.x-curve_start.x,point_frame.origin.y-curve_start.y,point_frame.origin.z-curve_start.z)<1e-8,
        "Owned Point annotation frame disagrees with the exact placed surface boundary");
    const auto owned_before=owned_state->session.document().serialized();
    require(!workspace::commit_general_surface(owned_live,k,owned_input.document_id,stored_owned)&&owned_state->session.document().serialized()==owned_before,
        "Unchanged owned surface confirmation created a transaction");
    const auto owned_copy=document::PartDocument::from_serialized(owned_before);
    require(owned_copy.find_container(owned.id)->general_surface==stored_owned.general_surface,"Owned geometry did not round trip native data");
    {
        auto projection=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Projection"));
        auto root=document::PartDocument::create_sketch_container();
        auto parent=stored_owned;
        graph.insert({document::PartHistoryKind::Feature,root.id});
        graph.insert({document::PartHistoryKind::Feature,parent.id});
        auto& boundary=parent.general_surface.boundaries[0];
        auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
        sketcher::SketchExternalReference reference;
        reference.id="owned-external-point";reference.kind=sketcher::ExternalReferenceKind::Point;
        reference.source_document_id=projection.document_id;reference.source_owner_id=root.id;
        reference.source_semantic_key="anchor";reference.cached_points={{0,0}};
        sketch.add_external_reference(reference);boundary.sketch_serialized=sketch.serialized();
        projection.history={root,parent};projection.set_body_history(graph);
        kernel::BodyResult source;const kernel::Vec3 fixed{100,200,300};
        source.mesh.original_references.points.push_back({fixed,{root.id,"anchor",{}}});
        for(double shift:{0.,50.}) {
            auto& surface=projection.history.back();surface.placement.x=20+shift;
            static_cast<void>(workspace::refresh_sketch_external_references(projection,{source}));
            const auto actual=document::general_surface_display_sketch(surface,surface.general_surface.boundaries[0]);
            require(projection.reference_errors.empty()&&!actual.external_references.empty()&&!actual.external_references.front().cached_points.empty(),
                "Owned external point refresh has no valid projected point");
            const auto expected=actual.local_point(fixed);
            const auto& cached=actual.external_references.front().cached_points.front();
            require(projection.reference_errors.empty()&&!actual.external_references.front().broken&&
                std::hypot(cached[0]-expected[0],cached[1]-expected[1])<1e-7,
                "Owned external projection did not compose Sketch and Surface frames");
            require(!workspace::refresh_sketch_external_references(projection,{source}),
                "Unchanged owned external projection drifted or caused repeated refresh");
        }
    }
    {
        auto referenced=stored_owned;
        auto& boundary=referenced.general_surface.boundaries[0];
        boundary.sketch_feature->placement.references={{{},referenced.container_origin.id,"origin:point"}};
        document::resolve_general_surface_sketch(boundary,
            document::general_surface_boundary_reference_geometry(referenced,0,{}));
        require(boundary.sketch_feature->placement.reference_valid&&boundary.sketch_feature->placement.x==0,
            "Owned Sketch cannot use the Surface Origin as a placement reference");
        kernel::ViewerReferenceGeometry external;
        external.points.push_back({{100,200,300},{"fixed","point",{}}});
        boundary.sketch_feature->placement.references={{{},"fixed","point"}};
        for(double displacement:{0.,50.}) {
            referenced.placement.x=20+displacement;
            document::resolve_general_surface_sketch(boundary,
                document::general_surface_boundary_reference_geometry(referenced,0,external));
            const auto sketch=document::general_surface_display_sketch(referenced,boundary);
            require(boundary.sketch_feature->placement.reference_valid&&
                std::hypot(sketch.resolved_origin.x-100,sketch.resolved_origin.y-200,sketch.resolved_origin.z-300)<1e-7&&
                boundary.sketch_feature->placement.references.front().owner_id=="fixed",
                "Moving the Surface broke or silently detached an external Sketch placement reference");
        }
    }
    const auto owned_file=std::filesystem::temp_directory_path()/"zima-owned-surface-test.prtz";
    owned_state->session.document().save(owned_file,owned_state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> owned_cache;
    auto owned_reopened=document::PartDocument::load(owned_file,&owned_cache);std::filesystem::remove(owned_file);
    kernel::OcctKernel owned_cold;const auto owned_rebuilt=owned_cold.evaluate_history_incremental(owned_reopened.kernel_operations(),owned_cache);
    require(owned_rebuilt.back().calculation_errors.empty()&&std::abs(owned_rebuilt.back().surface_area-8000)<.01,
        "Cold native owned surface reopen lost calculation");
    auto bad_owned=stored_owned;bad_owned.general_surface.boundaries[3].curve->parent_construction_id="wrong-parent";
    bool bad_owned_rejected=false;try{static_cast<void>(workspace::commit_general_surface(owned_live,k,owned_input.document_id,bad_owned));}catch(const std::exception&){bad_owned_rejected=true;}
    require(bad_owned_rejected&&owned_state->session.document().serialized()==owned_before,"Broken owned parent committed partial geometry");
    auto duplicate_owned=owned_copy;
    auto duplicate_sketch=sketcher::Sketch::from_serialized(duplicate_owned.find_container(owned.id)->general_surface.boundaries[0].sketch_serialized);
    duplicate_sketch.id=owned.id;
    duplicate_owned.find_container(owned.id)->general_surface.boundaries[0].sketch_serialized=duplicate_sketch.serialized();
    bool owned_duplicate_rejected=false;try{static_cast<void>(duplicate_owned.serialized());}catch(const std::exception&){owned_duplicate_rejected=true;}
    require(owned_duplicate_rejected,"Owned boundary accepted a document-wide identity collision");
    auto display_owned=stored_owned;display_owned.origin_text_visible=!display_owned.origin_text_visible;
    const auto calculated_shape=owned_state->session.calculated_boundaries().back().kernel_shape;
    const auto calculated_fingerprint=owned_state->session.calculated_boundaries().back().source_fingerprint;
    require(workspace::commit_general_surface(owned_live,k,owned_input.document_id,display_owned)&&
        owned_state->session.calculated_boundaries().back().kernel_shape==calculated_shape&&
        owned_state->session.calculated_boundaries().back().source_fingerprint==calculated_fingerprint,
        "Origin annotation change replaced the General Surface calculation");
    require(workspace::step_part_document_history(owned_live,owned_input.document_id,false)&&owned_state->session.document().serialized()==owned_before,
        "General Surface annotation Undo changed native geometry");
    require(workspace::step_part_document_history(owned_live,owned_input.document_id,false)&&!owned_live.open_part(owned_input.document_id)->session.document().find_container(owned.id),
        "Owned surface Undo retained its native geometry");
    require(workspace::step_part_document_history(owned_live,owned_input.document_id,true)&&owned_live.open_part(owned_input.document_id)->session.document().find_container(owned.id)->general_surface==stored_owned.general_surface,
        "Owned surface Redo lost its boundary definitions");
    auto intersection_input=document::PartDocument::create_default();document::BodyHistoryGraph intersection_graph;
    static_cast<void>(intersection_graph.create_body("Surface Intersection"));intersection_input.set_body_history(intersection_graph);
    workspace::Workspace intersection_live;intersection_live.add_part(intersection_input);intersection_live.activate(intersection_input.document_id);
    auto base_surface=owned;base_surface.placement={};
    require(workspace::commit_general_surface(intersection_live,k,intersection_input.document_id,base_surface),"Intersection base surface failed");
    auto crossing_surface=document::create_general_surface();
    for(unsigned i=0;i<4;++i) {
        auto sketch=sketcher::Sketch::from_serialized(crossing_surface.general_surface.boundaries[i].sketch_serialized);
        static_cast<void>(sketch.add_segment(owned_corners[i].x,owned_corners[i].y,owned_corners[(i+1)%4].x,owned_corners[(i+1)%4].y));
        crossing_surface.general_surface.boundaries[i].sketch_serialized=sketch.serialized();
    }
    crossing_surface.placement.x=50;crossing_surface.placement.z=50;
    crossing_surface.placement.rotation_y=90;crossing_surface.placement.absolute_rotation_y=90;
    require(workspace::commit_general_surface(intersection_live,k,intersection_input.document_id,crossing_surface),"Intersection crossing surface failed");
    auto intersection_feature=document::create_surface_intersection();
    intersection_feature.surface_intersection.faces={kernel::FaceReference{base_surface.id,"surface:from:"+base_surface.feature_id,{}},
        kernel::FaceReference{crossing_surface.id,"surface:from:"+crossing_surface.feature_id,{}}};
    require(workspace::commit_surface_intersection(intersection_live,k,intersection_input.document_id,intersection_feature),"Native intersection did not commit");
    auto* intersection_state=intersection_live.open_part(intersection_input.document_id);
    const auto intersection_before=intersection_state->session.document().serialized();
    require(!workspace::commit_surface_intersection(intersection_live,k,intersection_input.document_id,intersection_feature)&&
        intersection_state->session.document().serialized()==intersection_before,"Unchanged intersection created a transaction");
    const auto intersection_copy=document::PartDocument::from_serialized(intersection_before);
    require(intersection_copy.find_container(intersection_feature.id)->surface_intersection==intersection_feature.surface_intersection,
        "Native intersection lost its two source parents");
    auto invalid_intersection=intersection_feature;invalid_intersection.surface_intersection.faces[1].semantic_key="missing";
    bool invalid_intersection_rejected=false;try{static_cast<void>(workspace::commit_surface_intersection(intersection_live,k,intersection_input.document_id,invalid_intersection));}catch(const std::exception&){invalid_intersection_rejected=true;}
    require(invalid_intersection_rejected&&intersection_state->session.document().serialized()==intersection_before,"Invalid intersection committed partial geometry");
    require(workspace::step_part_document_history(intersection_live,intersection_input.document_id,false)&&!intersection_state->session.document().find_container(intersection_feature.id),
        "Intersection Undo retained the feature");
    require(workspace::step_part_document_history(intersection_live,intersection_input.document_id,true)&&intersection_state->session.document().serialized()==intersection_before,
        "Intersection Redo changed the source identity");
    const auto intersection_file=std::filesystem::temp_directory_path()/"zima-native-intersection-test.prtz";
    intersection_state->session.document().save(intersection_file,intersection_state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> intersection_cache;
    auto intersection_reopened=document::PartDocument::load(intersection_file,&intersection_cache);std::filesystem::remove(intersection_file);
    kernel::OcctKernel intersection_cold;
    const auto cold_intersection=intersection_cold.evaluate_history_incremental(intersection_reopened.kernel_operations(),intersection_cache);
    const auto fresh_intersection=intersection_cold.evaluate_history(intersection_reopened.kernel_operations());
    const auto branch=[&](const kernel::BodyResult& result) {
        const auto found=std::ranges::find_if(result.mesh.original_references.edges,[&](const auto& edge){return edge.reference.owner_id==intersection_feature.id;});
        require(found!=result.mesh.original_references.edges.end()&&found->exact_spline.has_value(),"Cold Intersection lost its exact branch");
        return *found;
    };
    const auto saved_branch=branch(intersection_cache.back());
    require(branch(cold_intersection.back()).reference==saved_branch.reference&&branch(fresh_intersection.back()).reference==saved_branch.reference,
        "Cold native regeneration renamed an intersection branch");
    workspace::Workspace trim_live;trim_live.add_part(intersection_reopened,fresh_intersection);trim_live.activate(intersection_input.document_id);
    auto trim_feature=document::create_surface_trim();trim_feature.surface_trim.target=intersection_feature.surface_intersection.faces[0];
    const auto& trim_tool=intersection_feature.surface_intersection.faces[1];
    trim_feature.surface_trim.tools={{{trim_tool.owner_id,trim_tool.semantic_key,{}},true}};
    trim_feature.surface_trim.seed={25,40,0};trim_feature.surface_trim.seed_valid=true;
    require(workspace::commit_surface_trim(trim_live,k,intersection_input.document_id,trim_feature),"Native Trim did not commit");
    auto* trim_state=trim_live.open_part(intersection_input.document_id);const auto stored_trim=*trim_state->session.document().find_container(trim_feature.id);
    require(!stored_trim.surface_trim.retained_region_key.empty()&&trim_state->session.calculated_boundaries().back().calculation_errors.empty()&&
        std::abs(trim_state->session.calculated_boundaries().back().surface_area-12000)<.02,"Trim did not persist its exact retained intent");
    const auto trim_before=trim_state->session.document().serialized();
    require(!workspace::commit_surface_trim(trim_live,k,intersection_input.document_id,stored_trim)&&trim_state->session.document().serialized()==trim_before,
        "Unchanged Trim created an Undo transaction");
    auto bad_trim=stored_trim;bad_trim.surface_trim.seed={50,40,0};bool invalid_trim_rejected=false;
    try{static_cast<void>(workspace::commit_surface_trim(trim_live,k,intersection_input.document_id,bad_trim));}catch(const std::exception&){invalid_trim_rejected=true;}
    require(invalid_trim_rejected&&trim_state->session.document().serialized()==trim_before,"Failed Trim committed partial geometry");
    require(workspace::part_history_dependencies(trim_state->session.document()).contains({base_surface.id,trim_feature.id})&&
        workspace::part_history_dependencies(trim_state->session.document()).contains({crossing_surface.id,trim_feature.id}),"Trim lost native prerequisites");
    const auto trim_file=std::filesystem::temp_directory_path()/"zima-native-trim-test.prtz";
    trim_state->session.document().save(trim_file,trim_state->session.calculated_boundaries());std::vector<kernel::BodyResult> trim_cache;
    const auto reopened_trim=document::PartDocument::load(trim_file,&trim_cache);std::filesystem::remove(trim_file);kernel::OcctKernel trim_cold;
    const auto rebuilt_trim=trim_cold.evaluate_history(reopened_trim.kernel_operations());
    require(rebuilt_trim.back().calculation_errors.empty()&&std::abs(rebuilt_trim.back().surface_area-12000)<.02&&
        reopened_trim.find_container(trim_feature.id)->surface_trim==stored_trim.surface_trim,"Cold native Trim lost its region or result");
    require(workspace::step_part_document_history(trim_live,intersection_input.document_id,false)&&!trim_state->session.document().find_container(trim_feature.id),"Trim Undo failed");
    require(workspace::step_part_document_history(trim_live,intersection_input.document_id,true)&&trim_state->session.document().serialized()==trim_before,"Trim Redo lost region intent");
    auto moved_tool=*trim_state->session.document().find_container(crossing_surface.id);moved_tool.placement.x=60;
    require(workspace::commit_general_surface(trim_live,k,intersection_input.document_id,moved_tool)&&
        trim_state->session.calculated_boundaries().back().calculation_errors.empty()&&std::abs(trim_state->session.calculated_boundaries().back().surface_area-12800)<.02&&
        trim_state->session.document().find_container(trim_feature.id)->surface_trim.retained_region_key==stored_trim.surface_trim.retained_region_key,
        "Moving the cutting surface lost the retained native region");
    moved_tool=*trim_state->session.document().find_container(crossing_surface.id);moved_tool.placement.x=20;
    require(workspace::commit_general_surface(trim_live,k,intersection_input.document_id,moved_tool)&&
        trim_state->session.calculated_boundaries().back().calculation_errors.empty()&&std::abs(trim_state->session.calculated_boundaries().back().surface_area-9600)<.02,
        "A moving tool changed the retained region to the seed's opposite side");
    moved_tool=*trim_state->session.document().find_container(crossing_surface.id);moved_tool.placement.x=-20;
    require(workspace::commit_general_surface(trim_live,k,intersection_input.document_id,moved_tool)&&
        trim_state->session.calculated_boundaries().back().calculation_errors.contains(trim_feature.id),"A vanished Trim region did not report its invalid intent");
    moved_tool=*trim_state->session.document().find_container(crossing_surface.id);moved_tool.placement.x=20;
    require(workspace::commit_general_surface(trim_live,k,intersection_input.document_id,moved_tool),"Trim tool restoration did not commit");
    auto repaired_trim=*trim_state->session.document().find_container(trim_feature.id);repaired_trim.surface_trim.seed={10,40,0};
    require(workspace::commit_surface_trim(trim_live,k,intersection_input.document_id,repaired_trim)&&
        trim_state->session.calculated_boundaries().back().calculation_errors.empty()&&std::abs(trim_state->session.calculated_boundaries().back().surface_area-9600)<.02,
        "Selecting a new retained region could not repair Trim");
    moved_tool=*trim_state->session.document().find_container(crossing_surface.id);moved_tool.placement.x=520;
    require(workspace::commit_general_surface(trim_live,k,intersection_input.document_id,moved_tool),"Moving the complete trim input failed");
    auto moved_target=*trim_state->session.document().find_container(base_surface.id);moved_target.placement.x=500;
    require(workspace::commit_general_surface(trim_live,k,intersection_input.document_id,moved_target)&&
        trim_state->session.calculated_boundaries().back().calculation_errors.empty()&&std::abs(trim_state->session.calculated_boundaries().back().surface_area-9600)<.02,
        "Trim did not follow its native region after the complete input moved away from the original click");
    workspace::delete_part_history(trim_live,intersection_input.document_id,k,crossing_surface.id);
    const auto unresolved_trim=document::PartDocument::from_serialized(trim_state->session.document().serialized());
    require(unresolved_trim.find_container(trim_feature.id)&&unresolved_trim.removed_reference_states.contains(trim_feature.id),"Removing a Trim tool lost its unresolved feature");
    auto reference_sweep=document::PartDocument::create_sweep2d_container();reference_sweep.sweep2d.result_type=document::ProfileResultType::Surface;
    const auto start_point=std::ranges::find_if(intersection_cache.back().mesh.original_references.points,[&](const auto& point){return
        point.reference.owner_id==intersection_feature.id&&std::hypot(point.position.x-50,point.position.y,point.position.z)<.001;});
    require(start_point!=intersection_cache.back().mesh.original_references.points.end(),"Intersection omitted its native starting point");
    reference_sweep.placement.references={{{},start_point->reference.owner_id,start_point->reference.semantic_key}};
    reference_sweep.placement.x=50;document::PartDocument::reframe_sweep2d_sketches(reference_sweep);
    auto reference_path=sketcher::Sketch::from_serialized(reference_sweep.sweep2d.path_sketch);
    const auto projected=workspace::prepare_sketch_external_reference(intersection_live,intersection_input.document_id,reference_path,
        sketcher::ExternalReferenceKind::Edge,saved_branch.reference.owner_id,saved_branch.reference.semantic_key,{},
        intersection_state->session.document().body_history.active_body_id());
    require(projected.exact_spline.has_value()&&!projected.broken,"Intersection could not be used by ordinary Sketch external references");
    reference_path.add_external_reference(projected);static_cast<void>(reference_path.add_external_profile_geometry(projected.id));
    reference_sweep.sweep2d.path_sketch=reference_path.serialized();document::PartDocument::reframe_sweep2d_sketches(reference_sweep);
    const auto sweep_route=document::PartDocument::sweep2d_route(reference_sweep);
    require(!sweep_route.stations.empty(),"Projected intersection cannot define a Sweep path");
    const auto profile_index=document::PartDocument::ensure_sweep2d_profile(reference_sweep,sweep_route.stations.front().point_id,sweep_route.stations.front().incoming);
    auto reference_profile=sketcher::Sketch::from_serialized(reference_sweep.sweep2d.profiles[profile_index].sketch_serialized);
    static_cast<void>(reference_profile.add_circle(0,0,2));reference_sweep.sweep2d.profiles[profile_index].sketch_serialized=reference_profile.serialized();
    workspace::commit_sweep(intersection_live,k,intersection_input.document_id,reference_sweep,workspace::SweepEditMode::Create);
    require(intersection_state->session.calculated_boundaries().back().calculation_errors.empty()&&
        std::abs(intersection_state->session.calculated_boundaries().back().surface_area-16000-320*std::acos(-1.))<.05,
        "Sweep could not consume the projected intersection's exact curve");
    require(workspace::part_history_dependencies(intersection_state->session.document()).contains({intersection_feature.id,reference_sweep.id}),
        "Sweep lost its projected intersection prerequisite");
    auto changed_crossing=*intersection_state->session.document().find_container(crossing_surface.id);changed_crossing.placement.x=60;
    require(workspace::commit_general_surface(intersection_live,k,intersection_input.document_id,changed_crossing),"Source surface displacement did not commit");
    require(std::abs(intersection_state->session.document().find_container(reference_sweep.id)->placement.x-60)<.001&&
        intersection_state->session.calculated_boundaries().back().calculation_errors.empty(),"Dependent Sweep did not follow the intersection's native point");
    require(workspace::set_part_history_suppressed(intersection_live,intersection_input.document_id,k,base_surface.id,true)&&
        intersection_state->session.document().find_container(intersection_feature.id)->suppressed&&
        intersection_state->session.document().find_container(reference_sweep.id)->suppressed,
        "Surface suppression did not cascade through Intersection to Sweep");
    require(workspace::set_part_history_suppressed(intersection_live,intersection_input.document_id,k,reference_sweep.id,false)&&
        !intersection_state->session.document().find_container(base_surface.id)->suppressed&&
        !intersection_state->session.document().find_container(intersection_feature.id)->suppressed&&
        !intersection_state->session.document().find_container(reference_sweep.id)->suppressed,
        "Sweep restoration did not restore its preceding surface prerequisites");
    std::cout<<"Boundary surfaces: plane, spatial patch, arc, gaps, identity, owned Sketch/3D Curve, persistence and cache passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
