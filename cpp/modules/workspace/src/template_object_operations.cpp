#include <zima/workspace/template_object_operations.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
template<class Values> const auto& find(const Values& values,const std::string& id) {
    using Value=typename Values::value_type;
    const auto found=std::ranges::find(values,id,&Value::id);
    if(found==values.end())throw TemplateOperationError("object_not_found","The template object does not exist.");return *found;
}
template<class Values,class Value> void replace(Values& values,const std::string& previous,Value value,const std::string& kind) {
    if(previous.empty()) {
        if(kind!="title_block")throw TemplateOperationError("unsupported_document","Images and repeat regions can only be created in a title block.");
        if(std::ranges::any_of(values,[&](const auto& item){return item.id==value.id;}))throw TemplateOperationError("identity_changed","A template object with this identity already exists.");
        values.push_back(std::move(value));return;
    }
    static_cast<void>(find(values,previous));
    if(previous!=value.id)throw TemplateOperationError("identity_changed","Editing must preserve the template object identity.");
    *std::ranges::find(values,previous,&Value::id)=std::move(value);
}
void validate_locks(const std::set<std::string>& locks,bool region) {
    for(const auto& key:locks)if(key!="x"&&key!="y"&&key!="width"&&key!="height"&&!(region&&key=="step"))
        throw TemplateOperationError("unknown_parameter","The template numeric value lock key is not supported.");
}
}
const sketcher::TemplateImage& template_image(const Workspace& live,const std::string& id,const std::string& object) {
    return find(drawing_template_sketch(live,id).drawing_template->images,object);
}
const sketcher::SketchRepeatRegion& template_region(const Workspace& live,const std::string& id,const std::string& object) {
    return find(drawing_template_sketch(live,id).drawing_template->repeat_regions,object);
}
bool commit_template_image(Workspace& live,const std::string& id,const std::string& previous,sketcher::TemplateImage image) {
    image.validate();validate_locks(image.value_locks,false);auto sketch=drawing_template_sketch(live,id);
    replace(sketch.drawing_template->images,previous,std::move(image),sketch.drawing_template->kind);
    return commit_template_sketch(live,id,std::move(sketch));
}
bool commit_template_region(Workspace& live,const std::string& id,const std::string& previous,sketcher::SketchRepeatRegion region) {
    drawing::validate_repeat_region(region);validate_locks(region.value_locks,true);auto sketch=drawing_template_sketch(live,id);
    replace(sketch.drawing_template->repeat_regions,previous,std::move(region),sketch.drawing_template->kind);
    return commit_template_sketch(live,id,std::move(sketch));
}
bool remove_template_image(Workspace& live,const std::string& id,const std::string& object) {
    auto sketch=drawing_template_sketch(live,id);std::erase_if(sketch.drawing_template->images,[&](const auto& image){return image.id==object;});
    return commit_template_sketch(live,id,std::move(sketch));
}
bool remove_template_region(Workspace& live,const std::string& id,const std::string& object) {
    auto sketch=drawing_template_sketch(live,id);std::erase_if(sketch.drawing_template->repeat_regions,[&](const auto& region){return region.id==object;});
    return commit_template_sketch(live,id,std::move(sketch));
}
}
