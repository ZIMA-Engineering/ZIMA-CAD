#include <zima/workspace/reference_sources.hpp>
#include <iostream>
#include <cmath>
using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-10)throw std::runtime_error("Incorrect original reference coordinate");}
void near(kernel::Vec3 actual,kernel::Vec3 expected){near(actual.x,expected.x);near(actual.y,expected.y);near(actual.z,expected.z);}
void verify() {
    kernel::ViewerReferenceGeometry source;
    kernel::ViewerEdge spline;spline.reference={"source","curve",""};spline.points={{1,0,0},{0,1,0}};
    spline.exact_spline=kernel::BSplineGeometry{2,{{1,0,0},{1,1,0},{0,1,0}},{0,0,0,1,1,1},{1,std::sqrt(.5),1}};
    spline.edge_treatment_side_references={{"source","side",""},{"source","wall",""}};
    spline.edge_treatment_side_references[0].sheet_role=kernel::SheetFaceRole::SideA;
    spline.edge_treatment_side_references[1].sheet_role=kernel::SheetFaceRole::ThicknessFace;
    for(auto& side:spline.edge_treatment_side_references)side.sheet_thickness=2;
    spline.edge_treatment_endpoint_references={{"source","first",""},{"source","last",""}};
    source.edges.push_back(spline);
    source.points.push_back({{2,3,4},{"source","point",""}});
    source.axes.push_back({{2,3,4},{0,1,0},100,{"source","axis",""}});
    source.vertices={{0,0,0},{1,0,0},{0,1,0},{100,100,100}};source.triangles={0,1,2,0,2,1,0,1,3};
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->origin={2,3,4};surface->axis={1,0,0};surface->radial={0,1,0};
    source.triangle_references={{"source","plane","",surface},{"source","plane","",surface},{"unselected","plane","",surface}};
    workspace::ReferenceFrame frame;frame.instance_prefix=assembly::InstancePath{{"exact-occurrence"}}.encoded();
    frame.point=[](auto p){return kernel::Vec3{10-p.y,20+p.x,30+p.z};};
    frame.direction=[](auto p){return kernel::Vec3{-p.y,p.x,p.z};};
    // Analytic faces have their own source frame, distinct from already placed samples.
    frame.surface_point=[&](const auto&,auto p){p.x+=5;return frame.point(p);};
    frame.surface_direction=[&](const auto&,auto p){return frame.direction(p);};
    kernel::ViewerReferenceGeometry result;
    const auto accept=[&](workspace::OriginalReferenceKind,const auto& owner,const auto&,const auto& path){return owner=="source"&&path==frame.instance_prefix;};
    workspace::append_original_reference_geometry(result,source,frame,accept);
    require(result.edges.size()==1&&result.points.size()==1&&result.axes.size()==1&&result.vertices.size()==3&&result.triangles.size()==6,
        "Reference collection included unrelated geometry or lost selected primitives");
    const auto& actual=result.edges.front();require(actual.reference.owner_id=="source"&&actual.reference.semantic_key=="curve"&&actual.reference.instance_path==frame.instance_prefix,
        "Reference transformation replaced topology identity");
    require(kernel::sheet_edge_role(actual)==kernel::SheetEdgeRole::Boundary,"Occurrence transform lost sheet roles");
    for(const auto& reference:actual.edge_treatment_side_references) {
        require(reference.instance_path==frame.instance_prefix,"Sheet adjacency points to a different occurrence");near(reference.sheet_thickness,2);
    }
    for(const auto& reference:actual.edge_treatment_endpoint_references)
        require(reference.instance_path==frame.instance_prefix,"Sheet endpoint points to a different occurrence");
    near(actual.points.front(),{10,21,30});near(actual.points.back(),{9,20,30});
    require(actual.exact_spline&&actual.exact_spline->weights==spline.exact_spline->weights&&actual.exact_spline->knots==spline.exact_spline->knots,
        "Exact rational geometry was reconstructed from samples");
    for(unsigned i=0;i<=256;++i){const auto p=kernel::bspline_value(*actual.exact_spline,i/256.);near((p.x-10)*(p.x-10)+(p.y-20)*(p.y-20),1);near(p.z,30);}
    near(result.points.front().position,{7,22,34});near(result.axes.front().point,{7,22,34});near(result.axes.front().direction,{-1,0,0});
    const auto& transformed=result.triangle_references.front();require(transformed.surface&&transformed.surface==result.triangle_references.back().surface&&transformed.surface!=surface,
        "Analytic face transformation mutated source geometry or copied every triangle's surface");
    near(transformed.surface->origin,{7,27,34});near(transformed.surface->axis,{0,1,0});near(transformed.surface->radial,{-1,0,0});
    near(surface->origin,{2,3,4});near(source.edges.front().exact_spline->poles.front(),{1,0,0});
    workspace::append_original_reference_geometry(result,source,frame,[](auto kind,const auto& owner,const auto&,const auto&){return owner=="source"&&kind==workspace::OriginalReferenceKind::Face;});
    require(result.vertices.size()==6&&result.triangles[6]==3&&result.triangles[7]==4&&result.triangles[8]==5,
        "Appending references reused vertex indices from another packet");
    source.triangles.front()=99;bool rejected=false;try{kernel::ViewerReferenceGeometry bad;workspace::append_original_reference_geometry(bad,source,frame,accept);}
    catch(const workspace::ReferenceQueryError& error){rejected=std::string(error.code)=="invalid_reference_geometry";}
    require(rejected,"Malformed persisted triangle index was accepted");
}
}
int main(){try{verify();std::cout<<"Original identity, sparse geometry, rational poles, source face frames and invalid packets passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
