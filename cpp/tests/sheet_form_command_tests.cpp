#include <zima/document/sheet_form_definition.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <zima/workspace/sheet_form_operations.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/drawing/model_annotations.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <set>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-7){check(std::abs(a-b)<tolerance,"FORM equation mismatch");}
const symbols::Placement& symbol(const std::vector<drawing::ModelAnnotationSource>& packets) {
    check(packets.size()==1&&packets[0].symbols.size()==1,"FORM did not offer exactly one manufacturing symbol");
    return packets[0].symbols[0];
}
}
int main(){try {
    auto source=document::read_sheet_form_definition("config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz");
    const auto authored=source.part.serialized();
    auto part=document::PartDocument::create_default();
    const auto body=document::create_origin_bound_body(part.body_history,part.document_id,"Sheet");
    auto stock=document::PartDocument::create_sketch_container();stock.feature_kind=document::FeatureKind::Flat;
    stock.flat.direction=document::ExtrusionDirection::Reverse;stock.flat.thickness_override=true;stock.flat.thickness=1;
    auto outline=sketcher::Sketch::create_default();outline.owner_container_id=stock.id;
    static_cast<void>(outline.add_rectangle(-100,-100,100,100));stock.flat.sketch_id=outline.id;
    part.insert_history_entry(document::PartHistoryKind::Feature,stock.id);part.history.push_back(stock);part.sketches.push_back(outline);
    kernel::OcctKernel kernel;const auto baseline=workspace::calculate_part_with_resolved_references(kernel,part);
    auto face=std::ranges::find_if(baseline.back().mesh.triangle_references,[](const auto& f){return f.surface&&
        f.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&f.sheet_thickness>0&&std::abs(f.surface->origin.z)<1e-8&&
        (f.sheet_role==kernel::SheetFaceRole::SideA||f.sheet_role==kernel::SheetFaceRole::SideB);});
    check(face!=baseline.back().mesh.triangle_references.end(),"Sheet support missing");
    auto feature=document::create_sheet_form();feature.sheet_form=document::copy_sheet_form_definition(source);
    feature.sheet_form.support=*face;
    const auto geometry=part.construction_reference_geometry_for(stock.id,baseline.back().mesh.original_references);
    feature.placement=document::sheet_form_attachment(*face,part.body_history.find(body)->origin().id,geometry,{},{});
    workspace::Workspace live;live.add_part(part,baseline);
    check(workspace::commit_sheet_form(live,kernel,part.document_id,feature),"FORM command did not create history");
    const auto saved=[&]()->const document::PartDocument&{return live.open_part(part.document_id)->session.document();};
    const auto boundaries=[&]()->const std::vector<kernel::BodyResult>&{return live.open_part(part.document_id)->session.calculated_boundaries();};
    check(boundaries().back().calculation_errors.empty(),"FORM command failed");
    auto bad=saved().find_container(feature.id)->sheet_form;
    static_cast<void>(document::stored_sheet_form_definition(bad));
    bad.cut_sketch="missing";bool rejected=false;
    try{static_cast<void>(document::stored_sheet_form_definition(bad));}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Parsed FORM cache accepted changed role metadata");
    for(double thickness:{1.2,.8,1.}) {
        auto flat=*saved().find_container(stock.id);flat.flat.thickness=thickness;
        check(workspace::commit_flat(live,kernel,part.document_id,flat,outline),"Sheet thickness edit was ignored");
        check(boundaries().back().calculation_errors.empty(),"Sheet thickness change broke FORM");
        const auto* current=saved().find_container(feature.id);
        near(current->sheet_form.thickness,thickness);near(current->sheet_form.support.sheet_thickness,thickness);
        check(std::ranges::any_of(boundaries().back().mesh.triangle_references,[&](const auto& f){return
            f.owner_id==feature.id&&f.sheet_role==kernel::SheetFaceRole::SideA&&std::abs(f.sheet_thickness-thickness)<1e-8;}),
            "FORM skin retained stale sheet thickness");
    }
    check(workspace::step_part_document_history(live,part.document_id,false),"Thickness Undo failed");
    near(saved().find_container(feature.id)->sheet_form.thickness,.8);
    check(workspace::step_part_document_history(live,part.document_id,true),"Thickness Redo failed");
    near(saved().find_container(feature.id)->sheet_form.thickness,1.);
    auto packets=workspace::drawing_annotation_sources(&live,part.document_id,{});
    const auto initial=symbol(packets);check(!initial.viewer_mesh().edges.empty(),"FORM_SYMBOL lost authored strokes");
    check(initial.symbol.id=="form:symbol:"+feature.id,"FORM symbol has no stable feature ancestry");
    drawing::DrawingView view;view.camera={{1,0,0},{0,1,0},{0,0,1}};view.scale=1;
    drawing::refresh_model_annotations(view,packets);
    const auto annotation=std::ranges::find_if(view.model_annotations,[](const auto& a){return a.kind==drawing::ModelAnnotationKind::Symbol;});
    check(annotation!=view.model_annotations.end()&&!annotation->curves.empty(),"Drawing did not inherit FORM_SYMBOL");
    check(drawing::show_erase_candidates(view.model_annotations,drawing::ShowEraseMode::Show,{drawing::ModelAnnotationKind::Symbol}).size()==1,
        "Show/Erase did not offer FORM_SYMBOL");
    annotation->visible=true;
    check(drawing::deserialize_model_annotations(drawing::serialize_model_annotations(view.model_annotations))==view.model_annotations,
        "Drawing FORM symbol persistence failed");
    for(double angle:{35.,-65.,0.}) {
        auto edited=*saved().find_container(feature.id);edited.placement.absolute_rotation_y=angle;
        check(workspace::commit_sheet_form(live,kernel,part.document_id,edited),"FORM rotation edit ignored");
        packets=workspace::drawing_annotation_sources(&live,part.document_id,{});const auto& rotated=symbol(packets);
        near(kernel::sheet_material::dot(rotated.frame.x,rotated.frame.y),0.);
        near(kernel::sheet_material::dot(rotated.frame.x,rotated.frame.x),1.);
        const auto outward=kernel::sheet_material::mul(face->surface->axis,face->surface->reversed?-1.:1.);
        const auto radians=angle*std::numbers::pi/180.;
        const auto expected=kernel::sheet_material::add(kernel::sheet_material::mul(initial.frame.x,std::cos(radians)),
            kernel::sheet_material::mul(kernel::sheet_material::cross(outward,initial.frame.x),std::sin(radians)));
        near(kernel::sheet_material::dot(rotated.frame.x,expected),1.);
        near(rotated.frame.origin.z,initial.frame.origin.z);
        check(rotated.symbol.id==initial.symbol.id,"Rotation changed manufacturing symbol identity");
        drawing::refresh_model_annotations(view,packets);
        check(std::ranges::any_of(view.model_annotations,[&](const auto& a){return a.source.owner_id==initial.symbol.id&&a.visible&&!a.unresolved;}),
            "FORM symbol refresh lost Show/Erase state");
    }
    // Replace an empty flat role by a real native cutting Sketch. Only the
    // flat variant changes; the authored outer shell snapshot remains valid.
    auto precut=source.part;
    precut.body_history.activate(source.bodies[2]);
    auto flat_feature=document::PartDocument::create_sketch_container();
    flat_feature.feature_kind=document::FeatureKind::Sketch;
    auto flat_sketch=sketcher::Sketch::create_default();flat_sketch.owner_container_id=flat_feature.id;
    const auto& cut=*std::ranges::find(precut.sketches,source.cut_sketch,&sketcher::Sketch::id);
    flat_sketch.plane=cut.plane;flat_sketch.plane_offset=cut.plane_offset;flat_sketch.refresh_default_frame();
    flat_feature.placement=precut.find_container(cut.owner_container_id)->placement;
    static_cast<void>(flat_sketch.add_rectangle(-2,-2,2,2));
    precut.insert_history_entry(document::PartHistoryKind::Feature,flat_feature.id);
    precut.history.push_back(flat_feature);precut.sketches.push_back(flat_sketch);
    auto replaced=*saved().find_container(feature.id);const auto attachment=replaced.placement;
    replaced.sheet_form=document::copy_sheet_form_definition(document::sheet_form_definition(precut,*source.calculated));
    replaced.sheet_form.support=saved().find_container(feature.id)->sheet_form.support;
    check(workspace::commit_sheet_form(live,kernel,part.document_id,replaced),"Nonempty FORM_FLAT replacement failed");
    check(saved().find_container(feature.id)->placement==attachment,"FORM_FLAT replacement changed attachment");
    auto flatten=document::PartDocument::create_sketch_container();flatten.feature_kind=document::FeatureKind::Unbend;
    flatten.sheet_state.all=false;flatten.sheet_state.owners={feature.id};
    check(workspace::commit_sheet_state(live,kernel,part.document_id,flatten),"FORM flatten command failed");
    check(boundaries().back().calculation_errors.empty(),"FORM_FLAT calculation failed");
    near(boundaries().back().volume,39984.,1e-5);
    packets=workspace::drawing_annotation_sources(&live,part.document_id,{});
    check(symbol(packets).symbol.id==initial.symbol.id,"Flattening changed FORM_SYMBOL ancestry");
    check(workspace::step_part_document_history(live,part.document_id,false),"FORM flatten Undo failed");
    check(boundaries().back().volume>40000.,"FORM flatten Undo did not restore the spatial skin");
    packets=workspace::drawing_annotation_sources(&live,part.document_id,{});
    auto dependent=saved();auto dependent_feature=document::PartDocument::create_sketch_container();
    dependent_feature.feature_kind=document::FeatureKind::Sketch;
    auto dependent_sketch=sketcher::Sketch::create_default();dependent_sketch.owner_container_id=dependent_feature.id;
    const auto edge=std::ranges::find_if(boundaries().back().mesh.edges,[&](const auto& e){return e.reference.owner_id==feature.id&&
        e.points.size()>1&&std::hypot(e.points.back().x-e.points.front().x,e.points.back().y-e.points.front().y)>1e-4;});
    check(edge!=boundaries().back().mesh.edges.end(),"FORM has no native dependent edge");
    auto external=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
    external.body_edge=true;external.source_document_id=part.document_id;external.source_owner_id=feature.id;
    external.source_semantic_key=edge->reference.semantic_key;
    for(const auto p:edge->points)external.cached_points.push_back({p.x,p.y});dependent_sketch.add_external_reference(external);
    dependent.insert_history_entry(document::PartHistoryKind::Feature,dependent_feature.id);
    dependent.history.push_back(dependent_feature);dependent.sketches.push_back(dependent_sketch);
    auto dependent_result=workspace::calculate_part_with_resolved_references(kernel,dependent,&boundaries());
    workspace::commit_part_document(live,part.document_id,std::move(dependent),std::move(dependent_result));
    const auto find_dependent=[&]()->const sketcher::Sketch&{return *std::ranges::find(saved().sketches,dependent_sketch.id,&sketcher::Sketch::id);};
    check(!find_dependent().external_references.front().broken,"Native FORM edge dependency was initially unresolved");
    auto changed=*saved().find_container(feature.id);const auto old_definition=changed.sheet_form;
    changed.sheet_form=document::copy_sheet_form_definition(source);changed.sheet_form.support=old_definition.support;
    check(workspace::commit_sheet_form(live,kernel,part.document_id,changed),"Dependent FORM replacement failed");
    check(find_dependent().external_references.front().broken,"Replacement retained missing FORM geometry as a resolved reference");
    check(workspace::step_part_document_history(live,part.document_id,false),"Dependent replacement Undo failed");
    check(!find_dependent().external_references.front().broken,"Replacement Undo did not restore the exact dependent source");
    check(workspace::step_part_document_history(live,part.document_id,true),"Dependent replacement Redo failed");
    check(find_dependent().external_references.front().broken,"Replacement Redo lost its unresolved source state");
    packets=workspace::drawing_annotation_sources(&live,part.document_id,{});
    const auto native_file=std::filesystem::temp_directory_path()/("zima-form-command-"+kernel::make_stable_id()+".prtz");
    saved().save(native_file,boundaries());
    std::vector<kernel::BodyResult> reopened_cache;auto reopened=document::PartDocument::load(native_file,&reopened_cache);
    workspace::Workspace cold;cold.add_part(reopened,reopened_cache,native_file);
    check(symbol(workspace::drawing_annotation_sources(&cold,reopened.document_id,native_file))==symbol(packets),"FORM symbol changed after save/reopen");
    auto suppressed=reopened;suppressed.find_container(feature.id)->suppressed=true;
    workspace::Workspace hidden;hidden.add_part(suppressed,reopened_cache);
    const auto hidden_packets=workspace::drawing_annotation_sources(&hidden,suppressed.document_id,{});
    check(hidden_packets.front().symbols.empty(),"Suppressed FORM still offered its symbol");
    auto assembly=assembly::AssemblyDocument::create_default();
    for(int i=0;i<2;++i) {assembly::PartOccurrence item;item.occurrence_id="form-occurrence-"+std::to_string(i);
        item.source_document_id=reopened.document_id;item.source_path=native_file;item.placement.x=i*300;assembly.components.push_back(item);}
    cold.add_assembly(assembly);auto repeated=workspace::drawing_annotation_sources(&cold,assembly.document_id,{});
    std::set<std::string> paths;std::set<double> origins;
    for(const auto& packet:repeated)for(const auto& item:packet.symbols){paths.insert(packet.instance_path);origins.insert(item.frame.origin.x);}
    check(paths.size()==2&&origins.size()==2,"Repeated FORM symbols lost occurrence placement");
    std::filesystem::remove(native_file);
    check(source.part.serialized()==authored,"Commands changed the FORM library");
    std::cout<<"FORM command: repeated thickness edits, Undo/Redo, cache validation, Drawing symbols, rotation, suppression, repeated occurrences and native persistence passed"<<std::endl;
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
