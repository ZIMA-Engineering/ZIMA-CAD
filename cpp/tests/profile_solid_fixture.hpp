#pragma once
#include <zima/document/part_document.hpp>
#include "profile_request_fixture.hpp"
#include <algorithm>
#include <utility>
#include <zima/kernel/dimension_layout.hpp>

namespace zima::test {
// Fixture geometry is authored as a real Sketch and ordinary profile feature.
// Keep dimensions/placement editable through the same native data as the GUI.
inline document::HistoryContainer rectangular_feature(document::PartDocument& document,
        kernel::Vec3 size={100,80,50}) {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(-size.x/2,-size.y/2,size.x/2,size.y/2));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);
    sketch.owner_container_id=feature.id;
    feature.extrusion.extent_mode=document::ProfileExtentMode::Symmetric;
    feature.extrusion.length_forward=size.z/2;
    document.sketches.push_back(std::move(sketch));
    return feature;
}
inline void resize_rectangular_feature(document::PartDocument& document,
        document::HistoryContainer& feature,kernel::Vec3 size) {
    auto sketch=std::ranges::find(document.sketches,feature.extrusion.sketch_id,&sketcher::Sketch::id);
    if(sketch==document.sketches.end())throw std::runtime_error("Fixture Sketch missing");
    double x=0,y=0;
    for(const auto& point:sketch->points){x=std::max(x,std::abs(point.x));y=std::max(y,std::abs(point.y));}
    for(auto& point:sketch->points){point.x*=size.x/(2*x);point.y*=size.y/(2*y);}
    for(auto& dim:sketch->dimensions) {
        const auto* a=sketch->find_point(dim.first_point_id);const auto* b=sketch->find_point(dim.second_point_id);
        if(a&&b&&dim.kind==sketcher::DimensionKind::DistanceX)dim.value=std::abs(b->x-a->x);
        if(a&&b&&dim.kind==sketcher::DimensionKind::DistanceY)dim.value=std::abs(b->y-a->y);
    }
    feature.extrusion.length_forward=size.z/2;
}
inline document::HistoryContainer circular_feature(document::PartDocument& document,
        double radius=40,double height=50) {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_circle(0,0,radius));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);
    sketch.owner_container_id=feature.id;
    feature.extrusion.length_forward=height;
    document.sketches.push_back(std::move(sketch));
    return feature;
}
inline double profile_dimension(const document::PartDocument& doc,
        const document::HistoryContainer& feature,int axis) {
    if(axis==2)return feature.extrusion.length_forward*
        (feature.extrusion.extent_mode==document::ProfileExtentMode::Symmetric?2:1);
    const auto sketch=std::ranges::find(doc.sketches,feature.extrusion.sketch_id,&sketcher::Sketch::id);
    if(sketch==doc.sketches.end())throw std::runtime_error("Fixture Sketch missing");
    double low=1e100,high=-1e100;
    for(const auto& p:sketch->points){const double v=axis==0?p.x:p.y;low=std::min(low,v);high=std::max(high,v);}
    return high-low;
}
struct ProfileDimension {
    document::PartDocument& doc;document::HistoryContainer& feature;int axis;
    operator double() const {return profile_dimension(std::as_const(doc),feature,axis);}
    ProfileDimension& operator=(double value) {
        kernel::Vec3 size{profile_dimension(std::as_const(doc),feature,0),
            profile_dimension(std::as_const(doc),feature,1),profile_dimension(std::as_const(doc),feature,2)};
        if(axis==0)size.x=value;else if(axis==1)size.y=value;else size.z=value;
        resize_rectangular_feature(doc,feature,size);return *this;
    }
    ProfileDimension& operator+=(double value){return *this=double(*this)+value;}
    ProfileDimension& operator*=(double value){return *this=double(*this)*value;}
};
inline ProfileDimension profile_dimension(document::PartDocument& doc,document::HistoryContainer& feature,int axis){return {doc,feature,axis};}
inline double& circular_radius(document::PartDocument& doc,const document::HistoryContainer& feature) {
    const auto sketch=std::ranges::find(doc.sketches,feature.extrusion.sketch_id,&sketcher::Sketch::id);
    if(sketch==doc.sketches.end()||sketch->circles.empty())throw std::runtime_error("Fixture circle missing");
    return sketch->circles.front().radius;
}
inline double circular_radius(const document::PartDocument& doc,const document::HistoryContainer& feature) {
    const auto sketch=std::ranges::find(doc.sketches,feature.extrusion.sketch_id,&sketcher::Sketch::id);
    if(sketch==doc.sketches.end()||sketch->circles.empty())throw std::runtime_error("Fixture circle missing");
    return sketch->circles.front().radius;
}
inline document::HistoryContainer spherical_feature(document::PartDocument& doc,double radius=40) {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_arc(0,0,0,-radius,0,radius));
    static_cast<void>(sketch.add_segment(0,radius,0,-radius));
    const auto axis=sketch.add_segment(0,radius+1,0,-radius-1);
    sketch.set_segment_centerline(axis,true);
    auto feature=document::PartDocument::create_revolution_container(sketch.id);feature.revolution.axis_segment_id=axis;
    sketch.owner_container_id=feature.id;doc.sketches.push_back(std::move(sketch));return feature;
}

// Select fixture topology by its authored profile position. The returned key
// always names the real Sketch parent; these selectors are not product IDs.
inline std::string profile_key(const document::PartDocument& doc,
        const std::string& owner,const std::string& selector) {
    const auto* feature=doc.find_container(owner);
    if(!feature||feature->feature_kind!=document::FeatureKind::Extrusion)
        throw std::runtime_error("Profile fixture owner missing: "+owner);
    const auto source=std::ranges::find(doc.sketches,feature->extrusion.sketch_id,&sketcher::Sketch::id);
    if(source==doc.sketches.end())throw std::runtime_error("Profile fixture Sketch missing");
    auto local=document::PartDocument::create_default();auto value=*feature;
    value.placement={};value.combine_mode=document::CombineMode::Add;
    value.extrusion.end_condition_forward=document::EndCondition::Length;
    value.extrusion.end_condition_reverse=document::EndCondition::Length;
    value.extrusion.end_targets_forward.clear();value.extrusion.end_targets_reverse.clear();
    local.history={value};local.sketches={*source};local.resolve_constructions();
    const auto operations=local.kernel_operations();
    const kernel::ExtrusionRequest* request{};
    for(const auto& op:operations)if(op.owner_id==owner)request=std::get_if<kernel::ExtrusionRequest>(&op.primitive);
    if(!request)throw std::runtime_error("Profile fixture request missing");
    const auto role=[](const std::string& location){return location.find("z_max")!=std::string::npos?std::string("end"):std::string("start");};
    if(selector=="z_min"||selector=="z_max")return role(selector)+":from:"+
        std::to_string(request->profile_region_id.size())+":"+request->profile_region_id;
    if(!source->circles.empty()) {
        const auto id=source->circles.front().id;
        if(selector=="side")return "generated:"+id;
        if(selector.starts_with("circle:z_"))return role(selector)+":"+id;
    }
    const auto point=[&](const std::string& location)->std::string {
        const bool right=location.find("x_max")!=std::string::npos,top=location.find("y_max")!=std::string::npos;
        const sketcher::SketchPoint* chosen{};
        for(const auto& p:source->points)
            if(!chosen||(right?p.x:-p.x)+(top?p.y:-p.y)>(right?chosen->x:-chosen->x)+(top?chosen->y:-chosen->y))chosen=&p;
        if(!chosen)throw std::runtime_error("Fixture corner missing");return chosen->id;
    };
    const auto segment=[&](const std::string& a,const std::string& b)->std::string {
        for(const auto& e:source->segments)if((e.first_point_id==a&&e.second_point_id==b)||(e.first_point_id==b&&e.second_point_id==a))return e.id;
        throw std::runtime_error("Fixture edge missing");
    };
    if(selector=="x_min"||selector=="x_max")return "generated:"+segment(point(selector+":y_min"),point(selector+":y_max"));
    if(selector=="y_min"||selector=="y_max")return "generated:"+segment(point("x_min:"+selector),point("x_max:"+selector));
    if(selector.starts_with("edge:")) {
        const auto split=selector.find("--");const auto a=selector.substr(5,split-5),b=selector.substr(split+2);
        const auto pa=point(a),pb=point(b);
        return pa==pb?"generated:"+pa:role(a)+":"+segment(pa,pb);
    }
    if(selector.starts_with("vertex:"))return role(selector)+":"+point(selector);
    throw std::runtime_error("Unknown fixture topology selector: "+selector);
}

inline std::string profile_key(const document::PartDocument& doc,const document::HistoryContainer& feature,const std::string& selector) {
    auto source=doc;if(!source.find_container(feature.id))source.history.push_back(feature);
    return profile_key(source,feature.id,selector);
}
inline std::pair<std::string,std::string> family_length_binding(document::PartDocument& doc,const document::HistoryContainer& feature) {
    auto s=std::ranges::find(doc.sketches,feature.extrusion.sketch_id,&sketcher::Sketch::id);
    if(s==doc.sketches.end())throw std::runtime_error("Family fixture Sketch missing");
    const auto& edge=s->segments.front();
    auto d=s->create_point_dimension(edge.first_point_id,edge.second_point_id,sketcher::DimensionKind::DistanceX);
    const auto key="dimension:"+d.id;s->dimensions.push_back(std::move(d));return {s->id,key};
}
} // namespace zima::test
