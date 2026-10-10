#include <zima/interchange/step_model.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/solid_state_calculation.hpp>
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
        TopoDS_Shape frozen;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(frozen,data,builder);
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
        std::cout<<"rational="<<(torus?"torus":"cylinder")<<" moved="<<moved<<" exact GK, unchanged tensor and finite analytic bounds passed\n";
    }
    std::filesystem::remove_all(folder);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
