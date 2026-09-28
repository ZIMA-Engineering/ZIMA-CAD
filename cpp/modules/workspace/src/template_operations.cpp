#include <zima/workspace/template_operations.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <zima/document/metadata.hpp>
#include <zima/workspace/document_operations.hpp>
#include <algorithm>
#include <cctype>
namespace zima::workspace {
namespace {
std::string text(const std::filesystem::path& path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
bool title_path(const std::filesystem::path& path) {
    auto ext=path.extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return std::tolower(c);});
    if(ext!=".tblz"&&ext!=".frmz")throw TemplateOperationError("unsupported_format","A drawing template requires a tblz or frmz extension.");return ext==".tblz";
}
bool same_path(const std::filesystem::path& first,const std::filesystem::path& second) {
    if(first.empty()||second.empty())return false;
    if(std::filesystem::absolute(first).lexically_normal()==std::filesystem::absolute(second).lexically_normal())return true;
    std::error_code error;return std::filesystem::equivalent(first,second,error)&&!error;
}
std::optional<std::string> opened_at_path(const Workspace& live,const std::filesystem::path& path) {
    for(const auto& state:live.documents()) {
        const auto found=std::visit([&](const auto& value)->std::optional<std::string> {
            if(!same_path(value.path,path))return {};
            if constexpr(requires {value.session.document();})return value.session.document().document_id;
            else return value.document().document_id;
        },state);
        if(found)return found;
    }
    return {};
}
void check_target(const Workspace& live,const std::filesystem::path& path,const std::string& id,bool overwrite) {
    if(const auto owner=opened_at_path(live,path);owner&&*owner!=id)throw TemplateOperationError("path_in_use","Another document is already open at the target path.");
    if(std::filesystem::exists(path)&&(!overwrite||!std::filesystem::is_regular_file(path)))throw TemplateOperationError("path_exists","The target file already exists.");
    if(!std::filesystem::is_directory(path.parent_path()))throw TemplateOperationError("invalid_directory","The target directory does not exist.");
}
}
bool is_drawing_template(const Workspace& live,const std::string& id) {
    const auto* part=live.open_part(id);return part&&!part->session.document().sketches.empty()&&part->session.document().sketches.front().drawing_template.has_value();
}
const sketcher::Sketch& drawing_template_sketch(const Workspace& live,const std::string& id,const std::string& selection) {
    if(!is_drawing_template(live,id))throw TemplateOperationError("unsupported_document","This command requires an open drawing template.");
    if(!selection.empty())for(const auto& sketch:live.open_part(id)->session.document().sketches) {
        if(!sketch.drawing_template)continue;
        if(sketch.id==selection||std::ranges::any_of(sketch.drawing_template->repeat_regions,[&](const auto& value){return value.id==selection;})||
            std::ranges::any_of(sketch.drawing_template->images,[&](const auto& value){return value.id==selection;}))return sketch;
    }
    if(!selection.empty())throw TemplateOperationError("object_not_found","The template object does not exist.");
    return live.open_part(id)->session.document().sketches.front();
}
document::PartDocument template_part_from_sketch(sketcher::Sketch sketch,std::string name) {
    if(!sketch.drawing_template)throw TemplateOperationError("unsupported_document","This command requires an open drawing template.");
    sketch.validate();auto document=document::PartDocument::create_default();document.name=std::move(name);
    auto container=document::PartDocument::create_sketch_container();container.name=sketch.name;sketch.owner_container_id=container.id;
    document.insert_history_entry(document::PartHistoryKind::Feature,container.id);document.history.push_back(std::move(container));document.sketches.push_back(std::move(sketch));return document;
}
std::string create_drawing_template(Workspace& live,bool title,const std::string& name,const std::filesystem::path& target) {
    if(name.empty()||name=="."||name==".."||name.find_first_of("\\/:*?\"<>|\r\n\t")!=std::string::npos||name.back()=='.'||name.back()==' '||
        std::ranges::all_of(name,[](unsigned char c){return std::isspace(c)!=0;}))throw TemplateOperationError("invalid_name","Specify a template name without a path or extension.");
    document::validate_native_metadata_text(name);const auto path=std::filesystem::absolute(target).lexically_normal();
    if(title_path(path)!=title)throw TemplateOperationError("unsupported_format","The template kind does not match its extension.");
    check_target(live,path,{},false);auto document=template_part_from_sketch(drawing::create_template_sketch(title,name),name);
    document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body(name));
    bodies.insert({document::PartHistoryKind::Feature,document.history.front().id});document.set_body_history(bodies);
    const auto id=document.document_id;live.add_part(std::move(document),{},path);live.open_part(id)->native_drawing_template=true;return id;
}
std::string open_drawing_template(Workspace& live,const std::filesystem::path& target) {
    const auto path=std::filesystem::absolute(target).lexically_normal();static_cast<void>(title_path(path));
    if(const auto opened=opened_at_path(live,path)){static_cast<void>(drawing_template_sketch(live,*opened));return *opened;}
    if(drawing::is_native_template_file(path)) {
        std::vector<kernel::BodyResult> cache;auto document=document::PartDocument::load(path,&cache);
        const auto id=document.document_id;live.add_part(std::move(document),std::move(cache),path);
        live.open_part(id)->native_drawing_template=true;return id;
    }
    auto sketch=drawing::load_template_sketch(path,[](auto& text){sketcher::rebuild_text_contours(text,true);});
    auto document=template_part_from_sketch(std::move(sketch),text(path.stem()));const auto id=document.document_id;
    live.add_part(std::move(document),{},path);return id;
}
void save_drawing_template(Workspace& live,const std::string& id,const std::filesystem::path& target,bool copy,bool overwrite) {
    const auto& sketch=drawing_template_sketch(live,id);auto* state=live.open_part(id);
    const auto path=std::filesystem::absolute(target).lexically_normal(),source=std::filesystem::absolute(state->path).lexically_normal();
    if(title_path(path)!=(sketch.drawing_template->kind=="title_block"))throw TemplateOperationError("unsupported_format","The template kind does not match its extension.");
    if(copy&&same_path(path,source))throw TemplateOperationError("invalid_path","A template copy must use a different target path.");
    check_target(live,path,id,overwrite||(!copy&&same_path(path,source)));live.reserve_file(path);
    if(state->native_drawing_template) {
        if(copy)static_cast<void>(live.save_copy(id,path,{},overwrite));
        else {const auto saved=prepare_document_save(live,id,path).write();static_cast<void>(complete_document_save(live,saved));}
        return;
    }
    drawing::save_template_sketch(sketch,path);
    if(!copy){state->path=path;state->session.mark_saved();}
}
bool commit_template_sketch(Workspace& live,const std::string& id,sketcher::Sketch sketch) {
    const auto* state=live.open_part(id);
    if(!state)throw TemplateOperationError("unsupported_document","This command requires an open drawing template.");
    const auto found=std::ranges::find(state->session.document().sketches,sketch.id,&sketcher::Sketch::id);
    if(found==state->session.document().sketches.end())throw TemplateOperationError("identity_changed","Editing must preserve the template and Sketch identities.");
    const auto& original=*found;
    if(!sketch.drawing_template||!original.drawing_template||sketch.id!=original.id||sketch.owner_container_id!=original.owner_container_id||sketch.drawing_template->kind!=original.drawing_template->kind)
        throw TemplateOperationError("identity_changed","Editing must preserve the template and Sketch identities.");
    if(!sketch.external_references.empty())throw TemplateOperationError("invalid_reference","A drawing template cannot depend on external model references.");
    sketch.validate();for(const auto& region:sketch.drawing_template->repeat_regions)drawing::validate_repeat_region(region);
    for(const auto& image:sketch.drawing_template->images)image.validate();
    if(sketch.serialized()==original.serialized())return false;
    auto* part=live.open_part(id);auto next=part->session.document();*std::ranges::find(next.sketches,sketch.id,&sketcher::Sketch::id)=std::move(sketch);part->session.commit(std::move(next),part->session.calculated_boundaries());return true;
}
}
