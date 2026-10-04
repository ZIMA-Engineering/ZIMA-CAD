#include <zima/workspace/symbol_operations.hpp>
#include <zima/symbols/native_document.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <iostream>
using namespace zima;
namespace {void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
namespace {
void native_annotations(const std::filesystem::path& root) {
    workspace::Workspace live;
    auto part=document::PartDocument::create_default();const auto part_id=part.document_id;live.add_part(part);
    auto assembly=assembly::AssemblyDocument::create_default();const auto assembly_id=assembly.document_id;live.add_assembly(assembly);
    auto drawing=drawing::DrawingDocument::create_default();const auto drawing_id=drawing.document_id;
    const auto sheet=drawing.sheets.front().id;live.add_drawing(drawing);
    auto definition=symbols::projection_method();symbols::Placement annotation;
    annotation.symbol.id="native-annotation";annotation.symbol.definition=definition.serialized();annotation.symbol.variant=definition.default_variant;
    annotation.symbol.x=12;annotation.symbol.y=8;annotation.leader=true;annotation.frame.origin={20,30,40};
    annotation.weld_all_around=true;
    for(const auto& id:{part_id,assembly_id,drawing_id}) {
        live.activate(id);const auto target=id==drawing_id?sheet:std::string{};
        annotation.reference=symbols::Reference{id,id+":origin","plane:xy","",symbols::ReferenceKind::Plane,true};
        annotation.unresolved=true;
        check(workspace::store_symbol_annotation(live,id,annotation,target),"Annotation insertion was not committed");
        check(!workspace::store_symbol_annotation(live,id,annotation,target),"Unchanged annotation created an Undo record");
        check(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Annotation Undo failed");
        check(workspace::symbol_annotations(live,id,target).empty(),"Annotation Undo retained inserted symbol");
        check(workspace::step_document_history(live,id,workspace::HistoryDirection::Redo),"Annotation Redo failed");
        check(workspace::symbol_annotations(live,id,target)==std::vector{annotation},"Annotation Redo lost placement or identity");
        if(id!=drawing_id) {
            const auto mesh=id==part_id?live.open_part(id)->session.document().construction_viewer_mesh():live.open_assembly(id)->session.document().construction_viewer_mesh();
            const auto count=std::ranges::count_if(mesh.edges,[&](const auto& edge){return edge.reference.owner_id==annotation.symbol.id;});
            check(count==annotation.viewer_mesh().edges.size(),"Model display omitted or duplicated symbol strokes");
            check(std::ranges::none_of(mesh.original_references.edges,[&](const auto& edge){return edge.reference.owner_id==annotation.symbol.id;}),"Annotation became a model topology reference");
        }
        auto invalid=annotation;invalid.frame.x={0,0,0};bool rejected=false;
        try{workspace::store_symbol_annotation(live,id,invalid,target);}catch(const std::exception&){rejected=true;}
        check(rejected&&workspace::symbol_annotations(live,id,target)==std::vector{annotation},"Invalid edit changed annotation document");
        const auto extension=id==part_id?".prtz":id==assembly_id?".asmz":".drwz";
        const auto path=root/(id+extension);auto pending=workspace::prepare_document_save(live,id,path);
        check(workspace::complete_document_save(live,pending.write()),"Native annotation save failed");
        const auto restored=id==part_id?document::PartDocument::load(path).symbol_annotations:
            id==assembly_id?assembly::AssemblyDocument::load(path).symbol_annotations:drawing::DrawingDocument::load(path).sheets.front().symbol_annotations;
        check(restored==std::vector{annotation},"Native file lost unresolved symbol or leader");
        const auto copy_path=root/(id+"-copy"+extension);static_cast<void>(live.save_copy(id,copy_path));
        symbols::Placement copied;std::string copied_id;
        if(id==part_id){auto copy=document::PartDocument::load(copy_path);copied=copy.symbol_annotations.front();copied_id=copy.document_id;}
        else if(id==assembly_id){auto copy=assembly::AssemblyDocument::load(copy_path);copied=copy.symbol_annotations.front();copied_id=copy.document_id;}
        else {auto copy=drawing::DrawingDocument::load(copy_path);copied=copy.sheets.front().symbol_annotations.front();copied_id=copy.document_id;}
        check(copied_id!=id&&copied.reference->document_id==copied_id&&copied.reference->owner_id==copied_id+":origin","Copy retained source document annotation identity");
        check(copied.frame==annotation.frame&&copied.symbol==annotation.symbol&&copied.unresolved,"Copy changed embedded definition or unresolved pose");
        check(workspace::remove_symbol_annotation(live,id,annotation.symbol.id,target),"Annotation removal failed");
        check(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Annotation deletion Undo failed");
        check(workspace::symbol_annotations(live,id,target)==std::vector{annotation},"Annotation deletion Undo lost data");
    }
}
void contact_transactions() {
    auto part=document::PartDocument::create_default();document::DocumentSession session(part,{});
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;surface->radius=5;
    kernel::FaceReference face{"authored-cylinder","face:wall","",surface};kernel::BodyResult body;body.mesh.original_references.triangle_references={face};
    const auto definition=symbols::projection_method();symbols::Placement value;value.symbol.id="contact-symbol";
    value.symbol.definition=definition.serialized();value.symbol.variant=definition.default_variant;
    symbols::attach_to_surface(value,face,part.document_id,{5,0,2});part.symbol_annotations={value};session.commit(part,{body});
    surface=std::make_shared<kernel::SurfaceGeometry>(*surface);surface->radius=8;body.mesh.original_references.triangle_references.front().surface=surface;
    session.commit(session.document(),{body});check(session.document().symbol_annotations.front().frame.origin==kernel::Vec3{8,0,2},"Model transaction failed to move surface attachment");
    session.commit(session.document(),{});const auto lost=session.document().symbol_annotations.front();
    check(lost.unresolved&&lost.frame.origin==kernel::Vec3{8,0,2},"Deleting source did not retain last symbol pose");
    check(session.undo()&&!session.document().symbol_annotations.front().unresolved,"Undo source removal did not restore attachment");
    check(session.redo()&&session.document().symbol_annotations.front()==lost,"Redo source removal changed retained annotation");
    auto replaced=session.document();replaced.symbol_annotations.front().reference->document_id="different-source";
    session.commit(replaced,{body});
    check(session.document().symbol_annotations.front().unresolved&&session.document().symbol_annotations.front().frame==lost.frame,
          "Same topology IDs in a different source rebound the symbol");
}
}
int main(){try {
    const auto bend_path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/symbols/sheetm/ZE-BEND-NOTE.symz";
    const auto bend=symbols::Definition::load(bend_path);
    check(bend.variant_source.empty()&&bend.fields.size()==1&&bend.fields.at("Text").allow_custom&&bend.fields.at("Text").choices.empty(),
        "Bend note introduced automatic values or restricted text entry");
    const auto bend_native=document::PartDocument::load(bend_path);
    check(bend_native.history.size()==1&&bend_native.sketches.size()==1&&bend_native.body_history.bodies().size()==1&&
        bend_native.body_owner_for_object(bend_native.history.front().id),"Bend-note Sketch is not owned by an ordinary Body");
    for(const auto* language:{"cs","en","de","fr","ru"}) {
        const auto evaluated=bend.evaluate(language,{{"Text","Ohyb 37° dolů / custom"}});
        check(evaluated.size()==1&&evaluated.front().texts.size()==1&&evaluated.front().texts.front().value=="Ohyb 37° dolů / custom",
            "Bend note does not preserve manual custom text");
        sketcher::SymbolInstance note;note.id="bend-note";note.definition=bend.serialized();note.variant=language;
        note.text_values["Text"]="Ohyb 37° dolů / custom";
        for(double angle:{0.,90.,180.,270.}) {
            note.angle_degrees=angle;check(!symbols::instance_mesh(note).edges.empty(),"Rotated bend note has no renderable text");
            check(note.text_values.at("Text")=="Ohyb 37° dolů / custom","Rotation rewrote manual bend direction");
        }
    }
    const auto root=std::filesystem::temp_directory_path()/("zima-symbol-editor-"+kernel::make_stable_id());
    std::filesystem::create_directory(root);
    native_annotations(root);
    contact_transactions();
    workspace::Workspace live;const auto id=workspace::create_symbol_document(live,"Example",root/"example.symz");
    check(workspace::document_needs_save(live,id),"New symbol incorrectly clean");
    const auto& native=live.open_part(id)->session.document();
    check(native.body_history.bodies().size()==1&&!native.body_history.active_body_id().empty(),"Symbol requires an ordinary active Body");
    check(native.body_owner_for_object(native.sketches.front().owner_container_id)!=nullptr,"Symbol Sketch is outside its Body");
    auto hidden=native;auto hidden_body=hidden.body_history.bodies().front();hidden_body.visible=false;
    hidden.body_history.update_body(hidden_body);
    check(symbols::native_definition(hidden).evaluate("default").empty(),"Hidden Body leaked into symbol insertion");
    check(symbols::native_definition(hidden,false).variants.at("default").sketches.size()==1,"Hidden Body erased editable Family presence");
    hidden_body.visible=true;hidden_body.cursor=0;hidden.body_history.update_body(hidden_body);
    check(symbols::native_definition(hidden).evaluate("default").empty(),"Downstream Sketch leaked past Body history cursor");
    const auto sketch_id=live.open_part(id)->session.document().sketches.front().id;
    workspace::mutate_document_sketch(live,id,sketch_id,[](auto& sketch){static_cast<void>(sketch.add_rectangle(0,0,10,5));});
    auto pending=workspace::prepare_document_save(live,id,root/"example.symz");
    check(workspace::complete_document_save(live,pending.write()),"Symbol save receipt failed");
    auto saved=symbols::Definition::load(root/"example.symz");check(saved.sketches.front().segments.size()==4,"Symbol saved wrong geometry");
    check(!workspace::document_needs_save(live,id),"Saved symbol dirty");
    check(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Symbol Undo failed");
    check(workspace::edited_symbol_definition(live,id).sketches.front().segments.empty(),"Undo retained rectangle");
    check(workspace::step_document_history(live,id,workspace::HistoryDirection::Redo),"Symbol Redo failed");
    workspace::save_symbol_document(live,id,root/"copy.symz",true);
    check(live.open_part(id)->path.filename()=="example.symz","Copy retargeted document");
    check(symbols::Definition::load(root/"copy.symz").serialized()==saved.serialized(),"Copy lost definition");
    workspace::Workspace reopened;const auto reopened_id=workspace::open_symbol_document(reopened,root/"example.symz");
    check(workspace::edited_symbol_definition(reopened,reopened_id).serialized()==saved.serialized(),"Reopen changed definition");
    check(reopened_id==id&&reopened.open_part(reopened_id)->session.document().body_history==live.open_part(id)->session.document().body_history,"Reopen changed document or Body identity");
    check(workspace::open_symbol_document(reopened,root/"example.symz")==reopened_id&&reopened.size()==1,"Open duplicated tab");
    auto multivariant=symbols::projection_method();multivariant.save(root/"variants.symz");
    const auto multi=workspace::open_symbol_document(live,root/"variants.symz");
    workspace::save_symbol_document(live,multi,root/"variants.symz");
    check(symbols::Definition::load(root/"variants.symz").serialized()==multivariant.serialized(),"Editing carrier lost variants, identities or pens");
    auto label=sketcher::Sketch::create_text();label.value="Ra 3.2";label.modeling_geometry=false;
    sketcher::rebuild_text_contours(label,true);multivariant.sketches.front().texts.push_back(label);
    multivariant.fields["Specification"]={multivariant.sketches.front().id,label.id,{"Ra 3.2","Ra 6.3"},true};
    multivariant.variants.at(multivariant.default_variant).text_values["Specification"]="Ra 3.2";
    multivariant.save(root/"fields.symz");const auto fields=workspace::open_symbol_document(live,root/"fields.symz");
    workspace::mutate_document_sketch(live,fields,multivariant.sketches.front().id,[](auto& s){s.texts.clear();});
    check(workspace::edited_symbol_definition(live,fields).fields.empty(),"Deleting text retained dangling editable field");
    check(workspace::step_document_history(live,fields,workspace::HistoryDirection::Undo),"Field deletion Undo failed");
    check(workspace::edited_symbol_definition(live,fields).serialized()==multivariant.serialized(),"Undo failed to restore field metadata");
    auto revised=multivariant;
    revised.variant_source.clear();revised.variants["Custom"]=revised.variants.at(revised.default_variant);
    revised.variants["Custom"].hidden_texts={"Specification"};revised.default_variant="Custom";
    revised.fields.at("Specification").choices.push_back("Ra 12.5");
    check(workspace::store_symbol_definition(live,fields,revised),"Symbol metadata edit was not committed");
    check(!workspace::store_symbol_definition(live,fields,revised),"Unchanged Symbol metadata created a transaction");
    check(workspace::step_document_history(live,fields,workspace::HistoryDirection::Undo),"Symbol metadata Undo failed");
    check(workspace::edited_symbol_definition(live,fields).serialized()==multivariant.serialized(),"Symbol metadata Undo lost original definition");
    check(workspace::step_document_history(live,fields,workspace::HistoryDirection::Redo),"Symbol metadata Redo failed");
    check(workspace::edited_symbol_definition(live,fields).serialized()==revised.serialized(),"Symbol metadata Redo lost choices or variants");
    workspace::save_symbol_document(live,fields,root/"fields.symz");
    check(symbols::Definition::load(root/"fields.symz").serialized()==revised.serialized(),"Edited Symbol metadata did not persist");
    auto* field_state=live.open_part(fields);auto with_body_column=field_state->session.document();
    auto family=document::parse_family_table(with_body_column.family_table);
    family.columns.push_back("Body presence");family.bindings["Body presence"]={"body",with_body_column.body_history.bodies().front().scope.id,{}};
    for(auto& row:family.instances)row.values["Body presence"]="yes";
    family.instances.front().shared_name=false;family.instances.front().labels={{"cs","Moje varianta"},{"en","My variant"}};
    with_body_column.family_table=document::serialize_family_table(family);
    field_state->session.commit(std::move(with_body_column),field_state->session.calculated_boundaries());
    auto text_edit=workspace::edited_symbol_definition(live,fields);text_edit.fields.at("Specification").choices.push_back("Ra 25");
    check(workspace::store_symbol_definition(live,fields,text_edit),"Text choices were not updated");
    check(document::parse_family_table(field_state->session.document().family_table)==family,"Text choices erased ordinary Family columns or row labels");
    std::filesystem::remove_all(root);std::cout<<"Symbol document creation, edit, save, copy, reopen, variants and Undo/Redo passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
