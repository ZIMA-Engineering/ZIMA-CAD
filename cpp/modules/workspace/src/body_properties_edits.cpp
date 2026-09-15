#include <zima/workspace/body_properties_edits.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/document/metadata.hpp>
namespace zima::workspace {
namespace {
using Error=MeasurementOperationError;
void writable(const Workspace& live,const std::string& id) {
    if(!live.open_part(id))throw Error("unsupported_document","Body properties require an open Part.");
    if(live.active_document_id()!=id||live.displayed_document_id()!=id)
        throw Error("unsupported_context","Activate the displayed Part before editing body properties.");
}
}
BodyPropertiesEdit prepare_body_properties_edit(const Workspace& live,const std::string& id,const std::string& object,const std::string& prefix) {
    writable(live,id);const auto& part=*live.open_part(id);const auto& doc=part.session.document();
    BodyPropertiesEdit edit{id,part.session.revision(),part.session.data_generation(),part.runtime_identity,object.empty(),{}};
    if(edit.creating) {
        edit.initial.id=kernel::make_stable_id();
        for(unsigned i=1;;++i){edit.initial.name=prefix+" "+std::to_string(i);
            if(std::ranges::none_of(doc.body_properties,[&](const auto& r){return r.name==edit.initial.name;}))break;}
        edit.initial.body_id=doc.body_history.active_body_id();
        if(const auto* body=doc.body_history.find(edit.initial.body_id)) {
            if(body->cursor)edit.initial.after_object_id=body->entries.at(body->cursor-1).id;
        }else if(!doc.body_history.bodies().empty()) {
            if(doc.body_history.insertion_cursor())edit.initial.after_object_id=doc.body_history.order().at(doc.body_history.insertion_cursor()-1);
        }else {
            const auto cursor=doc.effective_history_cursor();
            if(cursor)edit.initial.after_object_id=doc.history_order.empty()?doc.history.at(cursor-1).id:doc.history_order.at(cursor-1).id;
        }
        edit.initial=document::evaluate_body_properties(doc,part.session.calculated_boundaries(),std::move(edit.initial));
    }else {
        const auto found=std::ranges::find(doc.body_properties,object,&document::BodyProperties::id);
        if(found==doc.body_properties.end())throw Error("body_properties_not_found","The body properties feature does not exist.");
        edit.initial=*found;
    }
    return edit;
}
bool commit_body_properties(Workspace& live,const BodyPropertiesEdit& edit,document::BodyProperties row) {
    writable(live,edit.document_id);auto& part=*live.open_part(edit.document_id);
    if(part.runtime_identity!=edit.runtime_identity||part.session.revision()!=edit.revision||part.session.data_generation()!=edit.generation)
        throw Error("stale_edit","The document changed while body properties were open.");
    if(row.id!=edit.initial.id||row.body_id!=edit.initial.body_id||row.after_object_id!=edit.initial.after_object_id)
        throw Error("identity_changed","Body properties must retain their identity and history position.");
    const auto first=row.name.find_first_not_of(" \t\r\n"),last=row.name.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)throw Error("invalid_name","Enter a body properties name.");
    row.name=row.name.substr(first,last-first+1);document::validate_native_metadata_text(row.name);
    auto next=part.session.document();
    if(std::ranges::any_of(next.body_properties,[&](const auto& r){return r.id!=row.id&&r.name==row.name;}))
        throw Error("duplicate_name","A body properties feature with this name already exists.");
    row=document::evaluate_body_properties(next,part.session.calculated_boundaries(),std::move(row));
    // Permit editing/removing a broken existing feature; creation requires a solid.
    if(edit.creating&&!row.error.empty())throw std::invalid_argument(row.error);
    static_cast<void>(document::parse_body_properties(document::serialize_body_properties({row})));
    if(!edit.creating&&row==edit.initial)return false;
    if(edit.creating)next.body_properties.push_back(std::move(row));
    else *std::ranges::find(next.body_properties,row.id,&document::BodyProperties::id)=std::move(row);
    part.session.commit(std::move(next),part.session.calculated_boundaries());return true;
}
void remove_body_properties(Workspace& live,const std::string& id,const std::string& object) {
    writable(live,id);auto& part=*live.open_part(id);auto next=part.session.document();
    if(!std::erase_if(next.body_properties,[&](const auto& r){return r.id==object;}))
        throw Error("body_properties_not_found","The body properties feature does not exist.");
    part.session.commit(std::move(next),part.session.calculated_boundaries());
}
} // namespace zima::workspace
