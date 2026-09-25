#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/document/metadata.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/kernel/pattern_geometry.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace zima::workspace {
namespace {
std::uint64_t revision(const Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.revision();
    if(const auto* assembly=live.open_assembly(id))return assembly->session.revision();
    throw DerivedCopyError("unsupported_document","Derived copies require an open Part or Assembly.");
}
kernel::ViewerReferenceGeometry references(const Workspace& live,const std::string& id,const CopySources& sources) {
    kernel::ViewerReferenceGeometry geometry;
    const auto available=[&](const std::string& owner){return std::ranges::find(sources.context_bodies,owner)!=sources.context_bodies.end()||
        std::ranges::any_of(sources.items,[&](const auto& item){return item.id==owner;});};
    const auto* part=live.open_part(id);
    if(part) {
        const auto& document=part->session.document();
        if(!part->session.calculated_boundaries().empty())geometry=part->session.calculated_boundaries().back().mesh.original_references;
        append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,document.body_origin_reference_geometry());
        append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
        append_reference_geometry(geometry,document.history_origin_reference_geometry_before({}));
    }else {
        const auto& document=live.open_assembly(id)->session.document();
        geometry=document.build_scene().original_references;
        append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
    }
    const auto unavailable=[&](const auto& reference) {
        if(part) {
            const auto* owner=part->session.document().body_owner_for_object(reference.owner_id);
            return owner&&!available(owner->scope.id)&&std::ranges::none_of(sources.items,[&](const auto& source) {
                return part->session.document().body_owner_for_object(source.id)==owner;
            });
        }
        const auto path=assembly::InstancePath::decode(reference.instance_path);
        return !path.occurrence_ids.empty()&&!available(path.occurrence_ids.front());
    };
    for(auto& ref:geometry.triangle_references)if(unavailable(ref))ref={};
    std::erase_if(geometry.edges,[&](const auto& edge){return unavailable(edge.reference);});
    std::erase_if(geometry.points,[&](const auto& point){return unavailable(point.reference);});
    std::erase_if(geometry.axes,[&](const auto& axis){return unavailable(axis.reference);});
    if(part&&!sources.body_id.empty())
        geometry=part->session.document().construction_reference_geometry_for(sources.body_id,std::move(geometry));
    return geometry;
}
void validate(const DerivedCopyEdit& edit,const DerivedCopyDefinition& value) {
    if(value.id!=edit.initial.id||value.parameters.pattern.has_value()!=edit.initial.parameters.pattern.has_value())
        throw DerivedCopyError("identity_changed","Editing must preserve the copy identity and operation type.");
    document::validate_native_metadata_text(value.name);
    if(value.name.empty()||std::ranges::all_of(value.name,[](unsigned char c){return std::isspace(c)!=0;}))
        throw DerivedCopyError("invalid_arguments","Specify a nonempty object name.");
    if(std::ranges::none_of(edit.sources.items,[&](const auto& source){return source.id==value.parameters.source_id;}))
        throw DerivedCopyError("invalid_source","Select an available source before this copy in its owning document.");
    const auto& placement=value.placement;
    for(double number:{placement.x,placement.y,placement.z,placement.rotation_x,placement.rotation_y,placement.rotation_z,
        placement.absolute_rotation_x,placement.absolute_rotation_y,placement.absolute_rotation_z,
        placement.rotation_offset_x,placement.rotation_offset_y,placement.rotation_offset_z})
        if(!std::isfinite(number))throw DerivedCopyError("invalid_arguments","Placement value must be finite.");
    for(const auto& ref:placement.references)if(!std::isfinite(ref.offset))throw DerivedCopyError("invalid_arguments","Placement value must be finite.");
    if(!std::isfinite(value.parameters.reference.offset))throw DerivedCopyError("invalid_arguments","Placement value must be finite.");
    if(!value.parameters.pattern)return;
    const auto& pattern=*value.parameters.pattern;
    if(pattern.count<2||pattern.count>1000||!std::isfinite(pattern.angle_degrees)||std::abs(pattern.angle_degrees)>359.999)
        throw DerivedCopyError("invalid_arguments","Pattern count or angle is outside the supported range.");
    for(const auto& direction:pattern.linear) {
        if(direction.local_axis< -1||direction.local_axis>2||direction.count<2||direction.count>1000||direction.reverse_count<1||direction.reverse_count>999||
            !std::isfinite(direction.spacing)||direction.spacing<.001||direction.spacing>1e6)
            throw DerivedCopyError("invalid_arguments","Pattern direction parameters are outside the supported range.");
        if(direction.distribution!=kernel::PatternDistribution::Forward&&direction.distribution!=kernel::PatternDistribution::Reverse&&
            direction.distribution!=kernel::PatternDistribution::Both&&direction.distribution!=kernel::PatternDistribution::Symmetric)
            throw DerivedCopyError("invalid_arguments","Unknown Pattern distribution.");
    }
    if(!edit.creating) {
        const auto& before=*edit.initial.parameters.pattern;
        const auto locked=[&](const std::string& key,double old_value,double new_value) {
            if(edit.initial.parameters.value_locks.contains(key)&&value.parameters.value_locks.contains(key)&&old_value!=new_value)
                throw DerivedCopyError("value_locked","Unlock the dimension before changing it.");
        };
        locked("pattern:angle",before.full_circle?360.0/before.count:before.angle_degrees,
            pattern.full_circle?360.0/pattern.count:pattern.angle_degrees);
        locked("pattern:angle",before.angle_degrees,pattern.angle_degrees);
        for(std::size_t i=0;i<pattern.linear.size();++i)locked("pattern:spacing:"+std::to_string(i),before.linear[i].spacing,pattern.linear[i].spacing);
    }
}
}
DerivedCopyEdit prepare_derived_copy_edit(Workspace& live,const std::string& id,const std::string& object,bool pattern) {
    // CLI has no scene refresh. Use the same current-source sharing that GUI
    // already performs before opening Properties; never regenerate a source.
    // This keeps Undo from restoring an obsolete source packet with the copy.
    if(live.open_assembly(id))live.refresh_source_geometry();
    DerivedCopyEdit edit;edit.document_id=id;edit.revision=revision(live,id);edit.creating=object.empty();
    edit.sources=derived_copy_sources(live,id,object);
    if(edit.creating) {
        edit.initial.id=kernel::make_stable_id();edit.initial.name=pattern?"Pole":"Zrcadlo";
        if(pattern) {
            edit.initial.parameters.pattern=kernel::PatternRequest{};
            edit.initial.parameters.reference={{},edit.initial.id+":origin","origin:axis:z"};
        }
    }else edit.initial=derived_copy_definition(live,id,object);
    edit.references=references(live,id,edit.sources);return edit;
}
bool commit_derived_copy(Workspace& live,const kernel::OcctKernel& kernel,const DerivedCopyEdit& edit,DerivedCopyDefinition value) {
    if(revision(live,edit.document_id)!=edit.revision)
        throw DerivedCopyError("document_changed","The document changed while copy properties were open.");
    if(const auto* part=live.open_part(edit.document_id)) {
        const auto* source=part->session.document().find_container(value.parameters.source_id);
        value.parameters.subtract_source=source&&source->combine_mode==document::CombineMode::Subtract;
    }
    validate(edit,value);
    if(!document::resolve_placement(value.placement,edit.references))
        throw DerivedCopyError("missing_reference","Chybí reference umístění kontejneru.");
    document::PartDocument::resolve_copy_reference(value.parameters,value.id,value.placement,edit.references);
    if(!edit.creating&&value==edit.initial)return false;
    if(auto* part=live.open_part(edit.document_id)) {
        auto next=part->session.document();auto graph=next.body_history;document::BodyHistory body;
        if(!edit.sources.body_id.empty()) {
            auto feature=edit.creating?document::HistoryContainer{}:*next.find_container(value.id);
            feature.id=value.id;feature.feature_id=value.id+":entity";feature.feature_parent_id=value.id;
            feature.container_origin=document::create_container_origin(value.id);
            feature.feature_kind=document::FeatureKind::DerivedCopy;feature.name=value.name;
            feature.placement=value.placement;feature.derived_copy=value.parameters;feature.suppressed=!value.visible;
            feature.combine_mode=value.parameters.subtract_source?document::CombineMode::Subtract:document::CombineMode::Add;
            if(edit.creating) {
                graph.activate(edit.sources.body_id);graph.set_history_cursor(edit.sources.body_id,edit.sources.body_cursor);
                graph.insert({document::PartHistoryKind::Feature,value.id});next.history.push_back(std::move(feature));
            }else *next.find_container(value.id)=std::move(feature);
            next.set_body_history(std::move(graph));
            auto calculated=calculate_part_with_resolved_references(kernel,next,&part->session.calculated_boundaries());
            if(calculated.empty()||calculated.back().calculation_errors.contains(value.id))
                throw DerivedCopyError("calculation_failed","The in-Body copy could not be calculated.");
            part->session.commit(std::move(next),std::move(calculated));return true;
        }
        if(edit.creating)graph.set_insertion_cursor(edit.sources.boundary);else body=*graph.find(value.id);
        body.scope.id=value.id;body.scope.placement=value.placement;body.name=value.name;body.derived_copy=value.parameters;body.visible=value.visible;
        if(edit.creating)static_cast<void>(graph.create_derived_copy(std::move(body)));else graph.update_body(std::move(body));
        graph.activate({});next.set_body_history(std::move(graph));
        auto calculated=calculate_part_with_resolved_references(kernel,next,&part->session.calculated_boundaries());
        if(calculated.empty()||!calculated.back().body_outputs.contains(value.id))
            throw DerivedCopyError("calculation_failed","The derived copy has no calculated result.");
        const auto& result=calculated.back().body_outputs.at(value.id).get();
        if(!result.calculation_errors.empty())throw std::runtime_error(result.calculation_errors.begin()->second);
        part->session.commit(std::move(next),std::move(calculated));return true;
    }
    auto* assembly=live.open_assembly(edit.document_id);auto next=assembly->session.document();
    const auto* source=next.find_occurrence(value.parameters.source_id);
    auto result=edit.creating?*source:*next.find_occurrence(value.id);
    result.occurrence_id=value.id;result.name=value.name;result.derived_copy=value.parameters;result.copy_placement=value.placement;
    if(!edit.creating)result.visible=value.visible;
    result.placement={};result.placement_references.clear();result.grounded=true;
    if(edit.creating)next.components.push_back(std::move(result));else *next.find_occurrence(value.id)=std::move(result);
    next.calculate_derived_copies(kernel);assembly->session.commit(std::move(next));return true;
}
} // namespace zima::workspace
