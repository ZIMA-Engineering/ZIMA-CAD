#include <zima/workspace/value_lock_operations.hpp>
#include <algorithm>
#include <type_traits>
namespace zima::workspace {
std::pair<std::string,std::string> value_lock_address(std::string object,std::string key) {
    if(key.starts_with("placement-reference:")) {
        const auto separator=key.rfind(':');
        if(separator!=std::string::npos&&separator>20) {
            object=key.substr(20,separator-20);key="placement:reference_offset:"+key.substr(separator+1);
        }
    }
    if(key.starts_with("parameter:"))key.erase(0,10);
    if(key=="size")key="primary";
    if(key=="included_angle")key="angle";
    if(key=="thread_nominal_diameter")key="thread_diameter";
    if(key=="thread_designation")key="nominal_diameter";
    if(key=="thread_pitch")key="pitch";
    if(key=="x"||key=="y"||key=="z"||key.starts_with("rotation_")||key.starts_with("reference_offset:"))key="placement:"+key;
    return {std::move(object),std::move(key)};
}
namespace {
template<bool Const> struct Field {
    using Set=std::conditional_t<Const,const std::set<std::string>,std::set<std::string>>;
    using Flag=std::conditional_t<Const,const bool,bool>;
    std::string key,storage_key;Set* values{};Flag* flag{};bool editable{true};
    bool get() const{return flag?*flag:values->contains(storage_key);}
    void set(bool locked) const requires(!Const) {if(flag)*flag=locked;else if(locked)values->insert(storage_key);else values->erase(storage_key);}
};
template<bool Const,class Set> void number(std::vector<Field<Const>>& out,Set& values,std::string key,std::string storage_key={}) {
    if(storage_key.empty())storage_key=key;
    out.push_back({std::move(key),std::move(storage_key),&values});
}
template<bool Const,class Set,class References> void placement(std::vector<Field<Const>>& out,Set& values,References& references,bool prefix,bool corrections=true) {
    for(const auto* key:{"x","y","z","rotation_x","rotation_y","rotation_z","rotation_offset_x","rotation_offset_y","rotation_offset_z"}) {
        if(!corrections&&std::string_view(key).starts_with("rotation_offset_"))continue;
        number(out,values,"placement:"+std::string(key),prefix?"placement:"+std::string(key):key);
    }
    std::size_t index{};
    for(auto& ref:references) {
        if constexpr(requires{ref.orientation_only;}) {if(ref.orientation_only||ref.owner_id.empty())continue;}
        bool editable=true;
        if constexpr(requires{ref.supports_offset;})editable=ref.supports_offset;
        else editable=!ref.component_reference.owner_id.empty()&&!ref.target_reference.owner_id.empty()&&
            ref.mate_type!=assembly::MateKind::AxisCoincident&&ref.mate_type!=assembly::MateKind::PointCoincident;
        out.push_back({"placement:reference_offset:"+std::to_string(index++),{},nullptr,&ref.offset_locked,editable});
    }
}
template<bool Const,class Parameters> void derived_numbers(std::vector<Field<Const>>& out,Parameters& parameters) {
    if(!parameters||!parameters->pattern)return;
    number(out,parameters->value_locks,"pattern:angle");
    for(std::size_t i=0;i<3;++i)number(out,parameters->value_locks,"pattern:spacing:"+std::to_string(i));
}
template<class Document> auto fields(Document& doc,const std::string& owner) {
    constexpr bool Const=std::is_const_v<Document>;std::vector<Field<Const>> out;
    const auto construction=[&](auto&& self,auto& object)->void {
        if(object.id==owner) {
            placement(out,object.value_locks,object.references,true);
            if(object.kind==document::ConstructionKind::Axis)number(out,object.value_locks,"length");
            if(object.kind==document::ConstructionKind::Plane)number(out,object.value_locks,"offset");
            if(!object.parent_construction_id.empty())number(out,object.value_locks,"radius");
            return;
        }
        for(auto& child:object.curve_points)self(self,child);
    };
    const auto feature=[&](auto& object) {
        if(object.id==owner) {
            placement(out,object.placement.value_locks,object.placement.references,false);
            const auto add=[&](std::initializer_list<const char*> keys){for(const auto* key:keys)number(out,object.value_locks,key);};
            using Kind=document::FeatureKind;
            // The numeric slots actually exposed by the shared Properties
            // dialogs. Catalog selections and derived values are not locks.
            switch(object.feature_kind) {
            case Kind::Box:case Kind::Pyramid:add({"length","width","height"});break;
            case Kind::Cylinder:add({"radius","height"});break;
            case Kind::Sphere:add({"radius"});break;
            case Kind::Cone:add({"bottom_radius","top_radius","height"});break;
            case Kind::Wedge:add({"length","width","height","top_offset"});break;
            case Kind::Sketch:number(out,object.placement.value_locks,"profile_offset");break;
            case Kind::Extrusion:add({"profile_offset","length_forward","length_reverse","thin_thickness"});break;
            case Kind::Revolution:add({"profile_offset","angle","length_reverse","thin_thickness"});break;
            case Kind::Fillet:add({"primary","secondary"});break;
            case Kind::Chamfer:add({"primary","secondary","treatment_angle"});break;
            case Kind::Shell:add({"thickness"});break;
            case Kind::Hole:add({"diameter","bore_length","entrance_chamfer","exit_chamfer","drill_point_angle","thread_diameter","pitch","thread_length"});break;
            case Kind::Thread:add({"bore_diameter","bore_length","nominal_diameter","pitch","thread_length","length_reverse","chamfer_depth","chamfer_angle","drill_point_angle","runout_pitch_factor"});break;
            case Kind::ShaftThread:add({"root_diameter","length","runout_pitch_factor"});break;
            case Kind::DrillPoint:add({"angle"});break;
            case Kind::Sweep2D:case Kind::Sweep3D:add({"thickness"});break;
            case Kind::HelicalSweep:add({"pitch","base_offset"});break;
            case Kind::ImportedStep:break;
            }
        }
        if(object.feature_kind==document::FeatureKind::Sweep3D)construction(construction,object.sweep3d.path);
    };
    for(auto& object:doc.constructions)construction(construction,object);
    if constexpr(requires{doc.history;})for(auto& object:doc.history)feature(object);
    if constexpr(requires{doc.cuts;})for(auto& cut:doc.cuts)feature(cut.definition);
    if constexpr(requires{doc.sketch_containers;})for(auto& container:doc.sketch_containers)feature(container);
    if constexpr(requires{doc.components;})for(auto& component:doc.components)
        if(component.occurrence_id==owner) {
            if(component.derived_copy) {
                placement(out,component.copy_placement.value_locks,component.copy_placement.references,false);
                derived_numbers(out,component.derived_copy);
            }else placement(out,component.value_locks,component.placement_references,true,false);
        }
    if constexpr(Const&&requires{doc.body_history;})
        if(const auto* body=doc.body_history.find(owner)) {
            placement(out,body->scope.placement.value_locks,body->scope.placement.references,false);
            derived_numbers(out,body->derived_copy);
        }
    return out;
}
template<class Document> std::vector<ValueLockInfo> list(const Document& doc,const std::string& object) {
    std::vector<ValueLockInfo> result;
    for(const auto& field:fields(doc,object))result.push_back({field.key,field.get(),field.editable});
    if(result.empty())throw ValueLockError("object_not_found","The object has no supported numeric value locks.");
    return result;
}
}
std::vector<ValueLockInfo> value_locks(const Workspace& live,const std::string& id,const std::string& object) {
    if(const auto* part=live.open_part(id))return list(part->session.document(),object);
    if(const auto* assembly=live.open_assembly(id))return list(assembly->session.document(),object);
    throw ValueLockError("unsupported_document","Value locks require an open Part or Assembly.");
}
std::optional<bool> value_locked(const Workspace& live,const std::string& id,const std::string& object,const std::string& key) {
    const auto address=value_lock_address(object,key);
    for(const auto& field:value_locks(live,id,address.first))if(field.key==address.second)return field.locked;
    return {};
}
bool set_value_lock(Workspace& live,const std::string& id,const std::string& object,const std::string& key,bool locked) {
    const auto [owner,canonical]=value_lock_address(object,key);
    const auto available=value_locks(live,id,owner);
    const auto field=std::ranges::find_if(available,[&](const auto& field){return field.key==canonical;});
    if(field==available.end())throw ValueLockError("unknown_parameter","The numeric value lock key is not supported by this object.");
    if(!field->editable)throw ValueLockError("parameter_not_editable","This reference has no editable offset.");
    const auto assign=[&](auto& values) {
        const auto value=std::ranges::find_if(values,[&](const auto& value){return value.key==canonical;});
        if(value==values.end())throw ValueLockError("unknown_parameter","The numeric value lock key is not supported by this object.");
        value->set(locked);
    };
    if(auto* part=live.open_part(id)) {
        const auto& before=part->session.document();
        if(const auto* body=before.body_owner_for_object(owner)) {
            if(body->derived_copy&&owner!=body->scope.id)throw ValueLockError("read_only_body","A derived Body cannot be edited directly.");
            if(owner!=body->scope.id&&body->scope.id!=before.body_history.active_body_id())
                throw ValueLockError("inactive_body","Activate the owning Body before changing its value locks.");
        }
        if(field->locked==locked)return false;
        auto next=before;
        if(const auto* stored=next.body_history.find(owner)) {
            auto body=*stored;std::vector<Field<false>> values;
            placement(values,body.scope.placement.value_locks,body.scope.placement.references,false);
            derived_numbers(values,body.derived_copy);assign(values);
            next.body_history.update_body(std::move(body));
        }else {auto values=fields(next,owner);assign(values);}
        // Same metadata-only commit as the existing View lock action. Retain
        // the calculated packet; no OCCT, mate solving or dependency refresh.
        part->session.commit(std::move(next),part->session.calculated_boundaries());return true;
    }
    auto* assembly=live.open_assembly(id);
    if(field->locked==locked)return false;
    auto next=assembly->session.document();auto values=fields(next,owner);assign(values);
    assembly->session.commit(std::move(next));return true;
}
} // namespace zima::workspace
