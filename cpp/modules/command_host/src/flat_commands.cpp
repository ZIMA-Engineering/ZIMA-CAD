#include <zima/command_host/host.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/document/flat.hpp>
#include <algorithm>
#include <cmath>

namespace zima::command_host {
namespace {
const document::HistoryContainer& flat(const workspace::PartState* state,const std::string& id) {
    if(!state)throw std::invalid_argument("Flat is available only in a Part.");
    const auto* feature=state->session.document().find_container(id);
    if(!feature||feature->feature_kind!=document::FeatureKind::Flat)throw std::invalid_argument("Flat container does not exist.");
    return *feature;
}
Json details(const workspace::PartState& state,const document::HistoryContainer& feature) {
    const auto& p=feature.flat;
    return {{"document",state.session.document().document_id},{"container",feature.id},{"feature",feature.feature_id},
        {"sketch",p.sketch_id},{"name",feature.name},
        {"thickness_mm",document::flat_thickness(feature,document::sheet_metal_defaults(state.session.document()))},
        {"thickness_override",p.thickness_override},
        {"direction",p.direction==document::ExtrusionDirection::Forward?"forward":p.direction==document::ExtrusionDirection::Reverse?"reverse":"symmetric"},
        {"revision",state.session.revision()}};
}
}
void Host::register_flat_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"flat.get",tr("Read a Flat sheet and its effective document thickness without calculation."),
        {{"container",true},{"document",false}},false},[this](const Json& args){
        try{const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));const auto& feature=flat(state,args.at("container"));return Result::success(details(*state,feature));}
        catch(const std::exception& e){return Result::failure("flat_rejected",tr(e.what()));}
    });
    for(bool create:{true,false}) {
        std::vector<commands::Argument> arguments;
        if(create){arguments.push_back({"width_mm",false,Type::Number});arguments.push_back({"height_mm",false,Type::Number});}
        else arguments.push_back({"container",true});
        arguments.push_back({"thickness_mm",false,Type::Number});arguments.push_back({"thickness_override",false,Type::Boolean});
        for(const auto* key:{"direction","name","document"})arguments.push_back({key,false});
        dispatcher_.add({create?"flat.create":"flat.set",tr("Create or edit a Flat sheet from an owned closed Sketch."),arguments,true},[this,create](const Json& args){
            if(auto result=target(args);!result.ok)return result;
            try {
                const auto id=workspace_.active_document_id();const auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw std::invalid_argument("Flat is available only in a Part.");
                auto feature=create?document::PartDocument::create_sketch_container():flat(state,args.at("container"));
                sketcher::Sketch sketch;
                if(create) {
                    feature.feature_kind=document::FeatureKind::Flat;feature.name="Tabule";
                    sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;feature.flat.sketch_id=sketch.id;
                    feature.flat.thickness=document::sheet_metal_defaults(state->session.document()).thickness_mm.value_or(1);
                    const double width=args.value("width_mm",40.0),height=args.value("height_mm",30.0);
                    for(double size:{width,height})if(!std::isfinite(size)||size<.001||size>1000000)
                        throw std::invalid_argument("Flat dimensions must be between 0.001 and 1000000 mm.");
                    static_cast<void>(sketch.add_rectangle(0,0,width,height));
                } else {
                    const auto found=std::ranges::find(state->session.document().sketches,feature.flat.sketch_id,&sketcher::Sketch::id);
                    if(found==state->session.document().sketches.end())throw std::invalid_argument("Flat source Sketch is missing.");
                    sketch=*found;
                }
                if(args.contains("thickness_mm")) {
                    const double next=args.at("thickness_mm");
                    if(feature.value_locks.contains("thickness")&&next!=feature.flat.thickness)throw std::invalid_argument("Unlock the thickness before changing it.");
                    feature.flat.thickness=next;feature.flat.thickness_override=true;
                }
                if(args.contains("thickness_override"))feature.flat.thickness_override=args.at("thickness_override");
                if(args.contains("direction")) {
                    const auto direction=args.at("direction").get<std::string>();
                    if(direction!="forward"&&direction!="reverse"&&direction!="symmetric")throw std::invalid_argument("Flat direction must be forward, reverse or symmetric.");
                    feature.flat.direction=direction=="forward"?document::ExtrusionDirection::Forward:direction=="reverse"?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Symmetric;
                }
                if(args.contains("name"))feature.name=args.at("name");sketch.name=feature.name;
                const auto container=feature.id;
                const bool changed=workspace::commit_flat(workspace_,kernel_,id,std::move(feature),std::move(sketch));
                if(changed)change_=Change{ChangeKind::Model,id};state=workspace_.open_part(id);
                auto result=details(*state,flat(state,container));result["changed"]=changed;return Result::success(std::move(result));
            } catch(const std::exception& e){return Result::failure("flat_rejected",tr(e.what()));}
        });
    }
}
}
