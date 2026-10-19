#include "profile_solid_fixture.hpp"
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <future>
#include <iostream>
#include <chrono>

using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected,const char* message){check(std::abs(value-expected)<1e-9*std::max(1.,std::abs(expected)),message);}
void save(workspace::Workspace& w,const std::string& id,const std::filesystem::path& path) {
    auto job=workspace::prepare_document_save(w,id,path);
    auto result=std::async(std::launch::async,[job=std::move(job)]{return job.write();}).get();
    check(workspace::complete_document_save(w,result),"Save completion failed");
}
}
int main() {try {
    namespace fs=std::filesystem;
    kernel::OcctKernel kernel;
    auto doc=document::PartDocument::create_default();
    doc.history.push_back(test::rectangular_feature(doc,{10,20,30}));
    document::BodyHistoryGraph history;static_cast<void>(history.create_body("Physical solid"));
    history.insert({document::PartHistoryKind::Feature,doc.history.front().id});doc.set_body_history(history);
    auto& sketch=doc.sketches.front();
    const auto edge=sketch.segments.front();
    const auto a=*sketch.find_point(edge.first_point_id),b=*sketch.find_point(edge.second_point_id);
    sketch.segments.erase(sketch.segments.begin());
    static_cast<void>(sketch.add_bspline({{a.x,a.y},{(a.x+b.x)/2,(a.y+b.y)/2},{b.x,b.y}},2));
    doc.physical_parameters["MASS_DENSITY"]="7850";doc.physical_parameter_units["MASS_DENSITY"]="kg/m^3";
    doc.relations="mass = model.mass\nlabel = \"M=\" & mass";
    doc.user_parameters["mass"]="0";doc.user_parameter_values["mass"][""]="0";
    const auto id=doc.document_id;
    const auto dir=fs::temp_directory_path()/("zima-save-physical-"+id);fs::create_directories(dir);
    const auto path=dir/"steel.prtz";
    std::vector<kernel::BodyResult> pending;
    {kernel::AggregatePropertyScope deferred(kernel::AggregatePropertyDemand::Deferred);pending=kernel.evaluate_history(doc.kernel_operations());}
    check(!pending.back().physical_properties_current,"Fixture did not defer physical analysis");
    const auto native_shape=pending.back().kernel_shape;
    const auto fingerprint=pending.back().source_fingerprint;
    const auto packet=document::serialize_body_result(pending.back());
    doc.save(path,pending); // A previously saved stale-zero document.
    workspace::Workspace w;w.add_part(doc,pending,path);w.activate(id);w.display_top_level(id);
    auto& session=w.open_part(id)->session;
    const auto revision=session.revision();
    check(workspace::prepare_document_save_if_needed(w,id,path).has_value(),"Clean stale mass was skipped");
    auto before=session.document();before.name="edited";session.commit(before,pending);
    const auto edit_revision=session.revision();
    const auto save_start=std::chrono::steady_clock::now();
    save(w,id,path);
    const auto save_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-save_start).count();
    check(session.revision()==edit_revision&&!session.is_dirty()&&session.can_undo(),"Save created an edit or discarded Undo");
    check(session.document().user_parameters.at("mass")=="0.047"&&session.document().user_parameters.at("label")=="M=0.047","Save did not publish physical Relations");
    const auto& body=session.calculated_boundaries().back();
    near(body.volume,6000,"Save volume mismatch");near(document::physical_values(session.document(),session.calculated_boundaries()).at("model.mass"),.0471,"Save mass mismatch");
    auto analyzed_packet=document::serialize_body_result(body),original_packet=packet;
    const auto geometry_only=[&](const auto& self,nlohmann::json& value)->void {
        if(value.is_object()) {
            for(const auto* field:{"volume","surface_area","volume_integrals","surface_centroid","solid_present"})value.erase(field);
            for(auto& child:value.items())self(self,child.value());
        } else if(value.is_array())for(auto& child:value)self(self,child);
    };
    geometry_only(geometry_only,analyzed_packet);geometry_only(geometry_only,original_packet);
    check(body.kernel_shape==native_shape&&body.source_fingerprint==fingerprint&&analyzed_packet==original_packet,"Save changed native geometry or references");
    check(w.active_document_id()==id&&w.displayed_document_id()==id,"Save changed editing/display context");
    std::vector<kernel::BodyResult> reopened;
    auto loaded=document::PartDocument::load(path,&reopened);
    check(loaded.user_parameters.at("mass")=="0.047"&&reopened.back().physical_properties_current,"Analysis was not persisted");
    const auto generation=session.data_generation();const auto timestamp=fs::last_write_time(path);
    const auto no_op_start=std::chrono::steady_clock::now();
    for(int repeat=0;repeat<50;++repeat)check(!workspace::prepare_document_save_if_needed(w,id,path),"Unchanged Save repeated physical work");
    const auto no_op_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-no_op_start).count()/50;
    check(session.data_generation()==generation&&fs::last_write_time(path)==timestamp,"No-op Save changed state or file");
    check(session.undo()&&session.revision()==revision&&session.document().name==doc.name,"Save replaced the Undo stack");
    save(w,id,path);check(session.can_redo(),"Analysis discarded Redo");
    check(session.redo()&&session.document().name=="edited","Redo failed after Save analysis");
    // Activation is focus, not a revision. Completion may merge analysis but
    // must retain the current focus even when it differs from the job snapshot.
    before=session.document();before.user_parameters["mass"]="0";
    session.commit(before,pending);
    const auto active_body=session.document().body_history.active_body_id();
    auto focus_job=workspace::prepare_document_save(w,id,path);
    session.activate_body({});
    check(workspace::complete_document_save(w,focus_job.write()),"Focus-only save completion failed");
    check(session.document().body_history.active_body_id().empty()&&session.document().user_parameters.at("mass")=="0.047","Save restored old Body activation");
    session.activate_body(active_body);
    // A completed background save must never overwrite a newer edit.
    auto job=workspace::prepare_document_save(w,id,path);
    before=session.document();before.physical_parameters["MASS_DENSITY"]="2700";
    session.commit(before,session.calculated_boundaries());
    auto receipt=job.write();check(workspace::complete_document_save(w,receipt)&&session.is_dirty(),"Older save cleared a material edit");
    check(session.document().physical_parameters.at("MASS_DENSITY")=="2700","Older analysis replaced a material edit");
    save(w,id,path);check(session.document().user_parameters.at("mass")=="0.016","Material-only Save retained stale mass");
    // Independent Save As computes the copy without changing the original.
    before=session.document();before.physical_parameters["MASS_DENSITY"]="7850";
    session.commit(before,pending);const auto original_generation=session.data_generation();
    const auto copy=dir/"copy.prtz";static_cast<void>(w.save_copy(id,copy));
    loaded=document::PartDocument::load(copy,&reopened);
    check(loaded.document_id!=id&&loaded.user_parameters.at("mass")=="0.047"&&reopened.back().physical_properties_current,"Save As omitted physical analysis");
    check(session.data_generation()==original_generation&&session.document().user_parameters.at("mass")=="0.016","Save As changed its source");
    // Repeated, hidden, suppressed and nested components, with mixed materials.
    auto aluminum=doc;aluminum.document_id=document::PartDocument::create_default().document_id;
    aluminum.physical_parameters["MASS_DENSITY"]="2700";
    const auto aluminum_path=dir/"aluminum.prtz";aluminum.save(aluminum_path,pending);w.add_part(aluminum,pending,aluminum_path);
    auto nested=assembly::AssemblyDocument::create_default();nested.document_units["Mass"]="g";nested.relations="mass = model.mass";
    auto steel=assembly::AssemblyDocument::create_part_occurrence("steel",id,path,pending.back());steel.density_kg_mm3=7.85e-6;
    auto alloy=assembly::AssemblyDocument::create_part_occurrence("alloy",aluminum.document_id,aluminum_path,pending.back());alloy.density_kg_mm3=2.7e-6;
    nested.components={steel,alloy};const auto nested_path=dir/"nested.asmz";nested.save(nested_path);w.add_assembly(nested,nested_path);
    auto group=assembly::AssemblyDocument::create_default();group.relations="mass = model.mass";group.user_parameters["mass"]="0";
    auto first=assembly::AssemblyDocument::create_assembly_occurrence("nested",nested.document_id,nested_path,nested);
    auto repeated=assembly::AssemblyDocument::create_assembly_occurrence("again",nested.document_id,nested_path,nested);repeated.visible=false;
    auto ignored=assembly::AssemblyDocument::create_part_occurrence("suppressed",id,path,pending.back());ignored.suppressed=true;
    group.components={first,repeated,ignored};const auto group_id=group.document_id;const auto group_path=dir/"assembly.asmz";w.add_assembly(group,group_path);
    save(w,group_id,group_path);
    const auto& assembled=w.open_assembly(group_id)->session.document();
    near(assembly::physical_values(assembled).at("model.mass"),2*(.0471+.0162),"Nested/repeated/suppression mass mismatch");
    check(assembled.user_parameters.at("mass")=="0.127","Assembly Save did not publish its parameter");
    for(std::size_t i=0;i<group.components.size();++i)
        check(assembled.components[i].placement==group.components[i].placement&&assembled.components[i].occurrence_id==group.components[i].occurrence_id,"Save solved or replaced a component");
    check(!workspace::prepare_document_save_if_needed(w,group_id,group_path),"Unchanged Assembly Save repeated work");
    // Missing density must remain unavailable, rather than becoming zero.
    auto& current=w.open_part(id)->session;
    before=current.document();before.physical_parameters.clear();current.commit(before,current.calculated_boundaries());save(w,id,path);
    check(current.document().user_parameters.at("mass").empty(),"Unknown material produced a false zero mass");
    // Explicit Regenerate still publishes physical results through the full path.
    before=current.document();before.physical_parameters["MASS_DENSITY"]="7850";current.commit(before,current.calculated_boundaries());
    static_cast<void>(workspace::regenerate_part(w,kernel,id));
    check(w.open_part(id)->session.document().user_parameters.at("mass")=="0.047","Regenerate no longer publishes mass");
    auto& units=w.open_part(id)->session;
    before=units.document();before.document_units["Mass"]="g";units.commit(before,units.calculated_boundaries());save(w,id,path);
    check(units.document().user_parameters.at("mass")=="47.1","Save used the wrong mass unit");
    // An edited open source is authoritative even without a preceding scene
    // refresh, source Save or Assembly regeneration.
    before=units.document();test::resize_rectangular_feature(before,before.history.front(),{20,20,30});
    const auto expanded=kernel.evaluate_history(before.kernel_operations());units.commit(before,expanded);
    save(w,group_id,group_path);
    near(assembly::physical_values(w.open_assembly(group_id)->session.document()).at("model.mass"),.2208,"Assembly Save ignored current calculated open-source geometry");
    check(w.open_part(id)->session.document().user_parameters.at("mass")=="94.2","Assembly Save did not refresh the source mass in grams");
    {
        auto family=document::PartDocument::create_default();family.name="Physical family";
        family.history={test::rectangular_feature(family,{10,20,30})};
        family.physical_parameters=doc.physical_parameters;family.physical_parameter_units=doc.physical_parameter_units;
        family.relations="mass = model.mass";family.user_parameters["mass"]="0";
        const auto family_id=family.document_id;const auto family_path=dir/"family.prtz";
        workspace::Workspace family_live;
        family_live.add_part(family,kernel.evaluate_history(family.kernel_operations()),family_path);
        document::FamilyTable table;table.instances={{"A",{}},{"B",{}}};
        static_cast<void>(workspace::set_family_table(family_live,family_id,table));
        const auto member=workspace::open_family_instance(family_live,kernel,family_id,"A");
        const auto closed=workspace::open_family_instance(family_live,kernel,family_id,"B");
        static_cast<void>(family_live.remove(closed));
        const auto member_revision=family_live.open_part(member)->session.revision();
        save(family_live,member,family_path);
        check(family_live.open_part(member)->session.document().user_parameters.at("mass")=="0.047","Family member Save did not publish physical Relations");
        check(family_live.open_part(member)->session.revision()==member_revision,"Family Save created an edit");
        auto saved_family=document::PartDocument::load(family_path);
        for(const auto& [row,packet]:saved_family.family.evaluated) {
            std::vector<kernel::BodyResult> cache;const auto saved_member=document::PartDocument::from_serialized(*packet,&cache);
            check(saved_member.user_parameters.at("mass")=="0.047"&&cache.back().physical_properties_current,"Family Save retained stale embedded variant properties");
        }
        check(!workspace::prepare_document_save_if_needed(family_live,member,family_path),"Unchanged Family Save repeated work");
        auto assembly_family=assembly::AssemblyDocument::create_default();assembly_family.relations="mass = model.mass";
        assembly_family.user_parameters["mass"]="0";
        const auto shape=family_live.open_part(family_id)->session.calculated_boundaries().back();
        auto first=assembly::AssemblyDocument::create_part_occurrence("Generic",family_id,family_path,shape);
        first.density_kg_mm3=7.85e-6;
        auto second=assembly::AssemblyDocument::create_part_occurrence("Member",member,family_path,shape);second.density_kg_mm3=7.85e-6;
        assembly_family.components={first,second};const auto assembly_family_id=assembly_family.document_id;
        const auto assembly_family_path=dir/"family.asmz";family_live.add_assembly(assembly_family,assembly_family_path);
        table={};table.columns={"Member"};table.bindings["Member"]={"component",second.occurrence_id,{}};
        table.instances={{"Both",{{"Member","yes"}}},{"Single",{{"Member","no"}}}};
        static_cast<void>(workspace::set_family_table(family_live,assembly_family_id,table));
        const auto assembly_member=workspace::open_family_instance(family_live,kernel,assembly_family_id,"Both");
        const auto assembly_closed=workspace::open_family_instance(family_live,kernel,assembly_family_id,"Single");
        static_cast<void>(family_live.remove(assembly_closed));
        save(family_live,assembly_member,assembly_family_path);
        check(family_live.open_assembly(assembly_member)->session.document().user_parameters.at("mass")=="0.094","Assembly Family tab retained stale mass");
        const auto saved_assembly=assembly::AssemblyDocument::load(assembly_family_path,workspace::native_source_resolver(family_live));
        for(const auto& [row,packet]:saved_assembly.family.evaluated) {
            const auto member=assembly::AssemblyDocument::from_serialized(*packet);
            check(member.user_parameters.at("mass")== (member.document_id==assembly_member?"0.094":"0.047"),"Assembly Family embedded mass ignored its component presence");
        }
        check(!workspace::prepare_document_save_if_needed(family_live,assembly_member,assembly_family_path),"Unchanged Assembly Family Save repeated work");
        auto cut_family=assembly::AssemblyDocument::create_default();cut_family.relations="mass = model.mass";
        cut_family.user_parameters["mass"]="0";
        first=assembly::AssemblyDocument::create_part_occurrence("Cut",family_id,family_path,shape);first.density_kg_mm3=7.85e-6;
        second=assembly::AssemblyDocument::create_part_occurrence("Uncut",family_id,family_path,shape);second.density_kg_mm3=7.85e-6;
        cut_family.components={first,second};const auto cut_family_id=cut_family.document_id;const auto cut_family_path=dir/"cut-family.asmz";
        family_live.add_assembly(cut_family,cut_family_path);
        auto cutter=sketcher::Sketch::create_default();static_cast<void>(cutter.add_rectangle(-2,-10,2,10));
        auto cut=document::PartDocument::create_extrusion_container(cutter.id);cutter.owner_container_id=cut.id;
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.extent_mode=document::ProfileExtentMode::Symmetric;cut.extrusion.length_forward=5;
        workspace::commit_assembly_profile(family_live,kernel,cut_family_id,cut,{first.occurrence_id},workspace::ProfileEditMode::Create,cutter);
        table={};table.columns={"Cut"};table.bindings["Cut"]={"feature",cut.id,{}};
        table.instances={{"Cut",{{"Cut","yes"}}},{"Uncut",{{"Cut","no"}}}};
        static_cast<void>(workspace::set_family_table(family_live,cut_family_id,table));
        const auto cut_member=workspace::open_family_instance(family_live,kernel,cut_family_id,"Cut");
        const auto uncut_member=workspace::open_family_instance(family_live,kernel,cut_family_id,"Uncut");
        const auto cut_shape=family_live.open_assembly(cut_member)->session.document().components.front().calculated_source->kernel_shape;
        save(family_live,cut_member,cut_family_path);
        check(family_live.open_assembly(cut_member)->session.document().user_parameters.at("mass")=="0.088","Cut Family mass used the wrong source geometry");
        check(family_live.open_assembly(uncut_member)->session.document().user_parameters.at("mass")=="0.094","Uncut Family row inherited another row's cut geometry");
        check(family_live.open_assembly(cut_member)->session.document().components.front().calculated_source->kernel_shape==cut_shape,"Family Save changed an Assembly-owned cut");
        check(!workspace::prepare_document_save_if_needed(family_live,cut_member,cut_family_path),"Unchanged cut Family Save repeated work");
        // With every generic occurrence cut, its uncut Family row must resolve
        // the current native source rather than copy any cut occurrence.
        const auto both_cut=family_live.open_assembly(cut_family_id)->session.document().find_cut(cut.id)->definition;
        workspace::commit_assembly_profile(family_live,kernel,cut_family_id,both_cut,{first.occurrence_id,second.occurrence_id},workspace::ProfileEditMode::Replace);
        save(family_live,cut_member,cut_family_path);
        check(family_live.open_assembly(cut_member)->session.document().user_parameters.at("mass")=="0.082","All-cut Family mass mismatch");
        check(family_live.open_assembly(uncut_member)->session.document().user_parameters.at("mass")=="0.094","All-cut generic replaced uncut variant source");
        const auto family_no_op_start=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<50;++repeat)check(!workspace::prepare_document_save_if_needed(family_live,uncut_member,cut_family_path),"All-cut Family repeated an unchanged Save");
        std::cout<<"All-cut Family unchanged Save check mean: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-family_no_op_start).count()/50<<" ms (50 checks)\n";
    }
    std::cout<<"Deferred 6000 mm3 Part save: "<<save_ms<<" ms; unchanged Save check mean: "<<no_op_ms<<" ms (50 checks)\n";
    fs::remove_all(dir);
    std::cout<<"Save/Save As physical analysis, Relations, no-op, concurrency, Undo/Redo, nested mass and Regenerate passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
