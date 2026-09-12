#include <zima/workspace/drawing_title_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/document/file_path.hpp>
#include <algorithm>
#include <cctype>
namespace zima::workspace {
namespace {
std::string extension(const std::filesystem::path& path){auto result=path.extension().string();std::ranges::transform(result,result.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return result;}
std::string parameter_identity(const std::string& token,const drawing::TitleBlockContext& context) {
    const auto scope=drawing::title_block_token_scope(token);
    return scope+":"+(scope=="model"?drawing::title_block_parameter_key(token,context):token.substr(token.find('.')+1));
}
[[noreturn]] void stale(){throw DrawingOperationError("source_changed","The title block or source parameter changed. Reopen its properties.");}
document::UserParameterData parameters(const drawing::TitleBlockContext& context) {
    document::UserParameterData result{context.parameters,context.parameter_order,context.parameter_labels,context.parameter_values};
    for(const auto& [name,value]:result.flat) {
        if(std::ranges::find(result.order,name)==result.order.end())result.order.push_back(name);
        result.values[name][""]=value;
    }
    document::normalize_user_parameters(result);return result;
}
template<class Model> document::UserParameterData model_parameters(const Model& model) {
    drawing::TitleBlockContext context;context.parameters=model.user_parameters;context.parameter_order=model.user_parameter_order;
    context.parameter_labels=model.user_parameter_labels;context.parameter_values=model.user_parameter_values;return parameters(context);
}
}
bool drawing_title_field_writable(const drawing::TitleBlockField& field,const DrawingTitleEdit& edit) {
    if(!field.editable)return false;const auto tokens=drawing::title_block_tokens(field.expression);
    if(tokens.empty())return true;
    if(tokens.size()!=1||field.expression!="&"+tokens.front())return false;
    const auto scope=drawing::title_block_token_scope(tokens.front());
    return scope=="drawing"||(scope=="model"&&field.write_back&&!edit.calculated.contains(drawing::title_block_parameter_key(tokens.front(),edit.context)));
}
std::string drawing_title_focus_field(const DrawingTitleEdit& edit,const std::string& expression) {
    for(const auto& token:drawing::title_block_tokens(expression)) {
        if(drawing::title_block_token_scope(token)=="system")continue;
        const auto identity=parameter_identity(token,edit.context);
        for(const auto& field:edit.fields) {
            const auto tokens=drawing::title_block_tokens(field.expression);
            if(tokens.size()==1&&field.expression=="&"+tokens.front()&&parameter_identity(tokens.front(),edit.context)==identity)return field.id;
        }
    }
    return {};
}
DrawingTitleEdit prepare_drawing_title_edit(const drawing::DrawingDocument& doc,const std::string& sheet_id,
    const Workspace* live,const std::filesystem::path& path,const std::string& bom_row) {
    const auto* sheet=doc.find_sheet(sheet_id);if(!sheet)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
    DrawingTitleEdit edit;edit.sheet=sheet_id;edit.drawing_path=path;edit.bom_row=bom_row;
    edit.source_document=sheet->views.empty()?doc.source_document_id:sheet->views.front().source_document_id;
    edit.source_path=sheet->views.empty()?doc.source_path:sheet->views.front().source_path;
    if(!edit.source_path.empty()&&edit.source_path.is_relative()&&!path.empty())edit.source_path=path.parent_path()/edit.source_path;
    if(live&&!live->find(edit.source_document)&&!edit.source_path.empty())if(const auto open=live->document_id_for_path(edit.source_path))edit.source_document=*open;
    if(!bom_row.empty()) {
        if(std::ranges::none_of(sheet->bom_rows,[&](const auto& row){return row.designation==bom_row;}))throw DrawingOperationError("bom_row_not_found","The stored BOM row does not exist.");
        const auto current=build_bom_rows_for_source(edit.source_document,edit.source_path,live);
        const auto row=std::ranges::find(current,bom_row,&drawing::BomRow::designation);
        if(row==current.end())throw DrawingOperationError("source_changed","Regenerate the drawing before editing this changed BOM row.");
        edit.source_document=row->source_document_id;edit.source_path=row->source_path;
    }
    edit.context=build_title_block_context_for_source(edit.source_document,edit.source_path,live);
    edit.context.sheet_index=static_cast<int>(sheet-doc.sheets.data());edit.context.sheet_count=static_cast<int>(doc.sheets.size());
    const auto collect=[&](const auto& model) {
        if(!edit.source_document.empty()&&model.document_id!=edit.source_document)throw DrawingOperationError("source_changed","The drawing source document identity changed.");
        edit.source_document=model.document_id;for(const auto& relation:model.relations)edit.calculated.insert(relation.target);
    };
    if(live&&live->open_part(edit.source_document))collect(live->open_part(edit.source_document)->session.document());
    else if(live&&live->open_assembly(edit.source_document))collect(live->open_assembly(edit.source_document)->session.document());
    else if(extension(edit.source_path)==".prtz")collect(document::PartDocument::load(edit.source_path));
    else if(extension(edit.source_path)==".asmz")collect(assembly::AssemblyDocument::load(edit.source_path));
    edit.fields=sheet->title_block_fields;
    const auto add_parameters=[&](const std::string& expression) {
        for(const auto& token:drawing::title_block_tokens(expression)) {
            if(drawing::title_block_token_scope(token)=="system")continue;
            const auto identity=parameter_identity(token,edit.context);
            if(std::ranges::any_of(edit.fields,[&](const auto& f){const auto tokens=drawing::title_block_tokens(f.expression);return tokens.size()==1&&f.expression=="&"+tokens.front()&&parameter_identity(tokens.front(),edit.context)==identity;}))continue;
            drawing::TitleBlockField field;field.id="parameter:"+token;field.expression="&"+token;field.editable=true;field.write_back=true;edit.fields.push_back(std::move(field));
        }
    };
    for(const auto& field:sheet->title_block_fields)add_parameters(field.expression);
    for(const auto& text:sheet->title_block_texts)add_parameters(text.text);
    const auto order=[&](const auto& field) {
        const auto tokens=drawing::title_block_tokens(field.expression);
        if(tokens.size()==1&&field.expression=="&"+tokens.front()&&drawing::title_block_token_scope(tokens.front())=="model")
            return std::ranges::find(edit.context.parameter_order,drawing::title_block_parameter_key(tokens.front(),edit.context))-edit.context.parameter_order.begin();
        return edit.context.parameter_order.end()-edit.context.parameter_order.begin();
    };
    std::stable_sort(edit.fields.begin(),edit.fields.end(),[&](const auto& a,const auto& b){return order(a)<order(b);});
    for(const auto& field:edit.fields)edit.initial_values[field.id]=drawing::resolve_title_block_text(field,edit.context,*sheet);
    return edit;
}
DrawingTitleChange edit_drawing_title(drawing::DrawingDocument& doc,Workspace* live,const DrawingTitleEdit& initial,
    const std::map<std::string,std::string>& changes) {
    if(changes.empty())return {};
    const auto current=prepare_drawing_title_edit(doc,initial.sheet,live,initial.drawing_path,initial.bom_row);
    if(current.source_document!=initial.source_document||current.source_path.lexically_normal()!=initial.source_path.lexically_normal())stale();
    auto next=doc;auto* sheet=next.find_sheet(current.sheet);std::map<std::string,std::string> updates,local_updates;bool changed=false;
    for(const auto& [id,value]:changes) {
        const auto field=std::ranges::find(current.fields,id,&drawing::TitleBlockField::id),before=std::ranges::find(initial.fields,id,&drawing::TitleBlockField::id);
        if(field==current.fields.end()||before==initial.fields.end())throw DrawingOperationError("field_not_found","The title block field does not exist.");
        if(!drawing_title_field_writable(*field,current))throw DrawingOperationError("read_only","The title block field is read-only.");
        const auto& now=current.initial_values.at(id);
        if(field->expression!=before->expression||!initial.initial_values.contains(id)||now!=initial.initial_values.at(id))stale();
        if(now==value)continue;
        const auto tokens=drawing::title_block_tokens(field->expression);
        if(tokens.size()==1&&parameter_identity(tokens.front(),current.context)!=parameter_identity(tokens.front(),initial.context))stale();
        if(tokens.empty()) {
            auto stored=std::ranges::find(sheet->title_block_fields,id,&drawing::TitleBlockField::id);
            if(stored==sheet->title_block_fields.end())throw DrawingOperationError("field_not_found","The title block field does not exist.");
            stored->expression=value;stored->value=value;changed=true;
        } else if(drawing::title_block_token_scope(tokens.front())=="drawing") {
            const auto key=tokens.front().substr(tokens.front().find('.')+1);
            if(local_updates.contains(key)&&local_updates.at(key)!=value)throw DrawingOperationError("invalid_arguments","Conflicting values for one title block parameter.");
            local_updates[key]=value;changed=true;
        } else {
            const auto key=drawing::title_block_parameter_key(tokens.front(),current.context);
            if(updates.contains(key)&&updates.at(key)!=value)throw DrawingOperationError("invalid_arguments","Conflicting values for one title block parameter.");
            updates[key]=value;
        }
    }
    for(const auto& [key,value]:local_updates)sheet->local_parameters[key]=value;
    DrawingTitleChange result{changed,false,current.source_document};
    if(!updates.empty()) {
        if(!live)throw DrawingOperationError("source_unavailable","Open the source model in the workspace to edit its parameters.");
        auto data=parameters(current.context);
        for(const auto& [key,value]:updates) {
            auto& values=data.values[key];if(values.empty()||values.contains(""))values[""]=value;else values[sheet->title_block_locale]=value;
            if(std::ranges::find(data.order,key)==data.order.end())data.order.push_back(key);
        }
        document::normalize_user_parameters(data);
        if(!live->open_part(current.source_document)&&!live->open_assembly(current.source_document)) {
            if(extension(current.source_path)==".prtz") {
                std::vector<kernel::BodyResult> boundaries;auto model=document::PartDocument::load(current.source_path,&boundaries);
                if(model.document_id!=current.source_document||model_parameters(model)!=parameters(current.context))stale();live->add_part(std::move(model),std::move(boundaries),current.source_path);
            } else if(extension(current.source_path)==".asmz") {
                auto model=assembly::AssemblyDocument::load(current.source_path);if(model.document_id!=current.source_document||model_parameters(model)!=parameters(current.context))stale();live->add_assembly(std::move(model),current.source_path);
            } else throw DrawingOperationError("source_unavailable","The source model is unavailable.");
        }
        result.source_changed=set_user_parameters(*live,current.source_document,std::move(data));result.changed=result.changed||result.source_changed;
        // Parameter relations can change other values. Mirror the committed source,
        // not the proposed input, without recalculating any body geometry.
        const auto fresh=build_title_block_context_for_source(current.source_document,current.source_path,live);
        const auto identity=current.source_document+"|"+document::path_to_utf8(current.source_path.lexically_normal());
        for(auto& target:next.sheets)for(auto& row:target.bom_rows)
            if(row.designation==identity||(row.source_document_id==current.source_document&&row.source_path.lexically_normal()==current.source_path.lexically_normal())) {
                row.parameters=fresh.parameters;row.parameter_values=fresh.parameter_values;row.parameter_aliases=fresh.parameter_aliases;
                row.file_stem=fresh.file_stem;row.mass_unit=fresh.mass_unit;
            }

    }
    if(result.changed)doc=std::move(next);
    return result;
}
}
