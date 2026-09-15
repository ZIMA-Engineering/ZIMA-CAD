#include <zima/workspace/measurement_edits.hpp>
#include <zima/document/measurement_record.hpp>
#include <zima/document/metadata.hpp>
#include <zima/kernel/stable_id.hpp>
namespace zima::workspace {
namespace {
using Error=MeasurementOperationError;
const std::vector<kernel::SavedMeasurement>& records(const Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.document().measurements;
    if(const auto* assembly=live.open_assembly(id))return assembly->session.document().measurements;
    throw Error("unsupported_document","Measurement commands require an open Part or Assembly.");
}
void writable(const Workspace& live,const std::string& id) {
    if(live.active_document_id()!=id||live.displayed_document_id()!=id)
        throw Error("unsupported_context","Activate the displayed model before changing saved measurements.");
}
void commit(Workspace& live,const std::string& id,std::vector<kernel::SavedMeasurement> values) {
    if(auto* part=live.open_part(id)) {
        auto next=part->session.document();next.measurements=std::move(values);
        part->session.commit(std::move(next),part->session.calculated_boundaries());
    }else {
        auto* assembly=live.open_assembly(id);auto next=assembly->session.document();next.measurements=std::move(values);
        assembly->session.commit(std::move(next));
    }
}
void valid_reference(const kernel::MeasurementReference& ref) {
    using K=kernel::MeasurementKind;
    if(static_cast<int>(ref.kind)<0||static_cast<int>(ref.kind)>static_cast<int>(K::Plane))
        throw Error("invalid_reference","Unknown measurement reference kind.");
    if(ref.kind==K::Object?((ref.owner_id.empty()&&ref.instance_path.empty())||!ref.semantic_key.empty()):
        (ref.owner_id.empty()||ref.semantic_key.empty()||ref.semantic_key=="container:display"))
        throw Error("invalid_reference","A measurement requires an original object or exact occurrence identity.");
    if(!ref.instance_path.empty())try {
        if(assembly::InstancePath::decode(ref.instance_path).encoded()!=ref.instance_path)throw std::invalid_argument("path");
    }catch(const std::exception&) {throw Error("invalid_reference","The measurement occurrence path is invalid.");}
}
}
MeasurementEdit prepare_measurement_edit(const Workspace& live,const std::string& id,const std::string& object,const std::string& name_prefix) {
    const auto& values=records(live,id);MeasurementEdit edit;edit.document_id=id;edit.creating=object.empty();
    const auto stamp=[&](const auto& state){edit.revision=state.session.revision();edit.generation=state.session.data_generation();edit.runtime_identity=state.runtime_identity;};
    if(const auto* part=live.open_part(id))stamp(*part);else stamp(*live.open_assembly(id));
    if(edit.creating) {
        edit.initial.id=kernel::make_stable_id();
        for(std::size_t i=1;;++i) {
            edit.initial.name=name_prefix+" "+std::to_string(i);
            if(std::ranges::none_of(values,[&](const auto& value){return value.name==edit.initial.name;}))break;
        }
        if(const auto* part=live.open_part(id)) {
            const auto& doc=part->session.document();edit.initial.body_id=doc.body_history.active_body_id();
            if(const auto* body=doc.body_history.find(edit.initial.body_id)) {
                if(body->cursor)edit.initial.after_object_id=body->entries.at(body->cursor-1).id;
            }else if(!doc.body_history.bodies().empty()) {
                if(doc.body_history.insertion_cursor())edit.initial.after_object_id=doc.body_history.order().at(doc.body_history.insertion_cursor()-1);
            }else {
                const auto cursor=doc.effective_history_cursor();
                if(cursor)edit.initial.after_object_id=doc.history_order.empty()?doc.history.at(cursor-1).id:doc.history_order.at(cursor-1).id;
            }
        }
    }else {
        const auto found=std::ranges::find(values,object,&kernel::SavedMeasurement::id);
        if(found==values.end())throw Error("measurement_not_found","The requested measurement does not exist in this document.");
        edit.initial=*found;
    }
    return edit;
}
void evaluate_measurement_references(const Workspace& live,const std::string& id,kernel::SavedMeasurement& record) {
    static_cast<void>(records(live,id));
    if(record.references.empty()||record.references.size()>2)throw Error("invalid_arguments","Measurement requires one or two original references.");
    for(const auto& ref:record.references)valid_reference(ref);
    const auto scene=measurement_scene(live,id);std::vector<measurement::MeasurementGeometry> geometry;
    std::vector<kernel::MeasurementValues> values;
    for(std::size_t i=0;i<record.references.size();++i) {
        auto value=resolve_measurement(live,id,record.references[i],scene);
        if(!value)throw Error("missing_reference","The original measurement reference is unavailable.",i);
        values.push_back(value->values);geometry.push_back(std::move(*value));
    }
    const auto distance=geometry.size()==2?measurement::measure_distance(geometry[0],geometry[1]):std::nullopt;
    // Publish the whole result only after every requested reference resolves.
    record.values=std::move(values);record.distance=distance;
}
bool commit_measurement(Workspace& live,const MeasurementEdit& edit,kernel::SavedMeasurement record) {
    auto values=records(live,edit.document_id);writable(live,edit.document_id);
    const auto current=[&](const auto& state){return state.runtime_identity==edit.runtime_identity&&state.session.revision()==edit.revision&&state.session.data_generation()==edit.generation;};
    const bool unchanged=live.open_part(edit.document_id)?current(*live.open_part(edit.document_id)):current(*live.open_assembly(edit.document_id));
    if(!unchanged)throw Error("stale_edit","The document changed while measurement properties were open.");
    if(record.id!=edit.initial.id||record.body_id!=edit.initial.body_id||record.after_object_id!=edit.initial.after_object_id)
        throw Error("identity_changed","Editing must preserve the measurement identity and history location.");
    const auto first=record.name.find_first_not_of(" \t\r\n"),last=record.name.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)throw Error("invalid_name","Enter a measurement name.");
    record.name=record.name.substr(first,last-first+1);document::validate_native_metadata_text(record.name);
    if(std::ranges::any_of(values,[&](const auto& value){return value.id!=record.id&&value.name==record.name;}))
        throw Error("duplicate_name","A measurement with this name already exists.");
    evaluate_measurement_references(live,edit.document_id,record);
    static_cast<void>(document::parse_measurements(document::serialize_measurements({record})));
    if(!edit.creating&&record==edit.initial)return false;
    const auto found=std::ranges::find(values,record.id,&kernel::SavedMeasurement::id);
    if(edit.creating) {
        if(found!=values.end())throw Error("identity_changed","A measurement with this identity already exists.");
        values.push_back(std::move(record));
    }else {
        if(found==values.end())throw Error("measurement_not_found","The requested measurement does not exist in this document.");
        *found=std::move(record);
    }
    commit(live,edit.document_id,std::move(values));return true;
}
bool remove_measurement(Workspace& live,const std::string& id,const std::string& object) {
    auto values=records(live,id);writable(live,id);
    const auto removed=std::erase_if(values,[&](const auto& value){return value.id==object;});
    if(!removed)throw Error("measurement_not_found","The requested measurement does not exist in this document.");
    commit(live,id,std::move(values));return true;
}
}
