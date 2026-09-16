#include <zima/command_host/host.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/document/bend.hpp>
#include <algorithm>
#include <cmath>

namespace zima::command_host {
namespace {
const document::HistoryContainer& bend(const workspace::PartState* state,const std::string& id) {
    if(!state)throw std::invalid_argument("Bend is available only in a Part.");
    const auto* f=state->session.document().find_container(id);
    if(!f||f->feature_kind!=document::FeatureKind::Bend)throw std::invalid_argument("Bend container does not exist.");
    return *f;
}
Json details(const workspace::PartState& state,const document::HistoryContainer& feature) {
    const auto p=document::resolved_bend_parameters(feature,document::sheet_metal_defaults(state.session.document()));
    return {{"document",state.session.document().document_id},{"container",feature.id},{"feature",feature.feature_id},
        {"sketch",p.sketch_id},{"name",feature.name},{"radius_mm",p.radius},{"angle_degrees",p.angle_degrees},
        {"thickness_mm",p.thickness},{"k_factor",p.k_factor},{"thickness_override",p.thickness_override},
        {"k_factor_override",p.k_factor_override},{"state",p.unbend?"unbend":"bend"},{"revision",state.session.revision()}};
}
}
void Host::register_bend_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"bend.get",tr("Read a Bend and its effective document defaults without calculation."),
        {{"container",true},{"document",false}},false},[this](const Json& args){
        try{const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));const auto& feature=bend(state,args.at("container"));return Result::success(details(*state,feature));}
        catch(const std::exception& e){return Result::failure("bend_rejected",tr(e.what()));}
    });
    for(bool create:{true,false}) {
        std::vector<commands::Argument> arguments;
        // The GUI and console use the same atomic workspace operation.
        if(!create)arguments.push_back({"container",true});
        if(create)arguments.push_back({"width_mm",false,Type::Number});
        for(const auto* key:{"radius_mm","angle_degrees","thickness_mm","k_factor"})arguments.push_back({key,false,Type::Number});
        for(const auto* key:{"thickness_override","k_factor_override"})arguments.push_back({key,false,Type::Boolean});
        for(const auto* key:{"state","name","document"})arguments.push_back({key,false});
        dispatcher_.add({create?"bend.create":"bend.set",tr("Create or edit a sketch-based sheet metal Bend/Unbend."),arguments,true},[this,create](const Json& args){
            if(auto result=target(args);!result.ok)return result;
            try {
                const auto id=workspace_.active_document_id();const auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw std::invalid_argument("Bend is available only in a Part.");
                auto feature=create?document::PartDocument::create_sketch_container():bend(state,args.at("container"));
                sketcher::Sketch sketch;
                if(create) {
                    feature.feature_kind=document::FeatureKind::Bend;feature.name="Ohyb";
                    sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;feature.bend.sketch_id=sketch.id;
                    const auto defaults=document::sheet_metal_defaults(state->session.document());
                    feature.bend.thickness=defaults.thickness_mm.value_or(1);feature.bend.k_factor=defaults.k_factor;
                    const double width=args.value("width_mm",40.0);
                    if(!std::isfinite(width)||width<.001||width>1000000)throw std::invalid_argument("Bend width must be between 0.001 and 1000000 mm.");
                    static_cast<void>(sketch.add_segment(-width*.5,0,width*.5,0));
                } else {
                    const auto found=std::ranges::find(state->session.document().sketches,feature.bend.sketch_id,&sketcher::Sketch::id);
                    if(found==state->session.document().sketches.end())throw std::invalid_argument("Bend source Sketch is missing.");
                    sketch=*found;
                }
                const auto number=[&](const char* key,const char* lock,double& value){if(!args.contains(key))return;
                    const double next=args.at(key);if(feature.value_locks.contains(lock)&&next!=value)throw std::invalid_argument("Unlock the dimension before changing it.");value=next;};
                number("radius_mm","radius",feature.bend.radius);number("angle_degrees","angle",feature.bend.angle_degrees);
                number("thickness_mm","thickness",feature.bend.thickness);number("k_factor","k_factor",feature.bend.k_factor);
                if(args.contains("thickness_mm"))feature.bend.thickness_override=true;
                if(args.contains("k_factor"))feature.bend.k_factor_override=true;
                if(args.contains("thickness_override"))feature.bend.thickness_override=args.at("thickness_override");
                if(args.contains("k_factor_override"))feature.bend.k_factor_override=args.at("k_factor_override");
                if(args.contains("state")){const auto value=args.at("state").get<std::string>();
                    if(value!="bend"&&value!="unbend")throw std::invalid_argument("Bend state must be bend or unbend.");feature.bend.unbend=value=="unbend";}
                if(args.contains("name"))feature.name=args.at("name");sketch.name=feature.name;
                const auto container=feature.id;
                const bool changed=workspace::commit_bend(workspace_,kernel_,id,std::move(feature),std::move(sketch));
                if(changed)change_=Change{ChangeKind::Model,id};state=workspace_.open_part(id);
                auto result=details(*state,bend(state,container));result["changed"]=changed;return Result::success(std::move(result));
            }catch(const std::exception& e){return Result::failure("bend_rejected",tr(e.what()));}
        });
    }
}
}
