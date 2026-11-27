#include <zima/document/mech_keyway.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/loft.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <map>
#include <stdexcept>

namespace zima::document {
void PartDocument::validate_mech_keyways() const {
    std::map<std::string,std::map<std::string,const HistoryContainer*>> groups;
    for(const auto& item:history) {
        const auto& binding=item.mech_keyway_binding;
        if(binding.parent_id.empty()) {
            if(!binding.role.empty())throw std::invalid_argument("Incomplete keyway child binding");
            continue;
        }
        const auto* parent=find_container(binding.parent_id);
        if(!parent||parent->feature_kind!=FeatureKind::MechKeyway||
            item.feature_kind==FeatureKind::MechKeyway||!item.sheet_profile_binding.parent_id.empty())
            throw std::invalid_argument("Invalid keyway child binding");
        if(body_owner_for_object(parent->id)!=body_owner_for_object(item.id)||
            parent->suppressed!=item.suppressed)
            throw std::invalid_argument("Keyway children must share their parent's Body and suppression state");
        if(!groups[parent->id].emplace(binding.role,&item).second)
            throw std::invalid_argument("Duplicate keyway child role");
    }
    for(const auto& parent:history) {
        if(parent.feature_kind!=FeatureKind::MechKeyway)continue;
        validate_mech_keyway(parent.mech_keyway);
        const auto* body=body_owner_for_object(parent.id);
        if(!body)throw std::invalid_argument("Keyway must belong to an editable Body");
        const bool unresolved=reference_errors.contains(parent.id);
        const auto group=groups.find(parent.id);
        auto row=std::ranges::find(body->entries,parent.id,&PartHistoryEntry::id);
        if(row==body->entries.end())throw std::invalid_argument("Keyway is missing from Body history");
        // A confirmed preparation failure can precede the first valid cut.
        // Existing compounds retain their last valid children for later repair.
        if(unresolved&&group==groups.end())continue;
        const auto has_role=[&](const char* role){return group!=groups.end()&&group->second.contains(role);};
        std::vector<std::pair<std::string,FeatureKind>> roles{{"cut",FeatureKind::Feature}};
        const auto actual_cut=std::ranges::find_if(history,[&](const auto& c){return c.mech_keyway_binding.parent_id==parent.id&&c.mech_keyway_binding.role=="cut";});
        if(actual_cut!=history.end()&&actual_cut->feature_kind==FeatureKind::Loft) {
            if(!unresolved&&(!parent.mech_keyway.internal||parent.mech_keyway.floor!=KeywayFloor::ParallelToGenerator))
                throw std::invalid_argument("Invalid keyway child binding");
            roles.front().second=FeatureKind::Loft;
        }
        if(unresolved?has_role("bottom-fillet"):parent.mech_keyway.bottom_radius>0)roles.push_back({"bottom-fillet",FeatureKind::Fillet});
        if(unresolved?has_role("retaining-1"):parent.mech_keyway.retaining_screws>0)roles.push_back({"retaining-1",FeatureKind::Hole});
        if(unresolved?has_role("retaining-2"):parent.mech_keyway.retaining_screws>1)roles.push_back({"retaining-2",FeatureKind::Hole});
        roles.push_back({"annotation",FeatureKind::Sketch});
        if(group==groups.end()||group->second.size()!=roles.size())
            throw std::invalid_argument("Keyway is missing its ordinary feature children");
        const auto begin=static_cast<std::size_t>(row-body->entries.begin());
        for(const auto& [role,kind]:roles) {
            const auto child=group->second.find(role);
            if(child==group->second.end()||child->second->feature_kind!=kind||
                ++row==body->entries.end()||row->kind!=PartHistoryKind::Feature||row->id!=child->second->id)
                throw std::invalid_argument("Keyway children must remain one ordered history group");
            if(role=="cut") {
                const auto& cut=*child->second;
                if(cut.feature_kind==FeatureKind::Loft) {
                    if(cut.combine_mode!=CombineMode::Subtract||cut.loft.profiles.size()!=2||!cut.loft.guides.empty())
                        throw std::invalid_argument("Keyway cut must own its ordinary subtractive profile");
                    validate_loft(cut);continue;
                }
                const auto profile=std::ranges::find(sketches,cut.feature.sketch_id,&sketcher::Sketch::id);
                if(cut.combine_mode!=CombineMode::Subtract||cut.feature.type!=FeatureType::Modeling||
                    profile==sketches.end()||profile->owner_container_id!=cut.id)
                    throw std::invalid_argument("Keyway cut must own its ordinary subtractive profile");
            } else if(role=="annotation") {
                if(std::ranges::count(sketches,child->second->id,&sketcher::Sketch::owner_container_id)!=1)
                    throw std::invalid_argument("Keyway must own one annotation Sketch");
            }
        }
        if(body->cursor>begin&&body->cursor<begin+1+roles.size())
            throw std::invalid_argument("History insertion cannot split a keyway");
    }
}

namespace {
using V=kernel::Vec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V mul(V a,double k){return {a.x*k,a.y*k,a.z*k};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V v){const double n=std::sqrt(dot(v,v));
    if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("Invalid keyway orientation");
    return mul(v,1/n);}
bool finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
nlohmann::json ref(const kernel::FaceReference& r){return {{"owner",r.owner_id},{"key",r.semantic_key},{"occurrence",r.instance_path}};}
kernel::FaceReference ref(const nlohmann::json& j){kernel::FaceReference r;
    r.owner_id=j.at("owner");r.semantic_key=j.at("key");r.instance_path=j.at("occurrence");return r;}
template<class Enum> Enum enumeration(const nlohmann::json& j,int count){
    const auto n=j.get<int>();if(n<0||n>=count)throw std::invalid_argument("Invalid keyway choice");return static_cast<Enum>(n);}
sketcher::Sketch sketch(const std::string& owner,const char* role,V o,V x,V y,V n){
    auto s=sketcher::Sketch::create_default();s.id=owner+":"+role;s.owner_container_id=owner;
    s.resolved_origin=o;s.resolved_x_axis=x;s.resolved_y_axis=y;s.resolved_normal=n;
    s.plane_auto=false;return s;
}
std::string point(sketcher::Sketch& s,const char* role,double x,double y,bool construction=false){
    auto id=s.id+":"+role;s.points.push_back({id,x,y,false,construction});return id;
}
void line(sketcher::Sketch& s,const char* role,const std::string& a,const std::string& b){
    s.segments.push_back({s.id+":"+role,a,b});
}
}

KeywayFrame keyway_frame(const kernel::SurfaceGeometry& s,V orientation,double station,double angle){
    if((s.kind!=kernel::SurfaceGeometry::Kind::Cylinder&&s.kind!=kernel::SurfaceGeometry::Kind::Cone)||
        !finite(s.origin)||!std::isfinite(s.radius)||s.radius<0||
        !std::isfinite(station)||!std::isfinite(angle)||!std::isfinite(s.semi_angle)||
        std::abs(s.semi_angle)>=std::numbers::pi/2)
        throw std::invalid_argument("Keyway requires a cylindrical or conical material face");
    KeywayFrame f;f.axis=unit(s.axis);f.origin=add(s.origin,mul(f.axis,station));
    f.slope=s.kind==kernel::SurfaceGeometry::Kind::Cone?std::tan(s.semi_angle):0;
    f.radius=s.radius+station*f.slope;f.internal=s.reversed;
    if(!std::isfinite(f.radius)||f.radius<=1e-9)throw std::invalid_argument("Keyway station is outside the cone branch");
    auto radial=unit(add(orientation,mul(f.axis,-dot(orientation,f.axis))));
    const double a=angle*std::numbers::pi/180;
    f.radial=add(mul(radial,std::cos(a)),mul(cross(f.axis,radial),std::sin(a)));
    f.tangent=cross(f.radial,f.axis);return f;
}

void validate_mech_keyway(const MechKeywayParameters& p){
    for(const auto& limit:p.limits)
        if(!std::isfinite(limit.length)||limit.length<0||!std::isfinite(limit.correction))
            throw std::invalid_argument("Invalid keyway axial limit");
    for(double n:{p.width,p.key_height,p.depth,p.length,p.axial_offset,p.angular_offset,
            p.measurement_station,p.first_overrun,p.last_overrun,p.bottom_radius,p.woodruff_diameter,
            p.retaining_diameter,p.retaining_pitch,p.retaining_depth})
        if(!std::isfinite(n))throw std::invalid_argument("Keyway dimensions must be finite");
    if(p.width<=0||p.key_height<=0||p.depth<=0||p.length<=0||p.first_overrun<0||p.last_overrun<0||
       p.bottom_radius<0||p.bottom_radius>=p.width/2||p.bottom_radius>=p.depth||
       (p.axial_direction!=1&&p.axial_direction!=-1)||p.retaining_screws>2||
       (p.first_end==KeywayEnd::Round&&p.first_overrun!=0)||
       (p.last_end==KeywayEnd::Round&&p.last_overrun!=0))
        throw std::invalid_argument("Invalid keyway dimensions");
    if(p.type==KeywayType::Woodruff&&(!p.internal&&(p.woodruff_diameter<=2*p.depth||
       p.key_height>=p.woodruff_diameter||p.position==KeywayPosition::TwoSided||p.first_overrun||p.last_overrun)||
       p.retaining_screws))
        throw std::invalid_argument("Invalid Woodruff keyway dimensions");
    if(p.retaining_screws&&(p.internal||p.retaining_diameter<=0||p.retaining_pitch<=0||
       p.retaining_depth<=0||p.retaining_diameter>=p.width||
       (p.retaining_screws==2&&p.length<=2*p.width+p.retaining_diameter)))
        throw std::invalid_argument("Retaining screw holes do not fit this keyway");
}

nlohmann::json serialize_mech_keyway(const MechKeywayParameters& p){
    validate_mech_keyway(p);
    auto limits=nlohmann::json::array();
    for(const auto& limit:p.limits)limits.push_back({{"termination",static_cast<int>(limit.termination)},
        {"length",limit.length},{"correction",limit.correction},{"reference",ref(limit.reference)}});
    return {{"support",ref(p.support)},{"limits",std::move(limits)},
        {"type",static_cast<int>(p.type)},{"position",static_cast<int>(p.position)},{"floor",static_cast<int>(p.floor)},
        {"first_end",static_cast<int>(p.first_end)},{"last_end",static_cast<int>(p.last_end)},
        {"internal",p.internal},{"width",p.width},{"key_height",p.key_height},{"depth",p.depth},{"length",p.length},
        {"axial_offset",p.axial_offset},{"angular_offset",p.angular_offset},{"measurement_station",p.measurement_station},
        {"axial_direction",p.axial_direction},{"first_overrun",p.first_overrun},{"last_overrun",p.last_overrun},
        {"bottom_radius",p.bottom_radius},{"woodruff_diameter",p.woodruff_diameter},{"table_id",p.table_id},{"table_row_id",p.table_row_id},
        {"width_fit",p.width_fit},{"depth_fit",p.depth_fit},{"depth_upper_tolerance",p.depth_upper_tolerance},{"depth_lower_tolerance",p.depth_lower_tolerance},
        {"retaining_screws",p.retaining_screws},{"retaining_diameter",p.retaining_diameter},{"retaining_pitch",p.retaining_pitch},
        {"retaining_depth",p.retaining_depth},{"walls_symbol_id",p.walls_symbol_id},{"floor_symbol_id",p.floor_symbol_id},
        {"show_roughness",p.show_roughness}};
}
MechKeywayParameters load_mech_keyway(const nlohmann::json& j){
    MechKeywayParameters p;p.support=ref(j.at("support"));
    if(j.at("limits").size()!=2)throw std::invalid_argument("Keyway requires two axial limits");
    for(std::size_t i=0;i<2;++i){const auto& limit=j.at("limits").at(i);
        p.limits[i].termination=enumeration<KeywayTermination>(limit.at("termination"),2);
        limit.at("length").get_to(p.limits[i].length);limit.at("correction").get_to(p.limits[i].correction);
        p.limits[i].reference=ref(limit.at("reference"));}
    p.type=enumeration<KeywayType>(j.at("type"),2);p.position=enumeration<KeywayPosition>(j.at("position"),3);
    p.floor=enumeration<KeywayFloor>(j.at("floor"),2);p.first_end=enumeration<KeywayEnd>(j.at("first_end"),2);p.last_end=enumeration<KeywayEnd>(j.at("last_end"),2);
#define LOAD(field) j.at(#field).get_to(p.field)
    LOAD(internal);LOAD(width);LOAD(key_height);LOAD(depth);LOAD(length);LOAD(axial_offset);LOAD(angular_offset);LOAD(measurement_station);
    LOAD(axial_direction);LOAD(first_overrun);LOAD(last_overrun);LOAD(bottom_radius);LOAD(woodruff_diameter);LOAD(table_id);LOAD(table_row_id);
    LOAD(width_fit);LOAD(depth_fit);LOAD(depth_upper_tolerance);LOAD(depth_lower_tolerance);LOAD(retaining_screws);LOAD(retaining_diameter);LOAD(retaining_pitch);
    LOAD(retaining_depth);LOAD(walls_symbol_id);LOAD(floor_symbol_id);LOAD(show_roughness);
#undef LOAD
    validate_mech_keyway(p);return p;
}

std::array<double,2> keyway_axial_extents(const MechKeywayParameters& p,double reference,
        const std::array<std::optional<double>,2>& targets){
    validate_mech_keyway(p);
    if(!std::isfinite(reference))throw std::invalid_argument("Invalid keyway reference station");
    double start=reference+p.axial_direction*p.axial_offset;
    double finish=start+p.axial_direction*p.length;
    if(p.position==KeywayPosition::Symmetric){start-=p.axial_direction*p.length/2;finish-=p.axial_direction*p.length/2;}
    if(p.position==KeywayPosition::TwoSided){
        const double centre=start;
        std::array<double,2> endpoints;
        for(std::size_t i=0;i<2;++i){
            const auto& limit=p.limits[i];const double sign=(i?-1:1)*p.axial_direction;
            double distance=limit.length;
            if(limit.termination==KeywayTermination::UpTo){
                if(!targets[i]||!std::isfinite(*targets[i]))
                    throw std::invalid_argument("Keyway end reference is unavailable");
                distance=(*targets[i]-centre)*sign+limit.correction;
            }
            if(distance < -1e-9)throw std::invalid_argument("Keyway end reference is not in the selected direction");
            endpoints[i]=centre+sign*std::max(0.,distance);
        }
        start=endpoints[1];finish=endpoints[0];
        if((finish-start)*p.axial_direction<=1e-9)throw std::invalid_argument("Keyway axial limits have zero length");
    }
    if(p.type==KeywayType::Woodruff&&!p.internal) {
        // Woodruff position is the cutter centre. Length is derived from the
        // selected disc diameter and segment height, not an end-milled slot.
        const double centre=reference+p.axial_direction*p.axial_offset;
        const double half=std::sqrt(p.key_height*(p.woodruff_diameter-p.key_height));
        start=centre-p.axial_direction*half;finish=centre+p.axial_direction*half;
    }
    return {start,finish};
}

KeywayCutDefinition keyway_cut_definition(const std::string& owner,const MechKeywayParameters& p,
        const KeywayFrame& f,double reference,const std::array<std::optional<double>,2>& targets){
    if(owner.empty()||p.internal!=f.internal||(!p.internal&&p.depth>=f.radius))
        throw std::invalid_argument("Invalid keyway support or depth");
    if(!p.internal&&f.slope!=0&&p.type!=KeywayType::Woodruff)
        throw std::invalid_argument("A tapered shaft supports only a Woodruff keyway");
    const auto [start,finish]=keyway_axial_extents(p,reference,targets);
    if(p.type==KeywayType::Woodruff&&!p.internal&&std::abs((start+finish)/2-p.measurement_station)>1e-8)
        throw std::invalid_argument("Woodruff depth must use the cutter-centre station");
    KeywayCutDefinition result;
    result.nominal_start=start;result.nominal_end=finish;
    const auto axial=mul(f.axis,static_cast<double>(p.axial_direction));
    const auto tangent=mul(f.tangent,static_cast<double>(p.axial_direction));
    const double lo=(start-p.measurement_station)*p.axial_direction;
    const double hi=(finish-p.measurement_station)*p.axial_direction;
    const double floor_radius=f.radius+(p.internal?p.depth:-p.depth);
    result.measurement=p.type==KeywayType::Woodruff&&!p.internal?p.depth:2*f.radius+(p.internal?p.depth:-p.depth);
    if(p.type==KeywayType::Woodruff&&!p.internal){
        const auto center=add(add(f.origin,mul(axial,(lo+hi)/2)),mul(f.radial,floor_radius+p.woodruff_diameter/2));
        result.profile=sketch(owner,"profile",center,axial,f.radial,mul(tangent,-1));
        auto c=point(result.profile,"cutter-centre",0,0);
        result.profile.circles.push_back({result.profile.id+":cutter",c,p.woodruff_diameter/2});
        result.forward=result.reverse=p.width/2;
    }else if(p.internal){
        const double cut_start=start-p.axial_direction*p.first_overrun;
        const double cut_finish=finish+p.axial_direction*p.last_overrun;
        const double first=std::min(cut_start,cut_finish),last=std::max(cut_start,cut_finish);
        const double min_radius=f.radius+std::min((first-p.measurement_station)*f.slope,(last-p.measurement_station)*f.slope);
        if(min_radius<=0||p.width>=2*min_radius)
            throw std::invalid_argument("Keyway crosses the cone apex or exceeds the support diameter");
        const double centre=reference+p.axial_direction*p.axial_offset;
        const auto section=[&](const std::string& section_owner,double at) {
            const double radius=f.radius+(at-p.measurement_station)*f.slope;
            const double floor=(p.floor==KeywayFloor::ParallelToGenerator?radius:f.radius)+p.depth;
            auto profile=sketch(section_owner,"profile",add(add(f.origin,mul(f.axis,at-p.measurement_station)),mul(f.radial,radius)),
                f.tangent,f.radial,f.axis);
            // The lower side lies wholly inside the existing bore. Its exact
            // position does not define material or an additional cut branch.
            auto fl=point(profile,"first-left",-p.width/2,-radius),fr=point(profile,"first-right",p.width/2,-radius);
            auto ll=point(profile,"last-left",-p.width/2,floor-radius),lr=point(profile,"last-right",p.width/2,floor-radius);
            line(profile,"first-end",fl,fr);line(profile,"right-wall",fr,lr);
            line(profile,"last-end",lr,ll);line(profile,"left-wall",ll,fl);
            return profile;
        };
        result.profile=section(owner,centre);
        result.forward=last-centre;result.reverse=centre-first;
        if(f.slope!=0&&p.floor==KeywayFloor::ParallelToGenerator)
            result.loft_sections={section(owner+":section:first",first),section(owner+":section:last",last)};
    }else{
        const double rise=p.floor==KeywayFloor::ParallelToGenerator?f.slope*p.axial_direction:0;
        const double scale=std::hypot(1.,rise);
        const auto floor_axis=unit(add(axial,mul(f.radial,rise)));
        const auto normal=unit(cross(floor_axis,tangent));
        // Author the tool from the support surface. Moving its sketch along
        // the exact normal preserves the established flat floor and axial
        // profile, including a floor parallel to a conical generator.
        const auto floor_origin=add(f.origin,mul(f.radial,floor_radius));
        const double normal_depth=p.depth/scale;
        auto surface_origin=add(floor_origin,mul(normal,p.internal?-normal_depth:normal_depth));
        double profile_shift=0,forward_depth=normal_depth;
        if(p.internal&&f.slope!=0&&p.floor==KeywayFloor::ParallelToAxis){
            // An axis-parallel Sketch through the middle of a cone crosses
            // its bore and makes inward Up To select the opposite wall. Put
            // that same planar profile on the largest-radius end generator.
            // Keep the requested floor and all axial stations unchanged.
            profile_shift=f.slope*p.axial_direction>0?hi+p.last_overrun:lo-p.first_overrun;
            const double radius=f.radius+profile_shift*p.axial_direction*f.slope;
            surface_origin=add(add(f.origin,mul(axial,profile_shift)),mul(f.radial,radius));
            forward_depth=floor_radius-radius;
            if(forward_depth<=1e-9)throw std::invalid_argument("Invalid keyway support or depth");
        }
        result.profile=sketch(owner,"profile",surface_origin,floor_axis,tangent,normal);
        const double a=(lo-p.first_overrun-profile_shift)*scale,b=(hi+p.last_overrun-profile_shift)*scale,r=p.width/2;
        const double first=a+(p.first_end==KeywayEnd::Round?r:0),last=b-(p.last_end==KeywayEnd::Round?r:0);
        if(last-first<1e-9)throw std::invalid_argument("Keyway is too short for its rounded ends");
        auto fl=point(result.profile,"first-left",first,-r),fr=point(result.profile,"first-right",first,r);
        auto ll=point(result.profile,"last-left",last,-r),lr=point(result.profile,"last-right",last,r);
        line(result.profile,"left-wall",fl,ll);line(result.profile,"right-wall",lr,fr);
        if(p.first_end==KeywayEnd::Round){auto c=point(result.profile,"first-centre",first,0);
            result.profile.arcs.push_back({result.profile.id+":first-end",c,fr,fl,r,std::numbers::pi/2,3*std::numbers::pi/2});
        }else line(result.profile,"first-end",fr,fl);
        if(p.last_end==KeywayEnd::Round){auto c=point(result.profile,"last-centre",last,0);
            result.profile.arcs.push_back({result.profile.id+":last-end",c,ll,lr,r,-std::numbers::pi/2,std::numbers::pi/2});
        }else line(result.profile,"last-end",ll,lr);
        const double min_r=f.radius+std::min((a/scale+profile_shift)*p.axial_direction*f.slope,(b/scale+profile_shift)*p.axial_direction*f.slope);
        const double max_r=f.radius+std::max((a/scale+profile_shift)*p.axial_direction*f.slope,(b/scale+profile_shift)*p.axial_direction*f.slope);
        if(min_r<=0||p.width>=2*min_r)throw std::invalid_argument("Keyway crosses the cone apex or exceeds the support diameter");
        const double reach=(p.depth+std::max(std::abs(max_r-f.radius),std::abs(min_r-f.radius))+p.width)*scale;
        if(p.internal){result.forward=forward_depth;result.reverse=reach;}
        else result.reverse=normal_depth;
    }
    result.profile.validate();
    for(unsigned i=0;i<p.retaining_screws;++i){
        const double at=p.retaining_screws==1?(lo+hi)/2:(i?hi-p.width:lo+p.width);
        const double rise=p.floor==KeywayFloor::ParallelToGenerator?at*p.axial_direction*f.slope:0;
        result.retaining_centres.push_back(add(add(f.origin,mul(axial,at)),mul(f.radial,floor_radius+rise)));
    }
    return result;
}

sketcher::Sketch keyway_measurement_sketch(const std::string& owner,const MechKeywayParameters& p,const KeywayFrame& f){
    validate_mech_keyway(p);
    auto s=sketch(owner,"measurement",f.origin,f.tangent,f.radial,f.axis);
    const bool direct=p.type==KeywayType::Woodruff&&!p.internal;
    const auto support=point(s,direct?"surface":"opposite",0,direct?f.radius:-f.radius,true);
    const auto floor=point(s,"floor",0,f.radius+(p.internal?p.depth:-p.depth),true);
    sketcher::SketchDimension d;d.id=s.id+(direct?":surface-to-floor":":floor-to-opposite");d.kind=sketcher::DimensionKind::DistanceY;
    d.first_point_id=direct?floor:support;d.second_point_id=direct?support:floor;d.driving=false;
    d.value=direct?p.depth:2*f.radius+(p.internal?p.depth:-p.depth);
    d.tolerance_mode="deviations";d.upper_tolerance=p.depth_upper_tolerance;d.lower_tolerance=p.depth_lower_tolerance;
    if(!p.depth_fit.empty()){d.suffix=p.depth_fit;d.tolerance_mode.clear();d.upper_tolerance.clear();d.lower_tolerance.clear();}
    d.placement=std::array{p.width+5.,0.};s.dimensions.push_back(std::move(d));s.validate();return s;
}
}
