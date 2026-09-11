#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <functional>
#include <set>

namespace zima::sketcher {
namespace {
using Curve=kernel::BSplineGeometry;
void install(Sketch& sketch,const std::string& id,const Curve& curve) {
    curve.validate();
    auto spline=std::find_if(sketch.bsplines.begin(),sketch.bsplines.end(),[&](const auto& c){return c.id==id;});
    if(spline==sketch.bsplines.end()){sketch.bsplines.push_back({});spline=std::prev(sketch.bsplines.end());spline->id=id;}
    const auto old=spline->control_point_ids;
    std::vector<std::string> ids;
    for(std::size_t i=0;i<curve.poles.size();++i) {
        std::string point_id;
        if(i==0&&!old.empty())point_id=old.front();
        else if(i+1==curve.poles.size()&&old.size()>1)point_id=old.back();
        else if(i>0&&i+1<old.size())point_id=old[i];
        auto* point=point_id.empty()?nullptr:sketch.find_point(point_id);
        if(!point){sketch.points.push_back(Sketch::create_point(curve.poles[i].x,curve.poles[i].y));point=&sketch.points.back();}
        point->x=curve.poles[i].x;point->y=curve.poles[i].y;ids.push_back(point->id);
    }
    spline->control_point_ids=std::move(ids);spline->degree=curve.degree;spline->knots=curve.knots;
    spline->weights=curve.weights;spline->interpolating=false;
    spline->closed=std::hypot(curve.poles.front().x-curve.poles.back().x,curve.poles.front().y-curve.poles.back().y)<1e-12;
    for(const auto& point_id:old)if(std::ranges::find(spline->control_point_ids,point_id)==spline->control_point_ids.end()) {
        const bool referenced=std::ranges::any_of(sketch.constraints,[&](const auto& c){return c.first_point_id==point_id||c.second_point_id==point_id;}) ||
            std::ranges::any_of(sketch.segments,[&](const auto& c){return c.first_point_id==point_id||c.second_point_id==point_id;});
        if(!referenced)std::erase_if(sketch.points,[&](const auto& p){return p.id==point_id;});
    }
}
Curve axis_geometry(const std::string& id) {
    return id=="__sketch_axis_x__"?Curve{1,{{-1e6,0,0},{1e6,0,0}},{0,0,1,1},{1,1}}:Curve{1,{{0,-1e6,0},{0,1e6,0}},{0,0,1,1},{1,1}};
}
double nearest_parameter(const Curve& curve,kernel::Vec3 point) {
    auto error=[&](double t){const auto p=kernel::bspline_value(curve,t);return std::hypot(p.x-point.x,p.y-point.y);};
    unsigned best=0;double score=error(0);
    for(unsigned i=1;i<=256;++i)if(const double e=error(i/256.);e<score){best=i;score=e;}
    double a=std::max(0.,(double(best)-1)/256),b=std::min(1.,(double(best)+1)/256);
    for(unsigned i=0;i<70;++i){const double x=a+(b-a)/3,y=b-(b-a)/3;if(error(x)<error(y))b=y;else a=x;}
    return (a+b)/2;
}
std::optional<SketchTrimAnchor> anchor_at(const Sketch& sketch,const std::string& own,const Curve& curve,double parameter) {
    const auto point=kernel::bspline_value(curve,parameter);
    std::vector<std::string> ids;
    const auto collect=[&](const auto& values){for(const auto& value:values)if(value.id!=own)ids.push_back(value.id);};
    collect(sketch.segments);collect(sketch.circles);collect(sketch.arcs);collect(sketch.ellipses);collect(sketch.elliptical_arcs);collect(sketch.bsplines);
    std::optional<SketchTrimAnchor> found;
    for(const auto& id:ids) {
        if(sketch.find_offset(id)&&sketch.find_offset(id)->source_id==own)continue;
        const auto other=sketch.supporting_curve(id);const double t=nearest_parameter(other,point);const auto q=kernel::bspline_value(other,t);
        if(std::hypot(q.x-point.x,q.y-point.y)>1e-7)continue;
        if(found)throw std::invalid_argument("Trim endpoint has ambiguous intersecting curves");
        found=SketchTrimAnchor{id,t};
    }
    if(!found) {
        if(std::abs(point.y)<1e-9)found=SketchTrimAnchor{"__sketch_axis_x__",(point.x+1e6)/2e6};
        else if(std::abs(point.x)<1e-9)found=SketchTrimAnchor{"__sketch_axis_y__",(point.y+1e6)/2e6};
    }
    return found;
}
void follow_anchor(const Sketch& sketch,const Curve& curve,double& parameter,std::optional<SketchTrimAnchor>& anchor) {
    if(!anchor)return;
    const auto other=anchor->curve_id.starts_with("__sketch_axis_")?axis_geometry(anchor->curve_id):sketch.supporting_curve(anchor->curve_id);
    const double original=parameter,previous=anchor->parameter;double t=parameter,u=previous;
    const auto derivative=[](const Curve& c,double t){const double a=std::max(0.,t-1e-6),b=std::min(1.,t+1e-6);const auto p=kernel::bspline_value(c,a),q=kernel::bspline_value(c,b);return std::array{(q.x-p.x)/(b-a),(q.y-p.y)/(b-a)};};
    for(unsigned i=0;i<30;++i) {
        const auto p=kernel::bspline_value(curve,t),q=kernel::bspline_value(other,u);const double x=p.x-q.x,y=p.y-q.y;
        if(std::hypot(x,y)<1e-8){parameter=t;anchor->parameter=u;return;}
        const auto a=derivative(curve,t),b=derivative(other,u);const double det=-a[0]*b[1]+a[1]*b[0];
        if(std::abs(det)<1e-12)break;
        t+=(x*b[1]-b[0]*y)/det;u+=(a[1]*x-a[0]*y)/det;
        if(t<0||t>1||u<0||u>1||std::abs(t-original)>.15||std::abs(u-previous)>.15)break;
    }
    throw std::invalid_argument("Trim intersection is missing or moved to another branch");
}
Curve supporting(const Sketch& sketch,const std::string& id,std::set<std::string>& visiting) {
    if(!visiting.insert(id).second)throw std::invalid_argument("Cyclic Sketch curve dependency");
    if(const auto* offset=sketch.find_offset(id)) {
        auto source=supporting(sketch,offset->source_id,visiting);
        auto result=offset_curve_geometry(source,offset->flipped?-offset->distance:offset->distance,offset->tolerance);
        visiting.erase(id);return result;
    }
    std::string support_id=id;
    for(const auto& trim:sketch.curve_trims)if(trim.id==id){support_id=trim.support_id;break;}
    for(const auto& support:sketch.curve_supports)if(support.id==support_id){visiting.erase(id);return support.geometry;}
    auto result=sketch_curve_geometry(sketch,id);visiting.erase(id);return result;
}
}
const SketchOffset* Sketch::find_offset(const std::string& id) const {
    const auto it=std::find_if(offsets.begin(),offsets.end(),[&](const auto& c){return c.id==id;});
    return it==offsets.end()?nullptr:&*it;
}
kernel::BSplineGeometry Sketch::supporting_curve(const std::string& id) const {
    std::set<std::string> visiting;return supporting(*this,id,visiting);
}
std::string Sketch::add_offset(const std::string& source_id,double distance,bool flipped) {
    if(!std::isfinite(distance)||distance<=0)throw std::invalid_argument("Offset distance must be positive");
    const auto source=supporting_curve(source_id);
    const auto geometry=offset_curve_geometry(source,flipped?-distance:distance);
    auto next=*this;SketchOffset offset;
    offset.id=Sketch::create_point(0,0).id;offset.operation_id=offset.id;offset.source_id=source_id;offset.distance=distance;offset.flipped=flipped;
    // Starting from an already trimmed source keeps its currently selected interval.
    for(const auto& trim:curve_trims)if(trim.id==source_id){offset.start=trim.start;offset.end=trim.end;}
    if(const auto* previous=find_offset(source_id)){offset.start=previous->start;offset.end=previous->end;}
    install(next,offset.id,trim_curve_geometry(geometry,offset.start,offset.end));
    next.offsets.push_back(offset);next.validate();*this=std::move(next);return offset.id;
}
void Sketch::update_offset(const std::string& id,double distance,bool flipped) {
    if(!std::isfinite(distance)||distance<=0)throw std::invalid_argument("Offset distance must be positive");
    auto next=*this;auto it=std::find_if(next.offsets.begin(),next.offsets.end(),[&](const auto& c){return c.id==id;});
    if(it==next.offsets.end())throw std::invalid_argument("Missing offset");
    const auto group=it->operation_id,source=it->source_id;
    for(auto& offset:next.offsets)if(offset.operation_id==group){offset.distance=distance;offset.flipped=flipped;offset.source_id=source;}
    static_cast<void>(next.supporting_curve(id));
    next.refresh_curve_dependencies();
    if(std::ranges::any_of(next.offsets,[&](const auto& c){return c.operation_id==group&&c.broken;}))throw std::invalid_argument("Offset trim intersection requires repair");
    next.validate();*this=std::move(next);
}
void Sketch::free_offset(const std::string& id) {
    const auto* original=find_offset(id);if(!original)throw std::invalid_argument("Missing offset");
    auto next=*this;
    if(original->start!=0 || original->end!=1) {
        const double a=original->start,span=original->end-a;
        std::set<std::string> affected{id};
        for(bool changed=true;changed;) {changed=false;for(const auto& c:offsets)if(affected.contains(c.source_id)&&affected.insert(c.id).second)changed=true;}
        const auto rebase=[&](double p) {
            if(p<a-1e-10||p>a+span+1e-10)throw std::invalid_argument("Free downstream offsets before removing their hidden supporting interval");
            return std::clamp((p-a)/span,0.0,1.0);
        };
        const auto anchor=[&](auto& value){if(value&&affected.contains(value->curve_id))value->parameter=rebase(value->parameter);};
        for(auto& c:next.offsets) {
            if(c.id!=id&&affected.contains(c.id)){c.start=rebase(c.start);c.end=rebase(c.end);}
            anchor(c.start_anchor);anchor(c.end_anchor);
        }
        for(auto& c:next.curve_trims){anchor(c.start_anchor);anchor(c.end_anchor);}
    }
    // Keep the current native geometry and IDs. Its retained basis now spans 0..1.
    std::erase_if(next.offsets,[&](const auto& c){return c.id==id;});
    next.refresh_curve_dependencies();next.validate();*this=std::move(next);
}
void Sketch::refresh_curve_dependencies() {
    // A projected source updates only its own support. Offsets never reference an external edge.
    for(auto& support:curve_supports)for(const auto& block:import_blocks) {
        if(!block.source_path.starts_with("external-reference:")||std::ranges::find(block.geometry_ids,support.id)==block.geometry_ids.end())continue;
        const auto reference_id=block.source_path.substr(19);
        for(const auto& ref:external_references)if(ref.id==reference_id&&!ref.broken&&ref.exact_spline)support.geometry=*ref.exact_spline;
    }
    for(auto& trim:curve_trims) {
        const auto support=std::find_if(curve_supports.begin(),curve_supports.end(),[&](const auto& c){return c.id==trim.support_id;});
        if(support!=curve_supports.end())try {
            auto pending=trim;follow_anchor(*this,support->geometry,pending.start,pending.start_anchor);follow_anchor(*this,support->geometry,pending.end,pending.end_anchor);
            install(*this,trim.id,trim_curve_geometry(support->geometry,pending.start,pending.end));trim=std::move(pending);trim.broken=false;
        }catch(const std::exception&){trim.broken=true;}
    }
    for(auto& offset:offsets) {
        try {const auto curve=supporting_curve(offset.id);auto pending=offset;
            follow_anchor(*this,curve,pending.start,pending.start_anchor);follow_anchor(*this,curve,pending.end,pending.end_anchor);
            install(*this,offset.id,trim_curve_geometry(curve,pending.start,pending.end));offset=std::move(pending);offset.broken=false;}
        catch(const std::exception&){offset.broken=true;}
    }
    for(auto& block:import_blocks)if(block.source_path.starts_with("external-reference:") &&
        std::ranges::any_of(curve_trims,[&](const auto& trim){return std::ranges::find(block.geometry_ids,trim.id)!=block.geometry_ids.end();})) {
        block.point_ids.clear();
        for(const auto& c:bsplines)if(std::ranges::find(block.geometry_ids,c.id)!=block.geometry_ids.end())block.point_ids.insert(block.point_ids.end(),c.control_point_ids.begin(),c.control_point_ids.end());
        std::sort(block.point_ids.begin(),block.point_ids.end());block.point_ids.erase(std::unique(block.point_ids.begin(),block.point_ids.end()),block.point_ids.end());
    }
}
void Sketch::validate_curve_dependencies() const {
    std::set<std::string> support_ids,derived_ids;
    for(const auto& support:curve_supports){if(support.id.empty()||!support_ids.insert(support.id).second)throw std::invalid_argument("Duplicate curve support");support.geometry.validate();}
    const auto interval=[](double a,double b){if(!std::isfinite(a)||!std::isfinite(b)||a<0||b>1||b-a<1e-12)throw std::invalid_argument("Invalid curve interval");};
    const auto output=[&](const std::string& id){if(id.empty()||!derived_ids.insert(id).second||!std::ranges::any_of(bsplines,[&](const auto& c){return c.id==id;}))throw std::invalid_argument("Missing or duplicate derived curve");};
    for(const auto& trim:curve_trims){output(trim.id);interval(trim.start,trim.end);if(!support_ids.contains(trim.support_id))throw std::invalid_argument("Missing curve support");}
    for(const auto& offset:offsets){output(offset.id);interval(offset.start,offset.end);
        if(offset.source_id.empty()||offset.operation_id.empty()||!std::isfinite(offset.distance)||offset.distance<=0||!std::isfinite(offset.tolerance)||offset.tolerance<=0)throw std::invalid_argument("Invalid offset parameters");
        std::set<std::string> visited{offset.id};auto id=offset.source_id;
        while(const auto* parent=find_offset(id)){if(!visited.insert(id).second)throw std::invalid_argument("Cyclic offset dependency");id=parent->source_id;}
    }
}
std::vector<std::string> Sketch::retain_curve_intervals(const std::string& id,const std::vector<std::array<double,2>>& intervals) {
    auto next=*this;const auto support=supporting_curve(id);
    const auto* offset=find_offset(id);std::optional<SketchOffset> original_offset;
    if(offset)original_offset=*offset;
    double start=0,end=1;std::string support_id=id;
    if(offset){start=offset->start;end=offset->end;}
    for(const auto& trim:curve_trims)if(trim.id==id){start=trim.start;end=trim.end;support_id=trim.support_id;}
    std::erase_if(next.offsets,[&](const auto& c){return c.id==id;});
    std::erase_if(next.curve_trims,[&](const auto& c){return c.id==id;});
    // The source identity now denotes the complete support, independently of the visible survivors.
    if(!offset&&!std::ranges::any_of(next.curve_supports,[&](const auto& c){return c.id==support_id;}))next.curve_supports.push_back({support_id,support});
    // Temporarily remove the external block's visible ownership; its reference remains on the support.
    auto blocks=next.import_blocks;
    std::erase_if(next.import_blocks,[&](const auto& b){return std::ranges::find(b.geometry_ids,id)!=b.geometry_ids.end();});
    next.remove_geometry(id);
    std::vector<std::string> result;
    for(const auto& range:intervals) {
        if(range[0]<0||range[1]>1||range[1]<=range[0])throw std::invalid_argument("Invalid retained interval");
        const double a=start+(end-start)*range[0],b=start+(end-start)*range[1];
        const auto new_id=result.empty()?id:Sketch::create_point(0,0).id;
        install(next,new_id,trim_curve_geometry(support,a,b));
        std::optional<SketchTrimAnchor> first,last;
        if(range[0]>1e-10)first=anchor_at(*this,id,support,a);
        if(range[1]<1-1e-10)last=anchor_at(*this,id,support,b);
        if(original_offset){auto copy=*original_offset;copy.id=new_id;copy.start=a;copy.end=b;if(range[0]>1e-10)copy.start_anchor=first;if(range[1]<1-1e-10)copy.end_anchor=last;next.offsets.push_back(copy);}
        else {
            SketchCurveTrim trim{new_id,support_id,a,b,first,last};
            for(const auto& old:curve_trims)if(old.id==id){if(range[0]<=1e-10)trim.start_anchor=old.start_anchor;if(range[1]>=1-1e-10)trim.end_anchor=old.end_anchor;}
            next.curve_trims.push_back(trim);
        }
        result.push_back(new_id);
    }
    // Join only the known parameter seam of a closed support, never nearby points.
    if(std::hypot(support.poles.front().x-support.poles.back().x,support.poles.front().y-support.poles.back().y)<1e-12) {
        std::string first_id,last_id;
        for(std::size_t i=0;i<intervals.size();++i) {
            const double a=start+(end-start)*intervals[i][0],b=start+(end-start)*intervals[i][1];
            if(a==0)first_id=result[i];if(b==1)last_id=result[i];
        }
        if(!first_id.empty()&&!last_id.empty()&&first_id!=last_id) {
            auto first=std::find_if(next.bsplines.begin(),next.bsplines.end(),[&](const auto& c){return c.id==first_id;});
            auto last=std::find_if(next.bsplines.begin(),next.bsplines.end(),[&](const auto& c){return c.id==last_id;});
            const auto removed=last->control_point_ids.back();last->control_point_ids.back()=first->control_point_ids.front();
            std::erase_if(next.points,[&](const auto& p){return p.id==removed;});
        }
    }
    // Retain only valid visible ownership; the support keeps the projected curve's shape.
    for(auto& block:blocks)if(block.source_path.starts_with("external-reference:")&&std::ranges::find(block.geometry_ids,id)!=block.geometry_ids.end()&&!result.empty()) {
        block.geometry_ids=result;block.point_ids.clear();
        for(const auto& c:next.bsplines)if(std::ranges::find(result,c.id)!=result.end())block.point_ids.insert(block.point_ids.end(),c.control_point_ids.begin(),c.control_point_ids.end());
        std::sort(block.point_ids.begin(),block.point_ids.end());block.point_ids.erase(std::unique(block.point_ids.begin(),block.point_ids.end()),block.point_ids.end());
        next.import_blocks.push_back(block);
    }
    next.refresh_curve_dependencies();next.validate();*this=std::move(next);return result;
}
}
