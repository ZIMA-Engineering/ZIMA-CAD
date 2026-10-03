#include "group_solid_state_calculation.hpp"
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <numbers>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/pattern_dimensions.hpp>
#include <zima/document/document_copy_json.hpp>
#include <zima/document/file_path.hpp>
#include <zima/document/physical_properties.hpp>
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
    case FeatureKind::Feature: {
        Slots result{{"profile_offset",&f.feature.profile_plane_offset}};
        if(f.feature.result_type==document::ProfileResultType::Thin)result.emplace("thin_thickness",&f.feature.thin_thickness);
        for(std::size_t i=0;i<2;++i) {
            if(i==1&&f.feature.symmetric)continue;
            const auto side=f.feature.effective_side(i);const auto prefix="side"+std::to_string(i);
            if(side.operation==document::FeatureSideOperation::Extrusion&&side.extrusion_extent==document::EndCondition::Length)result.emplace(prefix+"_length",&f.feature.sides[i].length);
            if(side.operation==document::FeatureSideOperation::Revolution&&side.rotation_extent==document::FeatureRotationExtent::Angle)result.emplace(prefix+"_angle",&f.feature.sides[i].angle_degrees);
            if(side.operation==document::FeatureSideOperation::Extrusion&&f.feature.type==document::FeatureType::Modeling)result.emplace(prefix+"_draft_angle",&f.feature.sides[i].draft_angle_degrees);
        }
        return result;
    }
    case FeatureKind::SheetTransition:return {{"end_x",&f.sheet_transition.end_position.x},{"end_y",&f.sheet_transition.end_position.y},{"end_z",&f.sheet_transition.end_position.z},{"end_rx",&f.sheet_transition.end_rotation.x},{"end_ry",&f.sheet_transition.end_rotation.y},{"end_rz",&f.sheet_transition.end_rotation.z},{"thickness",&f.sheet_transition.thickness},{"inside_radius",&f.sheet_transition.inside_radius}};
    case FeatureKind::Extrusion: return {{"length_forward",&f.extrusion.length_forward},{"length_reverse",&f.extrusion.length_reverse},{"profile_offset",&f.extrusion.profile_plane_offset},{"thin_thickness",&f.extrusion.thin_thickness}};
    case FeatureKind::Revolution: return {{"angle",&f.revolution.angle_degrees},{"length_reverse",&f.revolution.angle_reverse},{"profile_offset",&f.revolution.profile_plane_offset},{"thin_thickness",&f.revolution.thin_thickness}};
    case FeatureKind::Fillet: return {{"primary",&f.edge_treatment.primary_size},{"secondary",&f.edge_treatment.secondary_size}};
    case FeatureKind::Chamfer: return {{"primary",&f.edge_treatment.primary_size},{"secondary",&f.edge_treatment.secondary_size},{"treatment_angle",&f.edge_treatment.angle_degrees}};
    case FeatureKind::Shell: return {{"thickness",&f.shell.thickness}};
    case FeatureKind::Flat: return f.flat.thickness_override?Slots{{"thickness",&f.flat.thickness}}:Slots{};
    case FeatureKind::Bend: {
        Slots result{{"angle",&f.bend.angle_degrees}};
        if(!f.bend.radius_follows_thickness)result.emplace("radius",&f.bend.radius);
        return result;
    }
    case FeatureKind::Holes: return {{"diameter",&f.holes.diameter}};
    case FeatureKind::Hole: return {{"diameter",&f.hole.diameter},{"bore_length",&f.hole.bore_length},{"entrance_chamfer",&f.hole.entrance_chamfer},{"exit_chamfer",&f.hole.exit_chamfer},{"drill_point_angle",&f.hole.drill_point_angle_degrees},{"thread_diameter",&f.hole.thread_nominal_diameter},{"thread_pitch",&f.hole.thread_pitch},{"thread_length",&f.hole.thread_length}};
    case FeatureKind::Thread: return {{"bore_diameter",&f.thread.nominal_diameter},{"bore_length",&f.thread.bore_length},{"thread_length",&f.thread.length_forward},{"length_reverse",&f.thread.length_reverse},{"chamfer_depth",&f.thread.chamfer_depth},{"chamfer_angle",&f.thread.chamfer_angle_degrees},{"drill_point_angle",&f.hole.drill_point_angle_degrees}};
    case FeatureKind::ShaftThread: return {{"root_diameter",&f.shaft_thread.root_diameter},{"length",&f.shaft_thread.length}};
    case FeatureKind::Sweep2D: return {{"thickness",&f.sweep2d.thickness}};
    case FeatureKind::Sweep3D: return {{"thickness",&f.sweep3d.thickness}};
    case FeatureKind::HelicalSweep: return {{"pitch",&f.helical.pitch},{"thickness",&f.helical.thickness}};
    case FeatureKind::DrillPoint: return {{"angle",&f.drill_point.included_angle_degrees}};
    default: return {};
    }
}
std::string number(double value) {
    char buffer[64];
    const auto [end,error]=std::to_chars(std::begin(buffer),std::end(buffer),value);
    if(error!=std::errc{}||!std::isfinite(value))throw std::invalid_argument("Family dimensions must be finite numbers.");
    return {buffer,end};
}
double native_number(const std::string& text) {
    double value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{}||end!=text.data()+text.size()||!std::isfinite(value))
        throw std::invalid_argument("Family dimensions must be finite numbers.");
    return value;
}
bool has_feature_solid(const document::HistoryContainer& f) { return f.feature_kind!=FeatureKind::Sketch; }
template<class Doc> void add_feature_references(std::vector<FamilyReference>& out,const Doc& doc,document::HistoryContainer f) {
    if(has_feature_solid(f)||f.feature_kind==FeatureKind::Sketch)out.push_back({{"feature",f.id,{}},f.name,f.name,f.suppressed?"no":"yes"});
    const auto add=[&](const std::string& key,double value) {
        if(f.value_locks.contains(key))return;
        const auto semantic="parameter:"+key;
        const auto name=doc.dimension_identifiers.identifier(f.id,semantic);
        if(!name.empty())out.push_back({{"dimension",f.id,semantic},name,f.name,number(value)});
    };
    if(primitive_definition(f.feature_kind))for(const auto& [key,value]:primitive_dimensions(f))add(key,value);
    else for(const auto& [key,value]:feature_slots(f))add(key,*value);
    if(f.feature_kind==FeatureKind::Sketch || f.feature_kind==FeatureKind::Holes || f.feature_kind==FeatureKind::Bend)
        for(const auto& sketch:doc.sketches)if(sketch.owner_container_id==f.id)add("profile_offset",sketch.plane_offset);
}
template<class Doc> void add_sketch_references(std::vector<FamilyReference>& out,const Doc& doc,const sketcher::Sketch& sketch) {
    for(const auto& dimension:sketch.dimensions) {
        const auto key="dimension:"+dimension.id;
        const auto name=doc.dimension_identifiers.identifier(sketch.id,key);
        if(!name.empty() && dimension.driving && !dimension.suppressed && !dimension.locked)out.push_back({{"dimension",sketch.id,key},name,sketch.name,number(sketcher::dimension_display_value(dimension))});
    }
    for(const auto& radius:sketch.corner_radii) {
        const auto key="corner_dimension:"+radius.id;
        const auto name=doc.dimension_identifiers.identifier(sketch.id,key);
        if(!name.empty())out.push_back({{"dimension",sketch.id,key},name,sketch.name,number(radius.radius)});
    }
}
void assign_feature(document::HistoryContainer& f,const std::string& key,double value) {
    if(f.feature_kind==FeatureKind::DerivedCopy&&document::assign_pattern_dimension(f.derived_copy,key,value))return;
    if(f.value_locks.contains(key))throw std::invalid_argument("The family dimension is locked.");
    if(primitive_definition(f.feature_kind)){assign_primitive_dimensions(f,{{key,value}});return;}
    auto slots=feature_slots(f);const auto found=slots.find(key);
    if(found==slots.end())throw std::invalid_argument("The family dimension is not editable.");
    const bool zero_bend_angle=f.feature_kind==FeatureKind::Bend&&(key=="angle"||key=="radius")&&value==0;
    const bool signed_value=key=="profile_offset"||key.ends_with("draft_angle")||(f.feature_kind==FeatureKind::SheetTransition&&key.starts_with("end_"));
    if(!signed_value && value<=0&&!zero_bend_angle)throw std::invalid_argument("Family feature dimensions must be positive.");
    if(key.ends_with("draft_angle")&&std::abs(value)>=90)throw std::invalid_argument("Draft angle must be between -90 and 90 degrees.");
    if(f.feature_kind==FeatureKind::Feature&&(key=="side0_angle"||key=="side1_angle")) {
        const auto side=key=="side0_angle"?0:1;
        const bool twist=f.feature.sides[side].operation==document::FeatureSideOperation::Twist;
        if(value>(twist?36000:360))throw std::invalid_argument(twist?
            "The family angle is outside its allowed range.":"Revolution angle must be in (0, 360]");
    }
    if((f.feature_kind==FeatureKind::Revolution&&(key=="angle"||key=="length_reverse")&&value>360) ||
        (f.feature_kind==FeatureKind::Bend&&key=="angle"&&value>180) ||
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
    if(binding.semantic_key.starts_with("parameter:pattern:")) {
        const auto key=binding.semantic_key.substr(10);
        if constexpr(requires {doc.body_history;}) {
            if(const auto* body=doc.body_history.find(binding.owner_id);body&&body->derived_copy) {
                auto next=*body;
                if(!document::assign_pattern_dimension(*next.derived_copy,key,value))return false;
                doc.body_history.update_body(std::move(next));return true;
            }
        } else if(auto* component=doc.find_occurrence(binding.owner_id);component&&component->derived_copy)
            return document::assign_pattern_dimension(*component->derived_copy,key,value);
    }
    if constexpr(requires {doc.history;}) {
        for(auto& f:doc.history)if(f.id==binding.owner_id && binding.semantic_key.starts_with("parameter:")) {
            const auto key=binding.semantic_key.substr(10);
            if(key=="profile_offset" && (f.feature_kind==FeatureKind::Sketch||f.feature_kind==FeatureKind::Holes||f.feature_kind==FeatureKind::Bend)) {
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
    if constexpr(requires {doc.history;})for(auto& f:doc.history)document::visit_feature_sketches(f,[&](auto& serialized,std::size_t stage){
        auto sketch=sketcher::Sketch::from_serialized(serialized);
        if(assign_sketch(sketch,binding,value)){
            if(f.feature_kind==FeatureKind::Bend) {
                const auto start=std::ranges::find(doc.sketches,f.bend.sketch_id,&sketcher::Sketch::id);
                if(start==doc.sketches.end())throw std::invalid_argument("Bend start profile is missing.");
                document::accept_bend_sketch(f,*start,stage,std::move(sketch),document::sheet_metal_defaults(doc));
            } else serialized=sketch.serialized();
            changed=true;
        }
    });
    return changed;
}
void presence(document::PartDocument& doc,const FamilyColumn& binding,bool present) {
    if(binding.kind=="feature") {
        auto* f=doc.find_container(binding.owner_id);if(!f)throw std::invalid_argument("Family feature no longer exists.");f->suppressed=!present;return;
    }
    if(binding.kind=="body") {
        if(!present&&doc.body_history.active_body_id()==binding.owner_id)doc.body_history.activate({});
        if(const auto* body=doc.body_history.find(binding.owner_id)) {
            auto value=*body;value.suppressed=!present;doc.body_history.update_body(std::move(value));
        } else if(const auto* operation=doc.body_history.find_boolean(binding.owner_id)) {
            auto value=*operation;value.suppressed=!present;doc.body_history.update_boolean(std::move(value));
        } else throw std::invalid_argument("Family Body no longer exists.");
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
        if(!assign_dimension(doc,binding,native_number(value->second)))throw std::invalid_argument("Family dimension no longer exists.");
    }
    for(const auto& [name,binding]:table.bindings)if(binding.kind!="dimension") {
        const auto value=row.values.find(name);if(value!=row.values.end()&&!value->second.empty())presence(doc,binding,value->second=="yes");
    }
    doc.name=row.name;doc.family_table=document::serialize_family_table({});
    doc.user_parameters["name"]=row.name;doc.user_parameter_values["name"][""]=row.name;
}
}

bool assign_driving_dimension(document::PartDocument& doc,const document::FamilyColumn& binding,double value) {
    return assign_dimension(doc,binding,value);
}

namespace {
template<class Doc> std::map<std::string,RelationDimension> dimension_inputs(const Doc& doc) {
    std::map<std::string,RelationDimension> result;
    const double length=document::length_unit_mm(doc.document_units.at("Length"));
    const double angle=doc.document_units.at("Angle")=="rad"?180./std::numbers::pi:1.;
    const auto add=[&](const std::string& owner,const std::string& key,double native,bool angular,bool writable,bool count=false) {
        const auto name=doc.dimension_identifiers.identifier(owner,key);if(name.empty())return;
        const double scale=count?1.:angular?angle:length;
        result[name]={{"dimension",owner,key},{{native/scale,count?std::array<int,3>{}:angular?std::array<int,3>{0,1,0}:std::array<int,3>{1,0,0}},writable},scale};
    };
    const auto pattern=[&](const std::string& owner,const document::DerivedCopyParameters& copy) {
        for(const auto& d:document::pattern_dimension_values(copy))add(owner,"parameter:"+d.key,d.value,d.angular,d.writable,d.count);
    };
    const auto feature=[&](document::HistoryContainer f) {
        if(f.feature_kind==FeatureKind::DerivedCopy)pattern(f.id,f.derived_copy);
        const auto slot=[&](const std::string& key,double value) {
            const bool angular=key.find("angle")!=std::string::npos||key=="end_rx"||key=="end_ry"||key=="end_rz"||(f.feature_kind==FeatureKind::Revolution&&key=="length_reverse");
            add(f.id,"parameter:"+key,value,angular,!f.value_locks.contains(key));
        };
        if(primitive_definition(f.feature_kind))for(const auto& [key,value]:primitive_dimensions(f))slot(key,value);
        else for(const auto& [key,value]:feature_slots(f))slot(key,*value);
        if(f.feature_kind==FeatureKind::Sketch||f.feature_kind==FeatureKind::Holes||f.feature_kind==FeatureKind::Bend)
            for(const auto& sketch:doc.sketches)if(sketch.owner_container_id==f.id)slot("profile_offset",sketch.plane_offset);
    };
    const auto sketch=[&](const sketcher::Sketch& s) {
        using K=sketcher::DimensionKind;
        for(const auto& d:s.dimensions) {
            const bool angular=d.kind==K::Angle||d.kind==K::AngleBetween||d.kind==K::AngleThreePoint||d.kind==K::AngleSymmetric||d.kind==K::EllipseRotation;
            add(s.id,"dimension:"+d.id,sketcher::dimension_display_value(d),angular,d.driving&&!d.suppressed&&!d.locked);
            const auto name=doc.dimension_identifiers.identifier(s.id,"dimension:"+d.id);
            if(result.contains(name))result.at(name).input.calculated=!d.driving;
        }
        for(const auto& r:s.corner_radii)add(s.id,"corner_dimension:"+r.id,r.radius,false,true);
    };
    if constexpr(requires{doc.history;})for(const auto& f:doc.history){feature(f);document::visit_feature_sketches(f,[&](const auto& data,std::size_t){sketch(sketcher::Sketch::from_serialized(data));});}
    else for(const auto& cut:doc.cuts)feature(cut.definition);
    for(const auto& s:doc.sketches)sketch(s);
    if constexpr(requires{doc.body_history;}) {
        for(const auto& body:doc.body_history.bodies())if(body.derived_copy)pattern(body.scope.id,*body.derived_copy);
    } else for(const auto& c:doc.components)if(c.derived_copy)pattern(c.occurrence_id,*c.derived_copy);
    return result;
}
template<class Doc> void dimension_units(std::vector<FamilyReference>& references,const Doc& doc) {
    const auto dimensions=dimension_inputs(doc);
    for(auto& reference:references)if(reference.binding.kind=="dimension") {
        const auto found=dimensions.find(reference.name);
        if(found==dimensions.end()||found->second.binding!=reference.binding)continue;
        const auto& dimension=found->second;
        reference.native_scale=dimension.native_scale;
        if(dimension.input.value.units==std::array<int,3>{1,0,0})reference.unit=doc.document_units.at("Length");
        else if(dimension.input.value.units==std::array<int,3>{0,1,0})reference.unit=doc.document_units.at("Angle");
    }
}
}
std::map<std::string,RelationDimension> relation_dimensions(const document::PartDocument& doc){return dimension_inputs(doc);}
std::map<std::string,RelationDimension> relation_dimensions(const assembly::AssemblyDocument& doc){return dimension_inputs(doc);}
void apply_family_variant(document::PartDocument& doc,const document::FamilyTable& table,const document::FamilyInstance& row) { apply(doc,table,row); }
bool assign_driving_dimension(assembly::AssemblyDocument& doc,const document::FamilyColumn& binding,double value) {
    return assign_dimension(doc,binding,value);
}

std::vector<FamilyReference> family_references(const Workspace& live,const std::string& id) {
    std::vector<FamilyReference> out;
    if(const auto* state=live.open_part(id)) {
        const auto& doc=state->session.document();
        for(const auto& body:doc.body_history.bodies()) {
            const bool present=!body.suppressed;
            out.push_back({{"body",body.scope.id,{}},body.name,body.name,present?"yes":"no"});
        }
        for(const auto& id:doc.body_history.order())if(const auto* body=doc.body_history.find_boolean(id))
            out.push_back({{"body",id,{}},body->name,body->name,body->suppressed?"no":"yes"});
        for(const auto& f:doc.history) {
            add_feature_references(out,doc,f);
            document::visit_feature_sketches(f,[&](const auto& serialized,std::size_t){add_sketch_references(out,doc,sketcher::Sketch::from_serialized(serialized));});
        }
        for(const auto& sketch:doc.sketches)add_sketch_references(out,doc,sketch);
        dimension_units(out,doc);
    } else if(const auto* state=live.open_assembly(id)) {
        const auto& doc=state->session.document();
        for(const auto& c:doc.components)out.push_back({{"component",c.occurrence_id,{}},c.name,c.name,c.suppressed?"no":"yes"});
        for(const auto& cut:doc.cuts)add_feature_references(out,doc,cut.definition);
        for(const auto& sketch:doc.sketches)add_sketch_references(out,doc,sketch);
        dimension_units(out,doc);
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
namespace {
struct FamilyTransaction {
    Workspace& live;
    explicit FamilyTransaction(Workspace& value):live(value){live.family_transaction_active=true;}
    ~FamilyTransaction(){live.family_transaction_active=false;}
};
template<class Doc> std::vector<FamilyReference> catalog(const Doc& doc) {
    Workspace draft;
    if constexpr(std::is_same_v<Doc,document::PartDocument>)draft.add_part(doc);
    else draft.add_assembly(doc);
    return family_references(draft,doc.document_id);
}
template<class Doc> Doc reidentify(Doc doc,const std::string& id) {
    doc.family={};
    auto packet=doc.serialized();
    document::remap_document_identity(packet,doc.document_id,id);
    return Doc::from_serialized(packet);
}
template<class Doc> Doc member(const Doc& base,const document::FamilyTable& table,const document::FamilyInstance& row) {
    auto next=reidentify(base,base.document_id+":family:"+row.id);
    apply(next,table,row);next.family.parent_id=base.document_id;next.family.row_id=row.id;
    return next;
}
template<class Doc> Doc merge_member(const Doc& base,const Doc& before,Doc next) {
    const auto nested=document::parse_family_table(next.family_table);
    if(!nested.columns.empty()||!nested.instances.empty())throw std::invalid_argument("A variant cannot own a nested Family Table. Edit the parent table instead.");
    auto table=document::parse_family_table(base.family_table);
    auto row=std::ranges::find(table.instances,before.family.row_id,&document::FamilyInstance::id);
    if(row==table.instances.end())throw std::invalid_argument("Family variant no longer exists.");
    const auto old_values=catalog(before),new_values=catalog(next),base_values=catalog(base);
    std::vector<std::string> removed;
    for(const auto& [name,binding]:table.bindings) {
        const auto old=std::ranges::find_if(old_values,[&](const auto& r){return r.binding==binding;});
        const auto value=std::ranges::find_if(new_values,[&](const auto& r){return r.binding==binding;});
        const auto generic=std::ranges::find_if(base_values,[&](const auto& r){return r.binding==binding;});
        if(value==new_values.end()){removed.push_back(name);continue;}
        if(old!=old_values.end()&&old->value!=value->value)row->values[name]=value->value;
        // Remove row-specific overrides before publishing the shared definition.
        if(generic==base_values.end())continue;
        if(binding.kind=="dimension") {
            if(value->value!=generic->value && !assign_dimension(next,binding,native_number(generic->value)))
                throw std::invalid_argument("Cannot restore the generic family dimension.");
        } else if constexpr(std::is_same_v<Doc,document::PartDocument>) {
            if(binding.kind=="body") {
                presence(next,binding,generic->value=="yes");
            } else if(auto* f=next.find_container(binding.owner_id))f->suppressed=base.find_container(binding.owner_id)->suppressed;
        } else presence(next,binding,generic->value=="yes");
    }
    for(const auto& name:removed) {
        std::erase(table.columns,name);table.bindings.erase(name);
        for(auto& instance:table.instances)instance.values.erase(name);
    }
    if(next.name!=before.name)row->name=next.name;
    else if(next.user_parameters.contains("name")&&before.user_parameters.contains("name")&&next.user_parameters.at("name")!=before.user_parameters.at("name"))row->name=next.user_parameters.at("name");
    document::validate_family_table(table,base.name);
    next=reidentify(std::move(next),base.document_id);
    next.name=base.name;next.user_parameters["name"]=base.user_parameters.contains("name")?base.user_parameters.at("name"):base.name;
    next.user_parameter_values["name"]=base.user_parameter_values.contains("name")?base.user_parameter_values.at("name"):std::map<std::string,std::string>{};
    next.family_table=document::serialize_family_table(table);
    next.family=base.family;
    return next;
}
std::vector<kernel::BodyResult> evaluated_part(document::PartDocument& next,
    const std::vector<kernel::BodyResult>& previous,const kernel::OcctKernel& kernel) {
    const auto operations=group_solid_state_cached_operations(next,&previous);
    bool exact=previous.size()==operations.size();
    for(std::size_t i=0;exact&&i<previous.size();++i)exact=previous[i].source_fingerprint==kernel::history_fingerprint(operations,i+1);
    if(exact)return previous;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&previous,{true});
    const auto resolved=group_solid_state_cached_operations(next,&calculated);
    exact=calculated.size()==resolved.size();
    for(std::size_t i=0;exact&&i<calculated.size();++i)exact=calculated[i].source_fingerprint==kernel::history_fingerprint(resolved,i+1);
    if(!exact)calculated=calculate_part(kernel,next,&calculated,{true});
    return calculated;
}
std::set<std::string> evaluated_rows(const document::FamilyDocument& family) {
    std::set<std::string> result;for(const auto& [row,packet]:family.evaluated)result.insert(row);return result;
}
bool same_assembly_calculation(const assembly::AssemblyDocument& a,const assembly::AssemblyDocument& b) {
    if(a.cuts!=b.cuts||a.components.size()!=b.components.size())return false;
    for(std::size_t i=0;i<a.components.size();++i) {
        const auto& x=a.components[i];const auto& y=b.components[i];
        if(x.occurrence_id!=y.occurrence_id||x.source_document_id!=y.source_document_id||
            x.placement!=y.placement||x.suppressed!=y.suppressed||x.derived_copy!=y.derived_copy||
            x.calculated_source->source_fingerprint!=y.calculated_source->source_fingerprint)return false;
    }
    return true;
}
void evaluate_assembly(assembly::AssemblyDocument& next,const assembly::AssemblyDocument* before,
    const kernel::OcctKernel& kernel) {
    if(before&&same_assembly_calculation(next,*before))return;
    next.resolve_constructions();
    if(!next.cuts.empty()||std::ranges::any_of(next.components,[](const auto& c){return c.derived_copy.has_value();}))
        calculate_resolved_assembly_cuts(kernel,next);
}
}
std::string family_owner(const Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.document().family.parent_id.empty()?id:part->session.document().family.parent_id;
    if(const auto* assembly=live.open_assembly(id))return assembly->session.document().family.parent_id.empty()?id:assembly->session.document().family.parent_id;
    return id;
}
bool commit_family_part(Workspace& live,const std::string& id,document::PartDocument& candidate,
    std::vector<kernel::BodyResult>& calculated) {
    if(live.family_transaction_active)return false;
    const auto owner=family_owner(live,id);
    const auto* source=std::as_const(live).open_part(id);const auto* parent=std::as_const(live).open_part(owner);
    if(!parent)throw std::invalid_argument("The owning family document is not open.");
    if(owner==id&&parent->session.document().family.evaluated.empty())return false;
    FamilyTransaction transaction(live);
    auto base=owner==id?candidate:merge_member(parent->session.document(),source->session.document(),candidate);
    const auto table=document::parse_family_table(base.family_table);document::validate_family_table(table,base.name);
    for(const auto& state:live.documents())std::visit([&](const auto& value){
        if constexpr(requires{value.session;}) {
            const auto& doc=value.session.document();
            if(doc.family.parent_id==owner&&std::ranges::none_of(table.instances,[&](const auto& row){return row.id==doc.family.row_id;}))
                throw std::invalid_argument("Close the variant tab before deleting its Family Table row.");
        }
    },state);
    const kernel::OcctKernel kernel;
    auto base_calculated=evaluated_part(base,owner==id?calculated:parent->session.calculated_boundaries(),kernel);
    auto rows=evaluated_rows(parent->session.document().family);if(owner!=id)rows.insert(source->session.document().family.row_id);
    base.family={};
    std::map<std::string,document::DocumentSession> members;
    for(const auto& row:table.instances)if(rows.contains(row.id)) {
        auto next=member(base,table,row);std::vector<kernel::BodyResult> previous;
        const auto* existing=std::as_const(live).open_part(next.document_id);
        if(existing)previous=existing->session.calculated_boundaries();
        else if(const auto old=parent->session.document().family.evaluated.find(row.id);old!=parent->session.document().family.evaluated.end())
            static_cast<void>(document::PartDocument::from_serialized(*old->second,&previous));
        auto result=evaluated_part(next,previous,kernel);
        auto session=existing?existing->session:document::DocumentSession(next,result);
        if(existing)session.commit(std::move(next),std::move(result));
        base.family.evaluated[row.id]=std::make_shared<const nlohmann::json>(session.document().serialized(session.calculated_boundaries()));
        members.emplace(session.document().document_id,std::move(session));
    }
    auto prepared=parent->session;prepared.commit(std::move(base),std::move(base_calculated));
    // Prepared session copies retain monotonic revisions/generations for viewers and commands.
    // Rebind routing immediately after publication for callers retaining a state pointer.
    live.open_part(owner)->session=std::move(prepared);
    static_cast<void>(live.find(owner));
    for(auto& [member_id,session]:members)if(auto* existing=live.open_part(member_id)) {
        existing->session=std::move(session);static_cast<void>(live.find(member_id));
    }
    return true;
}
bool commit_family_assembly(Workspace& live,const std::string& id,assembly::AssemblyDocument& candidate) {
    if(live.family_transaction_active)return false;
    const auto owner=family_owner(live,id);
    const auto* source=std::as_const(live).open_assembly(id);const auto* parent=std::as_const(live).open_assembly(owner);
    if(!parent)throw std::invalid_argument("The owning family document is not open.");
    if(owner==id&&parent->session.document().family.evaluated.empty())return false;
    FamilyTransaction transaction(live);
    auto base=owner==id?candidate:merge_member(parent->session.document(),source->session.document(),candidate);
    const auto table=document::parse_family_table(base.family_table);document::validate_family_table(table,base.name);
    for(const auto& state:live.documents())std::visit([&](const auto& value){
        if constexpr(requires{value.session;}) {
            const auto& doc=value.session.document();
            if(doc.family.parent_id==owner&&std::ranges::none_of(table.instances,[&](const auto& row){return row.id==doc.family.row_id;}))
                throw std::invalid_argument("Close the variant tab before deleting its Family Table row.");
        }
    },state);
    const kernel::OcctKernel kernel;
    if(owner!=id)evaluate_assembly(base,&parent->session.document(),kernel);
    auto rows=evaluated_rows(parent->session.document().family);if(owner!=id)rows.insert(source->session.document().family.row_id);
    base.family={};std::map<std::string,assembly::AssemblySession> members;
    for(const auto& row:table.instances)if(rows.contains(row.id)) {
        auto next=member(base,table,row);const auto* existing=std::as_const(live).open_assembly(next.document_id);
        std::optional<assembly::AssemblyDocument> previous;
        if(!existing)if(const auto old=parent->session.document().family.evaluated.find(row.id);old!=parent->session.document().family.evaluated.end())previous=assembly::AssemblyDocument::from_serialized(*old->second);
        evaluate_assembly(next,existing?&existing->session.document():previous?&*previous:nullptr,kernel);
        auto session=existing?existing->session:assembly::AssemblySession(next);
        if(existing)session.commit(std::move(next));
        base.family.evaluated[row.id]=std::make_shared<const nlohmann::json>(session.document().serialized());
        members.emplace(session.document().document_id,std::move(session));
    }
    auto prepared=parent->session;prepared.commit(std::move(base));
    live.open_assembly(owner)->session=std::move(prepared);
    static_cast<void>(live.find(owner));
    for(auto& [member_id,session]:members)if(auto* existing=live.open_assembly(member_id)) {
        existing->session=std::move(session);static_cast<void>(live.find(member_id));
    }
    return true;
}
document::PartDocument family_part_source(document::PartDocument base,std::vector<kernel::BodyResult>& calculated,const std::string& expected) {
    if(expected.empty()||expected==base.document_id)return base;
    for(const auto& [id,packet]:base.family.evaluated)if(packet->at("document_id")==expected)
        return document::PartDocument::from_serialized(*packet,&calculated);
    throw DrawingOperationError("source_identity","The source file does not contain the requested model or evaluated family variant.");
}
assembly::AssemblyDocument family_assembly_source(assembly::AssemblyDocument base,const std::string& expected,bool resolve_sources) {
    if(expected.empty()||expected==base.document_id)return base;
    for(const auto& [id,packet]:base.family.evaluated)if(packet->at("document_id")==expected) {
        auto member=assembly::AssemblyDocument::from_serialized(*packet);
        if(resolve_sources)member.hydrate_sources(base.native_source_path,[&](auto& component) {
            for(const auto& source:base.components)if(source.source_document_id==component.source_document_id && !base.owns_component_result(source.occurrence_id)) {
                component.calculated_source=source.calculated_source;component.nested_snapshot=source.nested_snapshot;
                component.body_color=source.body_color;component.face_colors=source.face_colors;component.appearance=source.appearance;
                component.density_kg_mm3=source.density_kg_mm3;component.nested_mass_kg=source.nested_mass_kg;
                component.mass_volume_mm3=source.mass_volume_mm3;component.source_missing=source.source_missing;return true;
            }
            return false;
        });
        return member;
    }
    throw DrawingOperationError("source_identity","The source file does not contain the requested model or evaluated family variant.");
}
void restore_family_tabs(Workspace& live,const std::string& owner) {
    FamilyTransaction transaction(live);
    std::vector<std::string> close;
    for(auto& state:live.documents())std::visit([&](auto& value) {
        if constexpr(requires{value.session;}) {
            const auto& doc=value.session.document();if(doc.family.parent_id!=owner)return;
            const auto id=doc.document_id;
            if constexpr(std::is_same_v<std::decay_t<decltype(value)>,PartState>) {
                const auto* parent=std::as_const(live).open_part(owner);
                if(!parent->session.document().family.evaluated.contains(doc.family.row_id)){close.push_back(id);return;}
                std::vector<kernel::BodyResult> cache;
                auto next=family_part_source(parent->session.document(),cache,id);
                value.session.commit(std::move(next),std::move(cache));value.path=parent->path;
            } else {
                const auto* parent=std::as_const(live).open_assembly(owner);
                if(!parent->session.document().family.evaluated.contains(doc.family.row_id)){close.push_back(id);return;}
                value.session.commit(family_assembly_source(parent->session.document(),id));value.path=parent->path;
            }
            static_cast<void>(live.find(id));
        }
    },state);
    for(const auto& id:close)static_cast<void>(live.remove(id));
}
document::PartDocument read_family_part(const Workspace* live,const std::filesystem::path& path,
    const std::string& expected,std::vector<kernel::BodyResult>& cache) {
    if(live) {
        const auto root=expected.substr(0,expected.find(":family:"));
        if(const auto* source=live->open_part(root)){cache=source->session.calculated_boundaries();return family_part_source(source->session.document(),cache,expected);}
    }
    auto base=document::PartDocument::load(path,&cache);return family_part_source(std::move(base),cache,expected);
}
assembly::AssemblyDocument read_family_assembly(const Workspace* live,const std::filesystem::path& path,
    const std::string& expected,bool resolve_sources) {
    if(live)if(const auto* source=live->open_assembly(expected.substr(0,expected.find(":family:")))) {
        auto member=family_assembly_source(source->session.document(),expected,false);
        if(resolve_sources && member.document_id!=source->session.document().document_id)
            member.hydrate_sources(source->path,native_source_resolver(*live));
        return member;
    }
    return family_assembly_source(assembly::AssemblyDocument::load(path,live&&resolve_sources?native_source_resolver(*live):assembly::AssemblyDocument::SourceResolver{},resolve_sources),expected,resolve_sources);
}
std::string open_family_instance(Workspace& live,const kernel::OcctKernel& kernel,const std::string& requested,const std::string& name,bool activate) {
    const auto id=family_owner(live,requested);
    const auto table=family_table(live,id);validate_family_references(live,id,table);
    const auto row=std::ranges::find(table.instances,name,&document::FamilyInstance::name);
    if(row==table.instances.end()||row->id.empty())throw std::invalid_argument("Family variant no longer exists.");
    const auto instance_id=id+":family:"+row->id;
    if(live.find(instance_id)){if(activate){live.display_top_level(instance_id);live.activate(instance_id);}return instance_id;}
    FamilyTransaction transaction(live);
    if(auto* source=live.open_part(id)) {
        auto family=source->session.document().family;const auto path=source->path;
        document::PartDocument next;std::vector<kernel::BodyResult> cache;
        if(const auto found=family.evaluated.find(row->id);found!=family.evaluated.end())next=document::PartDocument::from_serialized(*found->second,&cache);
        else {
            next=member(source->session.document(),table,*row);cache=evaluated_part(next,{},kernel);
            document::DocumentSession prepared(next,cache);next=prepared.document();
            family.evaluated[row->id]=std::make_shared<const nlohmann::json>(next.serialized(cache));
            source->session.update_family_evaluated(std::move(family));
        }
        const bool native_template=source->native_drawing_template;
        const auto symbol_definition=source->symbol_definition;
        live.add_part(std::move(next),std::move(cache),path);
        live.open_part(instance_id)->native_drawing_template=native_template;
        live.open_part(instance_id)->symbol_definition=symbol_definition;
    } else if(auto* source=live.open_assembly(id)) {
        auto family=source->session.document().family;const auto path=source->path;assembly::AssemblyDocument next;
        if(const auto found=family.evaluated.find(row->id);found!=family.evaluated.end())next=assembly::AssemblyDocument::from_serialized(*found->second);
        else {
            next=member(source->session.document(),table,*row);evaluate_assembly(next,nullptr,kernel);
            family.evaluated[row->id]=std::make_shared<const nlohmann::json>(next.serialized());source->session.update_family_evaluated(std::move(family));
        }
        next.hydrate_sources(path,native_source_resolver(live));
        live.add_assembly(std::move(next),path);
    } else throw std::invalid_argument("Family Table requires a Part or Assembly.");
    if(activate){live.display_top_level(instance_id);live.activate(instance_id);}return instance_id;
}
void select_family_drawing_source(drawing::DrawingDocument& drawing,const Workspace& live,
    const std::string& sheet_id,const std::string& source) {
    const auto sources=drawing.data_sources();
    if(std::ranges::none_of(sources,[&](const auto& s){return source==s.document_id||source.starts_with(s.document_id+":family:");}))
        throw std::invalid_argument("The model is not registered as a Drawing source.");
    std::filesystem::path path;std::string name;
    if(const auto* part=live.open_part(source)){path=part->path;name=part->session.document().name;}
    else if(const auto* assembly=live.open_assembly(source)){path=assembly->path;name=assembly->session.document().name;}
    else throw std::invalid_argument("Open the family variant before selecting it in the Drawing.");
    if(path.empty()||!std::filesystem::is_regular_file(path))throw std::invalid_argument("Save the owning family document before using it as a Drawing source.");
    auto next=drawing;auto* sheet=next.find_sheet(sheet_id);
    if(!sheet)throw std::invalid_argument("The Drawing sheet no longer exists.");
    if(next.source_document_id.empty()) {
        next.source_document_id=source;next.source_path=path;next.source_name=name;
    }
    sheet->selected_source_document_id=source;
    drawing=std::move(next);
}
}
