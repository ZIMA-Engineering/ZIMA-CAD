#include <zima/command_host/host.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);return result;
}
Json detail(command_host::Host& host,const Json& ref,const std::string& doc,std::size_t limit=256) {
    auto args=ref;args["document"]=doc;args["limit"]=limit;return run(host,"reference.get",args).data;
}
void shifted(const Json& local,const Json& world,double dx,double dy,double dz) {
    for(std::size_t i=0;i<3;++i) {
        const double delta=i==0?dx:i==1?dy:dz;
        require(std::abs(world[i].get<double>()-local[i].get<double>()-delta)<1e-7,"Reference transformed to wrong occurrence coordinates");
    }
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","source"}});const auto part_id=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    const auto listed=run(host,"reference.list",{{"owner",box},{"kind","face"}}).data;
    require(listed.at("items").size()==6 && listed.at("total")==6,"Box did not expose six original faces");
    const auto first=listed.at("items")[0];const auto face=detail(host,first,part_id);
    require(face.at("surface").at("kind")=="plane" && face.at("triangle_count")==2 && !face.at("samples_truncated").get<bool>(),"Stored Box plane or triangles unavailable");
    const auto limited=detail(host,first,part_id,1);require(limited.at("triangles").size()==1 && limited.at("samples_truncated")==true,"Face sample limit ignored");
    const auto page=run(host,"reference.list",{{"owner",box},{"kind","face"},{"limit",2}}).data;
    const auto page2=run(host,"reference.list",{{"owner",box},{"kind","face"},{"limit",2},{"offset",2}}).data;
    require(page.at("items").size()==2 && page.at("next_offset")==2 && page2.at("items")[0]==listed.at("items")[2],"Reference pages repeated/skipped identities");
    auto* part=live.open_part(part_id);const auto rev=part->session.revision();const auto* cache=part->session.calculated_boundaries().data();
    bool borrowed=false;workspace::visit_original_references(live,part_id,[&](const auto& packet,const auto&){borrowed=&packet==&part->session.calculated_boundaries().back().mesh.original_references;return false;});
    require(borrowed,"Reference visitor copied the full calculated geometry");
    interaction.editing=true;run(host,"reference.list");detail(host,first,part_id);interaction.editing=false;
    require(part->session.revision()==rev && cache==part->session.calculated_boundaries().data() && !host.change(),"Read-only reference query changed model/cache or GUI state");
    require(host.execute_text("reference.get face missing missing").code=="reference_not_found","Missing reference was guessed");
    require(host.execute({{"command","reference.list"},{"arguments",{{"kind","triangle"}}}}).code=="invalid_arguments","Unknown kind accepted");
    for(const Json& value:std::vector<Json>{-1,0,10001,"5",true}) {
        auto args=first;args["limit"]=value;
        require(host.execute({{"command","reference.get"},{"arguments",args}}).code=="invalid_arguments","Invalid reference limit accepted");
    }
    // Two occurrences of the same immutable Part, then two copies of that
    // Assembly. Query only the persisted top snapshot, with no source files.
    kernel::BodySnapshot source=part->session.calculated_boundaries().back();
    auto nested=assembly::AssemblyDocument::create_default();
    auto a=assembly::AssemblyDocument::create_part_occurrence("A",part_id,"missing-source.prtz",source);
    auto b=assembly::AssemblyDocument::create_part_occurrence("B",part_id,"missing-source.prtz",source);
    a.placement.x=20;b.placement.y=30;nested.components={a,b};
    auto top=assembly::AssemblyDocument::create_default();
    auto left=assembly::AssemblyDocument::create_assembly_occurrence("Left",nested.document_id,"missing-child.asmz",nested);
    auto right=assembly::AssemblyDocument::create_assembly_occurrence("Right",nested.document_id,"missing-child.asmz",nested);
    left.placement.z=40;right.placement.x=100;right.placement.rotation_z=90;top.components={left,right};const auto top_id=top.document_id;
    live.add_assembly(top);run(host,"activate",{{"document",top_id}});
    const auto top_revision=live.open_assembly(top_id)->session.revision();const auto source_snapshot=live.open_assembly(top_id)->session.document().components[0].calculated_source;
    const auto all=run(host,"reference.list",{{"owner",box},{"kind","face"}}).data;
    require(all.at("total")==24 && live.size()==2,"Nested query lost repeated occurrences or opened missing dependencies");
    const auto nested_path=assembly::InstancePath{{left.occurrence_id,a.occurrence_id}}.encoded();
    auto nested_ref=first;nested_ref["instance_path"]=nested_path;
    const auto nested_face=detail(host,nested_ref,top_id);
    shifted(face.at("triangles")[0][0],nested_face.at("triangles")[0][0],20,0,40);
    shifted(face.at("surface").at("origin"),nested_face.at("surface").at("origin"),20,0,40);
    auto right_ref=first;right_ref["instance_path"]=assembly::InstancePath{{right.occurrence_id,b.occurrence_id}}.encoded();
    const auto right_face=detail(host,right_ref,top_id);
    const auto& raw_origin=face.at("surface").at("origin");const auto& turned=right_face.at("surface").at("origin");
    require(std::abs(turned[0].get<double>()-(70-raw_origin[1].get<double>()))<1e-7 &&
        std::abs(turned[1].get<double>()-raw_origin[0].get<double>())<1e-7,"Nested face did not follow rotated parent frame");
    const auto& normal=face.at("surface").at("axis");const auto& turned_normal=right_face.at("surface").at("axis");
    require(std::abs(turned_normal[0].get<double>()+normal[1].get<double>())<1e-7 &&
        std::abs(turned_normal[1].get<double>()-normal[0].get<double>())<1e-7,"Analytic normal did not rotate with occurrence");
    const auto point_ref=run(host,"reference.list",{{"document",part_id},{"kind","point"},{"owner",box}}).data.at("items")[0];
    const auto source_point=detail(host,point_ref,part_id);auto nested_point_ref=point_ref;nested_point_ref["instance_path"]=nested_path;
    const auto nested_point=detail(host,nested_point_ref,top_id);shifted(source_point.at("position"),nested_point.at("position"),20,0,40);
    require(host.execute({{"command","reference.get"},{"arguments",first}}).code=="reference_not_found","Ambiguous Assembly query silently selected an occurrence");
    // Editing an open source does not change the parent query until its
    // caller explicitly refreshes/recalculates that parent's state.
    run(host,"activate",{{"document",part_id}});run(host,"box.set",{{"container",box},{"length_mm","20"}});
    const auto still_old=detail(host,nested_ref,top_id);require(still_old.at("triangles")==nested_face.at("triangles"),"Query silently pulled newer source geometry into parent");
    require(live.active_document_id()==part_id && live.open_assembly(top_id)->session.revision()==top_revision &&
        live.open_assembly(top_id)->session.document().components[0].calculated_source.shares_with(source_snapshot),"Query activated/rebuilt parent snapshot");
    // A persisted exact spline is returned as data, without OCCT or fitting.
    auto spline_doc=document::PartDocument::create_default();kernel::BodyResult spline_result;
    kernel::ViewerEdge edge;edge.reference={spline_doc.document_id,"test-source-curve",{}};
    edge.points={{0,0,0},{1,2,0},{3,0,0}};edge.exact_spline=kernel::BSplineGeometry{2,{{0,0,0},{1,2,0},{3,0,0}},{0,0,0,1,1,1},{1,1,1}};
    edge.exact_spline->validate();spline_result.mesh.original_references.edges.push_back(edge);
    auto display_only=edge;display_only.reference={spline_doc.document_id,"display-only",{}};spline_result.mesh.edges.push_back(display_only);
    live.add_part(spline_doc,{spline_result});
    const Json spline_ref={{"kind","edge"},{"owner",spline_doc.document_id},{"key","test-source-curve"}};
    auto display_ref=spline_ref;display_ref["key"]="display-only";display_ref["document"]=spline_doc.document_id;
    require(host.execute({{"command","reference.get"},{"arguments",display_ref}}).code=="reference_not_found","Result topology leaked into original reference queries");
    const auto spline=detail(host,spline_ref,spline_doc.document_id);require(spline.at("segments")[0].at("exact_spline").at("knots")==Json(edge.exact_spline->knots),"Exact spline changed during query");
    const auto short_spline=detail(host,spline_ref,spline_doc.document_id,1);require(short_spline.at("segments")[0].at("points").size()==1 && short_spline.at("segments")[0].at("spline_omitted_by_limit")==true,"Spline data ignored query limit");
    run(host,"new",{{"type","drawing"},{"name","drawing"}});
    require(host.execute_text("reference.list").code=="unsupported_document","Drawing exposed fabricated model references");
}
}
int main() {
    try {
        kernel::OcctKernel kernel;const auto directory=fs::canonical(fs::temp_directory_path())/("zima-reference-commands-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);
        require(directory.parent_path()==fs::canonical(fs::temp_directory_path()),"Unsafe cleanup");fs::remove_all(directory);
        std::cout<<"Original reference queries, pagination, exact surfaces/splines, nested occurrences and unchanged snapshots passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
