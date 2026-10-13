#include <zima/interchange/step_model.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/document/solid_state_calculation.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
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
}
int main(){try {
    using namespace zima;
    const auto folder=std::filesystem::temp_directory_path()/("zima-parallel-step-"+document::PartDocument::create_default().document_id);
    std::filesystem::create_directory(folder);
    kernel::OcctKernel kernel;
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
