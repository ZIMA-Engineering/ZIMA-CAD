#include <zima/workspace/native_documents.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <nlohmann/json.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Bnd_Box.hxx>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <array>

using namespace zima;
namespace fs=std::filesystem;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){
    if(std::abs(actual-expected)>1e-6*std::max(1.,std::abs(expected)))
        throw std::runtime_error(std::string(message)+": "+std::to_string(actual)+" != "+std::to_string(expected));
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
    bool shaft=false,fillet=false,bearing=false,hole=false,tip=false;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if(surface.GetType()==GeomAbs_Cylinder) {
            const double radius=surface.Cylinder().Radius();
            shaft|=std::abs(radius-d/2)<1e-6;
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
    const bool raised=expected.value("bearing_face",expected.at("product_grade").get<std::string>()!="C");
    require(bearing==raised,"Bearing-face geometry disagrees with the selected variant");
    require(hole==expected.contains("locking_hole_diameter_mm"),"Locking hole radius, axis or head-center position disagrees");
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
    const auto* feature=document.find_container("01a14429e3207acba34fc9c038cdccfe");
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
int main(int argc,char** argv) {
    try {
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
                drawing.source_document_id=source.document_id;drawing.source_name=source_path.stem().string();
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
            if(argc==5)drawing.name=output.stem().string();
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
            try{verify(found,variant);}catch(const std::exception& error){throw std::runtime_error(found.filename().string()+": "+error.what());}
            ++count;
        }
        require(count>0,"No manifest candidates found");
        std::cout<<count<<" fasteners: valid single solids, exact dimensions/mass, bearing choice, fillets, constrained Sketches and thread limits passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
