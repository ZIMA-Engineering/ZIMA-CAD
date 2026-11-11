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
const sketcher::Sketch& image_sketch(const Workspace& live,
        const std::string& id,const std::string& selection) {
    // The template command host historically omits the Sketch identity because
    // a title-block document owns exactly one template Sketch. Preserve that
    // contract while ordinary Sketch images always provide their Sketch ID.
    if(selection.empty())return drawing_template_sketch(live,id);
    const auto* part=live.open_part(id);
    if(!part)throw TemplateOperationError("unsupported_document",
        "This command requires an open Part.");
    for(const auto& sketch:part->session.document().sketches) {
        if(sketch.id==selection)return sketch;
        if(std::ranges::any_of(sketch.reference_images,
                [&](const auto& image){return image.id==selection;}))
            return sketch;
        if(sketch.drawing_template&&std::ranges::any_of(
                sketch.drawing_template->images,
                [&](const auto& image){return image.id==selection;}))
            return sketch;
    }
    throw TemplateOperationError("object_not_found",
        "The Sketch image does not exist.");
}
bool commit_image_sketch(Workspace& live,const std::string& id,
        sketcher::Sketch sketch) {
    auto* part=live.open_part(id);
    if(!part)throw TemplateOperationError("unsupported_document",
        "This command requires an open Part.");
    const auto found=std::ranges::find(part->session.document().sketches,
        sketch.id,&sketcher::Sketch::id);
    if(found==part->session.document().sketches.end()||
       found->owner_container_id!=sketch.owner_container_id)
        throw TemplateOperationError("identity_changed",
            "Editing must preserve the Sketch identity.");
    for(const auto& image:sketch.reference_images)image.validate();
    if(sketch.serialized()==found->serialized())return false;
    auto next=part->session.document();
    *std::ranges::find(next.sketches,sketch.id,
        &sketcher::Sketch::id)=std::move(sketch);
    part->session.commit(std::move(next),
        part->session.calculated_boundaries());
    return true;
}
}
const sketcher::TemplateImage& template_image(const Workspace& live,const std::string& id,const std::string& object) {
    return find(drawing_template_sketch(live,id,object).drawing_template->images,object);
}
const sketcher::SketchRepeatRegion& template_region(const Workspace& live,const std::string& id,const std::string& object) {
    return find(drawing_template_sketch(live,id,object).drawing_template->repeat_regions,object);
}
bool commit_template_image(Workspace& live,const std::string& id,const std::string& previous,sketcher::TemplateImage image,const std::string& selected) {
    image.validate();validate_locks(image.value_locks,false);
    auto sketch=image_sketch(live,id,selected.empty()?previous:selected);
    if(sketch.drawing_template) {
        replace(sketch.drawing_template->images,previous,std::move(image),
            sketch.drawing_template->kind);
        return commit_template_sketch(live,id,std::move(sketch));
    }
    if(previous.empty()) {
        if(std::ranges::any_of(sketch.reference_images,
                [&](const auto& value){return value.id==image.id;}))
            throw TemplateOperationError("identity_changed",
                "A Sketch image with this identity already exists.");
        sketch.reference_images.push_back(std::move(image));
    } else {
        static_cast<void>(find(sketch.reference_images,previous));
        if(previous!=image.id)throw TemplateOperationError(
            "identity_changed",
            "Editing must preserve the Sketch image identity.");
        *std::ranges::find(sketch.reference_images,previous,
            &sketcher::TemplateImage::id)=std::move(image);
    }
    return commit_image_sketch(live,id,std::move(sketch));
}
bool commit_template_region(Workspace& live,const std::string& id,const std::string& previous,sketcher::SketchRepeatRegion region,const std::string& selected) {
    drawing::validate_repeat_region(region);validate_locks(region.value_locks,true);auto sketch=drawing_template_sketch(live,id,selected.empty()?previous:selected);
    replace(sketch.drawing_template->repeat_regions,previous,std::move(region),sketch.drawing_template->kind);
    return commit_template_sketch(live,id,std::move(sketch));
}
bool remove_template_image(Workspace& live,const std::string& id,const std::string& object) {
    const auto* part=live.open_part(id);
    if(!part)throw TemplateOperationError("unsupported_document",
        "This command requires an open Part.");
    const auto owner=std::ranges::find_if(part->session.document().sketches,
        [&](const auto& sketch) {
            const auto ordinary=std::ranges::any_of(sketch.reference_images,
                [&](const auto& image){return image.id==object;});
            const auto drawing=sketch.drawing_template&&std::ranges::any_of(
                sketch.drawing_template->images,
                [&](const auto& image){return image.id==object;});
            return ordinary||drawing;
        });
    if(owner==part->session.document().sketches.end())return false;
    auto sketch=*owner;
    if(sketch.drawing_template) {
        std::erase_if(sketch.drawing_template->images,
            [&](const auto& image){return image.id==object;});
        return commit_template_sketch(live,id,std::move(sketch));
    }
    std::erase_if(sketch.reference_images,
        [&](const auto& image){return image.id==object;});
    return commit_image_sketch(live,id,std::move(sketch));
}
bool remove_template_region(Workspace& live,const std::string& id,const std::string& object) {
    static_cast<void>(drawing_template_sketch(live,id));
    const auto& sketches=live.open_part(id)->session.document().sketches;
    if(std::ranges::none_of(sketches,[&](const auto& s){return s.drawing_template&&std::ranges::any_of(s.drawing_template->repeat_regions,[&](const auto& region){return region.id==object;});}))return false;
    auto sketch=drawing_template_sketch(live,id,object);std::erase_if(sketch.drawing_template->repeat_regions,[&](const auto& region){return region.id==object;});
    return commit_template_sketch(live,id,std::move(sketch));
}
}
