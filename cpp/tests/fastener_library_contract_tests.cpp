#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/kernel/boolean_face_ancestry.hpp>
#include <zima/kernel/shaft_thread_geometry.hpp>
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
                report.push_back({{"file",fs::relative(entry.path(),root).generic_string()},{"threads",threads},
                    {"saved_and_regenerated_extents",true},{"unique_start_end_rims",true},{"save_reopen",true}});
                std::ofstream(output/"report.json")<<report.dump(2);
                std::cout<<report.size()<<" "<<entry.path().filename().string()<<std::endl;
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
