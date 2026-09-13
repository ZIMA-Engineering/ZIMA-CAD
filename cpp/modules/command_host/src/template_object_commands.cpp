#include <zima/command_host/host.hpp>
#include <zima/workspace/template_object_operations.hpp>
#include <zima/drawing_render/template_image_import.hpp>
#include <zima/kernel/stable_id.hpp>
#include <cmath>
#include <algorithm>
#include <type_traits>
namespace zima::command_host {
namespace {
using Image=sketcher::TemplateImage;using Region=sketcher::SketchRepeatRegion;
Json properties(const Image& v) {
    return {{"image",v.id},{"name",v.name},{"format",v.format},{"embedded_bytes_base64",v.data_base64.size()},
        {"x_mm",v.x},{"y_mm",v.y},{"width_mm",v.width},{"height_mm",v.height},{"intrinsic_size",{v.pixel_width,v.pixel_height}},
        {"horizontal",v.horizontal},{"vertical",v.vertical},{"lock_aspect",v.lock_aspect},{"value_locks",v.value_locks},{"corners_mm",v.corners()}};
}
Json properties(const Region& v) {
    return {{"region",v.id},{"x_mm",v.x},{"y_mm",v.y},{"width_mm",v.width},{"height_mm",v.height},
        {"step_mm",v.step},{"direction",v.direction},{"value_locks",v.value_locks}};
}
void rejected(const char* code,const char* message){throw workspace::TemplateOperationError(code,message);}
template<class Value> void locks(Value& next,const Json& args) {
    if(!args.contains("value_locks"))return;
    for(const auto& key:args.at("value_locks"))if(!key.is_string())rejected("invalid_arguments","Template value locks must be an array of numeric field names.");
    next.value_locks=args.at("value_locks").get<std::set<std::string>>();
}
template<class Value> bool locked(const Value& before,const Value& after,const std::string& key) {
    return before.value_locks.contains(key)&&after.value_locks.contains(key);
}
template<class Value> void numeric(Value& next,const Value& before,const Json& args,const char* key,double Value::*field) {
    const auto arg=std::string(key)+"_mm";if(!args.contains(arg))return;
    const double value=args.at(arg).get<double>();
    if(!std::isfinite(value))rejected("invalid_arguments","Template coordinates and dimensions must be finite numbers.");
    if(value!=before.*field&&locked(before,next,key))rejected("parameter_locked","Unlock the template numeric value before changing it.");
    next.*field=value;
}
void patch(Image& next,const Image& before,const Json& args,bool replaced) {
    locks(next,args);numeric(next,before,args,"x",&Image::x);numeric(next,before,args,"y",&Image::y);
    numeric(next,before,args,"width",&Image::width);numeric(next,before,args,"height",&Image::height);
    if(args.contains("horizontal"))next.horizontal=args.at("horizontal");if(args.contains("vertical"))next.vertical=args.at("vertical");
    if(args.contains("lock_aspect"))next.lock_aspect=args.at("lock_aspect").get<bool>();
    if(next.lock_aspect&&(replaced||args.contains("width_mm")||args.contains("height_mm")||(args.contains("lock_aspect")&&!before.lock_aspect))) {
        const double ratio=next.pixel_width/next.pixel_height;
        const auto agrees=[](double a,double b){return std::abs(a-b)<=1e-9*std::max({1.0,std::abs(a),std::abs(b)});};
        if(args.contains("width_mm")&&args.contains("height_mm")&&!agrees(next.width,next.height*ratio))
            rejected("invalid_arguments","The requested image dimensions do not preserve its aspect ratio.");
        const bool width_locked=locked(before,next,"width"),height_locked=locked(before,next,"height");
        if(!(width_locked&&height_locked)) {
            const bool by_height=(args.contains("height_mm")&&!args.contains("width_mm"))||height_locked;
            if(by_height) {
                const double width=next.height*ratio;
                if((width_locked&&!agrees(width,before.width))||(args.contains("width_mm")&&!agrees(width,next.width)))
                    rejected("parameter_locked","Unlock the template numeric value before changing it.");
                next.width=width;
            }else {
                const double height=next.width/ratio;
                if((height_locked&&!agrees(height,before.height))||(args.contains("height_mm")&&!agrees(height,next.height)))
                    rejected("parameter_locked","Unlock the template numeric value before changing it.");
                next.height=height;
            }
        }
    }
}
void patch(Region& next,const Region& before,const Json& args,bool) {
    locks(next,args);numeric(next,before,args,"x",&Region::x);numeric(next,before,args,"y",&Region::y);
    numeric(next,before,args,"width",&Region::width);numeric(next,before,args,"height",&Region::height);numeric(next,before,args,"step",&Region::step);
    if(args.contains("direction"))next.direction=args.at("direction");
}
}
void Host::register_template_object_commands() {
    using Type=commands::ArgumentType;
    const auto add=[this](commands::Command declaration,std::function<Result(const Json&)> operation){
        dispatcher_.add(std::move(declaration),[this,operation=std::move(operation)](const Json& args){try{return operation(args);}
            catch(const workspace::TemplateOperationError& error){return Result::failure(error.code,tr(error.what()));}
            catch(const std::exception& error){return Result::failure("template_rejected",tr(error.what()));}});
    };
    const auto configure=[&]<class Value>(const char* key,auto read,auto commit,auto remove) {
        const std::string prefix=std::string("template.")+key;
        const auto objects=[](const sketcher::Sketch& sketch)->const auto& {
            if constexpr(std::is_same_v<Value,Image>)return sketch.drawing_template->images;
            else return sketch.drawing_template->repeat_regions;
        };
        add({prefix+".list",tr("List embedded template objects without calculating geometry."),{{"document",false}},false},[this,objects](const Json& args){
            const auto id=args.value("document",workspace_.active_document_id());auto items=Json::array();
            for(const auto& item:objects(workspace::drawing_template_sketch(workspace_,id)))items.push_back(properties(item));
            return Result::success({{"document",id},{"total",items.size()},{"items",std::move(items)}});
        });
        add({prefix+".get",tr("Read the properties of an embedded template object."),{{key,true},{"document",false}},false},[this,key,read](const Json& args){
            const auto id=args.value("document",workspace_.active_document_id());auto data=properties(read(workspace_,id,args.at(key)));data["document"]=id;return Result::success(std::move(data));
        });
        add({prefix+".remove",tr("Remove an embedded template object in one undoable operation."),{{key,true},{"document",false}},true},[this,key,remove](const Json& args){
            const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
            const bool changed=remove(workspace_,id,args.at(key));if(changed)change_=Change{ChangeKind::Model,id,true};
            return Result::success({{"document",id},{key,args.at(key)},{"changed",changed},{"body_calculated",false}});
        });
        for(const bool create:{true,false}) {
            std::vector<commands::Argument> fields;if(!create)fields.push_back({key,true});
            for(const auto* name:{"x_mm","y_mm","width_mm","height_mm"})fields.push_back({name,create&&(!std::is_same_v<Value,Image>||std::string_view(name)=="x_mm"||std::string_view(name)=="y_mm"),Type::Number});
            fields.push_back({"value_locks",false,Type::Array});fields.push_back({"document",false});
            if constexpr(std::is_same_v<Value,Image>) {
                fields.push_back({"path",create});fields.push_back({"horizontal",false});fields.push_back({"vertical",false});fields.push_back({"lock_aspect",false,Type::Boolean});
            }else {fields.push_back({"direction",false});fields.push_back({"step_mm",false,Type::Number});}
            add({prefix+(create?".create":".set"),tr("Create or edit an embedded template object with one Undo step."),fields,true},[this,key,create,read,commit](const Json& args){
                const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
                const auto& sketch=workspace::drawing_template_sketch(workspace_,id);
                if(create&&sketch.drawing_template->kind!="title_block")rejected("unsupported_document","Images and repeat regions can only be created in a title block.");
                Value before;if(!create)before=read(workspace_,id,args.at(key));else before.id=kernel::make_stable_id();auto next=before;
                bool replaced=false;
                if constexpr(std::is_same_v<Value,Image>) {
                    if(args.contains("path")) {
                        auto path=std::filesystem::u8path(args.at("path").get<std::string>());if(path.is_relative())path=directory_/path;
                        auto image=drawing_render::read_template_image(path);next.name=std::move(image.name);next.data_base64=std::move(image.data_base64);next.format=std::move(image.format);
                        next.pixel_width=image.pixel_width;next.pixel_height=image.pixel_height;replaced=true;
                        if(create){next.width=image.width;next.height=image.height;}
                    }
                }else if(create&&!args.contains("step_mm"))next.step=args.at("height_mm").get<double>();
                patch(next,before,args,replaced);const bool changed=commit(workspace_,id,create?std::string{}:before.id,std::move(next));
                if(changed)change_=Change{ChangeKind::Model,id,true};auto data=properties(read(workspace_,id,before.id));
                data["document"]=id;data["revision"]=workspace_.open_part(id)->session.revision();data["changed"]=changed;data["body_calculated"]=false;return Result::success(std::move(data));
            });
        }
    };
    configure.template operator()<Image>("image",workspace::template_image,workspace::commit_template_image,workspace::remove_template_image);
    configure.template operator()<Region>("region",workspace::template_region,workspace::commit_template_region,workspace::remove_template_region);
}
}
