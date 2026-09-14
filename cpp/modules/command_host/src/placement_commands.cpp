#include <zima/command_host/host.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/workspace/primitive_reference_operations.hpp>
#include <zima/workspace/profile_reference_operations.hpp>
#include <zima/workspace/body_reference_operations.hpp>
#include <zima/workspace/construction_reference_operations.hpp>

namespace zima::command_host {
namespace {
Json data(const workspace::Workspace& live, const std::string& id, const std::string& object) {
    const auto value = workspace::read_placement(live, id, object);
    const auto revision = live.open_part(id) ? live.open_part(id)->session.revision()
        : live.open_assembly(id)->session.revision();
    return {{"document", id}, {"object", object}, {"kind", value.kind}, {"body", value.body},
        {"coordinate_system", value.coordinate_system}, {"coordinate_owner", value.coordinate_owner},
        {"length_unit", "mm"}, {"angle_unit", "degrees"}, {"placement", value.placement}, {"revision", revision}};
}
}
void Host::register_placement_commands() {
    dispatcher_.add({"placement.reference.set",tr("Assign an original reference to a Body, construction, primitive or Part profile placement."),
        {{"object",true},{"index",true,commands::ArgumentType::Integer},{"reference",true,commands::ArgumentType::Object},
         {"offset_mm",false,commands::ArgumentType::Number},{"flip",false,commands::ArgumentType::Boolean},{"derive_orientation",false,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Placement operations require an open Part or Assembly."));
        try {
            const auto& ref=args.at("reference");
            const auto invalid=[](){throw workspace::PlacementEditError("invalid_arguments","Specify owner, key and an optional instance_path for the placement reference.");};
            for(const auto& [key,item]:ref.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!item.is_string())invalid();
            if(!ref.contains("owner")||!ref.contains("key")||args.at("index")<0||args.at("index")>4)invalid();
            const auto id=workspace_.active_document_id(),object=args.at("object").get<std::string>();const auto info=workspace::read_placement(workspace_,id,object);
            document::ConstructionReference source;source.owner_id=ref.at("owner");source.semantic_key=ref.at("key");source.instance_path=ref.value("instance_path",std::string{});
            source.offset=args.value("offset_mm",0.0);source.flip=args.value("flip",false);
            const auto index=args.at("index").get<std::size_t>();const auto derive=args.value("derive_orientation",true);bool changed{};
            if(info.kind=="body")changed=workspace::set_body_placement_reference(workspace_,kernel_,id,object,index,std::move(source),derive);
            else if(info.kind=="construction")changed=workspace::set_construction_reference(workspace_,id,object,index,std::move(source),derive);
            else {
                const auto kind=workspace_.open_part(id)->session.document().find_container(object)->feature_kind;
                if(kind==document::FeatureKind::Extrusion||kind==document::FeatureKind::Revolution)
                    changed=workspace::set_part_profile_reference(workspace_,kernel_,id,object,index,std::move(source),derive);
                else changed=workspace::set_primitive_reference(workspace_,kernel_,id,object,index,std::move(source),derive);
            }
            auto result=data(workspace_,id,object);result["changed"]=changed;
            if(changed)change_=Change{ChangeKind::Model,id};return Result::success(std::move(result));
        }catch(const workspace::PlacementEditError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const workspace::BodyOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const workspace::ProfileOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const workspace::PrimitiveOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("placement_rejected",tr(error.what()));}
    });
    dispatcher_.add({"placement.get", tr("Read stored Body, feature or construction placement without calculation."),
        {{"object", true}, {"document", false}}, false}, [this](const Json& args) {
        try { return Result::success(data(workspace_, args.value("document", workspace_.active_document_id()), args.at("object").get<std::string>())); }
        catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
    });
    dispatcher_.add({"placement.set", tr("Edit placement dimensions using the shared reference solver; values are mm or degrees."),
        {{"object", true}, {"values", true, commands::ArgumentType::Object}, {"document", false}}, true},
        [this](const Json& args) {
        const auto check = target(args); if (!check.ok) return check;
        if (interaction().template_document) return Result::failure("unsupported_document", tr("Placement operations require an open Part or Assembly."));
        try {
            workspace::PlacementValuePatch patch;
            for (const auto& [key, number] : args.at("values").items()) {
                if (!number.is_number()) return Result::failure("invalid_arguments", tr("Placement parameters must be JSON numbers."));
                patch.emplace(key, number.get<double>());
            }
            const auto id = workspace_.active_document_id(), object = args.at("object").get<std::string>();
            const bool changed = workspace::set_placement_values(workspace_, kernel_, id, object, patch);
            auto result = data(workspace_, id, object); result["changed"] = changed;
            if (changed) change_ = Change{ChangeKind::Model, id};
            return Result::success(std::move(result));
        } catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const std::exception& error) { return Result::failure("placement_rejected", tr(error.what())); }
    });
}
} // namespace zima::command_host
