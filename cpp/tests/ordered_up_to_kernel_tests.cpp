#include "../modules/kernel_occt/src/ordered_up_to.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/ordered_up_to_resolution.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/feature_rotation_direction.hpp>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax1.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

using namespace zima::kernel::detail;
namespace {
std::string active_case="ordered foundation";
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
unsigned exact_preview_cases=0;
void preview_matches_native(const zima::kernel::HistoryOperation& source,const zima::kernel::BodyResult& accepted) {
    using namespace zima::kernel;
    auto operation=source;FeatureGroupRequest group;
    if(const auto* linear=std::get_if<ExtrusionRequest>(&source.primitive))group.children.push_back(feature_side_request(*linear,source.owner_id,FeatureSide::End));
    else {
        auto rotation=std::get<RevolutionRequest>(source.primitive);
        // Standalone Forward/Reverse follows its axis; Feature sides follow
        // the Sketch normal. Preserve the same physical direction in both.
        rotation.feature_direction_reversed=feature_rotation_reversed(rotation);
        group.children.push_back(feature_side_request(rotation,source.owner_id,FeatureSide::End));
    }
    operation.primitive=std::move(group);OcctKernel private_kernel;
    const auto wire=private_kernel.calculate_feature_preview(operation,{});
    check(!wire.empty(),"Exact native preview returned no wire");
    std::vector<Vec3> expected,actual;
    for(const auto& edge:accepted.mesh.edges)if(!edge.parameter_seam)expected.insert(expected.end(),edge.points.begin(),edge.points.end());
    for(const auto& edge:wire){check(edge.reference.valid()&&edge.reference.owner_id==source.owner_id&&!edge.parameter_seam,"Exact native preview has an unnamed or foreign edge");actual.insert(actual.end(),edge.points.begin(),edge.points.end());}
    const auto contained=[](const auto& points,auto p){return std::ranges::any_of(points,[&](auto q){return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z)<1e-5;});};
    for(const auto p:actual)check(contained(expected,p),"Exact native preview differs from the independently accepted solid wire");
    for(const auto p:expected)check(contained(actual,p),"Exact native preview omitted an independently accepted solid edge");
    ++exact_preview_cases;
}
struct Plane {double a,b;};
TopoDS_Face face(Plane plane,double first,double last,bool reverse=false) {
    BRepBuilderAPI_MakePolygon polygon;
    for(const auto& p:std::array<gp_Pnt,4>{{{first,-2,plane.a*first+plane.b},
            {last,-2,plane.a*last+plane.b},{last,5,plane.a*last+plane.b},
            {first,5,plane.a*first+plane.b}}})polygon.Add(p);
    polygon.Close();auto result=BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
    if(reverse)result.Reverse();return result;
}
std::string bytes(const TopoDS_Shape& shape) {std::ostringstream out;BRepTools::Write(shape,out);return out.str();}
double volume(const TopoDS_Shape& shape) {GProp_GProps props;BRepGProp::VolumeProperties(shape,props);return props.Mass();}
zima::kernel::ExtrusionRequest native_extrusion(const std::array<Plane,3>& planes,
        bool positive,double correction,bool reverse) {
    using namespace zima::kernel;
    ExtrusionRequest request;request.profile_region_id="authored-region";request.outer_boundary_id="authored-boundary";
    request.outer_profile=ExtrusionRequest::PolygonProfile{{{-2,0,0},{10,0,0},{10,3,0},{-2,3,0}}};
    request.outer_edge_source_ids={"bottom","right","top","left"};request.outer_vertex_source_ids={"p","q","r","s"};
    request.direction={0,0,reverse?-50.:50.};request.ordered_end_limit=OrderedUpToLimit{};
    request.ordered_end_limit->path_correction=correction;
    for(std::size_t row=0;row<3;++row) {
        const auto plane=planes[row];const FaceReference reference{"datum","plane-"+std::to_string(row),{}};
        OrderedUpToPatch patch;patch.source_reference=reference;
        patch.target={true,reference,true,{0,0,plane.b},{-plane.a,0,1},{}};
        if(reverse){patch.target.origin.z=-patch.target.origin.z;patch.target.normal.z=-1;}
        if(row)patch.keep_previous_positive=positive;
        if(row+1<3)patch.keep_next_positive=positive;
        request.ordered_end_limit->patches.push_back(patch);
    }
    return request;
}
double roof_integral(const std::array<Plane,3>& planes,double lo,double hi) {
    const std::array<double,4> bounds{{lo,2,6,hi}};double integral=0;
    for(std::size_t i=0;i<3;++i)integral+=.5*planes[i].a*(bounds[i+1]*bounds[i+1]-bounds[i]*bounds[i])+
        planes[i].b*(bounds[i+1]-bounds[i]);return integral;
}
// Independent exact piecewise-polynomial integration in horizontal slices.
// All changes of active linear bounds become interval endpoints; two interior
// Gauss points integrate each remaining quadratic without sampling the B-Rep.
double drafted_measure(const std::array<Plane,3>& planes,double correction,double degrees,bool surface) {
    const double t=std::tan(degrees*std::numbers::pi/180);
    std::vector<std::pair<double,double>> lines{{t,-2},{-t,10},{0,2},{0,6}};
    std::vector<double> cuts{0,50};
    for(const auto p:planes) {
        if(p.a!=0)lines.push_back({1/p.a,-(p.b+correction)/p.a});
        else cuts.push_back(p.b+correction);
    }
    for(const auto& a:lines)for(const auto& b:lines)if(std::abs(a.first-b.first)>1e-12) {
        const double z=(b.second-a.second)/(a.first-b.first);if(z>0&&z<50)cuts.push_back(z);
    }
    std::ranges::sort(cuts);cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),cuts.end());
    const auto section=[&](double z) {
        const double xlo=-2+t*z,xhi=10-t*z,y=3-2*t*z;
        if(y<=0||xhi<=xlo)return 0.;double width=0;
        for(std::size_t row=0;row<3;++row) {
            double lo=std::max(xlo,row==0?-1e6:row==1?2.:6.);
            double hi=std::min(xhi,row==0?2.:row==1?6.:1e6);
            const auto plane=planes[row];
            if(plane.a>0)lo=std::max(lo,(z-plane.b-correction)/plane.a);
            else if(plane.a<0)hi=std::min(hi,(z-plane.b-correction)/plane.a);
            else if(z>plane.b+correction)continue;
            width+=std::max(0.,hi-lo);
        }
        if(!surface)return y*width;
        const auto inside=[&](double x){const auto p=planes[x<2?0:x<6?1:2];return z<=p.a*x+p.b+correction?1.:0.;};
        return (2*width+y*(inside(xlo)+inside(xhi)))*std::sqrt(1+t*t);
    };
    double result=0;
    for(std::size_t i=1;i<cuts.size();++i) {
        const double mid=(cuts[i]+cuts[i-1])*.5,half=(cuts[i]-cuts[i-1])*.5;
        result+=half*(section(mid-half/std::sqrt(3.))+section(mid+half/std::sqrt(3.)));
    }
    return result;
}
double clip_shape(const TopoDS_Shell& limit,const TopoDS_Shape& prism,const gp_Pnt& keep) {
    BRepPrimAPI_MakeHalfSpace half(limit,keep);
    BRepAlgoAPI_Common common(prism,half.Solid());common.Build();
    check(common.IsDone()&&!common.Shape().IsNull()&&BRepCheck_Analyzer(common.Shape()).IsValid(),
        "Ordered support clipping produced an invalid result");return volume(common.Shape());
}
double clipped(const OrderedLimitResult& source,double correction,bool reverse_direction=false,
        double rotation_degrees=0) {
    gp_Trsf correction_trsf;correction_trsf.SetTranslation({0,0,correction});
    auto limit=transform_ordered_up_to(source,correction_trsf);
    auto prism=BRepPrimAPI_MakeBox(gp_Pnt(-2,0,0),12,3,30).Shape();gp_Pnt keep(4,1,0);
    if(reverse_direction) {
        gp_Trsf mirror;mirror.SetMirror(gp_Ax2({0,0,0},{0,0,1}));
        limit=transform_ordered_up_to(limit,mirror);
        prism=BRepBuilderAPI_Transform(prism,mirror,true).Shape();keep.Transform(mirror);
    }
    if(rotation_degrees!=0) {
        gp_Trsf rotate;rotate.SetRotation(gp_Ax1({0,0,0},{1,2,3}),rotation_degrees*std::numbers::pi/180);
        limit=transform_ordered_up_to(limit,rotate);
        prism=BRepBuilderAPI_Transform(prism,rotate,true).Shape();keep.Transform(rotate);
    }
    TopTools_IndexedMapOfShape actual;TopExp::MapShapes(limit.shell,TopAbs_FACE,actual);
    for(std::size_t row=0;row<source.patches.size();++row) {
        check(actual.Contains(limit.patches[row].face),"A transformed semantic patch is not an actual shell member");
        check(source.patches[row].keep_previous_positive==limit.patches[row].keep_previous_positive&&
            source.patches[row].keep_next_positive==limit.patches[row].keep_next_positive,
            "Correction or reflection changed a retained source-side choice");
    }
    return clip_shape(limit.shell,prism,keep);
}
}
int main(){try {
    unsigned cases=0;Bnd_Box envelope;envelope.Update(-40,-40,-40,40,40,60);
    for(unsigned flipped=0;flipped<4;++flipped)for(bool reverse_order:{false,true})for(bool separated:{false,true})
    for(bool opposite_hemisphere:{false,true})for(double correction:{-1.,0.,1.}) {
        active_case="authored tangent plane-cylinder";
        auto plane=face({0,15},-5,0,flipped&1);
        if(separated) {
            BRepBuilderAPI_MakePolygon wire;for(const auto& p:std::array<gp_Pnt,4>{{{-5,-3,15},{0,-3,15},{0,-2,15},{-5,-2,15}}})wire.Add(p);
            wire.Close();plane=BRepBuilderAPI_MakeFace(wire.Wire()).Face();if(flipped&1)plane.Reverse();
        }
        Handle(Geom_CylindricalSurface) cylinder=new Geom_CylindricalSurface(gp_Ax3({0,0,10},{0,1,0},{1,0,0}),5);
        auto curved=BRepBuilderAPI_MakeFace(cylinder,-std::numbers::pi/2,opposite_hemisphere?std::numbers::pi/2:0,separated?4:-2,5,1e-7).Face();
        if(flipped&2)curved.Reverse();
        std::vector<OrderedLimitInput> inputs{{plane},{curved}};if(reverse_order)std::reverse(inputs.begin(),inputs.end());
        const auto first=bytes(inputs[0].authored_face),second=bytes(inputs[1].authored_face);
        const auto built=build_ordered_up_to(inputs,envelope,1e-7);
        gp_Trsf advance;advance.SetTranslation({0,0,correction});const auto moved=transform_ordered_up_to(built,advance);
        const auto prism=BRepPrimAPI_MakeBox(gp_Pnt(-2,0,0),4,3,30).Shape();
        const double expected=3*(50+std::sqrt(21.)+12.5*std::asin(.4))+12*correction;
        check(std::abs(clip_shape(moved.shell,prism,{-1,1,0})-expected)<1e-5,"Tangent limiting patches disagree with the independent circular integral");
        check(bytes(inputs[0].authored_face)==first&&bytes(inputs[1].authored_face)==second,"Tangent sewing modified its source");
        for(const auto& patch:built.patches)check(!patch.keep_previous_positive&&!patch.keep_next_positive,"Tangent seam invented a transverse retained-side choice");
        ++cases;
    }
    {
        // A surface can fit a plane within modeling tolerance while remaining
        // genuinely curved. Pole certification must preserve that small sag.
        TColgp_Array2OfPnt poles(1,3,1,3);
        for(int u=1;u<=3;++u)for(int v=1;v<=3;++v)poles.SetValue(u,v,
            gp_Pnt(u-1,v-1,10.+(u==2&&v==2?1e-9:0.)));
        const Handle(Geom_BezierSurface) curved=new Geom_BezierSurface(poles);
        const auto source=BRepBuilderAPI_MakeFace(curved,1e-7).Face();
        const auto before=bytes(source);
        const auto built=build_ordered_up_to({{source,{},{}}},envelope,1e-7);
        const auto result=BRep_Tool::Surface(built.patches.front().face);
        check(!Handle(Geom_BezierSurface)::DownCast(result).IsNull()&&result->Value(.5,.5).Z()>10.+1e-10,
            "A genuinely curved source was flattened at modeling tolerance");
        check(bytes(source)==before,"Curvature certification changed the source surface");++cases;
    }
    const std::array<std::array<Plane,3>,2> shapes{{{{{1,10},{0,12},{-.5,15}}},{{{-1,12},{0,10},{1,4}}}}};
    for(unsigned kind=0;kind<shapes.size();++kind)for(unsigned flipped=0;flipped<8;++flipped)
    for(bool reverse_order:{false,true}) {
        const auto& planes=shapes[kind];
        std::vector<OrderedLimitInput> inputs{{face(planes[0],-2,1.7,flipped&1)},
            {face(planes[1],2.3,5.7,flipped&2)},{face(planes[2],6.3,10,flipped&4)}};
        if(reverse_order)std::reverse(inputs.begin(),inputs.end());
        std::vector<std::string> before;for(const auto& input:inputs)before.push_back(bytes(input.authored_face));
        const auto result=build_ordered_up_to(inputs,envelope,1e-7);
        check(result.patches.size()==3,"Ordered chain lost a semantic row");
        for(std::size_t row=0;row<inputs.size();++row) {
            check(bytes(inputs[row].authored_face)==before[row],"Ordered patch builder modified an authored source");
            check(row==0||result.patches[row].keep_previous_positive.has_value(),"Previous patch choice was not resolved");
            check(row+1==inputs.size()||result.patches[row].keep_next_positive.has_value(),"Next patch choice was not resolved");
            inputs[row].keep_previous_positive=result.patches[row].keep_previous_positive;
            inputs[row].keep_next_positive=result.patches[row].keep_next_positive;
        }
        for(double correction:{-1.5,-0.,0.,1.5})for(bool reversed:{false,true})for(double rotation:{0.,37.}) {
            const auto actual=clipped(result,correction,reversed,rotation);
            if(std::abs(actual-((kind==0?396.:408.)+36*correction))>1e-6)
                throw std::runtime_error("Automatically delimited chain disagrees with the independent analytic integral");
            ++cases;
        }
        const auto replay=build_ordered_up_to(inputs,envelope,1e-7);
        check(std::abs(clipped(replay,0)-(kind==0?396.:408.))<1e-6,
            "Explicit retained sides changed the automatic authored result");++cases;
    }
    const auto rejected=[&](std::vector<OrderedLimitInput> inputs,OrderedLimitFailure expected) {
        std::vector<std::string> before;for(const auto& input:inputs)before.push_back(bytes(input.authored_face));
        bool caught=false;try{static_cast<void>(build_ordered_up_to(inputs,envelope,1e-7));}
        catch(const OrderedLimitError& error){
            if(error.failure!=expected)throw std::runtime_error("Invalid chain diagnostic "+
                std::to_string(static_cast<int>(error.failure))+" instead of "+std::to_string(static_cast<int>(expected))+
                " at rows "+std::to_string(error.row)+","+std::to_string(error.other_row));
            caught=true;
        }
        check(caught,"Invalid ordered chain was accepted");
        for(std::size_t row=0;row<inputs.size();++row)check(bytes(inputs[row].authored_face)==before[row],
            "Rejected chain modified its source");++cases;
    };
    rejected({{face({1,10},-2,1.7)},{face({1,12},2.3,10)}},OrderedLimitFailure::AmbiguousAdjacency);
    rejected({{face({1,10},-2,1.7)},{face({1,10},2.3,10)}},OrderedLimitFailure::AmbiguousAdjacency);
    rejected({{face({1,10},-2,5)},{face({0,12},2.3,10)}},OrderedLimitFailure::AmbiguousRetainedPatch);
    rejected({{face({1,10},-2,1.7),{},true},{face({0,12},2.3,5.7),true,true},
        {face({-.5,15},6.3,10),true,{}}},OrderedLimitFailure::EmptyPatch);
    rejected({{face({1,10},-2,1.7)},{face({-.5,15},6.3,10)},
        {face({0,12},2.3,5.7)}},OrderedLimitFailure::DisconnectedChain);
    // Once accepted, each retained choice is an authored intent. Moving a
    // source may make its finite domain straddle a seam; replay must retain the
    // accepted choice rather than seed it again or silently flip its side.
    {
        std::vector<OrderedLimitInput> inputs{{face({1,10},-2,1.7)},
            {face({0,12},2.3,5.7)},{face({-.5,15},6.3,10)}};
        const auto initial=build_ordered_up_to(inputs,envelope,1e-7);
        for(std::size_t row=0;row<inputs.size();++row) {
            inputs[row].keep_previous_positive=initial.patches[row].keep_previous_positive;
            inputs[row].keep_next_positive=initial.patches[row].keep_next_positive;
        }
        for(const auto& [height,expected]:std::array<std::pair<double,double>,4>{{{13,403.5},{11,379.5},{12,396},{13,403.5}}}) {
            inputs[1].authored_face=face({0,height},2.3,5.7);
            const auto changed=build_ordered_up_to(inputs,envelope,1e-7);
            check(std::abs(clipped(changed,0)-expected)<1e-6,
                "Source edit changed a retained side or disagrees with the piecewise integral");++cases;
        }
    }
    // A curved cap and a planar skirt have one connected circular seam. The
    // exact independent measure is the plane prism plus a spherical cap.
    for(double radius:{5.,.1,.01})for(bool swap:{false,true})for(unsigned flip=0;flip<4;++flip) {
        active_case="sphere radius="+std::to_string(radius)+" flip="+std::to_string(flip)+" swap="+std::to_string(swap);
        occ::handle<Geom_SphericalSurface> sphere=new Geom_SphericalSurface(gp_Ax3({0,0,10},{0,0,1}),radius);
        auto cap=BRepBuilderAPI_MakeFace(sphere,0,2*std::numbers::pi,
            std::asin(.4)+.05,std::numbers::pi/2,1e-7).Face();
        auto skirt=face({0,10+.4*radius},1.04*radius,1.2*radius);
        if(flip&1)cap.Reverse();if(flip&2)skirt.Reverse();
        std::vector<OrderedLimitInput> inputs{{cap},{skirt}};if(swap)std::reverse(inputs.begin(),inputs.end());
        const auto result=build_ordered_up_to(inputs,envelope,1e-7);
        const auto& skirt_choice=swap?result.patches[0].keep_next_positive:result.patches[1].keep_previous_positive;
        check(skirt_choice==!(flip&1),"Retained patch positive side does not match the adjacent oriented material normal");
        const double width=2.4*radius;
        const double expected=width*width*(10+.4*radius)+.288*std::numbers::pi*radius*radius*radius;
        const auto prism=BRepPrimAPI_MakeBox(gp_Pnt(-1.2*radius,-1.2*radius,0),width,width,10+2*radius).Shape();
        check(std::abs(clip_shape(result.shell,prism,{0,0,0})-expected)<std::max(1e-10,expected*1e-9),
            "Sphere/plane chain disagrees with the independent spherical cap integral");++cases;
    }
    // A real circular sweep reaches a radial plane at 45 degrees for r<sqrt(32)
    // and z=4 at asin(4/r) beyond that seam. Integrate r*angle(r) independently.
    const double join=std::sqrt(32.);
    const auto integral=[](double r){return .5*r*r*std::asin(4/r)+2*std::sqrt(r*r-16);};
    const double angular_volume=3*((join*join-16)*std::numbers::pi/8+integral(8)-integral(join));
    BRepBuilderAPI_MakePolygon profile;
    for(const auto& p:std::array<gp_Pnt,4>{{{4,0,0},{8,0,0},{8,3,0},{4,3,0}}})profile.Add(p);
    profile.Close();const auto profile_face=BRepBuilderAPI_MakeFace(profile.Wire()).Face();
    for(bool reversed:{false,true})for(bool swap:{false,true}) {
        std::vector<OrderedLimitInput> inputs{{face({1,0},1,3)},{face({0,4},5,9)}};
        if(swap)std::reverse(inputs.begin(),inputs.end());
        const auto joined=build_ordered_up_to(inputs,envelope,1e-7);
        const gp_Ax1 axis({0,0,0},reversed?gp_Dir(0,1,0):gp_Dir(0,-1,0));
        const auto swept=BRepPrimAPI_MakeRevol(profile_face,axis,std::numbers::pi/2,true).Shape();
        for(double correction:{-5.,-0.,0.,5.}) {
            auto limit=joined;
            if(reversed){gp_Trsf mirror;mirror.SetMirror(gp_Ax2({0,0,0},{0,0,1}));
                limit=transform_ordered_up_to(limit,mirror);}
            gp_Trsf advance;advance.SetRotation(axis,correction*std::numbers::pi/180);
            limit=transform_ordered_up_to(limit,advance);
            const double expected=angular_volume+3*(64-16)*.5*correction*std::numbers::pi/180;
            check(std::abs(clip_shape(limit.shell,swept,{5,1,0})-expected)<1e-6,
                "Circular-sweep ordered limit disagrees with the independent angular integral");++cases;
        }
    }
    for(double angle:{-90.,37.,180.,270.}) {
        gp_Trsf rotate;rotate.SetRotation(gp_Ax1({0,0,0},{1,2,3}),angle*std::numbers::pi/180);
        gp_Trsf translate;translate.SetTranslation({14,-8,7});const auto frame=translate*rotate;
        std::vector<OrderedLimitInput> inputs{{face({1,10},-2,1.7)},
            {face({0,12},2.3,5.7)},{face({-.5,15},6.3,10)}};
        for(auto& input:inputs)input.authored_face=TopoDS::Face(BRepBuilderAPI_Transform(input.authored_face,frame,true).Shape());
        Bnd_Box placed_envelope;placed_envelope.Update(-200,-200,-200,200,200,200);
        const auto joined=build_ordered_up_to(inputs,placed_envelope,1e-7);
        const auto prism=BRepBuilderAPI_Transform(BRepPrimAPI_MakeBox(gp_Pnt(-2,0,0),12,3,30).Shape(),frame,true).Shape();
        gp_Pnt keep(4,1,0);keep.Transform(frame);
        check(std::abs(clip_shape(joined.shell,prism,keep)-396)<1e-6,
            "Placed source patches were resolved in the wrong coordinate frame");++cases;
    }
    {
        occ::handle<Geom_CylindricalSurface> cylinder=new Geom_CylindricalSurface(gp_Ax3({0,0,10},{0,1,0}),5);
        const auto curved=BRepBuilderAPI_MakeFace(cylinder,0,2*std::numbers::pi,-2,5,1e-7).Face();
        rejected({{curved},{face({0,12},5.2,6)}},OrderedLimitFailure::AmbiguousAdjacency);
        occ::handle<Geom_SphericalSurface> sphere=new Geom_SphericalSurface(gp_Ax3({0,0,10},{0,0,1}),5);
        const auto curved_sphere=BRepBuilderAPI_MakeFace(sphere,1e-7).Face();
        rejected({{curved_sphere},{face({0,15},5.2,6)}},OrderedLimitFailure::AmbiguousAdjacency);
        rejected({{curved_sphere},{face({0,16},5.2,6)}},OrderedLimitFailure::AmbiguousAdjacency);
    }
    // Exercise the real native history request, not only the shell primitive.
    for(unsigned kind=0;kind<shapes.size();++kind)for(double correction:{-1.5,-0.,0.,1.5})
    for(bool reverse:{false,true}) {
        using namespace zima::kernel;
        auto request=native_extrusion(shapes[kind],kind!=0,correction,reverse);
        HistoryOperation operation;operation.owner_id="limited-feature";operation.primitive=request;
        operation.boolean_tolerance=1e-7;OcctKernel kernel;
        auto calculated=kernel.evaluate_history({operation});
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Ordered native Extrusion request failed");
        preview_matches_native(operation,calculated.back());
        kernel.ensure_body_properties(calculated.back());
        if(std::abs(calculated.back().volume-((kind==0?396.:408.)+36*correction))>1e-6)
            throw std::runtime_error("Native ordered Extrusion request disagrees with the independent volume");
        const auto& originals=calculated.back().mesh.original_references.triangle_references;
        for(std::size_t row=0;row<3;++row) {
            const std::string parent="plane-"+std::to_string(row);
            check(std::ranges::any_of(originals,[&](const auto& ref){return ref.owner_id==operation.owner_id&&
                (row==0?ref.semantic_key.starts_with("end:")&&ref.semantic_key.find(":limit:")==std::string::npos:
                ref.semantic_key.find(":limit:")!=std::string::npos&&ref.semantic_key.ends_with(":"+parent+":0:"));}),
                "Native limited cap lost a target's authored ancestry");
        }
        auto altered=operation;std::get<ExtrusionRequest>(altered.primitive).ordered_end_limit->path_correction+=1;
        check(history_fingerprint({operation},1)!=history_fingerprint({altered},1),"Ordered correction was omitted from the calculation cache key");
        altered=operation;std::get<ExtrusionRequest>(altered.primitive).ordered_end_limit->patches[0].keep_next_positive=kind==0;
        check(history_fingerprint({operation},1)!=history_fingerprint({altered},1),"Retained patch side was omitted from the cache key");
        altered=operation;std::get<ExtrusionRequest>(altered.primitive).ordered_end_limit->patches[0].source_reference.instance_path="another-copy";
        check(history_fingerprint({operation},1)!=history_fingerprint({altered},1),"Authored source occurrence was omitted from the cache key");
        ++cases;
    }
    for(bool reverse:{false,true})for(bool mirror:{false,true})for(double correction:{-29.9,-5.,-0.,0.,5.,150.,250.,314.9}) {
        if(mirror&&!reverse)continue;
        active_case="native Revolution reverse="+std::to_string(reverse)+" mirror="+std::to_string(mirror)+" correction="+std::to_string(correction);
        using namespace zima::kernel;
        RevolutionRequest request;request.profile_region_id="authored-angular-region";request.outer_boundary_id="angular-boundary";
        request.outer_profile=ExtrusionRequest::PolygonProfile{{{4,0,0},{8,0,0},{8,3,0},{4,3,0}}};
        request.outer_edge_source_ids={"bottom","right","top","left"};request.outer_vertex_source_ids={"p","q","r","s"};
        request.profile_normal={0,0,1};request.axis_direction={0,reverse?1.:-1.,0};
        request.angle_degrees=90;request.ordered_end_limit=OrderedUpToLimit{};request.mirror_end_limit=mirror;
        request.ordered_end_limit->path_correction=correction;
        for(unsigned row=0;row<2;++row) {
            const FaceReference reference{"datum","angular-plane-"+std::to_string(row),{}};
            OrderedUpToPatch patch;patch.source_reference=reference;
            patch.target={true,reference,true,{0,0,row?4.:0.},{row?0.:-1.,0,1},{}};
            if(reverse&&!mirror){patch.target.origin.z=-patch.target.origin.z;patch.target.normal.z=-1;}
            if(row)patch.keep_previous_positive=false;else patch.keep_next_positive=false;
            request.ordered_end_limit->patches.push_back(patch);
        }
        HistoryOperation operation;operation.owner_id="limited-angular-feature";operation.primitive=request;operation.boolean_tolerance=1e-7;
        OcctKernel kernel;auto calculated=kernel.evaluate_history({operation});
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Ordered native Revolution request failed");
        preview_matches_native(operation,calculated.back());
        kernel.ensure_body_properties(calculated.back());
        const double expected=angular_volume+3*(64-16)*.5*correction*std::numbers::pi/180;
        check(std::abs(calculated.back().volume-expected)<1e-6,
            "Native ordered Revolution disagrees with the independent angular integral");
        std::set<std::string> end_sources;
        for(const auto& reference:calculated.back().mesh.original_references.triangle_references)
            if(reference.owner_id==operation.owner_id&&reference.semantic_key.starts_with("end:from:"))
                end_sources.insert(reference.semantic_key);
        check(end_sources.size()==2,"Ordered Revolution lost an original target's cap identity");
        if(correction==150.&&!reverse&&!mirror) {
            auto placed=operation;auto& rotated=std::get<RevolutionRequest>(placed.primitive);
            rotated.start_angle_degrees=37.;
            gp_Trsf initial;initial.SetRotation(gp_Ax1({0,0,0},{0,-1,0}),37*std::numbers::pi/180);
            for(auto& patch:rotated.ordered_end_limit->patches) {
                auto& target=patch.target;gp_Pnt point(target.origin.x,target.origin.y,target.origin.z);point.Transform(initial);
                gp_Vec normal(target.normal.x,target.normal.y,target.normal.z);normal.Transform(initial);
                target.origin={point.X(),point.Y(),point.Z()};target.normal={normal.X(),normal.Y(),normal.Z()};
            }
            gp_Trsf placement;placement.SetRotation(gp_Ax1({0,0,0},{0,0,1}),23*std::numbers::pi/180);
            const auto point=[&](Vec3 p){gp_Pnt q(p.x,p.y,p.z);q.Transform(placement);return Vec3{q.X(),q.Y(),q.Z()};};
            const auto direction=[&](Vec3 p){gp_Vec q(p.x,p.y,p.z);q.Transform(placement);return Vec3{q.X(),q.Y(),q.Z()};};
            for(auto& p:std::get<ExtrusionRequest::PolygonProfile>(rotated.outer_profile).vertices)p=point(p);
            rotated.axis_point=point(rotated.axis_point);rotated.axis_direction=direction(rotated.axis_direction);
            rotated.profile_normal=direction(rotated.profile_normal);
            for(auto& patch:rotated.ordered_end_limit->patches) {
                patch.target.origin=point(patch.target.origin);patch.target.normal=direction(patch.target.normal);
            }
            auto result=kernel.evaluate_history({placed});
            check(!result.empty()&&result.back().calculation_errors.empty(),"Placed long Revolution with an authored start failed");
            kernel.ensure_body_properties(result.back());
            check(std::abs(result.back().volume-expected)<1e-6,"Terminal window changed under placement or a nonzero start angle");++cases;
        }
        auto altered=operation;std::get<RevolutionRequest>(altered.primitive).mirror_end_limit=!request.mirror_end_limit;
        check(history_fingerprint({operation},1)!=history_fingerprint({altered},1),"Ordered Revolution reflection was omitted from its cache key");++cases;
        if(correction>100.) {
            auto surface=operation;std::get<RevolutionRequest>(surface.primitive).surface_result=true;
            auto skin=kernel.evaluate_history({surface});
            check(!skin.empty()&&skin.back().calculation_errors.empty(),"Long ordered Surface Revolution failed");
            kernel.ensure_body_properties(skin.back());
            const double radians=correction*std::numbers::pi/180;
            const double area=expected*2/3+3*(4*(std::numbers::pi/4+radians)+8*(std::numbers::pi/6+radians));
            check(std::abs(skin.back().surface_area-area)<1e-6,"Long Surface Revolution disagrees with the independent wall integral");++cases;
            auto hollow=operation;auto& tube=std::get<RevolutionRequest>(hollow.primitive);
            tube.inner_profiles={ExtrusionRequest::PolygonProfile{{{4.2,.2,0},{7.8,.2,0},{7.8,2.8,0},{4.2,2.8,0}}}};
            tube.inner_boundary_ids={"inner-angular-boundary"};
            tube.inner_edge_source_ids={{"ib","ir","it","il"}};tube.inner_vertex_source_ids={{"ip","iq","ir","is"}};
            auto walls=kernel.evaluate_history({hollow});
            check(!walls.empty()&&walls.back().calculation_errors.empty(),"Long hollow ordered Revolution failed");
            kernel.ensure_body_properties(walls.back());
            const double hole=2.6*((join*join-4.2*4.2)*std::numbers::pi/8+integral(7.8)-integral(join)+
                (7.8*7.8-4.2*4.2)*.5*radians);
            check(std::abs(walls.back().volume-(expected-hole))<1e-6,"Long hollow Revolution disagrees with the independent angular integral");++cases;
        }
        if(correction==5.)for(double invalid_correction:{-50.,-35.,320.,340.}) {
            auto invalid=operation;
            std::get<RevolutionRequest>(invalid.primitive).ordered_end_limit->path_correction=invalid_correction;
            bool rejected=false;
            try {const auto result=kernel.evaluate_history({invalid});
                rejected=!result.empty()&&!result.back().calculation_errors.empty();}
            catch(const std::runtime_error&) {rejected=true;}
            check(rejected,
                "Ordered Revolution correction wrapped an invalid angular span into a different turn");++cases;
        }
    }
    for(bool opposite:{false,true}) {
        const auto built=build_ordered_up_to({{face({1,10},-2,1.7),{},opposite},
            {face({0,12},2.3,10),opposite,{}}},envelope,1e-7);
        check(std::abs(clipped(built,0)-(opposite?528.:408.))<1e-6,
            "Explicit opposite retained patches did not produce the independent alternate roof");++cases;
    }
    for(unsigned kind=0;kind<shapes.size();++kind)for(bool reverse:{false,true})for(double correction:{-1.5,1.5})
    for(bool surface:{false,true}) {
        active_case="ordered "+std::string(surface?"Surface":"Thin")+" kind="+std::to_string(kind)+
            " reverse="+std::to_string(reverse)+" correction="+std::to_string(correction);
        using namespace zima::kernel;auto request=native_extrusion(shapes[kind],kind!=0,correction,reverse);
        request.surface_result=surface;if(!surface)request.wall=ProfileWall{-.2,.2,{}};
        HistoryOperation operation;operation.owner_id="limited-wall-feature";operation.primitive=request;operation.boolean_tolerance=1e-7;
        OcctKernel kernel;auto calculated=kernel.evaluate_history({operation});kernel.ensure_body_properties(calculated.back());
        const double expected=surface?(kind==0?318.:356.)+30*correction:
            3.4*roof_integral(shapes[kind],-2.2,10.2)-2.6*roof_integral(shapes[kind],-1.8,9.8)+12*correction;
        check(calculated.back().calculation_errors.empty()&&std::abs((surface?calculated.back().surface_area:calculated.back().volume)-expected)<1e-6,
            "Native ordered Thin/Surface result disagrees with its independent integral");++cases;
    }
    for(unsigned kind=0;kind<shapes.size();++kind)for(double angle:{-1.,1.})for(bool surface:{false,true}) {
        active_case="ordered Draft kind="+std::to_string(kind)+" angle="+std::to_string(angle)+" surface="+std::to_string(surface);
        using namespace zima::kernel;auto request=native_extrusion(shapes[kind],kind!=0,0,false);
        request.draft_angle_degrees=angle;request.surface_result=surface;
        HistoryOperation operation;operation.owner_id="limited-draft-feature";operation.primitive=request;operation.boolean_tolerance=1e-7;
        OcctKernel kernel;auto calculated=kernel.evaluate_history({operation});kernel.ensure_body_properties(calculated.back());
        const auto actual=surface?calculated.back().surface_area:calculated.back().volume;
        const auto expected=drafted_measure(shapes[kind],0,angle,surface);
        if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Drafted native ordered result "+std::to_string(actual)+
            " disagrees with the independent horizontal-slice integral "+std::to_string(expected));++cases;
    }
    // Integrate circular cross-sections independently: integral_disk |x| dA
    // is 4*r^3/3; the corresponding cylindrical wall integral is 4*r^2.
    for(bool concave:{false,true})for(bool reverse:{false,true})for(unsigned mode=0;mode<4;++mode)
    for(double correction:{-1.5,-0.,0.,1.5}) {
        active_case="circular ordered profile mode="+std::to_string(mode);
        using namespace zima::kernel;
        auto request=native_extrusion(shapes[0],concave,correction,reverse);
        request.outer_profile=ExtrusionRequest::CircleProfile{{0,0,0},2};
        request.outer_edge_source_ids={"circle"};request.outer_vertex_source_ids.clear();
        request.surface_result=mode==3;
        if(mode==1) {
            request.inner_profiles.push_back(ExtrusionRequest::CircleProfile{{0,0,0},1});
            request.inner_boundary_ids={"hole"};request.inner_edge_source_ids={{"hole-circle"}};request.inner_vertex_source_ids={{}};
        }
        if(mode==2)request.wall=ProfileWall{-.2,.2,{}};
        request.ordered_end_limit->patches.clear();
        for(unsigned row=0;row<2;++row) {
            const double a=(concave?-1.:1.)*(row?-1.:1.);
            const FaceReference reference{"circle-datum","plane-"+std::to_string(row),{}};
            OrderedUpToPatch patch;patch.source_reference=reference;
            patch.target={true,reference,true,{0,0,reverse?-12.:12.},{-a,0,reverse?-1.:1.},{}};
            if(row)patch.keep_previous_positive=concave;else patch.keep_next_positive=concave;
            request.ordered_end_limit->patches.push_back(patch);
        }
        HistoryOperation operation;operation.owner_id="circular-ordered-feature";operation.primitive=request;
        OcctKernel engine;auto result=engine.evaluate_history({operation});engine.ensure_body_properties(result.back());
        const double sign=concave?1.:-1.,outer=mode==2?2.2:2.,inner=mode==1?1.:mode==2?1.8:0.;
        const double expected=mode==3?4*std::numbers::pi*(12+correction)+sign*16:
            std::numbers::pi*(outer*outer-inner*inner)*(12+correction)+sign*4./3.*(outer*outer*outer-inner*inner*inner);
        check(std::abs((mode==3?result.back().surface_area:result.back().volume)-expected)<1e-5,
            "Circular Solid/hole/Thin/Surface limit disagrees with its independent integral");++cases;
    }
    for(unsigned kind=0;kind<shapes.size();++kind) {
        active_case="actual original face targets kind="+std::to_string(kind);
        using namespace zima::kernel;std::vector<HistoryOperation> operations;
        const std::array<double,3> first{{-2,2.3,6.3}},last{{1.7,5.7,10}};
        for(std::size_t row=0;row<3;++row) {
            const auto plane=shapes[kind][row];ExtrusionRequest source;
            source.profile_region_id="source-region-"+std::to_string(row);source.outer_boundary_id="source-boundary";
            source.outer_edge_source_ids={"a","b","c","d"};source.outer_vertex_source_ids={"p","q","r","s"};
            const auto point=[&](double x,double y){return Vec3{x,y,plane.a*x+plane.b};};
            source.outer_profile=ExtrusionRequest::PolygonProfile{{point(first[row],-2),point(last[row],-2),point(last[row],5),point(first[row],5)}};
            source.direction={0,0,-1};HistoryOperation op;op.owner_id="native-target-"+std::to_string(row);op.primitive=source;
            operations.push_back(op);
        }
        OcctKernel kernel;const auto prefix=kernel.evaluate_history(operations);
        auto request=native_extrusion(shapes[kind],kind!=0,0,false);
        for(std::size_t row=0;row<3;++row) {
            const auto& refs=prefix.back().mesh.original_references.triangle_references;
            const auto found=std::ranges::find_if(refs,[&](const auto& ref){return ref.owner_id==operations[row].owner_id&&ref.semantic_key.starts_with("start:");});
            check(found!=refs.end(),"Authored native target has no original cap identity");
            auto& patch=request.ordered_end_limit->patches[row];patch.source_reference=*found;patch.target.reference=*found;
            patch.target.datum=false;patch.keep_previous_positive.reset();patch.keep_next_positive.reset();
        }
        HistoryOperation limited;limited.owner_id="limited-from-original-faces";limited.primitive=request;limited.replaces_body=true;
        operations.push_back(limited);
        auto result=kernel.evaluate_history_incremental(operations,prefix);kernel.ensure_body_properties(result.back());
        check(std::abs(result.back().volume-(kind==0?396.:408.))<1e-6,
            "Exact original-face resolution disagrees with the independent integral");++cases;
        check(result.back().resolved_up_to_limits.size()==1,"Automatic native retained choices were not returned to their owner");
        auto accepted=operations;
        check(resolve_ordered_up_to_operation(accepted.back(),result.back().resolved_up_to_limits),"Accepted automatic choices did not canonicalize the native request");
        check(result.back().source_fingerprint==history_fingerprint(accepted,accepted.size()),
            "Accepted retained choices would force a second geometry calculation");
        const auto unchanged=kernel.evaluate_history_incremental(accepted,result);
        check(unchanged.back().kernel_shape.shares_with(result.back().kernel_shape)&&
            unchanged.back().kernel_faces.shares_with(result.back().kernel_faces),
            "Accepted retained-side metadata rebuilt or reserialized unchanged native geometry");++cases;
        auto cold=prefix;for(auto& boundary:cold)boundary.calculation_provenance.reset();
        OcctKernel reopened;auto reopened_result=reopened.evaluate_history_incremental(operations,cold);
        reopened.ensure_body_properties(reopened_result.back());
        check(std::abs(reopened_result.back().volume-(kind==0?396.:408.))<1e-6,
            "Cold original-face resolution lost a source binding");++cases;
        // An Assembly cutter borrows native sources; it must neither replay
        // the source history nor clip against display triangles after reopen.
        auto external=request;BodySnapshot source_snapshot(prefix.back());
        for(auto& patch:external.ordered_end_limit->patches) {
            const auto native=patch.source_reference;patch.source_reference.instance_path="fixture-occurrence";
            patch.native_source=std::make_shared<const OrderedUpToSource>(OrderedUpToSource{source_snapshot,native,{}});
            patch.target.triangles.clear();
        }
        HistoryOperation cutter; cutter.owner_id="external-native-cutter";cutter.primitive=external;
        OcctKernel cold_external;auto external_result=cold_external.evaluate_history({cutter});
        cold_external.ensure_body_properties(external_result.back());
        check(std::abs(external_result.back().volume-(kind==0?396.:408.))<1e-6,
            "Cold external native targets were approximated or lost");++cases;
        auto placed=cutter;
        for(auto& patch:std::get<ExtrusionRequest>(placed.primitive).ordered_end_limit->patches) {
            auto source=*patch.native_source;source.placements.push_back({{0,0,.75},{}});
            patch.native_source=std::make_shared<const OrderedUpToSource>(std::move(source));
        }
        check(history_fingerprint({placed},1)!=history_fingerprint({cutter},1),"External source placement was omitted from the cache key");
        auto placed_result=cold_external.evaluate_history({placed});cold_external.ensure_body_properties(placed_result.back());
        check(std::abs(placed_result.back().volume-(kind==0?423.:435.))<1e-6,
            "External source placement did not move the exact target chain");++cases;
        auto rotated=placed;
        auto& rotated_request=std::get<ExtrusionRequest>(rotated.primitive);
        for(auto& patch:rotated_request.ordered_end_limit->patches) {
            auto source=*patch.native_source;source.placements.back().rotation_degrees.z=90;
            patch.native_source=std::make_shared<const OrderedUpToSource>(std::move(source));
        }
        for(auto& point:std::get<ExtrusionRequest::PolygonProfile>(rotated_request.outer_profile).vertices)
            point={-point.y,point.x,point.z};
        auto rotated_result=cold_external.evaluate_history({rotated});cold_external.ensure_body_properties(rotated_result.back());
        check(std::abs(rotated_result.back().volume-(kind==0?423.:435.))<1e-6,
            "External target rotation lost the exact source or its operation frame");++cases;
        // Replay accepted, explicit choices after a real source cap moves.
        auto& changed_source=std::get<ExtrusionRequest>(operations[1].primitive);
        auto& polygon=std::get<ExtrusionRequest::PolygonProfile>(changed_source.outer_profile);
        for(auto& point:polygon.vertices)point.z+=1;
        auto& changed=std::get<ExtrusionRequest>(operations.back().primitive);
        for(std::size_t row=0;row<3;++row) {
            if(row)changed.ordered_end_limit->patches[row].keep_previous_positive=kind!=0;
            if(row+1<3)changed.ordered_end_limit->patches[row].keep_next_positive=kind!=0;
        }
        auto edited=kernel.evaluate_history_incremental(operations,result);kernel.ensure_body_properties(edited.back());
        const double expected=kind==0?403.5:423.;
        check(std::abs(edited.back().volume-expected)<1e-6,
            "Changed native source geometry did not invalidate and rebuild the exact limiter");++cases;
    }
    std::cout<<"PASS "<<exact_preview_cases<<" exact native Feature preview comparisons: signed linear/angular corrections, both directions, mirrored limits, all accepted operand wire points and named source edges\n";
    std::cout<<"PASS "<<cases<<" ordered native patch-builder checks; automatic finite-plane extension, convex/concave chains, sphere/plane seam, explicit choice replay and opposite patches, all source orientations, reversed input order, mirrored/rotated clipping, actual native Extrusion/Revolution requests, signed corrections, cache identity and atomic rejection. Native Feature definitions, persistence and GUI acceptance remain separate.\n";
}catch(const Standard_Failure& error){std::cerr<<active_case<<": OCCT failure: "<<error.GetMessageString()<<'\n';return 1;}
catch(const zima::kernel::OrderedUpToError& error){std::cerr<<active_case<<": ordered failure="<<int(error.failure)<<" rows="<<error.row<<","<<error.other_row<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<active_case<<": "<<error.what()<<'\n';return 1;}}
