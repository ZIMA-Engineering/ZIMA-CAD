#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/command_host/host.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/line_bend.hpp>
#include <zima/document/sheet_profile.hpp>
#include <zima/workspace/sheet_profile_operations.hpp>
#include <zima/kernel/sheet_attachment_reference.hpp>
#include "../app/part_reference_index.hpp"
#include "../app/reference_tree_policy.hpp"
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace zima;
void require(bool value,const std::string& message) {
    if(!value)throw std::runtime_error(message);
}
void near(double a,double b,const std::string& message) {
    require(std::abs(a-b)<1e-6,message+": expected "+std::to_string(b)+", got "+std::to_string(a));
}
struct Flat {
    document::HistoryContainer feature=document::PartDocument::create_sketch_container();
    sketcher::Sketch sketch=sketcher::Sketch::create_default();
    Flat(const char* name,double width,double height,double thickness,kernel::Vec3 position) {
        feature.feature_kind=document::FeatureKind::Flat;feature.name=name;
        feature.flat.sketch_id=sketch.id;sketch.owner_container_id=feature.id;
        feature.flat.thickness=thickness;feature.flat.thickness_override=true;
        feature.placement.x=position.x;feature.placement.y=position.y;feature.placement.z=position.z;
        static_cast<void>(sketch.add_rectangle(0,0,width,height));
    }
};
struct Case {const char* name;kernel::Vec3 position;};
constexpr std::array cases{
    Case{"separated",{120,10,.5}}, Case{"point-contact",{100,50,2}},
    Case{"edge-contact",{100,50,.5}}, Case{"face-contact",{100,10,.5}},
    Case{"overlap",{90,10,.5}}, Case{"contained",{30,10,.5}}};

void verify(const kernel::OcctKernel& kernel,const document::PartDocument& part,
        const std::vector<kernel::BodyResult>& boundaries,const Flat& a,const Flat& b,
        const std::string& stage) {
    require(!boundaries.empty(),stage+": no calculated result");
    auto body=boundaries.back();
    require(body.calculation_errors.empty(),stage+": calculation failed");
    for(const auto& feature:part.history)
        require(feature.placement.reference_valid,stage+": invalid placement "+feature.name);
    kernel.ensure_body_properties(body);
    // Independently known authored material. Overlap must not consume either
    // contribution, even if one complete branch is inside another.
    near(body.volume,100*50*2+20*10*1,stage+": branch material was fused or lost");
    const auto shape=kernel::read_calculated_body_shape(body);
    std::size_t solids=0;
    for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    require(solids==2,stage+": independent branches no longer own two solids");
    for(const auto* branch:{&a,&b}) {
        std::size_t edges=0;
        for(const auto& edge:body.mesh.edges)if(edge.reference.owner_id==branch->feature.id)++edges;
        require(edges==12,stage+": a branch lost its twelve current boundary edges");
        std::vector<std::string> faces;
        for(const auto& face:body.mesh.triangle_references)if(face.owner_id==branch->feature.id)
            faces.push_back(face.semantic_key);
        std::ranges::sort(faces);faces.erase(std::unique(faces.begin(),faces.end()),faces.end());
        require(faces.size()==6,stage+": a branch lost its six independently owned faces");
    }
}

void run_line_bend(const std::filesystem::path& directory) {
    auto part=document::PartDocument::create_default();
    static_cast<void>(part.body_history.create_body("Independent Line Bend sheets"));
    for(const double shift:{0.,10.}) {
        Flat flat("Line Bend branch",100,80,2,{-50,-40+shift,0});
        flat.feature.flat.direction=document::ExtrusionDirection::Reverse;
        part.insert_history_entry(document::PartHistoryKind::Feature,flat.feature.id);
        part.history.push_back(flat.feature);part.sketches.push_back(flat.sketch);
    }
    auto bend=document::PartDocument::create_sketch_container();
    bend.feature_kind=document::FeatureKind::LineBend;
    bend.line_bend.radius=5;bend.line_bend.angle_degrees=90;
    bend.line_bend.thickness_override=true;bend.line_bend.thickness=2;
    bend.line_bend.k_factor_override=true;bend.line_bend.k_factor=.5;
    bend.line_bend.fixed_side=1;
    auto line=sketcher::Sketch::create_default();line.owner_container_id=bend.id;
    bend.line_bend.sketch_id=line.id;bend.line_bend.segment_id=line.add_segment(-50,0,50,0);
    part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);
    part.history.push_back(bend);part.sketches.push_back(line);
    const auto check=[&](kernel::OcctKernel& engine,const auto& result,const char* stage,double expected=32000,std::size_t count=2) {
        for(const auto& [owner,error]:result.back().calculation_errors)std::cerr<<stage<<": "<<owner<<": "<<error<<'\n';
        require(result.back().calculation_errors.empty(),std::string(stage)+": Line Bend failed");
        auto value=result.back();engine.ensure_body_properties(value);
        // The existing single-sheet deformation contract allows 0.01 mm3
        // approximation per sheet. Retain that budget for both contributions.
        require(std::abs(value.volume-expected)<.02,std::string(stage)+
            ": Line Bend changed branch material: "+std::to_string(value.volume));
        std::size_t solids=0;const auto shape=kernel::read_calculated_body_shape(value);
        for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
        require(solids==count,std::string(stage)+": Line Bend merged independent solids");
    };
    kernel::OcctKernel engine;
    const auto parallel_before=kernel::parallel_sheet_branch_jobs.load();
    auto result=workspace::calculate_part_with_resolved_references(engine,part);check(engine,result,"creation");
    require(kernel::parallel_sheet_branch_jobs.load()>=parallel_before+2,"Line Bend bypassed parallel branches");
    for(int repeat=0;repeat<3;++repeat) {
        result=workspace::calculate_part_with_resolved_references(engine,part,&result);check(engine,result,"warm");
    }
    const auto path=directory/"line-bend.prtz";part.save(path,result);
    std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(path,&saved);
    check(engine,saved,"saved packets");kernel::OcctKernel cold;
    result=workspace::calculate_part_with_resolved_references(cold,reopened);check(cold,result,"cold");
    for(const auto kind:{document::FeatureKind::Unbend,document::FeatureKind::BendBack}) {
        auto state=document::PartDocument::create_sketch_container();state.feature_kind=kind;state.sheet_state.all=true;
        reopened.insert_history_entry(document::PartHistoryKind::Feature,state.id);reopened.history.push_back(state);
        result=workspace::calculate_part_with_resolved_references(cold,reopened,&result);check(cold,result,"sheet state");
    }
    auto cut=document::PartDocument::create_sketch_container();
    cut.feature_kind=document::FeatureKind::Extrusion;cut.combine_mode=document::CombineMode::Subtract;
    auto opening=sketcher::Sketch::create_default();opening.owner_container_id=cut.id;
    cut.extrusion.sketch_id=opening.id;cut.extrusion.length_forward=200;cut.placement.z=-100;
    static_cast<void>(opening.add_rectangle(-2,-100,2,100));
    part.insert_history_entry(document::PartHistoryKind::Feature,cut.id);
    part.history.push_back(cut);part.sketches.push_back(opening);
    result=workspace::calculate_part_with_resolved_references(engine,part);check(engine,result,"cut",30720,4);
    for(const auto kind:{document::FeatureKind::Unbend,document::FeatureKind::BendBack}) {
        auto state=document::PartDocument::create_sketch_container();state.feature_kind=kind;state.sheet_state.all=true;
        part.insert_history_entry(document::PartHistoryKind::Feature,state.id);part.history.push_back(state);
        result=workspace::calculate_part_with_resolved_references(engine,part,&result);check(engine,result,"cut state",30720,4);
    }
}

void run_case(const Case& test,const std::filesystem::path& directory) {
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));
    part.set_body_history(std::move(graph));
    const auto id=part.document_id;
    workspace::Workspace live;live.add_part(part);live.activate(id);
    kernel::OcctKernel kernel;
    Flat first("Branch A",100,50,2,{0,0,0}),second("Branch B",20,10,1,test.position);
    require(workspace::commit_flat(live,kernel,id,first.feature,first.sketch),"Cannot create first branch");
    const auto first_definition=live.open_part(id)->session.document().serialized();
    require(workspace::commit_flat(live,kernel,id,second.feature,second.sketch),"Cannot create second branch");
    auto& session=live.open_part(id)->session;
    const auto check=[&](const char* stage) {
        verify(kernel,session.document(),session.calculated_boundaries(),first,second,
            std::string(test.name)+" / "+stage);
    };
    check("creation");
    const auto definition=session.document().serialized();
    require(session.undo(),"Branch creation cannot be undone");
    require(session.document().history.size()==1,"Undo retained the second branch");
    require(session.document().history.front()==document::PartDocument::from_serialized(first_definition).history.front(),
        "Undo changed the first branch definition");
    require(session.redo(),"Branch creation cannot be redone");
    require(session.document().serialized()==definition,"Redo changed the branch definition");
    check("redo");
    for(int repeat=0;repeat<3;++repeat) {
        static_cast<void>(workspace::regenerate_part(live,kernel,id));
        check("live regeneration");
    }
    const auto path=directory/(std::string(test.name)+".prtz");
    session.document().save(path,session.calculated_boundaries());
    std::vector<kernel::BodyResult> saved;
    auto reopened=document::PartDocument::load(path,&saved);
    verify(kernel,reopened,saved,first,second,std::string(test.name)+" / saved packets");
    kernel::OcctKernel cold;
    workspace::PartCalculationPolicy policy;policy.history_reference_baseline=&saved;
    const auto parallel_before=kernel::parallel_sheet_branch_jobs.load();
    const auto calculated=workspace::calculate_part_with_resolved_references(cold,reopened,nullptr,policy);
    verify(cold,reopened,calculated,first,second,std::string(test.name)+" / cold regeneration");
    require(kernel::parallel_sheet_branch_jobs.load()>=parallel_before+2,
        std::string(test.name)+": full regeneration bypassed the parallel branch calculation path");
    policy.publish_complete_history=true;
    kernel::OcctKernel complete;
    const auto sequential=workspace::calculate_part_with_resolved_references(complete,reopened,nullptr,policy);
    verify(complete,reopened,sequential,first,second,std::string(test.name)+" / complete history");
    // Compound Sheet Profiles and transitions enter through FeatureGroupRequest.
    // Exercise that path with the same independently known overlapping material.
    kernel::FeatureGroupRequest group;kernel::HistoryOperation compound;
    compound.owner_id="compound-branches";
    for(const auto& operation:reopened.kernel_operations())if(operation.sheet_material) {
        std::visit([&](const auto& primitive) {
            using T=std::decay_t<decltype(primitive)>;
            if constexpr(std::is_same_v<T,kernel::ExtrusionRequest>||std::is_same_v<T,kernel::RevolutionRequest>||
                    std::is_same_v<T,kernel::Sweep3DRequest>)group.children.push_back(primitive);
            else throw std::runtime_error("Unexpected Flat primitive in compound fixture");
        },operation.primitive);
        compound.sheet_regions.push_back(*operation.sheet_material);
    }
    require(group.children.size()==2,"Compound fixture did not retain both branches");
    compound.primitive=std::move(group);
    auto grouped=complete.evaluate_history({compound}).back();complete.ensure_body_properties(grouped);
    near(grouped.volume,10200,"Compound sheet group merged independent material");
    std::size_t solids=0;const auto compound_shape=kernel::read_calculated_body_shape(grouped);
    for(TopExp_Explorer it(compound_shape,TopAbs_SOLID);it.More();it.Next())++solids;
    require(solids==2,"Compound sheet group merged independent solids");

    // Two consecutive cuts exercise the direct parallel subtraction block as
    // well as full history. Both tools cross B; where B overlaps A they must
    // remove material independently from both authored branches.
    auto cuts=reopened.kernel_operations();
    for(auto& operation:cuts)operation.body={};
    double removed=0;std::size_t expected_solids=4;
    for(const double offset:{2.,6.}) {
        const double x=test.position.x+offset;
        kernel::ExtrusionRequest tool;
        tool.profile_region_id="cut-profile-"+std::to_string(cuts.size());
        tool.outer_boundary_id=tool.profile_region_id+":boundary";
        for(int corner=0;corner<4;++corner) {
            tool.outer_edge_source_ids.push_back(tool.profile_region_id+":edge:"+std::to_string(corner));
            tool.outer_vertex_source_ids.push_back(tool.profile_region_id+":point:"+std::to_string(corner));
        }
        tool.outer_profile=kernel::ExtrusionRequest::PolygonProfile{
            {{x,-10,-5},{x+2,-10,-5},{x+2,80,-5},{x,80,-5}}};
        tool.direction={0,0,15};
        kernel::HistoryOperation operation;
        operation.owner_id="cut-"+std::to_string(cuts.size());
        kernel::FeatureGroupRequest cutter; cutter.children.push_back(std::move(tool));
        operation.primitive=std::move(cutter);operation.operation=kernel::BooleanOperation::Subtract;
        cuts.push_back(std::move(operation));
        const double overlap=std::max(0.,std::min(100.,x+2)-std::max(0.,x));
        removed+=overlap*50*2+2*10;
        if(overlap==2)++expected_solids;
    }
    for(const bool parallel:{false,true}) {
        kernel::OcctKernel engine;kernel::HistoryOutputPolicy output;
        output.final_boundary_first=parallel;
        const auto jobs=kernel::parallel_sheet_branch_jobs.load();
        auto result=[&] {
            kernel::AggregatePropertyScope deferred(kernel::AggregatePropertyDemand::Deferred);
            return engine.evaluate_history_recovering(cuts,{},output).back();
        }();
        for(const auto& [owner,error]:result.calculation_errors)
            std::cerr<<"Branch cut "<<owner<<" parallel="<<parallel<<": "<<error<<'\n';
        require(result.calculation_errors.empty(),"Independent branch cuts failed");
        engine.ensure_body_properties(result);
        near(result.volume,10200-removed,"Cut consumed another branch's material");
        const auto shape=kernel::read_calculated_body_shape(result);std::size_t count=0;
        for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++count;
        require(count==expected_solids,"Cut merged independent branch fragments");
        for(const auto* branch:{&first,&second})
            for(const auto side:{kernel::SheetFaceRole::SideA,kernel::SheetFaceRole::SideB})
                require(std::ranges::any_of(result.mesh.triangle_references,[&](const auto& face) {
                    return face.sheet_owner==branch->feature.id&&face.sheet_role==side;
                }),"Cut lost an independently owned material skin: parallel="+std::to_string(parallel)+
                    " owner="+branch->feature.id+" side="+std::to_string(int(side))+
                    " faces="+std::to_string(result.mesh.triangle_references.size()));
        if(parallel)require(kernel::parallel_sheet_branch_jobs.load()>jobs,
            "Independent cut block bypassed parallel calculation");
    }
}

commands::Result command(command_host::Host& host,const char* name,commands::Json args=commands::Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",args}});
    require(result.ok,std::string(name)+": "+result.message);return result;
}
void references_valid(const document::PartDocument& part,const std::vector<kernel::BodyResult>& calculated) {
    require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Branch calculation failed");
    const auto statuses=workspace::cached_sheet_attachment_reference_status(part,calculated);
    const auto index=app::part_reference_index(part,calculated.back().mesh.original_references);
    for(const auto& feature:part.history) {
        const auto found=statuses.find(feature.id);
        require(feature.placement.reference_valid&&app::feature_reference_issue(feature,part,index,nullptr,
            found==statuses.end()?nullptr:&found->second).empty(),"Branch reference lost: "+feature.name);
    }
}
void run_fork(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;
    command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={std::filesystem::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    command(host,"new",{{"type","part"},{"name","Common parent branches"}});
    const auto id=live.active_document_id();
    const auto base=command(host,"flat.create",{{"width_mm",40.},{"height_mm",30.},
        {"thickness_mm",1.}}).data.at("container").get<std::string>();
    auto* state=live.open_part(id);
    const auto base_geometry=document::sheet_attachment_reference_geometry(state->session.calculated_boundaries().back().mesh);
    std::array<kernel::ViewerEdge,2> supports;
    for(int axis=0;axis<2;++axis) {
        const auto found=std::ranges::find_if(base_geometry.edges,[&](const auto& edge) {
            if(edge.points.size()!=2||kernel::sheet_edge_role(edge)!=kernel::SheetEdgeRole::Boundary)return false;
            const bool side=std::ranges::any_of(edge.edge_treatment_side_references,[](const auto& face){return face.sheet_role==kernel::SheetFaceRole::SideA;});
            return side&&(axis==0?std::abs(edge.points[0].x)+std::abs(edge.points[1].x):
                std::abs(edge.points[0].y)+std::abs(edge.points[1].y))<1e-8;
        });
        require(found!=base_geometry.edges.end(),"Missing common-parent corner edge");supports[axis]=*found;
    }
    std::array<std::string,2> bends,flats;
    for(int branch=0;branch<2;++branch) {
        bends[branch]=command(host,"bend.create",{{"edge_owner",base},
            {"edge_key",supports[branch].reference.semantic_key},{"radius_mm",1.},
            {"radius_follows_thickness",false},{"angle_degrees",90.}}).data.at("container").get<std::string>();
        const auto geometry=document::sheet_attachment_reference_geometry(state->session.calculated_boundaries().back().mesh);
        std::string key;
        for(const auto& edge:geometry.edges)if(edge.reference.owner_id==bends[branch]) {
            try {
                const auto refs=document::flat_sheet_references(edge);
                if(refs[1].semantic_key.starts_with("sweep:cap:end:from:")&&
                    std::ranges::any_of(edge.edge_treatment_side_references,[](const auto& face){return face.sheet_role==kernel::SheetFaceRole::SideA;})) {
                    key=edge.reference.semantic_key;break;
                }
            }catch(const std::exception&){}
        }
        require(!key.empty(),"Missing common-parent Bend end");
        flats[branch]=command(host,"flat.create",{{"edge_owner",bends[branch]},
            {"edge_key",key},{"height_mm",20.}}).data.at("container").get<std::string>();
    }
    const auto verify_fork=[&](double extension) {
        references_valid(state->session.document(),state->session.calculated_boundaries());
        auto body=state->session.calculated_boundaries().back();kernel.ensure_body_properties(body);
        // End extensions taper the Bend from its unchanged start width. The
        // two authored rectangular Flats retain their original widths.
        near(body.volume,1200+70*20+(70+2*extension)*std::numbers::pi*.75,
            "Common-parent branches consumed overlapping material");
        std::size_t solids=0;const auto shape=kernel::read_calculated_body_shape(body);
        for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
        require(solids==3,"Common-parent stem and two branches were merged or split");
    };
    verify_fork(0);
    for(double extension:{2.,3.,-1.,0.,3.}) {
        for(const auto& bend:bends)command(host,"bend.set",{{"container",bend},
            {"first_extension_mm",extension},{"last_extension_mm",extension}});
        command(host,"regenerate");verify_fork(extension);
        const auto definition=state->session.document().serialized();
        command(host,"undo");command(host,"redo");
        require(state->session.document().serialized()==definition,"Overhang Undo/Redo changed branch references");
        verify_fork(extension);
    }
    const auto source=state->session.document();const auto source_boundaries=state->session.calculated_boundaries();
    source.save(directory/"fork-overlap.prtz",source_boundaries);
    {
        std::vector<TopoDS_Shape> solids;
        const auto shape=kernel::read_calculated_body_shape(source_boundaries.back());
        for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())solids.push_back(it.Current());
        bool overlap=false;
        for(std::size_t a=0;a<solids.size();++a)for(std::size_t b=a+1;b<solids.size();++b) {
            BRepAlgoAPI_Common common(solids[a],solids[b]);require(common.IsDone(),"Cannot verify branch overlap");
            GProp_GProps properties;BRepGProp::VolumeProperties(common.Shape(),properties);
            overlap|=properties.Mass()>1e-5;
        }
        require(overlap,"Common-parent fixture does not exercise actual volume overlap");
        workspace::Workspace unfolded;unfolded.add_part(source,source_boundaries);unfolded.activate(id);
        command_host::Host states(unfolded,kernel,directory);
        command(states,"unbend.create");
        references_valid(unfolded.open_part(id)->session.document(),unfolded.open_part(id)->session.calculated_boundaries());
        command(states,"bend_back.create");command(states,"regenerate");
        auto& restored=unfolded.open_part(id)->session;
        references_valid(restored.document(),restored.calculated_boundaries());
        auto result=restored.calculated_boundaries().back();kernel.ensure_body_properties(result);
        near(result.volume,1200+70*20+76*std::numbers::pi*.75,"Bend Back changed branch material");
        std::size_t count=0;const auto returned=kernel::read_calculated_body_shape(result);
        for(TopExp_Explorer it(returned,TopAbs_SOLID);it.More();it.Next())++count;
        require(count==3,"Unbend/Bend Back merged independent branches");
    }
    const auto geometry=document::sheet_attachment_reference_geometry(source_boundaries.back().mesh);
    std::size_t variants=0;
    for(const auto& owner:flats)for(auto side:{kernel::SheetFaceRole::SideA,kernel::SheetFaceRole::SideB}) {
        const kernel::ViewerEdge* selected=nullptr;double height=-INFINITY;
        for(const auto& edge:geometry.edges) {
            if(edge.reference.owner_id!=owner||edge.points.size()!=2||
                    kernel::sheet_edge_role(edge)!=kernel::SheetEdgeRole::Boundary||
                    !std::ranges::any_of(edge.edge_treatment_side_references,[&](const auto& face){return face.sheet_role==side;}))continue;
            const double z=(edge.points[0].z+edge.points[1].z)*.5;
            if(z>height){height=z;selected=&edge;}
        }
        require(selected,"Missing branch-specific wall boundary on one skin");
        for(int kind=0;kind<3;++kind)for(int anchor=0;anchor<3;++anchor) {
            workspace::Workspace probe;probe.add_part(source,source_boundaries);probe.activate(id);
            command_host::Host commands(probe,kernel,directory);
            std::string created;
            if(kind<2)created=command(commands,kind?"bend.create":"flat.create",
                {{"edge_owner",owner},{"edge_key",selected->reference.semantic_key},
                 {"origin_last",anchor==1},{"origin_midpoint",anchor==2},{"width_mm",10.}}).data.at("container").get<std::string>();
            else {
                auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::SheetProfile;
                feature.name="Branch Profile";auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
                feature.sheet_profile.sketch_id=sketch.id;feature.sheet_profile.sheet_attachment=true;
                feature.sheet_profile.width.symmetric=true;feature.sheet_profile.width.sides[0].length=5.;
                feature.placement.references=document::bend_sheet_references(*selected,anchor==2?
                    kernel::sheet_edge_midpoint_reference(selected->reference):selected->edge_treatment_endpoint_references[anchor==1?1:0]);
                feature.sheet_profile.start_point_id=document::initialize_attached_sheet_profile(sketch,1.);
                created=feature.id;require(workspace::commit_sheet_profile(probe,kernel,id,feature,sketch),"Cannot attach branch Profile");
            }
            auto& session=probe.open_part(id)->session;
            references_valid(session.document(),session.calculated_boundaries());
            const auto expected=session.document().find_container(created)->placement.references;
            require(expected.front().owner_id==owner&&expected.front().semantic_key==selected->reference.semantic_key,
                "Attachment substituted another branch edge");
            const auto definition=session.document().serialized();
            command(commands,"undo");command(commands,"redo");
            require(session.document().serialized()==definition,"Attachment Undo/Redo changed identity");
            command(commands,"regenerate");references_valid(session.document(),session.calculated_boundaries());
            const auto file=directory/("dependent-"+std::to_string(variants++)+".prtz");
            session.document().save(file,session.calculated_boundaries());
            std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(file,&saved);
            references_valid(reopened,saved);kernel::OcctKernel cold;
            workspace::PartCalculationPolicy policy;policy.history_reference_baseline=&saved;
            auto calculated=workspace::calculate_part_with_resolved_references(cold,reopened,nullptr,policy);
            references_valid(reopened,calculated);
            require(reopened.find_container(created)->placement.references==expected,"Reopening changed the selected branch reference");
        }
    }
    require(variants==36,"Incomplete common-parent attachment matrix");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2||argc==3,"Specify the required branch contact case and optional output directory");
        const auto output=argc==3?std::filesystem::path(argv[2]):std::filesystem::current_path()/"build/sheet-branch-contract";
        if(std::string_view(argv[1])=="line-bend") {
            std::filesystem::create_directories(output);run_line_bend(output);
            std::cout<<"Independent Line Bend material and states passed\n";return 0;
        }
        if(std::string_view(argv[1])=="common-parent") {
            const auto directory=output/"common-parent";
            std::filesystem::create_directories(directory);run_fork(directory);
            std::cout<<"Common-parent branch extensions and 36 attachment variants passed\n";return 0;
        }
        const auto selected=std::ranges::find(cases,std::string_view(argv[1]),&Case::name);
        require(selected!=cases.end(),"Unknown branch contact case");
        const auto directory=output/selected->name;
        std::filesystem::create_directories(directory);
        run_case(*selected,directory);
        std::cout<<selected->name<<": independent material, current topology, live/cold regeneration, Undo/Redo and native reopen passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
