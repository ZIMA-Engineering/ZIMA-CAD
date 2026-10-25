#ifdef _WIN32
#include <windows.h>
// The Windows legacy near macro must not replace the native comparison helper.
#ifdef near
#undef near
#endif
#endif
#include <zima/document/file_path.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/kernel/boolean_face_ancestry.hpp>
#include <zima/kernel/shaft_thread_geometry.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <nlohmann/json.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools_ShapeSet.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Bnd_Box.hxx>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <array>
#include <functional>
#include <set>

using namespace zima;
namespace fs=std::filesystem;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){
    if(std::abs(actual-expected)>1e-6*std::max(1.,std::abs(expected)))
        throw std::runtime_error(std::string(message)+": "+std::to_string(actual)+" != "+std::to_string(expected));
}
template<class Sketch> void verify_profile_dragging(Sketch sketch) {
    const auto baseline=sketch;
    for(const bool locked:{false,true}) {
      sketch=baseline;if(locked)for(auto& dimension:sketch.dimensions)dimension.locked=true;
      const auto points=sketch.points;
      for(const auto& point:points)for(const auto delta:std::array<std::array<double,2>,3>{{{.2,.3},{-.3,.1},{.1,-.2}}}) {
        static_cast<void>(sketch.move_point(point.id,point.x+delta[0],point.y+delta[1]));
        const auto solved=sketch.solve();
        require(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Factory profile escaped constraints during repeated dragging");
        require(sketch.external_references==baseline.external_references,"Factory dragging changed a read-only external source");
        for(const auto& original:points) {
            const auto* current=sketch.find_point(original.id);require(current,"Factory dragging lost native point identity");
            // Unlocked driving dimensions are existing direct-manipulation
            // handles: they may change value while retaining their equations.
            if(locked){near(current->x,original.x,"Locked factory point X moved");near(current->y,original.y,"Locked factory point Y moved");}
        }
        for(const auto& c:sketch.constraints)if(!c.suppressed&&c.kind==sketcher::ConstraintKind::PointReference&&c.second_point_id=="sketch_origin") {
            const auto* origin=sketch.find_point(c.first_point_id);require(origin,"Factory origin contact missing");
            near(origin->x,0,"Factory origin contact X escaped");near(origin->y,0,"Factory origin contact Y escaped");
        }
      }
    }
}
void verify(const fs::path& path,const Json& expected) {
    workspace::Workspace live;
    const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
    const auto* state=live.open_part(id);require(state,"Candidate is not a Part");
    const auto& document=state->session.document();
    const auto& boundaries=state->session.calculated_boundaries();
    require(!boundaries.empty(),"Calculated native geometry missing");
    const auto& complete=boundaries.back();require(complete.calculation_errors.empty(),"Native calculation errors persisted");
    require(complete.body_outputs.size()==1,"Fastener must retain one ordinary Body");
    const auto& result=complete.body_outputs.begin()->second.get();
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream input(result.kernel_shape);
    BRepTools::Read(shape,input,builder);
    require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid native BRep");
    int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    require(solids==1,"Fastener must contain one connected solid");
    const double d=expected.at("diameter_mm"),l=expected.at("length_mm"),k=expected.at("head_height_mm");
    const double s=expected.at("across_flats_mm"),r=expected.at("underhead_radius_mm");
    Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);
    double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
    near(z0,-k,"Head height");near(z1,l,"Length from bearing plane");
    near(x0,-s/std::sqrt(3.),"Centered hexagon left extent");near(x1,s/std::sqrt(3.),"Centered hexagon right extent");
    near(y0,-s/2,"Centered hexagon lower extent");near(y1,s/2,"Centered hexagon upper extent");
    bool shaft=false,fillet=false,bearing=false,hole=false,tip=false,fitted=false,shoulder=false;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if(surface.GetType()==GeomAbs_Cylinder) {
            const double radius=surface.Cylinder().Radius();
            shaft|=std::abs(radius-d/2)<1e-6;
            if(expected.contains("fitted_diameter_mm")&&std::abs(radius-expected.at("fitted_diameter_mm").get<double>()/2)<1e-6) {
                const auto cylinder=surface.Cylinder();
                require(std::abs(cylinder.Axis().Direction().Z())>1.-1e-8&&std::hypot(cylinder.Location().X(),cylinder.Location().Y())<1e-8,
                    "Fitted shank is not concentric with the nominal thread");
                fitted=true;
            }
            bearing|=std::abs(radius-expected.at("bearing_diameter_mm").get<double>()/2)<1e-6;
            if(expected.contains("locking_hole_diameter_mm")&&
               std::abs(radius-expected.at("locking_hole_diameter_mm").get<double>()/2)<1e-6) {
                const auto cylinder=surface.Cylinder();
                const auto axis=cylinder.Axis();
                if(std::abs(axis.Direction().Y())>1.-1e-8&&
                   std::abs(axis.Location().X())<1e-8&&std::abs(axis.Location().Z()+k/2)<1e-8)hole=true;
            }
        }
        if(surface.GetType()==GeomAbs_Torus)fillet|=std::abs(surface.Torus().MinorRadius()-r)<1e-6;
        if(surface.GetType()==GeomAbs_Cone) {
            Bnd_Box face_box;BRepBndLib::AddOptimal(it.Current(),face_box,false,false);
            double a,b,c,e,f,g;face_box.Get(a,b,c,e,f,g);
            if(expected.contains("fitted_diameter_mm")&&c>0&&g<l-1e-6&&std::abs(std::abs(surface.Cone().SemiAngle())-std::acos(-1.)/6)<1e-7) {
                const double end=l-expected.at("thread_length_mm").get<double>()-expected.at("runout_mm").get<double>();
                const double depth=(expected.at("fitted_diameter_mm").get<double>()-d)/2/std::tan(std::acos(-1.)/6);
                near(g,end,"Fitted shoulder follows thread runout");near(g-c,depth,"Fitted shoulder chamfer depth");shoulder=true;
            }
            if(c>0&&std::abs(g-l)<1e-6) {
                const bool explicit_tip=expected.contains("tip_chamfer");
                const double angle=explicit_tip?expected.at("tip_chamfer").at("angle_degrees").get<double>():45.;
                near(std::abs(surface.Cone().SemiAngle()),angle*std::acos(-1.)/180.,"Tip chamfer cone angle");
                const double depth=explicit_tip?expected.at("tip_chamfer").at("distance_a_mm").get<double>():expected.at("pitch_mm").get<double>()/2;
                near(g-c,depth,"Tip chamfer axial depth");tip=true;
                if(expected.at("standard").get<std::string>().starts_with("ISO ")&&explicit_tip) {
                    const double end_diameter=d-2*(g-c)*std::tan(std::abs(surface.Cone().SemiAngle()));
                    require(angle<=50&&g-c<=2*expected.at("pitch_mm").get<double>(),"ISO CH end angle or incomplete length exceeded");
                    require(end_diameter<=d-1.226869*expected.at("pitch_mm").get<double>()+1e-8,"ISO CH end exceeds the external thread root diameter");
                }
            }
        }
    }
    require(shaft&&fillet,"Actual shaft cylinder or underhead fillet missing");
    if(expected.contains("fitted_diameter_mm"))require(fitted&&shoulder,"Actual fitted cylinder or 30-degree shoulder is missing");
    const bool raised=expected.value("bearing_face",expected.at("product_grade").get<std::string>()!="C");
    require(bearing==raised,"Bearing-face geometry disagrees with the selected variant");
    require(hole==expected.contains("locking_hole_diameter_mm"),"Locking hole radius, axis or head-center position disagrees");
    if(expected.contains("locking_hole_diameter_mm")) {
        const auto& cut=document.history.back();
        require(cut.feature_kind==document::FeatureKind::Feature&&cut.feature.type==document::FeatureType::Modeling&&
            cut.combine_mode==document::CombineMode::Subtract&&!cut.feature.symmetric,"Locking hole is not an independent current Default cut");
        for(const auto& side:cut.feature.sides)require(side.operation==document::FeatureSideOperation::Extrusion&&
            side.extrusion_extent==document::EndCondition::ThroughAll,"Locking hole must cut Through All on both sides");
    }
    require(tip,"Actual tip chamfer cone missing");
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
    require(properties.Mass()>0,"Nonpositive solid volume");
    if(result.physical_properties_current)near(result.volume,properties.Mass(),"Persisted exact volume");
    const double bom_mass=std::stod(document.user_parameter_values.at("mass").at(""));
    require(std::abs(bom_mass-properties.Mass()*7.85e-6)<=.50001e-6,
        "BOM kilogram mass disagrees with exact steel volume at six decimal places");
    for(auto sketch:document.sketches) {
        const auto solved=sketch.solve();
        require(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Library Sketch is not fully constrained");
        require(std::ranges::none_of(sketch.external_references,[](const auto& ref){return ref.broken;}),"Broken external Sketch support");
        // Unlocked driving dimensions may deliberately change during dragging.
        // Lock this independent copy before asserting immobility; DOF=0 alone
        // does not mean that editable driving values have been locked.
        for(auto& dimension:sketch.dimensions)dimension.locked=true;
        const auto initial_points=sketch.points;
        for(const auto& point:initial_points) {
            if(point.id!=initial_points.front().id&&point.id!=initial_points.back().id)continue;
            for(const auto delta:std::array<std::array<double,2>,3>{{{2.,3.},{-2.,1.},{4.,-3.}}}) {
                static_cast<void>(sketch.move_point(point.id,point.x+delta[0],point.y+delta[1]));
                const auto after=sketch.solve();
                require(after.remaining_degrees_of_freedom==0&&after.maximum_residual<1e-8,"Fully constrained Sketch escaped during repeated dragging");
                for(const auto& original:initial_points) {
                    const auto* current=sketch.find_point(original.id);
                    require(current,"Dragging changed native point identity");
                    near(current->x,original.x,"Fully constrained point X moved");
                    near(current->y,original.y,"Fully constrained point Y moved");
                }
            }
        }
    }
    const auto* feature=document.find_container(expected.value("thread_container",std::string{"01a14429e3207acba34fc9c038cdccfe"}));
    require(feature,"Thread feature missing");const auto& thread=feature->shaft_thread;
    near(thread.nominal_diameter,d,"Thread nominal diameter");near(thread.pitch,expected.at("pitch_mm"),"Thread pitch");
    require(thread.designation==expected.at("designation").get<std::string>(),"Thread designation changed");
    if(expected.contains("thread_length_mm")) {
        require(thread.end_condition==document::EndCondition::Length,"Partial thread does not use numerical length");
        near(thread.length,expected.at("thread_length_mm"),"Partial thread length");
        near(thread.runout_pitch_factor*thread.pitch,expected.at("runout_mm"),"Partial thread runout");
        require(thread.runout_enabled&&l-thread.length-thread.runout_pitch_factor*thread.pitch>r,
            "Partial thread reaches the underhead transition");
    }else require(thread.end_condition==document::EndCondition::UpTo&&!thread.runout_enabled,"Full thread lost head support");
    require(!state->session.is_dirty()&&!state->session.can_undo(),"Inspection modified the native candidate");
}
}
static int run_fastener_contract_tests(int argc,char** argv) {
    try {
        if(argc==4&&std::string(argv[1])=="--verify-native-seating") {
            const auto shape=[](const char* path) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(fs::u8path(path)));
                const auto* state=live.open_part(id);require(state,"Seating source is not a Part");
                const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Seating requires one calculated Body");
                TopoDS_Shape result;BRep_Builder builder;std::istringstream stream(boundary.body_outputs.begin()->second.get().kernel_shape.get());
                BRepTools::Read(result,stream,builder);require(!result.IsNull()&&BRepCheck_Analyzer(result).IsValid(),"Invalid seating source");return result;
            };
            const auto first=shape(argv[2]),second=shape(argv[3]);BRepAlgoAPI_Common common(first,second);
            require(common.IsDone(),"Seating material intersection failed");GProp_GProps properties;
            BRepGProp::VolumeProperties(common.Shape(),properties,1e-12);
            BRepExtrema_DistShapeShape distance(first,second);distance.Perform();
            require(distance.IsDone()&&distance.Value()<1e-7,"Seating sources do not touch");
            require(std::abs(properties.Mass())<1e-8,"Seating sources have material interference");
            std::cout<<Json{{"material_intersection_mm3",properties.Mass()},{"contact_distance_mm",distance.Value()}}.dump()<<'\n';return 0;
        }
        if(argc==3&&std::string(argv[1])=="--formed-nose-washers") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            std::size_t checked{};
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/expected.at("filename").get<std::string>()));
                const auto* state=live.open_part(id);require(state,"Expected native nose washer Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Nose washer calculation failed");
                const auto& output=boundary.body_outputs.begin()->second.get();TopoDS_Shape shape;BRep_Builder builder;
                std::istringstream stream(output.kernel_shape.get());BRepTools::Read(shape,stream,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid nose washer BRep");
                std::size_t solids{};for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                require(solids==1&&output.solid_present&&output.physical_properties_current,"Nose washer lost its connected physical solid");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);
                const double volume=properties.Mass();require(volume>0,"Nose washer has zero volume");
                require(std::abs(volume-output.volume)<1e-8*volume,"Nose washer physical cache differs from BRep");
                require(std::abs(volume-expected.at("independent_volume_mm3").get<double>())<1e-8*volume,"Nose washer independent section integral differs");
                Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                const std::array<double,6> actual{x0,y0,z0,x1,y1,z1};
                for(std::size_t i=0;i<6;++i)near(actual[i],expected.at("bounds_mm")[i],"Nose washer physical extent");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Nose washer BOM mass is stale");
                require(doc.history.size()==3&&doc.sketches.size()==3,"Nose washer lost its native sheet history");
                const auto bend=std::ranges::find_if(doc.history,[](const auto& feature){return feature.feature_kind==document::FeatureKind::Bend;});
                require(bend!=doc.history.end()&&bend->bend.sheet_attachment&&bend->bend.radius_follows_thickness,"Nose washer lost its attached native Bend");
                near(bend->bend.angle_degrees,expected.at("angle_degrees"),"Nose washer Bend angle");
                for(const auto& sketch:doc.sketches)verify_profile_dragging(sketch);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Nose washer inspection changed the source");++checked;
            }
            std::cout<<checked<<" native nose washers: real Bend, connected BRep, independent volume, bounds, mass and profile dragging passed\n";return 0;
        }
        if(argc==4&&std::string(argv[1])=="--recalculate-candidate") {
            auto doc=document::PartDocument::load(fs::u8path(argv[2]));kernel::OcctKernel kernel;
            const auto calculated=workspace::calculate_part_with_resolved_references(kernel,doc,nullptr);
            require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Fresh candidate calculation failed");
            for(std::size_t i=0;i<calculated.size();++i)for(const auto& [body,output]:calculated[i].body_outputs)
                std::cout<<i<<' '<<body<<' '<<output.get().volume<<'\n';
            doc.save(fs::u8path(argv[3]),calculated);std::cout<<"Candidate calculated without previous boundaries\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--inspect-candidate-properties") {
            workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(fs::u8path(argv[2])));
            const auto* state=live.open_part(id);require(state,"Expected native Part");Json rows=Json::array();
            for(const auto& [body,output]:state->session.calculated_boundaries().back().body_outputs) {
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(output.get().kernel_shape.get());BRepTools::Read(shape,stream,builder);
                GProp_GProps fixed,adaptive,gk;BRepGProp::VolumeProperties(shape,fixed);
                const double error=BRepGProp::VolumeProperties(shape,adaptive,1e-12);BRepGProp::VolumePropertiesGK(shape,gk,1e-12,false,true);
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                std::size_t solids{};for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                rows.push_back({{"body",body},{"valid",BRepCheck_Analyzer(shape).IsValid()},{"cached",output.get().volume},
                    {"solid_count",solids},{"solid_present",output.get().solid_present},{"properties_current",output.get().physical_properties_current},
                    {"fixed",fixed.Mass()},{"adaptive",adaptive.Mass()},{"adaptive_error",error},{"gk",gk.Mass()},
                    {"bounds",{x0,y0,z0,x1,y1,z1}}});
            }
            std::cout<<rows.dump(2)<<'\n';return 0;
        }
        if(argc==4&&(std::string(argv[1])=="--wing-review-title"||std::string(argv[1])=="--reference-title")) {
            const auto path=fs::absolute(fs::u8path(argv[2]));const std::string locale=argv[3];
            require(std::set<std::string>{"cs","en","de","fr","ru"}.contains(locale),"Unsupported study title language");
            auto drawing=drawing::DrawingDocument::load(path);require(drawing.sheets.size()==1&&!drawing.sheets.front().views.empty(),"Wing review requires one sheet with real source views");
            auto& sheet=drawing.sheets.front();const auto source=sheet.views.front();auto source_path=source.source_path;if(source_path.is_relative())source_path=path.parent_path()/source_path;
            workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(source_path));require(id==source.source_document_id,"Wing review source identity mismatch");
            drawing.source_document_id=id;drawing.source_path=source_path;drawing.source_name=document::path_to_utf8(source_path.stem());
            sheet.selected_source_document_id=id;sheet.title_block_locale=locale;
            const auto root=fs::path(__FILE__).parent_path().parent_path().parent_path();std::string upper=locale;std::ranges::transform(upper,upper.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
            workspace::load_drawing_template(drawing,sheet.id,root/"config/formats/ZE-DRAWING-FRAME.frmz",false,&live,path,"ze-frame-variant-A4");
            workspace::load_drawing_template(drawing,sheet.id,root/"config/formats/ZE-TITLE-BLOCK.tblz",true,&live,path,"ze-title-block-variant-"+upper);
            require(!sheet.title_block_fields.empty()&&!sheet.title_block_texts.empty()&&!sheet.title_block_images.empty(),"Native title Family variant is incomplete");
            drawing.save(path);const auto reopened=drawing::DrawingDocument::load(path);
            const auto& saved=reopened.sheets.front();require(saved.title_block_fields.size()==sheet.title_block_fields.size()&&saved.title_block_images.size()==sheet.title_block_images.size(),"Wing review title content count changed");
            for(std::size_t i=0;i<sheet.title_block_fields.size();++i)require(saved.title_block_fields[i].id==sheet.title_block_fields[i].id&&saved.title_block_fields[i].expression==sheet.title_block_fields[i].expression,"Wing review title field identity changed");
            for(std::size_t i=0;i<sheet.title_block_images.size();++i)require(saved.title_block_images[i].id==sheet.title_block_images[i].id&&saved.title_block_images[i].data_base64==sheet.title_block_images[i].data_base64,"Wing review embedded artwork changed");
            std::cout<<"Native wing review source, frame and title Family variant embedded: "<<locale<<'\n';return 0;
        }
        if(argc==3&&std::string(argv[1])=="--pressed-wing-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            std::size_t checked=0;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/expected.at("filename").get<std::string>()));
                const auto* state=live.open_part(id);require(state,"Pressed wing nut is not a native Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Pressed wing calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();TopoDS_Shape shape;BRep_Builder builder;
                std::istringstream stream(result.kernel_shape.get());BRepTools::Read(shape,stream,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid pressed wing BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Pressed wing nut is disconnected");
                const double span=expected.at("span_mm"),h=expected.at("height_mm"),base=expected.at("hub_base_diameter_mm");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(x0,-span/2,"Pressed wing negative span");near(x1,span/2,"Pressed wing positive span");
                near(y0,-base/2,"Pressed hub negative radius");near(y1,base/2,"Pressed hub positive radius");
                near(z0,0,"Pressed wing seating plane");near(z1,h,"Pressed wing overall height");
                GProp_GProps volume,gk;BRepGProp::VolumeProperties(shape,volume,1e-12);
                BRepGProp::VolumePropertiesGK(shape,gk,1e-12,false,true);
                require(volume.Mass()>0&&result.physical_properties_current,"Pressed wing physical properties missing");
                require(std::abs(result.volume-gk.Mass())<1e-10*gk.Mass(),"Pressed wing cached mass integral differs from Gauss-Kronrod BRep integral");
                require(std::abs(volume.Mass()-gk.Mass())<2e-8*gk.Mass(),"Pressed wing adaptive and Gauss-Kronrod BRep integrals diverge");
                near(gk.Mass(),expected.at("independent_volume_mm3"),"Pressed wing independent nominal-section integral");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-gk.Mass()*expected.at("density_kg_mm3").get<double>())<=.50001e-6,
                    "Pressed wing BOM mass is stale");
                bool bore=false,crown=false;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()!=GeomAbs_Cylinder)continue;
                    const auto cylinder=surface.Cylinder();
                    bore|=std::abs(std::abs(cylinder.Axis().Direction().Z())-1)<1e-8&&std::abs(cylinder.Radius()-expected.at("bore_mm").get<double>()/2)<1e-6;
                    crown|=std::abs(std::abs(cylinder.Axis().Direction().Y())-1)<1e-8&&std::abs(cylinder.Radius()-expected.at("outside_radius_mm").get<double>())<1e-6;
                }
                require(bore&&crown,"Pressed wing bore or original crown cylinder missing");
                require(doc.history.size()==6&&doc.sketches.size()==3,"Pressed wing authored history differs from the expected native operations");
                for(const auto& sketch:doc.sketches)verify_profile_dragging(sketch);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Pressed wing inspection modified the source");++checked;
            }
            std::cout<<checked<<" pressed wing nuts: connected BRep, seat, independent volume, mass and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--wing-studies") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;Json report=Json::array();
            for(const auto& expected:manifest.at("variants")) {
                require(expected.at("review_status").get<std::string>().starts_with("unreleased study"),"This checker accepts only explicitly unfinished studies");
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/expected.at("filename").get<std::string>()));
                const auto* state=live.open_part(id);require(state,"Wing study is not a Part");const auto& doc=state->session.document();const auto& complete=state->session.calculated_boundaries().back();
                require(complete.calculation_errors.empty()&&complete.body_outputs.size()==1&&doc.body_history.bodies().size()==1,"Wing study must have one valid ordinary Body");
                const auto& result=complete.body_outputs.begin()->second.get();TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(result.kernel_shape.get());BRepTools::Read(shape,stream,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid wing study BRep");int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Disconnected wing study");
                Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                const double span=expected.at("span_mm"),height=expected.at("height_mm"),base=expected.at("hub_base_diameter_mm");
                near(x0,-span/2,"Wing left span");near(x1,span/2,"Wing right span");near(z0,0,"Wing seating plane");near(z1,height,"Wing total height");near(y0,-base/2,"Hub lower extent");near(y1,base/2,"Hub upper extent");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);const auto volume=properties.Mass();require(volume>0&&result.physical_properties_current,"Missing wing physical analysis");
                require(std::abs(volume-result.volume)<=1e-8*std::max(1.,volume),"Wing cached/adaptive volume mismatch");
                const auto mass=volume*expected.at("density_kg_mm3").get<double>();require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-mass)<=.50001e-6,"Wing persisted mass is stale");
                bool bore=false,crown=false,front_root=false;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()!=GeomAbs_Cylinder)continue;const auto cylinder=surface.Cylinder();
                    bore|=std::abs(std::abs(cylinder.Axis().Direction().Z())-1)<1e-8&&std::abs(cylinder.Radius()-expected.at("bore_mm").get<double>()/2)<1e-6;
                    if(std::abs(std::abs(cylinder.Axis().Direction().Y())-1)<1e-8){crown|=std::abs(cylinder.Radius()-expected.at("crown_radius_mm").get<double>())<1e-6;front_root|=std::abs(cylinder.Radius()-expected.at("secondary_edge_radius_mm").get<double>())<1e-6;}
                }
                require(bore&&crown&&front_root,"Wing bore, crown or front root cylinder missing");
                for(const auto& sketch:doc.sketches)verify_profile_dragging(sketch);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Wing inspection modified the native state");
                report.push_back({{"filename",expected.at("filename")},{"valid_single_solid",true},{"bounds",true},{"adaptive_volume_mm3",volume},{"physical_mass_kg",mass},{"sketch_dragging",true},{"factory_ready",false}});
            }
            std::ofstream output(folder/"native-study-verification.json");output<<report.dump(2)<<'\n';std::cout<<report.size()<<" unfinished wing studies: BRep, bounds, physical mass and Sketch dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--probe-wing-fillets") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));std::ifstream input(folder/"manifest.json");Json manifest;input>>manifest;Json report=Json::array();
            for(const auto& v:manifest.at("variants")) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/v.at("filename").get<std::string>()));
                const auto* state=live.open_part(id);require(state,"Expected wing Part");const auto& result=state->session.calculated_boundaries().back().body_outputs.begin()->second.get();
                BRep_Builder builder;BRepTools_ShapeSet archive(builder);std::istringstream stream(result.kernel_shape.get());archive.Read(stream);TopoDS_Shape body;archive.Read(body,stream);require(BRepCheck_Analyzer(body).IsValid(),"Invalid wing input");
                std::vector<TopoDS_Edge> roots,tips;const double h=v.at("hub_height_mm"),m=v.at("height_mm"),R=v.at("crown_radius_mm"),cx=v.at("span_mm").get<double>()/2-R,cz=m-R;
                for(TopExp_Explorer it(body,TopAbs_EDGE);it.More();it.Next()) {
                    const auto edge=TopoDS::Edge(it.Current());BRepAdaptor_Curve curve(edge);bool side=true,root=true,tip=true;double lo=1e100,hi=-1e100;
                    for(int sample=0;sample<=20;++sample) {
                        const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*sample/20.);
                        lo=std::min(lo,p.Z());hi=std::max(hi,p.Z());
                        side&=std::abs(std::abs(p.Y())-(v.at("root_thickness_mm").get<double>()+(v.at("tip_thickness_mm").get<double>()-v.at("root_thickness_mm").get<double>())*p.Z()/m)/2)<1e-6;
                        root&=p.Z()>=-1e-6&&p.Z()<=h+1e-6&&std::abs(std::hypot(p.X(),p.Y())-(v.at("hub_base_diameter_mm").get<double>()+(v.at("hub_top_diameter_mm").get<double>()-v.at("hub_base_diameter_mm").get<double>())*p.Z()/h)/2)<1e-6;
                        tip&=std::abs(std::hypot(std::abs(p.X())-cx,p.Z()-cz)-R)<1e-6;
                    }
                    const auto unique=[&](auto& values){if(std::ranges::none_of(values,[&](const auto& existing){return edge.IsSame(existing);}))values.push_back(edge);};
                    if(side&&root&&hi-lo>h/2)unique(roots);if(side&&tip&&hi-lo>1)unique(tips);
                }
                require(roots.size()==4&&tips.size()==4,"Expected four exact root and four exact crown edges");
                for(int mode=0;mode<3;++mode) {
                    BRepFilletAPI_MakeFillet fillet(body);
                    if(mode!=0)for(const auto& edge:roots)fillet.Add(v.at("root_radius_mm").get<double>(),edge);
                    if(mode!=1)for(const auto& edge:tips)fillet.Add(v.at("edge_radius_mm").get<double>(),edge);
                    fillet.Build();const bool valid=fillet.IsDone()&&!fillet.Shape().IsNull()&&BRepCheck_Analyzer(fillet.Shape()).IsValid();
                    report.push_back({{"filename",v.at("filename")},{"mode",mode},{"contours",fillet.NbContours()},{"done",fillet.IsDone()},{"valid",valid},{"faulty_contours",fillet.NbFaultyContours()}});
                    if(valid&&mode==2){std::ofstream saved(folder/"joint-fillet.brep");BRepTools::Write(fillet.Shape(),saved);}
                }
            }
            std::ofstream output(folder/"joint-fillet-probe.json");output<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';return 0;
        }
        if(argc==4&&std::string(argv[1])=="--dump-candidate-edges") {
            workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(fs::u8path(argv[2])));
            const auto* state=live.open_part(id);require(state,"Expected native Part");
            const auto& calculated=state->session.calculated_boundaries();require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Candidate contains calculation errors");
            Json rows=Json::array();
            for(const auto& edge:calculated.back().mesh.edges)if(edge.reference.valid()) {
                auto points=Json::array();for(const auto& p:edge.points)points.push_back({p.x,p.y,p.z});
                rows.push_back({{"owner",edge.reference.owner_id},{"key",edge.reference.semantic_key},{"instance_path",edge.reference.instance_path},{"parameter_seam",edge.parameter_seam},{"points",points}});
            }
            std::ofstream output(fs::u8path(argv[3]));output<<rows.dump(2)<<'\n';require(output.good(),"Cannot save candidate edge inspection");
            require(!state->session.is_dirty()&&!state->session.can_undo(),"Edge inspection changed the candidate");
            return 0;
        }
        if(argc==4&&std::string(argv[1])=="--audit-native-properties") {
            const auto root=fs::absolute(fs::u8path(argv[2]));
            Json report={{"checked_parts",0},{"trimmed_cylindrical_bodies",0},{"stale",Json::array()},{"errors",Json::array()}};
            for(const auto& entry:fs::recursive_directory_iterator(root)) {
                if(!entry.is_regular_file()||entry.path().extension()!=".prtz")continue;
                const auto filename=document::path_to_utf8(entry.path().lexically_relative(root));
                try {
                    workspace::Workspace live;
                    const auto id=workspace::insert_native_document(live,workspace::read_native_document(entry.path()));
                    const auto* state=live.open_part(id);require(state,"Property audit expected a native Part");
                    report["checked_parts"]=report["checked_parts"].get<int>()+1;
                    const auto& boundaries=state->session.calculated_boundaries();
                    if(boundaries.empty())continue;
                    for(const auto& [body_id,output]:boundaries.back().body_outputs) {
                        const auto& result=output.get();if(result.kernel_shape.empty())continue;
                        TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                        require(!shape.IsNull(),"Property audit could not read BRep");
                        // FORM definitions may intentionally contain open role
                        // surfaces. A volume integral over them is not a solid
                        // volume and must not invalidate their zero-volume cache.
                        if(!TopExp_Explorer(shape,TopAbs_SOLID).More())continue;
                        bool trimmed_cylinder=false;
                        for(TopExp_Explorer face(shape,TopAbs_FACE);face.More()&&!trimmed_cylinder;face.Next()) {
                            const auto support=TopoDS::Face(face.Current());BRepAdaptor_Surface surface(support);
                            if(surface.GetType()!=GeomAbs_Cylinder)continue;
                            for(TopExp_Explorer edge(support,TopAbs_EDGE);edge.More();edge.Next()) {
                                BRepAdaptor_Curve2d curve(TopoDS::Edge(edge.Current()),support);
                                if(curve.GetType()!=GeomAbs_Line) {trimmed_cylinder=true;break;}
                                const auto tangent=curve.Line().Direction();
                                if(std::abs(tangent.X())>1e-12&&std::abs(tangent.Y())>1e-12) {trimmed_cylinder=true;break;}
                            }
                        }
                        if(!trimmed_cylinder)continue;
                        report["trimmed_cylindrical_bodies"]=report["trimmed_cylindrical_bodies"].get<int>()+1;
                        GProp_GProps properties;const auto error=BRepGProp::VolumeProperties(shape,properties,1e-12);
                        require(std::isfinite(error)&&error>=0,"Independent property integration failed");
                        const double exact=properties.Mass(),difference=result.volume-exact;
                        if(std::abs(difference)>1e-9*std::max(1.,std::abs(exact)))
                            report["stale"].push_back({{"path",filename},{"body",body_id},{"cached_volume",result.volume},{"adaptive_volume",exact},{"difference_mm3",difference}});
                    }
                    require(!state->session.is_dirty()&&!state->session.can_undo(),"Property audit changed the source session");
                } catch(const std::exception& error) {report["errors"].push_back({{"path",filename},{"error",error.what()}});}
            }
            std::ofstream output(fs::u8path(argv[3]));output<<report.dump(2)<<'\n';require(output.good(),"Cannot save property audit");
            std::cout<<report.at("checked_parts")<<" native Parts inspected; "<<report.at("trimmed_cylindrical_bodies")
                <<" trimmed cylindrical bodies; "<<report.at("stale").size()<<" stale volumes; "<<report.at("errors").size()<<" errors\n";
            return report.at("stale").empty()&&report.at("errors").empty()?0:1;
        }
        if(argc==3&&std::string(argv[1])=="--insert-lock-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            std::size_t checked=0;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Lock nut must be a native Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==2,"Lock nut requires two ordinary calculated Bodies in one Part");
                const double pi=std::acos(-1.),s=expected.at("across_flats_mm"),h=expected.at("height_mm"),z=expected.at("hex_height_mm"),
                    r=expected.at("bore_mm").get<double>()/2,d=expected.at("diameter_mm"),lip=expected.at("lip_mm"),
                    ro=expected.at("collar_outside_radius_mm"),rc=ro-expected.at("collar_wall_mm").get<double>(),
                    ri=expected.at("lip_bore_radius_mm"),round=expected.at("corner_radius_mm"),insert_r=expected.at("insert_bore_radius_mm");
                const double t=std::tan(pi/6),sec=1/std::cos(pi/6),a=s/2,mouth=expected.at("mouth_depth_mm");
                const double removed=12*t*a*a*a*((sec*t+std::log(sec+t))/3-t+pi/18);
                const double hole=pi*r*r*z+pi*(r*std::tan(pi/3)*mouth*mouth+std::pow(std::tan(pi/3),2)*std::pow(mouth,3)/3);
                const double collar=pi*((ro*ro-rc*rc)*(h-lip-z)+(ro*ro-ri*ri)*lip)
                    -pi*((2-pi/2)*ro*round*round+(-5./3+pi/2)*std::pow(round,3));
                const double shell_volume=std::sqrt(3.)*s*s*z/2-removed-hole+collar;
                const double insert_volume=pi*(rc*rc-insert_r*insert_r)*(h-lip-z);
                require(shell_volume>0&&insert_volume>0,"Nonpositive independent lock nut volumes");
                std::vector<TopoDS_Shape> shapes;double total=0;int blue=0;
                for(const auto& [body_id,output]:boundary.body_outputs) {
                    const auto& result=output.get();TopoDS_Shape shape;BRep_Builder builder;
                    std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                    require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid lock nut Body BRep");
                    int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                    require(solids==1,"Each lock nut Body must contain one connected solid");
                    const bool insert=doc.appearance.bodies.contains(body_id)&&doc.appearance.bodies.at(body_id).color=="#2455a4";
                    blue+=insert;GProp_GProps props;BRepGProp::VolumeProperties(shape,props,1e-11);
                    near(props.Mass(),insert?insert_volume:shell_volume,"Lock nut independent volume");
                    near(result.volume,props.Mass(),"Lock nut cached physical volume");total+=props.Mass();
                    Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                    near(z0,insert?z:0,"Lock nut bottom plane");near(z1,insert?h-lip:h,"Lock nut top plane");
                    near(x1,insert?rc:s/std::sqrt(3.),"Lock nut positive X extent");near(x0,-x1,"Lock nut centered X extent");
                    near(y1,insert?rc:s/2,"Lock nut positive Y extent");near(y0,-y1,"Lock nut centered Y extent");
                    shapes.push_back(shape);
                }
                require(blue==1,"Symbolic insert must retain its blue Body appearance");
                BRepAlgoAPI_Common common(shapes[0],shapes[1]);common.Build();require(common.IsDone(),"Cannot inspect Body intersection");
                GProp_GProps overlap;BRepGProp::VolumeProperties(common.Shape(),overlap,1e-11);near(overlap.Mass(),0,"Symbolic insert overlaps steel shell");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-total*expected.at("steel_density_kg_mm3").get<double>())<=.50001e-6,
                    "Lock nut BOM must apply the requested homogeneous steel density to both Bodies");
                for(const auto& profile:doc.sketches)verify_profile_dragging(profile);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Lock nut inspection modified native state");++checked;
            }
            std::cout<<checked<<" steel Part lock nuts: valid Bodies, analytic volumes, zero overlap, color and homogeneous mass passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--hex-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            std::size_t checked=0;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Nut is not a native Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Nut calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid nut BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Nut must have one connected solid");
                const double s=expected.at("across_flats_mm"),m=expected.at("height_mm"),d=expected.at("diameter_mm"),
                    pitch=expected.at("pitch_mm"),r=expected.at("bore_mm").get<double>()/2,c=expected.at("mouth_chamfer_mm"),
                    beta=expected.at("chamfer_degrees");
                const double pi=std::acos(-1.),q=pi/6,a=s/2,sec=1/std::cos(q),t=std::tan(q);
                // Integrate each conically trimmed hexagon corner in polar
                // coordinates; this is independent of the author's BRep path.
                const double corner_integral=(sec*t+std::log(sec+t))/3-t+q/3;
                const double removed=12*std::tan(beta*pi/180)*a*a*a*corner_integral;
                const double volume=std::sqrt(3.)*s*s*m/2-removed-pi*r*r*m-2*pi*(r*c*c+c*c*c/3);
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-11);
                near(properties.Mass(),volume,"Nut independent analytical volume");near(result.volume,volume,"Nut cached volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Nut BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Nut seating plane");near(z1,m,"Nut height");near(x0,-s/std::sqrt(3.),"Nut negative corner");near(x1,s/std::sqrt(3.),"Nut positive corner");
                near(y0,-s/2,"Nut lower flat");near(y1,s/2,"Nut upper flat");
                int bores=0,outer_cones=0,mouth_cones=0;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
                    if(surface.GetType()==GeomAbs_Cylinder){near(surface.Cylinder().Radius(),r,"Nut bore radius");++bores;}
                    if(surface.GetType()==GeomAbs_Cone) {
                        const double angle=std::abs(surface.Cone().SemiAngle());
                        if(std::abs(angle-pi/4)<1e-8)++mouth_cones;
                        else {near(angle,pi/2-beta*pi/180,"Nut outer chamfer slope");++outer_cones;}
                    }
                }
                require(bores>=1&&mouth_cones==2,"Nut bore or its two entrance chamfers are missing");
                require(beta>0?outer_cones>=2:outer_cones==0,"Nut outside chamfer form is wrong");
                require(doc.history.size()==(beta>0?5u:3u),"Nut authored history differs from prism, optional bevels, thread and exit chamfer");
                const auto& prism=doc.history.front();require(prism.feature_kind==document::FeatureKind::Feature&&
                    prism.feature.sides[0].operation==document::FeatureSideOperation::Extrusion&&prism.feature.sides[1].operation==document::FeatureSideOperation::None,
                    "Nut must extrude one-sided from its seating plane");
                const auto thread=std::ranges::find(doc.history,document::FeatureKind::Thread,&document::HistoryContainer::feature_kind);
                require(thread!=doc.history.end()&&thread->thread.enabled,"Native internal thread missing");
                near(thread->thread.nominal_diameter,d,"Nut thread nominal diameter");near(thread->thread.pitch,pitch,"Nut thread pitch");
                near(thread->thread.length_forward,m,"Nut full thread length");
                require(thread->thread.end_condition_forward==document::EndCondition::ThroughAll&&!thread->hole.drill_point_enabled,"Nut opening is not Through All");
                for(const auto& profile:doc.sketches)verify_profile_dragging(profile);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Nut inspection modified native state");++checked;
            }
            std::cout<<checked<<" hex nuts: Origin seating, one solid, conical bevels, native internal threads, analytical volume and mass passed\n";return 0;
        }
        if(argc==3&&(std::string(argv[1])=="--wheel-fasteners"||std::string(argv[1])=="--flange-nuts")) {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Wheel fastener is not a native Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Wheel fastener calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid wheel fastener BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Wheel fastener must be one solid");
                const double h=expected.at("height_mm"),volume=expected.at("independent_volume_mm3");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);
                if(std::abs(properties.Mass()-volume)>=std::max(1e-5,volume*1e-8)||std::abs(result.volume-volume)>=std::max(1e-5,volume*1e-8))
                    std::cerr<<"Independent volume "<<std::setprecision(16)<<volume<<", adaptive BRep "<<properties.Mass()<<", cached "<<result.volume<<'\n';
                require(std::abs(properties.Mass()-volume)<std::max(1e-5,volume*1e-8),"Wheel independent section volume");
                require(std::abs(result.volume-volume)<std::max(1e-5,volume*1e-8),"Wheel cached volume");
                require(result.physical_properties_current,"Wheel physical properties are stale");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Wheel BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Wheel fastener bottom datum");near(z1,h,"Wheel fastener height");
                const bool washer=expected.value("state",std::string{})=="compressed";
                const bool spherical=expected.contains("sphere_radius_mm"),flange=expected.contains("roof_angle_degrees");int contacts=0;
                if(flange) {
                    const double R=expected.at("flange_diameter_mm").get<double>()/2,b=expected.at("bearing_radius_mm"),d=expected.at("diameter_mm");
                    near(x0,-R,"Flange negative X extent");near(x1,R,"Flange positive X extent");near(y0,-R,"Flange negative Y extent");near(y1,R,"Flange positive Y extent");
                    require(2*(R-b)>=expected.at("minimum_bearing_diameter_mm").get<double>()-1e-8,"Flange bearing diameter below source limit");
                    double bearing_area=0.;
                    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                        const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
                        if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Location().Z())<1e-8&&std::abs(surface.Plane().Axis().Direction().Z())>1-1e-8) {
                            GProp_GProps area;BRepGProp::SurfaceProperties(it.Current(),area);bearing_area+=area.Mass();
                        }
                    }
                    near(bearing_area,std::numbers::pi*((R-b)*(R-b)-d*d/4),"Flange exact bearing face");
                }
                if(washer) {
                    const auto axis=std::ranges::find_if(doc.history,[](const auto& c){return c.feature_kind==document::FeatureKind::Feature&&c.feature.type==document::FeatureType::Axis;});
                    require(axis!=doc.history.end(),"Spherical washer native datum axis missing");near(axis->feature.sides[0].length,h,"Washer datum axis follows height");
                }
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
                    if(spherical&&(surface.GetType()==GeomAbs_Sphere||surface.GetType()==GeomAbs_SurfaceOfRevolution)) {
                        const double R=expected.at("sphere_radius_mm");
                        const double r=expected.at(washer?"transition_diameter_mm":"end_diameter_mm").get<double>()/2;
                        const double center=washer?h-std::sqrt(R*R-r*r):std::sqrt(R*R-r*r);++contacts;
                        for(double u:{.25,.5,.75})for(double v:{.25,.5,.75}) {
                            const auto p=surface.Value(surface.FirstUParameter()+u*(surface.LastUParameter()-surface.FirstUParameter()),
                                surface.FirstVParameter()+v*(surface.LastVParameter()-surface.FirstVParameter()));
                            near(p.X()*p.X()+p.Y()*p.Y()+std::pow(p.Z()-center,2),R*R,"Wheel spherical contact equation");
                        }
                    }
                    if(!spherical&&!flange&&surface.GetType()==GeomAbs_Cone) {
                        const double slope=std::tan(expected.at("included_angle_degrees").get<double>()*std::numbers::pi/360);
                        bool matches=true;
                        for(double u:{.25,.5,.75})for(double v:{.25,.5,.75}) {
                            const auto p=surface.Value(surface.FirstUParameter()+u*(surface.LastUParameter()-surface.FirstUParameter()),
                                surface.FirstVParameter()+v*(surface.LastVParameter()-surface.FirstVParameter()));
                            matches&=std::abs(std::hypot(p.X(),p.Y())-expected.at("end_diameter_mm").get<double>()/2-p.Z()*slope)<1e-7;
                        }
                        if(matches)++contacts;
                    }
                }
                require(flange||contacts>0,"Wheel spherical/conical contact missing");
                BRepClass3d_SolidClassifier classifier(shape);
                for(double f:{.1,.5,.9}) {classifier.Perform(gp_Pnt(0,0,h*f),1e-8);require(classifier.State()==TopAbs_OUT,"Wheel fastener bore obstructed");}
                if(!washer) {
                    const auto thread=std::ranges::find(doc.history,document::FeatureKind::Thread,&document::HistoryContainer::feature_kind);
                    require(thread!=doc.history.end()&&thread->thread.enabled,"Wheel native thread missing");
                    near(thread->thread.nominal_diameter,expected.at("diameter_mm"),"Wheel thread diameter");near(thread->thread.pitch,expected.at("pitch_mm"),"Wheel thread pitch");
                    near(thread->thread.length_forward,h-expected.value("clearance_depth_mm",0.),"Wheel thread length");
                }
                for(const auto& profile:doc.sketches)verify_profile_dragging(profile);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Wheel inspection changed document");
            }
            std::cout<<manifest.at("variants").size()<<" wheel fasteners: valid solids, independent volume/mass, contact equations, native threads and profile dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--weld-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            std::size_t checked=0;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/fs::u8path(expected.at("filename").get<std::string>())));
                const auto* state=live.open_part(id);require(state,"Weld nut is not an ordinary Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Weld nut calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Weld nut BRep is invalid");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                require(solids==1,"Weld projections are disconnected from the nut");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);
                const double volume=expected.at("independent_volume_mm3");
                require(std::abs(properties.Mass()-volume)<=std::max(1e-6,volume*1e-8),"Weld nut independent volume");
                require(std::abs(result.volume-volume)<=std::max(1e-6,volume*1e-8),"Weld nut cached volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Weld nut derived mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                const double h=expected.at("projection_height_mm"),m=expected.at("height_mm"),start=expected.at("bore_start_mm");
                const bool din=expected.at("kind")=="din";
                const double overall=din?m:m+h;
                near(z0,0,"Weld projection contact plane");near(z1,overall,"Weld nut overall height");
                BRepClass3d_SolidClassifier classifier(shape);
                for(const double z:{start+(m-start)*.25,start+(m-start)*.75}) {
                    classifier.Perform(gp_Pnt(0,0,z),1e-8);require(classifier.State()==TopAbs_OUT,"Weld nut thread bore obstructed");
                }
                const double r=expected.at("across_flats_mm").get<double>()/std::sqrt(3),b=expected.at("projection_depth_mm");
                double contact_area=0;
                if(din) {
                for(int corner=0;corner<6;++corner) {
                    const double angle=corner*std::acos(-1.)/3;
                    classifier.Perform(gp_Pnt((r-b/2)*std::cos(angle),(r-b/2)*std::sin(angle),h/2),1e-8);
                    require(classifier.State()==(corner%2?TopAbs_OUT:TopAbs_IN),"Weld nut does not have three alternating corner projections");
                }
                const double pilot=expected.at("pilot_diameter_mm").get<double>()/2,core=expected.at("bore_mm").get<double>()/2;
                classifier.Perform(gp_Pnt((pilot+core)/2,0,(start+h)/2),1e-8);require(classifier.State()==TopAbs_IN,"Weld nut pilot missing");
                contact_area=3*std::sqrt(3)*b*b;
                } else {
                    require(expected.at("kind")=="iso","Unknown weld-nut family");
                    const double R=expected.at("flange_diameter_mm").get<double>()/2;
                    const double rounding=expected.at("projection_radius_mm"),half=expected.at("projection_width_mm").get<double>()/2;
                    for(int corner=0;corner<6;++corner) {
                        const double angle=-std::acos(-1.)/2+corner*std::acos(-1.)/3;
                        classifier.Perform(gp_Pnt((R-b/2)*std::cos(angle),(R-b/2)*std::sin(angle),h/2),1e-8);
                        require(classifier.State()==(corner%2?TopAbs_OUT:TopAbs_IN),"Flange weld projections missing or incorrectly spaced");
                    }
                    const auto strip_primitive=[half](double radius){return radius*radius*std::asin(half/radius)+half*std::sqrt(radius*radius-half*half);};
                    contact_area=3*(strip_primitive(R-rounding)-strip_primitive(R-b+rounding));
                }
                double seating_area=0;int seats=0;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface surface(face);
                    if(surface.GetType()!=GeomAbs_Plane||std::abs(surface.Plane().Location().Z())>1e-7||std::abs(surface.Plane().Axis().Direction().Z())<.999)continue;
                    GProp_GProps face_properties;BRepGProp::SurfaceProperties(face,face_properties);seating_area+=face_properties.Mass();++seats;
                }
                require(seats==3,"Weld nut projection contact faces missing");near(seating_area,contact_area,"Weld projection contact area");
                for(const auto& sketch:doc.sketches)verify_profile_dragging(sketch);
                const auto opening=std::ranges::find(doc.history,document::FeatureKind::Thread,&document::HistoryContainer::feature_kind);
                require(opening!=doc.history.end()&&opening->placement.reference_valid&&!opening->placement.references.empty(),"Weld nut opening lost its actual pilot-face support");
                near(opening->placement.z,start,"Weld opening start face");near(opening->thread.length_forward,overall-start,"Weld thread length");
                ++checked;
            }
            require(checked>0,"No weld nuts verified");std::cout<<checked<<" weld nuts: valid connected solids, three projections, pilot support, independent volume/mass and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--cap-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/fs::u8path(expected.at("filename").get<std::string>())));
                const auto* state=live.open_part(id);require(state,"Cap nut is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Cap nut calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid cap nut BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Cap nut must contain one solid");
                const bool flat_cap=expected.value("closed_profile",std::string{})=="cone_flat";
                const double h=expected.at("height_mm"),depth=expected.at("bore_depth_mm");
                const double radius=flat_cap?expected.at("across_flats_mm").get<double>()/std::sqrt(3):expected.at("dome_radius_mm").get<double>();
                BRepClass3d_SolidClassifier classifier(shape);
                for(const double z:{depth*.25,depth*.75}) {classifier.Perform(gp_Pnt(0,0,z),1e-8);require(classifier.State()==TopAbs_OUT,"Cap nut bore is obstructed");}
                classifier.Perform(gp_Pnt(0,0,(depth+h)/2),1e-8);require(classifier.State()==TopAbs_IN,"Cap nut blind bore broke through its end wall");
                classifier.Perform(gp_Pnt(0,0,h+.1),1e-8);require(classifier.State()==TopAbs_OUT,"Cap nut crown exceeds its height");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);
                const double volume=expected.at("independent_volume_mm3");
                require(std::abs(properties.Mass()-volume)<=std::max(1e-6,volume*1e-8),"Cap nut independent section volume");
                require(std::abs(result.volume-volume)<=std::max(1e-6,volume*1e-8),"Cap nut persisted volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Cap nut derived BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Cap nut seating plane");near(z1,h,"Cap nut height");int crowns=0,flats=0;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
                    if(flat_cap) {
                        if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Location().Z()-h)<1e-7&&
                            std::abs(surface.Plane().Axis().Direction().Z())>1-1e-8) {
                            GProp_GProps face;BRepGProp::SurfaceProperties(it.Current(),face);
                            const double k=expected.at("flat_diameter_mm");near(face.Mass(),std::numbers::pi*k*k/4,"Closed nut flat diameter");++flats;
                        }
                        if(surface.GetType()!=GeomAbs_Cone||std::abs(std::abs(surface.Cone().SemiAngle())*180/std::numbers::pi-
                            expected.at("crown_angle_degrees").get<double>())>1e-7)continue;
                        ++crowns;
                        for(const double u:{.25,.5,.75})for(const double v:{.25,.5,.75}) {
                            const auto p=surface.Value(surface.FirstUParameter()+u*(surface.LastUParameter()-surface.FirstUParameter()),
                                surface.FirstVParameter()+v*(surface.LastVParameter()-surface.FirstVParameter()));
                            near(std::hypot(p.X(),p.Y()),expected.at("flat_diameter_mm").get<double>()/2+(h-p.Z())*
                                std::tan(expected.at("crown_angle_degrees").get<double>()*std::numbers::pi/180),"Closed nut cone equation");
                        }
                        continue;
                    }
                    if(surface.GetType()!=GeomAbs_Sphere&&surface.GetType()!=GeomAbs_SurfaceOfRevolution)continue;
                    ++crowns;
                    for(const double u:{.25,.5,.75})for(const double v:{.25,.5,.75}) {
                        const auto p=surface.Value(surface.FirstUParameter()+u*(surface.LastUParameter()-surface.FirstUParameter()),
                            surface.FirstVParameter()+v*(surface.LastVParameter()-surface.FirstVParameter()));
                        near(p.X()*p.X()+p.Y()*p.Y()+std::pow(p.Z()-h+radius,2),radius*radius,"Cap nut crown sphere equation");
                    }
                }
                require(crowns>0,"Cap nut crown missing");require(!flat_cap||flats==1,"Closed nut top flat missing");std::size_t retained=0;
                auto axial_section=document::create_section();
                static_cast<void>(axial_section.sketch.add_segment(0,-2*radius,0,2*radius));
                for(const bool reverse:{false,true}) {
                    axial_section.reversed=reverse;
                    for(const auto& patch:document::calculate_section(result.mesh,axial_section).patches)
                        for(const double angle:{45.,135.})for(const double scale:{.5,1.,2.})
                            for(const auto& edge:document::section_hatch_lines(patch,patch.frame,{angle,1,.25,0},scale))
                                for(const double t:{.125,.5,.875}) {
                                    const auto a=edge.points.front(),b=edge.points.back();
                                    const gp_Pnt sample(a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z));
                                    classifier.Perform(sample,1e-7);
                                    if(classifier.State()==TopAbs_OUT) {
                                        // Sections consume the display triangulation. At
                                        // a concave bore/chamfer its chord lies inside
                                        // the analytic cavity by up to the existing
                                        // native mesher's 0.1 mm deflection. Check this
                                        // boundary independently; never allow an open
                                        // runout sheet to hatch across the cavity.
                                        BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(sample).Vertex(),shape);
                                        require(distance.IsDone()&&distance.Value()<=.1+1e-7,
                                            "Cap nut section hatched beyond native mesh deflection outside BRep material");
                                    }
                                }
                }
                drawing::DrawingView projection;const double q=std::sqrt(.5);
                projection.camera={{1,0,0},{0,-q,q},{0,q,q}};drawing::refresh_view_geometry(projection,result.mesh);
                for(const auto& edge:projection.projected_edges)if(!flat_cap&&edge.silhouette&&!edge.hidden)
                    for(std::size_t i=1;i<edge.points.size();++i) {
                        const auto a=edge.points[i-1],b=edge.points[i];
                        const double x=(a.x+b.x)/2,y=(a.y+b.y)/2;
                        if(expected.contains("hex_height_mm")) {
                            // A full hemisphere's real outer contour passes
                            // near the projected pole. The shallow historical
                            // cap's empty-pole-neighbourhood gate is not valid
                            // here. Check the actual projected sphere outline;
                            // exact BRep sphere equations remain checked above.
                            if(std::hypot(x,y-h*q)<expected.at("across_flats_mm").get<double>()*.25)
                                require(std::abs(std::hypot(x,y-(h-radius)*q)-radius)<radius*.02,
                                    "DIN crown silhouette leaves its projected sphere outline");
                        } else require(std::hypot(x,y-h*q)>=expected.at("across_flats_mm").get<double>()*.25,
                            "A collapsed pole triangle invented a contour inside the cap crown");
                    }
                for(std::size_t i=0;i<result.mesh.triangle_references.size();++i)if(result.mesh.triangle_references[i].semantic_key.starts_with("thread:surface:")) {
                    const double extent=expected.at("thread_length_mm").get<double>()+
                        expected.value("runout_pitch_factor",0.)*expected.at("pitch_mm").get<double>();
                    require(extent<=depth+1e-6,"Cap nut run-out exceeds its blind bore");
                    ++retained;for(int j=0;j<3;++j)require(result.mesh.vertices.at(result.mesh.triangles.at(i*3+j)).z<=extent+1e-6,
                        "Cap nut thread display exceeds its specified length and run-out");
                }
                require(retained>0,"Cap nut lost its internal thread display");
                if(expected.value("diagnostic_mesh",false)) {
                    Json packet={{"vertices",Json::array()},{"triangles",result.mesh.triangles},{"faces",Json::array()},{"edges",Json::array()}};
                    auto section=document::create_section();
                    static_cast<void>(section.sketch.add_segment(0,-20,0,20));
                    packet["section_patches"]=Json::array();
                    for(const auto& patch:document::calculate_section(result.mesh,section).patches) {
                        Json boundary=Json::array(),hatches=Json::array();
                        for(const auto& edge:patch.boundary)boundary.push_back({{edge[0].x,edge[0].y,edge[0].z},{edge[1].x,edge[1].y,edge[1].z}});
                        for(const auto& edge:document::section_hatch_lines(patch,patch.frame,{45,2,0,0},42/(2*expected.at("across_flats_mm").get<double>()/std::sqrt(3))))
                            hatches.push_back({{edge.points[0].x,edge.points[0].y,edge.points[0].z},{edge.points[1].x,edge.points[1].y,edge.points[1].z}});
                        packet["section_patches"].push_back({{"boundary",boundary},{"hatches",hatches}});
                    }
                    for(const auto& p:result.mesh.vertices)packet["vertices"].push_back({p.x,p.y,p.z});
                    for(const auto& ref:result.mesh.triangle_references)packet["faces"].push_back({ref.owner_id,ref.semantic_key,ref.instance_path});
                    for(const auto& edge:result.mesh.edges) {
                        Json points=Json::array();for(const auto& p:edge.points)points.push_back({p.x,p.y,p.z});
                        packet["edges"].push_back({{"points",std::move(points)},{"key",edge.reference.semantic_key},{"seam",edge.parameter_seam}});
                    }
                    drawing::DrawingView view;const double q=std::sqrt(.5);view.camera={{1,0,0},{0,-q,q},{0,q,q}};
                    drawing::refresh_view_geometry(view,result.mesh);packet["projected_edges"]=Json::array();
                    for(const auto& edge:view.projected_edges) {
                        Json points=Json::array();for(const auto& p:edge.points)points.push_back({p.x,p.y});
                        packet["projected_edges"].push_back({{"points",std::move(points)},{"key",edge.source.semantic_key},
                            {"hidden",edge.hidden},{"silhouette",edge.silhouette},{"thread",edge.thread}});
                    }
                    std::ofstream output(folder/"cap-mesh-diagnostic.json");output<<packet.dump();
                }
                for(const auto& profile:doc.sketches)verify_profile_dragging(profile);
            }
            std::cout<<manifest.at("variants").size()<<" cap nuts: valid solid, blind bore/end wall, specified crown, independent volume/mass, thread extent and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--knurled-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/fs::u8path(expected.at("filename").get<std::string>())));
                const auto* state=live.open_part(id);require(state,"Knurled nut is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Knurled nut calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid knurled nut BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Knurled nut must contain one solid");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);
                near(properties.Mass(),expected.at("independent_volume_mm3"),"Knurled nut independent section volume");
                near(result.volume,expected.at("independent_volume_mm3"),"Knurled nut persisted volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-
                    expected.at("independent_volume_mm3").get<double>()*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Knurled nut derived BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Knurled nut seating plane");near(z1,expected.at("height_mm"),"Knurled nut height");
                int flanks=0,roots=0;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
                    if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().Z())<1e-8)++flanks;
                    if(surface.GetType()==GeomAbs_Torus) {
                        near(surface.Torus().MinorRadius(),expected.at("root_radius_mm"),"Knurled nut shoulder radius");++roots;
                    }
                }
                const int teeth=static_cast<int>(std::round(std::acos(-1.)*expected.at("outside_mm").get<double>()/expected.at("knurl_pitch_mm").get<double>()));
                require(flanks==2*teeth,"Knurled nut must retain two real planar flanks per groove");
                require(roots>0,"Knurled nut lost its rounded shoulder");
                std::size_t retained=0;const auto& mesh=result.mesh;const double floor=expected.at("clearance_depth_mm");
                for(std::size_t i=0;i<mesh.triangle_references.size();++i)if(mesh.triangle_references[i].semantic_key.starts_with("thread:surface:")) {
                    ++retained;for(int j=0;j<3;++j)require(mesh.vertices.at(mesh.triangles.at(i*3+j)).z>=floor-1e-6,"Thread display enters unthreaded hub clearance");
                }
                require(retained>0,"Knurled nut lost its internal thread display");
                for(const auto& profile:doc.sketches)verify_profile_dragging(profile);
            }
            std::cout<<manifest.at("variants").size()<<" knurled nuts: valid solid, independent volume/mass, real groove flanks, shoulder radius, trimmed thread and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--castle-nuts") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/fs::u8path(expected.at("filename").get<std::string>())));
                const auto* state=live.open_part(id);require(state,"Castle nut is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Castle nut calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid castle nut BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Castle nut must contain one solid");
                const double m=expected.at("height_mm"),h=expected.at("slot_floor_mm"),radius=expected.at("slot_width_mm").get<double>()/2;
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-12);
                near(properties.Mass(),expected.at("independent_volume_mm3"),"Castle nut independent section volume");
                near(result.volume,expected.at("independent_volume_mm3"),"Castle nut persisted volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-
                    expected.at("independent_volume_mm3").get<double>()*expected.at("density_kg_mm3").get<double>())<=.50001e-6,
                    "Castle nut derived BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Castle nut seating plane");near(z1,m,"Castle nut height");
                int roots=0;for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()!=GeomAbs_Cylinder)continue;
                    const auto cylinder=surface.Cylinder();if(std::abs(cylinder.Axis().Direction().Z())>1e-7)continue;
                    near(cylinder.Radius(),radius,"Castle nut slot-root radius");near(cylinder.Location().Z(),h+radius,"Castle nut slot-root height");++roots;
                }
                require(roots>=6,"Castle nut lost one of its six round-root slots");
                const auto in_gap=[&](const auto& p) {
                    if(p.z<=h+1e-6)return false;
                    for(int i=0;i<3;++i) {
                        const double angle=i*std::acos(-1.)/3;
                        const double cross=p.x*std::cos(angle)+p.y*std::sin(angle);
                        if(std::abs(cross)>=radius-1e-6)continue;
                        const double floor=h+radius-std::sqrt(std::max(0.,radius*radius-cross*cross));
                        if(p.z>floor+1e-6)return true;
                    }return false;
                };
                std::size_t retained=0;const auto& mesh=result.mesh;
                for(std::size_t i=0;i<mesh.triangle_references.size();++i)if(mesh.triangle_references[i].semantic_key.starts_with("thread:surface:")) {
                    ++retained;for(int j=0;j<3;++j)require(!in_gap(mesh.vertices.at(mesh.triangles.at(i*3+j))),"Thread display surface bridges a castle slot");
                }
                require(retained>0,"Castle slots erased the complete thread display");
                for(const auto& edge:mesh.edges)if(edge.reference.semantic_key.starts_with("thread:"))
                    for(std::size_t i=1;i<edge.points.size();++i) {
                        const auto& a=edge.points[i-1];const auto& b=edge.points[i];
                        const kernel::Vec3 mid{(a.x+b.x)/2,(a.y+b.y)/2,(a.z+b.z)/2};
                        require(!in_gap(a)&&!in_gap(b),"Thread display edge vertex enters a castle slot");
                        // A tessellated circular boundary's chord cuts inside
                        // its exact arc. Check the midpoint only in the straight
                        // slot region, where no chord-deflection allowance is needed.
                        if(mid.z>h+radius+1e-6)
                            require(!in_gap(mid),"Thread display edge bridges a castle slot");
                    }
                for(const auto& profile:doc.sketches)verify_profile_dragging(profile);
            }
            std::cout<<manifest.at("variants").size()<<" castle nuts: valid solid, independent volume, six round roots, trimmed thread surfaces/edges and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--tab-washers") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                workspace::Workspace live;
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(folder/fs::u8path(expected.at("filename").get<std::string>())));
                const auto* state=live.open_part(id);require(state,"Tab washer is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Tab washer calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid tab washer BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Tab washer must contain one solid");
                const double r=expected.at("outside_mm").get<double>()/2,f=expected.at("root_radius_mm"),b=expected.at("tab_width_mm"),l=expected.at("tab_length_mm"),t=expected.at("thickness_mm"),d=expected.at("bore_mm");
                const double cy=b/2+f,cx=-std::sqrt((r+f)*(r+f)-cy*cy),px=cx*r/(r+f);
                const auto integral=[](double u,double radius){return (u*std::sqrt(radius*radius-u*u)+radius*radius*std::asin(u/radius))/2;};
                const double area=b*(l+cx)+2*cy*(px-cx)-2*integral(px-cx,f)+std::acos(-1.)*r*r/2-2*integral(px,r);
                const double volume=t*(area-std::acos(-1.)*d*d/4);
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-10);
                near(properties.Mass(),volume,"Tab washer analytical volume");near(result.volume,volume,"Native tab washer volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Tab washer BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(x0,-l,"Tab washer tab end");near(x1,r,"Tab washer outer radius");near(y0,-r,"Tab washer lower radius");near(y1,r,"Tab washer upper radius");near(z0,0,"Tab washer seating plane");near(z1,t,"Tab washer thickness");
                require(doc.history.size()==1&&doc.sketches.size()==1,"Tab washer must retain its single authored extrusion");
                const auto& profile=doc.sketches.front();
                require(profile.arcs.size()==3&&profile.circles.size()==1,"Tab washer outline must retain three exact arcs and its bore");
                verify_profile_dragging(profile);
                require(!state->session.is_dirty()&&!state->session.can_undo(),"Tab washer inspection changed native state");
            }
            std::cout<<manifest.at("variants").size()<<" tab washers: valid solid, independent tangent-outline volume, seating bounds, mass and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--plain-washers") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());workspace::Workspace live;
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Plain washer is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Plain washer calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid plain washer BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;require(solids==1,"Plain washer must contain one solid");
                const double bore=expected.at("bore_mm"),outside=expected.at("outside_mm"),t=expected.at("thickness_mm"),c=expected.at("chamfer_mm");
                const double volume=std::acos(-1.)*((outside*outside-bore*bore)*t/4-c*c*(outside/2-c/3));
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-10);
                near(properties.Mass(),volume,"Plain washer analytical volume");near(result.volume,volume,"Native plain washer volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Plain washer BOM mass");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Plain washer seating plane");near(z1,t,"Plain washer thickness");
                near(x0,-outside/2,"Plain washer negative radius");near(x1,outside/2,"Plain washer positive radius");
                near(y0,-outside/2,"Plain washer lower radius");near(y1,outside/2,"Plain washer upper radius");
                require(doc.sketches.size()==1&&doc.history.size()==(c>0?2u:1u),"Plain washer authored history differs from annulus and optional chamfer");
                const auto& annulus=doc.history.front();
                require(annulus.feature_kind==document::FeatureKind::Feature&&annulus.feature.sides[0].operation==document::FeatureSideOperation::Extrusion&&
                    annulus.feature.sides[1].operation==document::FeatureSideOperation::None,"Plain washer must extrude one-sided from the seating plane");
                auto profile=doc.sketches.front();require(profile.circles.size()==2&&profile.circles[0].center_point_id==profile.circles[1].center_point_id,"Plain washer lost its shared native center");
                const auto* center=profile.find_point(profile.circles[0].center_point_id);near(center->x,0,"Plain washer origin X");near(center->y,0,"Plain washer origin Y");
                std::vector<double> diameters;for(const auto& circle:profile.circles)diameters.push_back(2*circle.radius);std::ranges::sort(diameters);
                near(diameters[0],bore,"Plain washer bore diameter");near(diameters[1],outside,"Plain washer outside diameter");
                verify_profile_dragging(profile);
            }
            std::cout<<manifest.at("variants").size()<<" plain washers: valid solids, shared origin, one-sided thickness, exact chamfer volume, mass and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--spring-washers") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));
            Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());
                workspace::Workspace live;
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Spring washer is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Spring washer calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid spring washer BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                require(solids==1,"Compressed spring washer must remain one connected solid");
                const double bore=expected.at("bore_mm"),outside=expected.at("outside_mm"),thickness=expected.at("thickness_mm");
                const double width=expected.at("width_mm"),round=expected.at("corner_radius_mm"),gap=expected.at("split_gap_mm");
                near(expected.at("split_angle_degrees"),45,"Axial spring washer cut angle");
                near(outside-bore,2*width,"Spring washer radial section width");
                require(expected.at("state")=="compressed","Spring washer default state is not compressed");
                require(expected.at("free_height_mm").get<double>()>thickness,"Free and compressed heights were confused");
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                near(z0,0,"Spring washer seating plane");near(z1,thickness,"Compressed spring washer thickness");
                near(x0,-outside/2,"Spring washer negative radius");near(y0,-outside/2,"Spring washer lower radius");near(x1,outside/2,"Spring washer positive radius");
                require(y1>0&&y1<=outside/2+1e-7,"Axial split has invalid radial bounds");
                std::vector<double> cut_planes;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()!=GeomAbs_Plane)continue;
                    const auto plane=surface.Plane();auto n=plane.Axis().Direction();
                    if(std::abs(n.Y())<1e-8&&std::abs(std::abs(n.X())-std::sqrt(.5))<1e-8&&std::abs(std::abs(n.Z())-std::sqrt(.5))<1e-8) {
                        if(n.X()<0)n.Reverse();require(n.Z()>0,"Axial cut planes have the wrong inclination");
                        cut_planes.push_back(n.X()*plane.Location().X()+n.Z()*plane.Location().Z());
                    }
                }
                require(cut_planes.size()==2,"Axial split must have two real parallel 45-degree walls");
                near(std::abs(cut_planes[0]-cut_planes[1]),gap,"Axial split perpendicular gap");
                // Integrate a straight strip through the exact rounded annular
                // sections independently from OCCT and the authored Sketch.
                const double section=width*thickness-(4-std::acos(-1.))*round*round;
                const double uncut=2*std::acos(-1.)*(outside+bore)/4*section;
                const auto removed=[&](double z) {
                    const double edge=std::min(z,thickness-z);
                    const double delta=edge<round?round-std::sqrt(std::max(0.,round*round-(round-edge)*(round-edge))):0;
                    const double ri=bore/2+delta,ro=outside/2-delta,c=thickness/2-z,h=gap/std::sqrt(2.);
                    require(std::max(std::abs(c-h),std::abs(c+h))<ri,"Axial cutter starts outside the bore");
                    const auto primitive=[](double r,double x){return .5*(x*std::sqrt(r*r-x*x)+r*r*std::asin(x/r));};
                    return primitive(ro,c+h)-primitive(ro,c-h)-primitive(ri,c+h)+primitive(ri,c-h);
                };
                std::function<double(double,double,double,double,double,double,double,int)> integrate;
                integrate=[&](double a,double b,double fa,double fm,double fb,double whole,double eps,int depth) {
                    const double m=(a+b)/2,fl=removed((a+m)/2),fr=removed((m+b)/2);
                    const double left=(m-a)*(fa+4*fl+fm)/6,right=(b-m)*(fm+4*fr+fb)/6,delta=left+right-whole;
                    if(depth==0||std::abs(delta)<15*eps)return left+right+delta/15;
                    return integrate(a,m,fa,fl,fm,left,eps/2,depth-1)+integrate(m,b,fm,fr,fb,right,eps/2,depth-1);
                };
                double cut_volume=0;const std::array<double,4> levels{0,round,thickness-round,thickness};
                for(std::size_t i=1;i<levels.size();++i) {
                    const double a=levels[i-1],b=levels[i];if(b<=a)continue;
                    const double fa=removed(a),fm=removed((a+b)/2),fb=removed(b);
                    cut_volume+=integrate(a,b,fa,fm,fb,(b-a)*(fa+4*fm+fb)/6,uncut*1e-11,30);
                }
                const double volume=uncut-cut_volume;
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1e-10);
                near(properties.Mass(),volume,"Rounded split washer analytical volume");
                require(std::abs(result.volume-volume)<=1e-8*std::max(1.,volume),"Native rounded split washer volume lost integration precision");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-volume*expected.at("density_kg_mm3").get<double>())<=.50001e-6,"Spring washer BOM mass");
                require(doc.history.size()==3&&doc.sketches.size()==2,"Spring washer editable history differs from annulus, round and split");
                const auto& annulus=doc.history.front();
                require(annulus.feature_kind==document::FeatureKind::Feature&&annulus.feature.sides[0].operation==document::FeatureSideOperation::Extrusion&&
                    annulus.feature.sides[1].operation==document::FeatureSideOperation::None,"Washer must extrude one-sided from the seating plane");
                require(doc.history.back().combine_mode==document::CombineMode::Subtract&&
                    doc.history.back().feature.sides[0].extrusion_extent==document::EndCondition::ThroughAll,"Spring washer split must be a native Through All cut");
                for(auto sketch:doc.sketches) {
                    const auto solved=sketch.solve();require(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Spring washer Sketch is not fully constrained");
                    verify_profile_dragging(sketch);
                }
                const auto& profile=doc.sketches.front();
                require(profile.circles.size()==2&&profile.circles[0].center_point_id==profile.circles[1].center_point_id,"Washer circles must share their native center point");
                const auto* center=profile.find_point(profile.circles[0].center_point_id);
                near(center->x,0,"Spring washer origin X");near(center->y,0,"Spring washer origin Y");
                const auto& split=doc.sketches.back();
                require(split.external_references.size()==2,"Spring washer split must reference both actual end planes");
                std::vector<double> support_heights;
                for(const auto& support:split.external_references) {
                    require(!support.broken&&support.kind==sketcher::ExternalReferenceKind::Face&&support.source_owner_id==annulus.id,
                        "Spring washer split lost its original extrusion end-plane identity");
                    require(std::ranges::count_if(split.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnLine&&c.geometry_id==support.id;})==2,
                        "Both cutter corners must contact each external end plane");
                    const auto line=sketcher::external_reference_line(support);require(line.has_value(),"End plane has no axial reference line");
                    near(line->second[1],0,"Axial end reference is not horizontal");support_heights.push_back(line->first[1]);
                }
                std::ranges::sort(support_heights);near(support_heights[0],-thickness,"External upper end plane");near(support_heights[1],0,"External seating end plane");
            }
            std::cout<<manifest.at("variants").size()<<" compressed spring washers: valid solids, seating origin, exact rounded-section volume, native split, mass and constrained dragging passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--caps") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));
            Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());
                workspace::Workspace live;
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Cap screw is not a Part");
                const auto& doc=state->session.document();const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Cap screw Body calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid cap screw BRep");
                int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                require(solids==1,("Cap screw must contain one connected solid: "+document::path_to_utf8(path.filename())+" ("+std::to_string(solids)+" solids)").c_str());
                const double d=expected.at("diameter_mm"),l=expected.at("length_mm"),k=expected.at("head_height_mm");
                const double radius=expected.at("head_diameter_mm").get<double>()/2;
                const double width=expected.at("drive_width_mm"),depth=expected.at("drive_depth_mm");
                const bool entry=expected.contains("socket_entry_radius_mm")&&!expected.at("socket_entry_radius_mm").is_null();
                const bool lobed=expected.contains("socket_no");
                const bool pilot=expected.contains("dh");
                double re=0,ri=0,outer_center=0,inner_center=0;
                if(lobed) {
                    const double A=expected.at("A"),B=expected.at("B"),b=B/2,cs=std::cos(std::acos(-1.)/6);
                    re=.1*A;outer_center=A/2-re;
                    ri=(re*re-outer_center*outer_center-b*b+2*outer_center*b*cs)/(2*b-2*outer_center*cs-2*re);inner_center=b+ri;
                }
                Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                near(x0,-radius,"Cap screw left");near(x1,radius,"Cap screw right");near(y0,-radius,"Cap screw lower");near(y1,radius,"Cap screw upper");
                near(z0,-k,"Cap screw head height");near(z1,l,"Cap screw length");
                int walls=0,lobe_outer=0,lobe_inner=0;bool pilot_wall=false,pilot_floor=false;
                bool floor=false,shaft=false,round=false,head_round=false,tip=false,entry_cone=false;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
                    if(surface.GetType()==GeomAbs_Plane) {
                        const auto plane=surface.Plane();const auto normal=plane.Axis().Direction();
                        if(std::abs(normal.Z())>1-1e-8&&std::abs(plane.Location().Z()+k-depth)<1e-6)floor=true;
                        if(pilot&&std::abs(normal.Z())>1-1e-8&&std::abs(plane.Location().Z()+k-expected.at("t2").get<double>())<1e-6)pilot_floor=true;
                        if(std::abs(normal.Z())<1e-8&&std::abs(plane.Distance(gp_Pnt(0,0,0))-width/2)<1e-6)++walls;
                    } else if(surface.GetType()==GeomAbs_Cylinder) {
                        const auto cylinder=surface.Cylinder();const double r=cylinder.Radius();
                        shaft|=std::abs(r-d/2)<1e-6;
                        const double center=std::hypot(cylinder.Location().X(),cylinder.Location().Y());
                        if(pilot&&center<1e-7&&std::abs(r-expected.at("dh").get<double>()/2)<1e-7)pilot_wall=true;
                        if(lobed&&std::abs(r-re)<1e-7){near(center,outer_center,"Hexalobular convex centre");++lobe_outer;}
                        if(lobed&&std::abs(r-ri)<1e-7){near(center,inner_center,"Hexalobular concave centre");++lobe_inner;}
                    }
                    else if(surface.GetType()==GeomAbs_Torus) {
                        round|=std::abs(surface.Torus().MinorRadius()-expected.at("underhead_radius_mm").get<double>())<1e-6;
                        head_round|=std::abs(surface.Torus().MinorRadius()-expected.at("head_edge_radius_mm").get<double>())<1e-6;
                    } else if(surface.GetType()==GeomAbs_Sphere&&expected.value("head_profile",std::string{})=="spherical_crown") {
                        near(surface.Sphere().Radius(),expected.at("head_edge_radius_mm"),"Button crown radius");
                        near(surface.Sphere().Location().Z(),expected.at("crown_center_z_mm"),"Button crown center");
                        head_round=true;
                    } else if(surface.GetType()==GeomAbs_Cone) {
                        tip|=surface.Cone().Location().Z()>0;
                        entry_cone|=surface.Cone().Location().Z()<0&&std::abs(std::abs(surface.Cone().SemiAngle())-std::acos(-1.)/4)<1e-7;
                    }
                }
                require(floor,"Drive floor is missing or at the wrong offset");
                require(k-depth>=expected.value("head_floor_min_mm",0.)-1e-8,"Socket violates minimum head-floor thickness");
                if(lobed)require(lobe_outer==6&&lobe_inner==6,"Hexalobular drive must have six convex and six concave exact circular walls");
                else require(walls==(expected.at("socket").get<bool>()?6:2),"Incorrect drive wall count or across-flats width");
                if(pilot)require(pilot_wall&&pilot_floor,"DIN 6912 pilot diameter or total depth missing");
                if(expected.contains("socket_depth_max_mm"))require(depth<=expected.at("socket_depth_max_mm").get<double>()+1e-8,"Button socket exceeds maximum depth");
                require(shaft&&round&&head_round&&tip,"Shaft, head rounding, underhead radius or tip chamfer missing");
                const auto opening_it=std::ranges::find_if(doc.history,[&](const auto& feature){return feature.feature_kind==document::FeatureKind::Feature&&feature.combine_mode==document::CombineMode::Subtract&&
                    feature.feature.sides[1].operation==document::FeatureSideOperation::Extrusion&&std::abs(feature.feature.profile_plane_offset+k-depth)<1e-7;});
                require(opening_it!=doc.history.end(),"Drive Default not found");const auto& opening=*opening_it;
                require(opening.feature_kind==document::FeatureKind::Feature&&opening.combine_mode==document::CombineMode::Subtract,"Drive must be an ordinary subtractive Default");
                near(opening.feature.profile_plane_offset,-k+depth,"Drive Sketch plane offset");
                require(opening.feature.sides[0].operation==document::FeatureSideOperation::None&&
                    opening.feature.sides[1].extrusion_extent==document::EndCondition::ThroughAll,"Drive must cut through the head in the negative direction only");
                if(entry) {
                    require(entry_cone,"Socket entrance has no real 45-degree conical cut");
                    const auto& mouth=doc.history.back();
                    require(mouth.feature_kind==document::FeatureKind::Feature&&mouth.combine_mode==document::CombineMode::Subtract&&
                        mouth.feature.sides[0].operation==document::FeatureSideOperation::Revolution&&mouth.feature.sides[0].rotation_extent==document::FeatureRotationExtent::Full,
                        "Socket entrance must use a current full-revolution subtractive Default");
                    if(expected.value("socket_entry_kind",Json{})=="geometry_corner") {
                        const auto sk=std::ranges::find(doc.sketches,mouth.feature.sketch_id,&sketcher::Sketch::id);
                        require(sk!=doc.sketches.end()&&!sk->external_references.empty(),"Socket entrance lost actual rim reference");
                        const auto axis=std::ranges::find(sk->segments,mouth.feature.axis_segment_id,&sketcher::SketchSegment::id);
                        require(axis!=sk->segments.end(),"Socket entrance axis missing");
                        require(std::ranges::any_of(sk->segments,[&](const auto& segment){return !segment.construction&&(segment.first_point_id==axis->second_point_id||segment.second_point_id==axis->second_point_id);}),"Socket cutter and axis do not share their top point");
                        for(const auto& segment:sk->segments)if(!segment.construction)for(const auto& id:{segment.first_point_id,segment.second_point_id})
                            require(sk->find_point(id)->y>=k-depth-1e-7,"Socket cutter deepens the existing broach floor");
                    }else near(depth-(expected.at("socket_entry_radius_mm").get<double>()-width/2),expected.at("socket_effective_depth_mm"),"Socket effective key depth after entrance cut");
                }
                for(auto sketch:doc.sketches) {
                    const auto solved=sketch.solve();require(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Cap screw Sketch is not fully constrained");
                    verify_profile_dragging(sketch);
                }
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-properties.Mass()*expected.value("density_kg_mm3",7.85e-6))<=.50001e-6,"Cap screw BOM mass");
                const auto thread_it=std::ranges::find_if(doc.history,[](const auto& feature){return feature.feature_kind==document::FeatureKind::ShaftThread;});
                require(thread_it!=doc.history.end(),"Shaft thread missing");const auto& thread=thread_it->shaft_thread;
                near(thread.nominal_diameter,d,"Cap screw thread diameter");near(thread.pitch,expected.at("pitch_mm"),"Cap screw pitch");
                near(thread.length,expected.at("thread_length_mm"),"Cap screw thread length");
                require(thread.length+2*thread.pitch+expected.at("underhead_radius_mm").get<double>()<=l+1e-8,"Thread enters the head or underhead fillet");
            }
            std::cout<<manifest.at("variants").size()<<" cap screws: valid solids, exact dimensions, rounded heads, real drive walls/floors, constrained Sketches, mass and thread limits passed\n";return 0;
        }
        if(argc==3&&std::string(argv[1])=="--studs") {
            const auto folder=fs::absolute(fs::u8path(argv[2]));
            Json manifest;std::ifstream input(folder/"manifest.json");input>>manifest;
            for(const auto& expected:manifest.at("variants")) {
                const auto path=folder/fs::u8path(expected.at("filename").get<std::string>());
                workspace::Workspace live;
                const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
                const auto* state=live.open_part(id);require(state,"Stud is not a Part");
                const auto& doc=state->session.document();
                const auto& boundary=state->session.calculated_boundaries().back();
                require(boundary.calculation_errors.empty()&&boundary.body_outputs.size()==1,"Stud Body calculation failed");
                const auto& result=boundary.body_outputs.begin()->second.get();
                TopoDS_Shape shape;BRep_Builder builder;std::istringstream data(result.kernel_shape);BRepTools::Read(shape,data,builder);
                require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid stud BRep");
                int solids=0,cones=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
                require(solids==1,"Stud must be a connected solid");
                const double radius=expected.at("diameter_mm").get<double>()/2;
                const double length=expected.at("total_length_mm"),cut=.5*expected.at("metal_pitch_mm").get<double>();
                Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                near(x0,-radius,"Stud left");near(x1,radius,"Stud right");near(y0,-radius,"Stud lower");near(y1,radius,"Stud upper");
                near(z0,-expected.at("length_mm").get<double>(),"Stud nominal end from seating origin");near(z1,expected.at("metal_length_mm"),"Stud metal end from seating origin");
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()==GeomAbs_Cone)++cones;
                }
                require(cones==2,"Stud must have two real tip chamfers");
                GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
                near(properties.Mass(),std::acos(-1.)*(radius*radius*length-2*(radius*cut*cut-cut*cut*cut/3)),"Stud exact chamfered volume");
                require(std::abs(std::stod(doc.user_parameter_values.at("mass").at(""))-properties.Mass()*7.85e-6)<=.50001e-6,"Stud BOM mass");
                require(doc.history.size()==4&&doc.sketches.size()==1,"Stud authored history must be extrusion, chamfer and two threads");
                require(!doc.history.front().feature.symmetric,"Stud extrusion must have independent nominal/metal sides");
                near(doc.history.front().feature.sides[0].length,expected.at("metal_length_mm"),"Stud first metal side");
                near(doc.history.front().feature.sides[1].length,expected.at("length_mm"),"Stud second nominal side");
                auto sketch=doc.sketches.front();const auto solved=sketch.solve();
                require(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Stud profile is not anchored and dimensioned");
                verify_profile_dragging(sketch);
                near(sketch.points.front().x,0,"Stud center X");near(sketch.points.front().y,0,"Stud center Y");
                for(std::size_t i=0;i<2;++i) {
                    const auto& thread=doc.history.at(i+2).shaft_thread;
                    near(thread.nominal_diameter,2*radius,"Stud thread diameter");
                    near(thread.pitch,expected.at(i==0?"metal_pitch_mm":"pitch_mm"),"Stud end pitch");
                    near(thread.length,expected.at(i==0?"metal_length_mm":"thread_length_mm"),"Stud end thread length");
                    require(thread.end_condition==document::EndCondition::Length&&thread.runout_enabled,"Stud thread limit/runout");
                }
            }
            std::cout<<manifest.at("variants").size()<<" studs: valid BReps, independent l/e extents, analytical volumes/masses, origin constraints and two independent thread bands passed\n";
            return 0;
        }
        if(argc==4&&std::string(argv[1])=="--audit-projected-faces") {
            std::ifstream input(fs::u8path(argv[2]));Json inventory;input>>inventory;
            const auto output=fs::absolute(fs::u8path(argv[3]));fs::create_directories(output);
            kernel::OcctKernel kernel;Json report=Json::array();
            for(const auto& row:inventory) {
                if(row.at("face_references").empty())continue;
                const auto relative=fs::u8path(row.at("file").get<std::string>());
                std::vector<kernel::BodyResult> previous;auto doc=document::PartDocument::load(relative,&previous);
                const auto before=doc;
                // Factory conversion must execute this runtime, not reuse saved boundaries.
                auto calculated=workspace::calculate_part_with_resolved_references(kernel,doc,nullptr);
                require(!calculated.empty()&&calculated.back().calculation_errors.empty()&&doc.reference_errors.empty(),
                    "Projected factory references failed regeneration");
                const auto authored=[](auto&& self,Json value)->Json {
                    if(value.is_object()) {
                        if(value.contains("external_references"))for(auto& ref:value["external_references"])
                            if(ref.value("kind",std::string{})=="face") {
                                ref.erase("cached_points");ref.erase("cached_paths");ref.erase("infinite");
                            }
                        for(auto it=value.begin();it!=value.end();++it) {
                            if(it.key()=="sketch_serialized"&&it.value().is_string())
                                it.value()=self(self,Json::parse(it.value().get<std::string>())).dump();
                            else it.value()=self(self,std::move(it.value()));
                        }
                    } else if(value.is_array())for(auto& item:value)item=self(self,std::move(item));
                    return value;
                };
                if(authored(authored,doc.serialized().at("history"))!=authored(authored,before.serialized().at("history")) ||
                    doc.body_history.serialized()!=before.body_history.serialized()) {
                    std::ofstream(output/"history-before.json")<<before.serialized().dump(2);
                    std::ofstream(output/"history-after.json")<<doc.serialized().dump(2);
                    throw std::runtime_error("Projected factory history difference: "+document::path_to_utf8(relative));
                }
                require(!previous.empty()&&previous.back().body_outputs.size()==calculated.back().body_outputs.size(),
                    "Projected factory regeneration changed its Body outputs");
                for(const auto& [id,old_value]:previous.back().body_outputs) {
                    const auto found=calculated.back().body_outputs.find(id);
                    require(found!=calculated.back().body_outputs.end(),"Projected factory regeneration lost a Body identity");
                    const auto& old_body=old_value.get();const auto& body=found->second.get();
                    const auto properties=[](const auto& value) {
                        if(value.kernel_shape.empty())return std::array{0.,0.};
                        TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(value.kernel_shape);BRepTools::Read(shape,stream,builder);
                        require(!shape.IsNull(),"Factory Body BRep is missing");
                        GProp_GProps volume,area;BRepGProp::VolumeProperties(shape,volume);BRepGProp::SurfaceProperties(shape,area);
                        return std::array{volume.Mass(),area.Mass()};
                    };
                    const auto old=properties(old_body),now=properties(body);
                    near(now[0],old[0],"Projected factory references changed volume");
                    near(now[1],old[1],"Projected factory references changed surface area");
                }
                require(doc.sketches.size()==before.sketches.size(),"Projected factory regeneration changed Sketch ownership");
                for(std::size_t i=0;i<doc.sketches.size();++i) {
                    const auto& old=before.sketches[i];const auto& sketch=doc.sketches[i];
                    require(sketch.id==old.id&&sketch.owner_container_id==old.owner_container_id&&sketch.constraints==old.constraints&&
                        sketch.external_references.size()==old.external_references.size(),"Projected factory references changed native relation identity");
                    for(std::size_t j=0;j<sketch.external_references.size();++j) {
                        const auto& a=old.external_references[j];const auto& b=sketch.external_references[j];
                        require(!b.broken&&a.id==b.id&&a.kind==b.kind&&a.source_owner_id==b.source_owner_id&&
                            a.source_semantic_key==b.source_semantic_key&&a.source_instance_path==b.source_instance_path,
                            "Projected factory reference lost source identity");
                    }
                }
                const auto target=output/relative;fs::create_directories(target.parent_path());doc.save(target,calculated);
                std::vector<kernel::BodyResult> reopened;const auto loaded=document::PartDocument::load(target,&reopened);
                require(loaded.history==doc.history&&loaded.body_history.serialized()==doc.body_history.serialized()&&!reopened.empty(),
                    "Projected factory candidate failed save/reopen");
                report.push_back({{"file",row.at("file")},{"face_references",row.at("face_references").size()},
                    {"history",true},{"body_properties",true},{"save_reopen",true}});
                std::ofstream(output/"report.json")<<report.dump(2);
                std::cout<<document::path_to_utf8(relative)<<" verified\n";
            }
            std::cout<<"Projected factory candidates verified: "<<report.size()<<'\n';return 0;
        }
        if(argc==4&&std::string(argv[1])=="--audit-shaft-threads") {
            const auto root=fs::canonical(fs::u8path(argv[2]));
            const auto output=fs::absolute(fs::u8path(argv[3]));
            fs::create_directories(output);kernel::OcctKernel kernel;Json report=Json::array();
            for(const auto& entry:fs::recursive_directory_iterator(root)) {
                if(!entry.is_regular_file()||entry.path().extension()!=".prtz")continue;
                std::vector<kernel::BodyResult> saved;auto doc=document::PartDocument::load(entry.path(),&saved);
                if(std::ranges::none_of(doc.history,[](const auto& c){return c.feature_kind==document::FeatureKind::ShaftThread||
                    (c.feature_kind==document::FeatureKind::Thread&&c.thread.enabled)||
                    (c.feature_kind==document::FeatureKind::Hole&&c.hole.thread_enabled);} ))continue;
                const auto fresh=kernel.evaluate_history(doc.kernel_operations());
                require(!fresh.empty()&&fresh.back().calculation_errors.empty(),"Fastener regeneration failed");
                for(const auto& sketch:doc.sketches)for(const auto& reference:sketch.external_references)
                    require(reference.source_semantic_key!="thread:boundary:root"&&
                        reference.source_semantic_key!="thread:boundary:nominal"&&
                        reference.source_semantic_key!="thread:boundary:runout:end",
                        "Factory Sketch uses an ambiguous thread rim reference; reauthor it explicitly");
                Json threads=Json::array();
                for(const auto& container:doc.history) {
                    if(container.feature_kind!=document::FeatureKind::ShaftThread||container.suppressed)continue;
                    const auto request=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(
                        container,&fresh.back().mesh.original_references));
                    const auto check=[&](const auto& mesh) {
                        double low=1e30,high=-1e30,runout_end=-1e30;
                        for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
                            const auto& ref=mesh.triangle_references[i];if(ref.owner_id!=container.id)continue;
                            for(std::size_t j=0;j<3;++j) {
                                const auto& p=mesh.vertices.at(mesh.triangles.at(i*3+j));
                                const double x=p.x-request.origin.x,y=p.y-request.origin.y,z=p.z-request.origin.z;
                                const auto& a=request.axis_direction;const double t=x*a.x+y*a.y+z*a.z;
                                if(ref.semantic_key=="thread:surface:root") {
                                    near(std::sqrt(std::max(0.,x*x+y*y+z*z-t*t)),request.root_radius,"Thread root radius");
                                    low=std::min(low,t);high=std::max(high,t);
                                }
                                if(ref.semantic_key=="thread:surface:runout:end")runout_end=std::max(runout_end,t);
                            }
                        }
                        for(const auto& key:{std::string{"thread:surface:root"},std::string{"thread:surface:runout:end"}}) {
                            if(key.ends_with("end")&&request.runout_end<=1e-9)continue;
                            const auto found=std::ranges::find_if(mesh.original_references.triangle_references,[&](const auto& ref){
                                return ref.owner_id==container.id&&ref.semantic_key==key;});
                            require(found!=mesh.original_references.triangle_references.end()&&found->contour_surface&&!found->surface,
                                "Factory thread lost its exact outline-only surface metadata");
                        }
                        near(low,request.start_offset,"Thread start/entrance chamfer");
                        near(high,request.start_offset+request.length-request.runout_end,"Full thread end");
                        if(request.runout_end>1e-9)near(runout_end,request.start_offset+request.length,"Thread runout end");
                        else require(runout_end==-1e30,"Unexpected thread runout");
                    };
                    require(!saved.empty(),"Saved fastener geometry missing");check(saved.back().mesh);check(fresh.back().mesh);
                    for(const auto* end:{"start","end"}) {
                        const std::string key=std::string("thread:boundary:root:")+end;
                        require(std::ranges::any_of(fresh.back().mesh.original_references.edges,[&](const auto& edge){
                            return edge.reference.owner_id==container.id&&edge.reference.semantic_key==key&&!edge.parameter_seam;
                        }),"Calculated factory thread lost its unique start/end rim");
                    }
                    threads.push_back({{"owner",container.id},{"explicit_chamfer",container.shaft_thread.chamfer.has_value()},
                        {"start_offset_mm",request.start_offset},{"full_end_mm",request.start_offset+request.length-request.runout_end},
                        {"runout_mm",request.runout_end}});
                }
                const auto target=output/fs::relative(entry.path(),root);fs::create_directories(target.parent_path());
                doc.save(target,fresh);std::vector<kernel::BodyResult> reopened;
                const auto restored=document::PartDocument::load(target,&reopened);
                require(restored.history==doc.history&&!reopened.empty(),"Fastener round trip changed authored history");
                report.push_back({{"file",document::path_to_utf8(fs::relative(entry.path(),root))},{"threads",threads},
                    {"saved_and_regenerated_extents",true},{"unique_start_end_rims",true},{"save_reopen",true}});
                std::ofstream(output/"report.json")<<report.dump(2);
                std::cout<<report.size()<<" "<<document::path_to_utf8(entry.path().filename())<<std::endl;
            }
            require(!report.empty(),"No shaft-thread fasteners found");return 0;
        }
        if(argc==3&&std::string(argv[1])=="--verify-fitted-starter") {
            std::vector<kernel::BodyResult> cache;
            const auto part=document::PartDocument::load(fs::u8path(argv[2]),&cache);
            require(part.history.size()==4&&std::ranges::all_of(part.history,[](const auto& value){return value.feature_kind==document::FeatureKind::Feature&&!value.suppressed;}),
                "Fitted starting model must retain four active current Default features");
            require(!cache.empty()&&cache.back().calculation_errors.empty()&&cache.back().body_outputs.size()==1,"Fitted starting model calculation failed");
            const auto& body=cache.back().body_outputs.begin()->second.get();
            TopoDS_Shape shape;BRep_Builder builder;std::istringstream input(body.kernel_shape);BRepTools::Read(shape,input,builder);
            require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Fitted starting model has invalid BRep");
            int solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
            require(solids==1,"Fitted starting model is disconnected");
            bool fitted=false,thread_blank=false;
            for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()){
                const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()!=GeomAbs_Cylinder)continue;
                const auto cylinder=surface.Cylinder();near(cylinder.Axis().Location().X(),0,"Fitted cylinder X");near(cylinder.Axis().Location().Y(),0,"Fitted cylinder Y");
                Bnd_Box bounds;BRepBndLib::AddOptimal(it.Current(),bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
                if(std::abs(cylinder.Radius()-5.5)<1e-8){near(z0,0,"Fitted region beginning");near(z1,34,"Fitted region end");fitted=true;}
                if(std::abs(cylinder.Radius()-5)<1e-8){near(z0,34,"M10 blank beginning");near(z1,50,"M10 blank end");thread_blank=true;}
            }
            require(fitted&&thread_blank,"Fitted Ø11 and nominal M10 blank are not distinct actual cylinders");
            Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            near(z0,-7,"Fitted head height");near(z1,50,"Fitted bolt length");near(y1-y0,16,"Fitted head width across flats");
            auto sketch=part.sketches.back();const auto solved=sketch.solve();require(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Fitted profile is not anchored and dimensioned");
            std::cout<<"Editable fitted starter: four current Defaults, one valid solid, Ø11 × 34 fitted region, Ø10 × 16 thread blank, 16 mm head and origin-anchored profile passed\n";return 0;
        }
        if(argc==4&&std::string(argv[1])=="--reauthor-locking-hole") {
            const auto source=fs::absolute(fs::u8path(argv[2])),output=fs::absolute(fs::u8path(argv[3]));
            require(source!=output&&!fs::exists(output),"Use a new staging path for factory reauthoring");
            std::vector<kernel::BodyResult> previous;
            auto document=document::PartDocument::load(source,&previous);
            require(!previous.empty()&&!document.history.empty(),"Factory source has no calculated history");
            const auto standard=document.user_parameter_values.at("standard").at("");
            require(standard=="ČSN 02 1205"||standard=="ČSN 02 1208","Only factory locking-hole variants may be reauthored");
            auto& hole=document.history.back();
            require(hole.feature_kind==document::FeatureKind::Extrusion&&hole.combine_mode==document::CombineMode::Subtract&&
                !hole.extrusion.sheet_cut&&hole.extrusion.profile_source==document::ProfileSource::Internal,
                "Expected the generator's final ordinary locking-hole extrusion");
            const auto sketch_id=hole.extrusion.sketch_id;
            hole.feature_kind=document::FeatureKind::Feature;
            hole.feature={};hole.feature.sketch_id=sketch_id;hole.feature.automatic_name=hole.name;
            hole.feature.profile_plane_offset=hole.extrusion.profile_plane_offset;
            for(auto& side:hole.feature.sides){side.operation=document::FeatureSideOperation::Extrusion;side.extrusion_extent=document::EndCondition::ThroughAll;}
            hole.extrusion={};
            const auto hole_id=hole.id,hole_feature_id=hole.feature_id;
            const auto definition=hole.feature;
            kernel::OcctKernel kernel;
            auto calculated=workspace::calculate_part_with_resolved_references(kernel,document,&previous);
            require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Reauthored Default failed calculation");
            const auto& old_body=previous.back().body_outputs.begin()->second.get();
            const auto& new_body=calculated.back().body_outputs.begin()->second.get();
            near(new_body.volume,old_body.volume,"Reauthoring changed the bolt volume");
            near(new_body.surface_area,old_body.surface_area,"Reauthoring changed the bolt surface area");
            const auto identities=[&](const auto& faces){
                std::set<std::array<std::string,3>> result;
                for(const auto& face:faces)if(face.reference.owner_id!=hole_id){
                    auto owner=face.reference.owner_id,key=face.reference.semantic_key;
                    while(const auto parent=kernel::boolean_face_parent(key)){owner=parent->first;key=parent->second;}
                    result.insert({owner,key,face.reference.instance_path});
                }
                return result;
            };
            if(identities(old_body.kernel_faces)!=identities(new_body.kernel_faces)){
                const auto old=identities(old_body.kernel_faces),now=identities(new_body.kernel_faces);
                for(const auto& ref:old)if(!now.contains(ref))std::cerr<<"Removed source identity: "<<Json(ref).dump()<<'\n';
                for(const auto& ref:now)if(!old.contains(ref))std::cerr<<"Added source identity: "<<Json(ref).dump()<<'\n';
                throw std::runtime_error("Reauthoring changed a preceding feature's face reference identity");
            }
            fs::create_directories(output.parent_path());document.save(output,calculated);
            std::vector<kernel::BodyResult> reopened_geometry;const auto reopened=document::PartDocument::load(output,&reopened_geometry);
            require(reopened.document_id==document.document_id&&reopened.history.back().feature==definition&&
                reopened.history.back().id==hole_id&&reopened.history.back().feature_id==hole_feature_id,
                "Default definition or authored identity failed save/reopen");
            require(reopened.sketches.size()==document.sketches.size(),"Reauthoring changed Sketch ownership");
            for(std::size_t i=0;i<document.sketches.size();++i)require(reopened.sketches[i].serialized()==document.sketches[i].serialized(),"Reauthoring changed a persisted Sketch");
            std::cout<<"Locking-hole Default, both sides through all, unchanged solid and preceding face identities saved and reopened\n";
            return 0;
        }
        if(argc==4&&std::string(argv[1])=="--illustration-texts") {
            const auto path=fs::absolute(fs::u8path(argv[2]));
            auto drawing=drawing::DrawingDocument::load(path);
            Json specification;std::ifstream input(fs::u8path(argv[3]));input>>specification;
            require(drawing.sheets.size()==1,"Factory illustration must have one sheet");
            auto& sheet=drawing.sheets.front();
            std::erase_if(sheet.texts,[](const auto& text){return text.id.find(":illustration:")!=std::string::npos;});
            const auto retained_texts=sheet.texts.size();
            sheet.title_block_locale=specification.at("locale").get<std::string>();
            for(const auto& note:specification.at("texts")) {
                drawing::DrawingText text;text.id=drawing.document_id+":illustration:"+std::to_string(sheet.texts.size());
                text.presentation.text=note.at("text").get<std::string>();
                text.presentation.position={note.at("x_mm"),note.at("y_mm")};
                text.presentation.height=note.value("height_mm",2.5);
                text.presentation.pen=drawing::DrawingPen::Green;
                text.presentation.font="osifont";
                sheet.texts.push_back(std::move(text));
            }
            drawing.save(path);
            require(drawing::DrawingDocument::load(path).sheets.front().texts.size()==retained_texts+specification.at("texts").size(),
                "Localized illustration texts did not persist");
            std::cout<<"Native illustration text and locale saved\n";return 0;
        }
        if((argc==3&&std::string(argv[1])=="--rebase-reference-drawing")||
           (argc==5&&std::string(argv[1])=="--adopt-reference-source")) {
            const auto path=fs::absolute(fs::u8path(argv[2]));
            auto source_path=argc==5?fs::absolute(fs::u8path(argv[3])):path;
            if(argc==3)source_path.replace_extension(".prtz");
            const auto output=argc==5?fs::absolute(fs::u8path(argv[4])):path;
            const auto source=document::PartDocument::load(source_path);
            auto drawing=drawing::DrawingDocument::load(path);
            if(std::string(argv[1])=="--adopt-reference-source") {
                for(auto& sheet:drawing.sheets)for(auto& view:sheet.views) {
                    require(view.source_document_id==source.document_id,"Retarget all real views before adopting a reference source");
                    // Projection has already supplied current source geometry.
                    // Transfer the pattern's explicit display choices by its
                    // real native feature/semantic identity, never stale curves.
                    for(auto& current:view.model_annotations)if(current.source.document_id==source.document_id) {
                        const auto old=std::ranges::find_if(view.model_annotations,[&](const auto& value) {
                            return value.source.document_id!=source.document_id&&value.kind==current.kind&&
                                value.source.owner_id==current.source.owner_id&&value.source.semantic_id==current.source.semantic_id&&
                                value.source.instance_path==current.source.instance_path;
                        });
                        if(old!=view.model_annotations.end()) {
                            current.visible=old->visible;current.view_layout=old->view_layout;current.paper_handles=old->paper_handles;
                        }
                    }
                    std::erase_if(view.model_annotations,[&](const auto& value){return value.source.document_id!=source.document_id;});
                    for(const auto& value:view.model_annotations)require(!value.visible||!value.unresolved,
                        "Visible pattern annotation failed to retarget to its current native source");
                }
                for(const auto& old:drawing.data_sources())if(old.document_id!=source.document_id)
                    static_cast<void>(drawing.remove_data_source(old.document_id));
                drawing.source_document_id=source.document_id;drawing.source_name=document::path_to_utf8(source_path.stem());
            }
            require(drawing.source_document_id==source.document_id,"Reference Drawing source identity mismatch");
            drawing.source_path=source_path.filename();
            for(auto& item:drawing.sources) {
                require(item.document_id==source.document_id,"Unexpected reference Drawing source");
                item.source_path=source_path.filename();
            }
            for(auto& sheet:drawing.sheets) {
                for(auto& view:sheet.views) {
                    require(view.source_document_id==source.document_id,"Unexpected reference view source");
                    view.source_path=source_path.filename();
                }
                for(const auto& dimension:sheet.dimensions) {
                    const auto view=std::ranges::find(sheet.views,dimension.view_id,&drawing::DrawingView::id);
                    require(view!=sheet.views.end(),"Reference dimension view missing");
                    require(drawing::evaluate_drawing_dimension(*view,dimension).state==drawing::MeasurementState::Resolved,
                        "Reference dimension unresolved after source rebasing");
                }
            }
            if(argc==5)drawing.name=document::path_to_utf8(output.stem());
            drawing.save(output);
            require(drawing::DrawingDocument::load(output).source_path==source_path.filename(),"Relative native source path did not persist");
            std::cout<<"Native reference Drawing source rebased to sibling Part\n";
            return 0;
        }
        require(argc>=3,"Usage: fastener-library-contract manifest.json candidate-directory [...]");
        Json manifest;std::ifstream input(fs::u8path(argv[1]));input>>manifest;
        std::size_t count=0;
        for(const auto& variant:manifest.at("variants")) {
            fs::path found;
            for(int i=2;i<argc;++i) {
                const auto candidate=fs::u8path(argv[i])/fs::u8path(variant.at("filename").get<std::string>());
                if(fs::exists(candidate)){require(found.empty(),"Duplicate candidate filename");found=candidate;}
            }
            if(found.empty())continue;
            try{verify(found,variant);}catch(const std::exception& error){throw std::runtime_error(document::path_to_utf8(found.filename())+": "+error.what());}
            ++count;
        }
        require(count>0,"No manifest candidates found");
        std::cout<<count<<" fasteners: valid single solids, exact dimensions/mass, bearing choice, fillets, constrained Sketches and thread limits passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#ifdef _WIN32
// Windows supplies UTF-16 arguments. The shared verifier and its JSON/native
// paths consume UTF-8, independently of the machine's ANSI code page.
int wmain(int argc,wchar_t** wide) {
    std::vector<std::string> arguments;arguments.reserve(argc);
    for(int i=0;i<argc;++i) {
        const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
        if(!length){std::cerr<<"Invalid Unicode argument\n";return 1;}
        std::string text(length,'\0');
        if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,text.data(),length,nullptr,nullptr))return 1;
        text.pop_back();arguments.push_back(std::move(text));
    }
    std::vector<char*> pointers;pointers.reserve(argc+1);
    for(auto& argument:arguments)pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return run_fastener_contract_tests(argc,pointers.data());
}
#else
int main(int argc,char** argv) {return run_fastener_contract_tests(argc,argv);}
#endif
