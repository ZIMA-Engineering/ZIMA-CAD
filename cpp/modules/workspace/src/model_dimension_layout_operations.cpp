#include <zima/workspace/model_dimension_layout_operations.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
const std::vector<kernel::DimensionLayoutEntry>& entries(const Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.document().dimension_layouts;
    if(const auto* assembly=live.open_assembly(id))return assembly->session.document().dimension_layouts;
    throw ModelDimensionLayoutError("unsupported_document","Model dimension properties require an open Part or Assembly.");
}
void validate(const kernel::DimensionLayout& layout) {
    try{kernel::validate_dimension_layout(layout);}catch(const std::invalid_argument&){throw ModelDimensionLayoutError("invalid_arguments","Invalid model dimension layout.");}
    if(!layout.text_style)return;const auto& style=*layout.text_style;
    if(style.decimals<0||style.decimals>12||(style.tolerance_mode!=""&&style.tolerance_mode!="symmetric"&&style.tolerance_mode!="single_deviation"&&style.tolerance_mode!="deviations"))
        throw ModelDimensionLayoutError("invalid_arguments","Invalid model dimension text style.");
    for(const auto* text:{&style.prefix,&style.suffix,&style.text_override,&style.tolerance_mode,&style.symmetric_tolerance,&style.single_tolerance,&style.upper_tolerance,&style.lower_tolerance})
        if(text->size()>2048)throw ModelDimensionLayoutError("invalid_arguments","Invalid model dimension text style.");
}
}
std::vector<document::DimensionParameter> model_dimension_parameters(const Workspace& live,const std::string& id) {
    std::vector<document::DimensionParameter> result;
    if(const auto* part=live.open_part(id)) {
        const auto& doc=part->session.document();result=doc.dimension_parameters();
        for(const auto& body:doc.body_history.bodies())
            document::append_placement_dimension_parameters(result,body.scope.id,body.name,body.scope.placement.references);
    }else if(const auto* assembly=live.open_assembly(id))result=assembly->session.document().dimension_parameters();
    else throw ModelDimensionLayoutError("unsupported_document","Model dimension properties require an open Part or Assembly.");
    // The Point coordinate display and the generic placement editor already
    // expose these two semantic forms of the same native coordinate slot.
    // Describe both existing View identities; do not allocate dimension numbers
    // or rewrite persisted appearance keys when switching between the editors.
    const auto native_count=result.size();
    for(std::size_t i=0;i<native_count;++i) {
        auto parameter=result[i];
        if(parameter.semantic_key!="parameter:x"&&parameter.semantic_key!="parameter:y"&&parameter.semantic_key!="parameter:z")continue;
        parameter.semantic_key="parameter:placement:"+parameter.semantic_key.substr(10);
        if(std::ranges::none_of(result,[&](const auto& item){return item.owner_id==parameter.owner_id&&item.semantic_key==parameter.semantic_key;}))
            result.push_back(std::move(parameter));
    }
    return result;
}
ModelDimensionLayoutInfo read_model_dimension_layout(const Workspace& live,const std::string& id,const kernel::EdgeReference& reference) {
    const auto& stored=entries(live,id);
    const auto expected=id==live.active_document_id()?live.active_occurrence_path():std::string{};
    if(reference.instance_path!=expected)throw ModelDimensionLayoutError("wrong_occurrence","The dimension reference is outside the addressed editing occurrence.");
    const auto parameters=model_dimension_parameters(live,id);
    const document::DimensionParameter* found=nullptr;
    for(const auto& parameter:parameters)if(parameter.owner_id==reference.owner_id&&parameter.semantic_key==reference.semantic_key) {
        if(found)throw ModelDimensionLayoutError("ambiguous_reference","The model dimension reference is ambiguous.");found=&parameter;
    }
    if(!found)throw ModelDimensionLayoutError("dimension_not_found","The model parameter dimension does not exist.");
    ModelDimensionLayoutInfo result{*found,{}};
    if(const auto* value=kernel::find_dimension_layout(stored,reference))result.stored_layout=*value;
    return result;
}
bool set_model_dimension_layout(Workspace& live,const std::string& id,const kernel::EdgeReference& reference,std::optional<kernel::DimensionLayout> layout) {
    if(id!=live.active_document_id()||reference.instance_path!=live.active_occurrence_path())
        throw ModelDimensionLayoutError("wrong_occurrence","The dimension reference is outside the addressed editing occurrence.");
    const auto current=read_model_dimension_layout(live,id,reference);
    if(const auto* part=live.open_part(id)) {
        const auto& doc=part->session.document();
        if(!doc.body_history.find(reference.owner_id))if(const auto* body=doc.body_owner_for_object(reference.owner_id);body&&body->scope.id!=doc.body_history.active_body_id())
            throw ModelDimensionLayoutError("inactive_body","Activate the owning Body before editing its dimension properties.");
    }
    if(layout)validate(*layout);
    if(current.stored_layout==layout||(!current.stored_layout&&layout==default_model_dimension_layout()))return false;
    const auto update=[&](auto& values) {
        if(layout)kernel::store_dimension_layout(values,reference,*layout);
        else std::erase_if(values,[&](const auto& item){return item.owner_id==reference.owner_id&&item.semantic_key==reference.semantic_key;});
    };
    if(auto* part=live.open_part(id)) {
        auto next=part->session.document();update(next.dimension_layouts);part->session.commit(std::move(next),part->session.calculated_boundaries());
    }else {
        auto* assembly=live.open_assembly(id);auto next=assembly->session.document();update(next.dimension_layouts);assembly->session.commit(std::move(next));
    }
    return true;
}
}
