#include "sketch_command_support.hpp"
#include <map>

namespace zima::command_host {
using namespace sketch_commands;
namespace {
using Dimension=sketcher::SketchDimension;
using Kind=sketcher::DimensionKind;
const std::map<std::string,Kind> kinds={
    {"distance",Kind::Distance},{"distance_x",Kind::DistanceX},{"distance_y",Kind::DistanceY},
    {"point_line",Kind::DistancePointLine},{"symmetric",Kind::DistanceSymmetric},{"line_distance",Kind::DistanceLine},
    {"radius",Kind::Radius},{"diameter",Kind::Diameter},{"angle",Kind::Angle},{"three_point_angle",Kind::AngleThreePoint},
    {"angle_between",Kind::AngleBetween},{"symmetric_angle",Kind::AngleSymmetric},{"symmetric_line_distance",Kind::DistanceLineSymmetric},
    {"ellipse_major",Kind::EllipseMajorRadius},{"ellipse_minor",Kind::EllipseMinorRadius},{"ellipse_rotation",Kind::EllipseRotation}};
bool angular(const Dimension& d) {
    return d.kind==Kind::Angle||d.kind==Kind::AngleBetween||d.kind==Kind::AngleThreePoint||d.kind==Kind::AngleSymmetric||d.kind==Kind::EllipseRotation;
}
const Dimension& dimension(const Sketch& s,const std::string& id) {
    const auto found=std::ranges::find(s.dimensions,id,&Dimension::id);
    if(found==s.dimensions.end())throw workspace::SketchOperationError("dimension_not_found","The Sketch dimension does not exist.");return *found;
}
Dimension create_dimension(const Sketch& s,const Json& a) {
    const auto found=kinds.find(a["kind"].get<std::string>());if(found==kinds.end())invalid("Unknown Sketch dimension kind.");
    const auto kind=found->second;const auto p=ids(a,"points"),g=ids(a,"geometry");
    const auto shape=[&](std::size_t points,std::size_t geometry){if(p.size()!=points||g.size()!=geometry)invalid("The dimension requires a different number of points or geometry references.");};
    if(kind==Kind::Radius||kind==Kind::Diameter) {
        shape(0,1);const bool circle=std::ranges::any_of(s.circles,[&](const auto& c){return c.id==g[0];});
        return circle?(kind==Kind::Radius?s.create_circle_radius_dimension(g[0]):s.create_circle_diameter_dimension(g[0])):
            (kind==Kind::Radius?s.create_arc_radius_dimension(g[0]):s.create_arc_diameter_dimension(g[0]));
    }
    if(kind==Kind::EllipseMajorRadius||kind==Kind::EllipseMinorRadius||kind==Kind::EllipseRotation) {
        shape(0,1);return kind==Kind::EllipseRotation?s.create_ellipse_rotation_dimension(g[0]):s.create_ellipse_radius_dimension(g[0],kind==Kind::EllipseMajorRadius);
    }
    if(kind==Kind::AngleThreePoint){shape(3,0);return s.create_three_point_angle_dimension(p[0],p[1],p[2]);}
    if(kind==Kind::DistancePointLine){shape(1,1);return s.create_point_line_dimension(p[0],g[0]);}
    if(kind==Kind::DistanceSymmetric){if((p.size()!=1&&p.size()!=2)||g.size()!=1)invalid("A symmetric point dimension requires one or two points and an axis.");return s.create_symmetric_dimension(p[0],p.size()==2?p[1]:std::string{},g[0]);}
    if(kind==Kind::DistanceLineSymmetric||kind==Kind::AngleSymmetric) {
        if(!p.empty()||(g.size()!=2&&g.size()!=3))invalid("A symmetric line dimension requires an axis and one or two lines.");
        return s.create_line_symmetric_dimension(g[0],g[1],g.size()==3?g[2]:std::string{},kind);
    }
    if(kind==Kind::AngleBetween&&p.size()==4){shape(4,0);return s.create_four_point_angle_dimension(p[0],p[1],p[2],p[3]);}
    if(kind==Kind::AngleBetween&&p.size()==2){shape(2,1);return s.create_point_line_angle_dimension(p[0],p[1],g[0]);}
    if(kind==Kind::AngleBetween||kind==Kind::DistanceLine){shape(0,2);return s.create_line_pair_dimension(g[0],g[1],kind);}
    if((kind==Kind::DistanceX||kind==Kind::DistanceY)&&p.size()==1) {
        shape(1,1);auto result=s.create_axis_dimension(p[0],g[0]);if(result.kind!=kind)invalid("The selected axis does not match the requested coordinate dimension.");return result;
    }
    if(p.empty()){shape(0,1);return s.create_segment_dimension(g[0],kind);}
    shape(2,0);return s.create_point_dimension(p[0],p[1],kind);
}
void patch(Dimension& d,const Json& a) {
    const double measurement=d.value;
    if(a.contains("value"))d.value=number(a["value"]);
    if(a.contains("driving"))d.driving=a["driving"].get<bool>();
    if(a.contains("locked"))d.locked=a["locked"].get<bool>();
    if(!d.driving){d.locked=false;d.value=measurement;}
    if(a.contains("position"))d.placement=point(a,"position");
    if(a.contains("solution_side")){const auto& value=a["solution_side"];if(value!=-1&&value!=1)invalid("Dimension solution side must be -1 or 1.");d.solution_side=value.get<int>();}
    if(a.contains("angle_sector")){const auto& value=a["angle_sector"];if(value<-1||value>1)invalid("Dimension angle sector must be -1, 0 or 1.");d.angle_sector=value.get<int>();}
    if(a.contains("limits")) {
        for(const auto& [key,value]:a["limits"].items()) {
            if(key!="lower"&&key!="upper")invalid("Dimension limits accept only lower and upper.");
            auto& target=key=="lower"?d.lower_limit:d.upper_limit;target=value.is_null()?std::optional<double>{}:std::optional<double>{number(value)};
        }
    }
    if(a.contains("text")) {
        const std::map<std::string,std::string*> fields={{"prefix",&d.prefix},{"suffix",&d.suffix},{"text_override",&d.display_text_override},
            {"tolerance_mode",&d.tolerance_mode},{"symmetric_tolerance",&d.symmetric_tolerance},{"single_tolerance",&d.single_tolerance},{"upper_tolerance",&d.upper_tolerance},{"lower_tolerance",&d.lower_tolerance}};
        for(const auto& [key,value]:a["text"].items()) {
            const auto field=fields.find(key);if(field==fields.end()||!value.is_string()||value.get_ref<const std::string&>().size()>2048)invalid("Dimension text accepts known string fields of at most 2048 bytes.");*field->second=value.get<std::string>();
        }
        if(d.tolerance_mode!=""&&d.tolerance_mode!="symmetric"&&d.tolerance_mode!="single_deviation"&&d.tolerance_mode!="deviations")invalid("Unknown dimension tolerance mode.");
    }
    sketcher::validate_dimension_property_value(d);
}
const std::vector<kernel::DimensionLayoutEntry>& layouts(const workspace::Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.document().dimension_layouts;
    if(const auto* assembly=live.open_assembly(id))return assembly->session.document().dimension_layouts;
    throw workspace::SketchOperationError("unsupported_document","Sketch operations require an open Part or Assembly.");
}
kernel::DimensionLayout layout_patch(kernel::DimensionLayout value,const Json& patch) {
    for(const auto& [key,data]:patch.items()) {
        if(key=="plane_quarter_turns")value.plane_quarter_turns=static_cast<int>(integer(patch,"plane_quarter_turns",0,0,3));
        else if(key=="envelope_offset")value.envelope_offset=data.is_null()?std::optional<double>{}:std::optional<double>{number(data)};
        else if(key=="text_along")value.text_along=number(data);
        else if(key=="text_outward")value.text_outward=number(data);
        else if(key=="line_offset")value.line_offset=number(data);
        else if(key=="radius_rotation_degrees")value.radius_rotation_degrees=number(data);
        else if(key=="arrows_reversed"||key=="radius_center_line_hidden") {
            if(!data.is_boolean())invalid("Dimension layout flags must be booleans.");
            (key=="arrows_reversed"?value.arrows_reversed:value.radius_center_line_hidden)=data.get<bool>();
        } else invalid("Unknown dimension label layout field.");
    }
    kernel::validate_dimension_layout(value);return value;
}
Json layout_data(const kernel::DimensionLayout* d) {
    if(!d)return nullptr;return {{"plane_quarter_turns",d->plane_quarter_turns},{"envelope_offset",d->envelope_offset?Json(*d->envelope_offset):Json(nullptr)},
        {"text_along",d->text_along},{"text_outward",d->text_outward},{"arrows_reversed",d->arrows_reversed},{"line_offset",d->line_offset},
        {"radius_center_line_hidden",d->radius_center_line_hidden},{"radius_rotation_degrees",d->radius_rotation_degrees}};
}
Json data(const workspace::Workspace& live,const std::string& doc,const Sketch& sketch,const Dimension& d) {
    std::string kind;for(const auto& [key,value]:kinds)if(value==d.kind)kind=key;
    const kernel::EdgeReference reference{sketch.id,"dimension:"+d.id,{}};
    return {{"dimension",d.id},{"kind",kind},{"value",d.value},{"unit",angular(d)?"deg":"mm"},{"driving",d.driving},{"locked",d.locked},{"suppressed",d.suppressed},
        {"first_point",d.first_point_id},{"second_point",d.second_point_id},{"geometry",d.geometry_id},{"second_geometry",d.second_geometry_id},{"third_geometry",d.third_geometry_id},
        {"position",d.placement?Json(*d.placement):Json(nullptr)},{"solution_side",d.solution_side},{"angle_sector",d.angle_sector},
        {"limits",{{"lower",d.lower_limit?Json(*d.lower_limit):Json(nullptr)},{"upper",d.upper_limit?Json(*d.upper_limit):Json(nullptr)}}},
        {"text",{{"prefix",d.prefix},{"suffix",d.suffix},{"text_override",d.display_text_override},{"tolerance_mode",d.tolerance_mode},{"symmetric_tolerance",d.symmetric_tolerance},{"single_tolerance",d.single_tolerance},{"upper_tolerance",d.upper_tolerance},{"lower_tolerance",d.lower_tolerance}}},
        {"document_layout",layout_data(kernel::find_dimension_layout(layouts(live,doc),reference))},{"sketch_layout",layout_data(kernel::find_dimension_layout(sketch.dimension_layouts,reference))}};
}
}
void Host::register_sketch_dimension_commands() {
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> args={{"sketch",true},{create?"kind":"dimension",true}};
        if(create){args.push_back({"points",false,Type::Array});args.push_back({"geometry",false,Type::Array});}
        const std::vector<commands::Argument> properties={{"value",false,Type::Number},{"driving",false,Type::Boolean},{"locked",false,Type::Boolean},
            {"position",false,Type::Array},{"solution_side",false,Type::Integer},{"angle_sector",false,Type::Integer},{"limits",false,Type::Object},{"text",false,Type::Object},{"layout",false,Type::Object},{"document",false}};
        args.insert(args.end(),properties.begin(),properties.end());
        dispatcher_.add({create?"sketch.dimension.create":"sketch.dimension.set",tr("Create or edit Sketch dimension properties; lengths use mm and angles degrees."),args,true},[this,create](const Json& a) {
            const auto check=target(a);if(!check.ok)return check;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Sketch commands require an ordinary Part or Assembly."));
            try {
                const auto doc=workspace_.active_document_id(),id=a["sketch"].get<std::string>();const auto before=workspace::document_sketch(workspace_,doc,id);
                auto value=create?create_dimension(before,a):dimension(before,a["dimension"].get<std::string>());patch(value,a);
                std::vector<kernel::DimensionLayoutEntry> updated_layouts;
                if(a.contains("layout")) {
                    const kernel::EdgeReference reference{id,"dimension:"+value.id,{}};const auto* current=kernel::find_dimension_layout(layouts(workspace_,doc),reference);
                    if(!current)current=kernel::find_dimension_layout(before.dimension_layouts,reference);
                    auto layout=layout_patch(current?*current:kernel::DimensionLayout{},a["layout"]);
                    if(angular(value)&&layout.plane_quarter_turns)invalid("Angular dimension plane is defined by its measured rays");
                    updated_layouts.push_back({id,reference.semantic_key,std::move(layout)});
                }
                const bool changed=workspace::mutate_document_sketch(workspace_,doc,id,[&](Sketch& s){s.apply_dimension(value);},updated_layouts);
                const auto after=workspace::document_sketch(workspace_,doc,id);auto result=data(workspace_,doc,after,dimension(after,value.id));
                result["document"]=doc;result["sketch"]=id;result["changed"]=changed;result["body_calculated"]=false;
                if(const auto* part=workspace_.open_part(doc))result["revision"]=part->session.revision();else result["revision"]=workspace_.open_assembly(doc)->session.revision();
                if(changed)change_=Change{ChangeKind::Model,doc,true};return Result::success(std::move(result));
            }catch(const workspace::SketchOperationError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("dimension_rejected",tr(error.what()));}
        });
    }
    add_sketch_query({"sketch.dimension.get",tr("Read a Sketch dimension's parameters, references and label layout."),{{"dimension",true}}},[this](const Sketch& s,const Json& a) {
        return data(workspace_,a.value("document",workspace_.active_document_id()),s,dimension(s,a["dimension"].get<std::string>()));
    });
    add_sketch_command({"sketch.dimension.delete",tr("Remove a Sketch dimension using the native solver transaction."),{{"dimension",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["dimension"].get<std::string>();s.remove_dimension(id);return Json{{"dimension",id}};
    });
}
}
