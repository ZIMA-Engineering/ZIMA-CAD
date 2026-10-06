#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_label_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include "drawing_label_test_support.hpp"
#include <iostream>
#include <limits>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;const auto doc=drawing_label_test::fixture();const auto original=doc.sheets.front().views.front();
    const auto view_id=original.id,marker=original.section_markers.front().id;live.add_drawing(doc,dir/"labels.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    bool editing=false;command_host::Options options;options.interaction=[&]{command_host::Interaction i;i.editing=editing;return i;};
    command_host::Host host(live,kernel,dir,options);auto* state=live.open_drawing(doc.document_id);
    const auto run=[&](const char* name,Json args=Json::object()){auto r=host.execute({{"command",name},{"arguments",std::move(args)}});if(!r.ok)throw std::runtime_error(r.code+": "+r.message);return r.data;};
    const auto get=[&]{return run("drawing.view.labels.get",{{"view",view_id}});};
    const auto set=[&](Json values){return run("drawing.view.labels.set",{{"view",view_id},{"values",std::move(values)}});};
    const auto rev=state->revision();const auto initial=get();require(initial.at("caption_position_mm").is_null()&&initial.at("markers")[0].at("displayable")==true&&state->revision()==rev,"Query mutated the drawing or lost trace");
    require(set(Json::object()).at("changed")==false,"Empty patch created history");
    require(set({{"markers",Json::array({{{"section",marker},{"offsets_mm",{0,0}}}})}}).at("changed")==false,"Default offsets pinned unnecessary state");
    const Json values={{"caption_position_mm",{12,-7}},{"section_label_position_mm",{-3,14}},{"markers",Json::array({{{"section",marker},{"offsets_mm",{3,4}}}})}};
    const auto changed=set(values);require(changed.at("changed")==true&&state->revision()==rev+1,"Label batch was not one transaction");
    const auto& current=*state->document().find_view(view_id);
    require(current.caption_position==drawing::Point2{12,-7}&&current.section_label_position==drawing::Point2{-3,14}&&current.section_marker_offsets.at(marker)==std::array<double,2>{3,4},"Paper millimetres were scaled or stored incorrectly");
    const auto trace=drawing::section_trace_layout(current,current.section_markers.front());const auto before=drawing::section_trace_layout(original,original.section_markers.front());
    require(trace&&before&&std::abs(std::hypot(trace->arrow_tips[0].x-before->arrow_tips[0].x,trace->arrow_tips[0].y-before->arrow_tips[0].y)-3)<1e-9&&std::abs(std::hypot(trace->arrow_tips[1].x-before->arrow_tips[1].x,trace->arrow_tips[1].y-before->arrow_tips[1].y)-4)<1e-9,"Trace ends did not move by independent paper distances");
    require(current.x==original.x&&current.y==original.y&&current.scale==2&&current.projected_edges.front().points==original.projected_edges.front().points&&current.measurement_geometry==original.measurement_geometry,"Label edit changed view position or calculated geometry");
    require(!state->document().sheets.front().views[1].caption_position&&state->document().sheets.front().views[1].section_marker_offsets.empty()&&live.documents().size()==1,"Label edit touched another view or loaded a source");
    run("undo");require(get().at("caption_position_mm").is_null()&&get().at("markers")[0].at("offsets_mm").is_null(),"Undo did not restore all label fields");run("redo");
    const auto same=state->revision(),generation=state->data_generation();require(set(values).at("changed")==false&&state->revision()==same&&state->data_generation()==generation,"No-op changed history or cache generation");
    const auto fail=[&](Json args,const std::string& code="invalid_arguments") {
        const auto saved=get();const auto revision=state->revision(),gen=state->data_generation();
        const auto result=host.execute({{"command","drawing.view.labels.set"},{"arguments",std::move(args)}});
        require(!result.ok&&result.code==code,"Invalid label request was not rejected correctly");
        require(get()==saved&&state->revision()==revision&&state->data_generation()==gen,"Rejected label batch partially committed");
    };
    for(const auto& bad:std::vector<Json>{{{"caption_position_mm",{true,2}}},{{"caption_position_mm",{1}}},{{"caption_position_mm",{1,2,3}}},{{"caption_position_mm",{1,std::numeric_limits<double>::infinity()}}},{{"other",1}},{{"markers",Json::object()}},{{"markers",Json::array({{{"section",marker},{"offsets_mm",{1,2}},{"extra",true}}})}}})fail({{"view",view_id},{"values",bad}});
    auto late=values;late["caption_position_mm"]={90,90};late["markers"].push_back({{"section","missing"},{"offsets_mm",{8,9}}});fail({{"view",view_id},{"values",late}},"section_not_found");
    late=values;late["markers"].push_back(late["markers"][0]);fail({{"view",view_id},{"values",late}});
    fail({{"view","missing"},{"values",values}},"view_not_found");
    editing=true;const auto blocked=host.execute({{"command","drawing.view.labels.set"},{"arguments",{{"view",view_id},{"values",values}}}});require(!blocked.ok&&state->revision()==same,"Editing guard allowed a label mutation");get();editing=false;
    set({{"markers",Json::array({{{"section",marker},{"offsets_mm",{-1000,-1000}}}})}});
    const auto& clamped=*state->document().find_view(view_id);const auto narrow=drawing::section_trace_layout(clamped,clamped.section_markers.front());
    require(narrow&&std::abs(std::hypot(narrow->chain.front()[1].x-narrow->chain.front()[0].x,narrow->chain.front()[1].y-narrow->chain.front()[0].y)-1)<1e-8&&clamped.section_marker_offsets.at(marker)[0]<0&&clamped.section_marker_offsets.at(marker)[1]==0,"Both ends crossed or negative shortening was rejected");
    set(values);run("save");const auto loaded=drawing::DrawingDocument::load(dir/"labels.drwz");const auto* saved=loaded.find_view(view_id);
    require(saved&&saved->caption_position==drawing::Point2{12,-7}&&saved->section_label_position==drawing::Point2{-3,14}&&saved->section_marker_offsets.at(marker)==std::array<double,2>{3,4},"Native labels did not survive reopening");
    set({{"caption_position_mm",nullptr},{"section_label_position_mm",nullptr},{"markers",Json::array({{{"section",marker},{"offsets_mm",nullptr}}})}});
    require(get().at("caption_position_mm").is_null()&&get().at("section_label_position_mm").is_null()&&get().at("markers")[0].at("offsets_mm").is_null(),"Reset did not restore automatic labels");run("undo");require(get().at("caption_position_mm")==Json::array({12,-7}),"Reset Undo failed");
    auto unavailable=state->document();unavailable.find_view(view_id)->projected_edges.clear();state->commit(unavailable);
    require(get().at("markers")[0].at("displayable")==false,"Unprojected trace reported available");
    fail({{"view",view_id},{"values",values}},"trace_unavailable");set({{"markers",Json::array({{{"section",marker},{"offsets_mm",nullptr}}})}});
    auto draft=original;
    {
        auto descriptions=doc;auto accepted=original;
        accepted.description_rows={{drawing::ViewDescriptionKind::Text,true,"&document.file_stem",2.5,"#00ff00"},
            {drawing::ViewDescriptionKind::Name,false,"",5,"#ffffff"},{drawing::ViewDescriptionKind::Scale,true,"",3.5,"#ffff00"}};
        workspace::DrawingProjection projection(nullptr,dir/"descriptions.drwz");
        workspace::edit_drawing_view(descriptions,descriptions.sheets.front().id,accepted,false,projection);
        require(projection.source_load_count()==0&&projection.calculated_camera_count()==0&&projection.calculated_interactive_camera_count()==0,
            "Description-only edit loaded an unavailable source or calculated projection");
        require(descriptions.find_view(view_id)->projected_edges==original.projected_edges&&descriptions.find_view(view_id)->measurement_geometry==original.measurement_geometry,
            "Description-only edit changed reference geometry");
        descriptions.save(dir/"descriptions.drwz");const auto reopened=drawing::DrawingDocument::load(dir/"descriptions.drwz");
        require(reopened.find_view(view_id)->description_rows==accepted.description_rows,"Description order, visibility, text, height or color lost on reopen");
        auto bad=accepted;bad.description_rows[0].height=0;
        try{workspace::edit_drawing_view(descriptions,descriptions.sheets.front().id,bad,false,projection);throw std::logic_error("Invalid description height accepted");}catch(const workspace::DrawingOperationError&){}
        require(descriptions.find_view(view_id)->description_rows==accepted.description_rows,"Invalid description partially committed");
        const auto rows=Json::array({
            {{"kind","text"},{"visible",true},{"text","&document.file_stem"},{"height_mm",2.5},{"color","#00ff00"}},
            {{"kind","name"},{"visible",false},{"height_mm",5},{"color","#ffffff"}},
            {{"kind","scale"},{"visible",true},{"height_mm",3.5},{"color","#ffff00"}}});
        const auto before=state->revision();run("drawing.view.set",{{"view",view_id},{"description_rows",rows}});
        require(state->revision()==before+1&&state->document().find_view(view_id)->description_rows==accepted.description_rows,"CLI description edit did not commit one atomic definition");
        const auto generation=state->data_generation();
        require(run("drawing.view.set",{{"view",view_id},{"description_rows",rows}}).at("changed")==false&&state->revision()==before+1&&state->data_generation()==generation,"Unchanged CLI descriptions created history or invalidated geometry");
        run("undo");require(state->document().find_view(view_id)->description_rows==original.description_rows,"Description Undo failed");
        run("redo");require(state->document().find_view(view_id)->description_rows==accepted.description_rows,"Description Redo failed");
        auto invalid=rows;invalid[0]["height_mm"]=true;const auto revision=state->revision();
        const auto rejected=host.execute({{"command","drawing.view.set"},{"arguments",{{"view",view_id},{"description_rows",invalid}}}});
        require(!rejected.ok&&rejected.code=="invalid_arguments"&&state->revision()==revision,"Invalid CLI description partially committed");
    }
    try{workspace::set_drawing_section_end(draft,marker,2,4,-10);throw std::logic_error("Bad end accepted");}catch(const workspace::DrawingOperationError&){}
    try{workspace::set_drawing_label_position(draft,workspace::DrawingLabel::Caption,drawing::Point2{0,std::numeric_limits<double>::quiet_NaN()});throw std::logic_error("NaN position accepted");}catch(const workspace::DrawingOperationError&){}
    require(!draft.caption_position&&draft.section_marker_offsets.empty(),"Invalid shared operation changed GUI draft");
    draft.section_markers.push_back(draft.section_markers.front());try{workspace::reset_drawing_section_ends(draft,marker);throw std::logic_error("Ambiguous marker accepted");}catch(const workspace::DrawingOperationError& e){require(e.code=="ambiguous_reference","Wrong ambiguity error");}
}
}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-labels-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing labels, paper units, bounded Section ends, atomic edits, Undo/Redo and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
