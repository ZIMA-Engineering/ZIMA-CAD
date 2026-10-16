#include <zima/command_host/host.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;
using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
#include "drawing_surface_contour_tests.inc"
void repair_copied_annotations(const std::filesystem::path& input,const std::string& previous,const std::filesystem::path& output,bool local_primary=false) {
    require(std::filesystem::absolute(input).lexically_normal()!=std::filesystem::absolute(output).lexically_normal(),"Repair output must be an independent file");
    auto document=drawing::DrawingDocument::load(input);unsigned repaired=0;
    for(auto& sheet:document.sheets)for(auto& view:sheet.views) {
        std::set<drawing::ModelAnnotationReference> authored;
        for(const auto& item:view.model_annotations)if(item.source.document_id==previous) {
            auto source=item.source;source.document_id=view.source_document_id;authored.insert(std::move(source));
        }
        // Retain the authored visibility/layout, replacing only the fresh
        // default duplicate produced when its old owner became unresolved.
        std::erase_if(view.model_annotations,[&](const auto& item){return item.source.document_id!=previous&&authored.contains(item.source);});
        for(auto& item:view.model_annotations)if(item.source.document_id==previous){item.source.document_id=view.source_document_id;++repaired;}
    }
    if(local_primary) {
        const auto local=document.source_path.filename();
        require(!local.empty()&&std::filesystem::exists(output.parent_path()/local),"Local primary model is unavailable beside Drawing output");
        document.source_path=local;
        for(auto& source:document.sources)if(source.document_id==document.source_document_id)source.source_path=local;
        for(auto& sheet:document.sheets) {
            for(auto& view:sheet.views)if(view.source_document_id==document.source_document_id)view.source_path=local;
            for(auto& row:sheet.bom_rows)if(row.source_document_id==document.source_document_id)row.source_path=local;
        }
    }
    require(repaired>0,"No requested copied-source annotations found");document.save(output);
    require(drawing::DrawingDocument::load(output).document_id==document.document_id,"Annotation repair changed Drawing identity");
    std::cout<<"Rebound "<<repaired<<" authored model annotations without changing Drawing layout\n";
}
void compare_authored_parts(const std::filesystem::path& first,const std::filesystem::path& second) {
    if(first.extension()==".asmz") {
        auto original=assembly::AssemblyDocument::load(first);const auto updated=assembly::AssemblyDocument::load(second);
        workspace::apply_relation_parameters(original,assembly::physical_values(updated));
        const auto before=original.serialized(),after=updated.serialized();
        if(before!=after)throw std::runtime_error("Assembly template definition changed: "+Json::diff(before,after).dump());
        std::cout<<"Assembly template authored definition preserved; Relations evaluated\n";return;
    }
    std::vector<kernel::BodyResult> old_geometry,new_geometry;
    auto original=document::PartDocument::load(first,&old_geometry);const auto updated=document::PartDocument::load(second,&new_geometry);
    workspace::apply_relation_parameters(original,document::physical_values(original,new_geometry));
    auto before=original.serialized(),after=updated.serialized();unsigned refreshed_bounds=0;
    before.erase("calculation_definition_fingerprint");after.erase("calculation_definition_fingerprint");
    for(std::size_t i=0;i<original.history.size()&&i<updated.history.size();++i)
        if(original.history[i].feature_kind==document::FeatureKind::ShaftThread&&updated.history[i].feature_kind==document::FeatureKind::ShaftThread) {
            const auto& previous=original.history[i].shaft_thread.cylinder;const auto& current=updated.history[i].shaft_thread.cylinder;
            if(previous==current&&previous.surface&&current.surface) {
                auto old_support=*previous.surface,new_support=*current.surface;
                new_support.axial_min=old_support.axial_min;new_support.axial_max=old_support.axial_max;
                if(old_support==new_support)for(const auto* field:{"min","max"}) {
                    auto& value=after["history"][i]["cylinder"]["surface"][field];
                    const auto& old=before["history"][i]["cylinder"]["surface"][field];
                    if(value!=old){value=old;++refreshed_bounds;}
                }
            }
        }
    if(before!=after)throw std::runtime_error("Authored Part definitions differ: "+Json::diff(before,after).dump());
    if(!old_geometry.empty()&&!new_geometry.empty()) {
        const auto& a=old_geometry.back();const auto& b=new_geometry.back();
        require(std::abs(a.volume-b.volume)<1e-8*std::max(1.,std::abs(a.volume))&&
            std::abs(a.surface_area-b.surface_area)<1e-8*std::max(1.,std::abs(a.surface_area)),"Calculated volume or area changed");
        const auto identities=[](const auto& geometry) {
            std::set<std::tuple<std::string,std::string,std::string>> faces,edges;
            for(const auto& face:geometry.triangle_references)if(face.valid())faces.emplace(face.owner_id,face.semantic_key,face.instance_path);
            for(const auto& edge:geometry.edges)if(edge.reference.valid())edges.emplace(edge.reference.owner_id,edge.reference.semantic_key,edge.reference.instance_path);
            return std::pair{faces,edges};
        };
        require(identities(a.mesh.original_references)==identities(b.mesh.original_references),"Original face or edge reference identities changed");
    }
    std::cout<<"Authored fields and original references preserved; Relations evaluated; "<<refreshed_bounds<<" derived Thread support bounds refreshed\n";
}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,std::filesystem::path directory) {
    auto doc=drawing::DrawingDocument::create_default();kernel::ViewerMesh mesh;
    const kernel::EdgeReference first{"profile","first","assembly/one"},second{"profile","second","assembly/one"},circle{"profile","circle","assembly/one"};
    mesh.edges={{{{0,0,0},{20,0,0}},first},{{{0,0,0},{10,10*std::sqrt(3.),0}},second}};
    kernel::ViewerEdge circular;circular.reference=circle;
    for(int i=0;i<=96;++i){const auto angle=i*2*std::numbers::pi/96;circular.points.push_back({5*std::cos(angle),5*std::sin(angle),0});}mesh.edges.push_back(circular);
    auto view=drawing::DrawingDocument::create_view("source","missing.prtz",mesh,drawing::ViewOrientation::Top);
    view.camera={{1,0,0},{0,1,0},{0,0,1}};
    auto angle=drawing::make_drawing_dimension(view.id,drawing::DrawingDimensionKind::Angular);
    angle.attachments={{drawing::DimensionAttachmentKind::Line,first,{},.5},{drawing::DimensionAttachmentKind::Line,second,{},.5}};
    drawing::refresh_drawing_dimension(view,angle);
    auto broken=drawing::make_drawing_dimension(view.id,drawing::DrawingDimensionKind::Angular);
    broken.attachments=angle.attachments;drawing::refresh_drawing_dimension(view,broken);broken.attachments[1].reference.instance_path="assembly/missing";
    auto tilted=drawing::DrawingDocument::create_view("source","missing.prtz",mesh,drawing::ViewOrientation::Front);tilted.camera={{1,0,0},{0,0,1},{0,1,0}};
    auto hidden=drawing::make_drawing_dimension(tilted.id,drawing::DrawingDimensionKind::Radius);hidden.attachments={{drawing::DimensionAttachmentKind::CurvePoint,circle,{},.5}};
    doc.sheets.front().views={view,tilted};doc.sheets.front().dimensions={angle,broken,hidden};
    workspace::Workspace live;live.add_drawing(doc,directory/"dimensions.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    command_host::Host host(live,kernel,directory);auto* state=live.open_drawing(doc.document_id);
    const auto revision=state->revision(),generation=state->data_generation();const auto cache=state->document().find_view(view.id)->measurement_geometry;
    const auto listed=run(host,"drawing.dimension.list",{{"limit",2}}).data;
    require(listed.at("total")==3&&listed.at("items").size()==2,"Dimension query limit changed total");
    const auto valid=run(host,"drawing.dimension.get",{{"dimension",angle.id}}).data;
    require(valid.at("state")=="resolved"&&std::abs(valid.at("measurements")[0].at("value").get<double>()-60)<1e-6&&valid.at("measurements")[0].at("text")=="60°","Angular query lost exact value or units");
    require(valid.at("attachments")[0].at("reference").at("instance_path")=="assembly/one","Query lost occurrence reference");
    const auto invalid=run(host,"drawing.dimension.get",{{"dimension",broken.id}}).data;
    require(invalid.at("state")=="unresolved"&&invalid.at("measurements")[0].at("last_valid")==true&&invalid.at("measurements")[0].at("text")=="60°"&&!invalid.at("resolved_attachments")[1].get<bool>(),"Broken query lost last value or reported a valid reference");
    const auto concealed=run(host,"drawing.dimension.get",{{"dimension",hidden.id}}).data;
    require(concealed.at("state")=="hidden"&&concealed.at("measurements").empty(),"Hidden radial projection reported a visible measurement");
    require(state->revision()==revision&&state->data_generation()==generation&&state->document().find_view(view.id)->measurement_geometry==cache,"Read query changed document or geometry");
    require(run(host,"drawing.dimension.list",{{"view",view.id}}).data.at("total")==2,"View filter lost dimensions");
    require(!host.execute({{"command","drawing.dimension.delete"},{"arguments",{{"dimension","missing"}}}}).ok&&state->revision()==revision,"Invalid delete changed history");
    const auto allocations=state->document().dimension_identifiers.allocation_count();
    run(host,"drawing.dimension.delete",{{"dimension",broken.id}});
    require(state->document().sheets.front().dimensions.size()==2&&state->document().sheets.front().dimensions.front()==angle&&state->document().dimension_identifiers.allocation_count()==allocations,"Delete affected another dimension or recycled identifiers");
    run(host,"undo");require(state->document().sheets.front().dimensions[1]==broken,"Undo lost invalid reference, layout or last value");
    run(host,"redo");run(host,"save");const auto loaded=drawing::DrawingDocument::load(directory/"dimensions.drwz");
    require(loaded.sheets.front().dimensions.size()==2&&loaded.sheets.front().dimensions.front()==angle,"Deletion did not persist");
    const auto attach=[](kernel::EdgeReference r,const char* kind="line",double parameter=.5){return Json{{"kind",kind},{"reference",{{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path}}},{"parameter",parameter}};};
    const auto created=run(host,"drawing.dimension.create",{{"view",view.id},{"kind","angular"},{"attachments",Json::array({attach(first),attach(second)})}}).data;
    const auto created_id=created.at("dimension").get<std::string>(),segment_id=created.at("segments")[0].at("segment").get<std::string>();
    require(created.at("measurements")[0].at("text")=="60°","CLI create did not calculate angle");
    const auto sector=run(host,"drawing.dimension.set",{{"dimension",created_id},{"placements",Json::array({Json::array({-10,10})})},{"style",{{"decimals",2},{"prefix","A="}}}}).data;
    require(sector.at("measurements")[0].at("text")=="A=120°"&&sector.at("segments")[0].at("segment")==segment_id,"Angle placement lost sector or stable identity");
    const auto no_op_revision=state->revision();require(run(host,"drawing.dimension.set",{{"dimension",created_id}}).data.at("changed")==false&&state->revision()==no_op_revision,"Empty dimension edit created history");
    const auto before_bad=state->document();auto absent=second;absent.instance_path="assembly/wrong";
    require(!host.execute({{"command","drawing.dimension.set"},{"arguments",{{"dimension",created_id},{"attachments",Json::array({attach(first),attach(absent)})}}}}).ok&&state->revision()==no_op_revision,"Invalid reference edit committed");
    require(!host.execute({{"command","drawing.dimension.set"},{"arguments",{{"dimension",created_id},{"style",{{"decimals",13}}}}}}).ok,"Invalid text precision accepted");
    require(!host.execute({{"command","drawing.dimension.set"},{"arguments",{{"dimension",created_id},{"layouts",Json::array({Json{{"unknown",2}}})}}}}).ok,"Unknown layout field accepted");
    require(!host.execute({{"command","drawing.dimension.create"},{"arguments",{{"view",view.id},{"kind","angular"},{"attachments",Json::array({attach(first),attach(first)})}}}}).ok,"Duplicate angular references accepted");
    require(state->document().sheets.front().dimensions==before_bad.sheets.front().dimensions,"Failed edit modified the document");
    auto broken_doc=state->document();auto& damaged=broken_doc.sheets.front().dimensions.back();damaged.attachments[1].reference=absent;state->commit(std::move(broken_doc));
    const auto repaired=run(host,"drawing.dimension.set",{{"dimension",created_id},{"attachments",Json::array({attach(first),attach(second)})}}).data;
    require(repaired.at("state")=="resolved"&&repaired.at("dimension")==created_id,"Reference repair lost identity");
    const auto chain=run(host,"drawing.dimension.create",{{"view",view.id},{"attachments",Json::array({attach(first,"curve_point",0),attach(first,"curve_point",.5)})}}).data;
    const auto chain_id=chain.at("dimension").get<std::string>(),old_segment=chain.at("segments")[0].at("segment").get<std::string>();
    const auto extended=run(host,"drawing.dimension.extend",{{"dimension",chain_id},{"attachment",attach(first,"curve_point",1)}}).data;
    require(extended.at("kind")=="chain"&&extended.at("segments").size()==2&&extended.at("segments")[0].at("segment")==old_segment,"Appending chain changed existing segment");
    const auto prepended=run(host,"drawing.dimension.extend",{{"dimension",chain_id},{"attachment",attach(second,"curve_point",1)},{"at_first",true}}).data;
    require(prepended.at("segments").size()==3&&prepended.at("segments")[1].at("segment")==old_segment&&prepended.at("anchor_attachment")==1,"Prepending chain lost original segment or anchor");
    run(host,"undo");require(run(host,"drawing.dimension.get",{{"dimension",chain_id}}).data.at("segments").size()==2,"Chain extension Undo failed");
    run(host,"redo");run(host,"save");
    const auto edited=drawing::DrawingDocument::load(directory/"dimensions.drwz");require(edited.sheets.front().dimensions==state->document().sheets.front().dimensions,"Created/edited dimensions did not roundtrip");

}
}
int main(int argc,char** argv) {
    try {
        if(argc==4&&std::string_view(argv[1])=="--compare-authored-parts") {
            compare_authored_parts(std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]));return 0;
        }
        if((argc==5||argc==6)&&std::string_view(argv[1])=="--repair-copied-annotations") {
            require(argc==5||std::string_view(argv[5])=="--local-primary","Unknown annotation repair option");
            repair_copied_annotations(std::filesystem::u8path(argv[2]),argv[3],std::filesystem::u8path(argv[4]),argc==6);return 0;
        }
        verify_surface_contours();kernel::OcctKernel kernel;
        const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());
        const auto dir=root/("zima-dimensions-"+document::PartDocument::create_default().document_id);
        std::filesystem::create_directory(dir);verify(kernel,dir);
        require(dir.parent_path()==root,"Unsafe cleanup");std::filesystem::remove_all(dir);
        std::cout<<"Measured dimension queries, original references, unresolved values, deletion and history passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
