#include <zima/interchange/step_model.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/document/solid_state_calculation.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <sstream>
#include <iomanip>
#include <source_location>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,std::source_location where=std::source_location::current()){
    if(std::abs(actual-expected)>1e-8*std::max(1.,std::abs(expected))) {
        std::ostringstream detail;detail<<std::setprecision(17)<<"Rational STEP analytic mismatch at line "<<where.line()<<": "<<actual<<" vs "<<expected;
        throw std::runtime_error(detail.str());
    }
}
TopoDS_Shape long_trim_extrusion(int intervals,bool perturbed) {
    NCollection_Array1<gp_Pnt> poles(1,3);poles(1)={0,0,0};poles(2)={.5,0,.5};poles(3)={1,0,0};
    NCollection_Array1<double> ends(1,2);ends(1)=0;ends(2)=1;
    NCollection_Array1<int> end_mult(1,2);end_mult(1)=end_mult(2)=3;
    occ::handle<Geom_Curve> basis=new Geom_BSplineCurve(poles,ends,end_mult,2);
    occ::handle<Geom_Surface> surface=new Geom_SurfaceOfLinearExtrusion(basis,gp_Dir(0,1,0));
    const gp_Pnt2d corners[]={{0,0},{1,0},{1,1},{0,1},{0,0}};
    BRepBuilderAPI_MakeWire wire;
    for(int edge=0;edge<4;++edge) {
        occ::handle<Geom2d_Curve> curve=GCE2d_MakeSegment(corners[edge],corners[edge+1]).Value();
        if(edge==0||edge==2) {
            NCollection_Array1<gp_Pnt2d> points(1,intervals+1);
            NCollection_Array1<double> knots(1,intervals+1);
            NCollection_Array1<int> mult(1,intervals+1);
            for(int i=1;i<=intervals+1;++i) {
                const double t=double(i-1)/intervals;
                const double noise=perturbed&&i>1&&i<intervals+1?1e-9*std::sin(7*t):0.;
                points(i)={edge==0?t:1-t,(edge==0?0.:1.)+noise};knots(i)=t;
                mult(i)=i==1||i==intervals+1?2:1;
            }
            curve=new Geom2d_BSplineCurve(points,knots,mult,1);
        }
        auto boundary=BRepBuilderAPI_MakeEdge(curve,surface).Edge();
        BRepLib::BuildCurve3d(boundary);wire.Add(boundary);
    }
    auto face=BRepBuilderAPI_MakeFace(surface,wire.Wire(),true).Face();
    auto solid=BRepPrimAPI_MakePrism(face,gp_Vec(0,0,2)).Shape();
    require(BRepCheck_Analyzer(solid).IsValid(),"Long-trim extrusion fixture is invalid");return solid;
}
}
int main(){try {
    using namespace zima;
    const auto folder=std::filesystem::temp_directory_path()/("zima-parallel-step-"+document::PartDocument::create_default().document_id);
    std::filesystem::create_directory(folder);
    kernel::OcctKernel kernel;
    for(int intervals:{64,65})for(bool perturbed:{false,true})for(bool moved:{false,true}) {
        auto shape=long_trim_extrusion(intervals,perturbed);
        if(moved) {gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.47);transform.SetTranslationPart(gp_Vec(61,-23,17));shape=shape.Moved(TopLoc_Location(transform));}
        const auto path=folder/"long-trim.step";STEPControl_Writer writer;
        require(writer.Transfer(shape,STEPControl_AsIs)==IFSelect_RetDone&&writer.Write(path.string().c_str())==IFSelect_RetDone,"Long-trim STEP write failed");
        const auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},path);
        const auto& result=imported.calculated.back();const auto frozen=kernel::read_calculated_body_shape(result);
        int complex_faces=0;
        for(TopExp_Explorer it(frozen,TopAbs_FACE);it.More();it.Next()) {
            BRepGProp_Face surface(TopoDS::Face(it.Current()),true);
            if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()!=GeomAbs_SurfaceOfExtrusion)continue;
            for(BRepGProp_Domain domain(TopoDS::Face(it.Current()));domain.More();domain.Next())
                if(surface.Load(domain.Value())&&surface.LIntSubs()>64) {++complex_faces;break;}
        }
        require((intervals==65)==(complex_faces>0),"Long-trim STEP does not exercise the intended scheduling boundary");
        TopoDS_Compound solid;BRep_Builder builder;builder.MakeCompound(solid);
        for(TopExp_Explorer it(frozen,TopAbs_SOLID);it.More();it.Next())builder.Add(solid,it.Current());
        GProp_GProps sequential;const auto error=BRepGProp::VolumePropertiesGK(solid,sequential,1e-12,false,true);
        require(std::isfinite(error)&&error>=0,"Long-trim serial GK integration failed");
        require(result.volume==sequential.Mass(),"Long-trim scheduling changed the exact serial volume");near(result.volume,2.);
        GProp_GProps tensor;BRepGProp::VolumeProperties(solid,tensor,1e-12);
        const auto center=tensor.CentreOfMass();const auto matrix=tensor.MatrixOfInertia();
        require(result.volume_integrals.has_value()&&result.volume_integrals->centroid==kernel::Vec3{center.X(),center.Y(),center.Z()},"Long-trim scheduling changed the exact centroid");
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)require(result.volume_integrals->inertia[3*i+j]==matrix.Value(i+1,j+1),"Long-trim scheduling changed the exact inertia");
        const auto native=folder/"long-trim.prtz";imported.document.save(native,imported.calculated);
        std::vector<kernel::BodyResult> reopened;const auto restored=document::PartDocument::load(native,&reopened);
        require(restored.serialized()==imported.document.serialized()&&reopened.size()==imported.calculated.size(),"Long-trim native reopen changed its definition");
        for(std::size_t index=0;index<reopened.size();++index)require(document::serialize_body_result(reopened[index]).dump()==document::serialize_body_result(imported.calculated[index]).dump(),"Long-trim native reopen changed geometry, properties, references or sides");
        std::cout<<"long-trim intervals="<<intervals<<" perturbed="<<perturbed<<" moved="<<moved<<" complex_faces="<<complex_faces<<" exact serial properties and persistence passed\n";
    }
    for(bool torus:{false,true})for(bool moved:{false,true}) {
        TopoDS_Shape shape=torus?BRepPrimAPI_MakeTorus(20,5).Shape():BRepPrimAPI_MakeCylinder(5,20).Shape();
        shape=BRepBuilderAPI_NurbsConvert(shape,true).Shape();
        if(moved){gp_Trsf placement;placement.SetTranslation(gp_Vec(-137,23,41));shape=BRepBuilderAPI_Transform(shape,placement,true).Shape();}
        const auto path=folder/"rational.step";
        STEPControl_Writer writer;require(writer.Transfer(shape,STEPControl_AsIs)==IFSelect_RetDone,"Rational STEP transfer failed");
        require(writer.Write(path.string().c_str())==IFSelect_RetDone,"Rational STEP write failed");
        const auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},path);
        const auto& result=imported.calculated.back();
        const auto frozen=kernel::read_calculated_body_shape(result);BRep_Builder builder;
        require(!frozen.IsNull(),"Rational STEP lost its native B-Rep");
        TopoDS_Compound solid;builder.MakeCompound(solid);
        for(TopExp_Explorer it(frozen,TopAbs_SOLID);it.More();it.Next())builder.Add(solid,it.Current());
        GProp_GProps sequential;const double error=BRepGProp::VolumePropertiesGK(solid,sequential,1e-12,false,true);
        require(std::isfinite(error)&&error>=0,"Sequential reference integration failed");
        require(result.volume==sequential.Mass(),"Parallel STEP integration changed the exact ordered GK result");
        GProp_GProps unchanged_tensor;BRepGProp::VolumeProperties(solid,unchanged_tensor,1e-12);
        const double pi=std::numbers::pi,volume=torus?2*pi*pi*20*25:pi*25*20;
        near(result.volume,volume);require(result.volume_integrals.has_value(),"Rational STEP lost its full tensor");
        const auto& tensor=*result.volume_integrals;
        near(tensor.centroid.x,moved?-137:0);near(tensor.centroid.y,moved?23:0);near(tensor.centroid.z,(torus?0:10)+(moved?41:0));
        const double transverse=torus?volume*(20*20*.5+25*5./8.):volume*(3*25+400)/12;
        const double axial=torus?volume*(400+25*.75):volume*25*.5;
        const auto reference_tensor=unchanged_tensor.MatrixOfInertia();
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)
            require(tensor.inertia[3*i+j]==reference_tensor.Value(i+1,j+1),"Parallel volume changed the established full tensor");
        // The existing Gauss tensor has a measured ~1.4e-7 relative error on
        // this NURBS cylinder despite the requested 1e-12. Record its finite
        // analytic bound separately; do not claim quadrature tolerance is a
        // proof of tensor accuracy or alter calculation precision here.
        require(std::abs(tensor.inertia[0]-transverse)<=1e-6*transverse&&
            std::abs(tensor.inertia[4]-transverse)<=1e-6*transverse&&
            std::abs(tensor.inertia[8]-axial)<=1e-6*axial,"NURBS tensor exceeds its recorded analytic bound");
        for(int i:{1,2,3,5,6,7})require(std::abs(tensor.inertia[i])<=1e-8*volume,"NURBS off-diagonal inertia is not near zero");
        const auto native=folder/"rational.prtz";imported.document.save(native,imported.calculated);
        std::vector<kernel::BodyResult> reopened;const auto restored=document::PartDocument::load(native,&reopened);
        require(restored.serialized()==imported.document.serialized()&&reopened.size()==imported.calculated.size(),"Rational STEP native reopen changed its authored definition");
        for(std::size_t index=0;index<reopened.size();++index)
            require(document::serialize_body_result(reopened[index]).dump()==document::serialize_body_result(imported.calculated[index]).dump(),"Rational STEP reopen changed geometry, complete properties, reference identity or sides");
        std::cout<<"rational="<<(torus?"torus":"cylinder")<<" moved="<<moved<<" exact GK, unchanged tensor and finite analytic bounds passed\n";
    }
    // Repeated and mixed rotated sources may become separate native Bodies.
    // Compare their local packets exactly; the aggregate additionally includes
    // placement and the established ordered Body-property reduction.
    for(const bool repeated:{true,false}) {
        auto first=BRepBuilderAPI_NurbsConvert(BRepPrimAPI_MakeCylinder(5,20).Shape(),true).Shape();
        auto second=repeated?first:BRepBuilderAPI_NurbsConvert(BRepPrimAPI_MakeTorus(20,5).Shape(),true).Shape();
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.47);transform.SetTranslationPart(gp_Vec(61,-23,17));
        second=second.Moved(TopLoc_Location(transform));
        TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,first);builder.Add(compound,second);
        const auto path=folder/"compound.step";STEPControl_Writer writer;
        require(writer.Transfer(compound,STEPControl_AsIs)==IFSelect_RetDone&&writer.Write(path.string().c_str())==IFSelect_RetDone,"Compound STEP fixture write failed");
        const auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},path);
        const auto& aggregate=imported.calculated.back();
        std::vector<const kernel::BodyResult*> packets{&aggregate};
        for(const auto& [id,boundaries]:aggregate.body_boundaries)
            if(!boundaries.empty()&&boundaries.back().solid_present)packets.push_back(&boundaries.back());
        require(packets.size()>1,"Compound source has no local Body packets");
        for(std::size_t slot=0;slot<packets.size();++slot) {
        const auto& result=*packets[slot];const auto shape=kernel::read_calculated_body_shape(result);
        TopoDS_Compound solids;builder.MakeCompound(solids);for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())builder.Add(solids,it.Current());
        GProp_GProps volume,tensor,area;
        const auto error=BRepGProp::VolumePropertiesGK(solids,volume,1e-12,false,true);
        require(std::isfinite(error)&&error>=0,"Compound serial volume integration failed");
        if(slot==0)near(result.volume,volume.Mass());
        else require(result.volume==volume.Mass(),"Compound local Body changed exact ordered volume integration");
        BRepGProp::VolumeProperties(solids,tensor,1e-12);BRepGProp::SurfaceProperties(shape,area);
        require(result.volume_integrals.has_value(),"Compound STEP lost inertia");
        if(slot==0)near(result.surface_area,area.Mass());
        else require(result.surface_area==area.Mass(),"Compound local Body changed exact area");
        const auto center=tensor.CentreOfMass();const auto matrix=tensor.MatrixOfInertia();
        const auto tensor_scale=std::max({1.,std::abs(matrix.Value(1,1)),std::abs(matrix.Value(2,2)),std::abs(matrix.Value(3,3))});
        if(slot==0){near(result.volume_integrals->centroid.x,center.X());near(result.volume_integrals->centroid.y,center.Y());near(result.volume_integrals->centroid.z,center.Z());}
        else require(result.volume_integrals->centroid==kernel::Vec3{center.X(),center.Y(),center.Z()},"Compound local Body changed exact centroid");
        for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
            // Whole-shape Gauss and the existing placed-Body reduction have
            // different reference-point arithmetic for these NURBS sources.
            // This difference also exists in the preserved serial runtime;
            // use the existing recorded 1e-6 tensor bound for this independent
            // aggregate oracle. Local tensors below remain exactly equal.
            if(slot==0)require(std::abs(result.volume_integrals->inertia[3*i+j]-matrix.Value(i+1,j+1))<=1e-6*tensor_scale,"Compound aggregate exceeds the existing NURBS tensor bound");
            else require(result.volume_integrals->inertia[3*i+j]==matrix.Value(i+1,j+1),"Compound local Body changed exact serial inertia tensor");
        }
        }
        std::cout<<"compound repeated="<<repeated<<" exact local properties and independent aggregate bounds passed\n";
    }
    std::filesystem::remove_all(folder);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
