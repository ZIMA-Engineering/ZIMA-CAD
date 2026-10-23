#include "profile_solid_fixture.hpp"
#include <set>
#include <tuple>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <zima/kernel/pattern_geometry.hpp>
#include <zima/kernel/axis_display.hpp>
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/workspace/workspace.hpp>
#include <iostream>
#include <set>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <zima/document/viewer_packet_json.hpp>
using namespace zima;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void close(double a,double b){if(std::abs(a-b)>=1e-7){std::cerr<<"Measure: actual="<<a<<" expected="<<b<<'\n';throw std::runtime_error("Mirror changed a geometric measure");}}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid Mirror accepted");}
static zima::test::ProfilePrism box(double length){zima::test::ProfilePrism result{length,5,7};result.translation={4,2,1};return result;}
struct HistoryTrace {
    std::ostringstream log;
    std::streambuf* previous_stream=std::cerr.rdbuf(log.rdbuf());
    std::string previous_flag=std::getenv("ZIMA_CPP_PROFILE_COMMAND")?std::getenv("ZIMA_CPP_PROFILE_COMMAND"):"";
    static void flag(const std::string& value) {
#ifdef _WIN32
        _putenv_s("ZIMA_CPP_PROFILE_COMMAND",value.c_str());
#else
        if(value.empty())unsetenv("ZIMA_CPP_PROFILE_COMMAND");else setenv("ZIMA_CPP_PROFILE_COMMAND",value.c_str(),1);
#endif
    }
    HistoryTrace(){flag("1");}
    ~HistoryTrace(){std::cerr.rdbuf(previous_stream);flag(previous_flag);}
};
static void same_packet(const nlohmann::json& a,const nlohmann::json& b,const std::string& path={}) {
    if(a.is_number()&&b.is_number()) {
        const double x=a.get<double>(),y=b.get<double>();
        if(std::abs(x-y)>1e-9*std::max({1.,std::abs(x),std::abs(y)})) {
            std::cerr<<"Packet mismatch "<<path<<": "<<x<<" / "<<y<<'\n';
            throw std::runtime_error("Incremental copy changed calculated geometry/properties");
        }return;
    }
    require(a.type()==b.type()&&a.size()==b.size(),"Incremental copy changed packet structure");
    if(a.is_object())for(auto it=a.begin();it!=a.end();++it) {
        require(b.contains(it.key()),"Incremental copy lost a packet field");same_packet(it.value(),b.at(it.key()),path+"/"+it.key());
    }else if(a.is_array())for(std::size_t i=0;i<a.size();++i)same_packet(a[i],b[i],path+"/"+std::to_string(i));
    else require(a==b,"Incremental copy changed persisted reference identity");
}
static void compare_calculation(const kernel::BodyResult& a,const kernel::BodyResult& b) {
    auto packet=[](auto body) {
        // OCCT archive addresses are private to an individual calculation.
        body.kernel_shape.clear();body.kernel_faces.clear();body.kernel_edges.clear();body.kernel_vertices.clear();
        return document::serialize_body_result(body,false,false);
    };
    same_packet(packet(a),packet(b));
}
static void incremental_copy_sources() {
    kernel::HistoryOperation stock{"cache-stock",zima::test::ProfilePrism{20,20,20}};
    zima::test::ProfilePrism hole{4,4,1};hole.translation={2,2,-2};
    auto through=static_cast<kernel::ExtrusionRequest>(hole);through.extent=kernel::ExtrusionRequest::Extent::ThroughAll;
    kernel::HistoryOperation source{"cache-source",through,kernel::BooleanOperation::Subtract};
    kernel::HistoryOperation copy;copy.owner_id="cache-pattern";copy.operation=kernel::BooleanOperation::Subtract;
    kernel::BodyHistoryScope scope;scope.source_feature_id=source.owner_id;scope.combination=kernel::BodyCombination::Pattern;
    scope.pattern.count=2;scope.pattern.linear[0].count=2;scope.pattern.linear[0].spacing=8;copy.feature_copy=scope;
    zima::test::ProfilePrism tail{2,2,20};tail.translation={16,16,0};
    kernel::HistoryOperation last{"cache-last",tail,kernel::BooleanOperation::Subtract};
    std::vector<kernel::HistoryOperation> operations{stock,source,copy,last};kernel::OcctKernel warm;
    auto result=warm.evaluate_history(operations);close(result.back().volume,7280);
    const auto verify=[&](bool expect_prefix=false) {
        std::vector<kernel::BodyResult> next;
        {HistoryTrace trace;next=warm.evaluate_history_incremental(operations,result);
            if(expect_prefix)require(trace.log.str().find("operations=4 reused=3 calculated=1 copy_sources=1")!=std::string::npos,
                "A final edit recalculated the unchanged copy/source history");}
        kernel::OcctKernel fresh;
        const auto expected=fresh.evaluate_history(operations);
        require(next.size()==expected.size(),"Incremental copy lost a history boundary");
        for(std::size_t i=0;i<next.size();++i)compare_calculation(next[i],expected[i]);
        result=next;
    };
    std::get<kernel::ExtrusionRequest>(operations.back().primitive).direction.z=19;verify(true);
    // Restore operands from live cache after a native packet round trip.
    for(auto& boundary:result)boundary=document::load_body_result(document::serialize_body_result(boundary));
    std::get<kernel::ExtrusionRequest>(operations.back().primitive).direction.z=18;verify(true);
    for(const double extent:{17.,16.,15.,14.,13.,12.,11.,10.,9.,8.}) {
        std::get<kernel::ExtrusionRequest>(operations.back().primitive).direction.z=extent;verify(true);
    }
    // Changed source, Through All input and copy transform/count invalidate reuse.
    std::get<kernel::ExtrusionRequest>(operations[1].primitive).direction.z=2;verify();
    std::get<kernel::ExtrusionRequest>(operations[0].primitive).direction.z=24;verify();
    operations[2].feature_copy->pattern.linear[0].spacing=6;verify();
    operations[2].feature_copy->pattern.count=3;operations[2].feature_copy->pattern.linear[0].count=3;verify();
    operations[2].feature_copy->combination=kernel::BodyCombination::Mirror;
    operations[2].feature_copy->mirror_plane={{10,0,0},{1,0,0}};verify();
    auto& limited=std::get<kernel::ExtrusionRequest>(operations[1].primitive);
    limited.extent=kernel::ExtrusionRequest::Extent::UpToPlane;limited.target_is_datum=true;
    limited.target_face={"cache-limit","plane",{}};
    limited.target_plane_origin={0,0,12};verify();
    limited.target_plane_origin.z=14;verify();
    std::get<kernel::ExtrusionRequest>(operations.back().primitive).direction.z=17;verify();
    kernel::OcctKernel cold;const auto rebuilt=cold.evaluate_history_incremental(operations,result);
    compare_calculation(rebuilt.back(),result.back());
    // A separate Body requesting one feature must receive that operand, not stock.
    for(auto& operation:operations)operation.body.id="cache-main";
    auto external=copy;external.owner_id="cache-external";external.feature_copy.reset();external.operation=kernel::BooleanOperation::Add;
    external.body.id="cache-copy-body";external.body.source_id="cache-main";
    external.body.source_feature_id=source.owner_id;external.body.combination=kernel::BodyCombination::Pattern;
    external.body.pattern.count=2;external.body.pattern.linear[0].count=2;external.body.pattern.linear[0].spacing=30;operations.push_back(external);
    result=warm.evaluate_history(operations);
    std::get<kernel::ExtrusionRequest>(operations[3].primitive).direction.z=16;verify();
    close(result.back().body_outputs.at("cache-copy-body")->volume,256);
}
static void copied_construction_axes() {
    for(const bool circular:{false,true})for(const bool extra:{false,true}) {
        kernel::OcctKernel engine;
        auto profile=static_cast<kernel::ExtrusionRequest>(test::CircularExtrusion{2,8});
        std::get<kernel::ExtrusionRequest::CircleProfile>(profile.outer_profile).center={12,0,0};
        profile.centerlines.origin_enabled=profile.centerlines.centroid_enabled=extra;
        profile.centerlines.origin={12,0,0};profile.centerlines.profile_id="profile";profile.centerlines.origin_id="origin";
        kernel::HistoryOperation source{"axis-source",profile};
        kernel::HistoryOperation copy;copy.owner_id="axis-pattern";
        kernel::BodyHistoryScope scope;scope.source_feature_id=source.owner_id;
        scope.combination=kernel::BodyCombination::Pattern;
        scope.pattern.circular=circular;scope.pattern.count=3;
        scope.pattern.angle_degrees=90;scope.pattern.full_circle=false;
        scope.pattern.linear[0].count=3;scope.pattern.linear[0].spacing=10;
        copy.feature_copy=scope;
        std::vector<kernel::HistoryOperation> operations{source,copy};
        const auto result=engine.evaluate_history(operations);
        const auto source_axes=std::ranges::count_if(result.back().mesh.original_references.axes,
            [](const auto& a){return a.reference.owner_id=="axis-source";});
        require(source_axes>0,"Source construction fixture has no axes");
        const auto verify=[&](const kernel::BodyResult& body) {
            const auto& axes=body.mesh.original_references.axes;
            require(std::ranges::count_if(axes,[](const auto& a){return a.reference.owner_id=="axis-source";})==source_axes,
                "Pattern changed the original axis");
            require(std::ranges::count_if(axes,[](const auto& a){return a.reference.owner_id=="axis-pattern";})==2*source_axes,
                "Pattern omitted or duplicated copied axes");
            for(unsigned index=1;index<3;++index) {
                const auto expected=kernel::pattern_point({12,0,4},scope.pattern,index);
                const auto found=std::ranges::find_if(axes,[&](const auto& a){
                    return a.reference.owner_id==copy.owner_id &&
                        a.reference.semantic_key.starts_with("pattern:"+kernel::pattern_copy_id(scope.pattern,index)+":")&&
                        a.reference.semantic_key.ends_with("axis:primary");});
                require(found!=axes.end(),"Pattern axis lost its copy identity");
                close(found->point.x,expected.x);close(found->point.y,expected.y);close(found->point.z,expected.z);
                close(found->direction.z,1);
                close(kernel::axis_display_length(*found),found->display_length);
            }
            for(const auto& point:body.mesh.original_references.points)if(point.reference.owner_id==copy.owner_id) {
                const auto parent=kernel::axis_endpoint_parent_key(point.reference.semantic_key);
                if(!parent.empty())require(std::ranges::any_of(axes,[&](const auto& a){return a.reference.owner_id==copy.owner_id&&a.reference.semantic_key==parent;}),
                    "Copied endpoint lost its parent axis identity");
            }
            const auto source_edges=std::ranges::count_if(body.mesh.original_references.edges,[](const auto& e){return e.construction&&e.reference.owner_id=="axis-source";});
            const auto copied_edges=std::ranges::count_if(body.mesh.original_references.edges,[](const auto& e){return e.construction&&e.reference.owner_id=="axis-pattern";});
            require(copied_edges==2*source_edges,"Pattern lost enabled origin/centroid centerlines");
        };
        verify(result.back());
        verify(document::load_body_result(document::serialize_body_result(result.back())));
        kernel::OcctKernel cold;verify(cold.evaluate_history(operations).back());
        auto toggled=operations;
        auto& settings=std::get<kernel::ExtrusionRequest>(toggled.front().primitive).centerlines;
        settings.origin_enabled=settings.centroid_enabled=!extra;
        kernel::OcctKernel fresh;
        compare_calculation(engine.evaluate_history_incremental(toggled,result).back(),fresh.evaluate_history(toggled).back());
        // A copy of a copy must retain the construction packet as its source.
        auto nested=copy;nested.owner_id="nested-pattern";
        nested.feature_copy->source_feature_id=copy.owner_id;
        nested.feature_copy->pattern.circular=false;
        nested.feature_copy->pattern.linear[0].spacing=60;
        operations.push_back(nested);
        const auto chained=engine.evaluate_history(operations).back();
        require(std::ranges::count_if(chained.mesh.original_references.axes,[](const auto& a){return a.reference.owner_id=="nested-pattern";})==4*source_axes,
            "Nested Pattern dropped construction axes");
    }
    std::cout<<"Linear/circular and nested Pattern construction axes passed\n";
}
int main(){try{
    copied_construction_axes();
    incremental_copy_sources();
    kernel::HistoryOperation zero;zero.owner_id="signed-zero";zero.body.id="body";zero.primitive=zima::test::rectangular_request();
    auto signed_zero=zero;signed_zero.body.translation.y=-0.0;
    // Profile direction carries modeling intent; test only placement zero here.
    require(kernel::history_fingerprint({zero},1)==kernel::history_fingerprint({signed_zero},1),
        "Geometrically identical signed zeros must have the same cache identity");
    kernel::HistoryOperation directed;directed.owner_id="directed";
    kernel::ExtrusionRequest extrusion;extrusion.wall=kernel::ProfileWall{};
    directed.primitive=extrusion;auto opposite=directed;
    std::get<kernel::ExtrusionRequest>(opposite.primitive).wall->first_offset=-0.0;
    require(kernel::history_fingerprint({directed},1)!=kernel::history_fingerprint({opposite},1),
        "Authored offset signed zero must retain its cache identity");
    directed.body.id="pattern";directed.body.combination=kernel::BodyCombination::Pattern;
    directed.body.pattern.linear[0].direction={1,0,0};opposite=directed;
    opposite.body.pattern.linear[0].direction={-1,0,0};
    require(kernel::history_fingerprint({directed},1)!=kernel::history_fingerprint({opposite},1),
        "Opposite Pattern directions must retain distinct cache identities");
    kernel::OcctKernel kernel;
    const auto source=kernel.evaluate_history({{"box",box(3)}}).back();
    for(auto plane:{kernel::MirrorPlane{{0,0,0},{1,0,0}},kernel::MirrorPlane{{3,-2,5},{1,2,3}},kernel::MirrorPlane{{4,0,0},{0,1,0}}}) {
        const auto result=kernel.mirror_body(source,plane,"mirror");close(result.volume,source.volume);close(result.surface_area,source.surface_area);
        close(kernel.compound_bodies({{result,{},{}}}).volume,source.volume);
        const auto twice=kernel.mirror_body(kernel.mirror_body(source,plane),plane);
        require(twice.mesh.vertices.size()==source.mesh.vertices.size(),"Reflection changed tessellation identity");
        for(std::size_t i=0;i<source.mesh.vertices.size();++i){const auto a=source.mesh.vertices[i],b=twice.mesh.vertices[i];close(a.x,b.x);close(a.y,b.y);close(a.z,b.z);}
        require(result.mesh.triangles[1]==source.mesh.triangles[2],"Mirror did not reverse winding");
        for(const auto& ref:result.mesh.original_references.triangle_references)if(ref.valid()){
            require(ref.owner_id=="mirror","Mirror reused the original body owner");
            const auto parent=kernel::mirror_parent_reference(ref.semantic_key);require(parent&&parent->owner_id=="box","Mirror lost its persisted topology parent");}
    }
    document::BodyHistoryGraph graph;const auto source_id=graph.create_body("Zdroj");graph.insert({document::PartHistoryKind::Feature,"box"});
    document::BodyHistory body;body.name="Zrcadlo";body.scope.id="mirror-body";body.derived_copy=document::DerivedCopyParameters{source_id,{{},"mirror-body:origin","origin:plane:yz"}};
    body.scope.placement.x=2;document::PartDocument::resolve_copy_reference(*body.derived_copy,body.scope.id,body.scope.placement,{});close(body.derived_copy->resolved_plane.point.x,2);
    const auto mirrored_id=graph.create_derived_copy(body);
    const auto compile=[&](double length){return graph.compile([&](const auto& e)->std::optional<kernel::HistoryOperation>{return kernel::HistoryOperation{e.id,box(length)};});};
    auto result=kernel.evaluate_history(compile(3));close(result.back().volume,210);
    close(result.back().body_outputs.at(mirrored_id)->volume,105);
    result=kernel.evaluate_history_incremental(compile(6),result);close(result.back().body_outputs.at(mirrored_id)->volume,210);
    require(graph.available_before(graph.order().size()).size()==2,"Mirror consumed its source");
    rejects([&]{graph.activate(mirrored_id);});rejects([&]{graph.move_step(mirrored_id,0);});
    auto restored=document::BodyHistoryGraph::from_serialized(graph.serialized());require(restored.find(mirrored_id)->derived_copy==body.derived_copy,"Mirror parameters lost on reload");
    auto boolean_graph=graph;const auto joined=boolean_graph.create_boolean("Součet",kernel::BodyCombination::Add,source_id,mirrored_id);
    const auto boolean_operations=boolean_graph.compile([&](const auto& e)->std::optional<kernel::HistoryOperation>{return kernel::HistoryOperation{e.id,box(3)};});
    const auto boolean_body=kernel.evaluate_history(boolean_operations).back().body_outputs.at(joined);
    const auto mirrored_boolean=kernel.mirror_body(boolean_body,{{20,0,0},{1,0,0}},"copy-of-boolean");
    close(kernel.compound_bodies({{mirrored_boolean,{},{}}}).volume,210);
    require(std::ranges::all_of(mirrored_boolean.mesh.triangle_references,[](const auto& ref){return ref.owner_id=="copy-of-boolean";}),"Boolean copy has no whole-container display ownership");
    require(std::ranges::none_of(mirrored_boolean.mesh.original_references.triangle_references,[](const auto& ref){return ref.semantic_key=="container:display";}),"Display ownership leaked into original topology");
    assembly::AssemblyDocument assembly=assembly::AssemblyDocument::create_default();
    auto component=assembly::AssemblyDocument::create_part_occurrence("Zdroj","source-part",{},source);component.placement.x=10;
    auto mirror=assembly::AssemblyDocument::create_part_occurrence("Zrcadlo","source-part",{},{});
    mirror.derived_copy=document::DerivedCopyParameters{component.occurrence_id,{{},mirror.occurrence_id+":origin","origin:plane:yz"}};
    mirror.copy_placement.x=1;assembly.components={component,mirror};assembly.calculate_derived_copies(kernel);
    const auto& reflected=assembly.components.back();close(reflected.calculated_source->volume,source.volume);
    close(reflected.calculated_source->mesh.vertices.front().x,2-(source.mesh.vertices.front().x+10));
    require(assembly.derived_source(mirror.occurrence_id)->occurrence_id==component.occurrence_id,"Mirror did not resolve its source occurrence");
    const auto snapshot=reflected.calculated_source->mesh.vertices;
    assembly.components.front().placement.x=20;static_cast<void>(assembly.build_scene());require(assembly.components.back().calculated_source->mesh.vertices==snapshot,"Scene refresh recalculated Mirror");
    assembly.calculate_derived_copies(kernel);close(assembly.components.back().calculated_source->mesh.vertices.front().x,2-(source.mesh.vertices.front().x+20));
    const auto file=std::filesystem::temp_directory_path()/"zima-mirror-contract.asmz";assembly.save(file);auto loaded=assembly::AssemblyDocument::load(file);std::filesystem::remove(file);
    require(loaded.components.back().derived_copy==assembly.components.back().derived_copy,"Assembly reload lost Mirror source/plane");
    auto cycle=assembly;cycle.components.front().derived_copy=document::DerivedCopyParameters{mirror.occurrence_id,{{},component.occurrence_id+":origin","origin:plane:yz"}};
    rejects([&]{cycle.calculate_derived_copies(kernel);});
    auto followed=assembly;auto follower=component;follower.occurrence_id="follower";follower.placement={};follower.grounded=false;
    const auto vertex=source.mesh.original_references.points.front().reference;
    follower.placement_references.push_back({assembly::MateKind::PointCoincident,
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(follower.occurrence_id),vertex.owner_id,vertex.semantic_key},
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(mirror.occurrence_id),vertex.owner_id,vertex.semantic_key}});
    followed.components.push_back(follower);followed.calculate_derived_copies(kernel);
    followed.components.front().placement.x+=13;followed.calculate_derived_copies(kernel);
    const auto& row=followed.components.back().placement_references.front();
    const auto moving=followed.resolve_point(row.component_reference),target=followed.resolve_point(row.target_reference);
    require(moving.status==assembly::MateStatus::Valid&&target.status==assembly::MateStatus::Valid,"Follower lost its copied reference");
    close(moving.point.x,target.point.x);close(moving.point.y,target.point.y);close(moving.point.z,target.point.z);
    auto driven_source=followed;driven_source.components.front().placement_references=driven_source.components.back().placement_references;
    driven_source.components.front().grounded=false;rejects([&]{driven_source.calculate_derived_copies(kernel);});
    // Independent linear/circular checks: position, volume, identity, mode
    // changes, persistence and explicit source ownership inside a nested group.
    kernel::PatternRequest pattern;pattern.count=4;pattern.linear[0].spacing=20;
    auto copies=kernel.pattern_body(source,pattern,"pattern");close(copies.volume,3*source.volume);
    close(kernel.compound_bodies({{copies,{},{}}}).volume,3*source.volume);
    const auto v=source.mesh.vertices.front();close(copies.mesh.vertices.front().x,v.x+20);
    for(const auto& r:copies.mesh.original_references.triangle_references)if(r.valid())
        require(r.owner_id=="pattern"&&r.semantic_key.starts_with("pattern:copy-"),"Pattern lost copy topology identity");
    auto grid=pattern;
    grid.linear[0].count=3;grid.linear[0].distribution=kernel::PatternDistribution::Symmetric;
    grid.linear[1].local_axis=1;grid.linear[1].direction={0,1,0};grid.linear[1].count=2;grid.linear[1].spacing=30;
    grid.linear[2].local_axis=2;grid.linear[2].direction={0,0,1};grid.linear[2].count=2;grid.linear[2].spacing=40;grid.linear[2].distribution=kernel::PatternDistribution::Reverse;
    require(kernel::pattern_instance_count(grid)==12,"3D grid total is incorrect");
    auto grid_result=kernel.pattern_body(source,grid,"grid");close(grid_result.volume,11*source.volume);
    std::set<std::tuple<int,int,int>> positions;
    for(unsigned index=0;index<12;++index){const auto p=kernel::pattern_point({},grid,index);positions.emplace(std::lround(p.x),std::lround(p.y),std::lround(p.z));}
    require(positions.size()==12&&positions.contains({0,0,0})&&positions.contains({-20,30,-40})&&positions.contains({20,0,0}),"Grid lost origin or a symmetric/reverse corner");
    const auto stable_id=kernel::pattern_copy_id(grid,3); // First Y copy, no X/Z displacement.
    auto resized_grid=grid;resized_grid.linear[0].count=5;
    require(kernel::pattern_copy_id(resized_grid,5)==stable_id,"Resizing X changed the identity of an existing Y copy");
    auto both=grid;both.linear[0].distribution=kernel::PatternDistribution::Both;both.linear[0].count=2;both.linear[0].reverse_count=2;
    require(kernel::pattern_instance_count(both)==16,"Independent reverse count is ignored");
    auto invalid=grid;invalid.linear[1].local_axis=0;rejects([&]{static_cast<void>(kernel::validated_pattern(invalid));});
    invalid=grid;invalid.linear[0].count=4;rejects([&]{static_cast<void>(kernel::validated_pattern(invalid));});
    invalid=grid;invalid.linear[1].count=1000;rejects([&]{static_cast<void>(kernel::validated_pattern(invalid));});
    auto grid_graph=graph;document::BodyHistory grid_body;grid_body.name="Mřížka";
    grid_body.derived_copy=document::DerivedCopyParameters{};grid_body.derived_copy->source_id=source_id;grid_body.derived_copy->pattern=grid;
    const auto grid_id=grid_graph.create_derived_copy(grid_body);
    require(document::BodyHistoryGraph::from_serialized(grid_graph.serialized()).find(grid_id)->derived_copy==grid_body.derived_copy,"Grid directions did not round-trip");
    pattern.circular=true;pattern.origin={2,3,4};pattern.axis={0,0,1};
    copies=kernel.pattern_body(source,pattern,"pattern");
    close(copies.mesh.vertices.front().x,2-(v.y-3));close(copies.mesh.vertices.front().y,3+(v.x-2));close(copies.mesh.vertices.front().z,v.z);
    pattern.full_circle=false;pattern.angle_degrees=180;rejects([&]{static_cast<void>(kernel.pattern_body(source,pattern));});
    pattern.full_circle=true;pattern.count=1;rejects([&]{static_cast<void>(kernel.pattern_body(source,pattern));});
    pattern.count=4;
    document::DerivedCopyParameters parameters{source_id,{{},"pattern-body:origin","origin:axis:z"}};parameters.pattern=pattern;
    document::Placement frame;frame.rotation_y=90;parameters.pattern->linear[0].local_axis=1;
    document::PartDocument::resolve_copy_reference(parameters,"pattern-body",frame,{});
    close(parameters.pattern->axis.x,1);close(parameters.pattern->axis.z,0);
    for(const bool patterned:{false,true}) {
        auto own=parameters;if(!patterned)own.pattern.reset();
        own.reference={{},"pattern-body:origin",patterned?"origin:axis:z":"origin:plane:yz"};
        for(const auto& ref:{document::ConstructionReference{{},"other:origin",own.reference.semantic_key},
            document::ConstructionReference{"other-occurrence","pattern-body:origin",own.reference.semantic_key},
            document::ConstructionReference{{},"pattern-body:origin",own.reference.semantic_key,2}}) {
            auto invalid=own;invalid.reference=ref;
            rejects([&]{document::PartDocument::resolve_copy_reference(invalid,"pattern-body",frame,{});});
        }
    }
    parameters.pattern->circular=false;const auto retained=parameters.reference;
    document::PartDocument::resolve_copy_reference(parameters,"pattern-body",frame,{});
    close(parameters.pattern->linear[0].direction.y,1);require(parameters.reference==retained,"Linear mode discarded the stored circular axis");
    auto pattern_graph=graph;document::BodyHistory patterned;patterned.name="Pole";patterned.scope.id="pattern-body";patterned.derived_copy=parameters;
    const auto pattern_id=pattern_graph.create_derived_copy(patterned);
    auto operations=pattern_graph.compile([&](const auto& e)->std::optional<kernel::HistoryOperation>{return kernel::HistoryOperation{e.id,box(3)};});
    const auto patterned_result=kernel.evaluate_history(operations).back();close(patterned_result.body_outputs.at(pattern_id)->volume,315);
    require(patterned_result.body_inputs.contains(pattern_id),"Pattern has no selectable body result");
    require(document::BodyHistoryGraph::from_serialized(pattern_graph.serialized()).find(pattern_id)->derived_copy==patterned.derived_copy,"Pattern mode/axis not persisted");
    auto patterned_assembly=assembly;auto group=mirror;group.occurrence_id="pattern-group";group.name="Pole";
    group.derived_copy->pattern=kernel::PatternRequest{};group.derived_copy->reference={{},"pattern-group:origin","origin:axis:z"};
    patterned_assembly.components.push_back(group);patterned_assembly.calculate_derived_copies(kernel);
    const auto* calculated_group=patterned_assembly.find_occurrence(group.occurrence_id);
    require(calculated_group->nested_snapshot.size()==3,"Pattern does not persist its individual occurrences");
    const auto scene=patterned_assembly.build_scene();std::set<std::string> copy_paths;
    for(const auto& r:scene.triangle_references)if(r.instance_path.starts_with(assembly::InstancePath{}.child(group.occurrence_id).encoded()))copy_paths.insert(r.instance_path);
    require(copy_paths.size()==3,"Pattern copies share one selectable occurrence path");
    workspace::Workspace workspace;workspace.add_assembly(patterned_assembly);
    require(workspace.derived_source_path(patterned_assembly.document_id,assembly::InstancePath{}.child(group.occurrence_id).child("copy-x1-y0-z0"))==assembly::InstancePath{}.child(component.occurrence_id),"Pattern edit did not return to its exact source");
    const auto pattern_file=std::filesystem::temp_directory_path()/"zima-pattern-contract.asmz";patterned_assembly.save(pattern_file);
    const auto reloaded=assembly::AssemblyDocument::load(pattern_file);std::filesystem::remove(pattern_file);
    require(reloaded.find_occurrence(group.occurrence_id)->nested_snapshot==calculated_group->nested_snapshot,"Pattern occurrence snapshot did not round-trip");
    std::cout<<"Mirror and Pattern contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
