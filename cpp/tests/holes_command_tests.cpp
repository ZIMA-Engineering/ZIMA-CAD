#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/holes_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/holes.hpp>
#include <cmath>
#include <chrono>
#include <iostream>
#include <numbers>
#include <set>

using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void check(bool value, const char* message) {if (!value) throw std::runtime_error(message);}
void near(double value, double expected) {if (std::abs(value-expected)>1e-5) throw std::runtime_error("Volume: " + std::to_string(value) + " expected " + std::to_string(expected));}
commands::Result run(command_host::Host& host, const std::string& command, Json args=Json::object()) {
    auto result=host.execute({{"command", command}, {"arguments", std::move(args)}});
    if (!result.ok) throw std::runtime_error(command+": "+result.message);
    return result;
}
void verify_preview(const document::HistoryContainer& feature, const sketcher::Sketch& sketch) {
    const auto preview=document::holes_preview(feature,sketch);
    std::size_t cylinders=0;
    for(const auto& segment:sketch.segments) {
        if(segment.construction)continue;
        ++cylinders;
        const auto* a=sketch.find_point(segment.first_point_id),*b=sketch.find_point(segment.second_point_id);
        const auto start=sketch.world_point(a->x,a->y),end=sketch.world_point(b->x,b->y);
        const kernel::Vec3 delta{end.x-start.x,end.y-start.y,end.z-start.z};
        const double length=std::hypot(delta.x,delta.y,delta.z);
        std::size_t count=0;double low=length,high=0;
        for(const auto& edge:preview.edges)if(edge.reference.semantic_key.starts_with("preview:holes:"+segment.id+":")) {
            ++count;
            for(const auto& point:edge.points) {
                const kernel::Vec3 offset{point.x-start.x,point.y-start.y,point.z-start.z};
                const double along=(offset.x*delta.x+offset.y*delta.y+offset.z*delta.z)/length;
                const double radial=std::hypot(offset.x-along*delta.x/length,
                    offset.y-along*delta.y/length,offset.z-along*delta.z/length);
                check(std::abs(radial-feature.holes.diameter*.5)<1e-8,"Preview radius differs from drilling diameter");
                check(along>=-1e-8 && along<=length+1e-8,"Preview exceeds the segment depth");
                low=std::min(low,along);high=std::max(high,along);
            }
        }
        check(count==6 && std::abs(low)<1e-8 && std::abs(high-length)<1e-8,"Cylinder preview has incomplete endpoints or sides");
    }
    check(preview.edges.size()==cylinders*6 && preview.dimensions.size()==1,"Preview includes construction geometry or duplicate diameter dimensions");
    check(preview.axes.size()==cylinders,"Each drilling segment must have one preview axis");
    const auto dimension=preview.dimensions.front();
    check(dimension.kind==kernel::ViewerDimensionKind::Diameter && dimension.value==feature.holes.diameter &&
          dimension.reference.semantic_key=="parameter:diameter" && dimension.label_prefix=="⌀ ","Preview lost its editable diameter annotation");
    auto reordered=sketch;std::reverse(reordered.segments.begin(),reordered.segments.end());
    check(document::holes_preview(feature,reordered).dimensions.front().witness_first==dimension.witness_first,
          "Reordering segments moved the diameter anchor");
    auto reduced=sketch;
    std::erase_if(reduced.segments,[&](const auto& segment){
        const auto* point=reduced.find_point(segment.first_point_id);
        return !segment.construction && reduced.world_point(point->x,point->y)==dimension.witness_first;
    });
    if(std::ranges::any_of(reduced.segments,[](const auto& segment){return !segment.construction;}))
        check(document::holes_preview(feature,reduced).dimensions.front().witness_first!=dimension.witness_first,
              "Deleted segment retained the diameter anchor");
    reduced.segments.clear();
    const auto empty=document::holes_preview(feature,reduced);
    check(empty.edges.empty()&&empty.dimensions.empty(),"Empty Sketch retained a cylinder or diameter dimension");
}

void verify(const kernel::OcctKernel& kernel, fs::path directory) {
    workspace::Workspace live; command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","hydraulic-block"}});
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","40"},{"width_mm","40"},{"height_mm","40"}});
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    const auto source=run(host,"sketch.create",{{"name","Drilling axes"},{"plane","XY"}}).data;
    const auto sketch_id=source.at("sketch").get<std::string>();
    const auto owner=source.at("owner").get<std::string>();
    const auto add_line=[&](Json first,Json second) {return run(host,"sketch.segment.create",{{"sketch",sketch_id},{"first",first},{"second",second}}).data.at("geometry").get<std::string>();};
    add_line({-20,0},{20,0});
    const auto standalone=workspace::document_sketch(live,id,sketch_id);
    run(host,"holes.create",{{"sketch",sketch_id},{"diameter_mm",6}});
    const auto volume=[&]{return live.open_part(id)->session.calculated_boundaries().back().volume;};
    near(volume(),64000-std::numbers::pi*9*40);
    const auto feature=*state->session.document().find_container(owner);
    check(feature.feature_kind==document::FeatureKind::Holes && feature.id==standalone.owner_container_id,"Conversion lost ownership");
    run(host,"undo");check(state->session.document().find_container(owner)->feature_kind==document::FeatureKind::Sketch,"Conversion Undo did not restore Sketch");
    run(host,"redo");near(volume(),64000-std::numbers::pi*9*40);
    run(host,"holes.set",{{"container",owner},{"diameter_mm",4}});near(volume(),64000-std::numbers::pi*4*40);
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    run(host,"holes.get",{{"container",owner}});
    check(!run(host,"holes.set",{{"container",owner},{"diameter_mm",4}}).data.at("changed").get<bool>(),"No-op committed");
    for (double diameter : {0.,-1.,1000001.})
        check(!host.execute({{"command","holes.set"},{"arguments",{{"container",owner},{"diameter_mm",diameter}}}}).ok,"Invalid diameter accepted");
    check(state->session.revision()==revision && state->session.calculated_boundaries().data()==cache,"Query or rejection modified session");
    run(host,"undo");near(volume(),64000-std::numbers::pi*9*40);run(host,"redo");
    // Orthogonal through channels: sum of cylinders minus Steinmetz overlap.
    auto sketch=workspace::document_sketch(live,id,sketch_id);
    static_cast<void>(sketch.add_segment(0,-20,0,20));
    auto current=*state->session.document().find_container(owner);
    workspace::commit_holes(live,kernel,id,current,sketch);
    near(volume(),64000-(2*std::numbers::pi*4*40-16.*8/3));
    // Reordering segments preserves native provenance and the result.
    const auto references=state->session.calculated_boundaries().back().mesh.original_references;
    std::reverse(sketch.segments.begin(),sketch.segments.end());
    workspace::commit_holes(live,kernel,id,current,sketch);
    near(volume(),64000-(2*std::numbers::pi*4*40-16.*8/3));
    const auto keys=[](const kernel::ViewerReferenceGeometry& geometry) {
        std::set<std::string> result;
        for(const auto& f:geometry.triangle_references)result.insert("face:"+f.owner_id+":"+f.semantic_key);
        for(const auto& e:geometry.edges)result.insert("edge:"+e.reference.owner_id+":"+e.reference.semantic_key);
        for(const auto& p:geometry.points)result.insert("point:"+p.reference.owner_id+":"+p.reference.semantic_key);
        for(const auto& a:geometry.axes)result.insert("axis:"+a.reference.owner_id+":"+a.reference.semantic_key);
        return result;
    };
    check(!references.triangle_references.empty(),"Missing original topology");
    check(keys(references)==keys(state->session.calculated_boundaries().back().mesh.original_references),"Segment reordering changed original topology identities");
    // An auxiliary construction segment never drills material.
    auto auxiliary=sketch;static_cast<void>(auxiliary.add_segment(-20,10,20,10));auxiliary.segments.back().construction=true;
    workspace::commit_holes(live,kernel,id,current,auxiliary);
    near(volume(),64000-(2*std::numbers::pi*4*40-16.*8/3));
    verify_preview(current,auxiliary);
    // Plane rotation and translation must use the resolved Sketch frame once.
    current.placement.rotation_x=90;current.placement.absolute_rotation_x=90;current.placement.z=5;
    workspace::commit_holes(live,kernel,id,current,sketch);
    const auto& resolved=*std::ranges::find(state->session.document().sketches,sketch_id,&sketcher::Sketch::id);
    const auto request=document::holes_request(*state->session.document().find_container(owner),resolved);
    bool vertical=false;for(const auto& child:request.children){const auto& e=std::get<kernel::ExtrusionRequest>(child);vertical=vertical||std::abs(e.direction.z)>39.9;}
    check(vertical,"Rotated Sketch did not rotate drilling axes");
    verify_preview(current,resolved);
    run(host,"save");std::vector<kernel::BodyResult> loaded_cache;
    const auto loaded=document::PartDocument::load(state->path,&loaded_cache);
    for (const auto& axis:request.axes) {
        const auto matches=[&](const auto& a){return a.reference==axis.reference && a.point==axis.point &&
            a.direction==axis.direction && a.display_length==axis.display_length;};
        check(std::ranges::any_of(loaded_cache.back().mesh.axes,matches),"Native cache lost the visible drilling axis");
        check(std::ranges::any_of(loaded_cache.back().mesh.original_references.axes,matches),"Native cache lost the original drilling axis reference");
    }
    check(std::ranges::count_if(loaded_cache.back().mesh.axes,[&](const auto& a){return a.reference.owner_id==owner;})==2,
        "Drilling axes were duplicated or omitted");
    check(loaded.find_container(owner)->holes==current.holes,"Native file lost Holes parameters");
    near(loaded_cache.back().volume,volume());
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,volume());
    // Unsupported curves and degenerate lines fail atomically.
    const auto saved=state->session.revision();
    auto invalid=sketch;static_cast<void>(invalid.add_circle(0,0,5));
    bool rejected=false;try{workspace::commit_holes(live,kernel,id,current,invalid);}catch(const std::exception&){rejected=true;}
    check(rejected&&saved==state->session.revision(),"Unsupported curve changed document");
    auto degenerate=sketch;
    const auto segment=degenerate.segments.front();
    const auto* start=degenerate.find_point(segment.first_point_id);
    auto* end=degenerate.find_point(segment.second_point_id);end->x=start->x;end->y=start->y;
    rejected=false;try{workspace::commit_holes(live,kernel,id,current,degenerate);}catch(const std::exception&){rejected=true;}
    check(rejected&&saved==state->session.revision(),"Zero-length drilling changed document");
    auto empty=sketch;for(auto& segment:empty.segments)segment.construction=true;
    rejected=false;try{workspace::commit_holes(live,kernel,id,current,empty);}catch(const std::exception&){rejected=true;}
    check(rejected&&saved==state->session.revision(),"Empty drilling changed document");
    auto additive=current;additive.combine_mode=document::CombineMode::Add;
    rejected=false;try{workspace::commit_holes(live,kernel,id,additive,sketch);}catch(const std::exception&){rejected=true;}
    check(rejected&&saved==state->session.revision(),"Holes added material");
    // Shared start points must not alias the seam of two drilling axes.
    auto adjacent=sketcher::Sketch::create_default();adjacent.owner_container_id=current.id;
    static_cast<void>(adjacent.add_segment(0,0,10,0));static_cast<void>(adjacent.add_segment(0,0,0,10));
    auto adjacent_feature=current;adjacent_feature.holes.sketch_id=adjacent.id;
    const auto adjacent_request=document::holes_request(adjacent_feature,adjacent);
    check(std::get<kernel::ExtrusionRequest>(adjacent_request.children[0]).outer_vertex_source_ids!=
        std::get<kernel::ExtrusionRequest>(adjacent_request.children[1]).outer_vertex_source_ids,"Shared start point aliased bore topology");
    // Blind depth follows endpoints, including reversed drawing direction.
    auto blind=sketch;blind.segments.resize(1);
    auto* a=blind.find_point(blind.segments[0].first_point_id);auto* b=blind.find_point(blind.segments[0].second_point_id);
    a->x=-10;a->y=0;b->x=-20;b->y=0;current.placement.rotation_x=0;current.placement.absolute_rotation_x=0;current.placement.z=0;
    workspace::commit_holes(live,kernel,id,current,blind);
    near(volume(),64000-std::numbers::pi*4*10);
    run(host,"new",{{"type","assembly"},{"name","assembly"}});
    check(!host.execute({{"command","holes.create"},{"arguments",{{"sketch",sketch_id},{"diameter_mm",4}}}}).ok,"Assembly accepted Part-only holes");
}
}
int main() {
    const auto directory=fs::temp_directory_path()/("zima-holes-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {fs::create_directories(directory);kernel::OcctKernel kernel;verify(kernel,directory);fs::remove_all(directory);std::cout<<"Holes contracts passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
