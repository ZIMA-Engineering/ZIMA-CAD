#include <zima/cam/serialization.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <set>
#include <string_view>

namespace zima::cam {
namespace {
using Json=nlohmann::json;
constexpr std::array<std::string_view,4> motion_names{"rapid","linear","arc-cw","arc-ccw"};
constexpr std::array<std::string_view,5> block_names{"tool","spindle","feed","motion","stop"};
constexpr std::array<std::string_view,7> collision_names{"rapid-stock","cutting-fixture","non-cutting-stock","non-cutting-fixture","target-gouge","cutting-clamping","non-cutting-clamping"};
constexpr std::array<std::string_view,2> warning_names{"below-jaw-top","contact-loss"};
[[noreturn]] void invalid(){throw ProgramError(Error::InvalidProgram);}
template<class E,std::size_t N> std::string name(E value,const std::array<std::string_view,N>& names) {
    const auto index=static_cast<std::size_t>(value);if(index>=N)invalid();return std::string(names[index]);
}
template<class E,std::size_t N> E parse(const Json& value,const std::array<std::string_view,N>& names) {
    const auto text=value.get<std::string>();for(std::size_t i=0;i<N;++i)if(text==names[i])return static_cast<E>(i);
    invalid();
}
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Json point(Point p){if(!finite(p))invalid();return {p.x,p.y,p.z};}
Point point(const Json& j){if(!j.is_array()||j.size()!=3)invalid();Point p{j.at(0),j.at(1),j.at(2)};if(!finite(p))invalid();return p;}
Json profile(const std::vector<ToolStation>& stations) {
    Json result=Json::array();for(const auto& p:stations)result.push_back({p.z_mm,p.radius_mm});return result;
}
std::vector<ToolStation> profile(const Json& j) {
    if(!j.is_array())invalid();std::vector<ToolStation> result;
    for(const auto& p:j){if(!p.is_array()||p.size()!=2)invalid();result.push_back({p.at(0),p.at(1)});}return result;
}
}
void validate_program_definition(const Program& program) {
    if(!finite(program.initial_tip)||(program.display_units!=Units::Millimetres&&program.display_units!=Units::Inches))invalid();
    std::set<std::string> ids;
    for(const auto& b:program.blocks) {
        if(b.id.empty()||b.operation_id.empty()||!ids.insert(b.id).second||!std::isfinite(b.value)||!finite(b.end)||!finite(b.center))invalid();
        static_cast<void>(name(b.kind,block_names));static_cast<void>(name(b.motion,motion_names));
        if(b.kind==BlockKind::Tool&&b.tool_id.empty())invalid();
    }
}
void to_json(Json& j,const Machine& m) {
    validate(m);j={{"id",m.id},{"name",m.name},{"kind",m.kind==MachineKind::Milling?"milling":"turning"},
        {"axes",m.axes},{"minimum",point(m.minimum)},{"maximum",point(m.maximum)},
        {"maximum_feed_mm_min",m.maximum_feed_mm_min},{"maximum_spindle_rpm",m.maximum_spindle_rpm},
        {"rapid_mm_min",m.rapid_mm_min},{"postprocessor",m.postprocessor_id}};
}
void from_json(const Json& j,Machine& value) {
    Machine m;m.id=j.at("id");m.name=j.at("name");
    const auto kind=j.at("kind").get<std::string>();if(kind!="milling"&&kind!="turning")invalid();
    m.kind=kind=="milling"?MachineKind::Milling:MachineKind::Turning;
    if(!j.at("axes").is_number_integer()||j.at("axes").get<double>()<2||j.at("axes").get<double>()>5)invalid();
    m.axes=j.at("axes");m.minimum=point(j.at("minimum"));m.maximum=point(j.at("maximum"));
    m.maximum_feed_mm_min=j.at("maximum_feed_mm_min");m.maximum_spindle_rpm=j.at("maximum_spindle_rpm");
    m.rapid_mm_min=j.at("rapid_mm_min");m.postprocessor_id=j.at("postprocessor");validate(m);value=std::move(m);
}
void to_json(Json& j,const Tool& tool) {
    validate(tool);j={{"id",tool.id},{"name",tool.name},{"cutting",profile(tool.cutting)},{"non_cutting",profile(tool.non_cutting)},
        {"feed_mm_min",tool.feed_mm_min},{"spindle_rpm",tool.spindle_rpm},{"clockwise",tool.clockwise}};
}
void from_json(const Json& j,Tool& value) {
    Tool tool;tool.id=j.at("id");tool.name=j.at("name");tool.cutting=profile(j.at("cutting"));tool.non_cutting=profile(j.at("non_cutting"));
    tool.feed_mm_min=j.at("feed_mm_min");tool.spindle_rpm=j.at("spindle_rpm");tool.clockwise=j.at("clockwise");validate(tool);value=std::move(tool);
}
void to_json(Json& j,const Program& p) {
    validate_program_definition(p);j={{"units",p.display_units==Units::Millimetres?"mm":"in"},{"initial_tip",point(p.initial_tip)},{"blocks",Json::array()}};
    for(const auto& b:p.blocks)j["blocks"].push_back({{"id",b.id},{"operation",b.operation_id},{"kind",name(b.kind,block_names)},
        {"tool",b.tool_id},{"value",b.value},{"clockwise",b.clockwise},{"motion",name(b.motion,motion_names)},
        {"end",point(b.end)},{"center",point(b.center)}});
}
void from_json(const Json& j,Program& value) {
    Program p;const auto units=j.at("units").get<std::string>();if(units!="mm"&&units!="in")invalid();
    p.display_units=units=="mm"?Units::Millimetres:Units::Inches;p.initial_tip=point(j.at("initial_tip"));
    if(!j.at("blocks").is_array())invalid();
    for(const auto& row:j.at("blocks")) {
        Block b;b.id=row.at("id");b.operation_id=row.at("operation");b.kind=parse<BlockKind>(row.at("kind"),block_names);
        b.tool_id=row.at("tool");b.value=row.at("value");b.clockwise=row.at("clockwise");b.motion=parse<MotionKind>(row.at("motion"),motion_names);
        b.end=point(row.at("end"));b.center=point(row.at("center"));p.blocks.push_back(std::move(b));
    }
    validate_program_definition(p);value=std::move(p);
}
void to_json(Json& j,const Facing& f) {
    j={{"id",f.id},{"name",f.name},{"tool",f.tool_id},{"step_down_mm",f.step_down_mm},{"step_over_mm",f.step_over_mm},
        {"clearance_mm",f.clearance_mm},{"allowance_mm",f.allowance_mm}};
}
void from_json(const Json& j,Facing& value) {
    Facing f;f.id=j.at("id");f.name=j.at("name");f.tool_id=j.at("tool");f.step_down_mm=j.at("step_down_mm");
    f.step_over_mm=j.at("step_over_mm");f.clearance_mm=j.at("clearance_mm");f.allowance_mm=j.at("allowance_mm");value=std::move(f);
}
void to_json(Json& j,const ProfileMilling& p) {
    validate(p);j={{"id",p.id},{"name",p.name},{"tool",p.tool_id},{"sketch",p.sketch_id},
        {"strategy",p.kind==ProfileMillingKind::Pocket?"pocket":"contour"},{"side",p.side==ContourSide::Inside?"inside":"outside"},
        {"clockwise",p.clockwise},{"top_mm",p.top_mm},{"bottom_mm",p.bottom_mm},{"step_down_mm",p.step_down_mm},{"step_over_mm",p.step_over_mm},
        {"clearance_mm",p.clearance_mm},{"radial_allowance_mm",p.radial_allowance_mm},{"axial_allowance_mm",p.axial_allowance_mm},{"path_tolerance_mm",p.path_tolerance_mm}};
}
void from_json(const Json& j,ProfileMilling& value) {
    ProfileMilling p;p.id=j.at("id");p.name=j.at("name");p.tool_id=j.at("tool");p.sketch_id=j.at("sketch");
    const auto strategy=j.at("strategy").get<std::string>(),side=j.at("side").get<std::string>();
    if((strategy!="pocket"&&strategy!="contour")||(side!="inside"&&side!="outside"))invalid();
    p.kind=strategy=="pocket"?ProfileMillingKind::Pocket:ProfileMillingKind::Contour;p.side=side=="inside"?ContourSide::Inside:ContourSide::Outside;
    p.clockwise=j.at("clockwise");p.top_mm=j.at("top_mm");p.bottom_mm=j.at("bottom_mm");p.step_down_mm=j.at("step_down_mm");p.step_over_mm=j.at("step_over_mm");
    p.clearance_mm=j.at("clearance_mm");p.radial_allowance_mm=j.at("radial_allowance_mm");p.axial_allowance_mm=j.at("axial_allowance_mm");p.path_tolerance_mm=j.at("path_tolerance_mm");
    validate(p);value=std::move(p);
}
void to_json(Json& j,const Collision& c) {
    j={{"kind",name(c.kind,collision_names)},{"block",c.block_id},{"tool",c.tool_id},{"obstacle",c.obstacle_id},{"intersection_mm3",c.intersection_mm3}};
}
void from_json(const Json& j,Collision& value) {
    Collision c;c.kind=parse<CollisionKind>(j.at("kind"),collision_names);c.block_id=j.at("block");c.tool_id=j.at("tool");
    c.obstacle_id=j.at("obstacle");c.intersection_mm3=j.at("intersection_mm3");
    if(c.block_id.empty()||c.tool_id.empty()||!std::isfinite(c.intersection_mm3)||c.intersection_mm3<=0)invalid();value=std::move(c);
}
void to_json(Json& j,const ClampingWarning& w) {
    j={{"kind",name(w.kind,warning_names)},{"operation",w.operation_id},{"block",w.block_id},{"tool",w.tool_id},{"clamping",w.clamping_id},
       {"removed_mm3",w.removed_mm3},{"lost_contact_mm2",w.lost_contact_mm2},{"remaining_contact_mm2",w.remaining_contact_mm2}};
}
void from_json(const Json& j,ClampingWarning& w) {
    w.kind=parse<ClampingWarningKind>(j.at("kind"),warning_names);w.operation_id=j.at("operation");w.block_id=j.at("block");w.tool_id=j.at("tool");w.clamping_id=j.at("clamping");
    w.removed_mm3=j.at("removed_mm3");w.lost_contact_mm2=j.at("lost_contact_mm2");w.remaining_contact_mm2=j.at("remaining_contact_mm2");
    if(w.operation_id.empty()||w.block_id.empty()||w.tool_id.empty()||w.clamping_id.empty())invalid();
    for(double n:{w.removed_mm3,w.lost_contact_mm2,w.remaining_contact_mm2})if(!std::isfinite(n)||n<0)invalid();
    if((w.kind==ClampingWarningKind::BelowJawTop&&w.removed_mm3<=0)||(w.kind==ClampingWarningKind::ContactLoss&&w.lost_contact_mm2<=0))invalid();
}
void to_json(Json& j,const ClampingContact& c){j={{"clamping",c.clamping_id},{"initial_mm2",c.initial_mm2},{"remaining_mm2",c.remaining_mm2}};}
void from_json(const Json& j,ClampingContact& c){c.clamping_id=j.at("clamping");c.initial_mm2=j.at("initial_mm2");c.remaining_mm2=j.at("remaining_mm2");
    if(c.clamping_id.empty()||!std::isfinite(c.initial_mm2)||!std::isfinite(c.remaining_mm2)||c.initial_mm2<0||c.remaining_mm2<0)invalid();}
}
