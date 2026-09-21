#include <zima/command_host/host.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
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
    const auto extensions=document::bend_profile_extensions(feature);
    return {{"document",state.session.document().document_id},{"container",feature.id},{"feature",feature.feature_id},
        {"sketch",p.sketch_id},{"name",feature.name},{"radius_mm",p.radius},{"angle_degrees",p.angle_degrees},
        {"path_sketch",sketcher::Sketch::from_serialized(p.auxiliary_sketches[0]).id},
        {"end_sketch",sketcher::Sketch::from_serialized(p.auxiliary_sketches[1]).id},
        {"first_extension_mm",extensions[0]},{"last_extension_mm",extensions[1]},
        {"thickness_mm",p.thickness},{"k_factor",p.k_factor},{"thickness_override",p.thickness_override},
        {"k_factor_override",p.k_factor_override},{"radius_follows_thickness",p.radius_follows_thickness},
        {"revision",state.session.revision()}};
}
}
void Host::register_bend_commands() {
    using Type=commands::ArgumentType;
    for(bool unfold:{true,false})for(bool create:{true,false}) {
        const std::string name=std::string(unfold?"unbend":"bend_back")+(create?".create":".set");
        dispatcher_.add({name,tr("Change selected sheet regions at this history boundary; preserve intervening material edits."),
            {{"container",!create},{"all",false,Type::Boolean},{"owners",false,Type::Array},{"name",false},{"document",false}},true},
            [this,unfold,create](const Json& args) {
                if(auto result=target(args);!result.ok)return result;
                try {
                    const auto id=workspace_.active_document_id();const auto* part=workspace_.open_part(id);
                    if(!part||interaction().template_document)throw std::invalid_argument("Sheet state requires an open Part.");
                    const auto* stored=create?nullptr:part->session.document().find_container(args.at("container"));
                    const auto kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
                    if(!create&&(!stored||stored->feature_kind!=kind))throw std::invalid_argument("Sheet state container does not exist.");
                    auto feature=stored?*stored:document::PartDocument::create_sketch_container();feature.feature_kind=kind;
                    feature.name=args.value("name",stored?stored->name:tr(unfold?"Rozvinout":"Ohnout zpět"));
                    if(args.contains("owners"))feature.sheet_state.owners=args.at("owners").get<std::vector<std::string>>();
                    feature.sheet_state.all=args.value("all",args.contains("owners")?false:feature.sheet_state.all);
                    const auto owner=feature.id;const bool changed=workspace::commit_sheet_state(workspace_,kernel_,id,std::move(feature));
                    if(changed)change_=Change{ChangeKind::Model,id};
                    return Result::success({{"document",id},{"container",owner},{"changed",changed}});
                }catch(const std::exception& error){return Result::failure("sheet_state_rejected",tr(error.what()));}
            });
    }
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
        for(const auto* key:{"radius_mm","angle_degrees","thickness_mm","k_factor","first_extension_mm","last_extension_mm"})arguments.push_back({key,false,Type::Number});
        for(const auto* key:{"thickness_override","k_factor_override","radius_follows_thickness","origin_last"})arguments.push_back({key,false,Type::Boolean});
        for(const auto* key:{"name","document","edge_owner","edge_key"})arguments.push_back({key,false});
        dispatcher_.add({create?"bend.create":"bend.set",tr("Create or edit a Sheet Profile."),arguments,true},[this,create](const Json& args){
            if(auto result=target(args);!result.ok)return result;
            try {
                const auto id=workspace_.active_document_id();const auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw std::invalid_argument("Bend is available only in a Part.");
                auto feature=create?document::PartDocument::create_sketch_container():bend(state,args.at("container"));
                sketcher::Sketch sketch;
                if(create) {
                    feature.feature_kind=document::FeatureKind::Bend;feature.name=tr("Profil plechu");
                    sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;feature.bend.sketch_id=sketch.id;
                    const auto defaults=document::sheet_metal_defaults(state->session.document());
                    feature.bend.thickness=defaults.thickness_mm.value_or(1);feature.bend.k_factor=defaults.k_factor;
                    const double width=args.value("width_mm",40.0);
                    if(!std::isfinite(width)||width<.001||width>1000000)throw std::invalid_argument("Bend width must be between 0.001 and 1000000 mm.");
                    document::initialize_bend_start_profile(sketch,width);
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
                if(args.contains("radius_follows_thickness")) {
                    const bool enabled=args.at("radius_follows_thickness");
                    if(feature.bend.radius_follows_thickness&&!enabled&&!args.contains("radius_mm"))
                        feature.bend.radius=document::resolved_bend_parameters(feature,document::sheet_metal_defaults(state->session.document())).radius;
                    feature.bend.radius_follows_thickness=enabled;
                }
                if(args.contains("radius_mm")&&feature.bend.radius_follows_thickness)
                    throw std::invalid_argument("Disable radius linked to thickness before editing the radius.");
                if(args.contains("name"))feature.name=args.at("name");sketch.name=feature.name;
                if(args.contains("edge_owner")||args.contains("edge_key")||args.contains("origin_last")) {
                    auto owner=args.value("edge_owner",std::string{}),key=args.value("edge_key",std::string{});
                    if(owner.empty()&&key.empty()&&feature.bend.sheet_attachment&&!feature.placement.references.empty()) {
                        owner=feature.placement.references.front().owner_id;key=feature.placement.references.front().semantic_key;
                    }
                    std::optional<kernel::ViewerEdge> selected;
                    for(const auto& boundary:state->session.calculated_boundaries())for(const auto& edge:boundary.mesh.original_references.edges)
                        if(edge.reference.owner_id==owner&&edge.reference.semantic_key==key&&edge.reference.instance_path.empty())selected=edge;
                    if(!selected)throw std::invalid_argument("Bend attachment edge does not exist.");
                    feature.placement.references=document::bend_sheet_references(*selected);
                    if(args.value("origin_last",false))feature.placement.references=document::bend_sheet_references(*selected,selected->edge_treatment_endpoint_references.back());
                    feature.bend.sheet_attachment=true;
                }
                document::prepare_bend_sketches(feature,sketch,document::sheet_metal_defaults(state->session.document()));
                if(args.contains("first_extension_mm")||args.contains("last_extension_mm")) {
                    const auto extensions=document::bend_profile_extensions(feature);
                    document::set_bend_profile_extensions(feature,args.value("first_extension_mm",extensions[0]),args.value("last_extension_mm",extensions[1]));
                }
                const auto container=feature.id;
                const bool changed=workspace::commit_bend(workspace_,kernel_,id,std::move(feature),std::move(sketch));
                if(changed)change_=Change{ChangeKind::Model,id};state=workspace_.open_part(id);
                auto result=details(*state,bend(state,container));result["changed"]=changed;return Result::success(std::move(result));
            }catch(const std::exception& e){return Result::failure("bend_rejected",tr(e.what()));}
        });
    }
}
}
