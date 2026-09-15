#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/file_path.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <stdexcept>

namespace zima::workspace {
namespace {
using document::FeatureKind;
using document::FamilyColumn;
using Slots=std::map<std::string,double*>;
Slots feature_slots(document::HistoryContainer& f) {
    switch(f.feature_kind) {
    case FeatureKind::Extrusion: return {{"length_forward",&f.extrusion.length_forward},{"length_reverse",&f.extrusion.length_reverse},{"profile_offset",&f.extrusion.profile_plane_offset},{"thin_thickness",&f.extrusion.thin_thickness}};
    case FeatureKind::Revolution: return {{"angle",&f.revolution.angle_degrees},{"length_reverse",&f.revolution.angle_reverse},{"profile_offset",&f.revolution.profile_plane_offset},{"thin_thickness",&f.revolution.thin_thickness}};
    case FeatureKind::Fillet: return {{"primary",&f.edge_treatment.primary_size},{"secondary",&f.edge_treatment.secondary_size}};
    case FeatureKind::Chamfer: return {{"primary",&f.edge_treatment.primary_size},{"secondary",&f.edge_treatment.secondary_size},{"treatment_angle",&f.edge_treatment.angle_degrees}};
    case FeatureKind::Shell: return {{"thickness",&f.shell.thickness}};
    case FeatureKind::Holes: return {{"diameter",&f.holes.diameter}};
    case FeatureKind::Hole: return {{"diameter",&f.hole.diameter},{"bore_length",&f.hole.bore_length},{"entrance_chamfer",&f.hole.entrance_chamfer},{"exit_chamfer",&f.hole.exit_chamfer},{"drill_point_angle",&f.hole.drill_point_angle_degrees},{"thread_diameter",&f.hole.thread_nominal_diameter},{"thread_pitch",&f.hole.thread_pitch},{"thread_length",&f.hole.thread_length}};
    case FeatureKind::Thread: return {{"bore_diameter",&f.thread.nominal_diameter},{"bore_length",&f.thread.bore_length},{"thread_length",&f.thread.length_forward},{"length_reverse",&f.thread.length_reverse},{"chamfer_depth",&f.thread.chamfer_depth},{"chamfer_angle",&f.thread.chamfer_angle_degrees},{"drill_point_angle",&f.hole.drill_point_angle_degrees}};
    case FeatureKind::ShaftThread: return {{"root_diameter",&f.shaft_thread.root_diameter},{"length",&f.shaft_thread.length}};
    case FeatureKind::Sweep2D: return {{"thickness",&f.sweep2d.thickness}};
    case FeatureKind::Sweep3D: return {{"thickness",&f.sweep3d.thickness}};
    case FeatureKind::HelicalSweep: return {{"pitch",&f.helical.pitch}};
    case FeatureKind::DrillPoint: return {{"angle",&f.drill_point.included_angle_degrees}};
    default: return {};
    }
}
std::string number(double value) { return kernel::dimension_number(value,12); }
bool has_feature_solid(const document::HistoryContainer& f) { return f.feature_kind!=FeatureKind::Sketch; }
template<class Doc> void add_feature_references(std::vector<FamilyReference>& out,const Doc& doc,document::HistoryContainer f) {
    if(has_feature_solid(f))out.push_back({{"feature",f.id,{}},f.name,f.name,f.suppressed?"no":"yes"});
    const auto add=[&](const std::string& key,double value) {
        if(f.value_locks.contains(key))return;
        const auto semantic="parameter:"+key;
        const auto name=doc.dimension_identifiers.identifier(f.id,semantic);
        if(!name.empty())out.push_back({{"dimension",f.id,semantic},name,f.name,number(value)});
    };
    if(primitive_definition(f.feature_kind))for(const auto& [key,value]:primitive_dimensions(f))add(key,value);
    else for(const auto& [key,value]:feature_slots(f))add(key,*value);
    if(f.feature_kind==FeatureKind::Sketch || f.feature_kind==FeatureKind::Holes)
        for(const auto& sketch:doc.sketches)if(sketch.owner_container_id==f.id)add("profile_offset",sketch.plane_offset);
}
template<class Doc> void add_sketch_references(std::vector<FamilyReference>& out,const Doc& doc,const sketcher::Sketch& sketch) {
    for(const auto& dimension:sketch.dimensions) {
        const auto key="dimension:"+dimension.id;
        const auto name=doc.dimension_identifiers.identifier(sketch.id,key);
        if(!name.empty() && dimension.driving && !dimension.suppressed && !dimension.locked)out.push_back({{"dimension",sketch.id,key},name,sketch.name,number(dimension.value)});
    }
    for(const auto& radius:sketch.corner_radii) {
        const auto key="corner_dimension:"+radius.id;
        const auto name=doc.dimension_identifiers.identifier(sketch.id,key);
        if(!name.empty())out.push_back({{"dimension",sketch.id,key},name,sketch.name,number(radius.radius)});
    }
}
void assign_feature(document::HistoryContainer& f,const std::string& key,double value) {
    if(f.value_locks.contains(key))throw std::invalid_argument("The family dimension is locked.");
    if(primitive_definition(f.feature_kind)){assign_primitive_dimensions(f,{{key,value}});return;}
    auto slots=feature_slots(f);const auto found=slots.find(key);
    if(found==slots.end())throw std::invalid_argument("The family dimension is not editable.");
    if(key!="profile_offset" && value<=0)throw std::invalid_argument("Family feature dimensions must be positive.");
    if((f.feature_kind==FeatureKind::Revolution&&(key=="angle"||key=="length_reverse")&&value>360) ||
        (key=="treatment_angle"&&value>=90) || ((key=="drill_point_angle"||key=="chamfer_angle")&&value>=180))
        throw std::invalid_argument("The family angle is outside its allowed range.");
    *found->second=value;
}
bool assign_sketch(sketcher::Sketch& sketch,const FamilyColumn& binding,double value) {
    if(sketch.id!=binding.owner_id)return false;
    if(binding.semantic_key.starts_with("dimension:")) {
        if(!sketch.set_dimension_value(binding.semantic_key.substr(10),value))throw std::invalid_argument("The family Sketch dimension cannot be solved.");
    } else if(binding.semantic_key.starts_with("corner_dimension:")) {
        const auto found=std::ranges::find(sketch.corner_radii,binding.semantic_key.substr(17),&sketcher::SketchCornerRadius::id);
        if(found==sketch.corner_radii.end()||value<=0)throw std::invalid_argument("Invalid family corner radius.");
        static_cast<void>(sketch.add_corner_fillet(found->first_segment_id,found->second_segment_id,value));
    } else return false;
    return true;
}
template<class Doc> bool assign_dimension(Doc& doc,const FamilyColumn& binding,double value) {
    if constexpr(requires {doc.history;}) {
        for(auto& f:doc.history)if(f.id==binding.owner_id && binding.semantic_key.starts_with("parameter:")) {
            const auto key=binding.semantic_key.substr(10);
            if(key=="profile_offset" && (f.feature_kind==FeatureKind::Sketch||f.feature_kind==FeatureKind::Holes)) {
                for(auto& sketch:doc.sketches)if(sketch.owner_container_id==f.id){sketch.plane_offset=value;return true;}
                throw std::invalid_argument("The family profile Sketch is missing.");
            }
            assign_feature(f,key,value);
            if(key=="profile_offset")for(auto& sketch:doc.sketches)if(sketch.owner_container_id==f.id)sketch.plane_offset=value;
            return true;
        }
    } else {
        for(auto& cut:doc.cuts)if(cut.definition.id==binding.owner_id&&binding.semantic_key.starts_with("parameter:")) {
            assign_feature(cut.definition,binding.semantic_key.substr(10),value);return true;
        }
    }
    for(auto& sketch:doc.sketches)if(assign_sketch(sketch,binding,value))return true;
    bool changed=false;
    if constexpr(requires {doc.history;})for(auto& f:doc.history)document::visit_feature_sketches(f,[&](auto& serialized,std::size_t){
        auto sketch=sketcher::Sketch::from_serialized(serialized);
        if(assign_sketch(sketch,binding,value)){serialized=sketch.serialized();changed=true;}
    });
    return changed;
}
void presence(document::PartDocument& doc,const FamilyColumn& binding,bool present) {
    if(binding.kind=="feature") {
        auto* f=doc.find_container(binding.owner_id);if(!f)throw std::invalid_argument("Family feature no longer exists.");f->suppressed=!present;return;
    }
    if(binding.kind=="body") {
        const auto* body=doc.body_history.find(binding.owner_id);
        if(!body||body->derived_copy)throw std::invalid_argument("Family Body no longer exists.");
        if(!present)for(const auto& entry:body->entries)if(auto* f=doc.find_container(entry.id))f->suppressed=true;
        return;
    }
    throw std::invalid_argument("Invalid Part family presence reference.");
}
void presence(assembly::AssemblyDocument& doc,const FamilyColumn& binding,bool present) {
    if(binding.kind=="component") {auto* c=doc.find_occurrence(binding.owner_id);if(!c)throw std::invalid_argument("Family component no longer exists.");c->suppressed=!present;return;}
    if(binding.kind=="feature")for(auto& cut:doc.cuts)if(cut.definition.id==binding.owner_id){cut.definition.suppressed=!present;return;}
    throw std::invalid_argument("Invalid Assembly family presence reference.");
}
template<class Doc> void apply(Doc& doc,const document::FamilyTable& table,const document::FamilyInstance& row) {
    // Dimension values are applied before presence, independent of column order.
    for(const auto& [name,binding]:table.bindings)if(binding.kind=="dimension") {
        const auto value=row.values.find(name);if(value==row.values.end()||value->second.empty())continue;
        if(!assign_dimension(doc,binding,std::stod(value->second)))throw std::invalid_argument("Family dimension no longer exists.");
    }
    for(const auto& [name,binding]:table.bindings)if(binding.kind!="dimension") {
        const auto value=row.values.find(name);if(value!=row.values.end()&&!value->second.empty())presence(doc,binding,value->second=="yes");
    }
    doc.name=row.name;doc.family_table=document::serialize_family_table({});
    doc.user_parameters["name"]=row.name;doc.user_parameter_values["name"][""]=row.name;
}
struct TemporaryNativeCopy {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/document::PartDocument::create_default().document_id;
    TemporaryNativeCopy(){std::filesystem::create_directory(directory);}
    ~TemporaryNativeCopy(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
}
std::vector<FamilyReference> family_references(const Workspace& live,const std::string& id) {
    std::vector<FamilyReference> out;
    if(const auto* state=live.open_part(id)) {
        const auto& doc=state->session.document();
        for(const auto& body:doc.body_history.bodies())if(!body.derived_copy) {
            bool present=false;for(const auto& entry:body.entries)if(const auto* f=doc.find_container(entry.id);f&&!f->suppressed)present=true;
            out.push_back({{"body",body.scope.id,{}},body.name,body.name,present?"yes":"no"});
        }
        for(const auto& f:doc.history) {
            add_feature_references(out,doc,f);
            document::visit_feature_sketches(f,[&](const auto& serialized,std::size_t){add_sketch_references(out,doc,sketcher::Sketch::from_serialized(serialized));});
        }
        for(const auto& sketch:doc.sketches)add_sketch_references(out,doc,sketch);
    } else if(const auto* state=live.open_assembly(id)) {
        const auto& doc=state->session.document();
        for(const auto& c:doc.components)out.push_back({{"component",c.occurrence_id,{}},c.name,c.name,c.suppressed?"no":"yes"});
        for(const auto& cut:doc.cuts)add_feature_references(out,doc,cut.definition);
        for(const auto& sketch:doc.sketches)add_sketch_references(out,doc,sketch);
    } else throw std::invalid_argument("Family Table requires a Part or Assembly.");
    std::set<std::pair<std::string,std::string>> seen;
    std::erase_if(out,[&](const auto& r){return !seen.emplace(r.binding.owner_id,r.binding.semantic_key).second;});
    return out;
}
void validate_family_references(const Workspace& live,const std::string& id,const document::FamilyTable& table) {
    const auto references=family_references(live,id);
    for(const auto& [name,binding]:table.bindings)if(std::ranges::none_of(references,[&](const auto& r){return r.binding==binding;}))
        throw std::invalid_argument("A family column references a missing or unsupported model parameter: "+name);
    for(const auto& [name,b]:table.bindings)if(b.kind=="body")if(const auto* state=live.open_part(id))
        for(const auto& [other,child]:table.bindings)if(child.kind=="feature")
            if(const auto* body=state->session.document().body_history.owner(child.owner_id);body&&body->scope.id==b.owner_id)
                throw std::invalid_argument("Choose either Body presence or its feature presence, not both.");
}
std::string open_family_instance(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,const std::string& name) {
    const auto table=family_table(live,id);validate_family_references(live,id,table);
    const auto row=std::ranges::find(table.instances,name,&document::FamilyInstance::name);
    if(row==table.instances.end()||row->id.empty())throw std::invalid_argument("Family instance no longer exists.");
    const auto instance_id=id+":family:"+row->id;
    const auto generation=live.open_part(id)?live.open_part(id)->session.data_generation():live.open_assembly(id)->session.data_generation();
    const auto reusable=[&](const auto* existing) {
        if(!existing)return false;
        if(existing->family_generation&&existing->family_generation->first==generation)return true;
        if(!existing->family_generation||existing->family_generation->second!=existing->session.data_generation())
            throw std::invalid_argument("The instance has its own edits. Close its tab before generating the updated family row.");
        return false;
    };
    if(reusable(live.open_part(instance_id))||reusable(live.open_assembly(instance_id))) {
        live.display_top_level(instance_id);live.activate(instance_id);return instance_id;
    }
    TemporaryNativeCopy temporary;
    if(const auto* source=live.open_part(id)) {
        const auto file=temporary.directory/"instance.prtz";
        // The existing native copy codec remaps only document-owned identity and
        // rebases external paths. This file is disposable staging, never required storage.
        source->session.document().save(file,{}, {instance_id,source->path,file});
        auto next=document::PartDocument::load(file);apply(next,table,*row);
        auto boundaries=calculate_part_with_resolved_references(kernel,next,nullptr,{true});
        // Reference resolution can normalize signed zero without changing C++
        // value equality. Persist a cache for the exact final parameter bytes.
        const auto operations=next.kernel_operations(false,true);
        bool exact=boundaries.size()==operations.size();
        for(std::size_t i=0;exact&&i<boundaries.size();++i)exact=boundaries[i].source_fingerprint==kernel::history_fingerprint(operations,i+1);
        if(!exact)boundaries=calculate_part(kernel,next,&boundaries,{true});
        document::refresh_physical_relations(next,document::physical_values(next,boundaries));
        if(auto* existing=live.open_part(instance_id))existing->session.commit(std::move(next),std::move(boundaries));
        else live.add_part(std::move(next),std::move(boundaries));
        auto* generated=live.open_part(instance_id);generated->family_generation={{generation,generated->session.data_generation()}};
    } else if(const auto* source=live.open_assembly(id)) {
        const auto file=temporary.directory/"instance.asmz";
        source->session.document().save(file,{instance_id,source->path,file});
        auto next=assembly::AssemblyDocument::load(file);apply(next,table,*row);
        next.resolve_constructions();calculate_resolved_assembly_cuts(kernel,next);
        if(auto* existing=live.open_assembly(instance_id))existing->session.commit(std::move(next));
        else live.add_assembly(std::move(next));
        auto* generated=live.open_assembly(instance_id);generated->family_generation={{generation,generated->session.data_generation()}};
    } else throw std::invalid_argument("Family Table requires a Part or Assembly.");
    live.display_top_level(instance_id);live.activate(instance_id);return instance_id;
}
void select_family_drawing_source(drawing::DrawingDocument& drawing,const Workspace& live,
    const std::string& source,const std::filesystem::path& drawing_path) {
    const auto generic=drawing.source_document_id.substr(0,drawing.source_document_id.find(":family:"));
    if(source!=generic&&!source.starts_with(generic+":family:"))throw std::invalid_argument("The model does not belong to this Drawing family.");
    std::filesystem::path path;std::string name;
    if(const auto* part=live.open_part(source)){path=part->path;name=part->session.document().name;}
    else if(const auto* assembly=live.open_assembly(source)){path=assembly->path;name=assembly->session.document().name;}
    else throw std::invalid_argument("Open the family instance before selecting it in the Drawing.");
    if(path.empty()||!std::filesystem::is_regular_file(path))throw std::invalid_argument("Save the family instance before using it as a Drawing source.");
    auto next=drawing;const auto old=next.source_document_id;
    next.source_document_id=source;next.source_path=path;next.source_name=name;
    for(auto& sheet:next.sheets)for(auto& view:sheet.views)if(view.source_document_id==old){view.source_document_id=source;view.source_path=path;}
    static_cast<void>(regenerate_drawing_views(next,&live,drawing_path));
    drawing=std::move(next);
}
}
