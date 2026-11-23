#include <zima/document/cam_definition.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace zima::document {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid CAM definition.");}
bool finite(cam::Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Json point(cam::Point p){return {p.x,p.y,p.z};}
cam::Point point(const Json& j){if(!j.is_array()||j.size()!=3)invalid();cam::Point p{j.at(0),j.at(1),j.at(2)};if(!finite(p))invalid();return p;}
void frame(const Placement& p) {
    for(double value:{p.x,p.y,p.z,p.rotation_x,p.rotation_y,p.rotation_z,p.absolute_rotation_x,p.absolute_rotation_y,
            p.absolute_rotation_z,p.rotation_offset_x,p.rotation_offset_y,p.rotation_offset_z})if(!std::isfinite(value))invalid();
    for(const auto& r:p.references)if(r.owner_id.empty()||r.semantic_key.empty()||!r.instance_path.empty()||!std::isfinite(r.offset))invalid();
}
void identities(const std::vector<std::string>& values) {
    std::set<std::string> ids;for(const auto& id:values)if(id.empty()||!ids.insert(id).second)invalid();
}
void intent_valid(const CamDefinition& d) {
    if(d.stock_id.empty()||d.target_id.empty()||!std::isfinite(d.tolerance_mm)||d.tolerance_mm<=0||!finite(d.machine_work_origin))invalid();
    if(d.display_units!=cam::Units::Millimetres&&d.display_units!=cam::Units::Inches)invalid();
    frame(d.work_frame);cam::validate(d.machine);identities(d.fixture_ids);identities(d.clamping_sketch_ids);
    std::set<std::string> tool_ids,operation_ids,block_ids;
    for(const auto& tool:d.tools){cam::validate(tool);if(!tool_ids.insert(tool.id).second)invalid();}
    for(const auto& operation:d.operations) {
        if(cam_operation_id(operation).empty()||cam_operation_name(operation).empty()||!operation_ids.insert(cam_operation_id(operation)).second)invalid();
        if(const auto* manual=std::get_if<CamManualOperation>(&operation)) {
            cam::validate_program_definition(manual->program);
            for(const auto& b:manual->program.blocks)if(b.operation_id!=manual->id||!block_ids.insert(b.id).second)invalid();
        }else {
            const auto& f=std::get<cam::Facing>(operation);
            if(f.tool_id.empty()||!std::isfinite(f.step_down_mm)||f.step_down_mm<=0||!std::isfinite(f.step_over_mm)||f.step_over_mm<=0||
               !std::isfinite(f.clearance_mm)||f.clearance_mm<=0||!std::isfinite(f.allowance_mm)||f.allowance_mm<0)invalid();
        }
    }
}
Json intent_json(const CamDefinition& d) {
    intent_valid(d);
    Json j={{"stock",d.stock_id},{"target",d.target_id},{"fixtures",d.fixture_ids},{"clamping_sketches",d.clamping_sketch_ids},
        {"work_frame",d.work_frame},{"machine",d.machine},{"machine_work_origin",point(d.machine_work_origin)},
        {"tools",d.tools},{"operations",Json::array()},{"units",d.display_units==cam::Units::Millimetres?"mm":"in"},
        {"tolerance_mm",d.tolerance_mm}};
    for(const auto& operation:d.operations) {
        if(const auto* manual=std::get_if<CamManualOperation>(&operation))
            j["operations"].push_back({{"kind","manual"},{"id",manual->id},{"name",manual->name},{"program",manual->program}});
        else j["operations"].push_back({{"kind","facing"},{"parameters",std::get<cam::Facing>(operation)}});
    }
    return j;
}
void result_valid(const CamCalculated& r,bool program_check=false) {
    if(r.id.empty()||r.intent_key.empty()||r.sources.empty()||r.remaining->source_fingerprint.empty()||r.remaining->kernel_shape.empty()||
       !r.remaining->calculation_errors.empty()||!std::isfinite(r.removed_mm3)||r.removed_mm3<0||
       !std::isfinite(r.missing_target_mm3)||r.missing_target_mm3<0||!std::isfinite(r.tolerance_mm)||r.tolerance_mm<=0)invalid();
    frame(r.work_frame);if(!r.work_frame.reference_valid)invalid();
    std::set<std::string> ids;
    for(const auto& [id,key]:r.sources)if(id.empty()||key.empty()||!ids.insert(id).second)invalid();
    if(!program_check)return;
    const auto compiled=cam::compile(r.program,r.machine,r.tools,r.machine_work_origin);
    for(const auto& c:r.collisions) {
        if(!std::isfinite(c.intersection_mm3)||c.intersection_mm3<=0||
            std::ranges::none_of(compiled.motions,[&](const auto& m){return m.block_id==c.block_id&&m.tool_id==c.tool_id;}))invalid();
        static_cast<void>(Json(c));
    }
}
}
const std::string& cam_operation_id(const CamOperation& o){return std::visit([](const auto& v)->const std::string&{return v.id;},o);}
const std::string& cam_operation_name(const CamOperation& o){return std::visit([](const auto& v)->const std::string&{return v.name;},o);}
bool CamCalculated::operator==(const CamCalculated& other) const {
    return id==other.id&&intent_key==other.intent_key&&sources==other.sources&&program==other.program&&machine==other.machine&&
        machine_work_origin==other.machine_work_origin&&tools==other.tools&&work_frame==other.work_frame&&
        remaining->source_fingerprint==other.remaining->source_fingerprint&&collisions==other.collisions&&
        removed_mm3==other.removed_mm3&&missing_target_mm3==other.missing_target_mm3&&tolerance_mm==other.tolerance_mm;
}
bool CamDefinition::operator==(const CamDefinition& other) const {
    return stock_id==other.stock_id&&target_id==other.target_id&&fixture_ids==other.fixture_ids&&clamping_sketch_ids==other.clamping_sketch_ids&&
        work_frame==other.work_frame&&machine==other.machine&&machine_work_origin==other.machine_work_origin&&tools==other.tools&&
        operations==other.operations&&display_units==other.display_units&&tolerance_mm==other.tolerance_mm&&
        (calculated==other.calculated||(calculated&&other.calculated&&*calculated==*other.calculated));
}
void validate_cam_definition(const CamDefinition& d){intent_valid(d);if(d.calculated)result_valid(*d.calculated);}
void validate_cam_calculated(const CamCalculated& r){result_valid(r,true);}
std::string cam_intent_key(const CamDefinition& d) {
    const auto bytes=intent_json(d).dump();unsigned char digest[EVP_MAX_MD_SIZE];unsigned size{};
    if(EVP_Digest(bytes.data(),bytes.size(),digest,&size,EVP_sha256(),nullptr)!=1)
        throw std::runtime_error("CAM input fingerprint failed.");
    constexpr char hex[]="0123456789abcdef";std::string key="cam-intent-v1:";
    for(unsigned i=0;i<size;++i){key+=hex[digest[i]>>4];key+=hex[digest[i]&15];}return key;
}
bool cam_result_matches_definition(const CamDefinition& d) {
    return d.calculated&&d.work_frame.reference_valid&&d.calculated->intent_key==cam_intent_key(d);
}
bool cam_result_has_collisions(const CamCalculated& r) {
    return !r.collisions.empty()||r.missing_target_mm3>r.tolerance_mm*r.tolerance_mm*r.tolerance_mm;
}
void to_json(Json& j,const CamDefinition& d) {
    validate_cam_definition(d);j=intent_json(d);j["calculated"]=nullptr;
    if(d.calculated) {
        const auto& r=*d.calculated;
        j["calculated"]={{"id",r.id},{"intent",r.intent_key},{"sources",r.sources},{"program",r.program},{"machine",r.machine},
            {"machine_work_origin",point(r.machine_work_origin)},{"tools",r.tools},{"work_frame",r.work_frame},
            {"remaining",serialize_body_result(r.remaining)},{"collisions",r.collisions},{"removed_mm3",r.removed_mm3},
            {"missing_target_mm3",r.missing_target_mm3},{"tolerance_mm",r.tolerance_mm}};
    }
}
void from_json(const Json& j,CamDefinition& value) {
    CamDefinition d;d.stock_id=j.at("stock");d.target_id=j.at("target");d.fixture_ids=j.at("fixtures").get<std::vector<std::string>>();
    d.clamping_sketch_ids=j.at("clamping_sketches").get<std::vector<std::string>>();d.work_frame=j.at("work_frame").get<Placement>();
    d.machine=j.at("machine").get<cam::Machine>();d.machine_work_origin=point(j.at("machine_work_origin"));d.tools=j.at("tools").get<std::vector<cam::Tool>>();
    const auto units=j.at("units").get<std::string>();if(units!="mm"&&units!="in")invalid();d.display_units=units=="mm"?cam::Units::Millimetres:cam::Units::Inches;
    d.tolerance_mm=j.at("tolerance_mm");if(!j.at("operations").is_array())invalid();
    for(const auto& row:j.at("operations")) {
        const auto kind=row.at("kind").get<std::string>();
        if(kind=="manual")d.operations.push_back(CamManualOperation{row.at("id"),row.at("name"),row.at("program").get<cam::Program>()});
        else if(kind=="facing")d.operations.push_back(row.at("parameters").get<cam::Facing>());else invalid();
    }
    if(!j.at("calculated").is_null()) {
        const auto& row=j.at("calculated");auto r=std::make_shared<CamCalculated>();r->id=row.at("id");r->intent_key=row.at("intent");
        r->sources=row.at("sources").get<std::vector<std::pair<std::string,std::string>>>();r->program=row.at("program").get<cam::Program>();
        r->machine=row.at("machine").get<cam::Machine>();r->machine_work_origin=point(row.at("machine_work_origin"));r->tools=row.at("tools").get<std::vector<cam::Tool>>();
        r->work_frame=row.at("work_frame").get<Placement>();r->remaining=load_body_result(row.at("remaining"));
        r->collisions=row.at("collisions").get<std::vector<cam::Collision>>();r->removed_mm3=row.at("removed_mm3");
        r->missing_target_mm3=row.at("missing_target_mm3");r->tolerance_mm=row.at("tolerance_mm");d.calculated=std::move(r);
    }
    validate_cam_definition(d);if(d.calculated)validate_cam_calculated(*d.calculated);value=std::move(d);
}
} // namespace zima::document
