#include "../app/point_station_annotation.hpp"
#include "../app/curve_point_annotations.hpp"
#include <zima/document/part_document.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <cmath>
#include <iostream>

int main(){try {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto near=[&](double a,double b){check(std::abs(a-b)<1e-7,"Route annotation or position has the wrong value");};
    kernel::ViewerReferenceGeometry geometry;
    geometry.edges.push_back({{{0,0,0},{10,0,0}},{"path","curve:segment:a:b",{}}});
    geometry.edges.push_back({{{10,0,0},{10,20,0}},{"path","curve:segment:b:c",{}}});
    geometry.points.push_back({{4,0,0},{"anchor","point",{}}});
    auto feature=document::PartDocument::create_feature_container("sketch");feature.feature.type=document::FeatureType::Point;
    document::ConstructionReference path{{},"path","placement-route:3d"};
    document::ConstructionReference anchor{{},"anchor","point",0.,true};
    feature.placement.references={path,anchor};
    for(const auto mode:{document::PlacementStationMode::Distance,document::PlacementStationMode::Fraction}) {
        feature.placement.references[1].station_mode=mode;
        for(double fraction:{0.,.25,.5,.75,1.}) {
            const double number=mode==document::PlacementStationMode::Fraction?fraction:26.*fraction;
            check(workspace::assign_placement_dimension(feature,geometry,"reference_offset:1",number),"Station parameter was not editable");
            check(document::resolve_placement(feature.placement,geometry),"Edited route station did not resolve");
            const auto& p=feature.placement;const kernel::Vec3 point{p.x,p.y,p.z};
            const auto annotation=app::point_station_annotation(feature.id,point,p.references,p.reference_valid);
            check(annotation&&annotation->label_only&&annotation->reference.owner_id==feature.id&&
                annotation->reference.semantic_key=="parameter:placement:reference_offset:1","Station annotation lost its parameter identity");
            near(annotation->value,number);near(p.x,std::min(10.,4.+26.*fraction));near(p.y,std::max(0.,26.*fraction-6.));near(p.z,0.);
            check(annotation->witness_first==point&&annotation->witness_second==point,"Station annotation measures a chord instead of labeling its point");
            check(annotation->unit_suffix==(mode==document::PlacementStationMode::Fraction?"":"mm"),"Station quantity or units changed");
            nlohmann::json saved=p;const auto reopened=saved.get<document::Placement>();
            check(app::point_station_annotation(feature.id,point,reopened.references,reopened.reference_valid)==annotation,"Station annotation changed after reopening");
            auto locked=feature;locked.placement.references[1].offset_locked=true;const auto before=locked.placement;
            check(!workspace::assign_placement_dimension(locked,geometry,"reference_offset:1",number+.1)&&locked.placement==before,"Locked station edit changed the definition");
            check(app::point_station_annotation(feature.id,point,locked.placement.references,true)->locked,"Station label lost its lock state");
        }
    }
    check(!app::point_station_annotation(feature.id,{},feature.placement.references,false),"Broken station displays an editable valid annotation");
    feature.placement.references[1].station_mode=document::PlacementStationMode::None;
    check(!app::point_station_annotation(feature.id,{},feature.placement.references,true),"Ordinary placement gained a route label");
    auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    curve.origin={7,11,13};curve.rotation={0,0,90};
    auto child=document::create_owned_point(curve.id);child.origin={10,20,30};curve.curve_points={child};
    const auto dimensions=app::curve_point_annotations(curve,{}).dimensions;
    check(dimensions.size()==3,"Curve inspection omitted owned Point coordinates");
    const auto z=std::ranges::find(dimensions,std::string("parameter:z"),[](const auto& d){return d.reference.semantic_key;});
    check(z!=dimensions.end()&&z->reference.owner_id==child.id,"Curve dimension lost its child owner");
    near(z->witness_second.x,-13);near(z->witness_second.y,21);near(z->witness_second.z,43);near(z->value,30);
    curve.curve_points.front().origin={};
    check(app::curve_point_annotations(curve,{}).dimensions.size()==3,"Zero Point coordinates cannot be edited from Curve inspection");
    std::cout<<"Point station annotations: distance/fraction, zero/end, repeated edits, independent coordinates, locks and reopen passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
