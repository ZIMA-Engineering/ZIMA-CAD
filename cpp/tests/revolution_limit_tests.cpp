#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/rotation_target.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/feature_rotation_limit.hpp>
#include <zima/workspace/general_surface_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <chrono>
#include <QCoreApplication>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cmath>
#include <set>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
kernel::RevolutionRequest request() {
    kernel::RevolutionRequest r;
    r.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{10,0,0},{20,0,0},{20,0,5},{10,0,5}}};
    r.profile_region_id="region";r.outer_boundary_id="boundary";
    r.outer_edge_source_ids={"a","b","c","d"};r.outer_vertex_source_ids={"p","q","r","s"};
    r.profile_normal={0,-1,0};r.axis_direction={0,0,1};
    r.end_limit=kernel::ExtrusionLimit{true,{"datum","plane",{}},true,{0,5,0},{0,1,0},{}};
    return r;
}
void valid(const kernel::BodyResult& body) {
    std::cout<<"Limited body volume="<<body.volume<<" errors="<<body.calculation_errors.size()<<'\n';
    for(const auto& [owner,error]:body.calculation_errors)std::cout<<owner<<": "<<error<<'\n';
    BRep_Builder builder;TopoDS_Shape shape;std::istringstream input(body.kernel_shape);BRepTools::Read(shape,input,builder);
    double independent_volume{};
    for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next()) {
        GProp_GProps properties;const auto error=BRepGProp::VolumeProperties(it.Current(),properties,1e-10);
        check(error>=0,"Independent volume integration failed");independent_volume+=properties.Mass();
    }
    check(std::abs(independent_volume-body.volume)<1e-5,"Persisted volume disagrees with independent integration");
    check(body.calculation_errors.empty()&&body.volume>0,"Limited rotation has no solid");
    check(BRepCheck_Analyzer(shape,true,false,true).IsValid(),"Limited rotation fails exact BRep check");
}
void native_surface(const std::filesystem::path& file) {
    auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Rotation limits"));part.set_body_history(graph);
    kernel::OcctKernel kernel;workspace::Workspace live;live.add_part(part);live.activate(part.document_id);
    auto patch=document::create_general_surface();
    const std::array<kernel::Vec3,4> corners{{{-30,5,-10},{30,5,-10},{30,7,10},{-30,5,10}}};
    for(unsigned i=0;i<4;++i) {
        patch.general_surface.boundaries[i]=document::create_general_surface_curve(patch);
        auto& curve=*patch.general_surface.boundaries[i].curve;curve.curve_type=document::Curve3DType::Polyline;curve.curve_points.clear();
        for(auto p:{corners[i],corners[(i+1)%4]}) {auto point=document::create_owned_point(curve.id);point.origin=p;curve.curve_points.push_back(point);}
    }
    check(workspace::commit_general_surface(live,kernel,part.document_id,patch),"Native target creation failed");
    auto sketch=sketcher::Sketch::create_default();sketch.plane=sketcher::SketchPlane::XZ;sketch.plane_auto=false;sketch.refresh_default_frame();
    static_cast<void>(sketch.add_rectangle(10,0,20,5));const auto axis=sketch.add_segment(0,-1,0,6);sketch.set_segment_centerline(axis,true);
    auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
    auto& p=feature.feature;p.axis_segment_id=axis;p.sides[0].operation=document::FeatureSideOperation::Revolution;
    p.sides[0].rotation_extent=document::FeatureRotationExtent::UpTo;p.origin_centerline=p.centroid_centerline=true;
    document::ExtrusionParameters::EndTarget requested;requested.reference={patch.id,"surface:from:"+patch.feature_id,{}};
    p.sides[0].targets={workspace::resolve_profile_end_target(live.open_part(part.document_id)->session.document(),
        live.open_part(part.document_id)->session.calculated_boundaries(),graph.active_body_id(),requested)};
    const auto begin=std::chrono::steady_clock::now();
    workspace::commit_profile(live,kernel,part.document_id,feature,workspace::ProfileEditMode::Create,sketch);
    std::cout<<"Native curved rotation calculation_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';
    auto* state=live.open_part(part.document_id);valid(state->session.calculated_boundaries().back());
    const auto check_cap=[&](const workspace::PartState& state,const document::HistoryContainer& surface) {
        const auto target=kernel.evaluate_history({{surface.id,document::general_surface_request(state.session.document(),surface)}}).back();
        BRep_Builder builder;TopoDS_Shape shape;std::istringstream input(target.kernel_shape);BRepTools::Read(shape,input,builder);
        Handle(Geom_Surface) support;for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())support=BRep_Tool::Surface(TopoDS::Face(it.Current()));
        std::size_t caps{};const auto& packet=state.session.calculated_boundaries().back().mesh.original_references;
        for(std::size_t i=0;i<packet.triangle_references.size();++i) {
            const auto& ref=packet.triangle_references[i];if(ref.owner_id!=feature.id||ref.semantic_key.starts_with("generated:"))continue;
            bool on_target=true;for(unsigned j=0;j<3;++j) {
                const auto v=packet.vertices[packet.triangles[3*i+j]];GeomAPI_ProjectPointOnSurf project(gp_Pnt(v.x,v.y,v.z),support);
                on_target=on_target&&project.NbPoints()>0&&project.LowerDistance()<1e-7;
            }
            if(on_target)++caps;
        }
        check(caps>0,"Native curved rotation has no exact cap on target");
        std::size_t path_ends{};for(const auto& point:packet.points)if(point.reference.owner_id==feature.id&&
            point.reference.semantic_key.starts_with("profile:path-point:")&&std::hypot(point.position.x,point.position.y)>1.) {
            if(std::abs(point.position.y)<1e-8)continue;
            GeomAPI_ProjectPointOnSurf project(gp_Pnt(point.position.x,point.position.y,point.position.z),support);
            check(project.NbPoints()>0&&project.LowerDistance()<1e-7,"Curved rotation centroid axis missed target");++path_ends;
        }
        check(path_ends>0,"Curved rotation lost centroid endpoint");
    };
    check_cap(*state,patch);
    const auto revision=state->session.revision();const auto* cached=state->session.calculated_boundaries().data();
    workspace::commit_profile(live,kernel,part.document_id,*state->session.document().find_container(feature.id),workspace::ProfileEditMode::Replace);
    check(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cached,"Unchanged rotation recalculated");
    check(state->session.undo()&&!state->session.document().find_container(feature.id),"Limited rotation Undo failed");
    check(state->session.redo()&&state->session.document().find_container(feature.id),"Limited rotation Redo failed");
    state->session.document().save(file,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;auto reopened=document::PartDocument::load(file,&cache);
    kernel::OcctKernel cold;workspace::Workspace reopened_live;reopened_live.add_part(reopened,cache,file);reopened_live.activate(reopened.document_id);
    static_cast<void>(workspace::regenerate_part(reopened_live,cold,reopened.document_id));
    state=reopened_live.open_part(reopened.document_id);valid(state->session.calculated_boundaries().back());check_cap(*state,patch);
    auto changed=*state->session.document().find_container(patch.id);const auto volume=state->session.calculated_boundaries().back().volume;
    for(auto& boundary:changed.general_surface.boundaries)for(auto& point:boundary.curve->curve_points)
        if(point.origin.x==30&&point.origin.z==10)point.origin.y=9;
    check(workspace::commit_general_surface(reopened_live,cold,reopened.document_id,changed),"Curved target edit failed");
    state=reopened_live.open_part(reopened.document_id);check(state->session.calculated_boundaries().back().volume>volume+.01,"Target edit did not update rotation");
    check(state->session.document().find_container(feature.id)->feature.sides[0].targets.front().reference==requested.reference,"Target edit changed original reference identity");
    check_cap(*state,changed);
    const auto preview=state->session.document().feature_preview_edges(*state->session.document().find_container(feature.id),{});
    check(!preview.empty(),"Limited rotation has no persisted-data wire preview");
    const auto one_side_volume=state->session.calculated_boundaries().back().volume;
    auto symmetric=*state->session.document().find_container(feature.id);symmetric.feature.symmetric=true;
    workspace::commit_profile(reopened_live,cold,reopened.document_id,symmetric,workspace::ProfileEditMode::Replace);
    const auto& both=state->session.calculated_boundaries().back();valid(both);
    check(std::abs(both.volume-2*one_side_volume)<.001,"Symmetric curved rotation did not reflect its target");
    const auto symmetric_preview=state->session.document().feature_preview_edges(symmetric,{});
    check(symmetric_preview.size()==2*preview.size(),"Symmetric Up To preview lost a side");
}
void drafted_surface_target() {
    for(bool surface:{false,true})for(double draft:{-5.,5.}) {
        std::cout<<"Draft target surface="<<surface<<" angle="<<draft<<'\n';
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Drafted Up To"));part.set_body_history(graph);
        kernel::OcctKernel kernel;workspace::Workspace live;live.add_part(part);live.activate(part.document_id);
        auto patch=document::create_general_surface();
        const std::array<kernel::Vec3,4> corners{{{-50,-50,25},{50,-50,25},{50,50,30},{-50,50,25}}};
        for(unsigned i=0;i<4;++i) {
            patch.general_surface.boundaries[i]=document::create_general_surface_curve(patch);
            auto& curve=*patch.general_surface.boundaries[i].curve;curve.curve_type=document::Curve3DType::Polyline;curve.curve_points.clear();
            for(auto p:{corners[i],corners[(i+1)%4]}){auto point=document::create_owned_point(curve.id);point.origin=p;curve.curve_points.push_back(point);}
        }
        check(workspace::commit_general_surface(live,kernel,part.document_id,patch),"Draft target creation failed");
        auto* state=live.open_part(part.document_id);const auto target_body=state->session.calculated_boundaries().back();
        BRep_Builder builder;TopoDS_Shape target;std::istringstream input(target_body.kernel_shape);BRepTools::Read(target,input,builder);
        Handle(Geom_Surface) support;for(TopExp_Explorer it(target,TopAbs_FACE);it.More();it.Next())support=BRep_Tool::Surface(TopoDS::Face(it.Current()));
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-10,-10,10,10));
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.result_type=surface?document::ProfileResultType::Surface:document::ProfileResultType::Solid;
        auto& side=feature.feature.sides[0];side.draft_angle_degrees=draft;side.extrusion_extent=document::EndCondition::UpTo;
        document::ExtrusionParameters::EndTarget requested;requested.reference={patch.id,"surface:from:"+patch.feature_id,{}};
        side.targets={workspace::resolve_profile_end_target(state->session.document(),state->session.calculated_boundaries(),graph.active_body_id(),requested)};
        workspace::commit_profile(live,kernel,part.document_id,feature,workspace::ProfileEditMode::Create,sketch);
        const auto& body=live.open_part(part.document_id)->session.calculated_boundaries().back();
        check(body.calculation_errors.empty()&&(surface?std::abs(body.volume)<1e-9:body.volume>5000),"Drafted Up To General Surface failed");
        std::size_t ends{};const auto& packet=body.mesh.original_references;
        for(const auto& edge:packet.edges)if(edge.reference.owner_id==feature.id&&
            (edge.reference.semantic_key.starts_with("end:")||edge.reference.semantic_key.starts_with("start:"))) {
            if(std::ranges::all_of(edge.points,[](auto p){return std::abs(p.z)<1e-7;}))continue;
            for(auto p:edge.points) {
                GeomAPI_ProjectPointOnSurf project(gp_Pnt(p.x,p.y,p.z),support);
                check(project.NbPoints()>0&&project.LowerDistance()<=.001,"Drafted end rim left the exact target surface");
            }
            ++ends;
        }
        check(ends>0,"Drafted General Surface limit lost its end rim ancestry");
    }
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try {
    kernel::OcctKernel kernel;
    for(bool reverse:{false,true})for(bool surface:{false,true}) {
        auto r=request();r.surface_result=surface;
        if(reverse){r.axis_direction.z=-1;r.end_limit->origin.y=-5;}
        const auto body=kernel.evaluate_history({{"rotation",r}}).back();
        if(!surface)valid(body);else check(std::abs(body.volume)<1e-9&&body.surface_area>0,"Limited surface rotation acquired volume");
        std::size_t caps=0;
        for(std::size_t i=0;i<body.mesh.original_references.triangle_references.size();++i) {
            const auto& ref=body.mesh.original_references.triangle_references[i];
            if(!ref.semantic_key.starts_with("end:"))continue;
            ++caps;for(unsigned j=0;j<3;++j) {
                const auto p=body.mesh.original_references.vertices[body.mesh.original_references.triangles[3*i+j]];
                check(std::abs(p.y-(reverse?-5:5))<1e-7,"Angular end missed exact plane");
            }
        }
        if(!surface)check(caps>0,"Limited rotation lost end cap ancestry");
        std::set<std::string> ends;
        for(const auto& edge:body.mesh.original_references.edges)if(edge.reference.owner_id=="rotation"&&edge.reference.semantic_key.starts_with("end:")) {
            ends.insert(edge.reference.semantic_key);
            for(auto point:edge.points)check(std::abs(point.y-(reverse?-5:5))<1e-7,"Rotation end rim missed exact target");
        }
        check(ends==std::set<std::string>{"end:a","end:b","end:c","end:d"},"Limited rotation lost Sketch curve parents");
        auto changed=r;changed.end_limit->origin.y=reverse?-6:6;
        check(kernel::history_fingerprint({{"rotation",r}},1)!=kernel::history_fingerprint({{"rotation",changed}},1),"Rotation end failed to invalidate cache");
        const auto fresh=kernel.evaluate_history_incremental({{"rotation",changed}},{body}).back();
        check(surface||fresh.volume>body.volume,"Changed end did not extend rotation");
        r.end_limit->origin.y=30;bool rejected=false;
        try{static_cast<void>(kernel.evaluate_history({{"rotation",r}}));}catch(const std::exception&){rejected=true;}
        check(rejected,"Rotation accepted a target beyond the circular trajectories");
    }
    std::cout<<"Exact rotation plane limits passed\n";
    auto offset=request();offset.start_angle_degrees=10.;offset.centerlines.origin_enabled=true;
    offset.centerlines.origin={15,0,2.5};offset.centerlines.origin_id="origin";
    const auto offset_body=kernel.evaluate_history({{"offset",offset}}).back();valid(offset_body);
    bool endpoint{};for(const auto& point:offset_body.mesh.original_references.points)
        if(point.reference.semantic_key=="profile:path-point:end:from:centerline:from:origin:origin") {
            check(std::abs(point.position.y-5.)<1e-7,"Offset rotation axis endpoint missed target");endpoint=true;
        }
    check(endpoint,"Offset limited rotation lost its path endpoint");
    const auto file=std::filesystem::temp_directory_path()/("zima-rotation-limit-"+document::PartDocument::create_default().document_id+".prtz");
    native_surface(file);std::filesystem::remove(file);
    drafted_surface_target();
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
